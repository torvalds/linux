//===-- esan_interceptors.cpp ---------------------------------------------===//
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
// Interception routines for the esan run-time.
//===----------------------------------------------------------------------===//

#include "esan.h"
#include "esan_shadow.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

using namespace __esan; // NOLINT

#define CUR_PC() (StackTrace::GetCurrentPc())

//===----------------------------------------------------------------------===//
// Interception via sanitizer common interceptors
//===----------------------------------------------------------------------===//

// Get the per-platform defines for what is possible to intercept
#include "sanitizer_common/sanitizer_platform_interceptors.h"

DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr)

// TODO(bruening): tsan disables several interceptors (getpwent, etc.) claiming
// that interception is a perf hit: should we do the same?

// We have no need to intercept:
#undef SANITIZER_INTERCEPT_TLS_GET_ADDR

// TODO(bruening): the common realpath interceptor assumes malloc is
// intercepted!  We should try to parametrize that, though we'll
// intercept malloc soon ourselves and can then remove this undef.
#undef SANITIZER_INTERCEPT_REALPATH

// We provide our own version:
#undef SANITIZER_INTERCEPT_SIGPROCMASK

#define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (!EsanIsInitialized)

#define COMMON_INTERCEPT_FUNCTION(name) INTERCEPT_FUNCTION(name)
#define COMMON_INTERCEPT_FUNCTION_VER(name, ver)                          \
  INTERCEPT_FUNCTION_VER(name, ver)

// We must initialize during early interceptors, to support tcmalloc.
// This means that for some apps we fully initialize prior to
// __esan_init() being called.
// We currently do not use ctx.
#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)                               \
  do {                                                                         \
    if (UNLIKELY(COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)) {                 \
      if (!UNLIKELY(EsanDuringInit))                                           \
        initializeLibrary(__esan_which_tool);                                  \
      return REAL(func)(__VA_ARGS__);                                          \
    }                                                                          \
    ctx = nullptr;                                                             \
    (void)ctx;                                                                 \
  } while (false)

#define COMMON_INTERCEPTOR_ENTER_NOIGNORE(ctx, func, ...)                      \
  COMMON_INTERCEPTOR_ENTER(ctx, func, __VA_ARGS__)

#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size)                         \
  processRangeAccess(CUR_PC(), (uptr)ptr, size, true)

#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size)                          \
  processRangeAccess(CUR_PC(), (uptr)ptr, size, false)

// This is only called if the app explicitly calls exit(), not on
// a normal exit.
#define COMMON_INTERCEPTOR_ON_EXIT(ctx) finalizeLibrary()

#define COMMON_INTERCEPTOR_FILE_OPEN(ctx, file, path)                          \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(file);                                                              \
    (void)(path);                                                              \
  } while (false)
#define COMMON_INTERCEPTOR_FILE_CLOSE(ctx, file)                               \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(file);                                                              \
  } while (false)
#define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle)                    \
  do {                                                                         \
    (void)(filename);                                                          \
    (void)(handle);                                                            \
  } while (false)
#define COMMON_INTERCEPTOR_LIBRARY_UNLOADED()                                  \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_ACQUIRE(ctx, u)                                     \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(u);                                                                 \
  } while (false)
#define COMMON_INTERCEPTOR_RELEASE(ctx, u)                                     \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(u);                                                                 \
  } while (false)
#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path)                              \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(path);                                                              \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd)                                 \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(fd);                                                                \
  } while (false)
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd)                                 \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(fd);                                                                \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd)                                  \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(fd);                                                                \
  } while (false)
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd)                    \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(fd);                                                                \
    (void)(newfd);                                                             \
  } while (false)
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name)                          \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(name);                                                              \
  } while (false)
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name)                 \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(thread);                                                            \
    (void)(name);                                                              \
  } while (false)
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
#define COMMON_INTERCEPTOR_MUTEX_LOCK(ctx, m)                                  \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(m);                                                                 \
  } while (false)
#define COMMON_INTERCEPTOR_MUTEX_UNLOCK(ctx, m)                                \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(m);                                                                 \
  } while (false)
#define COMMON_INTERCEPTOR_MUTEX_REPAIR(ctx, m)                                \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(m);                                                                 \
  } while (false)
