//===-- NativeProcessDarwin.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NativeProcessDarwin.h"

// C includes
#include <mach/mach_init.h>
#include <mach/mach_traps.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>

// C++ includes
// LLDB includes
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Target/ProcessLaunchInfo.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "CFBundle.h"
#include "CFString.h"
#include "DarwinProcessLauncher.h"

#include "MachException.h"

#include "llvm/Support/FileSystem.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_darwin;
using namespace lldb_private::darwin_process_launcher;

// -----------------------------------------------------------------------------
// Hidden Impl
// -----------------------------------------------------------------------------

namespace {
struct hack_task_dyld_info {
  mach_vm_address_t all_image_info_addr;
  mach_vm_size_t all_image_info_size;
};
}

// -----------------------------------------------------------------------------
// Public Static Methods
// -----------------------------------------------------------------------------

Status NativeProcessProtocol::Launch(
    ProcessLaunchInfo &launch_info,
    NativeProcessProtocol::NativeDelegate &native_delegate, MainLoop &mainloop,
    NativeProcessProtocolSP &native_process_sp) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  Status error;

  // Verify the working directory is valid if one was specified.
  FileSpec working_dir(launch_info.GetWorkingDirectory());
  if (working_dir) {
    FileInstance::Instance().Resolve(working_dir);
    if (!FileSystem::Instance().IsDirectory(working_dir)) {
      error.SetErrorStringWithFormat("No such file or directory: %s",
                                   working_dir.GetCString());
      return error;
    }
  }

  // Launch the inferior.
  int pty_master_fd = -1;
  LaunchFlavor launch_flavor = LaunchFlavor::Default;

  error = LaunchInferior(launch_info, &pty_master_fd, &launch_flavor);

  // Handle launch failure.
  if (!error.Success()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() failed to launch process: "
                  "%s",
                  __FUNCTION__, error.AsCString());
    return error;
  }

  // Handle failure to return a pid.
  if (launch_info.GetProcessID() == LLDB_INVALID_PROCESS_ID) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() launch succeeded but no "
                  "pid was returned!  Aborting.",
                  __FUNCTION__);
    return error;
  }

  // Create the Darwin native process impl.
  std::shared_ptr<NativeProcessDarwin> np_darwin_sp(
      new NativeProcessDarwin(launch_info.GetProcessID(), pty_master_fd));
  if (!np_darwin_sp->RegisterNativeDelegate(native_delegate)) {
    native_process_sp.reset();
    error.SetErrorStringWithFormat("failed to register the native delegate");
    return error;
  }

  // Finalize the processing needed to debug the launched process with a
  // NativeProcessDarwin instance.
  error = np_darwin_sp->FinalizeLaunch(launch_flavor, mainloop);
  if (!error.Success()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() aborting, failed to finalize"
                  " the launching of the process: %s",
                  __FUNCTION__, error.AsCString());
    return error;
  }

  // Return the process and process id to the caller through the launch args.
  native_process_sp = np_darwin_sp;
  return error;
}

Status NativeProcessProtocol::Attach(
    lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &native_delegate,
    MainLoop &mainloop, NativeProcessProtocolSP &native_process_sp) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("NativeProcessDarwin::%s(pid = %" PRIi64 ")", __FUNCTION__,
                pid);

  // Retrieve the architecture for the running process.
  ArchSpec process_arch;
  Status error = ResolveProcessArchitecture(pid, process_arch);
  if (!error.Success())
    return error;

  // TODO get attach to return this value.
  const int pty_master_fd = -1;
  std::shared_ptr<NativeProcessDarwin> native_process_darwin_sp(
      new NativeProcessDarwin(pid, pty_master_fd));

  if (!native_process_darwin_sp->RegisterNativeDelegate(native_delegate)) {
    error.SetErrorStringWithFormat("failed to register the native "
                                   "delegate");
    return error;
  }

  native_process_darwin_sp->AttachToInferior(mainloop, pid, error);
  if (!error.Success())
    return error;

  native_process_sp = native_process_darwin_sp;
  return error;
}

// -----------------------------------------------------------------------------
// ctor/dtor
// -----------------------------------------------------------------------------

NativeProcessDarwin::NativeProcessDarwin(lldb::pid_t pid, int pty_master_fd)
    : NativeProcessProtocol(pid), m_task(TASK_NULL), m_did_exec(false),
      m_cpu_type(0), m_exception_port(MACH_PORT_NULL), m_exc_port_info(),
      m_exception_thread(nullptr), m_exception_messages_mutex(),
      m_sent_interrupt_signo(0), m_auto_resume_signo(0), m_thread_list(),
      m_thread_actions(), m_waitpid_pipe(), m_waitpid_thread(nullptr),
      m_waitpid_reader_handle() {
  // TODO add this to the NativeProcessProtocol constructor.
  m_terminal_fd = pty_master_fd;
}

NativeProcessDarwin::~NativeProcessDarwin() {}

// -----------------------------------------------------------------------------
// Instance methods
// -----------------------------------------------------------------------------

Status NativeProcessDarwin::FinalizeLaunch(LaunchFlavor launch_flavor,
                                           MainLoop &main_loop) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

#if 0
    m_path = path;
    size_t i;
    char const *arg;
    for (i=0; (arg = argv[i]) != NULL; i++)
        m_args.push_back(arg);
