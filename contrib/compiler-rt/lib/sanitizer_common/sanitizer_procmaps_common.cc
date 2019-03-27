//===-- sanitizer_procmaps_common.cc --------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (common parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD ||                \
    SANITIZER_OPENBSD || SANITIZER_SOLARIS

#include "sanitizer_common.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_procmaps.h"

namespace __sanitizer {

static ProcSelfMapsBuff cached_proc_self_maps;
static StaticSpinMutex cache_lock;

static int TranslateDigit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Parse a number and promote 'p' up to the first non-digit character.
static uptr ParseNumber(const char **p, int base) {
  uptr n = 0;
  int d;
  CHECK(base >= 2 && base <= 16);
  while ((d = TranslateDigit(**p)) >= 0 && d < base) {
    n = n * base + d;
    (*p)++;
  }
  return n;
}

bool IsDecimal(char c) {
  int d = TranslateDigit(c);
  return d >= 0 && d < 10;
}

uptr ParseDecimal(const char **p) {
  return ParseNumber(p, 10);
}

bool IsHex(char c) {
  int d = TranslateDigit(c);
  return d >= 0 && d < 16;
}

uptr ParseHex(const char **p) {
  return ParseNumber(p, 16);
}

void MemoryMappedSegment::AddAddressRanges(LoadedModule *module) {
  // data_ should be unused on this platform
  CHECK(!data_);
  module->addAddressRange(start, end, IsExecutable(), IsWritable());
}

MemoryMappingLayout::MemoryMappingLayout(bool cache_enabled) {
  // FIXME: in the future we may want to cache the mappings on demand only.
  if (cache_enabled)
    CacheMemoryMappings();

  // Read maps after the cache update to capture the maps/unmaps happening in
  // the process of updating.
  ReadProcMaps(&data_.proc_self_maps);
  if (cache_enabled && data_.proc_self_maps.mmaped_size == 0)
    LoadFromCache();

  Reset();
}

bool MemoryMappingLayout::Error() const {
  return data_.current == nullptr;
}

MemoryMappingLayout::~MemoryMappingLayout() {
  // Only unmap the buffer if it is different from the cached one. Otherwise
  // it will be unmapped when the cache is refreshed.
  if (data_.proc_self_maps.data != cached_proc_self_maps.data)
    UnmapOrDie(data_.proc_self_maps.data, data_.proc_self_maps.mmaped_size);
}

void MemoryMappingLayout::Reset() {
  data_.current = data_.proc_self_maps.data;
}

// static
void MemoryMappingLayout::CacheMemoryMappings() {
  ProcSelfMapsBuff new_proc_self_maps;
  ReadProcMaps(&new_proc_self_maps);
  // Don't invalidate the cache if the mappings are unavailable.
  if (new_proc_self_maps.mmaped_size == 0)
    return;
  SpinMutexLock l(&cache_lock);
  if (cached_proc_self_maps.mmaped_size)
    UnmapOrDie(cached_proc_self_maps.data, cached_proc_self_maps.mmaped_size);
  cached_proc_self_maps = new_proc_self_maps;
}

void MemoryMappingLayout::LoadFromCache() {
  SpinMutexLock l(&cache_lock);
  if (cached_proc_self_maps.data)
    data_.proc_self_maps = cached_proc_self_maps;
}

void MemoryMappingLayout::DumpListOfModules(
    InternalMmapVectorNoCtor<LoadedModule> *modules) {
  Reset();
  InternalScopedString module_name(kMaxPathLength);
  MemoryMappedSegment segment(module_name.data(), module_name.size());
  for (uptr i = 0; Next(&segment); i++) {
    const char *cur_name = segment.filename;
    if (cur_name[0] == '\0')
      continue;
    // Don't subtract 'cur_beg' from the first entry:
    // * If a binary is compiled w/o -pie, then the first entry in
    //   process maps is likely the binary itself (all dynamic libs
    //   are mapped higher in address space). For such a binary,
    //   instruction offset in binary coincides with the actual
    //   instruction address in virtual memory (as code section
    //   is mapped to a fixed memory range).
    // * If a binary is compiled with -pie, all the modules are
    //   mapped high at address space (in particular, higher than
    //   shadow memory of the tool), so the module can't be the
    //   first entry.
    uptr base_address = (i ? segment.start : 0) - segment.offset;
    LoadedModule cur_module;
    cur_module.set(cur_name, base_address);
    segment.AddAddressRanges(&cur_module);
    modules->push_back(cur_module);
  }
}

void GetMemoryProfile(fill_profile_f cb, uptr *stats, uptr stats_size) {
  char *smaps = nullptr;
  uptr smaps_cap = 0;
  uptr smaps_len = 0;
  if (!ReadFileToBuffer("/proc/self/smaps", &smaps, &smaps_cap, &smaps_len))
    return;
  uptr start = 0;
  bool file = false;
  const char *pos = smaps;
  while (pos < smaps + smaps_len) {
    if (IsHex(pos[0])) {
      start = ParseHex(&pos);
      for (; *pos != '/' && *pos > '\n'; pos++) {}
      file = *pos == '/';
    } else if (internal_strncmp(pos, "Rss:", 4) == 0) {
      while (!IsDecimal(*pos)) pos++;
      uptr rss = ParseDecimal(&pos) * 1024;
      cb(start, rss, file, stats, stats_size);
    }
    while (*pos++ != '\n') {}
  }
  UnmapOrDie(smaps, smaps_cap);
}

} // namespace __sanitizer

#endif
