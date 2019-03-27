//===-- sanitizer_linux_libcdep.cc ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements linux-specific functions from
// sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD ||                \
    SANITIZER_OPENBSD || SANITIZER_SOLARIS

#include "sanitizer_allocator_internal.h"
#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_file.h"
#include "sanitizer_flags.h"
#include "sanitizer_freebsd.h"
#include "sanitizer_getauxval.h"
#include "sanitizer_linux.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_procmaps.h"

#include <dlfcn.h>  // for dlsym()
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <syslog.h>

#if SANITIZER_FREEBSD
#include <pthread_np.h>
#include <osreldate.h>
#include <sys/sysctl.h>
#define pthread_getattr_np pthread_attr_get_np
#endif

#if SANITIZER_OPENBSD
#include <pthread_np.h>
#include <sys/sysctl.h>
#endif

#if SANITIZER_NETBSD
#include <sys/sysctl.h>
#include <sys/tls.h>
#endif

#if SANITIZER_SOLARIS
#include <thread.h>
#endif

#if SANITIZER_ANDROID
#include <android/api-level.h>
#if !defined(CPU_COUNT) && !defined(__aarch64__)
#include <dirent.h>
#include <fcntl.h>
struct __sanitizer::linux_dirent {
  long           d_ino;
  off_t          d_off;
  unsigned short d_reclen;
  char           d_name[];
};
#endif
#endif

#if !SANITIZER_ANDROID
#include <elf.h>
#include <unistd.h>
#endif