#endif

  error = StartExceptionThread();
  if (!error.Success()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failure starting the "
                  "mach exception port monitor thread: %s",
                  __FUNCTION__, error.AsCString());

    // Terminate the inferior process.  There's nothing meaningful we can do if
    // we can't receive signals and exceptions.  Since we launched the process,
    // it's fair game for us to kill it.
    ::ptrace(PT_KILL, m_pid, 0, 0);
    SetState(eStateExited);

    return error;
  }

  StartSTDIOThread();

  if (launch_flavor == LaunchFlavor::PosixSpawn) {
    SetState(eStateAttaching);
    errno = 0;
    int err = ::ptrace(PT_ATTACHEXC, m_pid, 0, 0);
    if (err == 0) {
      // m_flags |= eMachProcessFlagsAttached;
      if (log)
        log->Printf("NativeProcessDarwin::%s(): successfully spawned "
                    "process with pid %" PRIu64,
                    __FUNCTION__, m_pid);
    } else {
      error.SetErrorToErrno();
      SetState(eStateExited);
      if (log)
        log->Printf("NativeProcessDarwin::%s(): error: failed to "
                    "attach to spawned pid %" PRIu64 " (error=%d (%s))",
                    __FUNCTION__, m_pid, (int)error.GetError(),
                    error.AsCString());
      return error;
    }
  }

  if (log)
    log->Printf("NativeProcessDarwin::%s(): new pid is %" PRIu64 "...",
                __FUNCTION__, m_pid);

  // Spawn a thread to reap our child inferior process...
  error = StartWaitpidThread(main_loop);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failed to start waitpid() "
                  "thread: %s",
                  __FUNCTION__, error.AsCString());
    kill(SIGKILL, static_cast<::pid_t>(m_pid));
    return error;
  }

  if (TaskPortForProcessID(error) == TASK_NULL) {
    // We failed to get the task for our process ID which is bad. Kill our
    // process; otherwise, it will be stopped at the entry point and get
    // reparented to someone else and never go away.
    if (log)
      log->Printf("NativeProcessDarwin::%s(): could not get task port "
                  "for process, sending SIGKILL and exiting: %s",
                  __FUNCTION__, error.AsCString());
    kill(SIGKILL, static_cast<::pid_t>(m_pid));
    return error;
  }

  // Indicate that we're stopped, as we always launch suspended.
  SetState(eStateStopped);

  // Success.
  return error;
}

Status NativeProcessDarwin::SaveExceptionPortInfo() {
  return m_exc_port_info.Save(m_task);
}

bool NativeProcessDarwin::ProcessUsingSpringBoard() const {
  // TODO implement flags
  // return (m_flags & eMachProcessFlagsUsingSBS) != 0;
  return false;
}

bool NativeProcessDarwin::ProcessUsingBackBoard() const {
  // TODO implement flags
  // return (m_flags & eMachProcessFlagsUsingBKS) != 0;
  return false;
}

// Called by the exception thread when an exception has been received from our
// process. The exception message is completely filled and the exception data
// has already been copied.
void NativeProcessDarwin::ExceptionMessageReceived(
    const MachException::Message &message) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  std::lock_guard<std::recursive_mutex> locker(m_exception_messages_mutex);
  if (m_exception_messages.empty()) {
    // Suspend the task the moment we receive our first exception message.
    SuspendTask();
  }

  // Use a locker to automatically unlock our mutex in case of exceptions Add
  // the exception to our internal exception stack
  m_exception_messages.push_back(message);

  if (log)
    log->Printf("NativeProcessDarwin::%s(): new queued message count: %lu",
                __FUNCTION__, m_exception_messages.size());
}

void *NativeProcessDarwin::ExceptionThread(void *arg) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));
  if (!arg) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): cannot run mach exception "
                  "thread, mandatory process arg was null",
                  __FUNCTION__);
    return nullptr;
  }

  return reinterpret_cast<NativeProcessDarwin *>(arg)->DoExceptionThread();
}

void *NativeProcessDarwin::DoExceptionThread() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  if (log)
    log->Printf("NativeProcessDarwin::%s(arg=%p) starting thread...",
                __FUNCTION__, this);

  pthread_setname_np("exception monitoring thread");

  // Ensure we don't get CPU starved.
  MaybeRaiseThreadPriority();

  // We keep a count of the number of consecutive exceptions received so we
  // know to grab all exceptions without a timeout. We do this to get a bunch
  // of related exceptions on our exception port so we can process then
  // together. When we have multiple threads, we can get an exception per
  // thread and they will come in consecutively. The main loop in this thread
  // can stop periodically if needed to service things related to this process.
  //
  // [did we lose some words here?]
  //
  // flag set in the options, so we will wait forever for an exception on
  // 0 our exception port. After we get one exception, we then will use the
  // MACH_RCV_TIMEOUT option with a zero timeout to grab all other current
  // exceptions for our process. After we have received the last pending
  // exception, we will get a timeout which enables us to then notify our main
  // thread that we have an exception bundle available. We then wait for the
  // main thread to tell this exception thread to start trying to get
  // exceptions messages again and we start again with a mach_msg read with
  // infinite timeout.
  //
  // We choose to park a thread on this, rather than polling, because the
  // polling is expensive.  On devices, we need to minimize overhead caused by
  // the process monitor.
  uint32_t num_exceptions_received = 0;
  Status error;
  task_t task = m_task;
  mach_msg_timeout_t periodic_timeout = 0;

#if defined(WITH_SPRINGBOARD) && !defined(WITH_BKS)
  mach_msg_timeout_t watchdog_elapsed = 0;
  mach_msg_timeout_t watchdog_timeout = 60 * 1000;
  ::pid_t pid = (::pid_t)process->GetID();
  CFReleaser<SBSWatchdogAssertionRef> watchdog;

  if (process->ProcessUsingSpringBoard()) {
    // Request a renewal for every 60 seconds if we attached using SpringBoard.
    watchdog.reset(::SBSWatchdogAssertionCreateForPID(nullptr, pid, 60));
    if (log)
      log->Printf("::SBSWatchdogAssertionCreateForPID(NULL, %4.4x, 60) "
                  "=> %p",
                  pid, watchdog.get());

    if (watchdog.get()) {
      ::SBSWatchdogAssertionRenew(watchdog.get());

      CFTimeInterval watchdogRenewalInterval =
          ::SBSWatchdogAssertionGetRenewalInterval(watchdog.get());
      if (log)
        log->Printf("::SBSWatchdogAssertionGetRenewalInterval(%p) => "
                    "%g seconds",
                    watchdog.get(), watchdogRenewalInterval);
      if (watchdogRenewalInterval > 0.0) {
        watchdog_timeout = (mach_msg_timeout_t)watchdogRenewalInterval * 1000;
        if (watchdog_timeout > 3000) {
          // Give us a second to renew our timeout.
          watchdog_timeout -= 1000;
        } else if (watchdog_timeout > 1000) {
          // Give us a quarter of a second to renew our timeout.
          watchdog_timeout -= 250;
        }
      }
    }
    if (periodic_timeout == 0 || periodic_timeout > watchdog_timeout)
      periodic_timeout = watchdog_timeout;
  }
