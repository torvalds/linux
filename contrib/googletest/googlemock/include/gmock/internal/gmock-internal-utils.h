// Copyright 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


// Google Mock - a framework for writing C++ mock classes.
//
// This file defines some utilities useful for implementing Google
// Mock.  They are subject to change without notice, so please DO NOT
// USE THEM IN USER CODE.

// GOOGLETEST_CM0002 DO NOT DELETE

#ifndef GMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_INTERNAL_UTILS_H_
#define GMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_INTERNAL_UTILS_H_

#include <stdio.h>
#include <ostream>  // NOLINT
#include <string>
#include "gmock/internal/gmock-generated-internal-utils.h"
#include "gmock/internal/gmock-port.h"
#include "gtest/gtest.h"

namespace testing {
namespace internal {

// Silence MSVC C4100 (unreferenced formal parameter) and
// C4805('==': unsafe mix of type 'const int' and type 'const bool')
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable:4100)
# pragma warning(disable:4805)
#endif

// Joins a vector of strings as if they are fields of a tuple; returns
// the joined string.
GTEST_API_ std::string JoinAsTuple(const Strings& fields);

// Converts an identifier name to a space-separated list of lower-case
// words.  Each maximum substring of the form [A-Za-z][a-z]*|\d+ is
// treated as one word.  For example, both "FooBar123" and
// "foo_bar_123" are converted to "foo bar 123".
GTEST_API_ std::string ConvertIdentifierNameToWords(const char* id_name);

// PointeeOf<Pointer>::type is the type of a value pointed to by a
// Pointer, which can be either a smart pointer or a raw pointer.  The
// following default implementation is for the case where Pointer is a
// smart pointer.
template <typename Pointer>
struct PointeeOf {
  // Smart pointer classes define type element_type as the type of
  // their pointees.
  typedef typename Pointer::element_type type;
};
// This specialization is for the raw pointer case.
template <typename T>
struct PointeeOf<T*> { typedef T type; };  // NOLINT

// GetRawPointer(p) returns the raw pointer underlying p when p is a
// smart pointer, or returns p itself when p is already a raw pointer.
// The following default implementation is for the smart pointer case.
template <typename Pointer>
inline const typename Pointer::element_type* GetRawPointer(const Pointer& p) {
  return p.get();
}
// This overloaded version is for the raw pointer case.
template <typename Element>
inline Element* GetRawPointer(Element* p) { return p; }

// This comparator allows linked_ptr to be stored in sets.
template <typename T>
struct LinkedPtrLessThan {
  bool operator()(const ::testing::internal::linked_ptr<T>& lhs,
                  const ::testing::internal::linked_ptr<T>& rhs) const {
    return lhs.get() < rhs.get();
  }
};

// Symbian compilation can be done with wchar_t being either a native
// type or a typedef.  Using Google Mock with OpenC without wchar_t
// should require the definition of _STLP_NO_WCHAR_T.
//
// MSVC treats wchar_t as a native type usually, but treats it as the
// same as unsigned short when the compiler option /Zc:wchar_t- is
// specified.  It defines _NATIVE_WCHAR_T_DEFINED symbol when wchar_t
// is a native type.
#if (GTEST_OS_SYMBIAN && defined(_STLP_NO_WCHAR_T)) || \
    (defined(_MSC_VER) && !defined(_NATIVE_WCHAR_T_DEFINED))
// wchar_t is a typedef.
#else
# define GMOCK_WCHAR_T_IS_NATIVE_ 1
#endif

// signed wchar_t and unsigned wchar_t are NOT in the C++ standard.
// Using them is a bad practice and not portable.  So DON'T use them.
//
// Still, Google Mock is designed to work even if the user uses signed
// wchar_t or unsigned wchar_t (obviously, assuming the compiler
// supports them).
//
// To gcc,
//   wchar_t == signed wchar_t != unsigned wchar_t == unsigned int
#ifdef __GNUC__
#if !defined(__WCHAR_UNSIGNED__)
// signed/unsigned wchar_t are valid types.
# define GMOCK_HAS_SIGNED_WCHAR_T_ 1
#endif
#endif

// In what follows, we use the term "kind" to indicate whether a type
// is bool, an integer type (excluding bool), a floating-point type,
// or none of them.  This categorization is useful for determining
// when a matcher argument type can be safely converted to another
// type in the implementation of SafeMatcherCast.
enum TypeKind {
  kBool, kInteger, kFloatingPoint, kOther
};

// KindOf<T>::value is the kind of type T.
template <typename T> struct KindOf {
  enum { value = kOther };  // The default kind.
};

// This macro declares that the kind of 'type' is 'kind'.
#define GMOCK_DECLARE_KIND_(type, kind) \
  template <> struct KindOf<type> { enum { value = kind }; }

GMOCK_DECLARE_KIND_(bool, kBool);

// All standard integer types.
GMOCK_DECLARE_KIND_(char, kInteger);
GMOCK_DECLARE_KIND_(signed char, kInteger);
GMOCK_DECLARE_KIND_(unsigned char, kInteger);
GMOCK_DECLARE_KIND_(short, kInteger);  // NOLINT
GMOCK_DECLARE_KIND_(unsigned short, kInteger);  // NOLINT
GMOCK_DECLARE_KIND_(int, kInteger);
GMOCK_DECLARE_KIND_(unsigned int, kInteger);
GMOCK_DECLARE_KIND_(long, kInteger);  // NOLINT
GMOCK_DECLARE_KIND_(unsigned long, kInteger);  // NOLINT

#if GMOCK_WCHAR_T_IS_NATIVE_
GMOCK_DECLARE_KIND_(wchar_t, kInteger);
#endif

// Non-standard integer types.
GMOCK_DECLARE_KIND_(Int64, kInteger);
GMOCK_DECLARE_KIND_(UInt64, kInteger);

// All standard floating-point types.
GMOCK_DECLARE_KIND_(float, kFloatingPoint);
GMOCK_DECLARE_KIND_(double, kFloatingPoint);
GMOCK_DECLARE_KIND_(long double, kFloatingPoint);

#undef GMOCK_DECLARE_KIND_

// Evaluates to the kind of 'type'.
#define GMOCK_KIND_OF_(type) \
  static_cast< ::testing::internal::TypeKind>( \
      ::testing::internal::KindOf<type>::value)

// Evaluates to true iff integer type T is signed.
#define GMOCK_IS_SIGNED_(T) (static_cast<T>(-1) < 0)

// LosslessArithmeticConvertibleImpl<kFromKind, From, kToKind, To>::value
// is true iff arithmetic type From can be losslessly converted to
// arithmetic type To.
//
// It's the user's responsibility to ensure that both From and To are
// raw (i.e. has no CV modifier, is not a pointer, and is not a
// reference) built-in arithmetic types, kFromKind is the kind of
// From, and kToKind is the kind of To; the value is
// implementation-defined when the above pre-condition is violated.
template <TypeKind kFromKind, typename From, TypeKind kToKind, typename To>
struct LosslessArithmeticConvertibleImpl : public false_type {};

// Converting bool to bool is lossless.
template <>
struct LosslessArithmeticConvertibleImpl<kBool, bool, kBool, bool>
    : public true_type {};  // NOLINT

// Converting bool to any integer type is lossless.
template <typename To>
struct LosslessArithmeticConvertibleImpl<kBool, bool, kInteger, To>
    : public true_type {};  // NOLINT

// Converting bool to any floating-point type is lossless.
template <typename To>
struct LosslessArithmeticConvertibleImpl<kBool, bool, kFloatingPoint, To>
    : public true_type {};  // NOLINT

// Converting an integer to bool is lossy.
template <typename From>
struct LosslessArithmeticConvertibleImpl<kInteger, From, kBool, bool>
    : public false_type {};  // NOLINT

// Converting an integer to another non-bool integer is lossless iff
// the target type's range encloses the source type's range.
template <typename From, typename To>
struct LosslessArithmeticConvertibleImpl<kInteger, From, kInteger, To>
    : public bool_constant<
      // When converting from a smaller size to a larger size, we are
      // fine as long as we are not converting from signed to unsigned.
      ((sizeof(From) < sizeof(To)) &&
       (!GMOCK_IS_SIGNED_(From) || GMOCK_IS_SIGNED_(To))) ||
      // When converting between the same size, the signedness must match.
      ((sizeof(From) == sizeof(To)) &&
       (GMOCK_IS_SIGNED_(From) == GMOCK_IS_SIGNED_(To)))> {};  // NOLINT

#undef GMOCK_IS_SIGNED_

// Converting an integer to a floating-point type may be lossy, since
// the format of a floating-point number is implementation-defined.
template <typename From, typename To>
struct LosslessArithmeticConvertibleImpl<kInteger, From, kFloatingPoint, To>
    : public false_type {};  // NOLINT

// Converting a floating-point to bool is lossy.
template <typename From>
struct LosslessArithmeticConvertibleImpl<kFloatingPoint, From, kBool, bool>
    : public false_type {};  // NOLINT

// Converting a floating-point to an integer is lossy.
template <typename From, typename To>
struct LosslessArithmeticConvertibleImpl<kFloatingPoint, From, kInteger, To>
    : public false_type {};  // NOLINT

// Converting a floating-point to another floating-point is lossless
// iff the target type is at least as big as the source type.
template <typename From, typename To>
struct LosslessArithmeticConvertibleImpl<
  kFloatingPoint, From, kFloatingPoint, To>
    : public bool_constant<sizeof(From) <= sizeof(To)> {};  // NOLINT

// LosslessArithmeticConvertible<From, To>::value is true iff arithmetic
// type From can be losslessly converted to arithmetic type To.
//
// It's the user's responsibility to ensure that both From and To are
// raw (i.e. has no CV modifier, is not a pointer, and is not a
// reference) built-in arithmetic types; the value is
// implementation-defined when the above pre-condition is violated.
template <typename From, typename To>
struct LosslessArithmeticConvertible
    : public LosslessArithmeticConvertibleImpl<
  GMOCK_KIND_OF_(From), From, GMOCK_KIND_OF_(To), To> {};  // NOLINT

// This interface knows how to report a Google Mock failure (either
// non-fatal or fatal).
class FailureReporterInterface {
 public:
  // The type of a failure (either non-fatal or fatal).
  enum FailureType {
    kNonfatal, kFatal
  };

