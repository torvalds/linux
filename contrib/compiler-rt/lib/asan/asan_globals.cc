//===-- asan_globals.cc ---------------------------------------------------===//
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
// Handle globals.
//===----------------------------------------------------------------------===//

#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_stats.h"
#include "asan_suppressions.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_symbolizer.h"

namespace __asan {

typedef __asan_global Global;

struct ListOfGlobals {
  const Global *g;
  ListOfGlobals *next;
};

static BlockingMutex mu_for_globals(LINKER_INITIALIZED);
static LowLevelAllocator allocator_for_globals;
static ListOfGlobals *list_of_all_globals;

static const int kDynamicInitGlobalsInitialCapacity = 512;
struct DynInitGlobal {
  Global g;
  bool initialized;
};
typedef InternalMmapVector<DynInitGlobal> VectorOfGlobals;
// Lazy-initialized and never deleted.
static VectorOfGlobals *dynamic_init_globals;

// We want to remember where a certain range of globals was registered.
struct GlobalRegistrationSite {
  u32 stack_id;
  Global *g_first, *g_last;
};
typedef InternalMmapVector<GlobalRegistrationSite> GlobalRegistrationSiteVector;
static GlobalRegistrationSiteVector *global_registration_site_vector;

ALWAYS_INLINE void PoisonShadowForGlobal(const Global *g, u8 value) {
  FastPoisonShadow(g->beg, g->size_with_redzone, value);
}

ALWAYS_INLINE void PoisonRedZones(const Global &g) {
  uptr aligned_size = RoundUpTo(g.size, SHADOW_GRANULARITY);
  FastPoisonShadow(g.beg + aligned_size, g.size_with_redzone - aligned_size,
                   kAsanGlobalRedzoneMagic);
  if (g.size != aligned_size) {
    FastPoisonShadowPartialRightRedzone(
        g.beg + RoundDownTo(g.size, SHADOW_GRANULARITY),
        g.size % SHADOW_GRANULARITY,
        SHADOW_GRANULARITY,
        kAsanGlobalRedzoneMagic);
  }
}

const uptr kMinimalDistanceFromAnotherGlobal = 64;

static bool IsAddressNearGlobal(uptr addr, const __asan_global &g) {
  if (addr <= g.beg - kMinimalDistanceFromAnotherGlobal) return false;
  if (addr >= g.beg + g.size_with_redzone) return false;
  return true;
}

static void ReportGlobal(const Global &g, const char *prefix) {
  Report(
      "%s Global[%p]: beg=%p size=%zu/%zu name=%s module=%s dyn_init=%zu "
      "odr_indicator=%p\n",
      prefix, &g, (void *)g.beg, g.size, g.size_with_redzone, g.name,
      g.module_name, g.has_dynamic_init, (void *)g.odr_indicator);
  if (g.location) {
    Report("  location (%p): name=%s[%p], %d %d\n", g.location,
           g.location->filename, g.location->filename, g.location->line_no,
           g.location->column_no);
  }
}

static u32 FindRegistrationSite(const Global *g) {
  mu_for_globals.CheckLocked();
  CHECK(global_registration_site_vector);
  for (uptr i = 0, n = global_registration_site_vector->size(); i < n; i++) {
    GlobalRegistrationSite &grs = (*global_registration_site_vector)[i];
    if (g >= grs.g_first && g <= grs.g_last)
      return grs.stack_id;
  }
  return 0;
}

int GetGlobalsForAddress(uptr addr, Global *globals, u32 *reg_sites,
                         int max_globals) {
  if (!flags()->report_globals) return 0;
  BlockingMutexLock lock(&mu_for_globals);
  int res = 0;
  for (ListOfGlobals *l = list_of_all_globals; l; l = l->next) {
    const Global &g = *l->g;
    if (flags()->report_globals >= 2)
      ReportGlobal(g, "Search");
    if (IsAddressNearGlobal(addr, g)) {
      globals[res] = g;
      if (reg_sites)
        reg_sites[res] = FindRegistrationSite(&g);
      res++;
      if (res == max_globals) break;
    }
  }
  return res;
}

enum GlobalSymbolState {
  UNREGISTERED = 0,
  REGISTERED = 1
};

// Check ODR violation for given global G via special ODR indicator. We use
// this method in case compiler instruments global variables through their
// local aliases.
static void CheckODRViolationViaIndicator(const Global *g) {
  // Instrumentation requests to skip ODR check.
  if (g->odr_indicator == UINTPTR_MAX)
    return;
  u8 *odr_indicator = reinterpret_cast<u8 *>(g->odr_indicator);
  if (*odr_indicator == UNREGISTERED) {
    *odr_indicator = REGISTERED;
    return;
  }
  // If *odr_indicator is DEFINED, some module have already registered
  // externally visible symbol with the same name. This is an ODR violation.
  for (ListOfGlobals *l = list_of_all_globals; l; l = l->next) {
    if (g->odr_indicator == l->g->odr_indicator &&
        (flags()->detect_odr_violation >= 2 || g->size != l->g->size) &&
        !IsODRViolationSuppressed(g->name))
      ReportODRViolation(g, FindRegistrationSite(g),
                         l->g, FindRegistrationSite(l->g));
  }
}

// Check ODR violation for given global G by checking if it's already poisoned.
// We use this method in case compiler doesn't use private aliases for global
// variables.
static void CheckODRViolationViaPoisoning(const Global *g) {
  if (__asan_region_is_poisoned(g->beg, g->size_with_redzone)) {
    // This check may not be enough: if the first global is much larger
    // the entire redzone of the second global may be within the first global.
    for (ListOfGlobals *l = list_of_all_globals; l; l = l->next) {
      if (g->beg == l->g->beg &&
          (flags()->detect_odr_violation >= 2 || g->size != l->g->size) &&
          !IsODRViolationSuppressed(g->name))
        ReportODRViolation(g, FindRegistrationSite(g),
                           l->g, FindRegistrationSite(l->g));
    }
  }
}

// Clang provides two different ways for global variables protection:
// it can poison the global itself or its private alias. In former
// case we may poison same symbol multiple times, that can help us to
// cheaply detect ODR violation: if we try to poison an already poisoned
// global, we have ODR violation error.
// In latter case, we poison each symbol exactly once, so we use special
// indicator symbol to perform similar check.
// In either case, compiler provides a special odr_indicator field to Global
// structure, that can contain two kinds of values:
//   1) Non-zero value. In this case, odr_indicator is an address of
//      corresponding indicator variable for given global.
//   2) Zero. This means that we don't use private aliases for global variables
//      and can freely check ODR violation with the first method.
//
// This routine chooses between two different methods of ODR violation
// detection.
static inline bool UseODRIndicator(const Global *g) {
  return g->odr_indicator > 0;
}

// Register a global variable.
// This function may be called more than once for every global
// so we store the globals in a map.
static void RegisterGlobal(const Global *g) {
  CHECK(asan_inited);
  if (flags()->report_globals >= 2)
    ReportGlobal(*g, "Added");
  CHECK(flags()->report_globals);
  CHECK(AddrIsInMem(g->beg));
  if (!AddrIsAlignedByGranularity(g->beg)) {
    Report("The following global variable is not properly aligned.\n");
    Report("This may happen if another global with the same name\n");
    Report("resides in another non-instrumented module.\n");
    Report("Or the global comes from a C file built w/o -fno-common.\n");
    Report("In either case this is likely an ODR violation bug,\n");
    Report("but AddressSanitizer can not provide more details.\n");
    ReportODRViolation(g, FindRegistrationSite(g), g, FindRegistrationSite(g));
    CHECK(AddrIsAlignedByGranularity(g->beg));
  }
  CHECK(AddrIsAlignedByGranularity(g->size_with_redzone));
  if (flags()->detect_odr_violation) {
    // Try detecting ODR (One Definition Rule) violation, i.e. the situation
    // where two globals with the same name are defined in different modules.
    if (UseODRIndicator(g))
      CheckODRViolationViaIndicator(g);
    else
      CheckODRViolationViaPoisoning(g);
  }
  if (CanPoisonMemory())
    PoisonRedZones(*g);
  ListOfGlobals *l = new(allocator_for_globals) ListOfGlobals;
  l->g = g;
  l->next = list_of_all_globals;
  list_of_all_globals = l;
  if (g->has_dynamic_init) {
    if (!dynamic_init_globals) {
      dynamic_init_globals =
          new (allocator_for_globals) VectorOfGlobals;  // NOLINT
      dynamic_init_globals->reserve(kDynamicInitGlobalsInitialCapacity);
    }
    DynInitGlobal dyn_global = { *g, false };
    dynamic_init_globals->push_back(dyn_global);
  }
}

static void UnregisterGlobal(const Global *g) {
  CHECK(asan_inited);
  if (flags()->report_globals >= 2)
    ReportGlobal(*g, "Removed");
  CHECK(flags()->report_globals);
  CHECK(AddrIsInMem(g->beg));
  CHECK(AddrIsAlignedByGranularity(g->beg));
  CHECK(AddrIsAlignedByGranularity(g->size_with_redzone));
  if (CanPoisonMemory())
    PoisonShadowForGlobal(g, 0);
  // We unpoison the shadow memory for the global but we do not remove it from
  // the list because that would require O(n^2) time with the current list
  // implementation. It might not be worth doing anyway.

  // Release ODR indicator.
  if (UseODRIndicator(g) && g->odr_indicator != UINTPTR_MAX) {
    u8 *odr_indicator = reinterpret_cast<u8 *>(g->odr_indicator);
    *odr_indicator = UNREGISTERED;
  }
}

void StopInitOrderChecking() {
  BlockingMutexLock lock(&mu_for_globals);
  if (!flags()->check_initialization_order || !dynamic_init_globals)
    return;
  flags()->check_initialization_order = false;
  for (uptr i = 0, n = dynamic_init_globals->size(); i < n; ++i) {
    DynInitGlobal &dyn_g = (*dynamic_init_globals)[i];
    const Global *g = &dyn_g.g;
    // Unpoison the whole global.
    PoisonShadowForGlobal(g, 0);
    // Poison redzones back.
    PoisonRedZones(*g);
  }
}

static bool IsASCII(unsigned char c) { return /*0x00 <= c &&*/ c <= 0x7F; }

const char *MaybeDemangleGlobalName(const char *name) {
  // We can spoil names of globals with C linkage, so use an heuristic
  // approach to check if the name should be demangled.
  bool should_demangle = false;
  if (name[0] == '_' && name[1] == 'Z')
    should_demangle = true;
  else if (SANITIZER_WINDOWS && name[0] == '\01' && name[1] == '?')
    should_demangle = true;

  return should_demangle ? Symbolizer::GetOrInit()->Demangle(name) : name;
}

// Check if the global is a zero-terminated ASCII string. If so, print it.
void PrintGlobalNameIfASCII(InternalScopedString *str, const __asan_global &g) {
  for (uptr p = g.beg; p < g.beg + g.size - 1; p++) {
    unsigned char c = *(unsigned char *)p;
    if (c == '\0' || !IsASCII(c)) return;
  }
  if (*(char *)(g.beg + g.size - 1) != '\0') return;
  str->append("  '%s' is ascii string '%s'\n", MaybeDemangleGlobalName(g.name),
              (char *)g.beg);
}

static const char *GlobalFilename(const __asan_global &g) {
  const char *res = g.module_name;
  // Prefer the filename from source location, if is available.
  if (g.location) res = g.location->filename;
  CHECK(res);
  return res;
}

void PrintGlobalLocation(InternalScopedString *str, const __asan_global &g) {
  str->append("%s", GlobalFilename(g));
  if (!g.location) return;
  if (g.location->line_no) str->append(":%d", g.location->line_no);
  if (g.location->column_no) str->append(":%d", g.location->column_no);
}

} // namespace __asan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;  // NOLINT


