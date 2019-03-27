//===-- DarwinProcessLauncher.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//
//  DarwinProcessLauncher.cpp
//  lldb
//
//  Created by Todd Fiala on 8/30/16.
//
//

#include "DarwinProcessLauncher.h"

// C includes
#include <spawn.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

// LLDB includes
#include "lldb/lldb-enumerations.h"

#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Target/ProcessLaunchInfo.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/Support/Errno.h"

#include "CFBundle.h"
#include "CFString.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_darwin;
using namespace lldb_private::darwin_process_launcher;

namespace {
static LaunchFlavor g_launch_flavor = LaunchFlavor::Default;
}

namespace lldb_private {
namespace darwin_process_launcher {

static uint32_t GetCPUTypeForLocalProcess(::pid_t pid) {
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

static bool ResolveExecutablePath(const char *path, char *resolved_path,
                                  size_t resolved_path_size) {
  if (path == NULL || path[0] == '\0')
    return false;

  char max_path[PATH_MAX];
  std::string result;
  CFString::GlobPath(path, result);

  if (result.empty())
    result = path;

  struct stat path_stat;
  if (::stat(path, &path_stat) == 0) {
    if ((path_stat.st_mode & S_IFMT) == S_IFDIR) {
      CFBundle bundle(path);
      CFReleaser<CFURLRef> url(bundle.CopyExecutableURL());
      if (url.get()) {
        if (::CFURLGetFileSystemRepresentation(
                url.get(), true, (UInt8 *)resolved_path, resolved_path_size))
          return true;
      }
    }
  }

  if (realpath(path, max_path)) {
    // Found the path relatively...
    ::strncpy(resolved_path, max_path, resolved_path_size);
    return strlen(resolved_path) + 1 < resolved_path_size;
  } else {
    // Not a relative path, check the PATH environment variable if the
    const char *PATH = getenv("PATH");
    if (PATH) {
      const char *curr_path_start = PATH;
      const char *curr_path_end;
      while (curr_path_start && *curr_path_start) {
        curr_path_end = strchr(curr_path_start, ':');
        if (curr_path_end == NULL) {
          result.assign(curr_path_start);
          curr_path_start = NULL;
        } else if (curr_path_end > curr_path_start) {
          size_t len = curr_path_end - curr_path_start;
          result.assign(curr_path_start, len);
          curr_path_start += len + 1;
        } else
          break;

        result += '/';
        result += path;
        struct stat s;
        if (stat(result.c_str(), &s) == 0) {
          ::strncpy(resolved_path, result.c_str(), resolved_path_size);
          return result.size() + 1 < resolved_path_size;
        }
      }
    }
  }
  return false;
}

// TODO check if we have a general purpose fork and exec.  We may be
// able to get rid of this entirely.
static Status ForkChildForPTraceDebugging(const char *path, char const *argv[],
                                          char const *envp[], ::pid_t *pid,
                                          int *pty_fd) {
  Status error;
  if (!path || !argv || !envp || !pid || !pty_fd) {
    error.SetErrorString("invalid arguments");
    return error;
  }

  // Use a fork that ties the child process's stdin/out/err to a pseudo
  // terminal so we can read it in our MachProcess::STDIOThread as unbuffered
  // io.
  PseudoTerminal pty;
  char fork_error[256];
  memset(fork_error, 0, sizeof(fork_error));
  *pid = static_cast<::pid_t>(pty.Fork(fork_error, sizeof(fork_error)));
  if (*pid < 0) {
    //--------------------------------------------------------------
    // Status during fork.
    //--------------------------------------------------------------
    *pid = static_cast<::pid_t>(LLDB_INVALID_PROCESS_ID);
    error.SetErrorStringWithFormat("%s(): fork failed: %s", __FUNCTION__,
                                   fork_error);
    return error;
  } else if (pid == 0) {
    //--------------------------------------------------------------
    // Child process
    //--------------------------------------------------------------

    // Debug this process.
    ::ptrace(PT_TRACE_ME, 0, 0, 0);

    // Get BSD signals as mach exceptions.
    ::ptrace(PT_SIGEXC, 0, 0, 0);

    // If our parent is setgid, lets make sure we don't inherit those extra
    // powers due to nepotism.
    if (::setgid(getgid()) == 0) {
      // Let the child have its own process group. We need to execute this call
      // in both the child and parent to avoid a race condition between the two
      // processes.

      // Set the child process group to match its pid.
      ::setpgid(0, 0);

      // Sleep a bit to before the exec call.
      ::sleep(1);

      // Turn this process into the given executable.
      ::execv(path, (char *const *)argv);
    }
    // Exit with error code. Child process should have taken over in above exec
    // call and if the exec fails it will exit the child process below.
    ::exit(127);
  } else {
    //--------------------------------------------------------------
    // Parent process
    //--------------------------------------------------------------
    // Let the child have its own process group. We need to execute this call
    // in both the child and parent to avoid a race condition between the two
    // processes.

    // Set the child process group to match its pid
    ::setpgid(*pid, *pid);
    if (pty_fd) {
      // Release our master pty file descriptor so the pty class doesn't close
      // it and so we can continue to use it in our STDIO thread
      *pty_fd = pty.ReleaseMasterFileDescriptor();
    }
  }
  return error;
}

static Status
CreatePosixSpawnFileAction(const FileAction &action,
                           posix_spawn_file_actions_t *file_actions) {
  Status error;

  // Log it.
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log) {
    StreamString stream;
    stream.PutCString("converting file action for posix_spawn(): ");
    action.Dump(stream);
    stream.Flush();
    log->PutCString(stream.GetString().c_str());
  }

  // Validate args.
  if (!file_actions) {
    error.SetErrorString("mandatory file_actions arg is null");
    return error;
  }

  // Build the posix file action.
  switch (action.GetAction()) {
  case FileAction::eFileActionOpen: {
    const int error_code = ::posix_spawn_file_actions_addopen(
        file_actions, action.GetFD(), action.GetPath(),
        action.GetActionArgument(), 0);
    if (error_code != 0) {
      error.SetError(error_code, eErrorTypePOSIX);
      return error;
    }
    break;
  }

  case FileAction::eFileActionClose: {
    const int error_code =
        ::posix_spawn_file_actions_addclose(file_actions, action.GetFD());
    if (error_code != 0) {
      error.SetError(error_code, eErrorTypePOSIX);
      return error;
    }
    break;
  }

  case FileAction::eFileActionDuplicate: {
    const int error_code = ::posix_spawn_file_actions_adddup2(
        file_actions, action.GetFD(), action.GetActionArgument());
    if (error_code != 0) {
      error.SetError(error_code, eErrorTypePOSIX);
      return error;
    }
    break;
  }

  case FileAction::eFileActionNone:
  default:
    if (log)
      log->Printf("%s(): unsupported file action %u", __FUNCTION__,
                  action.GetAction());
    break;
  }

  return error;
}

static Status PosixSpawnChildForPTraceDebugging(const char *path,
                                                ProcessLaunchInfo &launch_info,
                                                ::pid_t *pid,
                                                cpu_type_t *actual_cpu_type) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  if (!pid) {
    error.SetErrorStringWithFormat("%s(): pid arg cannot be null",
                                   __FUNCTION__);
    return error;
  }

