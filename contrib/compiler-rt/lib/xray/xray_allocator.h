//===-- xray_allocator.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// Defines the allocator interface for an arena allocator, used primarily for
// the profiling runtime.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_ALLOCATOR_H
#define XRAY_ALLOCATOR_H

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_mutex.h"
#if SANITIZER_FUCHSIA
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#else
#include "sanitizer_common/sanitizer_posix.h"
#endif
#include "xray_defs.h"
#include "xray_utils.h"
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>

namespace __xray {

// We implement our own memory allocation routine which will bypass the
// internal allocator. This allows us to manage the memory directly, using
// mmap'ed memory to back the allocators.
template <class T> T *allocate() XRAY_NEVER_INSTRUMENT {
  uptr RoundedSize = RoundUpTo(sizeof(T), GetPageSizeCached());
#if SANITIZER_FUCHSIA
  zx_handle_t Vmo;
  zx_status_t Status = _zx_vmo_create(RoundedSize, 0, &Vmo);
  if (Status != ZX_OK) {
    if (Verbosity())
      Report("XRay Profiling: Failed to create VMO of size %zu: %s\n",
             sizeof(T), _zx_status_get_string(Status));
    return nullptr;
  }
  uintptr_t B;
  Status =
      _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                   Vmo, 0, sizeof(T), &B);
  _zx_handle_close(Vmo);
  if (Status != ZX_OK) {
    if (Verbosity())
      Report("XRay Profiling: Failed to map VMAR of size %zu: %s\n", sizeof(T),
             _zx_status_get_string(Status));
    return nullptr;
  }
  return reinterpret_cast<T *>(B);
#else
  uptr B = internal_mmap(NULL, RoundedSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  int ErrNo = 0;
  if (UNLIKELY(internal_iserror(B, &ErrNo))) {
    if (Verbosity())
      Report(
          "XRay Profiling: Failed to allocate memory of size %d; Error = %d.\n",
          RoundedSize, B);
    return nullptr;
  }
#endif
  return reinterpret_cast<T *>(B);
}

template <class T> void deallocate(T *B) XRAY_NEVER_INSTRUMENT {
  if (B == nullptr)
    return;
  uptr RoundedSize = RoundUpTo(sizeof(T), GetPageSizeCached());
#if SANITIZER_FUCHSIA
  _zx_vmar_unmap(_zx_vmar_root_self(), reinterpret_cast<uintptr_t>(B),
                 RoundedSize);
#else
  internal_munmap(B, RoundedSize);
#endif
}

template <class T = unsigned char>
T *allocateBuffer(size_t S) XRAY_NEVER_INSTRUMENT {
  uptr RoundedSize = RoundUpTo(S * sizeof(T), GetPageSizeCached());
#if SANITIZER_FUCHSIA
  zx_handle_t Vmo;
  zx_status_t Status = _zx_vmo_create(RoundedSize, 0, &Vmo);
  if (Status != ZX_OK) {
    if (Verbosity())
      Report("XRay Profiling: Failed to create VMO of size %zu: %s\n", S,
             _zx_status_get_string(Status));
    return nullptr;
  }
  uintptr_t B;
  Status = _zx_vmar_map(_zx_vmar_root_self(),
                        ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, Vmo, 0, S, &B);
  _zx_handle_close(Vmo);
  if (Status != ZX_OK) {
    if (Verbosity())
      Report("XRay Profiling: Failed to map VMAR of size %zu: %s\n", S,
             _zx_status_get_string(Status));
    return nullptr;
  }
#else
  uptr B = internal_mmap(NULL, RoundedSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  int ErrNo = 0;
  if (UNLIKELY(internal_iserror(B, &ErrNo))) {
    if (Verbosity())
      Report(
          "XRay Profiling: Failed to allocate memory of size %d; Error = %d.\n",
          RoundedSize, B);
    return nullptr;
  }
#endif
  return reinterpret_cast<T *>(B);
}

template <class T> void deallocateBuffer(T *B, size_t S) XRAY_NEVER_INSTRUMENT {
  if (B == nullptr)
    return;
  uptr RoundedSize = RoundUpTo(S * sizeof(T), GetPageSizeCached());
#if SANITIZER_FUCHSIA
  _zx_vmar_unmap(_zx_vmar_root_self(), reinterpret_cast<uintptr_t>(B),
                 RoundedSize);
#else
  internal_munmap(B, RoundedSize);
#endif
}

template <class T, class... U>
T *initArray(size_t N, U &&... Us) XRAY_NEVER_INSTRUMENT {
  auto A = allocateBuffer<T>(N);
  if (A != nullptr)
    while (N > 0)
      new (A + (--N)) T(std::forward<U>(Us)...);
  return A;
}

/// The Allocator type hands out fixed-sized chunks of memory that are
/// cache-line aligned and sized. This is useful for placement of
/// performance-sensitive data in memory that's frequently accessed. The
/// allocator also self-limits the peak memory usage to a dynamically defined
/// maximum.
///
/// N is the lower-bound size of the block of memory to return from the
/// allocation function. N is used to compute the size of a block, which is
/// cache-line-size multiples worth of memory. We compute the size of a block by
/// determining how many cache lines worth of memory is required to subsume N.
///
/// The Allocator instance will manage its own memory acquired through mmap.
/// This severely constrains the platforms on which this can be used to POSIX
/// systems where mmap semantics are well-defined.
///
/// FIXME: Isolate the lower-level memory management to a different abstraction
/// that can be platform-specific.
template <size_t N> struct Allocator {
  // The Allocator returns memory as Block instances.
  struct Block {
    /// Compute the minimum cache-line size multiple that is >= N.
    static constexpr auto Size = nearest_boundary(N, kCacheLineSize);
    void *Data;
  };

private:
  size_t MaxMemory{0};
  unsigned char *BackingStore = nullptr;
  unsigned char *AlignedNextBlock = nullptr;
  size_t AllocatedBlocks = 0;
  bool Owned;
  SpinMutex Mutex{};

  void *Alloc() XRAY_NEVER_INSTRUMENT {
    SpinMutexLock Lock(&Mutex);
    if (UNLIKELY(BackingStore == nullptr)) {
      BackingStore = allocateBuffer(MaxMemory);
      if (BackingStore == nullptr) {
        if (Verbosity())
          Report("XRay Profiling: Failed to allocate memory for allocator.\n");
        return nullptr;
      }

      AlignedNextBlock = BackingStore;

      // Ensure that NextBlock is aligned appropriately.
      auto BackingStoreNum = reinterpret_cast<uintptr_t>(BackingStore);
      auto AlignedNextBlockNum = nearest_boundary(
          reinterpret_cast<uintptr_t>(AlignedNextBlock), kCacheLineSize);
      if (diff(AlignedNextBlockNum, BackingStoreNum) > ptrdiff_t(MaxMemory)) {
        deallocateBuffer(BackingStore, MaxMemory);
        AlignedNextBlock = BackingStore = nullptr;
        if (Verbosity())
          Report("XRay Profiling: Cannot obtain enough memory from "
                 "preallocated region.\n");
        return nullptr;
      }

      AlignedNextBlock = reinterpret_cast<unsigned char *>(AlignedNextBlockNum);

      // Assert that AlignedNextBlock is cache-line aligned.
      DCHECK_EQ(reinterpret_cast<uintptr_t>(AlignedNextBlock) % kCacheLineSize,
                0);
    }

    if (((AllocatedBlocks + 1) * Block::Size) > MaxMemory)
      return nullptr;

    // Align the pointer we'd like to return to an appropriate alignment, then
    // advance the pointer from where to start allocations.
    void *Result = AlignedNextBlock;
    AlignedNextBlock =
        reinterpret_cast<unsigned char *>(AlignedNextBlock) + Block::Size;
    ++AllocatedBlocks;
    return Result;
  }

public:
  explicit Allocator(size_t M) XRAY_NEVER_INSTRUMENT
      : MaxMemory(RoundUpTo(M, kCacheLineSize)),
        BackingStore(nullptr),
        AlignedNextBlock(nullptr),
        AllocatedBlocks(0),
        Owned(true),
        Mutex() {}

  explicit Allocator(void *P, size_t M) XRAY_NEVER_INSTRUMENT
      : MaxMemory(M),
        BackingStore(reinterpret_cast<unsigned char *>(P)),
        AlignedNextBlock(reinterpret_cast<unsigned char *>(P)),
        AllocatedBlocks(0),
        Owned(false),
        Mutex() {}

  Allocator(const Allocator &) = delete;
  Allocator &operator=(const Allocator &) = delete;

  Allocator(Allocator &&O) XRAY_NEVER_INSTRUMENT {
    SpinMutexLock L0(&Mutex);
    SpinMutexLock L1(&O.Mutex);
    MaxMemory = O.MaxMemory;
    O.MaxMemory = 0;
    BackingStore = O.BackingStore;
    O.BackingStore = nullptr;
    AlignedNextBlock = O.AlignedNextBlock;
    O.AlignedNextBlock = nullptr;
    AllocatedBlocks = O.AllocatedBlocks;
    O.AllocatedBlocks = 0;
    Owned = O.Owned;
    O.Owned = false;
  }

  Allocator &operator=(Allocator &&O) XRAY_NEVER_INSTRUMENT {
    SpinMutexLock L0(&Mutex);
    SpinMutexLock L1(&O.Mutex);
    MaxMemory = O.MaxMemory;
    O.MaxMemory = 0;
    if (BackingStore != nullptr)
      deallocateBuffer(BackingStore, MaxMemory);
    BackingStore = O.BackingStore;
    O.BackingStore = nullptr;
    AlignedNextBlock = O.AlignedNextBlock;
    O.AlignedNextBlock = nullptr;
    AllocatedBlocks = O.AllocatedBlocks;
    O.AllocatedBlocks = 0;
    Owned = O.Owned;
    O.Owned = false;
    return *this;
  }

  Block Allocate() XRAY_NEVER_INSTRUMENT { return {Alloc()}; }

  ~Allocator() NOEXCEPT XRAY_NEVER_INSTRUMENT {
    if (Owned && BackingStore != nullptr) {
      deallocateBuffer(BackingStore, MaxMemory);
    }
  }
};

} // namespace __xray

#endif // XRAY_ALLOCATOR_H
