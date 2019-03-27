//===-- tsan_interface_java.cc --------------------------------------------===//
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

#include "tsan_interface_java.h"
#include "tsan_rtl.h"
#include "tsan_mutex.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
#include "sanitizer_common/sanitizer_procmaps.h"

using namespace __tsan;  // NOLINT

const jptr kHeapAlignment = 8;

namespace __tsan {

struct JavaContext {
  const uptr heap_begin;
  const uptr heap_size;

  JavaContext(jptr heap_begin, jptr heap_size)
      : heap_begin(heap_begin)
      , heap_size(heap_size) {
  }
};

class ScopedJavaFunc {
 public:
  ScopedJavaFunc(ThreadState *thr, uptr pc)
      : thr_(thr) {
    Initialize(thr_);
    FuncEntry(thr, pc);
  }

  ~ScopedJavaFunc() {
    FuncExit(thr_);
    // FIXME(dvyukov): process pending signals.
  }

 private:
  ThreadState *thr_;
};

static u64 jctx_buf[sizeof(JavaContext) / sizeof(u64) + 1];
static JavaContext *jctx;

}  // namespace __tsan

#define SCOPED_JAVA_FUNC(func) \
  ThreadState *thr = cur_thread(); \
  const uptr caller_pc = GET_CALLER_PC(); \
  const uptr pc = StackTrace::GetCurrentPc(); \
  (void)pc; \
  ScopedJavaFunc scoped(thr, caller_pc); \
/**/

void __tsan_java_init(jptr heap_begin, jptr heap_size) {
  SCOPED_JAVA_FUNC(__tsan_java_init);
  DPrintf("#%d: java_init(%p, %p)\n", thr->tid, heap_begin, heap_size);
  CHECK_EQ(jctx, 0);
  CHECK_GT(heap_begin, 0);
  CHECK_GT(heap_size, 0);
  CHECK_EQ(heap_begin % kHeapAlignment, 0);
  CHECK_EQ(heap_size % kHeapAlignment, 0);
  CHECK_LT(heap_begin, heap_begin + heap_size);
  jctx = new(jctx_buf) JavaContext(heap_begin, heap_size);
}

int  __tsan_java_fini() {
  SCOPED_JAVA_FUNC(__tsan_java_fini);
  DPrintf("#%d: java_fini()\n", thr->tid);
  CHECK_NE(jctx, 0);
  // FIXME(dvyukov): this does not call atexit() callbacks.
  int status = Finalize(thr);
  DPrintf("#%d: java_fini() = %d\n", thr->tid, status);
  return status;
}

void __tsan_java_alloc(jptr ptr, jptr size) {
  SCOPED_JAVA_FUNC(__tsan_java_alloc);
  DPrintf("#%d: java_alloc(%p, %p)\n", thr->tid, ptr, size);
  CHECK_NE(jctx, 0);
  CHECK_NE(size, 0);
  CHECK_EQ(ptr % kHeapAlignment, 0);
  CHECK_EQ(size % kHeapAlignment, 0);
  CHECK_GE(ptr, jctx->heap_begin);
  CHECK_LE(ptr + size, jctx->heap_begin + jctx->heap_size);

  OnUserAlloc(thr, pc, ptr, size, false);
}

void __tsan_java_free(jptr ptr, jptr size) {
  SCOPED_JAVA_FUNC(__tsan_java_free);
  DPrintf("#%d: java_free(%p, %p)\n", thr->tid, ptr, size);
  CHECK_NE(jctx, 0);
  CHECK_NE(size, 0);
  CHECK_EQ(ptr % kHeapAlignment, 0);
  CHECK_EQ(size % kHeapAlignment, 0);
  CHECK_GE(ptr, jctx->heap_begin);
  CHECK_LE(ptr + size, jctx->heap_begin + jctx->heap_size);

  ctx->metamap.FreeRange(thr->proc(), ptr, size);
}

