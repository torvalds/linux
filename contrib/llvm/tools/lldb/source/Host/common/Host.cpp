//===-- Host.cpp ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// C includes
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#ifndef _WIN32
#include <dlfcn.h>
#include <grp.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#endif

#if defined(__linux__) || defined(__FreeBSD__) ||                              \
    defined(__FreeBSD_kernel__) || defined(__APPLE__) ||                       \
    defined(__NetBSD__) || defined(__OpenBSD__)
#if !defined(__ANDROID__)
#include <spawn.h>
#endif
#include <sys/syscall.h>
#include <sys/wait.h>
#endif

#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif

#if defined(__NetBSD__)
#include <lwp.h>
#endif

#include <csignal>

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/MonitoringProcessLauncher.h"
#include "lldb/Host/ProcessLauncher.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/posix/ConnectionFileDescriptorPosix.h"
#include "lldb/Target/FileAction.h"
#include "lldb/Target/ProcessLaunchInfo.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/DataBufferLLVM.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-forward.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"

#if defined(_WIN32)
#include "lldb/Host/windows/ConnectionGenericFileWindows.h"
#include "lldb/Host/windows/ProcessLauncherWindows.h"
#else
#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#endif

#if defined(__APPLE__)
#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

extern "C" {
int __pthread_chdir(const char *path);
int __pthread_fchdir(int fildes);
}

#endif

using namespace lldb;
using namespace lldb_private;

#if !defined(__APPLE__) && !defined(_WIN32)
struct MonitorInfo {
  lldb::pid_t pid; // The process ID to monitor
  Host::MonitorChildProcessCallback
      callback; // The callback function to call when "pid" exits or signals
  bool monitor_signals; // If true, call the callback when "pid" gets signaled.
};

static thread_result_t MonitorChildProcessThreadFunction(void *arg);

HostThread Host::StartMonitoringChildProcess(
    const Host::MonitorChildProcessCallback &callback, lldb::pid_t pid,
    bool monitor_signals) {
  MonitorInfo *info_ptr = new MonitorInfo();

  info_ptr->pid = pid;
  info_ptr->callback = callback;
  info_ptr->monitor_signals = monitor_signals;

  char thread_name[256];
  ::snprintf(thread_name, sizeof(thread_name),
             "<lldb.host.wait4(pid=%" PRIu64 ")>", pid);
  return ThreadLauncher::LaunchThread(
      thread_name, MonitorChildProcessThreadFunction, info_ptr, NULL);
}

#ifndef __linux__
//------------------------------------------------------------------
// Scoped class that will disable thread canceling when it is constructed, and
// exception safely restore the previous value it when it goes out of scope.
//------------------------------------------------------------------
class ScopedPThreadCancelDisabler {
public:
  ScopedPThreadCancelDisabler() {
    // Disable the ability for this thread to be cancelled
    int err = ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &m_old_state);
    if (err != 0)
      m_old_state = -1;
  }

  ~ScopedPThreadCancelDisabler() {
    // Restore the ability for this thread to be cancelled to what it
    // previously was.
    if (m_old_state != -1)
      ::pthread_setcancelstate(m_old_state, 0);
  }

private:
  int m_old_state; // Save the old cancelability state.
};
#endif // __linux__

#ifdef __linux__
#if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8))
static __thread volatile sig_atomic_t g_usr1_called;
#else
static thread_local volatile sig_atomic_t g_usr1_called;
#endif

static void SigUsr1Handler(int) { g_usr1_called = 1; }
#endif // __linux__

static bool CheckForMonitorCancellation() {
#ifdef __linux__
  if (g_usr1_called) {
    g_usr1_called = 0;
    return true;
  }
#else
  ::pthread_testcancel();
#endif
  return false;
}

static thread_result_t MonitorChildProcessThreadFunction(void *arg) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  const char *function = __FUNCTION__;
  if (log)
    log->Printf("%s (arg = %p) thread starting...", function, arg);

  MonitorInfo *info = (MonitorInfo *)arg;

  const Host::MonitorChildProcessCallback callback = info->callback;
  const bool monitor_signals = info->monitor_signals;

  assert(info->pid <= UINT32_MAX);
  const ::pid_t pid = monitor_signals ? -1 * getpgid(info->pid) : info->pid;

  delete info;

  int status = -1;
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__OpenBSD__)
#define __WALL 0
#endif
  const int options = __WALL;

