//===- CallPromotionUtils.cpp - Utilities for call promotion ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements utilities useful for promoting indirect call sites to
// direct call sites.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "call-promotion-utils"

/// Fix-up phi nodes in an invoke instruction's normal destination.
///
/// After versioning an invoke instruction, values coming from the original
/// block will now be coming from the "merge" block. For example, in the code
/// below:
///
///   then_bb:
///     %t0 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   else_bb:
///     %t1 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   merge_bb:
///     %t2 = phi i32 [ %t0, %then_bb ], [ %t1, %else_bb ]
///     br %normal_dst
///
///   normal_dst:
///     %t3 = phi i32 [ %x, %orig_bb ], ...
///
/// "orig_bb" is no longer a predecessor of "normal_dst", so the phi nodes in
/// "normal_dst" must be fixed to refer to "merge_bb":
///
///    normal_dst:
///      %t3 = phi i32 [ %x, %merge_bb ], ...
///
static void fixupPHINodeForNormalDest(InvokeInst *Invoke, BasicBlock *OrigBlock,
                                      BasicBlock *MergeBlock) {
  for (PHINode &Phi : Invoke->getNormalDest()->phis()) {
    int Idx = Phi.getBasicBlockIndex(OrigBlock);
    if (Idx == -1)
      continue;
    Phi.setIncomingBlock(Idx, MergeBlock);
  }
}

/// Fix-up phi nodes in an invoke instruction's unwind destination.
///
/// After versioning an invoke instruction, values coming from the original
/// block will now be coming from either the "then" block or the "else" block.
/// For example, in the code below:
///
///   then_bb:
///     %t0 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   else_bb:
///     %t1 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   unwind_dst:
///     %t3 = phi i32 [ %x, %orig_bb ], ...
///
/// "orig_bb" is no longer a predecessor of "unwind_dst", so the phi nodes in
/// "unwind_dst" must be fixed to refer to "then_bb" and "else_bb":
///
///   unwind_dst:
///     %t3 = phi i32 [ %x, %then_bb ], [ %x, %else_bb ], ...
///
static void fixupPHINodeForUnwindDest(InvokeInst *Invoke, BasicBlock *OrigBlock,
                                      BasicBlock *ThenBlock,
                                      BasicBlock *ElseBlock) {
  for (PHINode &Phi : Invoke->getUnwindDest()->phis()) {
    int Idx = Phi.getBasicBlockIndex(OrigBlock);
    if (Idx == -1)
      continue;
    auto *V = Phi.getIncomingValue(Idx);
    Phi.setIncomingBlock(Idx, ThenBlock);
    Phi.addIncoming(V, ElseBlock);
  }
}

/// Create a phi node for the returned value of a call or invoke instruction.
///
/// After versioning a call or invoke instruction that returns a value, we have
/// to merge the value of the original and new instructions. We do this by
/// creating a phi node and replacing uses of the original instruction with this
/// phi node.
///
/// For example, if \p OrigInst is defined in "else_bb" and \p NewInst is
/// defined in "then_bb", we create the following phi node:
///
///   ; Uses of the original instruction are replaced by uses of the phi node.
///   %t0 = phi i32 [ %orig_inst, %else_bb ], [ %new_inst, %then_bb ],
///
static void createRetPHINode(Instruction *OrigInst, Instruction *NewInst,
                             BasicBlock *MergeBlock, IRBuilder<> &Builder) {

  if (OrigInst->getType()->isVoidTy() || OrigInst->use_empty())
    return;

  Builder.SetInsertPoint(&MergeBlock->front());
  PHINode *Phi = Builder.CreatePHI(OrigInst->getType(), 0);
  SmallVector<User *, 16> UsersToUpdate;
  for (User *U : OrigInst->users())
    UsersToUpdate.push_back(U);
  for (User *U : UsersToUpdate)
    U->replaceUsesOfWith(OrigInst, Phi);
  Phi->addIncoming(OrigInst, OrigInst->getParent());
  Phi->addIncoming(NewInst, NewInst->getParent());
}

