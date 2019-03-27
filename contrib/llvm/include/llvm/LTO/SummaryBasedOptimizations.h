//=- llvm/LTO/SummaryBasedOptimizations.h -Link time optimizations-*- C++ -*-=//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_SUMMARYBASEDOPTIMIZATIONS_H
#define LLVM_LTO_SUMMARYBASEDOPTIMIZATIONS_H
namespace llvm {
class ModuleSummaryIndex;
void computeSyntheticCounts(ModuleSummaryIndex &Index);

} // namespace llvm
#endif
