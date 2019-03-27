//===- llvm/Support/Chrono.h - Utilities for Timing Manipulation-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CHRONO_H
#define LLVM_SUPPORT_CHRONO_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatProviders.h"

#include <chrono>
#include <ctime>

namespace llvm {

class raw_ostream;

namespace sys {

/// A time point on the system clock. This is provided for two reasons:
/// - to insulate us agains subtle differences in behavoir to differences in
///   system clock precision (which is implementation-defined and differs between
///   platforms).
/// - to shorten the type name
/// The default precision is nanoseconds. If need a specific precision specify
/// it explicitly. If unsure, use the default. If you need a time point on a
/// clock other than the system_clock, use std::chrono directly.
template <typename D = std::chrono::nanoseconds>
using TimePoint = std::chrono::time_point<std::chrono::system_clock, D>;

/// Convert a TimePoint to std::time_t
LLVM_ATTRIBUTE_ALWAYS_INLINE inline std::time_t toTimeT(TimePoint<> TP) {
  using namespace std::chrono;
  return system_clock::to_time_t(
      time_point_cast<system_clock::time_point::duration>(TP));
}

/// Convert a std::time_t to a TimePoint
LLVM_ATTRIBUTE_ALWAYS_INLINE inline TimePoint<std::chrono::seconds>
toTimePoint(std::time_t T) {
  using namespace std::chrono;
  return time_point_cast<seconds>(system_clock::from_time_t(T));
}

/// Convert a std::time_t + nanoseconds to a TimePoint
LLVM_ATTRIBUTE_ALWAYS_INLINE inline TimePoint<>
toTimePoint(std::time_t T, uint32_t nsec) {
  using namespace std::chrono;
  return time_point_cast<nanoseconds>(system_clock::from_time_t(T))
    + nanoseconds(nsec);
}

} // namespace sys

raw_ostream &operator<<(raw_ostream &OS, sys::TimePoint<> TP);

/// Format provider for TimePoint<>
///
/// The options string is a strftime format string, with extensions:
///   - %L is millis: 000-999
///   - %f is micros: 000000-999999
///   - %N is nanos: 000000000 - 999999999
///
/// If no options are given, the default format is "%Y-%m-%d %H:%M:%S.%N".
template <>
struct format_provider<sys::TimePoint<>> {
  static void format(const sys::TimePoint<> &TP, llvm::raw_ostream &OS,
                     StringRef Style);
};

/// Implementation of format_provider<T> for duration types.
///
/// The options string of a duration  type has the grammar:
///
///   duration_options  ::= [unit][show_unit [number_options]]
///   unit              ::= `h`|`m`|`s`|`ms|`us`|`ns`
///   show_unit         ::= `+` | `-`
///   number_options    ::= options string for a integral or floating point type
///
///   Examples
///   =================================
///   |  options  | Input | Output    |
///   =================================
///   | ""        | 1s    | 1 s       |
///   | "ms"      | 1s    | 1000 ms   |
///   | "ms-"     | 1s    | 1000      |
///   | "ms-n"    | 1s    | 1,000     |
///   | ""        | 1.0s  | 1.00 s    |
///   =================================
///
///  If the unit of the duration type is not one of the units specified above,
///  it is still possible to format it, provided you explicitly request a
///  display unit or you request that the unit is not displayed.

namespace detail {
template <typename Period> struct unit { static const char value[]; };
template <typename Period> const char unit<Period>::value[] = "";

template <> struct unit<std::ratio<3600>> { static const char value[]; };
template <> struct unit<std::ratio<60>> { static const char value[]; };
template <> struct unit<std::ratio<1>> { static const char value[]; };
template <> struct unit<std::milli> { static const char value[]; };
template <> struct unit<std::micro> { static const char value[]; };
template <> struct unit<std::nano> { static const char value[]; };
} // namespace detail

template <typename Rep, typename Period>
struct format_provider<std::chrono::duration<Rep, Period>> {
private:
  typedef std::chrono::duration<Rep, Period> Dur;
  typedef typename std::conditional<
      std::chrono::treat_as_floating_point<Rep>::value, double, intmax_t>::type
      InternalRep;

  template <typename AsPeriod> static InternalRep getAs(const Dur &D) {
    using namespace std::chrono;
    return duration_cast<duration<InternalRep, AsPeriod>>(D).count();
  }

  static std::pair<InternalRep, StringRef> consumeUnit(StringRef &Style,
                                                        const Dur &D) {
    using namespace std::chrono;
    if (Style.consume_front("ns"))
      return {getAs<std::nano>(D), "ns"};
    if (Style.consume_front("us"))
      return {getAs<std::micro>(D), "us"};
    if (Style.consume_front("ms"))
      return {getAs<std::milli>(D), "ms"};
    if (Style.consume_front("s"))
      return {getAs<std::ratio<1>>(D), "s"};
    if (Style.consume_front("m"))
      return {getAs<std::ratio<60>>(D), "m"};
    if (Style.consume_front("h"))
      return {getAs<std::ratio<3600>>(D), "h"};
    return {D.count(), detail::unit<Period>::value};
  }

  static bool consumeShowUnit(StringRef &Style) {
    if (Style.empty())
      return true;
    if (Style.consume_front("-"))
      return false;
    if (Style.consume_front("+"))
      return true;
    assert(0 && "Unrecognised duration format");
    return true;
  }

public:
  static void format(const Dur &D, llvm::raw_ostream &Stream, StringRef Style) {
    InternalRep count;
    StringRef unit;
    std::tie(count, unit) = consumeUnit(Style, D);
    bool show_unit = consumeShowUnit(Style);

    format_provider<InternalRep>::format(count, Stream, Style);

    if (show_unit) {
      assert(!unit.empty());
      Stream << " " << unit;
    }
  }
};

} // namespace llvm

#endif // LLVM_SUPPORT_CHRONO_H
