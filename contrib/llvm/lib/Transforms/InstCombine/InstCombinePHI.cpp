//===- InstCombinePHI.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitPHINode function.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/PatternMatch.h"
using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "instcombine"

static cl::opt<unsigned>
MaxNumPhis("instcombine-max-num-phis", cl::init(512),
           cl::desc("Maximum number phis to handle in intptr/ptrint folding"));

/// The PHI arguments will be folded into a single operation with a PHI node
/// as input. The debug location of the single operation will be the merged
/// locations of the original PHI node arguments.
void InstCombiner::PHIArgMergedDebugLoc(Instruction *Inst, PHINode &PN) {
  auto *FirstInst = cast<Instruction>(PN.getIncomingValue(0));
  Inst->setDebugLoc(FirstInst->getDebugLoc());
  // We do not expect a CallInst here, otherwise, N-way merging of DebugLoc
  // will be inefficient.
  assert(!isa<CallInst>(Inst));

  for (unsigned i = 1; i != PN.getNumIncomingValues(); ++i) {
    auto *I = cast<Instruction>(PN.getIncomingValue(i));
    Inst->applyMergedLocation(Inst->getDebugLoc(), I->getDebugLoc());
  }
}

// Replace Integer typed PHI PN if the PHI's value is used as a pointer value.
// If there is an existing pointer typed PHI that produces the same value as PN,
// replace PN and the IntToPtr operation with it. Otherwise, synthesize a new
// PHI node:
//
// Case-1:
// bb1:
//     int_init = PtrToInt(ptr_init)
//     br label %bb2
// bb2:
//    int_val = PHI([int_init, %bb1], [int_val_inc, %bb2]
//    ptr_val = PHI([ptr_init, %bb1], [ptr_val_inc, %bb2]
//    ptr_val2 = IntToPtr(int_val)
//    ...
//    use(ptr_val2)
//    ptr_val_inc = ...
//    inc_val_inc = PtrToInt(ptr_val_inc)
//
// ==>
// bb1:
//     br label %bb2
// bb2:
//    ptr_val = PHI([ptr_init, %bb1], [ptr_val_inc, %bb2]
//    ...
//    use(ptr_val)
//    ptr_val_inc = ...
//
// Case-2:
// bb1:
//    int_ptr = BitCast(ptr_ptr)
//    int_init = Load(int_ptr)
//    br label %bb2
// bb2:
//    int_val = PHI([int_init, %bb1], [int_val_inc, %bb2]
//    ptr_val2 = IntToPtr(int_val)
//    ...
//    use(ptr_val2)
//    ptr_val_inc = ...
//    inc_val_inc = PtrToInt(ptr_val_inc)
// ==>
// bb1:
//    ptr_init = Load(ptr_ptr)
//    br label %bb2
// bb2:
//    ptr_val = PHI([ptr_init, %bb1], [ptr_val_inc, %bb2]
//    ...
//    use(ptr_val)
//    ptr_val_inc = ...
//    ...
//
Instruction *InstCombiner::FoldIntegerTypedPHI(PHINode &PN) {
  if (!PN.getType()->isIntegerTy())
    return nullptr;
  if (!PN.hasOneUse())
    return nullptr;

  auto *IntToPtr = dyn_cast<IntToPtrInst>(PN.user_back());
  if (!IntToPtr)
    return nullptr;

  // Check if the pointer is actually used as pointer:
  auto HasPointerUse = [](Instruction *IIP) {
    for (User *U : IIP->users()) {
      Value *Ptr = nullptr;
      if (LoadInst *LoadI = dyn_cast<LoadInst>(U)) {
        Ptr = LoadI->getPointerOperand();
      } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        Ptr = SI->getPointerOperand();
      } else if (GetElementPtrInst *GI = dyn_cast<GetElementPtrInst>(U)) {
        Ptr = GI->getPointerOperand();
      }

      if (Ptr && Ptr == IIP)
        return true;
    }
    return false;
  };

  if (!HasPointerUse(IntToPtr))
    return nullptr;

  if (DL.getPointerSizeInBits(IntToPtr->getAddressSpace()) !=
      DL.getTypeSizeInBits(IntToPtr->getOperand(0)->getType()))
    return nullptr;

  SmallVector<Value *, 4> AvailablePtrVals;
  for (unsigned i = 0; i != PN.getNumIncomingValues(); ++i) {
    Value *Arg = PN.getIncomingValue(i);

    // First look backward:
    if (auto *PI = dyn_cast<PtrToIntInst>(Arg)) {
      AvailablePtrVals.emplace_back(PI->getOperand(0));
      continue;
    }

    // Next look forward:
    Value *ArgIntToPtr = nullptr;
    for (User *U : Arg->users()) {
      if (isa<IntToPtrInst>(U) && U->getType() == IntToPtr->getType() &&
          (DT.dominates(cast<Instruction>(U), PN.getIncomingBlock(i)) ||
           cast<Instruction>(U)->getParent() == PN.getIncomingBlock(i))) {
        ArgIntToPtr = U;
        break;
      }
    }

    if (ArgIntToPtr) {
      AvailablePtrVals.emplace_back(ArgIntToPtr);
      continue;
    }

    // If Arg is defined by a PHI, allow it. This will also create
    // more opportunities iteratively.
    if (isa<PHINode>(Arg)) {
      AvailablePtrVals.emplace_back(Arg);
      continue;
    }

    // For a single use integer load:
    auto *LoadI = dyn_cast<LoadInst>(Arg);
    if (!LoadI)
      return nullptr;

    if (!LoadI->hasOneUse())
      return nullptr;

    // Push the integer typed Load instruction into the available
    // value set, and fix it up later when the pointer typed PHI
    // is synthesized.
    AvailablePtrVals.emplace_back(LoadI);
  }

  // Now search for a matching PHI
  auto *BB = PN.getParent();
  assert(AvailablePtrVals.size() == PN.getNumIncomingValues() &&
         "Not enough available ptr typed incoming values");
  PHINode *MatchingPtrPHI = nullptr;
  unsigned NumPhis = 0;
  for (auto II = BB->begin(), EI = BasicBlock::iterator(BB->getFirstNonPHI());
       II != EI; II++, NumPhis++) {
    // FIXME: consider handling this in AggressiveInstCombine
    if (NumPhis > MaxNumPhis)
      return nullptr;
    PHINode *PtrPHI = dyn_cast<PHINode>(II);
    if (!PtrPHI || PtrPHI == &PN || PtrPHI->getType() != IntToPtr->getType())
      continue;
    MatchingPtrPHI = PtrPHI;
    for (unsigned i = 0; i != PtrPHI->getNumIncomingValues(); ++i) {
      if (AvailablePtrVals[i] !=
          PtrPHI->getIncomingValueForBlock(PN.getIncomingBlock(i))) {
        MatchingPtrPHI = nullptr;
        break;
      }
    }

    if (MatchingPtrPHI)
      break;
  }

  if (MatchingPtrPHI) {
    assert(MatchingPtrPHI->getType() == IntToPtr->getType() &&
           "Phi's Type does not match with IntToPtr");
    // The PtrToCast + IntToPtr will be simplified later
    return CastInst::CreateBitOrPointerCast(MatchingPtrPHI,
                                            IntToPtr->getOperand(0)->getType());
  }

  // If it requires a conversion for every PHI operand, do not do it.
  if (all_of(AvailablePtrVals, [&](Value *V) {
        return (V->getType() != IntToPtr->getType()) || isa<IntToPtrInst>(V);
      }))
    return nullptr;

  // If any of the operand that requires casting is a terminator
  // instruction, do not do it.
  if (any_of(AvailablePtrVals, [&](Value *V) {
        if (V->getType() == IntToPtr->getType())
          return false;

        auto *Inst = dyn_cast<Instruction>(V);
        return Inst && Inst->isTerminator();
      }))
    return nullptr;

  PHINode *NewPtrPHI = PHINode::Create(
      IntToPtr->getType(), PN.getNumIncomingValues(), PN.getName() + ".ptr");

  InsertNewInstBefore(NewPtrPHI, PN);
  SmallDenseMap<Value *, Instruction *> Casts;
  for (unsigned i = 0; i != PN.getNumIncomingValues(); ++i) {
    auto *IncomingBB = PN.getIncomingBlock(i);
    auto *IncomingVal = AvailablePtrVals[i];

    if (IncomingVal->getType() == IntToPtr->getType()) {
      NewPtrPHI->addIncoming(IncomingVal, IncomingBB);
      continue;
    }

#ifndef NDEBUG
    LoadInst *LoadI = dyn_cast<LoadInst>(IncomingVal);
    assert((isa<PHINode>(IncomingVal) ||
            IncomingVal->getType()->isPointerTy() ||
            (LoadI && LoadI->hasOneUse())) &&
           "Can not replace LoadInst with multiple uses");
#endif
    // Need to insert a BitCast.
    // For an integer Load instruction with a single use, the load + IntToPtr
    // cast will be simplified into a pointer load:
    // %v = load i64, i64* %a.ip, align 8
    // %v.cast = inttoptr i64 %v to float **
    // ==>
    // %v.ptrp = bitcast i64 * %a.ip to float **
    // %v.cast = load float *, float ** %v.ptrp, align 8
    Instruction *&CI = Casts[IncomingVal];
    if (!CI) {
      CI = CastInst::CreateBitOrPointerCast(IncomingVal, IntToPtr->getType(),
                                            IncomingVal->getName() + ".ptr");
      if (auto *IncomingI = dyn_cast<Instruction>(IncomingVal)) {
        BasicBlock::iterator InsertPos(IncomingI);
        InsertPos++;
        if (isa<PHINode>(IncomingI))
          InsertPos = IncomingI->getParent()->getFirstInsertionPt();
        InsertNewInstBefore(CI, *InsertPos);
      } else {
        auto *InsertBB = &IncomingBB->getParent()->getEntryBlock();
        InsertNewInstBefore(CI, *InsertBB->getFirstInsertionPt());
      }
    }
    NewPtrPHI->addIncoming(CI, IncomingBB);
  }

  // The PtrToCast + IntToPtr will be simplified later
  return CastInst::CreateBitOrPointerCast(NewPtrPHI,
                                          IntToPtr->getOperand(0)->getType());
}

