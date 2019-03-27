//===- CoroEarly.cpp - Coroutine Early Function Pass ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass lowers coroutine intrinsics that hide the details of the exact
// calling convention for coroutine resume and destroy functions and details of
// the structure of the coroutine frame.
//===----------------------------------------------------------------------===//

#include "CoroInternal.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "coro-early"

namespace {
// Created on demand if CoroEarly pass has work to do.
class Lowerer : public coro::LowererBase {
  IRBuilder<> Builder;
  PointerType *const AnyResumeFnPtrTy;
  Constant *NoopCoro = nullptr;

  void lowerResumeOrDestroy(CallSite CS, CoroSubFnInst::ResumeKind);
  void lowerCoroPromise(CoroPromiseInst *Intrin);
  void lowerCoroDone(IntrinsicInst *II);
  void lowerCoroNoop(IntrinsicInst *II);

public:
  Lowerer(Module &M)
      : LowererBase(M), Builder(Context),
        AnyResumeFnPtrTy(FunctionType::get(Type::getVoidTy(Context), Int8Ptr,
                                           /*isVarArg=*/false)
                             ->getPointerTo()) {}
  bool lowerEarlyIntrinsics(Function &F);
};
}

// Replace a direct call to coro.resume or coro.destroy with an indirect call to
// an address returned by coro.subfn.addr intrinsic. This is done so that
// CGPassManager recognizes devirtualization when CoroElide pass replaces a call
// to coro.subfn.addr with an appropriate function address.
void Lowerer::lowerResumeOrDestroy(CallSite CS,
                                   CoroSubFnInst::ResumeKind Index) {
  Value *ResumeAddr =
      makeSubFnCall(CS.getArgOperand(0), Index, CS.getInstruction());
  CS.setCalledFunction(ResumeAddr);
  CS.setCallingConv(CallingConv::Fast);
}

// Coroutine promise field is always at the fixed offset from the beginning of
// the coroutine frame. i8* coro.promise(i8*, i1 from) intrinsic adds an offset
// to a passed pointer to move from coroutine frame to coroutine promise and
// vice versa. Since we don't know exactly which coroutine frame it is, we build
// a coroutine frame mock up starting with two function pointers, followed by a
// properly aligned coroutine promise field.
// TODO: Handle the case when coroutine promise alloca has align override.
void Lowerer::lowerCoroPromise(CoroPromiseInst *Intrin) {
  Value *Operand = Intrin->getArgOperand(0);
  unsigned Alignement = Intrin->getAlignment();
  Type *Int8Ty = Builder.getInt8Ty();

  auto *SampleStruct =
      StructType::get(Context, {AnyResumeFnPtrTy, AnyResumeFnPtrTy, Int8Ty});
  const DataLayout &DL = TheModule.getDataLayout();
  int64_t Offset = alignTo(
      DL.getStructLayout(SampleStruct)->getElementOffset(2), Alignement);
  if (Intrin->isFromPromise())
    Offset = -Offset;

  Builder.SetInsertPoint(Intrin);
  Value *Replacement =
      Builder.CreateConstInBoundsGEP1_32(Int8Ty, Operand, Offset);

  Intrin->replaceAllUsesWith(Replacement);
  Intrin->eraseFromParent();
}

// When a coroutine reaches final suspend point, it zeros out ResumeFnAddr in
// the coroutine frame (it is UB to resume from a final suspend point).
// The llvm.coro.done intrinsic is used to check whether a coroutine is
// suspended at the final suspend point or not.
void Lowerer::lowerCoroDone(IntrinsicInst *II) {
  Value *Operand = II->getArgOperand(0);

  // ResumeFnAddr is the first pointer sized element of the coroutine frame.
  auto *FrameTy = Int8Ptr;
  PointerType *FramePtrTy = FrameTy->getPointerTo();

  Builder.SetInsertPoint(II);
  auto *BCI = Builder.CreateBitCast(Operand, FramePtrTy);
  auto *Gep = Builder.CreateConstInBoundsGEP1_32(FrameTy, BCI, 0);
  auto *Load = Builder.CreateLoad(Gep);
  auto *Cond = Builder.CreateICmpEQ(Load, NullPtr);

  II->replaceAllUsesWith(Cond);
  II->eraseFromParent();
}

void Lowerer::lowerCoroNoop(IntrinsicInst *II) {
  if (!NoopCoro) {
    LLVMContext &C = Builder.getContext();
    Module &M = *II->getModule();

    // Create a noop.frame struct type.
    StructType *FrameTy = StructType::create(C, "NoopCoro.Frame");
    auto *FramePtrTy = FrameTy->getPointerTo();
    auto *FnTy = FunctionType::get(Type::getVoidTy(C), FramePtrTy,
                                   /*IsVarArgs=*/false);
    auto *FnPtrTy = FnTy->getPointerTo();
    FrameTy->setBody({FnPtrTy, FnPtrTy});

    // Create a Noop function that does nothing.
    Function *NoopFn =
        Function::Create(FnTy, GlobalValue::LinkageTypes::PrivateLinkage,
                         "NoopCoro.ResumeDestroy", &M);
    NoopFn->setCallingConv(CallingConv::Fast);
    auto *Entry = BasicBlock::Create(C, "entry", NoopFn);
    ReturnInst::Create(C, Entry);

    // Create a constant struct for the frame.
    Constant* Values[] = {NoopFn, NoopFn};
    Constant* NoopCoroConst = ConstantStruct::get(FrameTy, Values);
    NoopCoro = new GlobalVariable(M, NoopCoroConst->getType(), /*isConstant=*/true,
                                GlobalVariable::PrivateLinkage, NoopCoroConst,
                                "NoopCoro.Frame.Const");
  }

  Builder.SetInsertPoint(II);
  auto *NoopCoroVoidPtr = Builder.CreateBitCast(NoopCoro, Int8Ptr);
  II->replaceAllUsesWith(NoopCoroVoidPtr);
  II->eraseFromParent();
}

