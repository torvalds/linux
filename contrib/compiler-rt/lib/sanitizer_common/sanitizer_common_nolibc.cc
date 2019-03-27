//===-- sanitizer_common_nolibc.cc ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains stubs for libc function to facilitate optional use of
// libc in no-libcdep sources.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#include "sanitizer_common.h"
#include "sanitizer_libc.h"

namespace __sanitizer {

// The Windows implementations of these functions use the win32 API directly,
// bypassing libc.
#if !SANITIZER_WINDOWS
#if SANITIZER_LINUX
void LogMessageOnPrintf(const char *str) {}
#endif
void WriteToSyslog(const char *buffer) {}
void Abort() { internal__exit(1); }
void SleepForSeconds(int seconds) { internal_sleep(seconds); }
#endif // !SANITIZER_WINDOWS

#if !SANITIZER_WINDOWS && !SANITIZER_MAC
void ListOfModules::init() {}
#endif

}  // namespace __sanitizer