#define COMMON_INTERCEPTOR_HANDLE_RECVMSG(ctx, msg)                            \
  do {                                                                         \
    (void)(ctx);                                                               \
    (void)(msg);                                                               \
  } while (false)
#define COMMON_INTERCEPTOR_USER_CALLBACK_START()                               \
  do {                                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_USER_CALLBACK_END()                                 \
  do {                                                                         \
  } while (false)

#define COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, sz, prot, flags, fd,     \
                                     off)                                      \
  do {                                                                         \
    if (!fixMmapAddr(&addr, sz, flags))                                        \
      return (void *)-1;                                                       \
    void *result = REAL(mmap)(addr, sz, prot, flags, fd, off);                 \
    return (void *)checkMmapResult((uptr)result, sz);                          \
  } while (false)

#include "sanitizer_common/sanitizer_common_interceptors.inc"

//===----------------------------------------------------------------------===//
// Syscall interception
//===----------------------------------------------------------------------===//

// We want the caller's PC b/c unlike the other function interceptors these
// are separate pre and post functions called around the app's syscall().

#define COMMON_SYSCALL_PRE_READ_RANGE(ptr, size)                               \
  processRangeAccess(GET_CALLER_PC(), (uptr)ptr, size, false)

#define COMMON_SYSCALL_PRE_WRITE_RANGE(ptr, size)                              \
  do {                                                                         \
    (void)(ptr);                                                               \
    (void)(size);                                                              \
  } while (false)

#define COMMON_SYSCALL_POST_READ_RANGE(ptr, size)                              \
  do {                                                                         \
    (void)(ptr);                                                               \
    (void)(size);                                                              \
  } while (false)

// The actual amount written is in post, not pre.
#define COMMON_SYSCALL_POST_WRITE_RANGE(ptr, size)                             \
  processRangeAccess(GET_CALLER_PC(), (uptr)ptr, size, true)

#define COMMON_SYSCALL_ACQUIRE(addr)                                           \
  do {                                                                         \
    (void)(addr);                                                              \
  } while (false)
#define COMMON_SYSCALL_RELEASE(addr)                                           \
  do {                                                                         \
    (void)(addr);                                                              \
  } while (false)
#define COMMON_SYSCALL_FD_CLOSE(fd)                                            \
  do {                                                                         \
    (void)(fd);                                                                \
  } while (false)
#define COMMON_SYSCALL_FD_ACQUIRE(fd)                                          \
  do {                                                                         \
    (void)(fd);                                                                \
  } while (false)
#define COMMON_SYSCALL_FD_RELEASE(fd)                                          \
  do {                                                                         \
    (void)(fd);                                                                \
  } while (false)
#define COMMON_SYSCALL_PRE_FORK()                                              \
  do {                                                                         \
  } while (false)
#define COMMON_SYSCALL_POST_FORK(res)                                          \
  do {                                                                         \
    (void)(res);                                                               \
  } while (false)

#include "sanitizer_common/sanitizer_common_syscalls.inc"
#include "sanitizer_common/sanitizer_syscalls_netbsd.inc"

//===----------------------------------------------------------------------===//
// Custom interceptors
//===----------------------------------------------------------------------===//

// TODO(bruening): move more of these to the common interception pool as they
// are shared with tsan and asan.
// While our other files match LLVM style, here we match sanitizer style as we
// expect to move these to the common pool.

INTERCEPTOR(char *, strcpy, char *dst, const char *src) { // NOLINT
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strcpy, dst, src);
  uptr srclen = internal_strlen(src);
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, srclen + 1);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, src, srclen + 1);
  return REAL(strcpy)(dst, src); // NOLINT
}

INTERCEPTOR(char *, strncpy, char *dst, char *src, uptr n) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strncpy, dst, src, n);
  uptr srclen = internal_strnlen(src, n);
  uptr copied_size = srclen + 1 > n ? n : srclen + 1;
  COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, copied_size);
  COMMON_INTERCEPTOR_READ_RANGE(ctx, src, copied_size);
  return REAL(strncpy)(dst, src, n);
}

INTERCEPTOR(int, open, const char *name, int flags, int mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, open, name, flags, mode);
  COMMON_INTERCEPTOR_READ_STRING(ctx, name, 0);
  return REAL(open)(name, flags, mode);
}

