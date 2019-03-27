//===- X86EvexToVex.cpp ---------------------------------------------------===//
// Compress EVEX instructions to VEX encoding when possible to reduce code size
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file defines the pass that goes over all AVX-512 instructions which
/// are encoded using the EVEX prefix and if possible replaces them by their
/// corresponding VEX encoding which is usually shorter by 2 bytes.
/// EVEX instructions may be encoded via the VEX prefix when the AVX-512
/// instruction has a corresponding AVX/AVX2 opcode and when it does not
/// use the xmm or the mask registers or xmm/ymm registers with indexes
/// higher than 15.
/// The pass applies code reduction on the generated code for AVX-512 instrs.
//
//===----------------------------------------------------------------------===//

#include "InstPrinter/X86InstComments.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

// Including the generated EVEX2VEX tables.
struct X86EvexToVexCompressTableEntry {
  uint16_t EvexOpcode;
  uint16_t VexOpcode;

  bool operator<(const X86EvexToVexCompressTableEntry &RHS) const {
    return EvexOpcode < RHS.EvexOpcode;
  }

  friend bool operator<(const X86EvexToVexCompressTableEntry &TE,
                        unsigned Opc) {
    return TE.EvexOpcode < Opc;
  }
};
#include "X86GenEVEX2VEXTables.inc"

#define EVEX2VEX_DESC "Compressing EVEX instrs to VEX encoding when possible"
#define EVEX2VEX_NAME "x86-evex-to-vex-compress"

#define DEBUG_TYPE EVEX2VEX_NAME

namespace {

class EvexToVexInstPass : public MachineFunctionPass {

  /// For EVEX instructions that can be encoded using VEX encoding, replace
  /// them by the VEX encoding in order to reduce size.
  bool CompressEvexToVexImpl(MachineInstr &MI) const;

public:
  static char ID;

  EvexToVexInstPass() : MachineFunctionPass(ID) {
    initializeEvexToVexInstPassPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return EVEX2VEX_DESC; }

  /// Loop over all of the basic blocks, replacing EVEX instructions
  /// by equivalent VEX instructions when possible for reducing code size.
  bool runOnMachineFunction(MachineFunction &MF) override;

  // This pass runs after regalloc and doesn't support VReg operands.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  /// Machine instruction info used throughout the class.
  const X86InstrInfo *TII;
};

} // end anonymous namespace

char EvexToVexInstPass::ID = 0;

bool EvexToVexInstPass::runOnMachineFunction(MachineFunction &MF) {
  TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();

  const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
  if (!ST.hasAVX512())
    return false;

  bool Changed = false;

  /// Go over all basic blocks in function and replace
  /// EVEX encoded instrs by VEX encoding when possible.
  for (MachineBasicBlock &MBB : MF) {

    // Traverse the basic block.
    for (MachineInstr &MI : MBB)
      Changed |= CompressEvexToVexImpl(MI);
  }

  return Changed;
}

static bool usesExtendedRegister(const MachineInstr &MI) {
  auto isHiRegIdx = [](unsigned Reg) {
    // Check for XMM register with indexes between 16 - 31.
    if (Reg >= X86::XMM16 && Reg <= X86::XMM31)
      return true;

    // Check for YMM register with indexes between 16 - 31.
    if (Reg >= X86::YMM16 && Reg <= X86::YMM31)
      return true;

    return false;
  };

  // Check that operands are not ZMM regs or
  // XMM/YMM regs with hi indexes between 16 - 31.
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (!MO.isReg())
      continue;

    unsigned Reg = MO.getReg();

    assert(!(Reg >= X86::ZMM0 && Reg <= X86::ZMM31) &&
           "ZMM instructions should not be in the EVEX->VEX tables");

    if (isHiRegIdx(Reg))
      return true;
  }

  return false;
}

