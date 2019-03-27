//===-- hwasan_report.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer. HWASan-private header for error
/// reporting functions.
///
//===----------------------------------------------------------------------===//

#ifndef HWASAN_REPORT_H
#define HWASAN_REPORT_H

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

namespace __hwasan {

void ReportStats();
void ReportTagMismatch(StackTrace *stack, uptr addr, uptr access_size,
                       bool is_store, bool fatal);
void ReportInvalidFree(StackTrace *stack, uptr addr);
void ReportTailOverwritten(StackTrace *stack, uptr addr, uptr orig_size,
                           uptr tail_size, const u8 *expected);

void ReportAtExitStatistics();


}  // namespace __hwasan

#endif  // HWASAN_REPORT_H