#ifdef __linux__
  // This signal is only used to interrupt the thread from waitpid
  struct sigaction sigUsr1Action;
  memset(&sigUsr1Action, 0, sizeof(sigUsr1Action));
  sigUsr1Action.sa_handler = SigUsr1Handler;
  ::sigaction(SIGUSR1, &sigUsr1Action, nullptr);
#endif // __linux__

  while (1) {
    log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS);
    if (log)
      log->Printf("%s ::waitpid (pid = %" PRIi32 ", &status, options = %i)...",
                  function, pid, options);

    if (CheckForMonitorCancellation())
      break;

    // Get signals from all children with same process group of pid
    const ::pid_t wait_pid = ::waitpid(pid, &status, options);

    if (CheckForMonitorCancellation())
      break;

    if (wait_pid == -1) {
      if (errno == EINTR)
        continue;
      else {
        LLDB_LOG(log,
                 "arg = {0}, thread exiting because waitpid failed ({1})...",
                 arg, llvm::sys::StrError());
        break;
      }
    } else if (wait_pid > 0) {
      bool exited = false;
      int signal = 0;
      int exit_status = 0;
      const char *status_cstr = NULL;
      if (WIFSTOPPED(status)) {
        signal = WSTOPSIG(status);
        status_cstr = "STOPPED";
      } else if (WIFEXITED(status)) {
        exit_status = WEXITSTATUS(status);
        status_cstr = "EXITED";
        exited = true;
      } else if (WIFSIGNALED(status)) {
        signal = WTERMSIG(status);
        status_cstr = "SIGNALED";
        if (wait_pid == abs(pid)) {
          exited = true;
          exit_status = -1;
        }
      } else {
        status_cstr = "(\?\?\?)";
      }

      // Scope for pthread_cancel_disabler
      {
#ifndef __linux__
        ScopedPThreadCancelDisabler pthread_cancel_disabler;
#endif

        log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS);
        if (log)
          log->Printf("%s ::waitpid (pid = %" PRIi32
                      ", &status, options = %i) => pid = %" PRIi32
                      ", status = 0x%8.8x (%s), signal = %i, exit_state = %i",
                      function, pid, options, wait_pid, status, status_cstr,
                      signal, exit_status);

        if (exited || (signal != 0 && monitor_signals)) {
          bool callback_return = false;
          if (callback)
            callback_return = callback(wait_pid, exited, signal, exit_status);

          // If our process exited, then this thread should exit
          if (exited && wait_pid == abs(pid)) {
            if (log)
              log->Printf("%s (arg = %p) thread exiting because pid received "
                          "exit signal...",
                          __FUNCTION__, arg);
            break;
          }
          // If the callback returns true, it means this process should exit
          if (callback_return) {
            if (log)
              log->Printf("%s (arg = %p) thread exiting because callback "
                          "returned true...",
                          __FUNCTION__, arg);
            break;
          }
        }
      }
    }
  }

  log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS);
  if (log)
    log->Printf("%s (arg = %p) thread exiting...", __FUNCTION__, arg);

  return NULL;
}

#endif // #if !defined (__APPLE__) && !defined (_WIN32)

#if !defined(__APPLE__)

void Host::SystemLog(SystemLogType type, const char *format, va_list args) {
  vfprintf(stderr, format, args);
}

#endif

void Host::SystemLog(SystemLogType type, const char *format, ...) {
  va_list args;
  va_start(args, format);
  SystemLog(type, format, args);
  va_end(args);
}

lldb::pid_t Host::GetCurrentProcessID() { return ::getpid(); }

#ifndef _WIN32

lldb::thread_t Host::GetCurrentThread() {
  return lldb::thread_t(pthread_self());
}

