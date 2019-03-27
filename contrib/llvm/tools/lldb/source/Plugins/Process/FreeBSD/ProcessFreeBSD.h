//===-- ProcessFreeBSD.h ------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ProcessFreeBSD_H_
#define liblldb_ProcessFreeBSD_H_

#include "Plugins/Process/POSIX/ProcessMessage.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/ThreadList.h"
#include <mutex>
#include <queue>
#include <set>

class ProcessMonitor;
class FreeBSDThread;

class ProcessFreeBSD : public lldb_private::Process {

public:
  //------------------------------------------------------------------
  // Static functions.
  //------------------------------------------------------------------
  static lldb::ProcessSP
  CreateInstance(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                 const lldb_private::FileSpec *crash_file_path);

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static const char *GetPluginDescriptionStatic();

  //------------------------------------------------------------------
  // Constructors and destructors
  //------------------------------------------------------------------
  ProcessFreeBSD(lldb::TargetSP target_sp, lldb::ListenerSP listener_sp,
                 lldb::UnixSignalsSP &unix_signals_sp);

  ~ProcessFreeBSD();

  virtual lldb_private::Status WillResume() override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  virtual lldb_private::ConstString GetPluginName() override;

  virtual uint32_t GetPluginVersion() override;

public:
  //------------------------------------------------------------------
  // Process protocol.
  //------------------------------------------------------------------
  void Finalize() override;

  bool CanDebug(lldb::TargetSP target_sp,
                bool plugin_specified_by_name) override;

  lldb_private::Status WillLaunch(lldb_private::Module *module) override;

  lldb_private::Status DoAttachToProcessWithID(
      lldb::pid_t pid,
      const lldb_private::ProcessAttachInfo &attach_info) override;

  lldb_private::Status
  DoLaunch(lldb_private::Module *exe_module,
           lldb_private::ProcessLaunchInfo &launch_info) override;

  void DidLaunch() override;

  lldb_private::Status DoResume() override;

  lldb_private::Status DoHalt(bool &caused_stop) override;

  lldb_private::Status DoDetach(bool keep_stopped) override;

  lldb_private::Status DoSignal(int signal) override;

  lldb_private::Status DoDestroy() override;

  void DoDidExec() override;

  void RefreshStateAfterStop() override;

  bool IsAlive() override;

  size_t DoReadMemory(lldb::addr_t vm_addr, void *buf, size_t size,
                      lldb_private::Status &error) override;

  size_t DoWriteMemory(lldb::addr_t vm_addr, const void *buf, size_t size,
                       lldb_private::Status &error) override;

  lldb::addr_t DoAllocateMemory(size_t size, uint32_t permissions,
                                lldb_private::Status &error) override;

  lldb_private::Status DoDeallocateMemory(lldb::addr_t ptr) override;

  virtual size_t
  GetSoftwareBreakpointTrapOpcode(lldb_private::BreakpointSite *bp_site);

  lldb_private::Status
  EnableBreakpointSite(lldb_private::BreakpointSite *bp_site) override;

  lldb_private::Status
  DisableBreakpointSite(lldb_private::BreakpointSite *bp_site) override;

  lldb_private::Status EnableWatchpoint(lldb_private::Watchpoint *wp,
                                        bool notify = true) override;

  lldb_private::Status DisableWatchpoint(lldb_private::Watchpoint *wp,
                                         bool notify = true) override;

  lldb_private::Status GetWatchpointSupportInfo(uint32_t &num) override;

  lldb_private::Status GetWatchpointSupportInfo(uint32_t &num,
                                                bool &after) override;

  virtual uint32_t UpdateThreadListIfNeeded();

  bool UpdateThreadList(lldb_private::ThreadList &old_thread_list,
                        lldb_private::ThreadList &new_thread_list) override;

  virtual lldb::ByteOrder GetByteOrder() const;

  lldb::addr_t GetImageInfoAddress() override;

  size_t PutSTDIN(const char *buf, size_t len,
                  lldb_private::Status &error) override;

  const lldb::DataBufferSP GetAuxvData() override;

  //--------------------------------------------------------------------------
  // ProcessFreeBSD internal API.

  /// Registers the given message with this process.
  virtual void SendMessage(const ProcessMessage &message);

  ProcessMonitor &GetMonitor() {
    assert(m_monitor);
    return *m_monitor;
  }

  lldb_private::FileSpec
  GetFileSpec(const lldb_private::FileAction *file_action,
              const lldb_private::FileSpec &default_file_spec,
              const lldb_private::FileSpec &dbg_pts_file_spec);

  /// Adds the thread to the list of threads for which we have received the
  /// initial stopping signal.
  /// The \p stop_tid parameter indicates the thread which the stop happened
  /// for.
  bool AddThreadForInitialStopIfNeeded(lldb::tid_t stop_tid);

  bool WaitingForInitialStop(lldb::tid_t stop_tid);

  virtual FreeBSDThread *CreateNewFreeBSDThread(lldb_private::Process &process,
                                                lldb::tid_t tid);

  static bool SingleStepBreakpointHit(
      void *baton, lldb_private::StoppointCallbackContext *context,
      lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  lldb_private::Status SetupSoftwareSingleStepping(lldb::tid_t tid);

  lldb_private::Status SetSoftwareSingleStepBreakpoint(lldb::tid_t tid,
                                                       lldb::addr_t addr);

  bool IsSoftwareStepBreakpoint(lldb::tid_t tid);

  bool SupportHardwareSingleStepping() const;

  typedef std::vector<lldb::tid_t> tid_collection;
  tid_collection &GetStepTids() { return m_step_tids; }

protected:
  static const size_t MAX_TRAP_OPCODE_SIZE = 8;

  /// Target byte order.
  lldb::ByteOrder m_byte_order;

  /// Process monitor;
  ProcessMonitor *m_monitor;

  /// The module we are executing.
  lldb_private::Module *m_module;

  /// Message queue notifying this instance of inferior process state changes.
  std::recursive_mutex m_message_mutex;
  std::queue<ProcessMessage> m_message_queue;

  /// Drive any exit events to completion.
  bool m_exit_now;

  /// Returns true if the process has exited.
  bool HasExited();

  /// Returns true if the process is stopped.
  bool IsStopped();

  /// Returns true if at least one running is currently running
  bool IsAThreadRunning();

  typedef std::map<lldb::addr_t, lldb::addr_t> MMapMap;
  MMapMap m_addr_to_mmap_size;

  typedef std::set<lldb::tid_t> ThreadStopSet;
  /// Every thread begins with a stop signal. This keeps track
  /// of the threads for which we have received the stop signal.
  ThreadStopSet m_seen_initial_stop;

  friend class FreeBSDThread;

  tid_collection m_suspend_tids;
  tid_collection m_run_tids;
  tid_collection m_step_tids;
  std::map<lldb::tid_t, lldb::break_id_t> m_threads_stepping_with_breakpoint;

  int m_resume_signo;
};

#endif // liblldb_ProcessFreeBSD_H_