#endif // #if defined (WITH_SPRINGBOARD) && !defined (WITH_BKS)

#ifdef WITH_BKS
  CFReleaser<BKSWatchdogAssertionRef> watchdog;
  if (process->ProcessUsingBackBoard()) {
    ::pid_t pid = process->GetID();
    CFAllocatorRef alloc = kCFAllocatorDefault;
    watchdog.reset(::BKSWatchdogAssertionCreateForPID(alloc, pid));
  }
#endif // #ifdef WITH_BKS

  // Do we want to use a weak pointer to the NativeProcessDarwin here, in which
  // case we can guarantee we don't whack the process monitor if we race
  // between this thread and the main one on shutdown?
  while (IsExceptionPortValid()) {
    ::pthread_testcancel();

    MachException::Message exception_message;

    if (num_exceptions_received > 0) {
      // We don't want a timeout here, just receive as many exceptions as we
      // can since we already have one.  We want to get all currently available
      // exceptions for this task at once.
      error = exception_message.Receive(
          GetExceptionPort(),
          MACH_RCV_MSG | MACH_RCV_INTERRUPT | MACH_RCV_TIMEOUT, 0);
    } else if (periodic_timeout > 0) {
      // We need to stop periodically in this loop, so try and get a mach
      // message with a valid timeout (ms).
      error = exception_message.Receive(GetExceptionPort(),
                                        MACH_RCV_MSG | MACH_RCV_INTERRUPT |
                                            MACH_RCV_TIMEOUT,
                                        periodic_timeout);
    } else {
      // We don't need to parse all current exceptions or stop periodically,
      // just wait for an exception forever.
      error = exception_message.Receive(GetExceptionPort(),
                                        MACH_RCV_MSG | MACH_RCV_INTERRUPT, 0);
    }

    if (error.Success()) {
      // We successfully received an exception.
      if (exception_message.CatchExceptionRaise(task)) {
        ++num_exceptions_received;
        ExceptionMessageReceived(exception_message);
      }
    } else {
      if (error.GetError() == MACH_RCV_INTERRUPTED) {
        // We were interrupted.

        // If we have no task port we should exit this thread, as it implies
        // the inferior went down.
        if (!IsExceptionPortValid()) {
          if (log)
            log->Printf("NativeProcessDarwin::%s(): the inferior "
                        "exception port is no longer valid, "
                        "canceling exception thread...",
                        __FUNCTION__);
          // Should we be setting a process state here?
          break;
        }

        // Make sure the inferior task is still valid.
        if (IsTaskValid()) {
          // Task is still ok.
          if (log)
            log->Printf("NativeProcessDarwin::%s(): interrupted, but "
                        "the inferior task iss till valid, "
                        "continuing...",
                        __FUNCTION__);
          continue;
        } else {
          // The inferior task is no longer valid.  Time to exit as the process
          // has gone away.
          if (log)
            log->Printf("NativeProcessDarwin::%s(): the inferior task "
                        "has exited, and so will we...",
                        __FUNCTION__);
          // Does this race at all with our waitpid()?
          SetState(eStateExited);
          break;
        }
      } else if (error.GetError() == MACH_RCV_TIMED_OUT) {
        // We timed out when waiting for exceptions.

        if (num_exceptions_received > 0) {
          // We were receiving all current exceptions with a timeout of zero.
          // It is time to go back to our normal looping mode.
          num_exceptions_received = 0;

          // Notify our main thread we have a complete exception message bundle
          // available.  Get the possibly updated task port back from the
          // process in case we exec'ed and our task port changed.
          task = ExceptionMessageBundleComplete();

          // In case we use a timeout value when getting exceptions, make sure
          // our task is still valid.
          if (IsTaskValid(task)) {
            // Task is still ok.
            if (log)
              log->Printf("NativeProcessDarwin::%s(): got a timeout, "
                          "continuing...",
                          __FUNCTION__);
            continue;
          } else {
            // The inferior task is no longer valid.  Time to exit as the
            // process has gone away.
            if (log)
              log->Printf("NativeProcessDarwin::%s(): the inferior "
                          "task has exited, and so will we...",
                          __FUNCTION__);
            // Does this race at all with our waitpid()?
            SetState(eStateExited);
            break;
          }
        }

#if defined(WITH_SPRINGBOARD) && !defined(WITH_BKS)
        if (watchdog.get()) {
          watchdog_elapsed += periodic_timeout;
          if (watchdog_elapsed >= watchdog_timeout) {
            if (log)
              log->Printf("SBSWatchdogAssertionRenew(%p)", watchdog.get());
            ::SBSWatchdogAssertionRenew(watchdog.get());
            watchdog_elapsed = 0;
          }
        }
#endif
      } else {
        if (log)
          log->Printf("NativeProcessDarwin::%s(): continuing after "
                      "receiving an unexpected error: %u (%s)",
                      __FUNCTION__, error.GetError(), error.AsCString());
        // TODO: notify of error?
      }
    }
  }

#if defined(WITH_SPRINGBOARD) && !defined(WITH_BKS)
  if (watchdog.get()) {
    // TODO: change SBSWatchdogAssertionRelease to SBSWatchdogAssertionCancel
    // when we
    // all are up and running on systems that support it. The SBS framework has
    // a #define that will forward SBSWatchdogAssertionRelease to
    // SBSWatchdogAssertionCancel for now so it should still build either way.
    DNBLogThreadedIf(LOG_TASK, "::SBSWatchdogAssertionRelease(%p)",
                     watchdog.get());
    ::SBSWatchdogAssertionRelease(watchdog.get());
  }
#endif // #if defined (WITH_SPRINGBOARD) && !defined (WITH_BKS)

  if (log)
    log->Printf("NativeProcessDarwin::%s(%p): thread exiting...", __FUNCTION__,
                this);
  return nullptr;
}

