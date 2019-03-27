//=== WebAssemblyLateEHPrepare.cpp - WebAssembly Exception Preparation -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Does various transformations for exception handling.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/MC/MCAsmInfo.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-exception-prepare"

namespace {
class WebAssemblyLateEHPrepare final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Prepare Exception";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool removeUnnecessaryUnreachables(MachineFunction &MF);
  bool replaceFuncletReturns(MachineFunction &MF);
  bool hoistCatches(MachineFunction &MF);
  bool addCatchAlls(MachineFunction &MF);
  bool addRethrows(MachineFunction &MF);
  bool ensureSingleBBTermPads(MachineFunction &MF);
  bool mergeTerminatePads(MachineFunction &MF);
  bool addCatchAllTerminatePads(MachineFunction &MF);

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyLateEHPrepare() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyLateEHPrepare::ID = 0;
INITIALIZE_PASS(WebAssemblyLateEHPrepare, DEBUG_TYPE,
                "WebAssembly Late Exception Preparation", false, false)

FunctionPass *llvm::createWebAssemblyLateEHPrepare() {
  return new WebAssemblyLateEHPrepare();
}

// Returns the nearest EH pad that dominates this instruction. This does not use
// dominator analysis; it just does BFS on its predecessors until arriving at an
// EH pad. This assumes valid EH scopes so the first EH pad it arrives in all
// possible search paths should be the same.
// Returns nullptr in case it does not find any EH pad in the search, or finds
// multiple different EH pads.
static MachineBasicBlock *getMatchingEHPad(MachineInstr *MI) {
  MachineFunction *MF = MI->getParent()->getParent();
  SmallVector<MachineBasicBlock *, 2> WL;
  SmallPtrSet<MachineBasicBlock *, 2> Visited;
  WL.push_back(MI->getParent());
  MachineBasicBlock *EHPad = nullptr;
  while (!WL.empty()) {
    MachineBasicBlock *MBB = WL.pop_back_val();
    if (Visited.count(MBB))
      continue;
    Visited.insert(MBB);
    if (MBB->isEHPad()) {
      if (EHPad && EHPad != MBB)
        return nullptr;
      EHPad = MBB;
      continue;
    }
    if (MBB == &MF->front())
      return nullptr;
    WL.append(MBB->pred_begin(), MBB->pred_end());
  }
  return EHPad;
}

// Erase the specified BBs if the BB does not have any remaining predecessors,
// and also all its dead children.
template <typename Container>
static void eraseDeadBBsAndChildren(const Container &MBBs) {
  SmallVector<MachineBasicBlock *, 8> WL(MBBs.begin(), MBBs.end());
  while (!WL.empty()) {
    MachineBasicBlock *MBB = WL.pop_back_val();
    if (!MBB->pred_empty())
      continue;
    SmallVector<MachineBasicBlock *, 4> Succs(MBB->succ_begin(),
                                              MBB->succ_end());
    WL.append(MBB->succ_begin(), MBB->succ_end());
    for (auto *Succ : Succs)
      MBB->removeSuccessor(Succ);
    MBB->eraseFromParent();
  }
}

bool WebAssemblyLateEHPrepare::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Late EH Prepare **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  if (MF.getTarget().getMCAsmInfo()->getExceptionHandlingType() !=
      ExceptionHandling::Wasm)
    return false;

  bool Changed = false;
  Changed |= removeUnnecessaryUnreachables(MF);
  Changed |= addRethrows(MF);
  if (!MF.getFunction().hasPersonalityFn())
    return Changed;
  Changed |= replaceFuncletReturns(MF);
  Changed |= hoistCatches(MF);
  Changed |= addCatchAlls(MF);
  Changed |= ensureSingleBBTermPads(MF);
  Changed |= mergeTerminatePads(MF);
  Changed |= addCatchAllTerminatePads(MF);
  return Changed;
}

