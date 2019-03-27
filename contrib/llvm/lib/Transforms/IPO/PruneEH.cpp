//===- PruneEH.cpp - Pass which deletes unused exception handlers ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a simple interprocedural pass which walks the
// call-graph, turning invoke instructions into calls, iff the callee cannot
// throw an exception, and marking functions 'nounwind' if they cannot throw.
// It implements this as a bottom-up traversal of the call-graph.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "prune-eh"

STATISTIC(NumRemoved, "Number of invokes removed");
STATISTIC(NumUnreach, "Number of noreturn calls optimized");

namespace {
  struct PruneEH : public CallGraphSCCPass {
    static char ID; // Pass identification, replacement for typeid
    PruneEH() : CallGraphSCCPass(ID) {
      initializePruneEHPass(*PassRegistry::getPassRegistry());
    }

    // runOnSCC - Analyze the SCC, performing the transformation if possible.
    bool runOnSCC(CallGraphSCC &SCC) override;

  };
}
static bool SimplifyFunction(Function *F, CallGraph &CG);
static void DeleteBasicBlock(BasicBlock *BB, CallGraph &CG);

char PruneEH::ID = 0;
INITIALIZE_PASS_BEGIN(PruneEH, "prune-eh",
                "Remove unused exception handling info", false, false)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_END(PruneEH, "prune-eh",
                "Remove unused exception handling info", false, false)

Pass *llvm::createPruneEHPass() { return new PruneEH(); }

static bool runImpl(CallGraphSCC &SCC, CallGraph &CG) {
  SmallPtrSet<CallGraphNode *, 8> SCCNodes;
  bool MadeChange = false;

  // Fill SCCNodes with the elements of the SCC.  Used for quickly
  // looking up whether a given CallGraphNode is in this SCC.
  for (CallGraphNode *I : SCC)
    SCCNodes.insert(I);

  // First pass, scan all of the functions in the SCC, simplifying them
  // according to what we know.
  for (CallGraphNode *I : SCC)
    if (Function *F = I->getFunction())
      MadeChange |= SimplifyFunction(F, CG);

  // Next, check to see if any callees might throw or if there are any external
  // functions in this SCC: if so, we cannot prune any functions in this SCC.
  // Definitions that are weak and not declared non-throwing might be
  // overridden at linktime with something that throws, so assume that.
  // If this SCC includes the unwind instruction, we KNOW it throws, so
  // obviously the SCC might throw.
  //
  bool SCCMightUnwind = false, SCCMightReturn = false;
  for (CallGraphSCC::iterator I = SCC.begin(), E = SCC.end();
       (!SCCMightUnwind || !SCCMightReturn) && I != E; ++I) {
    Function *F = (*I)->getFunction();
    if (!F) {
      SCCMightUnwind = true;
      SCCMightReturn = true;
    } else if (!F->hasExactDefinition()) {
      SCCMightUnwind |= !F->doesNotThrow();
      SCCMightReturn |= !F->doesNotReturn();
    } else {
      bool CheckUnwind = !SCCMightUnwind && !F->doesNotThrow();
      bool CheckReturn = !SCCMightReturn && !F->doesNotReturn();
      // Determine if we should scan for InlineAsm in a naked function as it
      // is the only way to return without a ReturnInst.  Only do this for
      // no-inline functions as functions which may be inlined cannot
      // meaningfully return via assembly.
      bool CheckReturnViaAsm = CheckReturn &&
                               F->hasFnAttribute(Attribute::Naked) &&
                               F->hasFnAttribute(Attribute::NoInline);

      if (!CheckUnwind && !CheckReturn)
        continue;

      for (const BasicBlock &BB : *F) {
        const Instruction *TI = BB.getTerminator();
        if (CheckUnwind && TI->mayThrow()) {
          SCCMightUnwind = true;
        } else if (CheckReturn && isa<ReturnInst>(TI)) {
          SCCMightReturn = true;
        }

        for (const Instruction &I : BB) {
          if ((!CheckUnwind || SCCMightUnwind) &&
              (!CheckReturnViaAsm || SCCMightReturn))
            break;

          // Check to see if this function performs an unwind or calls an
          // unwinding function.
          if (CheckUnwind && !SCCMightUnwind && I.mayThrow()) {
            bool InstMightUnwind = true;
            if (const auto *CI = dyn_cast<CallInst>(&I)) {
              if (Function *Callee = CI->getCalledFunction()) {
                CallGraphNode *CalleeNode = CG[Callee];
                // If the callee is outside our current SCC then we may throw
                // because it might.  If it is inside, do nothing.
                if (SCCNodes.count(CalleeNode) > 0)
                  InstMightUnwind = false;
              }
            }
            SCCMightUnwind |= InstMightUnwind;
          }
          if (CheckReturnViaAsm && !SCCMightReturn)
            if (auto ICS = ImmutableCallSite(&I))
              if (const auto *IA = dyn_cast<InlineAsm>(ICS.getCalledValue()))
                if (IA->hasSideEffects())
                  SCCMightReturn = true;
        }

        if (SCCMightUnwind && SCCMightReturn)
          break;
      }
    }
  }

  // If the SCC doesn't unwind or doesn't throw, note this fact.
  if (!SCCMightUnwind || !SCCMightReturn)
    for (CallGraphNode *I : SCC) {
      Function *F = I->getFunction();

      if (!SCCMightUnwind && !F->hasFnAttribute(Attribute::NoUnwind)) {
        F->addFnAttr(Attribute::NoUnwind);
        MadeChange = true;
      }

      if (!SCCMightReturn && !F->hasFnAttribute(Attribute::NoReturn)) {
        F->addFnAttr(Attribute::NoReturn);
        MadeChange = true;
      }
    }

  for (CallGraphNode *I : SCC) {
    // Convert any invoke instructions to non-throwing functions in this node
    // into call instructions with a branch.  This makes the exception blocks
    // dead.
    if (Function *F = I->getFunction())
      MadeChange |= SimplifyFunction(F, CG);
  }

  return MadeChange;
}


