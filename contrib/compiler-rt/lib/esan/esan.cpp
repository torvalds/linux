//===-- esan.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Main file (entry points) for the Esan run-time.
//===----------------------------------------------------------------------===//

#include "esan.h"
#include "esan_flags.h"
#include "esan_interface_internal.h"
#include "esan_shadow.h"
#include "cache_frag.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "working_set.h"

// See comment below.
extern "C" {
extern void __cxa_atexit(void (*function)(void));
}

namespace __esan {

bool EsanIsInitialized;
bool EsanDuringInit;
ShadowMapping Mapping;

// Different tools use different scales within the same shadow mapping scheme.
// The scale used here must match that used by the compiler instrumentation.
// This array is indexed by the ToolType enum.
static const uptr ShadowScale[] = {
  0, // ESAN_None.
  2, // ESAN_CacheFrag: 4B:1B, so 4 to 1 == >>2.
  6, // ESAN_WorkingSet: 64B:1B, so 64 to 1 == >>6.
};

// We are combining multiple performance tuning tools under the umbrella of
// one EfficiencySanitizer super-tool.  Most of our tools have very similar
// memory access instrumentation, shadow memory mapping, libc interception,
// etc., and there is typically more shared code than distinct code.
//
// We are not willing to dispatch on tool dynamically in our fastpath
// instrumentation: thus, which tool to use is a static option selected
// at compile time and passed to __esan_init().
//
// We are willing to pay the overhead of tool dispatch in the slowpath to more
// easily share code.  We expect to only come here rarely.
// If this becomes a performance hit, we can add separate interface
// routines for each subtool (e.g., __esan_cache_frag_aligned_load_4).
// But for libc interceptors, we'll have to do one of the following:
// A) Add multiple-include support to sanitizer_common_interceptors.inc,
//    instantiate it separately for each tool, and call the selected
//    tool's intercept setup code.
// B) Build separate static runtime libraries, one for each tool.
// C) Completely split the tools into separate sanitizers.

void processRangeAccess(uptr PC, uptr Addr, int Size, bool IsWrite) {
  VPrintf(3, "in esan::%s %p: %c %p %d\n", __FUNCTION__, PC,
          IsWrite ? 'w' : 'r', Addr, Size);
  if (__esan_which_tool == ESAN_CacheFrag) {
    // TODO(bruening): add shadow mapping and update shadow bits here.
    // We'll move this to cache_frag.cpp once we have something.
  } else if (__esan_which_tool == ESAN_WorkingSet) {
    processRangeAccessWorkingSet(PC, Addr, Size, IsWrite);
  }
}

bool processSignal(int SigNum, void (*Handler)(int), void (**Result)(int)) {
  if (__esan_which_tool == ESAN_WorkingSet)
    return processWorkingSetSignal(SigNum, Handler, Result);
  return true;
}

bool processSigaction(int SigNum, const void *Act, void *OldAct) {
  if (__esan_which_tool == ESAN_WorkingSet)
    return processWorkingSetSigaction(SigNum, Act, OldAct);
  return true;
}

bool processSigprocmask(int How, void *Set, void *OldSet) {
  if (__esan_which_tool == ESAN_WorkingSet)
    return processWorkingSetSigprocmask(How, Set, OldSet);
  return true;
}

#if SANITIZER_DEBUG
static bool verifyShadowScheme() {
  // Sanity checks for our shadow mapping scheme.
  uptr AppStart, AppEnd;
  if (Verbosity() >= 3) {
    for (int i = 0; getAppRegion(i, &AppStart, &AppEnd); ++i) {
      VPrintf(3, "App #%d: [%zx-%zx) (%zuGB)\n", i, AppStart, AppEnd,
              (AppEnd - AppStart) >> 30);
    }
  }
  for (int Scale = 0; Scale < 8; ++Scale) {
    Mapping.initialize(Scale);
    if (Verbosity() >= 3) {
      VPrintf(3, "\nChecking scale %d\n", Scale);
      uptr ShadowStart, ShadowEnd;
      for (int i = 0; getShadowRegion(i, &ShadowStart, &ShadowEnd); ++i) {
        VPrintf(3, "Shadow #%d: [%zx-%zx) (%zuGB)\n", i, ShadowStart,
                ShadowEnd, (ShadowEnd - ShadowStart) >> 30);
      }
      for (int i = 0; getShadowRegion(i, &ShadowStart, &ShadowEnd); ++i) {
        VPrintf(3, "Shadow(Shadow) #%d: [%zx-%zx)\n", i,
                appToShadow(ShadowStart), appToShadow(ShadowEnd - 1)+1);
      }
    }
    for (int i = 0; getAppRegion(i, &AppStart, &AppEnd); ++i) {
      DCHECK(isAppMem(AppStart));
      DCHECK(!isAppMem(AppStart - 1));
      DCHECK(isAppMem(AppEnd - 1));
      DCHECK(!isAppMem(AppEnd));
      DCHECK(!isShadowMem(AppStart));
      DCHECK(!isShadowMem(AppEnd - 1));
      DCHECK(isShadowMem(appToShadow(AppStart)));
      DCHECK(isShadowMem(appToShadow(AppEnd - 1)));
      // Double-shadow checks.
      DCHECK(!isShadowMem(appToShadow(appToShadow(AppStart))));
      DCHECK(!isShadowMem(appToShadow(appToShadow(AppEnd - 1))));
    }
    // Ensure no shadow regions overlap each other.
    uptr ShadowAStart, ShadowBStart, ShadowAEnd, ShadowBEnd;
    for (int i = 0; getShadowRegion(i, &ShadowAStart, &ShadowAEnd); ++i) {
      for (int j = 0; getShadowRegion(j, &ShadowBStart, &ShadowBEnd); ++j) {
        DCHECK(i == j || ShadowAStart >= ShadowBEnd ||
               ShadowAEnd <= ShadowBStart);
      }
    }
  }
  return true;
}
#endif

uptr VmaSize;

static void initializeShadow() {
  verifyAddressSpace();

  // This is based on the assumption that the intial stack is always allocated
  // in the topmost segment of the user address space and the assumption
  // holds true on all the platforms currently supported.
  VmaSize =
    (MostSignificantSetBitIndex(GET_CURRENT_FRAME()) + 1);

  DCHECK(verifyShadowScheme());

  Mapping.initialize(ShadowScale[__esan_which_tool]);

  VPrintf(1, "Shadow scale=%d offset=%p\n", Mapping.Scale, Mapping.Offset);

  uptr ShadowStart, ShadowEnd;
  for (int i = 0; getShadowRegion(i, &ShadowStart, &ShadowEnd); ++i) {
    VPrintf(1, "Shadow #%d: [%zx-%zx) (%zuGB)\n", i, ShadowStart, ShadowEnd,
            (ShadowEnd - ShadowStart) >> 30);

    uptr Map = 0;
    if (__esan_which_tool == ESAN_WorkingSet) {
      // We want to identify all shadow pages that are touched so we start
      // out inaccessible.
      Map = (uptr)MmapFixedNoAccess(ShadowStart, ShadowEnd- ShadowStart,
                                    "shadow");
    } else {
      if (MmapFixedNoReserve(ShadowStart, ShadowEnd - ShadowStart, "shadow"))
        Map = ShadowStart;
    }
    if (Map != ShadowStart) {
      Printf("FATAL: EfficiencySanitizer failed to map its shadow memory.\n");
      Die();
    }

    if (common_flags()->no_huge_pages_for_shadow)
      NoHugePagesInRegion(ShadowStart, ShadowEnd - ShadowStart);
    if (common_flags()->use_madv_dontdump)
      DontDumpShadowMemory(ShadowStart, ShadowEnd - ShadowStart);

    // TODO: Call MmapNoAccess() on in-between regions.
  }
}

void initializeLibrary(ToolType Tool) {
  // We assume there is only one thread during init, but we need to
  // guard against double-init when we're (re-)called from an
  // early interceptor.
  if (EsanIsInitialized || EsanDuringInit)
    return;
  EsanDuringInit = true;
  CHECK(Tool == __esan_which_tool);
  SanitizerToolName = "EfficiencySanitizer";
  CacheBinaryName();
  initializeFlags();

  // Intercepting libc _exit or exit via COMMON_INTERCEPTOR_ON_EXIT only
  // finalizes on an explicit exit call by the app.  To handle a normal
  // exit we register an atexit handler.
  ::__cxa_atexit((void (*)())finalizeLibrary);

  VPrintf(1, "in esan::%s\n", __FUNCTION__);
  if (__esan_which_tool <= ESAN_None || __esan_which_tool >= ESAN_Max) {
    Printf("ERROR: unknown tool %d requested\n", __esan_which_tool);
    Die();
  }

  initializeShadow();
  if (__esan_which_tool == ESAN_WorkingSet)
    initializeShadowWorkingSet();

  initializeInterceptors();

  if (__esan_which_tool == ESAN_CacheFrag) {
    initializeCacheFrag();
  } else if (__esan_which_tool == ESAN_WorkingSet) {
    initializeWorkingSet();
  }

  EsanIsInitialized = true;
  EsanDuringInit = false;
}

int finalizeLibrary() {
  VPrintf(1, "in esan::%s\n", __FUNCTION__);
  if (__esan_which_tool == ESAN_CacheFrag) {
    return finalizeCacheFrag();
  } else if (__esan_which_tool == ESAN_WorkingSet) {
    return finalizeWorkingSet();
  }
  return 0;
}

void reportResults() {
  VPrintf(1, "in esan::%s\n", __FUNCTION__);
  if (__esan_which_tool == ESAN_CacheFrag) {
    return reportCacheFrag();
  } else if (__esan_which_tool == ESAN_WorkingSet) {
    return reportWorkingSet();
  }
}

void processCompilationUnitInit(void *Ptr) {
  VPrintf(2, "in esan::%s\n", __FUNCTION__);
  if (__esan_which_tool == ESAN_CacheFrag) {
    DCHECK(Ptr != nullptr);
    processCacheFragCompilationUnitInit(Ptr);
  } else {
    DCHECK(Ptr == nullptr);
  }
}

// This is called when the containing module is unloaded.
// For the main executable module, this is called after finalizeLibrary.
void processCompilationUnitExit(void *Ptr) {
  VPrintf(2, "in esan::%s\n", __FUNCTION__);
  if (__esan_which_tool == ESAN_CacheFrag) {
    DCHECK(Ptr != nullptr);
    processCacheFragCompilationUnitExit(Ptr);
  } else {
    DCHECK(Ptr == nullptr);
  }
}

unsigned int getSampleCount() {
  VPrintf(1, "in esan::%s\n", __FUNCTION__);
  if (__esan_which_tool == ESAN_WorkingSet) {
    return getSampleCountWorkingSet();
  }
  return 0;
}

} // namespace __esan
