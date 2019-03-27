//===-------- cfi.cc ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the runtime support for the cross-DSO CFI.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <elf.h>

#include "sanitizer_common/sanitizer_common.h"
#if SANITIZER_FREEBSD
#include <sys/link_elf.h>
#endif
#include <link.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#if SANITIZER_LINUX
typedef ElfW(Phdr) Elf_Phdr;
typedef ElfW(Ehdr) Elf_Ehdr;
typedef ElfW(Addr) Elf_Addr;
typedef ElfW(Sym) Elf_Sym;
typedef ElfW(Dyn) Elf_Dyn;
#elif SANITIZER_FREEBSD
#if SANITIZER_WORDSIZE == 64
#define ElfW64_Dyn Elf_Dyn
#define ElfW64_Sym Elf_Sym
#else
#define ElfW32_Dyn Elf_Dyn
#define ElfW32_Sym Elf_Sym
#endif
#endif

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "ubsan/ubsan_init.h"
#include "ubsan/ubsan_flags.h"

#ifdef CFI_ENABLE_DIAG
#include "ubsan/ubsan_handlers.h"
#endif

using namespace __sanitizer;

namespace __cfi {

#define kCfiShadowLimitsStorageSize 4096 // 1 page
// Lets hope that the data segment is mapped with 4K pages.
// The pointer to the cfi shadow region is stored at the start of this page.
// The rest of the page is unused and re-mapped read-only.
static union {
  char space[kCfiShadowLimitsStorageSize];
  struct {
    uptr start;
    uptr size;
  } limits;
} cfi_shadow_limits_storage
    __attribute__((aligned(kCfiShadowLimitsStorageSize)));
static constexpr uptr kShadowGranularity = 12;
static constexpr uptr kShadowAlign = 1UL << kShadowGranularity; // 4096

static constexpr uint16_t kInvalidShadow = 0;
static constexpr uint16_t kUncheckedShadow = 0xFFFFU;

// Get the start address of the CFI shadow region.
uptr GetShadow() {
  return cfi_shadow_limits_storage.limits.start;
}

uptr GetShadowSize() {
  return cfi_shadow_limits_storage.limits.size;
}

// This will only work while the shadow is not allocated.
void SetShadowSize(uptr size) {
  cfi_shadow_limits_storage.limits.size = size;
}

uptr MemToShadowOffset(uptr x) {
  return (x >> kShadowGranularity) << 1;
}

uint16_t *MemToShadow(uptr x, uptr shadow_base) {
  return (uint16_t *)(shadow_base + MemToShadowOffset(x));
}

typedef int (*CFICheckFn)(u64, void *, void *);

// This class reads and decodes the shadow contents.
class ShadowValue {
  uptr addr;
  uint16_t v;
  explicit ShadowValue(uptr addr, uint16_t v) : addr(addr), v(v) {}

public:
  bool is_invalid() const { return v == kInvalidShadow; }

  bool is_unchecked() const { return v == kUncheckedShadow; }

  CFICheckFn get_cfi_check() const {
    assert(!is_invalid() && !is_unchecked());
    uptr aligned_addr = addr & ~(kShadowAlign - 1);
    uptr p = aligned_addr - (((uptr)v - 1) << kShadowGranularity);
    return reinterpret_cast<CFICheckFn>(p);
  }

