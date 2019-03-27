//===- PHITransAddr.cpp - PHI Translation for Addresses -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the PHITransAddr class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static bool CanPHITrans(Instruction *Inst) {
  if (isa<PHINode>(Inst) ||
      isa<GetElementPtrInst>(Inst))
    return true;

  if (isa<CastInst>(Inst) &&
      isSafeToSpeculativelyExecute(Inst))
    return true;

  if (Inst->getOpcode() == Instruction::Add &&
      isa<ConstantInt>(Inst->getOperand(1)))
    return true;

  //   cerr << "MEMDEP: Could not PHI translate: " << *Pointer;
  //   if (isa<BitCastInst>(PtrInst) || isa<GetElementPtrInst>(PtrInst))
  //     cerr << "OP:\t\t\t\t" << *PtrInst->getOperand(0);
  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void PHITransAddr::dump() const {
  if (!Addr) {
    dbgs() << "PHITransAddr: null\n";
    return;
  }
  dbgs() << "PHITransAddr: " << *Addr << "\n";
  for (unsigned i = 0, e = InstInputs.size(); i != e; ++i)
    dbgs() << "  Input #" << i << " is " << *InstInputs[i] << "\n";
}
#endif


static bool VerifySubExpr(Value *Expr,
                          SmallVectorImpl<Instruction*> &InstInputs) {
  // If this is a non-instruction value, there is nothing to do.
  Instruction *I = dyn_cast<Instruction>(Expr);
  if (!I) return true;

  // If it's an instruction, it is either in Tmp or its operands recursively
  // are.
  SmallVectorImpl<Instruction *>::iterator Entry = find(InstInputs, I);
  if (Entry != InstInputs.end()) {
    InstInputs.erase(Entry);
    return true;
  }

  // If it isn't in the InstInputs list it is a subexpr incorporated into the
  // address.  Sanity check that it is phi translatable.
  if (!CanPHITrans(I)) {
    errs() << "Instruction in PHITransAddr is not phi-translatable:\n";
    errs() << *I << '\n';
    llvm_unreachable("Either something is missing from InstInputs or "
                     "CanPHITrans is wrong.");
  }

  // Validate the operands of the instruction.
  for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i)
    if (!VerifySubExpr(I->getOperand(i), InstInputs))
      return false;

  return true;
}

/// Verify - Check internal consistency of this data structure.  If the
/// structure is valid, it returns true.  If invalid, it prints errors and
/// returns false.
bool PHITransAddr::Verify() const {
  if (!Addr) return true;

  SmallVector<Instruction*, 8> Tmp(InstInputs.begin(), InstInputs.end());

  if (!VerifySubExpr(Addr, Tmp))
    return false;

  if (!Tmp.empty()) {
    errs() << "PHITransAddr contains extra instructions:\n";
    for (unsigned i = 0, e = InstInputs.size(); i != e; ++i)
      errs() << "  InstInput #" << i << " is " << *InstInputs[i] << "\n";
    llvm_unreachable("This is unexpected.");
  }

  // a-ok.
  return true;
}


/// IsPotentiallyPHITranslatable - If this needs PHI translation, return true
/// if we have some hope of doing it.  This should be used as a filter to
/// avoid calling PHITranslateValue in hopeless situations.
bool PHITransAddr::IsPotentiallyPHITranslatable() const {
  // If the input value is not an instruction, or if it is not defined in CurBB,
  // then we don't need to phi translate it.
  Instruction *Inst = dyn_cast<Instruction>(Addr);
  return !Inst || CanPHITrans(Inst);
}


static void RemoveInstInputs(Value *V,
                             SmallVectorImpl<Instruction*> &InstInputs) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return;

  // If the instruction is in the InstInputs list, remove it.
  SmallVectorImpl<Instruction *>::iterator Entry = find(InstInputs, I);
  if (Entry != InstInputs.end()) {
    InstInputs.erase(Entry);
    return;
  }

  assert(!isa<PHINode>(I) && "Error, removing something that isn't an input");

  // Otherwise, it must have instruction inputs itself.  Zap them recursively.
  for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
    if (Instruction *Op = dyn_cast<Instruction>(I->getOperand(i)))
      RemoveInstInputs(Op, InstInputs);
  }
}

