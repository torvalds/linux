//===-- PlatformPOSIX.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PlatformPOSIX.h"


#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/UserExpression.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/File.h"
#include "lldb/Host/FileCache.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/ProcessLaunchInfo.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/CleanUp.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

//------------------------------------------------------------------
/// Default Constructor
//------------------------------------------------------------------
PlatformPOSIX::PlatformPOSIX(bool is_host)
    : Platform(is_host), // This is the local host platform
      m_option_group_platform_rsync(new OptionGroupPlatformRSync()),
      m_option_group_platform_ssh(new OptionGroupPlatformSSH()),
      m_option_group_platform_caching(new OptionGroupPlatformCaching()),
      m_remote_platform_sp() {}

//------------------------------------------------------------------
/// Destructor.
///
/// The destructor is virtual since this class is designed to be
/// inherited from by the plug-in instance.
//------------------------------------------------------------------
PlatformPOSIX::~PlatformPOSIX() {}

bool PlatformPOSIX::GetModuleSpec(const FileSpec &module_file_spec,
                                  const ArchSpec &arch,
                                  ModuleSpec &module_spec) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetModuleSpec(module_file_spec, arch,
                                               module_spec);

  return Platform::GetModuleSpec(module_file_spec, arch, module_spec);
}

lldb_private::OptionGroupOptions *PlatformPOSIX::GetConnectionOptions(
    lldb_private::CommandInterpreter &interpreter) {
  auto iter = m_options.find(&interpreter), end = m_options.end();
  if (iter == end) {
    std::unique_ptr<lldb_private::OptionGroupOptions> options(
        new OptionGroupOptions());
    options->Append(m_option_group_platform_rsync.get());
    options->Append(m_option_group_platform_ssh.get());
    options->Append(m_option_group_platform_caching.get());
    m_options[&interpreter] = std::move(options);
  }

  return m_options.at(&interpreter).get();
}

bool PlatformPOSIX::IsConnected() const {
  if (IsHost())
    return true;
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->IsConnected();
  return false;
}

lldb_private::Status PlatformPOSIX::RunShellCommand(
    const char *command, // Shouldn't be NULL
    const FileSpec &
        working_dir, // Pass empty FileSpec to use the current working directory
    int *status_ptr, // Pass NULL if you don't want the process exit status
    int *signo_ptr,  // Pass NULL if you don't want the signal that caused the
                     // process to exit
    std::string
        *command_output, // Pass NULL if you don't want the command output
    const Timeout<std::micro> &timeout) {
  if (IsHost())
    return Host::RunShellCommand(command, working_dir, status_ptr, signo_ptr,
                                 command_output, timeout);
  else {
    if (m_remote_platform_sp)
      return m_remote_platform_sp->RunShellCommand(
          command, working_dir, status_ptr, signo_ptr, command_output, timeout);
    else
      return Status("unable to run a remote command without a platform");
  }
}

Status
PlatformPOSIX::ResolveExecutable(const ModuleSpec &module_spec,
                                 lldb::ModuleSP &exe_module_sp,
                                 const FileSpecList *module_search_paths_ptr) {
  Status error;
  // Nothing special to do here, just use the actual file and architecture

  char exe_path[PATH_MAX];
  ModuleSpec resolved_module_spec(module_spec);

  if (IsHost()) {
    // If we have "ls" as the exe_file, resolve the executable location based
    // on the current path variables
    if (!FileSystem::Instance().Exists(resolved_module_spec.GetFileSpec())) {
      resolved_module_spec.GetFileSpec().GetPath(exe_path, sizeof(exe_path));
      resolved_module_spec.GetFileSpec().SetFile(exe_path,
                                                 FileSpec::Style::native);
      FileSystem::Instance().Resolve(resolved_module_spec.GetFileSpec());
    }

    if (!FileSystem::Instance().Exists(resolved_module_spec.GetFileSpec()))
      FileSystem::Instance().ResolveExecutableLocation(
          resolved_module_spec.GetFileSpec());

    // Resolve any executable within a bundle on MacOSX
    Host::ResolveExecutableInBundle(resolved_module_spec.GetFileSpec());

    if (FileSystem::Instance().Exists(resolved_module_spec.GetFileSpec()))
      error.Clear();
    else {
      const uint32_t permissions = FileSystem::Instance().GetPermissions(
          resolved_module_spec.GetFileSpec());
      if (permissions && (permissions & eFilePermissionsEveryoneR) == 0)
        error.SetErrorStringWithFormat(
            "executable '%s' is not readable",
            resolved_module_spec.GetFileSpec().GetPath().c_str());
      else
        error.SetErrorStringWithFormat(
            "unable to find executable for '%s'",
            resolved_module_spec.GetFileSpec().GetPath().c_str());
    }
  } else {
    if (m_remote_platform_sp) {
      error =
          GetCachedExecutable(resolved_module_spec, exe_module_sp,
                              module_search_paths_ptr, *m_remote_platform_sp);
    } else {
      // We may connect to a process and use the provided executable (Don't use
      // local $PATH).

      // Resolve any executable within a bundle on MacOSX
      Host::ResolveExecutableInBundle(resolved_module_spec.GetFileSpec());

      if (FileSystem::Instance().Exists(resolved_module_spec.GetFileSpec()))
        error.Clear();
      else
        error.SetErrorStringWithFormat("the platform is not currently "
                                       "connected, and '%s' doesn't exist in "
                                       "the system root.",
                                       exe_path);
    }
  }

  if (error.Success()) {
    if (resolved_module_spec.GetArchitecture().IsValid()) {
      error = ModuleList::GetSharedModule(resolved_module_spec, exe_module_sp,
                                          module_search_paths_ptr, nullptr, nullptr);
      if (error.Fail()) {
        // If we failed, it may be because the vendor and os aren't known. If
	// that is the case, try setting them to the host architecture and give
	// it another try.
        llvm::Triple &module_triple =
            resolved_module_spec.GetArchitecture().GetTriple();
        bool is_vendor_specified =
            (module_triple.getVendor() != llvm::Triple::UnknownVendor);
        bool is_os_specified =
            (module_triple.getOS() != llvm::Triple::UnknownOS);
        if (!is_vendor_specified || !is_os_specified) {
          const llvm::Triple &host_triple =
              HostInfo::GetArchitecture(HostInfo::eArchKindDefault).GetTriple();

          if (!is_vendor_specified)
            module_triple.setVendorName(host_triple.getVendorName());
          if (!is_os_specified)
            module_triple.setOSName(host_triple.getOSName());

          error = ModuleList::GetSharedModule(resolved_module_spec,
                                              exe_module_sp, module_search_paths_ptr, nullptr, nullptr);
        }
      }

      // TODO find out why exe_module_sp might be NULL
      if (error.Fail() || !exe_module_sp || !exe_module_sp->GetObjectFile()) {
        exe_module_sp.reset();
        error.SetErrorStringWithFormat(
            "'%s' doesn't contain the architecture %s",
            resolved_module_spec.GetFileSpec().GetPath().c_str(),
            resolved_module_spec.GetArchitecture().GetArchitectureName());
      }
    } else {
      // No valid architecture was specified, ask the platform for the
      // architectures that we should be using (in the correct order) and see
      // if we can find a match that way
      StreamString arch_names;
      for (uint32_t idx = 0; GetSupportedArchitectureAtIndex(
               idx, resolved_module_spec.GetArchitecture());
           ++idx) {
        error = ModuleList::GetSharedModule(resolved_module_spec, exe_module_sp,
                                            module_search_paths_ptr, nullptr, nullptr);
        // Did we find an executable using one of the
        if (error.Success()) {
          if (exe_module_sp && exe_module_sp->GetObjectFile())
            break;
          else
            error.SetErrorToGenericError();
        }

        if (idx > 0)
          arch_names.PutCString(", ");
        arch_names.PutCString(
            resolved_module_spec.GetArchitecture().GetArchitectureName());
      }

      if (error.Fail() || !exe_module_sp) {
        if (FileSystem::Instance().Readable(
                resolved_module_spec.GetFileSpec())) {
          error.SetErrorStringWithFormat(
              "'%s' doesn't contain any '%s' platform architectures: %s",
              resolved_module_spec.GetFileSpec().GetPath().c_str(),
              GetPluginName().GetCString(), arch_names.GetData());
        } else {
          error.SetErrorStringWithFormat(
              "'%s' is not readable",
              resolved_module_spec.GetFileSpec().GetPath().c_str());
        }
      }
    }
  }

  return error;
}

