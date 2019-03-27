//===-- ProcessLaunchInfo.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <climits>

#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/FileAction.h"
#include "lldb/Target/ProcessLaunchInfo.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileSystem.h"

#if !defined(_WIN32)
#include <limits.h>
#endif

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------------
// ProcessLaunchInfo member functions
//----------------------------------------------------------------------------

ProcessLaunchInfo::ProcessLaunchInfo()
    : ProcessInfo(), m_working_dir(), m_plugin_name(), m_flags(0),
      m_file_actions(), m_pty(new PseudoTerminal), m_resume_count(0),
      m_monitor_callback(nullptr), m_monitor_callback_baton(nullptr),
      m_monitor_signals(false), m_listener_sp(), m_hijack_listener_sp() {}

ProcessLaunchInfo::ProcessLaunchInfo(const FileSpec &stdin_file_spec,
                                     const FileSpec &stdout_file_spec,
                                     const FileSpec &stderr_file_spec,
                                     const FileSpec &working_directory,
                                     uint32_t launch_flags)
    : ProcessInfo(), m_working_dir(), m_plugin_name(), m_flags(launch_flags),
      m_file_actions(), m_pty(new PseudoTerminal), m_resume_count(0),
      m_monitor_callback(nullptr), m_monitor_callback_baton(nullptr),
      m_monitor_signals(false), m_listener_sp(), m_hijack_listener_sp() {
  if (stdin_file_spec) {
    FileAction file_action;
    const bool read = true;
    const bool write = false;
    if (file_action.Open(STDIN_FILENO, stdin_file_spec, read, write))
      AppendFileAction(file_action);
  }
  if (stdout_file_spec) {
    FileAction file_action;
    const bool read = false;
    const bool write = true;
    if (file_action.Open(STDOUT_FILENO, stdout_file_spec, read, write))
      AppendFileAction(file_action);
  }
  if (stderr_file_spec) {
    FileAction file_action;
    const bool read = false;
    const bool write = true;
    if (file_action.Open(STDERR_FILENO, stderr_file_spec, read, write))
      AppendFileAction(file_action);
  }
  if (working_directory)
    SetWorkingDirectory(working_directory);
}

bool ProcessLaunchInfo::AppendCloseFileAction(int fd) {
  FileAction file_action;
  if (file_action.Close(fd)) {
    AppendFileAction(file_action);
    return true;
  }
  return false;
}

bool ProcessLaunchInfo::AppendDuplicateFileAction(int fd, int dup_fd) {
  FileAction file_action;
  if (file_action.Duplicate(fd, dup_fd)) {
    AppendFileAction(file_action);
    return true;
  }
  return false;
}

bool ProcessLaunchInfo::AppendOpenFileAction(int fd, const FileSpec &file_spec,
                                             bool read, bool write) {
  FileAction file_action;
  if (file_action.Open(fd, file_spec, read, write)) {
    AppendFileAction(file_action);
    return true;
  }
  return false;
}

bool ProcessLaunchInfo::AppendSuppressFileAction(int fd, bool read,
                                                 bool write) {
  FileAction file_action;
  if (file_action.Open(fd, FileSpec(FileSystem::DEV_NULL), read, write)) {
    AppendFileAction(file_action);
    return true;
  }
  return false;
}

const FileAction *ProcessLaunchInfo::GetFileActionAtIndex(size_t idx) const {
  if (idx < m_file_actions.size())
    return &m_file_actions[idx];
  return nullptr;
}

const FileAction *ProcessLaunchInfo::GetFileActionForFD(int fd) const {
  for (size_t idx = 0, count = m_file_actions.size(); idx < count; ++idx) {
    if (m_file_actions[idx].GetFD() == fd)
      return &m_file_actions[idx];
  }
  return nullptr;
}

const FileSpec &ProcessLaunchInfo::GetWorkingDirectory() const {
  return m_working_dir;
}

void ProcessLaunchInfo::SetWorkingDirectory(const FileSpec &working_dir) {
  m_working_dir = working_dir;
}