namespace __sanitizer {

SANITIZER_WEAK_ATTRIBUTE int
real_sigaction(int signum, const void *act, void *oldact);

int internal_sigaction(int signum, const void *act, void *oldact) {
#if !SANITIZER_GO
  if (&real_sigaction)
    return real_sigaction(signum, act, oldact);
#endif
  return sigaction(signum, (const struct sigaction *)act,
                   (struct sigaction *)oldact);
}

void GetThreadStackTopAndBottom(bool at_initialization, uptr *stack_top,
                                uptr *stack_bottom) {
  CHECK(stack_top);
  CHECK(stack_bottom);
  if (at_initialization) {
    // This is the main thread. Libpthread may not be initialized yet.
    struct rlimit rl;
    CHECK_EQ(getrlimit(RLIMIT_STACK, &rl), 0);

    // Find the mapping that contains a stack variable.
    MemoryMappingLayout proc_maps(/*cache_enabled*/true);
    if (proc_maps.Error()) {
      *stack_top = *stack_bottom = 0;
      return;
    }
    MemoryMappedSegment segment;
    uptr prev_end = 0;
    while (proc_maps.Next(&segment)) {
      if ((uptr)&rl < segment.end) break;
      prev_end = segment.end;
    }
    CHECK((uptr)&rl >= segment.start && (uptr)&rl < segment.end);

    // Get stacksize from rlimit, but clip it so that it does not overlap
    // with other mappings.
    uptr stacksize = rl.rlim_cur;
    if (stacksize > segment.end - prev_end) stacksize = segment.end - prev_end;
    // When running with unlimited stack size, we still want to set some limit.
    // The unlimited stack size is caused by 'ulimit -s unlimited'.
    // Also, for some reason, GNU make spawns subprocesses with unlimited stack.
    if (stacksize > kMaxThreadStackSize)
      stacksize = kMaxThreadStackSize;
    *stack_top = segment.end;
    *stack_bottom = segment.end - stacksize;
    return;
  }
  uptr stacksize = 0;
  void *stackaddr = nullptr;
#if SANITIZER_SOLARIS
  stack_t ss;
  CHECK_EQ(thr_stksegment(&ss), 0);
  stacksize = ss.ss_size;
  stackaddr = (char *)ss.ss_sp - stacksize;
#elif SANITIZER_OPENBSD
  stack_t sattr;
  CHECK_EQ(pthread_stackseg_np(pthread_self(), &sattr), 0);
  stackaddr = sattr.ss_sp;
  stacksize = sattr.ss_size;
#else  // !SANITIZER_SOLARIS
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  CHECK_EQ(pthread_getattr_np(pthread_self(), &attr), 0);
  my_pthread_attr_getstack(&attr, &stackaddr, &stacksize);
  pthread_attr_destroy(&attr);
#endif // SANITIZER_SOLARIS

  *stack_top = (uptr)stackaddr + stacksize;
  *stack_bottom = (uptr)stackaddr;
}

#if !SANITIZER_GO
bool SetEnv(const char *name, const char *value) {
  void *f = dlsym(RTLD_NEXT, "setenv");
  if (!f)
    return false;
  typedef int(*setenv_ft)(const char *name, const char *value, int overwrite);
  setenv_ft setenv_f;
  CHECK_EQ(sizeof(setenv_f), sizeof(f));
  internal_memcpy(&setenv_f, &f, sizeof(f));
  return setenv_f(name, value, 1) == 0;
}
#endif

__attribute__((unused)) static bool GetLibcVersion(int *major, int *minor,
                                                   int *patch) {
#ifdef _CS_GNU_LIBC_VERSION
  char buf[64];
  uptr len = confstr(_CS_GNU_LIBC_VERSION, buf, sizeof(buf));
  if (len >= sizeof(buf))
    return false;
  buf[len] = 0;
  static const char kGLibC[] = "glibc ";
  if (internal_strncmp(buf, kGLibC, sizeof(kGLibC) - 1) != 0)
    return false;
  const char *p = buf + sizeof(kGLibC) - 1;
  *major = internal_simple_strtoll(p, &p, 10);
  *minor = (*p == '.') ? internal_simple_strtoll(p + 1, &p, 10) : 0;
  *patch = (*p == '.') ? internal_simple_strtoll(p + 1, &p, 10) : 0;
  return true;
#else
  return false;
#endif
}

#if !SANITIZER_FREEBSD && !SANITIZER_ANDROID && !SANITIZER_GO &&               \
    !SANITIZER_NETBSD && !SANITIZER_OPENBSD && !SANITIZER_SOLARIS
static uptr g_tls_size;

#ifdef __i386__
# ifndef __GLIBC_PREREQ
#  define CHECK_GET_TLS_STATIC_INFO_VERSION 1
# else
#  define CHECK_GET_TLS_STATIC_INFO_VERSION (!__GLIBC_PREREQ(2, 27))
# endif
#else
# define CHECK_GET_TLS_STATIC_INFO_VERSION 0
#endif

#if CHECK_GET_TLS_STATIC_INFO_VERSION
# define DL_INTERNAL_FUNCTION __attribute__((regparm(3), stdcall))
#else
# define DL_INTERNAL_FUNCTION
#endif

namespace {
struct GetTlsStaticInfoCall {
  typedef void (*get_tls_func)(size_t*, size_t*);
};
struct GetTlsStaticInfoRegparmCall {
  typedef void (*get_tls_func)(size_t*, size_t*) DL_INTERNAL_FUNCTION;
};

template <typename T>
void CallGetTls(void* ptr, size_t* size, size_t* align) {
  typename T::get_tls_func get_tls;
  CHECK_EQ(sizeof(get_tls), sizeof(ptr));
  internal_memcpy(&get_tls, &ptr, sizeof(ptr));
  CHECK_NE(get_tls, 0);
  get_tls(size, align);
}

bool CmpLibcVersion(int major, int minor, int patch) {
  int ma;
  int mi;
  int pa;
  if (!GetLibcVersion(&ma, &mi, &pa))
    return false;
  if (ma > major)
    return true;
  if (ma < major)
    return false;
  if (mi > minor)
    return true;
  if (mi < minor)
    return false;
  return pa >= patch;
}

}  // namespace

void InitTlsSize() {
  // all current supported platforms have 16 bytes stack alignment
  const size_t kStackAlign = 16;
  void *get_tls_static_info_ptr = dlsym(RTLD_NEXT, "_dl_get_tls_static_info");
  size_t tls_size = 0;
  size_t tls_align = 0;
  // On i?86, _dl_get_tls_static_info used to be internal_function, i.e.
  // __attribute__((regparm(3), stdcall)) before glibc 2.27 and is normal
  // function in 2.27 and later.
  if (CHECK_GET_TLS_STATIC_INFO_VERSION && !CmpLibcVersion(2, 27, 0))
    CallGetTls<GetTlsStaticInfoRegparmCall>(get_tls_static_info_ptr,
                                            &tls_size, &tls_align);
  else
    CallGetTls<GetTlsStaticInfoCall>(get_tls_static_info_ptr,
                                     &tls_size, &tls_align);
  if (tls_align < kStackAlign)
    tls_align = kStackAlign;
  g_tls_size = RoundUpTo(tls_size, tls_align);
}
#else
void InitTlsSize() { }
#endif  // !SANITIZER_FREEBSD && !SANITIZER_ANDROID && !SANITIZER_GO &&
        // !SANITIZER_NETBSD && !SANITIZER_SOLARIS

#if (defined(__x86_64__) || defined(__i386__) || defined(__mips__) ||          \
     defined(__aarch64__) || defined(__powerpc64__) || defined(__s390__) ||    \
     defined(__arm__)) &&                                                      \
    SANITIZER_LINUX && !SANITIZER_ANDROID
