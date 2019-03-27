//===-- sanitizer_thread_registry.cc --------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizer tools.
//
// General thread bookkeeping functionality.
//===----------------------------------------------------------------------===//

#include "sanitizer_thread_registry.h"

namespace __sanitizer {

ThreadContextBase::ThreadContextBase(u32 tid)
    : tid(tid), unique_id(0), reuse_count(), os_id(0), user_id(0),
      status(ThreadStatusInvalid),
      detached(false), workerthread(false), parent_tid(0), next(0) {
  name[0] = '\0';
  atomic_store(&thread_destroyed, 0, memory_order_release);
}

ThreadContextBase::~ThreadContextBase() {
  // ThreadContextBase should never be deleted.
  CHECK(0);
}

void ThreadContextBase::SetName(const char *new_name) {
  name[0] = '\0';
  if (new_name) {
    internal_strncpy(name, new_name, sizeof(name));
    name[sizeof(name) - 1] = '\0';
  }
}

void ThreadContextBase::SetDead() {
  CHECK(status == ThreadStatusRunning ||
        status == ThreadStatusFinished);
  status = ThreadStatusDead;
  user_id = 0;
  OnDead();
}

void ThreadContextBase::SetDestroyed() {
  atomic_store(&thread_destroyed, 1, memory_order_release);
}

bool ThreadContextBase::GetDestroyed() {
  return !!atomic_load(&thread_destroyed, memory_order_acquire);
}

void ThreadContextBase::SetJoined(void *arg) {
  // FIXME(dvyukov): print message and continue (it's user error).
  CHECK_EQ(false, detached);
  CHECK_EQ(ThreadStatusFinished, status);
  status = ThreadStatusDead;
  user_id = 0;
  OnJoined(arg);
}

void ThreadContextBase::SetFinished() {
  // ThreadRegistry::FinishThread calls here in ThreadStatusCreated state
  // for a thread that never actually started.  In that case the thread
  // should go to ThreadStatusFinished regardless of whether it was created
  // as detached.
  if (!detached || status == ThreadStatusCreated) status = ThreadStatusFinished;
  OnFinished();
}

void ThreadContextBase::SetStarted(tid_t _os_id, bool _workerthread,
                                   void *arg) {
  status = ThreadStatusRunning;
  os_id = _os_id;
  workerthread = _workerthread;
  OnStarted(arg);
}

void ThreadContextBase::SetCreated(uptr _user_id, u64 _unique_id,
                                   bool _detached, u32 _parent_tid, void *arg) {
  status = ThreadStatusCreated;
  user_id = _user_id;
  unique_id = _unique_id;
  detached = _detached;
  // Parent tid makes no sense for the main thread.
  if (tid != 0)
    parent_tid = _parent_tid;
  OnCreated(arg);
}

void ThreadContextBase::Reset() {
  status = ThreadStatusInvalid;
  SetName(0);
  atomic_store(&thread_destroyed, 0, memory_order_release);
  OnReset();
}

// ThreadRegistry implementation.

const u32 ThreadRegistry::kUnknownTid = ~0U;

ThreadRegistry::ThreadRegistry(ThreadContextFactory factory, u32 max_threads,
                               u32 thread_quarantine_size, u32 max_reuse)
    : context_factory_(factory),
      max_threads_(max_threads),
      thread_quarantine_size_(thread_quarantine_size),
      max_reuse_(max_reuse),
      mtx_(),
      n_contexts_(0),
      total_threads_(0),
      alive_threads_(0),
      max_alive_threads_(0),
      running_threads_(0) {
  threads_ = (ThreadContextBase **)MmapOrDie(max_threads_ * sizeof(threads_[0]),
                                             "ThreadRegistry");
  dead_threads_.clear();
  invalid_threads_.clear();
}

void ThreadRegistry::GetNumberOfThreads(uptr *total, uptr *running,
                                        uptr *alive) {
  BlockingMutexLock l(&mtx_);
  if (total) *total = n_contexts_;
  if (running) *running = running_threads_;
  if (alive) *alive = alive_threads_;
}

uptr ThreadRegistry::GetMaxAliveThreads() {
  BlockingMutexLock l(&mtx_);
  return max_alive_threads_;
}

u32 ThreadRegistry::CreateThread(uptr user_id, bool detached, u32 parent_tid,
                                 void *arg) {
  BlockingMutexLock l(&mtx_);
  u32 tid = kUnknownTid;
  ThreadContextBase *tctx = QuarantinePop();
  if (tctx) {
    tid = tctx->tid;
  } else if (n_contexts_ < max_threads_) {
    // Allocate new thread context and tid.
    tid = n_contexts_++;
    tctx = context_factory_(tid);
    threads_[tid] = tctx;
  } else {
#if !SANITIZER_GO
    Report("%s: Thread limit (%u threads) exceeded. Dying.\n",
           SanitizerToolName, max_threads_);
#else
    Printf("race: limit on %u simultaneously alive goroutines is exceeded,"
        " dying\n", max_threads_);
#endif
    Die();
  }
  CHECK_NE(tctx, 0);
  CHECK_NE(tid, kUnknownTid);
  CHECK_LT(tid, max_threads_);
  CHECK_EQ(tctx->status, ThreadStatusInvalid);
  alive_threads_++;
  if (max_alive_threads_ < alive_threads_) {
    max_alive_threads_++;
    CHECK_EQ(alive_threads_, max_alive_threads_);
  }
  tctx->SetCreated(user_id, total_threads_++, detached,
                   parent_tid, arg);
  return tid;
}

void ThreadRegistry::RunCallbackForEachThreadLocked(ThreadCallback cb,
                                                    void *arg) {
  CheckLocked();
  for (u32 tid = 0; tid < n_contexts_; tid++) {
    ThreadContextBase *tctx = threads_[tid];
    if (tctx == 0)
      continue;
    cb(tctx, arg);
  }
}

u32 ThreadRegistry::FindThread(FindThreadCallback cb, void *arg) {
  BlockingMutexLock l(&mtx_);
  for (u32 tid = 0; tid < n_contexts_; tid++) {
    ThreadContextBase *tctx = threads_[tid];
    if (tctx != 0 && cb(tctx, arg))
      return tctx->tid;
  }
  return kUnknownTid;
}

ThreadContextBase *
ThreadRegistry::FindThreadContextLocked(FindThreadCallback cb, void *arg) {
  CheckLocked();
  for (u32 tid = 0; tid < n_contexts_; tid++) {
    ThreadContextBase *tctx = threads_[tid];
    if (tctx != 0 && cb(tctx, arg))
      return tctx;
  }
  return 0;
}

static bool FindThreadContextByOsIdCallback(ThreadContextBase *tctx,
                                            void *arg) {
  return (tctx->os_id == (uptr)arg && tctx->status != ThreadStatusInvalid &&
      tctx->status != ThreadStatusDead);
}

ThreadContextBase *ThreadRegistry::FindThreadContextByOsIDLocked(tid_t os_id) {
  return FindThreadContextLocked(FindThreadContextByOsIdCallback,
                                 (void *)os_id);
}

void ThreadRegistry::SetThreadName(u32 tid, const char *name) {
  BlockingMutexLock l(&mtx_);
  CHECK_LT(tid, n_contexts_);
  ThreadContextBase *tctx = threads_[tid];
  CHECK_NE(tctx, 0);
  CHECK_EQ(SANITIZER_FUCHSIA ? ThreadStatusCreated : ThreadStatusRunning,
           tctx->status);
  tctx->SetName(name);
}

void ThreadRegistry::SetThreadNameByUserId(uptr user_id, const char *name) {
  BlockingMutexLock l(&mtx_);
  for (u32 tid = 0; tid < n_contexts_; tid++) {
    ThreadContextBase *tctx = threads_[tid];
    if (tctx != 0 && tctx->user_id == user_id &&
        tctx->status != ThreadStatusInvalid) {
      tctx->SetName(name);
      return;
    }
  }
}

void ThreadRegistry::DetachThread(u32 tid, void *arg) {
  BlockingMutexLock l(&mtx_);
  CHECK_LT(tid, n_contexts_);
  ThreadContextBase *tctx = threads_[tid];
  CHECK_NE(tctx, 0);
  if (tctx->status == ThreadStatusInvalid) {
    Report("%s: Detach of non-existent thread\n", SanitizerToolName);
    return;
  }
  tctx->OnDetached(arg);
  if (tctx->status == ThreadStatusFinished) {
    tctx->SetDead();
    QuarantinePush(tctx);
  } else {
    tctx->detached = true;
  }
}

void ThreadRegistry::JoinThread(u32 tid, void *arg) {
  bool destroyed = false;
  do {
    {
      BlockingMutexLock l(&mtx_);
      CHECK_LT(tid, n_contexts_);
      ThreadContextBase *tctx = threads_[tid];
      CHECK_NE(tctx, 0);
      if (tctx->status == ThreadStatusInvalid) {
        Report("%s: Join of non-existent thread\n", SanitizerToolName);
        return;
      }
      if ((destroyed = tctx->GetDestroyed())) {
        tctx->SetJoined(arg);
        QuarantinePush(tctx);
      }
    }
    if (!destroyed)
      internal_sched_yield();
  } while (!destroyed);
}

// Normally this is called when the thread is about to exit.  If
// called in ThreadStatusCreated state, then this thread was never
// really started.  We just did CreateThread for a prospective new
// thread before trying to create it, and then failed to actually
// create it, and so never called StartThread.
void ThreadRegistry::FinishThread(u32 tid) {
  BlockingMutexLock l(&mtx_);
  CHECK_GT(alive_threads_, 0);
  alive_threads_--;
  CHECK_LT(tid, n_contexts_);
  ThreadContextBase *tctx = threads_[tid];
  CHECK_NE(tctx, 0);
  bool dead = tctx->detached;
  if (tctx->status == ThreadStatusRunning) {
    CHECK_GT(running_threads_, 0);
    running_threads_--;
  } else {
    // The thread never really existed.
    CHECK_EQ(tctx->status, ThreadStatusCreated);
    dead = true;
  }
  tctx->SetFinished();
  if (dead) {
    tctx->SetDead();
    QuarantinePush(tctx);
  }
  tctx->SetDestroyed();
}

void ThreadRegistry::StartThread(u32 tid, tid_t os_id, bool workerthread,
                                 void *arg) {
  BlockingMutexLock l(&mtx_);
  running_threads_++;
  CHECK_LT(tid, n_contexts_);
  ThreadContextBase *tctx = threads_[tid];
  CHECK_NE(tctx, 0);
  CHECK_EQ(ThreadStatusCreated, tctx->status);
  tctx->SetStarted(os_id, workerthread, arg);
}

void ThreadRegistry::QuarantinePush(ThreadContextBase *tctx) {
  if (tctx->tid == 0)
    return;  // Don't reuse the main thread.  It's a special snowflake.
  dead_threads_.push_back(tctx);
  if (dead_threads_.size() <= thread_quarantine_size_)
    return;
  tctx = dead_threads_.front();
  dead_threads_.pop_front();
  CHECK_EQ(tctx->status, ThreadStatusDead);
  tctx->Reset();
  tctx->reuse_count++;
  if (max_reuse_ > 0 && tctx->reuse_count >= max_reuse_)
    return;
  invalid_threads_.push_back(tctx);
}

ThreadContextBase *ThreadRegistry::QuarantinePop() {
  if (invalid_threads_.size() == 0)
    return 0;
  ThreadContextBase *tctx = invalid_threads_.front();
  invalid_threads_.pop_front();
  return tctx;
}

void ThreadRegistry::SetThreadUserId(u32 tid, uptr user_id) {
  BlockingMutexLock l(&mtx_);
  CHECK_LT(tid, n_contexts_);
  ThreadContextBase *tctx = threads_[tid];
  CHECK_NE(tctx, 0);
  CHECK_NE(tctx->status, ThreadStatusInvalid);
  CHECK_NE(tctx->status, ThreadStatusDead);
  CHECK_EQ(tctx->user_id, 0);
  tctx->user_id = user_id;
}

}  // namespace __sanitizer
