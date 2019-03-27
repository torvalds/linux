//===- llvm/Support/Unix/Host.inc -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the UNIX Host support.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include <cctype>
#include <string>
#include <sys/utsname.h>

using namespace llvm;

static std::string getOSVersion() {
  struct utsname info;

  if (uname(&info))
    return "";

  return info.release;
}

static std::string updateTripleOSVersion(std::string TargetTripleString) {
  // On darwin, we want to update the version to match that of the target.
  std::string::size_type DarwinDashIdx = TargetTripleString.find("-darwin");
  if (DarwinDashIdx != std::string::npos) {
    TargetTripleString.resize(DarwinDashIdx + strlen("-darwin"));
    TargetTripleString += getOSVersion();
    return TargetTripleString;
  }
  std::string::size_type MacOSDashIdx = TargetTripleString.find("-macos");
  if (MacOSDashIdx != std::string::npos) {
    TargetTripleString.resize(MacOSDashIdx);
    // Reset the OS to darwin as the OS version from `uname` doesn't use the
    // macOS version scheme.
    TargetTripleString += "-darwin";
    TargetTripleString += getOSVersion();
  }
  return TargetTripleString;
}

std::string sys::getDefaultTargetTriple() {
  std::string TargetTripleString =
      updateTripleOSVersion(LLVM_DEFAULT_TARGET_TRIPLE);

  // Override the default target with an environment variable named by
  // LLVM_TARGET_TRIPLE_ENV.
#if defined(LLVM_TARGET_TRIPLE_ENV)
  if (const char *EnvTriple = std::getenv(LLVM_TARGET_TRIPLE_ENV))
    TargetTripleString = EnvTriple;
#endif

  return TargetTripleString;
}