Status NativeProcessDarwin::StartExceptionThread() {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("NativeProcessDarwin::%s() called", __FUNCTION__);

  // Make sure we've looked up the inferior port.
  TaskPortForProcessID(error);

  // Ensure the inferior task is valid.
  if (!IsTaskValid()) {
    error.SetErrorStringWithFormat("cannot start exception thread: "
                                   "task 0x%4.4x is not valid",
                                   m_task);
    return error;
  }

  // Get the mach port for the process monitor.
  mach_port_t task_self = mach_task_self();

  // Allocate an exception port that we will use to track our child process
  auto mach_err = ::mach_port_allocate(task_self, MACH_PORT_RIGHT_RECEIVE,
                                       &m_exception_port);
  error.SetError(mach_err, eErrorTypeMachKernel);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): mach_port_allocate("
                  "task_self=0x%4.4x, MACH_PORT_RIGHT_RECEIVE, "
                  "&m_exception_port) failed: %u (%s)",
                  __FUNCTION__, task_self, error.GetError(), error.AsCString());
    return error;
  }

  // Add the ability to send messages on the new exception port
  mach_err = ::mach_port_insert_right(
      task_self, m_exception_port, m_exception_port, MACH_MSG_TYPE_MAKE_SEND);
  error.SetError(mach_err, eErrorTypeMachKernel);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): mach_port_insert_right("
                  "task_self=0x%4.4x, m_exception_port=0x%4.4x, "
                  "m_exception_port=0x%4.4x, MACH_MSG_TYPE_MAKE_SEND) "
                  "failed: %u (%s)",
                  __FUNCTION__, task_self, m_exception_port, m_exception_port,
                  error.GetError(), error.AsCString());
    return error;
  }

  // Save the original state of the exception ports for our child process.
  error = SaveExceptionPortInfo();
  if (error.Fail() || (m_exc_port_info.mask == 0)) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): SaveExceptionPortInfo() "
                  "failed, cannot install exception handler: %s",
                  __FUNCTION__, error.AsCString());
    return error;
  }

  // Set the ability to get all exceptions on this port.
  mach_err = ::task_set_exception_ports(
      m_task, m_exc_port_info.mask, m_exception_port,
      EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE);
  error.SetError(mach_err, eErrorTypeMachKernel);
  if (error.Fail()) {
    if (log)
      log->Printf("::task_set_exception_ports (task = 0x%4.4x, "
                  "exception_mask = 0x%8.8x, new_port = 0x%4.4x, "
                  "behavior = 0x%8.8x, new_flavor = 0x%8.8x) failed: "
                  "%u (%s)",
                  m_task, m_exc_port_info.mask, m_exception_port,
                  (EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES), THREAD_STATE_NONE,
                  error.GetError(), error.AsCString());
    return error;
  }

  // Create the exception thread.
  auto pthread_err =
      ::pthread_create(&m_exception_thread, nullptr, ExceptionThread, this);
  error.SetError(pthread_err, eErrorTypePOSIX);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failed to create Mach "
                  "exception-handling thread: %u (%s)",
                  __FUNCTION__, error.GetError(), error.AsCString());
  }

  return error;
}

lldb::addr_t
NativeProcessDarwin::GetDYLDAllImageInfosAddress(Status &error) const {
  error.Clear();

  struct hack_task_dyld_info dyld_info;
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  // Make sure that COUNT isn't bigger than our hacked up struct
  // hack_task_dyld_info.  If it is, then make COUNT smaller to match.
  if (count > (sizeof(struct hack_task_dyld_info) / sizeof(natural_t))) {
    count = (sizeof(struct hack_task_dyld_info) / sizeof(natural_t));
  }

  TaskPortForProcessID(error);
  if (error.Fail())
    return LLDB_INVALID_ADDRESS;

  auto mach_err =
      ::task_info(m_task, TASK_DYLD_INFO, (task_info_t)&dyld_info, &count);
  error.SetError(mach_err, eErrorTypeMachKernel);
  if (error.Success()) {
    // We now have the address of the all image infos structure.
    return dyld_info.all_image_info_addr;
  }

  // We don't have it.
  return LLDB_INVALID_ADDRESS;
}

uint32_t NativeProcessDarwin::GetCPUTypeForLocalProcess(::pid_t pid) {
  int mib[CTL_MAXNAME] = {
      0,
  };
  size_t len = CTL_MAXNAME;

  if (::sysctlnametomib("sysctl.proc_cputype", mib, &len))
    return 0;

  mib[len] = pid;
  len++;

  cpu_type_t cpu;
  size_t cpu_len = sizeof(cpu);
  if (::sysctl(mib, static_cast<u_int>(len), &cpu, &cpu_len, 0, 0))
    cpu = 0;
  return cpu;
}

uint32_t NativeProcessDarwin::GetCPUType() const {
  if (m_cpu_type == 0 && m_pid != 0)
    m_cpu_type = GetCPUTypeForLocalProcess(m_pid);
  return m_cpu_type;
}

