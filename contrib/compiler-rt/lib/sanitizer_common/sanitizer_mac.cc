//===-- sanitizer_mac.cc --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements OSX-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_MAC
#include "sanitizer_mac.h"

// Use 64-bit inodes in file operations. ASan does not support OS X 10.5, so
// the clients will most certainly use 64-bit ones as well.
#ifndef _DARWIN_USE_64_BIT_INODE
#define _DARWIN_USE_64_BIT_INODE 1
#endif
#include <stdio.h>

#include "sanitizer_common.h"
#include "sanitizer_file.h"
#include "sanitizer_flags.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_procmaps.h"

#if !SANITIZER_IOS
#include <crt_externs.h>  // for _NSGetEnviron
#else
extern char **environ;
#endif

#if defined(__has_include) && __has_include(<os/trace.h>)
#define SANITIZER_OS_TRACE 1
#include <os/trace.h>
#else
#define SANITIZER_OS_TRACE 0
#endif

#if !SANITIZER_IOS
#include <crt_externs.h>  // for _NSGetArgv and _NSGetEnviron
#else
extern "C" {
  extern char ***_NSGetArgv(void);
}
#endif

#include <asl.h>
#include <dlfcn.h>  // for dladdr()
#include <errno.h>
#include <fcntl.h>
#include <libkern/OSAtomic.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <util.h>

// From <crt_externs.h>, but we don't have that file on iOS.
extern "C" {
  extern char ***_NSGetArgv(void);
  extern char ***_NSGetEnviron(void);
}

// From <mach/mach_vm.h>, but we don't have that file on iOS.
extern "C" {
  extern kern_return_t mach_vm_region_recurse(
    vm_map_t target_task,
    mach_vm_address_t *address,
    mach_vm_size_t *size,
    natural_t *nesting_depth,
    vm_region_recurse_info_t info,
    mach_msg_type_number_t *infoCnt);
}

