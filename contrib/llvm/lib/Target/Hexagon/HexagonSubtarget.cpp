//===- HexagonSubtarget.cpp - Hexagon Subtarget Information ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Hexagon specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <map>

using namespace llvm;

#define DEBUG_TYPE "hexagon-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "HexagonGenSubtargetInfo.inc"


static cl::opt<bool> EnableBSBSched("enable-bsb-sched",
  cl::Hidden, cl::ZeroOrMore, cl::init(true));

static cl::opt<bool> EnableTCLatencySched("enable-tc-latency-sched",
  cl::Hidden, cl::ZeroOrMore, cl::init(false));

static cl::opt<bool> EnableDotCurSched("enable-cur-sched",
  cl::Hidden, cl::ZeroOrMore, cl::init(true),
  cl::desc("Enable the scheduler to generate .cur"));

static cl::opt<bool> DisableHexagonMISched("disable-hexagon-misched",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Disable Hexagon MI Scheduling"));

static cl::opt<bool> EnableSubregLiveness("hexagon-subreg-liveness",
  cl::Hidden, cl::ZeroOrMore, cl::init(true),
  cl::desc("Enable subregister liveness tracking for Hexagon"));

static cl::opt<bool> OverrideLongCalls("hexagon-long-calls",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("If present, forces/disables the use of long calls"));

static cl::opt<bool> EnablePredicatedCalls("hexagon-pred-calls",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Consider calls to be predicable"));

static cl::opt<bool> SchedPredsCloser("sched-preds-closer",
  cl::Hidden, cl::ZeroOrMore, cl::init(true));

static cl::opt<bool> SchedRetvalOptimization("sched-retval-optimization",
  cl::Hidden, cl::ZeroOrMore, cl::init(true));

static cl::opt<bool> EnableCheckBankConflict("hexagon-check-bank-conflict",
  cl::Hidden, cl::ZeroOrMore, cl::init(true),
  cl::desc("Enable checking for cache bank conflicts"));


HexagonSubtarget::HexagonSubtarget(const Triple &TT, StringRef CPU,
                                   StringRef FS, const TargetMachine &TM)
    : HexagonGenSubtargetInfo(TT, CPU, FS), OptLevel(TM.getOptLevel()),
      CPUString(Hexagon_MC::selectHexagonCPU(CPU)),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)),
      RegInfo(getHwMode()), TLInfo(TM, *this),
      InstrItins(getInstrItineraryForCPU(CPUString)) {
  // Beware of the default constructor of InstrItineraryData: it will
  // reset all members to 0.
  assert(InstrItins.Itineraries != nullptr && "InstrItins not initialized");
}

HexagonSubtarget &
HexagonSubtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  static std::map<StringRef, Hexagon::ArchEnum> CpuTable{
      {"generic", Hexagon::ArchEnum::V60},
      {"hexagonv5", Hexagon::ArchEnum::V5},
      {"hexagonv55", Hexagon::ArchEnum::V55},
      {"hexagonv60", Hexagon::ArchEnum::V60},
      {"hexagonv62", Hexagon::ArchEnum::V62},
      {"hexagonv65", Hexagon::ArchEnum::V65},
      {"hexagonv66", Hexagon::ArchEnum::V66},
  };

  auto FoundIt = CpuTable.find(CPUString);
  if (FoundIt != CpuTable.end())
    HexagonArchVersion = FoundIt->second;
  else
    llvm_unreachable("Unrecognized Hexagon processor version");

  UseHVX128BOps = false;
  UseHVX64BOps = false;
  UseLongCalls = false;

  UseBSBScheduling = hasV60Ops() && EnableBSBSched;

  ParseSubtargetFeatures(CPUString, FS);

  if (OverrideLongCalls.getPosition())
    UseLongCalls = OverrideLongCalls;

  FeatureBitset Features = getFeatureBits();
  if (HexagonDisableDuplex)
    setFeatureBits(Features.set(Hexagon::FeatureDuplex, false));
  setFeatureBits(Hexagon_MC::completeHVXFeatures(Features));

  return *this;
}

