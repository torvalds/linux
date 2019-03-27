//===-- scudo_flags.inc -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Hardened Allocator runtime flags.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_FLAG
# error "Define SCUDO_FLAG prior to including this file!"
#endif

SCUDO_FLAG(int, QuarantineSizeMb, -1,
           "Deprecated. Please use QuarantineSizeKb.")

// Default value is set in scudo_flags.cpp based on architecture.
SCUDO_FLAG(int, QuarantineSizeKb, -1,
           "Size in KB of quarantine used to delay the actual deallocation of "
           "chunks. Lower value may reduce memory usage but decrease the "
           "effectiveness of the mitigation. Defaults to 64KB (32-bit) or "
           "256KB (64-bit)")

// Default value is set in scudo_flags.cpp based on architecture.
SCUDO_FLAG(int, ThreadLocalQuarantineSizeKb, -1,
          "Size in KB of per-thread cache used to offload the global "
          "quarantine. Lower value may reduce memory usage but might increase "
          "the contention on the global quarantine. Defaults to 16KB (32-bit) "
          "or 64KB (64-bit)")

// Default value is set in scudo_flags.cpp based on architecture.
SCUDO_FLAG(int, QuarantineChunksUpToSize, -1,
          "Size in bytes up to which chunks will be quarantined (if lower than"
          "or equal to). Defaults to 256 (32-bit) or 2048 (64-bit)")

SCUDO_FLAG(bool, DeallocationTypeMismatch, true,
          "Report errors on malloc/delete, new/free, new/delete[], etc.")

SCUDO_FLAG(bool, DeleteSizeMismatch, true,
           "Report errors on mismatch between size of new and delete.")

SCUDO_FLAG(bool, ZeroContents, false,
          "Zero chunk contents on allocation and deallocation.")