namespace __sanitizer {

#include "sanitizer_syscall_generic.inc"

// Direct syscalls, don't call libmalloc hooks (but not available on 10.6).
extern "C" void *__mmap(void *addr, size_t len, int prot, int flags, int fildes,
                        off_t off) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int __munmap(void *, size_t) SANITIZER_WEAK_ATTRIBUTE;

// ---------------------- sanitizer_libc.h

// From <mach/vm_statistics.h>, but not on older OSs.
#ifndef VM_MEMORY_SANITIZER
#define VM_MEMORY_SANITIZER 99
#endif

// XNU on Darwin provides a mmap flag that optimizes allocation/deallocation of
// giant memory regions (i.e. shadow memory regions).
#define kXnuFastMmapFd 0x4
static size_t kXnuFastMmapThreshold = 2 << 30; // 2 GB
static bool use_xnu_fast_mmap = false;

uptr internal_mmap(void *addr, size_t length, int prot, int flags,
                   int fd, u64 offset) {
  if (fd == -1) {
    fd = VM_MAKE_TAG(VM_MEMORY_SANITIZER);
    if (length >= kXnuFastMmapThreshold) {
      if (use_xnu_fast_mmap) fd |= kXnuFastMmapFd;
    }
  }
  if (&__mmap) return (uptr)__mmap(addr, length, prot, flags, fd, offset);
  return (uptr)mmap(addr, length, prot, flags, fd, offset);
}

uptr internal_munmap(void *addr, uptr length) {
  if (&__munmap) return __munmap(addr, length);
  return munmap(addr, length);
}

int internal_mprotect(void *addr, uptr length, int prot) {
  return mprotect(addr, length, prot);
}

uptr internal_close(fd_t fd) {
  return close(fd);
}

uptr internal_open(const char *filename, int flags) {
  return open(filename, flags);
}

uptr internal_open(const char *filename, int flags, u32 mode) {
  return open(filename, flags, mode);
}

uptr internal_read(fd_t fd, void *buf, uptr count) {
  return read(fd, buf, count);
}

uptr internal_write(fd_t fd, const void *buf, uptr count) {
  return write(fd, buf, count);
}

uptr internal_stat(const char *path, void *buf) {
  return stat(path, (struct stat *)buf);
}

uptr internal_lstat(const char *path, void *buf) {
  return lstat(path, (struct stat *)buf);
}

uptr internal_fstat(fd_t fd, void *buf) {
  return fstat(fd, (struct stat *)buf);
}

uptr internal_filesize(fd_t fd) {
  struct stat st;
  if (internal_fstat(fd, &st))
    return -1;
  return (uptr)st.st_size;
}

uptr internal_dup(int oldfd) {
  return dup(oldfd);
}

uptr internal_dup2(int oldfd, int newfd) {
  return dup2(oldfd, newfd);
}

uptr internal_readlink(const char *path, char *buf, uptr bufsize) {
  return readlink(path, buf, bufsize);
}

uptr internal_unlink(const char *path) {
  return unlink(path);
}

uptr internal_sched_yield() {
  return sched_yield();
}

void internal__exit(int exitcode) {
  _exit(exitcode);
}

unsigned int internal_sleep(unsigned int seconds) {
  return sleep(seconds);
}

uptr internal_getpid() {
  return getpid();
}

int internal_sigaction(int signum, const void *act, void *oldact) {
  return sigaction(signum,
                   (const struct sigaction *)act, (struct sigaction *)oldact);
}

void internal_sigfillset(__sanitizer_sigset_t *set) { sigfillset(set); }

uptr internal_sigprocmask(int how, __sanitizer_sigset_t *set,
                          __sanitizer_sigset_t *oldset) {
  // Don't use sigprocmask here, because it affects all threads.
  return pthread_sigmask(how, set, oldset);
}

// Doesn't call pthread_atfork() handlers (but not available on 10.6).
extern "C" pid_t __fork(void) SANITIZER_WEAK_ATTRIBUTE;

int internal_fork() {
  if (&__fork)
    return __fork();
  return fork();
}

int internal_sysctl(const int *name, unsigned int namelen, void *oldp,
                    uptr *oldlenp, const void *newp, uptr newlen) {
  return sysctl(const_cast<int *>(name), namelen, oldp, (size_t *)oldlenp,
                const_cast<void *>(newp), (size_t)newlen);
}

int internal_sysctlbyname(const char *sname, void *oldp, uptr *oldlenp,
                          const void *newp, uptr newlen) {
  return sysctlbyname(sname, oldp, (size_t *)oldlenp, const_cast<void *>(newp),
                      (size_t)newlen);
}

int internal_forkpty(int *amaster) {
  int master, slave;
  if (openpty(&master, &slave, nullptr, nullptr, nullptr) == -1) return -1;
  int pid = internal_fork();
  if (pid == -1) {
    close(master);
    close(slave);
    return -1;
  }
  if (pid == 0) {
    close(master);
    if (login_tty(slave) != 0) {
      // We already forked, there's not much we can do.  Let's quit.
      Report("login_tty failed (errno %d)\n", errno);
      internal__exit(1);
    }
  } else {
    *amaster = master;
    close(slave);
  }
  return pid;
}

uptr internal_rename(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
}

uptr internal_ftruncate(fd_t fd, uptr size) {
  return ftruncate(fd, size);
}

uptr internal_execve(const char *filename, char *const argv[],
                     char *const envp[]) {
  return execve(filename, argv, envp);
}

uptr internal_waitpid(int pid, int *status, int options) {
  return waitpid(pid, status, options);
}

// ----------------- sanitizer_common.h
bool FileExists(const char *filename) {
  if (ShouldMockFailureToOpen(filename))
    return false;
  struct stat st;
  if (stat(filename, &st))
    return false;
  // Sanity check: filename is a regular file.
  return S_ISREG(st.st_mode);
}

tid_t GetTid() {
  tid_t tid;
  pthread_threadid_np(nullptr, &tid);
  return tid;
}

void GetThreadStackTopAndBottom(bool at_initialization, uptr *stack_top,
                                uptr *stack_bottom) {
  CHECK(stack_top);
  CHECK(stack_bottom);
  uptr stacksize = pthread_get_stacksize_np(pthread_self());
  // pthread_get_stacksize_np() returns an incorrect stack size for the main
  // thread on Mavericks. See
  // https://github.com/google/sanitizers/issues/261
  if ((GetMacosVersion() >= MACOS_VERSION_MAVERICKS) && at_initialization &&
      stacksize == (1 << 19))  {
    struct rlimit rl;
    CHECK_EQ(getrlimit(RLIMIT_STACK, &rl), 0);
    // Most often rl.rlim_cur will be the desired 8M.
    if (rl.rlim_cur < kMaxThreadStackSize) {
      stacksize = rl.rlim_cur;
    } else {
      stacksize = kMaxThreadStackSize;
    }
  }
  void *stackaddr = pthread_get_stackaddr_np(pthread_self());
  *stack_top = (uptr)stackaddr;
  *stack_bottom = *stack_top - stacksize;
}

char **GetEnviron() {
#if !SANITIZER_IOS
  char ***env_ptr = _NSGetEnviron();
  if (!env_ptr) {
    Report("_NSGetEnviron() returned NULL. Please make sure __asan_init() is "
           "called after libSystem_initializer().\n");
    CHECK(env_ptr);
  }
  char **environ = *env_ptr;
#endif
  CHECK(environ);
  return environ;
}

const char *GetEnv(const char *name) {
  char **env = GetEnviron();
  uptr name_len = internal_strlen(name);
  while (*env != 0) {
    uptr len = internal_strlen(*env);
    if (len > name_len) {
      const char *p = *env;
      if (!internal_memcmp(p, name, name_len) &&
          p[name_len] == '=') {  // Match.
        return *env + name_len + 1;  // String starting after =.
      }
    }
    env++;
  }
  return 0;
}

uptr ReadBinaryName(/*out*/char *buf, uptr buf_len) {
  CHECK_LE(kMaxPathLength, buf_len);

  // On OS X the executable path is saved to the stack by dyld. Reading it
  // from there is much faster than calling dladdr, especially for large
  // binaries with symbols.
  InternalScopedString exe_path(kMaxPathLength);
  uint32_t size = exe_path.size();
  if (_NSGetExecutablePath(exe_path.data(), &size) == 0 &&
      realpath(exe_path.data(), buf) != 0) {
    return internal_strlen(buf);
  }
  return 0;
}

uptr ReadLongProcessName(/*out*/char *buf, uptr buf_len) {
  return ReadBinaryName(buf, buf_len);
}

void ReExec() {
  UNIMPLEMENTED();
}

void CheckASLR() {
  // Do nothing
}

void CheckMPROTECT() {
  // Do nothing
}

uptr GetPageSize() {
  return sysconf(_SC_PAGESIZE);
}

extern "C" unsigned malloc_num_zones;
extern "C" malloc_zone_t **malloc_zones;
malloc_zone_t sanitizer_zone;

// We need to make sure that sanitizer_zone is registered as malloc_zones[0]. If
// libmalloc tries to set up a different zone as malloc_zones[0], it will call
// mprotect(malloc_zones, ..., PROT_READ).  This interceptor will catch that and
// make sure we are still the first (default) zone.
void MprotectMallocZones(void *addr, int prot) {
  if (addr == malloc_zones && prot == PROT_READ) {
    if (malloc_num_zones > 1 && malloc_zones[0] != &sanitizer_zone) {
      for (unsigned i = 1; i < malloc_num_zones; i++) {
        if (malloc_zones[i] == &sanitizer_zone) {
          // Swap malloc_zones[0] and malloc_zones[i].
          malloc_zones[i] = malloc_zones[0];
          malloc_zones[0] = &sanitizer_zone;
          break;
        }
      }
    }
  }
}

BlockingMutex::BlockingMutex() {
  internal_memset(this, 0, sizeof(*this));
}

void BlockingMutex::Lock() {
  CHECK(sizeof(OSSpinLock) <= sizeof(opaque_storage_));
  CHECK_EQ(OS_SPINLOCK_INIT, 0);
  CHECK_EQ(owner_, 0);
  OSSpinLockLock((OSSpinLock*)&opaque_storage_);
}

void BlockingMutex::Unlock() {
  OSSpinLockUnlock((OSSpinLock*)&opaque_storage_);
}

void BlockingMutex::CheckLocked() {
  CHECK_NE(*(OSSpinLock*)&opaque_storage_, 0);
}

u64 NanoTime() {
  timeval tv;
  internal_memset(&tv, 0, sizeof(tv));
  gettimeofday(&tv, 0);
  return (u64)tv.tv_sec * 1000*1000*1000 + tv.tv_usec * 1000;
}

// This needs to be called during initialization to avoid being racy.
u64 MonotonicNanoTime() {
  static mach_timebase_info_data_t timebase_info;
  if (timebase_info.denom == 0) mach_timebase_info(&timebase_info);
  return (mach_absolute_time() * timebase_info.numer) / timebase_info.denom;
}

uptr GetTlsSize() {
  return 0;
}

void InitTlsSize() {
}

uptr TlsBaseAddr() {
  uptr segbase = 0;
#if defined(__x86_64__)
  asm("movq %%gs:0,%0" : "=r"(segbase));
#elif defined(__i386__)
  asm("movl %%gs:0,%0" : "=r"(segbase));
#endif
  return segbase;
}

// The size of the tls on darwin does not appear to be well documented,
// however the vm memory map suggests that it is 1024 uptrs in size,
// with a size of 0x2000 bytes on x86_64 and 0x1000 bytes on i386.
uptr TlsSize() {
#if defined(__x86_64__) || defined(__i386__)
  return 1024 * sizeof(uptr);
#else
  return 0;
#endif
}

void GetThreadStackAndTls(bool main, uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
#if !SANITIZER_GO
  uptr stack_top, stack_bottom;
  GetThreadStackTopAndBottom(main, &stack_top, &stack_bottom);
  *stk_addr = stack_bottom;
  *stk_size = stack_top - stack_bottom;
  *tls_addr = TlsBaseAddr();
  *tls_size = TlsSize();
#else
  *stk_addr = 0;
  *stk_size = 0;
  *tls_addr = 0;
  *tls_size = 0;
#endif
}

void ListOfModules::init() {
  clearOrInit();
  MemoryMappingLayout memory_mapping(false);
  memory_mapping.DumpListOfModules(&modules_);
}

void ListOfModules::fallbackInit() { clear(); }

static HandleSignalMode GetHandleSignalModeImpl(int signum) {
  switch (signum) {
    case SIGABRT:
      return common_flags()->handle_abort;
    case SIGILL:
      return common_flags()->handle_sigill;
    case SIGTRAP:
      return common_flags()->handle_sigtrap;
    case SIGFPE:
      return common_flags()->handle_sigfpe;
    case SIGSEGV:
      return common_flags()->handle_segv;
    case SIGBUS:
      return common_flags()->handle_sigbus;
  }
  return kHandleSignalNo;
}

HandleSignalMode GetHandleSignalMode(int signum) {
  // Handling fatal signals on watchOS and tvOS devices is disallowed.
  if ((SANITIZER_WATCHOS || SANITIZER_TVOS) && !(SANITIZER_IOSSIM))
    return kHandleSignalNo;
  HandleSignalMode result = GetHandleSignalModeImpl(signum);
  if (result == kHandleSignalYes && !common_flags()->allow_user_segv_handler)
    return kHandleSignalExclusive;
  return result;
}

MacosVersion cached_macos_version = MACOS_VERSION_UNINITIALIZED;

MacosVersion GetMacosVersionInternal() {
  int mib[2] = { CTL_KERN, KERN_OSRELEASE };
  char version[100];
  uptr len = 0, maxlen = sizeof(version) / sizeof(version[0]);
  for (uptr i = 0; i < maxlen; i++) version[i] = '\0';
  // Get the version length.
  CHECK_NE(internal_sysctl(mib, 2, 0, &len, 0, 0), -1);
  CHECK_LT(len, maxlen);
  CHECK_NE(internal_sysctl(mib, 2, version, &len, 0, 0), -1);

  // Expect <major>.<minor>(.<patch>)
  CHECK_GE(len, 3);
  const char *p = version;
  int major = internal_simple_strtoll(p, &p, /*base=*/10);
  if (*p != '.') return MACOS_VERSION_UNKNOWN;
  p += 1;
  int minor = internal_simple_strtoll(p, &p, /*base=*/10);
  if (*p != '.') return MACOS_VERSION_UNKNOWN;

  switch (major) {
    case 9: return MACOS_VERSION_LEOPARD;
    case 10: return MACOS_VERSION_SNOW_LEOPARD;
    case 11: return MACOS_VERSION_LION;
    case 12: return MACOS_VERSION_MOUNTAIN_LION;
    case 13: return MACOS_VERSION_MAVERICKS;
    case 14: return MACOS_VERSION_YOSEMITE;
    case 15: return MACOS_VERSION_EL_CAPITAN;
    case 16: return MACOS_VERSION_SIERRA;
    case 17:
      // Not a typo, 17.5 Darwin Kernel Version maps to High Sierra 10.13.4.
      if (minor >= 5)
        return MACOS_VERSION_HIGH_SIERRA_DOT_RELEASE_4;
      return MACOS_VERSION_HIGH_SIERRA;
    case 18:
      return MACOS_VERSION_MOJAVE;
    default:
      if (major < 9) return MACOS_VERSION_UNKNOWN;
      return MACOS_VERSION_UNKNOWN_NEWER;
  }
}

MacosVersion GetMacosVersion() {
  atomic_uint32_t *cache =
      reinterpret_cast<atomic_uint32_t*>(&cached_macos_version);
  MacosVersion result =
      static_cast<MacosVersion>(atomic_load(cache, memory_order_acquire));
  if (result == MACOS_VERSION_UNINITIALIZED) {
    result = GetMacosVersionInternal();
    atomic_store(cache, result, memory_order_release);
  }
  return result;
}

bool PlatformHasDifferentMemcpyAndMemmove() {
  // On OS X 10.7 memcpy() and memmove() are both resolved
  // into memmove$VARIANT$sse42.
  // See also https://github.com/google/sanitizers/issues/34.
  // TODO(glider): need to check dynamically that memcpy() and memmove() are
  // actually the same function.
  return GetMacosVersion() == MACOS_VERSION_SNOW_LEOPARD;
}

uptr GetRSS() {
  struct task_basic_info info;
  unsigned count = TASK_BASIC_INFO_COUNT;
  kern_return_t result =
      task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count);
  if (UNLIKELY(result != KERN_SUCCESS)) {
    Report("Cannot get task info. Error: %d\n", result);
    Die();
  }
  return info.resident_size;
}

