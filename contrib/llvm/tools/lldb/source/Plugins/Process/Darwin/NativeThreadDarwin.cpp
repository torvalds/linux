//===-- NativeThreadDarwin.cpp -------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeThreadDarwin.h"

// C includes
#include <libproc.h>

// LLDB includes
#include "lldb/Utility/Stream.h"

#include "NativeProcessDarwin.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_darwin;

uint64_t NativeThreadDarwin::GetGloballyUniqueThreadIDForMachPortID(
    ::thread_t mach_port_id) {
  thread_identifier_info_data_t tident;
  mach_msg_type_number_t tident_count = THREAD_IDENTIFIER_INFO_COUNT;

  auto mach_err = ::thread_info(mach_port_id, THREAD_IDENTIFIER_INFO,
                                (thread_info_t)&tident, &tident_count);
  if (mach_err != KERN_SUCCESS) {
    // When we fail to get thread info for the supposed port, assume it is
    // really a globally unique thread id already, or return the best thing we
    // can, which is the thread port.
    return mach_port_id;
  }
  return tident.thread_id;
}

NativeThreadDarwin::NativeThreadDarwin(NativeProcessDarwin *process,
                                       bool is_64_bit,
                                       lldb::tid_t unique_thread_id,
                                       ::thread_t mach_thread_port)
    : NativeThreadProtocol(process, unique_thread_id),
      m_mach_thread_port(mach_thread_port), m_basic_info(),
      m_proc_threadinfo() {}

bool NativeThreadDarwin::GetIdentifierInfo() {
  // Don't try to get the thread info once and cache it for the life of the
  // thread.  It changes over time, for instance if the thread name changes,
  // then the thread_handle also changes...  So you have to refetch it every
  // time.
  mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
  kern_return_t kret = ::thread_info(m_mach_thread_port, THREAD_IDENTIFIER_INFO,
                                     (thread_info_t)&m_ident_info, &count);
  return kret == KERN_SUCCESS;

  return false;
}

std::string NativeThreadDarwin::GetName() {
  std::string name;

  if (GetIdentifierInfo()) {
    auto process_sp = GetProcess();
    if (!process_sp) {
      name = "<unavailable>";
      return name;
    }

    int len = ::proc_pidinfo(process_sp->GetID(), PROC_PIDTHREADINFO,
                             m_ident_info.thread_handle, &m_proc_threadinfo,
                             sizeof(m_proc_threadinfo));

    if (len && m_proc_threadinfo.pth_name[0])
      name = m_proc_threadinfo.pth_name;
  }
  return name;
}

lldb::StateType NativeThreadDarwin::GetState() {
  // TODO implement
  return eStateInvalid;
}

bool NativeThreadDarwin::GetStopReason(ThreadStopInfo &stop_info,
                                       std::string &description) {
  // TODO implement
  return false;
}

NativeRegisterContextSP NativeThreadDarwin::GetRegisterContext() {
  // TODO implement
  return NativeRegisterContextSP();
}

Status NativeThreadDarwin::SetWatchpoint(lldb::addr_t addr, size_t size,
                                         uint32_t watch_flags, bool hardware) {
  Status error;
  error.SetErrorString("not yet implemented");
  return error;
}

Status NativeThreadDarwin::RemoveWatchpoint(lldb::addr_t addr) {
  Status error;
  error.SetErrorString("not yet implemented");
  return error;
}

void NativeThreadDarwin::Dump(Stream &stream) const {
// This is what we really want once we have the thread class wired up.
#if 0
    DNBLogThreaded("[%3u] #%3u tid: 0x%8.8" PRIx64 ", pc: 0x%16.16" PRIx64 ", sp: 0x%16.16" PRIx64 ", user: %d.%6.6d, system: %d.%6.6d, cpu: %2d, policy: %2d, run_state: %2d (%s), flags: %2d, suspend_count: %2d (current %2d), sleep_time: %d",
                   index,
                   m_seq_id,
                   m_unique_id,
                   GetPC(INVALID_NUB_ADDRESS),
                   GetSP(INVALID_NUB_ADDRESS),
                   m_basic_info.user_time.seconds,      m_basic_info.user_time.microseconds,
                   m_basic_info.system_time.seconds,    m_basic_info.system_time.microseconds,
                   m_basic_info.cpu_usage,
                   m_basic_info.policy,
                   m_basic_info.run_state,
                   thread_run_state,
                   m_basic_info.flags,
                   m_basic_info.suspend_count, m_suspend_count,
                   m_basic_info.sleep_time);

#else
  // Here's all we have right now.
  stream.Printf("tid: 0x%8.8" PRIx64 ", thread port: 0x%4.4x", GetID(),
                m_mach_thread_port);
#endif
}

