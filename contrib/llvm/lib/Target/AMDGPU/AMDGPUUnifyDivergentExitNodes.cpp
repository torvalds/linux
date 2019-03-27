//===- AMDGPUUnifyDivergentExitNodes.cpp ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a variant of the UnifyDivergentExitNodes pass. Rather than ensuring
// there is at most one ret and one unreachable instruction, it ensures there is
// at most one divergent exiting block.
//
// StructurizeCFG can't deal with multi-exit regions formed by branches to
// multiple return nodes. It is not desirable to structurize regions with
// uniform branches, so unifying those to the same return block as divergent
// branches inhibits use of scalar branching. It still can't deal with the case
// where one branch goes to return, and one unreachable. Replace unreachable in
// this case with a return.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-unify-divergent-exit-nodes"

namespace {

class AMDGPUUnifyDivergentExitNodes : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  AMDGPUUnifyDivergentExitNodes() : FunctionPass(ID) {
    initializeAMDGPUUnifyDivergentExitNodesPass(*PassRegistry::getPassRegistry());
  }

  // We can preserve non-critical-edgeness when we unify function exit nodes
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};

} // end anonymous namespace

char AMDGPUUnifyDivergentExitNodes::ID = 0;

char &llvm::AMDGPUUnifyDivergentExitNodesID = AMDGPUUnifyDivergentExitNodes::ID;

INITIALIZE_PASS_BEGIN(AMDGPUUnifyDivergentExitNodes, DEBUG_TYPE,
                     "Unify divergent function exit nodes", false, false)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LegacyDivergenceAnalysis)
INITIALIZE_PASS_END(AMDGPUUnifyDivergentExitNodes, DEBUG_TYPE,
                    "Unify divergent function exit nodes", false, false)

void AMDGPUUnifyDivergentExitNodes::getAnalysisUsage(AnalysisUsage &AU) const{
  // TODO: Preserve dominator tree.
  AU.addRequired<PostDominatorTreeWrapperPass>();

  AU.addRequired<LegacyDivergenceAnalysis>();

  // No divergent values are changed, only blocks and branch edges.
  AU.addPreserved<LegacyDivergenceAnalysis>();

  // We preserve the non-critical-edgeness property
  AU.addPreservedID(BreakCriticalEdgesID);

  // This is a cluster of orthogonal Transforms
  AU.addPreservedID(LowerSwitchID);
  FunctionPass::getAnalysisUsage(AU);

  AU.addRequired<TargetTransformInfoWrapperPass>();
}

/// \returns true if \p BB is reachable through only uniform branches.
/// XXX - Is there a more efficient way to find this?
static bool isUniformlyReached(const LegacyDivergenceAnalysis &DA,
                               BasicBlock &BB) {
  SmallVector<BasicBlock *, 8> Stack;
  SmallPtrSet<BasicBlock *, 8> Visited;

  for (BasicBlock *Pred : predecessors(&BB))
    Stack.push_back(Pred);

  while (!Stack.empty()) {
    BasicBlock *Top = Stack.pop_back_val();
    if (!DA.isUniform(Top->getTerminator()))
      return false;

    for (BasicBlock *Pred : predecessors(Top)) {
      if (Visited.insert(Pred).second)
        Stack.push_back(Pred);
    }
  }

  return true;
}

static BasicBlock *unifyReturnBlockSet(Function &F,
                                       ArrayRef<BasicBlock *> ReturningBlocks,
                                       const TargetTransformInfo &TTI,
                                       StringRef Name) {
  // Otherwise, we need to insert a new basic block into the function, add a PHI
  // nodes (if the function returns values), and convert all of the return
  // instructions into unconditional branches.
  BasicBlock *NewRetBlock = BasicBlock::Create(F.getContext(), Name, &F);

  PHINode *PN = nullptr;
  if (F.getReturnType()->isVoidTy()) {
    ReturnInst::Create(F.getContext(), nullptr, NewRetBlock);
  } else {
    // If the function doesn't return void... add a PHI node to the block...
    PN = PHINode::Create(F.getReturnType(), ReturningBlocks.size(),
                         "UnifiedRetVal");
    NewRetBlock->getInstList().push_back(PN);
    ReturnInst::Create(F.getContext(), PN, NewRetBlock);
  }

  // Loop over all of the blocks, replacing the return instruction with an
  // unconditional branch.
  for (BasicBlock *BB : ReturningBlocks) {
    // Add an incoming element to the PHI node for every return instruction that
    // is merging into this new block...
    if (PN)
      PN->addIncoming(BB->getTerminator()->getOperand(0), BB);

    // Remove and delete the return inst.
    BB->getTerminator()->eraseFromParent();
    BranchInst::Create(NewRetBlock, BB);
  }

  for (BasicBlock *BB : ReturningBlocks) {
    // Cleanup possible branch to unconditional branch to the return.
    simplifyCFG(BB, TTI, {2});
  }

  return NewRetBlock;
}

