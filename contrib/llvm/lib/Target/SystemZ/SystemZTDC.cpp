//===-- SystemZTDC.cpp - Utilize Test Data Class instruction --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass looks for instructions that can be replaced by a Test Data Class
// instruction, and replaces them when profitable.
//
// Roughly, the following rules are recognized:
//
// 1: fcmp pred X, 0 -> tdc X, mask
// 2: fcmp pred X, +-inf -> tdc X, mask
// 3: fcmp pred X, +-minnorm -> tdc X, mask
// 4: tdc (fabs X), mask -> tdc X, newmask
// 5: icmp slt (bitcast float X to int), 0 -> tdc X, mask [ie. signbit]
// 6: icmp sgt (bitcast float X to int), -1 -> tdc X, mask
// 7: icmp ne/eq (call @llvm.s390.tdc.*(X, mask)) -> tdc X, mask/~mask
// 8: and i1 (tdc X, M1), (tdc X, M2) -> tdc X, (M1 & M2)
// 9: or i1 (tdc X, M1), (tdc X, M2) -> tdc X, (M1 | M2)
// 10: xor i1 (tdc X, M1), (tdc X, M2) -> tdc X, (M1 ^ M2)
//
// The pass works in 4 steps:
//
// 1. All fcmp and icmp instructions in a function are checked for a match
//    with rules 1-3 and 5-7.  Their TDC equivalents are stored in
//    the ConvertedInsts mapping.  If the operand of a fcmp instruction is
//    a fabs, it's also folded according to rule 4.
// 2. All and/or/xor i1 instructions whose both operands have been already
//    mapped are mapped according to rules 8-10.  LogicOpsWorklist is used
//    as a queue of instructions to check.
// 3. All mapped instructions that are considered worthy of conversion (ie.
//    replacing them will actually simplify the final code) are replaced
//    with a call to the s390.tdc intrinsic.
// 4. All intermediate results of replaced instructions are removed if unused.
//
// Instructions that match rules 1-3 are considered unworthy of conversion
// on their own (since a comparison instruction is superior), but are mapped
// in the hopes of folding the result using rules 4 and 8-10 (likely removing
// the original comparison in the process).
//
//===----------------------------------------------------------------------===//

#include "SystemZ.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include <deque>
#include <set>

using namespace llvm;

namespace llvm {
  void initializeSystemZTDCPassPass(PassRegistry&);
}

