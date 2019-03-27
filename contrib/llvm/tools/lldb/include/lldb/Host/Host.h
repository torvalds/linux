//===-- Host.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOST_H
#define LLDB_HOST_HOST_H

#include "lldb/Host/File.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Environment.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-private.h"
#include <cerrno>
#include <map>
#include <stdarg.h>
#include <string>
#include <type_traits>

namespace lldb_private {

class FileAction;
class ProcessLaunchInfo;

//----------------------------------------------------------------------
// Exit Type for inferior processes
//----------------------------------------------------------------------
struct WaitStatus {
  enum Type : uint8_t {
    Exit,   // The status represents the return code from normal
            // program exit (i.e. WIFEXITED() was true)
    Signal, // The status represents the signal number that caused
            // the program to exit (i.e. WIFSIGNALED() was true)
    Stop,   // The status represents the signal number that caused the
            // program to stop (i.e. WIFSTOPPED() was true)
  };

  Type type;
  uint8_t status;

  WaitStatus(Type type, uint8_t status) : type(type), status(status) {}

  static WaitStatus Decode(int wstatus);
};

inline bool operator==(WaitStatus a, WaitStatus b) {
  return a.type == b.type && a.status == b.status;
}

inline bool operator!=(WaitStatus a, WaitStatus b) { return !(a == b); }

//----------------------------------------------------------------------
/// @class Host Host.h "lldb/Host/Host.h"
/// A class that provides host computer information.
///
/// Host is a class that answers information about the host operating system.
//----------------------------------------------------------------------
class Host {
public:
  typedef std::function<bool(
      lldb::pid_t pid, bool exited,
      int signal,  // Zero for no signal
      int status)> // Exit value of process if signal is zero
      MonitorChildProcessCallback;

  //------------------------------------------------------------------
  /// Start monitoring a child process.
  ///
  /// Allows easy monitoring of child processes. \a callback will be called
  /// when the child process exits or if it gets a signal. The callback will
  /// only be called with signals if \a monitor_signals is \b true. \a
  /// callback will usually be called from another thread so the callback
  /// function must be thread safe.
  ///
  /// When the callback gets called, the return value indicates if monitoring
  /// should stop. If \b true is returned from \a callback the information
  /// will be removed. If \b false is returned then monitoring will continue.
  /// If the child process exits, the monitoring will automatically stop after
  /// the callback returned regardless of the callback return value.
  ///
  /// @param[in] callback
  ///     A function callback to call when a child receives a signal
  ///     (if \a monitor_signals is true) or a child exits.
  ///
  /// @param[in] pid
  ///     The process ID of a child process to monitor, -1 for all
  ///     processes.
  ///
  /// @param[in] monitor_signals
  ///     If \b true the callback will get called when the child
  ///     process gets a signal. If \b false, the callback will only
  ///     get called if the child process exits.
  ///
  /// @return
  ///     A thread handle that can be used to cancel the thread that
  ///     was spawned to monitor \a pid.
  ///
  /// @see static void Host::StopMonitoringChildProcess (uint32_t)
  //------------------------------------------------------------------
  static HostThread
  StartMonitoringChildProcess(const MonitorChildProcessCallback &callback,
                              lldb::pid_t pid, bool monitor_signals);

  enum SystemLogType { eSystemLogWarning, eSystemLogError };

