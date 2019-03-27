//===-- Instrumentation.cpp - TransformUtils Infrastructure ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the common initialization infrastructure for the
// Instrumentation library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation.h"
#include "llvm-c/Initialization.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"

using namespace llvm;

/// Moves I before IP. Returns new insert point.
static BasicBlock::iterator moveBeforeInsertPoint(BasicBlock::iterator I, BasicBlock::iterator IP) {
  // If I is IP, move the insert point down.
  if (I == IP)
    return ++IP;
  // Otherwise, move I before IP and return IP.
  I->moveBefore(&*IP);
  return IP;
}

/// Instrumentation passes often insert conditional checks into entry blocks.
/// Call this function before splitting the entry block to move instructions
/// that must remain in the entry block up before the split point. Static
/// allocas and llvm.localescape calls, for example, must remain in the entry
/// block.
BasicBlock::iterator llvm::PrepareToSplitEntryBlock(BasicBlock &BB,
                                                    BasicBlock::iterator IP) {
  assert(&BB.getParent()->getEntryBlock() == &BB);
  for (auto I = IP, E = BB.end(); I != E; ++I) {
    bool KeepInEntry = false;
    if (auto *AI = dyn_cast<AllocaInst>(I)) {
      if (AI->isStaticAlloca())
        KeepInEntry = true;
    } else if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if (II->getIntrinsicID() == llvm::Intrinsic::localescape)
        KeepInEntry = true;
    }
    if (KeepInEntry)
      IP = moveBeforeInsertPoint(I, IP);
  }
  return IP;
}

// Create a constant for Str so that we can pass it to the run-time lib.
GlobalVariable *llvm::createPrivateGlobalForString(Module &M, StringRef Str,
                                                   bool AllowMerging,
                                                   const char *NamePrefix) {
  Constant *StrConst = ConstantDataArray::getString(M.getContext(), Str);
  // We use private linkage for module-local strings. If they can be merged
  // with another one, we set the unnamed_addr attribute.
  GlobalVariable *GV =
      new GlobalVariable(M, StrConst->getType(), true,
                         GlobalValue::PrivateLinkage, StrConst, NamePrefix);
  if (AllowMerging)
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(1);  // Strings may not be merged w/o setting align 1.
  return GV;
}

Comdat *llvm::GetOrCreateFunctionComdat(Function &F, Triple &T,
                                        const std::string &ModuleId) {
  if (auto Comdat = F.getComdat()) return Comdat;
  assert(F.hasName());
  Module *M = F.getParent();
  std::string Name = F.getName();

  // Make a unique comdat name for internal linkage things on ELF. On COFF, the
  // name of the comdat group identifies the leader symbol of the comdat group.
  // The linkage of the leader symbol is considered during comdat resolution,
  // and internal symbols with the same name from different objects will not be
  // merged.
  if (T.isOSBinFormatELF() && F.hasLocalLinkage()) {
    if (ModuleId.empty())
      return nullptr;
    Name += ModuleId;
  }

  // Make a new comdat for the function. Use the "no duplicates" selection kind
  // for non-weak symbols if the object file format supports it.
  Comdat *C = M->getOrInsertComdat(Name);
  if (T.isOSBinFormatCOFF() && !F.isWeakForLinker())
    C->setSelectionKind(Comdat::NoDuplicates);
  F.setComdat(C);
  return C;
}

/// initializeInstrumentation - Initialize all passes in the TransformUtils
/// library.
void llvm::initializeInstrumentation(PassRegistry &Registry) {
  initializeAddressSanitizerPass(Registry);
  initializeAddressSanitizerModulePass(Registry);
  initializeBoundsCheckingLegacyPassPass(Registry);
  initializeControlHeightReductionLegacyPassPass(Registry);
  initializeGCOVProfilerLegacyPassPass(Registry);
  initializePGOInstrumentationGenLegacyPassPass(Registry);
  initializePGOInstrumentationUseLegacyPassPass(Registry);
  initializePGOIndirectCallPromotionLegacyPassPass(Registry);
  initializePGOMemOPSizeOptLegacyPassPass(Registry);
  initializeInstrProfilingLegacyPassPass(Registry);
  initializeMemorySanitizerLegacyPassPass(Registry);
  initializeHWAddressSanitizerPass(Registry);
  initializeThreadSanitizerLegacyPassPass(Registry);
  initializeSanitizerCoverageModulePass(Registry);
  initializeDataFlowSanitizerPass(Registry);
  initializeEfficiencySanitizerPass(Registry);
}

/// LLVMInitializeInstrumentation - C binding for
/// initializeInstrumentation.
void LLVMInitializeInstrumentation(LLVMPassRegistryRef R) {
  initializeInstrumentation(*unwrap(R));
}
