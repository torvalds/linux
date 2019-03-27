//===-- PPCInstrInfo.cpp - PowerPC Instruction Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "PPCInstrInfo.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCHazardRecognizers.h"
#include "PPCInstrBuilder.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-instr-info"

#define GET_INSTRMAP_INFO
#define GET_INSTRINFO_CTOR_DTOR
#include "PPCGenInstrInfo.inc"

STATISTIC(NumStoreSPILLVSRRCAsVec,
          "Number of spillvsrrc spilled to stack as vec");
STATISTIC(NumStoreSPILLVSRRCAsGpr,
          "Number of spillvsrrc spilled to stack as gpr");
STATISTIC(NumGPRtoVSRSpill, "Number of gpr spills to spillvsrrc");
STATISTIC(CmpIselsConverted,
          "Number of ISELs that depend on comparison of constants converted");
STATISTIC(MissedConvertibleImmediateInstrs,
          "Number of compare-immediate instructions fed by constants");
STATISTIC(NumRcRotatesConvertedToRcAnd,
          "Number of record-form rotates converted to record-form andi");

static cl::
opt<bool> DisableCTRLoopAnal("disable-ppc-ctrloop-analysis", cl::Hidden,
            cl::desc("Disable analysis for CTR loops"));

static cl::opt<bool> DisableCmpOpt("disable-ppc-cmp-opt",
cl::desc("Disable compare instruction optimization"), cl::Hidden);

static cl::opt<bool> VSXSelfCopyCrash("crash-on-ppc-vsx-self-copy",
cl::desc("Causes the backend to crash instead of generating a nop VSX copy"),
cl::Hidden);

static cl::opt<bool>
UseOldLatencyCalc("ppc-old-latency-calc", cl::Hidden,
  cl::desc("Use the old (incorrect) instruction latency calculation"));

// Index into the OpcodesForSpill array.
enum SpillOpcodeKey {
  SOK_Int4Spill,
  SOK_Int8Spill,
  SOK_Float8Spill,
  SOK_Float4Spill,
  SOK_CRSpill,
  SOK_CRBitSpill,
  SOK_VRVectorSpill,
  SOK_VSXVectorSpill,
  SOK_VectorFloat8Spill,
  SOK_VectorFloat4Spill,
  SOK_VRSaveSpill,
  SOK_QuadFloat8Spill,
  SOK_QuadFloat4Spill,
  SOK_QuadBitSpill,
  SOK_SpillToVSR,
  SOK_SPESpill,
  SOK_SPE4Spill,
  SOK_LastOpcodeSpill  // This must be last on the enum.
};

// Pin the vtable to this file.
void PPCInstrInfo::anchor() {}

PPCInstrInfo::PPCInstrInfo(PPCSubtarget &STI)
    : PPCGenInstrInfo(PPC::ADJCALLSTACKDOWN, PPC::ADJCALLSTACKUP,
                      /* CatchRetOpcode */ -1,
                      STI.isPPC64() ? PPC::BLR8 : PPC::BLR),
      Subtarget(STI), RI(STI.getTargetMachine()) {}

/// CreateTargetHazardRecognizer - Return the hazard recognizer to use for
/// this target when scheduling the DAG.
ScheduleHazardRecognizer *
PPCInstrInfo::CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                                           const ScheduleDAG *DAG) const {
  unsigned Directive =
      static_cast<const PPCSubtarget *>(STI)->getDarwinDirective();
  if (Directive == PPC::DIR_440 || Directive == PPC::DIR_A2 ||
      Directive == PPC::DIR_E500mc || Directive == PPC::DIR_E5500) {
    const InstrItineraryData *II =
        static_cast<const PPCSubtarget *>(STI)->getInstrItineraryData();
    return new ScoreboardHazardRecognizer(II, DAG);
  }

  return TargetInstrInfo::CreateTargetHazardRecognizer(STI, DAG);
}

/// CreateTargetPostRAHazardRecognizer - Return the postRA hazard recognizer
/// to use for this target when scheduling the DAG.
ScheduleHazardRecognizer *
PPCInstrInfo::CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                                 const ScheduleDAG *DAG) const {
  unsigned Directive =
      DAG->MF.getSubtarget<PPCSubtarget>().getDarwinDirective();

  // FIXME: Leaving this as-is until we have POWER9 scheduling info
  if (Directive == PPC::DIR_PWR7 || Directive == PPC::DIR_PWR8)
    return new PPCDispatchGroupSBHazardRecognizer(II, DAG);

  // Most subtargets use a PPC970 recognizer.
  if (Directive != PPC::DIR_440 && Directive != PPC::DIR_A2 &&
      Directive != PPC::DIR_E500mc && Directive != PPC::DIR_E5500) {
    assert(DAG->TII && "No InstrInfo?");

    return new PPCHazardRecognizer970(*DAG);
  }

  return new ScoreboardHazardRecognizer(II, DAG);
}

unsigned PPCInstrInfo::getInstrLatency(const InstrItineraryData *ItinData,
                                       const MachineInstr &MI,
                                       unsigned *PredCost) const {
  if (!ItinData || UseOldLatencyCalc)
    return PPCGenInstrInfo::getInstrLatency(ItinData, MI, PredCost);

  // The default implementation of getInstrLatency calls getStageLatency, but
  // getStageLatency does not do the right thing for us. While we have
  // itinerary, most cores are fully pipelined, and so the itineraries only
  // express the first part of the pipeline, not every stage. Instead, we need
  // to use the listed output operand cycle number (using operand 0 here, which
  // is an output).

  unsigned Latency = 1;
  unsigned DefClass = MI.getDesc().getSchedClass();
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isDef() || MO.isImplicit())
      continue;

    int Cycle = ItinData->getOperandCycle(DefClass, i);
    if (Cycle < 0)
      continue;

    Latency = std::max(Latency, (unsigned) Cycle);
  }

  return Latency;
}

int PPCInstrInfo::getOperandLatency(const InstrItineraryData *ItinData,
                                    const MachineInstr &DefMI, unsigned DefIdx,
                                    const MachineInstr &UseMI,
                                    unsigned UseIdx) const {
  int Latency = PPCGenInstrInfo::getOperandLatency(ItinData, DefMI, DefIdx,
                                                   UseMI, UseIdx);

  if (!DefMI.getParent())
    return Latency;

  const MachineOperand &DefMO = DefMI.getOperand(DefIdx);
  unsigned Reg = DefMO.getReg();

  bool IsRegCR;
  if (TargetRegisterInfo::isVirtualRegister(Reg)) {
    const MachineRegisterInfo *MRI =
        &DefMI.getParent()->getParent()->getRegInfo();
    IsRegCR = MRI->getRegClass(Reg)->hasSuperClassEq(&PPC::CRRCRegClass) ||
              MRI->getRegClass(Reg)->hasSuperClassEq(&PPC::CRBITRCRegClass);
  } else {
    IsRegCR = PPC::CRRCRegClass.contains(Reg) ||
              PPC::CRBITRCRegClass.contains(Reg);
  }

  if (UseMI.isBranch() && IsRegCR) {
    if (Latency < 0)
      Latency = getInstrLatency(ItinData, DefMI);

    // On some cores, there is an additional delay between writing to a condition
    // register, and using it from a branch.
    unsigned Directive = Subtarget.getDarwinDirective();
    switch (Directive) {
    default: break;
    case PPC::DIR_7400:
    case PPC::DIR_750:
    case PPC::DIR_970:
    case PPC::DIR_E5500:
    case PPC::DIR_PWR4:
    case PPC::DIR_PWR5:
    case PPC::DIR_PWR5X:
    case PPC::DIR_PWR6:
    case PPC::DIR_PWR6X:
    case PPC::DIR_PWR7:
    case PPC::DIR_PWR8:
    // FIXME: Is this needed for POWER9?
      Latency += 2;
      break;
    }
  }

  return Latency;
}

// This function does not list all associative and commutative operations, but
// only those worth feeding through the machine combiner in an attempt to
// reduce the critical path. Mostly, this means floating-point operations,
// because they have high latencies (compared to other operations, such and
// and/or, which are also associative and commutative, but have low latencies).
bool PPCInstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst) const {
  switch (Inst.getOpcode()) {
  // FP Add:
  case PPC::FADD:
  case PPC::FADDS:
  // FP Multiply:
  case PPC::FMUL:
  case PPC::FMULS:
  // Altivec Add:
  case PPC::VADDFP:
  // VSX Add:
  case PPC::XSADDDP:
  case PPC::XVADDDP:
  case PPC::XVADDSP:
  case PPC::XSADDSP:
  // VSX Multiply:
  case PPC::XSMULDP:
  case PPC::XVMULDP:
  case PPC::XVMULSP:
  case PPC::XSMULSP:
  // QPX Add:
  case PPC::QVFADD:
  case PPC::QVFADDS:
  case PPC::QVFADDSs:
  // QPX Multiply:
  case PPC::QVFMUL:
  case PPC::QVFMULS:
  case PPC::QVFMULSs:
    return true;
  default:
    return false;
  }
}

bool PPCInstrInfo::getMachineCombinerPatterns(
    MachineInstr &Root,
    SmallVectorImpl<MachineCombinerPattern> &Patterns) const {
  // Using the machine combiner in this way is potentially expensive, so
  // restrict to when aggressive optimizations are desired.
  if (Subtarget.getTargetMachine().getOptLevel() != CodeGenOpt::Aggressive)
    return false;

  // FP reassociation is only legal when we don't need strict IEEE semantics.
  if (!Root.getParent()->getParent()->getTarget().Options.UnsafeFPMath)
    return false;

  return TargetInstrInfo::getMachineCombinerPatterns(Root, Patterns);
}

// Detect 32 -> 64-bit extensions where we may reuse the low sub-register.
bool PPCInstrInfo::isCoalescableExtInstr(const MachineInstr &MI,
                                         unsigned &SrcReg, unsigned &DstReg,
                                         unsigned &SubIdx) const {
  switch (MI.getOpcode()) {
  default: return false;
  case PPC::EXTSW:
  case PPC::EXTSW_32:
  case PPC::EXTSW_32_64:
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    SubIdx = PPC::sub_32;
    return true;
  }
}

unsigned PPCInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  unsigned Opcode = MI.getOpcode();
  const unsigned *OpcodesForSpill = getLoadOpcodesForSpillArray();
  const unsigned *End = OpcodesForSpill + SOK_LastOpcodeSpill;

  if (End != std::find(OpcodesForSpill, End, Opcode)) {
    // Check for the operands added by addFrameReference (the immediate is the
    // offset which defaults to 0).
    if (MI.getOperand(1).isImm() && !MI.getOperand(1).getImm() &&
        MI.getOperand(2).isFI()) {
      FrameIndex = MI.getOperand(2).getIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

// For opcodes with the ReMaterializable flag set, this function is called to
// verify the instruction is really rematable.
bool PPCInstrInfo::isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                                     AliasAnalysis *AA) const {
  switch (MI.getOpcode()) {
  default:
    // This function should only be called for opcodes with the ReMaterializable
    // flag set.
    llvm_unreachable("Unknown rematerializable operation!");
    break;
  case PPC::LI:
  case PPC::LI8:
  case PPC::LIS:
  case PPC::LIS8:
  case PPC::QVGPCI:
  case PPC::ADDIStocHA:
  case PPC::ADDItocL:
  case PPC::LOAD_STACK_GUARD:
    return true;
  }
  return false;
}

unsigned PPCInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  unsigned Opcode = MI.getOpcode();
  const unsigned *OpcodesForSpill = getStoreOpcodesForSpillArray();
  const unsigned *End = OpcodesForSpill + SOK_LastOpcodeSpill;

  if (End != std::find(OpcodesForSpill, End, Opcode)) {
    if (MI.getOperand(1).isImm() && !MI.getOperand(1).getImm() &&
        MI.getOperand(2).isFI()) {
      FrameIndex = MI.getOperand(2).getIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

MachineInstr *PPCInstrInfo::commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                                   unsigned OpIdx1,
                                                   unsigned OpIdx2) const {
  MachineFunction &MF = *MI.getParent()->getParent();

  // Normal instructions can be commuted the obvious way.
  if (MI.getOpcode() != PPC::RLWIMI && MI.getOpcode() != PPC::RLWIMIo)
    return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
  // Note that RLWIMI can be commuted as a 32-bit instruction, but not as a
  // 64-bit instruction (so we don't handle PPC::RLWIMI8 here), because
  // changing the relative order of the mask operands might change what happens
  // to the high-bits of the mask (and, thus, the result).

  // Cannot commute if it has a non-zero rotate count.
  if (MI.getOperand(3).getImm() != 0)
    return nullptr;

  // If we have a zero rotate count, we have:
  //   M = mask(MB,ME)
  //   Op0 = (Op1 & ~M) | (Op2 & M)
  // Change this to:
  //   M = mask((ME+1)&31, (MB-1)&31)
  //   Op0 = (Op2 & ~M) | (Op1 & M)

  // Swap op1/op2
  assert(((OpIdx1 == 1 && OpIdx2 == 2) || (OpIdx1 == 2 && OpIdx2 == 1)) &&
         "Only the operands 1 and 2 can be swapped in RLSIMI/RLWIMIo.");
  unsigned Reg0 = MI.getOperand(0).getReg();
  unsigned Reg1 = MI.getOperand(1).getReg();
  unsigned Reg2 = MI.getOperand(2).getReg();
  unsigned SubReg1 = MI.getOperand(1).getSubReg();
  unsigned SubReg2 = MI.getOperand(2).getSubReg();
  bool Reg1IsKill = MI.getOperand(1).isKill();
  bool Reg2IsKill = MI.getOperand(2).isKill();
  bool ChangeReg0 = false;
  // If machine instrs are no longer in two-address forms, update
  // destination register as well.
  if (Reg0 == Reg1) {
    // Must be two address instruction!
    assert(MI.getDesc().getOperandConstraint(0, MCOI::TIED_TO) &&
           "Expecting a two-address instruction!");
    assert(MI.getOperand(0).getSubReg() == SubReg1 && "Tied subreg mismatch");
    Reg2IsKill = false;
    ChangeReg0 = true;
  }

  // Masks.
  unsigned MB = MI.getOperand(4).getImm();
  unsigned ME = MI.getOperand(5).getImm();

  // We can't commute a trivial mask (there is no way to represent an all-zero
  // mask).
  if (MB == 0 && ME == 31)
    return nullptr;

  if (NewMI) {
    // Create a new instruction.
    unsigned Reg0 = ChangeReg0 ? Reg2 : MI.getOperand(0).getReg();
    bool Reg0IsDead = MI.getOperand(0).isDead();
    return BuildMI(MF, MI.getDebugLoc(), MI.getDesc())
        .addReg(Reg0, RegState::Define | getDeadRegState(Reg0IsDead))
        .addReg(Reg2, getKillRegState(Reg2IsKill))
        .addReg(Reg1, getKillRegState(Reg1IsKill))
        .addImm((ME + 1) & 31)
        .addImm((MB - 1) & 31);
  }

  if (ChangeReg0) {
    MI.getOperand(0).setReg(Reg2);
    MI.getOperand(0).setSubReg(SubReg2);
  }
  MI.getOperand(2).setReg(Reg1);
  MI.getOperand(1).setReg(Reg2);
  MI.getOperand(2).setSubReg(SubReg1);
  MI.getOperand(1).setSubReg(SubReg2);
  MI.getOperand(2).setIsKill(Reg1IsKill);
  MI.getOperand(1).setIsKill(Reg2IsKill);

  // Swap the mask around.
  MI.getOperand(4).setImm((ME + 1) & 31);
  MI.getOperand(5).setImm((MB - 1) & 31);
  return &MI;
}

bool PPCInstrInfo::findCommutedOpIndices(MachineInstr &MI, unsigned &SrcOpIdx1,
                                         unsigned &SrcOpIdx2) const {
  // For VSX A-Type FMA instructions, it is the first two operands that can be
  // commuted, however, because the non-encoded tied input operand is listed
  // first, the operands to swap are actually the second and third.

  int AltOpc = PPC::getAltVSXFMAOpcode(MI.getOpcode());
  if (AltOpc == -1)
    return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);

  // The commutable operand indices are 2 and 3. Return them in SrcOpIdx1
  // and SrcOpIdx2.
  return fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, 2, 3);
}

void PPCInstrInfo::insertNoop(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI) const {
  // This function is used for scheduling, and the nop wanted here is the type
  // that terminates dispatch groups on the POWER cores.
  unsigned Directive = Subtarget.getDarwinDirective();
  unsigned Opcode;
  switch (Directive) {
  default:            Opcode = PPC::NOP; break;
  case PPC::DIR_PWR6: Opcode = PPC::NOP_GT_PWR6; break;
  case PPC::DIR_PWR7: Opcode = PPC::NOP_GT_PWR7; break;
  case PPC::DIR_PWR8: Opcode = PPC::NOP_GT_PWR7; break; /* FIXME: Update when P8 InstrScheduling model is ready */
  // FIXME: Update when POWER9 scheduling model is ready.
  case PPC::DIR_PWR9: Opcode = PPC::NOP_GT_PWR7; break;
  }

  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(Opcode));
}

/// Return the noop instruction to use for a noop.
void PPCInstrInfo::getNoop(MCInst &NopInst) const {
  NopInst.setOpcode(PPC::NOP);
}