  virtual ~FailureReporterInterface() {}

  // Reports a failure that occurred at the given source file location.
  virtual void ReportFailure(FailureType type, const char* file, int line,
                             const std::string& message) = 0;
};

// Returns the failure reporter used by Google Mock.
GTEST_API_ FailureReporterInterface* GetFailureReporter();

// Asserts that condition is true; aborts the process with the given
// message if condition is false.  We cannot use LOG(FATAL) or CHECK()
// as Google Mock might be used to mock the log sink itself.  We
// inline this function to prevent it from showing up in the stack
// trace.
inline void Assert(bool condition, const char* file, int line,
                   const std::string& msg) {
  if (!condition) {
    GetFailureReporter()->ReportFailure(FailureReporterInterface::kFatal,
                                        file, line, msg);
  }
}
inline void Assert(bool condition, const char* file, int line) {
  Assert(condition, file, line, "Assertion failed.");
}

// Verifies that condition is true; generates a non-fatal failure if
// condition is false.
inline void Expect(bool condition, const char* file, int line,
                   const std::string& msg) {
  if (!condition) {
    GetFailureReporter()->ReportFailure(FailureReporterInterface::kNonfatal,
                                        file, line, msg);
  }
}
inline void Expect(bool condition, const char* file, int line) {
  Expect(condition, file, line, "Expectation failed.");
}

// Severity level of a log.
enum LogSeverity {
  kInfo = 0,
  kWarning = 1
};

// Valid values for the --gmock_verbose flag.

// All logs (informational and warnings) are printed.
const char kInfoVerbosity[] = "info";
// Only warnings are printed.
const char kWarningVerbosity[] = "warning";
// No logs are printed.
const char kErrorVerbosity[] = "error";

// Returns true iff a log with the given severity is visible according
// to the --gmock_verbose flag.
GTEST_API_ bool LogIsVisible(LogSeverity severity);

// Prints the given message to stdout iff 'severity' >= the level
// specified by the --gmock_verbose flag.  If stack_frames_to_skip >=
// 0, also prints the stack trace excluding the top
// stack_frames_to_skip frames.  In opt mode, any positive
// stack_frames_to_skip is treated as 0, since we don't know which
// function calls will be inlined by the compiler and need to be
// conservative.
GTEST_API_ void Log(LogSeverity severity, const std::string& message,
                    int stack_frames_to_skip);

// A marker class that is used to resolve parameterless expectations to the
// correct overload. This must not be instantiable, to prevent client code from
// accidentally resolving to the overload; for example:
//
//    ON_CALL(mock, Method({}, nullptr))...
//
class WithoutMatchers {
 private:
  WithoutMatchers() {}
  friend GTEST_API_ WithoutMatchers GetWithoutMatchers();
};

// Internal use only: access the singleton instance of WithoutMatchers.
GTEST_API_ WithoutMatchers GetWithoutMatchers();

// FIXME: group all type utilities together.

// Type traits.

// is_reference<T>::value is non-zero iff T is a reference type.
template <typename T> struct is_reference : public false_type {};
template <typename T> struct is_reference<T&> : public true_type {};

// type_equals<T1, T2>::value is non-zero iff T1 and T2 are the same type.
template <typename T1, typename T2> struct type_equals : public false_type {};
template <typename T> struct type_equals<T, T> : public true_type {};

// remove_reference<T>::type removes the reference from type T, if any.
template <typename T> struct remove_reference { typedef T type; };  // NOLINT
template <typename T> struct remove_reference<T&> { typedef T type; }; // NOLINT

// DecayArray<T>::type turns an array type U[N] to const U* and preserves
// other types.  Useful for saving a copy of a function argument.
template <typename T> struct DecayArray { typedef T type; };  // NOLINT
template <typename T, size_t N> struct DecayArray<T[N]> {
  typedef const T* type;
};
// Sometimes people use arrays whose size is not available at the use site
// (e.g. extern const char kNamePrefix[]).  This specialization covers that
// case.
template <typename T> struct DecayArray<T[]> {
  typedef const T* type;
};

// Disable MSVC warnings for infinite recursion, since in this case the
// the recursion is unreachable.
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable:4717)
#endif