/// If we have something like phi [add (a,b), add(a,c)] and if a/b/c and the
/// adds all have a single use, turn this into a phi and a single binop.
Instruction *InstCombiner::FoldPHIArgBinOpIntoPHI(PHINode &PN) {
  Instruction *FirstInst = cast<Instruction>(PN.getIncomingValue(0));
  assert(isa<BinaryOperator>(FirstInst) || isa<CmpInst>(FirstInst));
  unsigned Opc = FirstInst->getOpcode();
  Value *LHSVal = FirstInst->getOperand(0);
  Value *RHSVal = FirstInst->getOperand(1);

  Type *LHSType = LHSVal->getType();
  Type *RHSType = RHSVal->getType();

  // Scan to see if all operands are the same opcode, and all have one use.
  for (unsigned i = 1; i != PN.getNumIncomingValues(); ++i) {
    Instruction *I = dyn_cast<Instruction>(PN.getIncomingValue(i));
    if (!I || I->getOpcode() != Opc || !I->hasOneUse() ||
        // Verify type of the LHS matches so we don't fold cmp's of different
        // types.
        I->getOperand(0)->getType() != LHSType ||
        I->getOperand(1)->getType() != RHSType)
      return nullptr;

    // If they are CmpInst instructions, check their predicates
    if (CmpInst *CI = dyn_cast<CmpInst>(I))
      if (CI->getPredicate() != cast<CmpInst>(FirstInst)->getPredicate())
        return nullptr;

    // Keep track of which operand needs a phi node.
    if (I->getOperand(0) != LHSVal) LHSVal = nullptr;
    if (I->getOperand(1) != RHSVal) RHSVal = nullptr;
  }

  // If both LHS and RHS would need a PHI, don't do this transformation,
  // because it would increase the number of PHIs entering the block,
  // which leads to higher register pressure. This is especially
  // bad when the PHIs are in the header of a loop.
  if (!LHSVal && !RHSVal)
    return nullptr;

  // Otherwise, this is safe to transform!

  Value *InLHS = FirstInst->getOperand(0);
  Value *InRHS = FirstInst->getOperand(1);
  PHINode *NewLHS = nullptr, *NewRHS = nullptr;
  if (!LHSVal) {
    NewLHS = PHINode::Create(LHSType, PN.getNumIncomingValues(),
                             FirstInst->getOperand(0)->getName() + ".pn");
    NewLHS->addIncoming(InLHS, PN.getIncomingBlock(0));
    InsertNewInstBefore(NewLHS, PN);
    LHSVal = NewLHS;
  }

  if (!RHSVal) {
    NewRHS = PHINode::Create(RHSType, PN.getNumIncomingValues(),
                             FirstInst->getOperand(1)->getName() + ".pn");
    NewRHS->addIncoming(InRHS, PN.getIncomingBlock(0));
    InsertNewInstBefore(NewRHS, PN);
    RHSVal = NewRHS;
  }

  // Add all operands to the new PHIs.
  if (NewLHS || NewRHS) {
    for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i) {
      Instruction *InInst = cast<Instruction>(PN.getIncomingValue(i));
      if (NewLHS) {
        Value *NewInLHS = InInst->getOperand(0);
        NewLHS->addIncoming(NewInLHS, PN.getIncomingBlock(i));
      }
      if (NewRHS) {
        Value *NewInRHS = InInst->getOperand(1);
        NewRHS->addIncoming(NewInRHS, PN.getIncomingBlock(i));
      }
    }
  }

  if (CmpInst *CIOp = dyn_cast<CmpInst>(FirstInst)) {
    CmpInst *NewCI = CmpInst::Create(CIOp->getOpcode(), CIOp->getPredicate(),
                                     LHSVal, RHSVal);
    PHIArgMergedDebugLoc(NewCI, PN);
    return NewCI;
  }

  BinaryOperator *BinOp = cast<BinaryOperator>(FirstInst);
  BinaryOperator *NewBinOp =
    BinaryOperator::Create(BinOp->getOpcode(), LHSVal, RHSVal);

  NewBinOp->copyIRFlags(PN.getIncomingValue(0));

  for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i)
    NewBinOp->andIRFlags(PN.getIncomingValue(i));

  PHIArgMergedDebugLoc(NewBinOp, PN);
  return NewBinOp;
}