  posix_spawnattr_t attr;
  short flags;
  if (log) {
    StreamString stream;
    stream.Printf("%s(path='%s',...)\n", __FUNCTION__, path);
    launch_info.Dump(stream, nullptr);
    stream.Flush();
    log->PutCString(stream.GetString().c_str());
  }

  int error_code;
  if ((error_code = ::posix_spawnattr_init(&attr)) != 0) {
    if (log)
      log->Printf("::posix_spawnattr_init(&attr) failed");
    error.SetError(error_code, eErrorTypePOSIX);
    return error;
  }

  // Ensure we clean up the spawnattr structure however we exit this function.
  std::unique_ptr<posix_spawnattr_t, int (*)(posix_spawnattr_t *)> spawnattr_up(
      &attr, ::posix_spawnattr_destroy);

  flags = POSIX_SPAWN_START_SUSPENDED | POSIX_SPAWN_SETSIGDEF |
          POSIX_SPAWN_SETSIGMASK;
  if (launch_info.GetFlags().Test(eLaunchFlagDisableASLR))
    flags |= _POSIX_SPAWN_DISABLE_ASLR;

  sigset_t no_signals;
  sigset_t all_signals;
  sigemptyset(&no_signals);
  sigfillset(&all_signals);
  ::posix_spawnattr_setsigmask(&attr, &no_signals);
  ::posix_spawnattr_setsigdefault(&attr, &all_signals);

