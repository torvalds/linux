//===- Unix/Memory.cpp - Generic UNIX System Configuration ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some functions for various memory management utilities.
//
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/Config/config.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Process.h"

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

#if defined(__mips__)
#  if defined(__OpenBSD__)
#    include <mips64/sysarch.h>
#  elif !defined(__FreeBSD__)
#    include <sys/cachectl.h>
#  endif
#endif

#if defined(__APPLE__)
extern "C" void sys_icache_invalidate(const void *Addr, size_t len);
#else
extern "C" void __clear_cache(void *, void*);
#endif

namespace {

int getPosixProtectionFlags(unsigned Flags) {
  switch (Flags) {
  case llvm::sys::Memory::MF_READ:
    return PROT_READ;
  case llvm::sys::Memory::MF_WRITE:
    return PROT_WRITE;
  case llvm::sys::Memory::MF_READ|llvm::sys::Memory::MF_WRITE:
    return PROT_READ | PROT_WRITE;
  case llvm::sys::Memory::MF_READ|llvm::sys::Memory::MF_EXEC:
    return PROT_READ | PROT_EXEC;
  case llvm::sys::Memory::MF_READ | llvm::sys::Memory::MF_WRITE |
      llvm::sys::Memory::MF_EXEC:
    return PROT_READ | PROT_WRITE | PROT_EXEC;
  case llvm::sys::Memory::MF_EXEC:
#if defined(__FreeBSD__)
    // On PowerPC, having an executable page that has no read permission
    // can have unintended consequences.  The function InvalidateInstruction-
    // Cache uses instructions dcbf and icbi, both of which are treated by
    // the processor as loads.  If the page has no read permissions,
    // executing these instructions will result in a segmentation fault.
    // Somehow, this problem is not present on Linux, but it does happen
    // on FreeBSD.
    return PROT_READ | PROT_EXEC;
#else
    return PROT_EXEC;
#endif
  default:
    llvm_unreachable("Illegal memory protection flag specified!");
  }
  // Provide a default return value as required by some compilers.
  return PROT_NONE;
}

} // anonymous namespace