bool WebAssemblyLateEHPrepare::removeUnnecessaryUnreachables(
    MachineFunction &MF) {
  bool Changed = false;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (!WebAssembly::isThrow(MI))
        continue;
      Changed = true;

      // The instruction after the throw should be an unreachable or a branch to
      // another BB that should eventually lead to an unreachable. Delete it
      // because throw itself is a terminator, and also delete successors if
      // any.
      MBB.erase(std::next(MachineBasicBlock::iterator(MI)), MBB.end());
      SmallVector<MachineBasicBlock *, 8> Succs(MBB.succ_begin(),
                                                MBB.succ_end());
      for (auto *Succ : Succs)
        MBB.removeSuccessor(Succ);
      eraseDeadBBsAndChildren(Succs);
    }
  }

  return Changed;
}

bool WebAssemblyLateEHPrepare::replaceFuncletReturns(MachineFunction &MF) {
  bool Changed = false;
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  auto *EHInfo = MF.getWasmEHFuncInfo();

  for (auto &MBB : MF) {
    auto Pos = MBB.getFirstTerminator();
    if (Pos == MBB.end())
      continue;
    MachineInstr *TI = &*Pos;

    switch (TI->getOpcode()) {
    case WebAssembly::CATCHRET: {
      // Replace a catchret with a branch
      MachineBasicBlock *TBB = TI->getOperand(0).getMBB();
      if (!MBB.isLayoutSuccessor(TBB))
        BuildMI(MBB, TI, TI->getDebugLoc(), TII.get(WebAssembly::BR))
            .addMBB(TBB);
      TI->eraseFromParent();
      Changed = true;
      break;
    }
    case WebAssembly::CLEANUPRET: {
      // Replace a cleanupret with a rethrow
      if (EHInfo->hasThrowUnwindDest(&MBB))
        BuildMI(MBB, TI, TI->getDebugLoc(), TII.get(WebAssembly::RETHROW))
            .addMBB(EHInfo->getThrowUnwindDest(&MBB));
      else
        BuildMI(MBB, TI, TI->getDebugLoc(),
                TII.get(WebAssembly::RETHROW_TO_CALLER));

      TI->eraseFromParent();
      Changed = true;
      break;
    }
    }
  }
  return Changed;
}

// Hoist catch instructions to the beginning of their matching EH pad BBs in
// case,
// (1) catch instruction is not the first instruction in EH pad.
// ehpad:
//   some_other_instruction
//   ...
//   %exn = catch 0
// (2) catch instruction is in a non-EH pad BB. For example,
// ehpad:
//   br bb0
// bb0:
//   %exn = catch 0
bool WebAssemblyLateEHPrepare::hoistCatches(MachineFunction &MF) {
  bool Changed = false;
  SmallVector<MachineInstr *, 16> Catches;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (WebAssembly::isCatch(MI))
        Catches.push_back(&MI);

  for (auto *Catch : Catches) {
    MachineBasicBlock *EHPad = getMatchingEHPad(Catch);
    assert(EHPad && "No matching EH pad for catch");
    if (EHPad->begin() == Catch)
      continue;
    Changed = true;
    EHPad->insert(EHPad->begin(), Catch->removeFromParent());
  }
  return Changed;
}

// Add catch_all to beginning of cleanup pads.
bool WebAssemblyLateEHPrepare::addCatchAlls(MachineFunction &MF) {
  bool Changed = false;
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  for (auto &MBB : MF) {
    if (!MBB.isEHPad())
      continue;
    // This runs after hoistCatches(), so we assume that if there is a catch,
    // that should be the first instruction in an EH pad.
    if (!WebAssembly::isCatch(*MBB.begin())) {
      Changed = true;
      BuildMI(MBB, MBB.begin(), MBB.begin()->getDebugLoc(),
              TII.get(WebAssembly::CATCH_ALL));
    }
  }
  return Changed;
}

