//===-- NativeThreadListDarwin.cpp ------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/19/07.
//
//===----------------------------------------------------------------------===//

#include "NativeThreadListDarwin.h"

// C includes
#include <inttypes.h>
#include <mach/vm_map.h>
#include <sys/sysctl.h>

// LLDB includes
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"

#include "NativeProcessDarwin.h"
#include "NativeThreadDarwin.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_darwin;

NativeThreadListDarwin::NativeThreadListDarwin()
    : m_threads(), m_threads_mutex(), m_is_64_bit(false) {}

NativeThreadListDarwin::~NativeThreadListDarwin() {}

// These methods will be accessed directly from NativeThreadDarwin
#if 0
nub_state_t
NativeThreadListDarwin::GetState(nub_thread_t tid)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetState();
    return eStateInvalid;
}

const char *
NativeThreadListDarwin::GetName (nub_thread_t tid)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetName();
    return NULL;
}
#endif

// TODO: figure out if we need to add this to NativeThreadDarwin yet.
#if 0
ThreadInfo::QoS
NativeThreadListDarwin::GetRequestedQoS (nub_thread_t tid, nub_addr_t tsd, uint64_t dti_qos_class_index)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetRequestedQoS(tsd, dti_qos_class_index);
    return ThreadInfo::QoS();
}

nub_addr_t
NativeThreadListDarwin::GetPThreadT (nub_thread_t tid)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetPThreadT();
    return INVALID_NUB_ADDRESS;
}

nub_addr_t
NativeThreadListDarwin::GetDispatchQueueT (nub_thread_t tid)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetDispatchQueueT();
    return INVALID_NUB_ADDRESS;
}

nub_addr_t
NativeThreadListDarwin::GetTSDAddressForThread (nub_thread_t tid, uint64_t plo_pthread_tsd_base_address_offset, uint64_t plo_pthread_tsd_base_offset, uint64_t plo_pthread_tsd_entry_size)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetTSDAddressForThread(plo_pthread_tsd_base_address_offset, plo_pthread_tsd_base_offset, plo_pthread_tsd_entry_size);
    return INVALID_NUB_ADDRESS;
}
#endif

// TODO implement these
#if 0
nub_thread_t
NativeThreadListDarwin::SetCurrentThread(nub_thread_t tid)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
    {
        m_current_thread = thread_sp;
        return tid;
    }
    return INVALID_NUB_THREAD;
}


bool
NativeThreadListDarwin::GetThreadStoppedReason(nub_thread_t tid, struct DNBThreadStopInfo *stop_info) const
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetStopException().GetStopInfo(stop_info);
    return false;
}

bool
NativeThreadListDarwin::GetIdentifierInfo (nub_thread_t tid, thread_identifier_info_data_t *ident_info)
{
    thread_t mach_port_number = GetMachPortNumberByThreadID (tid);

    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    return ::thread_info (mach_port_number, THREAD_IDENTIFIER_INFO, (thread_info_t)ident_info, &count) == KERN_SUCCESS;
}

void
NativeThreadListDarwin::DumpThreadStoppedReason (nub_thread_t tid) const
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        thread_sp->GetStopException().DumpStopReason();
}

const char *
NativeThreadListDarwin::GetThreadInfo (nub_thread_t tid) const
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetBasicInfoAsString();
    return NULL;
}

#endif

NativeThreadDarwinSP
NativeThreadListDarwin::GetThreadByID(lldb::tid_t tid) const {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  for (auto thread_sp : m_threads) {
    if (thread_sp && (thread_sp->GetID() == tid))
      return thread_sp;
  }
  return NativeThreadDarwinSP();
}

NativeThreadDarwinSP NativeThreadListDarwin::GetThreadByMachPortNumber(
    ::thread_t mach_port_number) const {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  for (auto thread_sp : m_threads) {
    if (thread_sp && (thread_sp->GetMachPortNumber() == mach_port_number))
      return thread_sp;
  }
  return NativeThreadDarwinSP();
}

