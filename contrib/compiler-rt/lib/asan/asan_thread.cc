//===-- asan_thread.cc ----------------------------------------------------===//
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
// Thread-related code.
//===----------------------------------------------------------------------===//
#include "asan_allocator.h"
#include "asan_interceptors.h"
#include "asan_poisoning.h"
#include "asan_stack.h"
#include "asan_thread.h"
#include "asan_mapping.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "lsan/lsan_common.h"

namespace __asan {

// AsanThreadContext implementation.

void AsanThreadContext::OnCreated(void *arg) {
  CreateThreadContextArgs *args = static_cast<CreateThreadContextArgs*>(arg);
  if (args->stack)
    stack_id = StackDepotPut(*args->stack);
  thread = args->thread;
  thread->set_context(this);
}

void AsanThreadContext::OnFinished() {
  // Drop the link to the AsanThread object.
  thread = nullptr;
}

// MIPS requires aligned address
static ALIGNED(16) char thread_registry_placeholder[sizeof(ThreadRegistry)];
static ThreadRegistry *asan_thread_registry;

static BlockingMutex mu_for_thread_context(LINKER_INITIALIZED);
static LowLevelAllocator allocator_for_thread_context;

static ThreadContextBase *GetAsanThreadContext(u32 tid) {
  BlockingMutexLock lock(&mu_for_thread_context);
  return new(allocator_for_thread_context) AsanThreadContext(tid);
}

ThreadRegistry &asanThreadRegistry() {
  static bool initialized;
  // Don't worry about thread_safety - this should be called when there is
  // a single thread.
  if (!initialized) {
    // Never reuse ASan threads: we store pointer to AsanThreadContext
    // in TSD and can't reliably tell when no more TSD destructors will
    // be called. It would be wrong to reuse AsanThreadContext for another
    // thread before all TSD destructors will be called for it.
    asan_thread_registry = new(thread_registry_placeholder) ThreadRegistry(
        GetAsanThreadContext, kMaxNumberOfThreads, kMaxNumberOfThreads);
    initialized = true;
  }
  return *asan_thread_registry;
}

AsanThreadContext *GetThreadContextByTidLocked(u32 tid) {
  return static_cast<AsanThreadContext *>(
      asanThreadRegistry().GetThreadLocked(tid));
}

// AsanThread implementation.

AsanThread *AsanThread::Create(thread_callback_t start_routine, void *arg,
                               u32 parent_tid, StackTrace *stack,
                               bool detached) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(AsanThread), PageSize);
  AsanThread *thread = (AsanThread*)MmapOrDie(size, __func__);
  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  AsanThreadContext::CreateThreadContextArgs args = {thread, stack};
  asanThreadRegistry().CreateThread(*reinterpret_cast<uptr *>(thread), detached,
                                    parent_tid, &args);

  return thread;
}

void AsanThread::TSDDtor(void *tsd) {
  AsanThreadContext *context = (AsanThreadContext*)tsd;
  VReport(1, "T%d TSDDtor\n", context->tid);
  if (context->thread)
    context->thread->Destroy();
}

void AsanThread::Destroy() {
  int tid = this->tid();
  VReport(1, "T%d exited\n", tid);

  malloc_storage().CommitBack();
  if (common_flags()->use_sigaltstack) UnsetAlternateSignalStack();
  asanThreadRegistry().FinishThread(tid);
  FlushToDeadThreadStats(&stats_);
  // We also clear the shadow on thread destruction because
  // some code may still be executing in later TSD destructors
  // and we don't want it to have any poisoned stack.
  ClearShadowForThreadStackAndTLS();
  DeleteFakeStack(tid);
  uptr size = RoundUpTo(sizeof(AsanThread), GetPageSizeCached());
  UnmapOrDie(this, size);
  DTLS_Destroy();
}