bool PruneEH::runOnSCC(CallGraphSCC &SCC) {
  if (skipSCC(SCC))
    return false;
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  return runImpl(SCC, CG);
}


// SimplifyFunction - Given information about callees, simplify the specified
// function if we have invokes to non-unwinding functions or code after calls to
// no-return functions.
static bool SimplifyFunction(Function *F, CallGraph &CG) {
  bool MadeChange = false;
  for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
    if (InvokeInst *II = dyn_cast<InvokeInst>(BB->getTerminator()))
      if (II->doesNotThrow() && canSimplifyInvokeNoUnwind(F)) {
        BasicBlock *UnwindBlock = II->getUnwindDest();
        removeUnwindEdge(&*BB);

        // If the unwind block is now dead, nuke it.
        if (pred_empty(UnwindBlock))
          DeleteBasicBlock(UnwindBlock, CG);  // Delete the new BB.

        ++NumRemoved;
        MadeChange = true;
      }

    for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; )
      if (CallInst *CI = dyn_cast<CallInst>(I++))
        if (CI->doesNotReturn() && !isa<UnreachableInst>(I)) {
          // This call calls a function that cannot return.  Insert an
          // unreachable instruction after it and simplify the code.  Do this
          // by splitting the BB, adding the unreachable, then deleting the
          // new BB.
          BasicBlock *New = BB->splitBasicBlock(I);

          // Remove the uncond branch and add an unreachable.
          BB->getInstList().pop_back();
          new UnreachableInst(BB->getContext(), &*BB);

          DeleteBasicBlock(New, CG);  // Delete the new BB.
          MadeChange = true;
          ++NumUnreach;
          break;
        }
  }

  return MadeChange;
}

/// DeleteBasicBlock - remove the specified basic block from the program,
/// updating the callgraph to reflect any now-obsolete edges due to calls that
/// exist in the BB.
static void DeleteBasicBlock(BasicBlock *BB, CallGraph &CG) {
  assert(pred_empty(BB) && "BB is not dead!");

  Instruction *TokenInst = nullptr;

  CallGraphNode *CGN = CG[BB->getParent()];
  for (BasicBlock::iterator I = BB->end(), E = BB->begin(); I != E; ) {
    --I;

    if (I->getType()->isTokenTy()) {
      TokenInst = &*I;
      break;
    }

    if (auto CS = CallSite (&*I)) {
      const Function *Callee = CS.getCalledFunction();
      if (!Callee || !Intrinsic::isLeaf(Callee->getIntrinsicID()))
        CGN->removeCallEdgeFor(CS);
      else if (!Callee->isIntrinsic())
        CGN->removeCallEdgeFor(CS);
    }

    if (!I->use_empty())
      I->replaceAllUsesWith(UndefValue::get(I->getType()));
  }

  if (TokenInst) {
    if (!TokenInst->isTerminator())
      changeToUnreachable(TokenInst->getNextNode(), /*UseLLVMTrap=*/false);
  } else {
    // Get the list of successors of this block.
    std::vector<BasicBlock *> Succs(succ_begin(BB), succ_end(BB));

    for (unsigned i = 0, e = Succs.size(); i != e; ++i)
      Succs[i]->removePredecessor(BB);

    BB->eraseFromParent();
  }
}
