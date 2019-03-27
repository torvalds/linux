//===-- asan_rtems.cc -----------------------------------------------------===//
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
// RTEMS-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_rtems.h"
#if SANITIZER_RTEMS

#include "asan_internal.h"
#include "asan_interceptors.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"

#include <pthread.h>
#include <stdlib.h>

namespace __asan {

static void ResetShadowMemory() {
  uptr shadow_start = SHADOW_OFFSET;
  uptr shadow_end = MEM_TO_SHADOW(kMyriadMemoryEnd32);
  uptr gap_start = MEM_TO_SHADOW(shadow_start);
  uptr gap_end = MEM_TO_SHADOW(shadow_end);

  REAL(memset)((void *)shadow_start, 0, shadow_end - shadow_start);
  REAL(memset)((void *)gap_start, kAsanShadowGap, gap_end - gap_start);
}

void InitializeShadowMemory() {
  kHighMemEnd = 0;
  kMidMemBeg =  0;
  kMidMemEnd =  0;

  ResetShadowMemory();
}

void AsanApplyToGlobals(globals_op_fptr op, const void *needle) {
  UNIMPLEMENTED();
}

void AsanCheckDynamicRTPrereqs() {}
void AsanCheckIncompatibleRT() {}
void InitializeAsanInterceptors() {}
void InitializePlatformInterceptors() {}
void InitializePlatformExceptionHandlers() {}

// RTEMS only support static linking; it sufficies to return with no
// error.
void *AsanDoesNotSupportStaticLinkage() { return nullptr; }

void AsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  UNIMPLEMENTED();
}

void EarlyInit() {
  // Provide early initialization of shadow memory so that
  // instrumented code running before full initialzation will not
  // report spurious errors.
  ResetShadowMemory();
}

// We can use a plain thread_local variable for TSD.
static thread_local void *per_thread;

void *AsanTSDGet() { return per_thread; }

void AsanTSDSet(void *tsd) { per_thread = tsd; }

// There's no initialization needed, and the passed-in destructor
// will never be called.  Instead, our own thread destruction hook
// (below) will call AsanThread::TSDDtor directly.
void AsanTSDInit(void (*destructor)(void *tsd)) {
  DCHECK(destructor == &PlatformTSDDtor);
}

void PlatformTSDDtor(void *tsd) { UNREACHABLE(__func__); }

//
// Thread registration.  We provide an API similar to the Fushia port.
//

struct AsanThread::InitOptions {
  uptr stack_bottom, stack_size, tls_bottom, tls_size;
};

// Shared setup between thread creation and startup for the initial thread.
static AsanThread *CreateAsanThread(StackTrace *stack, u32 parent_tid,
                                    uptr user_id, bool detached,
                                    uptr stack_bottom, uptr stack_size,
                                    uptr tls_bottom, uptr tls_size) {
  // In lieu of AsanThread::Create.
  AsanThread *thread = (AsanThread *)MmapOrDie(sizeof(AsanThread), __func__);
  AsanThreadContext::CreateThreadContextArgs args = {thread, stack};
  asanThreadRegistry().CreateThread(user_id, detached, parent_tid, &args);

  // On other systems, AsanThread::Init() is called from the new
  // thread itself.  But on RTEMS we already know the stack address
  // range beforehand, so we can do most of the setup right now.
  const AsanThread::InitOptions options = {stack_bottom, stack_size,
                                           tls_bottom, tls_size};
  thread->Init(&options);
  return thread;
}

// This gets the same arguments passed to Init by CreateAsanThread, above.
// We're in the creator thread before the new thread is actually started, but
// its stack and tls address range are already known.
void AsanThread::SetThreadStackAndTls(const AsanThread::InitOptions *options) {
  DCHECK_NE(GetCurrentThread(), this);
  DCHECK_NE(GetCurrentThread(), nullptr);
  CHECK_NE(options->stack_bottom, 0);
  CHECK_NE(options->stack_size, 0);
  stack_bottom_ = options->stack_bottom;
  stack_top_ = options->stack_bottom + options->stack_size;
  tls_begin_ = options->tls_bottom;
  tls_end_ = options->tls_bottom + options->tls_size;
}

// Called by __asan::AsanInitInternal (asan_rtl.c).  Unlike other ports, the
// main thread on RTEMS does not require special treatment; its AsanThread is
// already created by the provided hooks.  This function simply looks up and
// returns the created thread.
AsanThread *CreateMainThread() {
  return GetThreadContextByTidLocked(0)->thread;
}