namespace {

class SystemZTDCPass : public FunctionPass {
public:
  static char ID;
  SystemZTDCPass() : FunctionPass(ID) {
    initializeSystemZTDCPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;
private:
  // Maps seen instructions that can be mapped to a TDC, values are
  // (TDC operand, TDC mask, worthy flag) triples.
  MapVector<Instruction *, std::tuple<Value *, int, bool>> ConvertedInsts;
  // The queue of and/or/xor i1 instructions to be potentially folded.
  std::vector<BinaryOperator *> LogicOpsWorklist;
  // Instructions matched while folding, to be removed at the end if unused.
  std::set<Instruction *> PossibleJunk;

  // Tries to convert a fcmp instruction.
  void convertFCmp(CmpInst &I);

  // Tries to convert an icmp instruction.
  void convertICmp(CmpInst &I);

  // Tries to convert an i1 and/or/xor instruction, whose both operands
  // have been already converted.
  void convertLogicOp(BinaryOperator &I);

  // Marks an instruction as converted - adds it to ConvertedInsts and adds
  // any and/or/xor i1 users to the queue.
  void converted(Instruction *I, Value *V, int Mask, bool Worthy) {
    ConvertedInsts[I] = std::make_tuple(V, Mask, Worthy);
    auto &M = *I->getFunction()->getParent();
    auto &Ctx = M.getContext();
    for (auto *U : I->users()) {
      auto *LI = dyn_cast<BinaryOperator>(U);
      if (LI && LI->getType() == Type::getInt1Ty(Ctx) &&
          (LI->getOpcode() == Instruction::And ||
           LI->getOpcode() == Instruction::Or ||
           LI->getOpcode() == Instruction::Xor)) {
        LogicOpsWorklist.push_back(LI);
      }
    }
  }
};

} // end anonymous namespace

char SystemZTDCPass::ID = 0;
INITIALIZE_PASS(SystemZTDCPass, "systemz-tdc",
                "SystemZ Test Data Class optimization", false, false)

FunctionPass *llvm::createSystemZTDCPass() {
  return new SystemZTDCPass();
}

void SystemZTDCPass::convertFCmp(CmpInst &I) {
  Value *Op0 = I.getOperand(0);
  auto *Const = dyn_cast<ConstantFP>(I.getOperand(1));
  auto Pred = I.getPredicate();
  // Only comparisons with consts are interesting.
  if (!Const)
    return;
  // Compute the smallest normal number (and its negation).
  auto &Sem = Op0->getType()->getFltSemantics();
  APFloat Smallest = APFloat::getSmallestNormalized(Sem);
  APFloat NegSmallest = Smallest;
  NegSmallest.changeSign();
  // Check if Const is one of our recognized consts.
  int WhichConst;
  if (Const->isZero()) {
    // All comparisons with 0 can be converted.
    WhichConst = 0;
  } else if (Const->isInfinity()) {
    // Likewise for infinities.
    WhichConst = Const->isNegative() ? 2 : 1;
  } else if (Const->isExactlyValue(Smallest)) {
    // For Smallest, we cannot do EQ separately from GT.
    if ((Pred & CmpInst::FCMP_OGE) != CmpInst::FCMP_OGE &&
        (Pred & CmpInst::FCMP_OGE) != 0)
      return;
    WhichConst = 3;
  } else if (Const->isExactlyValue(NegSmallest)) {
    // Likewise for NegSmallest, we cannot do EQ separately from LT.
    if ((Pred & CmpInst::FCMP_OLE) != CmpInst::FCMP_OLE &&
        (Pred & CmpInst::FCMP_OLE) != 0)
      return;
    WhichConst = 4;
  } else {
    // Not one of our special constants.
    return;
  }
  // Partial masks to use for EQ, GT, LT, UN comparisons, respectively.
  static const int Masks[][4] = {
    { // 0
      SystemZ::TDCMASK_ZERO,              // eq
      SystemZ::TDCMASK_POSITIVE,          // gt
      SystemZ::TDCMASK_NEGATIVE,          // lt
      SystemZ::TDCMASK_NAN,               // un
    },
    { // inf
      SystemZ::TDCMASK_INFINITY_PLUS,     // eq
      0,                                  // gt
      (SystemZ::TDCMASK_ZERO |
       SystemZ::TDCMASK_NEGATIVE |
       SystemZ::TDCMASK_NORMAL_PLUS |
       SystemZ::TDCMASK_SUBNORMAL_PLUS),  // lt
      SystemZ::TDCMASK_NAN,               // un
    },
    { // -inf
      SystemZ::TDCMASK_INFINITY_MINUS,    // eq
      (SystemZ::TDCMASK_ZERO |
       SystemZ::TDCMASK_POSITIVE |
       SystemZ::TDCMASK_NORMAL_MINUS |
       SystemZ::TDCMASK_SUBNORMAL_MINUS), // gt
      0,                                  // lt
      SystemZ::TDCMASK_NAN,               // un
    },
    { // minnorm
      0,                                  // eq (unsupported)
      (SystemZ::TDCMASK_NORMAL_PLUS |
       SystemZ::TDCMASK_INFINITY_PLUS),   // gt (actually ge)
      (SystemZ::TDCMASK_ZERO |
       SystemZ::TDCMASK_NEGATIVE |
       SystemZ::TDCMASK_SUBNORMAL_PLUS),  // lt
      SystemZ::TDCMASK_NAN,               // un
    },
    { // -minnorm
      0,                                  // eq (unsupported)
      (SystemZ::TDCMASK_ZERO |
       SystemZ::TDCMASK_POSITIVE |
       SystemZ::TDCMASK_SUBNORMAL_MINUS), // gt
      (SystemZ::TDCMASK_NORMAL_MINUS |
       SystemZ::TDCMASK_INFINITY_MINUS),  // lt (actually le)
      SystemZ::TDCMASK_NAN,               // un
    }
  };
  // Construct the mask as a combination of the partial masks.
  int Mask = 0;
  if (Pred & CmpInst::FCMP_OEQ)
    Mask |= Masks[WhichConst][0];
  if (Pred & CmpInst::FCMP_OGT)
    Mask |= Masks[WhichConst][1];
  if (Pred & CmpInst::FCMP_OLT)
    Mask |= Masks[WhichConst][2];
  if (Pred & CmpInst::FCMP_UNO)
    Mask |= Masks[WhichConst][3];
  // A lone fcmp is unworthy of tdc conversion on its own, but may become
  // worthy if combined with fabs.
  bool Worthy = false;
  if (CallInst *CI = dyn_cast<CallInst>(Op0)) {
    Function *F = CI->getCalledFunction();
    if (F && F->getIntrinsicID() == Intrinsic::fabs) {
      // Fold with fabs - adjust the mask appropriately.
      Mask &= SystemZ::TDCMASK_PLUS;
      Mask |= Mask >> 1;
      Op0 = CI->getArgOperand(0);
      // A combination of fcmp with fabs is a win, unless the constant
      // involved is 0 (which is handled by later passes).
      Worthy = WhichConst != 0;
      PossibleJunk.insert(CI);
    }
  }
  converted(&I, Op0, Mask, Worthy);
}

void SystemZTDCPass::convertICmp(CmpInst &I) {
  Value *Op0 = I.getOperand(0);
  auto *Const = dyn_cast<ConstantInt>(I.getOperand(1));
  auto Pred = I.getPredicate();
  // All our icmp rules involve comparisons with consts.
  if (!Const)
    return;
  if (auto *Cast = dyn_cast<BitCastInst>(Op0)) {
    // Check for icmp+bitcast used for signbit.
    if (!Cast->getSrcTy()->isFloatTy() &&
        !Cast->getSrcTy()->isDoubleTy() &&
        !Cast->getSrcTy()->isFP128Ty())
      return;
    Value *V = Cast->getOperand(0);
    int Mask;
    if (Pred == CmpInst::ICMP_SLT && Const->isZero()) {
      // icmp slt (bitcast X), 0 - set if sign bit true
      Mask = SystemZ::TDCMASK_MINUS;
    } else if (Pred == CmpInst::ICMP_SGT && Const->isMinusOne()) {
      // icmp sgt (bitcast X), -1 - set if sign bit false
      Mask = SystemZ::TDCMASK_PLUS;
    } else {
      // Not a sign bit check.
      return;
    }
    PossibleJunk.insert(Cast);
    converted(&I, V, Mask, true);
  } else if (auto *CI = dyn_cast<CallInst>(Op0)) {
    // Check if this is a pre-existing call of our tdc intrinsic.
    Function *F = CI->getCalledFunction();
    if (!F || F->getIntrinsicID() != Intrinsic::s390_tdc)
      return;
    if (!Const->isZero())
      return;
    Value *V = CI->getArgOperand(0);
    auto *MaskC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    // Bail if the mask is not a constant.
    if (!MaskC)
      return;
    int Mask = MaskC->getZExtValue();
    Mask &= SystemZ::TDCMASK_ALL;
    if (Pred == CmpInst::ICMP_NE) {
      // icmp ne (call llvm.s390.tdc(...)), 0 -> simple TDC
    } else if (Pred == CmpInst::ICMP_EQ) {
      // icmp eq (call llvm.s390.tdc(...)), 0 -> TDC with inverted mask
      Mask ^= SystemZ::TDCMASK_ALL;
    } else {
      // An unknown comparison - ignore.
      return;
    }
    PossibleJunk.insert(CI);
    converted(&I, V, Mask, false);
  }
}

void SystemZTDCPass::convertLogicOp(BinaryOperator &I) {
  Value *Op0, *Op1;
  int Mask0, Mask1;
  bool Worthy0, Worthy1;
  std::tie(Op0, Mask0, Worthy0) = ConvertedInsts[cast<Instruction>(I.getOperand(0))];
  std::tie(Op1, Mask1, Worthy1) = ConvertedInsts[cast<Instruction>(I.getOperand(1))];
  if (Op0 != Op1)
    return;
  int Mask;
  switch (I.getOpcode()) {
    case Instruction::And:
      Mask = Mask0 & Mask1;
      break;
    case Instruction::Or:
      Mask = Mask0 | Mask1;
      break;
    case Instruction::Xor:
      Mask = Mask0 ^ Mask1;
      break;
    default:
      llvm_unreachable("Unknown op in convertLogicOp");
  }
  converted(&I, Op0, Mask, true);
}

bool SystemZTDCPass::runOnFunction(Function &F) {
  ConvertedInsts.clear();
  LogicOpsWorklist.clear();
  PossibleJunk.clear();

  // Look for icmp+fcmp instructions.
  for (auto &I : instructions(F)) {
    if (I.getOpcode() == Instruction::FCmp)
      convertFCmp(cast<CmpInst>(I));
    else if (I.getOpcode() == Instruction::ICmp)
      convertICmp(cast<CmpInst>(I));
  }

  // If none found, bail already.
  if (ConvertedInsts.empty())
    return false;

  // Process the queue of logic instructions.
  while (!LogicOpsWorklist.empty()) {
    BinaryOperator *Op = LogicOpsWorklist.back();
    LogicOpsWorklist.pop_back();
    // If both operands mapped, and the instruction itself not yet mapped,
    // convert it.
    if (ConvertedInsts.count(dyn_cast<Instruction>(Op->getOperand(0))) &&
        ConvertedInsts.count(dyn_cast<Instruction>(Op->getOperand(1))) &&
        !ConvertedInsts.count(Op))
      convertLogicOp(*Op);
  }

  // Time to actually replace the instructions.  Do it in the reverse order
  // of finding them, since there's a good chance the earlier ones will be
  // unused (due to being folded into later ones).
  Module &M = *F.getParent();
  auto &Ctx = M.getContext();
  Value *Zero32 = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
  bool MadeChange = false;
  for (auto &It : reverse(ConvertedInsts)) {
    Instruction *I = It.first;
    Value *V;
    int Mask;
    bool Worthy;
    std::tie(V, Mask, Worthy) = It.second;
    if (!I->user_empty()) {
      // If used and unworthy of conversion, skip it.
      if (!Worthy)
        continue;
      // Call the intrinsic, compare result with 0.
      Value *TDCFunc = Intrinsic::getDeclaration(&M, Intrinsic::s390_tdc,
                                                 V->getType());
      IRBuilder<> IRB(I);
      Value *MaskVal = ConstantInt::get(Type::getInt64Ty(Ctx), Mask);
      Instruction *TDC = IRB.CreateCall(TDCFunc, {V, MaskVal});
      Value *ICmp = IRB.CreateICmp(CmpInst::ICMP_NE, TDC, Zero32);
      I->replaceAllUsesWith(ICmp);
    }
    // If unused, or used and converted, remove it.
    I->eraseFromParent();
    MadeChange = true;
  }

  if (!MadeChange)
    return false;

  // We've actually done something - now clear misc accumulated junk (fabs,
  // bitcast).
  for (auto *I : PossibleJunk)
    if (I->user_empty())
      I->eraseFromParent();

  return true;
}
