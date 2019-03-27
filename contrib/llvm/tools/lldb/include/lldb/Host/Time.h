//===-- Time.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Include system time headers, adding missing functions as necessary

#ifndef liblldb_Host_Time_h_
#define liblldb_Host_Time_h_

#ifdef __ANDROID__
#include <android/api-level.h>
#endif

#if defined(__ANDROID_API__) && __ANDROID_API__ < 21
#include <time64.h>
extern time_t timegm(struct tm *t);
#else
#include <time.h>
#endif

#endif // liblldb_Host_Time_h_