// Apply __asan_register_globals to all globals found in the same loaded
// executable or shared library as `flag'. The flag tracks whether globals have
// already been registered or not for this image.
void __asan_register_image_globals(uptr *flag) {
  if (*flag)
    return;
  AsanApplyToGlobals(__asan_register_globals, flag);
  *flag = 1;
}

// This mirrors __asan_register_image_globals.
void __asan_unregister_image_globals(uptr *flag) {
  if (!*flag)
    return;
  AsanApplyToGlobals(__asan_unregister_globals, flag);
  *flag = 0;
}

void __asan_register_elf_globals(uptr *flag, void *start, void *stop) {
  if (*flag) return;
  if (!start) return;
  CHECK_EQ(0, ((uptr)stop - (uptr)start) % sizeof(__asan_global));
  __asan_global *globals_start = (__asan_global*)start;
  __asan_global *globals_stop = (__asan_global*)stop;
  __asan_register_globals(globals_start, globals_stop - globals_start);
  *flag = 1;
}

void __asan_unregister_elf_globals(uptr *flag, void *start, void *stop) {
  if (!*flag) return;
  if (!start) return;
  CHECK_EQ(0, ((uptr)stop - (uptr)start) % sizeof(__asan_global));
  __asan_global *globals_start = (__asan_global*)start;
  __asan_global *globals_stop = (__asan_global*)stop;
  __asan_unregister_globals(globals_start, globals_stop - globals_start);
  *flag = 0;
}

