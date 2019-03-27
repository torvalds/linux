//===-- ProcessPOSIXLog.h -----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ProcessPOSIXLog_h_
#define liblldb_ProcessPOSIXLog_h_


#include "lldb/Utility/Log.h"

#define POSIX_LOG_PROCESS (1u << 1)
#define POSIX_LOG_THREAD (1u << 2)
#define POSIX_LOG_MEMORY (1u << 4) // Log memory reads/writes calls
#define POSIX_LOG_PTRACE (1u << 5)
#define POSIX_LOG_REGISTERS (1u << 6)
#define POSIX_LOG_BREAKPOINTS (1u << 7)
#define POSIX_LOG_WATCHPOINTS (1u << 8)
#define POSIX_LOG_ALL (UINT32_MAX)
#define POSIX_LOG_DEFAULT POSIX_LOG_PROCESS

namespace lldb_private {
class ProcessPOSIXLog {
  static Log::Channel g_channel;

public:
  static void Initialize();

  static Log *GetLogIfAllCategoriesSet(uint32_t mask) {
    return g_channel.GetLogIfAll(mask);
  }
};
}

#endif // liblldb_ProcessPOSIXLog_h_
