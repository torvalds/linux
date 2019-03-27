//===-- NativeThreadNetBSD.cpp -------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeThreadNetBSD.h"
#include "NativeRegisterContextNetBSD.h"

#include "NativeProcessNetBSD.h"

#include "Plugins/Process/POSIX/CrashReason.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/State.h"

#include <sstream>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_netbsd;

NativeThreadNetBSD::NativeThreadNetBSD(NativeProcessNetBSD &process,
                                       lldb::tid_t tid)
    : NativeThreadProtocol(process, tid), m_state(StateType::eStateInvalid),
      m_stop_info(), m_reg_context_up(
NativeRegisterContextNetBSD::CreateHostNativeRegisterContextNetBSD(process.GetArchitecture(), *this)
), m_stop_description() {}

void NativeThreadNetBSD::SetStoppedBySignal(uint32_t signo,
                                            const siginfo_t *info) {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_THREAD));
  LLDB_LOG(log, "tid = {0} in called with signal {1}", GetID(), signo);

  SetStopped();

  m_stop_info.reason = StopReason::eStopReasonSignal;
  m_stop_info.details.signal.signo = signo;

  m_stop_description.clear();
  if (info) {
    switch (signo) {
    case SIGSEGV:
    case SIGBUS:
    case SIGFPE:
    case SIGILL:
      const auto reason = GetCrashReason(*info);
      m_stop_description = GetCrashReasonString(reason, *info);
      break;
    }
  }
}

void NativeThreadNetBSD::SetStoppedByBreakpoint() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonBreakpoint;
  m_stop_info.details.signal.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedByTrace() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonTrace;
  m_stop_info.details.signal.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedByExec() {
  SetStopped();
  m_stop_info.reason = StopReason::eStopReasonExec;
  m_stop_info.details.signal.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStoppedByWatchpoint(uint32_t wp_index) {
  SetStopped();

  lldbassert(wp_index != LLDB_INVALID_INDEX32 && "wp_index cannot be invalid");

  std::ostringstream ostr;
  ostr << GetRegisterContext().GetWatchpointAddress(wp_index) << " ";
  ostr << wp_index;

  ostr << " " << GetRegisterContext().GetWatchpointHitAddress(wp_index);

  m_stop_description = ostr.str();

  m_stop_info.reason = StopReason::eStopReasonWatchpoint;
  m_stop_info.details.signal.signo = SIGTRAP;
}

void NativeThreadNetBSD::SetStopped() {
  const StateType new_state = StateType::eStateStopped;
  m_state = new_state;
  m_stop_description.clear();
}

void NativeThreadNetBSD::SetRunning() {
  m_state = StateType::eStateRunning;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

void NativeThreadNetBSD::SetStepping() {
  m_state = StateType::eStateStepping;
  m_stop_info.reason = StopReason::eStopReasonNone;
}

std::string NativeThreadNetBSD::GetName() { return std::string(""); }

lldb::StateType NativeThreadNetBSD::GetState() { return m_state; }

bool NativeThreadNetBSD::GetStopReason(ThreadStopInfo &stop_info,
                                       std::string &description) {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_THREAD));

  description.clear();

  switch (m_state) {
  case eStateStopped:
  case eStateCrashed:
  case eStateExited:
  case eStateSuspended:
  case eStateUnloaded:
    stop_info = m_stop_info;
    description = m_stop_description;

    return true;

  case eStateInvalid:
  case eStateConnected:
  case eStateAttaching:
  case eStateLaunching:
  case eStateRunning:
  case eStateStepping:
  case eStateDetached:
    LLDB_LOG(log, "tid = {0} in state {1} cannot answer stop reason", GetID(),
             StateAsCString(m_state));
    return false;
  }
  llvm_unreachable("unhandled StateType!");
}

NativeRegisterContext& NativeThreadNetBSD::GetRegisterContext() {
  assert(m_reg_context_up);
return  *m_reg_context_up;
}

Status NativeThreadNetBSD::SetWatchpoint(lldb::addr_t addr, size_t size,
                                         uint32_t watch_flags, bool hardware) {
  if (!hardware)
    return Status("not implemented");
  if (m_state == eStateLaunching)
    return Status();
  Status error = RemoveWatchpoint(addr);
  if (error.Fail())
    return error;
  uint32_t wp_index = GetRegisterContext().SetHardwareWatchpoint(addr, size, watch_flags);
  if (wp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware watchpoint failed.");
  m_watchpoint_index_map.insert({addr, wp_index});
  return Status();
}

Status NativeThreadNetBSD::RemoveWatchpoint(lldb::addr_t addr) {
  auto wp = m_watchpoint_index_map.find(addr);
  if (wp == m_watchpoint_index_map.end())
    return Status();
  uint32_t wp_index = wp->second;
  m_watchpoint_index_map.erase(wp);
  if (GetRegisterContext().ClearHardwareWatchpoint(wp_index))
    return Status();
  return Status("Clearing hardware watchpoint failed.");
}

Status NativeThreadNetBSD::SetHardwareBreakpoint(lldb::addr_t addr,
                                                 size_t size) {
  if (m_state == eStateLaunching)
    return Status();

  Status error = RemoveHardwareBreakpoint(addr);
  if (error.Fail())
    return error;

  uint32_t bp_index = GetRegisterContext().SetHardwareBreakpoint(addr, size);

  if (bp_index == LLDB_INVALID_INDEX32)
    return Status("Setting hardware breakpoint failed.");

  m_hw_break_index_map.insert({addr, bp_index});
  return Status();
}

Status NativeThreadNetBSD::RemoveHardwareBreakpoint(lldb::addr_t addr) {
  auto bp = m_hw_break_index_map.find(addr);
  if (bp == m_hw_break_index_map.end())
    return Status();

  uint32_t bp_index = bp->second;
  if (GetRegisterContext().ClearHardwareBreakpoint(bp_index)) {
    m_hw_break_index_map.erase(bp);
    return Status();
  }

  return Status("Clearing hardware breakpoint failed.");
}
