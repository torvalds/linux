//===- CtorUtils.cpp - Helpers for working with global_ctors ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that are used to process llvm.global_ctors.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CtorUtils.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "ctor_utils"

using namespace llvm;

/// Given a specified llvm.global_ctors list, remove the listed elements.
static void removeGlobalCtors(GlobalVariable *GCL, const BitVector &CtorsToRemove) {
  // Filter out the initializer elements to remove.
  ConstantArray *OldCA = cast<ConstantArray>(GCL->getInitializer());
  SmallVector<Constant *, 10> CAList;
  for (unsigned I = 0, E = OldCA->getNumOperands(); I < E; ++I)
    if (!CtorsToRemove.test(I))
      CAList.push_back(OldCA->getOperand(I));

  // Create the new array initializer.
  ArrayType *ATy =
      ArrayType::get(OldCA->getType()->getElementType(), CAList.size());
  Constant *CA = ConstantArray::get(ATy, CAList);

  // If we didn't change the number of elements, don't create a new GV.
  if (CA->getType() == OldCA->getType()) {
    GCL->setInitializer(CA);
    return;
  }

  // Create the new global and insert it next to the existing list.
  GlobalVariable *NGV =
      new GlobalVariable(CA->getType(), GCL->isConstant(), GCL->getLinkage(),
                         CA, "", GCL->getThreadLocalMode());
  GCL->getParent()->getGlobalList().insert(GCL->getIterator(), NGV);
  NGV->takeName(GCL);

  // Nuke the old list, replacing any uses with the new one.
  if (!GCL->use_empty()) {
    Constant *V = NGV;
    if (V->getType() != GCL->getType())
      V = ConstantExpr::getBitCast(V, GCL->getType());
    GCL->replaceAllUsesWith(V);
  }
  GCL->eraseFromParent();
}

/// Given a llvm.global_ctors list that we can understand,
/// return a list of the functions and null terminator as a vector.
static std::vector<Function *> parseGlobalCtors(GlobalVariable *GV) {
  if (GV->getInitializer()->isNullValue())
    return std::vector<Function *>();
  ConstantArray *CA = cast<ConstantArray>(GV->getInitializer());
  std::vector<Function *> Result;
  Result.reserve(CA->getNumOperands());
  for (auto &V : CA->operands()) {
    ConstantStruct *CS = cast<ConstantStruct>(V);
    Result.push_back(dyn_cast<Function>(CS->getOperand(1)));
  }
  return Result;
}

/// Find the llvm.global_ctors list, verifying that all initializers have an
/// init priority of 65535.
static GlobalVariable *findGlobalCtors(Module &M) {
  GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors");
  if (!GV)
    return nullptr;

  // Verify that the initializer is simple enough for us to handle. We are
  // only allowed to optimize the initializer if it is unique.
  if (!GV->hasUniqueInitializer())
    return nullptr;

  if (isa<ConstantAggregateZero>(GV->getInitializer()))
    return GV;
  ConstantArray *CA = cast<ConstantArray>(GV->getInitializer());

  for (auto &V : CA->operands()) {
    if (isa<ConstantAggregateZero>(V))
      continue;
    ConstantStruct *CS = cast<ConstantStruct>(V);
    if (isa<ConstantPointerNull>(CS->getOperand(1)))
      continue;

    // Must have a function or null ptr.
    if (!isa<Function>(CS->getOperand(1)))
      return nullptr;

    // Init priority must be standard.
    ConstantInt *CI = cast<ConstantInt>(CS->getOperand(0));
    if (CI->getZExtValue() != 65535)
      return nullptr;
  }

  return GV;
}

/// Call "ShouldRemove" for every entry in M's global_ctor list and remove the
/// entries for which it returns true.  Return true if anything changed.
bool llvm::optimizeGlobalCtorsList(
    Module &M, function_ref<bool(Function *)> ShouldRemove) {
  GlobalVariable *GlobalCtors = findGlobalCtors(M);
  if (!GlobalCtors)
    return false;

  std::vector<Function *> Ctors = parseGlobalCtors(GlobalCtors);
  if (Ctors.empty())
    return false;

  bool MadeChange = false;

  // Loop over global ctors, optimizing them when we can.
  unsigned NumCtors = Ctors.size();
  BitVector CtorsToRemove(NumCtors);
  for (unsigned i = 0; i != Ctors.size() && NumCtors > 0; ++i) {
    Function *F = Ctors[i];
    // Found a null terminator in the middle of the list, prune off the rest of
    // the list.
    if (!F)
      continue;

    LLVM_DEBUG(dbgs() << "Optimizing Global Constructor: " << *F << "\n");

    // We cannot simplify external ctor functions.
    if (F->empty())
      continue;

    // If we can evaluate the ctor at compile time, do.
    if (ShouldRemove(F)) {
      Ctors[i] = nullptr;
      CtorsToRemove.set(i);
      NumCtors--;
      MadeChange = true;
      continue;
    }
  }

  if (!MadeChange)
    return false;

  removeGlobalCtors(GlobalCtors, CtorsToRemove);
  return true;
}
