// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// pa3ds: what a file read or open charges the game, pinned so a movie replays alike on any host.
//
// Azahar charges an uncached read, and an open, the modelled delay less the time the host took, so
// the frame a load ends on moves with the host and a replay can drift from its movie. With
// deterministic async operations on, every other input to the game is fixed by the movie.
//
// PA3DS_TIMING_OUT names a file each such charge is written to, "<ticks> <kind> <nanoseconds>".
// PA3DS_TIMING_IN names such a file to charge from instead, by the emulated tick of the call. A
// call the file has no charge for is charged as Azahar would, and logged if it comes before the
// file's last charge: the replay is no longer the run the file was taken from.
//
// The same file marks the first read of the buttons ("P"), and when the game turns the
// accelerometer on ("A") and off ("a"), each with a charge of 0 that nothing is charged from.
// Where the accelerometer's readings fall among the buttons' in a movie is the one place a
// movie says when something happened in the game.

#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"

namespace Service::FS::Pa3dsTiming {

struct State {
    std::map<std::pair<s64, char>, s64> pinned;
    bool pinning = false;
    std::ofstream out;
    std::mutex lock;

    State() {
        if (const char* in = std::getenv("PA3DS_TIMING_IN")) {
            std::ifstream file(in);
            s64 ticks, charge;
            char at;
            while (file >> ticks >> at >> charge) {
                pinned[{ticks, at}] = charge;
            }
            pinning = true;
            LOG_INFO(Service_FS, "pa3ds timing: {} charges pinned from {}", pinned.size(), in);
        }
        if (const char* path = std::getenv("PA3DS_TIMING_OUT")) {
            out.open(path);
        }
    }

    void Write(s64 ticks, char kind, s64 charged) {
        if (out.is_open()) {
            // Flushed a line at a time: the rig ends the emulator by killing it.
            out << ticks << ' ' << kind << ' ' << charged << std::endl;
        }
    }
};

inline State& Get() {
    static State state;
    return state;
}

inline s64 Charge(char kind, s64 computed) {
    if (!Settings::values.deterministic_async_operations) {
        return computed;
    }
    State& state = Get();
    const s64 ticks = Core::System::GetInstance().CoreTiming().GetTicks();
    std::scoped_lock guard{state.lock};
    s64 charged = computed;
    if (state.pinning) {
        if (const auto found = state.pinned.find({ticks, kind}); found != state.pinned.end()) {
            charged = found->second;
        } else if (!state.pinned.empty() && ticks <= state.pinned.rbegin()->first.first) {
            // Past the last pin is only past where the pinned run was stopped.
            LOG_WARNING(Service_FS, "pa3ds timing: no pinned charge at tick {} ({})", ticks, kind);
        }
    }
    state.Write(ticks, kind, charged);
    return charged;
}

inline void MarkAt(s64 ticks, char kind) {
    State& state = Get();
    std::scoped_lock guard{state.lock};
    state.Write(ticks, kind, 0);
}

inline void Mark(char kind) {
    MarkAt(Core::System::GetInstance().CoreTiming().GetTicks(), kind);
}

} // namespace Service::FS::Pa3dsTiming