// Invalid<T>() is usable as an expression of type T, but will terminate
// the program with an assertion failure if actually run.  This is useful
// when a value of type T is needed for compilation, but the statement
// will not really be executed (or we don't care if the statement
// crashes).
template <typename T>
inline T Invalid() {
  Assert(false, "", -1, "Internal error: attempt to return invalid value");
  // This statement is unreachable, and would never terminate even if it
  // could be reached. It is provided only to placate compiler warnings
  // about missing return statements.
  return Invalid<T>();
}

#ifdef _MSC_VER
# pragma warning(pop)
#endif

// Given a raw type (i.e. having no top-level reference or const
// modifier) RawContainer that's either an STL-style container or a
// native array, class StlContainerView<RawContainer> has the
// following members:
//
//   - type is a type that provides an STL-style container view to
//     (i.e. implements the STL container concept for) RawContainer;
//   - const_reference is a type that provides a reference to a const
//     RawContainer;
//   - ConstReference(raw_container) returns a const reference to an STL-style
//     container view to raw_container, which is a RawContainer.
//   - Copy(raw_container) returns an STL-style container view of a
//     copy of raw_container, which is a RawContainer.
//
// This generic version is used when RawContainer itself is already an
// STL-style container.
template <class RawContainer>
class StlContainerView {
 public:
  typedef RawContainer type;
  typedef const type& const_reference;

