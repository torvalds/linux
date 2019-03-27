//===- TwoAddressInstructionPass.cpp - Two-Address instruction pass -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TwoAddress instruction pass which is used
// by most register allocators. Two-Address instructions are rewritten
// from:
//
//     A = B op C
//
// to:
//
//     A = B
//     A op= C
//
// Note that if a register allocator chooses to use this pass, that it
// has to be capable of handling the non-SSA nature of these rewritten
// virtual registers.
//
// It is also worth noting that the duplicate operand of the two
// address instruction is removed.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "twoaddressinstruction"

STATISTIC(NumTwoAddressInstrs, "Number of two-address instructions");
STATISTIC(NumCommuted        , "Number of instructions commuted to coalesce");
STATISTIC(NumAggrCommuted    , "Number of instructions aggressively commuted");
STATISTIC(NumConvertedTo3Addr, "Number of instructions promoted to 3-address");
STATISTIC(Num3AddrSunk,        "Number of 3-address instructions sunk");
STATISTIC(NumReSchedUps,       "Number of instructions re-scheduled up");
STATISTIC(NumReSchedDowns,     "Number of instructions re-scheduled down");

// Temporary flag to disable rescheduling.
static cl::opt<bool>
EnableRescheduling("twoaddr-reschedule",
                   cl::desc("Coalesce copies by rescheduling (default=true)"),
                   cl::init(true), cl::Hidden);

// Limit the number of dataflow edges to traverse when evaluating the benefit
// of commuting operands.
static cl::opt<unsigned> MaxDataFlowEdge(
    "dataflow-edge-limit", cl::Hidden, cl::init(3),
    cl::desc("Maximum number of dataflow edges to traverse when evaluating "
             "the benefit of commuting operands"));

namespace {

class TwoAddressInstructionPass : public MachineFunctionPass {
  MachineFunction *MF;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const InstrItineraryData *InstrItins;
  MachineRegisterInfo *MRI;
  LiveVariables *LV;
  LiveIntervals *LIS;
  AliasAnalysis *AA;
  CodeGenOpt::Level OptLevel;

  // The current basic block being processed.
  MachineBasicBlock *MBB;

  // Keep track the distance of a MI from the start of the current basic block.
  DenseMap<MachineInstr*, unsigned> DistanceMap;

  // Set of already processed instructions in the current block.
  SmallPtrSet<MachineInstr*, 8> Processed;

  // Set of instructions converted to three-address by target and then sunk
  // down current basic block.
  SmallPtrSet<MachineInstr*, 8> SunkInstrs;

  // A map from virtual registers to physical registers which are likely targets
  // to be coalesced to due to copies from physical registers to virtual
  // registers. e.g. v1024 = move r0.
  DenseMap<unsigned, unsigned> SrcRegMap;

  // A map from virtual registers to physical registers which are likely targets
  // to be coalesced to due to copies to physical registers from virtual
  // registers. e.g. r1 = move v1024.
  DenseMap<unsigned, unsigned> DstRegMap;

  bool sink3AddrInstruction(MachineInstr *MI, unsigned Reg,
                            MachineBasicBlock::iterator OldPos);

  bool isRevCopyChain(unsigned FromReg, unsigned ToReg, int Maxlen);

  bool noUseAfterLastDef(unsigned Reg, unsigned Dist, unsigned &LastDef);

  bool isProfitableToCommute(unsigned regA, unsigned regB, unsigned regC,
                             MachineInstr *MI, unsigned Dist);

  bool commuteInstruction(MachineInstr *MI, unsigned DstIdx,
                          unsigned RegBIdx, unsigned RegCIdx, unsigned Dist);

  bool isProfitableToConv3Addr(unsigned RegA, unsigned RegB);

  bool convertInstTo3Addr(MachineBasicBlock::iterator &mi,
                          MachineBasicBlock::iterator &nmi,
                          unsigned RegA, unsigned RegB, unsigned Dist);

  bool isDefTooClose(unsigned Reg, unsigned Dist, MachineInstr *MI);

  bool rescheduleMIBelowKill(MachineBasicBlock::iterator &mi,
                             MachineBasicBlock::iterator &nmi,
                             unsigned Reg);
  bool rescheduleKillAboveMI(MachineBasicBlock::iterator &mi,
                             MachineBasicBlock::iterator &nmi,
                             unsigned Reg);

  bool tryInstructionTransform(MachineBasicBlock::iterator &mi,
                               MachineBasicBlock::iterator &nmi,
                               unsigned SrcIdx, unsigned DstIdx,
                               unsigned Dist, bool shouldOnlyCommute);

  bool tryInstructionCommute(MachineInstr *MI,
                             unsigned DstOpIdx,
                             unsigned BaseOpIdx,
                             bool BaseOpKilled,
                             unsigned Dist);
  void scanUses(unsigned DstReg);

  void processCopy(MachineInstr *MI);

  using TiedPairList = SmallVector<std::pair<unsigned, unsigned>, 4>;
  using TiedOperandMap = SmallDenseMap<unsigned, TiedPairList>;

  bool collectTiedOperands(MachineInstr *MI, TiedOperandMap&);
  void processTiedPairs(MachineInstr *MI, TiedPairList&, unsigned &Dist);
  void eliminateRegSequence(MachineBasicBlock::iterator&);

public:
  static char ID; // Pass identification, replacement for typeid

  TwoAddressInstructionPass() : MachineFunctionPass(ID) {
    initializeTwoAddressInstructionPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addUsedIfAvailable<AAResultsWrapperPass>();
    AU.addUsedIfAvailable<LiveVariables>();
    AU.addPreserved<LiveVariables>();
    AU.addPreserved<SlotIndexes>();
    AU.addPreserved<LiveIntervals>();
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Pass entry point.
  bool runOnMachineFunction(MachineFunction&) override;
};

} // end anonymous namespace

char TwoAddressInstructionPass::ID = 0;

char &llvm::TwoAddressInstructionPassID = TwoAddressInstructionPass::ID;

INITIALIZE_PASS_BEGIN(TwoAddressInstructionPass, DEBUG_TYPE,
                "Two-Address instruction pass", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(TwoAddressInstructionPass, DEBUG_TYPE,
                "Two-Address instruction pass", false, false)

static bool isPlainlyKilled(MachineInstr *MI, unsigned Reg, LiveIntervals *LIS);

/// A two-address instruction has been converted to a three-address instruction
/// to avoid clobbering a register. Try to sink it past the instruction that
/// would kill the above mentioned register to reduce register pressure.
bool TwoAddressInstructionPass::
sink3AddrInstruction(MachineInstr *MI, unsigned SavedReg,
                     MachineBasicBlock::iterator OldPos) {
  // FIXME: Shouldn't we be trying to do this before we three-addressify the
  // instruction?  After this transformation is done, we no longer need
  // the instruction to be in three-address form.

  // Check if it's safe to move this instruction.
  bool SeenStore = true; // Be conservative.
  if (!MI->isSafeToMove(AA, SeenStore))
    return false;

  unsigned DefReg = 0;
  SmallSet<unsigned, 4> UseRegs;

  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg())
      continue;
    unsigned MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MO.isUse() && MOReg != SavedReg)
      UseRegs.insert(MO.getReg());
    if (!MO.isDef())
      continue;
    if (MO.isImplicit())
      // Don't try to move it if it implicitly defines a register.
      return false;
    if (DefReg)
      // For now, don't move any instructions that define multiple registers.
      return false;
    DefReg = MO.getReg();
  }

  // Find the instruction that kills SavedReg.
  MachineInstr *KillMI = nullptr;
  if (LIS) {
    LiveInterval &LI = LIS->getInterval(SavedReg);
    assert(LI.end() != LI.begin() &&
           "Reg should not have empty live interval.");

    SlotIndex MBBEndIdx = LIS->getMBBEndIdx(MBB).getPrevSlot();
    LiveInterval::const_iterator I = LI.find(MBBEndIdx);
    if (I != LI.end() && I->start < MBBEndIdx)
      return false;

    --I;
    KillMI = LIS->getInstructionFromIndex(I->end);
  }
  if (!KillMI) {
    for (MachineOperand &UseMO : MRI->use_nodbg_operands(SavedReg)) {
      if (!UseMO.isKill())
        continue;
      KillMI = UseMO.getParent();
      break;
    }
  }

  // If we find the instruction that kills SavedReg, and it is in an
  // appropriate location, we can try to sink the current instruction
  // past it.
  if (!KillMI || KillMI->getParent() != MBB || KillMI == MI ||
      MachineBasicBlock::iterator(KillMI) == OldPos || KillMI->isTerminator())
    return false;

  // If any of the definitions are used by another instruction between the
  // position and the kill use, then it's not safe to sink it.
  //
  // FIXME: This can be sped up if there is an easy way to query whether an
  // instruction is before or after another instruction. Then we can use
  // MachineRegisterInfo def / use instead.
  MachineOperand *KillMO = nullptr;
  MachineBasicBlock::iterator KillPos = KillMI;
  ++KillPos;

  unsigned NumVisited = 0;
  for (MachineInstr &OtherMI : make_range(std::next(OldPos), KillPos)) {
    // Debug instructions cannot be counted against the limit.
    if (OtherMI.isDebugInstr())
      continue;
    if (NumVisited > 30)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    for (unsigned i = 0, e = OtherMI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = OtherMI.getOperand(i);
      if (!MO.isReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (DefReg == MOReg)
        return false;

      if (MO.isKill() || (LIS && isPlainlyKilled(&OtherMI, MOReg, LIS))) {
        if (&OtherMI == KillMI && MOReg == SavedReg)
          // Save the operand that kills the register. We want to unset the kill
          // marker if we can sink MI past it.
          KillMO = &MO;
        else if (UseRegs.count(MOReg))
          // One of the uses is killed before the destination.
          return false;
      }
    }
  }
  assert(KillMO && "Didn't find kill");

  if (!LIS) {
    // Update kill and LV information.
    KillMO->setIsKill(false);
    KillMO = MI->findRegisterUseOperand(SavedReg, false, TRI);
    KillMO->setIsKill(true);

    if (LV)
      LV->replaceKillInstruction(SavedReg, *KillMI, *MI);
  }

  // Move instruction to its destination.
  MBB->remove(MI);
  MBB->insert(KillPos, MI);

  if (LIS)
    LIS->handleMove(*MI);

  ++Num3AddrSunk;
  return true;
}