// sizeof(struct pthread) from glibc.
static atomic_uintptr_t thread_descriptor_size;

uptr ThreadDescriptorSize() {
  uptr val = atomic_load_relaxed(&thread_descriptor_size);
  if (val)
    return val;
#if defined(__x86_64__) || defined(__i386__) || defined(__arm__)
  int major;
  int minor;
  int patch;
  if (GetLibcVersion(&major, &minor, &patch) && major == 2) {
    /* sizeof(struct pthread) values from various glibc versions.  */
    if (SANITIZER_X32)
      val = 1728; // Assume only one particular version for x32.
    // For ARM sizeof(struct pthread) changed in Glibc 2.23.
    else if (SANITIZER_ARM)
      val = minor <= 22 ? 1120 : 1216;
    else if (minor <= 3)
      val = FIRST_32_SECOND_64(1104, 1696);
    else if (minor == 4)
      val = FIRST_32_SECOND_64(1120, 1728);
    else if (minor == 5)
      val = FIRST_32_SECOND_64(1136, 1728);
    else if (minor <= 9)
      val = FIRST_32_SECOND_64(1136, 1712);
    else if (minor == 10)
      val = FIRST_32_SECOND_64(1168, 1776);
    else if (minor == 11 || (minor == 12 && patch == 1))
      val = FIRST_32_SECOND_64(1168, 2288);
    else if (minor <= 14)
      val = FIRST_32_SECOND_64(1168, 2304);
    else
      val = FIRST_32_SECOND_64(1216, 2304);
  }
#elif defined(__mips__)
  // TODO(sagarthakur): add more values as per different glibc versions.
  val = FIRST_32_SECOND_64(1152, 1776);
#elif defined(__aarch64__)
  // The sizeof (struct pthread) is the same from GLIBC 2.17 to 2.22.
  val = 1776;
#elif defined(__powerpc64__)
  val = 1776; // from glibc.ppc64le 2.20-8.fc21
#elif defined(__s390__)
  val = FIRST_32_SECOND_64(1152, 1776); // valid for glibc 2.22
#endif
  if (val)
    atomic_store_relaxed(&thread_descriptor_size, val);
  return val;
}

// The offset at which pointer to self is located in the thread descriptor.
const uptr kThreadSelfOffset = FIRST_32_SECOND_64(8, 16);

uptr ThreadSelfOffset() {
  return kThreadSelfOffset;
}

#if defined(__mips__) || defined(__powerpc64__)
// TlsPreTcbSize includes size of struct pthread_descr and size of tcb
// head structure. It lies before the static tls blocks.
static uptr TlsPreTcbSize() {
# if defined(__mips__)
  const uptr kTcbHead = 16; // sizeof (tcbhead_t)
# elif defined(__powerpc64__)
  const uptr kTcbHead = 88; // sizeof (tcbhead_t)
# endif
  const uptr kTlsAlign = 16;
  const uptr kTlsPreTcbSize =
      RoundUpTo(ThreadDescriptorSize() + kTcbHead, kTlsAlign);
  return kTlsPreTcbSize;
}
#endif

