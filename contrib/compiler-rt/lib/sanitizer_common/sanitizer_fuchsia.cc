//===-- sanitizer_fuchsia.cc ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and other sanitizer
// run-time libraries and implements Fuchsia-specific functions from
// sanitizer_common.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_fuchsia.h"
#if SANITIZER_FUCHSIA

#include "sanitizer_common.h"
#include "sanitizer_libc.h"
#include "sanitizer_mutex.h"

#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

namespace __sanitizer {

void NORETURN internal__exit(int exitcode) { _zx_process_exit(exitcode); }

uptr internal_sched_yield() {
  zx_status_t status = _zx_nanosleep(0);
  CHECK_EQ(status, ZX_OK);
  return 0;  // Why doesn't this return void?
}

static void internal_nanosleep(zx_time_t ns) {
  zx_status_t status = _zx_nanosleep(_zx_deadline_after(ns));
  CHECK_EQ(status, ZX_OK);
}

unsigned int internal_sleep(unsigned int seconds) {
  internal_nanosleep(ZX_SEC(seconds));
  return 0;
}

u64 NanoTime() { return _zx_clock_get(ZX_CLOCK_UTC); }

u64 MonotonicNanoTime() { return _zx_clock_get(ZX_CLOCK_MONOTONIC); }

uptr internal_getpid() {
  zx_info_handle_basic_t info;
  zx_status_t status =
      _zx_object_get_info(_zx_process_self(), ZX_INFO_HANDLE_BASIC, &info,
                          sizeof(info), NULL, NULL);
  CHECK_EQ(status, ZX_OK);
  uptr pid = static_cast<uptr>(info.koid);
  CHECK_EQ(pid, info.koid);
  return pid;
}

uptr GetThreadSelf() { return reinterpret_cast<uptr>(thrd_current()); }

tid_t GetTid() { return GetThreadSelf(); }

void Abort() { abort(); }

int Atexit(void (*function)(void)) { return atexit(function); }

void SleepForSeconds(int seconds) { internal_sleep(seconds); }

void SleepForMillis(int millis) { internal_nanosleep(ZX_MSEC(millis)); }

void GetThreadStackTopAndBottom(bool, uptr *stack_top, uptr *stack_bottom) {
  pthread_attr_t attr;
  CHECK_EQ(pthread_getattr_np(pthread_self(), &attr), 0);
  void *base;
  size_t size;
  CHECK_EQ(pthread_attr_getstack(&attr, &base, &size), 0);
  CHECK_EQ(pthread_attr_destroy(&attr), 0);

  *stack_bottom = reinterpret_cast<uptr>(base);
  *stack_top = *stack_bottom + size;
}

void InitializePlatformEarly() {}
void MaybeReexec() {}
void CheckASLR() {}
void CheckMPROTECT() {}
void PlatformPrepareForSandboxing(__sanitizer_sandbox_arguments *args) {}
void DisableCoreDumperIfNecessary() {}
void InstallDeadlySignalHandlers(SignalHandlerType handler) {}
void SetAlternateSignalStack() {}
void UnsetAlternateSignalStack() {}
void InitTlsSize() {}

void PrintModuleMap() {}

bool SignalContext::IsStackOverflow() const { return false; }
void SignalContext::DumpAllRegisters(void *context) { UNIMPLEMENTED(); }
const char *SignalContext::Describe() const { UNIMPLEMENTED(); }

enum MutexState : int { MtxUnlocked = 0, MtxLocked = 1, MtxSleeping = 2 };

BlockingMutex::BlockingMutex() {
  // NOTE!  It's important that this use internal_memset, because plain
  // memset might be intercepted (e.g., actually be __asan_memset).
  // Defining this so the compiler initializes each field, e.g.:
  //   BlockingMutex::BlockingMutex() : BlockingMutex(LINKER_INITIALIZED) {}
  // might result in the compiler generating a call to memset, which would
  // have the same problem.
  internal_memset(this, 0, sizeof(*this));
}

void BlockingMutex::Lock() {
  CHECK_EQ(owner_, 0);
  atomic_uint32_t *m = reinterpret_cast<atomic_uint32_t *>(&opaque_storage_);
  if (atomic_exchange(m, MtxLocked, memory_order_acquire) == MtxUnlocked)
    return;
  while (atomic_exchange(m, MtxSleeping, memory_order_acquire) != MtxUnlocked) {
    zx_status_t status =
        _zx_futex_wait(reinterpret_cast<zx_futex_t *>(m), MtxSleeping,
                       ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    if (status != ZX_ERR_BAD_STATE)  // Normal race.
      CHECK_EQ(status, ZX_OK);
  }
}

void BlockingMutex::Unlock() {
  atomic_uint32_t *m = reinterpret_cast<atomic_uint32_t *>(&opaque_storage_);
  u32 v = atomic_exchange(m, MtxUnlocked, memory_order_release);
  CHECK_NE(v, MtxUnlocked);
  if (v == MtxSleeping) {
    zx_status_t status = _zx_futex_wake(reinterpret_cast<zx_futex_t *>(m), 1);
    CHECK_EQ(status, ZX_OK);
  }
}

void BlockingMutex::CheckLocked() {
  atomic_uint32_t *m = reinterpret_cast<atomic_uint32_t *>(&opaque_storage_);
  CHECK_NE(MtxUnlocked, atomic_load(m, memory_order_relaxed));
}

uptr GetPageSize() { return PAGE_SIZE; }

uptr GetMmapGranularity() { return PAGE_SIZE; }

sanitizer_shadow_bounds_t ShadowBounds;

uptr GetMaxUserVirtualAddress() {
  ShadowBounds = __sanitizer_shadow_bounds();
  return ShadowBounds.memory_limit - 1;
}

uptr GetMaxVirtualAddress() { return GetMaxUserVirtualAddress(); }

static void *DoAnonymousMmapOrDie(uptr size, const char *mem_type,
                                  bool raw_report, bool die_for_nomem) {
  size = RoundUpTo(size, PAGE_SIZE);

  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(size, 0, &vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmo_create", status,
                              raw_report);
    return nullptr;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, mem_type,
                          internal_strlen(mem_type));

  // TODO(mcgrathr): Maybe allocate a VMAR for all sanitizer heap and use that?
  uintptr_t addr;
  status =
      _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                   vmo, 0, size, &addr);
  _zx_handle_close(vmo);

  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmar_map", status,
                              raw_report);
    return nullptr;
  }

  IncreaseTotalMmap(size);

  return reinterpret_cast<void *>(addr);
}