// Register an array of globals.
void __asan_register_globals(__asan_global *globals, uptr n) {
  if (!flags()->report_globals) return;
  GET_STACK_TRACE_MALLOC;
  u32 stack_id = StackDepotPut(stack);
  BlockingMutexLock lock(&mu_for_globals);
  if (!global_registration_site_vector) {
    global_registration_site_vector =
        new (allocator_for_globals) GlobalRegistrationSiteVector;  // NOLINT
    global_registration_site_vector->reserve(128);
  }
  GlobalRegistrationSite site = {stack_id, &globals[0], &globals[n - 1]};
  global_registration_site_vector->push_back(site);
  if (flags()->report_globals >= 2) {
    PRINT_CURRENT_STACK();
    Printf("=== ID %d; %p %p\n", stack_id, &globals[0], &globals[n - 1]);
  }
  for (uptr i = 0; i < n; i++) {
    if (SANITIZER_WINDOWS && globals[i].beg == 0) {
      // The MSVC incremental linker may pad globals out to 256 bytes. As long
      // as __asan_global is less than 256 bytes large and its size is a power
      // of two, we can skip over the padding.
      static_assert(
          sizeof(__asan_global) < 256 &&
              (sizeof(__asan_global) & (sizeof(__asan_global) - 1)) == 0,
          "sizeof(__asan_global) incompatible with incremental linker padding");
      // If these are padding bytes, the rest of the global should be zero.
      CHECK(globals[i].size == 0 && globals[i].size_with_redzone == 0 &&
            globals[i].name == nullptr && globals[i].module_name == nullptr &&
            globals[i].odr_indicator == 0);
      continue;
    }
    RegisterGlobal(&globals[i]);
  }

  // Poison the metadata. It should not be accessible to user code.
  PoisonShadow(reinterpret_cast<uptr>(globals), n * sizeof(__asan_global),
               kAsanGlobalRedzoneMagic);
}

