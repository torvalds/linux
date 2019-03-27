//===-- sanitizer_rtems.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements RTEMS-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_rtems.h"
#if SANITIZER_RTEMS

#define posix_memalign __real_posix_memalign
#define free __real_free
#define memset __real_memset

#include "sanitizer_file.h"
#include "sanitizer_symbolizer.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// There is no mmap on RTEMS.  Use memalign, etc.
#define __mmap_alloc_aligned posix_memalign
#define __mmap_free free
#define __mmap_memset memset

namespace __sanitizer {

#include "sanitizer_syscall_generic.inc"

void NORETURN internal__exit(int exitcode) {
  _exit(exitcode);
}

uptr internal_sched_yield() {
  return sched_yield();
}

uptr internal_getpid() {
  return getpid();
}

bool FileExists(const char *filename) {
  struct stat st;
  if (stat(filename, &st))
    return false;
  // Sanity check: filename is a regular file.
  return S_ISREG(st.st_mode);
}

uptr GetThreadSelf() { return static_cast<uptr>(pthread_self()); }

tid_t GetTid() { return GetThreadSelf(); }

void Abort() { abort(); }

int Atexit(void (*function)(void)) { return atexit(function); }

void SleepForSeconds(int seconds) { sleep(seconds); }

void SleepForMillis(int millis) { usleep(millis * 1000); }

bool SupportsColoredOutput(fd_t fd) { return false; }

void GetThreadStackTopAndBottom(bool at_initialization,
                                uptr *stack_top, uptr *stack_bottom) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  CHECK_EQ(pthread_getattr_np(pthread_self(), &attr), 0);
  void *base = nullptr;
  size_t size = 0;
  CHECK_EQ(pthread_attr_getstack(&attr, &base, &size), 0);
  CHECK_EQ(pthread_attr_destroy(&attr), 0);

  *stack_bottom = reinterpret_cast<uptr>(base);
  *stack_top = *stack_bottom + size;
}

void GetThreadStackAndTls(bool main, uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
  uptr stack_top, stack_bottom;
  GetThreadStackTopAndBottom(main, &stack_top, &stack_bottom);
  *stk_addr = stack_bottom;
  *stk_size = stack_top - stack_bottom;
  *tls_addr = *tls_size = 0;
}

void InitializePlatformEarly() {}
void MaybeReexec() {}
void CheckASLR() {}
void CheckMPROTECT() {}
void DisableCoreDumperIfNecessary() {}
void InstallDeadlySignalHandlers(SignalHandlerType handler) {}
void SetAlternateSignalStack() {}
void UnsetAlternateSignalStack() {}
void InitTlsSize() {}

void PrintModuleMap() {}

void SignalContext::DumpAllRegisters(void *context) {}
const char *DescribeSignalOrException(int signo) { UNIMPLEMENTED(); }

enum MutexState { MtxUnlocked = 0, MtxLocked = 1, MtxSleeping = 2 };

BlockingMutex::BlockingMutex() {
  internal_memset(this, 0, sizeof(*this));
}

void BlockingMutex::Lock() {
  CHECK_EQ(owner_, 0);
  atomic_uint32_t *m = reinterpret_cast<atomic_uint32_t *>(&opaque_storage_);
  if (atomic_exchange(m, MtxLocked, memory_order_acquire) == MtxUnlocked)
    return;
  while (atomic_exchange(m, MtxSleeping, memory_order_acquire) != MtxUnlocked) {
    internal_sched_yield();
  }
}

void BlockingMutex::Unlock() {
  atomic_uint32_t *m = reinterpret_cast<atomic_uint32_t *>(&opaque_storage_);
  u32 v = atomic_exchange(m, MtxUnlocked, memory_order_release);
  CHECK_NE(v, MtxUnlocked);
}

void BlockingMutex::CheckLocked() {
  atomic_uint32_t *m = reinterpret_cast<atomic_uint32_t *>(&opaque_storage_);
  CHECK_NE(MtxUnlocked, atomic_load(m, memory_order_relaxed));
}

uptr GetPageSize() { return getpagesize(); }

uptr GetMmapGranularity() { return GetPageSize(); }

uptr GetMaxVirtualAddress() {
  return (1ULL << 32) - 1;  // 0xffffffff
}

