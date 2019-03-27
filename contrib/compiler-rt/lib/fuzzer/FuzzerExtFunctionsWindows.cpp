//=== FuzzerExtWindows.cpp - Interface to external functions --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Implementation of FuzzerExtFunctions for Windows. Uses alternatename when
// compiled with MSVC. Uses weak aliases when compiled with clang. Unfortunately
// the method each compiler supports is not supported by the other.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_WINDOWS

#include "FuzzerExtFunctions.h"
#include "FuzzerIO.h"

using namespace fuzzer;

// Intermediate macro to ensure the parameter is expanded before stringified.
#define STRINGIFY_(A) #A
#define STRINGIFY(A) STRINGIFY_(A)

#if LIBFUZZER_MSVC
// Copied from compiler-rt/lib/sanitizer_common/sanitizer_win_defs.h
#if defined(_M_IX86) || defined(__i386__)
#define WIN_SYM_PREFIX "_"
#else
#define WIN_SYM_PREFIX
#endif

// Declare external functions as having alternativenames, so that we can
// determine if they are not defined.
#define EXTERNAL_FUNC(Name, Default)                                   \
  __pragma(comment(linker, "/alternatename:" WIN_SYM_PREFIX STRINGIFY( \
                               Name) "=" WIN_SYM_PREFIX STRINGIFY(Default)))
#else
// Declare external functions as weak to allow them to default to a specified
// function if not defined explicitly. We must use weak symbols because clang's
// support for alternatename is not 100%, see
// https://bugs.llvm.org/show_bug.cgi?id=40218 for more details.
#define EXTERNAL_FUNC(Name, Default) \
  __attribute__((weak, alias(STRINGIFY(Default))))
#endif  // LIBFUZZER_MSVC

extern "C" {
#define EXT_FUNC(NAME, RETURN_TYPE, FUNC_SIG, WARN)         \
  RETURN_TYPE NAME##Def FUNC_SIG {                          \
    Printf("ERROR: Function \"%s\" not defined.\n", #NAME); \
    exit(1);                                                \
  }                                                         \
  EXTERNAL_FUNC(NAME, NAME##Def) RETURN_TYPE NAME FUNC_SIG;

#include "FuzzerExtFunctions.def"

#undef EXT_FUNC
}

template <typename T>
static T *GetFnPtr(T *Fun, T *FunDef, const char *FnName, bool WarnIfMissing) {
  if (Fun == FunDef) {
    if (WarnIfMissing)
      Printf("WARNING: Failed to find function \"%s\".\n", FnName);
    return nullptr;
  }
  return Fun;
}

namespace fuzzer {

ExternalFunctions::ExternalFunctions() {
#define EXT_FUNC(NAME, RETURN_TYPE, FUNC_SIG, WARN) \
  this->NAME = GetFnPtr<decltype(::NAME)>(::NAME, ::NAME##Def, #NAME, WARN);

#include "FuzzerExtFunctions.def"

#undef EXT_FUNC
}

}  // namespace fuzzer

#endif // LIBFUZZER_WINDOWS