uptr ThreadSelf() {
  uptr descr_addr;
# if defined(__i386__)
  asm("mov %%gs:%c1,%0" : "=r"(descr_addr) : "i"(kThreadSelfOffset));
# elif defined(__x86_64__)
  asm("mov %%fs:%c1,%0" : "=r"(descr_addr) : "i"(kThreadSelfOffset));
# elif defined(__mips__)
  // MIPS uses TLS variant I. The thread pointer (in hardware register $29)
  // points to the end of the TCB + 0x7000. The pthread_descr structure is
  // immediately in front of the TCB. TlsPreTcbSize() includes the size of the
  // TCB and the size of pthread_descr.
  const uptr kTlsTcbOffset = 0x7000;
  uptr thread_pointer;
  asm volatile(".set push;\
                .set mips64r2;\
                rdhwr %0,$29;\
                .set pop" : "=r" (thread_pointer));
  descr_addr = thread_pointer - kTlsTcbOffset - TlsPreTcbSize();
# elif defined(__aarch64__) || defined(__arm__)
  descr_addr = reinterpret_cast<uptr>(__builtin_thread_pointer()) -
                                      ThreadDescriptorSize();
# elif defined(__s390__)
  descr_addr = reinterpret_cast<uptr>(__builtin_thread_pointer());
# elif defined(__powerpc64__)
  // PPC64LE uses TLS variant I. The thread pointer (in GPR 13)
  // points to the end of the TCB + 0x7000. The pthread_descr structure is
  // immediately in front of the TCB. TlsPreTcbSize() includes the size of the
  // TCB and the size of pthread_descr.
  const uptr kTlsTcbOffset = 0x7000;
  uptr thread_pointer;
  asm("addi %0,13,%1" : "=r"(thread_pointer) : "I"(-kTlsTcbOffset));
  descr_addr = thread_pointer - TlsPreTcbSize();
# else
#  error "unsupported CPU arch"
# endif
  return descr_addr;
}
#endif  // (x86_64 || i386 || MIPS) && SANITIZER_LINUX

#if SANITIZER_FREEBSD
static void **ThreadSelfSegbase() {
  void **segbase = 0;
# if defined(__i386__)
  // sysarch(I386_GET_GSBASE, segbase);
  __asm __volatile("mov %%gs:0, %0" : "=r" (segbase));
# elif defined(__x86_64__)
  // sysarch(AMD64_GET_FSBASE, segbase);
  __asm __volatile("movq %%fs:0, %0" : "=r" (segbase));
# else
#  error "unsupported CPU arch"
# endif
  return segbase;
}

uptr ThreadSelf() {
  return (uptr)ThreadSelfSegbase()[2];
}
#endif  // SANITIZER_FREEBSD

#if SANITIZER_NETBSD
static struct tls_tcb * ThreadSelfTlsTcb() {
  struct tls_tcb * tcb;
# ifdef __HAVE___LWP_GETTCB_FAST
  tcb = (struct tls_tcb *)__lwp_gettcb_fast();
# elif defined(__HAVE___LWP_GETPRIVATE_FAST)
  tcb = (struct tls_tcb *)__lwp_getprivate_fast();
# endif
  return tcb;
}

uptr ThreadSelf() {
  return (uptr)ThreadSelfTlsTcb()->tcb_pthread;
}

int GetSizeFromHdr(struct dl_phdr_info *info, size_t size, void *data) {
  const Elf_Phdr *hdr = info->dlpi_phdr;
  const Elf_Phdr *last_hdr = hdr + info->dlpi_phnum;

  for (; hdr != last_hdr; ++hdr) {
    if (hdr->p_type == PT_TLS && info->dlpi_tls_modid == 1) {
      *(uptr*)data = hdr->p_memsz;
      break;
    }
  }
  return 0;
}
#endif  // SANITIZER_NETBSD

