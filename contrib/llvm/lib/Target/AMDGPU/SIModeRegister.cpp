//===-- SIModeRegister.cpp - Mode Register --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This pass inserts changes to the Mode register settings as required.
/// Note that currently it only deals with the Double Precision Floating Point
/// rounding mode setting, but is intended to be generic enough to be easily
/// expanded.
///
//===----------------------------------------------------------------------===//
//
#include "AMDGPU.h"
#include "AMDGPUInstrInfo.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <queue>

#define DEBUG_TYPE "si-mode-register"

STATISTIC(NumSetregInserted, "Number of setreg of mode register inserted.");

using namespace llvm;

struct Status {
  // Mask is a bitmask where a '1' indicates the corresponding Mode bit has a
  // known value
  unsigned Mask;
  unsigned Mode;

  Status() : Mask(0), Mode(0){};

  Status(unsigned Mask, unsigned Mode) : Mask(Mask), Mode(Mode) {
    Mode &= Mask;
  };

  // merge two status values such that only values that don't conflict are
  // preserved
  Status merge(const Status &S) const {
    return Status((Mask | S.Mask), ((Mode & ~S.Mask) | (S.Mode & S.Mask)));
  }

  // merge an unknown value by using the unknown value's mask to remove bits
  // from the result
  Status mergeUnknown(unsigned newMask) {
    return Status(Mask & ~newMask, Mode & ~newMask);
  }

  // intersect two Status values to produce a mode and mask that is a subset
  // of both values
  Status intersect(const Status &S) const {
    unsigned NewMask = (Mask & S.Mask) & (Mode ^ ~S.Mode);
    unsigned NewMode = (Mode & NewMask);
    return Status(NewMask, NewMode);
  }

  // produce the delta required to change the Mode to the required Mode
  Status delta(const Status &S) const {
    return Status((S.Mask & (Mode ^ S.Mode)) | (~Mask & S.Mask), S.Mode);
  }

  bool operator==(const Status &S) const {
    return (Mask == S.Mask) && (Mode == S.Mode);
  }

  bool operator!=(const Status &S) const { return !(*this == S); }

  bool isCompatible(Status &S) {
    return ((Mask & S.Mask) == S.Mask) && ((Mode & S.Mask) == S.Mode);
  }

  bool isCombinable(Status &S) {
    return !(Mask & S.Mask) || isCompatible(S);
  }
};

class BlockData {
public:
  // The Status that represents the mode register settings required by the
  // FirstInsertionPoint (if any) in this block. Calculated in Phase 1.
  Status Require;

  // The Status that represents the net changes to the Mode register made by
  // this block, Calculated in Phase 1.
  Status Change;

  // The Status that represents the mode register settings on exit from this
  // block. Calculated in Phase 2.
  Status Exit;

  // The Status that represents the intersection of exit Mode register settings
  // from all predecessor blocks. Calculated in Phase 2, and used by Phase 3.
  Status Pred;

  // In Phase 1 we record the first instruction that has a mode requirement,
  // which is used in Phase 3 if we need to insert a mode change.
  MachineInstr *FirstInsertionPoint;

  BlockData() : FirstInsertionPoint(nullptr) {};
};

namespace {

class SIModeRegister : public MachineFunctionPass {
public:
  static char ID;

  std::vector<std::unique_ptr<BlockData>> BlockInfo;
  std::queue<MachineBasicBlock *> Phase2List;

  // The default mode register setting currently only caters for the floating
  // point double precision rounding mode.
  // We currently assume the default rounding mode is Round to Nearest
  // NOTE: this should come from a per function rounding mode setting once such
  // a setting exists.
  unsigned DefaultMode = FP_ROUND_ROUND_TO_NEAREST;
  Status DefaultStatus =
      Status(FP_ROUND_MODE_DP(0x3), FP_ROUND_MODE_DP(DefaultMode));

public:
  SIModeRegister() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  void processBlockPhase1(MachineBasicBlock &MBB, const SIInstrInfo *TII);

  void processBlockPhase2(MachineBasicBlock &MBB, const SIInstrInfo *TII);

