//===-- tsan_platform_mac.cc ----------------------------------------------===//
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
// Mac-specific code.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_MAC

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_posix.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"
#include "tsan_flags.h"

#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>

namespace __tsan {

#if !SANITIZER_GO
static void *SignalSafeGetOrAllocate(uptr *dst, uptr size) {
  atomic_uintptr_t *a = (atomic_uintptr_t *)dst;
  void *val = (void *)atomic_load_relaxed(a);
  atomic_signal_fence(memory_order_acquire);  // Turns the previous load into
                                              // acquire wrt signals.
  if (UNLIKELY(val == nullptr)) {
    val = (void *)internal_mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(val);
    void *cmp = nullptr;
    if (!atomic_compare_exchange_strong(a, (uintptr_t *)&cmp, (uintptr_t)val,
                                        memory_order_acq_rel)) {
      internal_munmap(val, size);
      val = cmp;
    }
  }
  return val;
}

// On OS X, accessing TLVs via __thread or manually by using pthread_key_* is
// problematic, because there are several places where interceptors are called
// when TLVs are not accessible (early process startup, thread cleanup, ...).
// The following provides a "poor man's TLV" implementation, where we use the
// shadow memory of the pointer returned by pthread_self() to store a pointer to
// the ThreadState object. The main thread's ThreadState is stored separately
// in a static variable, because we need to access it even before the
// shadow memory is set up.
static uptr main_thread_identity = 0;
ALIGNED(64) static char main_thread_state[sizeof(ThreadState)];

ThreadState **cur_thread_location() {
  ThreadState **thread_identity = (ThreadState **)pthread_self();
  return ((uptr)thread_identity == main_thread_identity) ? nullptr
                                                         : thread_identity;
}

ThreadState *cur_thread() {
  ThreadState **thr_state_loc = cur_thread_location();
  if (thr_state_loc == nullptr || main_thread_identity == 0) {
    return (ThreadState *)&main_thread_state;
  }
  ThreadState **fake_tls = (ThreadState **)MemToShadow((uptr)thr_state_loc);
  ThreadState *thr = (ThreadState *)SignalSafeGetOrAllocate(
      (uptr *)fake_tls, sizeof(ThreadState));
  return thr;
}

// TODO(kuba.brecka): This is not async-signal-safe. In particular, we call
// munmap first and then clear `fake_tls`; if we receive a signal in between,
// handler will try to access the unmapped ThreadState.
void cur_thread_finalize() {
  ThreadState **thr_state_loc = cur_thread_location();
  if (thr_state_loc == nullptr) {
    // Calling dispatch_main() or xpc_main() actually invokes pthread_exit to
    // exit the main thread. Let's keep the main thread's ThreadState.
    return;
  }
  ThreadState **fake_tls = (ThreadState **)MemToShadow((uptr)thr_state_loc);
  internal_munmap(*fake_tls, sizeof(ThreadState));
  *fake_tls = nullptr;
}
#endif

void FlushShadowMemory() {
}

static void RegionMemUsage(uptr start, uptr end, uptr *res, uptr *dirty) {
  vm_address_t address = start;
  vm_address_t end_address = end;
  uptr resident_pages = 0;
  uptr dirty_pages = 0;
  while (address < end_address) {
    vm_size_t vm_region_size;
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    vm_region_extended_info_data_t vm_region_info;
    mach_port_t object_name;
    kern_return_t ret = vm_region_64(
        mach_task_self(), &address, &vm_region_size, VM_REGION_EXTENDED_INFO,
        (vm_region_info_t)&vm_region_info, &count, &object_name);
    if (ret != KERN_SUCCESS) break;

    resident_pages += vm_region_info.pages_resident;
    dirty_pages += vm_region_info.pages_dirtied;

    address += vm_region_size;
  }
  *res = resident_pages * GetPageSizeCached();
  *dirty = dirty_pages * GetPageSizeCached();
}

void WriteMemoryProfile(char *buf, uptr buf_size, uptr nthread, uptr nlive) {
  uptr shadow_res, shadow_dirty;
  uptr meta_res, meta_dirty;
  uptr trace_res, trace_dirty;
  RegionMemUsage(ShadowBeg(), ShadowEnd(), &shadow_res, &shadow_dirty);
  RegionMemUsage(MetaShadowBeg(), MetaShadowEnd(), &meta_res, &meta_dirty);
  RegionMemUsage(TraceMemBeg(), TraceMemEnd(), &trace_res, &trace_dirty);

#if !SANITIZER_GO
  uptr low_res, low_dirty;
  uptr high_res, high_dirty;
  uptr heap_res, heap_dirty;
  RegionMemUsage(LoAppMemBeg(), LoAppMemEnd(), &low_res, &low_dirty);
  RegionMemUsage(HiAppMemBeg(), HiAppMemEnd(), &high_res, &high_dirty);
  RegionMemUsage(HeapMemBeg(), HeapMemEnd(), &heap_res, &heap_dirty);
#else  // !SANITIZER_GO
  uptr app_res, app_dirty;
  RegionMemUsage(AppMemBeg(), AppMemEnd(), &app_res, &app_dirty);
#endif

  StackDepotStats *stacks = StackDepotGetStats();
  internal_snprintf(buf, buf_size,
    "shadow   (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
    "meta     (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
    "traces   (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
#if !SANITIZER_GO
    "low app  (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
    "high app (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
    "heap     (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
#else  // !SANITIZER_GO
    "app      (0x%016zx-0x%016zx): resident %zd kB, dirty %zd kB\n"
#endif
    "stacks: %zd unique IDs, %zd kB allocated\n"
    "threads: %zd total, %zd live\n"
    "------------------------------\n",
    ShadowBeg(), ShadowEnd(), shadow_res / 1024, shadow_dirty / 1024,
    MetaShadowBeg(), MetaShadowEnd(), meta_res / 1024, meta_dirty / 1024,
    TraceMemBeg(), TraceMemEnd(), trace_res / 1024, trace_dirty / 1024,
#if !SANITIZER_GO
    LoAppMemBeg(), LoAppMemEnd(), low_res / 1024, low_dirty / 1024,
    HiAppMemBeg(), HiAppMemEnd(), high_res / 1024, high_dirty / 1024,
    HeapMemBeg(), HeapMemEnd(), heap_res / 1024, heap_dirty / 1024,
#else  // !SANITIZER_GO
    AppMemBeg(), AppMemEnd(), app_res / 1024, app_dirty / 1024,
#endif
    stacks->n_uniq_ids, stacks->allocated / 1024,
    nthread, nlive);
}

#if !SANITIZER_GO
void InitializeShadowMemoryPlatform() { }

// On OS X, GCD worker threads are created without a call to pthread_create. We
// need to properly register these threads with ThreadCreate and ThreadStart.
// These threads don't have a parent thread, as they are created "spuriously".
// We're using a libpthread API that notifies us about a newly created thread.
// The `thread == pthread_self()` check indicates this is actually a worker
// thread. If it's just a regular thread, this hook is called on the parent
// thread.
typedef void (*pthread_introspection_hook_t)(unsigned int event,
                                             pthread_t thread, void *addr,
                                             size_t size);
extern "C" pthread_introspection_hook_t pthread_introspection_hook_install(
    pthread_introspection_hook_t hook);
static const uptr PTHREAD_INTROSPECTION_THREAD_CREATE = 1;
static const uptr PTHREAD_INTROSPECTION_THREAD_TERMINATE = 3;
static pthread_introspection_hook_t prev_pthread_introspection_hook;
static void my_pthread_introspection_hook(unsigned int event, pthread_t thread,
                                          void *addr, size_t size) {
  if (event == PTHREAD_INTROSPECTION_THREAD_CREATE) {
    if (thread == pthread_self()) {
      // The current thread is a newly created GCD worker thread.
      ThreadState *thr = cur_thread();
      Processor *proc = ProcCreate();
      ProcWire(proc, thr);
      ThreadState *parent_thread_state = nullptr;  // No parent.
      int tid = ThreadCreate(parent_thread_state, 0, (uptr)thread, true);
      CHECK_NE(tid, 0);
      ThreadStart(thr, tid, GetTid(), /*workerthread*/ true);
    }
  } else if (event == PTHREAD_INTROSPECTION_THREAD_TERMINATE) {
    if (thread == pthread_self()) {
      ThreadState *thr = cur_thread();
      if (thr->tctx) {
        DestroyThreadState();
      }
    }
  }

  if (prev_pthread_introspection_hook != nullptr)
    prev_pthread_introspection_hook(event, thread, addr, size);
}
#endif

void InitializePlatformEarly() {
#if defined(__aarch64__)
  uptr max_vm = GetMaxUserVirtualAddress() + 1;
  if (max_vm != Mapping::kHiAppMemEnd) {
    Printf("ThreadSanitizer: unsupported vm address limit %p, expected %p.\n",
           max_vm, Mapping::kHiAppMemEnd);
    Die();
  }
#endif
}

static const uptr kPthreadSetjmpXorKeySlot = 0x7;
extern "C" uptr __tsan_darwin_setjmp_xor_key = 0;

void InitializePlatform() {
  DisableCoreDumperIfNecessary();
#if !SANITIZER_GO
  CheckAndProtect();

  CHECK_EQ(main_thread_identity, 0);
  main_thread_identity = (uptr)pthread_self();

  prev_pthread_introspection_hook =
      pthread_introspection_hook_install(&my_pthread_introspection_hook);
#endif

  if (GetMacosVersion() >= MACOS_VERSION_MOJAVE) {
    __tsan_darwin_setjmp_xor_key =
        (uptr)pthread_getspecific(kPthreadSetjmpXorKeySlot);
  }
}

#if !SANITIZER_GO
void ImitateTlsWrite(ThreadState *thr, uptr tls_addr, uptr tls_size) {
  // The pointer to the ThreadState object is stored in the shadow memory
  // of the tls.
  uptr tls_end = tls_addr + tls_size;
  ThreadState **thr_state_loc = cur_thread_location();
  if (thr_state_loc == nullptr) {
    MemoryRangeImitateWrite(thr, /*pc=*/2, tls_addr, tls_size);
  } else {
    uptr thr_state_start = (uptr)thr_state_loc;
    uptr thr_state_end = thr_state_start + sizeof(uptr);
    CHECK_GE(thr_state_start, tls_addr);
    CHECK_LE(thr_state_start, tls_addr + tls_size);
    CHECK_GE(thr_state_end, tls_addr);
    CHECK_LE(thr_state_end, tls_addr + tls_size);
    MemoryRangeImitateWrite(thr, /*pc=*/2, tls_addr,
                            thr_state_start - tls_addr);
    MemoryRangeImitateWrite(thr, /*pc=*/2, thr_state_end,
                            tls_end - thr_state_end);
  }
}
#endif

#if !SANITIZER_GO
// Note: this function runs with async signals enabled,
// so it must not touch any tsan state.
int call_pthread_cancel_with_cleanup(int(*fn)(void *c, void *m,
    void *abstime), void *c, void *m, void *abstime,
    void(*cleanup)(void *arg), void *arg) {
  // pthread_cleanup_push/pop are hardcore macros mess.
  // We can't intercept nor call them w/o including pthread.h.
  int res;
  pthread_cleanup_push(cleanup, arg);
  res = fn(c, m, abstime);
  pthread_cleanup_pop(0);
  return res;
}
#endif

}  // namespace __tsan

#endif  // SANITIZER_MAC
