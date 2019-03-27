//===-- HostInfoFreeBSD.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_freebsd_HostInfoFreeBSD_h_
#define lldb_Host_freebsd_HostInfoFreeBSD_h_

#include "lldb/Host/posix/HostInfoPosix.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/VersionTuple.h"

namespace lldb_private {

class HostInfoFreeBSD : public HostInfoPosix {
public:
  static llvm::VersionTuple GetOSVersion();
  static bool GetOSBuildString(std::string &s);
  static bool GetOSKernelDescription(std::string &s);
  static FileSpec GetProgramFileSpec();
};
}

#endif