/// Cast a call or invoke instruction to the given type.
///
/// When promoting a call site, the return type of the call site might not match
/// that of the callee. If this is the case, we have to cast the returned value
/// to the correct type. The location of the cast depends on if we have a call
/// or invoke instruction.
///
/// For example, if the call instruction below requires a bitcast after
/// promotion:
///
///   orig_bb:
///     %t0 = call i32 @func()
///     ...
///
/// The bitcast is placed after the call instruction:
///
///   orig_bb:
///     ; Uses of the original return value are replaced by uses of the bitcast.
///     %t0 = call i32 @func()
///     %t1 = bitcast i32 %t0 to ...
///     ...
///
/// A similar transformation is performed for invoke instructions. However,
/// since invokes are terminating, a new block is created for the bitcast. For
/// example, if the invoke instruction below requires a bitcast after promotion:
///
///   orig_bb:
///     %t0 = invoke i32 @func() to label %normal_dst unwind label %unwind_dst
///
/// The edge between the original block and the invoke's normal destination is
/// split, and the bitcast is placed there:
///
///   orig_bb:
///     %t0 = invoke i32 @func() to label %split_bb unwind label %unwind_dst
///
///   split_bb:
///     ; Uses of the original return value are replaced by uses of the bitcast.
///     %t1 = bitcast i32 %t0 to ...
///     br label %normal_dst
///
static void createRetBitCast(CallSite CS, Type *RetTy, CastInst **RetBitCast) {

  // Save the users of the calling instruction. These uses will be changed to
  // use the bitcast after we create it.
  SmallVector<User *, 16> UsersToUpdate;
  for (User *U : CS.getInstruction()->users())
    UsersToUpdate.push_back(U);

  // Determine an appropriate location to create the bitcast for the return
  // value. The location depends on if we have a call or invoke instruction.
  Instruction *InsertBefore = nullptr;
  if (auto *Invoke = dyn_cast<InvokeInst>(CS.getInstruction()))
    InsertBefore =
        &SplitEdge(Invoke->getParent(), Invoke->getNormalDest())->front();
  else
    InsertBefore = &*std::next(CS.getInstruction()->getIterator());

  // Bitcast the return value to the correct type.
  auto *Cast = CastInst::CreateBitOrPointerCast(CS.getInstruction(), RetTy, "",
                                                InsertBefore);
  if (RetBitCast)
    *RetBitCast = Cast;

  // Replace all the original uses of the calling instruction with the bitcast.
  for (User *U : UsersToUpdate)
    U->replaceUsesOfWith(CS.getInstruction(), Cast);
}

