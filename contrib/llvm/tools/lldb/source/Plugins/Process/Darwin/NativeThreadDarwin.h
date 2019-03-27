//===-- NativeThreadDarwin.h ---------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef NativeThreadDarwin_H
#define NativeThreadDarwin_H

// C includes
#include <mach/mach_types.h>
#include <sched.h>
#include <sys/proc_info.h>

// C++ includes
#include <map>
#include <memory>
#include <string>

// LLDB includes
#include "lldb/Host/common/NativeThreadProtocol.h"
#include "lldb/lldb-private-forward.h"

#include "MachException.h"

namespace lldb_private {
namespace process_darwin {

class NativeProcessDarwin;
using NativeProcessDarwinSP = std::shared_ptr<NativeProcessDarwin>;

class NativeThreadListDarwin;

class NativeThreadDarwin : public NativeThreadProtocol {
  friend class NativeProcessDarwin;
  friend class NativeThreadListDarwin;

public:
  static uint64_t
  GetGloballyUniqueThreadIDForMachPortID(::thread_t mach_port_id);

  NativeThreadDarwin(NativeProcessDarwin *process, bool is_64_bit,
                     lldb::tid_t unique_thread_id = 0,
                     ::thread_t mach_thread_port = 0);

  // -----------------------------------------------------------------
  // NativeThreadProtocol Interface
  // -----------------------------------------------------------------
  std::string GetName() override;

  lldb::StateType GetState() override;

  bool GetStopReason(ThreadStopInfo &stop_info,
                     std::string &description) override;

  NativeRegisterContextSP GetRegisterContext() override;

  Status SetWatchpoint(lldb::addr_t addr, size_t size, uint32_t watch_flags,
                       bool hardware) override;

  Status RemoveWatchpoint(lldb::addr_t addr) override;

  // -----------------------------------------------------------------
  // New methods that are fine for others to call.
  // -----------------------------------------------------------------
  void Dump(Stream &stream) const;

private:
  // -----------------------------------------------------------------
  // Interface for friend classes
  // -----------------------------------------------------------------

  /// Resumes the thread.  If @p signo is anything but
  /// LLDB_INVALID_SIGNAL_NUMBER, deliver that signal to the thread.
  Status Resume(uint32_t signo);

  /// Single steps the thread.  If @p signo is anything but
  /// LLDB_INVALID_SIGNAL_NUMBER, deliver that signal to the thread.
  Status SingleStep(uint32_t signo);

  bool NotifyException(MachException::Data &exc);

  bool ShouldStop(bool &step_more) const;

  void ThreadDidStop();

  void SetStoppedBySignal(uint32_t signo, const siginfo_t *info = nullptr);

  /// Return true if the thread is stopped.
  /// If stopped by a signal, indicate the signo in the signo
  /// argument.  Otherwise, return LLDB_INVALID_SIGNAL_NUMBER.
  bool IsStopped(int *signo);

  const struct thread_basic_info *GetBasicInfo() const;

  static bool GetBasicInfo(::thread_t thread,
                           struct thread_basic_info *basicInfoPtr);

  bool IsUserReady() const;

  void SetStoppedByExec();

  void SetStoppedByBreakpoint();

  void SetStoppedByWatchpoint(uint32_t wp_index);

  bool IsStoppedAtBreakpoint();

  bool IsStoppedAtWatchpoint();

  void SetStoppedByTrace();

  void SetStoppedWithNoReason();

  void SetExited();

  Status RequestStop();

  // -------------------------------------------------------------------------
  /// Return the mach thread port number for this thread.
  ///
  /// @return
  ///     The mach port number for this thread.  Returns NULL_THREAD
  ///     when the thread is invalid.
  // -------------------------------------------------------------------------
  thread_t GetMachPortNumber() const { return m_mach_thread_port; }

  static bool MachPortNumberIsValid(::thread_t thread);

  // ---------------------------------------------------------------------
  // Private interface
  // ---------------------------------------------------------------------
  bool GetIdentifierInfo();

  void MaybeLogStateChange(lldb::StateType new_state);

  NativeProcessDarwinSP GetNativeProcessDarwinSP();

  void SetStopped();

  inline void MaybePrepareSingleStepWorkaround();

  inline void MaybeCleanupSingleStepWorkaround();

  // -----------------------------------------------------------------
  // Member Variables
  // -----------------------------------------------------------------

  // The mach thread port for the thread.
  ::thread_t m_mach_thread_port;

  // The most recently-retrieved thread basic info.
  mutable ::thread_basic_info m_basic_info;

  struct proc_threadinfo m_proc_threadinfo;

  thread_identifier_info_data_t m_ident_info;

#if 0
    lldb::StateType m_state;
    ThreadStopInfo m_stop_info;
    NativeRegisterContextSP m_reg_context_sp;
    std::string m_stop_description;
    using WatchpointIndexMap = std::map<lldb::addr_t, uint32_t>;
    WatchpointIndexMap m_watchpoint_index_map;
    // cpu_set_t m_original_cpu_set; // For single-step workaround.
#endif
};

typedef std::shared_ptr<NativeThreadDarwin> NativeThreadDarwinSP;

} // namespace process_darwin
} // namespace lldb_private

#endif // #ifndef NativeThreadDarwin_H
