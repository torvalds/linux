//===- EHPersonalities.cpp - Compute EH-related information ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

/// See if the given exception handling personality function is one that we
/// understand.  If so, return a description of it; otherwise return Unknown.
EHPersonality llvm::classifyEHPersonality(const Value *Pers) {
  const Function *F =
      Pers ? dyn_cast<Function>(Pers->stripPointerCasts()) : nullptr;
  if (!F)
    return EHPersonality::Unknown;
  return StringSwitch<EHPersonality>(F->getName())
    .Case("__gnat_eh_personality",     EHPersonality::GNU_Ada)
    .Case("__gxx_personality_v0",      EHPersonality::GNU_CXX)
    .Case("__gxx_personality_seh0",    EHPersonality::GNU_CXX)
    .Case("__gxx_personality_sj0",     EHPersonality::GNU_CXX_SjLj)
    .Case("__gcc_personality_v0",      EHPersonality::GNU_C)
    .Case("__gcc_personality_seh0",    EHPersonality::GNU_C)
    .Case("__gcc_personality_sj0",     EHPersonality::GNU_C_SjLj)
    .Case("__objc_personality_v0",     EHPersonality::GNU_ObjC)
    .Case("_except_handler3",          EHPersonality::MSVC_X86SEH)
    .Case("_except_handler4",          EHPersonality::MSVC_X86SEH)
    .Case("__C_specific_handler",      EHPersonality::MSVC_Win64SEH)
    .Case("__CxxFrameHandler3",        EHPersonality::MSVC_CXX)
    .Case("ProcessCLRException",       EHPersonality::CoreCLR)
    .Case("rust_eh_personality",       EHPersonality::Rust)
    .Case("__gxx_wasm_personality_v0", EHPersonality::Wasm_CXX)
    .Default(EHPersonality::Unknown);
}

StringRef llvm::getEHPersonalityName(EHPersonality Pers) {
  switch (Pers) {
  case EHPersonality::GNU_Ada:       return "__gnat_eh_personality";
  case EHPersonality::GNU_CXX:       return "__gxx_personality_v0";
  case EHPersonality::GNU_CXX_SjLj:  return "__gxx_personality_sj0";
  case EHPersonality::GNU_C:         return "__gcc_personality_v0";
  case EHPersonality::GNU_C_SjLj:    return "__gcc_personality_sj0";
  case EHPersonality::GNU_ObjC:      return "__objc_personality_v0";
  case EHPersonality::MSVC_X86SEH:   return "_except_handler3";
  case EHPersonality::MSVC_Win64SEH: return "__C_specific_handler";
  case EHPersonality::MSVC_CXX:      return "__CxxFrameHandler3";
  case EHPersonality::CoreCLR:       return "ProcessCLRException";
  case EHPersonality::Rust:          return "rust_eh_personality";
  case EHPersonality::Wasm_CXX:      return "__gxx_wasm_personality_v0";
  case EHPersonality::Unknown:       llvm_unreachable("Unknown EHPersonality!");
  }

  llvm_unreachable("Invalid EHPersonality!");
}

EHPersonality llvm::getDefaultEHPersonality(const Triple &T) {
  return EHPersonality::GNU_C;
}

bool llvm::canSimplifyInvokeNoUnwind(const Function *F) {
  EHPersonality Personality = classifyEHPersonality(F->getPersonalityFn());
  // We can't simplify any invokes to nounwind functions if the personality
  // function wants to catch asynch exceptions.  The nounwind attribute only
  // implies that the function does not throw synchronous exceptions.
  return !isAsynchronousEHPersonality(Personality);
}

DenseMap<BasicBlock *, ColorVector> llvm::colorEHFunclets(Function &F) {
  SmallVector<std::pair<BasicBlock *, BasicBlock *>, 16> Worklist;
  BasicBlock *EntryBlock = &F.getEntryBlock();
  DenseMap<BasicBlock *, ColorVector> BlockColors;

  // Build up the color map, which maps each block to its set of 'colors'.
  // For any block B the "colors" of B are the set of funclets F (possibly
  // including a root "funclet" representing the main function) such that
  // F will need to directly contain B or a copy of B (where the term "directly
  // contain" is used to distinguish from being "transitively contained" in
  // a nested funclet).
  //
  // Note: Despite not being a funclet in the truest sense, a catchswitch is
  // considered to belong to its own funclet for the purposes of coloring.

  DEBUG_WITH_TYPE("winehprepare-coloring", dbgs() << "\nColoring funclets for "
                                                  << F.getName() << "\n");

  Worklist.push_back({EntryBlock, EntryBlock});

  while (!Worklist.empty()) {
    BasicBlock *Visiting;
    BasicBlock *Color;
    std::tie(Visiting, Color) = Worklist.pop_back_val();
    DEBUG_WITH_TYPE("winehprepare-coloring",
                    dbgs() << "Visiting " << Visiting->getName() << ", "
                           << Color->getName() << "\n");
    Instruction *VisitingHead = Visiting->getFirstNonPHI();
    if (VisitingHead->isEHPad()) {
      // Mark this funclet head as a member of itself.
      Color = Visiting;
    }
    // Note that this is a member of the given color.
    ColorVector &Colors = BlockColors[Visiting];
    if (!is_contained(Colors, Color))
      Colors.push_back(Color);
    else
      continue;

    DEBUG_WITH_TYPE("winehprepare-coloring",
                    dbgs() << "  Assigned color \'" << Color->getName()
                           << "\' to block \'" << Visiting->getName()
                           << "\'.\n");

    BasicBlock *SuccColor = Color;
    Instruction *Terminator = Visiting->getTerminator();
    if (auto *CatchRet = dyn_cast<CatchReturnInst>(Terminator)) {
      Value *ParentPad = CatchRet->getCatchSwitchParentPad();
      if (isa<ConstantTokenNone>(ParentPad))
        SuccColor = EntryBlock;
      else
        SuccColor = cast<Instruction>(ParentPad)->getParent();
    }

    for (BasicBlock *Succ : successors(Visiting))
      Worklist.push_back({Succ, SuccColor});
  }
  return BlockColors;
}