Value *PHITransAddr::PHITranslateSubExpr(Value *V, BasicBlock *CurBB,
                                         BasicBlock *PredBB,
                                         const DominatorTree *DT) {
  // If this is a non-instruction value, it can't require PHI translation.
  Instruction *Inst = dyn_cast<Instruction>(V);
  if (!Inst) return V;

  // Determine whether 'Inst' is an input to our PHI translatable expression.
  bool isInput = is_contained(InstInputs, Inst);

  // Handle inputs instructions if needed.
  if (isInput) {
    if (Inst->getParent() != CurBB) {
      // If it is an input defined in a different block, then it remains an
      // input.
      return Inst;
    }

    // If 'Inst' is defined in this block and is an input that needs to be phi
    // translated, we need to incorporate the value into the expression or fail.

    // In either case, the instruction itself isn't an input any longer.
    InstInputs.erase(find(InstInputs, Inst));

    // If this is a PHI, go ahead and translate it.
    if (PHINode *PN = dyn_cast<PHINode>(Inst))
      return AddAsInput(PN->getIncomingValueForBlock(PredBB));

    // If this is a non-phi value, and it is analyzable, we can incorporate it
    // into the expression by making all instruction operands be inputs.
    if (!CanPHITrans(Inst))
      return nullptr;

    // All instruction operands are now inputs (and of course, they may also be
    // defined in this block, so they may need to be phi translated themselves.
    for (unsigned i = 0, e = Inst->getNumOperands(); i != e; ++i)
      if (Instruction *Op = dyn_cast<Instruction>(Inst->getOperand(i)))
        InstInputs.push_back(Op);
  }

  // Ok, it must be an intermediate result (either because it started that way
  // or because we just incorporated it into the expression).  See if its
  // operands need to be phi translated, and if so, reconstruct it.

  if (CastInst *Cast = dyn_cast<CastInst>(Inst)) {
    if (!isSafeToSpeculativelyExecute(Cast)) return nullptr;
    Value *PHIIn = PHITranslateSubExpr(Cast->getOperand(0), CurBB, PredBB, DT);
    if (!PHIIn) return nullptr;
    if (PHIIn == Cast->getOperand(0))
      return Cast;

    // Find an available version of this cast.

    // Constants are trivial to find.
    if (Constant *C = dyn_cast<Constant>(PHIIn))
      return AddAsInput(ConstantExpr::getCast(Cast->getOpcode(),
                                              C, Cast->getType()));

    // Otherwise we have to see if a casted version of the incoming pointer
    // is available.  If so, we can use it, otherwise we have to fail.
    for (User *U : PHIIn->users()) {
      if (CastInst *CastI = dyn_cast<CastInst>(U))
        if (CastI->getOpcode() == Cast->getOpcode() &&
            CastI->getType() == Cast->getType() &&
            (!DT || DT->dominates(CastI->getParent(), PredBB)))
          return CastI;
    }
    return nullptr;
  }

  // Handle getelementptr with at least one PHI translatable operand.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
    SmallVector<Value*, 8> GEPOps;
    bool AnyChanged = false;
    for (unsigned i = 0, e = GEP->getNumOperands(); i != e; ++i) {
      Value *GEPOp = PHITranslateSubExpr(GEP->getOperand(i), CurBB, PredBB, DT);
      if (!GEPOp) return nullptr;

      AnyChanged |= GEPOp != GEP->getOperand(i);
      GEPOps.push_back(GEPOp);
    }

    if (!AnyChanged)
      return GEP;

    // Simplify the GEP to handle 'gep x, 0' -> x etc.
    if (Value *V = SimplifyGEPInst(GEP->getSourceElementType(),
                                   GEPOps, {DL, TLI, DT, AC})) {
      for (unsigned i = 0, e = GEPOps.size(); i != e; ++i)
        RemoveInstInputs(GEPOps[i], InstInputs);

      return AddAsInput(V);
    }

    // Scan to see if we have this GEP available.
    Value *APHIOp = GEPOps[0];
    for (User *U : APHIOp->users()) {
      if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U))
        if (GEPI->getType() == GEP->getType() &&
            GEPI->getNumOperands() == GEPOps.size() &&
            GEPI->getParent()->getParent() == CurBB->getParent() &&
            (!DT || DT->dominates(GEPI->getParent(), PredBB))) {
          if (std::equal(GEPOps.begin(), GEPOps.end(), GEPI->op_begin()))
            return GEPI;
        }
    }
    return nullptr;
  }

  // Handle add with a constant RHS.
  if (Inst->getOpcode() == Instruction::Add &&
      isa<ConstantInt>(Inst->getOperand(1))) {
    // PHI translate the LHS.
    Constant *RHS = cast<ConstantInt>(Inst->getOperand(1));
    bool isNSW = cast<BinaryOperator>(Inst)->hasNoSignedWrap();
    bool isNUW = cast<BinaryOperator>(Inst)->hasNoUnsignedWrap();

    Value *LHS = PHITranslateSubExpr(Inst->getOperand(0), CurBB, PredBB, DT);
    if (!LHS) return nullptr;

    // If the PHI translated LHS is an add of a constant, fold the immediates.
    if (BinaryOperator *BOp = dyn_cast<BinaryOperator>(LHS))
      if (BOp->getOpcode() == Instruction::Add)
        if (ConstantInt *CI = dyn_cast<ConstantInt>(BOp->getOperand(1))) {
          LHS = BOp->getOperand(0);
          RHS = ConstantExpr::getAdd(RHS, CI);
          isNSW = isNUW = false;

          // If the old 'LHS' was an input, add the new 'LHS' as an input.
          if (is_contained(InstInputs, BOp)) {
            RemoveInstInputs(BOp, InstInputs);
            AddAsInput(LHS);
          }
        }

    // See if the add simplifies away.
    if (Value *Res = SimplifyAddInst(LHS, RHS, isNSW, isNUW, {DL, TLI, DT, AC})) {
      // If we simplified the operands, the LHS is no longer an input, but Res
      // is.
      RemoveInstInputs(LHS, InstInputs);
      return AddAsInput(Res);
    }

    // If we didn't modify the add, just return it.
    if (LHS == Inst->getOperand(0) && RHS == Inst->getOperand(1))
      return Inst;

    // Otherwise, see if we have this add available somewhere.
    for (User *U : LHS->users()) {
      if (BinaryOperator *BO = dyn_cast<BinaryOperator>(U))
        if (BO->getOpcode() == Instruction::Add &&
            BO->getOperand(0) == LHS && BO->getOperand(1) == RHS &&
            BO->getParent()->getParent() == CurBB->getParent() &&
            (!DT || DT->dominates(BO->getParent(), PredBB)))
          return BO;
    }

    return nullptr;
  }

  // Otherwise, we failed.
  return nullptr;
}