#if !SANITIZER_GO
static void GetTls(uptr *addr, uptr *size) {
#if SANITIZER_LINUX && !SANITIZER_ANDROID
# if defined(__x86_64__) || defined(__i386__) || defined(__s390__)
  *addr = ThreadSelf();
  *size = GetTlsSize();
  *addr -= *size;
  *addr += ThreadDescriptorSize();
# elif defined(__mips__) || defined(__aarch64__) || defined(__powerpc64__) \
    || defined(__arm__)
  *addr = ThreadSelf();
  *size = GetTlsSize();
# else
  *addr = 0;
  *size = 0;
# endif
#elif SANITIZER_FREEBSD
  void** segbase = ThreadSelfSegbase();
  *addr = 0;
  *size = 0;
  if (segbase != 0) {
    // tcbalign = 16
    // tls_size = round(tls_static_space, tcbalign);
    // dtv = segbase[1];
    // dtv[2] = segbase - tls_static_space;
    void **dtv = (void**) segbase[1];
    *addr = (uptr) dtv[2];
    *size = (*addr == 0) ? 0 : ((uptr) segbase[0] - (uptr) dtv[2]);
  }
#elif SANITIZER_NETBSD
  struct tls_tcb * const tcb = ThreadSelfTlsTcb();
  *addr = 0;
  *size = 0;
  if (tcb != 0) {
    // Find size (p_memsz) of dlpi_tls_modid 1 (TLS block of the main program).
    // ld.elf_so hardcodes the index 1.
    dl_iterate_phdr(GetSizeFromHdr, size);

    if (*size != 0) {
      // The block has been found and tcb_dtv[1] contains the base address
      *addr = (uptr)tcb->tcb_dtv[1];
    }
  }
#elif SANITIZER_OPENBSD
  *addr = 0;
  *size = 0;
#elif SANITIZER_ANDROID
  *addr = 0;
  *size = 0;
#elif SANITIZER_SOLARIS
  // FIXME
  *addr = 0;
  *size = 0;
#else
# error "Unknown OS"
#endif
}
#endif

#if !SANITIZER_GO
uptr GetTlsSize() {
#if SANITIZER_FREEBSD || SANITIZER_ANDROID || SANITIZER_NETBSD ||              \
    SANITIZER_OPENBSD || SANITIZER_SOLARIS
  uptr addr, size;
  GetTls(&addr, &size);
  return size;
#elif defined(__mips__) || defined(__powerpc64__)
  return RoundUpTo(g_tls_size + TlsPreTcbSize(), 16);
#else
  return g_tls_size;
#endif
}
#endif

void GetThreadStackAndTls(bool main, uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
#if SANITIZER_GO
  // Stub implementation for Go.
  *stk_addr = *stk_size = *tls_addr = *tls_size = 0;
#else
  GetTls(tls_addr, tls_size);

  uptr stack_top, stack_bottom;
  GetThreadStackTopAndBottom(main, &stack_top, &stack_bottom);
  *stk_addr = stack_bottom;
  *stk_size = stack_top - stack_bottom;

  if (!main) {
    // If stack and tls intersect, make them non-intersecting.
    if (*tls_addr > *stk_addr && *tls_addr < *stk_addr + *stk_size) {
      CHECK_GT(*tls_addr + *tls_size, *stk_addr);
      CHECK_LE(*tls_addr + *tls_size, *stk_addr + *stk_size);
      *stk_size -= *tls_size;
      *tls_addr = *stk_addr + *stk_size;
    }
  }
#endif
}

#if !SANITIZER_FREEBSD && !SANITIZER_OPENBSD
typedef ElfW(Phdr) Elf_Phdr;
#elif SANITIZER_WORDSIZE == 32 && __FreeBSD_version <= 902001 // v9.2
#define Elf_Phdr XElf32_Phdr
#define dl_phdr_info xdl_phdr_info
#define dl_iterate_phdr(c, b) xdl_iterate_phdr((c), (b))
#endif // !SANITIZER_FREEBSD && !SANITIZER_OPENBSD