void *internal_start_thread(void(*func)(void *arg), void *arg) {
  // Start the thread with signals blocked, otherwise it can steal user signals.
  __sanitizer_sigset_t set, old;
  internal_sigfillset(&set);
  internal_sigprocmask(SIG_SETMASK, &set, &old);
  pthread_t th;
  pthread_create(&th, 0, (void*(*)(void *arg))func, arg);
  internal_sigprocmask(SIG_SETMASK, &old, 0);
  return th;
}

void internal_join_thread(void *th) { pthread_join((pthread_t)th, 0); }

#if !SANITIZER_GO
static BlockingMutex syslog_lock(LINKER_INITIALIZED);
#endif

void WriteOneLineToSyslog(const char *s) {
#if !SANITIZER_GO
  syslog_lock.CheckLocked();
  asl_log(nullptr, nullptr, ASL_LEVEL_ERR, "%s", s);
#endif
}

void LogMessageOnPrintf(const char *str) {
  // Log all printf output to CrashLog.
  if (common_flags()->abort_on_error)
    CRAppendCrashLogMessage(str);
}

void LogFullErrorReport(const char *buffer) {
#if !SANITIZER_GO
  // Log with os_trace. This will make it into the crash log.
#if SANITIZER_OS_TRACE
  if (GetMacosVersion() >= MACOS_VERSION_YOSEMITE) {
    // os_trace requires the message (format parameter) to be a string literal.
    if (internal_strncmp(SanitizerToolName, "AddressSanitizer",
                         sizeof("AddressSanitizer") - 1) == 0)
      os_trace("Address Sanitizer reported a failure.");
    else if (internal_strncmp(SanitizerToolName, "UndefinedBehaviorSanitizer",
                              sizeof("UndefinedBehaviorSanitizer") - 1) == 0)
      os_trace("Undefined Behavior Sanitizer reported a failure.");
    else if (internal_strncmp(SanitizerToolName, "ThreadSanitizer",
                              sizeof("ThreadSanitizer") - 1) == 0)
      os_trace("Thread Sanitizer reported a failure.");
    else
      os_trace("Sanitizer tool reported a failure.");

    if (common_flags()->log_to_syslog)
      os_trace("Consult syslog for more information.");
  }
#endif

  // Log to syslog.
  // The logging on OS X may call pthread_create so we need the threading
  // environment to be fully initialized. Also, this should never be called when
  // holding the thread registry lock since that may result in a deadlock. If
  // the reporting thread holds the thread registry mutex, and asl_log waits
  // for GCD to dispatch a new thread, the process will deadlock, because the
  // pthread_create wrapper needs to acquire the lock as well.
  BlockingMutexLock l(&syslog_lock);
  if (common_flags()->log_to_syslog)
    WriteToSyslog(buffer);

  // The report is added to CrashLog as part of logging all of Printf output.
#endif
}