/// Predicate and clone the given call site.
///
/// This function creates an if-then-else structure at the location of the call
/// site. The "if" condition compares the call site's called value to the given
/// callee. The original call site is moved into the "else" block, and a clone
/// of the call site is placed in the "then" block. The cloned instruction is
/// returned.
///
/// For example, the call instruction below:
///
///   orig_bb:
///     %t0 = call i32 %ptr()
///     ...
///
/// Is replace by the following:
///
///   orig_bb:
///     %cond = icmp eq i32 ()* %ptr, @func
///     br i1 %cond, %then_bb, %else_bb
///
///   then_bb:
///     ; The clone of the original call instruction is placed in the "then"
///     ; block. It is not yet promoted.
///     %t1 = call i32 %ptr()
///     br merge_bb
///
///   else_bb:
///     ; The original call instruction is moved to the "else" block.
///     %t0 = call i32 %ptr()
///     br merge_bb
///
///   merge_bb:
///     ; Uses of the original call instruction are replaced by uses of the phi
///     ; node.
///     %t2 = phi i32 [ %t0, %else_bb ], [ %t1, %then_bb ]
///     ...
///
/// A similar transformation is performed for invoke instructions. However,
/// since invokes are terminating, more work is required. For example, the
/// invoke instruction below:
///
///   orig_bb:
///     %t0 = invoke %ptr() to label %normal_dst unwind label %unwind_dst
///
/// Is replace by the following:
///
///   orig_bb:
///     %cond = icmp eq i32 ()* %ptr, @func
///     br i1 %cond, %then_bb, %else_bb
///
///   then_bb:
///     ; The clone of the original invoke instruction is placed in the "then"
///     ; block, and its normal destination is set to the "merge" block. It is
///     ; not yet promoted.
///     %t1 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   else_bb:
///     ; The original invoke instruction is moved into the "else" block, and
///     ; its normal destination is set to the "merge" block.
///     %t0 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   merge_bb:
///     ; Uses of the original invoke instruction are replaced by uses of the
///     ; phi node, and the merge block branches to the normal destination.
///     %t2 = phi i32 [ %t0, %else_bb ], [ %t1, %then_bb ]
///     br %normal_dst
///
static Instruction *versionCallSite(CallSite CS, Value *Callee,
                                    MDNode *BranchWeights) {

  IRBuilder<> Builder(CS.getInstruction());
  Instruction *OrigInst = CS.getInstruction();
  BasicBlock *OrigBlock = OrigInst->getParent();

  // Create the compare. The called value and callee must have the same type to
  // be compared.
  if (CS.getCalledValue()->getType() != Callee->getType())
    Callee = Builder.CreateBitCast(Callee, CS.getCalledValue()->getType());
  auto *Cond = Builder.CreateICmpEQ(CS.getCalledValue(), Callee);

  // Create an if-then-else structure. The original instruction is moved into
  // the "else" block, and a clone of the original instruction is placed in the
  // "then" block.
  Instruction *ThenTerm = nullptr;
  Instruction *ElseTerm = nullptr;
  SplitBlockAndInsertIfThenElse(Cond, CS.getInstruction(), &ThenTerm, &ElseTerm,
                                BranchWeights);
  BasicBlock *ThenBlock = ThenTerm->getParent();
  BasicBlock *ElseBlock = ElseTerm->getParent();
  BasicBlock *MergeBlock = OrigInst->getParent();

  ThenBlock->setName("if.true.direct_targ");
  ElseBlock->setName("if.false.orig_indirect");
  MergeBlock->setName("if.end.icp");

  Instruction *NewInst = OrigInst->clone();
  OrigInst->moveBefore(ElseTerm);
  NewInst->insertBefore(ThenTerm);

  // If the original call site is an invoke instruction, we have extra work to
  // do since invoke instructions are terminating. We have to fix-up phi nodes
  // in the invoke's normal and unwind destinations.
  if (auto *OrigInvoke = dyn_cast<InvokeInst>(OrigInst)) {
    auto *NewInvoke = cast<InvokeInst>(NewInst);

    // Invoke instructions are terminating, so we don't need the terminator
    // instructions that were just created.
    ThenTerm->eraseFromParent();
    ElseTerm->eraseFromParent();

    // Branch from the "merge" block to the original normal destination.
    Builder.SetInsertPoint(MergeBlock);
    Builder.CreateBr(OrigInvoke->getNormalDest());

    // Fix-up phi nodes in the original invoke's normal and unwind destinations.
    fixupPHINodeForNormalDest(OrigInvoke, OrigBlock, MergeBlock);
    fixupPHINodeForUnwindDest(OrigInvoke, MergeBlock, ThenBlock, ElseBlock);

    // Now set the normal destinations of the invoke instructions to be the
    // "merge" block.
    OrigInvoke->setNormalDest(MergeBlock);
    NewInvoke->setNormalDest(MergeBlock);
  }

  // Create a phi node for the returned value of the call site.
  createRetPHINode(OrigInst, NewInst, MergeBlock, Builder);

  return NewInst;
}

bool llvm::isLegalToPromote(CallSite CS, Function *Callee,
                            const char **FailureReason) {
  assert(!CS.getCalledFunction() && "Only indirect call sites can be promoted");

  auto &DL = Callee->getParent()->getDataLayout();

  // Check the return type. The callee's return value type must be bitcast
  // compatible with the call site's type.
  Type *CallRetTy = CS.getInstruction()->getType();
  Type *FuncRetTy = Callee->getReturnType();
  if (CallRetTy != FuncRetTy)
    if (!CastInst::isBitOrNoopPointerCastable(FuncRetTy, CallRetTy, DL)) {
      if (FailureReason)
        *FailureReason = "Return type mismatch";
      return false;
    }

  // The number of formal arguments of the callee.
  unsigned NumParams = Callee->getFunctionType()->getNumParams();

  // Check the number of arguments. The callee and call site must agree on the
  // number of arguments.
  if (CS.arg_size() != NumParams && !Callee->isVarArg()) {
    if (FailureReason)
      *FailureReason = "The number of arguments mismatch";
    return false;
  }

  // Check the argument types. The callee's formal argument types must be
  // bitcast compatible with the corresponding actual argument types of the call
  // site.
  for (unsigned I = 0; I < NumParams; ++I) {
    Type *FormalTy = Callee->getFunctionType()->getFunctionParamType(I);
    Type *ActualTy = CS.getArgument(I)->getType();
    if (FormalTy == ActualTy)
      continue;
    if (!CastInst::isBitOrNoopPointerCastable(ActualTy, FormalTy, DL)) {
      if (FailureReason)
        *FailureReason = "Argument type mismatch";
      return false;
    }
  }

  return true;
}

