//===- LowerExpectIntrinsic.cpp - Lower expect intrinsic ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers the 'expect' intrinsic to LLVM metadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"

using namespace llvm;

#define DEBUG_TYPE "lower-expect-intrinsic"

STATISTIC(ExpectIntrinsicsHandled,
          "Number of 'expect' intrinsic instructions handled");

// These default values are chosen to represent an extremely skewed outcome for
// a condition, but they leave some room for interpretation by later passes.
//
// If the documentation for __builtin_expect() was made explicit that it should
// only be used in extreme cases, we could make this ratio higher. As it stands,
// programmers may be using __builtin_expect() / llvm.expect to annotate that a
// branch is likely or unlikely to be taken.
//
// There is a known dependency on this ratio in CodeGenPrepare when transforming
// 'select' instructions. It may be worthwhile to hoist these values to some
// shared space, so they can be used directly by other passes.

static cl::opt<uint32_t> LikelyBranchWeight(
    "likely-branch-weight", cl::Hidden, cl::init(2000),
    cl::desc("Weight of the branch likely to be taken (default = 2000)"));
static cl::opt<uint32_t> UnlikelyBranchWeight(
    "unlikely-branch-weight", cl::Hidden, cl::init(1),
    cl::desc("Weight of the branch unlikely to be taken (default = 1)"));

static bool handleSwitchExpect(SwitchInst &SI) {
  CallInst *CI = dyn_cast<CallInst>(SI.getCondition());
  if (!CI)
    return false;

  Function *Fn = CI->getCalledFunction();
  if (!Fn || Fn->getIntrinsicID() != Intrinsic::expect)
    return false;

  Value *ArgValue = CI->getArgOperand(0);
  ConstantInt *ExpectedValue = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!ExpectedValue)
    return false;

  SwitchInst::CaseHandle Case = *SI.findCaseValue(ExpectedValue);
  unsigned n = SI.getNumCases(); // +1 for default case.
  SmallVector<uint32_t, 16> Weights(n + 1, UnlikelyBranchWeight);

  if (Case == *SI.case_default())
    Weights[0] = LikelyBranchWeight;
  else
    Weights[Case.getCaseIndex() + 1] = LikelyBranchWeight;

  SI.setMetadata(LLVMContext::MD_prof,
                 MDBuilder(CI->getContext()).createBranchWeights(Weights));

  SI.setCondition(ArgValue);
  return true;
}