// Add a 'rethrow' instruction after __cxa_rethrow() call
bool WebAssemblyLateEHPrepare::addRethrows(MachineFunction &MF) {
  bool Changed = false;
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  auto *EHInfo = MF.getWasmEHFuncInfo();

  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      // Check if it is a call to __cxa_rethrow()
      if (!MI.isCall())
        continue;
      MachineOperand &CalleeOp = MI.getOperand(0);
      if (!CalleeOp.isGlobal() ||
          CalleeOp.getGlobal()->getName() != WebAssembly::CxaRethrowFn)
        continue;

      // Now we have __cxa_rethrow() call
      Changed = true;
      auto InsertPt = std::next(MachineBasicBlock::iterator(MI));
      while (InsertPt != MBB.end() && InsertPt->isLabel()) // Skip EH_LABELs
        ++InsertPt;
      MachineInstr *Rethrow = nullptr;
      if (EHInfo->hasThrowUnwindDest(&MBB))
        Rethrow = BuildMI(MBB, InsertPt, MI.getDebugLoc(),
                          TII.get(WebAssembly::RETHROW))
                      .addMBB(EHInfo->getThrowUnwindDest(&MBB));
      else
        Rethrow = BuildMI(MBB, InsertPt, MI.getDebugLoc(),
                          TII.get(WebAssembly::RETHROW_TO_CALLER));

      // Because __cxa_rethrow does not return, the instruction after the
      // rethrow should be an unreachable or a branch to another BB that should
      // eventually lead to an unreachable. Delete it because rethrow itself is
      // a terminator, and also delete non-EH pad successors if any.
      MBB.erase(std::next(MachineBasicBlock::iterator(Rethrow)), MBB.end());
      SmallVector<MachineBasicBlock *, 8> NonPadSuccessors;
      for (auto *Succ : MBB.successors())
        if (!Succ->isEHPad())
          NonPadSuccessors.push_back(Succ);
      for (auto *Succ : NonPadSuccessors)
        MBB.removeSuccessor(Succ);
      eraseDeadBBsAndChildren(NonPadSuccessors);
    }
  return Changed;
}

// Terminate pads are an single-BB EH pad in the form of
// termpad:
//   %exn = catch 0
//   call @__clang_call_terminate(%exn)
//   unreachable
// (There can be local.set and local.gets before the call if we didn't run
// RegStackify)
// But code transformations can change or add more control flow, so the call to
// __clang_call_terminate() function may not be in the original EH pad anymore.
// This ensures every terminate pad is a single BB in the form illustrated
// above.
bool WebAssemblyLateEHPrepare::ensureSingleBBTermPads(MachineFunction &MF) {
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  // Find calls to __clang_call_terminate()
  SmallVector<MachineInstr *, 8> ClangCallTerminateCalls;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (MI.isCall()) {
        const MachineOperand &CalleeOp = MI.getOperand(0);
        if (CalleeOp.isGlobal() && CalleeOp.getGlobal()->getName() ==
                                       WebAssembly::ClangCallTerminateFn)
          ClangCallTerminateCalls.push_back(&MI);
      }

  bool Changed = false;
  for (auto *Call : ClangCallTerminateCalls) {
    MachineBasicBlock *EHPad = getMatchingEHPad(Call);
    assert(EHPad && "No matching EH pad for catch");

    // If it is already the form we want, skip it
    if (Call->getParent() == EHPad &&
        Call->getNextNode()->getOpcode() == WebAssembly::UNREACHABLE)
      continue;

    // In case the __clang_call_terminate() call is not in its matching EH pad,
    // move the call to the end of EH pad and add an unreachable instruction
    // after that. Delete all successors and their children if any, because here
    // the program terminates.
    Changed = true;
    MachineInstr *Catch = &*EHPad->begin();
    // This runs after hoistCatches(), so catch instruction should be at the top
    assert(WebAssembly::isCatch(*Catch));
    // Takes the result register of the catch instruction as argument. There may
    // have been some other local.set/local.gets in between, but at this point
    // we don't care.
    Call->getOperand(1).setReg(Catch->getOperand(0).getReg());
    auto InsertPos = std::next(MachineBasicBlock::iterator(Catch));
    EHPad->insert(InsertPos, Call->removeFromParent());
    BuildMI(*EHPad, InsertPos, Call->getDebugLoc(),
            TII.get(WebAssembly::UNREACHABLE));
    EHPad->erase(InsertPos, EHPad->end());
    SmallVector<MachineBasicBlock *, 8> Succs(EHPad->succ_begin(),
                                              EHPad->succ_end());
    for (auto *Succ : Succs)
      EHPad->removeSuccessor(Succ);
    eraseDeadBBsAndChildren(Succs);
  }
  return Changed;
}

