//===- FormatProviders.h - Formatters for common LLVM types -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements format providers for many common LLVM types, for example
// allowing precision and width specifiers for scalar and string types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATPROVIDERS_H
#define LLVM_SUPPORT_FORMATPROVIDERS_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FormatVariadicDetails.h"
#include "llvm/Support/NativeFormatting.h"

#include <type_traits>
#include <vector>

namespace llvm {
namespace detail {
template <typename T>
struct use_integral_formatter
    : public std::integral_constant<
          bool, is_one_of<T, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                          int64_t, uint64_t, int, unsigned, long, unsigned long,
                          long long, unsigned long long>::value> {};

template <typename T>
struct use_char_formatter
    : public std::integral_constant<bool, std::is_same<T, char>::value> {};

template <typename T>
struct is_cstring
    : public std::integral_constant<bool,
                                    is_one_of<T, char *, const char *>::value> {
};

template <typename T>
struct use_string_formatter
    : public std::integral_constant<bool,
                                    std::is_convertible<T, llvm::StringRef>::value> {};

template <typename T>
struct use_pointer_formatter
    : public std::integral_constant<bool, std::is_pointer<T>::value &&
                                              !is_cstring<T>::value> {};

template <typename T>
struct use_double_formatter
    : public std::integral_constant<bool, std::is_floating_point<T>::value> {};

class HelperFunctions {
protected:
  static Optional<size_t> parseNumericPrecision(StringRef Str) {
    size_t Prec;
    Optional<size_t> Result;
    if (Str.empty())
      Result = None;
    else if (Str.getAsInteger(10, Prec)) {
      assert(false && "Invalid precision specifier");
      Result = None;
    } else {
      assert(Prec < 100 && "Precision out of range");
      Result = std::min<size_t>(99u, Prec);
    }
    return Result;
  }

  static bool consumeHexStyle(StringRef &Str, HexPrintStyle &Style) {
    if (!Str.startswith_lower("x"))
      return false;

    if (Str.consume_front("x-"))
      Style = HexPrintStyle::Lower;
    else if (Str.consume_front("X-"))
      Style = HexPrintStyle::Upper;
    else if (Str.consume_front("x+") || Str.consume_front("x"))
      Style = HexPrintStyle::PrefixLower;
    else if (Str.consume_front("X+") || Str.consume_front("X"))
      Style = HexPrintStyle::PrefixUpper;
    return true;
  }