void *MmapOrDie(uptr size, const char *mem_type, bool raw_report) {
  return DoAnonymousMmapOrDie(size, mem_type, raw_report, true);
}

void *MmapNoReserveOrDie(uptr size, const char *mem_type) {
  return MmapOrDie(size, mem_type);
}

void *MmapOrDieOnFatalError(uptr size, const char *mem_type) {
  return DoAnonymousMmapOrDie(size, mem_type, false, false);
}

uptr ReservedAddressRange::Init(uptr init_size, const char *name,
                                uptr fixed_addr) {
  init_size = RoundUpTo(init_size, PAGE_SIZE);
  DCHECK_EQ(os_handle_, ZX_HANDLE_INVALID);
  uintptr_t base;
  zx_handle_t vmar;
  zx_status_t status =
      _zx_vmar_allocate(
          _zx_vmar_root_self(),
          ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC,
          0, init_size, &vmar, &base);
  if (status != ZX_OK)
    ReportMmapFailureAndDie(init_size, name, "zx_vmar_allocate", status);
  base_ = reinterpret_cast<void *>(base);
  size_ = init_size;
  name_ = name;
  os_handle_ = vmar;

  return reinterpret_cast<uptr>(base_);
}

static uptr DoMmapFixedOrDie(zx_handle_t vmar, uptr fixed_addr, uptr map_size,
                             void *base, const char *name, bool die_for_nomem) {
  uptr offset = fixed_addr - reinterpret_cast<uptr>(base);
  map_size = RoundUpTo(map_size, PAGE_SIZE);
  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(map_size, 0, &vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem)
      ReportMmapFailureAndDie(map_size, name, "zx_vmo_create", status);
    return 0;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, name, internal_strlen(name));
  DCHECK_GE(base + size_, map_size + offset);
  uintptr_t addr;

  status =
      _zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                   offset, vmo, 0, map_size, &addr);
  _zx_handle_close(vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem) {
      ReportMmapFailureAndDie(map_size, name, "zx_vmar_map", status);
    }
    return 0;
  }
  IncreaseTotalMmap(map_size);
  return addr;
}

uptr ReservedAddressRange::Map(uptr fixed_addr, uptr map_size) {
  return DoMmapFixedOrDie(os_handle_, fixed_addr, map_size, base_,
                          name_, false);
}

uptr ReservedAddressRange::MapOrDie(uptr fixed_addr, uptr map_size) {
  return DoMmapFixedOrDie(os_handle_, fixed_addr, map_size, base_,
                          name_, true);
}

