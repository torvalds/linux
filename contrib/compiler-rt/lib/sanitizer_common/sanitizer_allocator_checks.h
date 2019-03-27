//===-- sanitizer_allocator_checks.h ----------------------------*- C++ -*-===//
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

#ifndef SANITIZER_ALLOCATOR_CHECKS_H
#define SANITIZER_ALLOCATOR_CHECKS_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_common.h"
#include "sanitizer_platform.h"

namespace __sanitizer {

// The following is defined in a separate compilation unit to avoid pulling in
// sanitizer_errno.h in this header, which leads to conflicts when other system
// headers include errno.h. This is usually the result of an unlikely event,
// and as such we do not care as much about having it inlined.
void SetErrnoToENOMEM();

// A common errno setting logic shared by almost all sanitizer allocator APIs.
INLINE void *SetErrnoOnNull(void *ptr) {
  if (UNLIKELY(!ptr))
    SetErrnoToENOMEM();
  return ptr;
}

// In case of the check failure, the caller of the following Check... functions
// should "return POLICY::OnBadRequest();" where POLICY is the current allocator
// failure handling policy.

// Checks aligned_alloc() parameters, verifies that the alignment is a power of
// two and that the size is a multiple of alignment for POSIX implementation,
// and a bit relaxed requirement for non-POSIX ones, that the size is a multiple
// of alignment.
INLINE bool CheckAlignedAllocAlignmentAndSize(uptr alignment, uptr size) {
#if SANITIZER_POSIX
  return alignment != 0 && IsPowerOfTwo(alignment) &&
         (size & (alignment - 1)) == 0;
#else
  return alignment != 0 && size % alignment == 0;
#endif
}

// Checks posix_memalign() parameters, verifies that alignment is a power of two
// and a multiple of sizeof(void *).
INLINE bool CheckPosixMemalignAlignment(uptr alignment) {
  return alignment != 0 && IsPowerOfTwo(alignment) &&
         (alignment % sizeof(void *)) == 0; // NOLINT
}

// Returns true if calloc(size, n) call overflows on size*n calculation.
INLINE bool CheckForCallocOverflow(uptr size, uptr n) {
  if (!size)
    return false;
  uptr max = (uptr)-1L;
  return (max / size) < n;
}

// Returns true if the size passed to pvalloc overflows when rounded to the next
// multiple of page_size.
INLINE bool CheckForPvallocOverflow(uptr size, uptr page_size) {
  return RoundUpTo(size, page_size) < size;
}

} // namespace __sanitizer

#endif  // SANITIZER_ALLOCATOR_CHECKS_H
