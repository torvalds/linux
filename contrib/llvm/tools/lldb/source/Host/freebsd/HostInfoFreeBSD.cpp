//===-- HostInfoFreeBSD.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/freebsd/HostInfoFreeBSD.h"

#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

using namespace lldb_private;

llvm::VersionTuple HostInfoFreeBSD::GetOSVersion() {
  struct utsname un;

  ::memset(&un, 0, sizeof(utsname));
  if (uname(&un) < 0)
    return llvm::VersionTuple();

  unsigned major, minor;
  if (2 == sscanf(un.release, "%u.%u", &major, &minor))
    return llvm::VersionTuple(major, minor);
  return llvm::VersionTuple();
}

bool HostInfoFreeBSD::GetOSBuildString(std::string &s) {
  int mib[2] = {CTL_KERN, KERN_OSREV};
  char osrev_str[12];
  uint32_t osrev = 0;
  size_t osrev_len = sizeof(osrev);

  if (::sysctl(mib, 2, &osrev, &osrev_len, NULL, 0) == 0) {
    ::snprintf(osrev_str, sizeof(osrev_str), "%-8.8u", osrev);
    s.assign(osrev_str);
    return true;
  }

  s.clear();
  return false;
}

bool HostInfoFreeBSD::GetOSKernelDescription(std::string &s) {
  struct utsname un;

  ::memset(&un, 0, sizeof(utsname));
  s.clear();

  if (uname(&un) < 0)
    return false;

  s.assign(un.version);

  return true;
}

FileSpec HostInfoFreeBSD::GetProgramFileSpec() {
  static FileSpec g_program_filespec;
  if (!g_program_filespec) {
    int exe_path_mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, getpid()};
    size_t exe_path_size;
    if (sysctl(exe_path_mib, 4, NULL, &exe_path_size, NULL, 0) == 0) {
      char *exe_path = new char[exe_path_size];
      if (sysctl(exe_path_mib, 4, exe_path, &exe_path_size, NULL, 0) == 0)
        g_program_filespec.SetFile(exe_path, FileSpec::Style::native);
      delete[] exe_path;
    }
  }
  return g_program_filespec;
}
