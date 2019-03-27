//===- llvm/Support/Unix/Program.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Unix specific portion of the Program class.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_POSIX_SPAWN
#include <spawn.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define USE_NSGETENVIRON 1
#else
#define USE_NSGETENVIRON 0
#endif

#if !USE_NSGETENVIRON
  extern char **environ;
#else
#include <crt_externs.h> // _NSGetEnviron
#endif
#endif

namespace llvm {

using namespace sys;

ProcessInfo::ProcessInfo() : Pid(0), ReturnCode(0) {}

ErrorOr<std::string> sys::findProgramByName(StringRef Name,
                                            ArrayRef<StringRef> Paths) {
  assert(!Name.empty() && "Must have a name!");
  // Use the given path verbatim if it contains any slashes; this matches
  // the behavior of sh(1) and friends.
  if (Name.find('/') != StringRef::npos)
    return std::string(Name);

  SmallVector<StringRef, 16> EnvironmentPaths;
  if (Paths.empty())
    if (const char *PathEnv = std::getenv("PATH")) {
      SplitString(PathEnv, EnvironmentPaths, ":");
      Paths = EnvironmentPaths;
    }

  for (auto Path : Paths) {
    if (Path.empty())
      continue;

    // Check to see if this first directory contains the executable...
    SmallString<128> FilePath(Path);
    sys::path::append(FilePath, Name);
    if (sys::fs::can_execute(FilePath.c_str()))
      return std::string(FilePath.str()); // Found the executable!
  }
  return errc::no_such_file_or_directory;
}

static bool RedirectIO(Optional<StringRef> Path, int FD, std::string* ErrMsg) {
  if (!Path) // Noop
    return false;
  std::string File;
  if (Path->empty())
    // Redirect empty paths to /dev/null
    File = "/dev/null";
  else
    File = *Path;

  // Open the file
  int InFD = open(File.c_str(), FD == 0 ? O_RDONLY : O_WRONLY|O_CREAT, 0666);
  if (InFD == -1) {
    MakeErrMsg(ErrMsg, "Cannot open file '" + File + "' for "
              + (FD == 0 ? "input" : "output"));
    return true;
  }

  // Install it as the requested FD
  if (dup2(InFD, FD) == -1) {
    MakeErrMsg(ErrMsg, "Cannot dup2");
    close(InFD);
    return true;
  }
  close(InFD);      // Close the original FD
  return false;
}

#ifdef HAVE_POSIX_SPAWN
static bool RedirectIO_PS(const std::string *Path, int FD, std::string *ErrMsg,
                          posix_spawn_file_actions_t *FileActions) {
  if (!Path) // Noop
    return false;
  const char *File;
  if (Path->empty())
    // Redirect empty paths to /dev/null
    File = "/dev/null";
  else
    File = Path->c_str();

  if (int Err = posix_spawn_file_actions_addopen(
          FileActions, FD, File,
          FD == 0 ? O_RDONLY : O_WRONLY | O_CREAT, 0666))
    return MakeErrMsg(ErrMsg, "Cannot dup2", Err);
  return false;
}
#endif

static void TimeOutHandler(int Sig) {
}

static void SetMemoryLimits(unsigned size) {
#if HAVE_SYS_RESOURCE_H && HAVE_GETRLIMIT && HAVE_SETRLIMIT
  struct rlimit r;
  __typeof__ (r.rlim_cur) limit = (__typeof__ (r.rlim_cur)) (size) * 1048576;

  // Heap size
  getrlimit (RLIMIT_DATA, &r);
  r.rlim_cur = limit;
  setrlimit (RLIMIT_DATA, &r);
#ifdef RLIMIT_RSS
  // Resident set size.
  getrlimit (RLIMIT_RSS, &r);
  r.rlim_cur = limit;
  setrlimit (RLIMIT_RSS, &r);
#endif
#endif
}

}

static std::vector<const char *>
toNullTerminatedCStringArray(ArrayRef<StringRef> Strings, StringSaver &Saver) {
  std::vector<const char *> Result;
  for (StringRef S : Strings)
    Result.push_back(Saver.save(S).data());
  Result.push_back(nullptr);
  return Result;
}

