//===-- HostNativeProcess.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostNativeProcess_h_
#define lldb_Host_HostNativeProcess_h_

#if defined(_WIN32)
#include "lldb/Host/windows/HostProcessWindows.h"
namespace lldb_private {
typedef HostProcessWindows HostNativeProcess;
}
#else
#include "lldb/Host/posix/HostProcessPosix.h"
namespace lldb_private {
typedef HostProcessPosix HostNativeProcess;
}
#endif

#endif
