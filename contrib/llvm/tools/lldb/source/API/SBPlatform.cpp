//===-- SBPlatform.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBPlatform.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBLaunchInfo.h"
#include "lldb/API/SBUnixSignals.h"
#include "lldb/Host/File.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Status.h"

#include "llvm/Support/FileSystem.h"

#include <functional>

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// PlatformConnectOptions
//----------------------------------------------------------------------
struct PlatformConnectOptions {
  PlatformConnectOptions(const char *url = NULL)
      : m_url(), m_rsync_options(), m_rsync_remote_path_prefix(),
        m_rsync_enabled(false), m_rsync_omit_hostname_from_remote_path(false),
        m_local_cache_directory() {
    if (url && url[0])
      m_url = url;
  }

  ~PlatformConnectOptions() {}

  std::string m_url;
  std::string m_rsync_options;
  std::string m_rsync_remote_path_prefix;
  bool m_rsync_enabled;
  bool m_rsync_omit_hostname_from_remote_path;
  ConstString m_local_cache_directory;
};

//----------------------------------------------------------------------
// PlatformShellCommand
//----------------------------------------------------------------------
struct PlatformShellCommand {
  PlatformShellCommand(const char *shell_command = NULL)
      : m_command(), m_working_dir(), m_status(0), m_signo(0) {
    if (shell_command && shell_command[0])
      m_command = shell_command;
  }

  ~PlatformShellCommand() {}

  std::string m_command;
  std::string m_working_dir;
  std::string m_output;
  int m_status;
  int m_signo;
  Timeout<std::ratio<1>> m_timeout = llvm::None;
};
//----------------------------------------------------------------------
// SBPlatformConnectOptions
//----------------------------------------------------------------------
SBPlatformConnectOptions::SBPlatformConnectOptions(const char *url)
    : m_opaque_ptr(new PlatformConnectOptions(url)) {}

SBPlatformConnectOptions::SBPlatformConnectOptions(
    const SBPlatformConnectOptions &rhs)
    : m_opaque_ptr(new PlatformConnectOptions()) {
  *m_opaque_ptr = *rhs.m_opaque_ptr;
}

SBPlatformConnectOptions::~SBPlatformConnectOptions() { delete m_opaque_ptr; }

void SBPlatformConnectOptions::operator=(const SBPlatformConnectOptions &rhs) {
  *m_opaque_ptr = *rhs.m_opaque_ptr;
}

const char *SBPlatformConnectOptions::GetURL() {
  if (m_opaque_ptr->m_url.empty())
    return NULL;
  return m_opaque_ptr->m_url.c_str();
}

void SBPlatformConnectOptions::SetURL(const char *url) {
  if (url && url[0])
    m_opaque_ptr->m_url = url;
  else
    m_opaque_ptr->m_url.clear();
}

bool SBPlatformConnectOptions::GetRsyncEnabled() {
  return m_opaque_ptr->m_rsync_enabled;
}

void SBPlatformConnectOptions::EnableRsync(
    const char *options, const char *remote_path_prefix,
    bool omit_hostname_from_remote_path) {
  m_opaque_ptr->m_rsync_enabled = true;
  m_opaque_ptr->m_rsync_omit_hostname_from_remote_path =
      omit_hostname_from_remote_path;
  if (remote_path_prefix && remote_path_prefix[0])
    m_opaque_ptr->m_rsync_remote_path_prefix = remote_path_prefix;
  else
    m_opaque_ptr->m_rsync_remote_path_prefix.clear();

  if (options && options[0])
    m_opaque_ptr->m_rsync_options = options;
  else
    m_opaque_ptr->m_rsync_options.clear();
}

void SBPlatformConnectOptions::DisableRsync() {
  m_opaque_ptr->m_rsync_enabled = false;
}

const char *SBPlatformConnectOptions::GetLocalCacheDirectory() {
  return m_opaque_ptr->m_local_cache_directory.GetCString();
}

void SBPlatformConnectOptions::SetLocalCacheDirectory(const char *path) {
  if (path && path[0])
    m_opaque_ptr->m_local_cache_directory.SetCString(path);
  else
    m_opaque_ptr->m_local_cache_directory = ConstString();
}

//----------------------------------------------------------------------
// SBPlatformShellCommand
//----------------------------------------------------------------------
SBPlatformShellCommand::SBPlatformShellCommand(const char *shell_command)
    : m_opaque_ptr(new PlatformShellCommand(shell_command)) {}

SBPlatformShellCommand::SBPlatformShellCommand(
    const SBPlatformShellCommand &rhs)
    : m_opaque_ptr(new PlatformShellCommand()) {
  *m_opaque_ptr = *rhs.m_opaque_ptr;
}

SBPlatformShellCommand::~SBPlatformShellCommand() { delete m_opaque_ptr; }

