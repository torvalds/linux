//===-- Timer.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Timer_h_
#define liblldb_Timer_h_

#include "lldb/lldb-defines.h"
#include "llvm/Support/Chrono.h"
#include <atomic>
#include <stdint.h>

namespace lldb_private {
class Stream;

//----------------------------------------------------------------------
/// @class Timer Timer.h "lldb/Utility/Timer.h"
/// A timer class that simplifies common timing metrics.
//----------------------------------------------------------------------

class Timer {
public:
  class Category {
  public:
    explicit Category(const char *category_name);

  private:
    friend class Timer;
    const char *m_name;
    std::atomic<uint64_t> m_nanos;
    std::atomic<Category *> m_next;

    DISALLOW_COPY_AND_ASSIGN(Category);
  };

  //--------------------------------------------------------------
  /// Default constructor.
  //--------------------------------------------------------------
  Timer(Category &category, const char *format, ...)
      __attribute__((format(printf, 3, 4)));

  //--------------------------------------------------------------
  /// Destructor
  //--------------------------------------------------------------
  ~Timer();

  void Dump();

  static void SetDisplayDepth(uint32_t depth);

  static void SetQuiet(bool value);

  static void DumpCategoryTimes(Stream *s);

  static void ResetCategoryTimes();

protected:
  using TimePoint = std::chrono::steady_clock::time_point;
  void ChildDuration(TimePoint::duration dur) { m_child_duration += dur; }

  Category &m_category;
  TimePoint m_total_start;
  TimePoint::duration m_child_duration{0};

  static std::atomic<bool> g_quiet;
  static std::atomic<unsigned> g_display_depth;

private:
  DISALLOW_COPY_AND_ASSIGN(Timer);
};

} // namespace lldb_private

#endif // liblldb_Timer_h_