Status PlatformPOSIX::GetFileWithUUID(const FileSpec &platform_file,
                                      const UUID *uuid_ptr,
                                      FileSpec &local_file) {
  if (IsRemote() && m_remote_platform_sp)
      return m_remote_platform_sp->GetFileWithUUID(platform_file, uuid_ptr,
                                                   local_file);

  // Default to the local case
  local_file = platform_file;
  return Status();
}

bool PlatformPOSIX::GetProcessInfo(lldb::pid_t pid,
                                     ProcessInstanceInfo &process_info) {
  if (IsHost())
    return Platform::GetProcessInfo(pid, process_info);
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetProcessInfo(pid, process_info);
  return false;
}

uint32_t
PlatformPOSIX::FindProcesses(const ProcessInstanceInfoMatch &match_info,
                               ProcessInstanceInfoList &process_infos) {
  if (IsHost())
    return Platform::FindProcesses(match_info, process_infos);
  if (m_remote_platform_sp)
    return
      m_remote_platform_sp->FindProcesses(match_info, process_infos);
  return 0;
}

Status PlatformPOSIX::MakeDirectory(const FileSpec &file_spec,
                                    uint32_t file_permissions) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->MakeDirectory(file_spec, file_permissions);
  else
    return Platform::MakeDirectory(file_spec, file_permissions);
}

Status PlatformPOSIX::GetFilePermissions(const FileSpec &file_spec,
                                         uint32_t &file_permissions) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetFilePermissions(file_spec,
                                                    file_permissions);
  else
    return Platform::GetFilePermissions(file_spec, file_permissions);
}

Status PlatformPOSIX::SetFilePermissions(const FileSpec &file_spec,
                                         uint32_t file_permissions) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->SetFilePermissions(file_spec,
                                                    file_permissions);
  else
    return Platform::SetFilePermissions(file_spec, file_permissions);
}

lldb::user_id_t PlatformPOSIX::OpenFile(const FileSpec &file_spec,
                                        uint32_t flags, uint32_t mode,
                                        Status &error) {
  if (IsHost())
    return FileCache::GetInstance().OpenFile(file_spec, flags, mode, error);
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->OpenFile(file_spec, flags, mode, error);
  else
    return Platform::OpenFile(file_spec, flags, mode, error);
}

bool PlatformPOSIX::CloseFile(lldb::user_id_t fd, Status &error) {
  if (IsHost())
    return FileCache::GetInstance().CloseFile(fd, error);
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->CloseFile(fd, error);
  else
    return Platform::CloseFile(fd, error);
}

uint64_t PlatformPOSIX::ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                                 uint64_t dst_len, Status &error) {
  if (IsHost())
    return FileCache::GetInstance().ReadFile(fd, offset, dst, dst_len, error);
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->ReadFile(fd, offset, dst, dst_len, error);
  else
    return Platform::ReadFile(fd, offset, dst, dst_len, error);
}

uint64_t PlatformPOSIX::WriteFile(lldb::user_id_t fd, uint64_t offset,
                                  const void *src, uint64_t src_len,
                                  Status &error) {
  if (IsHost())
    return FileCache::GetInstance().WriteFile(fd, offset, src, src_len, error);
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->WriteFile(fd, offset, src, src_len, error);
  else
    return Platform::WriteFile(fd, offset, src, src_len, error);
}