lldb::tid_t NativeThreadListDarwin::GetThreadIDByMachPortNumber(
    ::thread_t mach_port_number) const {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  for (auto thread_sp : m_threads) {
    if (thread_sp && (thread_sp->GetMachPortNumber() == mach_port_number))
      return thread_sp->GetID();
  }
  return LLDB_INVALID_THREAD_ID;
}

// TODO implement
#if 0
thread_t
NativeThreadListDarwin::GetMachPortNumberByThreadID (nub_thread_t globally_unique_id) const
{
    PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
    MachThreadSP thread_sp;
    const size_t num_threads = m_threads.size();
    for (size_t idx = 0; idx < num_threads; ++idx)
    {
        if (m_threads[idx]->ThreadID() == globally_unique_id)
        {
            return m_threads[idx]->MachPortNumber();
        }
    }
    return 0;
}

bool
NativeThreadListDarwin::GetRegisterValue (nub_thread_t tid, uint32_t set, uint32_t reg, DNBRegisterValue *reg_value ) const
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetRegisterValue(set, reg, reg_value);

    return false;
}

bool
NativeThreadListDarwin::SetRegisterValue (nub_thread_t tid, uint32_t set, uint32_t reg, const DNBRegisterValue *reg_value ) const
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->SetRegisterValue(set, reg, reg_value);

    return false;
}

nub_size_t
NativeThreadListDarwin::GetRegisterContext (nub_thread_t tid, void *buf, size_t buf_len)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->GetRegisterContext (buf, buf_len);
    return 0;
}

nub_size_t
NativeThreadListDarwin::SetRegisterContext (nub_thread_t tid, const void *buf, size_t buf_len)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->SetRegisterContext (buf, buf_len);
    return 0;
}

uint32_t
NativeThreadListDarwin::SaveRegisterState (nub_thread_t tid)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->SaveRegisterState ();
    return 0;
}

bool
NativeThreadListDarwin::RestoreRegisterState (nub_thread_t tid, uint32_t save_id)
{
    MachThreadSP thread_sp (GetThreadByID (tid));
    if (thread_sp)
        return thread_sp->RestoreRegisterState (save_id);
    return 0;
}
#endif

size_t NativeThreadListDarwin::GetNumberOfThreads() const {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  return static_cast<size_t>(m_threads.size());
}

// TODO implement
#if 0
nub_thread_t
NativeThreadListDarwin::ThreadIDAtIndex (nub_size_t idx) const
{
    PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
    if (idx < m_threads.size())
        return m_threads[idx]->ThreadID();
    return INVALID_NUB_THREAD;
}

nub_thread_t
NativeThreadListDarwin::CurrentThreadID ( )
{
    MachThreadSP thread_sp;
    CurrentThread(thread_sp);
    if (thread_sp.get())
        return thread_sp->ThreadID();
    return INVALID_NUB_THREAD;
}

#endif

bool NativeThreadListDarwin::NotifyException(MachException::Data &exc) {
  auto thread_sp = GetThreadByMachPortNumber(exc.thread_port);
  if (thread_sp) {
    thread_sp->NotifyException(exc);
    return true;
  }
  return false;
}

void NativeThreadListDarwin::Clear() {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  m_threads.clear();
}

uint32_t NativeThreadListDarwin::UpdateThreadList(NativeProcessDarwin &process,
                                                  bool update,
                                                  collection *new_threads) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_THREAD));

  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  if (log)
    log->Printf("NativeThreadListDarwin::%s() (pid = %" PRIu64 ", update = "
                "%u) process stop count = %u",
                __FUNCTION__, process.GetID(), update, process.GetStopID());

  if (process.GetStopID() == 0) {
    // On our first stop, we'll record details like 32/64 bitness and select
    // the proper architecture implementation.
    //
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)process.GetID()};

    struct kinfo_proc processInfo;
    size_t bufsize = sizeof(processInfo);
    if ((sysctl(mib, (unsigned)(sizeof(mib) / sizeof(int)), &processInfo,
                &bufsize, NULL, 0) == 0) &&
        (bufsize > 0)) {
      if (processInfo.kp_proc.p_flag & P_LP64)
        m_is_64_bit = true;
    }

