//===-- Timer.cpp -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/Stream.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

using namespace lldb_private;

#define TIMER_INDENT_AMOUNT 2

namespace {
typedef std::vector<Timer *> TimerStack;
static std::atomic<Timer::Category *> g_categories;
} // end of anonymous namespace

std::atomic<bool> Timer::g_quiet(true);
std::atomic<unsigned> Timer::g_display_depth(0);
static std::mutex &GetFileMutex() {
  static std::mutex *g_file_mutex_ptr = new std::mutex();
  return *g_file_mutex_ptr;
}

static TimerStack &GetTimerStackForCurrentThread() {
  static thread_local TimerStack g_stack;
  return g_stack;
}

Timer::Category::Category(const char *cat) : m_name(cat) {
  m_nanos.store(0, std::memory_order_release);
  Category *expected = g_categories;
  do {
    m_next = expected;
  } while (!g_categories.compare_exchange_weak(expected, this));
}

void Timer::SetQuiet(bool value) { g_quiet = value; }

Timer::Timer(Timer::Category &category, const char *format, ...)
    : m_category(category), m_total_start(std::chrono::steady_clock::now()) {
  TimerStack &stack = GetTimerStackForCurrentThread();

  stack.push_back(this);
  if (g_quiet && stack.size() <= g_display_depth) {
    std::lock_guard<std::mutex> lock(GetFileMutex());

    // Indent
    ::fprintf(stdout, "%*s", int(stack.size() - 1) * TIMER_INDENT_AMOUNT, "");
    // Print formatted string
    va_list args;
    va_start(args, format);
    ::vfprintf(stdout, format, args);
    va_end(args);

    // Newline
    ::fprintf(stdout, "\n");
  }
}

Timer::~Timer() {
  using namespace std::chrono;

  auto stop_time = steady_clock::now();
  auto total_dur = stop_time - m_total_start;
  auto timer_dur = total_dur - m_child_duration;

  TimerStack &stack = GetTimerStackForCurrentThread();
  if (g_quiet && stack.size() <= g_display_depth) {
    std::lock_guard<std::mutex> lock(GetFileMutex());
    ::fprintf(stdout, "%*s%.9f sec (%.9f sec)\n",
              int(stack.size() - 1) * TIMER_INDENT_AMOUNT, "",
              duration<double>(total_dur).count(),
              duration<double>(timer_dur).count());
  }

  assert(stack.back() == this);
  stack.pop_back();
  if (!stack.empty())
    stack.back()->ChildDuration(total_dur);

  // Keep total results for each category so we can dump results.
  m_category.m_nanos += std::chrono::nanoseconds(timer_dur).count();
}

void Timer::SetDisplayDepth(uint32_t depth) { g_display_depth = depth; }

/* binary function predicate:
 * - returns whether a person is less than another person
 */

typedef std::pair<const char *, uint64_t> TimerEntry;

static bool CategoryMapIteratorSortCriterion(const TimerEntry &lhs,
                                             const TimerEntry &rhs) {
  return lhs.second > rhs.second;
}

void Timer::ResetCategoryTimes() {
  for (Category *i = g_categories; i; i = i->m_next)
    i->m_nanos.store(0, std::memory_order_release);
}

void Timer::DumpCategoryTimes(Stream *s) {
  std::vector<TimerEntry> sorted;
  for (Category *i = g_categories; i; i = i->m_next) {
    uint64_t nanos = i->m_nanos.load(std::memory_order_acquire);
    if (nanos)
      sorted.push_back(std::make_pair(i->m_name, nanos));
  }
  if (sorted.empty())
    return; // Later code will break without any elements.

  // Sort by time
  llvm::sort(sorted.begin(), sorted.end(), CategoryMapIteratorSortCriterion);

  for (const auto &timer : sorted)
    s->Printf("%.9f sec for %s\n", timer.second / 1000000000., timer.first);
}