/// PHITranslateValue - PHI translate the current address up the CFG from
/// CurBB to Pred, updating our state to reflect any needed changes.  If
/// 'MustDominate' is true, the translated value must dominate
/// PredBB.  This returns true on failure and sets Addr to null.
bool PHITransAddr::PHITranslateValue(BasicBlock *CurBB, BasicBlock *PredBB,
                                     const DominatorTree *DT,
                                     bool MustDominate) {
  assert(DT || !MustDominate);
  assert(Verify() && "Invalid PHITransAddr!");
  if (DT && DT->isReachableFromEntry(PredBB))
    Addr =
        PHITranslateSubExpr(Addr, CurBB, PredBB, MustDominate ? DT : nullptr);
  else
    Addr = nullptr;
  assert(Verify() && "Invalid PHITransAddr!");

  if (MustDominate)
    // Make sure the value is live in the predecessor.
    if (Instruction *Inst = dyn_cast_or_null<Instruction>(Addr))
      if (!DT->dominates(Inst->getParent(), PredBB))
        Addr = nullptr;

  return Addr == nullptr;
}

/// PHITranslateWithInsertion - PHI translate this value into the specified
/// predecessor block, inserting a computation of the value if it is
/// unavailable.
///
/// All newly created instructions are added to the NewInsts list.  This
/// returns null on failure.
///
Value *PHITransAddr::
PHITranslateWithInsertion(BasicBlock *CurBB, BasicBlock *PredBB,
                          const DominatorTree &DT,
                          SmallVectorImpl<Instruction*> &NewInsts) {
  unsigned NISize = NewInsts.size();

  // Attempt to PHI translate with insertion.
  Addr = InsertPHITranslatedSubExpr(Addr, CurBB, PredBB, DT, NewInsts);

  // If successful, return the new value.
  if (Addr) return Addr;

  // If not, destroy any intermediate instructions inserted.
  while (NewInsts.size() != NISize)
    NewInsts.pop_back_val()->eraseFromParent();
  return nullptr;
}


