#ifndef KMP_STATS_TIMING_H
#define KMP_STATS_TIMING_H

/** @file kmp_stats_timing.h
 * Access to real time clock and timers.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp_os.h"
#include <limits>
#include <stdint.h>
#include <string>
#if KMP_HAVE_X86INTRIN_H
#include <x86intrin.h>
#endif

class tsc_tick_count {
private:
  int64_t my_count;

public:
  class tsc_interval_t {
    int64_t value;
    explicit tsc_interval_t(int64_t _value) : value(_value) {}

  public:
    tsc_interval_t() : value(0) {} // Construct 0 time duration
#if KMP_HAVE_TICK_TIME
    double seconds() const; // Return the length of a time interval in seconds
#endif
    double ticks() const { return double(value); }
    int64_t getValue() const { return value; }
    tsc_interval_t &operator=(int64_t nvalue) {
      value = nvalue;
      return *this;
    }

    friend class tsc_tick_count;

    friend tsc_interval_t operator-(const tsc_tick_count &t1,
                                    const tsc_tick_count &t0);
    friend tsc_interval_t operator-(const tsc_tick_count::tsc_interval_t &i1,
                                    const tsc_tick_count::tsc_interval_t &i0);
    friend tsc_interval_t &operator+=(tsc_tick_count::tsc_interval_t &i1,
                                      const tsc_tick_count::tsc_interval_t &i0);
  };

#if KMP_HAVE___BUILTIN_READCYCLECOUNTER
  tsc_tick_count()
      : my_count(static_cast<int64_t>(__builtin_readcyclecounter())) {}
#elif KMP_HAVE___RDTSC
  tsc_tick_count() : my_count(static_cast<int64_t>(__rdtsc())) {}
#else
#error Must have high resolution timer defined
#endif
  tsc_tick_count(int64_t value) : my_count(value) {}
  int64_t getValue() const { return my_count; }
  tsc_tick_count later(tsc_tick_count const other) const {
    return my_count > other.my_count ? (*this) : other;
  }
  tsc_tick_count earlier(tsc_tick_count const other) const {
    return my_count < other.my_count ? (*this) : other;
  }
#if KMP_HAVE_TICK_TIME
  static double tick_time(); // returns seconds per cycle (period) of clock
#endif
  static tsc_tick_count now() {
    return tsc_tick_count();
  } // returns the rdtsc register value
  friend tsc_tick_count::tsc_interval_t operator-(const tsc_tick_count &t1,
                                                  const tsc_tick_count &t0);
};

inline tsc_tick_count::tsc_interval_t operator-(const tsc_tick_count &t1,
                                                const tsc_tick_count &t0) {
  return tsc_tick_count::tsc_interval_t(t1.my_count - t0.my_count);
}

inline tsc_tick_count::tsc_interval_t
operator-(const tsc_tick_count::tsc_interval_t &i1,
          const tsc_tick_count::tsc_interval_t &i0) {
  return tsc_tick_count::tsc_interval_t(i1.value - i0.value);
}

inline tsc_tick_count::tsc_interval_t &
operator+=(tsc_tick_count::tsc_interval_t &i1,
           const tsc_tick_count::tsc_interval_t &i0) {
  i1.value += i0.value;
  return i1;
}

#if KMP_HAVE_TICK_TIME
inline double tsc_tick_count::tsc_interval_t::seconds() const {
  return value * tick_time();
}
#endif

extern std::string formatSI(double interval, int width, char unit);

inline std::string formatSeconds(double interval, int width) {
  return formatSI(interval, width, 'S');
}

inline std::string formatTicks(double interval, int width) {
  return formatSI(interval, width, 'T');
}

#endif // KMP_STATS_TIMING_H