void HexagonSubtarget::UsrOverflowMutation::apply(ScheduleDAGInstrs *DAG) {
  for (SUnit &SU : DAG->SUnits) {
    if (!SU.isInstr())
      continue;
    SmallVector<SDep, 4> Erase;
    for (auto &D : SU.Preds)
      if (D.getKind() == SDep::Output && D.getReg() == Hexagon::USR_OVF)
        Erase.push_back(D);
    for (auto &E : Erase)
      SU.removePred(E);
  }
}

void HexagonSubtarget::HVXMemLatencyMutation::apply(ScheduleDAGInstrs *DAG) {
  for (SUnit &SU : DAG->SUnits) {
    // Update the latency of chain edges between v60 vector load or store
    // instructions to be 1. These instruction cannot be scheduled in the
    // same packet.
    MachineInstr &MI1 = *SU.getInstr();
    auto *QII = static_cast<const HexagonInstrInfo*>(DAG->TII);
    bool IsStoreMI1 = MI1.mayStore();
    bool IsLoadMI1 = MI1.mayLoad();
    if (!QII->isHVXVec(MI1) || !(IsStoreMI1 || IsLoadMI1))
      continue;
    for (SDep &SI : SU.Succs) {
      if (SI.getKind() != SDep::Order || SI.getLatency() != 0)
        continue;
      MachineInstr &MI2 = *SI.getSUnit()->getInstr();
      if (!QII->isHVXVec(MI2))
        continue;
      if ((IsStoreMI1 && MI2.mayStore()) || (IsLoadMI1 && MI2.mayLoad())) {
        SI.setLatency(1);
        SU.setHeightDirty();
        // Change the dependence in the opposite direction too.
        for (SDep &PI : SI.getSUnit()->Preds) {
          if (PI.getSUnit() != &SU || PI.getKind() != SDep::Order)
            continue;
          PI.setLatency(1);
          SI.getSUnit()->setDepthDirty();
        }
      }
    }
  }
}

// Check if a call and subsequent A2_tfrpi instructions should maintain
// scheduling affinity. We are looking for the TFRI to be consumed in
// the next instruction. This should help reduce the instances of
// double register pairs being allocated and scheduled before a call
// when not used until after the call. This situation is exacerbated
// by the fact that we allocate the pair from the callee saves list,
// leading to excess spills and restores.
bool HexagonSubtarget::CallMutation::shouldTFRICallBind(
      const HexagonInstrInfo &HII, const SUnit &Inst1,
      const SUnit &Inst2) const {
  if (Inst1.getInstr()->getOpcode() != Hexagon::A2_tfrpi)
    return false;

  // TypeXTYPE are 64 bit operations.
  unsigned Type = HII.getType(*Inst2.getInstr());
  return Type == HexagonII::TypeS_2op || Type == HexagonII::TypeS_3op ||
         Type == HexagonII::TypeALU64 || Type == HexagonII::TypeM;
}

