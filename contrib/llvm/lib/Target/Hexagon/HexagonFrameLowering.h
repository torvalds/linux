//==- HexagonFrameLowering.h - Define frame lowering for Hexagon -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONFRAMELOWERING_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONFRAMELOWERING_H

#include "Hexagon.h"
#include "HexagonBlockRanges.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include <vector>

namespace llvm {

class BitVector;
class HexagonInstrInfo;
class HexagonRegisterInfo;
class MachineFunction;
class MachineInstr;
class MachineRegisterInfo;
class TargetRegisterClass;

class HexagonFrameLowering : public TargetFrameLowering {
public:
  explicit HexagonFrameLowering()
      : TargetFrameLowering(StackGrowsDown, 8, 0, 1, true) {}

  // All of the prolog/epilog functionality, including saving and restoring
  // callee-saved registers is handled in emitPrologue. This is to have the
  // logic for shrink-wrapping in one place.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const
      override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const
      override {}

  bool enableCalleeSaveSkip(const MachineFunction &MF) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator MI, const std::vector<CalleeSavedInfo> &CSI,
      const TargetRegisterInfo *TRI) const override {
    return true;
  }

  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator MI, std::vector<CalleeSavedInfo> &CSI,
      const TargetRegisterInfo *TRI) const override {
    return true;
  }

  bool hasReservedCallFrame(const MachineFunction &MF) const override {
    // We always reserve call frame as a part of the initial stack allocation.
    return true;
  }

  bool canSimplifyCallFramePseudos(const MachineFunction &MF) const override {
    // Override this function to avoid calling hasFP before CSI is set
    // (the default implementation calls hasFP).
    return true;
  }

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;
  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
      RegScavenger *RS = nullptr) const override;
  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
      RegScavenger *RS) const override;

  bool targetHandlesStackFrameRounding() const override {
    return true;
  }

  int getFrameIndexReference(const MachineFunction &MF, int FI,
      unsigned &FrameReg) const override;
  bool hasFP(const MachineFunction &MF) const override;

  const SpillSlot *getCalleeSavedSpillSlots(unsigned &NumEntries)
      const override {
    static const SpillSlot Offsets[] = {
      { Hexagon::R17, -4 }, { Hexagon::R16, -8 }, { Hexagon::D8, -8 },
      { Hexagon::R19, -12 }, { Hexagon::R18, -16 }, { Hexagon::D9, -16 },
      { Hexagon::R21, -20 }, { Hexagon::R20, -24 }, { Hexagon::D10, -24 },
      { Hexagon::R23, -28 }, { Hexagon::R22, -32 }, { Hexagon::D11, -32 },
      { Hexagon::R25, -36 }, { Hexagon::R24, -40 }, { Hexagon::D12, -40 },
      { Hexagon::R27, -44 }, { Hexagon::R26, -48 }, { Hexagon::D13, -48 }
    };
    NumEntries = array_lengthof(Offsets);
    return Offsets;
  }

  bool assignCalleeSavedSpillSlots(MachineFunction &MF,
      const TargetRegisterInfo *TRI, std::vector<CalleeSavedInfo> &CSI)
      const override;

  bool needsAligna(const MachineFunction &MF) const;
  const MachineInstr *getAlignaInstr(const MachineFunction &MF) const;

  void insertCFIInstructions(MachineFunction &MF) const;

private:
  using CSIVect = std::vector<CalleeSavedInfo>;

  void expandAlloca(MachineInstr *AI, const HexagonInstrInfo &TII,
      unsigned SP, unsigned CF) const;
  void insertPrologueInBlock(MachineBasicBlock &MBB, bool PrologueStubs) const;
  void insertEpilogueInBlock(MachineBasicBlock &MBB) const;
  void insertAllocframe(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator InsertPt, unsigned NumBytes) const;
  bool insertCSRSpillsInBlock(MachineBasicBlock &MBB, const CSIVect &CSI,
      const HexagonRegisterInfo &HRI, bool &PrologueStubs) const;
  bool insertCSRRestoresInBlock(MachineBasicBlock &MBB, const CSIVect &CSI,
      const HexagonRegisterInfo &HRI) const;
  void updateEntryPaths(MachineFunction &MF, MachineBasicBlock &SaveB) const;
  bool updateExitPaths(MachineBasicBlock &MBB, MachineBasicBlock &RestoreB,
      BitVector &DoneT, BitVector &DoneF, BitVector &Path) const;
  void insertCFIInstructionsAt(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator At) const;

  void adjustForCalleeSavedRegsSpillCall(MachineFunction &MF) const;

  bool expandCopy(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandStoreInt(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandLoadInt(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandStoreVecPred(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandLoadVecPred(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandStoreVec2(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandLoadVec2(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandStoreVec(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandLoadVec(MachineBasicBlock &B, MachineBasicBlock::iterator It,
      MachineRegisterInfo &MRI, const HexagonInstrInfo &HII,
      SmallVectorImpl<unsigned> &NewRegs) const;
  bool expandSpillMacros(MachineFunction &MF,
      SmallVectorImpl<unsigned> &NewRegs) const;

  unsigned findPhysReg(MachineFunction &MF, HexagonBlockRanges::IndexRange &FIR,
      HexagonBlockRanges::InstrIndexMap &IndexMap,
      HexagonBlockRanges::RegToRangeMap &DeadMap,
      const TargetRegisterClass *RC) const;
  void optimizeSpillSlots(MachineFunction &MF,
      SmallVectorImpl<unsigned> &VRegs) const;

  void findShrunkPrologEpilog(MachineFunction &MF, MachineBasicBlock *&PrologB,
      MachineBasicBlock *&EpilogB) const;

  void addCalleeSaveRegistersAsImpOperand(MachineInstr *MI, const CSIVect &CSI,
      bool IsDef, bool IsKill) const;
  bool shouldInlineCSR(const MachineFunction &MF, const CSIVect &CSI) const;
  bool useSpillFunction(const MachineFunction &MF, const CSIVect &CSI) const;
  bool useRestoreFunction(const MachineFunction &MF, const CSIVect &CSI) const;
  bool mayOverflowFrameOffset(MachineFunction &MF) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONFRAMELOWERING_H
