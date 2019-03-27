//===- PreISelIntrinsicLowering.cpp - Pre-ISel intrinsic lowering pass ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR lowering for the llvm.load.relative and llvm.objc.*
// intrinsics.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/PreISelIntrinsicLowering.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

static bool lowerLoadRelative(Function &F) {
  if (F.use_empty())
    return false;

  bool Changed = false;
  Type *Int32Ty = Type::getInt32Ty(F.getContext());
  Type *Int32PtrTy = Int32Ty->getPointerTo();
  Type *Int8Ty = Type::getInt8Ty(F.getContext());

  for (auto I = F.use_begin(), E = F.use_end(); I != E;) {
    auto CI = dyn_cast<CallInst>(I->getUser());
    ++I;
    if (!CI || CI->getCalledValue() != &F)
      continue;

    IRBuilder<> B(CI);
    Value *OffsetPtr =
        B.CreateGEP(Int8Ty, CI->getArgOperand(0), CI->getArgOperand(1));
    Value *OffsetPtrI32 = B.CreateBitCast(OffsetPtr, Int32PtrTy);
    Value *OffsetI32 = B.CreateAlignedLoad(OffsetPtrI32, 4);

    Value *ResultPtr = B.CreateGEP(Int8Ty, CI->getArgOperand(0), OffsetI32);

    CI->replaceAllUsesWith(ResultPtr);
    CI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

static bool lowerObjCCall(Function &F, const char *NewFn,
                          bool setNonLazyBind = false) {
  if (F.use_empty())
    return false;

  // If we haven't already looked up this function, check to see if the
  // program already contains a function with this name.
  Module *M = F.getParent();
  Constant* FCache = M->getOrInsertFunction(NewFn, F.getFunctionType());

  if (Function* Fn = dyn_cast<Function>(FCache)) {
    Fn->setLinkage(F.getLinkage());
    if (setNonLazyBind && !Fn->isWeakForLinker()) {
      // If we have Native ARC, set nonlazybind attribute for these APIs for
      // performance.
      Fn->addFnAttr(Attribute::NonLazyBind);
    }
  }

  for (auto I = F.use_begin(), E = F.use_end(); I != E;) {
    auto *CI = dyn_cast<CallInst>(I->getUser());
    assert(CI->getCalledFunction() && "Cannot lower an indirect call!");
    ++I;

    IRBuilder<> Builder(CI->getParent(), CI->getIterator());
    SmallVector<Value *, 8> Args(CI->arg_begin(), CI->arg_end());
    CallInst *NewCI = Builder.CreateCall(FCache, Args);
    NewCI->setName(CI->getName());
    NewCI->setTailCallKind(CI->getTailCallKind());
    if (!CI->use_empty())
      CI->replaceAllUsesWith(NewCI);
    CI->eraseFromParent();
  }

  return true;
}

static bool lowerIntrinsics(Module &M) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.getName().startswith("llvm.load.relative.")) {
      Changed |= lowerLoadRelative(F);
      continue;
    }
    switch (F.getIntrinsicID()) {
    default:
      break;
    case Intrinsic::objc_autorelease:
      Changed |= lowerObjCCall(F, "objc_autorelease");
      break;
    case Intrinsic::objc_autoreleasePoolPop:
      Changed |= lowerObjCCall(F, "objc_autoreleasePoolPop");
      break;
    case Intrinsic::objc_autoreleasePoolPush:
      Changed |= lowerObjCCall(F, "objc_autoreleasePoolPush");
      break;
    case Intrinsic::objc_autoreleaseReturnValue:
      Changed |= lowerObjCCall(F, "objc_autoreleaseReturnValue");
      break;
    case Intrinsic::objc_copyWeak:
      Changed |= lowerObjCCall(F, "objc_copyWeak");
      break;
    case Intrinsic::objc_destroyWeak:
      Changed |= lowerObjCCall(F, "objc_destroyWeak");
      break;
    case Intrinsic::objc_initWeak:
      Changed |= lowerObjCCall(F, "objc_initWeak");
      break;
    case Intrinsic::objc_loadWeak:
      Changed |= lowerObjCCall(F, "objc_loadWeak");
      break;
    case Intrinsic::objc_loadWeakRetained:
      Changed |= lowerObjCCall(F, "objc_loadWeakRetained");
      break;
    case Intrinsic::objc_moveWeak:
      Changed |= lowerObjCCall(F, "objc_moveWeak");
      break;
    case Intrinsic::objc_release:
      Changed |= lowerObjCCall(F, "objc_release", true);
      break;
    case Intrinsic::objc_retain:
      Changed |= lowerObjCCall(F, "objc_retain", true);
      break;
    case Intrinsic::objc_retainAutorelease:
      Changed |= lowerObjCCall(F, "objc_retainAutorelease");
      break;
    case Intrinsic::objc_retainAutoreleaseReturnValue:
      Changed |= lowerObjCCall(F, "objc_retainAutoreleaseReturnValue");
      break;
    case Intrinsic::objc_retainAutoreleasedReturnValue:
      Changed |= lowerObjCCall(F, "objc_retainAutoreleasedReturnValue");
      break;
    case Intrinsic::objc_retainBlock:
      Changed |= lowerObjCCall(F, "objc_retainBlock");
      break;
    case Intrinsic::objc_storeStrong:
      Changed |= lowerObjCCall(F, "objc_storeStrong");
      break;
    case Intrinsic::objc_storeWeak:
      Changed |= lowerObjCCall(F, "objc_storeWeak");
      break;
    case Intrinsic::objc_unsafeClaimAutoreleasedReturnValue:
      Changed |= lowerObjCCall(F, "objc_unsafeClaimAutoreleasedReturnValue");
      break;
    case Intrinsic::objc_retainedObject:
      Changed |= lowerObjCCall(F, "objc_retainedObject");
      break;
    case Intrinsic::objc_unretainedObject:
      Changed |= lowerObjCCall(F, "objc_unretainedObject");
      break;
    case Intrinsic::objc_unretainedPointer:
      Changed |= lowerObjCCall(F, "objc_unretainedPointer");
      break;
    case Intrinsic::objc_retain_autorelease:
      Changed |= lowerObjCCall(F, "objc_retain_autorelease");
      break;
    case Intrinsic::objc_sync_enter:
      Changed |= lowerObjCCall(F, "objc_sync_enter");
      break;
    case Intrinsic::objc_sync_exit:
      Changed |= lowerObjCCall(F, "objc_sync_exit");
      break;
    }
  }
  return Changed;
}

namespace {

class PreISelIntrinsicLoweringLegacyPass : public ModulePass {
public:
  static char ID;

  PreISelIntrinsicLoweringLegacyPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override { return lowerIntrinsics(M); }
};

} // end anonymous namespace

char PreISelIntrinsicLoweringLegacyPass::ID;

INITIALIZE_PASS(PreISelIntrinsicLoweringLegacyPass,
                "pre-isel-intrinsic-lowering", "Pre-ISel Intrinsic Lowering",
                false, false)

ModulePass *llvm::createPreISelIntrinsicLoweringPass() {
  return new PreISelIntrinsicLoweringLegacyPass;
}

PreservedAnalyses PreISelIntrinsicLoweringPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  if (!lowerIntrinsics(M))
    return PreservedAnalyses::all();
  else
    return PreservedAnalyses::none();
}