/// Return the MachineInstr* if it is the single def of the Reg in current BB.
static MachineInstr *getSingleDef(unsigned Reg, MachineBasicBlock *BB,
                                  const MachineRegisterInfo *MRI) {
  MachineInstr *Ret = nullptr;
  for (MachineInstr &DefMI : MRI->def_instructions(Reg)) {
    if (DefMI.getParent() != BB || DefMI.isDebugValue())
      continue;
    if (!Ret)
      Ret = &DefMI;
    else if (Ret != &DefMI)
      return nullptr;
  }
  return Ret;
}

/// Check if there is a reversed copy chain from FromReg to ToReg:
/// %Tmp1 = copy %Tmp2;
/// %FromReg = copy %Tmp1;
/// %ToReg = add %FromReg ...
/// %Tmp2 = copy %ToReg;
/// MaxLen specifies the maximum length of the copy chain the func
/// can walk through.
bool TwoAddressInstructionPass::isRevCopyChain(unsigned FromReg, unsigned ToReg,
                                               int Maxlen) {
  unsigned TmpReg = FromReg;
  for (int i = 0; i < Maxlen; i++) {
    MachineInstr *Def = getSingleDef(TmpReg, MBB, MRI);
    if (!Def || !Def->isCopy())
      return false;

    TmpReg = Def->getOperand(1).getReg();

    if (TmpReg == ToReg)
      return true;
  }
  return false;
}

/// Return true if there are no intervening uses between the last instruction
/// in the MBB that defines the specified register and the two-address
/// instruction which is being processed. It also returns the last def location
/// by reference.
bool TwoAddressInstructionPass::noUseAfterLastDef(unsigned Reg, unsigned Dist,
                                                  unsigned &LastDef) {
  LastDef = 0;
  unsigned LastUse = Dist;
  for (MachineOperand &MO : MRI->reg_operands(Reg)) {
    MachineInstr *MI = MO.getParent();
    if (MI->getParent() != MBB || MI->isDebugValue())
      continue;
    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
    if (DI == DistanceMap.end())
      continue;
    if (MO.isUse() && DI->second < LastUse)
      LastUse = DI->second;
    if (MO.isDef() && DI->second > LastDef)
      LastDef = DI->second;
  }

  return !(LastUse > LastDef && LastUse < Dist);
}

/// Return true if the specified MI is a copy instruction or an extract_subreg
/// instruction. It also returns the source and destination registers and
/// whether they are physical registers by reference.
static bool isCopyToReg(MachineInstr &MI, const TargetInstrInfo *TII,
                        unsigned &SrcReg, unsigned &DstReg,
                        bool &IsSrcPhys, bool &IsDstPhys) {
  SrcReg = 0;
  DstReg = 0;
  if (MI.isCopy()) {
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();
  } else if (MI.isInsertSubreg() || MI.isSubregToReg()) {
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(2).getReg();
  } else
    return false;

  IsSrcPhys = TargetRegisterInfo::isPhysicalRegister(SrcReg);
  IsDstPhys = TargetRegisterInfo::isPhysicalRegister(DstReg);
  return true;
}

/// Test if the given register value, which is used by the
/// given instruction, is killed by the given instruction.
static bool isPlainlyKilled(MachineInstr *MI, unsigned Reg,
                            LiveIntervals *LIS) {
  if (LIS && TargetRegisterInfo::isVirtualRegister(Reg) &&
      !LIS->isNotInMIMap(*MI)) {
    // FIXME: Sometimes tryInstructionTransform() will add instructions and
    // test whether they can be folded before keeping them. In this case it
    // sets a kill before recursively calling tryInstructionTransform() again.
    // If there is no interval available, we assume that this instruction is
    // one of those. A kill flag is manually inserted on the operand so the
    // check below will handle it.
    LiveInterval &LI = LIS->getInterval(Reg);
    // This is to match the kill flag version where undefs don't have kill
    // flags.
    if (!LI.hasAtLeastOneValue())
      return false;

    SlotIndex useIdx = LIS->getInstructionIndex(*MI);
    LiveInterval::const_iterator I = LI.find(useIdx);
    assert(I != LI.end() && "Reg must be live-in to use.");
    return !I->end.isBlock() && SlotIndex::isSameInstr(I->end, useIdx);
  }

  return MI->killsRegister(Reg);
}

/// Test if the given register value, which is used by the given
/// instruction, is killed by the given instruction. This looks through
/// coalescable copies to see if the original value is potentially not killed.
///
/// For example, in this code:
///
///   %reg1034 = copy %reg1024
///   %reg1035 = copy killed %reg1025
///   %reg1036 = add killed %reg1034, killed %reg1035
///
/// %reg1034 is not considered to be killed, since it is copied from a
/// register which is not killed. Treating it as not killed lets the
/// normal heuristics commute the (two-address) add, which lets
/// coalescing eliminate the extra copy.
///
/// If allowFalsePositives is true then likely kills are treated as kills even
/// if it can't be proven that they are kills.
static bool isKilled(MachineInstr &MI, unsigned Reg,
                     const MachineRegisterInfo *MRI,
                     const TargetInstrInfo *TII,
                     LiveIntervals *LIS,
                     bool allowFalsePositives) {
  MachineInstr *DefMI = &MI;
  while (true) {
    // All uses of physical registers are likely to be kills.
    if (TargetRegisterInfo::isPhysicalRegister(Reg) &&
        (allowFalsePositives || MRI->hasOneUse(Reg)))
      return true;
    if (!isPlainlyKilled(DefMI, Reg, LIS))
      return false;
    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      return true;
    MachineRegisterInfo::def_iterator Begin = MRI->def_begin(Reg);
    // If there are multiple defs, we can't do a simple analysis, so just
    // go with what the kill flag says.
    if (std::next(Begin) != MRI->def_end())
      return true;
    DefMI = Begin->getParent();
    bool IsSrcPhys, IsDstPhys;
    unsigned SrcReg,  DstReg;
    // If the def is something other than a copy, then it isn't going to
    // be coalesced, so follow the kill flag.
    if (!isCopyToReg(*DefMI, TII, SrcReg, DstReg, IsSrcPhys, IsDstPhys))
      return true;
    Reg = SrcReg;
  }
}

/// Return true if the specified MI uses the specified register as a two-address
/// use. If so, return the destination register by reference.
static bool isTwoAddrUse(MachineInstr &MI, unsigned Reg, unsigned &DstReg) {
  for (unsigned i = 0, NumOps = MI.getNumOperands(); i != NumOps; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isUse() || MO.getReg() != Reg)
      continue;
    unsigned ti;
    if (MI.isRegTiedToDefOperand(i, &ti)) {
      DstReg = MI.getOperand(ti).getReg();
      return true;
    }
  }
  return false;
}

/// Given a register, if has a single in-basic block use, return the use
/// instruction if it's a copy or a two-address use.
static
MachineInstr *findOnlyInterestingUse(unsigned Reg, MachineBasicBlock *MBB,
                                     MachineRegisterInfo *MRI,
                                     const TargetInstrInfo *TII,
                                     bool &IsCopy,
                                     unsigned &DstReg, bool &IsDstPhys) {
  if (!MRI->hasOneNonDBGUse(Reg))
    // None or more than one use.
    return nullptr;
  MachineInstr &UseMI = *MRI->use_instr_nodbg_begin(Reg);
  if (UseMI.getParent() != MBB)
    return nullptr;
  unsigned SrcReg;
  bool IsSrcPhys;
  if (isCopyToReg(UseMI, TII, SrcReg, DstReg, IsSrcPhys, IsDstPhys)) {
    IsCopy = true;
    return &UseMI;
  }
  IsDstPhys = false;
  if (isTwoAddrUse(UseMI, Reg, DstReg)) {
    IsDstPhys = TargetRegisterInfo::isPhysicalRegister(DstReg);
    return &UseMI;
  }
  return nullptr;
}