struct DlIteratePhdrData {
  InternalMmapVectorNoCtor<LoadedModule> *modules;
  bool first;
};

static int dl_iterate_phdr_cb(dl_phdr_info *info, size_t size, void *arg) {
  DlIteratePhdrData *data = (DlIteratePhdrData*)arg;
  InternalScopedString module_name(kMaxPathLength);
  if (data->first) {
    data->first = false;
    // First module is the binary itself.
    ReadBinaryNameCached(module_name.data(), module_name.size());
  } else if (info->dlpi_name) {
    module_name.append("%s", info->dlpi_name);
  }
  if (module_name[0] == '\0')
    return 0;
  LoadedModule cur_module;
  cur_module.set(module_name.data(), info->dlpi_addr);
  for (int i = 0; i < (int)info->dlpi_phnum; i++) {
    const Elf_Phdr *phdr = &info->dlpi_phdr[i];
    if (phdr->p_type == PT_LOAD) {
      uptr cur_beg = info->dlpi_addr + phdr->p_vaddr;
      uptr cur_end = cur_beg + phdr->p_memsz;
      bool executable = phdr->p_flags & PF_X;
      bool writable = phdr->p_flags & PF_W;
      cur_module.addAddressRange(cur_beg, cur_end, executable,
                                 writable);
    }
  }
  data->modules->push_back(cur_module);
  return 0;
}

#if SANITIZER_ANDROID && __ANDROID_API__ < 21
extern "C" __attribute__((weak)) int dl_iterate_phdr(
    int (*)(struct dl_phdr_info *, size_t, void *), void *);
#endif

static bool requiresProcmaps() {
#if SANITIZER_ANDROID && __ANDROID_API__ <= 22
  // Fall back to /proc/maps if dl_iterate_phdr is unavailable or broken.
  // The runtime check allows the same library to work with
  // both K and L (and future) Android releases.
  return AndroidGetApiLevel() <= ANDROID_LOLLIPOP_MR1;
#else
  return false;
#endif
}

static void procmapsInit(InternalMmapVectorNoCtor<LoadedModule> *modules) {
  MemoryMappingLayout memory_mapping(/*cache_enabled*/true);
  memory_mapping.DumpListOfModules(modules);
}

void ListOfModules::init() {
  clearOrInit();
  if (requiresProcmaps()) {
    procmapsInit(&modules_);
  } else {
    DlIteratePhdrData data = {&modules_, true};
    dl_iterate_phdr(dl_iterate_phdr_cb, &data);
  }
}

// When a custom loader is used, dl_iterate_phdr may not contain the full
// list of modules. Allow callers to fall back to using procmaps.
void ListOfModules::fallbackInit() {
  if (!requiresProcmaps()) {
    clearOrInit();
    procmapsInit(&modules_);
  } else {
    clear();
  }
}

// getrusage does not give us the current RSS, only the max RSS.
// Still, this is better than nothing if /proc/self/statm is not available
// for some reason, e.g. due to a sandbox.
static uptr GetRSSFromGetrusage() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))  // Failed, probably due to a sandbox.
    return 0;
  return usage.ru_maxrss << 10;  // ru_maxrss is in Kb.
}

uptr GetRSS() {
  if (!common_flags()->can_use_proc_maps_statm)
    return GetRSSFromGetrusage();
  fd_t fd = OpenFile("/proc/self/statm", RdOnly);
  if (fd == kInvalidFd)
    return GetRSSFromGetrusage();
  char buf[64];
  uptr len = internal_read(fd, buf, sizeof(buf) - 1);
  internal_close(fd);
  if ((sptr)len <= 0)
    return 0;
  buf[len] = 0;
  // The format of the file is:
  // 1084 89 69 11 0 79 0
  // We need the second number which is RSS in pages.
  char *pos = buf;
  // Skip the first number.
  while (*pos >= '0' && *pos <= '9')
    pos++;
  // Skip whitespaces.
  while (!(*pos >= '0' && *pos <= '9') && *pos != 0)
    pos++;
  // Read the number.
  uptr rss = 0;
  while (*pos >= '0' && *pos <= '9')
    rss = rss * 10 + *pos++ - '0';
  return rss * GetPageSizeCached();
}