  void processBlockPhase3(MachineBasicBlock &MBB, const SIInstrInfo *TII);

  Status getInstructionMode(MachineInstr &MI, const SIInstrInfo *TII);

  void insertSetreg(MachineBasicBlock &MBB, MachineInstr *I,
                    const SIInstrInfo *TII, Status InstrMode);
};
} // End anonymous namespace.

INITIALIZE_PASS(SIModeRegister, DEBUG_TYPE,
                "Insert required mode register values", false, false)

char SIModeRegister::ID = 0;

char &llvm::SIModeRegisterID = SIModeRegister::ID;

FunctionPass *llvm::createSIModeRegisterPass() { return new SIModeRegister(); }

// Determine the Mode register setting required for this instruction.
// Instructions which don't use the Mode register return a null Status.
// Note this currently only deals with instructions that use the floating point
// double precision setting.
Status SIModeRegister::getInstructionMode(MachineInstr &MI,
                                          const SIInstrInfo *TII) {
  if (TII->usesFPDPRounding(MI)) {
    switch (MI.getOpcode()) {
    case AMDGPU::V_INTERP_P1LL_F16:
    case AMDGPU::V_INTERP_P1LV_F16:
    case AMDGPU::V_INTERP_P2_F16:
      // f16 interpolation instructions need double precision round to zero
      return Status(FP_ROUND_MODE_DP(3),
                    FP_ROUND_MODE_DP(FP_ROUND_ROUND_TO_ZERO));
    default:
      return DefaultStatus;
    }
  }
  return Status();
}

// Insert a setreg instruction to update the Mode register.
// It is possible (though unlikely) for an instruction to require a change to
// the value of disjoint parts of the Mode register when we don't know the
// value of the intervening bits. In that case we need to use more than one
// setreg instruction.
void SIModeRegister::insertSetreg(MachineBasicBlock &MBB, MachineInstr *MI,
                                  const SIInstrInfo *TII, Status InstrMode) {
  while (InstrMode.Mask) {
    unsigned Offset = countTrailingZeros<unsigned>(InstrMode.Mask);
    unsigned Width = countTrailingOnes<unsigned>(InstrMode.Mask >> Offset);
    unsigned Value = (InstrMode.Mode >> Offset) & ((1 << Width) - 1);
    BuildMI(MBB, MI, 0, TII->get(AMDGPU::S_SETREG_IMM32_B32))
        .addImm(Value)
        .addImm(((Width - 1) << AMDGPU::Hwreg::WIDTH_M1_SHIFT_) |
                (Offset << AMDGPU::Hwreg::OFFSET_SHIFT_) |
                (AMDGPU::Hwreg::ID_MODE << AMDGPU::Hwreg::ID_SHIFT_));
    ++NumSetregInserted;
    InstrMode.Mask &= ~(((1 << Width) - 1) << Offset);
  }
}

