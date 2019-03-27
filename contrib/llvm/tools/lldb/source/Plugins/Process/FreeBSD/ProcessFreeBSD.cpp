//===-- ProcessFreeBSD.cpp ----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <machine/elf.h>

#include <mutex>
#include <unordered_map>

#include "lldb/Core/PluginManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/State.h"

#include "FreeBSDThread.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Plugins/Process/Utility/FreeBSDSignals.h"
#include "Plugins/Process/Utility/InferiorCallPOSIX.h"
#include "ProcessFreeBSD.h"
#include "ProcessMonitor.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/State.h"

#include "lldb/Host/posix/Fcntl.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"

using namespace lldb;
using namespace lldb_private;

namespace {
UnixSignalsSP &GetFreeBSDSignals() {
  static UnixSignalsSP s_freebsd_signals_sp(new FreeBSDSignals());
  return s_freebsd_signals_sp;
}
}

//------------------------------------------------------------------------------
// Static functions.

lldb::ProcessSP
ProcessFreeBSD::CreateInstance(lldb::TargetSP target_sp,
                               lldb::ListenerSP listener_sp,
                               const FileSpec *crash_file_path) {
  lldb::ProcessSP process_sp;
  if (crash_file_path == NULL)
    process_sp.reset(
        new ProcessFreeBSD(target_sp, listener_sp, GetFreeBSDSignals()));
  return process_sp;
}

void ProcessFreeBSD::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(), CreateInstance);
  });
}

lldb_private::ConstString ProcessFreeBSD::GetPluginNameStatic() {
  static ConstString g_name("freebsd");
  return g_name;
}

const char *ProcessFreeBSD::GetPluginDescriptionStatic() {
  return "Process plugin for FreeBSD";
}

//------------------------------------------------------------------------------
// ProcessInterface protocol.

lldb_private::ConstString ProcessFreeBSD::GetPluginName() {
  return GetPluginNameStatic();
}

uint32_t ProcessFreeBSD::GetPluginVersion() { return 1; }

void ProcessFreeBSD::Terminate() {}

Status ProcessFreeBSD::DoDetach(bool keep_stopped) {
  Status error;
  if (keep_stopped) {
    error.SetErrorString("Detaching with keep_stopped true is not currently "
                         "supported on FreeBSD.");
    return error;
  }

  error = m_monitor->Detach(GetID());

  if (error.Success())
    SetPrivateState(eStateDetached);

  return error;
}

Status ProcessFreeBSD::DoResume() {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));

  SetPrivateState(eStateRunning);

  std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
  bool do_step = false;
  bool software_single_step = !SupportHardwareSingleStepping();

  for (tid_collection::const_iterator t_pos = m_run_tids.begin(),
                                      t_end = m_run_tids.end();
       t_pos != t_end; ++t_pos) {
    m_monitor->ThreadSuspend(*t_pos, false);
  }
  for (tid_collection::const_iterator t_pos = m_step_tids.begin(),
                                      t_end = m_step_tids.end();
       t_pos != t_end; ++t_pos) {
    m_monitor->ThreadSuspend(*t_pos, false);
    do_step = true;
    if (software_single_step) {
      Status error = SetupSoftwareSingleStepping(*t_pos);
      if (error.Fail())
        return error;
    }
  }
  for (tid_collection::const_iterator t_pos = m_suspend_tids.begin(),
                                      t_end = m_suspend_tids.end();
       t_pos != t_end; ++t_pos) {
    m_monitor->ThreadSuspend(*t_pos, true);
    // XXX Cannot PT_CONTINUE properly with suspended threads.
    do_step = true;
  }

  if (log)
    log->Printf("process %" PRIu64 " resuming (%s)", GetID(),
                do_step ? "step" : "continue");
  if (do_step && !software_single_step)
    m_monitor->SingleStep(GetID(), m_resume_signo);
  else
    m_monitor->Resume(GetID(), m_resume_signo);

  return Status();
}

bool ProcessFreeBSD::UpdateThreadList(ThreadList &old_thread_list,
                                      ThreadList &new_thread_list) {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));
  if (log)
    log->Printf("ProcessFreeBSD::%s (pid = %" PRIu64 ")", __FUNCTION__,
                GetID());

  std::vector<lldb::pid_t> tds;
  if (!GetMonitor().GetCurrentThreadIDs(tds)) {
    return false;
  }

  ThreadList old_thread_list_copy(old_thread_list);
  for (size_t i = 0; i < tds.size(); ++i) {
    tid_t tid = tds[i];
    ThreadSP thread_sp(old_thread_list_copy.RemoveThreadByID(tid, false));
    if (!thread_sp) {
      thread_sp.reset(new FreeBSDThread(*this, tid));
      if (log)
        log->Printf("ProcessFreeBSD::%s new tid = %" PRIu64, __FUNCTION__, tid);
    } else {
      if (log)
        log->Printf("ProcessFreeBSD::%s existing tid = %" PRIu64, __FUNCTION__,
                    tid);
    }
    new_thread_list.AddThread(thread_sp);
  }
  for (size_t i = 0; i < old_thread_list_copy.GetSize(false); ++i) {
    ThreadSP old_thread_sp(old_thread_list_copy.GetThreadAtIndex(i, false));
    if (old_thread_sp) {
      if (log)
        log->Printf("ProcessFreeBSD::%s remove tid", __FUNCTION__);
    }
  }

  return true;
}