// sysconf(_SC_NPROCESSORS_{CONF,ONLN}) cannot be used on most platforms as
// they allocate memory.
u32 GetNumberOfCPUs() {
#if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_OPENBSD
  u32 ncpu;
  int req[2];
  uptr len = sizeof(ncpu);
  req[0] = CTL_HW;
  req[1] = HW_NCPU;
  CHECK_EQ(internal_sysctl(req, 2, &ncpu, &len, NULL, 0), 0);
  return ncpu;
#elif SANITIZER_ANDROID && !defined(CPU_COUNT) && !defined(__aarch64__)
  // Fall back to /sys/devices/system/cpu on Android when cpu_set_t doesn't
  // exist in sched.h. That is the case for toolchains generated with older
  // NDKs.
  // This code doesn't work on AArch64 because internal_getdents makes use of
  // the 64bit getdents syscall, but cpu_set_t seems to always exist on AArch64.
  uptr fd = internal_open("/sys/devices/system/cpu", O_RDONLY | O_DIRECTORY);
  if (internal_iserror(fd))
    return 0;
  InternalMmapVector<u8> buffer(4096);
  uptr bytes_read = buffer.size();
  uptr n_cpus = 0;
  u8 *d_type;
  struct linux_dirent *entry = (struct linux_dirent *)&buffer[bytes_read];
  while (true) {
    if ((u8 *)entry >= &buffer[bytes_read]) {
      bytes_read = internal_getdents(fd, (struct linux_dirent *)buffer.data(),
                                     buffer.size());
      if (internal_iserror(bytes_read) || !bytes_read)
        break;
      entry = (struct linux_dirent *)buffer.data();
    }
    d_type = (u8 *)entry + entry->d_reclen - 1;
    if (d_type >= &buffer[bytes_read] ||
        (u8 *)&entry->d_name[3] >= &buffer[bytes_read])
      break;
    if (entry->d_ino != 0 && *d_type == DT_DIR) {
      if (entry->d_name[0] == 'c' && entry->d_name[1] == 'p' &&
          entry->d_name[2] == 'u' &&
          entry->d_name[3] >= '0' && entry->d_name[3] <= '9')
        n_cpus++;
    }
    entry = (struct linux_dirent *)(((u8 *)entry) + entry->d_reclen);
  }
  internal_close(fd);
  return n_cpus;
#elif SANITIZER_SOLARIS
  return sysconf(_SC_NPROCESSORS_ONLN);
#else
  cpu_set_t CPUs;
  CHECK_EQ(sched_getaffinity(0, sizeof(cpu_set_t), &CPUs), 0);
  return CPU_COUNT(&CPUs);
#endif
}

#if SANITIZER_LINUX

# if SANITIZER_ANDROID
static atomic_uint8_t android_log_initialized;

void AndroidLogInit() {
  openlog(GetProcessName(), 0, LOG_USER);
  atomic_store(&android_log_initialized, 1, memory_order_release);
}

static bool ShouldLogAfterPrintf() {
  return atomic_load(&android_log_initialized, memory_order_acquire);
}

extern "C" SANITIZER_WEAK_ATTRIBUTE
int async_safe_write_log(int pri, const char* tag, const char* msg);
extern "C" SANITIZER_WEAK_ATTRIBUTE
int __android_log_write(int prio, const char* tag, const char* msg);

// ANDROID_LOG_INFO is 4, but can't be resolved at runtime.
#define SANITIZER_ANDROID_LOG_INFO 4

// async_safe_write_log is a new public version of __libc_write_log that is
// used behind syslog. It is preferable to syslog as it will not do any dynamic
// memory allocation or formatting.
// If the function is not available, syslog is preferred for L+ (it was broken
// pre-L) as __android_log_write triggers a racey behavior with the strncpy
// interceptor. Fallback to __android_log_write pre-L.
void WriteOneLineToSyslog(const char *s) {
  if (&async_safe_write_log) {
    async_safe_write_log(SANITIZER_ANDROID_LOG_INFO, GetProcessName(), s);
  } else if (AndroidGetApiLevel() > ANDROID_KITKAT) {
    syslog(LOG_INFO, "%s", s);
  } else {
    CHECK(&__android_log_write);
    __android_log_write(SANITIZER_ANDROID_LOG_INFO, nullptr, s);
  }
}

