//===-- FreeBSDThread.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FreeBSDThread_H_
#define liblldb_FreeBSDThread_H_

#include <memory>
#include <string>

#include "RegisterContextPOSIX.h"
#include "lldb/Target/Thread.h"

class ProcessMessage;
class ProcessMonitor;
class POSIXBreakpointProtocol;

//------------------------------------------------------------------------------
// @class FreeBSDThread
// Abstraction of a FreeBSD thread.
class FreeBSDThread : public lldb_private::Thread {
public:
  //------------------------------------------------------------------
  // Constructors and destructors
  //------------------------------------------------------------------
  FreeBSDThread(lldb_private::Process &process, lldb::tid_t tid);

  virtual ~FreeBSDThread();

  // POSIXThread
  void RefreshStateAfterStop() override;

  // This notifies the thread when a private stop occurs.
  void DidStop() override;

  const char *GetInfo() override;

  void SetName(const char *name) override;

  const char *GetName() override;

  lldb::RegisterContextSP GetRegisterContext() override;

  lldb::RegisterContextSP
  CreateRegisterContextForFrame(lldb_private::StackFrame *frame) override;

  lldb::addr_t GetThreadPointer() override;

  //--------------------------------------------------------------------------
  // These functions provide a mapping from the register offset
  // back to the register index or name for use in debugging or log
  // output.

  unsigned GetRegisterIndexFromOffset(unsigned offset);

  const char *GetRegisterName(unsigned reg);

  const char *GetRegisterNameFromOffset(unsigned offset);

  //--------------------------------------------------------------------------
  // These methods form a specialized interface to POSIX threads.
  //
  bool Resume();

  void Notify(const ProcessMessage &message);

  //--------------------------------------------------------------------------
  // These methods provide an interface to watchpoints
  //
  bool EnableHardwareWatchpoint(lldb_private::Watchpoint *wp);

  bool DisableHardwareWatchpoint(lldb_private::Watchpoint *wp);

  uint32_t NumSupportedHardwareWatchpoints();

  uint32_t FindVacantWatchpointIndex();

protected:
  POSIXBreakpointProtocol *GetPOSIXBreakpointProtocol() {
    if (!m_reg_context_sp)
      m_reg_context_sp = GetRegisterContext();
    return m_posix_thread;
  }

  std::unique_ptr<lldb_private::StackFrame> m_frame_ap;

  lldb::BreakpointSiteSP m_breakpoint;

  bool m_thread_name_valid;
  std::string m_thread_name;
  POSIXBreakpointProtocol *m_posix_thread;

  ProcessMonitor &GetMonitor();

  bool CalculateStopInfo() override;

  void BreakNotify(const ProcessMessage &message);
  void WatchNotify(const ProcessMessage &message);
  virtual void TraceNotify(const ProcessMessage &message);
  void LimboNotify(const ProcessMessage &message);
  void SignalNotify(const ProcessMessage &message);
  void SignalDeliveredNotify(const ProcessMessage &message);
  void CrashNotify(const ProcessMessage &message);
  void ExitNotify(const ProcessMessage &message);
  void ExecNotify(const ProcessMessage &message);

  lldb_private::Unwind *GetUnwinder() override;

  //--------------------------------------------------------------------------
  // FreeBSDThread internal API.

  // POSIXThread override
  virtual void WillResume(lldb::StateType resume_state) override;
};

#endif // #ifndef liblldb_FreeBSDThread_H_