// Branch analysis.
// Note: If the condition register is set to CTR or CTR8 then this is a
// BDNZ (imm == 1) or BDZ (imm == 0) branch.
bool PPCInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  bool isPPC64 = Subtarget.isPPC64();

  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;

  if (!isUnpredicatedTerminator(*I))
    return false;

  if (AllowModify) {
    // If the BB ends with an unconditional branch to the fallthrough BB,
    // we eliminate the branch instruction.
    if (I->getOpcode() == PPC::B &&
        MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
      I->eraseFromParent();

      // We update iterator after deleting the last branch.
      I = MBB.getLastNonDebugInstr();
      if (I == MBB.end() || !isUnpredicatedTerminator(*I))
        return false;
    }
  }

  // Get the last instruction in the block.
  MachineInstr &LastInst = *I;

  // If there is only one terminator instruction, process it.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (LastInst.getOpcode() == PPC::B) {
      if (!LastInst.getOperand(0).isMBB())
        return true;
      TBB = LastInst.getOperand(0).getMBB();
      return false;
    } else if (LastInst.getOpcode() == PPC::BCC) {
      if (!LastInst.getOperand(2).isMBB())
        return true;
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(2).getMBB();
      Cond.push_back(LastInst.getOperand(0));
      Cond.push_back(LastInst.getOperand(1));
      return false;
    } else if (LastInst.getOpcode() == PPC::BC) {
      if (!LastInst.getOperand(1).isMBB())
        return true;
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(1).getMBB();
      Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_SET));
      Cond.push_back(LastInst.getOperand(0));
      return false;
    } else if (LastInst.getOpcode() == PPC::BCn) {
      if (!LastInst.getOperand(1).isMBB())
        return true;
      // Block ends with fall-through condbranch.
      TBB = LastInst.getOperand(1).getMBB();
      Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_UNSET));
      Cond.push_back(LastInst.getOperand(0));
      return false;
    } else if (LastInst.getOpcode() == PPC::BDNZ8 ||
               LastInst.getOpcode() == PPC::BDNZ) {
      if (!LastInst.getOperand(0).isMBB())
        return true;
      if (DisableCTRLoopAnal)
        return true;
      TBB = LastInst.getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(1));
      Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                               true));
      return false;
    } else if (LastInst.getOpcode() == PPC::BDZ8 ||
               LastInst.getOpcode() == PPC::BDZ) {
      if (!LastInst.getOperand(0).isMBB())
        return true;
      if (DisableCTRLoopAnal)
        return true;
      TBB = LastInst.getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(0));
      Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                               true));
      return false;
    }

    // Otherwise, don't know what this is.
    return true;
  }

  // Get the instruction before it if it's a terminator.
  MachineInstr &SecondLastInst = *I;

  // If there are three terminators, we don't know what sort of block this is.
  if (I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  // If the block ends with PPC::B and PPC:BCC, handle it.
  if (SecondLastInst.getOpcode() == PPC::BCC &&
      LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(2).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(2).getMBB();
    Cond.push_back(SecondLastInst.getOperand(0));
    Cond.push_back(SecondLastInst.getOperand(1));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if (SecondLastInst.getOpcode() == PPC::BC &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(1).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_SET));
    Cond.push_back(SecondLastInst.getOperand(0));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if (SecondLastInst.getOpcode() == PPC::BCn &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(1).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_UNSET));
    Cond.push_back(SecondLastInst.getOperand(0));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if ((SecondLastInst.getOpcode() == PPC::BDNZ8 ||
              SecondLastInst.getOpcode() == PPC::BDNZ) &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(0).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    if (DisableCTRLoopAnal)
      return true;
    TBB = SecondLastInst.getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(1));
    Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                             true));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  } else if ((SecondLastInst.getOpcode() == PPC::BDZ8 ||
              SecondLastInst.getOpcode() == PPC::BDZ) &&
             LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(0).isMBB() ||
        !LastInst.getOperand(0).isMBB())
      return true;
    if (DisableCTRLoopAnal)
      return true;
    TBB = SecondLastInst.getOperand(0).getMBB();
    Cond.push_back(MachineOperand::CreateImm(0));
    Cond.push_back(MachineOperand::CreateReg(isPPC64 ? PPC::CTR8 : PPC::CTR,
                                             true));
    FBB = LastInst.getOperand(0).getMBB();
    return false;
  }

  // If the block ends with two PPC:Bs, handle it.  The second one is not
  // executed, so remove it.
  if (SecondLastInst.getOpcode() == PPC::B && LastInst.getOpcode() == PPC::B) {
    if (!SecondLastInst.getOperand(0).isMBB())
      return true;
    TBB = SecondLastInst.getOperand(0).getMBB();
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // Otherwise, can't handle this.
  return true;
}

unsigned PPCInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (I->getOpcode() != PPC::B && I->getOpcode() != PPC::BCC &&
      I->getOpcode() != PPC::BC && I->getOpcode() != PPC::BCn &&
      I->getOpcode() != PPC::BDNZ8 && I->getOpcode() != PPC::BDNZ &&
      I->getOpcode() != PPC::BDZ8  && I->getOpcode() != PPC::BDZ)
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin()) return 1;
  --I;
  if (I->getOpcode() != PPC::BCC &&
      I->getOpcode() != PPC::BC && I->getOpcode() != PPC::BCn &&
      I->getOpcode() != PPC::BDNZ8 && I->getOpcode() != PPC::BDNZ &&
      I->getOpcode() != PPC::BDZ8  && I->getOpcode() != PPC::BDZ)
    return 1;

  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

unsigned PPCInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) &&
         "PPC branch conditions have two components!");
  assert(!BytesAdded && "code size not handled");

  bool isPPC64 = Subtarget.isPPC64();

  // One-way branch.
  if (!FBB) {
    if (Cond.empty())   // Unconditional branch
      BuildMI(&MBB, DL, get(PPC::B)).addMBB(TBB);
    else if (Cond[1].getReg() == PPC::CTR || Cond[1].getReg() == PPC::CTR8)
      BuildMI(&MBB, DL, get(Cond[0].getImm() ?
                              (isPPC64 ? PPC::BDNZ8 : PPC::BDNZ) :
                              (isPPC64 ? PPC::BDZ8  : PPC::BDZ))).addMBB(TBB);
    else if (Cond[0].getImm() == PPC::PRED_BIT_SET)
      BuildMI(&MBB, DL, get(PPC::BC)).add(Cond[1]).addMBB(TBB);
    else if (Cond[0].getImm() == PPC::PRED_BIT_UNSET)
      BuildMI(&MBB, DL, get(PPC::BCn)).add(Cond[1]).addMBB(TBB);
    else                // Conditional branch
      BuildMI(&MBB, DL, get(PPC::BCC))
          .addImm(Cond[0].getImm())
          .add(Cond[1])
          .addMBB(TBB);
    return 1;
  }

  // Two-way Conditional Branch.
  if (Cond[1].getReg() == PPC::CTR || Cond[1].getReg() == PPC::CTR8)
    BuildMI(&MBB, DL, get(Cond[0].getImm() ?
                            (isPPC64 ? PPC::BDNZ8 : PPC::BDNZ) :
                            (isPPC64 ? PPC::BDZ8  : PPC::BDZ))).addMBB(TBB);
  else if (Cond[0].getImm() == PPC::PRED_BIT_SET)
    BuildMI(&MBB, DL, get(PPC::BC)).add(Cond[1]).addMBB(TBB);
  else if (Cond[0].getImm() == PPC::PRED_BIT_UNSET)
    BuildMI(&MBB, DL, get(PPC::BCn)).add(Cond[1]).addMBB(TBB);
  else
    BuildMI(&MBB, DL, get(PPC::BCC))
        .addImm(Cond[0].getImm())
        .add(Cond[1])
        .addMBB(TBB);
  BuildMI(&MBB, DL, get(PPC::B)).addMBB(FBB);
  return 2;
}

// Select analysis.
bool PPCInstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                ArrayRef<MachineOperand> Cond,
                unsigned TrueReg, unsigned FalseReg,
                int &CondCycles, int &TrueCycles, int &FalseCycles) const {
  if (Cond.size() != 2)
    return false;

  // If this is really a bdnz-like condition, then it cannot be turned into a
  // select.
  if (Cond[1].getReg() == PPC::CTR || Cond[1].getReg() == PPC::CTR8)
    return false;

  // Check register classes.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
    RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC)
    return false;

  // isel is for regular integer GPRs only.
  if (!PPC::GPRCRegClass.hasSubClassEq(RC) &&
      !PPC::GPRC_NOR0RegClass.hasSubClassEq(RC) &&
      !PPC::G8RCRegClass.hasSubClassEq(RC) &&
      !PPC::G8RC_NOX0RegClass.hasSubClassEq(RC))
    return false;

  // FIXME: These numbers are for the A2, how well they work for other cores is
  // an open question. On the A2, the isel instruction has a 2-cycle latency
  // but single-cycle throughput. These numbers are used in combination with
  // the MispredictPenalty setting from the active SchedMachineModel.
  CondCycles = 1;
  TrueCycles = 1;
  FalseCycles = 1;

  return true;
}

void PPCInstrInfo::insertSelect(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &dl, unsigned DestReg,
                                ArrayRef<MachineOperand> Cond, unsigned TrueReg,
                                unsigned FalseReg) const {
  assert(Cond.size() == 2 &&
         "PPC branch conditions have two components!");

  // Get the register classes.
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
    RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  assert(RC && "TrueReg and FalseReg must have overlapping register classes");

  bool Is64Bit = PPC::G8RCRegClass.hasSubClassEq(RC) ||
                 PPC::G8RC_NOX0RegClass.hasSubClassEq(RC);
  assert((Is64Bit ||
          PPC::GPRCRegClass.hasSubClassEq(RC) ||
          PPC::GPRC_NOR0RegClass.hasSubClassEq(RC)) &&
         "isel is for regular integer GPRs only");

  unsigned OpCode = Is64Bit ? PPC::ISEL8 : PPC::ISEL;
  auto SelectPred = static_cast<PPC::Predicate>(Cond[0].getImm());

  unsigned SubIdx = 0;
  bool SwapOps = false;
  switch (SelectPred) {
  case PPC::PRED_EQ:
  case PPC::PRED_EQ_MINUS:
  case PPC::PRED_EQ_PLUS:
      SubIdx = PPC::sub_eq; SwapOps = false; break;
  case PPC::PRED_NE:
  case PPC::PRED_NE_MINUS:
  case PPC::PRED_NE_PLUS:
      SubIdx = PPC::sub_eq; SwapOps = true; break;
  case PPC::PRED_LT:
  case PPC::PRED_LT_MINUS:
  case PPC::PRED_LT_PLUS:
      SubIdx = PPC::sub_lt; SwapOps = false; break;
  case PPC::PRED_GE:
  case PPC::PRED_GE_MINUS:
  case PPC::PRED_GE_PLUS:
      SubIdx = PPC::sub_lt; SwapOps = true; break;
  case PPC::PRED_GT:
  case PPC::PRED_GT_MINUS:
  case PPC::PRED_GT_PLUS:
      SubIdx = PPC::sub_gt; SwapOps = false; break;
  case PPC::PRED_LE:
  case PPC::PRED_LE_MINUS:
  case PPC::PRED_LE_PLUS:
      SubIdx = PPC::sub_gt; SwapOps = true; break;
  case PPC::PRED_UN:
  case PPC::PRED_UN_MINUS:
  case PPC::PRED_UN_PLUS:
      SubIdx = PPC::sub_un; SwapOps = false; break;
  case PPC::PRED_NU:
  case PPC::PRED_NU_MINUS:
  case PPC::PRED_NU_PLUS:
      SubIdx = PPC::sub_un; SwapOps = true; break;
  case PPC::PRED_BIT_SET:   SubIdx = 0; SwapOps = false; break;
  case PPC::PRED_BIT_UNSET: SubIdx = 0; SwapOps = true; break;
  }

  unsigned FirstReg =  SwapOps ? FalseReg : TrueReg,
           SecondReg = SwapOps ? TrueReg  : FalseReg;

  // The first input register of isel cannot be r0. If it is a member
  // of a register class that can be r0, then copy it first (the
  // register allocator should eliminate the copy).
  if (MRI.getRegClass(FirstReg)->contains(PPC::R0) ||
      MRI.getRegClass(FirstReg)->contains(PPC::X0)) {
    const TargetRegisterClass *FirstRC =
      MRI.getRegClass(FirstReg)->contains(PPC::X0) ?
        &PPC::G8RC_NOX0RegClass : &PPC::GPRC_NOR0RegClass;
    unsigned OldFirstReg = FirstReg;
    FirstReg = MRI.createVirtualRegister(FirstRC);
    BuildMI(MBB, MI, dl, get(TargetOpcode::COPY), FirstReg)
      .addReg(OldFirstReg);
  }

  BuildMI(MBB, MI, dl, get(OpCode), DestReg)
    .addReg(FirstReg).addReg(SecondReg)
    .addReg(Cond[1].getReg(), 0, SubIdx);
}

static unsigned getCRBitValue(unsigned CRBit) {
  unsigned Ret = 4;
  if (CRBit == PPC::CR0LT || CRBit == PPC::CR1LT ||
      CRBit == PPC::CR2LT || CRBit == PPC::CR3LT ||
      CRBit == PPC::CR4LT || CRBit == PPC::CR5LT ||
      CRBit == PPC::CR6LT || CRBit == PPC::CR7LT)
    Ret = 3;
  if (CRBit == PPC::CR0GT || CRBit == PPC::CR1GT ||
      CRBit == PPC::CR2GT || CRBit == PPC::CR3GT ||
      CRBit == PPC::CR4GT || CRBit == PPC::CR5GT ||
      CRBit == PPC::CR6GT || CRBit == PPC::CR7GT)
    Ret = 2;
  if (CRBit == PPC::CR0EQ || CRBit == PPC::CR1EQ ||
      CRBit == PPC::CR2EQ || CRBit == PPC::CR3EQ ||
      CRBit == PPC::CR4EQ || CRBit == PPC::CR5EQ ||
      CRBit == PPC::CR6EQ || CRBit == PPC::CR7EQ)
    Ret = 1;
  if (CRBit == PPC::CR0UN || CRBit == PPC::CR1UN ||
      CRBit == PPC::CR2UN || CRBit == PPC::CR3UN ||
      CRBit == PPC::CR4UN || CRBit == PPC::CR5UN ||
      CRBit == PPC::CR6UN || CRBit == PPC::CR7UN)
    Ret = 0;

  assert(Ret != 4 && "Invalid CR bit register");
  return Ret;
}

void PPCInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, unsigned DestReg,
                               unsigned SrcReg, bool KillSrc) const {
  // We can end up with self copies and similar things as a result of VSX copy
  // legalization. Promote them here.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  if (PPC::F8RCRegClass.contains(DestReg) &&
      PPC::VSRCRegClass.contains(SrcReg)) {
    unsigned SuperReg =
      TRI->getMatchingSuperReg(DestReg, PPC::sub_64, &PPC::VSRCRegClass);

    if (VSXSelfCopyCrash && SrcReg == SuperReg)
      llvm_unreachable("nop VSX copy");

    DestReg = SuperReg;
  } else if (PPC::F8RCRegClass.contains(SrcReg) &&
             PPC::VSRCRegClass.contains(DestReg)) {
    unsigned SuperReg =
      TRI->getMatchingSuperReg(SrcReg, PPC::sub_64, &PPC::VSRCRegClass);

    if (VSXSelfCopyCrash && DestReg == SuperReg)
      llvm_unreachable("nop VSX copy");

    SrcReg = SuperReg;
  }

  // Different class register copy
  if (PPC::CRBITRCRegClass.contains(SrcReg) &&
      PPC::GPRCRegClass.contains(DestReg)) {
    unsigned CRReg = getCRFromCRBit(SrcReg);
    BuildMI(MBB, I, DL, get(PPC::MFOCRF), DestReg).addReg(CRReg);
    getKillRegState(KillSrc);
    // Rotate the CR bit in the CR fields to be the least significant bit and
    // then mask with 0x1 (MB = ME = 31).
    BuildMI(MBB, I, DL, get(PPC::RLWINM), DestReg)
       .addReg(DestReg, RegState::Kill)
       .addImm(TRI->getEncodingValue(CRReg) * 4 + (4 - getCRBitValue(SrcReg)))
       .addImm(31)
       .addImm(31);
    return;
  } else if (PPC::CRRCRegClass.contains(SrcReg) &&
      PPC::G8RCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::MFOCRF8), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  } else if (PPC::CRRCRegClass.contains(SrcReg) &&
      PPC::GPRCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::MFOCRF), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  } else if (PPC::G8RCRegClass.contains(SrcReg) &&
             PPC::VSFRCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::MTVSRD), DestReg).addReg(SrcReg);
    NumGPRtoVSRSpill++;
    getKillRegState(KillSrc);
    return;
  } else if (PPC::VSFRCRegClass.contains(SrcReg) &&
             PPC::G8RCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::MFVSRD), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  } else if (PPC::SPERCRegClass.contains(SrcReg) &&
             PPC::SPE4RCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::EFSCFD), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  } else if (PPC::SPE4RCRegClass.contains(SrcReg) &&
             PPC::SPERCRegClass.contains(DestReg)) {
    BuildMI(MBB, I, DL, get(PPC::EFDCFS), DestReg).addReg(SrcReg);
    getKillRegState(KillSrc);
    return;
  }


  unsigned Opc;
  if (PPC::GPRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::OR;
  else if (PPC::G8RCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::OR8;
  else if (PPC::F4RCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::FMR;
  else if (PPC::CRRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::MCRF;
  else if (PPC::VRRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::VOR;
  else if (PPC::VSRCRegClass.contains(DestReg, SrcReg))
    // There are two different ways this can be done:
    //   1. xxlor : This has lower latency (on the P7), 2 cycles, but can only
    //      issue in VSU pipeline 0.
    //   2. xmovdp/xmovsp: This has higher latency (on the P7), 6 cycles, but
    //      can go to either pipeline.
    // We'll always use xxlor here, because in practically all cases where
    // copies are generated, they are close enough to some use that the
    // lower-latency form is preferable.
    Opc = PPC::XXLOR;
  else if (PPC::VSFRCRegClass.contains(DestReg, SrcReg) ||
           PPC::VSSRCRegClass.contains(DestReg, SrcReg))
    Opc = (Subtarget.hasP9Vector()) ? PPC::XSCPSGNDP : PPC::XXLORf;
  else if (PPC::QFRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::QVFMR;
  else if (PPC::QSRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::QVFMRs;
  else if (PPC::QBRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::QVFMRb;
  else if (PPC::CRBITRCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::CROR;
  else if (PPC::SPERCRegClass.contains(DestReg, SrcReg))
    Opc = PPC::EVOR;
  else
    llvm_unreachable("Impossible reg-to-reg copy");

  const MCInstrDesc &MCID = get(Opc);
  if (MCID.getNumOperands() == 3)
    BuildMI(MBB, I, DL, MCID, DestReg)
      .addReg(SrcReg).addReg(SrcReg, getKillRegState(KillSrc));
  else
    BuildMI(MBB, I, DL, MCID, DestReg).addReg(SrcReg, getKillRegState(KillSrc));
}

unsigned PPCInstrInfo::getStoreOpcodeForSpill(unsigned Reg,
                                              const TargetRegisterClass *RC)
                                              const {
  const unsigned *OpcodesForSpill = getStoreOpcodesForSpillArray();
  int OpcodeIndex = 0;

  if (RC != nullptr) {
    if (PPC::GPRCRegClass.hasSubClassEq(RC) ||
        PPC::GPRC_NOR0RegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Int4Spill;
    } else if (PPC::G8RCRegClass.hasSubClassEq(RC) ||
               PPC::G8RC_NOX0RegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Int8Spill;
    } else if (PPC::F8RCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Float8Spill;
    } else if (PPC::F4RCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Float4Spill;
    } else if (PPC::SPERCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_SPESpill;
    } else if (PPC::SPE4RCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_SPE4Spill;
    } else if (PPC::CRRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_CRSpill;
    } else if (PPC::CRBITRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_CRBitSpill;
    } else if (PPC::VRRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VRVectorSpill;
    } else if (PPC::VSRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VSXVectorSpill;
    } else if (PPC::VSFRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VectorFloat8Spill;
    } else if (PPC::VSSRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VectorFloat4Spill;
    } else if (PPC::VRSAVERCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VRSaveSpill;
    } else if (PPC::QFRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_QuadFloat8Spill;
    } else if (PPC::QSRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_QuadFloat4Spill;
    } else if (PPC::QBRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_QuadBitSpill;
    } else if (PPC::SPILLTOVSRRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_SpillToVSR;
    } else {
      llvm_unreachable("Unknown regclass!");
    }
  } else {
    if (PPC::GPRCRegClass.contains(Reg) ||
        PPC::GPRC_NOR0RegClass.contains(Reg)) {
      OpcodeIndex = SOK_Int4Spill;
    } else if (PPC::G8RCRegClass.contains(Reg) ||
               PPC::G8RC_NOX0RegClass.contains(Reg)) {
      OpcodeIndex = SOK_Int8Spill;
    } else if (PPC::F8RCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_Float8Spill;
    } else if (PPC::F4RCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_Float4Spill;
    } else if (PPC::CRRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_CRSpill;
    } else if (PPC::CRBITRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_CRBitSpill;
    } else if (PPC::VRRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VRVectorSpill;
    } else if (PPC::VSRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VSXVectorSpill;
    } else if (PPC::VSFRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VectorFloat8Spill;
    } else if (PPC::VSSRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VectorFloat4Spill;
    } else if (PPC::VRSAVERCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VRSaveSpill;
    } else if (PPC::QFRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_QuadFloat8Spill;
    } else if (PPC::QSRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_QuadFloat4Spill;
    } else if (PPC::QBRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_QuadBitSpill;
    } else if (PPC::SPILLTOVSRRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_SpillToVSR;
    } else {
      llvm_unreachable("Unknown regclass!");
    }
  }
  return OpcodesForSpill[OpcodeIndex];
}

unsigned
PPCInstrInfo::getLoadOpcodeForSpill(unsigned Reg,
                                    const TargetRegisterClass *RC) const {
  const unsigned *OpcodesForSpill = getLoadOpcodesForSpillArray();
  int OpcodeIndex = 0;

  if (RC != nullptr) {
    if (PPC::GPRCRegClass.hasSubClassEq(RC) ||
        PPC::GPRC_NOR0RegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Int4Spill;
    } else if (PPC::G8RCRegClass.hasSubClassEq(RC) ||
               PPC::G8RC_NOX0RegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Int8Spill;
    } else if (PPC::F8RCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Float8Spill;
    } else if (PPC::F4RCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_Float4Spill;
    } else if (PPC::SPERCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_SPESpill;
    } else if (PPC::SPE4RCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_SPE4Spill;
    } else if (PPC::CRRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_CRSpill;
    } else if (PPC::CRBITRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_CRBitSpill;
    } else if (PPC::VRRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VRVectorSpill;
    } else if (PPC::VSRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VSXVectorSpill;
    } else if (PPC::VSFRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VectorFloat8Spill;
    } else if (PPC::VSSRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VectorFloat4Spill;
    } else if (PPC::VRSAVERCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_VRSaveSpill;
    } else if (PPC::QFRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_QuadFloat8Spill;
    } else if (PPC::QSRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_QuadFloat4Spill;
    } else if (PPC::QBRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_QuadBitSpill;
    } else if (PPC::SPILLTOVSRRCRegClass.hasSubClassEq(RC)) {
      OpcodeIndex = SOK_SpillToVSR;
    } else {
      llvm_unreachable("Unknown regclass!");
    }
  } else {
    if (PPC::GPRCRegClass.contains(Reg) ||
        PPC::GPRC_NOR0RegClass.contains(Reg)) {
      OpcodeIndex = SOK_Int4Spill;
    } else if (PPC::G8RCRegClass.contains(Reg) ||
               PPC::G8RC_NOX0RegClass.contains(Reg)) {
      OpcodeIndex = SOK_Int8Spill;
    } else if (PPC::F8RCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_Float8Spill;
    } else if (PPC::F4RCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_Float4Spill;
    } else if (PPC::CRRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_CRSpill;
    } else if (PPC::CRBITRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_CRBitSpill;
    } else if (PPC::VRRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VRVectorSpill;
    } else if (PPC::VSRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VSXVectorSpill;
    } else if (PPC::VSFRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VectorFloat8Spill;
    } else if (PPC::VSSRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VectorFloat4Spill;
    } else if (PPC::VRSAVERCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_VRSaveSpill;
    } else if (PPC::QFRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_QuadFloat8Spill;
    } else if (PPC::QSRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_QuadFloat4Spill;
    } else if (PPC::QBRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_QuadBitSpill;
    } else if (PPC::SPILLTOVSRRCRegClass.contains(Reg)) {
      OpcodeIndex = SOK_SpillToVSR;
    } else {
      llvm_unreachable("Unknown regclass!");
    }
  }
  return OpcodesForSpill[OpcodeIndex];
}

void PPCInstrInfo::StoreRegToStackSlot(
    MachineFunction &MF, unsigned SrcReg, bool isKill, int FrameIdx,
    const TargetRegisterClass *RC,
    SmallVectorImpl<MachineInstr *> &NewMIs) const {
  unsigned Opcode = getStoreOpcodeForSpill(PPC::NoRegister, RC);
  DebugLoc DL;

  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setHasSpills();

  NewMIs.push_back(addFrameReference(
      BuildMI(MF, DL, get(Opcode)).addReg(SrcReg, getKillRegState(isKill)),
      FrameIdx));

  if (PPC::CRRCRegClass.hasSubClassEq(RC) ||
      PPC::CRBITRCRegClass.hasSubClassEq(RC))
    FuncInfo->setSpillsCR();

  if (PPC::VRSAVERCRegClass.hasSubClassEq(RC))
    FuncInfo->setSpillsVRSAVE();

  if (isXFormMemOp(Opcode))
    FuncInfo->setHasNonRISpills();
}

void PPCInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       unsigned SrcReg, bool isKill,
                                       int FrameIdx,
                                       const TargetRegisterClass *RC,
                                       const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  SmallVector<MachineInstr *, 4> NewMIs;

  // We need to avoid a situation in which the value from a VRRC register is
  // spilled using an Altivec instruction and reloaded into a VSRC register
  // using a VSX instruction. The issue with this is that the VSX
  // load/store instructions swap the doublewords in the vector and the Altivec
  // ones don't. The register classes on the spill/reload may be different if
  // the register is defined using an Altivec instruction and is then used by a
  // VSX instruction.
  RC = updatedRC(RC);

  StoreRegToStackSlot(MF, SrcReg, isKill, FrameIdx, RC, NewMIs);

  for (unsigned i = 0, e = NewMIs.size(); i != e; ++i)
    MBB.insert(MI, NewMIs[i]);

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlignment(FrameIdx));
  NewMIs.back()->addMemOperand(MF, MMO);
}

void PPCInstrInfo::LoadRegFromStackSlot(MachineFunction &MF, const DebugLoc &DL,
                                        unsigned DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        SmallVectorImpl<MachineInstr *> &NewMIs)
                                        const {
  unsigned Opcode = getLoadOpcodeForSpill(PPC::NoRegister, RC);
  NewMIs.push_back(addFrameReference(BuildMI(MF, DL, get(Opcode), DestReg),
                                     FrameIdx));
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();

  if (PPC::CRRCRegClass.hasSubClassEq(RC) ||
      PPC::CRBITRCRegClass.hasSubClassEq(RC))
    FuncInfo->setSpillsCR();

  if (PPC::VRSAVERCRegClass.hasSubClassEq(RC))
    FuncInfo->setSpillsVRSAVE();

  if (isXFormMemOp(Opcode))
    FuncInfo->setHasNonRISpills();
}

void
PPCInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   unsigned DestReg, int FrameIdx,
                                   const TargetRegisterClass *RC,
                                   const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  SmallVector<MachineInstr*, 4> NewMIs;
  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setHasSpills();

  // We need to avoid a situation in which the value from a VRRC register is
  // spilled using an Altivec instruction and reloaded into a VSRC register
  // using a VSX instruction. The issue with this is that the VSX
  // load/store instructions swap the doublewords in the vector and the Altivec
  // ones don't. The register classes on the spill/reload may be different if
  // the register is defined using an Altivec instruction and is then used by a
  // VSX instruction.
  if (Subtarget.hasVSX() && RC == &PPC::VRRCRegClass)
    RC = &PPC::VSRCRegClass;

  LoadRegFromStackSlot(MF, DL, DestReg, FrameIdx, RC, NewMIs);

  for (unsigned i = 0, e = NewMIs.size(); i != e; ++i)
    MBB.insert(MI, NewMIs[i]);

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlignment(FrameIdx));
  NewMIs.back()->addMemOperand(MF, MMO);
}

bool PPCInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Invalid PPC branch opcode!");
  if (Cond[1].getReg() == PPC::CTR8 || Cond[1].getReg() == PPC::CTR)
    Cond[0].setImm(Cond[0].getImm() == 0 ? 1 : 0);
  else
    // Leave the CR# the same, but invert the condition.
    Cond[0].setImm(PPC::InvertPredicate((PPC::Predicate)Cond[0].getImm()));
  return false;
}

bool PPCInstrInfo::FoldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                 unsigned Reg, MachineRegisterInfo *MRI) const {
  // For some instructions, it is legal to fold ZERO into the RA register field.
  // A zero immediate should always be loaded with a single li.
  unsigned DefOpc = DefMI.getOpcode();
  if (DefOpc != PPC::LI && DefOpc != PPC::LI8)
    return false;
  if (!DefMI.getOperand(1).isImm())
    return false;
  if (DefMI.getOperand(1).getImm() != 0)
    return false;

  // Note that we cannot here invert the arguments of an isel in order to fold
  // a ZERO into what is presented as the second argument. All we have here
  // is the condition bit, and that might come from a CR-logical bit operation.

  const MCInstrDesc &UseMCID = UseMI.getDesc();

  // Only fold into real machine instructions.
  if (UseMCID.isPseudo())
    return false;

  unsigned UseIdx;
  for (UseIdx = 0; UseIdx < UseMI.getNumOperands(); ++UseIdx)
    if (UseMI.getOperand(UseIdx).isReg() &&
        UseMI.getOperand(UseIdx).getReg() == Reg)
      break;

  assert(UseIdx < UseMI.getNumOperands() && "Cannot find Reg in UseMI");
  assert(UseIdx < UseMCID.getNumOperands() && "No operand description for Reg");

  const MCOperandInfo *UseInfo = &UseMCID.OpInfo[UseIdx];

  // We can fold the zero if this register requires a GPRC_NOR0/G8RC_NOX0
  // register (which might also be specified as a pointer class kind).
  if (UseInfo->isLookupPtrRegClass()) {
    if (UseInfo->RegClass /* Kind */ != 1)
      return false;
  } else {
    if (UseInfo->RegClass != PPC::GPRC_NOR0RegClassID &&
        UseInfo->RegClass != PPC::G8RC_NOX0RegClassID)
      return false;
  }

  // Make sure this is not tied to an output register (or otherwise
  // constrained). This is true for ST?UX registers, for example, which
  // are tied to their output registers.
  if (UseInfo->Constraints != 0)
    return false;

  unsigned ZeroReg;
  if (UseInfo->isLookupPtrRegClass()) {
    bool isPPC64 = Subtarget.isPPC64();
    ZeroReg = isPPC64 ? PPC::ZERO8 : PPC::ZERO;
  } else {
    ZeroReg = UseInfo->RegClass == PPC::G8RC_NOX0RegClassID ?
              PPC::ZERO8 : PPC::ZERO;
  }

  bool DeleteDef = MRI->hasOneNonDBGUse(Reg);
  UseMI.getOperand(UseIdx).setReg(ZeroReg);

  if (DeleteDef)
    DefMI.eraseFromParent();

  return true;
}

static bool MBBDefinesCTR(MachineBasicBlock &MBB) {
  for (MachineBasicBlock::iterator I = MBB.begin(), IE = MBB.end();
       I != IE; ++I)
    if (I->definesRegister(PPC::CTR) || I->definesRegister(PPC::CTR8))
      return true;
  return false;
}

// We should make sure that, if we're going to predicate both sides of a
// condition (a diamond), that both sides don't define the counter register. We
// can predicate counter-decrement-based branches, but while that predicates
// the branching, it does not predicate the counter decrement. If we tried to
// merge the triangle into one predicated block, we'd decrement the counter
// twice.
bool PPCInstrInfo::isProfitableToIfCvt(MachineBasicBlock &TMBB,
                     unsigned NumT, unsigned ExtraT,
                     MachineBasicBlock &FMBB,
                     unsigned NumF, unsigned ExtraF,
                     BranchProbability Probability) const {
  return !(MBBDefinesCTR(TMBB) && MBBDefinesCTR(FMBB));
}


bool PPCInstrInfo::isPredicated(const MachineInstr &MI) const {
  // The predicated branches are identified by their type, not really by the
  // explicit presence of a predicate. Furthermore, some of them can be
  // predicated more than once. Because if conversion won't try to predicate
  // any instruction which already claims to be predicated (by returning true
  // here), always return false. In doing so, we let isPredicable() be the
  // final word on whether not the instruction can be (further) predicated.

  return false;
}

bool PPCInstrInfo::isUnpredicatedTerminator(const MachineInstr &MI) const {
  if (!MI.isTerminator())
    return false;

  // Conditional branch is a special case.
  if (MI.isBranch() && !MI.isBarrier())
    return true;

  return !isPredicated(MI);
}

bool PPCInstrInfo::PredicateInstruction(MachineInstr &MI,
                                        ArrayRef<MachineOperand> Pred) const {
  unsigned OpC = MI.getOpcode();
  if (OpC == PPC::BLR || OpC == PPC::BLR8) {
    if (Pred[1].getReg() == PPC::CTR8 || Pred[1].getReg() == PPC::CTR) {
      bool isPPC64 = Subtarget.isPPC64();
      MI.setDesc(get(Pred[0].getImm() ? (isPPC64 ? PPC::BDNZLR8 : PPC::BDNZLR)
                                      : (isPPC64 ? PPC::BDZLR8 : PPC::BDZLR)));
    } else if (Pred[0].getImm() == PPC::PRED_BIT_SET) {
      MI.setDesc(get(PPC::BCLR));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_UNSET) {
      MI.setDesc(get(PPC::BCLRn));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
    } else {
      MI.setDesc(get(PPC::BCCLR));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addImm(Pred[0].getImm())
          .add(Pred[1]);
    }

    return true;
  } else if (OpC == PPC::B) {
    if (Pred[1].getReg() == PPC::CTR8 || Pred[1].getReg() == PPC::CTR) {
      bool isPPC64 = Subtarget.isPPC64();
      MI.setDesc(get(Pred[0].getImm() ? (isPPC64 ? PPC::BDNZ8 : PPC::BDNZ)
                                      : (isPPC64 ? PPC::BDZ8 : PPC::BDZ)));
    } else if (Pred[0].getImm() == PPC::PRED_BIT_SET) {
      MachineBasicBlock *MBB = MI.getOperand(0).getMBB();
      MI.RemoveOperand(0);

      MI.setDesc(get(PPC::BC));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .add(Pred[1])
          .addMBB(MBB);
    } else if (Pred[0].getImm() == PPC::PRED_BIT_UNSET) {
      MachineBasicBlock *MBB = MI.getOperand(0).getMBB();
      MI.RemoveOperand(0);

      MI.setDesc(get(PPC::BCn));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .add(Pred[1])
          .addMBB(MBB);
    } else {
      MachineBasicBlock *MBB = MI.getOperand(0).getMBB();
      MI.RemoveOperand(0);

      MI.setDesc(get(PPC::BCC));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI)
          .addImm(Pred[0].getImm())
          .add(Pred[1])
          .addMBB(MBB);
    }

    return true;
  } else if (OpC == PPC::BCTR || OpC == PPC::BCTR8 || OpC == PPC::BCTRL ||
             OpC == PPC::BCTRL8) {
    if (Pred[1].getReg() == PPC::CTR8 || Pred[1].getReg() == PPC::CTR)
      llvm_unreachable("Cannot predicate bctr[l] on the ctr register");

    bool setLR = OpC == PPC::BCTRL || OpC == PPC::BCTRL8;
    bool isPPC64 = Subtarget.isPPC64();

    if (Pred[0].getImm() == PPC::PRED_BIT_SET) {
      MI.setDesc(get(isPPC64 ? (setLR ? PPC::BCCTRL8 : PPC::BCCTR8)
                             : (setLR ? PPC::BCCTRL : PPC::BCCTR)));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
      return true;
    } else if (Pred[0].getImm() == PPC::PRED_BIT_UNSET) {
      MI.setDesc(get(isPPC64 ? (setLR ? PPC::BCCTRL8n : PPC::BCCTR8n)
                             : (setLR ? PPC::BCCTRLn : PPC::BCCTRn)));
      MachineInstrBuilder(*MI.getParent()->getParent(), MI).add(Pred[1]);
      return true;
    }

    MI.setDesc(get(isPPC64 ? (setLR ? PPC::BCCCTRL8 : PPC::BCCCTR8)
                           : (setLR ? PPC::BCCCTRL : PPC::BCCCTR)));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(Pred[0].getImm())
        .add(Pred[1]);
    return true;
  }

  return false;
}

bool PPCInstrInfo::SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                                     ArrayRef<MachineOperand> Pred2) const {
  assert(Pred1.size() == 2 && "Invalid PPC first predicate");
  assert(Pred2.size() == 2 && "Invalid PPC second predicate");

  if (Pred1[1].getReg() == PPC::CTR8 || Pred1[1].getReg() == PPC::CTR)
    return false;
  if (Pred2[1].getReg() == PPC::CTR8 || Pred2[1].getReg() == PPC::CTR)
    return false;

  // P1 can only subsume P2 if they test the same condition register.
  if (Pred1[1].getReg() != Pred2[1].getReg())
    return false;

  PPC::Predicate P1 = (PPC::Predicate) Pred1[0].getImm();
  PPC::Predicate P2 = (PPC::Predicate) Pred2[0].getImm();

  if (P1 == P2)
    return true;

  // Does P1 subsume P2, e.g. GE subsumes GT.
  if (P1 == PPC::PRED_LE &&
      (P2 == PPC::PRED_LT || P2 == PPC::PRED_EQ))
    return true;
  if (P1 == PPC::PRED_GE &&
      (P2 == PPC::PRED_GT || P2 == PPC::PRED_EQ))
    return true;

  return false;
}