  static const_reference ConstReference(const RawContainer& container) {
    // Ensures that RawContainer is not a const type.
    testing::StaticAssertTypeEq<RawContainer,
        GTEST_REMOVE_CONST_(RawContainer)>();
    return container;
  }
  static type Copy(const RawContainer& container) { return container; }
};

// This specialization is used when RawContainer is a native array type.
template <typename Element, size_t N>
class StlContainerView<Element[N]> {
 public:
  typedef GTEST_REMOVE_CONST_(Element) RawElement;
  typedef internal::NativeArray<RawElement> type;
  // NativeArray<T> can represent a native array either by value or by
  // reference (selected by a constructor argument), so 'const type'
  // can be used to reference a const native array.  We cannot
  // 'typedef const type& const_reference' here, as that would mean
  // ConstReference() has to return a reference to a local variable.
  typedef const type const_reference;

  static const_reference ConstReference(const Element (&array)[N]) {
    // Ensures that Element is not a const type.
    testing::StaticAssertTypeEq<Element, RawElement>();
#if GTEST_OS_SYMBIAN
    // The Nokia Symbian compiler confuses itself in template instantiation
    // for this call without the cast to Element*:
    // function call '[testing::internal::NativeArray<char *>].NativeArray(
    //     {lval} const char *[4], long, testing::internal::RelationToSource)'
    //     does not match
    // 'testing::internal::NativeArray<char *>::NativeArray(
    //     char *const *, unsigned int, testing::internal::RelationToSource)'
    // (instantiating: 'testing::internal::ContainsMatcherImpl
    //     <const char * (&)[4]>::Matches(const char * (&)[4]) const')
    // (instantiating: 'testing::internal::StlContainerView<char *[4]>::
    //     ConstReference(const char * (&)[4])')
    // (and though the N parameter type is mismatched in the above explicit
    // conversion of it doesn't help - only the conversion of the array).
    return type(const_cast<Element*>(&array[0]), N,
                RelationToSourceReference());
#else
    return type(array, N, RelationToSourceReference());
#endif  // GTEST_OS_SYMBIAN
  }
  static type Copy(const Element (&array)[N]) {
#if GTEST_OS_SYMBIAN
    return type(const_cast<Element*>(&array[0]), N, RelationToSourceCopy());
#else
    return type(array, N, RelationToSourceCopy());
#endif  // GTEST_OS_SYMBIAN
  }
};

