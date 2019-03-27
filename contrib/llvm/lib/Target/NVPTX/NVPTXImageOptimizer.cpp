//===-- NVPTXImageOptimizer.cpp - Image optimization pass -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR-level optimizations of image access code,
// including:
//
// 1. Eliminate istypep intrinsics when image access qualifier is known
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "NVPTXUtilities.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace {
class NVPTXImageOptimizer : public FunctionPass {
private:
  static char ID;
  SmallVector<Instruction*, 4> InstrToDelete;

public:
  NVPTXImageOptimizer();

  bool runOnFunction(Function &F) override;

private:
  bool replaceIsTypePSampler(Instruction &I);
  bool replaceIsTypePSurface(Instruction &I);
  bool replaceIsTypePTexture(Instruction &I);
  Value *cleanupValue(Value *V);
  void replaceWith(Instruction *From, ConstantInt *To);
};
}

char NVPTXImageOptimizer::ID = 0;

NVPTXImageOptimizer::NVPTXImageOptimizer()
  : FunctionPass(ID) {}

bool NVPTXImageOptimizer::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  bool Changed = false;
  InstrToDelete.clear();

  // Look for call instructions in the function
  for (Function::iterator BI = F.begin(), BE = F.end(); BI != BE;
       ++BI) {
    for (BasicBlock::iterator I = (*BI).begin(), E = (*BI).end();
         I != E; ++I) {
      Instruction &Instr = *I;
      if (CallInst *CI = dyn_cast<CallInst>(I)) {
        Function *CalledF = CI->getCalledFunction();
        if (CalledF && CalledF->isIntrinsic()) {
          // This is an intrinsic function call, check if its an istypep
          switch (CalledF->getIntrinsicID()) {
          default: break;
          case Intrinsic::nvvm_istypep_sampler:
            Changed |= replaceIsTypePSampler(Instr);
            break;
          case Intrinsic::nvvm_istypep_surface:
            Changed |= replaceIsTypePSurface(Instr);
            break;
          case Intrinsic::nvvm_istypep_texture:
            Changed |= replaceIsTypePTexture(Instr);
            break;
          }
        }
      }
    }
  }

  // Delete any istypep instances we replaced in the IR
  for (unsigned i = 0, e = InstrToDelete.size(); i != e; ++i)
    InstrToDelete[i]->eraseFromParent();

  return Changed;
}

bool NVPTXImageOptimizer::replaceIsTypePSampler(Instruction &I) {
  Value *TexHandle = cleanupValue(I.getOperand(0));
  if (isSampler(*TexHandle)) {
    // This is an OpenCL sampler, so it must be a samplerref
    replaceWith(&I, ConstantInt::getTrue(I.getContext()));
    return true;
  } else if (isImage(*TexHandle)) {
    // This is an OpenCL image, so it cannot be a samplerref
    replaceWith(&I, ConstantInt::getFalse(I.getContext()));
    return true;
  } else {
    // The image type is unknown, so we cannot eliminate the intrinsic
    return false;
  }
}

bool NVPTXImageOptimizer::replaceIsTypePSurface(Instruction &I) {
  Value *TexHandle = cleanupValue(I.getOperand(0));
  if (isImageReadWrite(*TexHandle) ||
      isImageWriteOnly(*TexHandle)) {
    // This is an OpenCL read-only/read-write image, so it must be a surfref
    replaceWith(&I, ConstantInt::getTrue(I.getContext()));
    return true;
  } else if (isImageReadOnly(*TexHandle) ||
             isSampler(*TexHandle)) {
    // This is an OpenCL read-only/ imageor sampler, so it cannot be
    // a surfref
    replaceWith(&I, ConstantInt::getFalse(I.getContext()));
    return true;
  } else {
    // The image type is unknown, so we cannot eliminate the intrinsic
    return false;
  }
}

bool NVPTXImageOptimizer::replaceIsTypePTexture(Instruction &I) {
  Value *TexHandle = cleanupValue(I.getOperand(0));
  if (isImageReadOnly(*TexHandle)) {
    // This is an OpenCL read-only image, so it must be a texref
    replaceWith(&I, ConstantInt::getTrue(I.getContext()));
    return true;
  } else if (isImageWriteOnly(*TexHandle) ||
             isImageReadWrite(*TexHandle) ||
             isSampler(*TexHandle)) {
    // This is an OpenCL read-write/write-only image or a sampler, so it
    // cannot be a texref
    replaceWith(&I, ConstantInt::getFalse(I.getContext()));
    return true;
  } else {
    // The image type is unknown, so we cannot eliminate the intrinsic
    return false;
  }
}

void NVPTXImageOptimizer::replaceWith(Instruction *From, ConstantInt *To) {
  // We implement "poor man's DCE" here to make sure any code that is no longer
  // live is actually unreachable and can be trivially eliminated by the
  // unreachable block elimination pass.
  for (CallInst::use_iterator UI = From->use_begin(), UE = From->use_end();
       UI != UE; ++UI) {
    if (BranchInst *BI = dyn_cast<BranchInst>(*UI)) {
      if (BI->isUnconditional()) continue;
      BasicBlock *Dest;
      if (To->isZero())
        // Get false block
        Dest = BI->getSuccessor(1);
      else
        // Get true block
        Dest = BI->getSuccessor(0);
      BranchInst::Create(Dest, BI);
      InstrToDelete.push_back(BI);
    }
  }
  From->replaceAllUsesWith(To);
  InstrToDelete.push_back(From);
}

Value *NVPTXImageOptimizer::cleanupValue(Value *V) {
  if (ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(V)) {
    return cleanupValue(EVI->getAggregateOperand());
  }
  return V;
}

FunctionPass *llvm::createNVPTXImageOptimizerPass() {
  return new NVPTXImageOptimizer();
}
