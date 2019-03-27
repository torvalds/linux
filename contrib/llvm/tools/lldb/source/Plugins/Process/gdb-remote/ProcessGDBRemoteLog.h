//===-- ProcessGDBRemoteLog.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ProcessGDBRemoteLog_h_
#define liblldb_ProcessGDBRemoteLog_h_


#include "lldb/Utility/Log.h"

#define GDBR_LOG_PROCESS (1u << 1)
#define GDBR_LOG_THREAD (1u << 2)
#define GDBR_LOG_PACKETS (1u << 3)
#define GDBR_LOG_MEMORY (1u << 4) // Log memory reads/writes calls
#define GDBR_LOG_MEMORY_DATA_SHORT                                             \
  (1u << 5) // Log short memory reads/writes bytes
#define GDBR_LOG_MEMORY_DATA_LONG (1u << 6) // Log all memory reads/writes bytes
#define GDBR_LOG_BREAKPOINTS (1u << 7)
#define GDBR_LOG_WATCHPOINTS (1u << 8)
#define GDBR_LOG_STEP (1u << 9)
#define GDBR_LOG_COMM (1u << 10)
#define GDBR_LOG_ASYNC (1u << 11)
#define GDBR_LOG_ALL (UINT32_MAX)
#define GDBR_LOG_DEFAULT GDBR_LOG_PACKETS

namespace lldb_private {
namespace process_gdb_remote {

class ProcessGDBRemoteLog {
  static Log::Channel g_channel;

public:
  static void Initialize();

  static Log *GetLogIfAllCategoriesSet(uint32_t mask) { return g_channel.GetLogIfAll(mask); }
  static Log *GetLogIfAnyCategoryIsSet(uint32_t mask) { return g_channel.GetLogIfAny(mask); }
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_ProcessGDBRemoteLog_h_
