//===-- State.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_STATE_H
#define LLDB_UTILITY_STATE_H

#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatProviders.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

namespace lldb_private {

//------------------------------------------------------------------
/// Converts a StateType to a C string.
///
/// @param[in] state
///     The StateType object to convert.
///
/// @return
///     A NULL terminated C string that describes \a state. The
///     returned string comes from constant string buffers and does
///     not need to be freed.
//------------------------------------------------------------------
const char *StateAsCString(lldb::StateType state);

//------------------------------------------------------------------
/// Check if a state represents a state where the process or thread
/// is running.
///
/// @param[in] state
///     The StateType enumeration value
///
/// @return
///     \b true if the state represents a process or thread state
///     where the process or thread is running, \b false otherwise.
//------------------------------------------------------------------
bool StateIsRunningState(lldb::StateType state);

//------------------------------------------------------------------
/// Check if a state represents a state where the process or thread
/// is stopped. Stopped can mean stopped when the process is still
/// around, or stopped when the process has exited or doesn't exist
/// yet. The \a must_exist argument tells us which of these cases is
/// desired.
///
/// @param[in] state
///     The StateType enumeration value
///
/// @param[in] must_exist
///     A boolean that indicates the thread must also be alive
///     so states like unloaded or exited won't return true.
///
/// @return
///     \b true if the state represents a process or thread state
///     where the process or thread is stopped. If \a must_exist is
///     \b true, then the process can't be exited or unloaded,
///     otherwise exited and unloaded or other states where the
///     process no longer exists are considered to be stopped.
//------------------------------------------------------------------
bool StateIsStoppedState(lldb::StateType state, bool must_exist);

const char *GetPermissionsAsCString(uint32_t permissions);

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb::StateType> {
  static void format(const lldb::StateType &state, raw_ostream &Stream,
                     StringRef Style) {
    Stream << lldb_private::StateAsCString(state);
  }
};
} // namespace llvm

#endif // LLDB_UTILITY_STATE_H