void AsanThread::StartSwitchFiber(FakeStack **fake_stack_save, uptr bottom,
                                  uptr size) {
  if (atomic_load(&stack_switching_, memory_order_relaxed)) {
    Report("ERROR: starting fiber switch while in fiber switch\n");
    Die();
  }

  next_stack_bottom_ = bottom;
  next_stack_top_ = bottom + size;
  atomic_store(&stack_switching_, 1, memory_order_release);

  FakeStack *current_fake_stack = fake_stack_;
  if (fake_stack_save)
    *fake_stack_save = fake_stack_;
  fake_stack_ = nullptr;
  SetTLSFakeStack(nullptr);
  // if fake_stack_save is null, the fiber will die, delete the fakestack
  if (!fake_stack_save && current_fake_stack)
    current_fake_stack->Destroy(this->tid());
}

void AsanThread::FinishSwitchFiber(FakeStack *fake_stack_save,
                                   uptr *bottom_old,
                                   uptr *size_old) {
  if (!atomic_load(&stack_switching_, memory_order_relaxed)) {
    Report("ERROR: finishing a fiber switch that has not started\n");
    Die();
  }

  if (fake_stack_save) {
    SetTLSFakeStack(fake_stack_save);
    fake_stack_ = fake_stack_save;
  }

  if (bottom_old)
    *bottom_old = stack_bottom_;
  if (size_old)
    *size_old = stack_top_ - stack_bottom_;
  stack_bottom_ = next_stack_bottom_;
  stack_top_ = next_stack_top_;
  atomic_store(&stack_switching_, 0, memory_order_release);
  next_stack_top_ = 0;
  next_stack_bottom_ = 0;
}

inline AsanThread::StackBounds AsanThread::GetStackBounds() const {
  if (!atomic_load(&stack_switching_, memory_order_acquire)) {
    // Make sure the stack bounds are fully initialized.
    if (stack_bottom_ >= stack_top_) return {0, 0};
    return {stack_bottom_, stack_top_};
  }
  char local;
  const uptr cur_stack = (uptr)&local;
  // Note: need to check next stack first, because FinishSwitchFiber
  // may be in process of overwriting stack_top_/bottom_. But in such case
  // we are already on the next stack.
  if (cur_stack >= next_stack_bottom_ && cur_stack < next_stack_top_)
    return {next_stack_bottom_, next_stack_top_};
  return {stack_bottom_, stack_top_};
}

uptr AsanThread::stack_top() {
  return GetStackBounds().top;
}

uptr AsanThread::stack_bottom() {
  return GetStackBounds().bottom;
}

uptr AsanThread::stack_size() {
  const auto bounds = GetStackBounds();
  return bounds.top - bounds.bottom;
}

// We want to create the FakeStack lazyly on the first use, but not eralier
// than the stack size is known and the procedure has to be async-signal safe.
FakeStack *AsanThread::AsyncSignalSafeLazyInitFakeStack() {
  uptr stack_size = this->stack_size();
  if (stack_size == 0)  // stack_size is not yet available, don't use FakeStack.
    return nullptr;
  uptr old_val = 0;
  // fake_stack_ has 3 states:
  // 0   -- not initialized
  // 1   -- being initialized
  // ptr -- initialized
  // This CAS checks if the state was 0 and if so changes it to state 1,
  // if that was successful, it initializes the pointer.
  if (atomic_compare_exchange_strong(
      reinterpret_cast<atomic_uintptr_t *>(&fake_stack_), &old_val, 1UL,
      memory_order_relaxed)) {
    uptr stack_size_log = Log2(RoundUpToPowerOfTwo(stack_size));
    CHECK_LE(flags()->min_uar_stack_size_log, flags()->max_uar_stack_size_log);
    stack_size_log =
        Min(stack_size_log, static_cast<uptr>(flags()->max_uar_stack_size_log));
    stack_size_log =
        Max(stack_size_log, static_cast<uptr>(flags()->min_uar_stack_size_log));
    fake_stack_ = FakeStack::Create(stack_size_log);
    SetTLSFakeStack(fake_stack_);
    return fake_stack_;
  }
  return nullptr;
}