SignalContext::WriteFlag SignalContext::GetWriteFlag() const {
#if defined(__x86_64__) || defined(__i386__)
  ucontext_t *ucontext = static_cast<ucontext_t*>(context);
  return ucontext->uc_mcontext->__es.__err & 2 /*T_PF_WRITE*/ ? WRITE : READ;
#else
  return UNKNOWN;
#endif
}

static void GetPcSpBp(void *context, uptr *pc, uptr *sp, uptr *bp) {
  ucontext_t *ucontext = (ucontext_t*)context;
# if defined(__aarch64__)
  *pc = ucontext->uc_mcontext->__ss.__pc;
#   if defined(__IPHONE_8_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_8_0
  *bp = ucontext->uc_mcontext->__ss.__fp;
#   else
  *bp = ucontext->uc_mcontext->__ss.__lr;
#   endif
  *sp = ucontext->uc_mcontext->__ss.__sp;
# elif defined(__x86_64__)
  *pc = ucontext->uc_mcontext->__ss.__rip;
  *bp = ucontext->uc_mcontext->__ss.__rbp;
  *sp = ucontext->uc_mcontext->__ss.__rsp;
# elif defined(__arm__)
  *pc = ucontext->uc_mcontext->__ss.__pc;
  *bp = ucontext->uc_mcontext->__ss.__r[7];
  *sp = ucontext->uc_mcontext->__ss.__sp;
# elif defined(__i386__)
  *pc = ucontext->uc_mcontext->__ss.__eip;
  *bp = ucontext->uc_mcontext->__ss.__ebp;
  *sp = ucontext->uc_mcontext->__ss.__esp;
# else
# error "Unknown architecture"
# endif
}

