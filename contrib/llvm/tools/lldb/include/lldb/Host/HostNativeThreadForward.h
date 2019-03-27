//===-- HostNativeThreadForward.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostNativeThreadForward_h_
#define lldb_Host_HostNativeThreadForward_h_

namespace lldb_private {
#if defined(_WIN32)
class HostThreadWindows;
typedef HostThreadWindows HostNativeThread;
#elif defined(__APPLE__)
class HostThreadMacOSX;
typedef HostThreadMacOSX HostNativeThread;
#else
class HostThreadPosix;
typedef HostThreadPosix HostNativeThread;
#endif
}

#endif