  static size_t consumeNumHexDigits(StringRef &Str, HexPrintStyle Style,
                                    size_t Default) {
    Str.consumeInteger(10, Default);
    if (isPrefixedHexStyle(Style))
      Default += 2;
    return Default;
  }
};
}

/// Implementation of format_provider<T> for integral arithmetic types.
///
/// The options string of an integral type has the grammar:
///
///   integer_options   :: [style][digits]
///   style             :: <see table below>
///   digits            :: <non-negative integer> 0-99
///
///   ==========================================================================
///   |  style  |     Meaning          |      Example     | Digits Meaning     |
///   --------------------------------------------------------------------------
///   |         |                      |  Input |  Output |                    |
///   ==========================================================================
///   |   x-    | Hex no prefix, lower |   42   |    2a   | Minimum # digits   |
///   |   X-    | Hex no prefix, upper |   42   |    2A   | Minimum # digits   |
///   | x+ / x  | Hex + prefix, lower  |   42   |   0x2a  | Minimum # digits   |
///   | X+ / X  | Hex + prefix, upper  |   42   |   0x2A  | Minimum # digits   |
///   | N / n   | Digit grouped number | 123456 | 123,456 | Ignored            |
///   | D / d   | Integer              | 100000 | 100000  | Ignored            |
///   | (empty) | Same as D / d        |        |         |                    |
///   ==========================================================================
///

template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_integral_formatter<T>::value>::type>
    : public detail::HelperFunctions {
private:
public:
  static void format(const T &V, llvm::raw_ostream &Stream, StringRef Style) {
    HexPrintStyle HS;
    size_t Digits = 0;
    if (consumeHexStyle(Style, HS)) {
      Digits = consumeNumHexDigits(Style, HS, 0);
      write_hex(Stream, V, HS, Digits);
      return;
    }

    IntegerStyle IS = IntegerStyle::Integer;
    if (Style.consume_front("N") || Style.consume_front("n"))
      IS = IntegerStyle::Number;
    else if (Style.consume_front("D") || Style.consume_front("d"))
      IS = IntegerStyle::Integer;

    Style.consumeInteger(10, Digits);
    assert(Style.empty() && "Invalid integral format style!");
    write_integer(Stream, V, Digits, IS);
  }
};

/// Implementation of format_provider<T> for integral pointer types.
///
/// The options string of a pointer type has the grammar:
///
///   pointer_options   :: [style][precision]
///   style             :: <see table below>
///   digits            :: <non-negative integer> 0-sizeof(void*)
///
///   ==========================================================================
///   |   S     |     Meaning          |                Example                |
///   --------------------------------------------------------------------------
///   |         |                      |       Input       |      Output       |
///   ==========================================================================
///   |   x-    | Hex no prefix, lower |    0xDEADBEEF     |     deadbeef      |
///   |   X-    | Hex no prefix, upper |    0xDEADBEEF     |     DEADBEEF      |
///   | x+ / x  | Hex + prefix, lower  |    0xDEADBEEF     |    0xdeadbeef     |
///   | X+ / X  | Hex + prefix, upper  |    0xDEADBEEF     |    0xDEADBEEF     |
///   | (empty) | Same as X+ / X       |                   |                   |
///   ==========================================================================
///
/// The default precision is the number of nibbles in a machine word, and in all
/// cases indicates the minimum number of nibbles to print.
template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_pointer_formatter<T>::value>::type>
    : public detail::HelperFunctions {
private:
public:
  static void format(const T &V, llvm::raw_ostream &Stream, StringRef Style) {
    HexPrintStyle HS = HexPrintStyle::PrefixUpper;
    consumeHexStyle(Style, HS);
    size_t Digits = consumeNumHexDigits(Style, HS, sizeof(void *) * 2);
    write_hex(Stream, reinterpret_cast<std::uintptr_t>(V), HS, Digits);
  }
};

/// Implementation of format_provider<T> for c-style strings and string
/// objects such as std::string and llvm::StringRef.
///
/// The options string of a string type has the grammar:
///
///   string_options :: [length]
///
/// where `length` is an optional integer specifying the maximum number of
/// characters in the string to print.  If `length` is omitted, the string is
/// printed up to the null terminator.

template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_string_formatter<T>::value>::type> {
  static void format(const T &V, llvm::raw_ostream &Stream, StringRef Style) {
    size_t N = StringRef::npos;
    if (!Style.empty() && Style.getAsInteger(10, N)) {
      assert(false && "Style is not a valid integer");
    }
    llvm::StringRef S = V;
    Stream << S.substr(0, N);
  }
};

/// Implementation of format_provider<T> for llvm::Twine.
///
/// This follows the same rules as the string formatter.

template <> struct format_provider<Twine> {
  static void format(const Twine &V, llvm::raw_ostream &Stream,
                     StringRef Style) {
    format_provider<std::string>::format(V.str(), Stream, Style);
  }
};

/// Implementation of format_provider<T> for characters.
///
/// The options string of a character type has the grammar:
///
///   char_options :: (empty) | [integer_options]
///
/// If `char_options` is empty, the character is displayed as an ASCII
/// character.  Otherwise, it is treated as an integer options string.
///
template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_char_formatter<T>::value>::type> {
  static void format(const char &V, llvm::raw_ostream &Stream,
                     StringRef Style) {
    if (Style.empty())
      Stream << V;
    else {
      int X = static_cast<int>(V);
      format_provider<int>::format(X, Stream, Style);
    }
  }
};

/// Implementation of format_provider<T> for type `bool`
///
/// The options string of a boolean type has the grammar:
///
///   bool_options :: "" | "Y" | "y" | "D" | "d" | "T" | "t"
///
///   ==================================
///   |    C    |     Meaning          |
///   ==================================
///   |    Y    |       YES / NO       |
///   |    y    |       yes / no       |
///   |  D / d  |    Integer 0 or 1    |
///   |    T    |     TRUE / FALSE     |
///   |    t    |     true / false     |
///   | (empty) |   Equivalent to 't'  |
///   ==================================
template <> struct format_provider<bool> {
  static void format(const bool &B, llvm::raw_ostream &Stream,
                     StringRef Style) {
    Stream << StringSwitch<const char *>(Style)
                  .Case("Y", B ? "YES" : "NO")
                  .Case("y", B ? "yes" : "no")
                  .CaseLower("D", B ? "1" : "0")
                  .Case("T", B ? "TRUE" : "FALSE")
                  .Cases("t", "", B ? "true" : "false")
                  .Default(B ? "1" : "0");
  }
};

/// Implementation of format_provider<T> for floating point types.
///
/// The options string of a floating point type has the format:
///
///   float_options   :: [style][precision]
///   style           :: <see table below>
///   precision       :: <non-negative integer> 0-99
///
///   =====================================================
///   |  style  |     Meaning          |      Example     |
///   -----------------------------------------------------
///   |         |                      |  Input |  Output |
///   =====================================================
///   | P / p   | Percentage           |  0.05  |  5.00%  |
///   | F / f   | Fixed point          |   1.0  |  1.00   |
///   |   E     | Exponential with E   | 100000 | 1.0E+05 |
///   |   e     | Exponential with e   | 100000 | 1.0e+05 |
///   | (empty) | Same as F / f        |        |         |
///   =====================================================
///
/// The default precision is 6 for exponential (E / e) and 2 for everything
/// else.

template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_double_formatter<T>::value>::type>
    : public detail::HelperFunctions {
  static void format(const T &V, llvm::raw_ostream &Stream, StringRef Style) {
    FloatStyle S;
    if (Style.consume_front("P") || Style.consume_front("p"))
      S = FloatStyle::Percent;
    else if (Style.consume_front("F") || Style.consume_front("f"))
      S = FloatStyle::Fixed;
    else if (Style.consume_front("E"))
      S = FloatStyle::ExponentUpper;
    else if (Style.consume_front("e"))
      S = FloatStyle::Exponent;
    else
      S = FloatStyle::Fixed;

    Optional<size_t> Precision = parseNumericPrecision(Style);
    if (!Precision.hasValue())
      Precision = getDefaultPrecision(S);

    write_double(Stream, static_cast<double>(V), S, Precision);
  }
};

namespace detail {
template <typename IterT>
using IterValue = typename std::iterator_traits<IterT>::value_type;

template <typename IterT>
struct range_item_has_provider
    : public std::integral_constant<
          bool, !uses_missing_provider<IterValue<IterT>>::value> {};
}

/// Implementation of format_provider<T> for ranges.
///
/// This will print an arbitrary range as a delimited sequence of items.
///
/// The options string of a range type has the grammar:
///
///   range_style       ::= [separator] [element_style]
///   separator         ::= "$" delimeted_expr
///   element_style     ::= "@" delimeted_expr
///   delimeted_expr    ::= "[" expr "]" | "(" expr ")" | "<" expr ">"
///   expr              ::= <any string not containing delimeter>
///
/// where the separator expression is the string to insert between consecutive
/// items in the range and the argument expression is the Style specification to
/// be used when formatting the underlying type.  The default separator if
/// unspecified is ' ' (space).  The syntax of the argument expression follows
/// whatever grammar is dictated by the format provider or format adapter used
/// to format the value type.
///
/// Note that attempting to format an `iterator_range<T>` where no format
/// provider can be found for T will result in a compile error.
///

template <typename IterT> class format_provider<llvm::iterator_range<IterT>> {
  using value = typename std::iterator_traits<IterT>::value_type;
  using reference = typename std::iterator_traits<IterT>::reference;

  static StringRef consumeOneOption(StringRef &Style, char Indicator,
                                    StringRef Default) {
    if (Style.empty())
      return Default;
    if (Style.front() != Indicator)
      return Default;
    Style = Style.drop_front();
    if (Style.empty()) {
      assert(false && "Invalid range style");
      return Default;
    }

    for (const char *D : {"[]", "<>", "()"}) {
      if (Style.front() != D[0])
        continue;
      size_t End = Style.find_first_of(D[1]);
      if (End == StringRef::npos) {
        assert(false && "Missing range option end delimeter!");
        return Default;
      }
      StringRef Result = Style.slice(1, End);
      Style = Style.drop_front(End + 1);
      return Result;
    }
    assert(false && "Invalid range style!");
    return Default;
  }

  static std::pair<StringRef, StringRef> parseOptions(StringRef Style) {
    StringRef Sep = consumeOneOption(Style, '$', ", ");
    StringRef Args = consumeOneOption(Style, '@', "");
    assert(Style.empty() && "Unexpected text in range option string!");
    return std::make_pair(Sep, Args);
  }

public:
  static_assert(detail::range_item_has_provider<IterT>::value,
                "Range value_type does not have a format provider!");
  static void format(const llvm::iterator_range<IterT> &V,
                     llvm::raw_ostream &Stream, StringRef Style) {
    StringRef Sep;
    StringRef ArgStyle;
    std::tie(Sep, ArgStyle) = parseOptions(Style);
    auto Begin = V.begin();
    auto End = V.end();
    if (Begin != End) {
      auto Adapter =
          detail::build_format_adapter(std::forward<reference>(*Begin));
      Adapter.format(Stream, ArgStyle);
      ++Begin;
    }
    while (Begin != End) {
      Stream << Sep;
      auto Adapter =
          detail::build_format_adapter(std::forward<reference>(*Begin));
      Adapter.format(Stream, ArgStyle);
      ++Begin;
    }
  }
};
}

#endif
