//===-- HostNativeThread.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostNativeThread_h_
#define lldb_Host_HostNativeThread_h_

#include "HostNativeThreadForward.h"

#if defined(_WIN32)
#include "lldb/Host/windows/HostThreadWindows.h"
#elif defined(__APPLE__)
#include "lldb/Host/macosx/HostThreadMacOSX.h"
#else
#include "lldb/Host/posix/HostThreadPosix.h"
#endif

#endif