// This specialization is used when RawContainer is a native array
// represented as a (pointer, size) tuple.
template <typename ElementPointer, typename Size>
class StlContainerView< ::testing::tuple<ElementPointer, Size> > {
 public:
  typedef GTEST_REMOVE_CONST_(
      typename internal::PointeeOf<ElementPointer>::type) RawElement;
  typedef internal::NativeArray<RawElement> type;
  typedef const type const_reference;

  static const_reference ConstReference(
      const ::testing::tuple<ElementPointer, Size>& array) {
    return type(get<0>(array), get<1>(array), RelationToSourceReference());
  }
  static type Copy(const ::testing::tuple<ElementPointer, Size>& array) {
    return type(get<0>(array), get<1>(array), RelationToSourceCopy());
  }
};

// The following specialization prevents the user from instantiating
// StlContainer with a reference type.
template <typename T> class StlContainerView<T&>;

// A type transform to remove constness from the first part of a pair.
// Pairs like that are used as the value_type of associative containers,
// and this transform produces a similar but assignable pair.
template <typename T>
struct RemoveConstFromKey {
  typedef T type;
};

// Partially specialized to remove constness from std::pair<const K, V>.
template <typename K, typename V>
struct RemoveConstFromKey<std::pair<const K, V> > {
  typedef std::pair<K, V> type;
};

// Mapping from booleans to types. Similar to boost::bool_<kValue> and
// std::integral_constant<bool, kValue>.
template <bool kValue>
struct BooleanConstant {};

// Emit an assertion failure due to incorrect DoDefault() usage. Out-of-lined to
// reduce code size.
GTEST_API_ void IllegalDoDefault(const char* file, int line);

#if GTEST_LANG_CXX11
// Helper types for Apply() below.
template <size_t... Is> struct int_pack { typedef int_pack type; };

template <class Pack, size_t I> struct append;
template <size_t... Is, size_t I>
struct append<int_pack<Is...>, I> : int_pack<Is..., I> {};

template <size_t C>
struct make_int_pack : append<typename make_int_pack<C - 1>::type, C - 1> {};
template <> struct make_int_pack<0> : int_pack<> {};

template <typename F, typename Tuple, size_t... Idx>
auto ApplyImpl(F&& f, Tuple&& args, int_pack<Idx...>) -> decltype(
    std::forward<F>(f)(std::get<Idx>(std::forward<Tuple>(args))...)) {
  return std::forward<F>(f)(std::get<Idx>(std::forward<Tuple>(args))...);
}

// Apply the function to a tuple of arguments.
template <typename F, typename Tuple>
auto Apply(F&& f, Tuple&& args)
    -> decltype(ApplyImpl(std::forward<F>(f), std::forward<Tuple>(args),
                          make_int_pack<std::tuple_size<Tuple>::value>())) {
  return ApplyImpl(std::forward<F>(f), std::forward<Tuple>(args),
                   make_int_pack<std::tuple_size<Tuple>::value>());
}
#endif


#ifdef _MSC_VER
# pragma warning(pop)
#endif

}  // namespace internal
}  // namespace testing

#endif  // GMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_INTERNAL_UTILS_H_