bool PPCInstrInfo::DefinesPredicate(MachineInstr &MI,
                                    std::vector<MachineOperand> &Pred) const {
  // Note: At the present time, the contents of Pred from this function is
  // unused by IfConversion. This implementation follows ARM by pushing the
  // CR-defining operand. Because the 'DZ' and 'DNZ' count as types of
  // predicate, instructions defining CTR or CTR8 are also included as
  // predicate-defining instructions.

  const TargetRegisterClass *RCs[] =
    { &PPC::CRRCRegClass, &PPC::CRBITRCRegClass,
      &PPC::CTRRCRegClass, &PPC::CTRRC8RegClass };

  bool Found = false;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    for (unsigned c = 0; c < array_lengthof(RCs) && !Found; ++c) {
      const TargetRegisterClass *RC = RCs[c];
      if (MO.isReg()) {
        if (MO.isDef() && RC->contains(MO.getReg())) {
          Pred.push_back(MO);
          Found = true;
        }
      } else if (MO.isRegMask()) {
        for (TargetRegisterClass::iterator I = RC->begin(),
             IE = RC->end(); I != IE; ++I)
          if (MO.clobbersPhysReg(*I)) {
            Pred.push_back(MO);
            Found = true;
          }
      }
    }
  }

  return Found;
}

bool PPCInstrInfo::isPredicable(const MachineInstr &MI) const {
  unsigned OpC = MI.getOpcode();
  switch (OpC) {
  default:
    return false;
  case PPC::B:
  case PPC::BLR:
  case PPC::BLR8:
  case PPC::BCTR:
  case PPC::BCTR8:
  case PPC::BCTRL:
  case PPC::BCTRL8:
    return true;
  }
}

bool PPCInstrInfo::analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                                  unsigned &SrcReg2, int &Mask,
                                  int &Value) const {
  unsigned Opc = MI.getOpcode();

  switch (Opc) {
  default: return false;
  case PPC::CMPWI:
  case PPC::CMPLWI:
  case PPC::CMPDI:
  case PPC::CMPLDI:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    Value = MI.getOperand(2).getImm();
    Mask = 0xFFFF;
    return true;
  case PPC::CMPW:
  case PPC::CMPLW:
  case PPC::CMPD:
  case PPC::CMPLD:
  case PPC::FCMPUS:
  case PPC::FCMPUD:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = MI.getOperand(2).getReg();
    Value = 0;
    Mask = 0;
    return true;
  }
}

bool PPCInstrInfo::optimizeCompareInstr(MachineInstr &CmpInstr, unsigned SrcReg,
                                        unsigned SrcReg2, int Mask, int Value,
                                        const MachineRegisterInfo *MRI) const {
  if (DisableCmpOpt)
    return false;

  int OpC = CmpInstr.getOpcode();
  unsigned CRReg = CmpInstr.getOperand(0).getReg();

  // FP record forms set CR1 based on the exception status bits, not a
  // comparison with zero.
  if (OpC == PPC::FCMPUS || OpC == PPC::FCMPUD)
    return false;

  // The record forms set the condition register based on a signed comparison
  // with zero (so says the ISA manual). This is not as straightforward as it
  // seems, however, because this is always a 64-bit comparison on PPC64, even
  // for instructions that are 32-bit in nature (like slw for example).
  // So, on PPC32, for unsigned comparisons, we can use the record forms only
  // for equality checks (as those don't depend on the sign). On PPC64,
  // we are restricted to equality for unsigned 64-bit comparisons and for
  // signed 32-bit comparisons the applicability is more restricted.
  bool isPPC64 = Subtarget.isPPC64();
  bool is32BitSignedCompare   = OpC ==  PPC::CMPWI || OpC == PPC::CMPW;
  bool is32BitUnsignedCompare = OpC == PPC::CMPLWI || OpC == PPC::CMPLW;
  bool is64BitUnsignedCompare = OpC == PPC::CMPLDI || OpC == PPC::CMPLD;

  // Get the unique definition of SrcReg.
  MachineInstr *MI = MRI->getUniqueVRegDef(SrcReg);
  if (!MI) return false;

  bool equalityOnly = false;
  bool noSub = false;
  if (isPPC64) {
    if (is32BitSignedCompare) {
      // We can perform this optimization only if MI is sign-extending.
      if (isSignExtended(*MI))
        noSub = true;
      else
        return false;
    } else if (is32BitUnsignedCompare) {
      // We can perform this optimization, equality only, if MI is
      // zero-extending.
      if (isZeroExtended(*MI)) {
        noSub = true;
        equalityOnly = true;
      } else
        return false;
    } else
      equalityOnly = is64BitUnsignedCompare;
  } else
    equalityOnly = is32BitUnsignedCompare;

  if (equalityOnly) {
    // We need to check the uses of the condition register in order to reject
    // non-equality comparisons.
    for (MachineRegisterInfo::use_instr_iterator
         I = MRI->use_instr_begin(CRReg), IE = MRI->use_instr_end();
         I != IE; ++I) {
      MachineInstr *UseMI = &*I;
      if (UseMI->getOpcode() == PPC::BCC) {
        PPC::Predicate Pred = (PPC::Predicate)UseMI->getOperand(0).getImm();
        unsigned PredCond = PPC::getPredicateCondition(Pred);
        // We ignore hint bits when checking for non-equality comparisons.
        if (PredCond != PPC::PRED_EQ && PredCond != PPC::PRED_NE)
          return false;
      } else if (UseMI->getOpcode() == PPC::ISEL ||
                 UseMI->getOpcode() == PPC::ISEL8) {
        unsigned SubIdx = UseMI->getOperand(3).getSubReg();
        if (SubIdx != PPC::sub_eq)
          return false;
      } else
        return false;
    }
  }

  MachineBasicBlock::iterator I = CmpInstr;

  // Scan forward to find the first use of the compare.
  for (MachineBasicBlock::iterator EL = CmpInstr.getParent()->end(); I != EL;
       ++I) {
    bool FoundUse = false;
    for (MachineRegisterInfo::use_instr_iterator
         J = MRI->use_instr_begin(CRReg), JE = MRI->use_instr_end();
         J != JE; ++J)
      if (&*J == &*I) {
        FoundUse = true;
        break;
      }

    if (FoundUse)
      break;
  }

  SmallVector<std::pair<MachineOperand*, PPC::Predicate>, 4> PredsToUpdate;
  SmallVector<std::pair<MachineOperand*, unsigned>, 4> SubRegsToUpdate;

  // There are two possible candidates which can be changed to set CR[01].
  // One is MI, the other is a SUB instruction.
  // For CMPrr(r1,r2), we are looking for SUB(r1,r2) or SUB(r2,r1).
  MachineInstr *Sub = nullptr;
  if (SrcReg2 != 0)
    // MI is not a candidate for CMPrr.
    MI = nullptr;
  // FIXME: Conservatively refuse to convert an instruction which isn't in the
  // same BB as the comparison. This is to allow the check below to avoid calls
  // (and other explicit clobbers); instead we should really check for these
  // more explicitly (in at least a few predecessors).
  else if (MI->getParent() != CmpInstr.getParent())
    return false;
  else if (Value != 0) {
    // The record-form instructions set CR bit based on signed comparison
    // against 0. We try to convert a compare against 1 or -1 into a compare
    // against 0 to exploit record-form instructions. For example, we change
    // the condition "greater than -1" into "greater than or equal to 0"
    // and "less than 1" into "less than or equal to 0".

    // Since we optimize comparison based on a specific branch condition,
    // we don't optimize if condition code is used by more than once.
    if (equalityOnly || !MRI->hasOneUse(CRReg))
      return false;

    MachineInstr *UseMI = &*MRI->use_instr_begin(CRReg);
    if (UseMI->getOpcode() != PPC::BCC)
      return false;

    PPC::Predicate Pred = (PPC::Predicate)UseMI->getOperand(0).getImm();
    PPC::Predicate NewPred = Pred;
    unsigned PredCond = PPC::getPredicateCondition(Pred);
    unsigned PredHint = PPC::getPredicateHint(Pred);
    int16_t Immed = (int16_t)Value;

    // When modifying the condition in the predicate, we propagate hint bits
    // from the original predicate to the new one.
    if (Immed == -1 && PredCond == PPC::PRED_GT)
      // We convert "greater than -1" into "greater than or equal to 0",
      // since we are assuming signed comparison by !equalityOnly
      NewPred = PPC::getPredicate(PPC::PRED_GE, PredHint);
    else if (Immed == -1 && PredCond == PPC::PRED_LE)
      // We convert "less than or equal to -1" into "less than 0".
      NewPred = PPC::getPredicate(PPC::PRED_LT, PredHint);
    else if (Immed == 1 && PredCond == PPC::PRED_LT)
      // We convert "less than 1" into "less than or equal to 0".
      NewPred = PPC::getPredicate(PPC::PRED_LE, PredHint);
    else if (Immed == 1 && PredCond == PPC::PRED_GE)
      // We convert "greater than or equal to 1" into "greater than 0".
      NewPred = PPC::getPredicate(PPC::PRED_GT, PredHint);
    else
      return false;

    PredsToUpdate.push_back(std::make_pair(&(UseMI->getOperand(0)),
                                            NewPred));
  }

  // Search for Sub.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  --I;

  // Get ready to iterate backward from CmpInstr.
  MachineBasicBlock::iterator E = MI, B = CmpInstr.getParent()->begin();

  for (; I != E && !noSub; --I) {
    const MachineInstr &Instr = *I;
    unsigned IOpC = Instr.getOpcode();

    if (&*I != &CmpInstr && (Instr.modifiesRegister(PPC::CR0, TRI) ||
                             Instr.readsRegister(PPC::CR0, TRI)))
      // This instruction modifies or uses the record condition register after
      // the one we want to change. While we could do this transformation, it
      // would likely not be profitable. This transformation removes one
      // instruction, and so even forcing RA to generate one move probably
      // makes it unprofitable.
      return false;

    // Check whether CmpInstr can be made redundant by the current instruction.
    if ((OpC == PPC::CMPW || OpC == PPC::CMPLW ||
         OpC == PPC::CMPD || OpC == PPC::CMPLD) &&
        (IOpC == PPC::SUBF || IOpC == PPC::SUBF8) &&
        ((Instr.getOperand(1).getReg() == SrcReg &&
          Instr.getOperand(2).getReg() == SrcReg2) ||
        (Instr.getOperand(1).getReg() == SrcReg2 &&
         Instr.getOperand(2).getReg() == SrcReg))) {
      Sub = &*I;
      break;
    }

    if (I == B)
      // The 'and' is below the comparison instruction.
      return false;
  }

  // Return false if no candidates exist.
  if (!MI && !Sub)
    return false;

  // The single candidate is called MI.
  if (!MI) MI = Sub;

  int NewOpC = -1;
  int MIOpC = MI->getOpcode();
  if (MIOpC == PPC::ANDIo || MIOpC == PPC::ANDIo8 ||
      MIOpC == PPC::ANDISo || MIOpC == PPC::ANDISo8)
    NewOpC = MIOpC;
  else {
    NewOpC = PPC::getRecordFormOpcode(MIOpC);
    if (NewOpC == -1 && PPC::getNonRecordFormOpcode(MIOpC) != -1)
      NewOpC = MIOpC;
  }

  // FIXME: On the non-embedded POWER architectures, only some of the record
  // forms are fast, and we should use only the fast ones.

  // The defining instruction has a record form (or is already a record
  // form). It is possible, however, that we'll need to reverse the condition
  // code of the users.
  if (NewOpC == -1)
    return false;

  // If we have SUB(r1, r2) and CMP(r2, r1), the condition code based on CMP
  // needs to be updated to be based on SUB.  Push the condition code
  // operands to OperandsToUpdate.  If it is safe to remove CmpInstr, the
  // condition code of these operands will be modified.
  // Here, Value == 0 means we haven't converted comparison against 1 or -1 to
  // comparison against 0, which may modify predicate.
  bool ShouldSwap = false;
  if (Sub && Value == 0) {
    ShouldSwap = SrcReg2 != 0 && Sub->getOperand(1).getReg() == SrcReg2 &&
      Sub->getOperand(2).getReg() == SrcReg;

    // The operands to subf are the opposite of sub, so only in the fixed-point
    // case, invert the order.
    ShouldSwap = !ShouldSwap;
  }

  if (ShouldSwap)
    for (MachineRegisterInfo::use_instr_iterator
         I = MRI->use_instr_begin(CRReg), IE = MRI->use_instr_end();
         I != IE; ++I) {
      MachineInstr *UseMI = &*I;
      if (UseMI->getOpcode() == PPC::BCC) {
        PPC::Predicate Pred = (PPC::Predicate) UseMI->getOperand(0).getImm();
        unsigned PredCond = PPC::getPredicateCondition(Pred);
        assert((!equalityOnly ||
                PredCond == PPC::PRED_EQ || PredCond == PPC::PRED_NE) &&
               "Invalid predicate for equality-only optimization");
        (void)PredCond; // To suppress warning in release build.
        PredsToUpdate.push_back(std::make_pair(&(UseMI->getOperand(0)),
                                PPC::getSwappedPredicate(Pred)));
      } else if (UseMI->getOpcode() == PPC::ISEL ||
                 UseMI->getOpcode() == PPC::ISEL8) {
        unsigned NewSubReg = UseMI->getOperand(3).getSubReg();
        assert((!equalityOnly || NewSubReg == PPC::sub_eq) &&
               "Invalid CR bit for equality-only optimization");

        if (NewSubReg == PPC::sub_lt)
          NewSubReg = PPC::sub_gt;
        else if (NewSubReg == PPC::sub_gt)
          NewSubReg = PPC::sub_lt;

        SubRegsToUpdate.push_back(std::make_pair(&(UseMI->getOperand(3)),
                                                 NewSubReg));
      } else // We need to abort on a user we don't understand.
        return false;
    }
  assert(!(Value != 0 && ShouldSwap) &&
         "Non-zero immediate support and ShouldSwap"
         "may conflict in updating predicate");

  // Create a new virtual register to hold the value of the CR set by the
  // record-form instruction. If the instruction was not previously in
  // record form, then set the kill flag on the CR.
  CmpInstr.eraseFromParent();

  MachineBasicBlock::iterator MII = MI;
  BuildMI(*MI->getParent(), std::next(MII), MI->getDebugLoc(),
          get(TargetOpcode::COPY), CRReg)
    .addReg(PPC::CR0, MIOpC != NewOpC ? RegState::Kill : 0);

  // Even if CR0 register were dead before, it is alive now since the
  // instruction we just built uses it.
  MI->clearRegisterDeads(PPC::CR0);

  if (MIOpC != NewOpC) {
    // We need to be careful here: we're replacing one instruction with
    // another, and we need to make sure that we get all of the right
    // implicit uses and defs. On the other hand, the caller may be holding
    // an iterator to this instruction, and so we can't delete it (this is
    // specifically the case if this is the instruction directly after the
    // compare).

    // Rotates are expensive instructions. If we're emitting a record-form
    // rotate that can just be an andi/andis, we should just emit that.
    if (MIOpC == PPC::RLWINM || MIOpC == PPC::RLWINM8) {
      unsigned GPRRes = MI->getOperand(0).getReg();
      int64_t SH = MI->getOperand(2).getImm();
      int64_t MB = MI->getOperand(3).getImm();
      int64_t ME = MI->getOperand(4).getImm();
      // We can only do this if both the start and end of the mask are in the
      // same halfword.
      bool MBInLoHWord = MB >= 16;
      bool MEInLoHWord = ME >= 16;
      uint64_t Mask = ~0LLU;

      if (MB <= ME && MBInLoHWord == MEInLoHWord && SH == 0) {
        Mask = ((1LLU << (32 - MB)) - 1) & ~((1LLU << (31 - ME)) - 1);
        // The mask value needs to shift right 16 if we're emitting andis.
        Mask >>= MBInLoHWord ? 0 : 16;
        NewOpC = MIOpC == PPC::RLWINM ?
          (MBInLoHWord ? PPC::ANDIo : PPC::ANDISo) :
          (MBInLoHWord ? PPC::ANDIo8 :PPC::ANDISo8);
      } else if (MRI->use_empty(GPRRes) && (ME == 31) &&
                 (ME - MB + 1 == SH) && (MB >= 16)) {
        // If we are rotating by the exact number of bits as are in the mask
        // and the mask is in the least significant bits of the register,
        // that's just an andis. (as long as the GPR result has no uses).
        Mask = ((1LLU << 32) - 1) & ~((1LLU << (32 - SH)) - 1);
        Mask >>= 16;
        NewOpC = MIOpC == PPC::RLWINM ? PPC::ANDISo :PPC::ANDISo8;
      }
      // If we've set the mask, we can transform.
      if (Mask != ~0LLU) {
        MI->RemoveOperand(4);
        MI->RemoveOperand(3);
        MI->getOperand(2).setImm(Mask);
        NumRcRotatesConvertedToRcAnd++;
      }
    } else if (MIOpC == PPC::RLDICL && MI->getOperand(2).getImm() == 0) {
      int64_t MB = MI->getOperand(3).getImm();
      if (MB >= 48) {
        uint64_t Mask = (1LLU << (63 - MB + 1)) - 1;
        NewOpC = PPC::ANDIo8;
        MI->RemoveOperand(3);
        MI->getOperand(2).setImm(Mask);
        NumRcRotatesConvertedToRcAnd++;
      }
    }

    const MCInstrDesc &NewDesc = get(NewOpC);
    MI->setDesc(NewDesc);

    if (NewDesc.ImplicitDefs)
      for (const MCPhysReg *ImpDefs = NewDesc.getImplicitDefs();
           *ImpDefs; ++ImpDefs)
        if (!MI->definesRegister(*ImpDefs))
          MI->addOperand(*MI->getParent()->getParent(),
                         MachineOperand::CreateReg(*ImpDefs, true, true));
    if (NewDesc.ImplicitUses)
      for (const MCPhysReg *ImpUses = NewDesc.getImplicitUses();
           *ImpUses; ++ImpUses)
        if (!MI->readsRegister(*ImpUses))
          MI->addOperand(*MI->getParent()->getParent(),
                         MachineOperand::CreateReg(*ImpUses, false, true));
  }
  assert(MI->definesRegister(PPC::CR0) &&
         "Record-form instruction does not define cr0?");

  // Modify the condition code of operands in OperandsToUpdate.
  // Since we have SUB(r1, r2) and CMP(r2, r1), the condition code needs to
  // be changed from r2 > r1 to r1 < r2, from r2 < r1 to r1 > r2, etc.
  for (unsigned i = 0, e = PredsToUpdate.size(); i < e; i++)
    PredsToUpdate[i].first->setImm(PredsToUpdate[i].second);

  for (unsigned i = 0, e = SubRegsToUpdate.size(); i < e; i++)
    SubRegsToUpdate[i].first->setSubReg(SubRegsToUpdate[i].second);

  return true;
}