// In case there are multiple terminate pads, merge them into one for code size.
// This runs after ensureSingleBBTermPads() and assumes every terminate pad is a
// single BB.
// In principle this violates EH scope relationship because it can merge
// multiple inner EH scopes, each of which is in different outer EH scope. But
// getEHScopeMembership() function will not be called after this, so it is fine.
bool WebAssemblyLateEHPrepare::mergeTerminatePads(MachineFunction &MF) {
  SmallVector<MachineBasicBlock *, 8> TermPads;
  for (auto &MBB : MF)
    if (WebAssembly::isCatchTerminatePad(MBB))
      TermPads.push_back(&MBB);
  if (TermPads.empty())
    return false;

  MachineBasicBlock *UniqueTermPad = TermPads.front();
  for (auto *TermPad :
       llvm::make_range(std::next(TermPads.begin()), TermPads.end())) {
    SmallVector<MachineBasicBlock *, 2> Preds(TermPad->pred_begin(),
                                              TermPad->pred_end());
    for (auto *Pred : Preds)
      Pred->replaceSuccessor(TermPad, UniqueTermPad);
    TermPad->eraseFromParent();
  }
  return true;
}

// Terminate pads are cleanup pads, so they should start with a 'catch_all'
// instruction. But in the Itanium model, when we have a C++ exception object,
// we pass them to __clang_call_terminate function, which calls __cxa_end_catch
// with the passed exception pointer and then std::terminate. This is the reason
// that terminate pads are generated with not a catch_all but a catch
// instruction in clang and earlier llvm passes. Here we append a terminate pad
// with a catch_all after each existing terminate pad so we can also catch
// foreign exceptions. For every terminate pad:
//   %exn = catch 0
//   call @__clang_call_terminate(%exn)
//   unreachable
// We append this BB right after that:
//   catch_all
//   call @std::terminate()
//   unreachable
bool WebAssemblyLateEHPrepare::addCatchAllTerminatePads(MachineFunction &MF) {
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  SmallVector<MachineBasicBlock *, 8> TermPads;
  for (auto &MBB : MF)
    if (WebAssembly::isCatchTerminatePad(MBB))
      TermPads.push_back(&MBB);
  if (TermPads.empty())
    return false;

  Function *StdTerminateFn =
      MF.getFunction().getParent()->getFunction(WebAssembly::StdTerminateFn);
  assert(StdTerminateFn && "There is no std::terminate() function");
  for (auto *CatchTermPad : TermPads) {
    DebugLoc DL = CatchTermPad->findDebugLoc(CatchTermPad->begin());
    auto *CatchAllTermPad = MF.CreateMachineBasicBlock();
    MF.insert(std::next(MachineFunction::iterator(CatchTermPad)),
              CatchAllTermPad);
    CatchAllTermPad->setIsEHPad();
    BuildMI(CatchAllTermPad, DL, TII.get(WebAssembly::CATCH_ALL));
    BuildMI(CatchAllTermPad, DL, TII.get(WebAssembly::CALL_VOID))
        .addGlobalAddress(StdTerminateFn);
    BuildMI(CatchAllTermPad, DL, TII.get(WebAssembly::UNREACHABLE));

    // Actually this CatchAllTermPad (new terminate pad with a catch_all) is not
    // a successor of an existing terminate pad. CatchAllTermPad should have all
    // predecessors CatchTermPad has instead. This is a hack to force
    // CatchAllTermPad be always sorted right after CatchTermPad; the correct
    // predecessor-successor relationships will be restored in CFGStackify pass.
    CatchTermPad->addSuccessor(CatchAllTermPad);
  }
  return true;
}
