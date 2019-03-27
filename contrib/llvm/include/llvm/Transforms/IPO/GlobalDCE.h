//===-- GlobalDCE.h - DCE unreachable internal functions ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transform is designed to eliminate unreachable internal globals from the
// program.  It uses an aggressive algorithm, searching out globals that are
// known to be alive.  After it finds all of the globals which are needed, it
// deletes whatever is left over.  This allows it to delete recursive chunks of
// the program which are unreachable.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_GLOBALDCE_H
#define LLVM_TRANSFORMS_IPO_GLOBALDCE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include <unordered_map>

namespace llvm {

/// Pass to remove unused function declarations.
class GlobalDCEPass : public PassInfoMixin<GlobalDCEPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

private:
  SmallPtrSet<GlobalValue*, 32> AliveGlobals;

  /// Global -> Global that uses this global.
  DenseMap<GlobalValue *, SmallPtrSet<GlobalValue *, 4>> GVDependencies;

  /// Constant -> Globals that use this global cache.
  std::unordered_map<Constant *, SmallPtrSet<GlobalValue *, 8>>
      ConstantDependenciesCache;

  /// Comdat -> Globals in that Comdat section.
  std::unordered_multimap<Comdat *, GlobalValue *> ComdatMembers;

  void UpdateGVDependencies(GlobalValue &GV);
  void MarkLive(GlobalValue &GV,
                SmallVectorImpl<GlobalValue *> *Updates = nullptr);
  bool RemoveUnusedGlobalValue(GlobalValue &GV);

  void ComputeDependencies(Value *V, SmallPtrSetImpl<GlobalValue *> &U);
};

}

#endif // LLVM_TRANSFORMS_IPO_GLOBALDCE_H
