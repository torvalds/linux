//===--------------------- LLDBAssert.cpp ------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/LLDBAssert.h"

#include "llvm/Support/Format.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace lldb_private;

void lldb_private::lldb_assert(bool expression, const char *expr_text,
                               const char *func, const char *file,
                               unsigned int line) {
  if (LLVM_LIKELY(expression))
    return;

  errs() << format("Assertion failed: (%s), function %s, file %s, line %u\n",
                   expr_text, func, file, line);
  errs() << "backtrace leading to the failure:\n";
  llvm::sys::PrintStackTrace(errs());
  errs() << "please file a bug report against lldb reporting this failure "
            "log, and as many details as possible\n";
  abort();
}
