//===-- Logging.cpp ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Logging.h"
#include "lldb/Utility/Log.h"

#include "llvm/ADT/ArrayRef.h"

#include <stdarg.h>

using namespace lldb_private;

static constexpr Log::Category g_categories[] = {
  {{"api"}, {"log API calls and return values"}, LIBLLDB_LOG_API},
  {{"break"}, {"log breakpoints"}, LIBLLDB_LOG_BREAKPOINTS},
  {{"commands"}, {"log command argument parsing"}, LIBLLDB_LOG_COMMANDS},
  {{"comm"}, {"log communication activities"}, LIBLLDB_LOG_COMMUNICATION},
  {{"conn"}, {"log connection details"}, LIBLLDB_LOG_CONNECTION},
  {{"demangle"}, {"log mangled names to catch demangler crashes"}, LIBLLDB_LOG_DEMANGLE},
  {{"dyld"}, {"log shared library related activities"}, LIBLLDB_LOG_DYNAMIC_LOADER},
  {{"event"}, {"log broadcaster, listener and event queue activities"}, LIBLLDB_LOG_EVENTS},
  {{"expr"}, {"log expressions"}, LIBLLDB_LOG_EXPRESSIONS},
  {{"formatters"}, {"log data formatters related activities"}, LIBLLDB_LOG_DATAFORMATTERS},
  {{"host"}, {"log host activities"}, LIBLLDB_LOG_HOST},
  {{"jit"}, {"log JIT events in the target"}, LIBLLDB_LOG_JIT_LOADER},
  {{"language"}, {"log language runtime events"}, LIBLLDB_LOG_LANGUAGE},
  {{"mmap"}, {"log mmap related activities"}, LIBLLDB_LOG_MMAP},
  {{"module"}, {"log module activities such as when modules are created, destroyed, replaced, and more"}, LIBLLDB_LOG_MODULES},
  {{"object"}, {"log object construction/destruction for important objects"}, LIBLLDB_LOG_OBJECT},
  {{"os"}, {"log OperatingSystem plugin related activities"}, LIBLLDB_LOG_OS},
  {{"platform"}, {"log platform events and activities"}, LIBLLDB_LOG_PLATFORM},
  {{"process"}, {"log process events and activities"}, LIBLLDB_LOG_PROCESS},
  {{"script"}, {"log events about the script interpreter"}, LIBLLDB_LOG_SCRIPT},
  {{"state"}, {"log private and public process state changes"}, LIBLLDB_LOG_STATE},
  {{"step"}, {"log step related activities"}, LIBLLDB_LOG_STEP},
  {{"symbol"}, {"log symbol related issues and warnings"}, LIBLLDB_LOG_SYMBOLS},
  {{"system-runtime"}, {"log system runtime events"}, LIBLLDB_LOG_SYSTEM_RUNTIME},
  {{"target"}, {"log target events and activities"}, LIBLLDB_LOG_TARGET},
  {{"temp"}, {"log internal temporary debug messages"}, LIBLLDB_LOG_TEMPORARY},
  {{"thread"}, {"log thread events and activities"}, LIBLLDB_LOG_THREAD},
  {{"types"}, {"log type system related activities"}, LIBLLDB_LOG_TYPES},
  {{"unwind"}, {"log stack unwind activities"}, LIBLLDB_LOG_UNWIND},
  {{"watch"}, {"log watchpoint related activities"}, LIBLLDB_LOG_WATCHPOINTS},
};

static Log::Channel g_log_channel(g_categories, LIBLLDB_LOG_DEFAULT);

void lldb_private::InitializeLldbChannel() {
  Log::Register("lldb", g_log_channel);
}

Log *lldb_private::GetLogIfAllCategoriesSet(uint32_t mask) {
  return g_log_channel.GetLogIfAll(mask);
}

Log *lldb_private::GetLogIfAnyCategoriesSet(uint32_t mask) {
  return g_log_channel.GetLogIfAny(mask);
}


void lldb_private::LogIfAnyCategoriesSet(uint32_t mask, const char *format, ...) {
  if (Log *log = GetLogIfAnyCategoriesSet(mask)) {
    va_list args;
    va_start(args, format);
    log->VAPrintf(format, args);
    va_end(args);
  }
}
