//===- FuzzerExtFunctions.h - Interface to external functions ---*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Defines an interface to (possibly optional) functions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_EXT_FUNCTIONS_H
#define LLVM_FUZZER_EXT_FUNCTIONS_H

#include <stddef.h>
#include <stdint.h>

namespace fuzzer {

struct ExternalFunctions {
  // Initialize function pointers. Functions that are not available will be set
  // to nullptr.  Do not call this constructor  before ``main()`` has been
  // entered.
  ExternalFunctions();

#define EXT_FUNC(NAME, RETURN_TYPE, FUNC_SIG, WARN)                            \
  RETURN_TYPE(*NAME) FUNC_SIG = nullptr

#include "FuzzerExtFunctions.def"

#undef EXT_FUNC
};
} // namespace fuzzer

#endif
