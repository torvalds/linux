//===----- RISCVMergeBaseOffset.cpp - Optimise address calculations  ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Merge the offset of address calculation into the offset field
// of instructions in a global address lowering sequence. This pass transforms:
//   lui  vreg1, %hi(s)
//   addi vreg2, vreg1, %lo(s)
//   addi vreg3, verg2, Offset
//
//   Into:
//   lui  vreg1, %hi(s+Offset)
//   addi vreg2, vreg1, %lo(s+Offset)
//
// The transformation is carried out under certain conditions:
// 1) The offset field in the base of global address lowering sequence is zero.
// 2) The lowered global address has only one use.
//
// The offset field can be in a different form. This pass handles all of them.
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVTargetMachine.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
#include <set>
using namespace llvm;

#define DEBUG_TYPE "riscv-merge-base-offset"
#define RISCV_MERGE_BASE_OFFSET_NAME "RISCV Merge Base Offset"
namespace {

struct RISCVMergeBaseOffsetOpt : public MachineFunctionPass {
  static char ID;
  const MachineFunction *MF;
  bool runOnMachineFunction(MachineFunction &Fn) override;
  bool detectLuiAddiGlobal(MachineInstr &LUI, MachineInstr *&ADDI);

  bool detectAndFoldOffset(MachineInstr &HiLUI, MachineInstr &LoADDI);
  void foldOffset(MachineInstr &HiLUI, MachineInstr &LoADDI, MachineInstr &Tail,
                  int64_t Offset);
  bool matchLargeOffset(MachineInstr &TailAdd, unsigned GSReg, int64_t &Offset);
  RISCVMergeBaseOffsetOpt() : MachineFunctionPass(ID) {}

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  StringRef getPassName() const override {
    return RISCV_MERGE_BASE_OFFSET_NAME;
  }

private:
  MachineRegisterInfo *MRI;
  std::set<MachineInstr *> DeadInstrs;
};
} // end anonymous namespace

char RISCVMergeBaseOffsetOpt::ID = 0;
INITIALIZE_PASS(RISCVMergeBaseOffsetOpt, "riscv-merge-base-offset",
                RISCV_MERGE_BASE_OFFSET_NAME, false, false)

// Detect the pattern:
//   lui   vreg1, %hi(s)
//   addi  vreg2, vreg1, %lo(s)
//
//   Pattern only accepted if:
//     1) ADDI has only one use.
//     2) LUI has only one use; which is the ADDI.
//     3) Both ADDI and LUI have GlobalAddress type which indicates that these
//        are generated from global address lowering.
//     4) Offset value in the Global Address is 0.
bool RISCVMergeBaseOffsetOpt::detectLuiAddiGlobal(MachineInstr &HiLUI,
                                                  MachineInstr *&LoADDI) {
  if (HiLUI.getOpcode() != RISCV::LUI ||
      HiLUI.getOperand(1).getTargetFlags() != RISCVII::MO_HI ||
      HiLUI.getOperand(1).getType() != MachineOperand::MO_GlobalAddress ||
      HiLUI.getOperand(1).getOffset() != 0 ||
      !MRI->hasOneUse(HiLUI.getOperand(0).getReg()))
    return false;
  unsigned HiLuiDestReg = HiLUI.getOperand(0).getReg();
  LoADDI = MRI->use_begin(HiLuiDestReg)->getParent();
  if (LoADDI->getOpcode() != RISCV::ADDI ||
      LoADDI->getOperand(2).getTargetFlags() != RISCVII::MO_LO ||
      LoADDI->getOperand(2).getType() != MachineOperand::MO_GlobalAddress ||
      LoADDI->getOperand(2).getOffset() != 0 ||
      !MRI->hasOneUse(LoADDI->getOperand(0).getReg()))
    return false;
  return true;
}