const char *ProcessLaunchInfo::GetProcessPluginName() const {
  return (m_plugin_name.empty() ? nullptr : m_plugin_name.c_str());
}

void ProcessLaunchInfo::SetProcessPluginName(llvm::StringRef plugin) {
  m_plugin_name = plugin;
}

const FileSpec &ProcessLaunchInfo::GetShell() const { return m_shell; }

void ProcessLaunchInfo::SetShell(const FileSpec &shell) {
  m_shell = shell;
  if (m_shell) {
    FileSystem::Instance().ResolveExecutableLocation(m_shell);
    m_flags.Set(lldb::eLaunchFlagLaunchInShell);
  } else
    m_flags.Clear(lldb::eLaunchFlagLaunchInShell);
}

void ProcessLaunchInfo::SetLaunchInSeparateProcessGroup(bool separate) {
  if (separate)
    m_flags.Set(lldb::eLaunchFlagLaunchInSeparateProcessGroup);
  else
    m_flags.Clear(lldb::eLaunchFlagLaunchInSeparateProcessGroup);
}

void ProcessLaunchInfo::SetShellExpandArguments(bool expand) {
  if (expand)
    m_flags.Set(lldb::eLaunchFlagShellExpandArguments);
  else
    m_flags.Clear(lldb::eLaunchFlagShellExpandArguments);
}

void ProcessLaunchInfo::Clear() {
  ProcessInfo::Clear();
  m_working_dir.Clear();
  m_plugin_name.clear();
  m_shell.Clear();
  m_flags.Clear();
  m_file_actions.clear();
  m_resume_count = 0;
  m_listener_sp.reset();
  m_hijack_listener_sp.reset();
}

void ProcessLaunchInfo::SetMonitorProcessCallback(
    const Host::MonitorChildProcessCallback &callback, bool monitor_signals) {
  m_monitor_callback = callback;
  m_monitor_signals = monitor_signals;
}

bool ProcessLaunchInfo::NoOpMonitorCallback(lldb::pid_t pid, bool exited, int signal, int status) {
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS);
  LLDB_LOG(log, "pid = {0}, exited = {1}, signal = {2}, status = {3}", pid,
           exited, signal, status);
  return true;
}

bool ProcessLaunchInfo::MonitorProcess() const {
  if (m_monitor_callback && ProcessIDIsValid()) {
    Host::StartMonitoringChildProcess(m_monitor_callback, GetProcessID(),
                                      m_monitor_signals);
    return true;
  }
  return false;
}

void ProcessLaunchInfo::SetDetachOnError(bool enable) {
  if (enable)
    m_flags.Set(lldb::eLaunchFlagDetachOnError);
  else
    m_flags.Clear(lldb::eLaunchFlagDetachOnError);
}

llvm::Error ProcessLaunchInfo::SetUpPtyRedirection() {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_PROCESS);
  LLDB_LOG(log, "Generating a pty to use for stdin/out/err");

  int open_flags = O_RDWR | O_NOCTTY;
#if !defined(_WIN32)
  // We really shouldn't be specifying platform specific flags that are
  // intended for a system call in generic code.  But this will have to
  // do for now.
  open_flags |= O_CLOEXEC;
#endif
  if (!m_pty->OpenFirstAvailableMaster(open_flags, nullptr, 0)) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "PTY::OpenFirstAvailableMaster failed");
  }
  const FileSpec slave_file_spec(m_pty->GetSlaveName(nullptr, 0));

  // Only use the slave tty if we don't have anything specified for
  // input and don't have an action for stdin
  if (GetFileActionForFD(STDIN_FILENO) == nullptr)
    AppendOpenFileAction(STDIN_FILENO, slave_file_spec, true, false);

  // Only use the slave tty if we don't have anything specified for
  // output and don't have an action for stdout
  if (GetFileActionForFD(STDOUT_FILENO) == nullptr)
    AppendOpenFileAction(STDOUT_FILENO, slave_file_spec, false, true);

  // Only use the slave tty if we don't have anything specified for
  // error and don't have an action for stderr
  if (GetFileActionForFD(STDERR_FILENO) == nullptr)
    AppendOpenFileAction(STDERR_FILENO, slave_file_spec, false, true);
  return llvm::Error::success();
}

