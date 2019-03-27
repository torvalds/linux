//===-- SBLaunchInfo.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBLaunchInfo_h_
#define LLDB_SBLaunchInfo_h_

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class SBLaunchInfoImpl;
}

namespace lldb {

class SBPlatform;
class SBTarget;

class LLDB_API SBLaunchInfo {
public:
  SBLaunchInfo(const char **argv);

  ~SBLaunchInfo();

  lldb::pid_t GetProcessID();

  uint32_t GetUserID();

  uint32_t GetGroupID();

  bool UserIDIsValid();

  bool GroupIDIsValid();

  void SetUserID(uint32_t uid);

  void SetGroupID(uint32_t gid);

  SBFileSpec GetExecutableFile();

  //----------------------------------------------------------------------
  /// Set the executable file that will be used to launch the process and
  /// optionally set it as the first argument in the argument vector.
  ///
  /// This only needs to be specified if clients wish to carefully control
  /// the exact path will be used to launch a binary. If you create a
  /// target with a symlink, that symlink will get resolved in the target
  /// and the resolved path will get used to launch the process. Calling
  /// this function can help you still launch your process using the
  /// path of your choice.
  ///
  /// If this function is not called prior to launching with
  /// SBTarget::Launch(...), the target will use the resolved executable
  /// path that was used to create the target.
  ///
  /// @param[in] exe_file
  ///     The override path to use when launching the executable.
  ///
  /// @param[in] add_as_first_arg
  ///     If true, then the path will be inserted into the argument vector
  ///     prior to launching. Otherwise the argument vector will be left
  ///     alone.
  //----------------------------------------------------------------------
  void SetExecutableFile(SBFileSpec exe_file, bool add_as_first_arg);

  //----------------------------------------------------------------------
  /// Get the listener that will be used to receive process events.
  ///
  /// If no listener has been set via a call to
  /// SBLaunchInfo::SetListener(), then an invalid SBListener will be
  /// returned (SBListener::IsValid() will return false). If a listener
  /// has been set, then the valid listener object will be returned.
  //----------------------------------------------------------------------
  SBListener GetListener();

  //----------------------------------------------------------------------
  /// Set the listener that will be used to receive process events.
  ///
  /// By default the SBDebugger, which has a listener, that the SBTarget
  /// belongs to will listen for the process events. Calling this function
  /// allows a different listener to be used to listen for process events.
  //----------------------------------------------------------------------
  void SetListener(SBListener &listener);

  uint32_t GetNumArguments();

  const char *GetArgumentAtIndex(uint32_t idx);

  void SetArguments(const char **argv, bool append);

  uint32_t GetNumEnvironmentEntries();

  const char *GetEnvironmentEntryAtIndex(uint32_t idx);

  void SetEnvironmentEntries(const char **envp, bool append);

  void Clear();

  const char *GetWorkingDirectory() const;

  void SetWorkingDirectory(const char *working_dir);

  uint32_t GetLaunchFlags();

  void SetLaunchFlags(uint32_t flags);

  const char *GetProcessPluginName();

  void SetProcessPluginName(const char *plugin_name);

  const char *GetShell();

  void SetShell(const char *path);

  bool GetShellExpandArguments();

  void SetShellExpandArguments(bool expand);

  uint32_t GetResumeCount();

  void SetResumeCount(uint32_t c);

  bool AddCloseFileAction(int fd);

  bool AddDuplicateFileAction(int fd, int dup_fd);

  bool AddOpenFileAction(int fd, const char *path, bool read, bool write);

  bool AddSuppressFileAction(int fd, bool read, bool write);

  void SetLaunchEventData(const char *data);

  const char *GetLaunchEventData() const;

  bool GetDetachOnError() const;

  void SetDetachOnError(bool enable);

protected:
  friend class SBPlatform;
  friend class SBTarget;

  const lldb_private::ProcessLaunchInfo &ref() const;
  void set_ref(const lldb_private::ProcessLaunchInfo &info);

  std::shared_ptr<lldb_private::SBLaunchInfoImpl> m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBLaunchInfo_h_