// Do any custom cleanup needed to finalize the conversion.
static bool performCustomAdjustments(MachineInstr &MI, unsigned NewOpc) {
  (void)NewOpc;
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case X86::VALIGNDZ128rri:
  case X86::VALIGNDZ128rmi:
  case X86::VALIGNQZ128rri:
  case X86::VALIGNQZ128rmi: {
    assert((NewOpc == X86::VPALIGNRrri || NewOpc == X86::VPALIGNRrmi) &&
           "Unexpected new opcode!");
    unsigned Scale = (Opc == X86::VALIGNQZ128rri ||
                      Opc == X86::VALIGNQZ128rmi) ? 8 : 4;
    MachineOperand &Imm = MI.getOperand(MI.getNumExplicitOperands()-1);
    Imm.setImm(Imm.getImm() * Scale);
    break;
  }
  case X86::VSHUFF32X4Z256rmi:
  case X86::VSHUFF32X4Z256rri:
  case X86::VSHUFF64X2Z256rmi:
  case X86::VSHUFF64X2Z256rri:
  case X86::VSHUFI32X4Z256rmi:
  case X86::VSHUFI32X4Z256rri:
  case X86::VSHUFI64X2Z256rmi:
  case X86::VSHUFI64X2Z256rri: {
    assert((NewOpc == X86::VPERM2F128rr || NewOpc == X86::VPERM2I128rr ||
            NewOpc == X86::VPERM2F128rm || NewOpc == X86::VPERM2I128rm) &&
           "Unexpected new opcode!");
    MachineOperand &Imm = MI.getOperand(MI.getNumExplicitOperands()-1);
    int64_t ImmVal = Imm.getImm();
    // Set bit 5, move bit 1 to bit 4, copy bit 0.
    Imm.setImm(0x20 | ((ImmVal & 2) << 3) | (ImmVal & 1));
    break;
  }
  case X86::VRNDSCALEPDZ128rri:
  case X86::VRNDSCALEPDZ128rmi:
  case X86::VRNDSCALEPSZ128rri:
  case X86::VRNDSCALEPSZ128rmi:
  case X86::VRNDSCALEPDZ256rri:
  case X86::VRNDSCALEPDZ256rmi:
  case X86::VRNDSCALEPSZ256rri:
  case X86::VRNDSCALEPSZ256rmi:
  case X86::VRNDSCALESDZr:
  case X86::VRNDSCALESDZm:
  case X86::VRNDSCALESSZr:
  case X86::VRNDSCALESSZm:
  case X86::VRNDSCALESDZr_Int:
  case X86::VRNDSCALESDZm_Int:
  case X86::VRNDSCALESSZr_Int:
  case X86::VRNDSCALESSZm_Int:
    const MachineOperand &Imm = MI.getOperand(MI.getNumExplicitOperands()-1);
    int64_t ImmVal = Imm.getImm();
    // Ensure that only bits 3:0 of the immediate are used.
    if ((ImmVal & 0xf) != ImmVal)
      return false;
    break;
  }

  return true;
}


// For EVEX instructions that can be encoded using VEX encoding
// replace them by the VEX encoding in order to reduce size.
bool EvexToVexInstPass::CompressEvexToVexImpl(MachineInstr &MI) const {
  // VEX format.
  // # of bytes: 0,2,3  1      1      0,1   0,1,2,4  0,1
  //  [Prefixes] [VEX]  OPCODE ModR/M [SIB] [DISP]  [IMM]
  //
  // EVEX format.
  //  # of bytes: 4    1      1      1      4       / 1         1
  //  [Prefixes]  EVEX Opcode ModR/M [SIB] [Disp32] / [Disp8*N] [Immediate]

  const MCInstrDesc &Desc = MI.getDesc();

  // Check for EVEX instructions only.
  if ((Desc.TSFlags & X86II::EncodingMask) != X86II::EVEX)
    return false;

  // Check for EVEX instructions with mask or broadcast as in these cases
  // the EVEX prefix is needed in order to carry this information
  // thus preventing the transformation to VEX encoding.
  if (Desc.TSFlags & (X86II::EVEX_K | X86II::EVEX_B))
    return false;

  // Check for EVEX instructions with L2 set. These instructions are 512-bits
  // and can't be converted to VEX.
  if (Desc.TSFlags & X86II::EVEX_L2)
    return false;

#ifndef NDEBUG
  // Make sure the tables are sorted.
  static std::atomic<bool> TableChecked(false);
  if (!TableChecked.load(std::memory_order_relaxed)) {
    assert(std::is_sorted(std::begin(X86EvexToVex128CompressTable),
                          std::end(X86EvexToVex128CompressTable)) &&
           "X86EvexToVex128CompressTable is not sorted!");
    assert(std::is_sorted(std::begin(X86EvexToVex256CompressTable),
                          std::end(X86EvexToVex256CompressTable)) &&
           "X86EvexToVex256CompressTable is not sorted!");
    TableChecked.store(true, std::memory_order_relaxed);
  }
#endif

  // Use the VEX.L bit to select the 128 or 256-bit table.
  ArrayRef<X86EvexToVexCompressTableEntry> Table =
    (Desc.TSFlags & X86II::VEX_L) ? makeArrayRef(X86EvexToVex256CompressTable)
                                  : makeArrayRef(X86EvexToVex128CompressTable);

  auto I = std::lower_bound(Table.begin(), Table.end(), MI.getOpcode());
  if (I == Table.end() || I->EvexOpcode != MI.getOpcode())
    return false;

  unsigned NewOpc = I->VexOpcode;

  if (usesExtendedRegister(MI))
    return false;

  if (!performCustomAdjustments(MI, NewOpc))
    return false;

  MI.setDesc(TII->get(NewOpc));
  MI.setAsmPrinterFlag(X86::AC_EVEX_2_VEX);
  return true;
}

INITIALIZE_PASS(EvexToVexInstPass, EVEX2VEX_NAME, EVEX2VEX_DESC, false, false)

FunctionPass *llvm::createX86EvexToVexInsts() {
  return new EvexToVexInstPass();
}
