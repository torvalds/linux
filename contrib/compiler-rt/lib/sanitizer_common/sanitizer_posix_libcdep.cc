//===-- sanitizer_posix_libcdep.cc ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements libc-dependent POSIX-specific functions
// from sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_POSIX

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_platform_limits_netbsd.h"
#include "sanitizer_platform_limits_openbsd.h"
#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_platform_limits_solaris.h"
#include "sanitizer_posix.h"
#include "sanitizer_procmaps.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if SANITIZER_FREEBSD
// The MAP_NORESERVE define has been removed in FreeBSD 11.x, and even before
// that, it was never implemented.  So just define it to zero.
#undef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

typedef void (*sa_sigaction_t)(int, siginfo_t *, void *);

namespace __sanitizer {

u32 GetUid() {
  return getuid();
}

uptr GetThreadSelf() {
  return (uptr)pthread_self();
}

void ReleaseMemoryPagesToOS(uptr beg, uptr end) {
  uptr page_size = GetPageSizeCached();
  uptr beg_aligned = RoundUpTo(beg, page_size);
  uptr end_aligned = RoundDownTo(end, page_size);
  if (beg_aligned < end_aligned)
    // In the default Solaris compilation environment, madvise() is declared
    // to take a caddr_t arg; casting it to void * results in an invalid
    // conversion error, so use char * instead.
    madvise((char *)beg_aligned, end_aligned - beg_aligned,
            SANITIZER_MADVISE_DONTNEED);
}

bool NoHugePagesInRegion(uptr addr, uptr size) {
#ifdef MADV_NOHUGEPAGE  // May not be defined on old systems.
  return madvise((void *)addr, size, MADV_NOHUGEPAGE) == 0;
#else
  return true;
#endif  // MADV_NOHUGEPAGE
}

bool DontDumpShadowMemory(uptr addr, uptr length) {
#if defined(MADV_DONTDUMP)
  return madvise((void *)addr, length, MADV_DONTDUMP) == 0;
#elif defined(MADV_NOCORE)
  return madvise((void *)addr, length, MADV_NOCORE) == 0;
#else
  return true;
#endif  // MADV_DONTDUMP
}

static rlim_t getlim(int res) {
  rlimit rlim;
  CHECK_EQ(0, getrlimit(res, &rlim));
  return rlim.rlim_cur;
}

static void setlim(int res, rlim_t lim) {
  struct rlimit rlim;
  if (getrlimit(res, const_cast<struct rlimit *>(&rlim))) {
    Report("ERROR: %s getrlimit() failed %d\n", SanitizerToolName, errno);
    Die();
  }
  rlim.rlim_cur = lim;
  if (setrlimit(res, const_cast<struct rlimit *>(&rlim))) {
    Report("ERROR: %s setrlimit() failed %d\n", SanitizerToolName, errno);
    Die();
  }
}

void DisableCoreDumperIfNecessary() {
  if (common_flags()->disable_coredump) {
    setlim(RLIMIT_CORE, 0);
  }
}

bool StackSizeIsUnlimited() {
  rlim_t stack_size = getlim(RLIMIT_STACK);
  return (stack_size == RLIM_INFINITY);
}

uptr GetStackSizeLimitInBytes() {
  return (uptr)getlim(RLIMIT_STACK);
}

void SetStackSizeLimitInBytes(uptr limit) {
  setlim(RLIMIT_STACK, (rlim_t)limit);
  CHECK(!StackSizeIsUnlimited());
}

bool AddressSpaceIsUnlimited() {
  rlim_t as_size = getlim(RLIMIT_AS);
  return (as_size == RLIM_INFINITY);
}

void SetAddressSpaceUnlimited() {
  setlim(RLIMIT_AS, RLIM_INFINITY);
  CHECK(AddressSpaceIsUnlimited());
}

void SleepForSeconds(int seconds) {
  sleep(seconds);
}

void SleepForMillis(int millis) {
  usleep(millis * 1000);
}

void Abort() {
#if !SANITIZER_GO
  // If we are handling SIGABRT, unhandle it first.
  // TODO(vitalybuka): Check if handler belongs to sanitizer.
  if (GetHandleSignalMode(SIGABRT) != kHandleSignalNo) {
    struct sigaction sigact;
    internal_memset(&sigact, 0, sizeof(sigact));
    sigact.sa_sigaction = (sa_sigaction_t)SIG_DFL;
    internal_sigaction(SIGABRT, &sigact, nullptr);
  }
#endif

  abort();
}

int Atexit(void (*function)(void)) {
#if !SANITIZER_GO
  return atexit(function);
#else
  return 0;
#endif
}

bool SupportsColoredOutput(fd_t fd) {
  return isatty(fd) != 0;
}

#if !SANITIZER_GO
// TODO(glider): different tools may require different altstack size.
static const uptr kAltStackSize = SIGSTKSZ * 4;  // SIGSTKSZ is not enough.

void SetAlternateSignalStack() {
  stack_t altstack, oldstack;
  CHECK_EQ(0, sigaltstack(nullptr, &oldstack));
  // If the alternate stack is already in place, do nothing.
  // Android always sets an alternate stack, but it's too small for us.
  if (!SANITIZER_ANDROID && !(oldstack.ss_flags & SS_DISABLE)) return;
  // TODO(glider): the mapped stack should have the MAP_STACK flag in the
  // future. It is not required by man 2 sigaltstack now (they're using
  // malloc()).
  void* base = MmapOrDie(kAltStackSize, __func__);
  altstack.ss_sp = (char*) base;
  altstack.ss_flags = 0;
  altstack.ss_size = kAltStackSize;
  CHECK_EQ(0, sigaltstack(&altstack, nullptr));
}

void UnsetAlternateSignalStack() {
  stack_t altstack, oldstack;
  altstack.ss_sp = nullptr;
  altstack.ss_flags = SS_DISABLE;
  altstack.ss_size = kAltStackSize;  // Some sane value required on Darwin.
  CHECK_EQ(0, sigaltstack(&altstack, &oldstack));
  UnmapOrDie(oldstack.ss_sp, oldstack.ss_size);
}

static void MaybeInstallSigaction(int signum,
                                  SignalHandlerType handler) {
  if (GetHandleSignalMode(signum) == kHandleSignalNo) return;

  struct sigaction sigact;
  internal_memset(&sigact, 0, sizeof(sigact));
  sigact.sa_sigaction = (sa_sigaction_t)handler;
  // Do not block the signal from being received in that signal's handler.
  // Clients are responsible for handling this correctly.
  sigact.sa_flags = SA_SIGINFO | SA_NODEFER;
  if (common_flags()->use_sigaltstack) sigact.sa_flags |= SA_ONSTACK;
  CHECK_EQ(0, internal_sigaction(signum, &sigact, nullptr));
  VReport(1, "Installed the sigaction for signal %d\n", signum);
}

void InstallDeadlySignalHandlers(SignalHandlerType handler) {
  // Set the alternate signal stack for the main thread.
  // This will cause SetAlternateSignalStack to be called twice, but the stack
  // will be actually set only once.
  if (common_flags()->use_sigaltstack) SetAlternateSignalStack();
  MaybeInstallSigaction(SIGSEGV, handler);
  MaybeInstallSigaction(SIGBUS, handler);
  MaybeInstallSigaction(SIGABRT, handler);
  MaybeInstallSigaction(SIGFPE, handler);
  MaybeInstallSigaction(SIGILL, handler);
  MaybeInstallSigaction(SIGTRAP, handler);
}

bool SignalContext::IsStackOverflow() const {
  // Access at a reasonable offset above SP, or slightly below it (to account
  // for x86_64 or PowerPC redzone, ARM push of multiple registers, etc) is
  // probably a stack overflow.
#ifdef __s390__
  // On s390, the fault address in siginfo points to start of the page, not
  // to the precise word that was accessed.  Mask off the low bits of sp to
  // take it into account.
  bool IsStackAccess = addr >= (sp & ~0xFFF) && addr < sp + 0xFFFF;
#else
  // Let's accept up to a page size away from top of stack. Things like stack
  // probing can trigger accesses with such large offsets.
  bool IsStackAccess = addr + GetPageSizeCached() > sp && addr < sp + 0xFFFF;
#endif

#if __powerpc__
  // Large stack frames can be allocated with e.g.
  //   lis r0,-10000
  //   stdux r1,r1,r0 # store sp to [sp-10000] and update sp by -10000
  // If the store faults then sp will not have been updated, so test above
  // will not work, because the fault address will be more than just "slightly"
  // below sp.
  if (!IsStackAccess && IsAccessibleMemoryRange(pc, 4)) {
    u32 inst = *(unsigned *)pc;
    u32 ra = (inst >> 16) & 0x1F;
    u32 opcd = inst >> 26;
    u32 xo = (inst >> 1) & 0x3FF;
    // Check for store-with-update to sp. The instructions we accept are:
    //   stbu rs,d(ra)          stbux rs,ra,rb
    //   sthu rs,d(ra)          sthux rs,ra,rb
    //   stwu rs,d(ra)          stwux rs,ra,rb
    //   stdu rs,ds(ra)         stdux rs,ra,rb
    // where ra is r1 (the stack pointer).
    if (ra == 1 &&
        (opcd == 39 || opcd == 45 || opcd == 37 || opcd == 62 ||
         (opcd == 31 && (xo == 247 || xo == 439 || xo == 183 || xo == 181))))
      IsStackAccess = true;
  }
#endif  // __powerpc__

  // We also check si_code to filter out SEGV caused by something else other
  // then hitting the guard page or unmapped memory, like, for example,
  // unaligned memory access.
  auto si = static_cast<const siginfo_t *>(siginfo);
  return IsStackAccess &&
         (si->si_code == si_SEGV_MAPERR || si->si_code == si_SEGV_ACCERR);
}

#endif  // SANITIZER_GO

bool IsAccessibleMemoryRange(uptr beg, uptr size) {
  uptr page_size = GetPageSizeCached();
  // Checking too large memory ranges is slow.
  CHECK_LT(size, page_size * 10);
  int sock_pair[2];
  if (pipe(sock_pair))
    return false;
  uptr bytes_written =
      internal_write(sock_pair[1], reinterpret_cast<void *>(beg), size);
  int write_errno;
  bool result;
  if (internal_iserror(bytes_written, &write_errno)) {
    CHECK_EQ(EFAULT, write_errno);
    result = false;
  } else {
    result = (bytes_written == size);
  }
  internal_close(sock_pair[0]);
  internal_close(sock_pair[1]);
  return result;
}

void PlatformPrepareForSandboxing(__sanitizer_sandbox_arguments *args) {
  // Some kinds of sandboxes may forbid filesystem access, so we won't be able
  // to read the file mappings from /proc/self/maps. Luckily, neither the
  // process will be able to load additional libraries, so it's fine to use the
  // cached mappings.
  MemoryMappingLayout::CacheMemoryMappings();
}

#if SANITIZER_ANDROID || SANITIZER_GO
int GetNamedMappingFd(const char *name, uptr size) {
  return -1;
}
#else
int GetNamedMappingFd(const char *name, uptr size) {
  if (!common_flags()->decorate_proc_maps)
    return -1;
  char shmname[200];
  CHECK(internal_strlen(name) < sizeof(shmname) - 10);
  internal_snprintf(shmname, sizeof(shmname), "%zu [%s]", internal_getpid(),
                    name);
  int fd = shm_open(shmname, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
  CHECK_GE(fd, 0);
  int res = internal_ftruncate(fd, size);
  CHECK_EQ(0, res);
  res = shm_unlink(shmname);
  CHECK_EQ(0, res);
  return fd;
}
#endif

bool MmapFixedNoReserve(uptr fixed_addr, uptr size, const char *name) {
  int fd = name ? GetNamedMappingFd(name, size) : -1;
  unsigned flags = MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE;
  if (fd == -1) flags |= MAP_ANON;

  uptr PageSize = GetPageSizeCached();
  uptr p = internal_mmap((void *)(fixed_addr & ~(PageSize - 1)),
                         RoundUpTo(size, PageSize), PROT_READ | PROT_WRITE,
                         flags, fd, 0);
  int reserrno;
  if (internal_iserror(p, &reserrno)) {
    Report("ERROR: %s failed to "
           "allocate 0x%zx (%zd) bytes at address %zx (errno: %d)\n",
           SanitizerToolName, size, size, fixed_addr, reserrno);
    return false;
  }
  IncreaseTotalMmap(size);
  return true;
}

uptr ReservedAddressRange::Init(uptr size, const char *name, uptr fixed_addr) {
  // We don't pass `name` along because, when you enable `decorate_proc_maps`
  // AND actually use a named mapping AND are using a sanitizer intercepting
  // `open` (e.g. TSAN, ESAN), then you'll get a failure during initialization.
  // TODO(flowerhack): Fix the implementation of GetNamedMappingFd to solve
  // this problem.
  base_ = fixed_addr ? MmapFixedNoAccess(fixed_addr, size) : MmapNoAccess(size);
  size_ = size;
  name_ = name;
  (void)os_handle_;  // unsupported
  return reinterpret_cast<uptr>(base_);
}

// Uses fixed_addr for now.
// Will use offset instead once we've implemented this function for real.
uptr ReservedAddressRange::Map(uptr fixed_addr, uptr size) {
  return reinterpret_cast<uptr>(MmapFixedOrDieOnFatalError(fixed_addr, size));
}

uptr ReservedAddressRange::MapOrDie(uptr fixed_addr, uptr size) {
  return reinterpret_cast<uptr>(MmapFixedOrDie(fixed_addr, size));
}

void ReservedAddressRange::Unmap(uptr addr, uptr size) {
  CHECK_LE(size, size_);
  if (addr == reinterpret_cast<uptr>(base_))
    // If we unmap the whole range, just null out the base.
    base_ = (size == size_) ? nullptr : reinterpret_cast<void*>(addr + size);
  else
    CHECK_EQ(addr + size, reinterpret_cast<uptr>(base_) + size_);
  size_ -= size;
  UnmapOrDie(reinterpret_cast<void*>(addr), size);
}

void *MmapFixedNoAccess(uptr fixed_addr, uptr size, const char *name) {
  int fd = name ? GetNamedMappingFd(name, size) : -1;
  unsigned flags = MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE;
  if (fd == -1) flags |= MAP_ANON;

  return (void *)internal_mmap((void *)fixed_addr, size, PROT_NONE, flags, fd,
                               0);
}

void *MmapNoAccess(uptr size) {
  unsigned flags = MAP_PRIVATE | MAP_ANON | MAP_NORESERVE;
  return (void *)internal_mmap(nullptr, size, PROT_NONE, flags, -1, 0);
}

// This function is defined elsewhere if we intercepted pthread_attr_getstack.
extern "C" {
SANITIZER_WEAK_ATTRIBUTE int
real_pthread_attr_getstack(void *attr, void **addr, size_t *size);
} // extern "C"

int my_pthread_attr_getstack(void *attr, void **addr, uptr *size) {
#if !SANITIZER_GO && !SANITIZER_MAC
  if (&real_pthread_attr_getstack)
    return real_pthread_attr_getstack((pthread_attr_t *)attr, addr,
                                      (size_t *)size);
#endif
  return pthread_attr_getstack((pthread_attr_t *)attr, addr, (size_t *)size);
}

#if !SANITIZER_GO
void AdjustStackSize(void *attr_) {
  pthread_attr_t *attr = (pthread_attr_t *)attr_;
  uptr stackaddr = 0;
  uptr stacksize = 0;
  my_pthread_attr_getstack(attr, (void**)&stackaddr, &stacksize);
  // GLibC will return (0 - stacksize) as the stack address in the case when
  // stacksize is set, but stackaddr is not.
  bool stack_set = (stackaddr != 0) && (stackaddr + stacksize != 0);
  // We place a lot of tool data into TLS, account for that.
  const uptr minstacksize = GetTlsSize() + 128*1024;
  if (stacksize < minstacksize) {
    if (!stack_set) {
      if (stacksize != 0) {
        VPrintf(1, "Sanitizer: increasing stacksize %zu->%zu\n", stacksize,
                minstacksize);
        pthread_attr_setstacksize(attr, minstacksize);
      }
    } else {
      Printf("Sanitizer: pre-allocated stack size is insufficient: "
             "%zu < %zu\n", stacksize, minstacksize);
      Printf("Sanitizer: pthread_create is likely to fail.\n");
    }
  }
}
#endif // !SANITIZER_GO

pid_t StartSubprocess(const char *program, const char *const argv[],
                      fd_t stdin_fd, fd_t stdout_fd, fd_t stderr_fd) {
  auto file_closer = at_scope_exit([&] {
    if (stdin_fd != kInvalidFd) {
      internal_close(stdin_fd);
    }
    if (stdout_fd != kInvalidFd) {
      internal_close(stdout_fd);
    }
    if (stderr_fd != kInvalidFd) {
      internal_close(stderr_fd);
    }
  });

  int pid = internal_fork();

  if (pid < 0) {
    int rverrno;
    if (internal_iserror(pid, &rverrno)) {
      Report("WARNING: failed to fork (errno %d)\n", rverrno);
    }
    return pid;
  }

  if (pid == 0) {
    // Child subprocess
    if (stdin_fd != kInvalidFd) {
      internal_close(STDIN_FILENO);
      internal_dup2(stdin_fd, STDIN_FILENO);
      internal_close(stdin_fd);
    }
    if (stdout_fd != kInvalidFd) {
      internal_close(STDOUT_FILENO);
      internal_dup2(stdout_fd, STDOUT_FILENO);
      internal_close(stdout_fd);
    }
    if (stderr_fd != kInvalidFd) {
      internal_close(STDERR_FILENO);
      internal_dup2(stderr_fd, STDERR_FILENO);
      internal_close(stderr_fd);
    }

    for (int fd = sysconf(_SC_OPEN_MAX); fd > 2; fd--) internal_close(fd);

    execv(program, const_cast<char **>(&argv[0]));
    internal__exit(1);
  }

  return pid;
}

bool IsProcessRunning(pid_t pid) {
  int process_status;
  uptr waitpid_status = internal_waitpid(pid, &process_status, WNOHANG);
  int local_errno;
  if (internal_iserror(waitpid_status, &local_errno)) {
    VReport(1, "Waiting on the process failed (errno %d).\n", local_errno);
    return false;
  }
  return waitpid_status == 0;
}

int WaitForProcess(pid_t pid) {
  int process_status;
  uptr waitpid_status = internal_waitpid(pid, &process_status, 0);
  int local_errno;
  if (internal_iserror(waitpid_status, &local_errno)) {
    VReport(1, "Waiting on the process failed (errno %d).\n", local_errno);
    return -1;
  }
  return process_status;
}

bool IsStateDetached(int state) {
  return state == PTHREAD_CREATE_DETACHED;
}

} // namespace __sanitizer

#endif // SANITIZER_POSIX
