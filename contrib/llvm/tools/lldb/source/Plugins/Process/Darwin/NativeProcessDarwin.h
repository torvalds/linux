//===-- NativeProcessDarwin.h --------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef NativeProcessDarwin_h
#define NativeProcessDarwin_h

// NOTE: this code should only be compiled on Apple Darwin systems.  It is
// not cross-platform code and is not intended to build on any other platform.
// Therefore, platform-specific headers and code are okay here.

// C includes
#include <mach/mach_types.h>

// C++ includes
#include <mutex>
#include <unordered_set>

#include "lldb/Host/Debug.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-types.h"

#include "LaunchFlavor.h"
#include "MachException.h"
#include "NativeThreadDarwin.h"
#include "NativeThreadListDarwin.h"

namespace lldb_private {
class Status;
class Scalar;

namespace process_darwin {

/// @class NativeProcessDarwin
/// Manages communication with the inferior (debugee) process.
///
/// Upon construction, this class prepares and launches an inferior process
/// for debugging.
///
/// Changes in the inferior process state are broadcasted.
class NativeProcessDarwin : public NativeProcessProtocol {
  friend Status NativeProcessProtocol::Launch(
      ProcessLaunchInfo &launch_info, NativeDelegate &native_delegate,
      MainLoop &mainloop, NativeProcessProtocolSP &process_sp);

  friend Status NativeProcessProtocol::Attach(
      lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &native_delegate,
      MainLoop &mainloop, NativeProcessProtocolSP &process_sp);

public:
  ~NativeProcessDarwin() override;

  // -----------------------------------------------------------------
  // NativeProcessProtocol Interface
  // -----------------------------------------------------------------
  Status Resume(const ResumeActionList &resume_actions) override;

  Status Halt() override;

  Status Detach() override;

  Status Signal(int signo) override;

  Status Interrupt() override;

  Status Kill() override;

  Status GetMemoryRegionInfo(lldb::addr_t load_addr,
                             MemoryRegionInfo &range_info) override;

  Status ReadMemory(lldb::addr_t addr, void *buf, size_t size,
                    size_t &bytes_read) override;

  Status ReadMemoryWithoutTrap(lldb::addr_t addr, void *buf, size_t size,
                               size_t &bytes_read) override;

  Status WriteMemory(lldb::addr_t addr, const void *buf, size_t size,
                     size_t &bytes_written) override;

  Status AllocateMemory(size_t size, uint32_t permissions,
                        lldb::addr_t &addr) override;

  Status DeallocateMemory(lldb::addr_t addr) override;

  lldb::addr_t GetSharedLibraryInfoAddress() override;

  size_t UpdateThreads() override;

  bool GetArchitecture(ArchSpec &arch) const override;

  Status SetBreakpoint(lldb::addr_t addr, uint32_t size,
                       bool hardware) override;

  void DoStopIDBumped(uint32_t newBumpId) override;

  Status GetLoadedModuleFileSpec(const char *module_path,
                                 FileSpec &file_spec) override;

  Status GetFileLoadAddress(const llvm::StringRef &file_name,
                            lldb::addr_t &load_addr) override;

  NativeThreadDarwinSP GetThreadByID(lldb::tid_t id);

  task_t GetTask() const { return m_task; }

  // -----------------------------------------------------------------
  // Interface used by NativeRegisterContext-derived classes.
  // -----------------------------------------------------------------
  static Status PtraceWrapper(int req, lldb::pid_t pid, void *addr = nullptr,
                              void *data = nullptr, size_t data_size = 0,
                              long *result = nullptr);

  bool SupportHardwareSingleStepping() const;

protected:
  // -----------------------------------------------------------------
  // NativeProcessProtocol protected interface
  // -----------------------------------------------------------------
  Status
  GetSoftwareBreakpointTrapOpcode(size_t trap_opcode_size_hint,
                                  size_t &actual_opcode_size,
                                  const uint8_t *&trap_opcode_bytes) override;

private:
  // -----------------------------------------------------------------
  /// Mach task-related Member Variables
  // -----------------------------------------------------------------

  // The task port for the inferior process.
  mutable task_t m_task;

  // True if the inferior process did an exec since we started
  // monitoring it.
  bool m_did_exec;

  // The CPU type of this process.
  mutable cpu_type_t m_cpu_type;

  // -----------------------------------------------------------------
  /// Exception/Signal Handling Member Variables
  // -----------------------------------------------------------------

  // Exception port on which we will receive child exceptions
  mach_port_t m_exception_port;

  // Saved state of the child exception port prior to us installing
  // our own intercepting port.
  MachException::PortInfo m_exc_port_info;

  // The thread that runs the Mach exception read and reply handler.
  pthread_t m_exception_thread;

  // TODO see if we can remove this if we get the exception collection
  // and distribution to happen in a single-threaded fashion.
  std::recursive_mutex m_exception_messages_mutex;

  // A collection of exception messages caught when listening to the
  // exception port.
  MachException::Message::collection m_exception_messages;

  // When we call MachProcess::Interrupt(), we want to send this
  // signal (if non-zero).
  int m_sent_interrupt_signo;

  // If we resume the process and still haven't received our
  // interrupt signal (if this is non-zero).
  int m_auto_resume_signo;

  // -----------------------------------------------------------------
  /// Thread-related Member Variables
  // -----------------------------------------------------------------
  NativeThreadListDarwin m_thread_list;
  ResumeActionList m_thread_actions;

  // -----------------------------------------------------------------
  /// Process Lifetime Member Variable
  // -----------------------------------------------------------------

