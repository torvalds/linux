//===-- msan_allocator.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef MSAN_ALLOCATOR_H
#define MSAN_ALLOCATOR_H

#include "sanitizer_common/sanitizer_common.h"

namespace __msan {

struct MsanThreadLocalMallocStorage {
  uptr quarantine_cache[16];
  // Allocator cache contains atomic_uint64_t which must be 8-byte aligned.
  ALIGNED(8) uptr allocator_cache[96 * (512 * 8 + 16)];  // Opaque.
  void CommitBack();

 private:
  // These objects are allocated via mmap() and are zero-initialized.
  MsanThreadLocalMallocStorage() {}
};

} // namespace __msan
#endif // MSAN_ALLOCATOR_H