const char *Host::GetSignalAsCString(int signo) {
  switch (signo) {
  case SIGHUP:
    return "SIGHUP"; // 1    hangup
  case SIGINT:
    return "SIGINT"; // 2    interrupt
  case SIGQUIT:
    return "SIGQUIT"; // 3    quit
  case SIGILL:
    return "SIGILL"; // 4    illegal instruction (not reset when caught)
  case SIGTRAP:
    return "SIGTRAP"; // 5    trace trap (not reset when caught)
  case SIGABRT:
    return "SIGABRT"; // 6    abort()
#if defined(SIGPOLL)
#if !defined(SIGIO) || (SIGPOLL != SIGIO)
  // Under some GNU/Linux, SIGPOLL and SIGIO are the same. Causing the build to
  // fail with 'multiple define cases with same value'
  case SIGPOLL:
    return "SIGPOLL"; // 7    pollable event ([XSR] generated, not supported)
#endif
#endif
#if defined(SIGEMT)
  case SIGEMT:
    return "SIGEMT"; // 7    EMT instruction
#endif
  case SIGFPE:
    return "SIGFPE"; // 8    floating point exception
  case SIGKILL:
    return "SIGKILL"; // 9    kill (cannot be caught or ignored)
  case SIGBUS:
    return "SIGBUS"; // 10    bus error
  case SIGSEGV:
    return "SIGSEGV"; // 11    segmentation violation
  case SIGSYS:
    return "SIGSYS"; // 12    bad argument to system call
  case SIGPIPE:
    return "SIGPIPE"; // 13    write on a pipe with no one to read it
  case SIGALRM:
    return "SIGALRM"; // 14    alarm clock
  case SIGTERM:
    return "SIGTERM"; // 15    software termination signal from kill
  case SIGURG:
    return "SIGURG"; // 16    urgent condition on IO channel
  case SIGSTOP:
    return "SIGSTOP"; // 17    sendable stop signal not from tty
  case SIGTSTP:
    return "SIGTSTP"; // 18    stop signal from tty
  case SIGCONT:
    return "SIGCONT"; // 19    continue a stopped process
  case SIGCHLD:
    return "SIGCHLD"; // 20    to parent on child stop or exit
  case SIGTTIN:
    return "SIGTTIN"; // 21    to readers pgrp upon background tty read
  case SIGTTOU:
    return "SIGTTOU"; // 22    like TTIN for output if (tp->t_local&LTOSTOP)
#if defined(SIGIO)
  case SIGIO:
    return "SIGIO"; // 23    input/output possible signal
#endif
  case SIGXCPU:
    return "SIGXCPU"; // 24    exceeded CPU time limit
  case SIGXFSZ:
    return "SIGXFSZ"; // 25    exceeded file size limit
  case SIGVTALRM:
    return "SIGVTALRM"; // 26    virtual time alarm
  case SIGPROF:
    return "SIGPROF"; // 27    profiling time alarm
#if defined(SIGWINCH)
  case SIGWINCH:
    return "SIGWINCH"; // 28    window size changes
#endif
#if defined(SIGINFO)
  case SIGINFO:
    return "SIGINFO"; // 29    information request
#endif
  case SIGUSR1:
    return "SIGUSR1"; // 30    user defined signal 1
  case SIGUSR2:
    return "SIGUSR2"; // 31    user defined signal 2
  default:
    break;
  }
  return NULL;
}

#endif

#if !defined(__APPLE__) // see Host.mm

bool Host::GetBundleDirectory(const FileSpec &file, FileSpec &bundle) {
  bundle.Clear();
  return false;
}

bool Host::ResolveExecutableInBundle(FileSpec &file) { return false; }
#endif

#ifndef _WIN32

FileSpec Host::GetModuleFileSpecForHostAddress(const void *host_addr) {
  FileSpec module_filespec;
#if !defined(__ANDROID__)
  Dl_info info;
  if (::dladdr(host_addr, &info)) {
    if (info.dli_fname) {
      module_filespec.SetFile(info.dli_fname, FileSpec::Style::native);
      FileSystem::Instance().Resolve(module_filespec);
    }
  }
#endif
  return module_filespec;
}

#endif

#if !defined(__linux__)
bool Host::FindProcessThreads(const lldb::pid_t pid, TidMap &tids_to_attach) {
  return false;
}
#endif

struct ShellInfo {
  ShellInfo()
      : process_reaped(false), pid(LLDB_INVALID_PROCESS_ID), signo(-1),
        status(-1) {}