// TODO implement architecture selection and abstraction.
#if 0
#if defined(__i386__) || defined(__x86_64__)
        if (m_is_64_bit)
            DNBArchProtocol::SetArchitecture(CPU_TYPE_X86_64);
        else
            DNBArchProtocol::SetArchitecture(CPU_TYPE_I386);
#elif defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
        if (m_is_64_bit)
            DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM64);
        else
            DNBArchProtocol::SetArchitecture(CPU_TYPE_ARM);
#endif
#endif
  }

  if (m_threads.empty() || update) {
    thread_array_t thread_list = nullptr;
    mach_msg_type_number_t thread_list_count = 0;
    task_t task = process.GetTask();

    Status error;
    auto mach_err = ::task_threads(task, &thread_list, &thread_list_count);
    error.SetError(mach_err, eErrorTypeMachKernel);
    if (error.Fail()) {
      if (log)
        log->Printf("::task_threads(task = 0x%4.4x, thread_list => %p, "
                    "thread_list_count => %u) failed: %u (%s)",
                    task, thread_list, thread_list_count, error.GetError(),
                    error.AsCString());
      return 0;
    }

    if (thread_list_count > 0) {
      collection currThreads;
      size_t idx;
      // Iterator through the current thread list and see which threads we
      // already have in our list (keep them), which ones we don't (add them),
      // and which ones are not around anymore (remove them).
      for (idx = 0; idx < thread_list_count; ++idx) {
        // Get the Mach thread port.
        const ::thread_t mach_port_num = thread_list[idx];

        // Get the unique thread id for the mach port number.
        uint64_t unique_thread_id =
            NativeThreadDarwin::GetGloballyUniqueThreadIDForMachPortID(
                mach_port_num);

        // Retrieve the thread if it exists.
        auto thread_sp = GetThreadByID(unique_thread_id);
        if (thread_sp) {
          // We are already tracking it. Keep the existing native thread
          // instance.
          currThreads.push_back(thread_sp);
        } else {
          // We don't have a native thread instance for this thread. Create it
          // now.
          thread_sp.reset(new NativeThreadDarwin(
              &process, m_is_64_bit, unique_thread_id, mach_port_num));

          // Add the new thread regardless of its is user ready state. Make
          // sure the thread is ready to be displayed and shown to users before
          // we add this thread to our list...
          if (thread_sp->IsUserReady()) {
            if (new_threads)
              new_threads->push_back(thread_sp);

            currThreads.push_back(thread_sp);
          }
        }
      }

      m_threads.swap(currThreads);
      m_current_thread.reset();

      // Free the vm memory given to us by ::task_threads()
      vm_size_t thread_list_size =
          (vm_size_t)(thread_list_count * sizeof(::thread_t));
      ::vm_deallocate(::mach_task_self(), (vm_address_t)thread_list,
                      thread_list_size);
    }
  }
  return static_cast<uint32_t>(m_threads.size());
}

// TODO implement
#if 0

void
NativeThreadListDarwin::CurrentThread (MachThreadSP& thread_sp)
{
    // locker will keep a mutex locked until it goes out of scope
    PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
    if (m_current_thread.get() == NULL)
    {
        // Figure out which thread is going to be our current thread. This is
        // currently done by finding the first thread in the list that has a
        // valid exception.
        const size_t num_threads = m_threads.size();
        for (uint32_t idx = 0; idx < num_threads; ++idx)
        {
            if (m_threads[idx]->GetStopException().IsValid())
            {
                m_current_thread = m_threads[idx];
                break;
            }
        }
    }
    thread_sp = m_current_thread;
}

#endif

void NativeThreadListDarwin::Dump(Stream &stream) const {
  bool first = true;

  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  for (auto thread_sp : m_threads) {
    if (thread_sp) {
      // Handle newlines between thread entries.
      if (first)
        first = false;
      else
        stream.PutChar('\n');
      thread_sp->Dump(stream);
    }
  }
}

void NativeThreadListDarwin::ProcessWillResume(
    NativeProcessDarwin &process, const ResumeActionList &thread_actions) {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);

  // Update our thread list, because sometimes libdispatch or the kernel will
  // spawn threads while a task is suspended.
  NativeThreadListDarwin::collection new_threads;