Status ProcessFreeBSD::WillResume() {
  m_resume_signo = 0;
  m_suspend_tids.clear();
  m_run_tids.clear();
  m_step_tids.clear();
  return Process::WillResume();
}

void ProcessFreeBSD::SendMessage(const ProcessMessage &message) {
  std::lock_guard<std::recursive_mutex> guard(m_message_mutex);

  switch (message.GetKind()) {
  case ProcessMessage::eInvalidMessage:
    return;

  case ProcessMessage::eAttachMessage:
    SetPrivateState(eStateStopped);
    return;

  case ProcessMessage::eLimboMessage:
  case ProcessMessage::eExitMessage:
    SetExitStatus(message.GetExitStatus(), NULL);
    break;

  case ProcessMessage::eSignalMessage:
  case ProcessMessage::eSignalDeliveredMessage:
  case ProcessMessage::eBreakpointMessage:
  case ProcessMessage::eTraceMessage:
  case ProcessMessage::eWatchpointMessage:
  case ProcessMessage::eCrashMessage:
    SetPrivateState(eStateStopped);
    break;

  case ProcessMessage::eNewThreadMessage:
    llvm_unreachable("eNewThreadMessage unexpected on FreeBSD");
    break;

  case ProcessMessage::eExecMessage:
    SetPrivateState(eStateStopped);
    break;
  }

  m_message_queue.push(message);
}

//------------------------------------------------------------------------------
// Constructors and destructors.

ProcessFreeBSD::ProcessFreeBSD(lldb::TargetSP target_sp,
                               lldb::ListenerSP listener_sp,
                               UnixSignalsSP &unix_signals_sp)
    : Process(target_sp, listener_sp, unix_signals_sp),
      m_byte_order(endian::InlHostByteOrder()), m_monitor(NULL), m_module(NULL),
      m_message_mutex(), m_exit_now(false), m_seen_initial_stop(),
      m_resume_signo(0) {
  // FIXME: Putting this code in the ctor and saving the byte order in a
  // member variable is a hack to avoid const qual issues in GetByteOrder.
  lldb::ModuleSP module = GetTarget().GetExecutableModule();
  if (module && module->GetObjectFile())
    m_byte_order = module->GetObjectFile()->GetByteOrder();
}

ProcessFreeBSD::~ProcessFreeBSD() { delete m_monitor; }

//------------------------------------------------------------------------------
// Process protocol.
void ProcessFreeBSD::Finalize() {
  Process::Finalize();

  if (m_monitor)
    m_monitor->StopMonitor();
}

bool ProcessFreeBSD::CanDebug(lldb::TargetSP target_sp,
                              bool plugin_specified_by_name) {
  // For now we are just making sure the file exists for a given module
  ModuleSP exe_module_sp(target_sp->GetExecutableModule());
  if (exe_module_sp.get())
    return FileSystem::Instance().Exists(exe_module_sp->GetFileSpec());
  // If there is no executable module, we return true since we might be
  // preparing to attach.
  return true;
}

Status
ProcessFreeBSD::DoAttachToProcessWithID(lldb::pid_t pid,
                                        const ProcessAttachInfo &attach_info) {
  Status error;
  assert(m_monitor == NULL);

  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));
  LLDB_LOGV(log, "pid = {0}", GetID());

  m_monitor = new ProcessMonitor(this, pid, error);

  if (!error.Success())
    return error;

  PlatformSP platform_sp(GetTarget().GetPlatform());
  assert(platform_sp.get());
  if (!platform_sp)
    return error; // FIXME: Detatch?

  // Find out what we can about this process
  ProcessInstanceInfo process_info;
  platform_sp->GetProcessInfo(pid, process_info);

  // Resolve the executable module
  ModuleSP exe_module_sp;
  FileSpecList executable_search_paths(
      Target::GetDefaultExecutableSearchPaths());
  ModuleSpec exe_module_spec(process_info.GetExecutableFile(),
                             GetTarget().GetArchitecture());
  error = platform_sp->ResolveExecutable(
      exe_module_spec, exe_module_sp,
      executable_search_paths.GetSize() ? &executable_search_paths : NULL);
  if (!error.Success())
    return error;

  // Fix the target architecture if necessary
  const ArchSpec &module_arch = exe_module_sp->GetArchitecture();
  if (module_arch.IsValid() &&
      !GetTarget().GetArchitecture().IsExactMatch(module_arch))
    GetTarget().SetArchitecture(module_arch);

  // Initialize the target module list
  GetTarget().SetExecutableModule(exe_module_sp, eLoadDependentsYes);

  SetSTDIOFileDescriptor(m_monitor->GetTerminalFD());

  SetID(pid);

  return error;
}