/// Handler for PHINodes that define the value argument to an
/// @llvm.expect call.
///
/// If the operand of the phi has a constant value and it 'contradicts'
/// with the expected value of phi def, then the corresponding incoming
/// edge of the phi is unlikely to be taken. Using that information,
/// the branch probability info for the originating branch can be inferred.
static void handlePhiDef(CallInst *Expect) {
  Value &Arg = *Expect->getArgOperand(0);
  ConstantInt *ExpectedValue = dyn_cast<ConstantInt>(Expect->getArgOperand(1));
  if (!ExpectedValue)
    return;
  const APInt &ExpectedPhiValue = ExpectedValue->getValue();

  // Walk up in backward a list of instructions that
  // have 'copy' semantics by 'stripping' the copies
  // until a PHI node or an instruction of unknown kind
  // is reached. Negation via xor is also handled.
  //
  //       C = PHI(...);
  //       B = C;
  //       A = B;
  //       D = __builtin_expect(A, 0);
  //
  Value *V = &Arg;
  SmallVector<Instruction *, 4> Operations;
  while (!isa<PHINode>(V)) {
    if (ZExtInst *ZExt = dyn_cast<ZExtInst>(V)) {
      V = ZExt->getOperand(0);
      Operations.push_back(ZExt);
      continue;
    }

    if (SExtInst *SExt = dyn_cast<SExtInst>(V)) {
      V = SExt->getOperand(0);
      Operations.push_back(SExt);
      continue;
    }

    BinaryOperator *BinOp = dyn_cast<BinaryOperator>(V);
    if (!BinOp || BinOp->getOpcode() != Instruction::Xor)
      return;

    ConstantInt *CInt = dyn_cast<ConstantInt>(BinOp->getOperand(1));
    if (!CInt)
      return;

    V = BinOp->getOperand(0);
    Operations.push_back(BinOp);
  }

  // Executes the recorded operations on input 'Value'.
  auto ApplyOperations = [&](const APInt &Value) {
    APInt Result = Value;
    for (auto Op : llvm::reverse(Operations)) {
      switch (Op->getOpcode()) {
      case Instruction::Xor:
        Result ^= cast<ConstantInt>(Op->getOperand(1))->getValue();
        break;
      case Instruction::ZExt:
        Result = Result.zext(Op->getType()->getIntegerBitWidth());
        break;
      case Instruction::SExt:
        Result = Result.sext(Op->getType()->getIntegerBitWidth());
        break;
      default:
        llvm_unreachable("Unexpected operation");
      }
    }
    return Result;
  };

  auto *PhiDef = dyn_cast<PHINode>(V);

  // Get the first dominating conditional branch of the operand
  // i's incoming block.
  auto GetDomConditional = [&](unsigned i) -> BranchInst * {
    BasicBlock *BB = PhiDef->getIncomingBlock(i);
    BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
    if (BI && BI->isConditional())
      return BI;
    BB = BB->getSinglePredecessor();
    if (!BB)
      return nullptr;
    BI = dyn_cast<BranchInst>(BB->getTerminator());
    if (!BI || BI->isUnconditional())
      return nullptr;
    return BI;
  };

  // Now walk through all Phi operands to find phi oprerands with values
  // conflicting with the expected phi output value. Any such operand
  // indicates the incoming edge to that operand is unlikely.
  for (unsigned i = 0, e = PhiDef->getNumIncomingValues(); i != e; ++i) {

    Value *PhiOpnd = PhiDef->getIncomingValue(i);
    ConstantInt *CI = dyn_cast<ConstantInt>(PhiOpnd);
    if (!CI)
      continue;

    // Not an interesting case when IsUnlikely is false -- we can not infer
    // anything useful when the operand value matches the expected phi
    // output.
    if (ExpectedPhiValue == ApplyOperations(CI->getValue()))
      continue;

    BranchInst *BI = GetDomConditional(i);
    if (!BI)
      continue;

    MDBuilder MDB(PhiDef->getContext());

    // There are two situations in which an operand of the PhiDef comes
    // from a given successor of a branch instruction BI.
    // 1) When the incoming block of the operand is the successor block;
    // 2) When the incoming block is BI's enclosing block and the
    // successor is the PhiDef's enclosing block.
    //
    // Returns true if the operand which comes from OpndIncomingBB
    // comes from outgoing edge of BI that leads to Succ block.
    auto *OpndIncomingBB = PhiDef->getIncomingBlock(i);
    auto IsOpndComingFromSuccessor = [&](BasicBlock *Succ) {
      if (OpndIncomingBB == Succ)
        // If this successor is the incoming block for this
        // Phi operand, then this successor does lead to the Phi.
        return true;
      if (OpndIncomingBB == BI->getParent() && Succ == PhiDef->getParent())
        // Otherwise, if the edge is directly from the branch
        // to the Phi, this successor is the one feeding this
        // Phi operand.
        return true;
      return false;
    };

    if (IsOpndComingFromSuccessor(BI->getSuccessor(1)))
      BI->setMetadata(
          LLVMContext::MD_prof,
          MDB.createBranchWeights(LikelyBranchWeight, UnlikelyBranchWeight));
    else if (IsOpndComingFromSuccessor(BI->getSuccessor(0)))
      BI->setMetadata(
          LLVMContext::MD_prof,
          MDB.createBranchWeights(UnlikelyBranchWeight, LikelyBranchWeight));
  }
}