Instruction *InstCombiner::FoldPHIArgGEPIntoPHI(PHINode &PN) {
  GetElementPtrInst *FirstInst =cast<GetElementPtrInst>(PN.getIncomingValue(0));

  SmallVector<Value*, 16> FixedOperands(FirstInst->op_begin(),
                                        FirstInst->op_end());
  // This is true if all GEP bases are allocas and if all indices into them are
  // constants.
  bool AllBasePointersAreAllocas = true;

  // We don't want to replace this phi if the replacement would require
  // more than one phi, which leads to higher register pressure. This is
  // especially bad when the PHIs are in the header of a loop.
  bool NeededPhi = false;

  bool AllInBounds = true;

  // Scan to see if all operands are the same opcode, and all have one use.
  for (unsigned i = 1; i != PN.getNumIncomingValues(); ++i) {
    GetElementPtrInst *GEP= dyn_cast<GetElementPtrInst>(PN.getIncomingValue(i));
    if (!GEP || !GEP->hasOneUse() || GEP->getType() != FirstInst->getType() ||
      GEP->getNumOperands() != FirstInst->getNumOperands())
      return nullptr;

    AllInBounds &= GEP->isInBounds();

    // Keep track of whether or not all GEPs are of alloca pointers.
    if (AllBasePointersAreAllocas &&
        (!isa<AllocaInst>(GEP->getOperand(0)) ||
         !GEP->hasAllConstantIndices()))
      AllBasePointersAreAllocas = false;

    // Compare the operand lists.
    for (unsigned op = 0, e = FirstInst->getNumOperands(); op != e; ++op) {
      if (FirstInst->getOperand(op) == GEP->getOperand(op))
        continue;

      // Don't merge two GEPs when two operands differ (introducing phi nodes)
      // if one of the PHIs has a constant for the index.  The index may be
      // substantially cheaper to compute for the constants, so making it a
      // variable index could pessimize the path.  This also handles the case
      // for struct indices, which must always be constant.
      if (isa<ConstantInt>(FirstInst->getOperand(op)) ||
          isa<ConstantInt>(GEP->getOperand(op)))
        return nullptr;

      if (FirstInst->getOperand(op)->getType() !=GEP->getOperand(op)->getType())
        return nullptr;

      // If we already needed a PHI for an earlier operand, and another operand
      // also requires a PHI, we'd be introducing more PHIs than we're
      // eliminating, which increases register pressure on entry to the PHI's
      // block.
      if (NeededPhi)
        return nullptr;

      FixedOperands[op] = nullptr;  // Needs a PHI.
      NeededPhi = true;
    }
  }

  // If all of the base pointers of the PHI'd GEPs are from allocas, don't
  // bother doing this transformation.  At best, this will just save a bit of
  // offset calculation, but all the predecessors will have to materialize the
  // stack address into a register anyway.  We'd actually rather *clone* the
  // load up into the predecessors so that we have a load of a gep of an alloca,
  // which can usually all be folded into the load.
  if (AllBasePointersAreAllocas)
    return nullptr;

  // Otherwise, this is safe to transform.  Insert PHI nodes for each operand
  // that is variable.
  SmallVector<PHINode*, 16> OperandPhis(FixedOperands.size());

  bool HasAnyPHIs = false;
  for (unsigned i = 0, e = FixedOperands.size(); i != e; ++i) {
    if (FixedOperands[i]) continue;  // operand doesn't need a phi.
    Value *FirstOp = FirstInst->getOperand(i);
    PHINode *NewPN = PHINode::Create(FirstOp->getType(), e,
                                     FirstOp->getName()+".pn");
    InsertNewInstBefore(NewPN, PN);

    NewPN->addIncoming(FirstOp, PN.getIncomingBlock(0));
    OperandPhis[i] = NewPN;
    FixedOperands[i] = NewPN;
    HasAnyPHIs = true;
  }


  // Add all operands to the new PHIs.
  if (HasAnyPHIs) {
    for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i) {
      GetElementPtrInst *InGEP =cast<GetElementPtrInst>(PN.getIncomingValue(i));
      BasicBlock *InBB = PN.getIncomingBlock(i);

      for (unsigned op = 0, e = OperandPhis.size(); op != e; ++op)
        if (PHINode *OpPhi = OperandPhis[op])
          OpPhi->addIncoming(InGEP->getOperand(op), InBB);
    }
  }

  Value *Base = FixedOperands[0];
  GetElementPtrInst *NewGEP =
      GetElementPtrInst::Create(FirstInst->getSourceElementType(), Base,
                                makeArrayRef(FixedOperands).slice(1));
  if (AllInBounds) NewGEP->setIsInBounds();
  PHIArgMergedDebugLoc(NewGEP, PN);
  return NewGEP;
}