void *MmapOrDie(uptr size, const char *mem_type, bool raw_report) {
  void* ptr = 0;
  int res = __mmap_alloc_aligned(&ptr, GetPageSize(), size);
  if (UNLIKELY(res))
    ReportMmapFailureAndDie(size, mem_type, "allocate", res, raw_report);
  __mmap_memset(ptr, 0, size);
  IncreaseTotalMmap(size);
  return ptr;
}

void *MmapOrDieOnFatalError(uptr size, const char *mem_type) {
  void* ptr = 0;
  int res = __mmap_alloc_aligned(&ptr, GetPageSize(), size);
  if (UNLIKELY(res)) {
    if (res == ENOMEM)
      return nullptr;
    ReportMmapFailureAndDie(size, mem_type, "allocate", false);
  }
  __mmap_memset(ptr, 0, size);
  IncreaseTotalMmap(size);
  return ptr;
}

void *MmapAlignedOrDieOnFatalError(uptr size, uptr alignment,
                                   const char *mem_type) {
  CHECK(IsPowerOfTwo(size));
  CHECK(IsPowerOfTwo(alignment));
  void* ptr = 0;
  int res = __mmap_alloc_aligned(&ptr, alignment, size);
  if (res)
    ReportMmapFailureAndDie(size, mem_type, "align allocate", res, false);
  __mmap_memset(ptr, 0, size);
  IncreaseTotalMmap(size);
  return ptr;
}

void *MmapNoReserveOrDie(uptr size, const char *mem_type) {
  return MmapOrDie(size, mem_type, false);
}

void UnmapOrDie(void *addr, uptr size) {
  if (!addr || !size) return;
  __mmap_free(addr);
  DecreaseTotalMmap(size);
}

fd_t OpenFile(const char *filename, FileAccessMode mode, error_t *errno_p) {
  int flags;
  switch (mode) {
    case RdOnly: flags = O_RDONLY; break;
    case WrOnly: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
    case RdWr: flags = O_RDWR | O_CREAT; break;
  }
  fd_t res = open(filename, flags, 0660);
  if (internal_iserror(res, errno_p))
    return kInvalidFd;
  return res;
}

void CloseFile(fd_t fd) {
  close(fd);
}

bool ReadFromFile(fd_t fd, void *buff, uptr buff_size, uptr *bytes_read,
                  error_t *error_p) {
  uptr res = read(fd, buff, buff_size);
  if (internal_iserror(res, error_p))
    return false;
  if (bytes_read)
    *bytes_read = res;
  return true;
}

bool WriteToFile(fd_t fd, const void *buff, uptr buff_size, uptr *bytes_written,
                 error_t *error_p) {
  uptr res = write(fd, buff, buff_size);
  if (internal_iserror(res, error_p))
    return false;
  if (bytes_written)
    *bytes_written = res;
  return true;
}

void ReleaseMemoryPagesToOS(uptr beg, uptr end) {}
void DumpProcessMap() {}

// There is no page protection so everything is "accessible."
bool IsAccessibleMemoryRange(uptr beg, uptr size) {
  return true;
}

char **GetArgv() { return nullptr; }
char **GetEnviron() { return nullptr; }

const char *GetEnv(const char *name) {
  return getenv(name);
}

uptr ReadBinaryName(/*out*/char *buf, uptr buf_len) {
  internal_strncpy(buf, "StubBinaryName", buf_len);
  return internal_strlen(buf);
}

uptr ReadLongProcessName(/*out*/ char *buf, uptr buf_len) {
  internal_strncpy(buf, "StubProcessName", buf_len);
  return internal_strlen(buf);
}

bool IsPathSeparator(const char c) {
  return c == '/';
}

bool IsAbsolutePath(const char *path) {
  return path != nullptr && IsPathSeparator(path[0]);
}

void ReportFile::Write(const char *buffer, uptr length) {
  SpinMutexLock l(mu);
  static const char *kWriteError =
      "ReportFile::Write() can't output requested buffer!\n";
  ReopenIfNecessary();
  if (length != write(fd, buffer, length)) {
    write(fd, kWriteError, internal_strlen(kWriteError));
    Die();
  }
}

uptr MainThreadStackBase, MainThreadStackSize;
uptr MainThreadTlsBase, MainThreadTlsSize;

} // namespace __sanitizer

#endif  // SANITIZER_RTEMS