static bool Execute(ProcessInfo &PI, StringRef Program,
                    ArrayRef<StringRef> Args, Optional<ArrayRef<StringRef>> Env,
                    ArrayRef<Optional<StringRef>> Redirects,
                    unsigned MemoryLimit, std::string *ErrMsg) {
  if (!llvm::sys::fs::exists(Program)) {
    if (ErrMsg)
      *ErrMsg = std::string("Executable \"") + Program.str() +
                std::string("\" doesn't exist!");
    return false;
  }

  BumpPtrAllocator Allocator;
  StringSaver Saver(Allocator);
  std::vector<const char *> ArgVector, EnvVector;
  const char **Argv = nullptr;
  const char **Envp = nullptr;
  ArgVector = toNullTerminatedCStringArray(Args, Saver);
  Argv = ArgVector.data();
  if (Env) {
    EnvVector = toNullTerminatedCStringArray(*Env, Saver);
    Envp = EnvVector.data();
  }

  // If this OS has posix_spawn and there is no memory limit being implied, use
  // posix_spawn.  It is more efficient than fork/exec.
#ifdef HAVE_POSIX_SPAWN
  if (MemoryLimit == 0) {
    posix_spawn_file_actions_t FileActionsStore;
    posix_spawn_file_actions_t *FileActions = nullptr;

    // If we call posix_spawn_file_actions_addopen we have to make sure the
    // c strings we pass to it stay alive until the call to posix_spawn,
    // so we copy any StringRefs into this variable.
    std::string RedirectsStorage[3];

    if (!Redirects.empty()) {
      assert(Redirects.size() == 3);
      std::string *RedirectsStr[3] = {nullptr, nullptr, nullptr};
      for (int I = 0; I < 3; ++I) {
        if (Redirects[I]) {
          RedirectsStorage[I] = *Redirects[I];
          RedirectsStr[I] = &RedirectsStorage[I];
        }
      }

      FileActions = &FileActionsStore;
      posix_spawn_file_actions_init(FileActions);

      // Redirect stdin/stdout.
      if (RedirectIO_PS(RedirectsStr[0], 0, ErrMsg, FileActions) ||
          RedirectIO_PS(RedirectsStr[1], 1, ErrMsg, FileActions))
        return false;
      if (!Redirects[1] || !Redirects[2] || *Redirects[1] != *Redirects[2]) {
        // Just redirect stderr
        if (RedirectIO_PS(RedirectsStr[2], 2, ErrMsg, FileActions))
          return false;
      } else {
        // If stdout and stderr should go to the same place, redirect stderr
        // to the FD already open for stdout.
        if (int Err = posix_spawn_file_actions_adddup2(FileActions, 1, 2))
          return !MakeErrMsg(ErrMsg, "Can't redirect stderr to stdout", Err);
      }
    }

    if (!Envp)
#if !USE_NSGETENVIRON
      Envp = const_cast<const char **>(environ);
#else
      // environ is missing in dylibs.
      Envp = const_cast<const char **>(*_NSGetEnviron());
#endif

    // Explicitly initialized to prevent what appears to be a valgrind false
    // positive.
    pid_t PID = 0;
    int Err = posix_spawn(&PID, Program.str().c_str(), FileActions,
                          /*attrp*/ nullptr, const_cast<char **>(Argv),
                          const_cast<char **>(Envp));

    if (FileActions)
      posix_spawn_file_actions_destroy(FileActions);

    if (Err)
     return !MakeErrMsg(ErrMsg, "posix_spawn failed", Err);

    PI.Pid = PID;
    PI.Process = PID;

    return true;
  }
#endif

  // Create a child process.
  int child = fork();
  switch (child) {
    // An error occurred:  Return to the caller.
    case -1:
      MakeErrMsg(ErrMsg, "Couldn't fork");
      return false;

    // Child process: Execute the program.
    case 0: {
      // Redirect file descriptors...
      if (!Redirects.empty()) {
        // Redirect stdin
        if (RedirectIO(Redirects[0], 0, ErrMsg)) { return false; }
        // Redirect stdout
        if (RedirectIO(Redirects[1], 1, ErrMsg)) { return false; }
        if (Redirects[1] && Redirects[2] && *Redirects[1] == *Redirects[2]) {
          // If stdout and stderr should go to the same place, redirect stderr
          // to the FD already open for stdout.
          if (-1 == dup2(1,2)) {
            MakeErrMsg(ErrMsg, "Can't redirect stderr to stdout");
            return false;
          }
        } else {
          // Just redirect stderr
          if (RedirectIO(Redirects[2], 2, ErrMsg)) { return false; }
        }
      }

      // Set memory limits
      if (MemoryLimit!=0) {
        SetMemoryLimits(MemoryLimit);
      }

      // Execute!
      std::string PathStr = Program;
      if (Envp != nullptr)
        execve(PathStr.c_str(), const_cast<char **>(Argv),
               const_cast<char **>(Envp));
      else
        execv(PathStr.c_str(), const_cast<char **>(Argv));
      // If the execve() failed, we should exit. Follow Unix protocol and
      // return 127 if the executable was not found, and 126 otherwise.
      // Use _exit rather than exit so that atexit functions and static
      // object destructors cloned from the parent process aren't
      // redundantly run, and so that any data buffered in stdio buffers
      // cloned from the parent aren't redundantly written out.
      _exit(errno == ENOENT ? 127 : 126);
    }

    // Parent process: Break out of the switch to do our processing.
    default:
      break;
  }

  PI.Pid = child;
  PI.Process = child;

  return true;
}