/// Return true if we know that it is safe to sink the load out of the block
/// that defines it. This means that it must be obvious the value of the load is
/// not changed from the point of the load to the end of the block it is in.
///
/// Finally, it is safe, but not profitable, to sink a load targeting a
/// non-address-taken alloca.  Doing so will cause us to not promote the alloca
/// to a register.
static bool isSafeAndProfitableToSinkLoad(LoadInst *L) {
  BasicBlock::iterator BBI = L->getIterator(), E = L->getParent()->end();

  for (++BBI; BBI != E; ++BBI)
    if (BBI->mayWriteToMemory())
      return false;

  // Check for non-address taken alloca.  If not address-taken already, it isn't
  // profitable to do this xform.
  if (AllocaInst *AI = dyn_cast<AllocaInst>(L->getOperand(0))) {
    bool isAddressTaken = false;
    for (User *U : AI->users()) {
      if (isa<LoadInst>(U)) continue;
      if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        // If storing TO the alloca, then the address isn't taken.
        if (SI->getOperand(1) == AI) continue;
      }
      isAddressTaken = true;
      break;
    }

    if (!isAddressTaken && AI->isStaticAlloca())
      return false;
  }

  // If this load is a load from a GEP with a constant offset from an alloca,
  // then we don't want to sink it.  In its present form, it will be
  // load [constant stack offset].  Sinking it will cause us to have to
  // materialize the stack addresses in each predecessor in a register only to
  // do a shared load from register in the successor.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(L->getOperand(0)))
    if (AllocaInst *AI = dyn_cast<AllocaInst>(GEP->getOperand(0)))
      if (AI->isStaticAlloca() && GEP->hasAllConstantIndices())
        return false;

  return true;
}

Instruction *InstCombiner::FoldPHIArgLoadIntoPHI(PHINode &PN) {
  LoadInst *FirstLI = cast<LoadInst>(PN.getIncomingValue(0));

  // FIXME: This is overconservative; this transform is allowed in some cases
  // for atomic operations.
  if (FirstLI->isAtomic())
    return nullptr;

  // When processing loads, we need to propagate two bits of information to the
  // sunk load: whether it is volatile, and what its alignment is.  We currently
  // don't sink loads when some have their alignment specified and some don't.
  // visitLoadInst will propagate an alignment onto the load when TD is around,
  // and if TD isn't around, we can't handle the mixed case.
  bool isVolatile = FirstLI->isVolatile();
  unsigned LoadAlignment = FirstLI->getAlignment();
  unsigned LoadAddrSpace = FirstLI->getPointerAddressSpace();

  // We can't sink the load if the loaded value could be modified between the
  // load and the PHI.
  if (FirstLI->getParent() != PN.getIncomingBlock(0) ||
      !isSafeAndProfitableToSinkLoad(FirstLI))
    return nullptr;

  // If the PHI is of volatile loads and the load block has multiple
  // successors, sinking it would remove a load of the volatile value from
  // the path through the other successor.
  if (isVolatile &&
      FirstLI->getParent()->getTerminator()->getNumSuccessors() != 1)
    return nullptr;

  // Check to see if all arguments are the same operation.
  for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i) {
    LoadInst *LI = dyn_cast<LoadInst>(PN.getIncomingValue(i));
    if (!LI || !LI->hasOneUse())
      return nullptr;

    // We can't sink the load if the loaded value could be modified between
    // the load and the PHI.
    if (LI->isVolatile() != isVolatile ||
        LI->getParent() != PN.getIncomingBlock(i) ||
        LI->getPointerAddressSpace() != LoadAddrSpace ||
        !isSafeAndProfitableToSinkLoad(LI))
      return nullptr;

    // If some of the loads have an alignment specified but not all of them,
    // we can't do the transformation.
    if ((LoadAlignment != 0) != (LI->getAlignment() != 0))
      return nullptr;

    LoadAlignment = std::min(LoadAlignment, LI->getAlignment());

    // If the PHI is of volatile loads and the load block has multiple
    // successors, sinking it would remove a load of the volatile value from
    // the path through the other successor.
    if (isVolatile &&
        LI->getParent()->getTerminator()->getNumSuccessors() != 1)
      return nullptr;
  }

  // Okay, they are all the same operation.  Create a new PHI node of the
  // correct type, and PHI together all of the LHS's of the instructions.
  PHINode *NewPN = PHINode::Create(FirstLI->getOperand(0)->getType(),
                                   PN.getNumIncomingValues(),
                                   PN.getName()+".in");

  Value *InVal = FirstLI->getOperand(0);
  NewPN->addIncoming(InVal, PN.getIncomingBlock(0));
  LoadInst *NewLI = new LoadInst(NewPN, "", isVolatile, LoadAlignment);

  unsigned KnownIDs[] = {
    LLVMContext::MD_tbaa,
    LLVMContext::MD_range,
    LLVMContext::MD_invariant_load,
    LLVMContext::MD_alias_scope,
    LLVMContext::MD_noalias,
    LLVMContext::MD_nonnull,
    LLVMContext::MD_align,
    LLVMContext::MD_dereferenceable,
    LLVMContext::MD_dereferenceable_or_null,
    LLVMContext::MD_access_group,
  };

  for (unsigned ID : KnownIDs)
    NewLI->setMetadata(ID, FirstLI->getMetadata(ID));

  // Add all operands to the new PHI and combine TBAA metadata.
  for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i) {
    LoadInst *LI = cast<LoadInst>(PN.getIncomingValue(i));
    combineMetadata(NewLI, LI, KnownIDs, true);
    Value *NewInVal = LI->getOperand(0);
    if (NewInVal != InVal)
      InVal = nullptr;
    NewPN->addIncoming(NewInVal, PN.getIncomingBlock(i));
  }

  if (InVal) {
    // The new PHI unions all of the same values together.  This is really
    // common, so we handle it intelligently here for compile-time speed.
    NewLI->setOperand(0, InVal);
    delete NewPN;
  } else {
    InsertNewInstBefore(NewPN, PN);
  }

  // If this was a volatile load that we are merging, make sure to loop through
  // and mark all the input loads as non-volatile.  If we don't do this, we will
  // insert a new volatile load and the old ones will not be deletable.
  if (isVolatile)
    for (Value *IncValue : PN.incoming_values())
      cast<LoadInst>(IncValue)->setVolatile(false);

  PHIArgMergedDebugLoc(NewLI, PN);
  return NewLI;
}