/// Return the physical register the specified virtual register might be mapped
/// to.
static unsigned
getMappedReg(unsigned Reg, DenseMap<unsigned, unsigned> &RegMap) {
  while (TargetRegisterInfo::isVirtualRegister(Reg))  {
    DenseMap<unsigned, unsigned>::iterator SI = RegMap.find(Reg);
    if (SI == RegMap.end())
      return 0;
    Reg = SI->second;
  }
  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return Reg;
  return 0;
}

/// Return true if the two registers are equal or aliased.
static bool
regsAreCompatible(unsigned RegA, unsigned RegB, const TargetRegisterInfo *TRI) {
  if (RegA == RegB)
    return true;
  if (!RegA || !RegB)
    return false;
  return TRI->regsOverlap(RegA, RegB);
}

// Returns true if Reg is equal or aliased to at least one register in Set.
static bool regOverlapsSet(const SmallVectorImpl<unsigned> &Set, unsigned Reg,
                           const TargetRegisterInfo *TRI) {
  for (unsigned R : Set)
    if (TRI->regsOverlap(R, Reg))
      return true;

  return false;
}

/// Return true if it's potentially profitable to commute the two-address
/// instruction that's being processed.
bool
TwoAddressInstructionPass::
isProfitableToCommute(unsigned regA, unsigned regB, unsigned regC,
                      MachineInstr *MI, unsigned Dist) {
  if (OptLevel == CodeGenOpt::None)
    return false;

  // Determine if it's profitable to commute this two address instruction. In
  // general, we want no uses between this instruction and the definition of
  // the two-address register.
  // e.g.
  // %reg1028 = EXTRACT_SUBREG killed %reg1027, 1
  // %reg1029 = COPY %reg1028
  // %reg1029 = SHR8ri %reg1029, 7, implicit dead %eflags
  // insert => %reg1030 = COPY %reg1028
  // %reg1030 = ADD8rr killed %reg1028, killed %reg1029, implicit dead %eflags
  // In this case, it might not be possible to coalesce the second COPY
  // instruction if the first one is coalesced. So it would be profitable to
  // commute it:
  // %reg1028 = EXTRACT_SUBREG killed %reg1027, 1
  // %reg1029 = COPY %reg1028
  // %reg1029 = SHR8ri %reg1029, 7, implicit dead %eflags
  // insert => %reg1030 = COPY %reg1029
  // %reg1030 = ADD8rr killed %reg1029, killed %reg1028, implicit dead %eflags

  if (!isPlainlyKilled(MI, regC, LIS))
    return false;

  // Ok, we have something like:
  // %reg1030 = ADD8rr killed %reg1028, killed %reg1029, implicit dead %eflags
  // let's see if it's worth commuting it.

  // Look for situations like this:
  // %reg1024 = MOV r1
  // %reg1025 = MOV r0
  // %reg1026 = ADD %reg1024, %reg1025
  // r0            = MOV %reg1026
  // Commute the ADD to hopefully eliminate an otherwise unavoidable copy.
  unsigned ToRegA = getMappedReg(regA, DstRegMap);
  if (ToRegA) {
    unsigned FromRegB = getMappedReg(regB, SrcRegMap);
    unsigned FromRegC = getMappedReg(regC, SrcRegMap);
    bool CompB = FromRegB && regsAreCompatible(FromRegB, ToRegA, TRI);
    bool CompC = FromRegC && regsAreCompatible(FromRegC, ToRegA, TRI);

    // Compute if any of the following are true:
    // -RegB is not tied to a register and RegC is compatible with RegA.
    // -RegB is tied to the wrong physical register, but RegC is.
    // -RegB is tied to the wrong physical register, and RegC isn't tied.
    if ((!FromRegB && CompC) || (FromRegB && !CompB && (!FromRegC || CompC)))
      return true;
    // Don't compute if any of the following are true:
    // -RegC is not tied to a register and RegB is compatible with RegA.
    // -RegC is tied to the wrong physical register, but RegB is.
    // -RegC is tied to the wrong physical register, and RegB isn't tied.
    if ((!FromRegC && CompB) || (FromRegC && !CompC && (!FromRegB || CompB)))
      return false;
  }

  // If there is a use of regC between its last def (could be livein) and this
  // instruction, then bail.
  unsigned LastDefC = 0;
  if (!noUseAfterLastDef(regC, Dist, LastDefC))
    return false;

  // If there is a use of regB between its last def (could be livein) and this
  // instruction, then go ahead and make this transformation.
  unsigned LastDefB = 0;
  if (!noUseAfterLastDef(regB, Dist, LastDefB))
    return true;

  // Look for situation like this:
  // %reg101 = MOV %reg100
  // %reg102 = ...
  // %reg103 = ADD %reg102, %reg101
  // ... = %reg103 ...
  // %reg100 = MOV %reg103
  // If there is a reversed copy chain from reg101 to reg103, commute the ADD
  // to eliminate an otherwise unavoidable copy.
  // FIXME:
  // We can extend the logic further: If an pair of operands in an insn has
  // been merged, the insn could be regarded as a virtual copy, and the virtual
  // copy could also be used to construct a copy chain.
  // To more generally minimize register copies, ideally the logic of two addr
  // instruction pass should be integrated with register allocation pass where
  // interference graph is available.
  if (isRevCopyChain(regC, regA, MaxDataFlowEdge))
    return true;

  if (isRevCopyChain(regB, regA, MaxDataFlowEdge))
    return false;

  // Since there are no intervening uses for both registers, then commute
  // if the def of regC is closer. Its live interval is shorter.
  return LastDefB && LastDefC && LastDefC > LastDefB;
}

