//===-- sanitizer_solaris.cc ----------------------------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements Solaris-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_SOLARIS

#include <stdio.h>

#include "sanitizer_common.h"
#include "sanitizer_flags.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_platform_limits_posix.h"
#include "sanitizer_procmaps.h"

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <thread.h>
#include <synch.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

namespace __sanitizer {

//#include "sanitizer_syscall_generic.inc"

#define _REAL(func) _ ## func
#define DECLARE__REAL(ret_type, func, ...) \
  extern "C" ret_type _REAL(func)(__VA_ARGS__)
#define DECLARE__REAL_AND_INTERNAL(ret_type, func, ...) \
  DECLARE__REAL(ret_type, func, __VA_ARGS__); \
  ret_type internal_ ## func(__VA_ARGS__)

#if !defined(_LP64) && _FILE_OFFSET_BITS == 64
#define _REAL64(func) _ ## func ## 64
#else
#define _REAL64(func) _REAL(func)
#endif
#define DECLARE__REAL64(ret_type, func, ...) \
  extern "C" ret_type _REAL64(func)(__VA_ARGS__)
#define DECLARE__REAL_AND_INTERNAL64(ret_type, func, ...) \
  DECLARE__REAL64(ret_type, func, __VA_ARGS__); \
  ret_type internal_ ## func(__VA_ARGS__)

// ---------------------- sanitizer_libc.h
DECLARE__REAL_AND_INTERNAL64(uptr, mmap, void *addr, uptr /*size_t*/ length,
                             int prot, int flags, int fd, OFF_T offset) {
  return (uptr)_REAL64(mmap)(addr, length, prot, flags, fd, offset);
}

DECLARE__REAL_AND_INTERNAL(uptr, munmap, void *addr, uptr length) {
  return _REAL(munmap)(addr, length);
}

DECLARE__REAL_AND_INTERNAL(int, mprotect, void *addr, uptr length, int prot) {
  return _REAL(mprotect)(addr, length, prot);
}

DECLARE__REAL_AND_INTERNAL(uptr, close, fd_t fd) {
  return _REAL(close)(fd);
}

extern "C" int _REAL64(open)(const char *, int, ...);

uptr internal_open(const char *filename, int flags) {
  return _REAL64(open)(filename, flags);
}

uptr internal_open(const char *filename, int flags, u32 mode) {
  return _REAL64(open)(filename, flags, mode);
}

uptr OpenFile(const char *filename, bool write) {
  return ReserveStandardFds(
      internal_open(filename, write ? O_WRONLY | O_CREAT : O_RDONLY, 0660));
}

DECLARE__REAL_AND_INTERNAL(uptr, read, fd_t fd, void *buf, uptr count) {
  return _REAL(read)(fd, buf, count);
}

DECLARE__REAL_AND_INTERNAL(uptr, write, fd_t fd, const void *buf, uptr count) {
  return _REAL(write)(fd, buf, count);
}

// FIXME: There's only _ftruncate64 beginning with Solaris 11.
DECLARE__REAL_AND_INTERNAL(uptr, ftruncate, fd_t fd, uptr size) {
  return ftruncate(fd, size);
}

DECLARE__REAL_AND_INTERNAL64(uptr, stat, const char *path, void *buf) {
  return _REAL64(stat)(path, (struct stat *)buf);
}

DECLARE__REAL_AND_INTERNAL64(uptr, lstat, const char *path, void *buf) {
  return _REAL64(lstat)(path, (struct stat *)buf);
}

DECLARE__REAL_AND_INTERNAL64(uptr, fstat, fd_t fd, void *buf) {
  return _REAL64(fstat)(fd, (struct stat *)buf);
}

uptr internal_filesize(fd_t fd) {
  struct stat st;
  if (internal_fstat(fd, &st))
    return -1;
  return (uptr)st.st_size;
}

DECLARE__REAL_AND_INTERNAL(uptr, dup2, int oldfd, int newfd) {
  return _REAL(dup2)(oldfd, newfd);
}

DECLARE__REAL_AND_INTERNAL(uptr, readlink, const char *path, char *buf,
                           uptr bufsize) {
  return _REAL(readlink)(path, buf, bufsize);
}

DECLARE__REAL_AND_INTERNAL(uptr, unlink, const char *path) {
  return _REAL(unlink)(path);
}

DECLARE__REAL_AND_INTERNAL(uptr, rename, const char *oldpath,
                           const char *newpath) {
  return _REAL(rename)(oldpath, newpath);
}

DECLARE__REAL_AND_INTERNAL(uptr, sched_yield, void) {
  return sched_yield();
}

DECLARE__REAL_AND_INTERNAL(void, _exit, int exitcode) {
  _exit(exitcode);
}

DECLARE__REAL_AND_INTERNAL(uptr, execve, const char *filename,
                           char *const argv[], char *const envp[]) {
  return _REAL(execve)(filename, argv, envp);
}

DECLARE__REAL_AND_INTERNAL(uptr, waitpid, int pid, int *status, int options) {
  return _REAL(waitpid)(pid, status, options);
}

DECLARE__REAL_AND_INTERNAL(uptr, getpid, void) {
  return _REAL(getpid)();
}

// FIXME: This might be wrong: _getdents doesn't take a struct linux_dirent *.
DECLARE__REAL_AND_INTERNAL64(uptr, getdents, fd_t fd, struct linux_dirent *dirp,
                             unsigned int count) {
  return _REAL64(getdents)(fd, dirp, count);
}

DECLARE__REAL_AND_INTERNAL64(uptr, lseek, fd_t fd, OFF_T offset, int whence) {
  return _REAL64(lseek)(fd, offset, whence);
}

// FIXME: This might be wrong: _sigfillset doesn't take a
// __sanitizer_sigset_t *.
DECLARE__REAL_AND_INTERNAL(void, sigfillset, __sanitizer_sigset_t *set) {
  _REAL(sigfillset)(set);
}

// FIXME: This might be wrong: _sigprocmask doesn't take __sanitizer_sigset_t *.
DECLARE__REAL_AND_INTERNAL(uptr, sigprocmask, int how,
                           __sanitizer_sigset_t *set,
                           __sanitizer_sigset_t *oldset) {
  return _REAL(sigprocmask)(how, set, oldset);
}

DECLARE__REAL_AND_INTERNAL(int, fork, void) {
  // TODO(glider): this may call user's pthread_atfork() handlers which is bad.
  return _REAL(fork)();
}

u64 NanoTime() {
  return gethrtime();
}

uptr internal_clock_gettime(__sanitizer_clockid_t clk_id, void *tp) {
  // FIXME: No internal variant.
  return clock_gettime(clk_id, (timespec *)tp);
}

// ----------------- sanitizer_common.h
BlockingMutex::BlockingMutex() {
  CHECK(sizeof(mutex_t) <= sizeof(opaque_storage_));
  internal_memset(this, 0, sizeof(*this));
  CHECK_EQ(mutex_init((mutex_t *)&opaque_storage_, USYNC_THREAD, NULL), 0);
}

void BlockingMutex::Lock() {
  CHECK(sizeof(mutex_t) <= sizeof(opaque_storage_));
  CHECK_NE(owner_, (uptr)thr_self());
  CHECK_EQ(mutex_lock((mutex_t *)&opaque_storage_), 0);
  CHECK(!owner_);
  owner_ = (uptr)thr_self();
}

void BlockingMutex::Unlock() {
  CHECK(owner_ == (uptr)thr_self());
  owner_ = 0;
  CHECK_EQ(mutex_unlock((mutex_t *)&opaque_storage_), 0);
}

void BlockingMutex::CheckLocked() {
  CHECK_EQ((uptr)thr_self(), owner_);
}

}  // namespace __sanitizer

#endif  // SANITIZER_SOLARIS