task_t NativeProcessDarwin::ExceptionMessageBundleComplete() {
  // We have a complete bundle of exceptions for our child process.
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  std::lock_guard<std::recursive_mutex> locker(m_exception_messages_mutex);
  if (log)
    log->Printf("NativeProcessDarwin::%s(): processing %lu exception "
                "messages.",
                __FUNCTION__, m_exception_messages.size());

  if (m_exception_messages.empty()) {
    // Not particularly useful...
    return m_task;
  }

  bool auto_resume = false;
  m_did_exec = false;

  // First check for any SIGTRAP and make sure we didn't exec
  const task_t task = m_task;
  size_t i;
  if (m_pid != 0) {
    bool received_interrupt = false;
    uint32_t num_task_exceptions = 0;
    for (i = 0; i < m_exception_messages.size(); ++i) {
      if (m_exception_messages[i].state.task_port != task) {
        // This is an exception that is not for our inferior, ignore.
        continue;
      }

      // This is an exception for the inferior.
      ++num_task_exceptions;
      const int signo = m_exception_messages[i].state.SoftSignal();
      if (signo == SIGTRAP) {
        // SIGTRAP could mean that we exec'ed. We need to check the
        // dyld all_image_infos.infoArray to see if it is NULL and if so, say
        // that we exec'ed.
        const addr_t aii_addr = GetDYLDAllImageInfosAddress(error);
        if (aii_addr == LLDB_INVALID_ADDRESS)
          break;

        const addr_t info_array_count_addr = aii_addr + 4;
        uint32_t info_array_count = 0;
        size_t bytes_read = 0;
        Status read_error;
        read_error = ReadMemory(info_array_count_addr, // source addr
                                &info_array_count,     // dest addr
                                4,                     // byte count
                                bytes_read);           // #bytes read
        if (read_error.Success() && (bytes_read == 4)) {
          if (info_array_count == 0) {
            // We got the all infos address, and there are zero entries.  We
            // think we exec'd.
            m_did_exec = true;

            // Force the task port to update itself in case the task port
            // changed after exec
            const task_t old_task = m_task;
            const bool force_update = true;
            const task_t new_task = TaskPortForProcessID(error, force_update);
            if (old_task != new_task) {
              if (log)
                log->Printf("exec: inferior task port changed "
                            "from 0x%4.4x to 0x%4.4x",
                            old_task, new_task);
            }
          }
        } else {
          if (log)
            log->Printf("NativeProcessDarwin::%s() warning: "
                        "failed to read all_image_infos."
                        "infoArrayCount from 0x%8.8llx",
                        __FUNCTION__, info_array_count_addr);
        }
      } else if ((m_sent_interrupt_signo != 0) &&
                 (signo == m_sent_interrupt_signo)) {
        // We just received the interrupt that we sent to ourselves.
        received_interrupt = true;
      }
    }

    if (m_did_exec) {
      cpu_type_t process_cpu_type = GetCPUTypeForLocalProcess(m_pid);
      if (m_cpu_type != process_cpu_type) {
        if (log)
          log->Printf("NativeProcessDarwin::%s(): arch changed from "
                      "0x%8.8x to 0x%8.8x",
                      __FUNCTION__, m_cpu_type, process_cpu_type);
        m_cpu_type = process_cpu_type;
        // TODO figure out if we need to do something here.
        // DNBArchProtocol::SetArchitecture (process_cpu_type);
      }
      m_thread_list.Clear();

      // TODO hook up breakpoints.
      // m_breakpoints.DisableAll();
    }

    if (m_sent_interrupt_signo != 0) {
      if (received_interrupt) {
        if (log)
          log->Printf("NativeProcessDarwin::%s(): process "
                      "successfully interrupted with signal %i",
                      __FUNCTION__, m_sent_interrupt_signo);

        // Mark that we received the interrupt signal
        m_sent_interrupt_signo = 0;
        // Now check if we had a case where:
        // 1 - We called NativeProcessDarwin::Interrupt() but we stopped
        //     for another reason.
        // 2 - We called NativeProcessDarwin::Resume() (but still
        //     haven't gotten the interrupt signal).
        // 3 - We are now incorrectly stopped because we are handling
        //     the interrupt signal we missed.
        // 4 - We might need to resume if we stopped only with the
        //     interrupt signal that we never handled.
        if (m_auto_resume_signo != 0) {
          // Only auto_resume if we stopped with _only_ the interrupt signal.
          if (num_task_exceptions == 1) {
            auto_resume = true;
            if (log)
              log->Printf("NativeProcessDarwin::%s(): auto "
                          "resuming due to unhandled interrupt "
                          "signal %i",
                          __FUNCTION__, m_auto_resume_signo);
          }
          m_auto_resume_signo = 0;
        }
      } else {
        if (log)
          log->Printf("NativeProcessDarwin::%s(): didn't get signal "
                      "%i after MachProcess::Interrupt()",
                      __FUNCTION__, m_sent_interrupt_signo);
      }
    }
  }

  // Let all threads recover from stopping and do any clean up based on the
  // previous thread state (if any).
  m_thread_list.ProcessDidStop(*this);

  // Let each thread know of any exceptions
  for (i = 0; i < m_exception_messages.size(); ++i) {
    // Let the thread list forward all exceptions on down to each thread.
    if (m_exception_messages[i].state.task_port == task) {
      // This exception is for our inferior.
      m_thread_list.NotifyException(m_exception_messages[i].state);
    }

    if (log) {
      StreamString stream;
      m_exception_messages[i].Dump(stream);
      stream.Flush();
      log->PutCString(stream.GetString().c_str());
    }
  }

  if (log) {
    StreamString stream;
    m_thread_list.Dump(stream);
    stream.Flush();
    log->PutCString(stream.GetString().c_str());
  }

  bool step_more = false;
  if (m_thread_list.ShouldStop(step_more) && (auto_resume == false)) {
// TODO - need to hook up event system here. !!!!
#if 0
        // Wait for the eEventProcessRunningStateChanged event to be reset
        // before changing state to stopped to avoid race condition with very
        // fast start/stops.
        struct timespec timeout;

        //DNBTimer::OffsetTimeOfDay(&timeout, 0, 250 * 1000);   // Wait for 250 ms
        DNBTimer::OffsetTimeOfDay(&timeout, 1, 0);  // Wait for 250 ms
        m_events.WaitForEventsToReset(eEventProcessRunningStateChanged,
                                      &timeout);
#endif
    SetState(eStateStopped);
  } else {
    // Resume without checking our current state.
    PrivateResume();
  }

  return m_task;
}

void NativeProcessDarwin::StartSTDIOThread() {
  // TODO implement
}

Status NativeProcessDarwin::StartWaitpidThread(MainLoop &main_loop) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  // Strategy: create a thread that sits on waitpid(), waiting for the inferior
  // process to die, reaping it in the process.  Arrange for the thread to have
  // a pipe file descriptor that it can send a byte over when the waitpid
  // completes.  Have the main loop have a read object for the other side of
  // the pipe, and have the callback for the read do the process termination
  // message sending.

  // Create a single-direction communication channel.
  const bool child_inherits = false;
  error = m_waitpid_pipe.CreateNew(child_inherits);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failed to create waitpid "
                  "communication pipe: %s",
                  __FUNCTION__, error.AsCString());
    return error;
  }

  // Hook up the waitpid reader callback.

  // TODO make PipePOSIX derive from IOObject.  This is goofy here.
  const bool transfer_ownership = false;
  auto io_sp = IOObjectSP(
      new File(m_waitpid_pipe.GetReadFileDescriptor(), transfer_ownership));
  m_waitpid_reader_handle = main_loop.RegisterReadObject(
      io_sp, [this](MainLoopBase &) { HandleWaitpidResult(); }, error);

  // Create the thread.
  auto pthread_err =
      ::pthread_create(&m_waitpid_thread, nullptr, WaitpidThread, this);
  error.SetError(pthread_err, eErrorTypePOSIX);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failed to create waitpid "
                  "handling thread: %u (%s)",
                  __FUNCTION__, error.GetError(), error.AsCString());
    return error;
  }

  return error;
}

