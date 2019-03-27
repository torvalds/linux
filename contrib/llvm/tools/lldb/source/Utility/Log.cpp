//===-- Log.cpp -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Log.h"
#include "lldb/Utility/VASPrintf.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"

#include "llvm/Support/Chrono.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <cstdarg>
#include <mutex>
#include <utility>

#include <assert.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

using namespace lldb_private;

llvm::ManagedStatic<Log::ChannelMap> Log::g_channel_map;

void Log::ListCategories(llvm::raw_ostream &stream, const ChannelMap::value_type &entry) {
  stream << llvm::formatv("Logging categories for '{0}':\n", entry.first());
  stream << "  all - all available logging categories\n";
  stream << "  default - default set of logging categories\n";
  for (const auto &category : entry.second.m_channel.categories)
    stream << llvm::formatv("  {0} - {1}\n", category.name,
                            category.description);
}

uint32_t Log::GetFlags(llvm::raw_ostream &stream, const ChannelMap::value_type &entry,
                         llvm::ArrayRef<const char *> categories) {
  bool list_categories = false;
  uint32_t flags = 0;
  for (const char *category : categories) {
    if (llvm::StringRef("all").equals_lower(category)) {
      flags |= UINT32_MAX;
      continue;
    }
    if (llvm::StringRef("default").equals_lower(category)) {
      flags |= entry.second.m_channel.default_flags;
      continue;
    }
    auto cat = llvm::find_if(
        entry.second.m_channel.categories,
        [&](const Log::Category &c) { return c.name.equals_lower(category); });
    if (cat != entry.second.m_channel.categories.end()) {
      flags |= cat->flag;
      continue;
    }
    stream << llvm::formatv("error: unrecognized log category '{0}'\n",
                            category);
    list_categories = true;
  }
  if (list_categories)
    ListCategories(stream, entry);
  return flags;
}

void Log::Enable(const std::shared_ptr<llvm::raw_ostream> &stream_sp,
                 uint32_t options, uint32_t flags) {
  llvm::sys::ScopedWriter lock(m_mutex);

  uint32_t mask = m_mask.fetch_or(flags, std::memory_order_relaxed);
  if (mask | flags) {
    m_options.store(options, std::memory_order_relaxed);
    m_stream_sp = stream_sp;
    m_channel.log_ptr.store(this, std::memory_order_relaxed);
  }
}

void Log::Disable(uint32_t flags) {
  llvm::sys::ScopedWriter lock(m_mutex);

  uint32_t mask = m_mask.fetch_and(~flags, std::memory_order_relaxed);
  if (!(mask & ~flags)) {
    m_stream_sp.reset();
    m_channel.log_ptr.store(nullptr, std::memory_order_relaxed);
  }
}

const Flags Log::GetOptions() const {
  return m_options.load(std::memory_order_relaxed);
}

const Flags Log::GetMask() const {
  return m_mask.load(std::memory_order_relaxed);
}

void Log::PutCString(const char *cstr) { Printf("%s", cstr); }
void Log::PutString(llvm::StringRef str) { PutCString(str.str().c_str()); }

//----------------------------------------------------------------------
// Simple variable argument logging with flags.
//----------------------------------------------------------------------
void Log::Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  VAPrintf(format, args);
  va_end(args);
}

//----------------------------------------------------------------------
// All logging eventually boils down to this function call. If we have a
// callback registered, then we call the logging callback. If we have a valid
// file handle, we also log to the file.
//----------------------------------------------------------------------
void Log::VAPrintf(const char *format, va_list args) {
  llvm::SmallString<64> FinalMessage;
  llvm::raw_svector_ostream Stream(FinalMessage);
  WriteHeader(Stream, "", "");

  llvm::SmallString<64> Content;
  lldb_private::VASprintf(Content, format, args);

  Stream << Content << "\n";

  WriteMessage(FinalMessage.str());
}

//----------------------------------------------------------------------
// Printing of errors that are not fatal.
//----------------------------------------------------------------------
void Log::Error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  VAError(format, args);
  va_end(args);
}

void Log::VAError(const char *format, va_list args) {
  llvm::SmallString<64> Content;
  VASprintf(Content, format, args);

  Printf("error: %s", Content.c_str());
}

//----------------------------------------------------------------------
// Printing of warnings that are not fatal only if verbose mode is enabled.
//----------------------------------------------------------------------
void Log::Verbose(const char *format, ...) {
  if (!GetVerbose())
    return;

  va_list args;
  va_start(args, format);
  VAPrintf(format, args);
  va_end(args);
}

//----------------------------------------------------------------------
// Printing of warnings that are not fatal.
//----------------------------------------------------------------------
void Log::Warning(const char *format, ...) {
  llvm::SmallString<64> Content;
  va_list args;
  va_start(args, format);
  VASprintf(Content, format, args);
  va_end(args);

  Printf("warning: %s", Content.c_str());
}

void Log::Initialize() {
#ifdef LLVM_ON_UNIX
  pthread_atfork(nullptr, nullptr, &Log::DisableLoggingChild);
#endif
  InitializeLldbChannel();
}