// In Phase 1 we iterate through the instructions of the block and for each
// instruction we get its mode usage. If the instruction uses the Mode register
// we:
// - update the Change status, which tracks the changes to the Mode register
//   made by this block
// - if this instruction's requirements are compatible with the current setting
//   of the Mode register we merge the modes
// - if it isn't compatible and an InsertionPoint isn't set, then we set the
//   InsertionPoint to the current instruction, and we remember the current
//   mode
// - if it isn't compatible and InsertionPoint is set we insert a seteg before
//   that instruction (unless this instruction forms part of the block's
//   entry requirements in which case the insertion is deferred until Phase 3
//   when predecessor exit values are known), and move the insertion point to
//   this instruction
// - if this is a setreg instruction we treat it as an incompatible instruction.
//   This is sub-optimal but avoids some nasty corner cases, and is expected to
//   occur very rarely.
// - on exit we have set the Require, Change, and initial Exit modes.
void SIModeRegister::processBlockPhase1(MachineBasicBlock &MBB,
                                        const SIInstrInfo *TII) {
  auto NewInfo = llvm::make_unique<BlockData>();
  MachineInstr *InsertionPoint = nullptr;
  // RequirePending is used to indicate whether we are collecting the initial
  // requirements for the block, and need to defer the first InsertionPoint to
  // Phase 3. It is set to false once we have set FirstInsertionPoint, or when
  // we discover an explict setreg that means this block doesn't have any
  // initial requirements.
  bool RequirePending = true;
  Status IPChange;
  for (MachineInstr &MI : MBB) {
    Status InstrMode = getInstructionMode(MI, TII);
    if ((MI.getOpcode() == AMDGPU::S_SETREG_B32) ||
        (MI.getOpcode() == AMDGPU::S_SETREG_IMM32_B32)) {
      // We preserve any explicit mode register setreg instruction we encounter,
      // as we assume it has been inserted by a higher authority (this is
      // likely to be a very rare occurrence).
      unsigned Dst = TII->getNamedOperand(MI, AMDGPU::OpName::simm16)->getImm();
      if (((Dst & AMDGPU::Hwreg::ID_MASK_) >> AMDGPU::Hwreg::ID_SHIFT_) !=
          AMDGPU::Hwreg::ID_MODE)
        continue;

      unsigned Width = ((Dst & AMDGPU::Hwreg::WIDTH_M1_MASK_) >>
                        AMDGPU::Hwreg::WIDTH_M1_SHIFT_) +
                       1;
      unsigned Offset =
          (Dst & AMDGPU::Hwreg::OFFSET_MASK_) >> AMDGPU::Hwreg::OFFSET_SHIFT_;
      unsigned Mask = ((1 << Width) - 1) << Offset;

      // If an InsertionPoint is set we will insert a setreg there.
      if (InsertionPoint) {
        insertSetreg(MBB, InsertionPoint, TII, IPChange.delta(NewInfo->Change));
        InsertionPoint = nullptr;
      }
      // If this is an immediate then we know the value being set, but if it is
      // not an immediate then we treat the modified bits of the mode register
      // as unknown.
      if (MI.getOpcode() == AMDGPU::S_SETREG_IMM32_B32) {
        unsigned Val = TII->getNamedOperand(MI, AMDGPU::OpName::imm)->getImm();
        unsigned Mode = (Val << Offset) & Mask;
        Status Setreg = Status(Mask, Mode);
        // If we haven't already set the initial requirements for the block we
        // don't need to as the requirements start from this explicit setreg.
        RequirePending = false;
        NewInfo->Change = NewInfo->Change.merge(Setreg);
      } else {
        NewInfo->Change = NewInfo->Change.mergeUnknown(Mask);
      }
    } else if (!NewInfo->Change.isCompatible(InstrMode)) {
      // This instruction uses the Mode register and its requirements aren't
      // compatible with the current mode.
      if (InsertionPoint) {
        // If the required mode change cannot be included in the current
        // InsertionPoint changes, we need a setreg and start a new
        // InsertionPoint.
        if (!IPChange.delta(NewInfo->Change).isCombinable(InstrMode)) {
          if (RequirePending) {
            // This is the first insertionPoint in the block so we will defer
            // the insertion of the setreg to Phase 3 where we know whether or
            // not it is actually needed.
            NewInfo->FirstInsertionPoint = InsertionPoint;
            NewInfo->Require = NewInfo->Change;
            RequirePending = false;
          } else {
            insertSetreg(MBB, InsertionPoint, TII,
                         IPChange.delta(NewInfo->Change));
            IPChange = NewInfo->Change;
          }
          // Set the new InsertionPoint
          InsertionPoint = &MI;
        }
        NewInfo->Change = NewInfo->Change.merge(InstrMode);
      } else {
        // No InsertionPoint is currently set - this is either the first in
        // the block or we have previously seen an explicit setreg.
        InsertionPoint = &MI;
        IPChange = NewInfo->Change;
        NewInfo->Change = NewInfo->Change.merge(InstrMode);
      }
    }
  }
  if (RequirePending) {
    // If we haven't yet set the initial requirements for the block we set them
    // now.
    NewInfo->FirstInsertionPoint = InsertionPoint;
    NewInfo->Require = NewInfo->Change;
  } else if (InsertionPoint) {
    // We need to insert a setreg at the InsertionPoint
    insertSetreg(MBB, InsertionPoint, TII, IPChange.delta(NewInfo->Change));
  }
  NewInfo->Exit = NewInfo->Change;
  BlockInfo[MBB.getNumber()] = std::move(NewInfo);
}