/// InsertPHITranslatedPointer - Insert a computation of the PHI translated
/// version of 'V' for the edge PredBB->CurBB into the end of the PredBB
/// block.  All newly created instructions are added to the NewInsts list.
/// This returns null on failure.
///
Value *PHITransAddr::
InsertPHITranslatedSubExpr(Value *InVal, BasicBlock *CurBB,
                           BasicBlock *PredBB, const DominatorTree &DT,
                           SmallVectorImpl<Instruction*> &NewInsts) {
  // See if we have a version of this value already available and dominating
  // PredBB.  If so, there is no need to insert a new instance of it.
  PHITransAddr Tmp(InVal, DL, AC);
  if (!Tmp.PHITranslateValue(CurBB, PredBB, &DT, /*MustDominate=*/true))
    return Tmp.getAddr();

  // We don't need to PHI translate values which aren't instructions.
  auto *Inst = dyn_cast<Instruction>(InVal);
  if (!Inst)
    return nullptr;

  // Handle cast of PHI translatable value.
  if (CastInst *Cast = dyn_cast<CastInst>(Inst)) {
    if (!isSafeToSpeculativelyExecute(Cast)) return nullptr;
    Value *OpVal = InsertPHITranslatedSubExpr(Cast->getOperand(0),
                                              CurBB, PredBB, DT, NewInsts);
    if (!OpVal) return nullptr;

    // Otherwise insert a cast at the end of PredBB.
    CastInst *New = CastInst::Create(Cast->getOpcode(), OpVal, InVal->getType(),
                                     InVal->getName() + ".phi.trans.insert",
                                     PredBB->getTerminator());
    New->setDebugLoc(Inst->getDebugLoc());
    NewInsts.push_back(New);
    return New;
  }

  // Handle getelementptr with at least one PHI operand.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Inst)) {
    SmallVector<Value*, 8> GEPOps;
    BasicBlock *CurBB = GEP->getParent();
    for (unsigned i = 0, e = GEP->getNumOperands(); i != e; ++i) {
      Value *OpVal = InsertPHITranslatedSubExpr(GEP->getOperand(i),
                                                CurBB, PredBB, DT, NewInsts);
      if (!OpVal) return nullptr;
      GEPOps.push_back(OpVal);
    }

    GetElementPtrInst *Result = GetElementPtrInst::Create(
        GEP->getSourceElementType(), GEPOps[0], makeArrayRef(GEPOps).slice(1),
        InVal->getName() + ".phi.trans.insert", PredBB->getTerminator());
    Result->setDebugLoc(Inst->getDebugLoc());
    Result->setIsInBounds(GEP->isInBounds());
    NewInsts.push_back(Result);
    return Result;
  }

#if 0
  // FIXME: This code works, but it is unclear that we actually want to insert
  // a big chain of computation in order to make a value available in a block.
  // This needs to be evaluated carefully to consider its cost trade offs.

  // Handle add with a constant RHS.
  if (Inst->getOpcode() == Instruction::Add &&
      isa<ConstantInt>(Inst->getOperand(1))) {
    // PHI translate the LHS.
    Value *OpVal = InsertPHITranslatedSubExpr(Inst->getOperand(0),
                                              CurBB, PredBB, DT, NewInsts);
    if (OpVal == 0) return 0;

    BinaryOperator *Res = BinaryOperator::CreateAdd(OpVal, Inst->getOperand(1),
                                           InVal->getName()+".phi.trans.insert",
                                                    PredBB->getTerminator());
    Res->setHasNoSignedWrap(cast<BinaryOperator>(Inst)->hasNoSignedWrap());
    Res->setHasNoUnsignedWrap(cast<BinaryOperator>(Inst)->hasNoUnsignedWrap());
    NewInsts.push_back(Res);
    return Res;
  }
#endif

  return nullptr;
}