void *NativeProcessDarwin::WaitpidThread(void *arg) {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (!arg) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): cannot run waitpid "
                  "thread, mandatory process arg was null",
                  __FUNCTION__);
    return nullptr;
  }

  return reinterpret_cast<NativeProcessDarwin *>(arg)->DoWaitpidThread();
}

void NativeProcessDarwin::MaybeRaiseThreadPriority() {
#if defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  struct sched_param thread_param;
  int thread_sched_policy;
  if (pthread_getschedparam(pthread_self(), &thread_sched_policy,
                            &thread_param) == 0) {
    thread_param.sched_priority = 47;
    pthread_setschedparam(pthread_self(), thread_sched_policy, &thread_param);
  }
#endif
}

void *NativeProcessDarwin::DoWaitpidThread() {
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  if (m_pid == LLDB_INVALID_PROCESS_ID) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): inferior process ID is "
                  "not set, cannot waitpid on it",
                  __FUNCTION__);
    return nullptr;
  }

  // Name the thread.
  pthread_setname_np("waitpid thread");

  // Ensure we don't get CPU starved.
  MaybeRaiseThreadPriority();

  Status error;
  int status = -1;

  while (1) {
    // Do a waitpid.
    ::pid_t child_pid = ::waitpid(m_pid, &status, 0);
    if (child_pid < 0)
      error.SetErrorToErrno();
    if (error.Fail()) {
      if (error.GetError() == EINTR) {
        // This is okay, we can keep going.
        if (log)
          log->Printf("NativeProcessDarwin::%s(): waitpid(pid = %" PRIu64
                      ", &status, 0) interrupted, continuing",
                      __FUNCTION__, m_pid);
        continue;
      }

      // This error is not okay, abort.
      if (log)
        log->Printf("NativeProcessDarwin::%s(): waitpid(pid = %" PRIu64
                    ", &status, 0) aborting due to error: %u (%s)",
                    __FUNCTION__, m_pid, error.GetError(), error.AsCString());
      break;
    }

    // Log the successful result.
    if (log)
      log->Printf("NativeProcessDarwin::%s(): waitpid(pid = %" PRIu64
                  ", &status, 0) => %i, status = %i",
                  __FUNCTION__, m_pid, child_pid, status);

    // Handle the result.
    if (WIFSTOPPED(status)) {
      if (log)
        log->Printf("NativeProcessDarwin::%s(): waitpid(pid = %" PRIu64
                    ") received a stop, continuing waitpid() loop",
                    __FUNCTION__, m_pid);
      continue;
    } else // if (WIFEXITED(status) || WIFSIGNALED(status))
    {
      if (log)
        log->Printf("NativeProcessDarwin::%s(pid = %" PRIu64 "): "
                    "waitpid thread is setting exit status for pid = "
                    "%i to %i",
                    __FUNCTION__, m_pid, child_pid, status);

      error = SendInferiorExitStatusToMainLoop(child_pid, status);
      return nullptr;
    }
  }

  // We should never exit as long as our child process is alive.  If we get
  // here, something completely unexpected went wrong and we should exit.
  if (log)
    log->Printf(
        "NativeProcessDarwin::%s(): internal error: waitpid thread "
        "exited out of its main loop in an unexpected way. pid = %" PRIu64
        ". Sending exit status of -1.",
        __FUNCTION__, m_pid);

  error = SendInferiorExitStatusToMainLoop((::pid_t)m_pid, -1);
  return nullptr;
}

Status NativeProcessDarwin::SendInferiorExitStatusToMainLoop(::pid_t pid,
                                                             int status) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  size_t bytes_written = 0;

  // Send the pid.
  error = m_waitpid_pipe.Write(&pid, sizeof(pid), bytes_written);
  if (error.Fail() || (bytes_written < sizeof(pid))) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() - failed to write "
                  "waitpid exiting pid to the pipe.  Client will not "
                  "hear about inferior exit status!",
                  __FUNCTION__);
    return error;
  }

  // Send the status.
  bytes_written = 0;
  error = m_waitpid_pipe.Write(&status, sizeof(status), bytes_written);
  if (error.Fail() || (bytes_written < sizeof(status))) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() - failed to write "
                  "waitpid exit result to the pipe.  Client will not "
                  "hear about inferior exit status!",
                  __FUNCTION__);
  }
  return error;
}

Status NativeProcessDarwin::HandleWaitpidResult() {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  // Read the pid.
  const bool notify_status = true;

  ::pid_t pid = -1;
  size_t bytes_read = 0;
  error = m_waitpid_pipe.Read(&pid, sizeof(pid), bytes_read);
  if (error.Fail() || (bytes_read < sizeof(pid))) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() - failed to read "
                  "waitpid exiting pid from the pipe.  Will notify "
                  "as if parent process died with exit status -1.",
                  __FUNCTION__);
    SetExitStatus(WaitStatus(WaitStatus::Exit, -1), notify_status);
    return error;
  }

  // Read the status.
  int status = -1;
  error = m_waitpid_pipe.Read(&status, sizeof(status), bytes_read);
  if (error.Fail() || (bytes_read < sizeof(status))) {
    if (log)
      log->Printf("NativeProcessDarwin::%s() - failed to read "
                  "waitpid exit status from the pipe.  Will notify "
                  "as if parent process died with exit status -1.",
                  __FUNCTION__);
    SetExitStatus(WaitStatus(WaitStatus::Exit, -1), notify_status);
    return error;
  }

  // Notify the monitor that our state has changed.
  if (log)
    log->Printf("NativeProcessDarwin::%s(): main loop received waitpid "
                "exit status info: pid=%i (%s), status=%i",
                __FUNCTION__, pid,
                (pid == m_pid) ? "the inferior" : "not the inferior", status);

  SetExitStatus(WaitStatus::Decode(status), notify_status);
  return error;
}