bool ProcessLaunchInfo::ConvertArgumentsForLaunchingInShell(
    Status &error, bool localhost, bool will_debug,
    bool first_arg_is_full_shell_command, int32_t num_resumes) {
  error.Clear();

  if (GetFlags().Test(eLaunchFlagLaunchInShell)) {
    if (m_shell) {
      std::string shell_executable = m_shell.GetPath();

      const char **argv = GetArguments().GetConstArgumentVector();
      if (argv == nullptr || argv[0] == nullptr)
        return false;
      Args shell_arguments;
      std::string safe_arg;
      shell_arguments.AppendArgument(shell_executable);
      const llvm::Triple &triple = GetArchitecture().GetTriple();
      if (triple.getOS() == llvm::Triple::Win32 &&
          !triple.isWindowsCygwinEnvironment())
        shell_arguments.AppendArgument(llvm::StringRef("/C"));
      else
        shell_arguments.AppendArgument(llvm::StringRef("-c"));

      StreamString shell_command;
      if (will_debug) {
        // Add a modified PATH environment variable in case argv[0] is a
        // relative path.
        const char *argv0 = argv[0];
        FileSpec arg_spec(argv0);
        if (arg_spec.IsRelative()) {
          // We have a relative path to our executable which may not work if we
          // just try to run "a.out" (without it being converted to "./a.out")
          FileSpec working_dir = GetWorkingDirectory();
          // Be sure to put quotes around PATH's value in case any paths have
          // spaces...
          std::string new_path("PATH=\"");
          const size_t empty_path_len = new_path.size();

          if (working_dir) {
            new_path += working_dir.GetPath();
          } else {
            llvm::SmallString<64> cwd;
            if (! llvm::sys::fs::current_path(cwd))
              new_path += cwd;
          }
          std::string curr_path;
          if (HostInfo::GetEnvironmentVar("PATH", curr_path)) {
            if (new_path.size() > empty_path_len)
              new_path += ':';
            new_path += curr_path;
          }
          new_path += "\" ";
          shell_command.PutCString(new_path);
        }

        if (triple.getOS() != llvm::Triple::Win32 ||
            triple.isWindowsCygwinEnvironment())
          shell_command.PutCString("exec");

        // Only Apple supports /usr/bin/arch being able to specify the
        // architecture
        if (GetArchitecture().IsValid() && // Valid architecture
            GetArchitecture().GetTriple().getVendor() ==
                llvm::Triple::Apple && // Apple only
            GetArchitecture().GetCore() !=
                ArchSpec::eCore_x86_64_x86_64h) // Don't do this for x86_64h
        {
          shell_command.Printf(" /usr/bin/arch -arch %s",
                               GetArchitecture().GetArchitectureName());
          // Set the resume count to 2:
          // 1 - stop in shell
          // 2 - stop in /usr/bin/arch
          // 3 - then we will stop in our program
          SetResumeCount(num_resumes + 1);
        } else {
          // Set the resume count to 1:
          // 1 - stop in shell
          // 2 - then we will stop in our program
          SetResumeCount(num_resumes);
        }
      }

      if (first_arg_is_full_shell_command) {
        // There should only be one argument that is the shell command itself
        // to be used as is
        if (argv[0] && !argv[1])
          shell_command.Printf("%s", argv[0]);
        else
          return false;
      } else {
        for (size_t i = 0; argv[i] != nullptr; ++i) {
          const char *arg =
              Args::GetShellSafeArgument(m_shell, argv[i], safe_arg);
          shell_command.Printf(" %s", arg);
        }
      }
      shell_arguments.AppendArgument(shell_command.GetString());
      m_executable = m_shell;
      m_arguments = shell_arguments;
      return true;
    } else {
      error.SetErrorString("invalid shell path");
    }
  } else {
    error.SetErrorString("not launching in shell");
  }
  return false;
}