void __tsan_java_move(jptr src, jptr dst, jptr size) {
  SCOPED_JAVA_FUNC(__tsan_java_move);
  DPrintf("#%d: java_move(%p, %p, %p)\n", thr->tid, src, dst, size);
  CHECK_NE(jctx, 0);
  CHECK_NE(size, 0);
  CHECK_EQ(src % kHeapAlignment, 0);
  CHECK_EQ(dst % kHeapAlignment, 0);
  CHECK_EQ(size % kHeapAlignment, 0);
  CHECK_GE(src, jctx->heap_begin);
  CHECK_LE(src + size, jctx->heap_begin + jctx->heap_size);
  CHECK_GE(dst, jctx->heap_begin);
  CHECK_LE(dst + size, jctx->heap_begin + jctx->heap_size);
  CHECK_NE(dst, src);
  CHECK_NE(size, 0);

  // Assuming it's not running concurrently with threads that do
  // memory accesses and mutex operations (stop-the-world phase).
  ctx->metamap.MoveMemory(src, dst, size);

  // Move shadow.
  u64 *s = (u64*)MemToShadow(src);
  u64 *d = (u64*)MemToShadow(dst);
  u64 *send = (u64*)MemToShadow(src + size);
  uptr inc = 1;
  if (dst > src) {
    s = (u64*)MemToShadow(src + size) - 1;
    d = (u64*)MemToShadow(dst + size) - 1;
    send = (u64*)MemToShadow(src) - 1;
    inc = -1;
  }
  for (; s != send; s += inc, d += inc) {
    *d = *s;
    *s = 0;
  }
}

jptr __tsan_java_find(jptr *from_ptr, jptr to) {
  SCOPED_JAVA_FUNC(__tsan_java_find);
  DPrintf("#%d: java_find(&%p, %p)\n", *from_ptr, to);
  CHECK_EQ((*from_ptr) % kHeapAlignment, 0);
  CHECK_EQ(to % kHeapAlignment, 0);
  CHECK_GE(*from_ptr, jctx->heap_begin);
  CHECK_LE(to, jctx->heap_begin + jctx->heap_size);
  for (uptr from = *from_ptr; from < to; from += kHeapAlignment) {
    MBlock *b = ctx->metamap.GetBlock(from);
    if (b) {
      *from_ptr = from;
      return b->siz;
    }
  }
  return 0;
}

void __tsan_java_finalize() {
  SCOPED_JAVA_FUNC(__tsan_java_finalize);
  DPrintf("#%d: java_mutex_finalize()\n", thr->tid);
  AcquireGlobal(thr, 0);
}

void __tsan_java_mutex_lock(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_mutex_lock);
  DPrintf("#%d: java_mutex_lock(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexPostLock(thr, pc, addr, MutexFlagLinkerInit | MutexFlagWriteReentrant |
      MutexFlagDoPreLockOnPostLock);
}

void __tsan_java_mutex_unlock(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_mutex_unlock);
  DPrintf("#%d: java_mutex_unlock(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexUnlock(thr, pc, addr);
}

void __tsan_java_mutex_read_lock(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_mutex_read_lock);
  DPrintf("#%d: java_mutex_read_lock(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexPostReadLock(thr, pc, addr, MutexFlagLinkerInit |
      MutexFlagWriteReentrant | MutexFlagDoPreLockOnPostLock);
}

void __tsan_java_mutex_read_unlock(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_mutex_read_unlock);
  DPrintf("#%d: java_mutex_read_unlock(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  MutexReadUnlock(thr, pc, addr);
}

void __tsan_java_mutex_lock_rec(jptr addr, int rec) {
  SCOPED_JAVA_FUNC(__tsan_java_mutex_lock_rec);
  DPrintf("#%d: java_mutex_lock_rec(%p, %d)\n", thr->tid, addr, rec);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);
  CHECK_GT(rec, 0);

  MutexPostLock(thr, pc, addr, MutexFlagLinkerInit | MutexFlagWriteReentrant |
      MutexFlagDoPreLockOnPostLock | MutexFlagRecursiveLock, rec);
}

int __tsan_java_mutex_unlock_rec(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_mutex_unlock_rec);
  DPrintf("#%d: java_mutex_unlock_rec(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  return MutexUnlock(thr, pc, addr, MutexFlagRecursiveUnlock);
}

void __tsan_java_acquire(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_acquire);
  DPrintf("#%d: java_acquire(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  Acquire(thr, caller_pc, addr);
}

void __tsan_java_release(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_release);
  DPrintf("#%d: java_release(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  Release(thr, caller_pc, addr);
}

void __tsan_java_release_store(jptr addr) {
  SCOPED_JAVA_FUNC(__tsan_java_release);
  DPrintf("#%d: java_release_store(%p)\n", thr->tid, addr);
  CHECK_NE(jctx, 0);
  CHECK_GE(addr, jctx->heap_begin);
  CHECK_LT(addr, jctx->heap_begin + jctx->heap_size);

  ReleaseStore(thr, caller_pc, addr);
}
