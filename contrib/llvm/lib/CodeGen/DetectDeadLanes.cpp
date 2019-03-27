//===- DetectDeadLanes.cpp - SubRegister Lane Usage Analysis --*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Analysis that tracks defined/used subregister lanes across COPY instructions
/// and instructions that get lowered to a COPY (PHI, REG_SEQUENCE,
/// INSERT_SUBREG, EXTRACT_SUBREG).
/// The information is used to detect dead definitions and the usage of
/// (completely) undefined values and mark the operands as such.
/// This pass is necessary because the dead/undef status is not obvious anymore
/// when subregisters are involved.
///
/// Example:
///    %0 = some definition
///    %1 = IMPLICIT_DEF
///    %2 = REG_SEQUENCE %0, sub0, %1, sub1
///    %3 = EXTRACT_SUBREG %2, sub1
///       = use %3
/// The %0 definition is dead and %3 contains an undefined value.
//
//===----------------------------------------------------------------------===//

#include <deque>
#include <vector>

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "detect-dead-lanes"

namespace {

/// Contains a bitmask of which lanes of a given virtual register are
/// defined and which ones are actually used.
struct VRegInfo {
  LaneBitmask UsedLanes;
  LaneBitmask DefinedLanes;
};

class DetectDeadLanes : public MachineFunctionPass {
public:
  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;
  DetectDeadLanes() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "Detect Dead Lanes"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  /// Add used lane bits on the register used by operand \p MO. This translates
  /// the bitmask based on the operands subregister, and puts the register into
  /// the worklist if any new bits were added.
  void addUsedLanesOnOperand(const MachineOperand &MO, LaneBitmask UsedLanes);

  /// Given a bitmask \p UsedLanes for the used lanes on a def output of a
  /// COPY-like instruction determine the lanes used on the use operands
  /// and call addUsedLanesOnOperand() for them.
  void transferUsedLanesStep(const MachineInstr &MI, LaneBitmask UsedLanes);

  /// Given a use regiser operand \p Use and a mask of defined lanes, check
  /// if the operand belongs to a lowersToCopies() instruction, transfer the
  /// mask to the def and put the instruction into the worklist.
  void transferDefinedLanesStep(const MachineOperand &Use,
                                LaneBitmask DefinedLanes);

  /// Given a mask \p DefinedLanes of lanes defined at operand \p OpNum
  /// of COPY-like instruction, determine which lanes are defined at the output
  /// operand \p Def.
  LaneBitmask transferDefinedLanes(const MachineOperand &Def, unsigned OpNum,
                                   LaneBitmask DefinedLanes) const;

  /// Given a mask \p UsedLanes used from the output of instruction \p MI
  /// determine which lanes are used from operand \p MO of this instruction.
  LaneBitmask transferUsedLanes(const MachineInstr &MI, LaneBitmask UsedLanes,
                                const MachineOperand &MO) const;

  bool runOnce(MachineFunction &MF);

  LaneBitmask determineInitialDefinedLanes(unsigned Reg);
  LaneBitmask determineInitialUsedLanes(unsigned Reg);

  bool isUndefRegAtInput(const MachineOperand &MO,
                         const VRegInfo &RegInfo) const;

  bool isUndefInput(const MachineOperand &MO, bool *CrossCopy) const;

  const MachineRegisterInfo *MRI;
  const TargetRegisterInfo *TRI;

  void PutInWorklist(unsigned RegIdx) {
    if (WorklistMembers.test(RegIdx))
      return;
    WorklistMembers.set(RegIdx);
    Worklist.push_back(RegIdx);
  }

  VRegInfo *VRegInfos;
  /// Worklist containing virtreg indexes.
  std::deque<unsigned> Worklist;
  BitVector WorklistMembers;
  /// This bitvector is set for each vreg index where the vreg is defined
  /// by an instruction where lowersToCopies()==true.
  BitVector DefinedByCopy;
};

} // end anonymous namespace

char DetectDeadLanes::ID = 0;
char &llvm::DetectDeadLanesID = DetectDeadLanes::ID;