namespace llvm {

ProcessInfo sys::Wait(const ProcessInfo &PI, unsigned SecondsToWait,
                      bool WaitUntilTerminates, std::string *ErrMsg) {
  struct sigaction Act, Old;
  assert(PI.Pid && "invalid pid to wait on, process not started?");

  int WaitPidOptions = 0;
  pid_t ChildPid = PI.Pid;
  if (WaitUntilTerminates) {
    SecondsToWait = 0;
  } else if (SecondsToWait) {
    // Install a timeout handler.  The handler itself does nothing, but the
    // simple fact of having a handler at all causes the wait below to return
    // with EINTR, unlike if we used SIG_IGN.
    memset(&Act, 0, sizeof(Act));
    Act.sa_handler = TimeOutHandler;
    sigemptyset(&Act.sa_mask);
    sigaction(SIGALRM, &Act, &Old);
    alarm(SecondsToWait);
  } else if (SecondsToWait == 0)
    WaitPidOptions = WNOHANG;

  // Parent process: Wait for the child process to terminate.
  int status;
  ProcessInfo WaitResult;

  do {
    WaitResult.Pid = waitpid(ChildPid, &status, WaitPidOptions);
  } while (WaitUntilTerminates && WaitResult.Pid == -1 && errno == EINTR);

  if (WaitResult.Pid != PI.Pid) {
    if (WaitResult.Pid == 0) {
      // Non-blocking wait.
      return WaitResult;
    } else {
      if (SecondsToWait && errno == EINTR) {
        // Kill the child.
        kill(PI.Pid, SIGKILL);

        // Turn off the alarm and restore the signal handler
        alarm(0);
        sigaction(SIGALRM, &Old, nullptr);

        // Wait for child to die
        if (wait(&status) != ChildPid)
          MakeErrMsg(ErrMsg, "Child timed out but wouldn't die");
        else
          MakeErrMsg(ErrMsg, "Child timed out", 0);

        WaitResult.ReturnCode = -2; // Timeout detected
        return WaitResult;
      } else if (errno != EINTR) {
        MakeErrMsg(ErrMsg, "Error waiting for child process");
        WaitResult.ReturnCode = -1;
        return WaitResult;
      }
    }
  }

  // We exited normally without timeout, so turn off the timer.
  if (SecondsToWait && !WaitUntilTerminates) {
    alarm(0);
    sigaction(SIGALRM, &Old, nullptr);
  }

  // Return the proper exit status. Detect error conditions
  // so we can return -1 for them and set ErrMsg informatively.
  int result = 0;
  if (WIFEXITED(status)) {
    result = WEXITSTATUS(status);
    WaitResult.ReturnCode = result;

    if (result == 127) {
      if (ErrMsg)
        *ErrMsg = llvm::sys::StrError(ENOENT);
      WaitResult.ReturnCode = -1;
      return WaitResult;
    }
    if (result == 126) {
      if (ErrMsg)
        *ErrMsg = "Program could not be executed";
      WaitResult.ReturnCode = -1;
      return WaitResult;
    }
  } else if (WIFSIGNALED(status)) {
    if (ErrMsg) {
      *ErrMsg = strsignal(WTERMSIG(status));
#ifdef WCOREDUMP
      if (WCOREDUMP(status))
        *ErrMsg += " (core dumped)";
#endif
    }
    // Return a special value to indicate that the process received an unhandled
    // signal during execution as opposed to failing to execute.
    WaitResult.ReturnCode = -2;
  }
  return WaitResult;
}

std::error_code sys::ChangeStdinToBinary() {
  // Do nothing, as Unix doesn't differentiate between text and binary.
  return std::error_code();
}

std::error_code sys::ChangeStdoutToBinary() {
  // Do nothing, as Unix doesn't differentiate between text and binary.
  return std::error_code();
}

std::error_code
llvm::sys::writeFileWithEncoding(StringRef FileName, StringRef Contents,
                                 WindowsEncodingMethod Encoding /*unused*/) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(FileName, EC, llvm::sys::fs::OpenFlags::F_Text);

  if (EC)
    return EC;

  OS << Contents;

  if (OS.has_error())
    return make_error_code(errc::io_error);

  return EC;
}

bool llvm::sys::commandLineFitsWithinSystemLimits(StringRef Program,
                                                  ArrayRef<StringRef> Args) {
  static long ArgMax = sysconf(_SC_ARG_MAX);
  // POSIX requires that _POSIX_ARG_MAX is 4096, which is the lowest possible
  // value for ARG_MAX on a POSIX compliant system.
  static long ArgMin = _POSIX_ARG_MAX;

  // This the same baseline used by xargs.
  long EffectiveArgMax = 128 * 1024;

  if (EffectiveArgMax > ArgMax)
    EffectiveArgMax = ArgMax;
  else if (EffectiveArgMax < ArgMin)
    EffectiveArgMax = ArgMin;

  // System says no practical limit.
  if (ArgMax == -1)
    return true;

  // Conservatively account for space required by environment variables.
  long HalfArgMax = EffectiveArgMax / 2;

  size_t ArgLength = Program.size() + 1;
  for (StringRef Arg : Args) {
    // Ensure that we do not exceed the MAX_ARG_STRLEN constant on Linux, which
    // does not have a constant unlike what the man pages would have you
    // believe. Since this limit is pretty high, perform the check
    // unconditionally rather than trying to be aggressive and limiting it to
    // Linux only.
    if (Arg.size() >= (32 * 4096))
      return false;

    ArgLength += Arg.size() + 1;
    if (ArgLength > size_t(HalfArgMax)) {
      return false;
    }
  }

  return true;
}
}