// Unregister an array of globals.
// We must do this when a shared objects gets dlclosed.
void __asan_unregister_globals(__asan_global *globals, uptr n) {
  if (!flags()->report_globals) return;
  BlockingMutexLock lock(&mu_for_globals);
  for (uptr i = 0; i < n; i++) {
    if (SANITIZER_WINDOWS && globals[i].beg == 0) {
      // Skip globals that look like padding from the MSVC incremental linker.
      // See comment in __asan_register_globals.
      continue;
    }
    UnregisterGlobal(&globals[i]);
  }

  // Unpoison the metadata.
  PoisonShadow(reinterpret_cast<uptr>(globals), n * sizeof(__asan_global), 0);
}

// This method runs immediately prior to dynamic initialization in each TU,
// when all dynamically initialized globals are unpoisoned.  This method
// poisons all global variables not defined in this TU, so that a dynamic
// initializer can only touch global variables in the same TU.
void __asan_before_dynamic_init(const char *module_name) {
  if (!flags()->check_initialization_order ||
      !CanPoisonMemory() ||
      !dynamic_init_globals)
    return;
  bool strict_init_order = flags()->strict_init_order;
  CHECK(module_name);
  CHECK(asan_inited);
  BlockingMutexLock lock(&mu_for_globals);
  if (flags()->report_globals >= 3)
    Printf("DynInitPoison module: %s\n", module_name);
  for (uptr i = 0, n = dynamic_init_globals->size(); i < n; ++i) {
    DynInitGlobal &dyn_g = (*dynamic_init_globals)[i];
    const Global *g = &dyn_g.g;
    if (dyn_g.initialized)
      continue;
    if (g->module_name != module_name)
      PoisonShadowForGlobal(g, kAsanInitializationOrderMagic);
    else if (!strict_init_order)
      dyn_g.initialized = true;
  }
}

// This method runs immediately after dynamic initialization in each TU, when
// all dynamically initialized globals except for those defined in the current
// TU are poisoned.  It simply unpoisons all dynamically initialized globals.
void __asan_after_dynamic_init() {
  if (!flags()->check_initialization_order ||
      !CanPoisonMemory() ||
      !dynamic_init_globals)
    return;
  CHECK(asan_inited);
  BlockingMutexLock lock(&mu_for_globals);
  // FIXME: Optionally report that we're unpoisoning globals from a module.
  for (uptr i = 0, n = dynamic_init_globals->size(); i < n; ++i) {
    DynInitGlobal &dyn_g = (*dynamic_init_globals)[i];
    const Global *g = &dyn_g.g;
    if (!dyn_g.initialized) {
      // Unpoison the whole global.
      PoisonShadowForGlobal(g, 0);
      // Poison redzones back.
      PoisonRedZones(*g);
    }
  }
}