INITIALIZE_PASS(DetectDeadLanes, DEBUG_TYPE, "Detect Dead Lanes", false, false)

/// Returns true if \p MI will get lowered to a series of COPY instructions.
/// We call this a COPY-like instruction.
static bool lowersToCopies(const MachineInstr &MI) {
  // Note: We could support instructions with MCInstrDesc::isRegSequenceLike(),
  // isExtractSubRegLike(), isInsertSubregLike() in the future even though they
  // are not lowered to a COPY.
  switch (MI.getOpcode()) {
  case TargetOpcode::COPY:
  case TargetOpcode::PHI:
  case TargetOpcode::INSERT_SUBREG:
  case TargetOpcode::REG_SEQUENCE:
  case TargetOpcode::EXTRACT_SUBREG:
    return true;
  }
  return false;
}

static bool isCrossCopy(const MachineRegisterInfo &MRI,
                        const MachineInstr &MI,
                        const TargetRegisterClass *DstRC,
                        const MachineOperand &MO) {
  assert(lowersToCopies(MI));
  unsigned SrcReg = MO.getReg();
  const TargetRegisterClass *SrcRC = MRI.getRegClass(SrcReg);
  if (DstRC == SrcRC)
    return false;

  unsigned SrcSubIdx = MO.getSubReg();

  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  unsigned DstSubIdx = 0;
  switch (MI.getOpcode()) {
  case TargetOpcode::INSERT_SUBREG:
    if (MI.getOperandNo(&MO) == 2)
      DstSubIdx = MI.getOperand(3).getImm();
    break;
  case TargetOpcode::REG_SEQUENCE: {
    unsigned OpNum = MI.getOperandNo(&MO);
    DstSubIdx = MI.getOperand(OpNum+1).getImm();
    break;
  }
  case TargetOpcode::EXTRACT_SUBREG: {
    unsigned SubReg = MI.getOperand(2).getImm();
    SrcSubIdx = TRI.composeSubRegIndices(SubReg, SrcSubIdx);
  }
  }

  unsigned PreA, PreB; // Unused.
  if (SrcSubIdx && DstSubIdx)
    return !TRI.getCommonSuperRegClass(SrcRC, SrcSubIdx, DstRC, DstSubIdx, PreA,
                                       PreB);
  if (SrcSubIdx)
    return !TRI.getMatchingSuperRegClass(SrcRC, DstRC, SrcSubIdx);
  if (DstSubIdx)
    return !TRI.getMatchingSuperRegClass(DstRC, SrcRC, DstSubIdx);
  return !TRI.getCommonSubClass(SrcRC, DstRC);
}

void DetectDeadLanes::addUsedLanesOnOperand(const MachineOperand &MO,
                                            LaneBitmask UsedLanes) {
  if (!MO.readsReg())
    return;
  unsigned MOReg = MO.getReg();
  if (!TargetRegisterInfo::isVirtualRegister(MOReg))
    return;

  unsigned MOSubReg = MO.getSubReg();
  if (MOSubReg != 0)
    UsedLanes = TRI->composeSubRegIndexLaneMask(MOSubReg, UsedLanes);
  UsedLanes &= MRI->getMaxLaneMaskForVReg(MOReg);

  unsigned MORegIdx = TargetRegisterInfo::virtReg2Index(MOReg);
  VRegInfo &MORegInfo = VRegInfos[MORegIdx];
  LaneBitmask PrevUsedLanes = MORegInfo.UsedLanes;
  // Any change at all?
  if ((UsedLanes & ~PrevUsedLanes).none())
    return;

  // Set UsedLanes and remember instruction for further propagation.
  MORegInfo.UsedLanes = PrevUsedLanes | UsedLanes;
  if (DefinedByCopy.test(MORegIdx))
    PutInWorklist(MORegIdx);
}

