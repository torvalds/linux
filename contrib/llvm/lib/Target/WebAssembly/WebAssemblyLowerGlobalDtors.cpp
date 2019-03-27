//===-- WebAssemblyLowerGlobalDtors.cpp - Lower @llvm.global_dtors --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lower @llvm.global_dtors.
///
/// WebAssembly doesn't have a builtin way to invoke static destructors.
/// Implement @llvm.global_dtors by creating wrapper functions that are
/// registered in @llvm.global_ctors and which contain a call to
/// `__cxa_atexit` to register their destructor functions.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-lower-global-dtors"

namespace {
class LowerGlobalDtors final : public ModulePass {
  StringRef getPassName() const override {
    return "WebAssembly Lower @llvm.global_dtors";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  LowerGlobalDtors() : ModulePass(ID) {}
};
} // End anonymous namespace

char LowerGlobalDtors::ID = 0;
INITIALIZE_PASS(LowerGlobalDtors, DEBUG_TYPE,
                "Lower @llvm.global_dtors for WebAssembly", false, false)

ModulePass *llvm::createWebAssemblyLowerGlobalDtors() {
  return new LowerGlobalDtors();
}

bool LowerGlobalDtors::runOnModule(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Lower Global Destructors **********\n");

  GlobalVariable *GV = M.getGlobalVariable("llvm.global_dtors");
  if (!GV)
    return false;

  const ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList)
    return false;

  // Sanity-check @llvm.global_dtor's type.
  StructType *ETy = dyn_cast<StructType>(InitList->getType()->getElementType());
  if (!ETy || ETy->getNumElements() != 3 ||
      !ETy->getTypeAtIndex(0U)->isIntegerTy() ||
      !ETy->getTypeAtIndex(1U)->isPointerTy() ||
      !ETy->getTypeAtIndex(2U)->isPointerTy())
    return false; // Not (int, ptr, ptr).

  // Collect the contents of @llvm.global_dtors, collated by priority and
  // associated symbol.
  std::map<uint16_t, MapVector<Constant *, std::vector<Constant *>>> DtorFuncs;
  for (Value *O : InitList->operands()) {
    ConstantStruct *CS = dyn_cast<ConstantStruct>(O);
    if (!CS)
      continue; // Malformed.

    ConstantInt *Priority = dyn_cast<ConstantInt>(CS->getOperand(0));
    if (!Priority)
      continue; // Malformed.
    uint16_t PriorityValue = Priority->getLimitedValue(UINT16_MAX);

    Constant *DtorFunc = CS->getOperand(1);
    if (DtorFunc->isNullValue())
      break; // Found a null terminator, skip the rest.

    Constant *Associated = CS->getOperand(2);
    Associated = cast<Constant>(Associated->stripPointerCastsNoFollowAliases());

    DtorFuncs[PriorityValue][Associated].push_back(DtorFunc);
  }
  if (DtorFuncs.empty())
    return false;

  // extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d);
  LLVMContext &C = M.getContext();
  PointerType *VoidStar = Type::getInt8PtrTy(C);
  Type *AtExitFuncArgs[] = {VoidStar};
  FunctionType *AtExitFuncTy =
      FunctionType::get(Type::getVoidTy(C), AtExitFuncArgs,
                        /*isVarArg=*/false);

  Type *AtExitArgs[] = {PointerType::get(AtExitFuncTy, 0), VoidStar, VoidStar};
  FunctionType *AtExitTy = FunctionType::get(Type::getInt32Ty(C), AtExitArgs,
                                             /*isVarArg=*/false);
  Constant *AtExit = M.getOrInsertFunction("__cxa_atexit", AtExitTy);

  // Declare __dso_local.
  Constant *DsoHandle = M.getNamedValue("__dso_handle");
  if (!DsoHandle) {
    Type *DsoHandleTy = Type::getInt8Ty(C);
    GlobalVariable *Handle = new GlobalVariable(
        M, DsoHandleTy, /*isConstant=*/true,
        GlobalVariable::ExternalWeakLinkage, nullptr, "__dso_handle");
    Handle->setVisibility(GlobalVariable::HiddenVisibility);
    DsoHandle = Handle;
  }

  // For each unique priority level and associated symbol, generate a function
  // to call all the destructors at that level, and a function to register the
  // first function with __cxa_atexit.
  for (auto &PriorityAndMore : DtorFuncs) {
    uint16_t Priority = PriorityAndMore.first;
    for (auto &AssociatedAndMore : PriorityAndMore.second) {
      Constant *Associated = AssociatedAndMore.first;

      Function *CallDtors = Function::Create(
          AtExitFuncTy, Function::PrivateLinkage,
          "call_dtors" +
              (Priority != UINT16_MAX ? (Twine(".") + Twine(Priority))
                                      : Twine()) +
              (!Associated->isNullValue() ? (Twine(".") + Associated->getName())
                                          : Twine()),
          &M);
      BasicBlock *BB = BasicBlock::Create(C, "body", CallDtors);

      for (auto Dtor : AssociatedAndMore.second)
        CallInst::Create(Dtor, "", BB);
      ReturnInst::Create(C, BB);

      FunctionType *VoidVoid = FunctionType::get(Type::getVoidTy(C),
                                                 /*isVarArg=*/false);
      Function *RegisterCallDtors = Function::Create(
          VoidVoid, Function::PrivateLinkage,
          "register_call_dtors" +
              (Priority != UINT16_MAX ? (Twine(".") + Twine(Priority))
                                      : Twine()) +
              (!Associated->isNullValue() ? (Twine(".") + Associated->getName())
                                          : Twine()),
          &M);
      BasicBlock *EntryBB = BasicBlock::Create(C, "entry", RegisterCallDtors);
      BasicBlock *FailBB = BasicBlock::Create(C, "fail", RegisterCallDtors);
      BasicBlock *RetBB = BasicBlock::Create(C, "return", RegisterCallDtors);

      Value *Null = ConstantPointerNull::get(VoidStar);
      Value *Args[] = {CallDtors, Null, DsoHandle};
      Value *Res = CallInst::Create(AtExit, Args, "call", EntryBB);
      Value *Cmp = new ICmpInst(*EntryBB, ICmpInst::ICMP_NE, Res,
                                Constant::getNullValue(Res->getType()));
      BranchInst::Create(FailBB, RetBB, Cmp, EntryBB);

      // If `__cxa_atexit` hits out-of-memory, trap, so that we don't misbehave.
      // This should be very rare, because if the process is running out of
      // memory before main has even started, something is wrong.
      CallInst::Create(Intrinsic::getDeclaration(&M, Intrinsic::trap), "",
                       FailBB);
      new UnreachableInst(C, FailBB);

      ReturnInst::Create(C, RetBB);

      // Now register the registration function with @llvm.global_ctors.
      appendToGlobalCtors(M, RegisterCallDtors, Priority, Associated);
    }
  }

  // Now that we've lowered everything, remove @llvm.global_dtors.
  GV->eraseFromParent();

  return true;
}