void HexagonSubtarget::CallMutation::apply(ScheduleDAGInstrs *DAGInstrs) {
  ScheduleDAGMI *DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);
  SUnit* LastSequentialCall = nullptr;
  // Map from virtual register to physical register from the copy.
  DenseMap<unsigned, unsigned> VRegHoldingReg;
  // Map from the physical register to the instruction that uses virtual
  // register. This is used to create the barrier edge.
  DenseMap<unsigned, SUnit *> LastVRegUse;
  auto &TRI = *DAG->MF.getSubtarget().getRegisterInfo();
  auto &HII = *DAG->MF.getSubtarget<HexagonSubtarget>().getInstrInfo();

  // Currently we only catch the situation when compare gets scheduled
  // before preceding call.
  for (unsigned su = 0, e = DAG->SUnits.size(); su != e; ++su) {
    // Remember the call.
    if (DAG->SUnits[su].getInstr()->isCall())
      LastSequentialCall = &DAG->SUnits[su];
    // Look for a compare that defines a predicate.
    else if (DAG->SUnits[su].getInstr()->isCompare() && LastSequentialCall)
      DAG->addEdge(&DAG->SUnits[su], SDep(LastSequentialCall, SDep::Barrier));
    // Look for call and tfri* instructions.
    else if (SchedPredsCloser && LastSequentialCall && su > 1 && su < e-1 &&
             shouldTFRICallBind(HII, DAG->SUnits[su], DAG->SUnits[su+1]))
      DAG->addEdge(&DAG->SUnits[su], SDep(&DAG->SUnits[su-1], SDep::Barrier));
    // Prevent redundant register copies due to reads and writes of physical
    // registers. The original motivation for this was the code generated
    // between two calls, which are caused both the return value and the
    // argument for the next call being in %r0.
    // Example:
    //   1: <call1>
    //   2: %vreg = COPY %r0
    //   3: <use of %vreg>
    //   4: %r0 = ...
    //   5: <call2>
    // The scheduler would often swap 3 and 4, so an additional register is
    // needed. This code inserts a Barrier dependence between 3 & 4 to prevent
    // this.
    // The code below checks for all the physical registers, not just R0/D0/V0.
    else if (SchedRetvalOptimization) {
      const MachineInstr *MI = DAG->SUnits[su].getInstr();
      if (MI->isCopy() &&
          TargetRegisterInfo::isPhysicalRegister(MI->getOperand(1).getReg())) {
        // %vregX = COPY %r0
        VRegHoldingReg[MI->getOperand(0).getReg()] = MI->getOperand(1).getReg();
        LastVRegUse.erase(MI->getOperand(1).getReg());
      } else {
        for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
          const MachineOperand &MO = MI->getOperand(i);
          if (!MO.isReg())
            continue;
          if (MO.isUse() && !MI->isCopy() &&
              VRegHoldingReg.count(MO.getReg())) {
            // <use of %vregX>
            LastVRegUse[VRegHoldingReg[MO.getReg()]] = &DAG->SUnits[su];
          } else if (MO.isDef() &&
                     TargetRegisterInfo::isPhysicalRegister(MO.getReg())) {
            for (MCRegAliasIterator AI(MO.getReg(), &TRI, true); AI.isValid();
                 ++AI) {
              if (LastVRegUse.count(*AI) &&
                  LastVRegUse[*AI] != &DAG->SUnits[su])
                // %r0 = ...
                DAG->addEdge(&DAG->SUnits[su], SDep(LastVRegUse[*AI], SDep::Barrier));
              LastVRegUse.erase(*AI);
            }
          }
        }
      }
    }
  }
}

void HexagonSubtarget::BankConflictMutation::apply(ScheduleDAGInstrs *DAG) {
  if (!EnableCheckBankConflict)
    return;

  const auto &HII = static_cast<const HexagonInstrInfo&>(*DAG->TII);

  // Create artificial edges between loads that could likely cause a bank
  // conflict. Since such loads would normally not have any dependency
  // between them, we cannot rely on existing edges.
  for (unsigned i = 0, e = DAG->SUnits.size(); i != e; ++i) {
    SUnit &S0 = DAG->SUnits[i];
    MachineInstr &L0 = *S0.getInstr();
    if (!L0.mayLoad() || L0.mayStore() ||
        HII.getAddrMode(L0) != HexagonII::BaseImmOffset)
      continue;
    int64_t Offset0;
    unsigned Size0;
    MachineOperand *BaseOp0 = HII.getBaseAndOffset(L0, Offset0, Size0);
    // Is the access size is longer than the L1 cache line, skip the check.
    if (BaseOp0 == nullptr || !BaseOp0->isReg() || Size0 >= 32)
      continue;
    // Scan only up to 32 instructions ahead (to avoid n^2 complexity).
    for (unsigned j = i+1, m = std::min(i+32, e); j != m; ++j) {
      SUnit &S1 = DAG->SUnits[j];
      MachineInstr &L1 = *S1.getInstr();
      if (!L1.mayLoad() || L1.mayStore() ||
          HII.getAddrMode(L1) != HexagonII::BaseImmOffset)
        continue;
      int64_t Offset1;
      unsigned Size1;
      MachineOperand *BaseOp1 = HII.getBaseAndOffset(L1, Offset1, Size1);
      if (BaseOp1 == nullptr || !BaseOp1->isReg() || Size1 >= 32 ||
          BaseOp0->getReg() != BaseOp1->getReg())
        continue;
      // Check bits 3 and 4 of the offset: if they differ, a bank conflict
      // is unlikely.
      if (((Offset0 ^ Offset1) & 0x18) != 0)
        continue;
      // Bits 3 and 4 are the same, add an artificial edge and set extra
      // latency.
      SDep A(&S0, SDep::Artificial);
      A.setLatency(1);
      S1.addPred(A, true);
    }
  }
}