// Prior to CoroSplit, calls to coro.begin needs to be marked as NoDuplicate,
// as CoroSplit assumes there is exactly one coro.begin. After CoroSplit,
// NoDuplicate attribute will be removed from coro.begin otherwise, it will
// interfere with inlining.
static void setCannotDuplicate(CoroIdInst *CoroId) {
  for (User *U : CoroId->users())
    if (auto *CB = dyn_cast<CoroBeginInst>(U))
      CB->setCannotDuplicate();
}

bool Lowerer::lowerEarlyIntrinsics(Function &F) {
  bool Changed = false;
  CoroIdInst *CoroId = nullptr;
  SmallVector<CoroFreeInst *, 4> CoroFrees;
  for (auto IB = inst_begin(F), IE = inst_end(F); IB != IE;) {
    Instruction &I = *IB++;
    if (auto CS = CallSite(&I)) {
      switch (CS.getIntrinsicID()) {
      default:
        continue;
      case Intrinsic::coro_free:
        CoroFrees.push_back(cast<CoroFreeInst>(&I));
        break;
      case Intrinsic::coro_suspend:
        // Make sure that final suspend point is not duplicated as CoroSplit
        // pass expects that there is at most one final suspend point.
        if (cast<CoroSuspendInst>(&I)->isFinal())
          CS.setCannotDuplicate();
        break;
      case Intrinsic::coro_end:
        // Make sure that fallthrough coro.end is not duplicated as CoroSplit
        // pass expects that there is at most one fallthrough coro.end.
        if (cast<CoroEndInst>(&I)->isFallthrough())
          CS.setCannotDuplicate();
        break;
      case Intrinsic::coro_noop:
        lowerCoroNoop(cast<IntrinsicInst>(&I));
        break;
      case Intrinsic::coro_id:
        // Mark a function that comes out of the frontend that has a coro.id
        // with a coroutine attribute.
        if (auto *CII = cast<CoroIdInst>(&I)) {
          if (CII->getInfo().isPreSplit()) {
            F.addFnAttr(CORO_PRESPLIT_ATTR, UNPREPARED_FOR_SPLIT);
            setCannotDuplicate(CII);
            CII->setCoroutineSelf();
            CoroId = cast<CoroIdInst>(&I);
          }
        }
        break;
      case Intrinsic::coro_resume:
        lowerResumeOrDestroy(CS, CoroSubFnInst::ResumeIndex);
        break;
      case Intrinsic::coro_destroy:
        lowerResumeOrDestroy(CS, CoroSubFnInst::DestroyIndex);
        break;
      case Intrinsic::coro_promise:
        lowerCoroPromise(cast<CoroPromiseInst>(&I));
        break;
      case Intrinsic::coro_done:
        lowerCoroDone(cast<IntrinsicInst>(&I));
        break;
      }
      Changed = true;
    }
  }
  // Make sure that all CoroFree reference the coro.id intrinsic.
  // Token type is not exposed through coroutine C/C++ builtins to plain C, so
  // we allow specifying none and fixing it up here.
  if (CoroId)
    for (CoroFreeInst *CF : CoroFrees)
      CF->setArgOperand(0, CoroId);
  return Changed;
}

//===----------------------------------------------------------------------===//
//                              Top Level Driver
//===----------------------------------------------------------------------===//

namespace {

struct CoroEarly : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid.
  CoroEarly() : FunctionPass(ID) {
    initializeCoroEarlyPass(*PassRegistry::getPassRegistry());
  }

  std::unique_ptr<Lowerer> L;

  // This pass has work to do only if we find intrinsics we are going to lower
  // in the module.
  bool doInitialization(Module &M) override {
    if (coro::declaresIntrinsics(
            M, {"llvm.coro.id", "llvm.coro.destroy", "llvm.coro.done",
                "llvm.coro.end", "llvm.coro.noop", "llvm.coro.free",
                "llvm.coro.promise", "llvm.coro.resume", "llvm.coro.suspend"}))
      L = llvm::make_unique<Lowerer>(M);
    return false;
  }

  bool runOnFunction(Function &F) override {
    if (!L)
      return false;

    return L->lowerEarlyIntrinsics(F);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
  StringRef getPassName() const override {
    return "Lower early coroutine intrinsics";
  }
};
}

char CoroEarly::ID = 0;
INITIALIZE_PASS(CoroEarly, "coro-early", "Lower early coroutine intrinsics",
                false, false)

Pass *llvm::createCoroEarlyPass() { return new CoroEarly(); }