void DetectDeadLanes::transferUsedLanesStep(const MachineInstr &MI,
                                            LaneBitmask UsedLanes) {
  for (const MachineOperand &MO : MI.uses()) {
    if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
      continue;
    LaneBitmask UsedOnMO = transferUsedLanes(MI, UsedLanes, MO);
    addUsedLanesOnOperand(MO, UsedOnMO);
  }
}

LaneBitmask DetectDeadLanes::transferUsedLanes(const MachineInstr &MI,
                                               LaneBitmask UsedLanes,
                                               const MachineOperand &MO) const {
  unsigned OpNum = MI.getOperandNo(&MO);
  assert(lowersToCopies(MI) && DefinedByCopy[
           TargetRegisterInfo::virtReg2Index(MI.getOperand(0).getReg())]);

  switch (MI.getOpcode()) {
  case TargetOpcode::COPY:
  case TargetOpcode::PHI:
    return UsedLanes;
  case TargetOpcode::REG_SEQUENCE: {
    assert(OpNum % 2 == 1);
    unsigned SubIdx = MI.getOperand(OpNum + 1).getImm();
    return TRI->reverseComposeSubRegIndexLaneMask(SubIdx, UsedLanes);
  }
  case TargetOpcode::INSERT_SUBREG: {
    unsigned SubIdx = MI.getOperand(3).getImm();
    LaneBitmask MO2UsedLanes =
        TRI->reverseComposeSubRegIndexLaneMask(SubIdx, UsedLanes);
    if (OpNum == 2)
      return MO2UsedLanes;

    const MachineOperand &Def = MI.getOperand(0);
    unsigned DefReg = Def.getReg();
    const TargetRegisterClass *RC = MRI->getRegClass(DefReg);
    LaneBitmask MO1UsedLanes;
    if (RC->CoveredBySubRegs)
      MO1UsedLanes = UsedLanes & ~TRI->getSubRegIndexLaneMask(SubIdx);
    else
      MO1UsedLanes = RC->LaneMask;

    assert(OpNum == 1);
    return MO1UsedLanes;
  }
  case TargetOpcode::EXTRACT_SUBREG: {
    assert(OpNum == 1);
    unsigned SubIdx = MI.getOperand(2).getImm();
    return TRI->composeSubRegIndexLaneMask(SubIdx, UsedLanes);
  }
  default:
    llvm_unreachable("function must be called with COPY-like instruction");
  }
}

void DetectDeadLanes::transferDefinedLanesStep(const MachineOperand &Use,
                                               LaneBitmask DefinedLanes) {
  if (!Use.readsReg())
    return;
  // Check whether the operand writes a vreg and is part of a COPY-like
  // instruction.
  const MachineInstr &MI = *Use.getParent();
  if (MI.getDesc().getNumDefs() != 1)
    return;
  // FIXME: PATCHPOINT instructions announce a Def that does not always exist,
  // they really need to be modeled differently!
  if (MI.getOpcode() == TargetOpcode::PATCHPOINT)
    return;
  const MachineOperand &Def = *MI.defs().begin();
  unsigned DefReg = Def.getReg();
  if (!TargetRegisterInfo::isVirtualRegister(DefReg))
    return;
  unsigned DefRegIdx = TargetRegisterInfo::virtReg2Index(DefReg);
  if (!DefinedByCopy.test(DefRegIdx))
    return;

  unsigned OpNum = MI.getOperandNo(&Use);
  DefinedLanes =
      TRI->reverseComposeSubRegIndexLaneMask(Use.getSubReg(), DefinedLanes);
  DefinedLanes = transferDefinedLanes(Def, OpNum, DefinedLanes);

  VRegInfo &RegInfo = VRegInfos[DefRegIdx];
  LaneBitmask PrevDefinedLanes = RegInfo.DefinedLanes;
  // Any change at all?
  if ((DefinedLanes & ~PrevDefinedLanes).none())
    return;

  RegInfo.DefinedLanes = PrevDefinedLanes | DefinedLanes;
  PutInWorklist(DefRegIdx);
}

