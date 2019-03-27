//===-- CrossDSOCFI.cpp - Externalize this module's CFI checks ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass exports all llvm.bitset's found in the module in the form of a
// __cfi_check function, which can be used to verify cross-DSO call targets.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"

using namespace llvm;

#define DEBUG_TYPE "cross-dso-cfi"

STATISTIC(NumTypeIds, "Number of unique type identifiers");

namespace {

struct CrossDSOCFI : public ModulePass {
  static char ID;
  CrossDSOCFI() : ModulePass(ID) {
    initializeCrossDSOCFIPass(*PassRegistry::getPassRegistry());
  }

  MDNode *VeryLikelyWeights;

  ConstantInt *extractNumericTypeId(MDNode *MD);
  void buildCFICheck(Module &M);
  bool runOnModule(Module &M) override;
};

} // anonymous namespace

INITIALIZE_PASS_BEGIN(CrossDSOCFI, "cross-dso-cfi", "Cross-DSO CFI", false,
                      false)
INITIALIZE_PASS_END(CrossDSOCFI, "cross-dso-cfi", "Cross-DSO CFI", false, false)
char CrossDSOCFI::ID = 0;

ModulePass *llvm::createCrossDSOCFIPass() { return new CrossDSOCFI; }

/// Extracts a numeric type identifier from an MDNode containing type metadata.
ConstantInt *CrossDSOCFI::extractNumericTypeId(MDNode *MD) {
  // This check excludes vtables for classes inside anonymous namespaces.
  auto TM = dyn_cast<ValueAsMetadata>(MD->getOperand(1));
  if (!TM)
    return nullptr;
  auto C = dyn_cast_or_null<ConstantInt>(TM->getValue());
  if (!C) return nullptr;
  // We are looking for i64 constants.
  if (C->getBitWidth() != 64) return nullptr;

  return C;
}

/// buildCFICheck - emits __cfi_check for the current module.
void CrossDSOCFI::buildCFICheck(Module &M) {
  // FIXME: verify that __cfi_check ends up near the end of the code section,
  // but before the jump slots created in LowerTypeTests.
  SetVector<uint64_t> TypeIds;
  SmallVector<MDNode *, 2> Types;
  for (GlobalObject &GO : M.global_objects()) {
    Types.clear();
    GO.getMetadata(LLVMContext::MD_type, Types);
    for (MDNode *Type : Types) {
      // Sanity check. GO must not be a function declaration.
      assert(!isa<Function>(&GO) || !cast<Function>(&GO)->isDeclaration());

      if (ConstantInt *TypeId = extractNumericTypeId(Type))
        TypeIds.insert(TypeId->getZExtValue());
    }
  }

  NamedMDNode *CfiFunctionsMD = M.getNamedMetadata("cfi.functions");
  if (CfiFunctionsMD) {
    for (auto Func : CfiFunctionsMD->operands()) {
      assert(Func->getNumOperands() >= 2);
      for (unsigned I = 2; I < Func->getNumOperands(); ++I)
        if (ConstantInt *TypeId =
                extractNumericTypeId(cast<MDNode>(Func->getOperand(I).get())))
          TypeIds.insert(TypeId->getZExtValue());
    }
  }

  LLVMContext &Ctx = M.getContext();
  Constant *C = M.getOrInsertFunction(
      "__cfi_check", Type::getVoidTy(Ctx), Type::getInt64Ty(Ctx),
      Type::getInt8PtrTy(Ctx), Type::getInt8PtrTy(Ctx));
  Function *F = dyn_cast<Function>(C);
  // Take over the existing function. The frontend emits a weak stub so that the
  // linker knows about the symbol; this pass replaces the function body.
  F->deleteBody();
  F->setAlignment(4096);

  Triple T(M.getTargetTriple());
  if (T.isARM() || T.isThumb())
    F->addFnAttr("target-features", "+thumb-mode");

  auto args = F->arg_begin();
  Value &CallSiteTypeId = *(args++);
  CallSiteTypeId.setName("CallSiteTypeId");
  Value &Addr = *(args++);
  Addr.setName("Addr");
  Value &CFICheckFailData = *(args++);
  CFICheckFailData.setName("CFICheckFailData");
  assert(args == F->arg_end());

  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "exit", F);

  BasicBlock *TrapBB = BasicBlock::Create(Ctx, "fail", F);
  IRBuilder<> IRBFail(TrapBB);
  Constant *CFICheckFailFn = M.getOrInsertFunction(
      "__cfi_check_fail", Type::getVoidTy(Ctx), Type::getInt8PtrTy(Ctx),
      Type::getInt8PtrTy(Ctx));
  IRBFail.CreateCall(CFICheckFailFn, {&CFICheckFailData, &Addr});
  IRBFail.CreateBr(ExitBB);

  IRBuilder<> IRBExit(ExitBB);
  IRBExit.CreateRetVoid();

  IRBuilder<> IRB(BB);
  SwitchInst *SI = IRB.CreateSwitch(&CallSiteTypeId, TrapBB, TypeIds.size());
  for (uint64_t TypeId : TypeIds) {
    ConstantInt *CaseTypeId = ConstantInt::get(Type::getInt64Ty(Ctx), TypeId);
    BasicBlock *TestBB = BasicBlock::Create(Ctx, "test", F);
    IRBuilder<> IRBTest(TestBB);
    Function *BitsetTestFn = Intrinsic::getDeclaration(&M, Intrinsic::type_test);

    Value *Test = IRBTest.CreateCall(
        BitsetTestFn, {&Addr, MetadataAsValue::get(
                                  Ctx, ConstantAsMetadata::get(CaseTypeId))});
    BranchInst *BI = IRBTest.CreateCondBr(Test, ExitBB, TrapBB);
    BI->setMetadata(LLVMContext::MD_prof, VeryLikelyWeights);

    SI->addCase(CaseTypeId, TestBB);
    ++NumTypeIds;
  }
}

bool CrossDSOCFI::runOnModule(Module &M) {
  VeryLikelyWeights =
    MDBuilder(M.getContext()).createBranchWeights((1U << 20) - 1, 1);
  if (M.getModuleFlag("Cross-DSO CFI") == nullptr)
    return false;
  buildCFICheck(M);
  return true;
}

PreservedAnalyses CrossDSOCFIPass::run(Module &M, ModuleAnalysisManager &AM) {
  CrossDSOCFI Impl;
  bool Changed = Impl.runOnModule(M);
  if (!Changed)
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