void AsanThread::Init(const InitOptions *options) {
  next_stack_top_ = next_stack_bottom_ = 0;
  atomic_store(&stack_switching_, false, memory_order_release);
  CHECK_EQ(this->stack_size(), 0U);
  SetThreadStackAndTls(options);
  if (stack_top_ != stack_bottom_) {
    CHECK_GT(this->stack_size(), 0U);
    CHECK(AddrIsInMem(stack_bottom_));
    CHECK(AddrIsInMem(stack_top_ - 1));
  }
  ClearShadowForThreadStackAndTLS();
  fake_stack_ = nullptr;
  if (__asan_option_detect_stack_use_after_return)
    AsyncSignalSafeLazyInitFakeStack();
  int local = 0;
  VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
          (void *)stack_bottom_, (void *)stack_top_, stack_top_ - stack_bottom_,
          &local);
}

// Fuchsia and RTEMS don't use ThreadStart.
// asan_fuchsia.c/asan_rtems.c define CreateMainThread and
// SetThreadStackAndTls.
#if !SANITIZER_FUCHSIA && !SANITIZER_RTEMS

thread_return_t AsanThread::ThreadStart(
    tid_t os_id, atomic_uintptr_t *signal_thread_is_registered) {
  Init();
  asanThreadRegistry().StartThread(tid(), os_id, /*workerthread*/ false,
                                   nullptr);
  if (signal_thread_is_registered)
    atomic_store(signal_thread_is_registered, 1, memory_order_release);

  if (common_flags()->use_sigaltstack) SetAlternateSignalStack();

  if (!start_routine_) {
    // start_routine_ == 0 if we're on the main thread or on one of the
    // OS X libdispatch worker threads. But nobody is supposed to call
    // ThreadStart() for the worker threads.
    CHECK_EQ(tid(), 0);
    return 0;
  }

  thread_return_t res = start_routine_(arg_);

  // On POSIX systems we defer this to the TSD destructor. LSan will consider
  // the thread's memory as non-live from the moment we call Destroy(), even
  // though that memory might contain pointers to heap objects which will be
  // cleaned up by a user-defined TSD destructor. Thus, calling Destroy() before
  // the TSD destructors have run might cause false positives in LSan.
  if (!SANITIZER_POSIX)
    this->Destroy();

  return res;
}

AsanThread *CreateMainThread() {
  AsanThread *main_thread = AsanThread::Create(
      /* start_routine */ nullptr, /* arg */ nullptr, /* parent_tid */ 0,
      /* stack */ nullptr, /* detached */ true);
  SetCurrentThread(main_thread);
  main_thread->ThreadStart(internal_getpid(),
                           /* signal_thread_is_registered */ nullptr);
  return main_thread;
}

// This implementation doesn't use the argument, which is just passed down
// from the caller of Init (which see, above).  It's only there to support
// OS-specific implementations that need more information passed through.
void AsanThread::SetThreadStackAndTls(const InitOptions *options) {
  DCHECK_EQ(options, nullptr);
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(tid() == 0, &stack_bottom_, &stack_size, &tls_begin_,
                       &tls_size);
  stack_top_ = stack_bottom_ + stack_size;
  tls_end_ = tls_begin_ + tls_size;
  dtls_ = DTLS_Get();

  if (stack_top_ != stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
  }
}

#endif  // !SANITIZER_FUCHSIA && !SANITIZER_RTEMS

void AsanThread::ClearShadowForThreadStackAndTLS() {
  if (stack_top_ != stack_bottom_)
    PoisonShadow(stack_bottom_, stack_top_ - stack_bottom_, 0);
  if (tls_begin_ != tls_end_) {
    uptr tls_begin_aligned = RoundDownTo(tls_begin_, SHADOW_GRANULARITY);
    uptr tls_end_aligned = RoundUpTo(tls_end_, SHADOW_GRANULARITY);
    FastPoisonShadowPartialRightRedzone(tls_begin_aligned,
                                        tls_end_ - tls_begin_aligned,
                                        tls_end_aligned - tls_end_, 0);
  }
}

