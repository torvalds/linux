//===-- SIFormMemoryClauses.cpp -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass creates bundles of SMEM and VMEM instructions forming memory
/// clauses if XNACK is enabled. Def operands of clauses are marked as early
/// clobber to make sure we will not override any source within a clause.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "GCNRegPressure.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "si-form-memory-clauses"

// Clauses longer then 15 instructions would overflow one of the counters
// and stall. They can stall even earlier if there are outstanding counters.
static cl::opt<unsigned>
MaxClause("amdgpu-max-memory-clause", cl::Hidden, cl::init(15),
          cl::desc("Maximum length of a memory clause, instructions"));

namespace {

class SIFormMemoryClauses : public MachineFunctionPass {
  typedef DenseMap<unsigned, std::pair<unsigned, LaneBitmask>> RegUse;

public:
  static char ID;

public:
  SIFormMemoryClauses() : MachineFunctionPass(ID) {
    initializeSIFormMemoryClausesPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI Form memory clauses";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LiveIntervals>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  template <typename Callable>
  void forAllLanes(unsigned Reg, LaneBitmask LaneMask, Callable Func) const;

  bool canBundle(const MachineInstr &MI, RegUse &Defs, RegUse &Uses) const;
  bool checkPressure(const MachineInstr &MI, GCNDownwardRPTracker &RPT);
  void collectRegUses(const MachineInstr &MI, RegUse &Defs, RegUse &Uses) const;
  bool processRegUses(const MachineInstr &MI, RegUse &Defs, RegUse &Uses,
                      GCNDownwardRPTracker &RPT);

  const GCNSubtarget *ST;
  const SIRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  SIMachineFunctionInfo *MFI;

  unsigned LastRecordedOccupancy;
  unsigned MaxVGPRs;
  unsigned MaxSGPRs;
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SIFormMemoryClauses, DEBUG_TYPE,
                      "SI Form memory clauses", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(SIFormMemoryClauses, DEBUG_TYPE,
                    "SI Form memory clauses", false, false)


char SIFormMemoryClauses::ID = 0;

char &llvm::SIFormMemoryClausesID = SIFormMemoryClauses::ID;

FunctionPass *llvm::createSIFormMemoryClausesPass() {
  return new SIFormMemoryClauses();
}

static bool isVMEMClauseInst(const MachineInstr &MI) {
  return SIInstrInfo::isFLAT(MI) || SIInstrInfo::isVMEM(MI);
}

static bool isSMEMClauseInst(const MachineInstr &MI) {
  return SIInstrInfo::isSMRD(MI);
}

// There no sense to create store clauses, they do not define anything,
// thus there is nothing to set early-clobber.
static bool isValidClauseInst(const MachineInstr &MI, bool IsVMEMClause) {
  if (MI.isDebugValue() || MI.isBundled())
    return false;
  if (!MI.mayLoad() || MI.mayStore())
    return false;
  if (AMDGPU::getAtomicNoRetOp(MI.getOpcode()) != -1 ||
      AMDGPU::getAtomicRetOp(MI.getOpcode()) != -1)
    return false;
  if (IsVMEMClause && !isVMEMClauseInst(MI))
    return false;
  if (!IsVMEMClause && !isSMEMClauseInst(MI))
    return false;
  return true;
}

static unsigned getMopState(const MachineOperand &MO) {
  unsigned S = 0;
  if (MO.isImplicit())
    S |= RegState::Implicit;
  if (MO.isDead())
    S |= RegState::Dead;
  if (MO.isUndef())
    S |= RegState::Undef;
  if (MO.isKill())
    S |= RegState::Kill;
  if (MO.isEarlyClobber())
    S |= RegState::EarlyClobber;
  if (TargetRegisterInfo::isPhysicalRegister(MO.getReg()) && MO.isRenamable())
    S |= RegState::Renamable;
  return S;
}

template <typename Callable>
void SIFormMemoryClauses::forAllLanes(unsigned Reg, LaneBitmask LaneMask,
                                      Callable Func) const {
  if (LaneMask.all() || TargetRegisterInfo::isPhysicalRegister(Reg) ||
      LaneMask == MRI->getMaxLaneMaskForVReg(Reg)) {
    Func(0);
    return;
  }

  const TargetRegisterClass *RC = MRI->getRegClass(Reg);
  unsigned E = TRI->getNumSubRegIndices();
  SmallVector<unsigned, AMDGPU::NUM_TARGET_SUBREGS> CoveringSubregs;
  for (unsigned Idx = 1; Idx < E; ++Idx) {
    // Is this index even compatible with the given class?
    if (TRI->getSubClassWithSubReg(RC, Idx) != RC)
      continue;
    LaneBitmask SubRegMask = TRI->getSubRegIndexLaneMask(Idx);
    // Early exit if we found a perfect match.
    if (SubRegMask == LaneMask) {
      Func(Idx);
      return;
    }

    if ((SubRegMask & ~LaneMask).any() || (SubRegMask & LaneMask).none())
      continue;

    CoveringSubregs.push_back(Idx);
  }

  llvm::sort(CoveringSubregs, [this](unsigned A, unsigned B) {
    LaneBitmask MaskA = TRI->getSubRegIndexLaneMask(A);
    LaneBitmask MaskB = TRI->getSubRegIndexLaneMask(B);
    unsigned NA = MaskA.getNumLanes();
    unsigned NB = MaskB.getNumLanes();
    if (NA != NB)
      return NA > NB;
    return MaskA.getHighestLane() > MaskB.getHighestLane();
  });

  for (unsigned Idx : CoveringSubregs) {
    LaneBitmask SubRegMask = TRI->getSubRegIndexLaneMask(Idx);
    if ((SubRegMask & ~LaneMask).any() || (SubRegMask & LaneMask).none())
      continue;

    Func(Idx);
    LaneMask &= ~SubRegMask;
    if (LaneMask.none())
      return;
  }

  llvm_unreachable("Failed to find all subregs to cover lane mask");
}

// Returns false if there is a use of a def already in the map.
// In this case we must break the clause.
bool SIFormMemoryClauses::canBundle(const MachineInstr &MI,
                                    RegUse &Defs, RegUse &Uses) const {
  // Check interference with defs.
  for (const MachineOperand &MO : MI.operands()) {
    // TODO: Prologue/Epilogue Insertion pass does not process bundled
    //       instructions.
    if (MO.isFI())
      return false;

    if (!MO.isReg())
      continue;

    unsigned Reg = MO.getReg();

    // If it is tied we will need to write same register as we read.
    if (MO.isTied())
      return false;

    RegUse &Map = MO.isDef() ? Uses : Defs;
    auto Conflict = Map.find(Reg);
    if (Conflict == Map.end())
      continue;

    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      return false;

    LaneBitmask Mask = TRI->getSubRegIndexLaneMask(MO.getSubReg());
    if ((Conflict->second.second & Mask).any())
      return false;
  }

  return true;
}

// Since all defs in the clause are early clobber we can run out of registers.
// Function returns false if pressure would hit the limit if instruction is
// bundled into a memory clause.
bool SIFormMemoryClauses::checkPressure(const MachineInstr &MI,
                                        GCNDownwardRPTracker &RPT) {
  // NB: skip advanceBeforeNext() call. Since all defs will be marked
  // early-clobber they will all stay alive at least to the end of the
  // clause. Therefor we should not decrease pressure even if load
  // pointer becomes dead and could otherwise be reused for destination.
  RPT.advanceToNext();
  GCNRegPressure MaxPressure = RPT.moveMaxPressure();
  unsigned Occupancy = MaxPressure.getOccupancy(*ST);
  if (Occupancy >= MFI->getMinAllowedOccupancy() &&
      MaxPressure.getVGPRNum() <= MaxVGPRs &&
      MaxPressure.getSGPRNum() <= MaxSGPRs) {
    LastRecordedOccupancy = Occupancy;
    return true;
  }
  return false;
}

// Collect register defs and uses along with their lane masks and states.
void SIFormMemoryClauses::collectRegUses(const MachineInstr &MI,
                                         RegUse &Defs, RegUse &Uses) const {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;

    LaneBitmask Mask = TargetRegisterInfo::isVirtualRegister(Reg) ?
                         TRI->getSubRegIndexLaneMask(MO.getSubReg()) :
                         LaneBitmask::getAll();
    RegUse &Map = MO.isDef() ? Defs : Uses;

    auto Loc = Map.find(Reg);
    unsigned State = getMopState(MO);
    if (Loc == Map.end()) {
      Map[Reg] = std::make_pair(State, Mask);
    } else {
      Loc->second.first |= State;
      Loc->second.second |= Mask;
    }
  }
}

// Check register def/use conflicts, occupancy limits and collect def/use maps.
// Return true if instruction can be bundled with previous. It it cannot
// def/use maps are not updated.
bool SIFormMemoryClauses::processRegUses(const MachineInstr &MI,
                                         RegUse &Defs, RegUse &Uses,
                                         GCNDownwardRPTracker &RPT) {
  if (!canBundle(MI, Defs, Uses))
    return false;

  if (!checkPressure(MI, RPT))
    return false;

  collectRegUses(MI, Defs, Uses);
  return true;
}

bool SIFormMemoryClauses::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  ST = &MF.getSubtarget<GCNSubtarget>();
  if (!ST->isXNACKEnabled())
    return false;