/// Enable use of alias analysis during code generation (during MI
/// scheduling, DAGCombine, etc.).
bool HexagonSubtarget::useAA() const {
  if (OptLevel != CodeGenOpt::None)
    return true;
  return false;
}

/// Perform target specific adjustments to the latency of a schedule
/// dependency.
void HexagonSubtarget::adjustSchedDependency(SUnit *Src, SUnit *Dst,
                                             SDep &Dep) const {
  MachineInstr *SrcInst = Src->getInstr();
  MachineInstr *DstInst = Dst->getInstr();
  if (!Src->isInstr() || !Dst->isInstr())
    return;

  const HexagonInstrInfo *QII = getInstrInfo();

  // Instructions with .new operands have zero latency.
  SmallSet<SUnit *, 4> ExclSrc;
  SmallSet<SUnit *, 4> ExclDst;
  if (QII->canExecuteInBundle(*SrcInst, *DstInst) &&
      isBestZeroLatency(Src, Dst, QII, ExclSrc, ExclDst)) {
    Dep.setLatency(0);
    return;
  }

  if (!hasV60Ops())
    return;

  // Set the latency for a copy to zero since we hope that is will get removed.
  if (DstInst->isCopy())
    Dep.setLatency(0);

  // If it's a REG_SEQUENCE/COPY, use its destination instruction to determine
  // the correct latency.
  if ((DstInst->isRegSequence() || DstInst->isCopy()) && Dst->NumSuccs == 1) {
    unsigned DReg = DstInst->getOperand(0).getReg();
    MachineInstr *DDst = Dst->Succs[0].getSUnit()->getInstr();
    unsigned UseIdx = -1;
    for (unsigned OpNum = 0; OpNum < DDst->getNumOperands(); OpNum++) {
      const MachineOperand &MO = DDst->getOperand(OpNum);
      if (MO.isReg() && MO.getReg() && MO.isUse() && MO.getReg() == DReg) {
        UseIdx = OpNum;
        break;
      }
    }
    int DLatency = (InstrInfo.getOperandLatency(&InstrItins, *SrcInst,
                                                0, *DDst, UseIdx));
    DLatency = std::max(DLatency, 0);
    Dep.setLatency((unsigned)DLatency);
  }

  // Try to schedule uses near definitions to generate .cur.
  ExclSrc.clear();
  ExclDst.clear();
  if (EnableDotCurSched && QII->isToBeScheduledASAP(*SrcInst, *DstInst) &&
      isBestZeroLatency(Src, Dst, QII, ExclSrc, ExclDst)) {
    Dep.setLatency(0);
    return;
  }

  updateLatency(*SrcInst, *DstInst, Dep);
}

void HexagonSubtarget::getPostRAMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(llvm::make_unique<UsrOverflowMutation>());
  Mutations.push_back(llvm::make_unique<HVXMemLatencyMutation>());
  Mutations.push_back(llvm::make_unique<BankConflictMutation>());
}