Status ProcessFreeBSD::WillLaunch(Module *module) {
  Status error;
  return error;
}

FileSpec
ProcessFreeBSD::GetFileSpec(const lldb_private::FileAction *file_action,
                            const FileSpec &default_file_spec,
                            const FileSpec &dbg_pts_file_spec) {
  FileSpec file_spec{};

  if (file_action && file_action->GetAction() == FileAction::eFileActionOpen) {
    file_spec = file_action->GetFileSpec();
    // By default the stdio paths passed in will be pseudo-terminal (/dev/pts).
    // If so, convert to using a different default path instead to redirect I/O
    // to the debugger console. This should also handle user overrides to
    // /dev/null or a different file.
    if (!file_spec || file_spec == dbg_pts_file_spec)
      file_spec = default_file_spec;
  }
  return file_spec;
}

Status ProcessFreeBSD::DoLaunch(Module *module,
                                ProcessLaunchInfo &launch_info) {
  Status error;
  assert(m_monitor == NULL);

  FileSpec working_dir = launch_info.GetWorkingDirectory();
  if (working_dir) {
    FileSystem::Instance().Resolve(working_dir);
    if (!FileSystem::Instance().IsDirectory(working_dir.GetPath())) {
      error.SetErrorStringWithFormat("No such file or directory: %s",
                                   working_dir.GetCString());
      return error;
    }
  }

  SetPrivateState(eStateLaunching);

  const lldb_private::FileAction *file_action;

  // Default of empty will mean to use existing open file descriptors
  FileSpec stdin_file_spec{};
  FileSpec stdout_file_spec{};
  FileSpec stderr_file_spec{};

  const FileSpec dbg_pts_file_spec{launch_info.GetPTY().GetSlaveName(NULL, 0)};

  file_action = launch_info.GetFileActionForFD(STDIN_FILENO);
  stdin_file_spec =
      GetFileSpec(file_action, stdin_file_spec, dbg_pts_file_spec);

  file_action = launch_info.GetFileActionForFD(STDOUT_FILENO);
  stdout_file_spec =
      GetFileSpec(file_action, stdout_file_spec, dbg_pts_file_spec);

  file_action = launch_info.GetFileActionForFD(STDERR_FILENO);
  stderr_file_spec =
      GetFileSpec(file_action, stderr_file_spec, dbg_pts_file_spec);

  m_monitor = new ProcessMonitor(
      this, module, launch_info.GetArguments().GetConstArgumentVector(),
      launch_info.GetEnvironment(), stdin_file_spec, stdout_file_spec,
      stderr_file_spec, working_dir, launch_info, error);

  m_module = module;

  if (!error.Success())
    return error;

  int terminal = m_monitor->GetTerminalFD();
  if (terminal >= 0) {
// The reader thread will close the file descriptor when done, so we pass it a
// copy.
#ifdef F_DUPFD_CLOEXEC
    int stdio = fcntl(terminal, F_DUPFD_CLOEXEC, 0);
    if (stdio == -1) {
      error.SetErrorToErrno();
      return error;
    }
#else
    // Special case when F_DUPFD_CLOEXEC does not exist (Debian kFreeBSD)
    int stdio = fcntl(terminal, F_DUPFD, 0);
    if (stdio == -1) {
      error.SetErrorToErrno();
      return error;
    }
    stdio = fcntl(terminal, F_SETFD, FD_CLOEXEC);
    if (stdio == -1) {
      error.SetErrorToErrno();
      return error;
    }
#endif
    SetSTDIOFileDescriptor(stdio);
  }

  SetID(m_monitor->GetPID());
  return error;
}

void ProcessFreeBSD::DidLaunch() {}

addr_t ProcessFreeBSD::GetImageInfoAddress() {
  Target *target = &GetTarget();
  ObjectFile *obj_file = target->GetExecutableModule()->GetObjectFile();
  Address addr = obj_file->GetImageInfoAddress(target);

  if (addr.IsValid())
    return addr.GetLoadAddress(target);
  return LLDB_INVALID_ADDRESS;
}

