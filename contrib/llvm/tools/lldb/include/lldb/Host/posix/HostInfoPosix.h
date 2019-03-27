//===-- HostInfoPosix.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_posix_HostInfoPosix_h_
#define lldb_Host_posix_HostInfoPosix_h_

#include "lldb/Host/HostInfoBase.h"
#include "lldb/Utility/FileSpec.h"

namespace lldb_private {

class HostInfoPosix : public HostInfoBase {
  friend class HostInfoBase;

public:
  static size_t GetPageSize();
  static bool GetHostname(std::string &s);
  static const char *LookupUserName(uint32_t uid, std::string &user_name);
  static const char *LookupGroupName(uint32_t gid, std::string &group_name);

  static uint32_t GetUserID();
  static uint32_t GetGroupID();
  static uint32_t GetEffectiveUserID();
  static uint32_t GetEffectiveGroupID();

  static FileSpec GetDefaultShell();

  static bool GetEnvironmentVar(const std::string &var_name, std::string &var);

  static bool ComputePathRelativeToLibrary(FileSpec &file_spec,
                                           llvm::StringRef dir);

protected:
  static bool ComputeSupportExeDirectory(FileSpec &file_spec);
  static bool ComputeHeaderDirectory(FileSpec &file_spec);
};
}

#endif