// Update the offset in HiLUI and LoADDI instructions.
// Delete the tail instruction and update all the uses to use the
// output from LoADDI.
void RISCVMergeBaseOffsetOpt::foldOffset(MachineInstr &HiLUI,
                                         MachineInstr &LoADDI,
                                         MachineInstr &Tail, int64_t Offset) {
  // Put the offset back in HiLUI and the LoADDI
  HiLUI.getOperand(1).setOffset(Offset);
  LoADDI.getOperand(2).setOffset(Offset);
  // Delete the tail instruction.
  DeadInstrs.insert(&Tail);
  MRI->replaceRegWith(Tail.getOperand(0).getReg(),
                      LoADDI.getOperand(0).getReg());
  LLVM_DEBUG(dbgs() << "  Merged offset " << Offset << " into base.\n"
                    << "     " << HiLUI << "     " << LoADDI;);
}

// Detect patterns for large offsets that are passed into an ADD instruction.
//
//                     Base address lowering is of the form:
//                        HiLUI:  lui   vreg1, %hi(s)
//                       LoADDI:  addi  vreg2, vreg1, %lo(s)
//                       /                                  \
//                      /                                    \
//                     /                                      \
//                    /  The large offset can be of two forms: \
//  1) Offset that has non zero bits in lower      2) Offset that has non zero
//     12 bits and upper 20 bits                      bits in upper 20 bits only
//   OffseLUI: lui   vreg3, 4
// OffsetTail: addi  voff, vreg3, 188                OffsetTail: lui  voff, 128
//                    \                                        /
//                     \                                      /
//                      \                                    /
//                       \                                  /
//                         TailAdd: add  vreg4, vreg2, voff
bool RISCVMergeBaseOffsetOpt::matchLargeOffset(MachineInstr &TailAdd,
                                               unsigned GAReg,
                                               int64_t &Offset) {
  assert((TailAdd.getOpcode() == RISCV::ADD) && "Expected ADD instruction!");
  unsigned Rs = TailAdd.getOperand(1).getReg();
  unsigned Rt = TailAdd.getOperand(2).getReg();
  unsigned Reg = Rs == GAReg ? Rt : Rs;

  // Can't fold if the register has more than one use.
  if (!MRI->hasOneUse(Reg))
    return false;
  // This can point to an ADDI or a LUI:
  MachineInstr &OffsetTail = *MRI->getVRegDef(Reg);
  if (OffsetTail.getOpcode() == RISCV::ADDI) {
    // The offset value has non zero bits in both %hi and %lo parts.
    // Detect an ADDI that feeds from a LUI instruction.
    MachineOperand &AddiImmOp = OffsetTail.getOperand(2);
    if (AddiImmOp.getTargetFlags() != RISCVII::MO_None)
      return false;
    int64_t OffLo = AddiImmOp.getImm();
    MachineInstr &OffsetLui =
        *MRI->getVRegDef(OffsetTail.getOperand(1).getReg());
    MachineOperand &LuiImmOp = OffsetLui.getOperand(1);
    if (OffsetLui.getOpcode() != RISCV::LUI ||
        LuiImmOp.getTargetFlags() != RISCVII::MO_None ||
        !MRI->hasOneUse(OffsetLui.getOperand(0).getReg()))
      return false;
    int64_t OffHi = OffsetLui.getOperand(1).getImm();
    Offset = (OffHi << 12) + OffLo;
    LLVM_DEBUG(dbgs() << "  Offset Instrs: " << OffsetTail
                      << "                 " << OffsetLui);
    DeadInstrs.insert(&OffsetTail);
    DeadInstrs.insert(&OffsetLui);
    return true;
  } else if (OffsetTail.getOpcode() == RISCV::LUI) {
    // The offset value has all zero bits in the lower 12 bits. Only LUI
    // exists.
    LLVM_DEBUG(dbgs() << "  Offset Instr: " << OffsetTail);
    Offset = OffsetTail.getOperand(1).getImm() << 12;
    DeadInstrs.insert(&OffsetTail);
    return true;
  }
  return false;
}