// This is called before each thread creation is attempted.  So, in
// its first call, the calling thread is the initial and sole thread.
static void *BeforeThreadCreateHook(uptr user_id, bool detached,
                                    uptr stack_bottom, uptr stack_size,
                                    uptr tls_bottom, uptr tls_size) {
  EnsureMainThreadIDIsCorrect();
  // Strict init-order checking is thread-hostile.
  if (flags()->strict_init_order) StopInitOrderChecking();

  GET_STACK_TRACE_THREAD;
  u32 parent_tid = GetCurrentTidOrInvalid();

  return CreateAsanThread(&stack, parent_tid, user_id, detached,
                          stack_bottom, stack_size, tls_bottom, tls_size);
}

// This is called after creating a new thread (in the creating thread),
// with the pointer returned by BeforeThreadCreateHook (above).
static void ThreadCreateHook(void *hook, bool aborted) {
  AsanThread *thread = static_cast<AsanThread *>(hook);
  if (!aborted) {
    // The thread was created successfully.
    // ThreadStartHook is already running in the new thread.
  } else {
    // The thread wasn't created after all.
    // Clean up everything we set up in BeforeThreadCreateHook.
    asanThreadRegistry().FinishThread(thread->tid());
    UnmapOrDie(thread, sizeof(AsanThread));
  }
}

// This is called (1) in the newly-created thread before it runs anything else,
// with the pointer returned by BeforeThreadCreateHook (above).  (2) before a
// thread restart.
static void ThreadStartHook(void *hook, uptr os_id) {
  if (!hook)
    return;

  AsanThread *thread = static_cast<AsanThread *>(hook);
  SetCurrentThread(thread);

  ThreadStatus status =
      asanThreadRegistry().GetThreadLocked(thread->tid())->status;
  DCHECK(status == ThreadStatusCreated || status == ThreadStatusRunning);
  // Determine whether we are starting or restarting the thread.
  if (status == ThreadStatusCreated)
    // In lieu of AsanThread::ThreadStart.
    asanThreadRegistry().StartThread(thread->tid(), os_id,
                                     /*workerthread*/ false, nullptr);
  else {
    // In a thread restart, a thread may resume execution at an
    // arbitrary function entry point, with its stack and TLS state
    // reset.  We unpoison the stack in that case.
    PoisonShadow(thread->stack_bottom(), thread->stack_size(), 0);
  }
}

// Each thread runs this just before it exits,
// with the pointer returned by BeforeThreadCreateHook (above).
// All per-thread destructors have already been called.
static void ThreadExitHook(void *hook, uptr os_id) {
  AsanThread *thread = static_cast<AsanThread *>(hook);
  if (thread)
    AsanThread::TSDDtor(thread->context());
}

static void HandleExit() {
  // Disable ASan by setting it to uninitialized.  Also reset the
  // shadow memory to avoid reporting errors after the run-time has
  // been desroyed.
  if (asan_inited) {
    asan_inited = false;
    ResetShadowMemory();
  }
}

bool HandleDlopenInit() {
  // Not supported on this platform.
  static_assert(!SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be false");
  return false;
}
}  // namespace __asan

// These are declared (in extern "C") by <some_path/sanitizer.h>.
// The system runtime will call our definitions directly.

extern "C" {
void __sanitizer_early_init() {
  __asan::EarlyInit();
}

void *__sanitizer_before_thread_create_hook(uptr thread, bool detached,
                                            const char *name,
                                            void *stack_base, size_t stack_size,
                                            void *tls_base, size_t tls_size) {
  return __asan::BeforeThreadCreateHook(
      thread, detached,
      reinterpret_cast<uptr>(stack_base), stack_size,
      reinterpret_cast<uptr>(tls_base), tls_size);
}

void __sanitizer_thread_create_hook(void *handle, uptr thread, int status) {
  __asan::ThreadCreateHook(handle, status != 0);
}

void __sanitizer_thread_start_hook(void *handle, uptr self) {
  __asan::ThreadStartHook(handle, self);
}

void __sanitizer_thread_exit_hook(void *handle, uptr self) {
  __asan::ThreadExitHook(handle, self);
}

void __sanitizer_exit() {
  __asan::HandleExit();
}
}  // "C"

#endif  // SANITIZER_RTEMS
