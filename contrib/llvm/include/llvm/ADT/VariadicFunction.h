//===--- VariadicFunctions.h - Variadic Functions ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements compile-time type-safe variadic functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_VARIADICFUNCTION_H
#define LLVM_ADT_VARIADICFUNCTION_H

#include "llvm/ADT/ArrayRef.h"

namespace llvm {

// Define macros to aid in expanding a comma separated series with the index of
// the series pasted onto the last token.
#define LLVM_COMMA_JOIN1(x) x ## 0
#define LLVM_COMMA_JOIN2(x) LLVM_COMMA_JOIN1(x), x ## 1
#define LLVM_COMMA_JOIN3(x) LLVM_COMMA_JOIN2(x), x ## 2
#define LLVM_COMMA_JOIN4(x) LLVM_COMMA_JOIN3(x), x ## 3
#define LLVM_COMMA_JOIN5(x) LLVM_COMMA_JOIN4(x), x ## 4
#define LLVM_COMMA_JOIN6(x) LLVM_COMMA_JOIN5(x), x ## 5
#define LLVM_COMMA_JOIN7(x) LLVM_COMMA_JOIN6(x), x ## 6
#define LLVM_COMMA_JOIN8(x) LLVM_COMMA_JOIN7(x), x ## 7
#define LLVM_COMMA_JOIN9(x) LLVM_COMMA_JOIN8(x), x ## 8
#define LLVM_COMMA_JOIN10(x) LLVM_COMMA_JOIN9(x), x ## 9
#define LLVM_COMMA_JOIN11(x) LLVM_COMMA_JOIN10(x), x ## 10
#define LLVM_COMMA_JOIN12(x) LLVM_COMMA_JOIN11(x), x ## 11
#define LLVM_COMMA_JOIN13(x) LLVM_COMMA_JOIN12(x), x ## 12
#define LLVM_COMMA_JOIN14(x) LLVM_COMMA_JOIN13(x), x ## 13
#define LLVM_COMMA_JOIN15(x) LLVM_COMMA_JOIN14(x), x ## 14
#define LLVM_COMMA_JOIN16(x) LLVM_COMMA_JOIN15(x), x ## 15
#define LLVM_COMMA_JOIN17(x) LLVM_COMMA_JOIN16(x), x ## 16
#define LLVM_COMMA_JOIN18(x) LLVM_COMMA_JOIN17(x), x ## 17
#define LLVM_COMMA_JOIN19(x) LLVM_COMMA_JOIN18(x), x ## 18
#define LLVM_COMMA_JOIN20(x) LLVM_COMMA_JOIN19(x), x ## 19
#define LLVM_COMMA_JOIN21(x) LLVM_COMMA_JOIN20(x), x ## 20
#define LLVM_COMMA_JOIN22(x) LLVM_COMMA_JOIN21(x), x ## 21
#define LLVM_COMMA_JOIN23(x) LLVM_COMMA_JOIN22(x), x ## 22
#define LLVM_COMMA_JOIN24(x) LLVM_COMMA_JOIN23(x), x ## 23
#define LLVM_COMMA_JOIN25(x) LLVM_COMMA_JOIN24(x), x ## 24
#define LLVM_COMMA_JOIN26(x) LLVM_COMMA_JOIN25(x), x ## 25
#define LLVM_COMMA_JOIN27(x) LLVM_COMMA_JOIN26(x), x ## 26
#define LLVM_COMMA_JOIN28(x) LLVM_COMMA_JOIN27(x), x ## 27
#define LLVM_COMMA_JOIN29(x) LLVM_COMMA_JOIN28(x), x ## 28
#define LLVM_COMMA_JOIN30(x) LLVM_COMMA_JOIN29(x), x ## 29
#define LLVM_COMMA_JOIN31(x) LLVM_COMMA_JOIN30(x), x ## 30
#define LLVM_COMMA_JOIN32(x) LLVM_COMMA_JOIN31(x), x ## 31

/// Class which can simulate a type-safe variadic function.
///
/// The VariadicFunction class template makes it easy to define
/// type-safe variadic functions where all arguments have the same
/// type.
///
/// Suppose we need a variadic function like this:
///
///   ResultT Foo(const ArgT &A_0, const ArgT &A_1, ..., const ArgT &A_N);
///
/// Instead of many overloads of Foo(), we only need to define a helper
/// function that takes an array of arguments:
///
///   ResultT FooImpl(ArrayRef<const ArgT *> Args) {
///     // 'Args[i]' is a pointer to the i-th argument passed to Foo().
///     ...
///   }
///
/// and then define Foo() like this:
///
///   const VariadicFunction<ResultT, ArgT, FooImpl> Foo;
///
/// VariadicFunction takes care of defining the overloads of Foo().
///
/// Actually, Foo is a function object (i.e. functor) instead of a plain
/// function.  This object is stateless and its constructor/destructor
/// does nothing, so it's safe to create global objects and call Foo(...) at
/// any time.
///
/// Sometimes we need a variadic function to have some fixed leading
/// arguments whose types may be different from that of the optional
/// arguments.  For example:
///
///   bool FullMatch(const StringRef &S, const RE &Regex,
///                  const ArgT &A_0, ..., const ArgT &A_N);
///
/// VariadicFunctionN is for such cases, where N is the number of fixed
/// arguments.  It is like VariadicFunction, except that it takes N more
/// template arguments for the types of the fixed arguments:
///
///   bool FullMatchImpl(const StringRef &S, const RE &Regex,
///                      ArrayRef<const ArgT *> Args) { ... }
///   const VariadicFunction2<bool, const StringRef&,
///                           const RE&, ArgT, FullMatchImpl>
///       FullMatch;
///
/// Currently VariadicFunction and friends support up-to 3
/// fixed leading arguments and up-to 32 optional arguments.
template <typename ResultT, typename ArgT,
          ResultT (*Func)(ArrayRef<const ArgT *>)>
struct VariadicFunction {
  ResultT operator()() const {
    return Func(None);
  }

#define LLVM_DEFINE_OVERLOAD(N) \
  ResultT operator()(LLVM_COMMA_JOIN ## N(const ArgT &A)) const { \
    const ArgT *const Args[] = { LLVM_COMMA_JOIN ## N(&A) }; \
    return Func(makeArrayRef(Args)); \
  }
  LLVM_DEFINE_OVERLOAD(1)
  LLVM_DEFINE_OVERLOAD(2)
  LLVM_DEFINE_OVERLOAD(3)
  LLVM_DEFINE_OVERLOAD(4)
  LLVM_DEFINE_OVERLOAD(5)
  LLVM_DEFINE_OVERLOAD(6)
  LLVM_DEFINE_OVERLOAD(7)
  LLVM_DEFINE_OVERLOAD(8)
  LLVM_DEFINE_OVERLOAD(9)
  LLVM_DEFINE_OVERLOAD(10)
  LLVM_DEFINE_OVERLOAD(11)
  LLVM_DEFINE_OVERLOAD(12)
  LLVM_DEFINE_OVERLOAD(13)
  LLVM_DEFINE_OVERLOAD(14)
  LLVM_DEFINE_OVERLOAD(15)
  LLVM_DEFINE_OVERLOAD(16)
  LLVM_DEFINE_OVERLOAD(17)
  LLVM_DEFINE_OVERLOAD(18)
  LLVM_DEFINE_OVERLOAD(19)
  LLVM_DEFINE_OVERLOAD(20)
  LLVM_DEFINE_OVERLOAD(21)
  LLVM_DEFINE_OVERLOAD(22)
  LLVM_DEFINE_OVERLOAD(23)
  LLVM_DEFINE_OVERLOAD(24)
  LLVM_DEFINE_OVERLOAD(25)
  LLVM_DEFINE_OVERLOAD(26)
  LLVM_DEFINE_OVERLOAD(27)
  LLVM_DEFINE_OVERLOAD(28)
  LLVM_DEFINE_OVERLOAD(29)
  LLVM_DEFINE_OVERLOAD(30)
  LLVM_DEFINE_OVERLOAD(31)
  LLVM_DEFINE_OVERLOAD(32)
#undef LLVM_DEFINE_OVERLOAD
};

template <typename ResultT, typename Param0T, typename ArgT,
          ResultT (*Func)(Param0T, ArrayRef<const ArgT *>)>
struct VariadicFunction1 {
  ResultT operator()(Param0T P0) const {
    return Func(P0, None);
  }

#define LLVM_DEFINE_OVERLOAD(N) \
  ResultT operator()(Param0T P0, LLVM_COMMA_JOIN ## N(const ArgT &A)) const { \
    const ArgT *const Args[] = { LLVM_COMMA_JOIN ## N(&A) }; \
    return Func(P0, makeArrayRef(Args)); \
  }
  LLVM_DEFINE_OVERLOAD(1)
  LLVM_DEFINE_OVERLOAD(2)
  LLVM_DEFINE_OVERLOAD(3)
  LLVM_DEFINE_OVERLOAD(4)
  LLVM_DEFINE_OVERLOAD(5)
  LLVM_DEFINE_OVERLOAD(6)
  LLVM_DEFINE_OVERLOAD(7)
  LLVM_DEFINE_OVERLOAD(8)
  LLVM_DEFINE_OVERLOAD(9)
  LLVM_DEFINE_OVERLOAD(10)
  LLVM_DEFINE_OVERLOAD(11)
  LLVM_DEFINE_OVERLOAD(12)
  LLVM_DEFINE_OVERLOAD(13)
  LLVM_DEFINE_OVERLOAD(14)
  LLVM_DEFINE_OVERLOAD(15)
  LLVM_DEFINE_OVERLOAD(16)
  LLVM_DEFINE_OVERLOAD(17)
  LLVM_DEFINE_OVERLOAD(18)
  LLVM_DEFINE_OVERLOAD(19)
  LLVM_DEFINE_OVERLOAD(20)
  LLVM_DEFINE_OVERLOAD(21)
  LLVM_DEFINE_OVERLOAD(22)
  LLVM_DEFINE_OVERLOAD(23)
  LLVM_DEFINE_OVERLOAD(24)
  LLVM_DEFINE_OVERLOAD(25)
  LLVM_DEFINE_OVERLOAD(26)
  LLVM_DEFINE_OVERLOAD(27)
  LLVM_DEFINE_OVERLOAD(28)
  LLVM_DEFINE_OVERLOAD(29)
  LLVM_DEFINE_OVERLOAD(30)
  LLVM_DEFINE_OVERLOAD(31)
  LLVM_DEFINE_OVERLOAD(32)
#undef LLVM_DEFINE_OVERLOAD
};

template <typename ResultT, typename Param0T, typename Param1T, typename ArgT,
          ResultT (*Func)(Param0T, Param1T, ArrayRef<const ArgT *>)>
struct VariadicFunction2 {
  ResultT operator()(Param0T P0, Param1T P1) const {
    return Func(P0, P1, None);
  }

#define LLVM_DEFINE_OVERLOAD(N) \
  ResultT operator()(Param0T P0, Param1T P1, \
                     LLVM_COMMA_JOIN ## N(const ArgT &A)) const { \
    const ArgT *const Args[] = { LLVM_COMMA_JOIN ## N(&A) }; \
    return Func(P0, P1, makeArrayRef(Args)); \
  }
  LLVM_DEFINE_OVERLOAD(1)
  LLVM_DEFINE_OVERLOAD(2)
  LLVM_DEFINE_OVERLOAD(3)
  LLVM_DEFINE_OVERLOAD(4)
  LLVM_DEFINE_OVERLOAD(5)
  LLVM_DEFINE_OVERLOAD(6)
  LLVM_DEFINE_OVERLOAD(7)
  LLVM_DEFINE_OVERLOAD(8)
  LLVM_DEFINE_OVERLOAD(9)
  LLVM_DEFINE_OVERLOAD(10)
  LLVM_DEFINE_OVERLOAD(11)
  LLVM_DEFINE_OVERLOAD(12)
  LLVM_DEFINE_OVERLOAD(13)
  LLVM_DEFINE_OVERLOAD(14)
  LLVM_DEFINE_OVERLOAD(15)
  LLVM_DEFINE_OVERLOAD(16)
  LLVM_DEFINE_OVERLOAD(17)
  LLVM_DEFINE_OVERLOAD(18)
  LLVM_DEFINE_OVERLOAD(19)
  LLVM_DEFINE_OVERLOAD(20)
  LLVM_DEFINE_OVERLOAD(21)
  LLVM_DEFINE_OVERLOAD(22)
  LLVM_DEFINE_OVERLOAD(23)
  LLVM_DEFINE_OVERLOAD(24)
  LLVM_DEFINE_OVERLOAD(25)
  LLVM_DEFINE_OVERLOAD(26)
  LLVM_DEFINE_OVERLOAD(27)
  LLVM_DEFINE_OVERLOAD(28)
  LLVM_DEFINE_OVERLOAD(29)
  LLVM_DEFINE_OVERLOAD(30)
  LLVM_DEFINE_OVERLOAD(31)
  LLVM_DEFINE_OVERLOAD(32)
#undef LLVM_DEFINE_OVERLOAD
};

template <typename ResultT, typename Param0T, typename Param1T,
          typename Param2T, typename ArgT,
          ResultT (*Func)(Param0T, Param1T, Param2T, ArrayRef<const ArgT *>)>
struct VariadicFunction3 {
  ResultT operator()(Param0T P0, Param1T P1, Param2T P2) const {
    return Func(P0, P1, P2, None);
  }

#define LLVM_DEFINE_OVERLOAD(N) \
  ResultT operator()(Param0T P0, Param1T P1, Param2T P2, \
                     LLVM_COMMA_JOIN ## N(const ArgT &A)) const { \
    const ArgT *const Args[] = { LLVM_COMMA_JOIN ## N(&A) }; \
    return Func(P0, P1, P2, makeArrayRef(Args)); \
  }
  LLVM_DEFINE_OVERLOAD(1)
  LLVM_DEFINE_OVERLOAD(2)
  LLVM_DEFINE_OVERLOAD(3)
  LLVM_DEFINE_OVERLOAD(4)
  LLVM_DEFINE_OVERLOAD(5)
  LLVM_DEFINE_OVERLOAD(6)
  LLVM_DEFINE_OVERLOAD(7)
  LLVM_DEFINE_OVERLOAD(8)
  LLVM_DEFINE_OVERLOAD(9)
  LLVM_DEFINE_OVERLOAD(10)
  LLVM_DEFINE_OVERLOAD(11)
  LLVM_DEFINE_OVERLOAD(12)
  LLVM_DEFINE_OVERLOAD(13)
  LLVM_DEFINE_OVERLOAD(14)
  LLVM_DEFINE_OVERLOAD(15)
  LLVM_DEFINE_OVERLOAD(16)
  LLVM_DEFINE_OVERLOAD(17)
  LLVM_DEFINE_OVERLOAD(18)
  LLVM_DEFINE_OVERLOAD(19)
  LLVM_DEFINE_OVERLOAD(20)
  LLVM_DEFINE_OVERLOAD(21)
  LLVM_DEFINE_OVERLOAD(22)
  LLVM_DEFINE_OVERLOAD(23)
  LLVM_DEFINE_OVERLOAD(24)
  LLVM_DEFINE_OVERLOAD(25)
  LLVM_DEFINE_OVERLOAD(26)
  LLVM_DEFINE_OVERLOAD(27)
  LLVM_DEFINE_OVERLOAD(28)
  LLVM_DEFINE_OVERLOAD(29)
  LLVM_DEFINE_OVERLOAD(30)
  LLVM_DEFINE_OVERLOAD(31)
  LLVM_DEFINE_OVERLOAD(32)
#undef LLVM_DEFINE_OVERLOAD
};

// Cleanup the macro namespace.
#undef LLVM_COMMA_JOIN1
#undef LLVM_COMMA_JOIN2
#undef LLVM_COMMA_JOIN3
#undef LLVM_COMMA_JOIN4
#undef LLVM_COMMA_JOIN5
#undef LLVM_COMMA_JOIN6
#undef LLVM_COMMA_JOIN7
#undef LLVM_COMMA_JOIN8
#undef LLVM_COMMA_JOIN9
#undef LLVM_COMMA_JOIN10
#undef LLVM_COMMA_JOIN11
#undef LLVM_COMMA_JOIN12
#undef LLVM_COMMA_JOIN13
#undef LLVM_COMMA_JOIN14
#undef LLVM_COMMA_JOIN15
#undef LLVM_COMMA_JOIN16
#undef LLVM_COMMA_JOIN17
#undef LLVM_COMMA_JOIN18
#undef LLVM_COMMA_JOIN19
#undef LLVM_COMMA_JOIN20
#undef LLVM_COMMA_JOIN21
#undef LLVM_COMMA_JOIN22
#undef LLVM_COMMA_JOIN23
#undef LLVM_COMMA_JOIN24
#undef LLVM_COMMA_JOIN25
#undef LLVM_COMMA_JOIN26
#undef LLVM_COMMA_JOIN27
#undef LLVM_COMMA_JOIN28
#undef LLVM_COMMA_JOIN29
#undef LLVM_COMMA_JOIN30
#undef LLVM_COMMA_JOIN31
#undef LLVM_COMMA_JOIN32

} // end namespace llvm

#endif  // LLVM_ADT_VARIADICFUNCTION_H