bool NativeThreadDarwin::NotifyException(MachException::Data &exc) {
// TODO implement this.
#if 0
    // Allow the arch specific protocol to process (MachException::Data &)exc
    // first before possible reassignment of m_stop_exception with exc. See
    // also MachThread::GetStopException().
    bool handled = m_arch_ap->NotifyException(exc);

    if (m_stop_exception.IsValid())
    {
        // We may have more than one exception for a thread, but we need to
        // only remember the one that we will say is the reason we stopped. We
        // may have been single stepping and also gotten a signal exception, so
        // just remember the most pertinent one.
        if (m_stop_exception.IsBreakpoint())
            m_stop_exception = exc;
    }
    else
    {
        m_stop_exception = exc;
    }

    return handled;
#else
  // Pretend we handled it.
  return true;
#endif
}

bool NativeThreadDarwin::ShouldStop(bool &step_more) const {
// TODO: implement this
#if 0
    // See if this thread is at a breakpoint?
    DNBBreakpoint *bp = CurrentBreakpoint();

    if (bp)
    {
        // This thread is sitting at a breakpoint, ask the breakpoint if we
        // should be stopping here.
        return true;
    }
    else
    {
        if (m_arch_ap->StepNotComplete())
        {
            step_more = true;
            return false;
        }
        // The thread state is used to let us know what the thread was trying
        // to do. MachThread::ThreadWillResume() will set the thread state to
        // various values depending if the thread was the current thread and if
        // it was to be single stepped, or resumed.
        if (GetState() == eStateRunning)
        {
            // If our state is running, then we should continue as we are in
            // the process of stepping over a breakpoint.
            return false;
        }
        else
        {
            // Stop if we have any kind of valid exception for this thread.
            if (GetStopException().IsValid())
                return true;
        }
    }
    return false;
#else
  return false;
#endif
}

void NativeThreadDarwin::ThreadDidStop() {
// TODO implement this.
#if 0
    // This thread has existed prior to resuming under debug nub control, and
    // has just been stopped. Do any cleanup that needs to be done after
    // running.

    // The thread state and breakpoint will still have the same values as they
    // had prior to resuming the thread, so it makes it easy to check if we
    // were trying to step a thread, or we tried to resume while being at a
    // breakpoint.

    // When this method gets called, the process state is still in the state it
    // was in while running so we can act accordingly.
    m_arch_ap->ThreadDidStop();


    // We may have suspended this thread so the primary thread could step
    // without worrying about race conditions, so lets restore our suspend
    // count.
    RestoreSuspendCountAfterStop();

    // Update the basic information for a thread
    MachThread::GetBasicInfo(m_mach_port_number, &m_basic_info);

    if (m_basic_info.suspend_count > 0)
        SetState(eStateSuspended);
    else
        SetState(eStateStopped);
#endif
}

bool NativeThreadDarwin::MachPortNumberIsValid(::thread_t thread) {
  return thread != (::thread_t)(0);
}

const struct thread_basic_info *NativeThreadDarwin::GetBasicInfo() const {
  if (GetBasicInfo(m_mach_thread_port, &m_basic_info))
    return &m_basic_info;
  return NULL;
}

bool NativeThreadDarwin::GetBasicInfo(::thread_t thread,
                                      struct thread_basic_info *basicInfoPtr) {
  if (MachPortNumberIsValid(thread)) {
    unsigned int info_count = THREAD_BASIC_INFO_COUNT;
    kern_return_t err = ::thread_info(thread, THREAD_BASIC_INFO,
                                      (thread_info_t)basicInfoPtr, &info_count);
    if (err == KERN_SUCCESS)
      return true;
  }
  ::memset(basicInfoPtr, 0, sizeof(struct thread_basic_info));
  return false;
}

bool NativeThreadDarwin::IsUserReady() const {
  if (m_basic_info.run_state == 0)
    GetBasicInfo();

  switch (m_basic_info.run_state) {
  default:
  case TH_STATE_UNINTERRUPTIBLE:
    break;

  case TH_STATE_RUNNING:
  case TH_STATE_STOPPED:
  case TH_STATE_WAITING:
  case TH_STATE_HALTED:
    return true;
  }
  return false;
}

NativeProcessDarwinSP NativeThreadDarwin::GetNativeProcessDarwinSP() {
  return std::static_pointer_cast<NativeProcessDarwin>(GetProcess());
}