// In Phase 2 we revisit each block and calculate the common Mode register
// value provided by all predecessor blocks. If the Exit value for the block
// is changed, then we add the successor blocks to the worklist so that the
// exit value is propagated.
void SIModeRegister::processBlockPhase2(MachineBasicBlock &MBB,
                                        const SIInstrInfo *TII) {
//  BlockData *BI = BlockInfo[MBB.getNumber()];
  unsigned ThisBlock = MBB.getNumber();
  if (MBB.pred_empty()) {
    // There are no predecessors, so use the default starting status.
    BlockInfo[ThisBlock]->Pred = DefaultStatus;
  } else {
    // Build a status that is common to all the predecessors by intersecting
    // all the predecessor exit status values.
    MachineBasicBlock::pred_iterator P = MBB.pred_begin(), E = MBB.pred_end();
    MachineBasicBlock &PB = *(*P);
    BlockInfo[ThisBlock]->Pred = BlockInfo[PB.getNumber()]->Exit;

    for (P = std::next(P); P != E; P = std::next(P)) {
      MachineBasicBlock *Pred = *P;
      BlockInfo[ThisBlock]->Pred = BlockInfo[ThisBlock]->Pred.intersect(BlockInfo[Pred->getNumber()]->Exit);
    }
  }
  Status TmpStatus = BlockInfo[ThisBlock]->Pred.merge(BlockInfo[ThisBlock]->Change);
  if (BlockInfo[ThisBlock]->Exit != TmpStatus) {
    BlockInfo[ThisBlock]->Exit = TmpStatus;
    // Add the successors to the work list so we can propagate the changed exit
    // status.
    for (MachineBasicBlock::succ_iterator S = MBB.succ_begin(),
                                          E = MBB.succ_end();
         S != E; S = std::next(S)) {
      MachineBasicBlock &B = *(*S);
      Phase2List.push(&B);
    }
  }
}

// In Phase 3 we revisit each block and if it has an insertion point defined we
// check whether the predecessor mode meets the block's entry requirements. If
// not we insert an appropriate setreg instruction to modify the Mode register.
void SIModeRegister::processBlockPhase3(MachineBasicBlock &MBB,
                                        const SIInstrInfo *TII) {
//  BlockData *BI = BlockInfo[MBB.getNumber()];
  unsigned ThisBlock = MBB.getNumber();
  if (!BlockInfo[ThisBlock]->Pred.isCompatible(BlockInfo[ThisBlock]->Require)) {
    Status Delta = BlockInfo[ThisBlock]->Pred.delta(BlockInfo[ThisBlock]->Require);
    if (BlockInfo[ThisBlock]->FirstInsertionPoint)
      insertSetreg(MBB, BlockInfo[ThisBlock]->FirstInsertionPoint, TII, Delta);
    else
      insertSetreg(MBB, &MBB.instr_front(), TII, Delta);
  }
}

bool SIModeRegister::runOnMachineFunction(MachineFunction &MF) {
  BlockInfo.resize(MF.getNumBlockIDs());
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();

  // Processing is performed in a number of phases

  // Phase 1 - determine the initial mode required by each block, and add setreg
  // instructions for intra block requirements.
  for (MachineBasicBlock &BB : MF)
    processBlockPhase1(BB, TII);

  // Phase 2 - determine the exit mode from each block. We add all blocks to the
  // list here, but will also add any that need to be revisited during Phase 2
  // processing.
  for (MachineBasicBlock &BB : MF)
    Phase2List.push(&BB);
  while (!Phase2List.empty()) {
    processBlockPhase2(*Phase2List.front(), TII);
    Phase2List.pop();
  }

  // Phase 3 - add an initial setreg to each block where the required entry mode
  // is not satisfied by the exit mode of all its predecessors.
  for (MachineBasicBlock &BB : MF)
    processBlockPhase3(BB, TII);

  BlockInfo.clear();

  return NumSetregInserted > 0;
}
