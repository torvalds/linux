//===- SanitizerStats.h - Sanitizer statistics gathering  -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Declares functions and data structures for sanitizer statistics gathering.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SANITIZERSTATS_H
#define LLVM_TRANSFORMS_UTILS_SANITIZERSTATS_H

#include "llvm/IR/IRBuilder.h"

namespace llvm {

// Number of bits in data that are used for the sanitizer kind. Needs to match
// __sanitizer::kKindBits in compiler-rt/lib/stats/stats.h
enum { kSanitizerStatKindBits = 3 };

enum SanitizerStatKind {
  SanStat_CFI_VCall,
  SanStat_CFI_NVCall,
  SanStat_CFI_DerivedCast,
  SanStat_CFI_UnrelatedCast,
  SanStat_CFI_ICall,
};

struct SanitizerStatReport {
  SanitizerStatReport(Module *M);

  /// Generates code into B that increments a location-specific counter tagged
  /// with the given sanitizer kind SK.
  void create(IRBuilder<> &B, SanitizerStatKind SK);

  /// Finalize module stats array and add global constructor to register it.
  void finish();

private:
  Module *M;
  GlobalVariable *ModuleStatsGV;
  ArrayType *StatTy;
  StructType *EmptyModuleStatsTy;

  std::vector<Constant *> Inits;
  ArrayType *makeModuleStatsArrayTy();
  StructType *makeModuleStatsTy();
};

}

#endif