/// TODO: This function could handle other cast types, but then it might
/// require special-casing a cast from the 'i1' type. See the comment in
/// FoldPHIArgOpIntoPHI() about pessimizing illegal integer types.
Instruction *InstCombiner::FoldPHIArgZextsIntoPHI(PHINode &Phi) {
  // We cannot create a new instruction after the PHI if the terminator is an
  // EHPad because there is no valid insertion point.
  if (Instruction *TI = Phi.getParent()->getTerminator())
    if (TI->isEHPad())
      return nullptr;

  // Early exit for the common case of a phi with two operands. These are
  // handled elsewhere. See the comment below where we check the count of zexts
  // and constants for more details.
  unsigned NumIncomingValues = Phi.getNumIncomingValues();
  if (NumIncomingValues < 3)
    return nullptr;

  // Find the narrower type specified by the first zext.
  Type *NarrowType = nullptr;
  for (Value *V : Phi.incoming_values()) {
    if (auto *Zext = dyn_cast<ZExtInst>(V)) {
      NarrowType = Zext->getSrcTy();
      break;
    }
  }
  if (!NarrowType)
    return nullptr;

  // Walk the phi operands checking that we only have zexts or constants that
  // we can shrink for free. Store the new operands for the new phi.
  SmallVector<Value *, 4> NewIncoming;
  unsigned NumZexts = 0;
  unsigned NumConsts = 0;
  for (Value *V : Phi.incoming_values()) {
    if (auto *Zext = dyn_cast<ZExtInst>(V)) {
      // All zexts must be identical and have one use.
      if (Zext->getSrcTy() != NarrowType || !Zext->hasOneUse())
        return nullptr;
      NewIncoming.push_back(Zext->getOperand(0));
      NumZexts++;
    } else if (auto *C = dyn_cast<Constant>(V)) {
      // Make sure that constants can fit in the new type.
      Constant *Trunc = ConstantExpr::getTrunc(C, NarrowType);
      if (ConstantExpr::getZExt(Trunc, C->getType()) != C)
        return nullptr;
      NewIncoming.push_back(Trunc);
      NumConsts++;
    } else {
      // If it's not a cast or a constant, bail out.
      return nullptr;
    }
  }

  // The more common cases of a phi with no constant operands or just one
  // variable operand are handled by FoldPHIArgOpIntoPHI() and foldOpIntoPhi()
  // respectively. foldOpIntoPhi() wants to do the opposite transform that is
  // performed here. It tries to replicate a cast in the phi operand's basic
  // block to expose other folding opportunities. Thus, InstCombine will
  // infinite loop without this check.
  if (NumConsts == 0 || NumZexts < 2)
    return nullptr;

  // All incoming values are zexts or constants that are safe to truncate.
  // Create a new phi node of the narrow type, phi together all of the new
  // operands, and zext the result back to the original type.
  PHINode *NewPhi = PHINode::Create(NarrowType, NumIncomingValues,
                                    Phi.getName() + ".shrunk");
  for (unsigned i = 0; i != NumIncomingValues; ++i)
    NewPhi->addIncoming(NewIncoming[i], Phi.getIncomingBlock(i));

  InsertNewInstBefore(NewPhi, Phi);
  return CastInst::CreateZExtOrBitCast(NewPhi, Phi.getType());
}

/// If all operands to a PHI node are the same "unary" operator and they all are
/// only used by the PHI, PHI together their inputs, and do the operation once,
/// to the result of the PHI.
Instruction *InstCombiner::FoldPHIArgOpIntoPHI(PHINode &PN) {
  // We cannot create a new instruction after the PHI if the terminator is an
  // EHPad because there is no valid insertion point.
  if (Instruction *TI = PN.getParent()->getTerminator())
    if (TI->isEHPad())
      return nullptr;

  Instruction *FirstInst = cast<Instruction>(PN.getIncomingValue(0));

  if (isa<GetElementPtrInst>(FirstInst))
    return FoldPHIArgGEPIntoPHI(PN);
  if (isa<LoadInst>(FirstInst))
    return FoldPHIArgLoadIntoPHI(PN);

  // Scan the instruction, looking for input operations that can be folded away.
  // If all input operands to the phi are the same instruction (e.g. a cast from
  // the same type or "+42") we can pull the operation through the PHI, reducing
  // code size and simplifying code.
  Constant *ConstantOp = nullptr;
  Type *CastSrcTy = nullptr;

  if (isa<CastInst>(FirstInst)) {
    CastSrcTy = FirstInst->getOperand(0)->getType();

    // Be careful about transforming integer PHIs.  We don't want to pessimize
    // the code by turning an i32 into an i1293.
    if (PN.getType()->isIntegerTy() && CastSrcTy->isIntegerTy()) {
      if (!shouldChangeType(PN.getType(), CastSrcTy))
        return nullptr;
    }
  } else if (isa<BinaryOperator>(FirstInst) || isa<CmpInst>(FirstInst)) {
    // Can fold binop, compare or shift here if the RHS is a constant,
    // otherwise call FoldPHIArgBinOpIntoPHI.
    ConstantOp = dyn_cast<Constant>(FirstInst->getOperand(1));
    if (!ConstantOp)
      return FoldPHIArgBinOpIntoPHI(PN);
  } else {
    return nullptr;  // Cannot fold this operation.
  }

  // Check to see if all arguments are the same operation.
  for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i) {
    Instruction *I = dyn_cast<Instruction>(PN.getIncomingValue(i));
    if (!I || !I->hasOneUse() || !I->isSameOperationAs(FirstInst))
      return nullptr;
    if (CastSrcTy) {
      if (I->getOperand(0)->getType() != CastSrcTy)
        return nullptr;  // Cast operation must match.
    } else if (I->getOperand(1) != ConstantOp) {
      return nullptr;
    }
  }

  // Okay, they are all the same operation.  Create a new PHI node of the
  // correct type, and PHI together all of the LHS's of the instructions.
  PHINode *NewPN = PHINode::Create(FirstInst->getOperand(0)->getType(),
                                   PN.getNumIncomingValues(),
                                   PN.getName()+".in");

  Value *InVal = FirstInst->getOperand(0);
  NewPN->addIncoming(InVal, PN.getIncomingBlock(0));

  // Add all operands to the new PHI.
  for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i) {
    Value *NewInVal = cast<Instruction>(PN.getIncomingValue(i))->getOperand(0);
    if (NewInVal != InVal)
      InVal = nullptr;
    NewPN->addIncoming(NewInVal, PN.getIncomingBlock(i));
  }

  Value *PhiVal;
  if (InVal) {
    // The new PHI unions all of the same values together.  This is really
    // common, so we handle it intelligently here for compile-time speed.
    PhiVal = InVal;
    delete NewPN;
  } else {
    InsertNewInstBefore(NewPN, PN);
    PhiVal = NewPN;
  }

  // Insert and return the new operation.
  if (CastInst *FirstCI = dyn_cast<CastInst>(FirstInst)) {
    CastInst *NewCI = CastInst::Create(FirstCI->getOpcode(), PhiVal,
                                       PN.getType());
    PHIArgMergedDebugLoc(NewCI, PN);
    return NewCI;
  }

  if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(FirstInst)) {
    BinOp = BinaryOperator::Create(BinOp->getOpcode(), PhiVal, ConstantOp);
    BinOp->copyIRFlags(PN.getIncomingValue(0));

    for (unsigned i = 1, e = PN.getNumIncomingValues(); i != e; ++i)
      BinOp->andIRFlags(PN.getIncomingValue(i));

    PHIArgMergedDebugLoc(BinOp, PN);
    return BinOp;
  }

  CmpInst *CIOp = cast<CmpInst>(FirstInst);
  CmpInst *NewCI = CmpInst::Create(CIOp->getOpcode(), CIOp->getPredicate(),
                                   PhiVal, ConstantOp);
  PHIArgMergedDebugLoc(NewCI, PN);
  return NewCI;
}