/// GetInstSize - Return the number of bytes of code the specified
/// instruction may be.  This returns the maximum number of bytes.
///
unsigned PPCInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();

  if (Opcode == PPC::INLINEASM) {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  } else if (Opcode == TargetOpcode::STACKMAP) {
    StackMapOpers Opers(&MI);
    return Opers.getNumPatchBytes();
  } else if (Opcode == TargetOpcode::PATCHPOINT) {
    PatchPointOpers Opers(&MI);
    return Opers.getNumPatchBytes();
  } else {
    return get(Opcode).getSize();
  }
}

std::pair<unsigned, unsigned>
PPCInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  const unsigned Mask = PPCII::MO_ACCESS_MASK;
  return std::make_pair(TF & Mask, TF & ~Mask);
}

ArrayRef<std::pair<unsigned, const char *>>
PPCInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace PPCII;
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_LO, "ppc-lo"},
      {MO_HA, "ppc-ha"},
      {MO_TPREL_LO, "ppc-tprel-lo"},
      {MO_TPREL_HA, "ppc-tprel-ha"},
      {MO_DTPREL_LO, "ppc-dtprel-lo"},
      {MO_TLSLD_LO, "ppc-tlsld-lo"},
      {MO_TOC_LO, "ppc-toc-lo"},
      {MO_TLS, "ppc-tls"}};
  return makeArrayRef(TargetFlags);
}

ArrayRef<std::pair<unsigned, const char *>>
PPCInstrInfo::getSerializableBitmaskMachineOperandTargetFlags() const {
  using namespace PPCII;
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_PLT, "ppc-plt"},
      {MO_PIC_FLAG, "ppc-pic"},
      {MO_NLP_FLAG, "ppc-nlp"},
      {MO_NLP_HIDDEN_FLAG, "ppc-nlp-hidden"}};
  return makeArrayRef(TargetFlags);
}

// Expand VSX Memory Pseudo instruction to either a VSX or a FP instruction.
// The VSX versions have the advantage of a full 64-register target whereas
// the FP ones have the advantage of lower latency and higher throughput. So
// what we are after is using the faster instructions in low register pressure
// situations and using the larger register file in high register pressure
// situations.
bool PPCInstrInfo::expandVSXMemPseudo(MachineInstr &MI) const {
    unsigned UpperOpcode, LowerOpcode;
    switch (MI.getOpcode()) {
    case PPC::DFLOADf32:
      UpperOpcode = PPC::LXSSP;
      LowerOpcode = PPC::LFS;
      break;
    case PPC::DFLOADf64:
      UpperOpcode = PPC::LXSD;
      LowerOpcode = PPC::LFD;
      break;
    case PPC::DFSTOREf32:
      UpperOpcode = PPC::STXSSP;
      LowerOpcode = PPC::STFS;
      break;
    case PPC::DFSTOREf64:
      UpperOpcode = PPC::STXSD;
      LowerOpcode = PPC::STFD;
      break;
    case PPC::XFLOADf32:
      UpperOpcode = PPC::LXSSPX;
      LowerOpcode = PPC::LFSX;
      break;
    case PPC::XFLOADf64:
      UpperOpcode = PPC::LXSDX;
      LowerOpcode = PPC::LFDX;
      break;
    case PPC::XFSTOREf32:
      UpperOpcode = PPC::STXSSPX;
      LowerOpcode = PPC::STFSX;
      break;
    case PPC::XFSTOREf64:
      UpperOpcode = PPC::STXSDX;
      LowerOpcode = PPC::STFDX;
      break;
    case PPC::LIWAX:
      UpperOpcode = PPC::LXSIWAX;
      LowerOpcode = PPC::LFIWAX;
      break;
    case PPC::LIWZX:
      UpperOpcode = PPC::LXSIWZX;
      LowerOpcode = PPC::LFIWZX;
      break;
    case PPC::STIWX:
      UpperOpcode = PPC::STXSIWX;
      LowerOpcode = PPC::STFIWX;
      break;
    default:
      llvm_unreachable("Unknown Operation!");
    }

    unsigned TargetReg = MI.getOperand(0).getReg();
    unsigned Opcode;
    if ((TargetReg >= PPC::F0 && TargetReg <= PPC::F31) ||
        (TargetReg >= PPC::VSL0 && TargetReg <= PPC::VSL31))
      Opcode = LowerOpcode;
    else
      Opcode = UpperOpcode;
    MI.setDesc(get(Opcode));
    return true;
}

static bool isAnImmediateOperand(const MachineOperand &MO) {
  return MO.isCPI() || MO.isGlobal() || MO.isImm();
}

bool PPCInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  auto &MBB = *MI.getParent();
  auto DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  case TargetOpcode::LOAD_STACK_GUARD: {
    assert(Subtarget.isTargetLinux() &&
           "Only Linux target is expected to contain LOAD_STACK_GUARD");
    const int64_t Offset = Subtarget.isPPC64() ? -0x7010 : -0x7008;
    const unsigned Reg = Subtarget.isPPC64() ? PPC::X13 : PPC::R2;
    MI.setDesc(get(Subtarget.isPPC64() ? PPC::LD : PPC::LWZ));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(Offset)
        .addReg(Reg);
    return true;
  }
  case PPC::DFLOADf32:
  case PPC::DFLOADf64:
  case PPC::DFSTOREf32:
  case PPC::DFSTOREf64: {
    assert(Subtarget.hasP9Vector() &&
           "Invalid D-Form Pseudo-ops on Pre-P9 target.");
    assert(MI.getOperand(2).isReg() &&
           isAnImmediateOperand(MI.getOperand(1)) &&
           "D-form op must have register and immediate operands");
    return expandVSXMemPseudo(MI);
  }
  case PPC::XFLOADf32:
  case PPC::XFSTOREf32:
  case PPC::LIWAX:
  case PPC::LIWZX:
  case PPC::STIWX: {
    assert(Subtarget.hasP8Vector() &&
           "Invalid X-Form Pseudo-ops on Pre-P8 target.");
    assert(MI.getOperand(2).isReg() && MI.getOperand(1).isReg() &&
           "X-form op must have register and register operands");
    return expandVSXMemPseudo(MI);
  }
  case PPC::XFLOADf64:
  case PPC::XFSTOREf64: {
    assert(Subtarget.hasVSX() &&
           "Invalid X-Form Pseudo-ops on target that has no VSX.");
    assert(MI.getOperand(2).isReg() && MI.getOperand(1).isReg() &&
           "X-form op must have register and register operands");
    return expandVSXMemPseudo(MI);
  }
  case PPC::SPILLTOVSR_LD: {
    unsigned TargetReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(TargetReg)) {
      MI.setDesc(get(PPC::DFLOADf64));
      return expandPostRAPseudo(MI);
    }
    else
      MI.setDesc(get(PPC::LD));
    return true;
  }
  case PPC::SPILLTOVSR_ST: {
    unsigned SrcReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(SrcReg)) {
      NumStoreSPILLVSRRCAsVec++;
      MI.setDesc(get(PPC::DFSTOREf64));
      return expandPostRAPseudo(MI);
    } else {
      NumStoreSPILLVSRRCAsGpr++;
      MI.setDesc(get(PPC::STD));
    }
    return true;
  }
  case PPC::SPILLTOVSR_LDX: {
    unsigned TargetReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(TargetReg))
      MI.setDesc(get(PPC::LXSDX));
    else
      MI.setDesc(get(PPC::LDX));
    return true;
  }
  case PPC::SPILLTOVSR_STX: {
    unsigned SrcReg = MI.getOperand(0).getReg();
    if (PPC::VSFRCRegClass.contains(SrcReg)) {
      NumStoreSPILLVSRRCAsVec++;
      MI.setDesc(get(PPC::STXSDX));
    } else {
      NumStoreSPILLVSRRCAsGpr++;
      MI.setDesc(get(PPC::STDX));
    }
    return true;
  }

  case PPC::CFENCE8: {
    auto Val = MI.getOperand(0).getReg();
    BuildMI(MBB, MI, DL, get(PPC::CMPD), PPC::CR7).addReg(Val).addReg(Val);
    BuildMI(MBB, MI, DL, get(PPC::CTRL_DEP))
        .addImm(PPC::PRED_NE_MINUS)
        .addReg(PPC::CR7)
        .addImm(1);
    MI.setDesc(get(PPC::ISYNC));
    MI.RemoveOperand(0);
    return true;
  }
  }
  return false;
}

// Essentially a compile-time implementation of a compare->isel sequence.
// It takes two constants to compare, along with the true/false registers
// and the comparison type (as a subreg to a CR field) and returns one
// of the true/false registers, depending on the comparison results.
static unsigned selectReg(int64_t Imm1, int64_t Imm2, unsigned CompareOpc,
                          unsigned TrueReg, unsigned FalseReg,
                          unsigned CRSubReg) {
  // Signed comparisons. The immediates are assumed to be sign-extended.
  if (CompareOpc == PPC::CMPWI || CompareOpc == PPC::CMPDI) {
    switch (CRSubReg) {
    default: llvm_unreachable("Unknown integer comparison type.");
    case PPC::sub_lt:
      return Imm1 < Imm2 ? TrueReg : FalseReg;
    case PPC::sub_gt:
      return Imm1 > Imm2 ? TrueReg : FalseReg;
    case PPC::sub_eq:
      return Imm1 == Imm2 ? TrueReg : FalseReg;
    }
  }
  // Unsigned comparisons.
  else if (CompareOpc == PPC::CMPLWI || CompareOpc == PPC::CMPLDI) {
    switch (CRSubReg) {
    default: llvm_unreachable("Unknown integer comparison type.");
    case PPC::sub_lt:
      return (uint64_t)Imm1 < (uint64_t)Imm2 ? TrueReg : FalseReg;
    case PPC::sub_gt:
      return (uint64_t)Imm1 > (uint64_t)Imm2 ? TrueReg : FalseReg;
    case PPC::sub_eq:
      return Imm1 == Imm2 ? TrueReg : FalseReg;
    }
  }
  return PPC::NoRegister;
}

void PPCInstrInfo::replaceInstrOperandWithImm(MachineInstr &MI,
                                              unsigned OpNo,
                                              int64_t Imm) const {
  assert(MI.getOperand(OpNo).isReg() && "Operand must be a REG");
  // Replace the REG with the Immediate.
  unsigned InUseReg = MI.getOperand(OpNo).getReg();
  MI.getOperand(OpNo).ChangeToImmediate(Imm);

  if (empty(MI.implicit_operands()))
    return;

  // We need to make sure that the MI didn't have any implicit use
  // of this REG any more.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  int UseOpIdx = MI.findRegisterUseOperandIdx(InUseReg, false, TRI);
  if (UseOpIdx >= 0) {
    MachineOperand &MO = MI.getOperand(UseOpIdx);
    if (MO.isImplicit())
      // The operands must always be in the following order:
      // - explicit reg defs,
      // - other explicit operands (reg uses, immediates, etc.),
      // - implicit reg defs
      // - implicit reg uses
      // Therefore, removing the implicit operand won't change the explicit
      // operands layout.
      MI.RemoveOperand(UseOpIdx);
  }
}

// Replace an instruction with one that materializes a constant (and sets
// CR0 if the original instruction was a record-form instruction).
void PPCInstrInfo::replaceInstrWithLI(MachineInstr &MI,
                                      const LoadImmediateInfo &LII) const {
  // Remove existing operands.
  int OperandToKeep = LII.SetCR ? 1 : 0;
  for (int i = MI.getNumOperands() - 1; i > OperandToKeep; i--)
    MI.RemoveOperand(i);

  // Replace the instruction.
  if (LII.SetCR) {
    MI.setDesc(get(LII.Is64Bit ? PPC::ANDIo8 : PPC::ANDIo));
    // Set the immediate.
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(LII.Imm).addReg(PPC::CR0, RegState::ImplicitDefine);
    return;
  }
  else
    MI.setDesc(get(LII.Is64Bit ? PPC::LI8 : PPC::LI));

  // Set the immediate.
  MachineInstrBuilder(*MI.getParent()->getParent(), MI)
      .addImm(LII.Imm);
}

MachineInstr *PPCInstrInfo::getForwardingDefMI(
  MachineInstr &MI,
  unsigned &OpNoForForwarding,
  bool &SeenIntermediateUse) const {
  OpNoForForwarding = ~0U;
  MachineInstr *DefMI = nullptr;
  MachineRegisterInfo *MRI = &MI.getParent()->getParent()->getRegInfo();
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  // If we're in SSA, get the defs through the MRI. Otherwise, only look
  // within the basic block to see if the register is defined using an LI/LI8.
  if (MRI->isSSA()) {
    for (int i = 1, e = MI.getNumOperands(); i < e; i++) {
      if (!MI.getOperand(i).isReg())
        continue;
      unsigned Reg = MI.getOperand(i).getReg();
      if (!TargetRegisterInfo::isVirtualRegister(Reg))
        continue;
      unsigned TrueReg = TRI->lookThruCopyLike(Reg, MRI);
      if (TargetRegisterInfo::isVirtualRegister(TrueReg)) {
        DefMI = MRI->getVRegDef(TrueReg);
        if (DefMI->getOpcode() == PPC::LI || DefMI->getOpcode() == PPC::LI8) {
          OpNoForForwarding = i;
          break;
        }
      }
    }
  } else {
    // Looking back through the definition for each operand could be expensive,
    // so exit early if this isn't an instruction that either has an immediate
    // form or is already an immediate form that we can handle.
    ImmInstrInfo III;
    unsigned Opc = MI.getOpcode();
    bool ConvertibleImmForm =
      Opc == PPC::CMPWI || Opc == PPC::CMPLWI ||
      Opc == PPC::CMPDI || Opc == PPC::CMPLDI ||
      Opc == PPC::ADDI || Opc == PPC::ADDI8 ||
      Opc == PPC::ORI || Opc == PPC::ORI8 ||
      Opc == PPC::XORI || Opc == PPC::XORI8 ||
      Opc == PPC::RLDICL || Opc == PPC::RLDICLo ||
      Opc == PPC::RLDICL_32 || Opc == PPC::RLDICL_32_64 ||
      Opc == PPC::RLWINM || Opc == PPC::RLWINMo ||
      Opc == PPC::RLWINM8 || Opc == PPC::RLWINM8o;
    if (!instrHasImmForm(MI, III, true) && !ConvertibleImmForm)
      return nullptr;

    // Don't convert or %X, %Y, %Y since that's just a register move.
    if ((Opc == PPC::OR || Opc == PPC::OR8) &&
        MI.getOperand(1).getReg() == MI.getOperand(2).getReg())
      return nullptr;
    for (int i = 1, e = MI.getNumOperands(); i < e; i++) {
      MachineOperand &MO = MI.getOperand(i);
      SeenIntermediateUse = false;
      if (MO.isReg() && MO.isUse() && !MO.isImplicit()) {
        MachineBasicBlock::reverse_iterator E = MI.getParent()->rend(), It = MI;
        It++;
        unsigned Reg = MI.getOperand(i).getReg();
        // MachineInstr::readsRegister only returns true if the machine
        // instruction reads the exact register or its super-register. It
        // does not consider uses of sub-registers which seems like strange
        // behaviour. Nonetheless, if we end up with a 64-bit register here,
        // get the corresponding 32-bit register to check.
        if (PPC::G8RCRegClass.contains(Reg))
          Reg = Reg - PPC::X0 + PPC::R0;

        // Is this register defined by some form of add-immediate (including
        // load-immediate) within this basic block?
        for ( ; It != E; ++It) {
          if (It->modifiesRegister(Reg, &getRegisterInfo())) {
            switch (It->getOpcode()) {
            default: break;
            case PPC::LI:
            case PPC::LI8:
            case PPC::ADDItocL:
            case PPC::ADDI:
            case PPC::ADDI8:
              OpNoForForwarding = i;
              return &*It;
            }
            break;
          } else if (It->readsRegister(Reg, &getRegisterInfo())) 
            // If we see another use of this reg between the def and the MI,
            // we want to flat it so the def isn't deleted.
            SeenIntermediateUse = true;
        }
      }
    }
  }
  return OpNoForForwarding == ~0U ? nullptr : DefMI;
}

const unsigned *PPCInstrInfo::getStoreOpcodesForSpillArray() const {
  static const unsigned OpcodesForSpill[2][SOK_LastOpcodeSpill] = {
      // Power 8
      {PPC::STW, PPC::STD, PPC::STFD, PPC::STFS, PPC::SPILL_CR,
       PPC::SPILL_CRBIT, PPC::STVX, PPC::STXVD2X, PPC::STXSDX, PPC::STXSSPX,
       PPC::SPILL_VRSAVE, PPC::QVSTFDX, PPC::QVSTFSXs, PPC::QVSTFDXb,
       PPC::SPILLTOVSR_ST, PPC::EVSTDD, PPC::SPESTW},
      // Power 9
      {PPC::STW, PPC::STD, PPC::STFD, PPC::STFS, PPC::SPILL_CR,
       PPC::SPILL_CRBIT, PPC::STVX, PPC::STXV, PPC::DFSTOREf64, PPC::DFSTOREf32,
       PPC::SPILL_VRSAVE, PPC::QVSTFDX, PPC::QVSTFSXs, PPC::QVSTFDXb,
       PPC::SPILLTOVSR_ST}};

  return OpcodesForSpill[(Subtarget.hasP9Vector()) ? 1 : 0];
}

const unsigned *PPCInstrInfo::getLoadOpcodesForSpillArray() const {
  static const unsigned OpcodesForSpill[2][SOK_LastOpcodeSpill] = {
      // Power 8
      {PPC::LWZ, PPC::LD, PPC::LFD, PPC::LFS, PPC::RESTORE_CR,
       PPC::RESTORE_CRBIT, PPC::LVX, PPC::LXVD2X, PPC::LXSDX, PPC::LXSSPX,
       PPC::RESTORE_VRSAVE, PPC::QVLFDX, PPC::QVLFSXs, PPC::QVLFDXb,
       PPC::SPILLTOVSR_LD, PPC::EVLDD, PPC::SPELWZ},
      // Power 9
      {PPC::LWZ, PPC::LD, PPC::LFD, PPC::LFS, PPC::RESTORE_CR,
       PPC::RESTORE_CRBIT, PPC::LVX, PPC::LXV, PPC::DFLOADf64, PPC::DFLOADf32,
       PPC::RESTORE_VRSAVE, PPC::QVLFDX, PPC::QVLFSXs, PPC::QVLFDXb,
       PPC::SPILLTOVSR_LD}};

  return OpcodesForSpill[(Subtarget.hasP9Vector()) ? 1 : 0];
}