  if ((error_code = ::posix_spawnattr_setflags(&attr, flags)) != 0) {
    LLDB_LOG(log,
             "::posix_spawnattr_setflags(&attr, "
             "POSIX_SPAWN_START_SUSPENDED{0}) failed: {1}",
             flags & _POSIX_SPAWN_DISABLE_ASLR ? " | _POSIX_SPAWN_DISABLE_ASLR"
                                               : "",
             llvm::sys::StrError(error_code));
    error.SetError(error_code, eErrorTypePOSIX);
    return error;
  }

#if !defined(__arm__)

  // We don't need to do this for ARM, and we really shouldn't now that we have
  // multiple CPU subtypes and no posix_spawnattr call that allows us to set
  // which CPU subtype to launch...
  cpu_type_t desired_cpu_type = launch_info.GetArchitecture().GetMachOCPUType();
  if (desired_cpu_type != LLDB_INVALID_CPUTYPE) {
    size_t ocount = 0;
    error_code =
        ::posix_spawnattr_setbinpref_np(&attr, 1, &desired_cpu_type, &ocount);
    if (error_code != 0) {
      LLDB_LOG(log,
               "::posix_spawnattr_setbinpref_np(&attr, 1, "
               "cpu_type = {0:x8}, count => {1}): {2}",
               desired_cpu_type, ocount, llvm::sys::StrError(error_code));
      error.SetError(error_code, eErrorTypePOSIX);
      return error;
    }
    if (ocount != 1) {
      error.SetErrorStringWithFormat("posix_spawnattr_setbinpref_np "
                                     "did not set the expected number "
                                     "of cpu_type entries: expected 1 "
                                     "but was %zu",
                                     ocount);
      return error;
    }
  }
#endif

  posix_spawn_file_actions_t file_actions;
  if ((error_code = ::posix_spawn_file_actions_init(&file_actions)) != 0) {
    LLDB_LOG(log, "::posix_spawn_file_actions_init(&file_actions) failed: {0}",
             llvm::sys::StrError(error_code));
    error.SetError(error_code, eErrorTypePOSIX);
    return error;
  }

  // Ensure we clean up file actions however we exit this.  When the
  // file_actions_up below goes out of scope, we'll get our file action
  // cleanup.
  std::unique_ptr<posix_spawn_file_actions_t,
                  int (*)(posix_spawn_file_actions_t *)>
      file_actions_up(&file_actions, ::posix_spawn_file_actions_destroy);

  // We assume the caller has setup the file actions appropriately.  We are not
  // in the business of figuring out what we really need here. lldb-server will
  // have already called FinalizeFileActions() as well to button these up
  // properly.
  const size_t num_actions = launch_info.GetNumFileActions();
  for (size_t action_index = 0; action_index < num_actions; ++action_index) {
    const FileAction *const action =
        launch_info.GetFileActionAtIndex(action_index);
    if (!action)
      continue;

    error = CreatePosixSpawnFileAction(*action, &file_actions);
    if (!error.Success()) {
      if (log)
        log->Printf("%s(): error converting FileAction to posix_spawn "
                    "file action: %s",
                    __FUNCTION__, error.AsCString());
      return error;
    }
  }

  // TODO: Verify if we can set the working directory back immediately
  // after the posix_spawnp call without creating a race condition???
  const char *const working_directory =
      launch_info.GetWorkingDirectory().GetCString();
  if (working_directory && working_directory[0])
    ::chdir(working_directory);

  auto argv = launch_info.GetArguments().GetArgumentVector();
  auto envp = launch_info.GetEnvironmentEntries().GetArgumentVector();
  error_code = ::posix_spawnp(pid, path, &file_actions, &attr,
                              (char *const *)argv, (char *const *)envp);
  if (error_code != 0) {
    LLDB_LOG(log,
             "::posix_spawnp(pid => {0}, path = '{1}', file_actions "
             "= {2}, attr = {3}, argv = {4}, envp = {5}) failed: {6}",
             pid, path, &file_actions, &attr, argv, envp,
             llvm::sys::StrError(error_code));
    error.SetError(error_code, eErrorTypePOSIX);
    return error;
  }