bool AsanThread::GetStackFrameAccessByAddr(uptr addr,
                                           StackFrameAccess *access) {
  if (stack_top_ == stack_bottom_)
    return false;

  uptr bottom = 0;
  if (AddrIsInStack(addr)) {
    bottom = stack_bottom();
  } else if (has_fake_stack()) {
    bottom = fake_stack()->AddrIsInFakeStack(addr);
    CHECK(bottom);
    access->offset = addr - bottom;
    access->frame_pc = ((uptr*)bottom)[2];
    access->frame_descr = (const char *)((uptr*)bottom)[1];
    return true;
  }
  uptr aligned_addr = RoundDownTo(addr, SANITIZER_WORDSIZE / 8);  // align addr.
  uptr mem_ptr = RoundDownTo(aligned_addr, SHADOW_GRANULARITY);
  u8 *shadow_ptr = (u8*)MemToShadow(aligned_addr);
  u8 *shadow_bottom = (u8*)MemToShadow(bottom);

  while (shadow_ptr >= shadow_bottom &&
         *shadow_ptr != kAsanStackLeftRedzoneMagic) {
    shadow_ptr--;
    mem_ptr -= SHADOW_GRANULARITY;
  }

  while (shadow_ptr >= shadow_bottom &&
         *shadow_ptr == kAsanStackLeftRedzoneMagic) {
    shadow_ptr--;
    mem_ptr -= SHADOW_GRANULARITY;
  }

  if (shadow_ptr < shadow_bottom) {
    return false;
  }

  uptr* ptr = (uptr*)(mem_ptr + SHADOW_GRANULARITY);
  CHECK(ptr[0] == kCurrentStackFrameMagic);
  access->offset = addr - (uptr)ptr;
  access->frame_pc = ptr[2];
  access->frame_descr = (const char*)ptr[1];
  return true;
}

uptr AsanThread::GetStackVariableShadowStart(uptr addr) {
  uptr bottom = 0;
  if (AddrIsInStack(addr)) {
    bottom = stack_bottom();
  } else if (has_fake_stack()) {
    bottom = fake_stack()->AddrIsInFakeStack(addr);
    CHECK(bottom);
  } else
    return 0;

  uptr aligned_addr = RoundDownTo(addr, SANITIZER_WORDSIZE / 8);  // align addr.
  u8 *shadow_ptr = (u8*)MemToShadow(aligned_addr);
  u8 *shadow_bottom = (u8*)MemToShadow(bottom);

  while (shadow_ptr >= shadow_bottom &&
         (*shadow_ptr != kAsanStackLeftRedzoneMagic &&
          *shadow_ptr != kAsanStackMidRedzoneMagic &&
          *shadow_ptr != kAsanStackRightRedzoneMagic))
    shadow_ptr--;

  return (uptr)shadow_ptr + 1;
}

bool AsanThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

static bool ThreadStackContainsAddress(ThreadContextBase *tctx_base,
                                       void *addr) {
  AsanThreadContext *tctx = static_cast<AsanThreadContext*>(tctx_base);
  AsanThread *t = tctx->thread;
  if (!t) return false;
  if (t->AddrIsInStack((uptr)addr)) return true;
  if (t->has_fake_stack() && t->fake_stack()->AddrIsInFakeStack((uptr)addr))
    return true;
  return false;
}

