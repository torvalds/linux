//===-- sanitizer_persistent_allocator.cc -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//
#include "sanitizer_persistent_allocator.h"

namespace __sanitizer {

PersistentAllocator thePersistentAllocator;

}  // namespace __sanitizer
