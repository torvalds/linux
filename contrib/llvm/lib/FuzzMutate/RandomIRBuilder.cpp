//===-- RandomIRBuilder.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/RandomIRBuilder.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/FuzzMutate/Random.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;
using namespace fuzzerop;

Value *RandomIRBuilder::findOrCreateSource(BasicBlock &BB,
                                           ArrayRef<Instruction *> Insts) {
  return findOrCreateSource(BB, Insts, {}, anyType());
}

Value *RandomIRBuilder::findOrCreateSource(BasicBlock &BB,
                                           ArrayRef<Instruction *> Insts,
                                           ArrayRef<Value *> Srcs,
                                           SourcePred Pred) {
  auto MatchesPred = [&Srcs, &Pred](Instruction *Inst) {
    return Pred.matches(Srcs, Inst);
  };
  auto RS = makeSampler(Rand, make_filter_range(Insts, MatchesPred));
  // Also consider choosing no source, meaning we want a new one.
  RS.sample(nullptr, /*Weight=*/1);
  if (Instruction *Src = RS.getSelection())
    return Src;
  return newSource(BB, Insts, Srcs, Pred);
}

Value *RandomIRBuilder::newSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                                  ArrayRef<Value *> Srcs, SourcePred Pred) {
  // Generate some constants to choose from.
  auto RS = makeSampler<Value *>(Rand);
  RS.sample(Pred.generate(Srcs, KnownTypes));

  // If we can find a pointer to load from, use it half the time.
  Value *Ptr = findPointer(BB, Insts, Srcs, Pred);
  if (Ptr) {
    // Create load from the chosen pointer
    auto IP = BB.getFirstInsertionPt();
    if (auto *I = dyn_cast<Instruction>(Ptr)) {
      IP = ++I->getIterator();
      assert(IP != BB.end() && "guaranteed by the findPointer");
    }
    auto *NewLoad = new LoadInst(Ptr, "L", &*IP);

    // Only sample this load if it really matches the descriptor
    if (Pred.matches(Srcs, NewLoad))
      RS.sample(NewLoad, RS.totalWeight());
    else
      NewLoad->eraseFromParent();
  }

  assert(!RS.isEmpty() && "Failed to generate sources");
  return RS.getSelection();
}

static bool isCompatibleReplacement(const Instruction *I, const Use &Operand,
                                    const Value *Replacement) {
  if (Operand->getType() != Replacement->getType())
    return false;
  switch (I->getOpcode()) {
  case Instruction::GetElementPtr:
  case Instruction::ExtractElement:
  case Instruction::ExtractValue:
    // TODO: We could potentially validate these, but for now just leave indices
    // alone.
    if (Operand.getOperandNo() >= 1)
      return false;
    break;
  case Instruction::InsertValue:
  case Instruction::InsertElement:
  case Instruction::ShuffleVector:
    if (Operand.getOperandNo() >= 2)
      return false;
    break;
  default:
    break;
  }
  return true;
}

void RandomIRBuilder::connectToSink(BasicBlock &BB,
                                    ArrayRef<Instruction *> Insts, Value *V) {
  auto RS = makeSampler<Use *>(Rand);
  for (auto &I : Insts) {
    if (isa<IntrinsicInst>(I))
      // TODO: Replacing operands of intrinsics would be interesting, but
      // there's no easy way to verify that a given replacement is valid given
      // that intrinsics can impose arbitrary constraints.
      continue;
    for (Use &U : I->operands())
      if (isCompatibleReplacement(I, U, V))
        RS.sample(&U, 1);
  }
  // Also consider choosing no sink, meaning we want a new one.
  RS.sample(nullptr, /*Weight=*/1);

  if (Use *Sink = RS.getSelection()) {
    User *U = Sink->getUser();
    unsigned OpNo = Sink->getOperandNo();
    U->setOperand(OpNo, V);
    return;
  }
  newSink(BB, Insts, V);
}

void RandomIRBuilder::newSink(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                              Value *V) {
  Value *Ptr = findPointer(BB, Insts, {V}, matchFirstType());
  if (!Ptr) {
    if (uniform(Rand, 0, 1))
      Ptr = new AllocaInst(V->getType(), 0, "A", &*BB.getFirstInsertionPt());
    else
      Ptr = UndefValue::get(PointerType::get(V->getType(), 0));
  }

  new StoreInst(V, Ptr, Insts.back());
}

Value *RandomIRBuilder::findPointer(BasicBlock &BB,
                                    ArrayRef<Instruction *> Insts,
                                    ArrayRef<Value *> Srcs, SourcePred Pred) {
  auto IsMatchingPtr = [&Srcs, &Pred](Instruction *Inst) {
    // Invoke instructions sometimes produce valid pointers but currently
    // we can't insert loads or stores from them
    if (Inst->isTerminator())
      return false;

    if (auto PtrTy = dyn_cast<PointerType>(Inst->getType())) {
      // We can never generate loads from non first class or non sized types
      if (!PtrTy->getElementType()->isSized() ||
          !PtrTy->getElementType()->isFirstClassType())
        return false;

      // TODO: Check if this is horribly expensive.
      return Pred.matches(Srcs, UndefValue::get(PtrTy->getElementType()));
    }
    return false;
  };
  if (auto RS = makeSampler(Rand, make_filter_range(Insts, IsMatchingPtr)))
    return RS.getSelection();
  return nullptr;
}