static uint32_t chown_file(Platform *platform, const char *path,
                           uint32_t uid = UINT32_MAX,
                           uint32_t gid = UINT32_MAX) {
  if (!platform || !path || *path == 0)
    return UINT32_MAX;

  if (uid == UINT32_MAX && gid == UINT32_MAX)
    return 0; // pretend I did chown correctly - actually I just didn't care

  StreamString command;
  command.PutCString("chown ");
  if (uid != UINT32_MAX)
    command.Printf("%d", uid);
  if (gid != UINT32_MAX)
    command.Printf(":%d", gid);
  command.Printf("%s", path);
  int status;
  platform->RunShellCommand(command.GetData(), NULL, &status, NULL, NULL,
                            std::chrono::seconds(10));
  return status;
}

lldb_private::Status
PlatformPOSIX::PutFile(const lldb_private::FileSpec &source,
                       const lldb_private::FileSpec &destination, uint32_t uid,
                       uint32_t gid) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM));

  if (IsHost()) {
    if (FileSpec::Equal(source, destination, true))
      return Status();
    // cp src dst
    // chown uid:gid dst
    std::string src_path(source.GetPath());
    if (src_path.empty())
      return Status("unable to get file path for source");
    std::string dst_path(destination.GetPath());
    if (dst_path.empty())
      return Status("unable to get file path for destination");
    StreamString command;
    command.Printf("cp %s %s", src_path.c_str(), dst_path.c_str());
    int status;
    RunShellCommand(command.GetData(), NULL, &status, NULL, NULL,
                    std::chrono::seconds(10));
    if (status != 0)
      return Status("unable to perform copy");
    if (uid == UINT32_MAX && gid == UINT32_MAX)
      return Status();
    if (chown_file(this, dst_path.c_str(), uid, gid) != 0)
      return Status("unable to perform chown");
    return Status();
  } else if (m_remote_platform_sp) {
    if (GetSupportsRSync()) {
      std::string src_path(source.GetPath());
      if (src_path.empty())
        return Status("unable to get file path for source");
      std::string dst_path(destination.GetPath());
      if (dst_path.empty())
        return Status("unable to get file path for destination");
      StreamString command;
      if (GetIgnoresRemoteHostname()) {
        if (!GetRSyncPrefix())
          command.Printf("rsync %s %s %s", GetRSyncOpts(), src_path.c_str(),
                         dst_path.c_str());
        else
          command.Printf("rsync %s %s %s%s", GetRSyncOpts(), src_path.c_str(),
                         GetRSyncPrefix(), dst_path.c_str());
      } else
        command.Printf("rsync %s %s %s:%s", GetRSyncOpts(), src_path.c_str(),
                       GetHostname(), dst_path.c_str());
      if (log)
        log->Printf("[PutFile] Running command: %s\n", command.GetData());
      int retcode;
      Host::RunShellCommand(command.GetData(), NULL, &retcode, NULL, NULL,
                            std::chrono::minutes(1));
      if (retcode == 0) {
        // Don't chown a local file for a remote system
        //                if (chown_file(this,dst_path.c_str(),uid,gid) != 0)
        //                    return Status("unable to perform chown");
        return Status();
      }
      // if we are still here rsync has failed - let's try the slow way before
      // giving up
    }
  }
  return Platform::PutFile(source, destination, uid, gid);
}

lldb::user_id_t PlatformPOSIX::GetFileSize(const FileSpec &file_spec) {
  if (IsHost()) {
    uint64_t Size;
    if (llvm::sys::fs::file_size(file_spec.GetPath(), Size))
      return 0;
    return Size;
  } else if (m_remote_platform_sp)
    return m_remote_platform_sp->GetFileSize(file_spec);
  else
    return Platform::GetFileSize(file_spec);
}

Status PlatformPOSIX::CreateSymlink(const FileSpec &src, const FileSpec &dst) {
  if (IsHost())
    return FileSystem::Instance().Symlink(src, dst);
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->CreateSymlink(src, dst);
  else
    return Platform::CreateSymlink(src, dst);
}

bool PlatformPOSIX::GetFileExists(const FileSpec &file_spec) {
  if (IsHost())
    return FileSystem::Instance().Exists(file_spec);
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->GetFileExists(file_spec);
  else
    return Platform::GetFileExists(file_spec);
}

Status PlatformPOSIX::Unlink(const FileSpec &file_spec) {
  if (IsHost())
    return llvm::sys::fs::remove(file_spec.GetPath());
  else if (m_remote_platform_sp)
    return m_remote_platform_sp->Unlink(file_spec);
  else
    return Platform::Unlink(file_spec);
}