  lldb_private::Predicate<bool> process_reaped;
  lldb::pid_t pid;
  int signo;
  int status;
};

static bool
MonitorShellCommand(std::shared_ptr<ShellInfo> shell_info, lldb::pid_t pid,
                    bool exited, // True if the process did exit
                    int signo,   // Zero for no signal
                    int status)  // Exit value of process if signal is zero
{
  shell_info->pid = pid;
  shell_info->signo = signo;
  shell_info->status = status;
  // Let the thread running Host::RunShellCommand() know that the process
  // exited and that ShellInfo has been filled in by broadcasting to it
  shell_info->process_reaped.SetValue(true, eBroadcastAlways);
  return true;
}

Status Host::RunShellCommand(const char *command, const FileSpec &working_dir,
                             int *status_ptr, int *signo_ptr,
                             std::string *command_output_ptr,
                             const Timeout<std::micro> &timeout,
                             bool run_in_default_shell) {
  return RunShellCommand(Args(command), working_dir, status_ptr, signo_ptr,
                         command_output_ptr, timeout, run_in_default_shell);
}

Status Host::RunShellCommand(const Args &args, const FileSpec &working_dir,
                             int *status_ptr, int *signo_ptr,
                             std::string *command_output_ptr,
                             const Timeout<std::micro> &timeout,
                             bool run_in_default_shell) {
  Status error;
  ProcessLaunchInfo launch_info;
  launch_info.SetArchitecture(HostInfo::GetArchitecture());
  if (run_in_default_shell) {
    // Run the command in a shell
    launch_info.SetShell(HostInfo::GetDefaultShell());
    launch_info.GetArguments().AppendArguments(args);
    const bool localhost = true;
    const bool will_debug = false;
    const bool first_arg_is_full_shell_command = false;
    launch_info.ConvertArgumentsForLaunchingInShell(
        error, localhost, will_debug, first_arg_is_full_shell_command, 0);
  } else {
    // No shell, just run it
    const bool first_arg_is_executable = true;
    launch_info.SetArguments(args, first_arg_is_executable);
  }

  if (working_dir)
    launch_info.SetWorkingDirectory(working_dir);
  llvm::SmallString<64> output_file_path;

  if (command_output_ptr) {
    // Create a temporary file to get the stdout/stderr and redirect the output
    // of the command into this file. We will later read this file if all goes
    // well and fill the data into "command_output_ptr"
    if (FileSpec tmpdir_file_spec = HostInfo::GetProcessTempDir()) {
      tmpdir_file_spec.AppendPathComponent("lldb-shell-output.%%%%%%");
      llvm::sys::fs::createUniqueFile(tmpdir_file_spec.GetPath(),
                                      output_file_path);
    } else {
      llvm::sys::fs::createTemporaryFile("lldb-shell-output.%%%%%%", "",
                                         output_file_path);
    }
  }

  FileSpec output_file_spec(output_file_path.c_str());

  launch_info.AppendSuppressFileAction(STDIN_FILENO, true, false);
  if (output_file_spec) {
    launch_info.AppendOpenFileAction(STDOUT_FILENO, output_file_spec, false,
                                     true);
    launch_info.AppendDuplicateFileAction(STDOUT_FILENO, STDERR_FILENO);
  } else {
    launch_info.AppendSuppressFileAction(STDOUT_FILENO, false, true);
    launch_info.AppendSuppressFileAction(STDERR_FILENO, false, true);
  }

  std::shared_ptr<ShellInfo> shell_info_sp(new ShellInfo());
  const bool monitor_signals = false;
  launch_info.SetMonitorProcessCallback(
      std::bind(MonitorShellCommand, shell_info_sp, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3,
                std::placeholders::_4),
      monitor_signals);

  error = LaunchProcess(launch_info);
  const lldb::pid_t pid = launch_info.GetProcessID();

  if (error.Success() && pid == LLDB_INVALID_PROCESS_ID)
    error.SetErrorString("failed to get process ID");

  if (error.Success()) {
    if (!shell_info_sp->process_reaped.WaitForValueEqualTo(true, timeout)) {
      error.SetErrorString("timed out waiting for shell command to complete");

      // Kill the process since it didn't complete within the timeout specified
      Kill(pid, SIGKILL);
      // Wait for the monitor callback to get the message
      shell_info_sp->process_reaped.WaitForValueEqualTo(
          true, std::chrono::seconds(1));
    } else {
      if (status_ptr)
        *status_ptr = shell_info_sp->status;

      if (signo_ptr)
        *signo_ptr = shell_info_sp->signo;

      if (command_output_ptr) {
        command_output_ptr->clear();
        uint64_t file_size =
            FileSystem::Instance().GetByteSize(output_file_spec);
        if (file_size > 0) {
          if (file_size > command_output_ptr->max_size()) {
            error.SetErrorStringWithFormat(
                "shell command output is too large to fit into a std::string");
          } else {
            auto Buffer =
                FileSystem::Instance().CreateDataBuffer(output_file_spec);
            if (error.Success())
              command_output_ptr->assign(Buffer->GetChars(),
                                         Buffer->GetByteSize());
          }
        }
      }
    }
  }

  llvm::sys::fs::remove(output_file_spec.GetPath());
  return error;
}

