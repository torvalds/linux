//===-- tsan_go.cc --------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// ThreadSanitizer runtime for Go language.
//
//===----------------------------------------------------------------------===//

#include "tsan_rtl.h"
#include "tsan_symbolize.h"
#include "sanitizer_common/sanitizer_common.h"
#include <stdlib.h>

namespace __tsan {

void InitializeInterceptors() {
}

void InitializeDynamicAnnotations() {
}

bool IsExpectedReport(uptr addr, uptr size) {
  return false;
}

void *internal_alloc(MBlockType typ, uptr sz) {
  return InternalAlloc(sz);
}

void internal_free(void *p) {
  InternalFree(p);
}

// Callback into Go.
static void (*go_runtime_cb)(uptr cmd, void *ctx);

enum {
  CallbackGetProc = 0,
  CallbackSymbolizeCode = 1,
  CallbackSymbolizeData = 2,
};

struct SymbolizeCodeContext {
  uptr pc;
  char *func;
  char *file;
  uptr line;
  uptr off;
  uptr res;
};

SymbolizedStack *SymbolizeCode(uptr addr) {
  SymbolizedStack *s = SymbolizedStack::New(addr);
  SymbolizeCodeContext cbctx;
  internal_memset(&cbctx, 0, sizeof(cbctx));
  cbctx.pc = addr;
  go_runtime_cb(CallbackSymbolizeCode, &cbctx);
  if (cbctx.res) {
    AddressInfo &info = s->info;
    info.module_offset = cbctx.off;
    info.function = internal_strdup(cbctx.func ? cbctx.func : "??");
    info.file = internal_strdup(cbctx.file ? cbctx.file : "-");
    info.line = cbctx.line;
    info.column = 0;
  }
  return s;
}

struct SymbolizeDataContext {
  uptr addr;
  uptr heap;
  uptr start;
  uptr size;
  char *name;
  char *file;
  uptr line;
  uptr res;
};

ReportLocation *SymbolizeData(uptr addr) {
  SymbolizeDataContext cbctx;
  internal_memset(&cbctx, 0, sizeof(cbctx));
  cbctx.addr = addr;
  go_runtime_cb(CallbackSymbolizeData, &cbctx);
  if (!cbctx.res)
    return 0;
  if (cbctx.heap) {
    MBlock *b = ctx->metamap.GetBlock(cbctx.start);
    if (!b)
      return 0;
    ReportLocation *loc = ReportLocation::New(ReportLocationHeap);
    loc->heap_chunk_start = cbctx.start;
    loc->heap_chunk_size = b->siz;
    loc->tid = b->tid;
    loc->stack = SymbolizeStackId(b->stk);
    return loc;
  } else {
    ReportLocation *loc = ReportLocation::New(ReportLocationGlobal);
    loc->global.name = internal_strdup(cbctx.name ? cbctx.name : "??");
    loc->global.file = internal_strdup(cbctx.file ? cbctx.file : "??");
    loc->global.line = cbctx.line;
    loc->global.start = cbctx.start;
    loc->global.size = cbctx.size;
    return loc;
  }
}

static ThreadState *main_thr;
static bool inited;

static Processor* get_cur_proc() {
  if (UNLIKELY(!inited)) {
    // Running Initialize().
    // We have not yet returned the Processor to Go, so we cannot ask it back.
    // Currently, Initialize() does not use the Processor, so return nullptr.
    return nullptr;
  }
  Processor *proc;
  go_runtime_cb(CallbackGetProc, &proc);
  return proc;
}

Processor *ThreadState::proc() {
  return get_cur_proc();
}

extern "C" {

static ThreadState *AllocGoroutine() {
  ThreadState *thr = (ThreadState*)internal_alloc(MBlockThreadContex,
      sizeof(ThreadState));
  internal_memset(thr, 0, sizeof(*thr));
  return thr;
}

void __tsan_init(ThreadState **thrp, Processor **procp,
                 void (*cb)(uptr cmd, void *cb)) {
  go_runtime_cb = cb;
  ThreadState *thr = AllocGoroutine();
  main_thr = *thrp = thr;
  Initialize(thr);
  *procp = thr->proc1;
  inited = true;
}

void __tsan_fini() {
  // FIXME: Not necessary thread 0.
  ThreadState *thr = main_thr;
  int res = Finalize(thr);
  exit(res);
}

void __tsan_map_shadow(uptr addr, uptr size) {
  MapShadow(addr, size);
}

void __tsan_read(ThreadState *thr, void *addr, void *pc) {
  MemoryRead(thr, (uptr)pc, (uptr)addr, kSizeLog1);
}

void __tsan_read_pc(ThreadState *thr, void *addr, uptr callpc, uptr pc) {
  if (callpc != 0)
    FuncEntry(thr, callpc);
  MemoryRead(thr, (uptr)pc, (uptr)addr, kSizeLog1);
  if (callpc != 0)
    FuncExit(thr);
}

void __tsan_write(ThreadState *thr, void *addr, void *pc) {
  MemoryWrite(thr, (uptr)pc, (uptr)addr, kSizeLog1);
}

void __tsan_write_pc(ThreadState *thr, void *addr, uptr callpc, uptr pc) {
  if (callpc != 0)
    FuncEntry(thr, callpc);
  MemoryWrite(thr, (uptr)pc, (uptr)addr, kSizeLog1);
  if (callpc != 0)
    FuncExit(thr);
}

void __tsan_read_range(ThreadState *thr, void *addr, uptr size, uptr pc) {
  MemoryAccessRange(thr, (uptr)pc, (uptr)addr, size, false);
}

void __tsan_write_range(ThreadState *thr, void *addr, uptr size, uptr pc) {
  MemoryAccessRange(thr, (uptr)pc, (uptr)addr, size, true);
}

void __tsan_func_enter(ThreadState *thr, void *pc) {
  FuncEntry(thr, (uptr)pc);
}

void __tsan_func_exit(ThreadState *thr) {
  FuncExit(thr);
}

void __tsan_malloc(ThreadState *thr, uptr pc, uptr p, uptr sz) {
  CHECK(inited);
  if (thr && pc)
    ctx->metamap.AllocBlock(thr, pc, p, sz);
  MemoryResetRange(0, 0, (uptr)p, sz);
}

void __tsan_free(uptr p, uptr sz) {
  ctx->metamap.FreeRange(get_cur_proc(), p, sz);
}

void __tsan_go_start(ThreadState *parent, ThreadState **pthr, void *pc) {
  ThreadState *thr = AllocGoroutine();
  *pthr = thr;
  int goid = ThreadCreate(parent, (uptr)pc, 0, true);
  ThreadStart(thr, goid, 0, /*workerthread*/ false);
}

void __tsan_go_end(ThreadState *thr) {
  ThreadFinish(thr);
  internal_free(thr);
}

void __tsan_proc_create(Processor **pproc) {
  *pproc = ProcCreate();
}

void __tsan_proc_destroy(Processor *proc) {
  ProcDestroy(proc);
}

void __tsan_acquire(ThreadState *thr, void *addr) {
  Acquire(thr, 0, (uptr)addr);
}

void __tsan_release(ThreadState *thr, void *addr) {
  ReleaseStore(thr, 0, (uptr)addr);
}

void __tsan_release_merge(ThreadState *thr, void *addr) {
  Release(thr, 0, (uptr)addr);
}

void __tsan_finalizer_goroutine(ThreadState *thr) {
  AcquireGlobal(thr, 0);
}

void __tsan_mutex_before_lock(ThreadState *thr, uptr addr, uptr write) {
  if (write)
    MutexPreLock(thr, 0, addr);
  else
    MutexPreReadLock(thr, 0, addr);
}

void __tsan_mutex_after_lock(ThreadState *thr, uptr addr, uptr write) {
  if (write)
    MutexPostLock(thr, 0, addr);
  else
    MutexPostReadLock(thr, 0, addr);
}

void __tsan_mutex_before_unlock(ThreadState *thr, uptr addr, uptr write) {
  if (write)
    MutexUnlock(thr, 0, addr);
  else
    MutexReadUnlock(thr, 0, addr);
}

void __tsan_go_ignore_sync_begin(ThreadState *thr) {
  ThreadIgnoreSyncBegin(thr, 0);
}

void __tsan_go_ignore_sync_end(ThreadState *thr) {
  ThreadIgnoreSyncEnd(thr, 0);
}

void __tsan_report_count(u64 *pn) {
  Lock lock(&ctx->report_mtx);
  *pn = ctx->nreported;
}

}  // extern "C"
}  // namespace __tsan
