//===-- tsan_rtl_proc.cc ------------------------------------------------===//
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

#include "sanitizer_common/sanitizer_placement_new.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"
#include "tsan_flags.h"

namespace __tsan {

Processor *ProcCreate() {
  void *mem = InternalAlloc(sizeof(Processor));
  internal_memset(mem, 0, sizeof(Processor));
  Processor *proc = new(mem) Processor;
  proc->thr = nullptr;
#if !SANITIZER_GO
  AllocatorProcStart(proc);
#endif
  if (common_flags()->detect_deadlocks)
    proc->dd_pt = ctx->dd->CreatePhysicalThread();
  return proc;
}

void ProcDestroy(Processor *proc) {
  CHECK_EQ(proc->thr, nullptr);
#if !SANITIZER_GO
  AllocatorProcFinish(proc);
#endif
  ctx->clock_alloc.FlushCache(&proc->clock_cache);
  ctx->metamap.OnProcIdle(proc);
  if (common_flags()->detect_deadlocks)
     ctx->dd->DestroyPhysicalThread(proc->dd_pt);
  proc->~Processor();
  InternalFree(proc);
}

void ProcWire(Processor *proc, ThreadState *thr) {
  CHECK_EQ(thr->proc1, nullptr);
  CHECK_EQ(proc->thr, nullptr);
  thr->proc1 = proc;
  proc->thr = thr;
}

void ProcUnwire(Processor *proc, ThreadState *thr) {
  CHECK_EQ(thr->proc1, proc);
  CHECK_EQ(proc->thr, thr);
  thr->proc1 = nullptr;
  proc->thr = nullptr;
}

}  // namespace __tsan