  // Load a shadow value for the given application memory address.
  static const ShadowValue load(uptr addr) {
    uptr shadow_base = GetShadow();
    uptr shadow_offset = MemToShadowOffset(addr);
    if (shadow_offset > GetShadowSize())
      return ShadowValue(addr, kInvalidShadow);
    else
      return ShadowValue(
          addr, *reinterpret_cast<uint16_t *>(shadow_base + shadow_offset));
  }
};

class ShadowBuilder {
  uptr shadow_;

public:
  // Allocate a new empty shadow (for the entire address space) on the side.
  void Start();
  // Mark the given address range as unchecked.
  // This is used for uninstrumented libraries like libc.
  // Any CFI check with a target in that range will pass.
  void AddUnchecked(uptr begin, uptr end);
  // Mark the given address range as belonging to a library with the given
  // cfi_check function.
  void Add(uptr begin, uptr end, uptr cfi_check);
  // Finish shadow construction. Atomically switch the current active shadow
  // region with the newly constructed one and deallocate the former.
  void Install();
};

void ShadowBuilder::Start() {
  shadow_ = (uptr)MmapNoReserveOrDie(GetShadowSize(), "CFI shadow");
  VReport(1, "CFI: shadow at %zx .. %zx\n", shadow_, shadow_ + GetShadowSize());
}

void ShadowBuilder::AddUnchecked(uptr begin, uptr end) {
  uint16_t *shadow_begin = MemToShadow(begin, shadow_);
  uint16_t *shadow_end = MemToShadow(end - 1, shadow_) + 1;
  // memset takes a byte, so our unchecked shadow value requires both bytes to
  // be the same. Make sure we're ok during compilation.
  static_assert((kUncheckedShadow & 0xff) == ((kUncheckedShadow >> 8) & 0xff),
                "Both bytes of the 16-bit value must be the same!");
  memset(shadow_begin, kUncheckedShadow & 0xff,
         (shadow_end - shadow_begin) * sizeof(*shadow_begin));
}

void ShadowBuilder::Add(uptr begin, uptr end, uptr cfi_check) {
  assert((cfi_check & (kShadowAlign - 1)) == 0);

  // Don't fill anything below cfi_check. We can not represent those addresses
  // in the shadow, and must make sure at codegen to place all valid call
  // targets above cfi_check.
  begin = Max(begin, cfi_check);
  uint16_t *s = MemToShadow(begin, shadow_);
  uint16_t *s_end = MemToShadow(end - 1, shadow_) + 1;
  uint16_t sv = ((begin - cfi_check) >> kShadowGranularity) + 1;
  for (; s < s_end; s++, sv++)
    *s = sv;
}

#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD
void ShadowBuilder::Install() {
  MprotectReadOnly(shadow_, GetShadowSize());
  uptr main_shadow = GetShadow();
  if (main_shadow) {
    // Update.
#if SANITIZER_LINUX
    void *res = mremap((void *)shadow_, GetShadowSize(), GetShadowSize(),
                       MREMAP_MAYMOVE | MREMAP_FIXED, (void *)main_shadow);
    CHECK(res != MAP_FAILED);
#elif SANITIZER_NETBSD
    void *res = mremap((void *)shadow_, GetShadowSize(), (void *)main_shadow,
                       GetShadowSize(), MAP_FIXED);
    CHECK(res != MAP_FAILED);
#else
    void *res = MmapFixedOrDie(shadow_, GetShadowSize());
    CHECK(res != MAP_FAILED);
    ::memcpy(&shadow_, &main_shadow, GetShadowSize());
#endif
  } else {
    // Initial setup.
    CHECK_EQ(kCfiShadowLimitsStorageSize, GetPageSizeCached());
    CHECK_EQ(0, GetShadow());
    cfi_shadow_limits_storage.limits.start = shadow_;
    MprotectReadOnly((uptr)&cfi_shadow_limits_storage,
                     sizeof(cfi_shadow_limits_storage));
    CHECK_EQ(shadow_, GetShadow());
  }
}
#else
#error not implemented
#endif

// This is a workaround for a glibc bug:
// https://sourceware.org/bugzilla/show_bug.cgi?id=15199
// Other platforms can, hopefully, just do
//    dlopen(RTLD_NOLOAD | RTLD_LAZY)
//    dlsym("__cfi_check").
uptr find_cfi_check_in_dso(dl_phdr_info *info) {
  const Elf_Dyn *dynamic = nullptr;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
      dynamic =
          (const Elf_Dyn *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
      break;
    }
  }
  if (!dynamic) return 0;
  uptr strtab = 0, symtab = 0, strsz = 0;
  for (const Elf_Dyn *p = dynamic; p->d_tag != PT_NULL; ++p) {
    if (p->d_tag == DT_SYMTAB)
      symtab = p->d_un.d_ptr;
    else if (p->d_tag == DT_STRTAB)
      strtab = p->d_un.d_ptr;
    else if (p->d_tag == DT_STRSZ)
      strsz = p->d_un.d_ptr;
  }

  if (symtab > strtab) {
    VReport(1, "Can not handle: symtab > strtab (%p > %zx)\n", symtab, strtab);
    return 0;
  }