task_t NativeProcessDarwin::TaskPortForProcessID(Status &error,
                                                 bool force) const {
  if ((m_task == TASK_NULL) || force) {
    Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
    if (m_pid == LLDB_INVALID_PROCESS_ID) {
      if (log)
        log->Printf("NativeProcessDarwin::%s(): cannot get task due "
                    "to invalid pid",
                    __FUNCTION__);
      return TASK_NULL;
    }

    const uint32_t num_retries = 10;
    const uint32_t usec_interval = 10000;

    mach_port_t task_self = mach_task_self();
    task_t task = TASK_NULL;

    for (uint32_t i = 0; i < num_retries; i++) {
      kern_return_t err = ::task_for_pid(task_self, m_pid, &task);
      if (err == 0) {
        // Succeeded.  Save and return it.
        error.Clear();
        m_task = task;
        log->Printf("NativeProcessDarwin::%s(): ::task_for_pid("
                    "stub_port = 0x%4.4x, pid = %llu, &task) "
                    "succeeded: inferior task port = 0x%4.4x",
                    __FUNCTION__, task_self, m_pid, m_task);
        return m_task;
      } else {
        // Failed to get the task for the inferior process.
        error.SetError(err, eErrorTypeMachKernel);
        if (log) {
          log->Printf("NativeProcessDarwin::%s(): ::task_for_pid("
                      "stub_port = 0x%4.4x, pid = %llu, &task) "
                      "failed, err = 0x%8.8x (%s)",
                      __FUNCTION__, task_self, m_pid, err, error.AsCString());
        }
      }

      // Sleep a bit and try again
      ::usleep(usec_interval);
    }

    // We failed to get the task for the inferior process. Ensure that it is
    // cleared out.
    m_task = TASK_NULL;
  }
  return m_task;
}

void NativeProcessDarwin::AttachToInferior(MainLoop &mainloop, lldb::pid_t pid,
                                           Status &error) {
  error.SetErrorString("TODO: implement");
}

Status NativeProcessDarwin::PrivateResume() {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  std::lock_guard<std::recursive_mutex> locker(m_exception_messages_mutex);
  m_auto_resume_signo = m_sent_interrupt_signo;

  if (log) {
    if (m_auto_resume_signo)
      log->Printf("NativeProcessDarwin::%s(): task 0x%x resuming (with "
                  "unhandled interrupt signal %i)...",
                  __FUNCTION__, m_task, m_auto_resume_signo);
    else
      log->Printf("NativeProcessDarwin::%s(): task 0x%x resuming...",
                  __FUNCTION__, m_task);
  }

  error = ReplyToAllExceptions();
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): aborting, failed to "
                  "reply to exceptions: %s",
                  __FUNCTION__, error.AsCString());
    return error;
  }
  //    bool stepOverBreakInstruction = step;

  // Let the thread prepare to resume and see if any threads want us to step
  // over a breakpoint instruction (ProcessWillResume will modify the value of
  // stepOverBreakInstruction).
  m_thread_list.ProcessWillResume(*this, m_thread_actions);

  // Set our state accordingly
  if (m_thread_actions.NumActionsWithState(eStateStepping))
    SetState(eStateStepping);
  else
    SetState(eStateRunning);

  // Now resume our task.
  error = ResumeTask();
  return error;
}

Status NativeProcessDarwin::ReplyToAllExceptions() {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));

  TaskPortForProcessID(error);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): no task port, aborting",
                  __FUNCTION__);
    return error;
  }

  std::lock_guard<std::recursive_mutex> locker(m_exception_messages_mutex);
  if (m_exception_messages.empty()) {
    // We're done.
    return error;
  }

  size_t index = 0;
  for (auto &message : m_exception_messages) {
    if (log) {
      log->Printf("NativeProcessDarwin::%s(): replying to exception "
                  "%zu...",
                  __FUNCTION__, index++);
    }

    int thread_reply_signal = 0;

    const tid_t tid =
        m_thread_list.GetThreadIDByMachPortNumber(message.state.thread_port);
    const ResumeAction *action = nullptr;
    if (tid != LLDB_INVALID_THREAD_ID)
      action = m_thread_actions.GetActionForThread(tid, false);

    if (action) {
      thread_reply_signal = action->signal;
      if (thread_reply_signal)
        m_thread_actions.SetSignalHandledForThread(tid);
    }

    error = message.Reply(m_pid, m_task, thread_reply_signal);
    if (error.Fail() && log) {
      // We log any error here, but we don't stop the exception response
      // handling.
      log->Printf("NativeProcessDarwin::%s(): failed to reply to "
                  "exception: %s",
                  __FUNCTION__, error.AsCString());
      error.Clear();
    }
  }

  // Erase all exception message as we should have used and replied to them all
  // already.
  m_exception_messages.clear();
  return error;
}

Status NativeProcessDarwin::ResumeTask() {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  TaskPortForProcessID(error);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failed to get task port "
                  "for process when attempting to resume: %s",
                  __FUNCTION__, error.AsCString());
    return error;
  }
  if (m_task == TASK_NULL) {
    error.SetErrorString("task port retrieval succeeded but task port is "
                         "null when attempting to resume the task");
    return error;
  }

  if (log)
    log->Printf("NativeProcessDarwin::%s(): requesting resume of task "
                "0x%4.4x",
                __FUNCTION__, m_task);

  // Get the BasicInfo struct to verify that we're suspended before we try to
  // resume the task.
  struct task_basic_info task_info;
  error = GetTaskBasicInfo(m_task, &task_info);
  if (error.Fail()) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): failed to get task "
                  "BasicInfo when attempting to resume: %s",
                  __FUNCTION__, error.AsCString());
    return error;
  }

  // task_resume isn't counted like task_suspend calls are, so if the task is
  // not suspended, don't try and resume it since it is already running
  if (task_info.suspend_count > 0) {
    auto mach_err = ::task_resume(m_task);
    error.SetError(mach_err, eErrorTypeMachKernel);
    if (log) {
      if (error.Success())
        log->Printf("::task_resume(target_task = 0x%4.4x): success", m_task);
      else
        log->Printf("::task_resume(target_task = 0x%4.4x) error: %s", m_task,
                    error.AsCString());
    }
  } else {
    if (log)
      log->Printf("::task_resume(target_task = 0x%4.4x): ignored, "
                  "already running",
                  m_task);
  }

  return error;
}