void UnmapOrDieVmar(void *addr, uptr size, zx_handle_t target_vmar) {
  if (!addr || !size) return;
  size = RoundUpTo(size, PAGE_SIZE);

  zx_status_t status =
      _zx_vmar_unmap(target_vmar, reinterpret_cast<uintptr_t>(addr), size);
  if (status != ZX_OK) {
    Report("ERROR: %s failed to deallocate 0x%zx (%zd) bytes at address %p\n",
           SanitizerToolName, size, size, addr);
    CHECK("unable to unmap" && 0);
  }

  DecreaseTotalMmap(size);
}

void ReservedAddressRange::Unmap(uptr addr, uptr size) {
  CHECK_LE(size, size_);
  const zx_handle_t vmar = static_cast<zx_handle_t>(os_handle_);
  if (addr == reinterpret_cast<uptr>(base_)) {
    if (size == size_) {
      // Destroying the vmar effectively unmaps the whole mapping.
      _zx_vmar_destroy(vmar);
      _zx_handle_close(vmar);
      os_handle_ = static_cast<uptr>(ZX_HANDLE_INVALID);
      DecreaseTotalMmap(size);
      return;
    }
  } else {
    CHECK_EQ(addr + size, reinterpret_cast<uptr>(base_) + size_);
  }
  // Partial unmapping does not affect the fact that the initial range is still
  // reserved, and the resulting unmapped memory can't be reused.
  UnmapOrDieVmar(reinterpret_cast<void *>(addr), size, vmar);
}

// This should never be called.
void *MmapFixedNoAccess(uptr fixed_addr, uptr size, const char *name) {
  UNIMPLEMENTED();
}

void *MmapAlignedOrDieOnFatalError(uptr size, uptr alignment,
                                   const char *mem_type) {
  CHECK_GE(size, PAGE_SIZE);
  CHECK(IsPowerOfTwo(size));
  CHECK(IsPowerOfTwo(alignment));

  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(size, 0, &vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmo_create", status, false);
    return nullptr;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, mem_type,
                          internal_strlen(mem_type));

  // TODO(mcgrathr): Maybe allocate a VMAR for all sanitizer heap and use that?

  // Map a larger size to get a chunk of address space big enough that
  // it surely contains an aligned region of the requested size.  Then
  // overwrite the aligned middle portion with a mapping from the
  // beginning of the VMO, and unmap the excess before and after.
  size_t map_size = size + alignment;
  uintptr_t addr;
  status =
      _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                   vmo, 0, map_size, &addr);
  if (status == ZX_OK) {
    uintptr_t map_addr = addr;
    uintptr_t map_end = map_addr + map_size;
    addr = RoundUpTo(map_addr, alignment);
    uintptr_t end = addr + size;
    if (addr != map_addr) {
      zx_info_vmar_t info;
      status = _zx_object_get_info(_zx_vmar_root_self(), ZX_INFO_VMAR, &info,
                                   sizeof(info), NULL, NULL);
      if (status == ZX_OK) {
        uintptr_t new_addr;
        status = _zx_vmar_map(
            _zx_vmar_root_self(),
            ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE,
            addr - info.base, vmo, 0, size, &new_addr);
        if (status == ZX_OK) CHECK_EQ(new_addr, addr);
      }
    }
    if (status == ZX_OK && addr != map_addr)
      status = _zx_vmar_unmap(_zx_vmar_root_self(), map_addr, addr - map_addr);
    if (status == ZX_OK && end != map_end)
      status = _zx_vmar_unmap(_zx_vmar_root_self(), end, map_end - end);
  }
  _zx_handle_close(vmo);

  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmar_map", status, false);
    return nullptr;
  }

  IncreaseTotalMmap(size);

  return reinterpret_cast<void *>(addr);
}

void UnmapOrDie(void *addr, uptr size) {
  UnmapOrDieVmar(addr, size, _zx_vmar_root_self());
}

// This is used on the shadow mapping, which cannot be changed.
// Zircon doesn't have anything like MADV_DONTNEED.
void ReleaseMemoryPagesToOS(uptr beg, uptr end) {}

void DumpProcessMap() {
  // TODO(mcgrathr): write it
  return;
}

bool IsAccessibleMemoryRange(uptr beg, uptr size) {
  // TODO(mcgrathr): Figure out a better way.
  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(size, 0, &vmo);
  if (status == ZX_OK) {
    status = _zx_vmo_write(vmo, reinterpret_cast<const void *>(beg), 0, size);
    _zx_handle_close(vmo);
  }
  return status == ZX_OK;
}