  // Verify that strtab and symtab are inside of the same LOAD segment.
  // This excludes VDSO, which has (very high) bogus strtab and symtab pointers.
  int phdr_idx;
  for (phdr_idx = 0; phdr_idx < info->dlpi_phnum; phdr_idx++) {
    const Elf_Phdr *phdr = &info->dlpi_phdr[phdr_idx];
    if (phdr->p_type == PT_LOAD) {
      uptr beg = info->dlpi_addr + phdr->p_vaddr;
      uptr end = beg + phdr->p_memsz;
      if (strtab >= beg && strtab + strsz < end && symtab >= beg &&
          symtab < end)
        break;
    }
  }
  if (phdr_idx == info->dlpi_phnum) {
    // Nope, either different segments or just bogus pointers.
    // Can not handle this.
    VReport(1, "Can not handle: symtab %p, strtab %zx\n", symtab, strtab);
    return 0;
  }

  for (const Elf_Sym *p = (const Elf_Sym *)symtab; (Elf_Addr)p < strtab;
       ++p) {
    // There is no reliable way to find the end of the symbol table. In
    // lld-produces files, there are other sections between symtab and strtab.
    // Stop looking when the symbol name is not inside strtab.
    if (p->st_name >= strsz) break;
    char *name = (char*)(strtab + p->st_name);
    if (strcmp(name, "__cfi_check") == 0) {
      assert(p->st_info == ELF32_ST_INFO(STB_GLOBAL, STT_FUNC) ||
             p->st_info == ELF32_ST_INFO(STB_WEAK, STT_FUNC));
      uptr addr = info->dlpi_addr + p->st_value;
      return addr;
    }
  }
  return 0;
}

int dl_iterate_phdr_cb(dl_phdr_info *info, size_t size, void *data) {
  uptr cfi_check = find_cfi_check_in_dso(info);
  if (cfi_check)
    VReport(1, "Module '%s' __cfi_check %zx\n", info->dlpi_name, cfi_check);

  ShadowBuilder *b = reinterpret_cast<ShadowBuilder *>(data);

  for (int i = 0; i < info->dlpi_phnum; i++) {
    const Elf_Phdr *phdr = &info->dlpi_phdr[i];
    if (phdr->p_type == PT_LOAD) {
      // Jump tables are in the executable segment.
      // VTables are in the non-executable one.
      // Need to fill shadow for both.
      // FIXME: reject writable if vtables are in the r/o segment. Depend on
      // PT_RELRO?
      uptr cur_beg = info->dlpi_addr + phdr->p_vaddr;
      uptr cur_end = cur_beg + phdr->p_memsz;
      if (cfi_check) {
        VReport(1, "   %zx .. %zx\n", cur_beg, cur_end);
        b->Add(cur_beg, cur_end, cfi_check);
      } else {
        b->AddUnchecked(cur_beg, cur_end);
      }
    }
  }
  return 0;
}

// Init or update shadow for the current set of loaded libraries.
void UpdateShadow() {
  ShadowBuilder b;
  b.Start();
  dl_iterate_phdr(dl_iterate_phdr_cb, &b);
  b.Install();
}

void InitShadow() {
  CHECK_EQ(0, GetShadow());
  CHECK_EQ(0, GetShadowSize());

  uptr vma = GetMaxUserVirtualAddress();
  // Shadow is 2 -> 2**kShadowGranularity.
  SetShadowSize((vma >> (kShadowGranularity - 1)) + 1);
  VReport(1, "CFI: VMA size %zx, shadow size %zx\n", vma, GetShadowSize());

  UpdateShadow();
}

THREADLOCAL int in_loader;
BlockingMutex shadow_update_lock(LINKER_INITIALIZED);

void EnterLoader() {
  if (in_loader == 0) {
    shadow_update_lock.Lock();
  }
  ++in_loader;
}

void ExitLoader() {
  CHECK(in_loader > 0);
  --in_loader;
  UpdateShadow();
  if (in_loader == 0) {
    shadow_update_lock.Unlock();
  }
}