  // Validate we got a pid.
  if (pid == LLDB_INVALID_PROCESS_ID) {
    error.SetErrorString("posix_spawn() did not indicate a failure but it "
                         "failed to return a pid, aborting.");
    return error;
  }

  if (actual_cpu_type) {
    *actual_cpu_type = GetCPUTypeForLocalProcess(*pid);
    if (log)
      log->Printf("%s(): cpu type for launched process pid=%i: "
                  "cpu_type=0x%8.8x",
                  __FUNCTION__, *pid, *actual_cpu_type);
  }

  return error;
}

Status LaunchInferior(ProcessLaunchInfo &launch_info, int *pty_master_fd,
                      LaunchFlavor *launch_flavor) {
  Status error;
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS));

  if (!launch_flavor) {
    error.SetErrorString("mandatory launch_flavor field was null");
    return error;
  }

  if (log) {
    StreamString stream;
    stream.Printf("NativeProcessDarwin::%s(): launching with the "
                  "following launch info:",
                  __FUNCTION__);
    launch_info.Dump(stream, nullptr);
    stream.Flush();
    log->PutCString(stream.GetString().c_str());
  }

  // Retrieve the binary name given to us.
  char given_path[PATH_MAX];
  given_path[0] = '\0';
  launch_info.GetExecutableFile().GetPath(given_path, sizeof(given_path));

  // Determine the manner in which we'll launch.
  *launch_flavor = g_launch_flavor;
  if (*launch_flavor == LaunchFlavor::Default) {
    // Our default launch method is posix spawn
    *launch_flavor = LaunchFlavor::PosixSpawn;
#if defined WITH_FBS
    // Check if we have an app bundle, if so launch using BackBoard Services.
    if (strstr(given_path, ".app")) {
      *launch_flavor = eLaunchFlavorFBS;
    }
#elif defined WITH_BKS
    // Check if we have an app bundle, if so launch using BackBoard Services.
    if (strstr(given_path, ".app")) {
      *launch_flavor = eLaunchFlavorBKS;
    }
#elif defined WITH_SPRINGBOARD
    // Check if we have an app bundle, if so launch using SpringBoard.
    if (strstr(given_path, ".app")) {
      *launch_flavor = eLaunchFlavorSpringBoard;
    }
#endif
  }

  // Attempt to resolve the binary name to an absolute path.
  char resolved_path[PATH_MAX];
  resolved_path[0] = '\0';

  if (log)
    log->Printf("%s(): attempting to resolve given binary path: \"%s\"",
                __FUNCTION__, given_path);

  // If we fail to resolve the path to our executable, then just use what we
  // were given and hope for the best
  if (!ResolveExecutablePath(given_path, resolved_path,
                             sizeof(resolved_path))) {
    if (log)
      log->Printf("%s(): failed to resolve binary path, using "
                  "what was given verbatim and hoping for the best",
                  __FUNCTION__);
    ::strncpy(resolved_path, given_path, sizeof(resolved_path));
  } else {
    if (log)
      log->Printf("%s(): resolved given binary path to: \"%s\"", __FUNCTION__,
                  resolved_path);
  }

  char launch_err_str[PATH_MAX];
  launch_err_str[0] = '\0';

  // TODO figure out how to handle QSetProcessEvent
  // const char *process_event = ctx.GetProcessEvent();

  // Ensure the binary is there.
  struct stat path_stat;
  if (::stat(resolved_path, &path_stat) == -1) {
    error.SetErrorToErrno();
    return error;
  }

  // Fork a child process for debugging
  // state_callback(eStateLaunching);

  const auto argv = launch_info.GetArguments().GetConstArgumentVector();
  const auto envp =
      launch_info.GetEnvironmentEntries().GetConstArgumentVector();

  switch (*launch_flavor) {
  case LaunchFlavor::ForkExec: {
    ::pid_t pid = LLDB_INVALID_PROCESS_ID;
    error = ForkChildForPTraceDebugging(resolved_path, argv, envp, &pid,
                                        pty_master_fd);
    if (error.Success()) {
      launch_info.SetProcessID(static_cast<lldb::pid_t>(pid));
    } else {
      // Reset any variables that might have been set during a failed launch
      // attempt.
      if (pty_master_fd)
        *pty_master_fd = -1;

      // We're done.
      return error;
    }
  } break;

#ifdef WITH_FBS
  case LaunchFlavor::FBS: {
    const char *app_ext = strstr(path, ".app");
    if (app_ext && (app_ext[4] == '\0' || app_ext[4] == '/')) {
      std::string app_bundle_path(path, app_ext + strlen(".app"));
      m_flags |= eMachProcessFlagsUsingFBS;
      if (BoardServiceLaunchForDebug(app_bundle_path.c_str(), argv, envp,
                                     no_stdio, disable_aslr, event_data,
                                     launch_err) != 0)
        return m_pid; // A successful SBLaunchForDebug() returns and assigns a
                      // non-zero m_pid.
      else
        break; // We tried a FBS launch, but didn't succeed lets get out
    }
  } break;
#endif

#ifdef WITH_BKS
  case LaunchFlavor::BKS: {
    const char *app_ext = strstr(path, ".app");
    if (app_ext && (app_ext[4] == '\0' || app_ext[4] == '/')) {
      std::string app_bundle_path(path, app_ext + strlen(".app"));
      m_flags |= eMachProcessFlagsUsingBKS;
      if (BoardServiceLaunchForDebug(app_bundle_path.c_str(), argv, envp,
                                     no_stdio, disable_aslr, event_data,
                                     launch_err) != 0)
        return m_pid; // A successful SBLaunchForDebug() returns and assigns a
                      // non-zero m_pid.
      else
        break; // We tried a BKS launch, but didn't succeed lets get out
    }
  } break;
#endif

#ifdef WITH_SPRINGBOARD
  case LaunchFlavor::SpringBoard: {
    //  .../whatever.app/whatever ?
    //  Or .../com.apple.whatever.app/whatever -- be careful of ".app" in
    //  "com.apple.whatever" here
    const char *app_ext = strstr(path, ".app/");
    if (app_ext == NULL) {
      // .../whatever.app ?
      int len = strlen(path);
      if (len > 5) {
        if (strcmp(path + len - 4, ".app") == 0) {
          app_ext = path + len - 4;
        }
      }
    }
    if (app_ext) {
      std::string app_bundle_path(path, app_ext + strlen(".app"));
      if (SBLaunchForDebug(app_bundle_path.c_str(), argv, envp, no_stdio,
                           disable_aslr, launch_err) != 0)
        return m_pid; // A successful SBLaunchForDebug() returns and assigns a
                      // non-zero m_pid.
      else
        break; // We tried a springboard launch, but didn't succeed lets get out
    }
  } break;
#endif

  case LaunchFlavor::PosixSpawn: {
    ::pid_t pid = LLDB_INVALID_PROCESS_ID;

    // Retrieve paths for stdin/stdout/stderr.
    cpu_type_t actual_cpu_type = 0;
    error = PosixSpawnChildForPTraceDebugging(resolved_path, launch_info, &pid,
                                              &actual_cpu_type);
    if (error.Success()) {
      launch_info.SetProcessID(static_cast<lldb::pid_t>(pid));
      if (pty_master_fd)
        *pty_master_fd = launch_info.GetPTY().ReleaseMasterFileDescriptor();
    } else {
      // Reset any variables that might have been set during a failed launch
      // attempt.
      if (pty_master_fd)
        *pty_master_fd = -1;

      // We're done.
      return error;
    }
    break;
  }

  default:
    // Invalid launch flavor.
    error.SetErrorStringWithFormat("NativeProcessDarwin::%s(): unknown "
                                   "launch flavor %d",
                                   __FUNCTION__, (int)*launch_flavor);
    return error;
  }

  if (launch_info.GetProcessID() == LLDB_INVALID_PROCESS_ID) {
    // If we don't have a valid process ID and no one has set the error, then
    // return a generic error.
    if (error.Success())
      error.SetErrorStringWithFormat("%s(): failed to launch, no reason "
                                     "specified",
                                     __FUNCTION__);
  }

  // We're done with the launch side of the operation.
  return error;
}
}
} // namespaces
