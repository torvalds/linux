//===-- source/Host/openbsd/Host.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <sys/types.h>

#include <sys/signal.h>
#include <sys/exec.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <stdio.h>

#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/Support/Host.h"

extern "C" {
extern char **environ;
}

using namespace lldb;
using namespace lldb_private;

Environment Host::GetEnvironment() {
  Environment env;
  char *v;
  char **var = environ;
  for (; var != NULL && *var != NULL; ++var) {
    v = strchr(*var, (int)'-');
    if (v == NULL)
      continue;
    env.insert(v);
  }
  return env;
}

static bool
GetOpenBSDProcessArgs(const ProcessInstanceInfoMatch *match_info_ptr,
                      ProcessInstanceInfo &process_info) {
  if (process_info.ProcessIDIsValid()) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ARGS,
                  (int)process_info.GetProcessID()};

    char arg_data[8192];
    size_t arg_data_size = sizeof(arg_data);
    if (::sysctl(mib, 4, arg_data, &arg_data_size, NULL, 0) == 0) {
      DataExtractor data(arg_data, arg_data_size, endian::InlHostByteOrder(),
                         sizeof(void *));
      lldb::offset_t offset = 0;
      const char *cstr;

      cstr = data.GetCStr(&offset);
      if (cstr) {
        process_info.GetExecutableFile().SetFile(cstr, FileSpec::Style::native);

        if (!(match_info_ptr == NULL ||
              NameMatches(
                  process_info.GetExecutableFile().GetFilename().GetCString(),
                  match_info_ptr->GetNameMatchType(),
                  match_info_ptr->GetProcessInfo().GetName())))
          return false;

        Args &proc_args = process_info.GetArguments();
        while (1) {
          const uint8_t *p = data.PeekData(offset, 1);
          while ((p != NULL) && (*p == '\0') && offset < arg_data_size) {
            ++offset;
            p = data.PeekData(offset, 1);
          }
          if (p == NULL || offset >= arg_data_size)
            return true;

          cstr = data.GetCStr(&offset);
          if (cstr)
            proc_args.AppendArgument(llvm::StringRef(cstr));
          else
            return true;
        }
      }
    }
  }
  return false;
}

static bool GetOpenBSDProcessCPUType(ProcessInstanceInfo &process_info) {
  if (process_info.ProcessIDIsValid()) {
    process_info.GetArchitecture() =
        HostInfo::GetArchitecture(HostInfo::eArchKindDefault);
    return true;
  }
  process_info.GetArchitecture().Clear();
  return false;
}

static bool GetOpenBSDProcessUserAndGroup(ProcessInstanceInfo &process_info) {
  struct kinfo_proc proc_kinfo;
  size_t proc_kinfo_size;

  if (process_info.ProcessIDIsValid()) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID,
                  (int)process_info.GetProcessID()};
    proc_kinfo_size = sizeof(struct kinfo_proc);

    if (::sysctl(mib, 4, &proc_kinfo, &proc_kinfo_size, NULL, 0) == 0) {
      if (proc_kinfo_size > 0) {
        process_info.SetParentProcessID(proc_kinfo.p_ppid);
        process_info.SetUserID(proc_kinfo.p_ruid);
        process_info.SetGroupID(proc_kinfo.p_rgid);
        process_info.SetEffectiveUserID(proc_kinfo.p_uid);
	process_info.SetEffectiveGroupID(proc_kinfo.p_gid);
        return true;
      }
    }
  }
  process_info.SetParentProcessID(LLDB_INVALID_PROCESS_ID);
  process_info.SetUserID(UINT32_MAX);
  process_info.SetGroupID(UINT32_MAX);
  process_info.SetEffectiveUserID(UINT32_MAX);
  process_info.SetEffectiveGroupID(UINT32_MAX);
  return false;
}

uint32_t Host::FindProcesses(const ProcessInstanceInfoMatch &match_info,
                             ProcessInstanceInfoList &process_infos) {
  std::vector<struct kinfo_proc> kinfos;

  int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};

  size_t pid_data_size = 0;
  if (::sysctl(mib, 3, NULL, &pid_data_size, NULL, 0) != 0)
    return 0;

  // Add a few extra in case a few more show up
  const size_t estimated_pid_count =
      (pid_data_size / sizeof(struct kinfo_proc)) + 10;

  kinfos.resize(estimated_pid_count);
  pid_data_size = kinfos.size() * sizeof(struct kinfo_proc);

  if (::sysctl(mib, 3, &kinfos[0], &pid_data_size, NULL, 0) != 0)
    return 0;

  const size_t actual_pid_count = (pid_data_size / sizeof(struct kinfo_proc));

  bool all_users = match_info.GetMatchAllUsers();
  const ::pid_t our_pid = getpid();
  const uid_t our_uid = getuid();
  for (size_t i = 0; i < actual_pid_count; i++) {
    const struct kinfo_proc &kinfo = kinfos[i];
    const bool kinfo_user_matches = (all_users || (kinfo.p_ruid == our_uid) ||
                                     // Special case, if lldb is being run as
                                     // root we can attach to anything.
                                     (our_uid == 0));

    if (kinfo_user_matches == false || // Make sure the user is acceptable
        kinfo.p_pid == our_pid ||     // Skip this process
        kinfo.p_pid == 0 ||           // Skip kernel (kernel pid is zero)
        kinfo.p_stat == SZOMB ||      // Zombies are bad, they like brains...
        kinfo.p_psflags & PS_TRACED || // Being debugged?
        kinfo.p_flag & P_WEXIT)       // Working on exiting
      continue;

    ProcessInstanceInfo process_info;
    process_info.SetProcessID(kinfo.p_pid);
    process_info.SetParentProcessID(kinfo.p_ppid);
    process_info.SetUserID(kinfo.p_ruid);
    process_info.SetGroupID(kinfo.p_rgid);
    process_info.SetEffectiveUserID(kinfo.p_svuid);
    process_info.SetEffectiveGroupID(kinfo.p_svgid);

    // Make sure our info matches before we go fetch the name and cpu type
    if (match_info.Matches(process_info) &&
        GetOpenBSDProcessArgs(&match_info, process_info)) {
      GetOpenBSDProcessCPUType(process_info);
      if (match_info.Matches(process_info))
        process_infos.Append(process_info);
    }
  }

  return process_infos.GetSize();
}

bool Host::GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  process_info.SetProcessID(pid);

  if (GetOpenBSDProcessArgs(NULL, process_info)) {
    // should use libprocstat instead of going right into sysctl?
    GetOpenBSDProcessCPUType(process_info);
    GetOpenBSDProcessUserAndGroup(process_info);
    return true;
  }

  process_info.Clear();
  return false;
}

Status Host::ShellExpandArguments(ProcessLaunchInfo &launch_info) {
  return Status("unimplemented");
}