/// Return true if this PHI node is only used by a PHI node cycle that is dead.
static bool DeadPHICycle(PHINode *PN,
                         SmallPtrSetImpl<PHINode*> &PotentiallyDeadPHIs) {
  if (PN->use_empty()) return true;
  if (!PN->hasOneUse()) return false;

  // Remember this node, and if we find the cycle, return.
  if (!PotentiallyDeadPHIs.insert(PN).second)
    return true;

  // Don't scan crazily complex things.
  if (PotentiallyDeadPHIs.size() == 16)
    return false;

  if (PHINode *PU = dyn_cast<PHINode>(PN->user_back()))
    return DeadPHICycle(PU, PotentiallyDeadPHIs);

  return false;
}

/// Return true if this phi node is always equal to NonPhiInVal.
/// This happens with mutually cyclic phi nodes like:
///   z = some value; x = phi (y, z); y = phi (x, z)
static bool PHIsEqualValue(PHINode *PN, Value *NonPhiInVal,
                           SmallPtrSetImpl<PHINode*> &ValueEqualPHIs) {
  // See if we already saw this PHI node.
  if (!ValueEqualPHIs.insert(PN).second)
    return true;

  // Don't scan crazily complex things.
  if (ValueEqualPHIs.size() == 16)
    return false;

  // Scan the operands to see if they are either phi nodes or are equal to
  // the value.
  for (Value *Op : PN->incoming_values()) {
    if (PHINode *OpPN = dyn_cast<PHINode>(Op)) {
      if (!PHIsEqualValue(OpPN, NonPhiInVal, ValueEqualPHIs))
        return false;
    } else if (Op != NonPhiInVal)
      return false;
  }

  return true;
}

/// Return an existing non-zero constant if this phi node has one, otherwise
/// return constant 1.
static ConstantInt *GetAnyNonZeroConstInt(PHINode &PN) {
  assert(isa<IntegerType>(PN.getType()) && "Expect only integer type phi");
  for (Value *V : PN.operands())
    if (auto *ConstVA = dyn_cast<ConstantInt>(V))
      if (!ConstVA->isZero())
        return ConstVA;
  return ConstantInt::get(cast<IntegerType>(PN.getType()), 1);
}

namespace {
struct PHIUsageRecord {
  unsigned PHIId;     // The ID # of the PHI (something determinstic to sort on)
  unsigned Shift;     // The amount shifted.
  Instruction *Inst;  // The trunc instruction.

  PHIUsageRecord(unsigned pn, unsigned Sh, Instruction *User)
    : PHIId(pn), Shift(Sh), Inst(User) {}

  bool operator<(const PHIUsageRecord &RHS) const {
    if (PHIId < RHS.PHIId) return true;
    if (PHIId > RHS.PHIId) return false;
    if (Shift < RHS.Shift) return true;
    if (Shift > RHS.Shift) return false;
    return Inst->getType()->getPrimitiveSizeInBits() <
           RHS.Inst->getType()->getPrimitiveSizeInBits();
  }
};

struct LoweredPHIRecord {
  PHINode *PN;        // The PHI that was lowered.
  unsigned Shift;     // The amount shifted.
  unsigned Width;     // The width extracted.

  LoweredPHIRecord(PHINode *pn, unsigned Sh, Type *Ty)
    : PN(pn), Shift(Sh), Width(Ty->getPrimitiveSizeInBits()) {}

  // Ctor form used by DenseMap.
  LoweredPHIRecord(PHINode *pn, unsigned Sh)
    : PN(pn), Shift(Sh), Width(0) {}
};
}

namespace llvm {
  template<>
  struct DenseMapInfo<LoweredPHIRecord> {
    static inline LoweredPHIRecord getEmptyKey() {
      return LoweredPHIRecord(nullptr, 0);
    }
    static inline LoweredPHIRecord getTombstoneKey() {
      return LoweredPHIRecord(nullptr, 1);
    }
    static unsigned getHashValue(const LoweredPHIRecord &Val) {
      return DenseMapInfo<PHINode*>::getHashValue(Val.PN) ^ (Val.Shift>>3) ^
             (Val.Width>>3);
    }
    static bool isEqual(const LoweredPHIRecord &LHS,
                        const LoweredPHIRecord &RHS) {
      return LHS.PN == RHS.PN && LHS.Shift == RHS.Shift &&
             LHS.Width == RHS.Width;
    }
  };
}


