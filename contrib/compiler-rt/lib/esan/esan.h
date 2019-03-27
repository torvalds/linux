//===-- esan.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Main internal esan header file.
//
// Ground rules:
//   - C++ run-time should not be used (static CTORs, RTTI, exceptions, static
//     function-scope locals)
//   - All functions/classes/etc reside in namespace __esan, except for those
//     declared in esan_interface_internal.h.
//   - Platform-specific files should be used instead of ifdefs (*).
//   - No system headers included in header files (*).
//   - Platform specific headers included only into platform-specific files (*).
//
//  (*) Except when inlining is critical for performance.
//===----------------------------------------------------------------------===//

#ifndef ESAN_H
#define ESAN_H

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"
#include "esan_interface_internal.h"

namespace __esan {

extern bool EsanIsInitialized;
extern bool EsanDuringInit;
extern uptr VmaSize;

void initializeLibrary(ToolType Tool);
int finalizeLibrary();
void reportResults();
unsigned int getSampleCount();
// Esan creates the variable per tool per compilation unit at compile time
// and passes its pointer Ptr to the runtime library.
void processCompilationUnitInit(void *Ptr);
void processCompilationUnitExit(void *Ptr);
void processRangeAccess(uptr PC, uptr Addr, int Size, bool IsWrite);
void initializeInterceptors();

// Platform-dependent routines.
void verifyAddressSpace();
bool fixMmapAddr(void **Addr, SIZE_T Size, int Flags);
uptr checkMmapResult(uptr Addr, SIZE_T Size);
// The return value indicates whether to call the real version or not.
bool processSignal(int SigNum, void (*Handler)(int), void (**Result)(int));
bool processSigaction(int SigNum, const void *Act, void *OldAct);
bool processSigprocmask(int How, void *Set, void *OldSet);

} // namespace __esan

#endif // ESAN_H
