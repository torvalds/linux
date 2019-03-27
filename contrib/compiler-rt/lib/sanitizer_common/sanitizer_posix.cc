//===-- sanitizer_posix.cc ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements POSIX-specific functions from
// sanitizer_posix.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_POSIX

#include "sanitizer_common.h"
#include "sanitizer_file.h"
#include "sanitizer_flags.h"
#include "sanitizer_libc.h"
#include "sanitizer_posix.h"
#include "sanitizer_procmaps.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>

#if SANITIZER_FREEBSD
// The MAP_NORESERVE define has been removed in FreeBSD 11.x, and even before
// that, it was never implemented.  So just define it to zero.
#undef  MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

namespace __sanitizer {

// ------------- sanitizer_common.h
uptr GetMmapGranularity() {
  return GetPageSize();
}

void *MmapOrDie(uptr size, const char *mem_type, bool raw_report) {
  size = RoundUpTo(size, GetPageSizeCached());
  uptr res = internal_mmap(nullptr, size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON, -1, 0);
  int reserrno;
  if (UNLIKELY(internal_iserror(res, &reserrno)))
    ReportMmapFailureAndDie(size, mem_type, "allocate", reserrno, raw_report);
  IncreaseTotalMmap(size);
  return (void *)res;
}

void UnmapOrDie(void *addr, uptr size) {
  if (!addr || !size) return;
  uptr res = internal_munmap(addr, size);
  if (UNLIKELY(internal_iserror(res))) {
    Report("ERROR: %s failed to deallocate 0x%zx (%zd) bytes at address %p\n",
           SanitizerToolName, size, size, addr);
    CHECK("unable to unmap" && 0);
  }
  DecreaseTotalMmap(size);
}

void *MmapOrDieOnFatalError(uptr size, const char *mem_type) {
  size = RoundUpTo(size, GetPageSizeCached());
  uptr res = internal_mmap(nullptr, size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON, -1, 0);
  int reserrno;
  if (UNLIKELY(internal_iserror(res, &reserrno))) {
    if (reserrno == ENOMEM)
      return nullptr;
    ReportMmapFailureAndDie(size, mem_type, "allocate", reserrno);
  }
  IncreaseTotalMmap(size);
  return (void *)res;
}

// We want to map a chunk of address space aligned to 'alignment'.
// We do it by mapping a bit more and then unmapping redundant pieces.
// We probably can do it with fewer syscalls in some OS-dependent way.
void *MmapAlignedOrDieOnFatalError(uptr size, uptr alignment,
                                   const char *mem_type) {
  CHECK(IsPowerOfTwo(size));
  CHECK(IsPowerOfTwo(alignment));
  uptr map_size = size + alignment;
  uptr map_res = (uptr)MmapOrDieOnFatalError(map_size, mem_type);
  if (UNLIKELY(!map_res))
    return nullptr;
  uptr map_end = map_res + map_size;
  uptr res = map_res;
  if (!IsAligned(res, alignment)) {
    res = (map_res + alignment - 1) & ~(alignment - 1);
    UnmapOrDie((void*)map_res, res - map_res);
  }
  uptr end = res + size;
  if (end != map_end)
    UnmapOrDie((void*)end, map_end - end);
  return (void*)res;
}

void *MmapNoReserveOrDie(uptr size, const char *mem_type) {
  uptr PageSize = GetPageSizeCached();
  uptr p = internal_mmap(nullptr,
                         RoundUpTo(size, PageSize),
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON | MAP_NORESERVE,
                         -1, 0);
  int reserrno;
  if (UNLIKELY(internal_iserror(p, &reserrno)))
    ReportMmapFailureAndDie(size, mem_type, "allocate noreserve", reserrno);
  IncreaseTotalMmap(size);
  return (void *)p;
}

void *MmapFixedImpl(uptr fixed_addr, uptr size, bool tolerate_enomem) {
  uptr PageSize = GetPageSizeCached();
  uptr p = internal_mmap((void*)(fixed_addr & ~(PageSize - 1)),
      RoundUpTo(size, PageSize),
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANON | MAP_FIXED,
      -1, 0);
  int reserrno;
  if (UNLIKELY(internal_iserror(p, &reserrno))) {
    if (tolerate_enomem && reserrno == ENOMEM)
      return nullptr;
    char mem_type[40];
    internal_snprintf(mem_type, sizeof(mem_type), "memory at address 0x%zx",
                      fixed_addr);
    ReportMmapFailureAndDie(size, mem_type, "allocate", reserrno);
  }
  IncreaseTotalMmap(size);
  return (void *)p;
}

void *MmapFixedOrDie(uptr fixed_addr, uptr size) {
  return MmapFixedImpl(fixed_addr, size, false /*tolerate_enomem*/);
}

void *MmapFixedOrDieOnFatalError(uptr fixed_addr, uptr size) {
  return MmapFixedImpl(fixed_addr, size, true /*tolerate_enomem*/);
}

bool MprotectNoAccess(uptr addr, uptr size) {
  return 0 == internal_mprotect((void*)addr, size, PROT_NONE);
}

bool MprotectReadOnly(uptr addr, uptr size) {
  return 0 == internal_mprotect((void *)addr, size, PROT_READ);
}

#if !SANITIZER_MAC
void MprotectMallocZones(void *addr, int prot) {}
#endif

fd_t OpenFile(const char *filename, FileAccessMode mode, error_t *errno_p) {
  if (ShouldMockFailureToOpen(filename))
    return kInvalidFd;
  int flags;
  switch (mode) {
    case RdOnly: flags = O_RDONLY; break;
    case WrOnly: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
    case RdWr: flags = O_RDWR | O_CREAT; break;
  }
  fd_t res = internal_open(filename, flags, 0660);
  if (internal_iserror(res, errno_p))
    return kInvalidFd;
  return ReserveStandardFds(res);
}

void CloseFile(fd_t fd) {
  internal_close(fd);
}

bool ReadFromFile(fd_t fd, void *buff, uptr buff_size, uptr *bytes_read,
                  error_t *error_p) {
  uptr res = internal_read(fd, buff, buff_size);
  if (internal_iserror(res, error_p))
    return false;
  if (bytes_read)
    *bytes_read = res;
  return true;
}

bool WriteToFile(fd_t fd, const void *buff, uptr buff_size, uptr *bytes_written,
                 error_t *error_p) {
  uptr res = internal_write(fd, buff, buff_size);
  if (internal_iserror(res, error_p))
    return false;
  if (bytes_written)
    *bytes_written = res;
  return true;
}

void *MapFileToMemory(const char *file_name, uptr *buff_size) {
  fd_t fd = OpenFile(file_name, RdOnly);
  CHECK(fd != kInvalidFd);
  uptr fsize = internal_filesize(fd);
  CHECK_NE(fsize, (uptr)-1);
  CHECK_GT(fsize, 0);
  *buff_size = RoundUpTo(fsize, GetPageSizeCached());
  uptr map = internal_mmap(nullptr, *buff_size, PROT_READ, MAP_PRIVATE, fd, 0);
  return internal_iserror(map) ? nullptr : (void *)map;
}

void *MapWritableFileToMemory(void *addr, uptr size, fd_t fd, OFF_T offset) {
  uptr flags = MAP_SHARED;
  if (addr) flags |= MAP_FIXED;
  uptr p = internal_mmap(addr, size, PROT_READ | PROT_WRITE, flags, fd, offset);
  int mmap_errno = 0;
  if (internal_iserror(p, &mmap_errno)) {
    Printf("could not map writable file (%d, %lld, %zu): %zd, errno: %d\n",
           fd, (long long)offset, size, p, mmap_errno);
    return nullptr;
  }
  return (void *)p;
}

static inline bool IntervalsAreSeparate(uptr start1, uptr end1,
                                        uptr start2, uptr end2) {
  CHECK(start1 <= end1);
  CHECK(start2 <= end2);
  return (end1 < start2) || (end2 < start1);
}

// FIXME: this is thread-unsafe, but should not cause problems most of the time.
// When the shadow is mapped only a single thread usually exists (plus maybe
// several worker threads on Mac, which aren't expected to map big chunks of
// memory).
bool MemoryRangeIsAvailable(uptr range_start, uptr range_end) {
  MemoryMappingLayout proc_maps(/*cache_enabled*/true);
  if (proc_maps.Error())
    return true; // and hope for the best
  MemoryMappedSegment segment;
  while (proc_maps.Next(&segment)) {
    if (segment.start == segment.end) continue;  // Empty range.
    CHECK_NE(0, segment.end);
    if (!IntervalsAreSeparate(segment.start, segment.end - 1, range_start,
                              range_end))
      return false;
  }
  return true;
}

void DumpProcessMap() {
  MemoryMappingLayout proc_maps(/*cache_enabled*/true);
  const sptr kBufSize = 4095;
  char *filename = (char*)MmapOrDie(kBufSize, __func__);
  MemoryMappedSegment segment(filename, kBufSize);
  Report("Process memory map follows:\n");
  while (proc_maps.Next(&segment)) {
    Printf("\t%p-%p\t%s\n", (void *)segment.start, (void *)segment.end,
           segment.filename);
  }
  Report("End of process memory map.\n");
  UnmapOrDie(filename, kBufSize);
}

const char *GetPwd() {
  return GetEnv("PWD");
}

bool IsPathSeparator(const char c) {
  return c == '/';
}

bool IsAbsolutePath(const char *path) {
  return path != nullptr && IsPathSeparator(path[0]);
}

void ReportFile::Write(const char *buffer, uptr length) {
  SpinMutexLock l(mu);
  ReopenIfNecessary();
  internal_write(fd, buffer, length);
}

bool GetCodeRangeForFile(const char *module, uptr *start, uptr *end) {
  MemoryMappingLayout proc_maps(/*cache_enabled*/false);
  InternalScopedString buff(kMaxPathLength);
  MemoryMappedSegment segment(buff.data(), kMaxPathLength);
  while (proc_maps.Next(&segment)) {
    if (segment.IsExecutable() &&
        internal_strcmp(module, segment.filename) == 0) {
      *start = segment.start;
      *end = segment.end;
      return true;
    }
  }
  return false;
}

uptr SignalContext::GetAddress() const {
  auto si = static_cast<const siginfo_t *>(siginfo);
  return (uptr)si->si_addr;
}

bool SignalContext::IsMemoryAccess() const {
  auto si = static_cast<const siginfo_t *>(siginfo);
  return si->si_signo == SIGSEGV;
}

int SignalContext::GetType() const {
  return static_cast<const siginfo_t *>(siginfo)->si_signo;
}

const char *SignalContext::Describe() const {
  switch (GetType()) {
    case SIGFPE:
      return "FPE";
    case SIGILL:
      return "ILL";
    case SIGABRT:
      return "ABRT";
    case SIGSEGV:
      return "SEGV";
    case SIGBUS:
      return "BUS";
  }
  return "UNKNOWN SIGNAL";
}

fd_t ReserveStandardFds(fd_t fd) {
  CHECK_GE(fd, 0);
  if (fd > 2)
    return fd;
  bool used[3];
  internal_memset(used, 0, sizeof(used));
  while (fd <= 2) {
    used[fd] = true;
    fd = internal_dup(fd);
  }
  for (int i = 0; i <= 2; ++i)
    if (used[i])
      internal_close(i);
  return fd;
}

bool ShouldMockFailureToOpen(const char *path) {
  return common_flags()->test_only_emulate_no_memorymap &&
         internal_strncmp(path, "/proc/", 6) == 0;
}

} // namespace __sanitizer

#endif // SANITIZER_POSIX
