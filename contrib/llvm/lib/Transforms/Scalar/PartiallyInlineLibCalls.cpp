//===--- PartiallyInlineLibCalls.cpp - Partially inline libcalls ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass tries to partially inline the fast path of well-known library
// functions, such as using square-root instructions for cases where sqrt()
// does not need to set errno.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/PartiallyInlineLibCalls.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "partially-inline-libcalls"

DEBUG_COUNTER(PILCounter, "partially-inline-libcalls-transform",
              "Controls transformations in partially-inline-libcalls");

static bool optimizeSQRT(CallInst *Call, Function *CalledFunc,
                         BasicBlock &CurrBB, Function::iterator &BB,
                         const TargetTransformInfo *TTI) {
  // There is no need to change the IR, since backend will emit sqrt
  // instruction if the call has already been marked read-only.
  if (Call->onlyReadsMemory())
    return false;

  if (!DebugCounter::shouldExecute(PILCounter))
    return false;

  // Do the following transformation:
  //
  // (before)
  // dst = sqrt(src)
  //
  // (after)
  // v0 = sqrt_noreadmem(src) # native sqrt instruction.
  // [if (v0 is a NaN) || if (src < 0)]
  //   v1 = sqrt(src)         # library call.
  // dst = phi(v0, v1)
  //

  // Move all instructions following Call to newly created block JoinBB.
  // Create phi and replace all uses.
  BasicBlock *JoinBB = llvm::SplitBlock(&CurrBB, Call->getNextNode());
  IRBuilder<> Builder(JoinBB, JoinBB->begin());
  Type *Ty = Call->getType();
  PHINode *Phi = Builder.CreatePHI(Ty, 2);
  Call->replaceAllUsesWith(Phi);

  // Create basic block LibCallBB and insert a call to library function sqrt.
  BasicBlock *LibCallBB = BasicBlock::Create(CurrBB.getContext(), "call.sqrt",
                                             CurrBB.getParent(), JoinBB);
  Builder.SetInsertPoint(LibCallBB);
  Instruction *LibCall = Call->clone();
  Builder.Insert(LibCall);
  Builder.CreateBr(JoinBB);

  // Add attribute "readnone" so that backend can use a native sqrt instruction
  // for this call. Insert a FP compare instruction and a conditional branch
  // at the end of CurrBB.
  Call->addAttribute(AttributeList::FunctionIndex, Attribute::ReadNone);
  CurrBB.getTerminator()->eraseFromParent();
  Builder.SetInsertPoint(&CurrBB);
  Value *FCmp = TTI->isFCmpOrdCheaperThanFCmpZero(Ty)
                    ? Builder.CreateFCmpORD(Call, Call)
                    : Builder.CreateFCmpOGE(Call->getOperand(0),
                                            ConstantFP::get(Ty, 0.0));
  Builder.CreateCondBr(FCmp, JoinBB, LibCallBB);

  // Add phi operands.
  Phi->addIncoming(Call, &CurrBB);
  Phi->addIncoming(LibCall, LibCallBB);

  BB = JoinBB->getIterator();
  return true;
}

static bool runPartiallyInlineLibCalls(Function &F, TargetLibraryInfo *TLI,
                                       const TargetTransformInfo *TTI) {
  bool Changed = false;

  Function::iterator CurrBB;
  for (Function::iterator BB = F.begin(), BE = F.end(); BB != BE;) {
    CurrBB = BB++;

    for (BasicBlock::iterator II = CurrBB->begin(), IE = CurrBB->end();
         II != IE; ++II) {
      CallInst *Call = dyn_cast<CallInst>(&*II);
      Function *CalledFunc;

      if (!Call || !(CalledFunc = Call->getCalledFunction()))
        continue;

      if (Call->isNoBuiltin())
        continue;

      // Skip if function either has local linkage or is not a known library
      // function.
      LibFunc LF;
      if (CalledFunc->hasLocalLinkage() ||
          !TLI->getLibFunc(*CalledFunc, LF) || !TLI->has(LF))
        continue;

      switch (LF) {
      case LibFunc_sqrtf:
      case LibFunc_sqrt:
        if (TTI->haveFastSqrt(Call->getType()) &&
            optimizeSQRT(Call, CalledFunc, *CurrBB, BB, TTI))
          break;
        continue;
      default:
        continue;
      }

      Changed = true;
      break;
    }
  }

  return Changed;
}

PreservedAnalyses
PartiallyInlineLibCallsPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  if (!runPartiallyInlineLibCalls(F, &TLI, &TTI))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

namespace {
class PartiallyInlineLibCallsLegacyPass : public FunctionPass {
public:
  static char ID;

  PartiallyInlineLibCallsLegacyPass() : FunctionPass(ID) {
    initializePartiallyInlineLibCallsLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    TargetLibraryInfo *TLI =
        &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
    const TargetTransformInfo *TTI =
        &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    return runPartiallyInlineLibCalls(F, TLI, TTI);
  }
};
}

char PartiallyInlineLibCallsLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(PartiallyInlineLibCallsLegacyPass,
                      "partially-inline-libcalls",
                      "Partially inline calls to library functions", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(PartiallyInlineLibCallsLegacyPass,
                    "partially-inline-libcalls",
                    "Partially inline calls to library functions", false, false)

FunctionPass *llvm::createPartiallyInlineLibCallsPass() {
  return new PartiallyInlineLibCallsLegacyPass();
}