void Log::Register(llvm::StringRef name, Channel &channel) {
  auto iter = g_channel_map->try_emplace(name, channel);
  assert(iter.second == true);
  (void)iter;
}

void Log::Unregister(llvm::StringRef name) {
  auto iter = g_channel_map->find(name);
  assert(iter != g_channel_map->end());
  iter->second.Disable(UINT32_MAX);
  g_channel_map->erase(iter);
}

bool Log::EnableLogChannel(
    const std::shared_ptr<llvm::raw_ostream> &log_stream_sp,
    uint32_t log_options, llvm::StringRef channel,
    llvm::ArrayRef<const char *> categories, llvm::raw_ostream &error_stream) {
  auto iter = g_channel_map->find(channel);
  if (iter == g_channel_map->end()) {
    error_stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  uint32_t flags = categories.empty()
                       ? iter->second.m_channel.default_flags
                       : GetFlags(error_stream, *iter, categories);
  iter->second.Enable(log_stream_sp, log_options, flags);
  return true;
}

bool Log::DisableLogChannel(llvm::StringRef channel,
                            llvm::ArrayRef<const char *> categories,
                            llvm::raw_ostream &error_stream) {
  auto iter = g_channel_map->find(channel);
  if (iter == g_channel_map->end()) {
    error_stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  uint32_t flags = categories.empty()
                       ? UINT32_MAX
                       : GetFlags(error_stream, *iter, categories);
  iter->second.Disable(flags);
  return true;
}

bool Log::ListChannelCategories(llvm::StringRef channel,
                                llvm::raw_ostream &stream) {
  auto ch = g_channel_map->find(channel);
  if (ch == g_channel_map->end()) {
    stream << llvm::formatv("Invalid log channel '{0}'.\n", channel);
    return false;
  }
  ListCategories(stream, *ch);
  return true;
}

void Log::DisableAllLogChannels() {
  for (auto &entry : *g_channel_map)
    entry.second.Disable(UINT32_MAX);
}

void Log::ListAllLogChannels(llvm::raw_ostream &stream) {
  if (g_channel_map->empty()) {
    stream << "No logging channels are currently registered.\n";
    return;
  }

  for (const auto &channel : *g_channel_map)
    ListCategories(stream, channel);
}

bool Log::GetVerbose() const {
  return m_options.load(std::memory_order_relaxed) & LLDB_LOG_OPTION_VERBOSE;
}

void Log::WriteHeader(llvm::raw_ostream &OS, llvm::StringRef file,
                      llvm::StringRef function) {
  Flags options = GetOptions();
  static uint32_t g_sequence_id = 0;
  // Add a sequence ID if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_SEQUENCE))
    OS << ++g_sequence_id << " ";

  // Timestamp if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_TIMESTAMP)) {
    auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch());
    OS << llvm::formatv("{0:f9} ", now.count());
  }

  // Add the process and thread if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD))
    OS << llvm::formatv("[{0,0+4}/{1,0+4}] ", getpid(),
                        llvm::get_threadid());

  // Add the thread name if requested
  if (options.Test(LLDB_LOG_OPTION_PREPEND_THREAD_NAME)) {
    llvm::SmallString<32> thread_name;
    llvm::get_thread_name(thread_name);

    llvm::SmallString<12> format_str;
    llvm::raw_svector_ostream format_os(format_str);
    format_os << "{0,-" << llvm::alignTo<16>(thread_name.size()) << "} ";
    OS << llvm::formatv(format_str.c_str(), thread_name);
  }

  if (options.Test(LLDB_LOG_OPTION_BACKTRACE))
    llvm::sys::PrintStackTrace(OS);

  if (options.Test(LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION) &&
      (!file.empty() || !function.empty())) {
    file = llvm::sys::path::filename(file).take_front(40);
    function = function.take_front(40);
    OS << llvm::formatv("{0,-60:60} ", (file + ":" + function).str());
  }
}

void Log::WriteMessage(const std::string &message) {
  // Make a copy of our stream shared pointer in case someone disables our log
  // while we are logging and releases the stream
  auto stream_sp = GetStream();
  if (!stream_sp)
    return;

  Flags options = GetOptions();
  if (options.Test(LLDB_LOG_OPTION_THREADSAFE)) {
    static std::recursive_mutex g_LogThreadedMutex;
    std::lock_guard<std::recursive_mutex> guard(g_LogThreadedMutex);
    *stream_sp << message;
    stream_sp->flush();
  } else {
    *stream_sp << message;
    stream_sp->flush();
  }
}

void Log::Format(llvm::StringRef file, llvm::StringRef function,
                 const llvm::formatv_object_base &payload) {
  std::string message_string;
  llvm::raw_string_ostream message(message_string);
  WriteHeader(message, file, function);
  message << payload << "\n";
  WriteMessage(message.str());
}

void Log::DisableLoggingChild() {
  // Disable logging by clearing out the atomic variable after forking -- if we
  // forked while another thread held the channel mutex, we would deadlock when
  // trying to write to the log.
  for (auto &c: *g_channel_map)
    c.second.m_channel.log_ptr.store(nullptr, std::memory_order_relaxed);
}