/// This is an integer PHI and we know that it has an illegal type: see if it is
/// only used by trunc or trunc(lshr) operations. If so, we split the PHI into
/// the various pieces being extracted. This sort of thing is introduced when
/// SROA promotes an aggregate to large integer values.
///
/// TODO: The user of the trunc may be an bitcast to float/double/vector or an
/// inttoptr.  We should produce new PHIs in the right type.
///
Instruction *InstCombiner::SliceUpIllegalIntegerPHI(PHINode &FirstPhi) {
  // PHIUsers - Keep track of all of the truncated values extracted from a set
  // of PHIs, along with their offset.  These are the things we want to rewrite.
  SmallVector<PHIUsageRecord, 16> PHIUsers;

  // PHIs are often mutually cyclic, so we keep track of a whole set of PHI
  // nodes which are extracted from. PHIsToSlice is a set we use to avoid
  // revisiting PHIs, PHIsInspected is a ordered list of PHIs that we need to
  // check the uses of (to ensure they are all extracts).
  SmallVector<PHINode*, 8> PHIsToSlice;
  SmallPtrSet<PHINode*, 8> PHIsInspected;

  PHIsToSlice.push_back(&FirstPhi);
  PHIsInspected.insert(&FirstPhi);

  for (unsigned PHIId = 0; PHIId != PHIsToSlice.size(); ++PHIId) {
    PHINode *PN = PHIsToSlice[PHIId];

    // Scan the input list of the PHI.  If any input is an invoke, and if the
    // input is defined in the predecessor, then we won't be split the critical
    // edge which is required to insert a truncate.  Because of this, we have to
    // bail out.
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      InvokeInst *II = dyn_cast<InvokeInst>(PN->getIncomingValue(i));
      if (!II) continue;
      if (II->getParent() != PN->getIncomingBlock(i))
        continue;

      // If we have a phi, and if it's directly in the predecessor, then we have
      // a critical edge where we need to put the truncate.  Since we can't
      // split the edge in instcombine, we have to bail out.
      return nullptr;
    }

    for (User *U : PN->users()) {
      Instruction *UserI = cast<Instruction>(U);

      // If the user is a PHI, inspect its uses recursively.
      if (PHINode *UserPN = dyn_cast<PHINode>(UserI)) {
        if (PHIsInspected.insert(UserPN).second)
          PHIsToSlice.push_back(UserPN);
        continue;
      }

      // Truncates are always ok.
      if (isa<TruncInst>(UserI)) {
        PHIUsers.push_back(PHIUsageRecord(PHIId, 0, UserI));
        continue;
      }

      // Otherwise it must be a lshr which can only be used by one trunc.
      if (UserI->getOpcode() != Instruction::LShr ||
          !UserI->hasOneUse() || !isa<TruncInst>(UserI->user_back()) ||
          !isa<ConstantInt>(UserI->getOperand(1)))
        return nullptr;

      unsigned Shift = cast<ConstantInt>(UserI->getOperand(1))->getZExtValue();
      PHIUsers.push_back(PHIUsageRecord(PHIId, Shift, UserI->user_back()));
    }
  }

  // If we have no users, they must be all self uses, just nuke the PHI.
  if (PHIUsers.empty())
    return replaceInstUsesWith(FirstPhi, UndefValue::get(FirstPhi.getType()));

  // If this phi node is transformable, create new PHIs for all the pieces
  // extracted out of it.  First, sort the users by their offset and size.
  array_pod_sort(PHIUsers.begin(), PHIUsers.end());

  LLVM_DEBUG(dbgs() << "SLICING UP PHI: " << FirstPhi << '\n';
             for (unsigned i = 1, e = PHIsToSlice.size(); i != e; ++i) dbgs()
             << "AND USER PHI #" << i << ": " << *PHIsToSlice[i] << '\n';);

  // PredValues - This is a temporary used when rewriting PHI nodes.  It is
  // hoisted out here to avoid construction/destruction thrashing.
  DenseMap<BasicBlock*, Value*> PredValues;

  // ExtractedVals - Each new PHI we introduce is saved here so we don't
  // introduce redundant PHIs.
  DenseMap<LoweredPHIRecord, PHINode*> ExtractedVals;

  for (unsigned UserI = 0, UserE = PHIUsers.size(); UserI != UserE; ++UserI) {
    unsigned PHIId = PHIUsers[UserI].PHIId;
    PHINode *PN = PHIsToSlice[PHIId];
    unsigned Offset = PHIUsers[UserI].Shift;
    Type *Ty = PHIUsers[UserI].Inst->getType();

    PHINode *EltPHI;

    // If we've already lowered a user like this, reuse the previously lowered
    // value.
    if ((EltPHI = ExtractedVals[LoweredPHIRecord(PN, Offset, Ty)]) == nullptr) {

      // Otherwise, Create the new PHI node for this user.
      EltPHI = PHINode::Create(Ty, PN->getNumIncomingValues(),
                               PN->getName()+".off"+Twine(Offset), PN);
      assert(EltPHI->getType() != PN->getType() &&
             "Truncate didn't shrink phi?");

      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        BasicBlock *Pred = PN->getIncomingBlock(i);
        Value *&PredVal = PredValues[Pred];

        // If we already have a value for this predecessor, reuse it.
        if (PredVal) {
          EltPHI->addIncoming(PredVal, Pred);
          continue;
        }

        // Handle the PHI self-reuse case.
        Value *InVal = PN->getIncomingValue(i);
        if (InVal == PN) {
          PredVal = EltPHI;
          EltPHI->addIncoming(PredVal, Pred);
          continue;
        }

        if (PHINode *InPHI = dyn_cast<PHINode>(PN)) {
          // If the incoming value was a PHI, and if it was one of the PHIs we
          // already rewrote it, just use the lowered value.
          if (Value *Res = ExtractedVals[LoweredPHIRecord(InPHI, Offset, Ty)]) {
            PredVal = Res;
            EltPHI->addIncoming(PredVal, Pred);
            continue;
          }
        }

        // Otherwise, do an extract in the predecessor.
        Builder.SetInsertPoint(Pred->getTerminator());
        Value *Res = InVal;
        if (Offset)
          Res = Builder.CreateLShr(Res, ConstantInt::get(InVal->getType(),
                                                          Offset), "extract");
        Res = Builder.CreateTrunc(Res, Ty, "extract.t");
        PredVal = Res;
        EltPHI->addIncoming(Res, Pred);

        // If the incoming value was a PHI, and if it was one of the PHIs we are
        // rewriting, we will ultimately delete the code we inserted.  This
        // means we need to revisit that PHI to make sure we extract out the
        // needed piece.
        if (PHINode *OldInVal = dyn_cast<PHINode>(PN->getIncomingValue(i)))
          if (PHIsInspected.count(OldInVal)) {
            unsigned RefPHIId =
                find(PHIsToSlice, OldInVal) - PHIsToSlice.begin();
            PHIUsers.push_back(PHIUsageRecord(RefPHIId, Offset,
                                              cast<Instruction>(Res)));
            ++UserE;
          }
      }
      PredValues.clear();

      LLVM_DEBUG(dbgs() << "  Made element PHI for offset " << Offset << ": "
                        << *EltPHI << '\n');
      ExtractedVals[LoweredPHIRecord(PN, Offset, Ty)] = EltPHI;
    }

    // Replace the use of this piece with the PHI node.
    replaceInstUsesWith(*PHIUsers[UserI].Inst, EltPHI);
  }

  // Replace all the remaining uses of the PHI nodes (self uses and the lshrs)
  // with undefs.
  Value *Undef = UndefValue::get(FirstPhi.getType());
  for (unsigned i = 1, e = PHIsToSlice.size(); i != e; ++i)
    replaceInstUsesWith(*PHIsToSlice[i], Undef);
  return replaceInstUsesWith(FirstPhi, Undef);
}

