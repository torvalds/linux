//===-- scudo_errors.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Verbose termination functions.
///
//===----------------------------------------------------------------------===//

#include "scudo_utils.h"

#include "sanitizer_common/sanitizer_flags.h"

namespace __scudo {

void NORETURN reportCallocOverflow(uptr Count, uptr Size) {
  dieWithMessage("calloc parameters overflow: count * size (%zd * %zd) cannot "
      "be represented with type size_t\n", Count, Size);
}

void NORETURN reportPvallocOverflow(uptr Size) {
  dieWithMessage("pvalloc parameters overflow: size 0x%zx rounded up to system "
      "page size 0x%zx cannot be represented in type size_t\n", Size,
      GetPageSizeCached());
}

void NORETURN reportAllocationAlignmentTooBig(uptr Alignment,
                                              uptr MaxAlignment) {
  dieWithMessage("invalid allocation alignment: %zd exceeds maximum supported "
      "allocation of %zd\n", Alignment, MaxAlignment);
}

void NORETURN reportAllocationAlignmentNotPowerOfTwo(uptr Alignment) {
  dieWithMessage("invalid allocation alignment: %zd, alignment must be a power "
      "of two\n", Alignment);
}

void NORETURN reportInvalidPosixMemalignAlignment(uptr Alignment) {
  dieWithMessage("invalid alignment requested in posix_memalign: %zd, alignment"
      " must be a power of two and a multiple of sizeof(void *) == %zd\n",
      Alignment, sizeof(void *));  // NOLINT
}

void NORETURN reportInvalidAlignedAllocAlignment(uptr Size, uptr Alignment) {
#if SANITIZER_POSIX
  dieWithMessage("invalid alignment requested in aligned_alloc: %zd, alignment "
      "must be a power of two and the requested size 0x%zx must be a multiple "
      "of alignment\n", Alignment, Size);
#else
  dieWithMessage("invalid alignment requested in aligned_alloc: %zd, the "
      "requested size 0x%zx must be a multiple of alignment\n", Alignment,
      Size);
#endif
}

void NORETURN reportAllocationSizeTooBig(uptr UserSize, uptr TotalSize,
                                         uptr MaxSize) {
  dieWithMessage("requested allocation size 0x%zx (0x%zx after adjustments) "
      "exceeds maximum supported size of 0x%zx\n", UserSize, TotalSize,
      MaxSize);
}

void NORETURN reportRssLimitExceeded() {
  dieWithMessage("specified RSS limit exceeded, currently set to "
      "soft_rss_limit_mb=%zd\n", common_flags()->soft_rss_limit_mb);
}

void NORETURN reportOutOfMemory(uptr RequestedSize) {
  dieWithMessage("allocator is out of memory trying to allocate 0x%zx bytes\n",
                 RequestedSize);
}

}  // namespace __scudo
