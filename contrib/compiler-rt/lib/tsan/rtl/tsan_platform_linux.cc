//===-- tsan_platform_linux.cc --------------------------------------------===//
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
// Linux- and FreeBSD-specific code.
//===----------------------------------------------------------------------===//


#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_platform_limits_netbsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "sanitizer_common/sanitizer_posix.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "sanitizer_common/sanitizer_stoptheworld.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"
#include "tsan_flags.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#if SANITIZER_LINUX
#include <sys/personality.h>
#include <setjmp.h>
#endif
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <dlfcn.h>
#if SANITIZER_LINUX
#define __need_res_state
#include <resolv.h>
#endif

#ifdef sa_handler
# undef sa_handler
#endif

#ifdef sa_sigaction
# undef sa_sigaction
#endif

#if SANITIZER_FREEBSD
extern "C" void *__libc_stack_end;
void *__libc_stack_end = 0;
#endif

#if SANITIZER_LINUX && defined(__aarch64__)
void InitializeGuardPtr() __attribute__((visibility("hidden")));
#endif

namespace __tsan {

#ifdef TSAN_RUNTIME_VMA
// Runtime detected VMA size.
uptr vmaSize;
#endif

enum {
  MemTotal  = 0,
  MemShadow = 1,
  MemMeta   = 2,
  MemFile   = 3,
  MemMmap   = 4,
  MemTrace  = 5,
  MemHeap   = 6,
  MemOther  = 7,
  MemCount  = 8,
};

void FillProfileCallback(uptr p, uptr rss, bool file,
                         uptr *mem, uptr stats_size) {
  mem[MemTotal] += rss;
  if (p >= ShadowBeg() && p < ShadowEnd())
    mem[MemShadow] += rss;
  else if (p >= MetaShadowBeg() && p < MetaShadowEnd())
    mem[MemMeta] += rss;
#if !SANITIZER_GO
  else if (p >= HeapMemBeg() && p < HeapMemEnd())
    mem[MemHeap] += rss;
  else if (p >= LoAppMemBeg() && p < LoAppMemEnd())
    mem[file ? MemFile : MemMmap] += rss;
  else if (p >= HiAppMemBeg() && p < HiAppMemEnd())
    mem[file ? MemFile : MemMmap] += rss;
#else
  else if (p >= AppMemBeg() && p < AppMemEnd())
    mem[file ? MemFile : MemMmap] += rss;
#endif
  else if (p >= TraceMemBeg() && p < TraceMemEnd())
    mem[MemTrace] += rss;
  else
    mem[MemOther] += rss;
}

void WriteMemoryProfile(char *buf, uptr buf_size, uptr nthread, uptr nlive) {
  uptr mem[MemCount];
  internal_memset(mem, 0, sizeof(mem[0]) * MemCount);
  __sanitizer::GetMemoryProfile(FillProfileCallback, mem, 7);
  StackDepotStats *stacks = StackDepotGetStats();
  internal_snprintf(buf, buf_size,
      "RSS %zd MB: shadow:%zd meta:%zd file:%zd mmap:%zd"
      " trace:%zd heap:%zd other:%zd stacks=%zd[%zd] nthr=%zd/%zd\n",
      mem[MemTotal] >> 20, mem[MemShadow] >> 20, mem[MemMeta] >> 20,
      mem[MemFile] >> 20, mem[MemMmap] >> 20, mem[MemTrace] >> 20,
      mem[MemHeap] >> 20, mem[MemOther] >> 20,
      stacks->allocated >> 20, stacks->n_uniq_ids,
      nlive, nthread);
}

#if SANITIZER_LINUX
void FlushShadowMemoryCallback(
    const SuspendedThreadsList &suspended_threads_list,
    void *argument) {
  ReleaseMemoryPagesToOS(ShadowBeg(), ShadowEnd());
}
#endif

void FlushShadowMemory() {
#if SANITIZER_LINUX
  StopTheWorld(FlushShadowMemoryCallback, 0);
#endif
}

#if !SANITIZER_GO
// Mark shadow for .rodata sections with the special kShadowRodata marker.
// Accesses to .rodata can't race, so this saves time, memory and trace space.
static void MapRodata() {
  // First create temp file.
  const char *tmpdir = GetEnv("TMPDIR");
  if (tmpdir == 0)
    tmpdir = GetEnv("TEST_TMPDIR");
#ifdef P_tmpdir
  if (tmpdir == 0)
    tmpdir = P_tmpdir;
#endif
  if (tmpdir == 0)
    return;
  char name[256];
  internal_snprintf(name, sizeof(name), "%s/tsan.rodata.%d",
                    tmpdir, (int)internal_getpid());
  uptr openrv = internal_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (internal_iserror(openrv))
    return;
  internal_unlink(name);  // Unlink it now, so that we can reuse the buffer.
  fd_t fd = openrv;
  // Fill the file with kShadowRodata.
  const uptr kMarkerSize = 512 * 1024 / sizeof(u64);
  InternalMmapVector<u64> marker(kMarkerSize);
  // volatile to prevent insertion of memset
  for (volatile u64 *p = marker.data(); p < marker.data() + kMarkerSize; p++)
    *p = kShadowRodata;
  internal_write(fd, marker.data(), marker.size() * sizeof(u64));
  // Map the file into memory.
  uptr page = internal_mmap(0, GetPageSizeCached(), PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
  if (internal_iserror(page)) {
    internal_close(fd);
    return;
  }
  // Map the file into shadow of .rodata sections.
  MemoryMappingLayout proc_maps(/*cache_enabled*/true);
  // Reusing the buffer 'name'.
  MemoryMappedSegment segment(name, ARRAY_SIZE(name));
  while (proc_maps.Next(&segment)) {
    if (segment.filename[0] != 0 && segment.filename[0] != '[' &&
        segment.IsReadable() && segment.IsExecutable() &&
        !segment.IsWritable() && IsAppMem(segment.start)) {
      // Assume it's .rodata
      char *shadow_start = (char *)MemToShadow(segment.start);
      char *shadow_end = (char *)MemToShadow(segment.end);
      for (char *p = shadow_start; p < shadow_end;
           p += marker.size() * sizeof(u64)) {
        internal_mmap(p, Min<uptr>(marker.size() * sizeof(u64), shadow_end - p),
                      PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
      }
    }
  }
  internal_close(fd);
}

void InitializeShadowMemoryPlatform() {
  MapRodata();
}

#endif  // #if !SANITIZER_GO

void InitializePlatformEarly() {
#ifdef TSAN_RUNTIME_VMA
  vmaSize =
    (MostSignificantSetBitIndex(GET_CURRENT_FRAME()) + 1);
#if defined(__aarch64__)
# if !SANITIZER_GO
  if (vmaSize != 39 && vmaSize != 42 && vmaSize != 48) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 39, 42 and 48\n", vmaSize);
    Die();
  }
#else
  if (vmaSize != 48) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 48\n", vmaSize);
    Die();
  }
#endif
#elif defined(__powerpc64__)
# if !SANITIZER_GO
  if (vmaSize != 44 && vmaSize != 46 && vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 44, 46, and 47\n", vmaSize);
    Die();
  }
# else
  if (vmaSize != 46 && vmaSize != 47) {
    Printf("FATAL: ThreadSanitizer: unsupported VMA range\n");
    Printf("FATAL: Found %zd - Supported 46, and 47\n", vmaSize);
    Die();
  }
# endif
#endif
#endif
}

void InitializePlatform() {
  DisableCoreDumperIfNecessary();

  // Go maps shadow memory lazily and works fine with limited address space.
  // Unlimited stack is not a problem as well, because the executable
  // is not compiled with -pie.
  if (!SANITIZER_GO) {
    bool reexec = false;
    // TSan doesn't play well with unlimited stack size (as stack
    // overlaps with shadow memory). If we detect unlimited stack size,
    // we re-exec the program with limited stack size as a best effort.
    if (StackSizeIsUnlimited()) {
      const uptr kMaxStackSize = 32 * 1024 * 1024;
      VReport(1, "Program is run with unlimited stack size, which wouldn't "
                 "work with ThreadSanitizer.\n"
                 "Re-execing with stack size limited to %zd bytes.\n",
              kMaxStackSize);
      SetStackSizeLimitInBytes(kMaxStackSize);
      reexec = true;
    }

    if (!AddressSpaceIsUnlimited()) {
      Report("WARNING: Program is run with limited virtual address space,"
             " which wouldn't work with ThreadSanitizer.\n");
      Report("Re-execing with unlimited virtual address space.\n");
      SetAddressSpaceUnlimited();
      reexec = true;
    }
#if SANITIZER_LINUX && defined(__aarch64__)
    // After patch "arm64: mm: support ARCH_MMAP_RND_BITS." is introduced in
    // linux kernel, the random gap between stack and mapped area is increased
    // from 128M to 36G on 39-bit aarch64. As it is almost impossible to cover
    // this big range, we should disable randomized virtual space on aarch64.
    int old_personality = personality(0xffffffff);
    if (old_personality != -1 && (old_personality & ADDR_NO_RANDOMIZE) == 0) {
      VReport(1, "WARNING: Program is run with randomized virtual address "
              "space, which wouldn't work with ThreadSanitizer.\n"
              "Re-execing with fixed virtual address space.\n");
      CHECK_NE(personality(old_personality | ADDR_NO_RANDOMIZE), -1);
      reexec = true;
    }
    // Initialize the guard pointer used in {sig}{set,long}jump.
    InitializeGuardPtr();
#endif
    if (reexec)
      ReExec();
  }

#if !SANITIZER_GO
  CheckAndProtect();
  InitTlsSize();
#endif
}

#if !SANITIZER_GO
// Extract file descriptors passed to glibc internal __res_iclose function.
// This is required to properly "close" the fds, because we do not see internal
// closes within glibc. The code is a pure hack.
int ExtractResolvFDs(void *state, int *fds, int nfd) {
#if SANITIZER_LINUX && !SANITIZER_ANDROID
  int cnt = 0;
  struct __res_state *statp = (struct __res_state*)state;
  for (int i = 0; i < MAXNS && cnt < nfd; i++) {
    if (statp->_u._ext.nsaddrs[i] && statp->_u._ext.nssocks[i] != -1)
      fds[cnt++] = statp->_u._ext.nssocks[i];
  }
  return cnt;
#else
  return 0;
#endif
}

// Extract file descriptors passed via UNIX domain sockets.
// This is requried to properly handle "open" of these fds.
// see 'man recvmsg' and 'man 3 cmsg'.
int ExtractRecvmsgFDs(void *msgp, int *fds, int nfd) {
  int res = 0;
  msghdr *msg = (msghdr*)msgp;
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
  for (; cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
      continue;
    int n = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(fds[0]);
    for (int i = 0; i < n; i++) {
      fds[res++] = ((int*)CMSG_DATA(cmsg))[i];
      if (res == nfd)
        return res;
    }
  }
  return res;
}

void ImitateTlsWrite(ThreadState *thr, uptr tls_addr, uptr tls_size) {
  // Check that the thr object is in tls;
  const uptr thr_beg = (uptr)thr;
  const uptr thr_end = (uptr)thr + sizeof(*thr);
  CHECK_GE(thr_beg, tls_addr);
  CHECK_LE(thr_beg, tls_addr + tls_size);
  CHECK_GE(thr_end, tls_addr);
  CHECK_LE(thr_end, tls_addr + tls_size);
  // Since the thr object is huge, skip it.
  MemoryRangeImitateWrite(thr, /*pc=*/2, tls_addr, thr_beg - tls_addr);
  MemoryRangeImitateWrite(thr, /*pc=*/2, thr_end,
                          tls_addr + tls_size - thr_end);
}

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

#if !SANITIZER_GO
void ReplaceSystemMalloc() { }
#endif

#if !SANITIZER_GO
#if SANITIZER_ANDROID
// On Android, one thread can call intercepted functions after
// DestroyThreadState(), so add a fake thread state for "dead" threads.
static ThreadState *dead_thread_state = nullptr;

ThreadState *cur_thread() {
  ThreadState* thr = reinterpret_cast<ThreadState*>(*get_android_tls_ptr());
  if (thr == nullptr) {
    __sanitizer_sigset_t emptyset;
    internal_sigfillset(&emptyset);
    __sanitizer_sigset_t oldset;
    CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &emptyset, &oldset));
    thr = reinterpret_cast<ThreadState*>(*get_android_tls_ptr());
    if (thr == nullptr) {
      thr = reinterpret_cast<ThreadState*>(MmapOrDie(sizeof(ThreadState),
                                                     "ThreadState"));
      *get_android_tls_ptr() = reinterpret_cast<uptr>(thr);
      if (dead_thread_state == nullptr) {
        dead_thread_state = reinterpret_cast<ThreadState*>(
            MmapOrDie(sizeof(ThreadState), "ThreadState"));
        dead_thread_state->fast_state.SetIgnoreBit();
        dead_thread_state->ignore_interceptors = 1;
        dead_thread_state->is_dead = true;
        *const_cast<int*>(&dead_thread_state->tid) = -1;
        CHECK_EQ(0, internal_mprotect(dead_thread_state, sizeof(ThreadState),
                                      PROT_READ));
      }
    }
    CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &oldset, nullptr));
  }
  return thr;
}

void cur_thread_finalize() {
  __sanitizer_sigset_t emptyset;
  internal_sigfillset(&emptyset);
  __sanitizer_sigset_t oldset;
  CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &emptyset, &oldset));
  ThreadState* thr = reinterpret_cast<ThreadState*>(*get_android_tls_ptr());
  if (thr != dead_thread_state) {
    *get_android_tls_ptr() = reinterpret_cast<uptr>(dead_thread_state);
    UnmapOrDie(thr, sizeof(ThreadState));
  }
  CHECK_EQ(0, internal_sigprocmask(SIG_SETMASK, &oldset, nullptr));
}
#endif  // SANITIZER_ANDROID
#endif  // if !SANITIZER_GO

}  // namespace __tsan

#endif  // SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD
