//===-- sanitizer_common.cc -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_allocator_interface.h"
#include "sanitizer_allocator_internal.h"
#include "sanitizer_atomic.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"

namespace __sanitizer {

const char *SanitizerToolName = "SanitizerTool";

atomic_uint32_t current_verbosity;
uptr PageSizeCached;
u32 NumberOfCPUsCached;

// PID of the tracer task in StopTheWorld. It shares the address space with the
// main process, but has a different PID and thus requires special handling.
uptr stoptheworld_tracer_pid = 0;
// Cached pid of parent process - if the parent process dies, we want to keep
// writing to the same log file.
uptr stoptheworld_tracer_ppid = 0;

void NORETURN ReportMmapFailureAndDie(uptr size, const char *mem_type,
                                      const char *mmap_type, error_t err,
                                      bool raw_report) {
  static int recursion_count;
  if (SANITIZER_RTEMS || raw_report || recursion_count) {
    // If we are on RTEMS or raw report is requested or we went into recursion,
    // just die.  The Report() and CHECK calls below may call mmap recursively
    // and fail.
    RawWrite("ERROR: Failed to mmap\n");
    Die();
  }
  recursion_count++;
  Report("ERROR: %s failed to "
         "%s 0x%zx (%zd) bytes of %s (error code: %d)\n",
         SanitizerToolName, mmap_type, size, size, mem_type, err);
#if !SANITIZER_GO
  DumpProcessMap();
#endif
  UNREACHABLE("unable to mmap");
}

typedef bool UptrComparisonFunction(const uptr &a, const uptr &b);
typedef bool U32ComparisonFunction(const u32 &a, const u32 &b);

const char *StripPathPrefix(const char *filepath,
                            const char *strip_path_prefix) {
  if (!filepath) return nullptr;
  if (!strip_path_prefix) return filepath;
  const char *res = filepath;
  if (const char *pos = internal_strstr(filepath, strip_path_prefix))
    res = pos + internal_strlen(strip_path_prefix);
  if (res[0] == '.' && res[1] == '/')
    res += 2;
  return res;
}

const char *StripModuleName(const char *module) {
  if (!module)
    return nullptr;
  if (SANITIZER_WINDOWS) {
    // On Windows, both slash and backslash are possible.
    // Pick the one that goes last.
    if (const char *bslash_pos = internal_strrchr(module, '\\'))
      return StripModuleName(bslash_pos + 1);
  }
  if (const char *slash_pos = internal_strrchr(module, '/')) {
    return slash_pos + 1;
  }
  return module;
}

void ReportErrorSummary(const char *error_message, const char *alt_tool_name) {
  if (!common_flags()->print_summary)
    return;
  InternalScopedString buff(kMaxSummaryLength);
  buff.append("SUMMARY: %s: %s",
              alt_tool_name ? alt_tool_name : SanitizerToolName, error_message);
  __sanitizer_report_error_summary(buff.data());
}

// Removes the ANSI escape sequences from the input string (in-place).
void RemoveANSIEscapeSequencesFromString(char *str) {
  if (!str)
    return;

  // We are going to remove the escape sequences in place.
  char *s = str;
  char *z = str;
  while (*s != '\0') {
    CHECK_GE(s, z);
    // Skip over ANSI escape sequences with pointer 's'.
    if (*s == '\033' && *(s + 1) == '[') {
      s = internal_strchrnul(s, 'm');
      if (*s == '\0') {
        break;
      }
      s++;
      continue;
    }
    // 's' now points at a character we want to keep. Copy over the buffer
    // content if the escape sequence has been perviously skipped andadvance
    // both pointers.
    if (s != z)
      *z = *s;

    // If we have not seen an escape sequence, just advance both pointers.
    z++;
    s++;
  }

  // Null terminate the string.
  *z = '\0';
}

void LoadedModule::set(const char *module_name, uptr base_address) {
  clear();
  full_name_ = internal_strdup(module_name);
  base_address_ = base_address;
}

void LoadedModule::set(const char *module_name, uptr base_address,
                       ModuleArch arch, u8 uuid[kModuleUUIDSize],
                       bool instrumented) {
  set(module_name, base_address);
  arch_ = arch;
  internal_memcpy(uuid_, uuid, sizeof(uuid_));
  instrumented_ = instrumented;
}

void LoadedModule::clear() {
  InternalFree(full_name_);
  base_address_ = 0;
  max_executable_address_ = 0;
  full_name_ = nullptr;
  arch_ = kModuleArchUnknown;
  internal_memset(uuid_, 0, kModuleUUIDSize);
  instrumented_ = false;
  while (!ranges_.empty()) {
    AddressRange *r = ranges_.front();
    ranges_.pop_front();
    InternalFree(r);
  }
}

void LoadedModule::addAddressRange(uptr beg, uptr end, bool executable,
                                   bool writable, const char *name) {
  void *mem = InternalAlloc(sizeof(AddressRange));
  AddressRange *r =
      new(mem) AddressRange(beg, end, executable, writable, name);
  ranges_.push_back(r);
  if (executable && end > max_executable_address_)
    max_executable_address_ = end;
}

bool LoadedModule::containsAddress(uptr address) const {
  for (const AddressRange &r : ranges()) {
    if (r.beg <= address && address < r.end)
      return true;
  }
  return false;
}

static atomic_uintptr_t g_total_mmaped;

void IncreaseTotalMmap(uptr size) {
  if (!common_flags()->mmap_limit_mb) return;
  uptr total_mmaped =
      atomic_fetch_add(&g_total_mmaped, size, memory_order_relaxed) + size;
  // Since for now mmap_limit_mb is not a user-facing flag, just kill
  // a program. Use RAW_CHECK to avoid extra mmaps in reporting.
  RAW_CHECK((total_mmaped >> 20) < common_flags()->mmap_limit_mb);
}

void DecreaseTotalMmap(uptr size) {
  if (!common_flags()->mmap_limit_mb) return;
  atomic_fetch_sub(&g_total_mmaped, size, memory_order_relaxed);
}

bool TemplateMatch(const char *templ, const char *str) {
  if ((!str) || str[0] == 0)
    return false;
  bool start = false;
  if (templ && templ[0] == '^') {
    start = true;
    templ++;
  }
  bool asterisk = false;
  while (templ && templ[0]) {
    if (templ[0] == '*') {
      templ++;
      start = false;
      asterisk = true;
      continue;
    }
    if (templ[0] == '$')
      return str[0] == 0 || asterisk;
    if (str[0] == 0)
      return false;
    char *tpos = (char*)internal_strchr(templ, '*');
    char *tpos1 = (char*)internal_strchr(templ, '$');
    if ((!tpos) || (tpos1 && tpos1 < tpos))
      tpos = tpos1;
    if (tpos)
      tpos[0] = 0;
    const char *str0 = str;
    const char *spos = internal_strstr(str, templ);
    str = spos + internal_strlen(templ);
    templ = tpos;
    if (tpos)
      tpos[0] = tpos == tpos1 ? '$' : '*';
    if (!spos)
      return false;
    if (start && spos != str0)
      return false;
    start = false;
    asterisk = false;
  }
  return true;
}

static char binary_name_cache_str[kMaxPathLength];
static char process_name_cache_str[kMaxPathLength];

const char *GetProcessName() {
  return process_name_cache_str;
}

static uptr ReadProcessName(/*out*/ char *buf, uptr buf_len) {
  ReadLongProcessName(buf, buf_len);
  char *s = const_cast<char *>(StripModuleName(buf));
  uptr len = internal_strlen(s);
  if (s != buf) {
    internal_memmove(buf, s, len);
    buf[len] = '\0';
  }
  return len;
}

void UpdateProcessName() {
  ReadProcessName(process_name_cache_str, sizeof(process_name_cache_str));
}

// Call once to make sure that binary_name_cache_str is initialized
void CacheBinaryName() {
  if (binary_name_cache_str[0] != '\0')
    return;
  ReadBinaryName(binary_name_cache_str, sizeof(binary_name_cache_str));
  ReadProcessName(process_name_cache_str, sizeof(process_name_cache_str));
}

uptr ReadBinaryNameCached(/*out*/char *buf, uptr buf_len) {
  CacheBinaryName();
  uptr name_len = internal_strlen(binary_name_cache_str);
  name_len = (name_len < buf_len - 1) ? name_len : buf_len - 1;
  if (buf_len == 0)
    return 0;
  internal_memcpy(buf, binary_name_cache_str, name_len);
  buf[name_len] = '\0';
  return name_len;
}

void PrintCmdline() {
  char **argv = GetArgv();
  if (!argv) return;
  Printf("\nCommand: ");
  for (uptr i = 0; argv[i]; ++i)
    Printf("%s ", argv[i]);
  Printf("\n\n");
}

// Malloc hooks.
static const int kMaxMallocFreeHooks = 5;
struct MallocFreeHook {
  void (*malloc_hook)(const void *, uptr);
  void (*free_hook)(const void *);
};

static MallocFreeHook MFHooks[kMaxMallocFreeHooks];

void RunMallocHooks(const void *ptr, uptr size) {
  for (int i = 0; i < kMaxMallocFreeHooks; i++) {
    auto hook = MFHooks[i].malloc_hook;
    if (!hook) return;
    hook(ptr, size);
  }
}

void RunFreeHooks(const void *ptr) {
  for (int i = 0; i < kMaxMallocFreeHooks; i++) {
    auto hook = MFHooks[i].free_hook;
    if (!hook) return;
    hook(ptr);
  }
}

static int InstallMallocFreeHooks(void (*malloc_hook)(const void *, uptr),
                                  void (*free_hook)(const void *)) {
  if (!malloc_hook || !free_hook) return 0;
  for (int i = 0; i < kMaxMallocFreeHooks; i++) {
    if (MFHooks[i].malloc_hook == nullptr) {
      MFHooks[i].malloc_hook = malloc_hook;
      MFHooks[i].free_hook = free_hook;
      return i + 1;
    }
  }
  return 0;
}

} // namespace __sanitizer

using namespace __sanitizer;  // NOLINT

extern "C" {
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_report_error_summary,
                             const char *error_summary) {
  Printf("%s\n", error_summary);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_acquire_crash_state() {
  static atomic_uint8_t in_crash_state = {};
  return !atomic_exchange(&in_crash_state, 1, memory_order_relaxed);
}

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const void *,
                                                                  uptr),
                                              void (*free_hook)(const void *)) {
  return InstallMallocFreeHooks(malloc_hook, free_hook);
}
} // extern "C"
