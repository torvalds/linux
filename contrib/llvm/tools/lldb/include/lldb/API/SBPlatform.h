//===-- SBPlatform.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBPlatform_h_
#define LLDB_SBPlatform_h_

#include "lldb/API/SBDefines.h"

#include <functional>

struct PlatformConnectOptions;
struct PlatformShellCommand;

namespace lldb {

class SBLaunchInfo;

class LLDB_API SBPlatformConnectOptions {
public:
  SBPlatformConnectOptions(const char *url);

  SBPlatformConnectOptions(const SBPlatformConnectOptions &rhs);

  ~SBPlatformConnectOptions();

  void operator=(const SBPlatformConnectOptions &rhs);

  const char *GetURL();

  void SetURL(const char *url);

  bool GetRsyncEnabled();

  void EnableRsync(const char *options, const char *remote_path_prefix,
                   bool omit_remote_hostname);

  void DisableRsync();

  const char *GetLocalCacheDirectory();

  void SetLocalCacheDirectory(const char *path);

protected:
  PlatformConnectOptions *m_opaque_ptr;
};

class LLDB_API SBPlatformShellCommand {
public:
  SBPlatformShellCommand(const char *shell_command);

  SBPlatformShellCommand(const SBPlatformShellCommand &rhs);

  ~SBPlatformShellCommand();

  void Clear();

  const char *GetCommand();

  void SetCommand(const char *shell_command);

  const char *GetWorkingDirectory();

  void SetWorkingDirectory(const char *path);

  uint32_t GetTimeoutSeconds();

  void SetTimeoutSeconds(uint32_t sec);

  int GetSignal();

  int GetStatus();

  const char *GetOutput();

protected:
  friend class SBPlatform;

  PlatformShellCommand *m_opaque_ptr;
};

class LLDB_API SBPlatform {
public:
  SBPlatform();

  SBPlatform(const char *platform_name);

  ~SBPlatform();

  bool IsValid() const;

  void Clear();

  const char *GetWorkingDirectory();

  bool SetWorkingDirectory(const char *path);

  const char *GetName();

  SBError ConnectRemote(SBPlatformConnectOptions &connect_options);

  void DisconnectRemote();

  bool IsConnected();

  //----------------------------------------------------------------------
  // The following functions will work if the platform is connected
  //----------------------------------------------------------------------
  const char *GetTriple();

  const char *GetHostname();

  const char *GetOSBuild();

  const char *GetOSDescription();

  uint32_t GetOSMajorVersion();

  uint32_t GetOSMinorVersion();

  uint32_t GetOSUpdateVersion();

  SBError Put(SBFileSpec &src, SBFileSpec &dst);

  SBError Get(SBFileSpec &src, SBFileSpec &dst);

  SBError Install(SBFileSpec &src, SBFileSpec &dst);

  SBError Run(SBPlatformShellCommand &shell_command);

  SBError Launch(SBLaunchInfo &launch_info);

  SBError Kill(const lldb::pid_t pid);

  SBError
  MakeDirectory(const char *path,
                uint32_t file_permissions = eFilePermissionsDirectoryDefault);

  uint32_t GetFilePermissions(const char *path);

  SBError SetFilePermissions(const char *path, uint32_t file_permissions);

  SBUnixSignals GetUnixSignals() const;

protected:
  friend class SBDebugger;
  friend class SBTarget;

  lldb::PlatformSP GetSP() const;

  void SetSP(const lldb::PlatformSP &platform_sp);

  SBError ExecuteConnected(
      const std::function<lldb_private::Status(const lldb::PlatformSP &)>
          &func);

  lldb::PlatformSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBPlatform_h_
