//===- PostDominators.cpp - Post-Dominator Calculation --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the post-dominator construction algorithms.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "postdomtree"

#ifdef EXPENSIVE_CHECKS
static constexpr bool ExpensiveChecksEnabled = true;
#else
static constexpr bool ExpensiveChecksEnabled = false;
#endif

//===----------------------------------------------------------------------===//
//  PostDominatorTree Implementation
//===----------------------------------------------------------------------===//

char PostDominatorTreeWrapperPass::ID = 0;

INITIALIZE_PASS(PostDominatorTreeWrapperPass, "postdomtree",
                "Post-Dominator Tree Construction", true, true)

bool PostDominatorTree::invalidate(Function &F, const PreservedAnalyses &PA,
                                   FunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on functions, or the function's
  // CFG have been preserved.
  auto PAC = PA.getChecker<PostDominatorTreeAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>() ||
           PAC.preservedSet<CFGAnalyses>());
}

bool PostDominatorTreeWrapperPass::runOnFunction(Function &F) {
  DT.recalculate(F);
  return false;
}

void PostDominatorTreeWrapperPass::verifyAnalysis() const {
  if (VerifyDomInfo)
    assert(DT.verify(PostDominatorTree::VerificationLevel::Full));
  else if (ExpensiveChecksEnabled)
    assert(DT.verify(PostDominatorTree::VerificationLevel::Basic));
}

void PostDominatorTreeWrapperPass::print(raw_ostream &OS, const Module *) const {
  DT.print(OS);
}

FunctionPass* llvm::createPostDomTree() {
  return new PostDominatorTreeWrapperPass();
}

AnalysisKey PostDominatorTreeAnalysis::Key;

PostDominatorTree PostDominatorTreeAnalysis::run(Function &F,
                                                 FunctionAnalysisManager &) {
  PostDominatorTree PDT(F);
  return PDT;
}

PostDominatorTreePrinterPass::PostDominatorTreePrinterPass(raw_ostream &OS)
  : OS(OS) {}

PreservedAnalyses
PostDominatorTreePrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  OS << "PostDominatorTree for function: " << F.getName() << "\n";
  AM.getResult<PostDominatorTreeAnalysis>(F).print(OS);

  return PreservedAnalyses::all();
}
