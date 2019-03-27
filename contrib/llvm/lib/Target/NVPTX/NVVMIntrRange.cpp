//===- NVVMIntrRange.cpp - Set !range metadata for NVVM intrinsics --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass adds appropriate !range metadata for calls to NVVM
// intrinsics that return a limited range of values.
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;

#define DEBUG_TYPE "nvvm-intr-range"

namespace llvm { void initializeNVVMIntrRangePass(PassRegistry &); }

// Add !range metadata based on limits of given SM variant.
static cl::opt<unsigned> NVVMIntrRangeSM("nvvm-intr-range-sm", cl::init(20),
                                         cl::Hidden, cl::desc("SM variant"));

namespace {
class NVVMIntrRange : public FunctionPass {
 private:
   struct {
     unsigned x, y, z;
   } MaxBlockSize, MaxGridSize;

 public:
   static char ID;
   NVVMIntrRange() : NVVMIntrRange(NVVMIntrRangeSM) {}
   NVVMIntrRange(unsigned int SmVersion) : FunctionPass(ID) {
     MaxBlockSize.x = 1024;
     MaxBlockSize.y = 1024;
     MaxBlockSize.z = 64;

     MaxGridSize.x = SmVersion >= 30 ? 0x7fffffff : 0xffff;
     MaxGridSize.y = 0xffff;
     MaxGridSize.z = 0xffff;

     initializeNVVMIntrRangePass(*PassRegistry::getPassRegistry());
   }

   bool runOnFunction(Function &) override;
};
}

FunctionPass *llvm::createNVVMIntrRangePass(unsigned int SmVersion) {
  return new NVVMIntrRange(SmVersion);
}

char NVVMIntrRange::ID = 0;
INITIALIZE_PASS(NVVMIntrRange, "nvvm-intr-range",
                "Add !range metadata to NVVM intrinsics.", false, false)

// Adds the passed-in [Low,High) range information as metadata to the
// passed-in call instruction.
static bool addRangeMetadata(uint64_t Low, uint64_t High, CallInst *C) {
  // This call already has range metadata, nothing to do.
  if (C->getMetadata(LLVMContext::MD_range))
    return false;

  LLVMContext &Context = C->getParent()->getContext();
  IntegerType *Int32Ty = Type::getInt32Ty(Context);
  Metadata *LowAndHigh[] = {
      ConstantAsMetadata::get(ConstantInt::get(Int32Ty, Low)),
      ConstantAsMetadata::get(ConstantInt::get(Int32Ty, High))};
  C->setMetadata(LLVMContext::MD_range, MDNode::get(Context, LowAndHigh));
  return true;
}

bool NVVMIntrRange::runOnFunction(Function &F) {
  // Go through the calls in this function.
  bool Changed = false;
  for (Instruction &I : instructions(F)) {
    CallInst *Call = dyn_cast<CallInst>(&I);
    if (!Call)
      continue;

    if (Function *Callee = Call->getCalledFunction()) {
      switch (Callee->getIntrinsicID()) {
      // Index within block
      case Intrinsic::nvvm_read_ptx_sreg_tid_x:
        Changed |= addRangeMetadata(0, MaxBlockSize.x, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_tid_y:
        Changed |= addRangeMetadata(0, MaxBlockSize.y, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_tid_z:
        Changed |= addRangeMetadata(0, MaxBlockSize.z, Call);
        break;

      // Block size
      case Intrinsic::nvvm_read_ptx_sreg_ntid_x:
        Changed |= addRangeMetadata(1, MaxBlockSize.x+1, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_ntid_y:
        Changed |= addRangeMetadata(1, MaxBlockSize.y+1, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_ntid_z:
        Changed |= addRangeMetadata(1, MaxBlockSize.z+1, Call);
        break;

      // Index within grid
      case Intrinsic::nvvm_read_ptx_sreg_ctaid_x:
        Changed |= addRangeMetadata(0, MaxGridSize.x, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_ctaid_y:
        Changed |= addRangeMetadata(0, MaxGridSize.y, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_ctaid_z:
        Changed |= addRangeMetadata(0, MaxGridSize.z, Call);
        break;

      // Grid size
      case Intrinsic::nvvm_read_ptx_sreg_nctaid_x:
        Changed |= addRangeMetadata(1, MaxGridSize.x+1, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_nctaid_y:
        Changed |= addRangeMetadata(1, MaxGridSize.y+1, Call);
        break;
      case Intrinsic::nvvm_read_ptx_sreg_nctaid_z:
        Changed |= addRangeMetadata(1, MaxGridSize.z+1, Call);
        break;

      // warp size is constant 32.
      case Intrinsic::nvvm_read_ptx_sreg_warpsize:
        Changed |= addRangeMetadata(32, 32+1, Call);
        break;

      // Lane ID is [0..warpsize)
      case Intrinsic::nvvm_read_ptx_sreg_laneid:
        Changed |= addRangeMetadata(0, 32, Call);
        break;

      default:
        break;
      }
    }
  }

  return Changed;
}