Status ProcessFreeBSD::DoHalt(bool &caused_stop) {
  Status error;

  if (IsStopped()) {
    caused_stop = false;
  } else if (kill(GetID(), SIGSTOP)) {
    caused_stop = false;
    error.SetErrorToErrno();
  } else {
    caused_stop = true;
  }
  return error;
}

Status ProcessFreeBSD::DoSignal(int signal) {
  Status error;

  if (kill(GetID(), signal))
    error.SetErrorToErrno();

  return error;
}

Status ProcessFreeBSD::DoDestroy() {
  Status error;

  if (!HasExited()) {
    assert(m_monitor);
    m_exit_now = true;
    if (GetID() == LLDB_INVALID_PROCESS_ID) {
      error.SetErrorString("invalid process id");
      return error;
    }
    if (!m_monitor->Kill()) {
      error.SetErrorToErrno();
      return error;
    }

    SetPrivateState(eStateExited);
  }

  return error;
}

void ProcessFreeBSD::DoDidExec() {
  Target *target = &GetTarget();
  if (target) {
    PlatformSP platform_sp(target->GetPlatform());
    assert(platform_sp.get());
    if (platform_sp) {
      ProcessInstanceInfo process_info;
      platform_sp->GetProcessInfo(GetID(), process_info);
      ModuleSP exe_module_sp;
      ModuleSpec exe_module_spec(process_info.GetExecutableFile(),
                                 target->GetArchitecture());
      FileSpecList executable_search_paths(
          Target::GetDefaultExecutableSearchPaths());
      Status error = platform_sp->ResolveExecutable(
          exe_module_spec, exe_module_sp,
          executable_search_paths.GetSize() ? &executable_search_paths : NULL);
      if (!error.Success())
        return;
      target->SetExecutableModule(exe_module_sp, eLoadDependentsYes);
    }
  }
}

bool ProcessFreeBSD::AddThreadForInitialStopIfNeeded(lldb::tid_t stop_tid) {
  bool added_to_set = false;
  ThreadStopSet::iterator it = m_seen_initial_stop.find(stop_tid);
  if (it == m_seen_initial_stop.end()) {
    m_seen_initial_stop.insert(stop_tid);
    added_to_set = true;
  }
  return added_to_set;
}

bool ProcessFreeBSD::WaitingForInitialStop(lldb::tid_t stop_tid) {
  return (m_seen_initial_stop.find(stop_tid) == m_seen_initial_stop.end());
}

FreeBSDThread *
ProcessFreeBSD::CreateNewFreeBSDThread(lldb_private::Process &process,
                                       lldb::tid_t tid) {
  return new FreeBSDThread(process, tid);
}

void ProcessFreeBSD::RefreshStateAfterStop() {
  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));
  LLDB_LOGV(log, "message_queue size = {0}", m_message_queue.size());

  std::lock_guard<std::recursive_mutex> guard(m_message_mutex);

  // This method used to only handle one message.  Changing it to loop allows
  // it to handle the case where we hit a breakpoint while handling a different
  // breakpoint.
  while (!m_message_queue.empty()) {
    ProcessMessage &message = m_message_queue.front();

    // Resolve the thread this message corresponds to and pass it along.
    lldb::tid_t tid = message.GetTID();
    LLDB_LOGV(log, " message_queue size = {0}, pid = {1}",
              m_message_queue.size(), tid);

    m_thread_list.RefreshStateAfterStop();

    FreeBSDThread *thread = static_cast<FreeBSDThread *>(
        GetThreadList().FindThreadByID(tid, false).get());
    if (thread)
      thread->Notify(message);

    if (message.GetKind() == ProcessMessage::eExitMessage) {
      // FIXME: We should tell the user about this, but the limbo message is
      // probably better for that.
      LLDB_LOG(log, "removing thread, tid = {0}", tid);
      std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());

      ThreadSP thread_sp = m_thread_list.RemoveThreadByID(tid, false);
      thread_sp.reset();
      m_seen_initial_stop.erase(tid);
    }

    m_message_queue.pop();
  }
}

bool ProcessFreeBSD::IsAlive() {
  StateType state = GetPrivateState();
  return state != eStateDetached && state != eStateExited &&
         state != eStateInvalid && state != eStateUnloaded;
}

size_t ProcessFreeBSD::DoReadMemory(addr_t vm_addr, void *buf, size_t size,
                                    Status &error) {
  assert(m_monitor);
  return m_monitor->ReadMemory(vm_addr, buf, size, error);
}

size_t ProcessFreeBSD::DoWriteMemory(addr_t vm_addr, const void *buf,
                                     size_t size, Status &error) {
  assert(m_monitor);
  return m_monitor->WriteMemory(vm_addr, buf, size, error);
}

