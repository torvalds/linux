//===-- scudo_errors.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Header for scudo_errors.cpp.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ERRORS_H_
#define SCUDO_ERRORS_H_

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __scudo {

void NORETURN reportCallocOverflow(uptr Count, uptr Size);
void NORETURN reportPvallocOverflow(uptr Size);
void NORETURN reportAllocationAlignmentTooBig(uptr Alignment,
                                              uptr MaxAlignment);
void NORETURN reportAllocationAlignmentNotPowerOfTwo(uptr Alignment);
void NORETURN reportInvalidPosixMemalignAlignment(uptr Alignment);
void NORETURN reportInvalidAlignedAllocAlignment(uptr Size, uptr Alignment);
void NORETURN reportAllocationSizeTooBig(uptr UserSize, uptr TotalSize,
                                         uptr MaxSize);
void NORETURN reportRssLimitExceeded();
void NORETURN reportOutOfMemory(uptr RequestedSize);

}  // namespace __scudo

#endif  // SCUDO_ERRORS_H_
