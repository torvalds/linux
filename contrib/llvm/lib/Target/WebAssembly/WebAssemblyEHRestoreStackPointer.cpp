//===-- WebAssemblyEHRestoreStackPointer.cpp - __stack_pointer restoration ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// After the stack is unwound due to a thrown exception, the __stack_pointer
/// global can point to an invalid address. This inserts instructions that
/// restore __stack_pointer global.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/MC/MCAsmInfo.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-eh-restore-stack-pointer"

namespace {
class WebAssemblyEHRestoreStackPointer final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyEHRestoreStackPointer() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Restore Stack Pointer for Exception Handling";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char WebAssemblyEHRestoreStackPointer::ID = 0;
INITIALIZE_PASS(WebAssemblyEHRestoreStackPointer, DEBUG_TYPE,
                "Restore Stack Pointer for Exception Handling", true, false)

FunctionPass *llvm::createWebAssemblyEHRestoreStackPointer() {
  return new WebAssemblyEHRestoreStackPointer();
}

bool WebAssemblyEHRestoreStackPointer::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** EH Restore Stack Pointer **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  const auto *FrameLowering = static_cast<const WebAssemblyFrameLowering *>(
      MF.getSubtarget().getFrameLowering());
  if (!FrameLowering->needsPrologForEH(MF))
    return false;
  bool Changed = false;

  for (auto &MBB : MF) {
    if (!MBB.isEHPad())
      continue;
    Changed = true;

    // Insert __stack_pointer restoring instructions at the beginning of each EH
    // pad, after the catch instruction. (Catch instructions may have been
    // reordered, and catch_all instructions have not been inserted yet, but
    // those cases are handled in LateEHPrepare).
    //
    // Here it is safe to assume that SP32 holds the latest value of
    // __stack_pointer, because the only exception for this case is when a
    // function uses the red zone, but that only happens with leaf functions,
    // and we don't restore __stack_pointer in leaf functions anyway.
    auto InsertPos = MBB.begin();
    if (WebAssembly::isCatch(*MBB.begin()))
      InsertPos++;
    FrameLowering->writeSPToGlobal(WebAssembly::SP32, MF, MBB, InsertPos,
                                   MBB.begin()->getDebugLoc());
  }
  return Changed;
}
