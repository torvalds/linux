//=-- lsan_common_mac.cc --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Implementation of common leak checking functionality. Darwin-specific code.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "lsan_common.h"

#if CAN_SANITIZE_LEAKS && SANITIZER_MAC

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "lsan_allocator.h"

#include <pthread.h>

#include <mach/mach.h>

// Only introduced in Mac OS X 10.9.
#ifdef VM_MEMORY_OS_ALLOC_ONCE
static const int kSanitizerVmMemoryOsAllocOnce = VM_MEMORY_OS_ALLOC_ONCE;
#else
static const int kSanitizerVmMemoryOsAllocOnce = 73;
#endif

namespace __lsan {

typedef struct {
  int disable_counter;
  u32 current_thread_id;
  AllocatorCache cache;
} thread_local_data_t;

static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

// The main thread destructor requires the current thread id,
// so we can't destroy it until it's been used and reset to invalid tid
void restore_tid_data(void *ptr) {
  thread_local_data_t *data = (thread_local_data_t *)ptr;
  if (data->current_thread_id != kInvalidTid)
    pthread_setspecific(key, data);
}

static void make_tls_key() {
  CHECK_EQ(pthread_key_create(&key, restore_tid_data), 0);
}

static thread_local_data_t *get_tls_val(bool alloc) {
  pthread_once(&key_once, make_tls_key);

  thread_local_data_t *ptr = (thread_local_data_t *)pthread_getspecific(key);
  if (ptr == NULL && alloc) {
    ptr = (thread_local_data_t *)InternalAlloc(sizeof(*ptr));
    ptr->disable_counter = 0;
    ptr->current_thread_id = kInvalidTid;
    ptr->cache = AllocatorCache();
    pthread_setspecific(key, ptr);
  }

  return ptr;
}

bool DisabledInThisThread() {
  thread_local_data_t *data = get_tls_val(false);
  return data ? data->disable_counter > 0 : false;
}

void DisableInThisThread() { ++get_tls_val(true)->disable_counter; }

void EnableInThisThread() {
  int *disable_counter = &get_tls_val(true)->disable_counter;
  if (*disable_counter == 0) {
    DisableCounterUnderflow();
  }
  --*disable_counter;
}

u32 GetCurrentThread() {
  thread_local_data_t *data = get_tls_val(false);
  return data ? data->current_thread_id : kInvalidTid;
}

void SetCurrentThread(u32 tid) { get_tls_val(true)->current_thread_id = tid; }

AllocatorCache *GetAllocatorCache() { return &get_tls_val(true)->cache; }

LoadedModule *GetLinker() { return nullptr; }

// Required on Linux for initialization of TLS behavior, but should not be
// required on Darwin.
void InitializePlatformSpecificModules() {}

// Sections which can't contain contain global pointers. This list errs on the
// side of caution to avoid false positives, at the expense of performance.
//
// Other potentially safe sections include:
// __all_image_info, __crash_info, __const, __got, __interpose, __objc_msg_break
//
// Sections which definitely cannot be included here are:
// __objc_data, __objc_const, __data, __bss, __common, __thread_data,
// __thread_bss, __thread_vars, __objc_opt_rw, __objc_opt_ptrs
static const char *kSkippedSecNames[] = {
    "__cfstring",       "__la_symbol_ptr",  "__mod_init_func",
    "__mod_term_func",  "__nl_symbol_ptr",  "__objc_classlist",
    "__objc_classrefs", "__objc_imageinfo", "__objc_nlclslist",
    "__objc_protolist", "__objc_selrefs",   "__objc_superrefs"};

// Scans global variables for heap pointers.
void ProcessGlobalRegions(Frontier *frontier) {
  for (auto name : kSkippedSecNames)
    CHECK(internal_strnlen(name, kMaxSegName + 1) <= kMaxSegName);

  MemoryMappingLayout memory_mapping(false);
  InternalMmapVector<LoadedModule> modules;
  modules.reserve(128);
  memory_mapping.DumpListOfModules(&modules);
  for (uptr i = 0; i < modules.size(); ++i) {
    // Even when global scanning is disabled, we still need to scan
    // system libraries for stashed pointers
    if (!flags()->use_globals && modules[i].instrumented()) continue;

    for (const __sanitizer::LoadedModule::AddressRange &range :
         modules[i].ranges()) {
      // Sections storing global variables are writable and non-executable
      if (range.executable || !range.writable) continue;

      for (auto name : kSkippedSecNames) {
        if (!internal_strcmp(range.name, name)) continue;
      }

      ScanGlobalRange(range.beg, range.end, frontier);
    }
  }
}

void ProcessPlatformSpecificAllocations(Frontier *frontier) {
  unsigned depth = 1;
  vm_size_t size = 0;
  vm_address_t address = 0;
  kern_return_t err = KERN_SUCCESS;
  mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;

  InternalMmapVector<RootRegion> const *root_regions = GetRootRegions();

  while (err == KERN_SUCCESS) {
    struct vm_region_submap_info_64 info;
    err = vm_region_recurse_64(mach_task_self(), &address, &size, &depth,
                               (vm_region_info_t)&info, &count);

    uptr end_address = address + size;

    // libxpc stashes some pointers in the Kernel Alloc Once page,
    // make sure not to report those as leaks.
    if (info.user_tag == kSanitizerVmMemoryOsAllocOnce) {
      ScanRangeForPointers(address, end_address, frontier, "GLOBAL",
                           kReachable);

      // Recursing over the full memory map is very slow, break out
      // early if we don't need the full iteration.
      if (!flags()->use_root_regions || !root_regions->size())
        break;
    }

    // This additional root region scan is required on Darwin in order to
    // detect root regions contained within mmap'd memory regions, because
    // the Darwin implementation of sanitizer_procmaps traverses images
    // as loaded by dyld, and not the complete set of all memory regions.
    //
    // TODO(fjricci) - remove this once sanitizer_procmaps_mac has the same
    // behavior as sanitizer_procmaps_linux and traverses all memory regions
    if (flags()->use_root_regions) {
      for (uptr i = 0; i < root_regions->size(); i++) {
        ScanRootRegion(frontier, (*root_regions)[i], address, end_address,
                       info.protection & kProtectionRead);
      }
    }

    address = end_address;
  }
}

// On darwin, we can intercept _exit gracefully, and return a failing exit code
// if required at that point. Calling Die() here is undefined behavior and
// causes rare race conditions.
void HandleLeaks() {}

void DoStopTheWorld(StopTheWorldCallback callback, void *argument) {
  StopTheWorld(callback, argument);
}

} // namespace __lsan

#endif // CAN_SANITIZE_LEAKS && SANITIZER_MAC