/// Commute a two-address instruction and update the basic block, distance map,
/// and live variables if needed. Return true if it is successful.
bool TwoAddressInstructionPass::commuteInstruction(MachineInstr *MI,
                                                   unsigned DstIdx,
                                                   unsigned RegBIdx,
                                                   unsigned RegCIdx,
                                                   unsigned Dist) {
  unsigned RegC = MI->getOperand(RegCIdx).getReg();
  LLVM_DEBUG(dbgs() << "2addr: COMMUTING  : " << *MI);
  MachineInstr *NewMI = TII->commuteInstruction(*MI, false, RegBIdx, RegCIdx);

  if (NewMI == nullptr) {
    LLVM_DEBUG(dbgs() << "2addr: COMMUTING FAILED!\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "2addr: COMMUTED TO: " << *NewMI);
  assert(NewMI == MI &&
         "TargetInstrInfo::commuteInstruction() should not return a new "
         "instruction unless it was requested.");

  // Update source register map.
  unsigned FromRegC = getMappedReg(RegC, SrcRegMap);
  if (FromRegC) {
    unsigned RegA = MI->getOperand(DstIdx).getReg();
    SrcRegMap[RegA] = FromRegC;
  }

  return true;
}

/// Return true if it is profitable to convert the given 2-address instruction
/// to a 3-address one.
bool
TwoAddressInstructionPass::isProfitableToConv3Addr(unsigned RegA,unsigned RegB){
  // Look for situations like this:
  // %reg1024 = MOV r1
  // %reg1025 = MOV r0
  // %reg1026 = ADD %reg1024, %reg1025
  // r2            = MOV %reg1026
  // Turn ADD into a 3-address instruction to avoid a copy.
  unsigned FromRegB = getMappedReg(RegB, SrcRegMap);
  if (!FromRegB)
    return false;
  unsigned ToRegA = getMappedReg(RegA, DstRegMap);
  return (ToRegA && !regsAreCompatible(FromRegB, ToRegA, TRI));
}

/// Convert the specified two-address instruction into a three address one.
/// Return true if this transformation was successful.
bool
TwoAddressInstructionPass::convertInstTo3Addr(MachineBasicBlock::iterator &mi,
                                              MachineBasicBlock::iterator &nmi,
                                              unsigned RegA, unsigned RegB,
                                              unsigned Dist) {
  // FIXME: Why does convertToThreeAddress() need an iterator reference?
  MachineFunction::iterator MFI = MBB->getIterator();
  MachineInstr *NewMI = TII->convertToThreeAddress(MFI, *mi, LV);
  assert(MBB->getIterator() == MFI &&
         "convertToThreeAddress changed iterator reference");
  if (!NewMI)
    return false;

  LLVM_DEBUG(dbgs() << "2addr: CONVERTING 2-ADDR: " << *mi);
  LLVM_DEBUG(dbgs() << "2addr:         TO 3-ADDR: " << *NewMI);
  bool Sunk = false;

  if (LIS)
    LIS->ReplaceMachineInstrInMaps(*mi, *NewMI);

  if (NewMI->findRegisterUseOperand(RegB, false, TRI))
    // FIXME: Temporary workaround. If the new instruction doesn't
    // uses RegB, convertToThreeAddress must have created more
    // then one instruction.
    Sunk = sink3AddrInstruction(NewMI, RegB, mi);

  MBB->erase(mi); // Nuke the old inst.

  if (!Sunk) {
    DistanceMap.insert(std::make_pair(NewMI, Dist));
    mi = NewMI;
    nmi = std::next(mi);
  }
  else
    SunkInstrs.insert(NewMI);

  // Update source and destination register maps.
  SrcRegMap.erase(RegA);
  DstRegMap.erase(RegB);
  return true;
}

/// Scan forward recursively for only uses, update maps if the use is a copy or
/// a two-address instruction.
void
TwoAddressInstructionPass::scanUses(unsigned DstReg) {
  SmallVector<unsigned, 4> VirtRegPairs;
  bool IsDstPhys;
  bool IsCopy = false;
  unsigned NewReg = 0;
  unsigned Reg = DstReg;
  while (MachineInstr *UseMI = findOnlyInterestingUse(Reg, MBB, MRI, TII,IsCopy,
                                                      NewReg, IsDstPhys)) {
    if (IsCopy && !Processed.insert(UseMI).second)
      break;

    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(UseMI);
    if (DI != DistanceMap.end())
      // Earlier in the same MBB.Reached via a back edge.
      break;

    if (IsDstPhys) {
      VirtRegPairs.push_back(NewReg);
      break;
    }
    bool isNew = SrcRegMap.insert(std::make_pair(NewReg, Reg)).second;
    if (!isNew)
      assert(SrcRegMap[NewReg] == Reg && "Can't map to two src registers!");
    VirtRegPairs.push_back(NewReg);
    Reg = NewReg;
  }

  if (!VirtRegPairs.empty()) {
    unsigned ToReg = VirtRegPairs.back();
    VirtRegPairs.pop_back();
    while (!VirtRegPairs.empty()) {
      unsigned FromReg = VirtRegPairs.back();
      VirtRegPairs.pop_back();
      bool isNew = DstRegMap.insert(std::make_pair(FromReg, ToReg)).second;
      if (!isNew)
        assert(DstRegMap[FromReg] == ToReg &&"Can't map to two dst registers!");
      ToReg = FromReg;
    }
    bool isNew = DstRegMap.insert(std::make_pair(DstReg, ToReg)).second;
    if (!isNew)
      assert(DstRegMap[DstReg] == ToReg && "Can't map to two dst registers!");
  }
}

/// If the specified instruction is not yet processed, process it if it's a
/// copy. For a copy instruction, we find the physical registers the
/// source and destination registers might be mapped to. These are kept in
/// point-to maps used to determine future optimizations. e.g.
/// v1024 = mov r0
/// v1025 = mov r1
/// v1026 = add v1024, v1025
/// r1    = mov r1026
/// If 'add' is a two-address instruction, v1024, v1026 are both potentially
/// coalesced to r0 (from the input side). v1025 is mapped to r1. v1026 is
/// potentially joined with r1 on the output side. It's worthwhile to commute
/// 'add' to eliminate a copy.
void TwoAddressInstructionPass::processCopy(MachineInstr *MI) {
  if (Processed.count(MI))
    return;

  bool IsSrcPhys, IsDstPhys;
  unsigned SrcReg, DstReg;
  if (!isCopyToReg(*MI, TII, SrcReg, DstReg, IsSrcPhys, IsDstPhys))
    return;

  if (IsDstPhys && !IsSrcPhys)
    DstRegMap.insert(std::make_pair(SrcReg, DstReg));
  else if (!IsDstPhys && IsSrcPhys) {
    bool isNew = SrcRegMap.insert(std::make_pair(DstReg, SrcReg)).second;
    if (!isNew)
      assert(SrcRegMap[DstReg] == SrcReg &&
             "Can't map to two src physical registers!");

    scanUses(DstReg);
  }

  Processed.insert(MI);
}

/// If there is one more local instruction that reads 'Reg' and it kills 'Reg,
/// consider moving the instruction below the kill instruction in order to
/// eliminate the need for the copy.
bool TwoAddressInstructionPass::
rescheduleMIBelowKill(MachineBasicBlock::iterator &mi,
                      MachineBasicBlock::iterator &nmi,
                      unsigned Reg) {
  // Bail immediately if we don't have LV or LIS available. We use them to find
  // kills efficiently.
  if (!LV && !LIS)
    return false;

  MachineInstr *MI = &*mi;
  DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
  if (DI == DistanceMap.end())
    // Must be created from unfolded load. Don't waste time trying this.
    return false;

  MachineInstr *KillMI = nullptr;
  if (LIS) {
    LiveInterval &LI = LIS->getInterval(Reg);
    assert(LI.end() != LI.begin() &&
           "Reg should not have empty live interval.");

    SlotIndex MBBEndIdx = LIS->getMBBEndIdx(MBB).getPrevSlot();
    LiveInterval::const_iterator I = LI.find(MBBEndIdx);
    if (I != LI.end() && I->start < MBBEndIdx)
      return false;

    --I;
    KillMI = LIS->getInstructionFromIndex(I->end);
  } else {
    KillMI = LV->getVarInfo(Reg).findKill(MBB);
  }
  if (!KillMI || MI == KillMI || KillMI->isCopy() || KillMI->isCopyLike())
    // Don't mess with copies, they may be coalesced later.
    return false;

  if (KillMI->hasUnmodeledSideEffects() || KillMI->isCall() ||
      KillMI->isBranch() || KillMI->isTerminator())
    // Don't move pass calls, etc.
    return false;

  unsigned DstReg;
  if (isTwoAddrUse(*KillMI, Reg, DstReg))
    return false;

  bool SeenStore = true;
  if (!MI->isSafeToMove(AA, SeenStore))
    return false;

  if (TII->getInstrLatency(InstrItins, *MI) > 1)
    // FIXME: Needs more sophisticated heuristics.
    return false;

  SmallVector<unsigned, 2> Uses;
  SmallVector<unsigned, 2> Kills;
  SmallVector<unsigned, 2> Defs;
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg())
      continue;
    unsigned MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MO.isDef())
      Defs.push_back(MOReg);
    else {
      Uses.push_back(MOReg);
      if (MOReg != Reg && (MO.isKill() ||
                           (LIS && isPlainlyKilled(MI, MOReg, LIS))))
        Kills.push_back(MOReg);
    }
  }

  // Move the copies connected to MI down as well.
  MachineBasicBlock::iterator Begin = MI;
  MachineBasicBlock::iterator AfterMI = std::next(Begin);
  MachineBasicBlock::iterator End = AfterMI;
  while (End != MBB->end()) {
    End = skipDebugInstructionsForward(End, MBB->end());
    if (End->isCopy() && regOverlapsSet(Defs, End->getOperand(1).getReg(), TRI))
      Defs.push_back(End->getOperand(0).getReg());
    else
      break;
    ++End;
  }

  // Check if the reschedule will not break dependencies.
  unsigned NumVisited = 0;
  MachineBasicBlock::iterator KillPos = KillMI;
  ++KillPos;
  for (MachineInstr &OtherMI : make_range(End, KillPos)) {
    // Debug instructions cannot be counted against the limit.
    if (OtherMI.isDebugInstr())
      continue;
    if (NumVisited > 10)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    if (OtherMI.hasUnmodeledSideEffects() || OtherMI.isCall() ||
        OtherMI.isBranch() || OtherMI.isTerminator())
      // Don't move pass calls, etc.
      return false;
    for (const MachineOperand &MO : OtherMI.operands()) {
      if (!MO.isReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (MO.isDef()) {
        if (regOverlapsSet(Uses, MOReg, TRI))
          // Physical register use would be clobbered.
          return false;
        if (!MO.isDead() && regOverlapsSet(Defs, MOReg, TRI))
          // May clobber a physical register def.
          // FIXME: This may be too conservative. It's ok if the instruction
          // is sunken completely below the use.
          return false;
      } else {
        if (regOverlapsSet(Defs, MOReg, TRI))
          return false;
        bool isKill =
            MO.isKill() || (LIS && isPlainlyKilled(&OtherMI, MOReg, LIS));
        if (MOReg != Reg && ((isKill && regOverlapsSet(Uses, MOReg, TRI)) ||
                             regOverlapsSet(Kills, MOReg, TRI)))
          // Don't want to extend other live ranges and update kills.
          return false;
        if (MOReg == Reg && !isKill)
          // We can't schedule across a use of the register in question.
          return false;
        // Ensure that if this is register in question, its the kill we expect.
        assert((MOReg != Reg || &OtherMI == KillMI) &&
               "Found multiple kills of a register in a basic block");
      }
    }
  }

  // Move debug info as well.
  while (Begin != MBB->begin() && std::prev(Begin)->isDebugInstr())
    --Begin;

  nmi = End;
  MachineBasicBlock::iterator InsertPos = KillPos;
  if (LIS) {
    // We have to move the copies first so that the MBB is still well-formed
    // when calling handleMove().
    for (MachineBasicBlock::iterator MBBI = AfterMI; MBBI != End;) {
      auto CopyMI = MBBI++;
      MBB->splice(InsertPos, MBB, CopyMI);
      LIS->handleMove(*CopyMI);
      InsertPos = CopyMI;
    }
    End = std::next(MachineBasicBlock::iterator(MI));
  }

  // Copies following MI may have been moved as well.
  MBB->splice(InsertPos, MBB, Begin, End);
  DistanceMap.erase(DI);

  // Update live variables
  if (LIS) {
    LIS->handleMove(*MI);
  } else {
    LV->removeVirtualRegisterKilled(Reg, *KillMI);
    LV->addVirtualRegisterKilled(Reg, *MI);
  }

  LLVM_DEBUG(dbgs() << "\trescheduled below kill: " << *KillMI);
  return true;
}