namespace llvm {
namespace sys {

MemoryBlock
Memory::allocateMappedMemory(size_t NumBytes,
                             const MemoryBlock *const NearBlock,
                             unsigned PFlags,
                             std::error_code &EC) {
  EC = std::error_code();
  if (NumBytes == 0)
    return MemoryBlock();

  static const size_t PageSize = Process::getPageSize();
  const size_t NumPages = (NumBytes+PageSize-1)/PageSize;

  int fd = -1;

  int MMFlags = MAP_PRIVATE |
#ifdef MAP_ANONYMOUS
  MAP_ANONYMOUS
#else
  MAP_ANON
#endif
  ; // Ends statement above

  int Protect = getPosixProtectionFlags(PFlags);

#if defined(__NetBSD__) && defined(PROT_MPROTECT)
  Protect |= PROT_MPROTECT(PROT_READ | PROT_WRITE | PROT_EXEC);
#endif

  // Use any near hint and the page size to set a page-aligned starting address
  uintptr_t Start = NearBlock ? reinterpret_cast<uintptr_t>(NearBlock->base()) +
                                      NearBlock->size() : 0;
  if (Start && Start % PageSize)
    Start += PageSize - Start % PageSize;

  void *Addr = ::mmap(reinterpret_cast<void*>(Start), PageSize*NumPages,
                      Protect, MMFlags, fd, 0);
  if (Addr == MAP_FAILED) {
    if (NearBlock) //Try again without a near hint
      return allocateMappedMemory(NumBytes, nullptr, PFlags, EC);

    EC = std::error_code(errno, std::generic_category());
    return MemoryBlock();
  }

  MemoryBlock Result;
  Result.Address = Addr;
  Result.Size = NumPages*PageSize;

  // Rely on protectMappedMemory to invalidate instruction cache.
  if (PFlags & MF_EXEC) {
    EC = Memory::protectMappedMemory (Result, PFlags);
    if (EC != std::error_code())
      return MemoryBlock();
  }

  return Result;
}

std::error_code
Memory::releaseMappedMemory(MemoryBlock &M) {
  if (M.Address == nullptr || M.Size == 0)
    return std::error_code();

  if (0 != ::munmap(M.Address, M.Size))
    return std::error_code(errno, std::generic_category());

  M.Address = nullptr;
  M.Size = 0;

  return std::error_code();
}

std::error_code
Memory::protectMappedMemory(const MemoryBlock &M, unsigned Flags) {
  static const size_t PageSize = Process::getPageSize();
  if (M.Address == nullptr || M.Size == 0)
    return std::error_code();

  if (!Flags)
    return std::error_code(EINVAL, std::generic_category());

  int Protect = getPosixProtectionFlags(Flags);
  uintptr_t Start = alignAddr((uint8_t *)M.Address - PageSize + 1, PageSize);
  uintptr_t End = alignAddr((uint8_t *)M.Address + M.Size, PageSize);

  bool InvalidateCache = (Flags & MF_EXEC);

#if defined(__arm__) || defined(__aarch64__)
  // Certain ARM implementations treat icache clear instruction as a memory read,
  // and CPU segfaults on trying to clear cache on !PROT_READ page.  Therefore we need
  // to temporarily add PROT_READ for the sake of flushing the instruction caches.
  if (InvalidateCache && !(Protect & PROT_READ)) {
    int Result = ::mprotect((void *)Start, End - Start, Protect | PROT_READ);
    if (Result != 0)
      return std::error_code(errno, std::generic_category());

    Memory::InvalidateInstructionCache(M.Address, M.Size);
    InvalidateCache = false;
  }
#endif

  int Result = ::mprotect((void *)Start, End - Start, Protect);

  if (Result != 0)
    return std::error_code(errno, std::generic_category());

  if (InvalidateCache)
    Memory::InvalidateInstructionCache(M.Address, M.Size);

  return std::error_code();
}

/// InvalidateInstructionCache - Before the JIT can run a block of code
/// that has been emitted it must invalidate the instruction cache on some
/// platforms.
void Memory::InvalidateInstructionCache(const void *Addr,
                                        size_t Len) {

// icache invalidation for PPC and ARM.
#if defined(__APPLE__)

#  if (defined(__POWERPC__) || defined (__ppc__) || \
       defined(_POWER) || defined(_ARCH_PPC) || defined(__arm__) || \
       defined(__arm64__))
  sys_icache_invalidate(const_cast<void *>(Addr), Len);
#  endif

#elif defined(__Fuchsia__)

  zx_status_t Status = zx_cache_flush(Addr, Len, ZX_CACHE_FLUSH_INSN);
  assert(Status == ZX_OK && "cannot invalidate instruction cache");

#else

#  if (defined(__POWERPC__) || defined (__ppc__) || \
       defined(_POWER) || defined(_ARCH_PPC)) && defined(__GNUC__)
  const size_t LineSize = 32;

  const intptr_t Mask = ~(LineSize - 1);
  const intptr_t StartLine = ((intptr_t) Addr) & Mask;
  const intptr_t EndLine = ((intptr_t) Addr + Len + LineSize - 1) & Mask;

  for (intptr_t Line = StartLine; Line < EndLine; Line += LineSize)
    asm volatile("dcbf 0, %0" : : "r"(Line));
  asm volatile("sync");

  for (intptr_t Line = StartLine; Line < EndLine; Line += LineSize)
    asm volatile("icbi 0, %0" : : "r"(Line));
  asm volatile("isync");
#  elif (defined(__arm__) || defined(__aarch64__) || defined(__mips__)) && \
        defined(__GNUC__)
  // FIXME: Can we safely always call this for __GNUC__ everywhere?
  const char *Start = static_cast<const char *>(Addr);
  const char *End = Start + Len;
  __clear_cache(const_cast<char *>(Start), const_cast<char *>(End));
#  endif

#endif  // end apple

  ValgrindDiscardTranslations(Addr, Len);
}

} // namespace sys
} // namespace llvm