lldb_private::Status PlatformPOSIX::GetFile(
    const lldb_private::FileSpec &source,      // remote file path
    const lldb_private::FileSpec &destination) // local file path
{
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM));

  // Check the args, first.
  std::string src_path(source.GetPath());
  if (src_path.empty())
    return Status("unable to get file path for source");
  std::string dst_path(destination.GetPath());
  if (dst_path.empty())
    return Status("unable to get file path for destination");
  if (IsHost()) {
    if (FileSpec::Equal(source, destination, true))
      return Status("local scenario->source and destination are the same file "
                    "path: no operation performed");
    // cp src dst
    StreamString cp_command;
    cp_command.Printf("cp %s %s", src_path.c_str(), dst_path.c_str());
    int status;
    RunShellCommand(cp_command.GetData(), NULL, &status, NULL, NULL,
                    std::chrono::seconds(10));
    if (status != 0)
      return Status("unable to perform copy");
    return Status();
  } else if (m_remote_platform_sp) {
    if (GetSupportsRSync()) {
      StreamString command;
      if (GetIgnoresRemoteHostname()) {
        if (!GetRSyncPrefix())
          command.Printf("rsync %s %s %s", GetRSyncOpts(), src_path.c_str(),
                         dst_path.c_str());
        else
          command.Printf("rsync %s %s%s %s", GetRSyncOpts(), GetRSyncPrefix(),
                         src_path.c_str(), dst_path.c_str());
      } else
        command.Printf("rsync %s %s:%s %s", GetRSyncOpts(),
                       m_remote_platform_sp->GetHostname(), src_path.c_str(),
                       dst_path.c_str());
      if (log)
        log->Printf("[GetFile] Running command: %s\n", command.GetData());
      int retcode;
      Host::RunShellCommand(command.GetData(), NULL, &retcode, NULL, NULL,
                            std::chrono::minutes(1));
      if (retcode == 0)
        return Status();
      // If we are here, rsync has failed - let's try the slow way before
      // giving up
    }
    // open src and dst
    // read/write, read/write, read/write, ...
    // close src
    // close dst
    if (log)
      log->Printf("[GetFile] Using block by block transfer....\n");
    Status error;
    user_id_t fd_src = OpenFile(source, File::eOpenOptionRead,
                                lldb::eFilePermissionsFileDefault, error);

    if (fd_src == UINT64_MAX)
      return Status("unable to open source file");

    uint32_t permissions = 0;
    error = GetFilePermissions(source, permissions);

    if (permissions == 0)
      permissions = lldb::eFilePermissionsFileDefault;

    user_id_t fd_dst = FileCache::GetInstance().OpenFile(
        destination, File::eOpenOptionCanCreate | File::eOpenOptionWrite |
                         File::eOpenOptionTruncate,
        permissions, error);

    if (fd_dst == UINT64_MAX) {
      if (error.Success())
        error.SetErrorString("unable to open destination file");
    }

    if (error.Success()) {
      lldb::DataBufferSP buffer_sp(new DataBufferHeap(1024, 0));
      uint64_t offset = 0;
      error.Clear();
      while (error.Success()) {
        const uint64_t n_read = ReadFile(fd_src, offset, buffer_sp->GetBytes(),
                                         buffer_sp->GetByteSize(), error);
        if (error.Fail())
          break;
        if (n_read == 0)
          break;
        if (FileCache::GetInstance().WriteFile(fd_dst, offset,
                                               buffer_sp->GetBytes(), n_read,
                                               error) != n_read) {
          if (!error.Fail())
            error.SetErrorString("unable to write to destination file");
          break;
        }
        offset += n_read;
      }
    }
    // Ignore the close error of src.
    if (fd_src != UINT64_MAX)
      CloseFile(fd_src, error);
    // And close the dst file descriptot.
    if (fd_dst != UINT64_MAX &&
        !FileCache::GetInstance().CloseFile(fd_dst, error)) {
      if (!error.Fail())
        error.SetErrorString("unable to close destination file");
    }
    return error;
  }
  return Platform::GetFile(source, destination);
}

std::string PlatformPOSIX::GetPlatformSpecificConnectionInformation() {
  StreamString stream;
  if (GetSupportsRSync()) {
    stream.PutCString("rsync");
    if ((GetRSyncOpts() && *GetRSyncOpts()) ||
        (GetRSyncPrefix() && *GetRSyncPrefix()) || GetIgnoresRemoteHostname()) {
      stream.Printf(", options: ");
      if (GetRSyncOpts() && *GetRSyncOpts())
        stream.Printf("'%s' ", GetRSyncOpts());
      stream.Printf(", prefix: ");
      if (GetRSyncPrefix() && *GetRSyncPrefix())
        stream.Printf("'%s' ", GetRSyncPrefix());
      if (GetIgnoresRemoteHostname())
        stream.Printf("ignore remote-hostname ");
    }
  }
  if (GetSupportsSSH()) {
    stream.PutCString("ssh");
    if (GetSSHOpts() && *GetSSHOpts())
      stream.Printf(", options: '%s' ", GetSSHOpts());
  }
  if (GetLocalCacheDirectory() && *GetLocalCacheDirectory())
    stream.Printf("cache dir: %s", GetLocalCacheDirectory());
  if (stream.GetSize())
    return stream.GetString();
  else
    return "";
}

bool PlatformPOSIX::CalculateMD5(const FileSpec &file_spec, uint64_t &low,
                                 uint64_t &high) {
  if (IsHost())
    return Platform::CalculateMD5(file_spec, low, high);
  if (m_remote_platform_sp)
    return m_remote_platform_sp->CalculateMD5(file_spec, low, high);
  return false;
}

const lldb::UnixSignalsSP &PlatformPOSIX::GetRemoteUnixSignals() {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteUnixSignals();
  return Platform::GetRemoteUnixSignals();
}

FileSpec PlatformPOSIX::GetRemoteWorkingDirectory() {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteWorkingDirectory();
  else
    return Platform::GetRemoteWorkingDirectory();
}

bool PlatformPOSIX::SetRemoteWorkingDirectory(const FileSpec &working_dir) {
  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->SetRemoteWorkingDirectory(working_dir);
  else
    return Platform::SetRemoteWorkingDirectory(working_dir);
}

bool PlatformPOSIX::GetRemoteOSVersion() {
  if (m_remote_platform_sp) {
    m_os_version = m_remote_platform_sp->GetOSVersion();
    return !m_os_version.empty();
  }
  return false;
}

bool PlatformPOSIX::GetRemoteOSBuildString(std::string &s) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteOSBuildString(s);
  s.clear();
  return false;
}

Environment PlatformPOSIX::GetEnvironment() {
  if (IsRemote()) {
    if (m_remote_platform_sp)
      return m_remote_platform_sp->GetEnvironment();
    return Environment();
  }
  return Host::GetEnvironment();
}

bool PlatformPOSIX::GetRemoteOSKernelDescription(std::string &s) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteOSKernelDescription(s);
  s.clear();
  return false;
}

// Remote Platform subclasses need to override this function
ArchSpec PlatformPOSIX::GetRemoteSystemArchitecture() {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetRemoteSystemArchitecture();
  return ArchSpec();
}

const char *PlatformPOSIX::GetHostname() {
  if (IsHost())
    return Platform::GetHostname();

  if (m_remote_platform_sp)
    return m_remote_platform_sp->GetHostname();
  return NULL;
}