/// Return true if the re-scheduling will put the given instruction too close
/// to the defs of its register dependencies.
bool TwoAddressInstructionPass::isDefTooClose(unsigned Reg, unsigned Dist,
                                              MachineInstr *MI) {
  for (MachineInstr &DefMI : MRI->def_instructions(Reg)) {
    if (DefMI.getParent() != MBB || DefMI.isCopy() || DefMI.isCopyLike())
      continue;
    if (&DefMI == MI)
      return true; // MI is defining something KillMI uses
    DenseMap<MachineInstr*, unsigned>::iterator DDI = DistanceMap.find(&DefMI);
    if (DDI == DistanceMap.end())
      return true;  // Below MI
    unsigned DefDist = DDI->second;
    assert(Dist > DefDist && "Visited def already?");
    if (TII->getInstrLatency(InstrItins, DefMI) > (Dist - DefDist))
      return true;
  }
  return false;
}

/// If there is one more local instruction that reads 'Reg' and it kills 'Reg,
/// consider moving the kill instruction above the current two-address
/// instruction in order to eliminate the need for the copy.
bool TwoAddressInstructionPass::
rescheduleKillAboveMI(MachineBasicBlock::iterator &mi,
                      MachineBasicBlock::iterator &nmi,
                      unsigned Reg) {
  // Bail immediately if we don't have LV or LIS available. We use them to find
  // kills efficiently.
  if (!LV && !LIS)
    return false;

  MachineInstr *MI = &*mi;
  DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
  if (DI == DistanceMap.end())
    // Must be created from unfolded load. Don't waste time trying this.
    return false;

  MachineInstr *KillMI = nullptr;
  if (LIS) {
    LiveInterval &LI = LIS->getInterval(Reg);
    assert(LI.end() != LI.begin() &&
           "Reg should not have empty live interval.");

    SlotIndex MBBEndIdx = LIS->getMBBEndIdx(MBB).getPrevSlot();
    LiveInterval::const_iterator I = LI.find(MBBEndIdx);
    if (I != LI.end() && I->start < MBBEndIdx)
      return false;

    --I;
    KillMI = LIS->getInstructionFromIndex(I->end);
  } else {
    KillMI = LV->getVarInfo(Reg).findKill(MBB);
  }
  if (!KillMI || MI == KillMI || KillMI->isCopy() || KillMI->isCopyLike())
    // Don't mess with copies, they may be coalesced later.
    return false;

  unsigned DstReg;
  if (isTwoAddrUse(*KillMI, Reg, DstReg))
    return false;

  bool SeenStore = true;
  if (!KillMI->isSafeToMove(AA, SeenStore))
    return false;

  SmallSet<unsigned, 2> Uses;
  SmallSet<unsigned, 2> Kills;
  SmallSet<unsigned, 2> Defs;
  SmallSet<unsigned, 2> LiveDefs;
  for (const MachineOperand &MO : KillMI->operands()) {
    if (!MO.isReg())
      continue;
    unsigned MOReg = MO.getReg();
    if (MO.isUse()) {
      if (!MOReg)
        continue;
      if (isDefTooClose(MOReg, DI->second, MI))
        return false;
      bool isKill = MO.isKill() || (LIS && isPlainlyKilled(KillMI, MOReg, LIS));
      if (MOReg == Reg && !isKill)
        return false;
      Uses.insert(MOReg);
      if (isKill && MOReg != Reg)
        Kills.insert(MOReg);
    } else if (TargetRegisterInfo::isPhysicalRegister(MOReg)) {
      Defs.insert(MOReg);
      if (!MO.isDead())
        LiveDefs.insert(MOReg);
    }
  }

  // Check if the reschedule will not break depedencies.
  unsigned NumVisited = 0;
  for (MachineInstr &OtherMI :
       make_range(mi, MachineBasicBlock::iterator(KillMI))) {
    // Debug instructions cannot be counted against the limit.
    if (OtherMI.isDebugInstr())
      continue;
    if (NumVisited > 10)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    if (OtherMI.hasUnmodeledSideEffects() || OtherMI.isCall() ||
        OtherMI.isBranch() || OtherMI.isTerminator())
      // Don't move pass calls, etc.
      return false;
    SmallVector<unsigned, 2> OtherDefs;
    for (const MachineOperand &MO : OtherMI.operands()) {
      if (!MO.isReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (MO.isUse()) {
        if (Defs.count(MOReg))
          // Moving KillMI can clobber the physical register if the def has
          // not been seen.
          return false;
        if (Kills.count(MOReg))
          // Don't want to extend other live ranges and update kills.
          return false;
        if (&OtherMI != MI && MOReg == Reg &&
            !(MO.isKill() || (LIS && isPlainlyKilled(&OtherMI, MOReg, LIS))))
          // We can't schedule across a use of the register in question.
          return false;
      } else {
        OtherDefs.push_back(MOReg);
      }
    }

    for (unsigned i = 0, e = OtherDefs.size(); i != e; ++i) {
      unsigned MOReg = OtherDefs[i];
      if (Uses.count(MOReg))
        return false;
      if (TargetRegisterInfo::isPhysicalRegister(MOReg) &&
          LiveDefs.count(MOReg))
        return false;
      // Physical register def is seen.
      Defs.erase(MOReg);
    }
  }

  // Move the old kill above MI, don't forget to move debug info as well.
  MachineBasicBlock::iterator InsertPos = mi;
  while (InsertPos != MBB->begin() && std::prev(InsertPos)->isDebugInstr())
    --InsertPos;
  MachineBasicBlock::iterator From = KillMI;
  MachineBasicBlock::iterator To = std::next(From);
  while (std::prev(From)->isDebugInstr())
    --From;
  MBB->splice(InsertPos, MBB, From, To);

  nmi = std::prev(InsertPos); // Backtrack so we process the moved instr.
  DistanceMap.erase(DI);

  // Update live variables
  if (LIS) {
    LIS->handleMove(*KillMI);
  } else {
    LV->removeVirtualRegisterKilled(Reg, *KillMI);
    LV->addVirtualRegisterKilled(Reg, *MI);
  }

  LLVM_DEBUG(dbgs() << "\trescheduled kill: " << *KillMI);
  return true;
}

/// Tries to commute the operand 'BaseOpIdx' and some other operand in the
/// given machine instruction to improve opportunities for coalescing and
/// elimination of a register to register copy.
///
/// 'DstOpIdx' specifies the index of MI def operand.
/// 'BaseOpKilled' specifies if the register associated with 'BaseOpIdx'
/// operand is killed by the given instruction.
/// The 'Dist' arguments provides the distance of MI from the start of the
/// current basic block and it is used to determine if it is profitable
/// to commute operands in the instruction.
///
/// Returns true if the transformation happened. Otherwise, returns false.
bool TwoAddressInstructionPass::tryInstructionCommute(MachineInstr *MI,
                                                      unsigned DstOpIdx,
                                                      unsigned BaseOpIdx,
                                                      bool BaseOpKilled,
                                                      unsigned Dist) {
  if (!MI->isCommutable())
    return false;

  bool MadeChange = false;
  unsigned DstOpReg = MI->getOperand(DstOpIdx).getReg();
  unsigned BaseOpReg = MI->getOperand(BaseOpIdx).getReg();
  unsigned OpsNum = MI->getDesc().getNumOperands();
  unsigned OtherOpIdx = MI->getDesc().getNumDefs();
  for (; OtherOpIdx < OpsNum; OtherOpIdx++) {
    // The call of findCommutedOpIndices below only checks if BaseOpIdx
    // and OtherOpIdx are commutable, it does not really search for
    // other commutable operands and does not change the values of passed
    // variables.
    if (OtherOpIdx == BaseOpIdx || !MI->getOperand(OtherOpIdx).isReg() ||
        !TII->findCommutedOpIndices(*MI, BaseOpIdx, OtherOpIdx))
      continue;

    unsigned OtherOpReg = MI->getOperand(OtherOpIdx).getReg();
    bool AggressiveCommute = false;

    // If OtherOp dies but BaseOp does not, swap the OtherOp and BaseOp
    // operands. This makes the live ranges of DstOp and OtherOp joinable.
    bool OtherOpKilled = isKilled(*MI, OtherOpReg, MRI, TII, LIS, false);
    bool DoCommute = !BaseOpKilled && OtherOpKilled;

    if (!DoCommute &&
        isProfitableToCommute(DstOpReg, BaseOpReg, OtherOpReg, MI, Dist)) {
      DoCommute = true;
      AggressiveCommute = true;
    }

    // If it's profitable to commute, try to do so.
    if (DoCommute && commuteInstruction(MI, DstOpIdx, BaseOpIdx, OtherOpIdx,
                                        Dist)) {
      MadeChange = true;
      ++NumCommuted;
      if (AggressiveCommute) {
        ++NumAggrCommuted;
        // There might be more than two commutable operands, update BaseOp and
        // continue scanning.
        BaseOpReg = OtherOpReg;
        BaseOpKilled = OtherOpKilled;
        continue;
      }
      // If this was a commute based on kill, we won't do better continuing.
      return MadeChange;
    }
  }
  return MadeChange;
}

/// For the case where an instruction has a single pair of tied register
/// operands, attempt some transformations that may either eliminate the tied
/// operands or improve the opportunities for coalescing away the register copy.
/// Returns true if no copy needs to be inserted to untie mi's operands
/// (either because they were untied, or because mi was rescheduled, and will
/// be visited again later). If the shouldOnlyCommute flag is true, only
/// instruction commutation is attempted.
bool TwoAddressInstructionPass::
tryInstructionTransform(MachineBasicBlock::iterator &mi,
                        MachineBasicBlock::iterator &nmi,
                        unsigned SrcIdx, unsigned DstIdx,
                        unsigned Dist, bool shouldOnlyCommute) {
  if (OptLevel == CodeGenOpt::None)
    return false;

  MachineInstr &MI = *mi;
  unsigned regA = MI.getOperand(DstIdx).getReg();
  unsigned regB = MI.getOperand(SrcIdx).getReg();

  assert(TargetRegisterInfo::isVirtualRegister(regB) &&
         "cannot make instruction into two-address form");
  bool regBKilled = isKilled(MI, regB, MRI, TII, LIS, true);

  if (TargetRegisterInfo::isVirtualRegister(regA))
    scanUses(regA);

  bool Commuted = tryInstructionCommute(&MI, DstIdx, SrcIdx, regBKilled, Dist);

  // If the instruction is convertible to 3 Addr, instead
  // of returning try 3 Addr transformation aggresively and
  // use this variable to check later. Because it might be better.
  // For example, we can just use `leal (%rsi,%rdi), %eax` and `ret`
  // instead of the following code.
  //   addl     %esi, %edi
  //   movl     %edi, %eax
  //   ret
  if (Commuted && !MI.isConvertibleTo3Addr())
    return false;

  if (shouldOnlyCommute)
    return false;

  // If there is one more use of regB later in the same MBB, consider
  // re-schedule this MI below it.
  if (!Commuted && EnableRescheduling && rescheduleMIBelowKill(mi, nmi, regB)) {
    ++NumReSchedDowns;
    return true;
  }

  // If we commuted, regB may have changed so we should re-sample it to avoid
  // confusing the three address conversion below.
  if (Commuted) {
    regB = MI.getOperand(SrcIdx).getReg();
    regBKilled = isKilled(MI, regB, MRI, TII, LIS, true);
  }

  if (MI.isConvertibleTo3Addr()) {
    // This instruction is potentially convertible to a true
    // three-address instruction.  Check if it is profitable.
    if (!regBKilled || isProfitableToConv3Addr(regA, regB)) {
      // Try to convert it.
      if (convertInstTo3Addr(mi, nmi, regA, regB, Dist)) {
        ++NumConvertedTo3Addr;
        return true; // Done with this instruction.
      }
    }
  }

  // Return if it is commuted but 3 addr conversion is failed.
  if (Commuted)
    return false;

  // If there is one more use of regB later in the same MBB, consider
  // re-schedule it before this MI if it's legal.
  if (EnableRescheduling && rescheduleKillAboveMI(mi, nmi, regB)) {
    ++NumReSchedUps;
    return true;
  }

  // If this is an instruction with a load folded into it, try unfolding
  // the load, e.g. avoid this:
  //   movq %rdx, %rcx
  //   addq (%rax), %rcx
  // in favor of this:
  //   movq (%rax), %rcx
  //   addq %rdx, %rcx
  // because it's preferable to schedule a load than a register copy.
  if (MI.mayLoad() && !regBKilled) {
    // Determine if a load can be unfolded.
    unsigned LoadRegIndex;
    unsigned NewOpc =
      TII->getOpcodeAfterMemoryUnfold(MI.getOpcode(),
                                      /*UnfoldLoad=*/true,
                                      /*UnfoldStore=*/false,
                                      &LoadRegIndex);
    if (NewOpc != 0) {
      const MCInstrDesc &UnfoldMCID = TII->get(NewOpc);
      if (UnfoldMCID.getNumDefs() == 1) {
        // Unfold the load.
        LLVM_DEBUG(dbgs() << "2addr:   UNFOLDING: " << MI);
        const TargetRegisterClass *RC =
          TRI->getAllocatableClass(
            TII->getRegClass(UnfoldMCID, LoadRegIndex, TRI, *MF));
        unsigned Reg = MRI->createVirtualRegister(RC);
        SmallVector<MachineInstr *, 2> NewMIs;
        if (!TII->unfoldMemoryOperand(*MF, MI, Reg,
                                      /*UnfoldLoad=*/true,
                                      /*UnfoldStore=*/false, NewMIs)) {
          LLVM_DEBUG(dbgs() << "2addr: ABANDONING UNFOLD\n");
          return false;
        }
        assert(NewMIs.size() == 2 &&
               "Unfolded a load into multiple instructions!");
        // The load was previously folded, so this is the only use.
        NewMIs[1]->addRegisterKilled(Reg, TRI);

        // Tentatively insert the instructions into the block so that they
        // look "normal" to the transformation logic.
        MBB->insert(mi, NewMIs[0]);
        MBB->insert(mi, NewMIs[1]);

        LLVM_DEBUG(dbgs() << "2addr:    NEW LOAD: " << *NewMIs[0]
                          << "2addr:    NEW INST: " << *NewMIs[1]);

        // Transform the instruction, now that it no longer has a load.
        unsigned NewDstIdx = NewMIs[1]->findRegisterDefOperandIdx(regA);
        unsigned NewSrcIdx = NewMIs[1]->findRegisterUseOperandIdx(regB);
        MachineBasicBlock::iterator NewMI = NewMIs[1];
        bool TransformResult =
          tryInstructionTransform(NewMI, mi, NewSrcIdx, NewDstIdx, Dist, true);
        (void)TransformResult;
        assert(!TransformResult &&
               "tryInstructionTransform() should return false.");
        if (NewMIs[1]->getOperand(NewSrcIdx).isKill()) {
          // Success, or at least we made an improvement. Keep the unfolded
          // instructions and discard the original.
          if (LV) {
            for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
              MachineOperand &MO = MI.getOperand(i);
              if (MO.isReg() &&
                  TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
                if (MO.isUse()) {
                  if (MO.isKill()) {
                    if (NewMIs[0]->killsRegister(MO.getReg()))
                      LV->replaceKillInstruction(MO.getReg(), MI, *NewMIs[0]);
                    else {
                      assert(NewMIs[1]->killsRegister(MO.getReg()) &&
                             "Kill missing after load unfold!");
                      LV->replaceKillInstruction(MO.getReg(), MI, *NewMIs[1]);
                    }
                  }
                } else if (LV->removeVirtualRegisterDead(MO.getReg(), MI)) {
                  if (NewMIs[1]->registerDefIsDead(MO.getReg()))
                    LV->addVirtualRegisterDead(MO.getReg(), *NewMIs[1]);
                  else {
                    assert(NewMIs[0]->registerDefIsDead(MO.getReg()) &&
                           "Dead flag missing after load unfold!");
                    LV->addVirtualRegisterDead(MO.getReg(), *NewMIs[0]);
                  }
                }
              }
            }
            LV->addVirtualRegisterKilled(Reg, *NewMIs[1]);
          }

          SmallVector<unsigned, 4> OrigRegs;
          if (LIS) {
            for (const MachineOperand &MO : MI.operands()) {
              if (MO.isReg())
                OrigRegs.push_back(MO.getReg());
            }
          }

          MI.eraseFromParent();

          // Update LiveIntervals.
          if (LIS) {
            MachineBasicBlock::iterator Begin(NewMIs[0]);
            MachineBasicBlock::iterator End(NewMIs[1]);
            LIS->repairIntervalsInRange(MBB, Begin, End, OrigRegs);
          }

          mi = NewMIs[1];
        } else {
          // Transforming didn't eliminate the tie and didn't lead to an
          // improvement. Clean up the unfolded instructions and keep the
          // original.
          LLVM_DEBUG(dbgs() << "2addr: ABANDONING UNFOLD\n");
          NewMIs[0]->eraseFromParent();
          NewMIs[1]->eraseFromParent();
        }
      }
    }
  }

  return false;
}