void HexagonSubtarget::getSMSMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(llvm::make_unique<UsrOverflowMutation>());
  Mutations.push_back(llvm::make_unique<HVXMemLatencyMutation>());
}

// Pin the vtable to this file.
void HexagonSubtarget::anchor() {}

bool HexagonSubtarget::enableMachineScheduler() const {
  if (DisableHexagonMISched.getNumOccurrences())
    return !DisableHexagonMISched;
  return true;
}

bool HexagonSubtarget::usePredicatedCalls() const {
  return EnablePredicatedCalls;
}

void HexagonSubtarget::updateLatency(MachineInstr &SrcInst,
      MachineInstr &DstInst, SDep &Dep) const {
  if (Dep.isArtificial()) {
    Dep.setLatency(1);
    return;
  }

  if (!hasV60Ops())
    return;

  auto &QII = static_cast<const HexagonInstrInfo&>(*getInstrInfo());

  // BSB scheduling.
  if (QII.isHVXVec(SrcInst) || useBSBScheduling())
    Dep.setLatency((Dep.getLatency() + 1) >> 1);
}

void HexagonSubtarget::restoreLatency(SUnit *Src, SUnit *Dst) const {
  MachineInstr *SrcI = Src->getInstr();
  for (auto &I : Src->Succs) {
    if (!I.isAssignedRegDep() || I.getSUnit() != Dst)
      continue;
    unsigned DepR = I.getReg();
    int DefIdx = -1;
    for (unsigned OpNum = 0; OpNum < SrcI->getNumOperands(); OpNum++) {
      const MachineOperand &MO = SrcI->getOperand(OpNum);
      if (MO.isReg() && MO.isDef() && MO.getReg() == DepR)
        DefIdx = OpNum;
    }
    assert(DefIdx >= 0 && "Def Reg not found in Src MI");
    MachineInstr *DstI = Dst->getInstr();
    SDep T = I;
    for (unsigned OpNum = 0; OpNum < DstI->getNumOperands(); OpNum++) {
      const MachineOperand &MO = DstI->getOperand(OpNum);
      if (MO.isReg() && MO.isUse() && MO.getReg() == DepR) {
        int Latency = (InstrInfo.getOperandLatency(&InstrItins, *SrcI,
                                                   DefIdx, *DstI, OpNum));

        // For some instructions (ex: COPY), we might end up with < 0 latency
        // as they don't have any Itinerary class associated with them.
        Latency = std::max(Latency, 0);

        I.setLatency(Latency);
        updateLatency(*SrcI, *DstI, I);
      }
    }

    // Update the latency of opposite edge too.
    T.setSUnit(Src);
    auto F = std::find(Dst->Preds.begin(), Dst->Preds.end(), T);
    assert(F != Dst->Preds.end());
    F->setLatency(I.getLatency());
  }
}

/// Change the latency between the two SUnits.
void HexagonSubtarget::changeLatency(SUnit *Src, SUnit *Dst, unsigned Lat)
      const {
  for (auto &I : Src->Succs) {
    if (!I.isAssignedRegDep() || I.getSUnit() != Dst)
      continue;
    SDep T = I;
    I.setLatency(Lat);

    // Update the latency of opposite edge too.
    T.setSUnit(Src);
    auto F = std::find(Dst->Preds.begin(), Dst->Preds.end(), T);
    assert(F != Dst->Preds.end());
    F->setLatency(Lat);
  }
}

/// If the SUnit has a zero latency edge, return the other SUnit.
static SUnit *getZeroLatency(SUnit *N, SmallVector<SDep, 4> &Deps) {
  for (auto &I : Deps)
    if (I.isAssignedRegDep() && I.getLatency() == 0 &&
        !I.getSUnit()->getInstr()->isPseudo())
      return I.getSUnit();
  return nullptr;
}

