//===-- IndirectCallVisitor.h - indirect call visitor ---------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements defines a visitor class and a helper function that find
// all indirect call-sites in a function.

#ifndef LLVM_ANALYSIS_INDIRECTCALLVISITOR_H
#define LLVM_ANALYSIS_INDIRECTCALLVISITOR_H

#include "llvm/IR/InstVisitor.h"
#include <vector>

namespace llvm {
// Visitor class that finds all indirect call.
struct PGOIndirectCallVisitor : public InstVisitor<PGOIndirectCallVisitor> {
  std::vector<Instruction *> IndirectCalls;
  PGOIndirectCallVisitor() {}

  void visitCallBase(CallBase &Call) {
    if (Call.isIndirectCall())
      IndirectCalls.push_back(&Call);
  }
};

// Helper function that finds all indirect call sites.
inline std::vector<Instruction *> findIndirectCalls(Function &F) {
  PGOIndirectCallVisitor ICV;
  ICV.visit(F);
  return ICV.IndirectCalls;
}
} // namespace llvm

#endif