// Collect tied operands of MI that need to be handled.
// Rewrite trivial cases immediately.
// Return true if any tied operands where found, including the trivial ones.
bool TwoAddressInstructionPass::
collectTiedOperands(MachineInstr *MI, TiedOperandMap &TiedOperands) {
  const MCInstrDesc &MCID = MI->getDesc();
  bool AnyOps = false;
  unsigned NumOps = MI->getNumOperands();

  for (unsigned SrcIdx = 0; SrcIdx < NumOps; ++SrcIdx) {
    unsigned DstIdx = 0;
    if (!MI->isRegTiedToDefOperand(SrcIdx, &DstIdx))
      continue;
    AnyOps = true;
    MachineOperand &SrcMO = MI->getOperand(SrcIdx);
    MachineOperand &DstMO = MI->getOperand(DstIdx);
    unsigned SrcReg = SrcMO.getReg();
    unsigned DstReg = DstMO.getReg();
    // Tied constraint already satisfied?
    if (SrcReg == DstReg)
      continue;

    assert(SrcReg && SrcMO.isUse() && "two address instruction invalid");

    // Deal with undef uses immediately - simply rewrite the src operand.
    if (SrcMO.isUndef() && !DstMO.getSubReg()) {
      // Constrain the DstReg register class if required.
      if (TargetRegisterInfo::isVirtualRegister(DstReg))
        if (const TargetRegisterClass *RC = TII->getRegClass(MCID, SrcIdx,
                                                             TRI, *MF))
          MRI->constrainRegClass(DstReg, RC);
      SrcMO.setReg(DstReg);
      SrcMO.setSubReg(0);
      LLVM_DEBUG(dbgs() << "\t\trewrite undef:\t" << *MI);
      continue;
    }
    TiedOperands[SrcReg].push_back(std::make_pair(SrcIdx, DstIdx));
  }
  return AnyOps;
}