  // The pipe over which the waitpid thread and the main loop will
  // communicate.
  Pipe m_waitpid_pipe;

  // The thread that runs the waitpid handler.
  pthread_t m_waitpid_thread;

  // waitpid reader callback handle.
  MainLoop::ReadHandleUP m_waitpid_reader_handle;

  // -----------------------------------------------------------------
  // Private Instance Methods
  // -----------------------------------------------------------------
  NativeProcessDarwin(lldb::pid_t pid, int pty_master_fd);

  // -----------------------------------------------------------------
  /// Finalize the launch.
  ///
  /// This method associates the NativeProcessDarwin instance with the host
  /// process that was just launched.  It peforms actions like attaching a
  /// listener to the inferior exception port, ptracing the process, and the
  /// like.
  ///
  /// @param[in] launch_flavor
  ///     The launch flavor that was used to launch the process.
  ///
  /// @param[in] main_loop
  ///     The main loop that will run the process monitor.  Work
  ///     that needs to be done (e.g. reading files) gets registered
  ///     here along with callbacks to process the work.
  ///
  /// @return
  ///     Any error that occurred during the aforementioned
  ///     operations.  Failure here will force termination of the
  ///     launched process and debugging session.
  // -----------------------------------------------------------------
  Status FinalizeLaunch(LaunchFlavor launch_flavor, MainLoop &main_loop);

  Status SaveExceptionPortInfo();

  void ExceptionMessageReceived(const MachException::Message &message);

  void MaybeRaiseThreadPriority();

  Status StartExceptionThread();

  Status SendInferiorExitStatusToMainLoop(::pid_t pid, int status);

  Status HandleWaitpidResult();

  bool ProcessUsingSpringBoard() const;

  bool ProcessUsingBackBoard() const;

  static void *ExceptionThread(void *arg);

  void *DoExceptionThread();

  lldb::addr_t GetDYLDAllImageInfosAddress(Status &error) const;

  static uint32_t GetCPUTypeForLocalProcess(::pid_t pid);

  uint32_t GetCPUType() const;

  task_t ExceptionMessageBundleComplete();

  void StartSTDIOThread();

  Status StartWaitpidThread(MainLoop &main_loop);

  static void *WaitpidThread(void *arg);

  void *DoWaitpidThread();

  task_t TaskPortForProcessID(Status &error, bool force = false) const;

  /// Attaches to an existing process.  Forms the implementation of
  /// Process::DoAttach.
  void AttachToInferior(MainLoop &mainloop, lldb::pid_t pid, Status &error);

  ::pid_t Attach(lldb::pid_t pid, Status &error);

  Status PrivateResume();

  Status ReplyToAllExceptions();

  Status ResumeTask();

  bool IsTaskValid() const;

  bool IsTaskValid(task_t task) const;

  mach_port_t GetExceptionPort() const;

  bool IsExceptionPortValid() const;

  Status GetTaskBasicInfo(task_t task, struct task_basic_info *info) const;

  Status SuspendTask();

  static Status SetDefaultPtraceOpts(const lldb::pid_t);

  static void *MonitorThread(void *baton);

  void MonitorCallback(lldb::pid_t pid, bool exited, int signal, int status);

  void WaitForNewThread(::pid_t tid);

  void MonitorSIGTRAP(const siginfo_t &info, NativeThreadDarwin &thread);

  void MonitorTrace(NativeThreadDarwin &thread);

  void MonitorBreakpoint(NativeThreadDarwin &thread);

  void MonitorWatchpoint(NativeThreadDarwin &thread, uint32_t wp_index);

  void MonitorSignal(const siginfo_t &info, NativeThreadDarwin &thread,
                     bool exited);

  Status SetupSoftwareSingleStepping(NativeThreadDarwin &thread);

  bool HasThreadNoLock(lldb::tid_t thread_id);

  bool StopTrackingThread(lldb::tid_t thread_id);

  NativeThreadDarwinSP AddThread(lldb::tid_t thread_id);

  Status GetSoftwareBreakpointPCOffset(uint32_t &actual_opcode_size);

  Status FixupBreakpointPCAsNeeded(NativeThreadDarwin &thread);

  /// Writes a siginfo_t structure corresponding to the given thread
  /// ID to the memory region pointed to by @p siginfo.
  Status GetSignalInfo(lldb::tid_t tid, void *siginfo);

  /// Writes the raw event message code (vis-a-vis PTRACE_GETEVENTMSG)
  /// corresponding to the given thread ID to the memory pointed to by @p
  /// message.
  Status GetEventMessage(lldb::tid_t tid, unsigned long *message);

  void NotifyThreadDeath(lldb::tid_t tid);

  Status Detach(lldb::tid_t tid);

  // This method is requests a stop on all threads which are still
  // running. It sets up a deferred delegate notification, which will
  // fire once threads report as stopped. The triggerring_tid will be
  // set as the current thread (main stop reason).
  void StopRunningThreads(lldb::tid_t triggering_tid);

  // Notify the delegate if all threads have stopped.
  void SignalIfAllThreadsStopped();

  // Resume the given thread, optionally passing it the given signal.
  // The type of resume operation (continue, single-step) depends on
  // the state parameter.
  Status ResumeThread(NativeThreadDarwin &thread, lldb::StateType state,
                      int signo);

  void ThreadWasCreated(NativeThreadDarwin &thread);

  void SigchldHandler();
};

} // namespace process_darwin
} // namespace lldb_private

#endif /* NativeProcessDarwin_h */