ALWAYS_INLINE void CfiSlowPathCommon(u64 CallSiteTypeId, void *Ptr,
                                     void *DiagData) {
  uptr Addr = (uptr)Ptr;
  VReport(3, "__cfi_slowpath: %llx, %p\n", CallSiteTypeId, Ptr);
  ShadowValue sv = ShadowValue::load(Addr);
  if (sv.is_invalid()) {
    VReport(1, "CFI: invalid memory region for a check target: %p\n", Ptr);
#ifdef CFI_ENABLE_DIAG
    if (DiagData) {
      __ubsan_handle_cfi_check_fail(
          reinterpret_cast<__ubsan::CFICheckFailData *>(DiagData), Addr, false);
      return;
    }
#endif
    Trap();
  }
  if (sv.is_unchecked()) {
    VReport(2, "CFI: unchecked call (shadow=FFFF): %p\n", Ptr);
    return;
  }
  CFICheckFn cfi_check = sv.get_cfi_check();
  VReport(2, "__cfi_check at %p\n", cfi_check);
  cfi_check(CallSiteTypeId, Ptr, DiagData);
}

void InitializeFlags() {
  SetCommonFlagsDefaults();
#ifdef CFI_ENABLE_DIAG
  __ubsan::Flags *uf = __ubsan::flags();
  uf->SetDefaults();
#endif

  FlagParser cfi_parser;
  RegisterCommonFlags(&cfi_parser);
  cfi_parser.ParseString(GetEnv("CFI_OPTIONS"));

#ifdef CFI_ENABLE_DIAG
  FlagParser ubsan_parser;
  __ubsan::RegisterUbsanFlags(&ubsan_parser, uf);
  RegisterCommonFlags(&ubsan_parser);

  const char *ubsan_default_options = __ubsan::MaybeCallUbsanDefaultOptions();
  ubsan_parser.ParseString(ubsan_default_options);
  ubsan_parser.ParseString(GetEnv("UBSAN_OPTIONS"));
#endif

  InitializeCommonFlags();

  if (Verbosity())
    ReportUnrecognizedFlags();

  if (common_flags()->help) {
    cfi_parser.PrintFlagDescriptions();
  }
}

} // namespace __cfi

using namespace __cfi;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__cfi_slowpath(u64 CallSiteTypeId, void *Ptr) {
  CfiSlowPathCommon(CallSiteTypeId, Ptr, nullptr);
}

#ifdef CFI_ENABLE_DIAG
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__cfi_slowpath_diag(u64 CallSiteTypeId, void *Ptr, void *DiagData) {
  CfiSlowPathCommon(CallSiteTypeId, Ptr, DiagData);
}
#endif

static void EnsureInterceptorsInitialized();

// Setup shadow for dlopen()ed libraries.
// The actual shadow setup happens after dlopen() returns, which means that
// a library can not be a target of any CFI checks while its constructors are
// running. It's unclear how to fix this without some extra help from libc.
// In glibc, mmap inside dlopen is not interceptable.
// Maybe a seccomp-bpf filter?
// We could insert a high-priority constructor into the library, but that would
// not help with the uninstrumented libraries.
INTERCEPTOR(void*, dlopen, const char *filename, int flag) {
  EnsureInterceptorsInitialized();
  EnterLoader();
  void *handle = REAL(dlopen)(filename, flag);
  ExitLoader();
  return handle;
}

INTERCEPTOR(int, dlclose, void *handle) {
  EnsureInterceptorsInitialized();
  EnterLoader();
  int res = REAL(dlclose)(handle);
  ExitLoader();
  return res;
}

static BlockingMutex interceptor_init_lock(LINKER_INITIALIZED);
static bool interceptors_inited = false;

static void EnsureInterceptorsInitialized() {
  BlockingMutexLock lock(&interceptor_init_lock);
  if (interceptors_inited)
    return;

  INTERCEPT_FUNCTION(dlopen);
  INTERCEPT_FUNCTION(dlclose);

  interceptors_inited = true;
}

extern "C" SANITIZER_INTERFACE_ATTRIBUTE
#if !SANITIZER_CAN_USE_PREINIT_ARRAY
// On ELF platforms, the constructor is invoked using .preinit_array (see below)
__attribute__((constructor(0)))
#endif
void __cfi_init() {
  SanitizerToolName = "CFI";
  InitializeFlags();
  InitShadow();

#ifdef CFI_ENABLE_DIAG
  __ubsan::InitAsPlugin();
#endif
}

#if SANITIZER_CAN_USE_PREINIT_ARRAY
// On ELF platforms, run cfi initialization before any other constructors.
// On other platforms we use the constructor attribute to arrange to run our
// initialization early.
extern "C" {
__attribute__((section(".preinit_array"),
               used)) void (*__cfi_preinit)(void) = __cfi_init;
}
#endif