void SignalContext::InitPcSpBp() { GetPcSpBp(context, &pc, &sp, &bp); }

void InitializePlatformEarly() {
  // Only use xnu_fast_mmap when on x86_64 and the OS supports it.
  use_xnu_fast_mmap =
#if defined(__x86_64__)
      GetMacosVersion() >= MACOS_VERSION_HIGH_SIERRA_DOT_RELEASE_4;
#else
      false;
#endif
}

#if !SANITIZER_GO
static const char kDyldInsertLibraries[] = "DYLD_INSERT_LIBRARIES";
LowLevelAllocator allocator_for_env;

// Change the value of the env var |name|, leaking the original value.
// If |name_value| is NULL, the variable is deleted from the environment,
// otherwise the corresponding "NAME=value" string is replaced with
// |name_value|.
void LeakyResetEnv(const char *name, const char *name_value) {
  char **env = GetEnviron();
  uptr name_len = internal_strlen(name);
  while (*env != 0) {
    uptr len = internal_strlen(*env);
    if (len > name_len) {
      const char *p = *env;
      if (!internal_memcmp(p, name, name_len) && p[name_len] == '=') {
        // Match.
        if (name_value) {
          // Replace the old value with the new one.
          *env = const_cast<char*>(name_value);
        } else {
          // Shift the subsequent pointers back.
          char **del = env;
          do {
            del[0] = del[1];
          } while (*del++);
        }
      }
    }
    env++;
  }
}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool ReexecDisabled() {
  return false;
}

extern "C" SANITIZER_WEAK_ATTRIBUTE double dyldVersionNumber;
static const double kMinDyldVersionWithAutoInterposition = 360.0;

bool DyldNeedsEnvVariable() {
  // Although sanitizer support was added to LLVM on OS X 10.7+, GCC users
  // still may want use them on older systems. On older Darwin platforms, dyld
  // doesn't export dyldVersionNumber symbol and we simply return true.
  if (!&dyldVersionNumber) return true;
  // If running on OS X 10.11+ or iOS 9.0+, dyld will interpose even if
  // DYLD_INSERT_LIBRARIES is not set. However, checking OS version via
  // GetMacosVersion() doesn't work for the simulator. Let's instead check
  // `dyldVersionNumber`, which is exported by dyld, against a known version
  // number from the first OS release where this appeared.
  return dyldVersionNumber < kMinDyldVersionWithAutoInterposition;
}