// Handle both BranchInst and SelectInst.
template <class BrSelInst> static bool handleBrSelExpect(BrSelInst &BSI) {

  // Handle non-optimized IR code like:
  //   %expval = call i64 @llvm.expect.i64(i64 %conv1, i64 1)
  //   %tobool = icmp ne i64 %expval, 0
  //   br i1 %tobool, label %if.then, label %if.end
  //
  // Or the following simpler case:
  //   %expval = call i1 @llvm.expect.i1(i1 %cmp, i1 1)
  //   br i1 %expval, label %if.then, label %if.end

  CallInst *CI;

  ICmpInst *CmpI = dyn_cast<ICmpInst>(BSI.getCondition());
  CmpInst::Predicate Predicate;
  ConstantInt *CmpConstOperand = nullptr;
  if (!CmpI) {
    CI = dyn_cast<CallInst>(BSI.getCondition());
    Predicate = CmpInst::ICMP_NE;
  } else {
    Predicate = CmpI->getPredicate();
    if (Predicate != CmpInst::ICMP_NE && Predicate != CmpInst::ICMP_EQ)
      return false;

    CmpConstOperand = dyn_cast<ConstantInt>(CmpI->getOperand(1));
    if (!CmpConstOperand)
      return false;
    CI = dyn_cast<CallInst>(CmpI->getOperand(0));
  }

  if (!CI)
    return false;

  uint64_t ValueComparedTo = 0;
  if (CmpConstOperand) {
    if (CmpConstOperand->getBitWidth() > 64)
      return false;
    ValueComparedTo = CmpConstOperand->getZExtValue();
  }

  Function *Fn = CI->getCalledFunction();
  if (!Fn || Fn->getIntrinsicID() != Intrinsic::expect)
    return false;

  Value *ArgValue = CI->getArgOperand(0);
  ConstantInt *ExpectedValue = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!ExpectedValue)
    return false;

  MDBuilder MDB(CI->getContext());
  MDNode *Node;

  if ((ExpectedValue->getZExtValue() == ValueComparedTo) ==
      (Predicate == CmpInst::ICMP_EQ))
    Node = MDB.createBranchWeights(LikelyBranchWeight, UnlikelyBranchWeight);
  else
    Node = MDB.createBranchWeights(UnlikelyBranchWeight, LikelyBranchWeight);

  BSI.setMetadata(LLVMContext::MD_prof, Node);

  if (CmpI)
    CmpI->setOperand(0, ArgValue);
  else
    BSI.setCondition(ArgValue);
  return true;
}

static bool handleBranchExpect(BranchInst &BI) {
  if (BI.isUnconditional())
    return false;

  return handleBrSelExpect<BranchInst>(BI);
}

static bool lowerExpectIntrinsic(Function &F) {
  bool Changed = false;

  for (BasicBlock &BB : F) {
    // Create "block_weights" metadata.
    if (BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator())) {
      if (handleBranchExpect(*BI))
        ExpectIntrinsicsHandled++;
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(BB.getTerminator())) {
      if (handleSwitchExpect(*SI))
        ExpectIntrinsicsHandled++;
    }

    // Remove llvm.expect intrinsics. Iterate backwards in order
    // to process select instructions before the intrinsic gets
    // removed.
    for (auto BI = BB.rbegin(), BE = BB.rend(); BI != BE;) {
      Instruction *Inst = &*BI++;
      CallInst *CI = dyn_cast<CallInst>(Inst);
      if (!CI) {
        if (SelectInst *SI = dyn_cast<SelectInst>(Inst)) {
          if (handleBrSelExpect(*SI))
            ExpectIntrinsicsHandled++;
        }
        continue;
      }

      Function *Fn = CI->getCalledFunction();
      if (Fn && Fn->getIntrinsicID() == Intrinsic::expect) {
        // Before erasing the llvm.expect, walk backward to find
        // phi that define llvm.expect's first arg, and
        // infer branch probability:
        handlePhiDef(CI);
        Value *Exp = CI->getArgOperand(0);
        CI->replaceAllUsesWith(Exp);
        CI->eraseFromParent();
        Changed = true;
      }
    }
  }

  return Changed;
}

PreservedAnalyses LowerExpectIntrinsicPass::run(Function &F,
                                                FunctionAnalysisManager &) {
  if (lowerExpectIntrinsic(F))
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}

namespace {
/// Legacy pass for lowering expect intrinsics out of the IR.
///
/// When this pass is run over a function it uses expect intrinsics which feed
/// branches and switches to provide branch weight metadata for those
/// terminators. It then removes the expect intrinsics from the IR so the rest
/// of the optimizer can ignore them.
class LowerExpectIntrinsic : public FunctionPass {
public:
  static char ID;
  LowerExpectIntrinsic() : FunctionPass(ID) {
    initializeLowerExpectIntrinsicPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override { return lowerExpectIntrinsic(F); }
};
}

char LowerExpectIntrinsic::ID = 0;
INITIALIZE_PASS(LowerExpectIntrinsic, "lower-expect",
                "Lower 'expect' Intrinsics", false, false)

FunctionPass *llvm::createLowerExpectIntrinsicPass() {
  return new LowerExpectIntrinsic();
}
