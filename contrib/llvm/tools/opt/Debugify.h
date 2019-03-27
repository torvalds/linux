//===- Debugify.h - Attach synthetic debug info to everything -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file Interface to the `debugify` synthetic debug info testing utility.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OPT_DEBUGIFY_H
#define LLVM_TOOLS_OPT_DEBUGIFY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

llvm::ModulePass *createDebugifyModulePass();
llvm::FunctionPass *createDebugifyFunctionPass();

struct NewPMDebugifyPass : public llvm::PassInfoMixin<NewPMDebugifyPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

/// Track how much `debugify` information has been lost.
struct DebugifyStatistics {
  /// Number of missing dbg.values.
  unsigned NumDbgValuesMissing = 0;

  /// Number of dbg.values expected.
  unsigned NumDbgValuesExpected = 0;

  /// Number of instructions with empty debug locations.
  unsigned NumDbgLocsMissing = 0;

  /// Number of instructions expected to have debug locations.
  unsigned NumDbgLocsExpected = 0;

  /// Get the ratio of missing/expected dbg.values.
  float getMissingValueRatio() const {
    return float(NumDbgValuesMissing) / float(NumDbgLocsExpected);
  }

  /// Get the ratio of missing/expected instructions with locations.
  float getEmptyLocationRatio() const {
    return float(NumDbgLocsMissing) / float(NumDbgLocsExpected);
  }
};

/// Map pass names to a per-pass DebugifyStatistics instance.
using DebugifyStatsMap = llvm::MapVector<llvm::StringRef, DebugifyStatistics>;

/// Export per-pass debugify statistics to the file specified by \p Path.
void exportDebugifyStats(llvm::StringRef Path, const DebugifyStatsMap &Map);

llvm::ModulePass *
createCheckDebugifyModulePass(bool Strip = false,
                              llvm::StringRef NameOfWrappedPass = "",
                              DebugifyStatsMap *StatsMap = nullptr);

llvm::FunctionPass *
createCheckDebugifyFunctionPass(bool Strip = false,
                                llvm::StringRef NameOfWrappedPass = "",
                                DebugifyStatsMap *StatsMap = nullptr);

struct NewPMCheckDebugifyPass
    : public llvm::PassInfoMixin<NewPMCheckDebugifyPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};

#endif // LLVM_TOOLS_OPT_DEBUGIFY_H
