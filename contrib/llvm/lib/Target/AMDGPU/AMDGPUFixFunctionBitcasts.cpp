//===-- AMDGPUFixFunctionBitcasts.cpp - Fix function bitcasts -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Promote indirect (bitcast) calls to direct calls when they are statically
/// known to be direct. Required when InstCombine is not run (e.g. at OptNone)
/// because AMDGPU does not support indirect calls.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Transforms/Utils/CallPromotionUtils.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-fix-function-bitcasts"

namespace {
class AMDGPUFixFunctionBitcasts final
    : public ModulePass,
      public InstVisitor<AMDGPUFixFunctionBitcasts> {

  bool runOnModule(Module &M) override;

  bool Modified;

public:
  void visitCallSite(CallSite CS) {
    if (CS.getCalledFunction())
      return;
    auto Callee = dyn_cast<Function>(CS.getCalledValue()->stripPointerCasts());
    if (Callee && isLegalToPromote(CS, Callee)) {
      promoteCall(CS, Callee);
      Modified = true;
    }
  }

  static char ID;
  AMDGPUFixFunctionBitcasts() : ModulePass(ID) {}
};
} // End anonymous namespace

char AMDGPUFixFunctionBitcasts::ID = 0;
char &llvm::AMDGPUFixFunctionBitcastsID = AMDGPUFixFunctionBitcasts::ID;
INITIALIZE_PASS(AMDGPUFixFunctionBitcasts, DEBUG_TYPE,
                "Fix function bitcasts for AMDGPU", false, false)

ModulePass *llvm::createAMDGPUFixFunctionBitcastsPass() {
  return new AMDGPUFixFunctionBitcasts();
}

bool AMDGPUFixFunctionBitcasts::runOnModule(Module &M) {
  Modified = false;
  visit(M);
  return Modified;
}