addr_t ProcessFreeBSD::DoAllocateMemory(size_t size, uint32_t permissions,
                                        Status &error) {
  addr_t allocated_addr = LLDB_INVALID_ADDRESS;

  unsigned prot = 0;
  if (permissions & lldb::ePermissionsReadable)
    prot |= eMmapProtRead;
  if (permissions & lldb::ePermissionsWritable)
    prot |= eMmapProtWrite;
  if (permissions & lldb::ePermissionsExecutable)
    prot |= eMmapProtExec;

  if (InferiorCallMmap(this, allocated_addr, 0, size, prot,
                       eMmapFlagsAnon | eMmapFlagsPrivate, -1, 0)) {
    m_addr_to_mmap_size[allocated_addr] = size;
    error.Clear();
  } else {
    allocated_addr = LLDB_INVALID_ADDRESS;
    error.SetErrorStringWithFormat(
        "unable to allocate %zu bytes of memory with permissions %s", size,
        GetPermissionsAsCString(permissions));
  }

  return allocated_addr;
}

Status ProcessFreeBSD::DoDeallocateMemory(lldb::addr_t addr) {
  Status error;
  MMapMap::iterator pos = m_addr_to_mmap_size.find(addr);
  if (pos != m_addr_to_mmap_size.end() &&
      InferiorCallMunmap(this, addr, pos->second))
    m_addr_to_mmap_size.erase(pos);
  else
    error.SetErrorStringWithFormat("unable to deallocate memory at 0x%" PRIx64,
                                   addr);

  return error;
}

size_t
ProcessFreeBSD::GetSoftwareBreakpointTrapOpcode(BreakpointSite *bp_site) {
  static const uint8_t g_aarch64_opcode[] = {0x00, 0x00, 0x20, 0xD4};
  static const uint8_t g_i386_opcode[] = {0xCC};

  ArchSpec arch = GetTarget().GetArchitecture();
  const uint8_t *opcode = NULL;
  size_t opcode_size = 0;

  switch (arch.GetMachine()) {
  default:
    assert(false && "CPU type not supported!");
    break;

  case llvm::Triple::arm: {
    // The ARM reference recommends the use of 0xe7fddefe and 0xdefe but the
    // linux kernel does otherwise.
    static const uint8_t g_arm_breakpoint_opcode[] = {0xf0, 0x01, 0xf0, 0xe7};
    static const uint8_t g_thumb_breakpoint_opcode[] = {0x01, 0xde};

    lldb::BreakpointLocationSP bp_loc_sp(bp_site->GetOwnerAtIndex(0));
    AddressClass addr_class = AddressClass::eUnknown;

    if (bp_loc_sp)
      addr_class = bp_loc_sp->GetAddress().GetAddressClass();

    if (addr_class == AddressClass::eCodeAlternateISA ||
        (addr_class == AddressClass::eUnknown &&
         bp_loc_sp->GetAddress().GetOffset() & 1)) {
      opcode = g_thumb_breakpoint_opcode;
      opcode_size = sizeof(g_thumb_breakpoint_opcode);
    } else {
      opcode = g_arm_breakpoint_opcode;
      opcode_size = sizeof(g_arm_breakpoint_opcode);
    }
  } break;
  case llvm::Triple::aarch64:
    opcode = g_aarch64_opcode;
    opcode_size = sizeof(g_aarch64_opcode);
    break;

  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    opcode = g_i386_opcode;
    opcode_size = sizeof(g_i386_opcode);
    break;
  }

  bp_site->SetTrapOpcode(opcode, opcode_size);
  return opcode_size;
}

Status ProcessFreeBSD::EnableBreakpointSite(BreakpointSite *bp_site) {
  return EnableSoftwareBreakpoint(bp_site);
}

Status ProcessFreeBSD::DisableBreakpointSite(BreakpointSite *bp_site) {
  return DisableSoftwareBreakpoint(bp_site);
}