LaneBitmask DetectDeadLanes::transferDefinedLanes(const MachineOperand &Def,
    unsigned OpNum, LaneBitmask DefinedLanes) const {
  const MachineInstr &MI = *Def.getParent();
  // Translate DefinedLanes if necessary.
  switch (MI.getOpcode()) {
  case TargetOpcode::REG_SEQUENCE: {
    unsigned SubIdx = MI.getOperand(OpNum + 1).getImm();
    DefinedLanes = TRI->composeSubRegIndexLaneMask(SubIdx, DefinedLanes);
    DefinedLanes &= TRI->getSubRegIndexLaneMask(SubIdx);
    break;
  }
  case TargetOpcode::INSERT_SUBREG: {
    unsigned SubIdx = MI.getOperand(3).getImm();
    if (OpNum == 2) {
      DefinedLanes = TRI->composeSubRegIndexLaneMask(SubIdx, DefinedLanes);
      DefinedLanes &= TRI->getSubRegIndexLaneMask(SubIdx);
    } else {
      assert(OpNum == 1 && "INSERT_SUBREG must have two operands");
      // Ignore lanes defined by operand 2.
      DefinedLanes &= ~TRI->getSubRegIndexLaneMask(SubIdx);
    }
    break;
  }
  case TargetOpcode::EXTRACT_SUBREG: {
    unsigned SubIdx = MI.getOperand(2).getImm();
    assert(OpNum == 1 && "EXTRACT_SUBREG must have one register operand only");
    DefinedLanes = TRI->reverseComposeSubRegIndexLaneMask(SubIdx, DefinedLanes);
    break;
  }
  case TargetOpcode::COPY:
  case TargetOpcode::PHI:
    break;
  default:
    llvm_unreachable("function must be called with COPY-like instruction");
  }

  assert(Def.getSubReg() == 0 &&
         "Should not have subregister defs in machine SSA phase");
  DefinedLanes &= MRI->getMaxLaneMaskForVReg(Def.getReg());
  return DefinedLanes;
}

