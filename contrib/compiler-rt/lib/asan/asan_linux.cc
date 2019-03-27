//===-- asan_linux.cc -----------------------------------------------------===//
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
// Linux-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
    SANITIZER_SOLARIS

#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_premap_shadow.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_freebsd.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_procmaps.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <unwind.h>

#if SANITIZER_FREEBSD
#include <sys/link_elf.h>
#endif

#if SANITIZER_SOLARIS
#include <link.h>
#endif

#if SANITIZER_ANDROID || SANITIZER_FREEBSD || SANITIZER_SOLARIS
#include <ucontext.h>
extern "C" void* _DYNAMIC;
#elif SANITIZER_NETBSD
#include <link_elf.h>
#include <ucontext.h>
extern Elf_Dyn _DYNAMIC;
#else
#include <sys/ucontext.h>
#include <link.h>
#endif

// x86-64 FreeBSD 9.2 and older define 'ucontext_t' incorrectly in
// 32-bit mode.
#if SANITIZER_FREEBSD && (SANITIZER_WORDSIZE == 32) && \
  __FreeBSD_version <= 902001  // v9.2
#define ucontext_t xucontext_t
#endif

typedef enum {
  ASAN_RT_VERSION_UNDEFINED = 0,
  ASAN_RT_VERSION_DYNAMIC,
  ASAN_RT_VERSION_STATIC,
} asan_rt_version_t;

// FIXME: perhaps also store abi version here?
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
asan_rt_version_t  __asan_rt_version;
}