void MaybeReexec() {
  // FIXME: This should really live in some "InitializePlatform" method.
  MonotonicNanoTime();

  if (ReexecDisabled()) return;

  // Make sure the dynamic runtime library is preloaded so that the
  // wrappers work. If it is not, set DYLD_INSERT_LIBRARIES and re-exec
  // ourselves.
  Dl_info info;
  RAW_CHECK(dladdr((void*)((uptr)&__sanitizer_report_error_summary), &info));
  char *dyld_insert_libraries =
      const_cast<char*>(GetEnv(kDyldInsertLibraries));
  uptr old_env_len = dyld_insert_libraries ?
      internal_strlen(dyld_insert_libraries) : 0;
  uptr fname_len = internal_strlen(info.dli_fname);
  const char *dylib_name = StripModuleName(info.dli_fname);
  uptr dylib_name_len = internal_strlen(dylib_name);

  bool lib_is_in_env = dyld_insert_libraries &&
                       internal_strstr(dyld_insert_libraries, dylib_name);
  if (DyldNeedsEnvVariable() && !lib_is_in_env) {
    // DYLD_INSERT_LIBRARIES is not set or does not contain the runtime
    // library.
    InternalScopedString program_name(1024);
    uint32_t buf_size = program_name.size();
    _NSGetExecutablePath(program_name.data(), &buf_size);
    char *new_env = const_cast<char*>(info.dli_fname);
    if (dyld_insert_libraries) {
      // Append the runtime dylib name to the existing value of
      // DYLD_INSERT_LIBRARIES.
      new_env = (char*)allocator_for_env.Allocate(old_env_len + fname_len + 2);
      internal_strncpy(new_env, dyld_insert_libraries, old_env_len);
      new_env[old_env_len] = ':';
      // Copy fname_len and add a trailing zero.
      internal_strncpy(new_env + old_env_len + 1, info.dli_fname,
                       fname_len + 1);
      // Ok to use setenv() since the wrappers don't depend on the value of
      // asan_inited.
      setenv(kDyldInsertLibraries, new_env, /*overwrite*/1);
    } else {
      // Set DYLD_INSERT_LIBRARIES equal to the runtime dylib name.
      setenv(kDyldInsertLibraries, info.dli_fname, /*overwrite*/0);
    }
    VReport(1, "exec()-ing the program with\n");
    VReport(1, "%s=%s\n", kDyldInsertLibraries, new_env);
    VReport(1, "to enable wrappers.\n");
    execv(program_name.data(), *_NSGetArgv());

    // We get here only if execv() failed.
    Report("ERROR: The process is launched without DYLD_INSERT_LIBRARIES, "
           "which is required for the sanitizer to work. We tried to set the "
           "environment variable and re-execute itself, but execv() failed, "
           "possibly because of sandbox restrictions. Make sure to launch the "
           "executable with:\n%s=%s\n", kDyldInsertLibraries, new_env);
    RAW_CHECK("execv failed" && 0);
  }

  // Verify that interceptors really work.  We'll use dlsym to locate
  // "pthread_create", if interceptors are working, it should really point to
  // "wrap_pthread_create" within our own dylib.
  Dl_info info_pthread_create;
  void *dlopen_addr = dlsym(RTLD_DEFAULT, "pthread_create");
  RAW_CHECK(dladdr(dlopen_addr, &info_pthread_create));
  if (internal_strcmp(info.dli_fname, info_pthread_create.dli_fname) != 0) {
    Report(
        "ERROR: Interceptors are not working. This may be because %s is "
        "loaded too late (e.g. via dlopen). Please launch the executable "
        "with:\n%s=%s\n",
        SanitizerToolName, kDyldInsertLibraries, info.dli_fname);
    RAW_CHECK("interceptors not installed" && 0);
  }

  if (!lib_is_in_env)
    return;

  if (!common_flags()->strip_env)
    return;

  // DYLD_INSERT_LIBRARIES is set and contains the runtime library. Let's remove
  // the dylib from the environment variable, because interceptors are installed
  // and we don't want our children to inherit the variable.

  uptr env_name_len = internal_strlen(kDyldInsertLibraries);
  // Allocate memory to hold the previous env var name, its value, the '='
  // sign and the '\0' char.
  char *new_env = (char*)allocator_for_env.Allocate(
      old_env_len + 2 + env_name_len);
  RAW_CHECK(new_env);
  internal_memset(new_env, '\0', old_env_len + 2 + env_name_len);
  internal_strncpy(new_env, kDyldInsertLibraries, env_name_len);
  new_env[env_name_len] = '=';
  char *new_env_pos = new_env + env_name_len + 1;

  // Iterate over colon-separated pieces of |dyld_insert_libraries|.
  char *piece_start = dyld_insert_libraries;
  char *piece_end = NULL;
  char *old_env_end = dyld_insert_libraries + old_env_len;
  do {
    if (piece_start[0] == ':') piece_start++;
    piece_end = internal_strchr(piece_start, ':');
    if (!piece_end) piece_end = dyld_insert_libraries + old_env_len;
    if ((uptr)(piece_start - dyld_insert_libraries) > old_env_len) break;
    uptr piece_len = piece_end - piece_start;

    char *filename_start =
        (char *)internal_memrchr(piece_start, '/', piece_len);
    uptr filename_len = piece_len;
    if (filename_start) {
      filename_start += 1;
      filename_len = piece_len - (filename_start - piece_start);
    } else {
      filename_start = piece_start;
    }

    // If the current piece isn't the runtime library name,
    // append it to new_env.
    if ((dylib_name_len != filename_len) ||
        (internal_memcmp(filename_start, dylib_name, dylib_name_len) != 0)) {
      if (new_env_pos != new_env + env_name_len + 1) {
        new_env_pos[0] = ':';
        new_env_pos++;
      }
      internal_strncpy(new_env_pos, piece_start, piece_len);
      new_env_pos += piece_len;
    }
    // Move on to the next piece.
    piece_start = piece_end;
  } while (piece_start < old_env_end);

  // Can't use setenv() here, because it requires the allocator to be
  // initialized.
  // FIXME: instead of filtering DYLD_INSERT_LIBRARIES here, do it in
  // a separate function called after InitializeAllocator().
  if (new_env_pos == new_env + env_name_len + 1) new_env = NULL;
  LeakyResetEnv(kDyldInsertLibraries, new_env);
}
#endif  // SANITIZER_GO