#if SANITIZER_LINUX
INTERCEPTOR(int, open64, const char *name, int flags, int mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, open64, name, flags, mode);
  COMMON_INTERCEPTOR_READ_STRING(ctx, name, 0);
  return REAL(open64)(name, flags, mode);
}
#define ESAN_MAYBE_INTERCEPT_OPEN64 INTERCEPT_FUNCTION(open64)
#else
#define ESAN_MAYBE_INTERCEPT_OPEN64
#endif

INTERCEPTOR(int, creat, const char *name, int mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, creat, name, mode);
  COMMON_INTERCEPTOR_READ_STRING(ctx, name, 0);
  return REAL(creat)(name, mode);
}

#if SANITIZER_LINUX
INTERCEPTOR(int, creat64, const char *name, int mode) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, creat64, name, mode);
  COMMON_INTERCEPTOR_READ_STRING(ctx, name, 0);
  return REAL(creat64)(name, mode);
}
#define ESAN_MAYBE_INTERCEPT_CREAT64 INTERCEPT_FUNCTION(creat64)
#else
#define ESAN_MAYBE_INTERCEPT_CREAT64
#endif

INTERCEPTOR(int, unlink, char *path) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, unlink, path);
  COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  return REAL(unlink)(path);
}

INTERCEPTOR(int, rmdir, char *path) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, rmdir, path);
  COMMON_INTERCEPTOR_READ_STRING(ctx, path, 0);
  return REAL(rmdir)(path);
}

//===----------------------------------------------------------------------===//
// Signal-related interceptors
//===----------------------------------------------------------------------===//

#if SANITIZER_LINUX || SANITIZER_FREEBSD
typedef void (*signal_handler_t)(int);
INTERCEPTOR(signal_handler_t, signal, int signum, signal_handler_t handler) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, signal, signum, handler);
  signal_handler_t result;
  if (!processSignal(signum, handler, &result))
    return result;
  else
    return REAL(signal)(signum, handler);
}
#define ESAN_MAYBE_INTERCEPT_SIGNAL INTERCEPT_FUNCTION(signal)
#else
#error Platform not supported
#define ESAN_MAYBE_INTERCEPT_SIGNAL
#endif

#if SANITIZER_LINUX || SANITIZER_FREEBSD
DECLARE_REAL(int, sigaction, int signum, const struct sigaction *act,
             struct sigaction *oldact)
INTERCEPTOR(int, sigaction, int signum, const struct sigaction *act,
            struct sigaction *oldact) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigaction, signum, act, oldact);
  if (!processSigaction(signum, act, oldact))
    return 0;
  else
    return REAL(sigaction)(signum, act, oldact);
}

// This is required to properly use internal_sigaction.
namespace __sanitizer {
int real_sigaction(int signum, const void *act, void *oldact) {
  if (REAL(sigaction) == nullptr) {
    // With an instrumented allocator, this is called during interceptor init
    // and we need a raw syscall solution.
#if SANITIZER_LINUX
    return internal_sigaction_syscall(signum, act, oldact);
#else
    return internal_sigaction(signum, act, oldact);
#endif
  }
  return REAL(sigaction)(signum, (const struct sigaction *)act,
                         (struct sigaction *)oldact);
}
} // namespace __sanitizer

#define ESAN_MAYBE_INTERCEPT_SIGACTION INTERCEPT_FUNCTION(sigaction)
#else
#error Platform not supported
#define ESAN_MAYBE_INTERCEPT_SIGACTION
#endif

#if SANITIZER_LINUX || SANITIZER_FREEBSD
INTERCEPTOR(int, sigprocmask, int how, __sanitizer_sigset_t *set,
            __sanitizer_sigset_t *oldset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, sigprocmask, how, set, oldset);
  int res = 0;
  if (processSigprocmask(how, set, oldset))
    res = REAL(sigprocmask)(how, set, oldset);
  if (!res && oldset)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldset, sizeof(*oldset));
  return res;
}
#define ESAN_MAYBE_INTERCEPT_SIGPROCMASK INTERCEPT_FUNCTION(sigprocmask)
#else
#define ESAN_MAYBE_INTERCEPT_SIGPROCMASK
#endif