Status ProcessFreeBSD::EnableWatchpoint(Watchpoint *wp, bool notify) {
  Status error;
  if (wp) {
    user_id_t watchID = wp->GetID();
    addr_t addr = wp->GetLoadAddress();
    Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_WATCHPOINTS));
    if (log)
      log->Printf("ProcessFreeBSD::EnableWatchpoint(watchID = %" PRIu64 ")",
                  watchID);
    if (wp->IsEnabled()) {
      if (log)
        log->Printf("ProcessFreeBSD::EnableWatchpoint(watchID = %" PRIu64
                    ") addr = 0x%8.8" PRIx64 ": watchpoint already enabled.",
                    watchID, (uint64_t)addr);
      return error;
    }

    // Try to find a vacant watchpoint slot in the inferiors' main thread
    uint32_t wp_hw_index = LLDB_INVALID_INDEX32;
    std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
    FreeBSDThread *thread = static_cast<FreeBSDThread *>(
        m_thread_list.GetThreadAtIndex(0, false).get());

    if (thread)
      wp_hw_index = thread->FindVacantWatchpointIndex();

    if (wp_hw_index == LLDB_INVALID_INDEX32) {
      error.SetErrorString("Setting hardware watchpoint failed.");
    } else {
      wp->SetHardwareIndex(wp_hw_index);
      bool wp_enabled = true;
      uint32_t thread_count = m_thread_list.GetSize(false);
      for (uint32_t i = 0; i < thread_count; ++i) {
        thread = static_cast<FreeBSDThread *>(
            m_thread_list.GetThreadAtIndex(i, false).get());
        if (thread)
          wp_enabled &= thread->EnableHardwareWatchpoint(wp);
        else
          wp_enabled = false;
      }
      if (wp_enabled) {
        wp->SetEnabled(true, notify);
        return error;
      } else {
        // Watchpoint enabling failed on at least one of the threads so roll
        // back all of them
        DisableWatchpoint(wp, false);
        error.SetErrorString("Setting hardware watchpoint failed");
      }
    }
  } else
    error.SetErrorString("Watchpoint argument was NULL.");
  return error;
}

Status ProcessFreeBSD::DisableWatchpoint(Watchpoint *wp, bool notify) {
  Status error;
  if (wp) {
    user_id_t watchID = wp->GetID();
    addr_t addr = wp->GetLoadAddress();
    Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_WATCHPOINTS));
    if (log)
      log->Printf("ProcessFreeBSD::DisableWatchpoint(watchID = %" PRIu64 ")",
                  watchID);
    if (!wp->IsEnabled()) {
      if (log)
        log->Printf("ProcessFreeBSD::DisableWatchpoint(watchID = %" PRIu64
                    ") addr = 0x%8.8" PRIx64 ": watchpoint already disabled.",
                    watchID, (uint64_t)addr);
      // This is needed (for now) to keep watchpoints disabled correctly
      wp->SetEnabled(false, notify);
      return error;
    }

    if (wp->IsHardware()) {
      bool wp_disabled = true;
      std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
      uint32_t thread_count = m_thread_list.GetSize(false);
      for (uint32_t i = 0; i < thread_count; ++i) {
        FreeBSDThread *thread = static_cast<FreeBSDThread *>(
            m_thread_list.GetThreadAtIndex(i, false).get());
        if (thread)
          wp_disabled &= thread->DisableHardwareWatchpoint(wp);
        else
          wp_disabled = false;
      }
      if (wp_disabled) {
        wp->SetHardwareIndex(LLDB_INVALID_INDEX32);
        wp->SetEnabled(false, notify);
        return error;
      } else
        error.SetErrorString("Disabling hardware watchpoint failed");
    }
  } else
    error.SetErrorString("Watchpoint argument was NULL.");
  return error;
}

Status ProcessFreeBSD::GetWatchpointSupportInfo(uint32_t &num) {
  Status error;
  std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
  FreeBSDThread *thread = static_cast<FreeBSDThread *>(
      m_thread_list.GetThreadAtIndex(0, false).get());
  if (thread)
    num = thread->NumSupportedHardwareWatchpoints();
  else
    error.SetErrorString("Process does not exist.");
  return error;
}

Status ProcessFreeBSD::GetWatchpointSupportInfo(uint32_t &num, bool &after) {
  Status error = GetWatchpointSupportInfo(num);
  // Watchpoints trigger and halt the inferior after the corresponding
  // instruction has been executed.
  after = true;
  return error;
}

uint32_t ProcessFreeBSD::UpdateThreadListIfNeeded() {
  std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
  // Do not allow recursive updates.
  return m_thread_list.GetSize(false);
}

ByteOrder ProcessFreeBSD::GetByteOrder() const {
  // FIXME: We should be able to extract this value directly.  See comment in
  // ProcessFreeBSD().
  return m_byte_order;
}

size_t ProcessFreeBSD::PutSTDIN(const char *buf, size_t len, Status &error) {
  ssize_t status;
  if ((status = write(m_monitor->GetTerminalFD(), buf, len)) < 0) {
    error.SetErrorToErrno();
    return 0;
  }
  return status;
}

//------------------------------------------------------------------------------
// Utility functions.

bool ProcessFreeBSD::HasExited() {
  switch (GetPrivateState()) {
  default:
    break;

  case eStateDetached:
  case eStateExited:
    return true;
  }

  return false;
}

