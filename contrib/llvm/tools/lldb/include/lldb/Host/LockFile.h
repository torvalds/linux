//===-- LockFile.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Host_LockFile_h_
#define liblldb_Host_LockFile_h_

#if defined(_WIN32)
#include "lldb/Host/windows/LockFileWindows.h"
namespace lldb_private {
typedef LockFileWindows LockFile;
}
#else
#include "lldb/Host/posix/LockFilePosix.h"
namespace lldb_private {
typedef LockFilePosix LockFile;
}
#endif

#endif // liblldb_Host_LockFile_h_