const char *PlatformPOSIX::GetUserName(uint32_t uid) {
  // Check the cache in Platform in case we have already looked this uid up
  const char *user_name = Platform::GetUserName(uid);
  if (user_name)
    return user_name;

  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetUserName(uid);
  return NULL;
}

const char *PlatformPOSIX::GetGroupName(uint32_t gid) {
  const char *group_name = Platform::GetGroupName(gid);
  if (group_name)
    return group_name;

  if (IsRemote() && m_remote_platform_sp)
    return m_remote_platform_sp->GetGroupName(gid);
  return NULL;
}

Status PlatformPOSIX::ConnectRemote(Args &args) {
  Status error;
  if (IsHost()) {
    error.SetErrorStringWithFormat(
        "can't connect to the host platform '%s', always connected",
        GetPluginName().GetCString());
  } else {
    if (!m_remote_platform_sp)
      m_remote_platform_sp =
          Platform::Create(ConstString("remote-gdb-server"), error);

    if (m_remote_platform_sp && error.Success())
      error = m_remote_platform_sp->ConnectRemote(args);
    else
      error.SetErrorString("failed to create a 'remote-gdb-server' platform");

    if (error.Fail())
      m_remote_platform_sp.reset();
  }

  if (error.Success() && m_remote_platform_sp) {
    if (m_option_group_platform_rsync.get() &&
        m_option_group_platform_ssh.get() &&
        m_option_group_platform_caching.get()) {
      if (m_option_group_platform_rsync->m_rsync) {
        SetSupportsRSync(true);
        SetRSyncOpts(m_option_group_platform_rsync->m_rsync_opts.c_str());
        SetRSyncPrefix(m_option_group_platform_rsync->m_rsync_prefix.c_str());
        SetIgnoresRemoteHostname(
            m_option_group_platform_rsync->m_ignores_remote_hostname);
      }
      if (m_option_group_platform_ssh->m_ssh) {
        SetSupportsSSH(true);
        SetSSHOpts(m_option_group_platform_ssh->m_ssh_opts.c_str());
      }
      SetLocalCacheDirectory(
          m_option_group_platform_caching->m_cache_dir.c_str());
    }
  }

  return error;
}