#if !SANITIZER_WINDOWS
INTERCEPTOR(int, pthread_sigmask, int how, __sanitizer_sigset_t *set,
            __sanitizer_sigset_t *oldset) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, pthread_sigmask, how, set, oldset);
  int res = 0;
  if (processSigprocmask(how, set, oldset))
    res = REAL(sigprocmask)(how, set, oldset);
  if (!res && oldset)
    COMMON_INTERCEPTOR_WRITE_RANGE(ctx, oldset, sizeof(*oldset));
  return res;
}
#define ESAN_MAYBE_INTERCEPT_PTHREAD_SIGMASK INTERCEPT_FUNCTION(pthread_sigmask)
#else
#define ESAN_MAYBE_INTERCEPT_PTHREAD_SIGMASK
#endif

//===----------------------------------------------------------------------===//
// Malloc interceptors
//===----------------------------------------------------------------------===//

static const uptr early_alloc_buf_size = 4096;
static uptr allocated_bytes;
static char early_alloc_buf[early_alloc_buf_size];

static bool isInEarlyAllocBuf(const void *ptr) {
  return ((uptr)ptr >= (uptr)early_alloc_buf &&
          ((uptr)ptr - (uptr)early_alloc_buf) < sizeof(early_alloc_buf));
}

static void *handleEarlyAlloc(uptr size) {
  // If esan is initialized during an interceptor (which happens with some
  // tcmalloc implementations that call pthread_mutex_lock), the call from
  // dlsym to calloc will deadlock.
  // dlsym may also call malloc before REAL(malloc) is retrieved from dlsym.
  // We work around it by using a static buffer for the early malloc/calloc
  // requests.
  // This solution will also allow us to deliberately intercept malloc & family
  // in the future (to perform tool actions on each allocation, without
  // replacing the allocator), as it also solves the problem of intercepting
  // calloc when it will itself be called before its REAL pointer is
  // initialized.
  // We do not handle multiple threads here.  This only happens at process init
  // time, and while it's possible for a shared library to create early threads
  // that race here, we consider that to be a corner case extreme enough that
  // it's not worth the effort to handle.
  void *mem = (void *)&early_alloc_buf[allocated_bytes];
  allocated_bytes += size;
  CHECK_LT(allocated_bytes, early_alloc_buf_size);
  return mem;
}

INTERCEPTOR(void*, calloc, uptr size, uptr n) {
  if (EsanDuringInit && REAL(calloc) == nullptr)
    return handleEarlyAlloc(size * n);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, calloc, size, n);
  void *res = REAL(calloc)(size, n);
  // The memory is zeroed and thus is all written.
  COMMON_INTERCEPTOR_WRITE_RANGE(nullptr, (uptr)res, size * n);
  return res;
}

INTERCEPTOR(void*, malloc, uptr size) {
  if (EsanDuringInit && REAL(malloc) == nullptr)
    return handleEarlyAlloc(size);
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, malloc, size);
  return REAL(malloc)(size);
}

INTERCEPTOR(void, free, void *p) {
  void *ctx;
  // There are only a few early allocation requests, so we simply skip the free.
  if (isInEarlyAllocBuf(p))
    return;
  COMMON_INTERCEPTOR_ENTER(ctx, free, p);
  REAL(free)(p);
}

namespace __esan {

void initializeInterceptors() {
  InitializeCommonInterceptors();

  INTERCEPT_FUNCTION(strcpy); // NOLINT
  INTERCEPT_FUNCTION(strncpy);

  INTERCEPT_FUNCTION(open);
  ESAN_MAYBE_INTERCEPT_OPEN64;
  INTERCEPT_FUNCTION(creat);
  ESAN_MAYBE_INTERCEPT_CREAT64;
  INTERCEPT_FUNCTION(unlink);
  INTERCEPT_FUNCTION(rmdir);

  ESAN_MAYBE_INTERCEPT_SIGNAL;
  ESAN_MAYBE_INTERCEPT_SIGACTION;
  ESAN_MAYBE_INTERCEPT_SIGPROCMASK;
  ESAN_MAYBE_INTERCEPT_PTHREAD_SIGMASK;

  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(free);

  // TODO(bruening): intercept routines that other sanitizers intercept that
  // are not in the common pool or here yet, ideally by adding to the common
  // pool.  Examples include wcslen and bcopy.

  // TODO(bruening): there are many more libc routines that read or write data
  // structures that no sanitizer is intercepting: sigaction, strtol, etc.
}

} // namespace __esan