void SBPlatformShellCommand::Clear() {
  m_opaque_ptr->m_output = std::string();
  m_opaque_ptr->m_status = 0;
  m_opaque_ptr->m_signo = 0;
}

const char *SBPlatformShellCommand::GetCommand() {
  if (m_opaque_ptr->m_command.empty())
    return NULL;
  return m_opaque_ptr->m_command.c_str();
}

void SBPlatformShellCommand::SetCommand(const char *shell_command) {
  if (shell_command && shell_command[0])
    m_opaque_ptr->m_command = shell_command;
  else
    m_opaque_ptr->m_command.clear();
}

const char *SBPlatformShellCommand::GetWorkingDirectory() {
  if (m_opaque_ptr->m_working_dir.empty())
    return NULL;
  return m_opaque_ptr->m_working_dir.c_str();
}

void SBPlatformShellCommand::SetWorkingDirectory(const char *path) {
  if (path && path[0])
    m_opaque_ptr->m_working_dir = path;
  else
    m_opaque_ptr->m_working_dir.clear();
}

uint32_t SBPlatformShellCommand::GetTimeoutSeconds() {
  if (m_opaque_ptr->m_timeout)
    return m_opaque_ptr->m_timeout->count();
  return UINT32_MAX;
}

void SBPlatformShellCommand::SetTimeoutSeconds(uint32_t sec) {
  if (sec == UINT32_MAX)
    m_opaque_ptr->m_timeout = llvm::None;
  else
    m_opaque_ptr->m_timeout = std::chrono::seconds(sec);
}

int SBPlatformShellCommand::GetSignal() { return m_opaque_ptr->m_signo; }

int SBPlatformShellCommand::GetStatus() { return m_opaque_ptr->m_status; }

const char *SBPlatformShellCommand::GetOutput() {
  if (m_opaque_ptr->m_output.empty())
    return NULL;
  return m_opaque_ptr->m_output.c_str();
}

//----------------------------------------------------------------------
// SBPlatform
//----------------------------------------------------------------------
SBPlatform::SBPlatform() : m_opaque_sp() {}

SBPlatform::SBPlatform(const char *platform_name) : m_opaque_sp() {
  Status error;
  if (platform_name && platform_name[0])
    m_opaque_sp = Platform::Create(ConstString(platform_name), error);
}

SBPlatform::~SBPlatform() {}

bool SBPlatform::IsValid() const { return m_opaque_sp.get() != NULL; }

void SBPlatform::Clear() { m_opaque_sp.reset(); }

const char *SBPlatform::GetName() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return platform_sp->GetName().GetCString();
  return NULL;
}

lldb::PlatformSP SBPlatform::GetSP() const { return m_opaque_sp; }

void SBPlatform::SetSP(const lldb::PlatformSP &platform_sp) {
  m_opaque_sp = platform_sp;
}

const char *SBPlatform::GetWorkingDirectory() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return platform_sp->GetWorkingDirectory().GetCString();
  return NULL;
}

bool SBPlatform::SetWorkingDirectory(const char *path) {
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    if (path)
      platform_sp->SetWorkingDirectory(FileSpec(path));
    else
      platform_sp->SetWorkingDirectory(FileSpec());
    return true;
  }
  return false;
}

SBError SBPlatform::ConnectRemote(SBPlatformConnectOptions &connect_options) {
  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp && connect_options.GetURL()) {
    Args args;
    args.AppendArgument(
        llvm::StringRef::withNullAsEmpty(connect_options.GetURL()));
    sb_error.ref() = platform_sp->ConnectRemote(args);
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

void SBPlatform::DisconnectRemote() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    platform_sp->DisconnectRemote();
}

bool SBPlatform::IsConnected() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return platform_sp->IsConnected();
  return false;
}

const char *SBPlatform::GetTriple() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    ArchSpec arch(platform_sp->GetSystemArchitecture());
    if (arch.IsValid()) {
      // Const-ify the string so we don't need to worry about the lifetime of
      // the string
      return ConstString(arch.GetTriple().getTriple().c_str()).GetCString();
    }
  }
  return NULL;
}

const char *SBPlatform::GetOSBuild() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    std::string s;
    if (platform_sp->GetOSBuildString(s)) {
      if (!s.empty()) {
        // Const-ify the string so we don't need to worry about the lifetime of
        // the string
        return ConstString(s.c_str()).GetCString();
      }
    }
  }
  return NULL;
}

const char *SBPlatform::GetOSDescription() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    std::string s;
    if (platform_sp->GetOSKernelDescription(s)) {
      if (!s.empty()) {
        // Const-ify the string so we don't need to worry about the lifetime of
        // the string
        return ConstString(s.c_str()).GetCString();
      }
    }
  }
  return NULL;
}

const char *SBPlatform::GetHostname() {
  PlatformSP platform_sp(GetSP());
  if (platform_sp)
    return platform_sp->GetHostname();
  return NULL;
}

