//===-- NativeThreadListDarwin.h --------------------------------------*- C++
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

#ifndef __NativeThreadListDarwin_h__
#define __NativeThreadListDarwin_h__

#include <memory>
#include <mutex>
#include <vector>

#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-types.h"

#include "MachException.h"

// #include "ThreadInfo.h"

namespace lldb_private {
namespace process_darwin {

class NativeBreakpointDarwin;
class NativeProcessDarwin;

class NativeThreadDarwin;
using NativeThreadDarwinSP = std::shared_ptr<NativeThreadDarwin>;

class NativeThreadListDarwin {
public:
  NativeThreadListDarwin();
  ~NativeThreadListDarwin();

  void Clear();

  void Dump(Stream &stream) const;

// These methods will be accessed directly from NativeThreadDarwin
#if 0
    bool            GetRegisterValue (nub_thread_t tid, uint32_t set, uint32_t reg, DNBRegisterValue *reg_value) const;
    bool            SetRegisterValue (nub_thread_t tid, uint32_t set, uint32_t reg, const DNBRegisterValue *reg_value) const;
    nub_size_t      GetRegisterContext (nub_thread_t tid, void *buf, size_t buf_len);
    nub_size_t      SetRegisterContext (nub_thread_t tid, const void *buf, size_t buf_len);
    uint32_t        SaveRegisterState (nub_thread_t tid);
    bool            RestoreRegisterState (nub_thread_t tid, uint32_t save_id);
#endif

  const char *GetThreadInfo(lldb::tid_t tid) const;

  void ProcessWillResume(NativeProcessDarwin &process,
                         const ResumeActionList &thread_actions);

  uint32_t ProcessDidStop(NativeProcessDarwin &process);

  bool NotifyException(MachException::Data &exc);

  bool ShouldStop(bool &step_more);

// These methods will be accessed directly from NativeThreadDarwin
#if 0
    const char *    GetName (nub_thread_t tid);
    nub_state_t     GetState (nub_thread_t tid);
    nub_thread_t    SetCurrentThread (nub_thread_t tid);
#endif

// TODO: figure out if we need to add this to NativeThreadDarwin yet.
#if 0
    ThreadInfo::QoS GetRequestedQoS (nub_thread_t tid, nub_addr_t tsd, uint64_t dti_qos_class_index);
    nub_addr_t      GetPThreadT (nub_thread_t tid);
    nub_addr_t      GetDispatchQueueT (nub_thread_t tid);
    nub_addr_t      GetTSDAddressForThread (nub_thread_t tid, uint64_t plo_pthread_tsd_base_address_offset, uint64_t plo_pthread_tsd_base_offset, uint64_t plo_pthread_tsd_entry_size);
#endif

// These methods will be accessed directly from NativeThreadDarwin
#if 0
    bool            GetThreadStoppedReason (nub_thread_t tid, struct DNBThreadStopInfo *stop_info) const;
    void            DumpThreadStoppedReason (nub_thread_t tid) const;
    bool            GetIdentifierInfo (nub_thread_t tid, thread_identifier_info_data_t *ident_info);
#endif

  size_t GetNumberOfThreads() const;

  lldb::tid_t ThreadIDAtIndex(size_t idx) const;

  lldb::tid_t GetCurrentThreadID();

  NativeThreadDarwinSP GetCurrentThread();

  void NotifyBreakpointChanged(const NativeBreakpointDarwin *bp);

  uint32_t EnableHardwareBreakpoint(const NativeBreakpointDarwin *bp) const;

  bool DisableHardwareBreakpoint(const NativeBreakpointDarwin *bp) const;

  uint32_t EnableHardwareWatchpoint(const NativeBreakpointDarwin *wp) const;

  bool DisableHardwareWatchpoint(const NativeBreakpointDarwin *wp) const;

  uint32_t GetNumberOfSupportedHardwareWatchpoints() const;

  size_t GetThreadIndexForThreadStoppedWithSignal(const int signo) const;

  NativeThreadDarwinSP GetThreadByID(lldb::tid_t tid) const;

  NativeThreadDarwinSP
  GetThreadByMachPortNumber(::thread_t mach_port_number) const;

  lldb::tid_t GetThreadIDByMachPortNumber(::thread_t mach_port_number) const;

  thread_t GetMachPortNumberByThreadID(lldb::tid_t globally_unique_id) const;

protected:
  typedef std::vector<NativeThreadDarwinSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  // Consider having this return an lldb_private::Status.
  uint32_t UpdateThreadList(NativeProcessDarwin &process, bool update,
                            collection *num_threads = nullptr);

  collection m_threads;
  mutable std::recursive_mutex m_threads_mutex;
  NativeThreadDarwinSP m_current_thread;
  bool m_is_64_bit;
};

} // namespace process_darwin
} // namespace lldb_private

#endif // #ifndef __NativeThreadListDarwin_h__