// Return true if these are the best two instructions to schedule
// together with a zero latency. Only one dependence should have a zero
// latency. If there are multiple choices, choose the best, and change
// the others, if needed.
bool HexagonSubtarget::isBestZeroLatency(SUnit *Src, SUnit *Dst,
      const HexagonInstrInfo *TII, SmallSet<SUnit*, 4> &ExclSrc,
      SmallSet<SUnit*, 4> &ExclDst) const {
  MachineInstr &SrcInst = *Src->getInstr();
  MachineInstr &DstInst = *Dst->getInstr();

  // Ignore Boundary SU nodes as these have null instructions.
  if (Dst->isBoundaryNode())
    return false;

  if (SrcInst.isPHI() || DstInst.isPHI())
    return false;

  if (!TII->isToBeScheduledASAP(SrcInst, DstInst) &&
      !TII->canExecuteInBundle(SrcInst, DstInst))
    return false;

  // The architecture doesn't allow three dependent instructions in the same
  // packet. So, if the destination has a zero latency successor, then it's
  // not a candidate for a zero latency predecessor.
  if (getZeroLatency(Dst, Dst->Succs) != nullptr)
    return false;

  // Check if the Dst instruction is the best candidate first.
  SUnit *Best = nullptr;
  SUnit *DstBest = nullptr;
  SUnit *SrcBest = getZeroLatency(Dst, Dst->Preds);
  if (SrcBest == nullptr || Src->NodeNum >= SrcBest->NodeNum) {
    // Check that Src doesn't have a better candidate.
    DstBest = getZeroLatency(Src, Src->Succs);
    if (DstBest == nullptr || Dst->NodeNum <= DstBest->NodeNum)
      Best = Dst;
  }
  if (Best != Dst)
    return false;

  // The caller frequently adds the same dependence twice. If so, then
  // return true for this case too.
  if ((Src == SrcBest && Dst == DstBest ) ||
      (SrcBest == nullptr && Dst == DstBest) ||
      (Src == SrcBest && Dst == nullptr))
    return true;

  // Reassign the latency for the previous bests, which requires setting
  // the dependence edge in both directions.
  if (SrcBest != nullptr) {
    if (!hasV60Ops())
      changeLatency(SrcBest, Dst, 1);
    else
      restoreLatency(SrcBest, Dst);
  }
  if (DstBest != nullptr) {
    if (!hasV60Ops())
      changeLatency(Src, DstBest, 1);
    else
      restoreLatency(Src, DstBest);
  }

  // Attempt to find another opprotunity for zero latency in a different
  // dependence.
  if (SrcBest && DstBest)
    // If there is an edge from SrcBest to DstBst, then try to change that
    // to 0 now.
    changeLatency(SrcBest, DstBest, 0);
  else if (DstBest) {
    // Check if the previous best destination instruction has a new zero
    // latency dependence opportunity.
    ExclSrc.insert(Src);
    for (auto &I : DstBest->Preds)
      if (ExclSrc.count(I.getSUnit()) == 0 &&
          isBestZeroLatency(I.getSUnit(), DstBest, TII, ExclSrc, ExclDst))
        changeLatency(I.getSUnit(), DstBest, 0);
  } else if (SrcBest) {
    // Check if previous best source instruction has a new zero latency
    // dependence opportunity.
    ExclDst.insert(Dst);
    for (auto &I : SrcBest->Succs)
      if (ExclDst.count(I.getSUnit()) == 0 &&
          isBestZeroLatency(SrcBest, I.getSUnit(), TII, ExclSrc, ExclDst))
        changeLatency(SrcBest, I.getSUnit(), 0);
  }

  return true;
}

unsigned HexagonSubtarget::getL1CacheLineSize() const {
  return 32;
}

unsigned HexagonSubtarget::getL1PrefetchDistance() const {
  return 32;
}

bool HexagonSubtarget::enableSubRegLiveness() const {
  return EnableSubregLiveness;
}
