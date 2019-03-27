//===-- asan_thread.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_thread.cc.
//===----------------------------------------------------------------------===//

#ifndef ASAN_THREAD_H
#define ASAN_THREAD_H

#include "asan_allocator.h"
#include "asan_internal.h"
#include "asan_fake_stack.h"
#include "asan_stats.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_thread_registry.h"

namespace __sanitizer {
struct DTLS;
}  // namespace __sanitizer

namespace __asan {

const u32 kInvalidTid = 0xffffff;  // Must fit into 24 bits.
const u32 kMaxNumberOfThreads = (1 << 22);  // 4M

class AsanThread;

// These objects are created for every thread and are never deleted,
// so we can find them by tid even if the thread is long dead.
class AsanThreadContext : public ThreadContextBase {
 public:
  explicit AsanThreadContext(int tid)
      : ThreadContextBase(tid), announced(false),
        destructor_iterations(GetPthreadDestructorIterations()), stack_id(0),
        thread(nullptr) {}
  bool announced;
  u8 destructor_iterations;
  u32 stack_id;
  AsanThread *thread;

  void OnCreated(void *arg) override;
  void OnFinished() override;

  struct CreateThreadContextArgs {
    AsanThread *thread;
    StackTrace *stack;
  };
};

// AsanThreadContext objects are never freed, so we need many of them.
COMPILER_CHECK(sizeof(AsanThreadContext) <= 256);

// AsanThread are stored in TSD and destroyed when the thread dies.
class AsanThread {
 public:
  static AsanThread *Create(thread_callback_t start_routine, void *arg,
                            u32 parent_tid, StackTrace *stack, bool detached);
  static void TSDDtor(void *tsd);
  void Destroy();

  struct InitOptions;
  void Init(const InitOptions *options = nullptr);

  thread_return_t ThreadStart(tid_t os_id,
                              atomic_uintptr_t *signal_thread_is_registered);

  uptr stack_top();
  uptr stack_bottom();
  uptr stack_size();
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  DTLS *dtls() { return dtls_; }
  u32 tid() { return context_->tid; }
  AsanThreadContext *context() { return context_; }
  void set_context(AsanThreadContext *context) { context_ = context; }

  struct StackFrameAccess {
    uptr offset;
    uptr frame_pc;
    const char *frame_descr;
  };
  bool GetStackFrameAccessByAddr(uptr addr, StackFrameAccess *access);

  // Returns a pointer to the start of the stack variable's shadow memory.
  uptr GetStackVariableShadowStart(uptr addr);

  bool AddrIsInStack(uptr addr);

  void DeleteFakeStack(int tid) {
    if (!fake_stack_) return;
    FakeStack *t = fake_stack_;
    fake_stack_ = nullptr;
    SetTLSFakeStack(nullptr);
    t->Destroy(tid);
  }

  void StartSwitchFiber(FakeStack **fake_stack_save, uptr bottom, uptr size);
  void FinishSwitchFiber(FakeStack *fake_stack_save, uptr *bottom_old,
                         uptr *size_old);

  bool has_fake_stack() {
    return !atomic_load(&stack_switching_, memory_order_relaxed) &&
           (reinterpret_cast<uptr>(fake_stack_) > 1);
  }

  FakeStack *fake_stack() {
    if (!__asan_option_detect_stack_use_after_return)
      return nullptr;
    if (atomic_load(&stack_switching_, memory_order_relaxed))
      return nullptr;
    if (!has_fake_stack())
      return AsyncSignalSafeLazyInitFakeStack();
    return fake_stack_;
  }

  // True is this thread is currently unwinding stack (i.e. collecting a stack
  // trace). Used to prevent deadlocks on platforms where libc unwinder calls
  // malloc internally. See PR17116 for more details.
  bool isUnwinding() const { return unwinding_; }
  void setUnwinding(bool b) { unwinding_ = b; }

  AsanThreadLocalMallocStorage &malloc_storage() { return malloc_storage_; }
  AsanStats &stats() { return stats_; }

 private:
  // NOTE: There is no AsanThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.

  void SetThreadStackAndTls(const InitOptions *options);

  void ClearShadowForThreadStackAndTLS();
  FakeStack *AsyncSignalSafeLazyInitFakeStack();

  struct StackBounds {
    uptr bottom;
    uptr top;
  };
  StackBounds GetStackBounds() const;

  AsanThreadContext *context_;
  thread_callback_t start_routine_;
  void *arg_;

  uptr stack_top_;
  uptr stack_bottom_;
  // these variables are used when the thread is about to switch stack
  uptr next_stack_top_;
  uptr next_stack_bottom_;
  // true if switching is in progress
  atomic_uint8_t stack_switching_;

  uptr tls_begin_;
  uptr tls_end_;
  DTLS *dtls_;

  FakeStack *fake_stack_;
  AsanThreadLocalMallocStorage malloc_storage_;
  AsanStats stats_;
  bool unwinding_;
};

// ScopedUnwinding is a scope for stacktracing member of a context
class ScopedUnwinding {
 public:
  explicit ScopedUnwinding(AsanThread *t) : thread(t) {
    t->setUnwinding(true);
  }
  ~ScopedUnwinding() { thread->setUnwinding(false); }

 private:
  AsanThread *thread;
};

// Returns a single instance of registry.
ThreadRegistry &asanThreadRegistry();

// Must be called under ThreadRegistryLock.
AsanThreadContext *GetThreadContextByTidLocked(u32 tid);

// Get the current thread. May return 0.
AsanThread *GetCurrentThread();
void SetCurrentThread(AsanThread *t);
u32 GetCurrentTidOrInvalid();
AsanThread *FindThreadByStackAddress(uptr addr);

// Used to handle fork().
void EnsureMainThreadIDIsCorrect();
} // namespace __asan

#endif // ASAN_THREAD_H