bool RISCVMergeBaseOffsetOpt::detectAndFoldOffset(MachineInstr &HiLUI,
                                                  MachineInstr &LoADDI) {
  unsigned DestReg = LoADDI.getOperand(0).getReg();
  assert(MRI->hasOneUse(DestReg) && "expected one use for LoADDI");
  // LoADDI has only one use.
  MachineInstr &Tail = *MRI->use_begin(DestReg)->getParent();
  switch (Tail.getOpcode()) {
  default:
    LLVM_DEBUG(dbgs() << "Don't know how to get offset from this instr:"
                      << Tail);
    return false;
  case RISCV::ADDI: {
    // Offset is simply an immediate operand.
    int64_t Offset = Tail.getOperand(2).getImm();
    LLVM_DEBUG(dbgs() << "  Offset Instr: " << Tail);
    foldOffset(HiLUI, LoADDI, Tail, Offset);
    return true;
  } break;
  case RISCV::ADD: {
    // The offset is too large to fit in the immediate field of ADDI.
    // This can be in two forms:
    // 1) LUI hi_Offset followed by:
    //    ADDI lo_offset
    //    This happens in case the offset has non zero bits in
    //    both hi 20 and lo 12 bits.
    // 2) LUI (offset20)
    //    This happens in case the lower 12 bits of the offset are zeros.
    int64_t Offset;
    if (!matchLargeOffset(Tail, DestReg, Offset))
      return false;
    foldOffset(HiLUI, LoADDI, Tail, Offset);
    return true;
  } break;
  case RISCV::LB:
  case RISCV::LH:
  case RISCV::LW:
  case RISCV::LBU:
  case RISCV::LHU:
  case RISCV::LWU:
  case RISCV::LD:
  case RISCV::FLW:
  case RISCV::FLD:
  case RISCV::SB:
  case RISCV::SH:
  case RISCV::SW:
  case RISCV::SD:
  case RISCV::FSW:
  case RISCV::FSD: {
    // Transforms the sequence:            Into:
    // HiLUI:  lui vreg1, %hi(foo)          --->  lui vreg1, %hi(foo+8)
    // LoADDI: addi vreg2, vreg1, %lo(foo)  --->  lw vreg3, lo(foo+8)(vreg1)
    // Tail:   lw vreg3, 8(vreg2)
    if (Tail.getOperand(1).isFI())
      return false;
    // Register defined by LoADDI should be used in the base part of the
    // load\store instruction. Otherwise, no folding possible.
    unsigned BaseAddrReg = Tail.getOperand(1).getReg();
    if (DestReg != BaseAddrReg)
      return false;
    MachineOperand &TailImmOp = Tail.getOperand(2);
    int64_t Offset = TailImmOp.getImm();
    // Update the offsets in global address lowering.
    HiLUI.getOperand(1).setOffset(Offset);
    // Update the immediate in the Tail instruction to add the offset.
    Tail.RemoveOperand(2);
    MachineOperand &ImmOp = LoADDI.getOperand(2);
    ImmOp.setOffset(Offset);
    Tail.addOperand(ImmOp);
    // Update the base reg in the Tail instruction to feed from LUI.
    // Output of HiLUI is only used in LoADDI, no need to use
    // MRI->replaceRegWith().
    Tail.getOperand(1).setReg(HiLUI.getOperand(0).getReg());
    DeadInstrs.insert(&LoADDI);
    return true;
  } break;
  }
  return false;
}

bool RISCVMergeBaseOffsetOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  DeadInstrs.clear();
  MRI = &Fn.getRegInfo();
  for (MachineBasicBlock &MBB : Fn) {
    LLVM_DEBUG(dbgs() << "MBB: " << MBB.getName() << "\n");
    for (MachineInstr &HiLUI : MBB) {
      MachineInstr *LoADDI = nullptr;
      if (!detectLuiAddiGlobal(HiLUI, LoADDI))
        continue;
      LLVM_DEBUG(dbgs() << "  Found lowered global address with one use: "
                        << *LoADDI->getOperand(2).getGlobal() << "\n");
      // If the use count is only one, merge the offset
      detectAndFoldOffset(HiLUI, *LoADDI);
    }
  }
  // Delete dead instructions.
  for (auto *MI : DeadInstrs)
    MI->eraseFromParent();
  return true;
}

/// Returns an instance of the Merge Base Offset Optimization pass.
FunctionPass *llvm::createRISCVMergeBaseOffsetOptPass() {
  return new RISCVMergeBaseOffsetOpt();
}