  static void SystemLog(SystemLogType type, const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  static void SystemLog(SystemLogType type, const char *format, va_list args);

  //------------------------------------------------------------------
  /// Get the process ID for the calling process.
  ///
  /// @return
  ///     The process ID for the current process.
  //------------------------------------------------------------------
  static lldb::pid_t GetCurrentProcessID();

  static void Kill(lldb::pid_t pid, int signo);

  //------------------------------------------------------------------
  /// Get the thread token (the one returned by ThreadCreate when the thread
  /// was created) for the calling thread in the current process.
  ///
  /// @return
  ///     The thread token for the calling thread in the current process.
  //------------------------------------------------------------------
  static lldb::thread_t GetCurrentThread();

  static const char *GetSignalAsCString(int signo);

  //------------------------------------------------------------------
  /// Given an address in the current process (the process that is running the
  /// LLDB code), return the name of the module that it comes from. This can
  /// be useful when you need to know the path to the shared library that your
  /// code is running in for loading resources that are relative to your
  /// binary.
  ///
  /// @param[in] host_addr
  ///     The pointer to some code in the current process.
  ///
  /// @return
  ///     \b A file spec with the module that contains \a host_addr,
  ///     which may be invalid if \a host_addr doesn't fall into
  ///     any valid module address range.
  //------------------------------------------------------------------
  static FileSpec GetModuleFileSpecForHostAddress(const void *host_addr);

  //------------------------------------------------------------------
  /// If you have an executable that is in a bundle and want to get back to
  /// the bundle directory from the path itself, this function will change a
  /// path to a file within a bundle to the bundle directory itself.
  ///
  /// @param[in] file
  ///     A file spec that might point to a file in a bundle.
  ///
  /// @param[out] bundle_directory
  ///     An object will be filled in with the bundle directory for
  ///     the bundle when \b true is returned. Otherwise \a file is
  ///     left untouched and \b false is returned.
  ///
  /// @return
  ///     \b true if \a file was resolved in \a bundle_directory,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  static bool GetBundleDirectory(const FileSpec &file,
                                 FileSpec &bundle_directory);

  //------------------------------------------------------------------
  /// When executable files may live within a directory, where the directory
  /// represents an executable bundle (like the MacOSX app bundles), then
  /// locate the executable within the containing bundle.
  ///
  /// @param[in,out] file
  ///     A file spec that currently points to the bundle that will
  ///     be filled in with the executable path within the bundle
  ///     if \b true is returned. Otherwise \a file is left untouched.
  ///
  /// @return
  ///     \b true if \a file was resolved, \b false if this function
  ///     was not able to resolve the path.
  //------------------------------------------------------------------
  static bool ResolveExecutableInBundle(FileSpec &file);

  static uint32_t FindProcesses(const ProcessInstanceInfoMatch &match_info,
                                ProcessInstanceInfoList &proc_infos);

  typedef std::map<lldb::pid_t, bool> TidMap;
  typedef std::pair<lldb::pid_t, bool> TidPair;
  static bool FindProcessThreads(const lldb::pid_t pid, TidMap &tids_to_attach);

  static bool GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &proc_info);

  static const lldb::UnixSignalsSP &GetUnixSignals();

  /// Launch the process specified in launch_info. The monitoring callback in
  /// launch_info must be set, and it will be called when the process
  /// terminates.
  static Status LaunchProcess(ProcessLaunchInfo &launch_info);

  //------------------------------------------------------------------
  /// Perform expansion of the command-line for this launch info This can
  /// potentially involve wildcard expansion
  //  environment variable replacement, and whatever other
  //  argument magic the platform defines as part of its typical
  //  user experience
  //------------------------------------------------------------------
  static Status ShellExpandArguments(ProcessLaunchInfo &launch_info);

  // TODO: Convert this function to take a StringRef.
  static Status RunShellCommand(
      const char *command,         // Shouldn't be NULL
      const FileSpec &working_dir, // Pass empty FileSpec to use the current
                                   // working directory
      int *status_ptr, // Pass NULL if you don't want the process exit status
      int *signo_ptr,  // Pass NULL if you don't want the signal that caused the
                       // process to exit
      std::string
          *command_output, // Pass NULL if you don't want the command output
      const Timeout<std::micro> &timeout, bool run_in_default_shell = true);

  static Status RunShellCommand(
      const Args &args,
      const FileSpec &working_dir, // Pass empty FileSpec to use the current
                                   // working directory
      int *status_ptr, // Pass NULL if you don't want the process exit status
      int *signo_ptr,  // Pass NULL if you don't want the signal that caused the
                       // process to exit
      std::string
          *command_output, // Pass NULL if you don't want the command output
      const Timeout<std::micro> &timeout, bool run_in_default_shell = true);

  static bool OpenFileInExternalEditor(const FileSpec &file_spec,
                                       uint32_t line_no);

  static Environment GetEnvironment();

  static std::unique_ptr<Connection>
  CreateDefaultConnection(llvm::StringRef url);
};

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb_private::WaitStatus> {
  /// Options = "" gives a human readable description of the status Options =
  /// "g" gives a gdb-remote protocol status (e.g., X09)
  static void format(const lldb_private::WaitStatus &WS, raw_ostream &OS,
                     llvm::StringRef Options);
};
} // namespace llvm

#endif // LLDB_HOST_HOST_H