// FIXME implement on this platform.
void GetMemoryProfile(fill_profile_f cb, uptr *stats, uptr stats_size) {}

bool ReadFileToBuffer(const char *file_name, char **buff, uptr *buff_size,
                      uptr *read_len, uptr max_len, error_t *errno_p) {
  zx_handle_t vmo;
  zx_status_t status = __sanitizer_get_configuration(file_name, &vmo);
  if (status == ZX_OK) {
    uint64_t vmo_size;
    status = _zx_vmo_get_size(vmo, &vmo_size);
    if (status == ZX_OK) {
      if (vmo_size < max_len) max_len = vmo_size;
      size_t map_size = RoundUpTo(max_len, PAGE_SIZE);
      uintptr_t addr;
      status = _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0,
                            map_size, &addr);
      if (status == ZX_OK) {
        *buff = reinterpret_cast<char *>(addr);
        *buff_size = map_size;
        *read_len = max_len;
      }
    }
    _zx_handle_close(vmo);
  }
  if (status != ZX_OK && errno_p) *errno_p = status;
  return status == ZX_OK;
}

void RawWrite(const char *buffer) {
  constexpr size_t size = 128;
  static _Thread_local char line[size];
  static _Thread_local size_t lastLineEnd = 0;
  static _Thread_local size_t cur = 0;

  while (*buffer) {
    if (cur >= size) {
      if (lastLineEnd == 0)
        lastLineEnd = size;
      __sanitizer_log_write(line, lastLineEnd);
      internal_memmove(line, line + lastLineEnd, cur - lastLineEnd);
      cur = cur - lastLineEnd;
      lastLineEnd = 0;
    }
    if (*buffer == '\n')
      lastLineEnd = cur + 1;
    line[cur++] = *buffer++;
  }
  // Flush all complete lines before returning.
  if (lastLineEnd != 0) {
    __sanitizer_log_write(line, lastLineEnd);
    internal_memmove(line, line + lastLineEnd, cur - lastLineEnd);
    cur = cur - lastLineEnd;
    lastLineEnd = 0;
  }
}

void CatastrophicErrorWrite(const char *buffer, uptr length) {
  __sanitizer_log_write(buffer, length);
}

char **StoredArgv;
char **StoredEnviron;

char **GetArgv() { return StoredArgv; }
char **GetEnviron() { return StoredEnviron; }

const char *GetEnv(const char *name) {
  if (StoredEnviron) {
    uptr NameLen = internal_strlen(name);
    for (char **Env = StoredEnviron; *Env != 0; Env++) {
      if (internal_strncmp(*Env, name, NameLen) == 0 && (*Env)[NameLen] == '=')
        return (*Env) + NameLen + 1;
    }
  }
  return nullptr;
}

uptr ReadBinaryName(/*out*/ char *buf, uptr buf_len) {
  const char *argv0 = "<UNKNOWN>";
  if (StoredArgv && StoredArgv[0]) {
    argv0 = StoredArgv[0];
  }
  internal_strncpy(buf, argv0, buf_len);
  return internal_strlen(buf);
}

uptr ReadLongProcessName(/*out*/ char *buf, uptr buf_len) {
  return ReadBinaryName(buf, buf_len);
}

uptr MainThreadStackBase, MainThreadStackSize;

bool GetRandom(void *buffer, uptr length, bool blocking) {
  CHECK_LE(length, ZX_CPRNG_DRAW_MAX_LEN);
  _zx_cprng_draw(buffer, length);
  return true;
}

u32 GetNumberOfCPUs() {
  return zx_system_get_num_cpus();
}

uptr GetRSS() { UNIMPLEMENTED(); }

}  // namespace __sanitizer

using namespace __sanitizer;  // NOLINT

extern "C" {
void __sanitizer_startup_hook(int argc, char **argv, char **envp,
                              void *stack_base, size_t stack_size) {
  __sanitizer::StoredArgv = argv;
  __sanitizer::StoredEnviron = envp;
  __sanitizer::MainThreadStackBase = reinterpret_cast<uintptr_t>(stack_base);
  __sanitizer::MainThreadStackSize = stack_size;
}

void __sanitizer_set_report_path(const char *path) {
  // Handle the initialization code in each sanitizer, but no other calls.
  // This setting is never consulted on Fuchsia.
  DCHECK_EQ(path, common_flags()->log_path);
}

void __sanitizer_set_report_fd(void *fd) {
  UNREACHABLE("not available on Fuchsia");
}
}  // extern "C"

#endif  // SANITIZER_FUCHSIA