bool AMDGPUUnifyDivergentExitNodes::runOnFunction(Function &F) {
  auto &PDT = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
  if (PDT.getRoots().size() <= 1)
    return false;

  LegacyDivergenceAnalysis &DA = getAnalysis<LegacyDivergenceAnalysis>();

  // Loop over all of the blocks in a function, tracking all of the blocks that
  // return.
  SmallVector<BasicBlock *, 4> ReturningBlocks;
  SmallVector<BasicBlock *, 4> UnreachableBlocks;

  // Dummy return block for infinite loop.
  BasicBlock *DummyReturnBB = nullptr;

  for (BasicBlock *BB : PDT.getRoots()) {
    if (isa<ReturnInst>(BB->getTerminator())) {
      if (!isUniformlyReached(DA, *BB))
        ReturningBlocks.push_back(BB);
    } else if (isa<UnreachableInst>(BB->getTerminator())) {
      if (!isUniformlyReached(DA, *BB))
        UnreachableBlocks.push_back(BB);
    } else if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {

      ConstantInt *BoolTrue = ConstantInt::getTrue(F.getContext());
      if (DummyReturnBB == nullptr) {
        DummyReturnBB = BasicBlock::Create(F.getContext(),
                                           "DummyReturnBlock", &F);
        Type *RetTy = F.getReturnType();
        Value *RetVal = RetTy->isVoidTy() ? nullptr : UndefValue::get(RetTy);
        ReturnInst::Create(F.getContext(), RetVal, DummyReturnBB);
        ReturningBlocks.push_back(DummyReturnBB);
      }

      if (BI->isUnconditional()) {
        BasicBlock *LoopHeaderBB = BI->getSuccessor(0);
        BI->eraseFromParent(); // Delete the unconditional branch.
        // Add a new conditional branch with a dummy edge to the return block.
        BranchInst::Create(LoopHeaderBB, DummyReturnBB, BoolTrue, BB);
      } else { // Conditional branch.
        // Create a new transition block to hold the conditional branch.
        BasicBlock *TransitionBB = BasicBlock::Create(F.getContext(),
                                                      "TransitionBlock", &F);

        // Move BI from BB to the new transition block.
        BI->removeFromParent();
        TransitionBB->getInstList().push_back(BI);

        // Create a branch that will always branch to the transition block.
        BranchInst::Create(TransitionBB, DummyReturnBB, BoolTrue, BB);
      }
    }
  }

  if (!UnreachableBlocks.empty()) {
    BasicBlock *UnreachableBlock = nullptr;

    if (UnreachableBlocks.size() == 1) {
      UnreachableBlock = UnreachableBlocks.front();
    } else {
      UnreachableBlock = BasicBlock::Create(F.getContext(),
                                            "UnifiedUnreachableBlock", &F);
      new UnreachableInst(F.getContext(), UnreachableBlock);

      for (BasicBlock *BB : UnreachableBlocks) {
        // Remove and delete the unreachable inst.
        BB->getTerminator()->eraseFromParent();
        BranchInst::Create(UnreachableBlock, BB);
      }
    }

    if (!ReturningBlocks.empty()) {
      // Don't create a new unreachable inst if we have a return. The
      // structurizer/annotator can't handle the multiple exits

      Type *RetTy = F.getReturnType();
      Value *RetVal = RetTy->isVoidTy() ? nullptr : UndefValue::get(RetTy);
      // Remove and delete the unreachable inst.
      UnreachableBlock->getTerminator()->eraseFromParent();

      Function *UnreachableIntrin =
        Intrinsic::getDeclaration(F.getParent(), Intrinsic::amdgcn_unreachable);

      // Insert a call to an intrinsic tracking that this is an unreachable
      // point, in case we want to kill the active lanes or something later.
      CallInst::Create(UnreachableIntrin, {}, "", UnreachableBlock);

      // Don't create a scalar trap. We would only want to trap if this code was
      // really reached, but a scalar trap would happen even if no lanes
      // actually reached here.
      ReturnInst::Create(F.getContext(), RetVal, UnreachableBlock);
      ReturningBlocks.push_back(UnreachableBlock);
    }
  }

  // Now handle return blocks.
  if (ReturningBlocks.empty())
    return false; // No blocks return

  if (ReturningBlocks.size() == 1)
    return false; // Already has a single return block

  const TargetTransformInfo &TTI
    = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  unifyReturnBlockSet(F, ReturningBlocks, TTI, "UnifiedReturnBlock");
  return true;
}