// TODO implement this.
#if 0
    // First figure out if we were planning on running only one thread, and if
    // so, force that thread to resume.
    bool run_one_thread;
    thread_t solo_thread = THREAD_NULL;
    if ((thread_actions.GetSize() > 0) &&
        (thread_actions.NumActionsWithState(eStateStepping) +
         thread_actions.NumActionsWithState (eStateRunning) == 1))
    {
        run_one_thread = true;
        const DNBThreadResumeAction *action_ptr = thread_actions.GetFirst();
        size_t num_actions = thread_actions.GetSize();
        for (size_t i = 0; i < num_actions; i++, action_ptr++)
        {
            if (action_ptr->state == eStateStepping || action_ptr->state == eStateRunning)
            {
                solo_thread = action_ptr->tid;
                break;
            }
        }
    }
    else
        run_one_thread = false;
#endif

  UpdateThreadList(process, true, &new_threads);

#if 0
    DNBThreadResumeAction resume_new_threads = { -1U, eStateRunning, 0, INVALID_NUB_ADDRESS };
    // If we are planning to run only one thread, any new threads should be
    // suspended.
    if (run_one_thread)
        resume_new_threads.state = eStateSuspended;

    const size_t num_new_threads = new_threads.size();
    const size_t num_threads = m_threads.size();
    for (uint32_t idx = 0; idx < num_threads; ++idx)
    {
        MachThread *thread = m_threads[idx].get();
        bool handled = false;
        for (uint32_t new_idx = 0; new_idx < num_new_threads; ++new_idx)
        {
            if (thread == new_threads[new_idx].get())
            {
                thread->ThreadWillResume(&resume_new_threads);
                handled = true;
                break;
            }
        }

        if (!handled)
        {
            const DNBThreadResumeAction *thread_action = thread_actions.GetActionForThread (thread->ThreadID(), true);
            // There must always be a thread action for every thread.
            assert (thread_action);
            bool others_stopped = false;
            if (solo_thread == thread->ThreadID())
                others_stopped = true;
            thread->ThreadWillResume (thread_action, others_stopped);
        }
    }
    
    if (new_threads.size())
    {
        for (uint32_t idx = 0; idx < num_new_threads; ++idx)
        {
            DNBLogThreadedIf (LOG_THREAD, "NativeThreadListDarwin::ProcessWillResume (pid = %4.4x) stop-id=%u, resuming newly discovered thread: 0x%8.8" PRIx64 ", thread-is-user-ready=%i)",
                              process->ProcessID(), 
                              process->StopCount(), 
                              new_threads[idx]->ThreadID(),
                              new_threads[idx]->IsUserReady());
        }
    }
#endif
}

uint32_t NativeThreadListDarwin::ProcessDidStop(NativeProcessDarwin &process) {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);

  // Update our thread list.
  UpdateThreadList(process, true);

  for (auto thread_sp : m_threads) {
    if (thread_sp)
      thread_sp->ThreadDidStop();
  }
  return (uint32_t)m_threads.size();
}

//----------------------------------------------------------------------
// Check each thread in our thread list to see if we should notify our client
// of the current halt in execution.
//
// Breakpoints can have callback functions associated with them than can return
// true to stop, or false to continue executing the inferior.
//
// RETURNS
//    true if we should stop and notify our clients
//    false if we should resume our child process and skip notification
//----------------------------------------------------------------------
bool NativeThreadListDarwin::ShouldStop(bool &step_more) {
  std::lock_guard<std::recursive_mutex> locker(m_threads_mutex);
  for (auto thread_sp : m_threads) {
    if (thread_sp && thread_sp->ShouldStop(step_more))
      return true;
  }
  return false;
}

// Implement.
#if 0

void
NativeThreadListDarwin::NotifyBreakpointChanged (const DNBBreakpoint *bp)
{
    PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
    const size_t num_threads = m_threads.size();
    for (uint32_t idx = 0; idx < num_threads; ++idx)
    {
        m_threads[idx]->NotifyBreakpointChanged(bp);
    }
}


