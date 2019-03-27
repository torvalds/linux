//===-- PlatformPOSIX.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_PlatformPOSIX_h_
#define liblldb_PlatformPOSIX_h_

#include <map>
#include <memory>

#include "lldb/Interpreter/Options.h"
#include "lldb/Target/Platform.h"

class PlatformPOSIX : public lldb_private::Platform {
public:
  PlatformPOSIX(bool is_host);

  ~PlatformPOSIX() override;

  //------------------------------------------------------------
  // lldb_private::Platform functions
  //------------------------------------------------------------

  bool GetModuleSpec(const lldb_private::FileSpec &module_file_spec,
                     const lldb_private::ArchSpec &arch,
                     lldb_private::ModuleSpec &module_spec) override;

  lldb_private::OptionGroupOptions *
  GetConnectionOptions(lldb_private::CommandInterpreter &interpreter) override;

  const char *GetHostname() override;

  const char *GetUserName(uint32_t uid) override;

  const char *GetGroupName(uint32_t gid) override;

  lldb_private::Status PutFile(const lldb_private::FileSpec &source,
                               const lldb_private::FileSpec &destination,
                               uint32_t uid = UINT32_MAX,
                               uint32_t gid = UINT32_MAX) override;

  lldb::user_id_t OpenFile(const lldb_private::FileSpec &file_spec,
                           uint32_t flags, uint32_t mode,
                           lldb_private::Status &error) override;

  bool CloseFile(lldb::user_id_t fd, lldb_private::Status &error) override;

  uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                    uint64_t dst_len, lldb_private::Status &error) override;

  uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset, const void *src,
                     uint64_t src_len, lldb_private::Status &error) override;

  lldb::user_id_t GetFileSize(const lldb_private::FileSpec &file_spec) override;

  lldb_private::Status
  CreateSymlink(const lldb_private::FileSpec &src,
                const lldb_private::FileSpec &dst) override;

  lldb_private::Status
  GetFile(const lldb_private::FileSpec &source,
          const lldb_private::FileSpec &destination) override;

  lldb_private::FileSpec GetRemoteWorkingDirectory() override;

  bool
  SetRemoteWorkingDirectory(const lldb_private::FileSpec &working_dir) override;

  bool GetRemoteOSVersion() override;

  bool GetRemoteOSBuildString(std::string &s) override;

  bool GetRemoteOSKernelDescription(std::string &s) override;

  lldb_private::ArchSpec GetRemoteSystemArchitecture() override;

  const lldb::UnixSignalsSP &GetRemoteUnixSignals() override;

  lldb_private::Environment GetEnvironment() override;

  bool IsConnected() const override;

  lldb_private::Status RunShellCommand(
      const char *command,                       // Shouldn't be nullptr
      const lldb_private::FileSpec &working_dir, // Pass empty FileSpec to use
                                                 // the current working
                                                 // directory
      int *status_ptr, // Pass nullptr if you don't want the process exit status
      int *signo_ptr,  // Pass nullptr if you don't want the signal that caused
                       // the process to exit
      std::string
          *command_output, // Pass nullptr if you don't want the command output
      const lldb_private::Timeout<std::micro> &timeout) override;

  lldb_private::Status ResolveExecutable(
      const lldb_private::ModuleSpec &module_spec, lldb::ModuleSP &module_sp,
      const lldb_private::FileSpecList *module_search_paths_ptr) override;

  lldb_private::Status
  GetFileWithUUID(const lldb_private::FileSpec &platform_file,
                  const lldb_private::UUID *uuid,
                  lldb_private::FileSpec &local_file) override;

  bool GetProcessInfo(lldb::pid_t pid, lldb_private::ProcessInstanceInfo &proc_info) override;

  uint32_t FindProcesses(const lldb_private::ProcessInstanceInfoMatch &match_info,
                         lldb_private::ProcessInstanceInfoList &process_infos) override;

  lldb_private::Status MakeDirectory(const lldb_private::FileSpec &file_spec,
                                     uint32_t mode) override;

  lldb_private::Status
  GetFilePermissions(const lldb_private::FileSpec &file_spec,
                     uint32_t &file_permissions) override;

  lldb_private::Status
  SetFilePermissions(const lldb_private::FileSpec &file_spec,
                     uint32_t file_permissions) override;

  bool GetFileExists(const lldb_private::FileSpec &file_spec) override;

  lldb_private::Status Unlink(const lldb_private::FileSpec &file_spec) override;

  lldb_private::Status
  LaunchProcess(lldb_private::ProcessLaunchInfo &launch_info) override;

  lldb_private::Status KillProcess(const lldb::pid_t pid) override;

  lldb::ProcessSP Attach(lldb_private::ProcessAttachInfo &attach_info,
                         lldb_private::Debugger &debugger,
                         lldb_private::Target *target, // Can be nullptr, if
                                                       // nullptr create a new
                                                       // target, else use
                                                       // existing one
                         lldb_private::Status &error) override;

  lldb::ProcessSP DebugProcess(lldb_private::ProcessLaunchInfo &launch_info,
                               lldb_private::Debugger &debugger,
                               lldb_private::Target *target, // Can be nullptr,
                                                             // if nullptr
                                                             // create a new
                                                             // target, else use
                                                             // existing one
                               lldb_private::Status &error) override;

  std::string GetPlatformSpecificConnectionInformation() override;

  bool CalculateMD5(const lldb_private::FileSpec &file_spec, uint64_t &low,
                    uint64_t &high) override;

  void CalculateTrapHandlerSymbolNames() override;

  lldb_private::Status ConnectRemote(lldb_private::Args &args) override;

  lldb_private::Status DisconnectRemote() override;

  uint32_t DoLoadImage(lldb_private::Process *process,
                       const lldb_private::FileSpec &remote_file,
                       const std::vector<std::string> *paths,
                       lldb_private::Status &error,
                       lldb_private::FileSpec *loaded_image) override;

  lldb_private::Status UnloadImage(lldb_private::Process *process,
                                   uint32_t image_token) override;

  lldb::ProcessSP ConnectProcess(llvm::StringRef connect_url,
                                 llvm::StringRef plugin_name,
                                 lldb_private::Debugger &debugger,
                                 lldb_private::Target *target,
                                 lldb_private::Status &error) override;

  size_t ConnectToWaitingProcesses(lldb_private::Debugger &debugger,
                                   lldb_private::Status &error) override;

  lldb_private::ConstString GetFullNameForDylib(lldb_private::ConstString basename) override;

protected:
  std::unique_ptr<lldb_private::OptionGroupPlatformRSync>
      m_option_group_platform_rsync;
  std::unique_ptr<lldb_private::OptionGroupPlatformSSH>
      m_option_group_platform_ssh;
  std::unique_ptr<lldb_private::OptionGroupPlatformCaching>
      m_option_group_platform_caching;

  std::map<lldb_private::CommandInterpreter *,
           std::unique_ptr<lldb_private::OptionGroupOptions>>
      m_options;
  lldb::PlatformSP m_remote_platform_sp; // Allow multiple ways to connect to a
                                         // remote POSIX-compliant OS

  lldb_private::Status
  EvaluateLibdlExpression(lldb_private::Process *process, const char *expr_cstr,
                          llvm::StringRef expr_prefix,
                          lldb::ValueObjectSP &result_valobj_sp);

  std::unique_ptr<lldb_private::UtilityFunction>
  MakeLoadImageUtilityFunction(lldb_private::ExecutionContext &exe_ctx,
                               lldb_private::Status &error);

  virtual
  llvm::StringRef GetLibdlFunctionDeclarations(lldb_private::Process *process);

private:
  DISALLOW_COPY_AND_ASSIGN(PlatformPOSIX);
};

#endif // liblldb_PlatformPOSIX_h_