bool ProcessFreeBSD::IsStopped() {
  switch (GetPrivateState()) {
  default:
    break;

  case eStateStopped:
  case eStateCrashed:
  case eStateSuspended:
    return true;
  }

  return false;
}

bool ProcessFreeBSD::IsAThreadRunning() {
  bool is_running = false;
  std::lock_guard<std::recursive_mutex> guard(m_thread_list.GetMutex());
  uint32_t thread_count = m_thread_list.GetSize(false);
  for (uint32_t i = 0; i < thread_count; ++i) {
    FreeBSDThread *thread = static_cast<FreeBSDThread *>(
        m_thread_list.GetThreadAtIndex(i, false).get());
    StateType thread_state = thread->GetState();
    if (thread_state == eStateRunning || thread_state == eStateStepping) {
      is_running = true;
      break;
    }
  }
  return is_running;
}

const DataBufferSP ProcessFreeBSD::GetAuxvData() {
  // If we're the local platform, we can ask the host for auxv data.
  PlatformSP platform_sp = GetTarget().GetPlatform();
  assert(platform_sp && platform_sp->IsHost());

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_AUXV, (int)m_process->GetID()};
  size_t auxv_size = AT_COUNT * sizeof(Elf_Auxinfo);
  DataBufferSP buf_sp(new DataBufferHeap(auxv_size, 0));

  if (::sysctl(mib, 4, buf_sp->GetBytes(), &auxv_size, NULL, 0) != 0) {
    perror("sysctl failed on auxv");
    buf_sp.reset();
  }

  return buf_sp;
}

struct EmulatorBaton {
  ProcessFreeBSD *m_process;
  RegisterContext *m_reg_context;

  // eRegisterKindDWARF -> RegisterValue
  std::unordered_map<uint32_t, RegisterValue> m_register_values;

  EmulatorBaton(ProcessFreeBSD *process, RegisterContext *reg_context)
      : m_process(process), m_reg_context(reg_context) {}
};

static size_t ReadMemoryCallback(EmulateInstruction *instruction, void *baton,
                                 const EmulateInstruction::Context &context,
                                 lldb::addr_t addr, void *dst, size_t length) {
  EmulatorBaton *emulator_baton = static_cast<EmulatorBaton *>(baton);

  Status error;
  size_t bytes_read =
      emulator_baton->m_process->DoReadMemory(addr, dst, length, error);
  if (!error.Success())
    bytes_read = 0;
  return bytes_read;
}

static bool ReadRegisterCallback(EmulateInstruction *instruction, void *baton,
                                 const RegisterInfo *reg_info,
                                 RegisterValue &reg_value) {
  EmulatorBaton *emulator_baton = static_cast<EmulatorBaton *>(baton);

  auto it = emulator_baton->m_register_values.find(
      reg_info->kinds[eRegisterKindDWARF]);
  if (it != emulator_baton->m_register_values.end()) {
    reg_value = it->second;
    return true;
  }

  // The emulator only fills in the dwarf register numbers (and in some cases
  // the generic register numbers). Get the full register info from the
  // register context based on the dwarf register numbers.
  const RegisterInfo *full_reg_info =
      emulator_baton->m_reg_context->GetRegisterInfo(
          eRegisterKindDWARF, reg_info->kinds[eRegisterKindDWARF]);

  bool error =
      emulator_baton->m_reg_context->ReadRegister(full_reg_info, reg_value);
  return error;
}

static bool WriteRegisterCallback(EmulateInstruction *instruction, void *baton,
                                  const EmulateInstruction::Context &context,
                                  const RegisterInfo *reg_info,
                                  const RegisterValue &reg_value) {
  EmulatorBaton *emulator_baton = static_cast<EmulatorBaton *>(baton);
  emulator_baton->m_register_values[reg_info->kinds[eRegisterKindDWARF]] =
      reg_value;
  return true;
}

static size_t WriteMemoryCallback(EmulateInstruction *instruction, void *baton,
                                  const EmulateInstruction::Context &context,
                                  lldb::addr_t addr, const void *dst,
                                  size_t length) {
  return length;
}

bool ProcessFreeBSD::SingleStepBreakpointHit(
    void *baton, lldb_private::StoppointCallbackContext *context,
    lldb::user_id_t break_id, lldb::user_id_t break_loc_id) {
  return false;
}

