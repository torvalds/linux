//===-- AMDGPULowerIntrinsics.cpp -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

#define DEBUG_TYPE "amdgpu-lower-intrinsics"

using namespace llvm;

namespace {

const unsigned MaxStaticSize = 1024;

class AMDGPULowerIntrinsics : public ModulePass {
private:
  bool makeLIDRangeMetadata(Function &F) const;

public:
  static char ID;

  AMDGPULowerIntrinsics() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
  bool expandMemIntrinsicUses(Function &F);
  StringRef getPassName() const override {
    return "AMDGPU Lower Intrinsics";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }
};

}

char AMDGPULowerIntrinsics::ID = 0;

char &llvm::AMDGPULowerIntrinsicsID = AMDGPULowerIntrinsics::ID;

INITIALIZE_PASS(AMDGPULowerIntrinsics, DEBUG_TYPE, "Lower intrinsics", false,
                false)

// TODO: Should refine based on estimated number of accesses (e.g. does it
// require splitting based on alignment)
static bool shouldExpandOperationWithSize(Value *Size) {
  ConstantInt *CI = dyn_cast<ConstantInt>(Size);
  return !CI || (CI->getZExtValue() > MaxStaticSize);
}

bool AMDGPULowerIntrinsics::expandMemIntrinsicUses(Function &F) {
  Intrinsic::ID ID = F.getIntrinsicID();
  bool Changed = false;

  for (auto I = F.user_begin(), E = F.user_end(); I != E;) {
    Instruction *Inst = cast<Instruction>(*I);
    ++I;

    switch (ID) {
    case Intrinsic::memcpy: {
      auto *Memcpy = cast<MemCpyInst>(Inst);
      if (shouldExpandOperationWithSize(Memcpy->getLength())) {
        Function *ParentFunc = Memcpy->getParent()->getParent();
        const TargetTransformInfo &TTI =
            getAnalysis<TargetTransformInfoWrapperPass>().getTTI(*ParentFunc);
        expandMemCpyAsLoop(Memcpy, TTI);
        Changed = true;
        Memcpy->eraseFromParent();
      }

      break;
    }
    case Intrinsic::memmove: {
      auto *Memmove = cast<MemMoveInst>(Inst);
      if (shouldExpandOperationWithSize(Memmove->getLength())) {
        expandMemMoveAsLoop(Memmove);
        Changed = true;
        Memmove->eraseFromParent();
      }

      break;
    }
    case Intrinsic::memset: {
      auto *Memset = cast<MemSetInst>(Inst);
      if (shouldExpandOperationWithSize(Memset->getLength())) {
        expandMemSetAsLoop(Memset);
        Changed = true;
        Memset->eraseFromParent();
      }

      break;
    }
    default:
      break;
    }
  }

  return Changed;
}

bool AMDGPULowerIntrinsics::makeLIDRangeMetadata(Function &F) const {
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  const TargetMachine &TM = TPC->getTM<TargetMachine>();
  bool Changed = false;

  for (auto *U : F.users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      continue;

    Changed |= AMDGPUSubtarget::get(TM, F).makeLIDRangeMetadata(CI);
  }
  return Changed;
}

bool AMDGPULowerIntrinsics::runOnModule(Module &M) {
  bool Changed = false;

  for (Function &F : M) {
    if (!F.isDeclaration())
      continue;

    switch (F.getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset:
      if (expandMemIntrinsicUses(F))
        Changed = true;
      break;

    case Intrinsic::amdgcn_workitem_id_x:
    case Intrinsic::r600_read_tidig_x:
    case Intrinsic::amdgcn_workitem_id_y:
    case Intrinsic::r600_read_tidig_y:
    case Intrinsic::amdgcn_workitem_id_z:
    case Intrinsic::r600_read_tidig_z:
    case Intrinsic::r600_read_local_size_x:
    case Intrinsic::r600_read_local_size_y:
    case Intrinsic::r600_read_local_size_z:
      Changed |= makeLIDRangeMetadata(F);
      break;

    default:
      break;
    }
  }

  return Changed;
}

ModulePass *llvm::createAMDGPULowerIntrinsicsPass() {
  return new AMDGPULowerIntrinsics();
}