// If this instruction has an immediate form and one of its operands is a
// result of a load-immediate or an add-immediate, convert it to
// the immediate form if the constant is in range.
bool PPCInstrInfo::convertToImmediateForm(MachineInstr &MI,
                                          MachineInstr **KilledDef) const {
  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  bool PostRA = !MRI->isSSA();
  bool SeenIntermediateUse = true;
  unsigned ForwardingOperand = ~0U;
  MachineInstr *DefMI = getForwardingDefMI(MI, ForwardingOperand,
                                           SeenIntermediateUse);
  if (!DefMI)
    return false;
  assert(ForwardingOperand < MI.getNumOperands() &&
         "The forwarding operand needs to be valid at this point");
  bool KillFwdDefMI = !SeenIntermediateUse &&
    MI.getOperand(ForwardingOperand).isKill();
  if (KilledDef && KillFwdDefMI)
    *KilledDef = DefMI;

  ImmInstrInfo III;
  bool HasImmForm = instrHasImmForm(MI, III, PostRA);
  // If this is a reg+reg instruction that has a reg+imm form,
  // and one of the operands is produced by an add-immediate,
  // try to convert it.
  if (HasImmForm && transformToImmFormFedByAdd(MI, III, ForwardingOperand,
                                               *DefMI, KillFwdDefMI))
    return true;

  if ((DefMI->getOpcode() != PPC::LI && DefMI->getOpcode() != PPC::LI8) ||
      !DefMI->getOperand(1).isImm())
    return false;

  int64_t Immediate = DefMI->getOperand(1).getImm();
  // Sign-extend to 64-bits.
  int64_t SExtImm = ((uint64_t)Immediate & ~0x7FFFuLL) != 0 ?
    (Immediate | 0xFFFFFFFFFFFF0000) : Immediate;

  // If this is a reg+reg instruction that has a reg+imm form,
  // and one of the operands is produced by LI, convert it now.
  if (HasImmForm)
    return transformToImmFormFedByLI(MI, III, ForwardingOperand, SExtImm);

  bool ReplaceWithLI = false;
  bool Is64BitLI = false;
  int64_t NewImm = 0;
  bool SetCR = false;
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default: return false;

  // FIXME: Any branches conditional on such a comparison can be made
  // unconditional. At this time, this happens too infrequently to be worth
  // the implementation effort, but if that ever changes, we could convert
  // such a pattern here.
  case PPC::CMPWI:
  case PPC::CMPLWI:
  case PPC::CMPDI:
  case PPC::CMPLDI: {
    // Doing this post-RA would require dataflow analysis to reliably find uses
    // of the CR register set by the compare.
    if (PostRA)
      return false;
    // If a compare-immediate is fed by an immediate and is itself an input of
    // an ISEL (the most common case) into a COPY of the correct register.
    bool Changed = false;
    unsigned DefReg = MI.getOperand(0).getReg();
    int64_t Comparand = MI.getOperand(2).getImm();
    int64_t SExtComparand = ((uint64_t)Comparand & ~0x7FFFuLL) != 0 ?
      (Comparand | 0xFFFFFFFFFFFF0000) : Comparand;

    for (auto &CompareUseMI : MRI->use_instructions(DefReg)) {
      unsigned UseOpc = CompareUseMI.getOpcode();
      if (UseOpc != PPC::ISEL && UseOpc != PPC::ISEL8)
        continue;
      unsigned CRSubReg = CompareUseMI.getOperand(3).getSubReg();
      unsigned TrueReg = CompareUseMI.getOperand(1).getReg();
      unsigned FalseReg = CompareUseMI.getOperand(2).getReg();
      unsigned RegToCopy = selectReg(SExtImm, SExtComparand, Opc, TrueReg,
                                     FalseReg, CRSubReg);
      if (RegToCopy == PPC::NoRegister)
        continue;
      // Can't use PPC::COPY to copy PPC::ZERO[8]. Convert it to LI[8] 0.
      if (RegToCopy == PPC::ZERO || RegToCopy == PPC::ZERO8) {
        CompareUseMI.setDesc(get(UseOpc == PPC::ISEL8 ? PPC::LI8 : PPC::LI));
        replaceInstrOperandWithImm(CompareUseMI, 1, 0);
        CompareUseMI.RemoveOperand(3);
        CompareUseMI.RemoveOperand(2);
        continue;
      }
      LLVM_DEBUG(
          dbgs() << "Found LI -> CMPI -> ISEL, replacing with a copy.\n");
      LLVM_DEBUG(DefMI->dump(); MI.dump(); CompareUseMI.dump());
      LLVM_DEBUG(dbgs() << "Is converted to:\n");
      // Convert to copy and remove unneeded operands.
      CompareUseMI.setDesc(get(PPC::COPY));
      CompareUseMI.RemoveOperand(3);
      CompareUseMI.RemoveOperand(RegToCopy == TrueReg ? 2 : 1);
      CmpIselsConverted++;
      Changed = true;
      LLVM_DEBUG(CompareUseMI.dump());
    }
    if (Changed)
      return true;
    // This may end up incremented multiple times since this function is called
    // during a fixed-point transformation, but it is only meant to indicate the
    // presence of this opportunity.
    MissedConvertibleImmediateInstrs++;
    return false;
  }

  // Immediate forms - may simply be convertable to an LI.
  case PPC::ADDI:
  case PPC::ADDI8: {
    // Does the sum fit in a 16-bit signed field?
    int64_t Addend = MI.getOperand(2).getImm();
    if (isInt<16>(Addend + SExtImm)) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::ADDI8;
      NewImm = Addend + SExtImm;
      break;
    }
    return false;
  }
  case PPC::RLDICL:
  case PPC::RLDICLo:
  case PPC::RLDICL_32:
  case PPC::RLDICL_32_64: {
    // Use APInt's rotate function.
    int64_t SH = MI.getOperand(2).getImm();
    int64_t MB = MI.getOperand(3).getImm();
    APInt InVal((Opc == PPC::RLDICL || Opc == PPC::RLDICLo) ?
                64 : 32, SExtImm, true);
    InVal = InVal.rotl(SH);
    uint64_t Mask = (1LLU << (63 - MB + 1)) - 1;
    InVal &= Mask;
    // Can't replace negative values with an LI as that will sign-extend
    // and not clear the left bits. If we're setting the CR bit, we will use
    // ANDIo which won't sign extend, so that's safe.
    if (isUInt<15>(InVal.getSExtValue()) ||
        (Opc == PPC::RLDICLo && isUInt<16>(InVal.getSExtValue()))) {
      ReplaceWithLI = true;
      Is64BitLI = Opc != PPC::RLDICL_32;
      NewImm = InVal.getSExtValue();
      SetCR = Opc == PPC::RLDICLo;
      break;
    }
    return false;
  }
  case PPC::RLWINM:
  case PPC::RLWINM8:
  case PPC::RLWINMo:
  case PPC::RLWINM8o: {
    int64_t SH = MI.getOperand(2).getImm();
    int64_t MB = MI.getOperand(3).getImm();
    int64_t ME = MI.getOperand(4).getImm();
    APInt InVal(32, SExtImm, true);
    InVal = InVal.rotl(SH);
    // Set the bits (        MB + 32        ) to (        ME + 32        ).
    uint64_t Mask = ((1LLU << (32 - MB)) - 1) & ~((1LLU << (31 - ME)) - 1);
    InVal &= Mask;
    // Can't replace negative values with an LI as that will sign-extend
    // and not clear the left bits. If we're setting the CR bit, we will use
    // ANDIo which won't sign extend, so that's safe.
    bool ValueFits = isUInt<15>(InVal.getSExtValue());
    ValueFits |= ((Opc == PPC::RLWINMo || Opc == PPC::RLWINM8o) &&
                  isUInt<16>(InVal.getSExtValue()));
    if (ValueFits) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::RLWINM8 || Opc == PPC::RLWINM8o;
      NewImm = InVal.getSExtValue();
      SetCR = Opc == PPC::RLWINMo || Opc == PPC::RLWINM8o;
      break;
    }
    return false;
  }
  case PPC::ORI:
  case PPC::ORI8:
  case PPC::XORI:
  case PPC::XORI8: {
    int64_t LogicalImm = MI.getOperand(2).getImm();
    int64_t Result = 0;
    if (Opc == PPC::ORI || Opc == PPC::ORI8)
      Result = LogicalImm | SExtImm;
    else
      Result = LogicalImm ^ SExtImm;
    if (isInt<16>(Result)) {
      ReplaceWithLI = true;
      Is64BitLI = Opc == PPC::ORI8 || Opc == PPC::XORI8;
      NewImm = Result;
      break;
    }
    return false;
  }
  }

  if (ReplaceWithLI) {
    // We need to be careful with CR-setting instructions we're replacing.
    if (SetCR) {
      // We don't know anything about uses when we're out of SSA, so only
      // replace if the new immediate will be reproduced.
      bool ImmChanged = (SExtImm & NewImm) != NewImm;
      if (PostRA && ImmChanged)
        return false;

      if (!PostRA) {
        // If the defining load-immediate has no other uses, we can just replace
        // the immediate with the new immediate.
        if (MRI->hasOneUse(DefMI->getOperand(0).getReg()))
          DefMI->getOperand(1).setImm(NewImm);

        // If we're not using the GPR result of the CR-setting instruction, we
        // just need to and with zero/non-zero depending on the new immediate.
        else if (MRI->use_empty(MI.getOperand(0).getReg())) {
          if (NewImm) {
            assert(Immediate && "Transformation converted zero to non-zero?");
            NewImm = Immediate;
          }
        }
        else if (ImmChanged)
          return false;
      }
    }

    LLVM_DEBUG(dbgs() << "Replacing instruction:\n");
    LLVM_DEBUG(MI.dump());
    LLVM_DEBUG(dbgs() << "Fed by:\n");
    LLVM_DEBUG(DefMI->dump());
    LoadImmediateInfo LII;
    LII.Imm = NewImm;
    LII.Is64Bit = Is64BitLI;
    LII.SetCR = SetCR;
    // If we're setting the CR, the original load-immediate must be kept (as an
    // operand to ANDIo/ANDI8o).
    if (KilledDef && SetCR)
      *KilledDef = nullptr;
    replaceInstrWithLI(MI, LII);
    LLVM_DEBUG(dbgs() << "With:\n");
    LLVM_DEBUG(MI.dump());
    return true;
  }
  return false;
}

static bool isVFReg(unsigned Reg) {
  return PPC::VFRCRegClass.contains(Reg);
}