extern "C" SANITIZER_WEAK_ATTRIBUTE
void android_set_abort_message(const char *);

void SetAbortMessage(const char *str) {
  if (&android_set_abort_message)
    android_set_abort_message(str);
}
# else
void AndroidLogInit() {}

static bool ShouldLogAfterPrintf() { return true; }

void WriteOneLineToSyslog(const char *s) { syslog(LOG_INFO, "%s", s); }

void SetAbortMessage(const char *str) {}
# endif  // SANITIZER_ANDROID

void LogMessageOnPrintf(const char *str) {
  if (common_flags()->log_to_syslog && ShouldLogAfterPrintf())
    WriteToSyslog(str);
}

#endif  // SANITIZER_LINUX

#if SANITIZER_LINUX && !SANITIZER_GO
// glibc crashes when using clock_gettime from a preinit_array function as the
// vDSO function pointers haven't been initialized yet. __progname is
// initialized after the vDSO function pointers, so if it exists, is not null
// and is not empty, we can use clock_gettime.
extern "C" SANITIZER_WEAK_ATTRIBUTE char *__progname;
INLINE bool CanUseVDSO() {
  // Bionic is safe, it checks for the vDSO function pointers to be initialized.
  if (SANITIZER_ANDROID)
    return true;
  if (&__progname && __progname && *__progname)
    return true;
  return false;
}

// MonotonicNanoTime is a timing function that can leverage the vDSO by calling
// clock_gettime. real_clock_gettime only exists if clock_gettime is
// intercepted, so define it weakly and use it if available.
extern "C" SANITIZER_WEAK_ATTRIBUTE
int real_clock_gettime(u32 clk_id, void *tp);
u64 MonotonicNanoTime() {
  timespec ts;
  if (CanUseVDSO()) {
    if (&real_clock_gettime)
      real_clock_gettime(CLOCK_MONOTONIC, &ts);
    else
      clock_gettime(CLOCK_MONOTONIC, &ts);
  } else {
    internal_clock_gettime(CLOCK_MONOTONIC, &ts);
  }
  return (u64)ts.tv_sec * (1000ULL * 1000 * 1000) + ts.tv_nsec;
}
#else
// Non-Linux & Go always use the syscall.
u64 MonotonicNanoTime() {
  timespec ts;
  internal_clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * (1000ULL * 1000 * 1000) + ts.tv_nsec;
}
#endif  // SANITIZER_LINUX && !SANITIZER_GO

#if !SANITIZER_OPENBSD
void ReExec() {
  const char *pathname = "/proc/self/exe";

#if SANITIZER_NETBSD
  static const int name[] = {
      CTL_KERN,
      KERN_PROC_ARGS,
      -1,
      KERN_PROC_PATHNAME,
  };
  char path[400];
  uptr len;

  len = sizeof(path);
  if (internal_sysctl(name, ARRAY_SIZE(name), path, &len, NULL, 0) != -1)
    pathname = path;
#elif SANITIZER_SOLARIS
  pathname = getexecname();
  CHECK_NE(pathname, NULL);
#elif SANITIZER_USE_GETAUXVAL
  // Calling execve with /proc/self/exe sets that as $EXEC_ORIGIN. Binaries that
  // rely on that will fail to load shared libraries. Query AT_EXECFN instead.
  pathname = reinterpret_cast<const char *>(getauxval(AT_EXECFN));
#endif

  uptr rv = internal_execve(pathname, GetArgv(), GetEnviron());
  int rverrno;
  CHECK_EQ(internal_iserror(rv, &rverrno), true);
  Printf("execve failed, errno %d\n", rverrno);
  Die();
}
#endif  // !SANITIZER_OPENBSD

} // namespace __sanitizer

#endif
