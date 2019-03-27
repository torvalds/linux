//===-- Log.h ---------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_LOG_H
#define LLDB_UTILITY_LOG_H

#include "lldb/Utility/Flags.h"
#include "lldb/Utility/Logging.h"
#include "lldb/lldb-defines.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/RWMutex.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace llvm {
class raw_ostream;
}
//----------------------------------------------------------------------
// Logging Options
//----------------------------------------------------------------------
#define LLDB_LOG_OPTION_THREADSAFE (1u << 0)
#define LLDB_LOG_OPTION_VERBOSE (1u << 1)
#define LLDB_LOG_OPTION_PREPEND_SEQUENCE (1u << 3)
#define LLDB_LOG_OPTION_PREPEND_TIMESTAMP (1u << 4)
#define LLDB_LOG_OPTION_PREPEND_PROC_AND_THREAD (1u << 5)
#define LLDB_LOG_OPTION_PREPEND_THREAD_NAME (1U << 6)
#define LLDB_LOG_OPTION_BACKTRACE (1U << 7)
#define LLDB_LOG_OPTION_APPEND (1U << 8)
#define LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION (1U << 9)

//----------------------------------------------------------------------
// Logging Functions
//----------------------------------------------------------------------
namespace lldb_private {

class Log final {
public:
  // Description of a log channel category.
  struct Category {
    llvm::StringLiteral name;
    llvm::StringLiteral description;
    uint32_t flag;
  };

  // This class describes a log channel. It also encapsulates the behavior
  // necessary to enable a log channel in an atomic manner.
  class Channel {
    std::atomic<Log *> log_ptr;
    friend class Log;

  public:
    const llvm::ArrayRef<Category> categories;
    const uint32_t default_flags;

    constexpr Channel(llvm::ArrayRef<Log::Category> categories,
                      uint32_t default_flags)
        : log_ptr(nullptr), categories(categories),
          default_flags(default_flags) {}

    // This function is safe to call at any time If the channel is disabled
    // after (or concurrently with) this function returning a non-null Log
    // pointer, it is still safe to attempt to write to the Log object -- the
    // output will be discarded.
    Log *GetLogIfAll(uint32_t mask) {
      Log *log = log_ptr.load(std::memory_order_relaxed);
      if (log && log->GetMask().AllSet(mask))
        return log;
      return nullptr;
    }

    // This function is safe to call at any time If the channel is disabled
    // after (or concurrently with) this function returning a non-null Log
    // pointer, it is still safe to attempt to write to the Log object -- the
    // output will be discarded.
    Log *GetLogIfAny(uint32_t mask) {
      Log *log = log_ptr.load(std::memory_order_relaxed);
      if (log && log->GetMask().AnySet(mask))
        return log;
      return nullptr;
    }
  };


  static void Initialize();

  //------------------------------------------------------------------
  // Static accessors for logging channels
  //------------------------------------------------------------------
  static void Register(llvm::StringRef name, Channel &channel);
  static void Unregister(llvm::StringRef name);

  static bool
  EnableLogChannel(const std::shared_ptr<llvm::raw_ostream> &log_stream_sp,
                   uint32_t log_options, llvm::StringRef channel,
                   llvm::ArrayRef<const char *> categories,
                   llvm::raw_ostream &error_stream);

  static bool DisableLogChannel(llvm::StringRef channel,
                                llvm::ArrayRef<const char *> categories,
                                llvm::raw_ostream &error_stream);

  static bool ListChannelCategories(llvm::StringRef channel, llvm::raw_ostream &stream);

  static void DisableAllLogChannels();

  static void ListAllLogChannels(llvm::raw_ostream &stream);

  //------------------------------------------------------------------
  // Member functions
  //
  // These functions are safe to call at any time you have a Log* obtained from
  // the Channel class. If logging is disabled between you obtaining the Log
  // object and writing to it, the output will be silently discarded.
  //------------------------------------------------------------------
  Log(Channel &channel) : m_channel(channel) {}
  ~Log() = default;

  void PutCString(const char *cstr);
  void PutString(llvm::StringRef str);

  template <typename... Args>
  void Format(llvm::StringRef file, llvm::StringRef function,
              const char *format, Args &&... args) {
    Format(file, function, llvm::formatv(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void FormatError(llvm::Error error, llvm::StringRef file,
                   llvm::StringRef function, const char *format,
                   Args &&... args) {
    Format(file, function,
           llvm::formatv(format, llvm::toString(std::move(error)),
                         std::forward<Args>(args)...));
  }

  void Printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  void VAPrintf(const char *format, va_list args);

  void Error(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  void VAError(const char *format, va_list args);

  void Verbose(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  void Warning(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  const Flags GetOptions() const;

  const Flags GetMask() const;

  bool GetVerbose() const;

private:
  Channel &m_channel;

  // The mutex makes sure enable/disable operations are thread-safe. The
  // options and mask variables are atomic to enable their reading in
  // Channel::GetLogIfAny without taking the mutex to speed up the fast path.
  // Their modification however, is still protected by this mutex.
  llvm::sys::RWMutex m_mutex;

  std::shared_ptr<llvm::raw_ostream> m_stream_sp;
  std::atomic<uint32_t> m_options{0};
  std::atomic<uint32_t> m_mask{0};

  void WriteHeader(llvm::raw_ostream &OS, llvm::StringRef file,
                   llvm::StringRef function);
  void WriteMessage(const std::string &message);

  void Format(llvm::StringRef file, llvm::StringRef function,
              const llvm::formatv_object_base &payload);

  std::shared_ptr<llvm::raw_ostream> GetStream() {
    llvm::sys::ScopedReader lock(m_mutex);
    return m_stream_sp;
  }

  void Enable(const std::shared_ptr<llvm::raw_ostream> &stream_sp,
              uint32_t options, uint32_t flags);

  void Disable(uint32_t flags);

  typedef llvm::StringMap<Log> ChannelMap;
  static llvm::ManagedStatic<ChannelMap> g_channel_map;

  static void ListCategories(llvm::raw_ostream &stream,
                             const ChannelMap::value_type &entry);
  static uint32_t GetFlags(llvm::raw_ostream &stream, const ChannelMap::value_type &entry,
                           llvm::ArrayRef<const char *> categories);

  static void DisableLoggingChild();

  Log(const Log &) = delete;
  void operator=(const Log &) = delete;
};

} // namespace lldb_private

#define LLDB_LOG(log, ...)                                                     \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    if (log_private)                                                           \
      log_private->Format(__FILE__, __func__, __VA_ARGS__);                    \
  } while (0)

#define LLDB_LOGV(log, ...)                                                    \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    if (log_private && log_private->GetVerbose())                              \
      log_private->Format(__FILE__, __func__, __VA_ARGS__);                    \
  } while (0)

// Write message to log, if error is set. In the log message refer to the error
// with {0}. Error is cleared regardless of whether logging is enabled.
#define LLDB_LOG_ERROR(log, error, ...)                                        \
  do {                                                                         \
    ::lldb_private::Log *log_private = (log);                                  \
    ::llvm::Error error_private = (error);                                     \
    if (log_private && error_private) {                                        \
      log_private->FormatError(::std::move(error_private), __FILE__, __func__, \
                               __VA_ARGS__);                                   \
    } else                                                                     \
      ::llvm::consumeError(::std::move(error_private));                        \
  } while (0)

#endif // LLDB_UTILITY_LOG_H