LaneBitmask DetectDeadLanes::determineInitialDefinedLanes(unsigned Reg) {
  // Live-In or unused registers have no definition but are considered fully
  // defined.
  if (!MRI->hasOneDef(Reg))
    return LaneBitmask::getAll();

  const MachineOperand &Def = *MRI->def_begin(Reg);
  const MachineInstr &DefMI = *Def.getParent();
  if (lowersToCopies(DefMI)) {
    // Start optimisatically with no used or defined lanes for copy
    // instructions. The following dataflow analysis will add more bits.
    unsigned RegIdx = TargetRegisterInfo::virtReg2Index(Reg);
    DefinedByCopy.set(RegIdx);
    PutInWorklist(RegIdx);

    if (Def.isDead())
      return LaneBitmask::getNone();

    // COPY/PHI can copy across unrelated register classes (example: float/int)
    // with incompatible subregister structure. Do not include these in the
    // dataflow analysis since we cannot transfer lanemasks in a meaningful way.
    const TargetRegisterClass *DefRC = MRI->getRegClass(Reg);

    // Determine initially DefinedLanes.
    LaneBitmask DefinedLanes;
    for (const MachineOperand &MO : DefMI.uses()) {
      if (!MO.isReg() || !MO.readsReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;

      LaneBitmask MODefinedLanes;
      if (TargetRegisterInfo::isPhysicalRegister(MOReg)) {
        MODefinedLanes = LaneBitmask::getAll();
      } else if (isCrossCopy(*MRI, DefMI, DefRC, MO)) {
        MODefinedLanes = LaneBitmask::getAll();
      } else {
        assert(TargetRegisterInfo::isVirtualRegister(MOReg));
        if (MRI->hasOneDef(MOReg)) {
          const MachineOperand &MODef = *MRI->def_begin(MOReg);
          const MachineInstr &MODefMI = *MODef.getParent();
          // Bits from copy-like operations will be added later.
          if (lowersToCopies(MODefMI) || MODefMI.isImplicitDef())
            continue;
        }
        unsigned MOSubReg = MO.getSubReg();
        MODefinedLanes = MRI->getMaxLaneMaskForVReg(MOReg);
        MODefinedLanes = TRI->reverseComposeSubRegIndexLaneMask(
            MOSubReg, MODefinedLanes);
      }

      unsigned OpNum = DefMI.getOperandNo(&MO);
      DefinedLanes |= transferDefinedLanes(Def, OpNum, MODefinedLanes);
    }
    return DefinedLanes;
  }
  if (DefMI.isImplicitDef() || Def.isDead())
    return LaneBitmask::getNone();

  assert(Def.getSubReg() == 0 &&
         "Should not have subregister defs in machine SSA phase");
  return MRI->getMaxLaneMaskForVReg(Reg);
}

LaneBitmask DetectDeadLanes::determineInitialUsedLanes(unsigned Reg) {
  LaneBitmask UsedLanes = LaneBitmask::getNone();
  for (const MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    if (!MO.readsReg())
      continue;

    const MachineInstr &UseMI = *MO.getParent();
    if (UseMI.isKill())
      continue;

    unsigned SubReg = MO.getSubReg();
    if (lowersToCopies(UseMI)) {
      assert(UseMI.getDesc().getNumDefs() == 1);
      const MachineOperand &Def = *UseMI.defs().begin();
      unsigned DefReg = Def.getReg();
      // The used lanes of COPY-like instruction operands are determined by the
      // following dataflow analysis.
      if (TargetRegisterInfo::isVirtualRegister(DefReg)) {
        // But ignore copies across incompatible register classes.
        bool CrossCopy = false;
        if (lowersToCopies(UseMI)) {
          const TargetRegisterClass *DstRC = MRI->getRegClass(DefReg);
          CrossCopy = isCrossCopy(*MRI, UseMI, DstRC, MO);
          if (CrossCopy)
            LLVM_DEBUG(dbgs() << "Copy across incompatible classes: " << UseMI);
        }

        if (!CrossCopy)
          continue;
      }
    }

    // Shortcut: All lanes are used.
    if (SubReg == 0)
      return MRI->getMaxLaneMaskForVReg(Reg);

    UsedLanes |= TRI->getSubRegIndexLaneMask(SubReg);
  }
  return UsedLanes;
}

bool DetectDeadLanes::isUndefRegAtInput(const MachineOperand &MO,
                                        const VRegInfo &RegInfo) const {
  unsigned SubReg = MO.getSubReg();
  LaneBitmask Mask = TRI->getSubRegIndexLaneMask(SubReg);
  return (RegInfo.DefinedLanes & RegInfo.UsedLanes & Mask).none();
}

bool DetectDeadLanes::isUndefInput(const MachineOperand &MO,
                                   bool *CrossCopy) const {
  if (!MO.isUse())
    return false;
  const MachineInstr &MI = *MO.getParent();
  if (!lowersToCopies(MI))
    return false;
  const MachineOperand &Def = MI.getOperand(0);
  unsigned DefReg = Def.getReg();
  if (!TargetRegisterInfo::isVirtualRegister(DefReg))
    return false;
  unsigned DefRegIdx = TargetRegisterInfo::virtReg2Index(DefReg);
  if (!DefinedByCopy.test(DefRegIdx))
    return false;

  const VRegInfo &DefRegInfo = VRegInfos[DefRegIdx];
  LaneBitmask UsedLanes = transferUsedLanes(MI, DefRegInfo.UsedLanes, MO);
  if (UsedLanes.any())
    return false;

  unsigned MOReg = MO.getReg();
  if (TargetRegisterInfo::isVirtualRegister(MOReg)) {
    const TargetRegisterClass *DstRC = MRI->getRegClass(DefReg);
    *CrossCopy = isCrossCopy(*MRI, MI, DstRC, MO);
  }
  return true;
}

bool DetectDeadLanes::runOnce(MachineFunction &MF) {
  // First pass: Populate defs/uses of vregs with initial values
  unsigned NumVirtRegs = MRI->getNumVirtRegs();
  for (unsigned RegIdx = 0; RegIdx < NumVirtRegs; ++RegIdx) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(RegIdx);

    // Determine used/defined lanes and add copy instructions to worklist.
    VRegInfo &Info = VRegInfos[RegIdx];
    Info.DefinedLanes = determineInitialDefinedLanes(Reg);
    Info.UsedLanes = determineInitialUsedLanes(Reg);
  }

  // Iterate as long as defined lanes/used lanes keep changing.
  while (!Worklist.empty()) {
    unsigned RegIdx = Worklist.front();
    Worklist.pop_front();
    WorklistMembers.reset(RegIdx);
    VRegInfo &Info = VRegInfos[RegIdx];
    unsigned Reg = TargetRegisterInfo::index2VirtReg(RegIdx);

    // Transfer UsedLanes to operands of DefMI (backwards dataflow).
    MachineOperand &Def = *MRI->def_begin(Reg);
    const MachineInstr &MI = *Def.getParent();
    transferUsedLanesStep(MI, Info.UsedLanes);
    // Transfer DefinedLanes to users of Reg (forward dataflow).
    for (const MachineOperand &MO : MRI->use_nodbg_operands(Reg))
      transferDefinedLanesStep(MO, Info.DefinedLanes);
  }

  LLVM_DEBUG(dbgs() << "Defined/Used lanes:\n"; for (unsigned RegIdx = 0;
                                                     RegIdx < NumVirtRegs;
                                                     ++RegIdx) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(RegIdx);
    const VRegInfo &Info = VRegInfos[RegIdx];
    dbgs() << printReg(Reg, nullptr)
           << " Used: " << PrintLaneMask(Info.UsedLanes)
           << " Def: " << PrintLaneMask(Info.DefinedLanes) << '\n';
  } dbgs() << "\n";);

  bool Again = false;
  // Mark operands as dead/unused.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg())
          continue;
        unsigned Reg = MO.getReg();
        if (!TargetRegisterInfo::isVirtualRegister(Reg))
          continue;
        unsigned RegIdx = TargetRegisterInfo::virtReg2Index(Reg);
        const VRegInfo &RegInfo = VRegInfos[RegIdx];
        if (MO.isDef() && !MO.isDead() && RegInfo.UsedLanes.none()) {
          LLVM_DEBUG(dbgs()
                     << "Marking operand '" << MO << "' as dead in " << MI);
          MO.setIsDead();
        }
        if (MO.readsReg()) {
          bool CrossCopy = false;
          if (isUndefRegAtInput(MO, RegInfo)) {
            LLVM_DEBUG(dbgs()
                       << "Marking operand '" << MO << "' as undef in " << MI);
            MO.setIsUndef();
          } else if (isUndefInput(MO, &CrossCopy)) {
            LLVM_DEBUG(dbgs()
                       << "Marking operand '" << MO << "' as undef in " << MI);
            MO.setIsUndef();
            if (CrossCopy)
              Again = true;
          }
        }
      }
    }
  }

  return Again;
}

bool DetectDeadLanes::runOnMachineFunction(MachineFunction &MF) {
  // Don't bother if we won't track subregister liveness later.  This pass is
  // required for correctness if subregister liveness is enabled because the
  // register coalescer cannot deal with hidden dead defs. However without
  // subregister liveness enabled, the expected benefits of this pass are small
  // so we safe the compile time.
  MRI = &MF.getRegInfo();
  if (!MRI->subRegLivenessEnabled()) {
    LLVM_DEBUG(dbgs() << "Skipping Detect dead lanes pass\n");
    return false;
  }

  TRI = MRI->getTargetRegisterInfo();

  unsigned NumVirtRegs = MRI->getNumVirtRegs();
  VRegInfos = new VRegInfo[NumVirtRegs];
  WorklistMembers.resize(NumVirtRegs);
  DefinedByCopy.resize(NumVirtRegs);

  bool Again;
  do {
    Again = runOnce(MF);
  } while(Again);

  DefinedByCopy.clear();
  WorklistMembers.clear();
  delete[] VRegInfos;
  return true;
}