uint32_t SBPlatform::GetOSMajorVersion() {
  llvm::VersionTuple version;
  if (PlatformSP platform_sp = GetSP())
    version = platform_sp->GetOSVersion();
  return version.empty() ? UINT32_MAX : version.getMajor();
}

uint32_t SBPlatform::GetOSMinorVersion() {
  llvm::VersionTuple version;
  if (PlatformSP platform_sp = GetSP())
    version = platform_sp->GetOSVersion();
  return version.getMinor().getValueOr(UINT32_MAX);
}

uint32_t SBPlatform::GetOSUpdateVersion() {
  llvm::VersionTuple version;
  if (PlatformSP platform_sp = GetSP())
    version = platform_sp->GetOSVersion();
  return version.getSubminor().getValueOr(UINT32_MAX);
}

SBError SBPlatform::Get(SBFileSpec &src, SBFileSpec &dst) {
  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    sb_error.ref() = platform_sp->GetFile(src.ref(), dst.ref());
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

SBError SBPlatform::Put(SBFileSpec &src, SBFileSpec &dst) {
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    if (src.Exists()) {
      uint32_t permissions = FileSystem::Instance().GetPermissions(src.ref());
      if (permissions == 0) {
        if (FileSystem::Instance().IsDirectory(src.ref()))
          permissions = eFilePermissionsDirectoryDefault;
        else
          permissions = eFilePermissionsFileDefault;
      }

      return platform_sp->PutFile(src.ref(), dst.ref(), permissions);
    }

    Status error;
    error.SetErrorStringWithFormat("'src' argument doesn't exist: '%s'",
                                   src.ref().GetPath().c_str());
    return error;
  });
}

SBError SBPlatform::Install(SBFileSpec &src, SBFileSpec &dst) {
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    if (src.Exists())
      return platform_sp->Install(src.ref(), dst.ref());

    Status error;
    error.SetErrorStringWithFormat("'src' argument doesn't exist: '%s'",
                                   src.ref().GetPath().c_str());
    return error;
  });
}

SBError SBPlatform::Run(SBPlatformShellCommand &shell_command) {
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    const char *command = shell_command.GetCommand();
    if (!command)
      return Status("invalid shell command (empty)");

    const char *working_dir = shell_command.GetWorkingDirectory();
    if (working_dir == NULL) {
      working_dir = platform_sp->GetWorkingDirectory().GetCString();
      if (working_dir)
        shell_command.SetWorkingDirectory(working_dir);
    }
    return platform_sp->RunShellCommand(command, FileSpec(working_dir),
                                        &shell_command.m_opaque_ptr->m_status,
                                        &shell_command.m_opaque_ptr->m_signo,
                                        &shell_command.m_opaque_ptr->m_output,
                                        shell_command.m_opaque_ptr->m_timeout);
  });
}

SBError SBPlatform::Launch(SBLaunchInfo &launch_info) {
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    ProcessLaunchInfo info = launch_info.ref();
    Status error = platform_sp->LaunchProcess(info);
    launch_info.set_ref(info);
    return error;
  });
}

SBError SBPlatform::Kill(const lldb::pid_t pid) {
  return ExecuteConnected([&](const lldb::PlatformSP &platform_sp) {
    return platform_sp->KillProcess(pid);
  });
}

SBError SBPlatform::ExecuteConnected(
    const std::function<Status(const lldb::PlatformSP &)> &func) {
  SBError sb_error;
  const auto platform_sp(GetSP());
  if (platform_sp) {
    if (platform_sp->IsConnected())
      sb_error.ref() = func(platform_sp);
    else
      sb_error.SetErrorString("not connected");
  } else
    sb_error.SetErrorString("invalid platform");

  return sb_error;
}

SBError SBPlatform::MakeDirectory(const char *path, uint32_t file_permissions) {
  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    sb_error.ref() =
        platform_sp->MakeDirectory(FileSpec(path), file_permissions);
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

uint32_t SBPlatform::GetFilePermissions(const char *path) {
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    uint32_t file_permissions = 0;
    platform_sp->GetFilePermissions(FileSpec(path), file_permissions);
    return file_permissions;
  }
  return 0;
}

SBError SBPlatform::SetFilePermissions(const char *path,
                                       uint32_t file_permissions) {
  SBError sb_error;
  PlatformSP platform_sp(GetSP());
  if (platform_sp) {
    sb_error.ref() =
        platform_sp->SetFilePermissions(FileSpec(path), file_permissions);
  } else {
    sb_error.SetErrorString("invalid platform");
  }
  return sb_error;
}

SBUnixSignals SBPlatform::GetUnixSignals() const {
  if (auto platform_sp = GetSP())
    return SBUnixSignals{platform_sp};

  return {};
}