bool NativeProcessDarwin::IsTaskValid() const {
  if (m_task == TASK_NULL)
    return false;

  struct task_basic_info task_info;
  return GetTaskBasicInfo(m_task, &task_info).Success();
}

bool NativeProcessDarwin::IsTaskValid(task_t task) const {
  if (task == TASK_NULL)
    return false;

  struct task_basic_info task_info;
  return GetTaskBasicInfo(task, &task_info).Success();
}

mach_port_t NativeProcessDarwin::GetExceptionPort() const {
  return m_exception_port;
}

bool NativeProcessDarwin::IsExceptionPortValid() const {
  return MACH_PORT_VALID(m_exception_port);
}

Status
NativeProcessDarwin::GetTaskBasicInfo(task_t task,
                                      struct task_basic_info *info) const {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  // Validate args.
  if (info == NULL) {
    error.SetErrorStringWithFormat("NativeProcessDarwin::%s(): mandatory "
                                   "info arg is null",
                                   __FUNCTION__);
    return error;
  }

  // Grab the task if we don't already have it.
  if (task == TASK_NULL) {
    error.SetErrorStringWithFormat("NativeProcessDarwin::%s(): given task "
                                   "is invalid",
                                   __FUNCTION__);
  }

  mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
  auto err = ::task_info(m_task, TASK_BASIC_INFO, (task_info_t)info, &count);
  error.SetError(err, eErrorTypeMachKernel);
  if (error.Fail()) {
    if (log)
      log->Printf("::task_info(target_task = 0x%4.4x, "
                  "flavor = TASK_BASIC_INFO, task_info_out => %p, "
                  "task_info_outCnt => %u) failed: %u (%s)",
                  m_task, info, count, error.GetError(), error.AsCString());
    return error;
  }

  Log *verbose_log(
      GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS | LIBLLDB_LOG_VERBOSE));
  if (verbose_log) {
    float user = (float)info->user_time.seconds +
                 (float)info->user_time.microseconds / 1000000.0f;
    float system = (float)info->user_time.seconds +
                   (float)info->user_time.microseconds / 1000000.0f;
    verbose_log->Printf("task_basic_info = { suspend_count = %i, "
                        "virtual_size = 0x%8.8llx, resident_size = "
                        "0x%8.8llx, user_time = %f, system_time = %f }",
                        info->suspend_count, (uint64_t)info->virtual_size,
                        (uint64_t)info->resident_size, user, system);
  }
  return error;
}

Status NativeProcessDarwin::SuspendTask() {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  if (m_task == TASK_NULL) {
    error.SetErrorString("task port is null, cannot suspend task");
    if (log)
      log->Printf("NativeProcessDarwin::%s() failed: %s", __FUNCTION__,
                  error.AsCString());
    return error;
  }

  auto mach_err = ::task_suspend(m_task);
  error.SetError(mach_err, eErrorTypeMachKernel);
  if (error.Fail() && log)
    log->Printf("::task_suspend(target_task = 0x%4.4x)", m_task);

  return error;
}

Status NativeProcessDarwin::Resume(const ResumeActionList &resume_actions) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  if (log)
    log->Printf("NativeProcessDarwin::%s() called", __FUNCTION__);

  if (CanResume()) {
    m_thread_actions = resume_actions;
    error = PrivateResume();
    return error;
  }

  auto state = GetState();
  if (state == eStateRunning) {
    if (log)
      log->Printf("NativeProcessDarwin::%s(): task 0x%x is already "
                  "running, ignoring...",
                  __FUNCTION__, TaskPortForProcessID(error));
    return error;
  }

  // We can't resume from this state.
  error.SetErrorStringWithFormat("task 0x%x has state %s, can't resume",
                                 TaskPortForProcessID(error),
                                 StateAsCString(state));
  return error;
}

Status NativeProcessDarwin::Halt() {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::Detach() {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::Signal(int signo) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::Interrupt() {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::Kill() {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::GetMemoryRegionInfo(lldb::addr_t load_addr,
                                                MemoryRegionInfo &range_info) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::ReadMemory(lldb::addr_t addr, void *buf,
                                       size_t size, size_t &bytes_read) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::ReadMemoryWithoutTrap(lldb::addr_t addr, void *buf,
                                                  size_t size,
                                                  size_t &bytes_read) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::WriteMemory(lldb::addr_t addr, const void *buf,
                                        size_t size, size_t &bytes_written) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::AllocateMemory(size_t size, uint32_t permissions,
                                           lldb::addr_t &addr) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::DeallocateMemory(lldb::addr_t addr) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

lldb::addr_t NativeProcessDarwin::GetSharedLibraryInfoAddress() {
  return LLDB_INVALID_ADDRESS;
}

size_t NativeProcessDarwin::UpdateThreads() { return 0; }

bool NativeProcessDarwin::GetArchitecture(ArchSpec &arch) const {
  return false;
}

Status NativeProcessDarwin::SetBreakpoint(lldb::addr_t addr, uint32_t size,
                                          bool hardware) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

void NativeProcessDarwin::DoStopIDBumped(uint32_t newBumpId) {}

Status NativeProcessDarwin::GetLoadedModuleFileSpec(const char *module_path,
                                                    FileSpec &file_spec) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

Status NativeProcessDarwin::GetFileLoadAddress(const llvm::StringRef &file_name,
                                               lldb::addr_t &load_addr) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}

// -----------------------------------------------------------------
// NativeProcessProtocol protected interface
// -----------------------------------------------------------------
Status NativeProcessDarwin::GetSoftwareBreakpointTrapOpcode(
    size_t trap_opcode_size_hint, size_t &actual_opcode_size,
    const uint8_t *&trap_opcode_bytes) {
  Status error;
  error.SetErrorString("TODO: implement");
  return error;
}