char **GetArgv() {
  return *_NSGetArgv();
}

#if defined(__aarch64__) && SANITIZER_IOS && !SANITIZER_IOSSIM
// The task_vm_info struct is normally provided by the macOS SDK, but we need
// fields only available in 10.12+. Declare the struct manually to be able to
// build against older SDKs.
struct __sanitizer_task_vm_info {
  mach_vm_size_t virtual_size;
  integer_t region_count;
  integer_t page_size;
  mach_vm_size_t resident_size;
  mach_vm_size_t resident_size_peak;
  mach_vm_size_t device;
  mach_vm_size_t device_peak;
  mach_vm_size_t internal;
  mach_vm_size_t internal_peak;
  mach_vm_size_t external;
  mach_vm_size_t external_peak;
  mach_vm_size_t reusable;
  mach_vm_size_t reusable_peak;
  mach_vm_size_t purgeable_volatile_pmap;
  mach_vm_size_t purgeable_volatile_resident;
  mach_vm_size_t purgeable_volatile_virtual;
  mach_vm_size_t compressed;
  mach_vm_size_t compressed_peak;
  mach_vm_size_t compressed_lifetime;
  mach_vm_size_t phys_footprint;
  mach_vm_address_t min_address;
  mach_vm_address_t max_address;
};
#define __SANITIZER_TASK_VM_INFO_COUNT ((mach_msg_type_number_t) \
    (sizeof(__sanitizer_task_vm_info) / sizeof(natural_t)))

uptr GetTaskInfoMaxAddress() {
  __sanitizer_task_vm_info vm_info = {} /* zero initialize */;
  mach_msg_type_number_t count = __SANITIZER_TASK_VM_INFO_COUNT;
  int err = task_info(mach_task_self(), TASK_VM_INFO, (int *)&vm_info, &count);
  if (err == 0 && vm_info.max_address != 0) {
    return vm_info.max_address - 1;
  } else {
    // xnu cannot provide vm address limit
    return 0x200000000 - 1;
  }
}
#endif

uptr GetMaxUserVirtualAddress() {
#if SANITIZER_WORDSIZE == 64
# if defined(__aarch64__) && SANITIZER_IOS && !SANITIZER_IOSSIM
  // Get the maximum VM address
  static uptr max_vm = GetTaskInfoMaxAddress();
  CHECK(max_vm);
  return max_vm;
# else
  return (1ULL << 47) - 1;  // 0x00007fffffffffffUL;
# endif
#else  // SANITIZER_WORDSIZE == 32
  return (1ULL << 32) - 1;  // 0xffffffff;
#endif  // SANITIZER_WORDSIZE
}

uptr GetMaxVirtualAddress() {
  return GetMaxUserVirtualAddress();
}

uptr FindAvailableMemoryRange(uptr size, uptr alignment, uptr left_padding,
                              uptr *largest_gap_found,
                              uptr *max_occupied_addr) {
  typedef vm_region_submap_short_info_data_64_t RegionInfo;
  enum { kRegionInfoSize = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 };
  // Start searching for available memory region past PAGEZERO, which is
  // 4KB on 32-bit and 4GB on 64-bit.
  mach_vm_address_t start_address =
    (SANITIZER_WORDSIZE == 32) ? 0x000000001000 : 0x000100000000;

  mach_vm_address_t address = start_address;
  mach_vm_address_t free_begin = start_address;
  kern_return_t kr = KERN_SUCCESS;
  if (largest_gap_found) *largest_gap_found = 0;
  if (max_occupied_addr) *max_occupied_addr = 0;
  while (kr == KERN_SUCCESS) {
    mach_vm_size_t vmsize = 0;
    natural_t depth = 0;
    RegionInfo vminfo;
    mach_msg_type_number_t count = kRegionInfoSize;
    kr = mach_vm_region_recurse(mach_task_self(), &address, &vmsize, &depth,
                                (vm_region_info_t)&vminfo, &count);
    if (kr == KERN_INVALID_ADDRESS) {
      // No more regions beyond "address", consider the gap at the end of VM.
      address = GetMaxVirtualAddress() + 1;
      vmsize = 0;
    } else {
      if (max_occupied_addr) *max_occupied_addr = address + vmsize;
    }
    if (free_begin != address) {
      // We found a free region [free_begin..address-1].
      uptr gap_start = RoundUpTo((uptr)free_begin + left_padding, alignment);
      uptr gap_end = RoundDownTo((uptr)address, alignment);
      uptr gap_size = gap_end > gap_start ? gap_end - gap_start : 0;
      if (size < gap_size) {
        return gap_start;
      }

      if (largest_gap_found && *largest_gap_found < gap_size) {
        *largest_gap_found = gap_size;
      }
    }
    // Move to the next region.
    address += vmsize;
    free_begin = address;
  }

  // We looked at all free regions and could not find one large enough.
  return 0;
}

// FIXME implement on this platform.
void GetMemoryProfile(fill_profile_f cb, uptr *stats, uptr stats_size) { }