// The functions below implement process launching for non-Apple-based
// platforms
#if !defined(__APPLE__)
Status Host::LaunchProcess(ProcessLaunchInfo &launch_info) {
  std::unique_ptr<ProcessLauncher> delegate_launcher;
#if defined(_WIN32)
  delegate_launcher.reset(new ProcessLauncherWindows());
#else
  delegate_launcher.reset(new ProcessLauncherPosixFork());
#endif
  MonitoringProcessLauncher launcher(std::move(delegate_launcher));

  Status error;
  HostProcess process = launcher.LaunchProcess(launch_info, error);

  // TODO(zturner): It would be better if the entire HostProcess were returned
  // instead of writing it into this structure.
  launch_info.SetProcessID(process.GetProcessId());

  return error;
}
#endif // !defined(__APPLE__)

#ifndef _WIN32
void Host::Kill(lldb::pid_t pid, int signo) { ::kill(pid, signo); }

#endif

#if !defined(__APPLE__)
bool Host::OpenFileInExternalEditor(const FileSpec &file_spec,
                                    uint32_t line_no) {
  return false;
}

#endif

const UnixSignalsSP &Host::GetUnixSignals() {
  static const auto s_unix_signals_sp =
      UnixSignals::Create(HostInfo::GetArchitecture());
  return s_unix_signals_sp;
}

std::unique_ptr<Connection> Host::CreateDefaultConnection(llvm::StringRef url) {
#if defined(_WIN32)
  if (url.startswith("file://"))
    return std::unique_ptr<Connection>(new ConnectionGenericFile());
#endif
  return std::unique_ptr<Connection>(new ConnectionFileDescriptor());
}

#if defined(LLVM_ON_UNIX)
WaitStatus WaitStatus::Decode(int wstatus) {
  if (WIFEXITED(wstatus))
    return {Exit, uint8_t(WEXITSTATUS(wstatus))};
  else if (WIFSIGNALED(wstatus))
    return {Signal, uint8_t(WTERMSIG(wstatus))};
  else if (WIFSTOPPED(wstatus))
    return {Stop, uint8_t(WSTOPSIG(wstatus))};
  llvm_unreachable("Unknown wait status");
}
#endif

void llvm::format_provider<WaitStatus>::format(const WaitStatus &WS,
                                               raw_ostream &OS,
                                               StringRef Options) {
  if (Options == "g") {
    char type;
    switch (WS.type) {
    case WaitStatus::Exit:
      type = 'W';
      break;
    case WaitStatus::Signal:
      type = 'X';
      break;
    case WaitStatus::Stop:
      type = 'S';
      break;
    }
    OS << formatv("{0}{1:x-2}", type, WS.status);
    return;
  }

  assert(Options.empty());
  const char *desc;
  switch(WS.type) {
  case WaitStatus::Exit:
    desc = "Exited with status";
    break;
  case WaitStatus::Signal:
    desc = "Killed by signal";
    break;
  case WaitStatus::Stop:
    desc = "Stopped by signal";
    break;
  }
  OS << desc << " " << int(WS.status);
}
