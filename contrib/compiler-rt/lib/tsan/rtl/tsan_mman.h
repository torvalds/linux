//===-- tsan_mman.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_MMAN_H
#define TSAN_MMAN_H

#include "tsan_defs.h"

namespace __tsan {

const uptr kDefaultAlignment = 16;

void InitializeAllocator();
void InitializeAllocatorLate();
void ReplaceSystemMalloc();
void AllocatorProcStart(Processor *proc);
void AllocatorProcFinish(Processor *proc);
void AllocatorPrintStats();

// For user allocations.
void *user_alloc_internal(ThreadState *thr, uptr pc, uptr sz,
                          uptr align = kDefaultAlignment, bool signal = true);
// Does not accept NULL.
void user_free(ThreadState *thr, uptr pc, void *p, bool signal = true);
// Interceptor implementations.
void *user_alloc(ThreadState *thr, uptr pc, uptr sz);
void *user_calloc(ThreadState *thr, uptr pc, uptr sz, uptr n);
void *user_realloc(ThreadState *thr, uptr pc, void *p, uptr sz);
void *user_memalign(ThreadState *thr, uptr pc, uptr align, uptr sz);
int user_posix_memalign(ThreadState *thr, uptr pc, void **memptr, uptr align,
                        uptr sz);
void *user_aligned_alloc(ThreadState *thr, uptr pc, uptr align, uptr sz);
void *user_valloc(ThreadState *thr, uptr pc, uptr sz);
void *user_pvalloc(ThreadState *thr, uptr pc, uptr sz);
uptr user_alloc_usable_size(const void *p);

// Invoking malloc/free hooks that may be installed by the user.
void invoke_malloc_hook(void *ptr, uptr size);
void invoke_free_hook(void *ptr);

enum MBlockType {
  MBlockScopedBuf,
  MBlockString,
  MBlockStackTrace,
  MBlockShadowStack,
  MBlockSync,
  MBlockClock,
  MBlockThreadContex,
  MBlockDeadInfo,
  MBlockRacyStacks,
  MBlockRacyAddresses,
  MBlockAtExit,
  MBlockFlag,
  MBlockReport,
  MBlockReportMop,
  MBlockReportThread,
  MBlockReportMutex,
  MBlockReportLoc,
  MBlockReportStack,
  MBlockSuppression,
  MBlockExpectRace,
  MBlockSignal,
  MBlockJmpBuf,

  // This must be the last.
  MBlockTypeCount
};

// For internal data structures.
void *internal_alloc(MBlockType typ, uptr sz);
void internal_free(void *p);

template<typename T>
void DestroyAndFree(T *&p) {
  p->~T();
  internal_free(p);
  p = 0;
}

}  // namespace __tsan
#endif  // TSAN_MMAN_H