Status PlatformPOSIX::DisconnectRemote() {
  Status error;

  if (IsHost()) {
    error.SetErrorStringWithFormat(
        "can't disconnect from the host platform '%s', always connected",
        GetPluginName().GetCString());
  } else {
    if (m_remote_platform_sp)
      error = m_remote_platform_sp->DisconnectRemote();
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return error;
}

Status PlatformPOSIX::LaunchProcess(ProcessLaunchInfo &launch_info) {
  Status error;

  if (IsHost()) {
    error = Platform::LaunchProcess(launch_info);
  } else {
    if (m_remote_platform_sp)
      error = m_remote_platform_sp->LaunchProcess(launch_info);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return error;
}

lldb_private::Status PlatformPOSIX::KillProcess(const lldb::pid_t pid) {
  if (IsHost())
    return Platform::KillProcess(pid);

  if (m_remote_platform_sp)
    return m_remote_platform_sp->KillProcess(pid);

  return Status("the platform is not currently connected");
}

lldb::ProcessSP PlatformPOSIX::Attach(ProcessAttachInfo &attach_info,
                                      Debugger &debugger, Target *target,
                                      Status &error) {
  lldb::ProcessSP process_sp;
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PLATFORM));

  if (IsHost()) {
    if (target == NULL) {
      TargetSP new_target_sp;

      error = debugger.GetTargetList().CreateTarget(
          debugger, "", "", eLoadDependentsNo, NULL, new_target_sp);
      target = new_target_sp.get();
      if (log)
        log->Printf("PlatformPOSIX::%s created new target", __FUNCTION__);
    } else {
      error.Clear();
      if (log)
        log->Printf("PlatformPOSIX::%s target already existed, setting target",
                    __FUNCTION__);
    }

    if (target && error.Success()) {
      debugger.GetTargetList().SetSelectedTarget(target);
      if (log) {
        ModuleSP exe_module_sp = target->GetExecutableModule();
        log->Printf("PlatformPOSIX::%s set selected target to %p %s",
                    __FUNCTION__, (void *)target,
                    exe_module_sp
                        ? exe_module_sp->GetFileSpec().GetPath().c_str()
                        : "<null>");
      }

      process_sp =
          target->CreateProcess(attach_info.GetListenerForProcess(debugger),
                                attach_info.GetProcessPluginName(), NULL);

      if (process_sp) {
        ListenerSP listener_sp = attach_info.GetHijackListener();
        if (listener_sp == nullptr) {
          listener_sp =
              Listener::MakeListener("lldb.PlatformPOSIX.attach.hijack");
          attach_info.SetHijackListener(listener_sp);
        }
        process_sp->HijackProcessEvents(listener_sp);
        error = process_sp->Attach(attach_info);
      }
    }
  } else {
    if (m_remote_platform_sp)
      process_sp =
          m_remote_platform_sp->Attach(attach_info, debugger, target, error);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return process_sp;
}

lldb::ProcessSP
PlatformPOSIX::DebugProcess(ProcessLaunchInfo &launch_info, Debugger &debugger,
                            Target *target, // Can be NULL, if NULL create a new
                                            // target, else use existing one
                            Status &error) {
  ProcessSP process_sp;

  if (IsHost()) {
    // We are going to hand this process off to debugserver which will be in
    // charge of setting the exit status.  However, we still need to reap it
    // from lldb. So, make sure we use a exit callback which does not set exit
    // status.
    const bool monitor_signals = false;
    launch_info.SetMonitorProcessCallback(
        &ProcessLaunchInfo::NoOpMonitorCallback, monitor_signals);
    process_sp = Platform::DebugProcess(launch_info, debugger, target, error);
  } else {
    if (m_remote_platform_sp)
      process_sp = m_remote_platform_sp->DebugProcess(launch_info, debugger,
                                                      target, error);
    else
      error.SetErrorString("the platform is not currently connected");
  }
  return process_sp;
}

void PlatformPOSIX::CalculateTrapHandlerSymbolNames() {
  m_trap_handlers.push_back(ConstString("_sigtramp"));
}

Status PlatformPOSIX::EvaluateLibdlExpression(
    lldb_private::Process *process, const char *expr_cstr,
    llvm::StringRef expr_prefix, lldb::ValueObjectSP &result_valobj_sp) {
  DynamicLoader *loader = process->GetDynamicLoader();
  if (loader) {
    Status error = loader->CanLoadImage();
    if (error.Fail())
      return error;
  }

  ThreadSP thread_sp(process->GetThreadList().GetExpressionExecutionThread());
  if (!thread_sp)
    return Status("Selected thread isn't valid");

  StackFrameSP frame_sp(thread_sp->GetStackFrameAtIndex(0));
  if (!frame_sp)
    return Status("Frame 0 isn't valid");

  ExecutionContext exe_ctx;
  frame_sp->CalculateExecutionContext(exe_ctx);
  EvaluateExpressionOptions expr_options;
  expr_options.SetUnwindOnError(true);
  expr_options.SetIgnoreBreakpoints(true);
  expr_options.SetExecutionPolicy(eExecutionPolicyAlways);
  expr_options.SetLanguage(eLanguageTypeC_plus_plus);
  expr_options.SetTrapExceptions(false); // dlopen can't throw exceptions, so
                                         // don't do the work to trap them.
  expr_options.SetTimeout(std::chrono::seconds(2));

  Status expr_error;
  ExpressionResults result =
      UserExpression::Evaluate(exe_ctx, expr_options, expr_cstr, expr_prefix,
                               result_valobj_sp, expr_error);
  if (result != eExpressionCompleted)
    return expr_error;

  if (result_valobj_sp->GetError().Fail())
    return result_valobj_sp->GetError();
  return Status();
}

std::unique_ptr<UtilityFunction>
PlatformPOSIX::MakeLoadImageUtilityFunction(ExecutionContext &exe_ctx,
                                            Status &error) {
  // Remember to prepend this with the prefix from
  // GetLibdlFunctionDeclarations. The returned values are all in
  // __lldb_dlopen_result for consistency. The wrapper returns a void * but
  // doesn't use it because UtilityFunctions don't work with void returns at
  // present.
  static const char *dlopen_wrapper_code = R"(
  struct __lldb_dlopen_result {
    void *image_ptr;
    const char *error_str;
  };
  
  extern void *memcpy(void *, const void *, size_t size);
  extern size_t strlen(const char *);
  

  void * __lldb_dlopen_wrapper (const char *name, 
                                const char *path_strings,
                                char *buffer,
                                __lldb_dlopen_result *result_ptr)
  {
    // This is the case where the name is the full path:
    if (!path_strings) {
      result_ptr->image_ptr = dlopen(name, 2);
      if (result_ptr->image_ptr)
        result_ptr->error_str = nullptr;
      return nullptr;
    }
    
    // This is the case where we have a list of paths:
    size_t name_len = strlen(name);
    while (path_strings && path_strings[0] != '\0') {
      size_t path_len = strlen(path_strings);
      memcpy((void *) buffer, (void *) path_strings, path_len);
      buffer[path_len] = '/';
      char *target_ptr = buffer+path_len+1; 
      memcpy((void *) target_ptr, (void *) name, name_len + 1);
      result_ptr->image_ptr = dlopen(buffer, 2);
      if (result_ptr->image_ptr) {
        result_ptr->error_str = nullptr;
        break;
      }
      result_ptr->error_str = dlerror();
      path_strings = path_strings + path_len + 1;
    }
    return nullptr;
  }
  )";

  static const char *dlopen_wrapper_name = "__lldb_dlopen_wrapper";
  Process *process = exe_ctx.GetProcessSP().get();
  // Insert the dlopen shim defines into our generic expression:
  std::string expr(GetLibdlFunctionDeclarations(process));
  expr.append(dlopen_wrapper_code);
  Status utility_error;
  DiagnosticManager diagnostics;
  
  std::unique_ptr<UtilityFunction> dlopen_utility_func_up(process
      ->GetTarget().GetUtilityFunctionForLanguage(expr.c_str(),
                                                  eLanguageTypeObjC,
                                                  dlopen_wrapper_name,
                                                  utility_error));
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat("dlopen error: could not make utility"
                                   "function: %s", utility_error.AsCString());
    return nullptr;
  }
  if (!dlopen_utility_func_up->Install(diagnostics, exe_ctx)) {
    error.SetErrorStringWithFormat("dlopen error: could not install utility"
                                   "function: %s", 
                                   diagnostics.GetString().c_str());
    return nullptr;
  }

  Value value;
  ValueList arguments;
  FunctionCaller *do_dlopen_function = nullptr;

  // Fetch the clang types we will need:
  ClangASTContext *ast = process->GetTarget().GetScratchClangASTContext();

  CompilerType clang_void_pointer_type
      = ast->GetBasicType(eBasicTypeVoid).GetPointerType();
  CompilerType clang_char_pointer_type
        = ast->GetBasicType(eBasicTypeChar).GetPointerType();

  // We are passing four arguments, the basename, the list of places to look,
  // a buffer big enough for all the path + name combos, and
  // a pointer to the storage we've made for the result:
  value.SetValueType(Value::eValueTypeScalar);
  value.SetCompilerType(clang_void_pointer_type);
  arguments.PushValue(value);
  value.SetCompilerType(clang_char_pointer_type);
  arguments.PushValue(value);
  arguments.PushValue(value);
  arguments.PushValue(value);
  
  do_dlopen_function = dlopen_utility_func_up->MakeFunctionCaller(
      clang_void_pointer_type, arguments, exe_ctx.GetThreadSP(), utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat("dlopen error: could not make function"
                                   "caller: %s", utility_error.AsCString());
    return nullptr;
  }
  
  do_dlopen_function = dlopen_utility_func_up->GetFunctionCaller();
  if (!do_dlopen_function) {
    error.SetErrorString("dlopen error: could not get function caller.");
    return nullptr;
  }
  
  // We made a good utility function, so cache it in the process:
  return dlopen_utility_func_up;
}

