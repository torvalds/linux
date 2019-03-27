//===- FormatVariadicDetails.h - Helpers for FormatVariadic.h ----*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATVARIADIC_DETAILS_H
#define LLVM_SUPPORT_FORMATVARIADIC_DETAILS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <type_traits>

namespace llvm {
template <typename T, typename Enable = void> struct format_provider {};
class Error;

namespace detail {
class format_adapter {
  virtual void anchor();

protected:
  virtual ~format_adapter() {}

public:
  virtual void format(raw_ostream &S, StringRef Options) = 0;
};

template <typename T> class provider_format_adapter : public format_adapter {
  T Item;

public:
  explicit provider_format_adapter(T &&Item) : Item(std::forward<T>(Item)) {}

  void format(llvm::raw_ostream &S, StringRef Options) override {
    format_provider<typename std::decay<T>::type>::format(Item, S, Options);
  }
};

template <typename T>
class stream_operator_format_adapter : public format_adapter {
  T Item;

public:
  explicit stream_operator_format_adapter(T &&Item)
      : Item(std::forward<T>(Item)) {}

  void format(llvm::raw_ostream &S, StringRef Options) override { S << Item; }
};

template <typename T> class missing_format_adapter;

// Test if format_provider<T> is defined on T and contains a member function
// with the signature:
//   static void format(const T&, raw_stream &, StringRef);
//
template <class T> class has_FormatProvider {
public:
  using Decayed = typename std::decay<T>::type;
  typedef void (*Signature_format)(const Decayed &, llvm::raw_ostream &,
                                   StringRef);

  template <typename U>
  static char test(SameType<Signature_format, &U::format> *);

  template <typename U> static double test(...);

  static bool const value =
      (sizeof(test<llvm::format_provider<Decayed>>(nullptr)) == 1);
};

// Test if raw_ostream& << T -> raw_ostream& is findable via ADL.
template <class T> class has_StreamOperator {
public:
  using ConstRefT = const typename std::decay<T>::type &;

  template <typename U>
  static char test(typename std::enable_if<
                   std::is_same<decltype(std::declval<llvm::raw_ostream &>()
                                         << std::declval<U>()),
                                llvm::raw_ostream &>::value,
                   int *>::type);

  template <typename U> static double test(...);

  static bool const value = (sizeof(test<ConstRefT>(nullptr)) == 1);
};

// Simple template that decides whether a type T should use the member-function
// based format() invocation.
template <typename T>
struct uses_format_member
    : public std::integral_constant<
          bool,
          std::is_base_of<format_adapter,
                          typename std::remove_reference<T>::type>::value> {};

// Simple template that decides whether a type T should use the format_provider
// based format() invocation.  The member function takes priority, so this test
// will only be true if there is not ALSO a format member.
template <typename T>
struct uses_format_provider
    : public std::integral_constant<
          bool, !uses_format_member<T>::value && has_FormatProvider<T>::value> {
};

// Simple template that decides whether a type T should use the operator<<
// based format() invocation.  This takes last priority.
template <typename T>
struct uses_stream_operator
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              !uses_format_provider<T>::value &&
                                              has_StreamOperator<T>::value> {};

// Simple template that decides whether a type T has neither a member-function
// nor format_provider based implementation that it can use.  Mostly used so
// that the compiler spits out a nice diagnostic when a type with no format
// implementation can be located.
template <typename T>
struct uses_missing_provider
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              !uses_format_provider<T>::value &&
                                              !uses_stream_operator<T>::value> {
};

template <typename T>
typename std::enable_if<uses_format_member<T>::value, T>::type
build_format_adapter(T &&Item) {
  return std::forward<T>(Item);
}

template <typename T>
typename std::enable_if<uses_format_provider<T>::value,
                        provider_format_adapter<T>>::type
build_format_adapter(T &&Item) {
  return provider_format_adapter<T>(std::forward<T>(Item));
}

template <typename T>
typename std::enable_if<uses_stream_operator<T>::value,
                        stream_operator_format_adapter<T>>::type
build_format_adapter(T &&Item) {
  // If the caller passed an Error by value, then stream_operator_format_adapter
  // would be responsible for consuming it.
  // Make the caller opt into this by calling fmt_consume().
  static_assert(
      !std::is_same<llvm::Error, typename std::remove_cv<T>::type>::value,
      "llvm::Error-by-value must be wrapped in fmt_consume() for formatv");
  return stream_operator_format_adapter<T>(std::forward<T>(Item));
}

template <typename T>
typename std::enable_if<uses_missing_provider<T>::value,
                        missing_format_adapter<T>>::type
build_format_adapter(T &&Item) {
  return missing_format_adapter<T>();
}
}
}

#endif