namespace __asan {

void InitializePlatformInterceptors() {}
void InitializePlatformExceptionHandlers() {}
bool IsSystemHeapAddress (uptr addr) { return false; }

void *AsanDoesNotSupportStaticLinkage() {
  // This will fail to link with -static.
  return &_DYNAMIC;  // defined in link.h
}

static void UnmapFromTo(uptr from, uptr to) {
  CHECK(to >= from);
  if (to == from) return;
  uptr res = internal_munmap(reinterpret_cast<void *>(from), to - from);
  if (UNLIKELY(internal_iserror(res))) {
    Report(
        "ERROR: AddresSanitizer failed to unmap 0x%zx (%zd) bytes at address "
        "%p\n",
        to - from, to - from, from);
    CHECK("unable to unmap" && 0);
  }
}

#if ASAN_PREMAP_SHADOW
uptr FindPremappedShadowStart() {
  uptr granularity = GetMmapGranularity();
  uptr shadow_start = reinterpret_cast<uptr>(&__asan_shadow);
  uptr premap_shadow_size = PremapShadowSize();
  uptr shadow_size = RoundUpTo(kHighShadowEnd, granularity);
  // We may have mapped too much. Release extra memory.
  UnmapFromTo(shadow_start + shadow_size, shadow_start + premap_shadow_size);
  return shadow_start;
}
#endif

uptr FindDynamicShadowStart() {
#if ASAN_PREMAP_SHADOW
  if (!PremapShadowFailed())
    return FindPremappedShadowStart();
#endif

  uptr granularity = GetMmapGranularity();
  uptr alignment = granularity * 8;
  uptr left_padding = granularity;
  uptr shadow_size = RoundUpTo(kHighShadowEnd, granularity);
  uptr map_size = shadow_size + left_padding + alignment;

  uptr map_start = (uptr)MmapNoAccess(map_size);
  CHECK_NE(map_start, ~(uptr)0);

  uptr shadow_start = RoundUpTo(map_start + left_padding, alignment);
  UnmapFromTo(map_start, shadow_start - left_padding);
  UnmapFromTo(shadow_start + shadow_size, map_start + map_size);

  return shadow_start;
}

void AsanApplyToGlobals(globals_op_fptr op, const void *needle) {
  UNIMPLEMENTED();
}

#if SANITIZER_ANDROID
// FIXME: should we do anything for Android?
void AsanCheckDynamicRTPrereqs() {}
void AsanCheckIncompatibleRT() {}
#else
static int FindFirstDSOCallback(struct dl_phdr_info *info, size_t size,
                                void *data) {
  VReport(2, "info->dlpi_name = %s\tinfo->dlpi_addr = %p\n",
          info->dlpi_name, info->dlpi_addr);

  // Continue until the first dynamic library is found
  if (!info->dlpi_name || info->dlpi_name[0] == 0)
    return 0;

  // Ignore vDSO
  if (internal_strncmp(info->dlpi_name, "linux-", sizeof("linux-") - 1) == 0)
    return 0;

#if SANITIZER_FREEBSD || SANITIZER_NETBSD
  // Ignore first entry (the main program)
  char **p = (char **)data;
  if (!(*p)) {
    *p = (char *)-1;
    return 0;
  }
#endif

#if SANITIZER_SOLARIS
  // Ignore executable on Solaris
  if (info->dlpi_addr == 0)
    return 0;
#endif

  *(const char **)data = info->dlpi_name;
  return 1;
}

static bool IsDynamicRTName(const char *libname) {
  return internal_strstr(libname, "libclang_rt.asan") ||
    internal_strstr(libname, "libasan.so");
}

static void ReportIncompatibleRT() {
  Report("Your application is linked against incompatible ASan runtimes.\n");
  Die();
}

void AsanCheckDynamicRTPrereqs() {
  if (!ASAN_DYNAMIC || !flags()->verify_asan_link_order)
    return;

  // Ensure that dynamic RT is the first DSO in the list
  const char *first_dso_name = nullptr;
  dl_iterate_phdr(FindFirstDSOCallback, &first_dso_name);
  if (first_dso_name && !IsDynamicRTName(first_dso_name)) {
    Report("ASan runtime does not come first in initial library list; "
           "you should either link runtime to your application or "
           "manually preload it with LD_PRELOAD.\n");
    Die();
  }
}

void AsanCheckIncompatibleRT() {
  if (ASAN_DYNAMIC) {
    if (__asan_rt_version == ASAN_RT_VERSION_UNDEFINED) {
      __asan_rt_version = ASAN_RT_VERSION_DYNAMIC;
    } else if (__asan_rt_version != ASAN_RT_VERSION_DYNAMIC) {
      ReportIncompatibleRT();
    }
  } else {
    if (__asan_rt_version == ASAN_RT_VERSION_UNDEFINED) {
      // Ensure that dynamic runtime is not present. We should detect it
      // as early as possible, otherwise ASan interceptors could bind to
      // the functions in dynamic ASan runtime instead of the functions in
      // system libraries, causing crashes later in ASan initialization.
      MemoryMappingLayout proc_maps(/*cache_enabled*/true);
      char filename[PATH_MAX];
      MemoryMappedSegment segment(filename, sizeof(filename));
      while (proc_maps.Next(&segment)) {
        if (IsDynamicRTName(segment.filename)) {
          Report("Your application is linked against "
                 "incompatible ASan runtimes.\n");
          Die();
        }
      }
      __asan_rt_version = ASAN_RT_VERSION_STATIC;
    } else if (__asan_rt_version != ASAN_RT_VERSION_STATIC) {
      ReportIncompatibleRT();
    }
  }
}
#endif // SANITIZER_ANDROID

#if !SANITIZER_ANDROID
void ReadContextStack(void *context, uptr *stack, uptr *ssize) {
  ucontext_t *ucp = (ucontext_t*)context;
  *stack = (uptr)ucp->uc_stack.ss_sp;
  *ssize = ucp->uc_stack.ss_size;
}
#else
void ReadContextStack(void *context, uptr *stack, uptr *ssize) {
  UNIMPLEMENTED();
}
#endif

void *AsanDlSymNext(const char *sym) {
  return dlsym(RTLD_NEXT, sym);
}

bool HandleDlopenInit() {
  // Not supported on this platform.
  static_assert(!SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be false");
  return false;
}

} // namespace __asan

#endif  // SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD ||
        // SANITIZER_SOLARIS
