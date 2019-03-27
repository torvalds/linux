//===-- WebAssemblyCallIndirectFixup.cpp - Fix call_indirects -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file converts pseudo call_indirect instructions into real
/// call_indirects.
///
/// The order of arguments for a call_indirect is the arguments to the function
/// call, followed by the function pointer. There's no natural way to express
/// a machineinstr with varargs followed by one more arg, so we express it as
/// the function pointer followed by varargs, then rewrite it here.
///
/// We need to rewrite the order of the arguments on the machineinstrs
/// themselves so that register stackification knows the order they'll be
/// executed in.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h" // for WebAssembly::ARGUMENT_*
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-call-indirect-fixup"

namespace {
class WebAssemblyCallIndirectFixup final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly CallIndirect Fixup";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyCallIndirectFixup() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyCallIndirectFixup::ID = 0;
INITIALIZE_PASS(WebAssemblyCallIndirectFixup, DEBUG_TYPE,
                "Rewrite call_indirect argument orderings", false, false)

FunctionPass *llvm::createWebAssemblyCallIndirectFixup() {
  return new WebAssemblyCallIndirectFixup();
}

static unsigned GetNonPseudoCallIndirectOpcode(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
    using namespace WebAssembly;
  case PCALL_INDIRECT_VOID:
    return CALL_INDIRECT_VOID;
  case PCALL_INDIRECT_I32:
    return CALL_INDIRECT_I32;
  case PCALL_INDIRECT_I64:
    return CALL_INDIRECT_I64;
  case PCALL_INDIRECT_F32:
    return CALL_INDIRECT_F32;
  case PCALL_INDIRECT_F64:
    return CALL_INDIRECT_F64;
  case PCALL_INDIRECT_v16i8:
    return CALL_INDIRECT_v16i8;
  case PCALL_INDIRECT_v8i16:
    return CALL_INDIRECT_v8i16;
  case PCALL_INDIRECT_v4i32:
    return CALL_INDIRECT_v4i32;
  case PCALL_INDIRECT_v2i64:
    return CALL_INDIRECT_v2i64;
  case PCALL_INDIRECT_v4f32:
    return CALL_INDIRECT_v4f32;
  case PCALL_INDIRECT_v2f64:
    return CALL_INDIRECT_v2f64;
  default:
    return INSTRUCTION_LIST_END;
  }
}

static bool IsPseudoCallIndirect(const MachineInstr &MI) {
  return GetNonPseudoCallIndirectOpcode(MI) !=
         WebAssembly::INSTRUCTION_LIST_END;
}

bool WebAssemblyCallIndirectFixup::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Fixing up CALL_INDIRECTs **********\n"
                    << "********** Function: " << MF.getName() << '\n');

  bool Changed = false;
  const WebAssemblyInstrInfo *TII =
      MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (IsPseudoCallIndirect(MI)) {
        LLVM_DEBUG(dbgs() << "Found call_indirect: " << MI << '\n');

        // Rewrite pseudo to non-pseudo
        const MCInstrDesc &Desc = TII->get(GetNonPseudoCallIndirectOpcode(MI));
        MI.setDesc(Desc);

        // Rewrite argument order
        SmallVector<MachineOperand, 8> Ops;

        // Set up a placeholder for the type signature immediate.
        Ops.push_back(MachineOperand::CreateImm(0));

        // Set up the flags immediate, which currently has no defined flags
        // so it's always zero.
        Ops.push_back(MachineOperand::CreateImm(0));

        for (const MachineOperand &MO :
             make_range(MI.operands_begin() + MI.getDesc().getNumDefs() + 1,
                        MI.operands_begin() + MI.getNumExplicitOperands()))
          Ops.push_back(MO);
        Ops.push_back(MI.getOperand(MI.getDesc().getNumDefs()));

        // Replace the instructions operands.
        while (MI.getNumOperands() > MI.getDesc().getNumDefs())
          MI.RemoveOperand(MI.getNumOperands() - 1);
        for (const MachineOperand &MO : Ops)
          MI.addOperand(MO);

        LLVM_DEBUG(dbgs() << "  After transform: " << MI);
        Changed = true;
      }
    }
  }

  LLVM_DEBUG(dbgs() << "\nDone fixing up CALL_INDIRECTs\n\n");

  return Changed;
}
