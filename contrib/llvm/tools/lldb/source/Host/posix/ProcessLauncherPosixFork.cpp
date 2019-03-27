//===-- ProcessLauncherLinux.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/ProcessLauncherPosixFork.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Target/ProcessLaunchInfo.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "llvm/Support/Errno.h"

#include <limits.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sstream>
#include <csignal>

#ifdef __ANDROID__
#include <android/api-level.h>
#define PT_TRACE_ME PTRACE_TRACEME
#endif

#if defined(__ANDROID_API__) && __ANDROID_API__ < 15
#include <linux/personality.h>
#elif defined(__linux__)
#include <sys/personality.h>
#endif

using namespace lldb;
using namespace lldb_private;

static void FixupEnvironment(Environment &env) {
#ifdef __ANDROID__
  // If there is no PATH variable specified inside the environment then set the
  // path to /system/bin. It is required because the default path used by
  // execve() is wrong on android.
  env.try_emplace("PATH", "/system/bin");
#endif
}

static void LLVM_ATTRIBUTE_NORETURN ExitWithError(int error_fd,
                                                  const char *operation) {
  int err = errno;
  llvm::raw_fd_ostream os(error_fd, true);
  os << operation << " failed: " << llvm::sys::StrError(err);
  os.flush();
  _exit(1);
}

static void DisableASLRIfRequested(int error_fd, const ProcessLaunchInfo &info) {
#if defined(__linux__)
  if (info.GetFlags().Test(lldb::eLaunchFlagDisableASLR)) {
    const unsigned long personality_get_current = 0xffffffff;
    int value = personality(personality_get_current);
    if (value == -1)
      ExitWithError(error_fd, "personality get");

    value = personality(ADDR_NO_RANDOMIZE | value);
    if (value == -1)
      ExitWithError(error_fd, "personality set");
  }
#endif
}

static void DupDescriptor(int error_fd, const FileSpec &file_spec, int fd,
                          int flags) {
  int target_fd = ::open(file_spec.GetCString(), flags, 0666);

  if (target_fd == -1)
    ExitWithError(error_fd, "DupDescriptor-open");

  if (target_fd == fd)
    return;

  if (::dup2(target_fd, fd) == -1)
    ExitWithError(error_fd, "DupDescriptor-dup2");

  ::close(target_fd);
  return;
}

static void LLVM_ATTRIBUTE_NORETURN ChildFunc(int error_fd,
                                              const ProcessLaunchInfo &info) {
  if (info.GetFlags().Test(eLaunchFlagLaunchInSeparateProcessGroup)) {
    if (setpgid(0, 0) != 0)
      ExitWithError(error_fd, "setpgid");
  }

  for (size_t i = 0; i < info.GetNumFileActions(); ++i) {
    const FileAction &action = *info.GetFileActionAtIndex(i);
    switch (action.GetAction()) {
    case FileAction::eFileActionClose:
      if (close(action.GetFD()) != 0)
        ExitWithError(error_fd, "close");
      break;
    case FileAction::eFileActionDuplicate:
      if (dup2(action.GetFD(), action.GetActionArgument()) == -1)
        ExitWithError(error_fd, "dup2");
      break;
    case FileAction::eFileActionOpen:
      DupDescriptor(error_fd, action.GetFileSpec(), action.GetFD(),
                    action.GetActionArgument());
      break;
    case FileAction::eFileActionNone:
      break;
    }
  }

  const char **argv = info.GetArguments().GetConstArgumentVector();

  // Change working directory
  if (info.GetWorkingDirectory() &&
      0 != ::chdir(info.GetWorkingDirectory().GetCString()))
    ExitWithError(error_fd, "chdir");

  DisableASLRIfRequested(error_fd, info);
  Environment env = info.GetEnvironment();
  FixupEnvironment(env);
  Environment::Envp envp = env.getEnvp();

  // Clear the signal mask to prevent the child from being affected by any
  // masking done by the parent.
  sigset_t set;
  if (sigemptyset(&set) != 0 ||
      pthread_sigmask(SIG_SETMASK, &set, nullptr) != 0)
    ExitWithError(error_fd, "pthread_sigmask");

  if (info.GetFlags().Test(eLaunchFlagDebug)) {
    // Do not inherit setgid powers.
    if (setgid(getgid()) != 0)
      ExitWithError(error_fd, "setgid");

    // HACK:
    // Close everything besides stdin, stdout, and stderr that has no file
    // action to avoid leaking. Only do this when debugging, as elsewhere we
    // actually rely on passing open descriptors to child processes.
    for (int fd = 3; fd < sysconf(_SC_OPEN_MAX); ++fd)
      if (!info.GetFileActionForFD(fd) && fd != error_fd)
        close(fd);

    // Start tracing this child that is about to exec.
    if (ptrace(PT_TRACE_ME, 0, nullptr, 0) == -1)
      ExitWithError(error_fd, "ptrace");
  }

  // Execute.  We should never return...
  execve(argv[0], const_cast<char *const *>(argv), envp);

#if defined(__linux__)
  if (errno == ETXTBSY) {
    // On android M and earlier we can get this error because the adb daemon
    // can hold a write handle on the executable even after it has finished
    // uploading it. This state lasts only a short time and happens only when
    // there are many concurrent adb commands being issued, such as when
    // running the test suite. (The file remains open when someone does an "adb
    // shell" command in the fork() child before it has had a chance to exec.)
    // Since this state should clear up quickly, wait a while and then give it
    // one more go.
    usleep(50000);
    execve(argv[0], const_cast<char *const *>(argv), envp);
  }
#endif

  // ...unless exec fails.  In which case we definitely need to end the child
  // here.
  ExitWithError(error_fd, "execve");
}

HostProcess
ProcessLauncherPosixFork::LaunchProcess(const ProcessLaunchInfo &launch_info,
                                        Status &error) {
  char exe_path[PATH_MAX];
  launch_info.GetExecutableFile().GetPath(exe_path, sizeof(exe_path));

  // A pipe used by the child process to report errors.
  PipePosix pipe;
  const bool child_processes_inherit = false;
  error = pipe.CreateNew(child_processes_inherit);
  if (error.Fail())
    return HostProcess();

  ::pid_t pid = ::fork();
  if (pid == -1) {
    // Fork failed
    error.SetErrorStringWithFormatv("Fork failed with error message: {0}",
                                    llvm::sys::StrError());
    return HostProcess(LLDB_INVALID_PROCESS_ID);
  }
  if (pid == 0) {
    // child process
    pipe.CloseReadFileDescriptor();
    ChildFunc(pipe.ReleaseWriteFileDescriptor(), launch_info);
  }

  // parent process

  pipe.CloseWriteFileDescriptor();
  char buf[1000];
  int r = read(pipe.GetReadFileDescriptor(), buf, sizeof buf);

  if (r == 0)
    return HostProcess(pid); // No error. We're done.

  error.SetErrorString(buf);

  waitpid(pid, nullptr, 0);

  return HostProcess();
}