// PHINode simplification
//
Instruction *InstCombiner::visitPHINode(PHINode &PN) {
  if (Value *V = SimplifyInstruction(&PN, SQ.getWithInstruction(&PN)))
    return replaceInstUsesWith(PN, V);

  if (Instruction *Result = FoldPHIArgZextsIntoPHI(PN))
    return Result;

  // If all PHI operands are the same operation, pull them through the PHI,
  // reducing code size.
  if (isa<Instruction>(PN.getIncomingValue(0)) &&
      isa<Instruction>(PN.getIncomingValue(1)) &&
      cast<Instruction>(PN.getIncomingValue(0))->getOpcode() ==
      cast<Instruction>(PN.getIncomingValue(1))->getOpcode() &&
      // FIXME: The hasOneUse check will fail for PHIs that use the value more
      // than themselves more than once.
      PN.getIncomingValue(0)->hasOneUse())
    if (Instruction *Result = FoldPHIArgOpIntoPHI(PN))
      return Result;

  // If this is a trivial cycle in the PHI node graph, remove it.  Basically, if
  // this PHI only has a single use (a PHI), and if that PHI only has one use (a
  // PHI)... break the cycle.
  if (PN.hasOneUse()) {
    if (Instruction *Result = FoldIntegerTypedPHI(PN))
      return Result;

    Instruction *PHIUser = cast<Instruction>(PN.user_back());
    if (PHINode *PU = dyn_cast<PHINode>(PHIUser)) {
      SmallPtrSet<PHINode*, 16> PotentiallyDeadPHIs;
      PotentiallyDeadPHIs.insert(&PN);
      if (DeadPHICycle(PU, PotentiallyDeadPHIs))
        return replaceInstUsesWith(PN, UndefValue::get(PN.getType()));
    }

    // If this phi has a single use, and if that use just computes a value for
    // the next iteration of a loop, delete the phi.  This occurs with unused
    // induction variables, e.g. "for (int j = 0; ; ++j);".  Detecting this
    // common case here is good because the only other things that catch this
    // are induction variable analysis (sometimes) and ADCE, which is only run
    // late.
    if (PHIUser->hasOneUse() &&
        (isa<BinaryOperator>(PHIUser) || isa<GetElementPtrInst>(PHIUser)) &&
        PHIUser->user_back() == &PN) {
      return replaceInstUsesWith(PN, UndefValue::get(PN.getType()));
    }
    // When a PHI is used only to be compared with zero, it is safe to replace
    // an incoming value proved as known nonzero with any non-zero constant.
    // For example, in the code below, the incoming value %v can be replaced
    // with any non-zero constant based on the fact that the PHI is only used to
    // be compared with zero and %v is a known non-zero value:
    // %v = select %cond, 1, 2
    // %p = phi [%v, BB] ...
    //      icmp eq, %p, 0
    auto *CmpInst = dyn_cast<ICmpInst>(PHIUser);
    // FIXME: To be simple, handle only integer type for now.
    if (CmpInst && isa<IntegerType>(PN.getType()) && CmpInst->isEquality() &&
        match(CmpInst->getOperand(1), m_Zero())) {
      ConstantInt *NonZeroConst = nullptr;
      for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i) {
        Instruction *CtxI = PN.getIncomingBlock(i)->getTerminator();
        Value *VA = PN.getIncomingValue(i);
        if (isKnownNonZero(VA, DL, 0, &AC, CtxI, &DT)) {
          if (!NonZeroConst)
            NonZeroConst = GetAnyNonZeroConstInt(PN);
          PN.setIncomingValue(i, NonZeroConst);
        }
      }
    }
  }

  // We sometimes end up with phi cycles that non-obviously end up being the
  // same value, for example:
  //   z = some value; x = phi (y, z); y = phi (x, z)
  // where the phi nodes don't necessarily need to be in the same block.  Do a
  // quick check to see if the PHI node only contains a single non-phi value, if
  // so, scan to see if the phi cycle is actually equal to that value.
  {
    unsigned InValNo = 0, NumIncomingVals = PN.getNumIncomingValues();
    // Scan for the first non-phi operand.
    while (InValNo != NumIncomingVals &&
           isa<PHINode>(PN.getIncomingValue(InValNo)))
      ++InValNo;

    if (InValNo != NumIncomingVals) {
      Value *NonPhiInVal = PN.getIncomingValue(InValNo);

      // Scan the rest of the operands to see if there are any conflicts, if so
      // there is no need to recursively scan other phis.
      for (++InValNo; InValNo != NumIncomingVals; ++InValNo) {
        Value *OpVal = PN.getIncomingValue(InValNo);
        if (OpVal != NonPhiInVal && !isa<PHINode>(OpVal))
          break;
      }

      // If we scanned over all operands, then we have one unique value plus
      // phi values.  Scan PHI nodes to see if they all merge in each other or
      // the value.
      if (InValNo == NumIncomingVals) {
        SmallPtrSet<PHINode*, 16> ValueEqualPHIs;
        if (PHIsEqualValue(&PN, NonPhiInVal, ValueEqualPHIs))
          return replaceInstUsesWith(PN, NonPhiInVal);
      }
    }
  }

  // If there are multiple PHIs, sort their operands so that they all list
  // the blocks in the same order. This will help identical PHIs be eliminated
  // by other passes. Other passes shouldn't depend on this for correctness
  // however.
  PHINode *FirstPN = cast<PHINode>(PN.getParent()->begin());
  if (&PN != FirstPN)
    for (unsigned i = 0, e = FirstPN->getNumIncomingValues(); i != e; ++i) {
      BasicBlock *BBA = PN.getIncomingBlock(i);
      BasicBlock *BBB = FirstPN->getIncomingBlock(i);
      if (BBA != BBB) {
        Value *VA = PN.getIncomingValue(i);
        unsigned j = PN.getBasicBlockIndex(BBB);
        Value *VB = PN.getIncomingValue(j);
        PN.setIncomingBlock(i, BBB);
        PN.setIncomingValue(i, VB);
        PN.setIncomingBlock(j, BBA);
        PN.setIncomingValue(j, VA);
        // NOTE: Instcombine normally would want us to "return &PN" if we
        // modified any of the operands of an instruction.  However, since we
        // aren't adding or removing uses (just rearranging them) we don't do
        // this in this case.
      }
    }

  // If this is an integer PHI and we know that it has an illegal type, see if
  // it is only used by trunc or trunc(lshr) operations.  If so, we split the
  // PHI into the various pieces being extracted.  This sort of thing is
  // introduced when SROA promotes an aggregate to a single large integer type.
  if (PN.getType()->isIntegerTy() &&
      !DL.isLegalInteger(PN.getType()->getPrimitiveSizeInBits()))
    if (Instruction *Res = SliceUpIllegalIntegerPHI(PN))
      return Res;

  return nullptr;
}
