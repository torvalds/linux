//===-- ProcessGDBRemoteLog.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProcessGDBRemoteLog.h"
#include "ProcessGDBRemote.h"
#include "llvm/Support/Threading.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

static constexpr Log::Category g_categories[] = {
    {{"async"}, {"log asynchronous activity"}, GDBR_LOG_ASYNC},
    {{"break"}, {"log breakpoints"}, GDBR_LOG_BREAKPOINTS},
    {{"comm"}, {"log communication activity"}, GDBR_LOG_COMM},
    {{"packets"}, {"log gdb remote packets"}, GDBR_LOG_PACKETS},
    {{"memory"}, {"log memory reads and writes"}, GDBR_LOG_MEMORY},
    {{"data-short"},
     {"log memory bytes for memory reads and writes for short transactions "
      "only"},
     GDBR_LOG_MEMORY_DATA_SHORT},
    {{"data-long"},
     {"log memory bytes for memory reads and writes for all transactions"},
     GDBR_LOG_MEMORY_DATA_LONG},
    {{"process"}, {"log process events and activities"}, GDBR_LOG_PROCESS},
    {{"step"}, {"log step related activities"}, GDBR_LOG_STEP},
    {{"thread"}, {"log thread events and activities"}, GDBR_LOG_THREAD},
    {{"watch"}, {"log watchpoint related activities"}, GDBR_LOG_WATCHPOINTS},
};

Log::Channel ProcessGDBRemoteLog::g_channel(g_categories, GDBR_LOG_DEFAULT);

void ProcessGDBRemoteLog::Initialize() {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    Log::Register("gdb-remote", g_channel);
  });
}