AsanThread *GetCurrentThread() {
  if (SANITIZER_RTEMS && !asan_inited)
    return nullptr;

  AsanThreadContext *context =
      reinterpret_cast<AsanThreadContext *>(AsanTSDGet());
  if (!context) {
    if (SANITIZER_ANDROID) {
      // On Android, libc constructor is called _after_ asan_init, and cleans up
      // TSD. Try to figure out if this is still the main thread by the stack
      // address. We are not entirely sure that we have correct main thread
      // limits, so only do this magic on Android, and only if the found thread
      // is the main thread.
      AsanThreadContext *tctx = GetThreadContextByTidLocked(0);
      if (tctx && ThreadStackContainsAddress(tctx, &context)) {
        SetCurrentThread(tctx->thread);
        return tctx->thread;
      }
    }
    return nullptr;
  }
  return context->thread;
}

void SetCurrentThread(AsanThread *t) {
  CHECK(t->context());
  VReport(2, "SetCurrentThread: %p for thread %p\n", t->context(),
          (void *)GetThreadSelf());
  // Make sure we do not reset the current AsanThread.
  CHECK_EQ(0, AsanTSDGet());
  AsanTSDSet(t->context());
  CHECK_EQ(t->context(), AsanTSDGet());
}

u32 GetCurrentTidOrInvalid() {
  AsanThread *t = GetCurrentThread();
  return t ? t->tid() : kInvalidTid;
}

AsanThread *FindThreadByStackAddress(uptr addr) {
  asanThreadRegistry().CheckLocked();
  AsanThreadContext *tctx = static_cast<AsanThreadContext *>(
      asanThreadRegistry().FindThreadContextLocked(ThreadStackContainsAddress,
                                                   (void *)addr));
  return tctx ? tctx->thread : nullptr;
}

void EnsureMainThreadIDIsCorrect() {
  AsanThreadContext *context =
      reinterpret_cast<AsanThreadContext *>(AsanTSDGet());
  if (context && (context->tid == 0))
    context->os_id = GetTid();
}

__asan::AsanThread *GetAsanThreadByOsIDLocked(tid_t os_id) {
  __asan::AsanThreadContext *context = static_cast<__asan::AsanThreadContext *>(
      __asan::asanThreadRegistry().FindThreadContextByOsIDLocked(os_id));
  if (!context) return nullptr;
  return context->thread;
}
} // namespace __asan

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {
bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  __asan::AsanThread *t = __asan::GetAsanThreadByOsIDLocked(os_id);
  if (!t) return false;
  *stack_begin = t->stack_bottom();
  *stack_end = t->stack_top();
  *tls_begin = t->tls_begin();
  *tls_end = t->tls_end();
  // ASan doesn't keep allocator caches in TLS, so these are unused.
  *cache_begin = 0;
  *cache_end = 0;
  *dtls = t->dtls();
  return true;
}

void ForEachExtraStackRange(tid_t os_id, RangeIteratorCallback callback,
                            void *arg) {
  __asan::AsanThread *t = __asan::GetAsanThreadByOsIDLocked(os_id);
  if (t && t->has_fake_stack())
    t->fake_stack()->ForEachFakeFrame(callback, arg);
}

void LockThreadRegistry() {
  __asan::asanThreadRegistry().Lock();
}

void UnlockThreadRegistry() {
  __asan::asanThreadRegistry().Unlock();
}

ThreadRegistry *GetThreadRegistryLocked() {
  __asan::asanThreadRegistry().CheckLocked();
  return &__asan::asanThreadRegistry();
}

void EnsureMainThreadIDIsCorrect() {
  __asan::EnsureMainThreadIDIsCorrect();
}
} // namespace __lsan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;  // NOLINT

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_start_switch_fiber(void **fakestacksave, const void *bottom,
                                    uptr size) {
  AsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__asan_start_switch_fiber called from unknown thread\n");
    return;
  }
  t->StartSwitchFiber((FakeStack**)fakestacksave, (uptr)bottom, size);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_finish_switch_fiber(void* fakestack,
                                     const void **bottom_old,
                                     uptr *size_old) {
  AsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__asan_finish_switch_fiber called from unknown thread\n");
    return;
  }
  t->FinishSwitchFiber((FakeStack*)fakestack,
                       (uptr*)bottom_old,
                       (uptr*)size_old);
}
}
