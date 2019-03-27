//===-- HostInfoNetBSD.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/netbsd/HostInfoNetBSD.h"

#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

using namespace lldb_private;

llvm::VersionTuple HostInfoNetBSD::GetOSVersion() {
  struct utsname un;

  ::memset(&un, 0, sizeof(un));
  if (::uname(&un) < 0)
    return llvm::VersionTuple();

  /* Accept versions like 7.99.21 and 6.1_STABLE */
  uint32_t major, minor, update;
  int status = ::sscanf(un.release, "%" PRIu32 ".%" PRIu32 ".%" PRIu32, &major,
                        &minor, &update);
  switch (status) {
  case 1:
    return llvm::VersionTuple(major);
  case 2:
    return llvm::VersionTuple(major, minor);
  case 3:
    return llvm::VersionTuple(major, minor, update);
  }
  return llvm::VersionTuple();
}

bool HostInfoNetBSD::GetOSBuildString(std::string &s) {
  int mib[2] = {CTL_KERN, KERN_OSREV};
  char osrev_str[12];
  int osrev = 0;
  size_t osrev_len = sizeof(osrev);

  if (::sysctl(mib, 2, &osrev, &osrev_len, NULL, 0) == 0) {
    ::snprintf(osrev_str, sizeof(osrev_str), "%-10.10d", osrev);
    s.assign(osrev_str);
    return true;
  }

  s.clear();
  return false;
}

bool HostInfoNetBSD::GetOSKernelDescription(std::string &s) {
  struct utsname un;

  ::memset(&un, 0, sizeof(un));
  s.clear();

  if (::uname(&un) < 0)
    return false;

  s.assign(un.version);

  return true;
}

FileSpec HostInfoNetBSD::GetProgramFileSpec() {
  static FileSpec g_program_filespec;

  if (!g_program_filespec) {
    static const int name[] = {
        CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME,
    };
    char path[MAXPATHLEN];
    size_t len;

    len = sizeof(path);
    if (sysctl(name, __arraycount(name), path, &len, NULL, 0) != -1) {
      g_program_filespec.SetFile(path, FileSpec::Style::native);
    }
  }
  return g_program_filespec;
}
