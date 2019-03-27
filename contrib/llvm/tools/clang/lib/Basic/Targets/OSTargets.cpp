//===--- OSTargets.cpp - Implement OS target feature support --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements OS specific TargetInfo types.
//===----------------------------------------------------------------------===//

#include "OSTargets.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::targets;

namespace clang {
namespace targets {

void getDarwinDefines(MacroBuilder &Builder, const LangOptions &Opts,
                      const llvm::Triple &Triple, StringRef &PlatformName,
                      VersionTuple &PlatformMinVersion) {
  Builder.defineMacro("__APPLE_CC__", "6000");
  Builder.defineMacro("__APPLE__");
  Builder.defineMacro("__STDC_NO_THREADS__");
  Builder.defineMacro("OBJC_NEW_PROPERTIES");
  // AddressSanitizer doesn't play well with source fortification, which is on
  // by default on Darwin.
  if (Opts.Sanitize.has(SanitizerKind::Address))
    Builder.defineMacro("_FORTIFY_SOURCE", "0");

  // Darwin defines __weak, __strong, and __unsafe_unretained even in C mode.
  if (!Opts.ObjC) {
    // __weak is always defined, for use in blocks and with objc pointers.
    Builder.defineMacro("__weak", "__attribute__((objc_gc(weak)))");
    Builder.defineMacro("__strong", "");
    Builder.defineMacro("__unsafe_unretained", "");
  }

  if (Opts.Static)
    Builder.defineMacro("__STATIC__");
  else
    Builder.defineMacro("__DYNAMIC__");

  if (Opts.POSIXThreads)
    Builder.defineMacro("_REENTRANT");

  // Get the platform type and version number from the triple.
  unsigned Maj, Min, Rev;
  if (Triple.isMacOSX()) {
    Triple.getMacOSXVersion(Maj, Min, Rev);
    PlatformName = "macos";
  } else {
    Triple.getOSVersion(Maj, Min, Rev);
    PlatformName = llvm::Triple::getOSTypeName(Triple.getOS());
  }

  // If -target arch-pc-win32-macho option specified, we're
  // generating code for Win32 ABI. No need to emit
  // __ENVIRONMENT_XX_OS_VERSION_MIN_REQUIRED__.
  if (PlatformName == "win32") {
    PlatformMinVersion = VersionTuple(Maj, Min, Rev);
    return;
  }

  // Set the appropriate OS version define.
  if (Triple.isiOS()) {
    assert(Maj < 100 && Min < 100 && Rev < 100 && "Invalid version!");
    char Str[7];
    if (Maj < 10) {
      Str[0] = '0' + Maj;
      Str[1] = '0' + (Min / 10);
      Str[2] = '0' + (Min % 10);
      Str[3] = '0' + (Rev / 10);
      Str[4] = '0' + (Rev % 10);
      Str[5] = '\0';
    } else {
      // Handle versions >= 10.
      Str[0] = '0' + (Maj / 10);
      Str[1] = '0' + (Maj % 10);
      Str[2] = '0' + (Min / 10);
      Str[3] = '0' + (Min % 10);
      Str[4] = '0' + (Rev / 10);
      Str[5] = '0' + (Rev % 10);
      Str[6] = '\0';
    }
    if (Triple.isTvOS())
      Builder.defineMacro("__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__", Str);
    else
      Builder.defineMacro("__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__",
                          Str);

  } else if (Triple.isWatchOS()) {
    assert(Maj < 10 && Min < 100 && Rev < 100 && "Invalid version!");
    char Str[6];
    Str[0] = '0' + Maj;
    Str[1] = '0' + (Min / 10);
    Str[2] = '0' + (Min % 10);
    Str[3] = '0' + (Rev / 10);
    Str[4] = '0' + (Rev % 10);
    Str[5] = '\0';
    Builder.defineMacro("__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__", Str);
  } else if (Triple.isMacOSX()) {
    // Note that the Driver allows versions which aren't representable in the
    // define (because we only get a single digit for the minor and micro
    // revision numbers). So, we limit them to the maximum representable
    // version.
    assert(Maj < 100 && Min < 100 && Rev < 100 && "Invalid version!");
    char Str[7];
    if (Maj < 10 || (Maj == 10 && Min < 10)) {
      Str[0] = '0' + (Maj / 10);
      Str[1] = '0' + (Maj % 10);
      Str[2] = '0' + std::min(Min, 9U);
      Str[3] = '0' + std::min(Rev, 9U);
      Str[4] = '\0';
    } else {
      // Handle versions > 10.9.
      Str[0] = '0' + (Maj / 10);
      Str[1] = '0' + (Maj % 10);
      Str[2] = '0' + (Min / 10);
      Str[3] = '0' + (Min % 10);
      Str[4] = '0' + (Rev / 10);
      Str[5] = '0' + (Rev % 10);
      Str[6] = '\0';
    }
    Builder.defineMacro("__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__", Str);
  }

  // Tell users about the kernel if there is one.
  if (Triple.isOSDarwin())
    Builder.defineMacro("__MACH__");

  PlatformMinVersion = VersionTuple(Maj, Min, Rev);
}
} // namespace targets
} // namespace clang
