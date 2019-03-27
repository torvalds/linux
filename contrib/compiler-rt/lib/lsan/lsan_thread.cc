//=-- lsan_thread.cc ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// See lsan_thread.h for details.
//
//===----------------------------------------------------------------------===//

#include "lsan_thread.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_thread_registry.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "lsan_allocator.h"
#include "lsan_common.h"

namespace __lsan {

static ThreadRegistry *thread_registry;

static ThreadContextBase *CreateThreadContext(u32 tid) {
  void *mem = MmapOrDie(sizeof(ThreadContext), "ThreadContext");
  return new(mem) ThreadContext(tid);
}

static const uptr kMaxThreads = 1 << 13;
static const uptr kThreadQuarantineSize = 64;

void InitializeThreadRegistry() {
  static ALIGNED(64) char thread_registry_placeholder[sizeof(ThreadRegistry)];
  thread_registry = new(thread_registry_placeholder)
    ThreadRegistry(CreateThreadContext, kMaxThreads, kThreadQuarantineSize);
}

ThreadContext::ThreadContext(int tid)
    : ThreadContextBase(tid),
      stack_begin_(0),
      stack_end_(0),
      cache_begin_(0),
      cache_end_(0),
      tls_begin_(0),
      tls_end_(0),
      dtls_(nullptr) {}

struct OnStartedArgs {
  uptr stack_begin, stack_end,
       cache_begin, cache_end,
       tls_begin, tls_end;
  DTLS *dtls;
};

void ThreadContext::OnStarted(void *arg) {
  OnStartedArgs *args = reinterpret_cast<OnStartedArgs *>(arg);
  stack_begin_ = args->stack_begin;
  stack_end_ = args->stack_end;
  tls_begin_ = args->tls_begin;
  tls_end_ = args->tls_end;
  cache_begin_ = args->cache_begin;
  cache_end_ = args->cache_end;
  dtls_ = args->dtls;
}

void ThreadContext::OnFinished() {
  AllocatorThreadFinish();
  DTLS_Destroy();
}

u32 ThreadCreate(u32 parent_tid, uptr user_id, bool detached) {
  return thread_registry->CreateThread(user_id, detached, parent_tid,
                                       /* arg */ nullptr);
}

void ThreadStart(u32 tid, tid_t os_id, bool workerthread) {
  OnStartedArgs args;
  uptr stack_size = 0;
  uptr tls_size = 0;
  GetThreadStackAndTls(tid == 0, &args.stack_begin, &stack_size,
                       &args.tls_begin, &tls_size);
  args.stack_end = args.stack_begin + stack_size;
  args.tls_end = args.tls_begin + tls_size;
  GetAllocatorCacheRange(&args.cache_begin, &args.cache_end);
  args.dtls = DTLS_Get();
  thread_registry->StartThread(tid, os_id, workerthread, &args);
}

void ThreadFinish() {
  thread_registry->FinishThread(GetCurrentThread());
  SetCurrentThread(kInvalidTid);
}

ThreadContext *CurrentThreadContext() {
  if (!thread_registry) return nullptr;
  if (GetCurrentThread() == kInvalidTid)
    return nullptr;
  // No lock needed when getting current thread.
  return (ThreadContext *)thread_registry->GetThreadLocked(GetCurrentThread());
}

static bool FindThreadByUid(ThreadContextBase *tctx, void *arg) {
  uptr uid = (uptr)arg;
  if (tctx->user_id == uid && tctx->status != ThreadStatusInvalid) {
    return true;
  }
  return false;
}

u32 ThreadTid(uptr uid) {
  return thread_registry->FindThread(FindThreadByUid, (void*)uid);
}

void ThreadJoin(u32 tid) {
  CHECK_NE(tid, kInvalidTid);
  thread_registry->JoinThread(tid, /* arg */nullptr);
}

void EnsureMainThreadIDIsCorrect() {
  if (GetCurrentThread() == 0)
    CurrentThreadContext()->os_id = GetTid();
}

///// Interface to the common LSan module. /////

bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  ThreadContext *context = static_cast<ThreadContext *>(
      thread_registry->FindThreadContextByOsIDLocked(os_id));
  if (!context) return false;
  *stack_begin = context->stack_begin();
  *stack_end = context->stack_end();
  *tls_begin = context->tls_begin();
  *tls_end = context->tls_end();
  *cache_begin = context->cache_begin();
  *cache_end = context->cache_end();
  *dtls = context->dtls();
  return true;
}

void ForEachExtraStackRange(tid_t os_id, RangeIteratorCallback callback,
                            void *arg) {
}

void LockThreadRegistry() {
  thread_registry->Lock();
}

void UnlockThreadRegistry() {
  thread_registry->Unlock();
}

ThreadRegistry *GetThreadRegistryLocked() {
  thread_registry->CheckLocked();
  return thread_registry;
}

} // namespace __lsan