void SignalContext::DumpAllRegisters(void *context) {
  Report("Register values:\n");

  ucontext_t *ucontext = (ucontext_t*)context;
# define DUMPREG64(r) \
    Printf("%s = 0x%016llx  ", #r, ucontext->uc_mcontext->__ss.__ ## r);
# define DUMPREG32(r) \
    Printf("%s = 0x%08x  ", #r, ucontext->uc_mcontext->__ss.__ ## r);
# define DUMPREG_(r)   Printf(" "); DUMPREG(r);
# define DUMPREG__(r)  Printf("  "); DUMPREG(r);
# define DUMPREG___(r) Printf("   "); DUMPREG(r);

# if defined(__x86_64__)
#  define DUMPREG(r) DUMPREG64(r)
  DUMPREG(rax); DUMPREG(rbx); DUMPREG(rcx); DUMPREG(rdx); Printf("\n");
  DUMPREG(rdi); DUMPREG(rsi); DUMPREG(rbp); DUMPREG(rsp); Printf("\n");
  DUMPREG_(r8); DUMPREG_(r9); DUMPREG(r10); DUMPREG(r11); Printf("\n");
  DUMPREG(r12); DUMPREG(r13); DUMPREG(r14); DUMPREG(r15); Printf("\n");
# elif defined(__i386__)
#  define DUMPREG(r) DUMPREG32(r)
  DUMPREG(eax); DUMPREG(ebx); DUMPREG(ecx); DUMPREG(edx); Printf("\n");
  DUMPREG(edi); DUMPREG(esi); DUMPREG(ebp); DUMPREG(esp); Printf("\n");
# elif defined(__aarch64__)
#  define DUMPREG(r) DUMPREG64(r)
  DUMPREG_(x[0]); DUMPREG_(x[1]); DUMPREG_(x[2]); DUMPREG_(x[3]); Printf("\n");
  DUMPREG_(x[4]); DUMPREG_(x[5]); DUMPREG_(x[6]); DUMPREG_(x[7]); Printf("\n");
  DUMPREG_(x[8]); DUMPREG_(x[9]); DUMPREG(x[10]); DUMPREG(x[11]); Printf("\n");
  DUMPREG(x[12]); DUMPREG(x[13]); DUMPREG(x[14]); DUMPREG(x[15]); Printf("\n");
  DUMPREG(x[16]); DUMPREG(x[17]); DUMPREG(x[18]); DUMPREG(x[19]); Printf("\n");
  DUMPREG(x[20]); DUMPREG(x[21]); DUMPREG(x[22]); DUMPREG(x[23]); Printf("\n");
  DUMPREG(x[24]); DUMPREG(x[25]); DUMPREG(x[26]); DUMPREG(x[27]); Printf("\n");
  DUMPREG(x[28]); DUMPREG___(fp); DUMPREG___(lr); DUMPREG___(sp); Printf("\n");
# elif defined(__arm__)
#  define DUMPREG(r) DUMPREG32(r)
  DUMPREG_(r[0]); DUMPREG_(r[1]); DUMPREG_(r[2]); DUMPREG_(r[3]); Printf("\n");
  DUMPREG_(r[4]); DUMPREG_(r[5]); DUMPREG_(r[6]); DUMPREG_(r[7]); Printf("\n");
  DUMPREG_(r[8]); DUMPREG_(r[9]); DUMPREG(r[10]); DUMPREG(r[11]); Printf("\n");
  DUMPREG(r[12]); DUMPREG___(sp); DUMPREG___(lr); DUMPREG___(pc); Printf("\n");
# else
# error "Unknown architecture"
# endif

# undef DUMPREG64
# undef DUMPREG32
# undef DUMPREG_
# undef DUMPREG__
# undef DUMPREG___
# undef DUMPREG
}

static inline bool CompareBaseAddress(const LoadedModule &a,
                                      const LoadedModule &b) {
  return a.base_address() < b.base_address();
}

void FormatUUID(char *out, uptr size, const u8 *uuid) {
  internal_snprintf(out, size,
                    "<%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
                    "%02X%02X%02X%02X%02X%02X>",
                    uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
                    uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
                    uuid[12], uuid[13], uuid[14], uuid[15]);
}

void PrintModuleMap() {
  Printf("Process module map:\n");
  MemoryMappingLayout memory_mapping(false);
  InternalMmapVector<LoadedModule> modules;
  modules.reserve(128);
  memory_mapping.DumpListOfModules(&modules);
  Sort(modules.data(), modules.size(), CompareBaseAddress);
  for (uptr i = 0; i < modules.size(); ++i) {
    char uuid_str[128];
    FormatUUID(uuid_str, sizeof(uuid_str), modules[i].uuid());
    Printf("0x%zx-0x%zx %s (%s) %s\n", modules[i].base_address(),
           modules[i].max_executable_address(), modules[i].full_name(),
           ModuleArchToString(modules[i].arch()), uuid_str);
  }
  Printf("End of module map.\n");
}

void CheckNoDeepBind(const char *filename, int flag) {
  // Do nothing.
}

bool GetRandom(void *buffer, uptr length, bool blocking) {
  if (!buffer || !length || length > 256)
    return false;
  // arc4random never fails.
  arc4random_buf(buffer, length);
  return true;
}

u32 GetNumberOfCPUs() {
  return (u32)sysconf(_SC_NPROCESSORS_ONLN);
}

}  // namespace __sanitizer

#endif  // SANITIZER_MAC