Instruction *llvm::promoteCall(CallSite CS, Function *Callee,
                               CastInst **RetBitCast) {
  assert(!CS.getCalledFunction() && "Only indirect call sites can be promoted");

  // Set the called function of the call site to be the given callee.
  CS.setCalledFunction(Callee);

  // Since the call site will no longer be direct, we must clear metadata that
  // is only appropriate for indirect calls. This includes !prof and !callees
  // metadata.
  CS.getInstruction()->setMetadata(LLVMContext::MD_prof, nullptr);
  CS.getInstruction()->setMetadata(LLVMContext::MD_callees, nullptr);

  // If the function type of the call site matches that of the callee, no
  // additional work is required.
  if (CS.getFunctionType() == Callee->getFunctionType())
    return CS.getInstruction();

  // Save the return types of the call site and callee.
  Type *CallSiteRetTy = CS.getInstruction()->getType();
  Type *CalleeRetTy = Callee->getReturnType();

  // Change the function type of the call site the match that of the callee.
  CS.mutateFunctionType(Callee->getFunctionType());

  // Inspect the arguments of the call site. If an argument's type doesn't
  // match the corresponding formal argument's type in the callee, bitcast it
  // to the correct type.
  auto CalleeType = Callee->getFunctionType();
  auto CalleeParamNum = CalleeType->getNumParams();

  LLVMContext &Ctx = Callee->getContext();
  const AttributeList &CallerPAL = CS.getAttributes();
  // The new list of argument attributes.
  SmallVector<AttributeSet, 4> NewArgAttrs;
  bool AttributeChanged = false;

  for (unsigned ArgNo = 0; ArgNo < CalleeParamNum; ++ArgNo) {
    auto *Arg = CS.getArgument(ArgNo);
    Type *FormalTy = CalleeType->getParamType(ArgNo);
    Type *ActualTy = Arg->getType();
    if (FormalTy != ActualTy) {
      auto *Cast = CastInst::CreateBitOrPointerCast(Arg, FormalTy, "",
                                                    CS.getInstruction());
      CS.setArgument(ArgNo, Cast);

      // Remove any incompatible attributes for the argument.
      AttrBuilder ArgAttrs(CallerPAL.getParamAttributes(ArgNo));
      ArgAttrs.remove(AttributeFuncs::typeIncompatible(FormalTy));
      NewArgAttrs.push_back(AttributeSet::get(Ctx, ArgAttrs));
      AttributeChanged = true;
    } else
      NewArgAttrs.push_back(CallerPAL.getParamAttributes(ArgNo));
  }

  // If the return type of the call site doesn't match that of the callee, cast
  // the returned value to the appropriate type.
  // Remove any incompatible return value attribute.
  AttrBuilder RAttrs(CallerPAL, AttributeList::ReturnIndex);
  if (!CallSiteRetTy->isVoidTy() && CallSiteRetTy != CalleeRetTy) {
    createRetBitCast(CS, CallSiteRetTy, RetBitCast);
    RAttrs.remove(AttributeFuncs::typeIncompatible(CalleeRetTy));
    AttributeChanged = true;
  }

  // Set the new callsite attribute.
  if (AttributeChanged)
    CS.setAttributes(AttributeList::get(Ctx, CallerPAL.getFnAttributes(),
                                        AttributeSet::get(Ctx, RAttrs),
                                        NewArgAttrs));

  return CS.getInstruction();
}

Instruction *llvm::promoteCallWithIfThenElse(CallSite CS, Function *Callee,
                                             MDNode *BranchWeights) {

  // Version the indirect call site. If the called value is equal to the given
  // callee, 'NewInst' will be executed, otherwise the original call site will
  // be executed.
  Instruction *NewInst = versionCallSite(CS, Callee, BranchWeights);

  // Promote 'NewInst' so that it directly calls the desired function.
  return promoteCall(CallSite(NewInst), Callee);
}

#undef DEBUG_TYPE