bool PPCInstrInfo::instrHasImmForm(const MachineInstr &MI,
                                   ImmInstrInfo &III, bool PostRA) const {
  unsigned Opc = MI.getOpcode();
  // The vast majority of the instructions would need their operand 2 replaced
  // with an immediate when switching to the reg+imm form. A marked exception
  // are the update form loads/stores for which a constant operand 2 would need
  // to turn into a displacement and move operand 1 to the operand 2 position.
  III.ImmOpNo = 2;
  III.OpNoForForwarding = 2;
  III.ImmWidth = 16;
  III.ImmMustBeMultipleOf = 1;
  III.TruncateImmTo = 0;
  III.IsSummingOperands = false;
  switch (Opc) {
  default: return false;
  case PPC::ADD4:
  case PPC::ADD8:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 1;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpcode = Opc == PPC::ADD4 ? PPC::ADDI : PPC::ADDI8;
    break;
  case PPC::ADDC:
  case PPC::ADDC8:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpcode = Opc == PPC::ADDC ? PPC::ADDIC : PPC::ADDIC8;
    break;
  case PPC::ADDCo:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpcode = PPC::ADDICo;
    break;
  case PPC::SUBFC:
  case PPC::SUBFC8:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    III.ImmOpcode = Opc == PPC::SUBFC ? PPC::SUBFIC : PPC::SUBFIC8;
    break;
  case PPC::CMPW:
  case PPC::CMPD:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    III.ImmOpcode = Opc == PPC::CMPW ? PPC::CMPWI : PPC::CMPDI;
    break;
  case PPC::CMPLW:
  case PPC::CMPLD:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    III.ImmOpcode = Opc == PPC::CMPLW ? PPC::CMPLWI : PPC::CMPLDI;
    break;
  case PPC::ANDo:
  case PPC::AND8o:
  case PPC::OR:
  case PPC::OR8:
  case PPC::XOR:
  case PPC::XOR8:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = true;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::ANDo: III.ImmOpcode = PPC::ANDIo; break;
    case PPC::AND8o: III.ImmOpcode = PPC::ANDIo8; break;
    case PPC::OR: III.ImmOpcode = PPC::ORI; break;
    case PPC::OR8: III.ImmOpcode = PPC::ORI8; break;
    case PPC::XOR: III.ImmOpcode = PPC::XORI; break;
    case PPC::XOR8: III.ImmOpcode = PPC::XORI8; break;
    }
    break;
  case PPC::RLWNM:
  case PPC::RLWNM8:
  case PPC::RLWNMo:
  case PPC::RLWNM8o:
  case PPC::SLW:
  case PPC::SLW8:
  case PPC::SLWo:
  case PPC::SLW8o:
  case PPC::SRW:
  case PPC::SRW8:
  case PPC::SRWo:
  case PPC::SRW8o:
  case PPC::SRAW:
  case PPC::SRAWo:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    // This isn't actually true, but the instructions ignore any of the
    // upper bits, so any immediate loaded with an LI is acceptable.
    // This does not apply to shift right algebraic because a value
    // out of range will produce a -1/0.
    III.ImmWidth = 16;
    if (Opc == PPC::RLWNM || Opc == PPC::RLWNM8 ||
        Opc == PPC::RLWNMo || Opc == PPC::RLWNM8o)
      III.TruncateImmTo = 5;
    else
      III.TruncateImmTo = 6;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::RLWNM: III.ImmOpcode = PPC::RLWINM; break;
    case PPC::RLWNM8: III.ImmOpcode = PPC::RLWINM8; break;
    case PPC::RLWNMo: III.ImmOpcode = PPC::RLWINMo; break;
    case PPC::RLWNM8o: III.ImmOpcode = PPC::RLWINM8o; break;
    case PPC::SLW: III.ImmOpcode = PPC::RLWINM; break;
    case PPC::SLW8: III.ImmOpcode = PPC::RLWINM8; break;
    case PPC::SLWo: III.ImmOpcode = PPC::RLWINMo; break;
    case PPC::SLW8o: III.ImmOpcode = PPC::RLWINM8o; break;
    case PPC::SRW: III.ImmOpcode = PPC::RLWINM; break;
    case PPC::SRW8: III.ImmOpcode = PPC::RLWINM8; break;
    case PPC::SRWo: III.ImmOpcode = PPC::RLWINMo; break;
    case PPC::SRW8o: III.ImmOpcode = PPC::RLWINM8o; break;
    case PPC::SRAW:
      III.ImmWidth = 5;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRAWI;
      break;
    case PPC::SRAWo:
      III.ImmWidth = 5;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRAWIo;
      break;
    }
    break;
  case PPC::RLDCL:
  case PPC::RLDCLo:
  case PPC::RLDCR:
  case PPC::RLDCRo:
  case PPC::SLD:
  case PPC::SLDo:
  case PPC::SRD:
  case PPC::SRDo:
  case PPC::SRAD:
  case PPC::SRADo:
    III.SignedImm = false;
    III.ZeroIsSpecialOrig = 0;
    III.ZeroIsSpecialNew = 0;
    III.IsCommutative = false;
    // This isn't actually true, but the instructions ignore any of the
    // upper bits, so any immediate loaded with an LI is acceptable.
    // This does not apply to shift right algebraic because a value
    // out of range will produce a -1/0.
    III.ImmWidth = 16;
    if (Opc == PPC::RLDCL || Opc == PPC::RLDCLo ||
        Opc == PPC::RLDCR || Opc == PPC::RLDCRo)
      III.TruncateImmTo = 6;
    else
      III.TruncateImmTo = 7;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::RLDCL: III.ImmOpcode = PPC::RLDICL; break;
    case PPC::RLDCLo: III.ImmOpcode = PPC::RLDICLo; break;
    case PPC::RLDCR: III.ImmOpcode = PPC::RLDICR; break;
    case PPC::RLDCRo: III.ImmOpcode = PPC::RLDICRo; break;
    case PPC::SLD: III.ImmOpcode = PPC::RLDICR; break;
    case PPC::SLDo: III.ImmOpcode = PPC::RLDICRo; break;
    case PPC::SRD: III.ImmOpcode = PPC::RLDICL; break;
    case PPC::SRDo: III.ImmOpcode = PPC::RLDICLo; break;
    case PPC::SRAD:
      III.ImmWidth = 6;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRADI;
       break;
    case PPC::SRADo:
      III.ImmWidth = 6;
      III.TruncateImmTo = 0;
      III.ImmOpcode = PPC::SRADIo;
      break;
    }
    break;
  // Loads and stores:
  case PPC::LBZX:
  case PPC::LBZX8:
  case PPC::LHZX:
  case PPC::LHZX8:
  case PPC::LHAX:
  case PPC::LHAX8:
  case PPC::LWZX:
  case PPC::LWZX8:
  case PPC::LWAX:
  case PPC::LDX:
  case PPC::LFSX:
  case PPC::LFDX:
  case PPC::STBX:
  case PPC::STBX8:
  case PPC::STHX:
  case PPC::STHX8:
  case PPC::STWX:
  case PPC::STWX8:
  case PPC::STDX:
  case PPC::STFSX:
  case PPC::STFDX:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 1;
    III.ZeroIsSpecialNew = 2;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpNo = 1;
    III.OpNoForForwarding = 2;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::LBZX: III.ImmOpcode = PPC::LBZ; break;
    case PPC::LBZX8: III.ImmOpcode = PPC::LBZ8; break;
    case PPC::LHZX: III.ImmOpcode = PPC::LHZ; break;
    case PPC::LHZX8: III.ImmOpcode = PPC::LHZ8; break;
    case PPC::LHAX: III.ImmOpcode = PPC::LHA; break;
    case PPC::LHAX8: III.ImmOpcode = PPC::LHA8; break;
    case PPC::LWZX: III.ImmOpcode = PPC::LWZ; break;
    case PPC::LWZX8: III.ImmOpcode = PPC::LWZ8; break;
    case PPC::LWAX:
      III.ImmOpcode = PPC::LWA;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::LDX: III.ImmOpcode = PPC::LD; III.ImmMustBeMultipleOf = 4; break;
    case PPC::LFSX: III.ImmOpcode = PPC::LFS; break;
    case PPC::LFDX: III.ImmOpcode = PPC::LFD; break;
    case PPC::STBX: III.ImmOpcode = PPC::STB; break;
    case PPC::STBX8: III.ImmOpcode = PPC::STB8; break;
    case PPC::STHX: III.ImmOpcode = PPC::STH; break;
    case PPC::STHX8: III.ImmOpcode = PPC::STH8; break;
    case PPC::STWX: III.ImmOpcode = PPC::STW; break;
    case PPC::STWX8: III.ImmOpcode = PPC::STW8; break;
    case PPC::STDX:
      III.ImmOpcode = PPC::STD;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::STFSX: III.ImmOpcode = PPC::STFS; break;
    case PPC::STFDX: III.ImmOpcode = PPC::STFD; break;
    }
    break;
  case PPC::LBZUX:
  case PPC::LBZUX8:
  case PPC::LHZUX:
  case PPC::LHZUX8:
  case PPC::LHAUX:
  case PPC::LHAUX8:
  case PPC::LWZUX:
  case PPC::LWZUX8:
  case PPC::LDUX:
  case PPC::LFSUX:
  case PPC::LFDUX:
  case PPC::STBUX:
  case PPC::STBUX8:
  case PPC::STHUX:
  case PPC::STHUX8:
  case PPC::STWUX:
  case PPC::STWUX8:
  case PPC::STDUX:
  case PPC::STFSUX:
  case PPC::STFDUX:
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 2;
    III.ZeroIsSpecialNew = 3;
    III.IsCommutative = false;
    III.IsSummingOperands = true;
    III.ImmOpNo = 2;
    III.OpNoForForwarding = 3;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::LBZUX: III.ImmOpcode = PPC::LBZU; break;
    case PPC::LBZUX8: III.ImmOpcode = PPC::LBZU8; break;
    case PPC::LHZUX: III.ImmOpcode = PPC::LHZU; break;
    case PPC::LHZUX8: III.ImmOpcode = PPC::LHZU8; break;
    case PPC::LHAUX: III.ImmOpcode = PPC::LHAU; break;
    case PPC::LHAUX8: III.ImmOpcode = PPC::LHAU8; break;
    case PPC::LWZUX: III.ImmOpcode = PPC::LWZU; break;
    case PPC::LWZUX8: III.ImmOpcode = PPC::LWZU8; break;
    case PPC::LDUX:
      III.ImmOpcode = PPC::LDU;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::LFSUX: III.ImmOpcode = PPC::LFSU; break;
    case PPC::LFDUX: III.ImmOpcode = PPC::LFDU; break;
    case PPC::STBUX: III.ImmOpcode = PPC::STBU; break;
    case PPC::STBUX8: III.ImmOpcode = PPC::STBU8; break;
    case PPC::STHUX: III.ImmOpcode = PPC::STHU; break;
    case PPC::STHUX8: III.ImmOpcode = PPC::STHU8; break;
    case PPC::STWUX: III.ImmOpcode = PPC::STWU; break;
    case PPC::STWUX8: III.ImmOpcode = PPC::STWU8; break;
    case PPC::STDUX:
      III.ImmOpcode = PPC::STDU;
      III.ImmMustBeMultipleOf = 4;
      break;
    case PPC::STFSUX: III.ImmOpcode = PPC::STFSU; break;
    case PPC::STFDUX: III.ImmOpcode = PPC::STFDU; break;
    }
    break;
  // Power9 and up only. For some of these, the X-Form version has access to all
  // 64 VSR's whereas the D-Form only has access to the VR's. We replace those
  // with pseudo-ops pre-ra and for post-ra, we check that the register loaded
  // into or stored from is one of the VR registers.
  case PPC::LXVX:
  case PPC::LXSSPX:
  case PPC::LXSDX:
  case PPC::STXVX:
  case PPC::STXSSPX:
  case PPC::STXSDX:
  case PPC::XFLOADf32:
  case PPC::XFLOADf64:
  case PPC::XFSTOREf32:
  case PPC::XFSTOREf64:
    if (!Subtarget.hasP9Vector())
      return false;
    III.SignedImm = true;
    III.ZeroIsSpecialOrig = 1;
    III.ZeroIsSpecialNew = 2;
    III.IsCommutative = true;
    III.IsSummingOperands = true;
    III.ImmOpNo = 1;
    III.OpNoForForwarding = 2;
    III.ImmMustBeMultipleOf = 4;
    switch(Opc) {
    default: llvm_unreachable("Unknown opcode");
    case PPC::LXVX:
      III.ImmOpcode = PPC::LXV;
      III.ImmMustBeMultipleOf = 16;
      break;
    case PPC::LXSSPX:
      if (PostRA) {
        if (isVFReg(MI.getOperand(0).getReg()))
          III.ImmOpcode = PPC::LXSSP;
        else {
          III.ImmOpcode = PPC::LFS;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      LLVM_FALLTHROUGH;
    case PPC::XFLOADf32:
      III.ImmOpcode = PPC::DFLOADf32;
      break;
    case PPC::LXSDX:
      if (PostRA) {
        if (isVFReg(MI.getOperand(0).getReg()))
          III.ImmOpcode = PPC::LXSD;
        else {
          III.ImmOpcode = PPC::LFD;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      LLVM_FALLTHROUGH;
    case PPC::XFLOADf64:
      III.ImmOpcode = PPC::DFLOADf64;
      break;
    case PPC::STXVX:
      III.ImmOpcode = PPC::STXV;
      III.ImmMustBeMultipleOf = 16;
      break;
    case PPC::STXSSPX:
      if (PostRA) {
        if (isVFReg(MI.getOperand(0).getReg()))
          III.ImmOpcode = PPC::STXSSP;
        else {
          III.ImmOpcode = PPC::STFS;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      LLVM_FALLTHROUGH;
    case PPC::XFSTOREf32:
      III.ImmOpcode = PPC::DFSTOREf32;
      break;
    case PPC::STXSDX:
      if (PostRA) {
        if (isVFReg(MI.getOperand(0).getReg()))
          III.ImmOpcode = PPC::STXSD;
        else {
          III.ImmOpcode = PPC::STFD;
          III.ImmMustBeMultipleOf = 1;
        }
        break;
      }
      LLVM_FALLTHROUGH;
    case PPC::XFSTOREf64:
      III.ImmOpcode = PPC::DFSTOREf64;
      break;
    }
    break;
  }
  return true;
}

// Utility function for swaping two arbitrary operands of an instruction.
static void swapMIOperands(MachineInstr &MI, unsigned Op1, unsigned Op2) {
  assert(Op1 != Op2 && "Cannot swap operand with itself.");

  unsigned MaxOp = std::max(Op1, Op2);
  unsigned MinOp = std::min(Op1, Op2);
  MachineOperand MOp1 = MI.getOperand(MinOp);
  MachineOperand MOp2 = MI.getOperand(MaxOp);
  MI.RemoveOperand(std::max(Op1, Op2));
  MI.RemoveOperand(std::min(Op1, Op2));

  // If the operands we are swapping are the two at the end (the common case)
  // we can just remove both and add them in the opposite order.
  if (MaxOp - MinOp == 1 && MI.getNumOperands() == MinOp) {
    MI.addOperand(MOp2);
    MI.addOperand(MOp1);
  } else {
    // Store all operands in a temporary vector, remove them and re-add in the
    // right order.
    SmallVector<MachineOperand, 2> MOps;
    unsigned TotalOps = MI.getNumOperands() + 2; // We've already removed 2 ops.
    for (unsigned i = MI.getNumOperands() - 1; i >= MinOp; i--) {
      MOps.push_back(MI.getOperand(i));
      MI.RemoveOperand(i);
    }
    // MOp2 needs to be added next.
    MI.addOperand(MOp2);
    // Now add the rest.
    for (unsigned i = MI.getNumOperands(); i < TotalOps; i++) {
      if (i == MaxOp)
        MI.addOperand(MOp1);
      else {
        MI.addOperand(MOps.back());
        MOps.pop_back();
      }
    }
  }
}

// Check if the 'MI' that has the index OpNoForForwarding 
// meets the requirement described in the ImmInstrInfo.
bool PPCInstrInfo::isUseMIElgibleForForwarding(MachineInstr &MI,
                                               const ImmInstrInfo &III,
                                               unsigned OpNoForForwarding
                                               ) const {
  // As the algorithm of checking for PPC::ZERO/PPC::ZERO8
  // would not work pre-RA, we can only do the check post RA.
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  if (MRI.isSSA())
    return false;

  // Cannot do the transform if MI isn't summing the operands.
  if (!III.IsSummingOperands)
    return false;

  // The instruction we are trying to replace must have the ZeroIsSpecialOrig set.
  if (!III.ZeroIsSpecialOrig)
    return false;

  // We cannot do the transform if the operand we are trying to replace
  // isn't the same as the operand the instruction allows.
  if (OpNoForForwarding != III.OpNoForForwarding)
    return false;

  // Check if the instruction we are trying to transform really has
  // the special zero register as its operand.
  if (MI.getOperand(III.ZeroIsSpecialOrig).getReg() != PPC::ZERO &&
      MI.getOperand(III.ZeroIsSpecialOrig).getReg() != PPC::ZERO8)
    return false;

  // This machine instruction is convertible if it is,
  // 1. summing the operands.
  // 2. one of the operands is special zero register.
  // 3. the operand we are trying to replace is allowed by the MI.
  return true;
}

// Check if the DefMI is the add inst and set the ImmMO and RegMO
// accordingly.
bool PPCInstrInfo::isDefMIElgibleForForwarding(MachineInstr &DefMI,
                                               const ImmInstrInfo &III,
                                               MachineOperand *&ImmMO,
                                               MachineOperand *&RegMO) const {
  unsigned Opc = DefMI.getOpcode();
  if (Opc != PPC::ADDItocL && Opc != PPC::ADDI && Opc != PPC::ADDI8)
    return false; 

  assert(DefMI.getNumOperands() >= 3 &&
         "Add inst must have at least three operands");
  RegMO = &DefMI.getOperand(1);
  ImmMO = &DefMI.getOperand(2);

  // This DefMI is elgible for forwarding if it is:
  // 1. add inst
  // 2. one of the operands is Imm/CPI/Global.
  return isAnImmediateOperand(*ImmMO);
}

bool PPCInstrInfo::isRegElgibleForForwarding(const MachineOperand &RegMO,
                                             const MachineInstr &DefMI,
                                             const MachineInstr &MI,
                                             bool KillDefMI
                                             ) const {
  // x = addi y, imm
  // ...
  // z = lfdx 0, x   -> z = lfd imm(y)
  // The Reg "y" can be forwarded to the MI(z) only when there is no DEF
  // of "y" between the DEF of "x" and "z".
  // The query is only valid post RA.
  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  if (MRI.isSSA())
    return false;

  // MachineInstr::readsRegister only returns true if the machine
  // instruction reads the exact register or its super-register. It
  // does not consider uses of sub-registers which seems like strange
  // behaviour. Nonetheless, if we end up with a 64-bit register here,
  // get the corresponding 32-bit register to check.
  unsigned Reg = RegMO.getReg();
  if (PPC::G8RCRegClass.contains(Reg))
    Reg = Reg - PPC::X0 + PPC::R0;

  // Walking the inst in reverse(MI-->DefMI) to get the last DEF of the Reg.
  MachineBasicBlock::const_reverse_iterator It = MI;
  MachineBasicBlock::const_reverse_iterator E = MI.getParent()->rend();
  It++;
  for (; It != E; ++It) {
    if (It->modifiesRegister(Reg, &getRegisterInfo()) && (&*It) != &DefMI)
      return false;
    // Made it to DefMI without encountering a clobber.
    if ((&*It) == &DefMI)
      break;
  }
  assert((&*It) == &DefMI && "DefMI is missing");

  // If DefMI also uses the register to be forwarded, we can only forward it
  // if DefMI is being erased.
  if (DefMI.readsRegister(Reg, &getRegisterInfo()))
    return KillDefMI;

  return true;
}

bool PPCInstrInfo::isImmElgibleForForwarding(const MachineOperand &ImmMO,
                                             const MachineInstr &DefMI,
                                             const ImmInstrInfo &III,
                                             int64_t &Imm) const {
  assert(isAnImmediateOperand(ImmMO) && "ImmMO is NOT an immediate");
  if (DefMI.getOpcode() == PPC::ADDItocL) {
    // The operand for ADDItocL is CPI, which isn't imm at compiling time,
    // However, we know that, it is 16-bit width, and has the alignment of 4.
    // Check if the instruction met the requirement.
    if (III.ImmMustBeMultipleOf > 4 ||
       III.TruncateImmTo || III.ImmWidth != 16)
      return false;

    // Going from XForm to DForm loads means that the displacement needs to be
    // not just an immediate but also a multiple of 4, or 16 depending on the
    // load. A DForm load cannot be represented if it is a multiple of say 2.
    // XForm loads do not have this restriction.
    if (ImmMO.isGlobal() &&
        ImmMO.getGlobal()->getAlignment() < III.ImmMustBeMultipleOf)
      return false;

    return true;
  }

  if (ImmMO.isImm()) {
    // It is Imm, we need to check if the Imm fit the range.
    int64_t Immediate = ImmMO.getImm();
    // Sign-extend to 64-bits.
    Imm = ((uint64_t)Immediate & ~0x7FFFuLL) != 0 ?
      (Immediate | 0xFFFFFFFFFFFF0000) : Immediate;

    if (Imm % III.ImmMustBeMultipleOf)
      return false;
    if (III.TruncateImmTo)
      Imm &= ((1 << III.TruncateImmTo) - 1);
    if (III.SignedImm) {
      APInt ActualValue(64, Imm, true);
      if (!ActualValue.isSignedIntN(III.ImmWidth))
        return false;
    } else {
      uint64_t UnsignedMax = (1 << III.ImmWidth) - 1;
      if ((uint64_t)Imm > UnsignedMax)
        return false;
    }
  }
  else
    return false;

  // This ImmMO is forwarded if it meets the requriement describle
  // in ImmInstrInfo
  return true;
}

// If an X-Form instruction is fed by an add-immediate and one of its operands
// is the literal zero, attempt to forward the source of the add-immediate to
// the corresponding D-Form instruction with the displacement coming from
// the immediate being added.
bool PPCInstrInfo::transformToImmFormFedByAdd(MachineInstr &MI,
                                              const ImmInstrInfo &III,
                                              unsigned OpNoForForwarding,
                                              MachineInstr &DefMI,
                                              bool KillDefMI) const {
  //         RegMO ImmMO
  //           |    |
  // x = addi reg, imm  <----- DefMI
  // y = op    0 ,  x   <----- MI
  //                |
  //         OpNoForForwarding
  // Check if the MI meet the requirement described in the III.
  if (!isUseMIElgibleForForwarding(MI, III, OpNoForForwarding))
    return false;

  // Check if the DefMI meet the requirement
  // described in the III. If yes, set the ImmMO and RegMO accordingly.
  MachineOperand *ImmMO = nullptr;
  MachineOperand *RegMO = nullptr;
  if (!isDefMIElgibleForForwarding(DefMI, III, ImmMO, RegMO))
    return false;
  assert(ImmMO && RegMO && "Imm and Reg operand must have been set");

  // As we get the Imm operand now, we need to check if the ImmMO meet
  // the requirement described in the III. If yes set the Imm.
  int64_t Imm = 0;
  if (!isImmElgibleForForwarding(*ImmMO, DefMI, III, Imm))
    return false;

  // Check if the RegMO can be forwarded to MI.
  if (!isRegElgibleForForwarding(*RegMO, DefMI, MI, KillDefMI))
    return false;

  // We know that, the MI and DefMI both meet the pattern, and
  // the Imm also meet the requirement with the new Imm-form.
  // It is safe to do the transformation now.
  LLVM_DEBUG(dbgs() << "Replacing instruction:\n");
  LLVM_DEBUG(MI.dump());
  LLVM_DEBUG(dbgs() << "Fed by:\n");
  LLVM_DEBUG(DefMI.dump());

  // Update the base reg first.
  MI.getOperand(III.OpNoForForwarding).ChangeToRegister(RegMO->getReg(),
                                                        false, false,
                                                        RegMO->isKill());

  // Then, update the imm.
  if (ImmMO->isImm()) {
    // If the ImmMO is Imm, change the operand that has ZERO to that Imm
    // directly.
    replaceInstrOperandWithImm(MI, III.ZeroIsSpecialOrig, Imm);
  }
  else {
    // Otherwise, it is Constant Pool Index(CPI) or Global,
    // which is relocation in fact. We need to replace the special zero
    // register with ImmMO.
    // Before that, we need to fixup the target flags for imm. 
    // For some reason, we miss to set the flag for the ImmMO if it is CPI.
    if (DefMI.getOpcode() == PPC::ADDItocL)
      ImmMO->setTargetFlags(PPCII::MO_TOC_LO);

    // MI didn't have the interface such as MI.setOperand(i) though
    // it has MI.getOperand(i). To repalce the ZERO MachineOperand with
    // ImmMO, we need to remove ZERO operand and all the operands behind it,
    // and, add the ImmMO, then, move back all the operands behind ZERO.
    SmallVector<MachineOperand, 2> MOps;
    for (unsigned i = MI.getNumOperands() - 1; i >= III.ZeroIsSpecialOrig; i--) {
      MOps.push_back(MI.getOperand(i));
      MI.RemoveOperand(i);
    }

    // Remove the last MO in the list, which is ZERO operand in fact.
    MOps.pop_back();
    // Add the imm operand.
    MI.addOperand(*ImmMO);
    // Now add the rest back.
    for (auto &MO : MOps)
      MI.addOperand(MO);
  }

  // Update the opcode.
  MI.setDesc(get(III.ImmOpcode));

  LLVM_DEBUG(dbgs() << "With:\n");
  LLVM_DEBUG(MI.dump());

  return true;
}

bool PPCInstrInfo::transformToImmFormFedByLI(MachineInstr &MI,
                                             const ImmInstrInfo &III,
                                             unsigned ConstantOpNo,
                                             int64_t Imm) const {
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  bool PostRA = !MRI.isSSA();
  // Exit early if we can't convert this.
  if ((ConstantOpNo != III.OpNoForForwarding) && !III.IsCommutative)
    return false;
  if (Imm % III.ImmMustBeMultipleOf)
    return false;
  if (III.TruncateImmTo)
    Imm &= ((1 << III.TruncateImmTo) - 1);
  if (III.SignedImm) {
    APInt ActualValue(64, Imm, true);
    if (!ActualValue.isSignedIntN(III.ImmWidth))
      return false;
  } else {
    uint64_t UnsignedMax = (1 << III.ImmWidth) - 1;
    if ((uint64_t)Imm > UnsignedMax)
      return false;
  }

  // If we're post-RA, the instructions don't agree on whether register zero is
  // special, we can transform this as long as the register operand that will
  // end up in the location where zero is special isn't R0.
  if (PostRA && III.ZeroIsSpecialOrig != III.ZeroIsSpecialNew) {
    unsigned PosForOrigZero = III.ZeroIsSpecialOrig ? III.ZeroIsSpecialOrig :
      III.ZeroIsSpecialNew + 1;
    unsigned OrigZeroReg = MI.getOperand(PosForOrigZero).getReg();
    unsigned NewZeroReg = MI.getOperand(III.ZeroIsSpecialNew).getReg();
    // If R0 is in the operand where zero is special for the new instruction,
    // it is unsafe to transform if the constant operand isn't that operand.
    if ((NewZeroReg == PPC::R0 || NewZeroReg == PPC::X0) &&
        ConstantOpNo != III.ZeroIsSpecialNew)
      return false;
    if ((OrigZeroReg == PPC::R0 || OrigZeroReg == PPC::X0) &&
        ConstantOpNo != PosForOrigZero)
      return false;
  }

  unsigned Opc = MI.getOpcode();
  bool SpecialShift32 =
    Opc == PPC::SLW || Opc == PPC::SLWo || Opc == PPC::SRW || Opc == PPC::SRWo;
  bool SpecialShift64 =
    Opc == PPC::SLD || Opc == PPC::SLDo || Opc == PPC::SRD || Opc == PPC::SRDo;
  bool SetCR = Opc == PPC::SLWo || Opc == PPC::SRWo ||
    Opc == PPC::SLDo || Opc == PPC::SRDo;
  bool RightShift =
    Opc == PPC::SRW || Opc == PPC::SRWo || Opc == PPC::SRD || Opc == PPC::SRDo;

  MI.setDesc(get(III.ImmOpcode));
  if (ConstantOpNo == III.OpNoForForwarding) {
    // Converting shifts to immediate form is a bit tricky since they may do
    // one of three things:
    // 1. If the shift amount is between OpSize and 2*OpSize, the result is zero
    // 2. If the shift amount is zero, the result is unchanged (save for maybe
    //    setting CR0)
    // 3. If the shift amount is in [1, OpSize), it's just a shift
    if (SpecialShift32 || SpecialShift64) {
      LoadImmediateInfo LII;
      LII.Imm = 0;
      LII.SetCR = SetCR;
      LII.Is64Bit = SpecialShift64;
      uint64_t ShAmt = Imm & (SpecialShift32 ? 0x1F : 0x3F);
      if (Imm & (SpecialShift32 ? 0x20 : 0x40))
        replaceInstrWithLI(MI, LII);
      // Shifts by zero don't change the value. If we don't need to set CR0,
      // just convert this to a COPY. Can't do this post-RA since we've already
      // cleaned up the copies.
      else if (!SetCR && ShAmt == 0 && !PostRA) {
        MI.RemoveOperand(2);
        MI.setDesc(get(PPC::COPY));
      } else {
        // The 32 bit and 64 bit instructions are quite different.
        if (SpecialShift32) {
          // Left shifts use (N, 0, 31-N), right shifts use (32-N, N, 31).
          uint64_t SH = RightShift ? 32 - ShAmt : ShAmt;
          uint64_t MB = RightShift ? ShAmt : 0;
          uint64_t ME = RightShift ? 31 : 31 - ShAmt;
          replaceInstrOperandWithImm(MI, III.OpNoForForwarding, SH);
          MachineInstrBuilder(*MI.getParent()->getParent(), MI).addImm(MB)
            .addImm(ME);
        } else {
          // Left shifts use (N, 63-N), right shifts use (64-N, N).
          uint64_t SH = RightShift ? 64 - ShAmt : ShAmt;
          uint64_t ME = RightShift ? ShAmt : 63 - ShAmt;
          replaceInstrOperandWithImm(MI, III.OpNoForForwarding, SH);
          MachineInstrBuilder(*MI.getParent()->getParent(), MI).addImm(ME);
        }
      }
    } else
      replaceInstrOperandWithImm(MI, ConstantOpNo, Imm);
  }
  // Convert commutative instructions (switch the operands and convert the
  // desired one to an immediate.
  else if (III.IsCommutative) {
    replaceInstrOperandWithImm(MI, ConstantOpNo, Imm);
    swapMIOperands(MI, ConstantOpNo, III.OpNoForForwarding);
  } else
    llvm_unreachable("Should have exited early!");

  // For instructions for which the constant register replaces a different
  // operand than where the immediate goes, we need to swap them.
  if (III.OpNoForForwarding != III.ImmOpNo)
    swapMIOperands(MI, III.OpNoForForwarding, III.ImmOpNo);

  // If the special R0/X0 register index are different for original instruction
  // and new instruction, we need to fix up the register class in new
  // instruction.
  if (!PostRA && III.ZeroIsSpecialOrig != III.ZeroIsSpecialNew) {
    if (III.ZeroIsSpecialNew) {
      // If operand at III.ZeroIsSpecialNew is physical reg(eg: ZERO/ZERO8), no
      // need to fix up register class.
      unsigned RegToModify = MI.getOperand(III.ZeroIsSpecialNew).getReg();
      if (TargetRegisterInfo::isVirtualRegister(RegToModify)) {
        const TargetRegisterClass *NewRC =
          MRI.getRegClass(RegToModify)->hasSuperClassEq(&PPC::GPRCRegClass) ?
          &PPC::GPRC_and_GPRC_NOR0RegClass : &PPC::G8RC_and_G8RC_NOX0RegClass;
        MRI.setRegClass(RegToModify, NewRC);
      }
    }
  }
  return true;
}

const TargetRegisterClass *
PPCInstrInfo::updatedRC(const TargetRegisterClass *RC) const {
  if (Subtarget.hasVSX() && RC == &PPC::VRRCRegClass)
    return &PPC::VSRCRegClass;
  return RC;
}

int PPCInstrInfo::getRecordFormOpcode(unsigned Opcode) {
  return PPC::getRecordFormOpcode(Opcode);
}

// This function returns true if the machine instruction
// always outputs a value by sign-extending a 32 bit value,
// i.e. 0 to 31-th bits are same as 32-th bit.
static bool isSignExtendingOp(const MachineInstr &MI) {
  int Opcode = MI.getOpcode();
  if (Opcode == PPC::LI     || Opcode == PPC::LI8     ||
      Opcode == PPC::LIS    || Opcode == PPC::LIS8    ||
      Opcode == PPC::SRAW   || Opcode == PPC::SRAWo   ||
      Opcode == PPC::SRAWI  || Opcode == PPC::SRAWIo  ||
      Opcode == PPC::LWA    || Opcode == PPC::LWAX    ||
      Opcode == PPC::LWA_32 || Opcode == PPC::LWAX_32 ||
      Opcode == PPC::LHA    || Opcode == PPC::LHAX    ||
      Opcode == PPC::LHA8   || Opcode == PPC::LHAX8   ||
      Opcode == PPC::LBZ    || Opcode == PPC::LBZX    ||
      Opcode == PPC::LBZ8   || Opcode == PPC::LBZX8   ||
      Opcode == PPC::LBZU   || Opcode == PPC::LBZUX   ||
      Opcode == PPC::LBZU8  || Opcode == PPC::LBZUX8  ||
      Opcode == PPC::LHZ    || Opcode == PPC::LHZX    ||
      Opcode == PPC::LHZ8   || Opcode == PPC::LHZX8   ||
      Opcode == PPC::LHZU   || Opcode == PPC::LHZUX   ||
      Opcode == PPC::LHZU8  || Opcode == PPC::LHZUX8  ||
      Opcode == PPC::EXTSB  || Opcode == PPC::EXTSBo  ||
      Opcode == PPC::EXTSH  || Opcode == PPC::EXTSHo  ||
      Opcode == PPC::EXTSB8 || Opcode == PPC::EXTSH8  ||
      Opcode == PPC::EXTSW  || Opcode == PPC::EXTSWo  ||
      Opcode == PPC::SETB   || Opcode == PPC::SETB8   ||
      Opcode == PPC::EXTSH8_32_64 || Opcode == PPC::EXTSW_32_64 ||
      Opcode == PPC::EXTSB8_32_64)
    return true;

  if (Opcode == PPC::RLDICL && MI.getOperand(3).getImm() >= 33)
    return true;

  if ((Opcode == PPC::RLWINM || Opcode == PPC::RLWINMo ||
       Opcode == PPC::RLWNM  || Opcode == PPC::RLWNMo) &&
      MI.getOperand(3).getImm() > 0 &&
      MI.getOperand(3).getImm() <= MI.getOperand(4).getImm())
    return true;

  return false;
}

// This function returns true if the machine instruction
// always outputs zeros in higher 32 bits.
static bool isZeroExtendingOp(const MachineInstr &MI) {
  int Opcode = MI.getOpcode();
  // The 16-bit immediate is sign-extended in li/lis.
  // If the most significant bit is zero, all higher bits are zero.
  if (Opcode == PPC::LI  || Opcode == PPC::LI8 ||
      Opcode == PPC::LIS || Opcode == PPC::LIS8) {
    int64_t Imm = MI.getOperand(1).getImm();
    if (((uint64_t)Imm & ~0x7FFFuLL) == 0)
      return true;
  }

  // We have some variations of rotate-and-mask instructions
  // that clear higher 32-bits.
  if ((Opcode == PPC::RLDICL || Opcode == PPC::RLDICLo ||
       Opcode == PPC::RLDCL  || Opcode == PPC::RLDCLo  ||
       Opcode == PPC::RLDICL_32_64) &&
      MI.getOperand(3).getImm() >= 32)
    return true;

  if ((Opcode == PPC::RLDIC || Opcode == PPC::RLDICo) &&
      MI.getOperand(3).getImm() >= 32 &&
      MI.getOperand(3).getImm() <= 63 - MI.getOperand(2).getImm())
    return true;

  if ((Opcode == PPC::RLWINM  || Opcode == PPC::RLWINMo ||
       Opcode == PPC::RLWNM   || Opcode == PPC::RLWNMo  ||
       Opcode == PPC::RLWINM8 || Opcode == PPC::RLWNM8) &&
      MI.getOperand(3).getImm() <= MI.getOperand(4).getImm())
    return true;

  // There are other instructions that clear higher 32-bits.
  if (Opcode == PPC::CNTLZW  || Opcode == PPC::CNTLZWo ||
      Opcode == PPC::CNTTZW  || Opcode == PPC::CNTTZWo ||
      Opcode == PPC::CNTLZW8 || Opcode == PPC::CNTTZW8 ||
      Opcode == PPC::CNTLZD  || Opcode == PPC::CNTLZDo ||
      Opcode == PPC::CNTTZD  || Opcode == PPC::CNTTZDo ||
      Opcode == PPC::POPCNTD || Opcode == PPC::POPCNTW ||
      Opcode == PPC::SLW     || Opcode == PPC::SLWo    ||
      Opcode == PPC::SRW     || Opcode == PPC::SRWo    ||
      Opcode == PPC::SLW8    || Opcode == PPC::SRW8    ||
      Opcode == PPC::SLWI    || Opcode == PPC::SLWIo   ||
      Opcode == PPC::SRWI    || Opcode == PPC::SRWIo   ||
      Opcode == PPC::LWZ     || Opcode == PPC::LWZX    ||
      Opcode == PPC::LWZU    || Opcode == PPC::LWZUX   ||
      Opcode == PPC::LWBRX   || Opcode == PPC::LHBRX   ||
      Opcode == PPC::LHZ     || Opcode == PPC::LHZX    ||
      Opcode == PPC::LHZU    || Opcode == PPC::LHZUX   ||
      Opcode == PPC::LBZ     || Opcode == PPC::LBZX    ||
      Opcode == PPC::LBZU    || Opcode == PPC::LBZUX   ||
      Opcode == PPC::LWZ8    || Opcode == PPC::LWZX8   ||
      Opcode == PPC::LWZU8   || Opcode == PPC::LWZUX8  ||
      Opcode == PPC::LWBRX8  || Opcode == PPC::LHBRX8  ||
      Opcode == PPC::LHZ8    || Opcode == PPC::LHZX8   ||
      Opcode == PPC::LHZU8   || Opcode == PPC::LHZUX8  ||
      Opcode == PPC::LBZ8    || Opcode == PPC::LBZX8   ||
      Opcode == PPC::LBZU8   || Opcode == PPC::LBZUX8  ||
      Opcode == PPC::ANDIo   || Opcode == PPC::ANDISo  ||
      Opcode == PPC::ROTRWI  || Opcode == PPC::ROTRWIo ||
      Opcode == PPC::EXTLWI  || Opcode == PPC::EXTLWIo ||
      Opcode == PPC::MFVSRWZ)
    return true;

  return false;
}

// This function returns true if the input MachineInstr is a TOC save
// instruction.
bool PPCInstrInfo::isTOCSaveMI(const MachineInstr &MI) const {
  if (!MI.getOperand(1).isImm() || !MI.getOperand(2).isReg())
    return false;
  unsigned TOCSaveOffset = Subtarget.getFrameLowering()->getTOCSaveOffset();
  unsigned StackOffset = MI.getOperand(1).getImm();
  unsigned StackReg = MI.getOperand(2).getReg();
  if (StackReg == PPC::X1 && StackOffset == TOCSaveOffset)
    return true;

  return false;
}

// We limit the max depth to track incoming values of PHIs or binary ops
// (e.g. AND) to avoid excessive cost.
const unsigned MAX_DEPTH = 1;

bool
PPCInstrInfo::isSignOrZeroExtended(const MachineInstr &MI, bool SignExt,
                                   const unsigned Depth) const {
  const MachineFunction *MF = MI.getParent()->getParent();
  const MachineRegisterInfo *MRI = &MF->getRegInfo();

  // If we know this instruction returns sign- or zero-extended result,
  // return true.
  if (SignExt ? isSignExtendingOp(MI):
                isZeroExtendingOp(MI))
    return true;

  switch (MI.getOpcode()) {
  case PPC::COPY: {
    unsigned SrcReg = MI.getOperand(1).getReg();

    // In both ELFv1 and v2 ABI, method parameters and the return value
    // are sign- or zero-extended.
    if (MF->getSubtarget<PPCSubtarget>().isSVR4ABI()) {
      const PPCFunctionInfo *FuncInfo = MF->getInfo<PPCFunctionInfo>();
      // We check the ZExt/SExt flags for a method parameter.
      if (MI.getParent()->getBasicBlock() ==
          &MF->getFunction().getEntryBlock()) {
        unsigned VReg = MI.getOperand(0).getReg();
        if (MF->getRegInfo().isLiveIn(VReg))
          return SignExt ? FuncInfo->isLiveInSExt(VReg) :
                           FuncInfo->isLiveInZExt(VReg);
      }

      // For a method return value, we check the ZExt/SExt flags in attribute.
      // We assume the following code sequence for method call.
      //   ADJCALLSTACKDOWN 32, implicit dead %r1, implicit %r1
      //   BL8_NOP @func,...
      //   ADJCALLSTACKUP 32, 0, implicit dead %r1, implicit %r1
      //   %5 = COPY %x3; G8RC:%5
      if (SrcReg == PPC::X3) {
        const MachineBasicBlock *MBB = MI.getParent();
        MachineBasicBlock::const_instr_iterator II =
          MachineBasicBlock::const_instr_iterator(&MI);
        if (II != MBB->instr_begin() &&
            (--II)->getOpcode() == PPC::ADJCALLSTACKUP) {
          const MachineInstr &CallMI = *(--II);
          if (CallMI.isCall() && CallMI.getOperand(0).isGlobal()) {
            const Function *CalleeFn =
              dyn_cast<Function>(CallMI.getOperand(0).getGlobal());
            if (!CalleeFn)
              return false;
            const IntegerType *IntTy =
              dyn_cast<IntegerType>(CalleeFn->getReturnType());
            const AttributeSet &Attrs =
              CalleeFn->getAttributes().getRetAttributes();
            if (IntTy && IntTy->getBitWidth() <= 32)
              return Attrs.hasAttribute(SignExt ? Attribute::SExt :
                                                  Attribute::ZExt);
          }
        }
      }
    }

    // If this is a copy from another register, we recursively check source.
    if (!TargetRegisterInfo::isVirtualRegister(SrcReg))
      return false;
    const MachineInstr *SrcMI = MRI->getVRegDef(SrcReg);
    if (SrcMI != NULL)
      return isSignOrZeroExtended(*SrcMI, SignExt, Depth);

    return false;
  }

  case PPC::ANDIo:
  case PPC::ANDISo:
  case PPC::ORI:
  case PPC::ORIS:
  case PPC::XORI:
  case PPC::XORIS:
  case PPC::ANDIo8:
  case PPC::ANDISo8:
  case PPC::ORI8:
  case PPC::ORIS8:
  case PPC::XORI8:
  case PPC::XORIS8: {
    // logical operation with 16-bit immediate does not change the upper bits.
    // So, we track the operand register as we do for register copy.
    unsigned SrcReg = MI.getOperand(1).getReg();
    if (!TargetRegisterInfo::isVirtualRegister(SrcReg))
      return false;
    const MachineInstr *SrcMI = MRI->getVRegDef(SrcReg);
    if (SrcMI != NULL)
      return isSignOrZeroExtended(*SrcMI, SignExt, Depth);

    return false;
  }

  // If all incoming values are sign-/zero-extended,
  // the output of OR, ISEL or PHI is also sign-/zero-extended.
  case PPC::OR:
  case PPC::OR8:
  case PPC::ISEL:
  case PPC::PHI: {
    if (Depth >= MAX_DEPTH)
      return false;

    // The input registers for PHI are operand 1, 3, ...
    // The input registers for others are operand 1 and 2.
    unsigned E = 3, D = 1;
    if (MI.getOpcode() == PPC::PHI) {
      E = MI.getNumOperands();
      D = 2;
    }

    for (unsigned I = 1; I != E; I += D) {
      if (MI.getOperand(I).isReg()) {
        unsigned SrcReg = MI.getOperand(I).getReg();
        if (!TargetRegisterInfo::isVirtualRegister(SrcReg))
          return false;
        const MachineInstr *SrcMI = MRI->getVRegDef(SrcReg);
        if (SrcMI == NULL || !isSignOrZeroExtended(*SrcMI, SignExt, Depth+1))
          return false;
      }
      else
        return false;
    }
    return true;
  }

  // If at least one of the incoming values of an AND is zero extended
  // then the output is also zero-extended. If both of the incoming values
  // are sign-extended then the output is also sign extended.
  case PPC::AND:
  case PPC::AND8: {
    if (Depth >= MAX_DEPTH)
       return false;

    assert(MI.getOperand(1).isReg() && MI.getOperand(2).isReg());

    unsigned SrcReg1 = MI.getOperand(1).getReg();
    unsigned SrcReg2 = MI.getOperand(2).getReg();

    if (!TargetRegisterInfo::isVirtualRegister(SrcReg1) ||
        !TargetRegisterInfo::isVirtualRegister(SrcReg2))
       return false;

    const MachineInstr *MISrc1 = MRI->getVRegDef(SrcReg1);
    const MachineInstr *MISrc2 = MRI->getVRegDef(SrcReg2);
    if (!MISrc1 || !MISrc2)
        return false;

    if(SignExt)
        return isSignOrZeroExtended(*MISrc1, SignExt, Depth+1) &&
               isSignOrZeroExtended(*MISrc2, SignExt, Depth+1);
    else
        return isSignOrZeroExtended(*MISrc1, SignExt, Depth+1) ||
               isSignOrZeroExtended(*MISrc2, SignExt, Depth+1);
  }

  default:
    break;
  }
  return false;
}