uint32_t
NativeThreadListDarwin::EnableHardwareBreakpoint (const DNBBreakpoint* bp) const
{
    if (bp != NULL)
    {
        const size_t num_threads = m_threads.size();
        for (uint32_t idx = 0; idx < num_threads; ++idx)
            m_threads[idx]->EnableHardwareBreakpoint(bp);
    }
    return INVALID_NUB_HW_INDEX;
}

bool
NativeThreadListDarwin::DisableHardwareBreakpoint (const DNBBreakpoint* bp) const
{
    if (bp != NULL)
    {
        const size_t num_threads = m_threads.size();
        for (uint32_t idx = 0; idx < num_threads; ++idx)
            m_threads[idx]->DisableHardwareBreakpoint(bp);
    }
    return false;
}

// DNBWatchpointSet() -> MachProcess::CreateWatchpoint() ->
// MachProcess::EnableWatchpoint() ->
// NativeThreadListDarwin::EnableHardwareWatchpoint().
uint32_t
NativeThreadListDarwin::EnableHardwareWatchpoint (const DNBBreakpoint* wp) const
{
    uint32_t hw_index = INVALID_NUB_HW_INDEX;
    if (wp != NULL)
    {
        PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
        const size_t num_threads = m_threads.size();
        // On Mac OS X we have to prime the control registers for new threads.
        // We do this using the control register data for the first thread, for
        // lack of a better way of choosing.
        bool also_set_on_task = true;
        for (uint32_t idx = 0; idx < num_threads; ++idx)
        {                
            if ((hw_index = m_threads[idx]->EnableHardwareWatchpoint(wp, also_set_on_task)) == INVALID_NUB_HW_INDEX)
            {
                // We know that idx failed for some reason.  Let's rollback the
                // transaction for [0, idx).
                for (uint32_t i = 0; i < idx; ++i)
                    m_threads[i]->RollbackTransForHWP();
                return INVALID_NUB_HW_INDEX;
            }
            also_set_on_task = false;
        }
        // Notify each thread to commit the pending transaction.
        for (uint32_t idx = 0; idx < num_threads; ++idx)
            m_threads[idx]->FinishTransForHWP();

    }
    return hw_index;
}

bool
NativeThreadListDarwin::DisableHardwareWatchpoint (const DNBBreakpoint* wp) const
{
    if (wp != NULL)
    {
        PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
        const size_t num_threads = m_threads.size();
        
        // On Mac OS X we have to prime the control registers for new threads.
        // We do this using the control register data for the first thread, for
        // lack of a better way of choosing.
        bool also_set_on_task = true;
        for (uint32_t idx = 0; idx < num_threads; ++idx)
        {
            if (!m_threads[idx]->DisableHardwareWatchpoint(wp, also_set_on_task))
            {
                // We know that idx failed for some reason.  Let's rollback the
                // transaction for [0, idx).
                for (uint32_t i = 0; i < idx; ++i)
                    m_threads[i]->RollbackTransForHWP();
                return false;
            }
            also_set_on_task = false;
        }
        // Notify each thread to commit the pending transaction.
        for (uint32_t idx = 0; idx < num_threads; ++idx)
            m_threads[idx]->FinishTransForHWP();

        return true;
    }
    return false;
}

uint32_t
NativeThreadListDarwin::NumSupportedHardwareWatchpoints () const
{
    PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
    const size_t num_threads = m_threads.size();
    // Use an arbitrary thread to retrieve the number of supported hardware
    // watchpoints.
    if (num_threads)
        return m_threads[0]->NumSupportedHardwareWatchpoints();
    return 0;
}

uint32_t
NativeThreadListDarwin::GetThreadIndexForThreadStoppedWithSignal (const int signo) const
{
    PTHREAD_MUTEX_LOCKER (locker, m_threads_mutex);
    uint32_t should_stop = false;
    const size_t num_threads = m_threads.size();
    for (uint32_t idx = 0; !should_stop && idx < num_threads; ++idx)
    {
        if (m_threads[idx]->GetStopException().SoftSignal () == signo)
            return idx;
    }
    return UINT32_MAX;
}

#endif
