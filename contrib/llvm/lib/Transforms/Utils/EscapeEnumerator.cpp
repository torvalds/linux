//===- EscapeEnumerator.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines a helper class that enumerates all possible exits from a function,
// including exception handling.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/EscapeEnumerator.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Module.h"
using namespace llvm;

static Constant *getDefaultPersonalityFn(Module *M) {
  LLVMContext &C = M->getContext();
  Triple T(M->getTargetTriple());
  EHPersonality Pers = getDefaultEHPersonality(T);
  return M->getOrInsertFunction(getEHPersonalityName(Pers),
                                FunctionType::get(Type::getInt32Ty(C), true));
}

IRBuilder<> *EscapeEnumerator::Next() {
  if (Done)
    return nullptr;

  // Find all 'return', 'resume', and 'unwind' instructions.
  while (StateBB != StateE) {
    BasicBlock *CurBB = &*StateBB++;

    // Branches and invokes do not escape, only unwind, resume, and return
    // do.
    Instruction *TI = CurBB->getTerminator();
    if (!isa<ReturnInst>(TI) && !isa<ResumeInst>(TI))
      continue;

    Builder.SetInsertPoint(TI);
    return &Builder;
  }

  Done = true;

  if (!HandleExceptions)
    return nullptr;

  if (F.doesNotThrow())
    return nullptr;

  // Find all 'call' instructions that may throw.
  SmallVector<Instruction *, 16> Calls;
  for (BasicBlock &BB : F)
    for (Instruction &II : BB)
      if (CallInst *CI = dyn_cast<CallInst>(&II))
        if (!CI->doesNotThrow())
          Calls.push_back(CI);

  if (Calls.empty())
    return nullptr;

  // Create a cleanup block.
  LLVMContext &C = F.getContext();
  BasicBlock *CleanupBB = BasicBlock::Create(C, CleanupBBName, &F);
  Type *ExnTy = StructType::get(Type::getInt8PtrTy(C), Type::getInt32Ty(C));
  if (!F.hasPersonalityFn()) {
    Constant *PersFn = getDefaultPersonalityFn(F.getParent());
    F.setPersonalityFn(PersFn);
  }

  if (isScopedEHPersonality(classifyEHPersonality(F.getPersonalityFn()))) {
    report_fatal_error("Scoped EH not supported");
  }

  LandingPadInst *LPad =
      LandingPadInst::Create(ExnTy, 1, "cleanup.lpad", CleanupBB);
  LPad->setCleanup(true);
  ResumeInst *RI = ResumeInst::Create(LPad, CleanupBB);

  // Transform the 'call' instructions into 'invoke's branching to the
  // cleanup block. Go in reverse order to make prettier BB names.
  SmallVector<Value *, 16> Args;
  for (unsigned I = Calls.size(); I != 0;) {
    CallInst *CI = cast<CallInst>(Calls[--I]);
    changeToInvokeAndSplitBasicBlock(CI, CleanupBB);
  }

  Builder.SetInsertPoint(RI);
  return &Builder;
}
