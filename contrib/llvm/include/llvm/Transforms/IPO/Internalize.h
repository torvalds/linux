//====- Internalize.h - Internalization API ---------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass loops over all of the functions and variables in the input module.
// If the function or variable does not need to be preserved according to the
// client supplied callback, it is marked as internal.
//
// This transformation would not be legal in a regular compilation, but it gets
// extra information from the linker about what is safe.
//
// For example: Internalizing a function with external linkage. Only if we are
// told it is only used from within this module, it is safe to do it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INTERNALIZE_H
#define LLVM_TRANSFORMS_IPO_INTERNALIZE_H

#include "llvm/ADT/StringSet.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/PassManager.h"
#include <functional>
#include <set>

namespace llvm {
class Module;
class CallGraph;

/// A pass that internalizes all functions and variables other than those that
/// must be preserved according to \c MustPreserveGV.
class InternalizePass : public PassInfoMixin<InternalizePass> {
  /// Client supplied callback to control wheter a symbol must be preserved.
  const std::function<bool(const GlobalValue &)> MustPreserveGV;
  /// Set of symbols private to the compiler that this pass should not touch.
  StringSet<> AlwaysPreserved;

  /// Return false if we're allowed to internalize this GV.
  bool shouldPreserveGV(const GlobalValue &GV);
  /// Internalize GV if it is possible to do so, i.e. it is not externally
  /// visible and is not a member of an externally visible comdat.
  bool maybeInternalize(GlobalValue &GV,
                        const std::set<const Comdat *> &ExternalComdats);
  /// If GV is part of a comdat and is externally visible, keep track of its
  /// comdat so that we don't internalize any of its members.
  void checkComdatVisibility(GlobalValue &GV,
                             std::set<const Comdat *> &ExternalComdats);

public:
  InternalizePass();
  InternalizePass(std::function<bool(const GlobalValue &)> MustPreserveGV)
      : MustPreserveGV(std::move(MustPreserveGV)) {}

  /// Run the internalizer on \p TheModule, returns true if any changes was
  /// made.
  ///
  /// If the CallGraph \p CG is supplied, it will be updated when
  /// internalizing a function (by removing any edge from the "external node")
  bool internalizeModule(Module &TheModule, CallGraph *CG = nullptr);

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Helper function to internalize functions and variables in a Module.
inline bool
internalizeModule(Module &TheModule,
                  std::function<bool(const GlobalValue &)> MustPreserveGV,
                  CallGraph *CG = nullptr) {
  return InternalizePass(std::move(MustPreserveGV))
      .internalizeModule(TheModule, CG);
}
} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_INTERNALIZE_H