// Process a list of tied MI operands that all use the same source register.
// The tied pairs are of the form (SrcIdx, DstIdx).
void
TwoAddressInstructionPass::processTiedPairs(MachineInstr *MI,
                                            TiedPairList &TiedPairs,
                                            unsigned &Dist) {
  bool IsEarlyClobber = false;
  for (unsigned tpi = 0, tpe = TiedPairs.size(); tpi != tpe; ++tpi) {
    const MachineOperand &DstMO = MI->getOperand(TiedPairs[tpi].second);
    IsEarlyClobber |= DstMO.isEarlyClobber();
  }

  bool RemovedKillFlag = false;
  bool AllUsesCopied = true;
  unsigned LastCopiedReg = 0;
  SlotIndex LastCopyIdx;
  unsigned RegB = 0;
  unsigned SubRegB = 0;
  for (unsigned tpi = 0, tpe = TiedPairs.size(); tpi != tpe; ++tpi) {
    unsigned SrcIdx = TiedPairs[tpi].first;
    unsigned DstIdx = TiedPairs[tpi].second;

    const MachineOperand &DstMO = MI->getOperand(DstIdx);
    unsigned RegA = DstMO.getReg();

    // Grab RegB from the instruction because it may have changed if the
    // instruction was commuted.
    RegB = MI->getOperand(SrcIdx).getReg();
    SubRegB = MI->getOperand(SrcIdx).getSubReg();

    if (RegA == RegB) {
      // The register is tied to multiple destinations (or else we would
      // not have continued this far), but this use of the register
      // already matches the tied destination.  Leave it.
      AllUsesCopied = false;
      continue;
    }
    LastCopiedReg = RegA;

    assert(TargetRegisterInfo::isVirtualRegister(RegB) &&
           "cannot make instruction into two-address form");

#ifndef NDEBUG
    // First, verify that we don't have a use of "a" in the instruction
    // (a = b + a for example) because our transformation will not
    // work. This should never occur because we are in SSA form.
    for (unsigned i = 0; i != MI->getNumOperands(); ++i)
      assert(i == DstIdx ||
             !MI->getOperand(i).isReg() ||
             MI->getOperand(i).getReg() != RegA);
#endif

    // Emit a copy.
    MachineInstrBuilder MIB = BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
                                      TII->get(TargetOpcode::COPY), RegA);
    // If this operand is folding a truncation, the truncation now moves to the
    // copy so that the register classes remain valid for the operands.
    MIB.addReg(RegB, 0, SubRegB);
    const TargetRegisterClass *RC = MRI->getRegClass(RegB);
    if (SubRegB) {
      if (TargetRegisterInfo::isVirtualRegister(RegA)) {
        assert(TRI->getMatchingSuperRegClass(RC, MRI->getRegClass(RegA),
                                             SubRegB) &&
               "tied subregister must be a truncation");
        // The superreg class will not be used to constrain the subreg class.
        RC = nullptr;
      }
      else {
        assert(TRI->getMatchingSuperReg(RegA, SubRegB, MRI->getRegClass(RegB))
               && "tied subregister must be a truncation");
      }
    }

    // Update DistanceMap.
    MachineBasicBlock::iterator PrevMI = MI;
    --PrevMI;
    DistanceMap.insert(std::make_pair(&*PrevMI, Dist));
    DistanceMap[MI] = ++Dist;

    if (LIS) {
      LastCopyIdx = LIS->InsertMachineInstrInMaps(*PrevMI).getRegSlot();

      if (TargetRegisterInfo::isVirtualRegister(RegA)) {
        LiveInterval &LI = LIS->getInterval(RegA);
        VNInfo *VNI = LI.getNextValue(LastCopyIdx, LIS->getVNInfoAllocator());
        SlotIndex endIdx =
            LIS->getInstructionIndex(*MI).getRegSlot(IsEarlyClobber);
        LI.addSegment(LiveInterval::Segment(LastCopyIdx, endIdx, VNI));
      }
    }

    LLVM_DEBUG(dbgs() << "\t\tprepend:\t" << *MIB);

    MachineOperand &MO = MI->getOperand(SrcIdx);
    assert(MO.isReg() && MO.getReg() == RegB && MO.isUse() &&
           "inconsistent operand info for 2-reg pass");
    if (MO.isKill()) {
      MO.setIsKill(false);
      RemovedKillFlag = true;
    }

    // Make sure regA is a legal regclass for the SrcIdx operand.
    if (TargetRegisterInfo::isVirtualRegister(RegA) &&
        TargetRegisterInfo::isVirtualRegister(RegB))
      MRI->constrainRegClass(RegA, RC);
    MO.setReg(RegA);
    // The getMatchingSuper asserts guarantee that the register class projected
    // by SubRegB is compatible with RegA with no subregister. So regardless of
    // whether the dest oper writes a subreg, the source oper should not.
    MO.setSubReg(0);

    // Propagate SrcRegMap.
    SrcRegMap[RegA] = RegB;
  }

  if (AllUsesCopied) {
    bool ReplacedAllUntiedUses = true;
    if (!IsEarlyClobber) {
      // Replace other (un-tied) uses of regB with LastCopiedReg.
      for (MachineOperand &MO : MI->operands()) {
        if (MO.isReg() && MO.getReg() == RegB && MO.isUse()) {
          if (MO.getSubReg() == SubRegB) {
            if (MO.isKill()) {
              MO.setIsKill(false);
              RemovedKillFlag = true;
            }
            MO.setReg(LastCopiedReg);
            MO.setSubReg(0);
          } else {
            ReplacedAllUntiedUses = false;
          }
        }
      }
    }

    // Update live variables for regB.
    if (RemovedKillFlag && ReplacedAllUntiedUses &&
        LV && LV->getVarInfo(RegB).removeKill(*MI)) {
      MachineBasicBlock::iterator PrevMI = MI;
      --PrevMI;
      LV->addVirtualRegisterKilled(RegB, *PrevMI);
    }

    // Update LiveIntervals.
    if (LIS) {
      LiveInterval &LI = LIS->getInterval(RegB);
      SlotIndex MIIdx = LIS->getInstructionIndex(*MI);
      LiveInterval::const_iterator I = LI.find(MIIdx);
      assert(I != LI.end() && "RegB must be live-in to use.");

      SlotIndex UseIdx = MIIdx.getRegSlot(IsEarlyClobber);
      if (I->end == UseIdx)
        LI.removeSegment(LastCopyIdx, UseIdx);
    }
  } else if (RemovedKillFlag) {
    // Some tied uses of regB matched their destination registers, so
    // regB is still used in this instruction, but a kill flag was
    // removed from a different tied use of regB, so now we need to add
    // a kill flag to one of the remaining uses of regB.
    for (MachineOperand &MO : MI->operands()) {
      if (MO.isReg() && MO.getReg() == RegB && MO.isUse()) {
        MO.setIsKill(true);
        break;
      }
    }
  }
}