uint32_t PlatformPOSIX::DoLoadImage(lldb_private::Process *process,
                                    const lldb_private::FileSpec &remote_file,
                                    const std::vector<std::string> *paths,
                                    lldb_private::Status &error,
                                    lldb_private::FileSpec *loaded_image) {
  if (loaded_image)
    loaded_image->Clear();

  std::string path;
  path = remote_file.GetPath();
  
  ThreadSP thread_sp = process->GetThreadList().GetExpressionExecutionThread();
  if (!thread_sp) {
    error.SetErrorString("dlopen error: no thread available to call dlopen.");
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  DiagnosticManager diagnostics;
  
  ExecutionContext exe_ctx;
  thread_sp->CalculateExecutionContext(exe_ctx);

  Status utility_error;
  UtilityFunction *dlopen_utility_func;
  ValueList arguments;
  FunctionCaller *do_dlopen_function = nullptr;

  // The UtilityFunction is held in the Process.  Platforms don't track the
  // lifespan of the Targets that use them, we can't put this in the Platform.
  dlopen_utility_func = process->GetLoadImageUtilityFunction(
      this, [&]() -> std::unique_ptr<UtilityFunction> {
        return MakeLoadImageUtilityFunction(exe_ctx, error);
      });
  // If we couldn't make it, the error will be in error, so we can exit here.
  if (!dlopen_utility_func)
    return LLDB_INVALID_IMAGE_TOKEN;
    
  do_dlopen_function = dlopen_utility_func->GetFunctionCaller();
  if (!do_dlopen_function) {
    error.SetErrorString("dlopen error: could not get function caller.");
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  arguments = do_dlopen_function->GetArgumentValues();
  
  // Now insert the path we are searching for and the result structure into the
  // target.
  uint32_t permissions = ePermissionsReadable|ePermissionsWritable;
  size_t path_len = path.size() + 1;
  lldb::addr_t path_addr = process->AllocateMemory(path_len, 
                                                   permissions,
                                                   utility_error);
  if (path_addr == LLDB_INVALID_ADDRESS) {
    error.SetErrorStringWithFormat("dlopen error: could not allocate memory"
                                    "for path: %s", utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make sure we deallocate the input string memory:
  CleanUp path_cleanup([process, path_addr] { 
      process->DeallocateMemory(path_addr); 
  });
  
  process->WriteMemory(path_addr, path.c_str(), path_len, utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat("dlopen error: could not write path string:"
                                    " %s", utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make space for our return structure.  It is two pointers big: the token
  // and the error string.
  const uint32_t addr_size = process->GetAddressByteSize();
  lldb::addr_t return_addr = process->CallocateMemory(2*addr_size,
                                                      permissions,
                                                      utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat("dlopen error: could not allocate memory"
                                    "for path: %s", utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make sure we deallocate the result structure memory
  CleanUp return_cleanup([process, return_addr] {
      process->DeallocateMemory(return_addr);
  });
  
  // This will be the address of the storage for paths, if we are using them,
  // or nullptr to signal we aren't.
  lldb::addr_t path_array_addr = 0x0;
  llvm::Optional<CleanUp> path_array_cleanup;

  // This is the address to a buffer large enough to hold the largest path
  // conjoined with the library name we're passing in.  This is a convenience 
  // to avoid having to call malloc in the dlopen function.
  lldb::addr_t buffer_addr = 0x0;
  llvm::Optional<CleanUp> buffer_cleanup;
  
  // Set the values into our args and write them to the target:
  if (paths != nullptr) {
    // First insert the paths into the target.  This is expected to be a 
    // continuous buffer with the strings laid out null terminated and
    // end to end with an empty string terminating the buffer.
    // We also compute the buffer's required size as we go.
    size_t buffer_size = 0;
    std::string path_array;
    for (auto path : *paths) {
      // Don't insert empty paths, they will make us abort the path
      // search prematurely.
      if (path.empty())
        continue;
      size_t path_size = path.size();
      path_array.append(path);
      path_array.push_back('\0');
      if (path_size > buffer_size)
        buffer_size = path_size;
    }
    path_array.push_back('\0');
    
    path_array_addr = process->AllocateMemory(path_array.size(), 
                                              permissions,
                                              utility_error);
    if (path_array_addr == LLDB_INVALID_ADDRESS) {
      error.SetErrorStringWithFormat("dlopen error: could not allocate memory"
                                      "for path array: %s", 
                                      utility_error.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
    
    // Make sure we deallocate the paths array.
    path_array_cleanup.emplace([process, path_array_addr] { 
        process->DeallocateMemory(path_array_addr); 
    });

    process->WriteMemory(path_array_addr, path_array.data(), 
                         path_array.size(), utility_error);

    if (utility_error.Fail()) {
      error.SetErrorStringWithFormat("dlopen error: could not write path array:"
                                     " %s", utility_error.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
    // Now make spaces in the target for the buffer.  We need to add one for
    // the '/' that the utility function will insert and one for the '\0':
    buffer_size += path.size() + 2;
    
    buffer_addr = process->AllocateMemory(buffer_size, 
                                          permissions,
                                          utility_error);
    if (buffer_addr == LLDB_INVALID_ADDRESS) {
      error.SetErrorStringWithFormat("dlopen error: could not allocate memory"
                                      "for buffer: %s", 
                                      utility_error.AsCString());
      return LLDB_INVALID_IMAGE_TOKEN;
    }
  
    // Make sure we deallocate the buffer memory:
    buffer_cleanup.emplace([process, buffer_addr] { 
        process->DeallocateMemory(buffer_addr); 
    });
  }
    
  arguments.GetValueAtIndex(0)->GetScalar() = path_addr;
  arguments.GetValueAtIndex(1)->GetScalar() = path_array_addr;
  arguments.GetValueAtIndex(2)->GetScalar() = buffer_addr;
  arguments.GetValueAtIndex(3)->GetScalar() = return_addr;

  lldb::addr_t func_args_addr = LLDB_INVALID_ADDRESS;
  
  diagnostics.Clear();
  if (!do_dlopen_function->WriteFunctionArguments(exe_ctx, 
                                                 func_args_addr,
                                                 arguments,
                                                 diagnostics)) {
    error.SetErrorStringWithFormat("dlopen error: could not write function "
                                   "arguments: %s", 
                                   diagnostics.GetString().c_str());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Make sure we clean up the args structure.  We can't reuse it because the
  // Platform lives longer than the process and the Platforms don't get a
  // signal to clean up cached data when a process goes away.
  CleanUp args_cleanup([do_dlopen_function, &exe_ctx, func_args_addr] {
    do_dlopen_function->DeallocateFunctionResults(exe_ctx, func_args_addr);
  });
  
  // Now run the caller:
  EvaluateExpressionOptions options;
  options.SetExecutionPolicy(eExecutionPolicyAlways);
  options.SetLanguage(eLanguageTypeC_plus_plus);
  options.SetIgnoreBreakpoints(true);
  options.SetUnwindOnError(true);
  options.SetTrapExceptions(false); // dlopen can't throw exceptions, so
                                    // don't do the work to trap them.
  options.SetTimeout(std::chrono::seconds(2));
  options.SetIsForUtilityExpr(true);

  Value return_value;
  // Fetch the clang types we will need:
  ClangASTContext *ast = process->GetTarget().GetScratchClangASTContext();

  CompilerType clang_void_pointer_type
      = ast->GetBasicType(eBasicTypeVoid).GetPointerType();

  return_value.SetCompilerType(clang_void_pointer_type);
  
  ExpressionResults results = do_dlopen_function->ExecuteFunction(
      exe_ctx, &func_args_addr, options, diagnostics, return_value);
  if (results != eExpressionCompleted) {
    error.SetErrorStringWithFormat("dlopen error: failed executing "
                                   "dlopen wrapper function: %s", 
                                   diagnostics.GetString().c_str());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // Read the dlopen token from the return area:
  lldb::addr_t token = process->ReadPointerFromMemory(return_addr, 
                                                      utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat("dlopen error: could not read the return "
                                    "struct: %s", utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  // The dlopen succeeded!
  if (token != 0x0) {
    if (loaded_image && buffer_addr != 0x0)
    {
      // Capture the image which was loaded.  We leave it in the buffer on
      // exit from the dlopen function, so we can just read it from there:
      std::string name_string;
      process->ReadCStringFromMemory(buffer_addr, name_string, utility_error);
      if (utility_error.Success())
        loaded_image->SetFile(name_string, llvm::sys::path::Style::posix);
    }
    return process->AddImageToken(token);
  }
    
  // We got an error, lets read in the error string:
  std::string dlopen_error_str;
  lldb::addr_t error_addr 
    = process->ReadPointerFromMemory(return_addr + addr_size, utility_error);
  if (utility_error.Fail()) {
    error.SetErrorStringWithFormat("dlopen error: could not read error string: "
                                    "%s", utility_error.AsCString());
    return LLDB_INVALID_IMAGE_TOKEN;
  }
  
  size_t num_chars = process->ReadCStringFromMemory(error_addr + addr_size, 
                                                    dlopen_error_str, 
                                                    utility_error);
  if (utility_error.Success() && num_chars > 0)
    error.SetErrorStringWithFormat("dlopen error: %s",
                                   dlopen_error_str.c_str());
  else
    error.SetErrorStringWithFormat("dlopen failed for unknown reasons.");

  return LLDB_INVALID_IMAGE_TOKEN;
}

Status PlatformPOSIX::UnloadImage(lldb_private::Process *process,
                                  uint32_t image_token) {
  const addr_t image_addr = process->GetImagePtrFromToken(image_token);
  if (image_addr == LLDB_INVALID_ADDRESS)
    return Status("Invalid image token");

  StreamString expr;
  expr.Printf("dlclose((void *)0x%" PRIx64 ")", image_addr);
  llvm::StringRef prefix = GetLibdlFunctionDeclarations(process);
  lldb::ValueObjectSP result_valobj_sp;
  Status error = EvaluateLibdlExpression(process, expr.GetData(), prefix,
                                         result_valobj_sp);
  if (error.Fail())
    return error;

  if (result_valobj_sp->GetError().Fail())
    return result_valobj_sp->GetError();

  Scalar scalar;
  if (result_valobj_sp->ResolveValue(scalar)) {
    if (scalar.UInt(1))
      return Status("expression failed: \"%s\"", expr.GetData());
    process->ResetImageToken(image_token);
  }
  return Status();
}

lldb::ProcessSP PlatformPOSIX::ConnectProcess(llvm::StringRef connect_url,
                                              llvm::StringRef plugin_name,
                                              lldb_private::Debugger &debugger,
                                              lldb_private::Target *target,
                                              lldb_private::Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->ConnectProcess(connect_url, plugin_name,
                                                debugger, target, error);

  return Platform::ConnectProcess(connect_url, plugin_name, debugger, target,
                                  error);
}

llvm::StringRef
PlatformPOSIX::GetLibdlFunctionDeclarations(lldb_private::Process *process) {
  return R"(
              extern "C" void* dlopen(const char*, int);
              extern "C" void* dlsym(void*, const char*);
              extern "C" int   dlclose(void*);
              extern "C" char* dlerror(void);
             )";
}

size_t PlatformPOSIX::ConnectToWaitingProcesses(Debugger &debugger,
                                                Status &error) {
  if (m_remote_platform_sp)
    return m_remote_platform_sp->ConnectToWaitingProcesses(debugger, error);
  return Platform::ConnectToWaitingProcesses(debugger, error);
}

ConstString PlatformPOSIX::GetFullNameForDylib(ConstString basename) {
  if (basename.IsEmpty())
    return basename;

  StreamString stream;
  stream.Printf("lib%s.so", basename.GetCString());
  return ConstString(stream.GetString());
}