  const SIInstrInfo *TII = ST->getInstrInfo();
  TRI = ST->getRegisterInfo();
  MRI = &MF.getRegInfo();
  MFI = MF.getInfo<SIMachineFunctionInfo>();
  LiveIntervals *LIS = &getAnalysis<LiveIntervals>();
  SlotIndexes *Ind = LIS->getSlotIndexes();
  bool Changed = false;

  MaxVGPRs = TRI->getAllocatableSet(MF, &AMDGPU::VGPR_32RegClass).count();
  MaxSGPRs = TRI->getAllocatableSet(MF, &AMDGPU::SGPR_32RegClass).count();

  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::instr_iterator Next;
    for (auto I = MBB.instr_begin(), E = MBB.instr_end(); I != E; I = Next) {
      MachineInstr &MI = *I;
      Next = std::next(I);

      bool IsVMEM = isVMEMClauseInst(MI);

      if (!isValidClauseInst(MI, IsVMEM))
        continue;

      RegUse Defs, Uses;
      GCNDownwardRPTracker RPT(*LIS);
      RPT.reset(MI);

      if (!processRegUses(MI, Defs, Uses, RPT))
        continue;

      unsigned Length = 1;
      for ( ; Next != E && Length < MaxClause; ++Next) {
        if (!isValidClauseInst(*Next, IsVMEM))
          break;

        // A load from pointer which was loaded inside the same bundle is an
        // impossible clause because we will need to write and read the same
        // register inside. In this case processRegUses will return false.
        if (!processRegUses(*Next, Defs, Uses, RPT))
          break;

        ++Length;
      }
      if (Length < 2)
        continue;

      Changed = true;
      MFI->limitOccupancy(LastRecordedOccupancy);

      auto B = BuildMI(MBB, I, DebugLoc(), TII->get(TargetOpcode::BUNDLE));
      Ind->insertMachineInstrInMaps(*B);

      for (auto BI = I; BI != Next; ++BI) {
        BI->bundleWithPred();
        Ind->removeSingleMachineInstrFromMaps(*BI);

        for (MachineOperand &MO : BI->defs())
          if (MO.readsReg())
            MO.setIsInternalRead(true);
      }

      for (auto &&R : Defs) {
        forAllLanes(R.first, R.second.second, [&R, &B](unsigned SubReg) {
          unsigned S = R.second.first | RegState::EarlyClobber;
          if (!SubReg)
            S &= ~(RegState::Undef | RegState::Dead);
          B.addDef(R.first, S, SubReg);
        });
      }

      for (auto &&R : Uses) {
        forAllLanes(R.first, R.second.second, [&R, &B](unsigned SubReg) {
          B.addUse(R.first, R.second.first & ~RegState::Kill, SubReg);
        });
      }

      for (auto &&R : Defs) {
        unsigned Reg = R.first;
        Uses.erase(Reg);
        if (TargetRegisterInfo::isPhysicalRegister(Reg))
          continue;
        LIS->removeInterval(Reg);
        LIS->createAndComputeVirtRegInterval(Reg);
      }

      for (auto &&R : Uses) {
        unsigned Reg = R.first;
        if (TargetRegisterInfo::isPhysicalRegister(Reg))
          continue;
        LIS->removeInterval(Reg);
        LIS->createAndComputeVirtRegInterval(Reg);
      }
    }
  }

  return Changed;
}
