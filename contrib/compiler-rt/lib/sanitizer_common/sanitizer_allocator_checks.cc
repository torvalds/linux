//===-- sanitizer_allocator_checks.cc ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Various checks shared between ThreadSanitizer, MemorySanitizer, etc. memory
// allocators.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_errno.h"

namespace __sanitizer {

void SetErrnoToENOMEM() {
  errno = errno_ENOMEM;
}

} // namespace __sanitizer