/// Reduce two-address instructions to two operands.
bool TwoAddressInstructionPass::runOnMachineFunction(MachineFunction &Func) {
  MF = &Func;
  const TargetMachine &TM = MF->getTarget();
  MRI = &MF->getRegInfo();
  TII = MF->getSubtarget().getInstrInfo();
  TRI = MF->getSubtarget().getRegisterInfo();
  InstrItins = MF->getSubtarget().getInstrItineraryData();
  LV = getAnalysisIfAvailable<LiveVariables>();
  LIS = getAnalysisIfAvailable<LiveIntervals>();
  if (auto *AAPass = getAnalysisIfAvailable<AAResultsWrapperPass>())
    AA = &AAPass->getAAResults();
  else
    AA = nullptr;
  OptLevel = TM.getOptLevel();
  // Disable optimizations if requested. We cannot skip the whole pass as some
  // fixups are necessary for correctness.
  if (skipFunction(Func.getFunction()))
    OptLevel = CodeGenOpt::None;

  bool MadeChange = false;

  LLVM_DEBUG(dbgs() << "********** REWRITING TWO-ADDR INSTRS **********\n");
  LLVM_DEBUG(dbgs() << "********** Function: " << MF->getName() << '\n');

  // This pass takes the function out of SSA form.
  MRI->leaveSSA();

  TiedOperandMap TiedOperands;
  for (MachineFunction::iterator MBBI = MF->begin(), MBBE = MF->end();
       MBBI != MBBE; ++MBBI) {
    MBB = &*MBBI;
    unsigned Dist = 0;
    DistanceMap.clear();
    SrcRegMap.clear();
    DstRegMap.clear();
    Processed.clear();
    SunkInstrs.clear();
    for (MachineBasicBlock::iterator mi = MBB->begin(), me = MBB->end();
         mi != me; ) {
      MachineBasicBlock::iterator nmi = std::next(mi);
      // Don't revisit an instruction previously converted by target. It may
      // contain undef register operands (%noreg), which are not handled.
      if (mi->isDebugInstr() || SunkInstrs.count(&*mi)) {
        mi = nmi;
        continue;
      }

      // Expand REG_SEQUENCE instructions. This will position mi at the first
      // expanded instruction.
      if (mi->isRegSequence())
        eliminateRegSequence(mi);

      DistanceMap.insert(std::make_pair(&*mi, ++Dist));

      processCopy(&*mi);

      // First scan through all the tied register uses in this instruction
      // and record a list of pairs of tied operands for each register.
      if (!collectTiedOperands(&*mi, TiedOperands)) {
        mi = nmi;
        continue;
      }

      ++NumTwoAddressInstrs;
      MadeChange = true;
      LLVM_DEBUG(dbgs() << '\t' << *mi);

      // If the instruction has a single pair of tied operands, try some
      // transformations that may either eliminate the tied operands or
      // improve the opportunities for coalescing away the register copy.
      if (TiedOperands.size() == 1) {
        SmallVectorImpl<std::pair<unsigned, unsigned>> &TiedPairs
          = TiedOperands.begin()->second;
        if (TiedPairs.size() == 1) {
          unsigned SrcIdx = TiedPairs[0].first;
          unsigned DstIdx = TiedPairs[0].second;
          unsigned SrcReg = mi->getOperand(SrcIdx).getReg();
          unsigned DstReg = mi->getOperand(DstIdx).getReg();
          if (SrcReg != DstReg &&
              tryInstructionTransform(mi, nmi, SrcIdx, DstIdx, Dist, false)) {
            // The tied operands have been eliminated or shifted further down
            // the block to ease elimination. Continue processing with 'nmi'.
            TiedOperands.clear();
            mi = nmi;
            continue;
          }
        }
      }

      // Now iterate over the information collected above.
      for (auto &TO : TiedOperands) {
        processTiedPairs(&*mi, TO.second, Dist);
        LLVM_DEBUG(dbgs() << "\t\trewrite to:\t" << *mi);
      }

      // Rewrite INSERT_SUBREG as COPY now that we no longer need SSA form.
      if (mi->isInsertSubreg()) {
        // From %reg = INSERT_SUBREG %reg, %subreg, subidx
        // To   %reg:subidx = COPY %subreg
        unsigned SubIdx = mi->getOperand(3).getImm();
        mi->RemoveOperand(3);
        assert(mi->getOperand(0).getSubReg() == 0 && "Unexpected subreg idx");
        mi->getOperand(0).setSubReg(SubIdx);
        mi->getOperand(0).setIsUndef(mi->getOperand(1).isUndef());
        mi->RemoveOperand(1);
        mi->setDesc(TII->get(TargetOpcode::COPY));
        LLVM_DEBUG(dbgs() << "\t\tconvert to:\t" << *mi);
      }

      // Clear TiedOperands here instead of at the top of the loop
      // since most instructions do not have tied operands.
      TiedOperands.clear();
      mi = nmi;
    }
  }

  if (LIS)
    MF->verify(this, "After two-address instruction pass");

  return MadeChange;
}

/// Eliminate a REG_SEQUENCE instruction as part of the de-ssa process.
///
/// The instruction is turned into a sequence of sub-register copies:
///
///   %dst = REG_SEQUENCE %v1, ssub0, %v2, ssub1
///
/// Becomes:
///
///   undef %dst:ssub0 = COPY %v1
///   %dst:ssub1 = COPY %v2
void TwoAddressInstructionPass::
eliminateRegSequence(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned DstReg = MI.getOperand(0).getReg();
  if (MI.getOperand(0).getSubReg() ||
      TargetRegisterInfo::isPhysicalRegister(DstReg) ||
      !(MI.getNumOperands() & 1)) {
    LLVM_DEBUG(dbgs() << "Illegal REG_SEQUENCE instruction:" << MI);
    llvm_unreachable(nullptr);
  }

  SmallVector<unsigned, 4> OrigRegs;
  if (LIS) {
    OrigRegs.push_back(MI.getOperand(0).getReg());
    for (unsigned i = 1, e = MI.getNumOperands(); i < e; i += 2)
      OrigRegs.push_back(MI.getOperand(i).getReg());
  }

  bool DefEmitted = false;
  for (unsigned i = 1, e = MI.getNumOperands(); i < e; i += 2) {
    MachineOperand &UseMO = MI.getOperand(i);
    unsigned SrcReg = UseMO.getReg();
    unsigned SubIdx = MI.getOperand(i+1).getImm();
    // Nothing needs to be inserted for undef operands.
    if (UseMO.isUndef())
      continue;

    // Defer any kill flag to the last operand using SrcReg. Otherwise, we
    // might insert a COPY that uses SrcReg after is was killed.
    bool isKill = UseMO.isKill();
    if (isKill)
      for (unsigned j = i + 2; j < e; j += 2)
        if (MI.getOperand(j).getReg() == SrcReg) {
          MI.getOperand(j).setIsKill();
          UseMO.setIsKill(false);
          isKill = false;
          break;
        }

    // Insert the sub-register copy.
    MachineInstr *CopyMI = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                                   TII->get(TargetOpcode::COPY))
                               .addReg(DstReg, RegState::Define, SubIdx)
                               .add(UseMO);

    // The first def needs an undef flag because there is no live register
    // before it.
    if (!DefEmitted) {
      CopyMI->getOperand(0).setIsUndef(true);
      // Return an iterator pointing to the first inserted instr.
      MBBI = CopyMI;
    }
    DefEmitted = true;

    // Update LiveVariables' kill info.
    if (LV && isKill && !TargetRegisterInfo::isPhysicalRegister(SrcReg))
      LV->replaceKillInstruction(SrcReg, MI, *CopyMI);

    LLVM_DEBUG(dbgs() << "Inserted: " << *CopyMI);
  }

  MachineBasicBlock::iterator EndMBBI =
      std::next(MachineBasicBlock::iterator(MI));

  if (!DefEmitted) {
    LLVM_DEBUG(dbgs() << "Turned: " << MI << " into an IMPLICIT_DEF");
    MI.setDesc(TII->get(TargetOpcode::IMPLICIT_DEF));
    for (int j = MI.getNumOperands() - 1, ee = 0; j > ee; --j)
      MI.RemoveOperand(j);
  } else {
    LLVM_DEBUG(dbgs() << "Eliminated: " << MI);
    MI.eraseFromParent();
  }

  // Udpate LiveIntervals.
  if (LIS)
    LIS->repairIntervalsInRange(MBB, MBBI, EndMBBI, OrigRegs);
}