Status ProcessFreeBSD::SetSoftwareSingleStepBreakpoint(lldb::tid_t tid,
                                                       lldb::addr_t addr) {
  Status error;

  Log *log(ProcessPOSIXLog::GetLogIfAllCategoriesSet(POSIX_LOG_PROCESS));
  if (log) {
    log->Printf("ProcessFreeBSD::%s addr = 0x%" PRIx64, __FUNCTION__, addr);
    log->Printf("SoftwareBreakpoint::%s addr = 0x%" PRIx64, __FUNCTION__, addr);
  }

  // Validate the address.
  if (addr == LLDB_INVALID_ADDRESS)
    return Status("ProcessFreeBSD::%s invalid load address specified.",
                  __FUNCTION__);

  Breakpoint *const sw_step_break =
      m_process->GetTarget().CreateBreakpoint(addr, true, false).get();
  sw_step_break->SetCallback(SingleStepBreakpointHit, this, true);
  sw_step_break->SetBreakpointKind("software-signle-step");

  if (log)
    log->Printf("ProcessFreeBSD::%s addr = 0x%" PRIx64 " -- SUCCESS",
                __FUNCTION__, addr);

  m_threads_stepping_with_breakpoint.insert({tid, sw_step_break->GetID()});
  return Status();
}

bool ProcessFreeBSD::IsSoftwareStepBreakpoint(lldb::tid_t tid) {
  ThreadSP thread = GetThreadList().FindThreadByID(tid);
  if (!thread)
    return false;

  assert(thread->GetRegisterContext());
  lldb::addr_t stop_pc = thread->GetRegisterContext()->GetPC();

  const auto &iter = m_threads_stepping_with_breakpoint.find(tid);
  if (iter == m_threads_stepping_with_breakpoint.end())
    return false;

  lldb::break_id_t bp_id = iter->second;
  BreakpointSP bp = GetTarget().GetBreakpointByID(bp_id);
  if (!bp)
    return false;

  BreakpointLocationSP bp_loc = bp->FindLocationByAddress(stop_pc);
  if (!bp_loc)
    return false;

  GetTarget().RemoveBreakpointByID(bp_id);
  m_threads_stepping_with_breakpoint.erase(tid);
  return true;
}

bool ProcessFreeBSD::SupportHardwareSingleStepping() const {
  lldb_private::ArchSpec arch = GetTarget().GetArchitecture();
  if (arch.GetMachine() == llvm::Triple::arm ||
      arch.GetMachine() == llvm::Triple::mips64 ||
      arch.GetMachine() == llvm::Triple::mips64el ||
      arch.GetMachine() == llvm::Triple::mips ||
      arch.GetMachine() == llvm::Triple::mipsel)
    return false;
  return true;
}

Status ProcessFreeBSD::SetupSoftwareSingleStepping(lldb::tid_t tid) {
  std::unique_ptr<EmulateInstruction> emulator_ap(
      EmulateInstruction::FindPlugin(GetTarget().GetArchitecture(),
                                     eInstructionTypePCModifying, nullptr));

  if (emulator_ap == nullptr)
    return Status("Instruction emulator not found!");

  FreeBSDThread *thread = static_cast<FreeBSDThread *>(
      m_thread_list.FindThreadByID(tid, false).get());
  if (thread == NULL)
    return Status("Thread not found not found!");

  lldb::RegisterContextSP register_context_sp = thread->GetRegisterContext();

  EmulatorBaton baton(this, register_context_sp.get());
  emulator_ap->SetBaton(&baton);
  emulator_ap->SetReadMemCallback(&ReadMemoryCallback);
  emulator_ap->SetReadRegCallback(&ReadRegisterCallback);
  emulator_ap->SetWriteMemCallback(&WriteMemoryCallback);
  emulator_ap->SetWriteRegCallback(&WriteRegisterCallback);

  if (!emulator_ap->ReadInstruction())
    return Status("Read instruction failed!");

  bool emulation_result =
      emulator_ap->EvaluateInstruction(eEmulateInstructionOptionAutoAdvancePC);
  const RegisterInfo *reg_info_pc = register_context_sp->GetRegisterInfo(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  auto pc_it =
      baton.m_register_values.find(reg_info_pc->kinds[eRegisterKindDWARF]);

  lldb::addr_t next_pc;
  if (emulation_result) {
    assert(pc_it != baton.m_register_values.end() &&
           "Emulation was successful but PC wasn't updated");
    next_pc = pc_it->second.GetAsUInt64();
  } else if (pc_it == baton.m_register_values.end()) {
    // Emulate instruction failed and it haven't changed PC. Advance PC with
    // the size of the current opcode because the emulation of all
    // PC modifying instruction should be successful. The failure most
    // likely caused by a not supported instruction which don't modify PC.
    next_pc =
        register_context_sp->GetPC() + emulator_ap->GetOpcode().GetByteSize();
  } else {
    // The instruction emulation failed after it modified the PC. It is an
    // unknown error where we can't continue because the next instruction is
    // modifying the PC but we don't  know how.
    return Status("Instruction emulation failed unexpectedly");
  }

  SetSoftwareSingleStepBreakpoint(tid, next_pc);
  return Status();
}
