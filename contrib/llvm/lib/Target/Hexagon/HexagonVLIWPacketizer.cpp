//===- HexagonPacketizer.cpp - VLIW packetizer ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements a simple VLIW packetizer using DFA. The packetizer works on
// machine basic blocks. For each instruction I in BB, the packetizer consults
// the DFA to see if machine resources are available to execute I. If so, the
// packetizer checks if I depends on any instruction J in the current packet.
// If no dependency is found, I is added to current packet and machine resource
// is marked as taken. If any dependency is found, a target API call is made to
// prune the dependence.
//
//===----------------------------------------------------------------------===//

#include "HexagonVLIWPacketizer.h"
#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "packets"

static cl::opt<bool> DisablePacketizer("disable-packetizer", cl::Hidden,
  cl::ZeroOrMore, cl::init(false),
  cl::desc("Disable Hexagon packetizer pass"));

cl::opt<bool> Slot1Store("slot1-store-slot0-load", cl::Hidden,
  cl::ZeroOrMore, cl::init(true),
  cl::desc("Allow slot1 store and slot0 load"));

static cl::opt<bool> PacketizeVolatiles("hexagon-packetize-volatiles",
  cl::ZeroOrMore, cl::Hidden, cl::init(true),
  cl::desc("Allow non-solo packetization of volatile memory references"));

static cl::opt<bool> EnableGenAllInsnClass("enable-gen-insn", cl::init(false),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Generate all instruction with TC"));

static cl::opt<bool> DisableVecDblNVStores("disable-vecdbl-nv-stores",
  cl::init(false), cl::Hidden, cl::ZeroOrMore,
  cl::desc("Disable vector double new-value-stores"));

extern cl::opt<bool> ScheduleInlineAsm;

namespace llvm {

FunctionPass *createHexagonPacketizer(bool Minimal);
void initializeHexagonPacketizerPass(PassRegistry&);

} // end namespace llvm

namespace {

  class HexagonPacketizer : public MachineFunctionPass {
  public:
    static char ID;

    HexagonPacketizer(bool Min = false)
      : MachineFunctionPass(ID), Minimal(Min) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<MachineBranchProbabilityInfo>();
      AU.addRequired<MachineDominatorTree>();
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineDominatorTree>();
      AU.addPreserved<MachineLoopInfo>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    StringRef getPassName() const override { return "Hexagon Packetizer"; }
    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

  private:
    const HexagonInstrInfo *HII;
    const HexagonRegisterInfo *HRI;
    const bool Minimal;
  };

} // end anonymous namespace

char HexagonPacketizer::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonPacketizer, "hexagon-packetizer",
                      "Hexagon Packetizer", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfo)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(HexagonPacketizer, "hexagon-packetizer",
                    "Hexagon Packetizer", false, false)

HexagonPacketizerList::HexagonPacketizerList(MachineFunction &MF,
      MachineLoopInfo &MLI, AliasAnalysis *AA,
      const MachineBranchProbabilityInfo *MBPI, bool Minimal)
    : VLIWPacketizerList(MF, MLI, AA), MBPI(MBPI), MLI(&MLI),
      Minimal(Minimal) {
  HII = MF.getSubtarget<HexagonSubtarget>().getInstrInfo();
  HRI = MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();

  addMutation(llvm::make_unique<HexagonSubtarget::UsrOverflowMutation>());
  addMutation(llvm::make_unique<HexagonSubtarget::HVXMemLatencyMutation>());
  addMutation(llvm::make_unique<HexagonSubtarget::BankConflictMutation>());
}

// Check if FirstI modifies a register that SecondI reads.
static bool hasWriteToReadDep(const MachineInstr &FirstI,
                              const MachineInstr &SecondI,
                              const TargetRegisterInfo *TRI) {
  for (auto &MO : FirstI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned R = MO.getReg();
    if (SecondI.readsRegister(R, TRI))
      return true;
  }
  return false;
}


static MachineBasicBlock::iterator moveInstrOut(MachineInstr &MI,
      MachineBasicBlock::iterator BundleIt, bool Before) {
  MachineBasicBlock::instr_iterator InsertPt;
  if (Before)
    InsertPt = BundleIt.getInstrIterator();
  else
    InsertPt = std::next(BundleIt).getInstrIterator();

  MachineBasicBlock &B = *MI.getParent();
  // The instruction should at least be bundled with the preceding instruction
  // (there will always be one, i.e. BUNDLE, if nothing else).
  assert(MI.isBundledWithPred());
  if (MI.isBundledWithSucc()) {
    MI.clearFlag(MachineInstr::BundledSucc);
    MI.clearFlag(MachineInstr::BundledPred);
  } else {
    // If it's not bundled with the successor (i.e. it is the last one
    // in the bundle), then we can simply unbundle it from the predecessor,
    // which will take care of updating the predecessor's flag.
    MI.unbundleFromPred();
  }
  B.splice(InsertPt, &B, MI.getIterator());

  // Get the size of the bundle without asserting.
  MachineBasicBlock::const_instr_iterator I = BundleIt.getInstrIterator();
  MachineBasicBlock::const_instr_iterator E = B.instr_end();
  unsigned Size = 0;
  for (++I; I != E && I->isBundledWithPred(); ++I)
    ++Size;

  // If there are still two or more instructions, then there is nothing
  // else to be done.
  if (Size > 1)
    return BundleIt;

  // Otherwise, extract the single instruction out and delete the bundle.
  MachineBasicBlock::iterator NextIt = std::next(BundleIt);
  MachineInstr &SingleI = *BundleIt->getNextNode();
  SingleI.unbundleFromPred();
  assert(!SingleI.isBundledWithSucc());
  BundleIt->eraseFromParent();
  return NextIt;
}

bool HexagonPacketizer::runOnMachineFunction(MachineFunction &MF) {
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  HII = HST.getInstrInfo();
  HRI = HST.getRegisterInfo();
  auto &MLI = getAnalysis<MachineLoopInfo>();
  auto *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto *MBPI = &getAnalysis<MachineBranchProbabilityInfo>();

  if (EnableGenAllInsnClass)
    HII->genAllInsnTimingClasses(MF);

  // Instantiate the packetizer.
  bool MinOnly = Minimal || DisablePacketizer || !HST.usePackets() ||
                 skipFunction(MF.getFunction());
  HexagonPacketizerList Packetizer(MF, MLI, AA, MBPI, MinOnly);

  // DFA state table should not be empty.
  assert(Packetizer.getResourceTracker() && "Empty DFA table!");

  // Loop over all basic blocks and remove KILL pseudo-instructions
  // These instructions confuse the dependence analysis. Consider:
  // D0 = ...   (Insn 0)
  // R0 = KILL R0, D0 (Insn 1)
  // R0 = ... (Insn 2)
  // Here, Insn 1 will result in the dependence graph not emitting an output
  // dependence between Insn 0 and Insn 2. This can lead to incorrect
  // packetization
  for (MachineBasicBlock &MB : MF) {
    auto End = MB.end();
    auto MI = MB.begin();
    while (MI != End) {
      auto NextI = std::next(MI);
      if (MI->isKill()) {
        MB.erase(MI);
        End = MB.end();
      }
      MI = NextI;
    }
  }

  // Loop over all of the basic blocks.
  for (auto &MB : MF) {
    auto Begin = MB.begin(), End = MB.end();
    while (Begin != End) {
      // Find the first non-boundary starting from the end of the last
      // scheduling region.
      MachineBasicBlock::iterator RB = Begin;
      while (RB != End && HII->isSchedulingBoundary(*RB, &MB, MF))
        ++RB;
      // Find the first boundary starting from the beginning of the new
      // region.
      MachineBasicBlock::iterator RE = RB;
      while (RE != End && !HII->isSchedulingBoundary(*RE, &MB, MF))
        ++RE;
      // Add the scheduling boundary if it's not block end.
      if (RE != End)
        ++RE;
      // If RB == End, then RE == End.
      if (RB != End)
        Packetizer.PacketizeMIs(&MB, RB, RE);

      Begin = RE;
    }
  }

  Packetizer.unpacketizeSoloInstrs(MF);
  return true;
}

// Reserve resources for a constant extender. Trigger an assertion if the
// reservation fails.
void HexagonPacketizerList::reserveResourcesForConstExt() {
  if (!tryAllocateResourcesForConstExt(true))
    llvm_unreachable("Resources not available");
}

bool HexagonPacketizerList::canReserveResourcesForConstExt() {
  return tryAllocateResourcesForConstExt(false);
}

// Allocate resources (i.e. 4 bytes) for constant extender. If succeeded,
// return true, otherwise, return false.
bool HexagonPacketizerList::tryAllocateResourcesForConstExt(bool Reserve) {
  auto *ExtMI = MF.CreateMachineInstr(HII->get(Hexagon::A4_ext), DebugLoc());
  bool Avail = ResourceTracker->canReserveResources(*ExtMI);
  if (Reserve && Avail)
    ResourceTracker->reserveResources(*ExtMI);
  MF.DeleteMachineInstr(ExtMI);
  return Avail;
}

bool HexagonPacketizerList::isCallDependent(const MachineInstr &MI,
      SDep::Kind DepType, unsigned DepReg) {
  // Check for LR dependence.
  if (DepReg == HRI->getRARegister())
    return true;

  if (HII->isDeallocRet(MI))
    if (DepReg == HRI->getFrameRegister() || DepReg == HRI->getStackRegister())
      return true;

  // Call-like instructions can be packetized with preceding instructions
  // that define registers implicitly used or modified by the call. Explicit
  // uses are still prohibited, as in the case of indirect calls:
  //   r0 = ...
  //   J2_jumpr r0
  if (DepType == SDep::Data) {
    for (const MachineOperand MO : MI.operands())
      if (MO.isReg() && MO.getReg() == DepReg && !MO.isImplicit())
        return true;
  }

  return false;
}

static bool isRegDependence(const SDep::Kind DepType) {
  return DepType == SDep::Data || DepType == SDep::Anti ||
         DepType == SDep::Output;
}

static bool isDirectJump(const MachineInstr &MI) {
  return MI.getOpcode() == Hexagon::J2_jump;
}

static bool isSchedBarrier(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Hexagon::Y2_barrier:
    return true;
  }
  return false;
}

static bool isControlFlow(const MachineInstr &MI) {
  return MI.getDesc().isTerminator() || MI.getDesc().isCall();
}

/// Returns true if the instruction modifies a callee-saved register.
static bool doesModifyCalleeSavedReg(const MachineInstr &MI,
                                     const TargetRegisterInfo *TRI) {
  const MachineFunction &MF = *MI.getParent()->getParent();
  for (auto *CSR = TRI->getCalleeSavedRegs(&MF); CSR && *CSR; ++CSR)
    if (MI.modifiesRegister(*CSR, TRI))
      return true;
  return false;
}

// Returns true if an instruction can be promoted to .new predicate or
// new-value store.
bool HexagonPacketizerList::isNewifiable(const MachineInstr &MI,
      const TargetRegisterClass *NewRC) {
  // Vector stores can be predicated, and can be new-value stores, but
  // they cannot be predicated on a .new predicate value.
  if (NewRC == &Hexagon::PredRegsRegClass) {
    if (HII->isHVXVec(MI) && MI.mayStore())
      return false;
    return HII->isPredicated(MI) && HII->getDotNewPredOp(MI, nullptr) > 0;
  }
  // If the class is not PredRegs, it could only apply to new-value stores.
  return HII->mayBeNewStore(MI);
}

// Promote an instructiont to its .cur form.
// At this time, we have already made a call to canPromoteToDotCur and made
// sure that it can *indeed* be promoted.
bool HexagonPacketizerList::promoteToDotCur(MachineInstr &MI,
      SDep::Kind DepType, MachineBasicBlock::iterator &MII,
      const TargetRegisterClass* RC) {
  assert(DepType == SDep::Data);
  int CurOpcode = HII->getDotCurOp(MI);
  MI.setDesc(HII->get(CurOpcode));
  return true;
}

void HexagonPacketizerList::cleanUpDotCur() {
  MachineInstr *MI = nullptr;
  for (auto BI : CurrentPacketMIs) {
    LLVM_DEBUG(dbgs() << "Cleanup packet has "; BI->dump(););
    if (HII->isDotCurInst(*BI)) {
      MI = BI;
      continue;
    }
    if (MI) {
      for (auto &MO : BI->operands())
        if (MO.isReg() && MO.getReg() == MI->getOperand(0).getReg())
          return;
    }
  }
  if (!MI)
    return;
  // We did not find a use of the CUR, so de-cur it.
  MI->setDesc(HII->get(HII->getNonDotCurOp(*MI)));
  LLVM_DEBUG(dbgs() << "Demoted CUR "; MI->dump(););
}

// Check to see if an instruction can be dot cur.
bool HexagonPacketizerList::canPromoteToDotCur(const MachineInstr &MI,
      const SUnit *PacketSU, unsigned DepReg, MachineBasicBlock::iterator &MII,
      const TargetRegisterClass *RC) {
  if (!HII->isHVXVec(MI))
    return false;
  if (!HII->isHVXVec(*MII))
    return false;

  // Already a dot new instruction.
  if (HII->isDotCurInst(MI) && !HII->mayBeCurLoad(MI))
    return false;

  if (!HII->mayBeCurLoad(MI))
    return false;

  // The "cur value" cannot come from inline asm.
  if (PacketSU->getInstr()->isInlineAsm())
    return false;

  // Make sure candidate instruction uses cur.
  LLVM_DEBUG(dbgs() << "Can we DOT Cur Vector MI\n"; MI.dump();
             dbgs() << "in packet\n";);
  MachineInstr &MJ = *MII;
  LLVM_DEBUG({
    dbgs() << "Checking CUR against ";
    MJ.dump();
  });
  unsigned DestReg = MI.getOperand(0).getReg();
  bool FoundMatch = false;
  for (auto &MO : MJ.operands())
    if (MO.isReg() && MO.getReg() == DestReg)
      FoundMatch = true;
  if (!FoundMatch)
    return false;

  // Check for existing uses of a vector register within the packet which
  // would be affected by converting a vector load into .cur formt.
  for (auto BI : CurrentPacketMIs) {
    LLVM_DEBUG(dbgs() << "packet has "; BI->dump(););
    if (BI->readsRegister(DepReg, MF.getSubtarget().getRegisterInfo()))
      return false;
  }

  LLVM_DEBUG(dbgs() << "Can Dot CUR MI\n"; MI.dump(););
  // We can convert the opcode into a .cur.
  return true;
}

// Promote an instruction to its .new form. At this time, we have already
// made a call to canPromoteToDotNew and made sure that it can *indeed* be
// promoted.
bool HexagonPacketizerList::promoteToDotNew(MachineInstr &MI,
      SDep::Kind DepType, MachineBasicBlock::iterator &MII,
      const TargetRegisterClass* RC) {
  assert(DepType == SDep::Data);
  int NewOpcode;
  if (RC == &Hexagon::PredRegsRegClass)
    NewOpcode = HII->getDotNewPredOp(MI, MBPI);
  else
    NewOpcode = HII->getDotNewOp(MI);
  MI.setDesc(HII->get(NewOpcode));
  return true;
}

bool HexagonPacketizerList::demoteToDotOld(MachineInstr &MI) {
  int NewOpcode = HII->getDotOldOp(MI);
  MI.setDesc(HII->get(NewOpcode));
  return true;
}

bool HexagonPacketizerList::useCallersSP(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::S2_storerd_io:
    case Hexagon::S2_storeri_io:
    case Hexagon::S2_storerh_io:
    case Hexagon::S2_storerb_io:
      break;
    default:
      llvm_unreachable("Unexpected instruction");
  }
  unsigned FrameSize = MF.getFrameInfo().getStackSize();
  MachineOperand &Off = MI.getOperand(1);
  int64_t NewOff = Off.getImm() - (FrameSize + HEXAGON_LRFP_SIZE);
  if (HII->isValidOffset(Opc, NewOff, HRI)) {
    Off.setImm(NewOff);
    return true;
  }
  return false;
}

void HexagonPacketizerList::useCalleesSP(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::S2_storerd_io:
    case Hexagon::S2_storeri_io:
    case Hexagon::S2_storerh_io:
    case Hexagon::S2_storerb_io:
      break;
    default:
      llvm_unreachable("Unexpected instruction");
  }
  unsigned FrameSize = MF.getFrameInfo().getStackSize();
  MachineOperand &Off = MI.getOperand(1);
  Off.setImm(Off.getImm() + FrameSize + HEXAGON_LRFP_SIZE);
}

/// Return true if we can update the offset in MI so that MI and MJ
/// can be packetized together.
bool HexagonPacketizerList::updateOffset(SUnit *SUI, SUnit *SUJ) {
  assert(SUI->getInstr() && SUJ->getInstr());
  MachineInstr &MI = *SUI->getInstr();
  MachineInstr &MJ = *SUJ->getInstr();

  unsigned BPI, OPI;
  if (!HII->getBaseAndOffsetPosition(MI, BPI, OPI))
    return false;
  unsigned BPJ, OPJ;
  if (!HII->getBaseAndOffsetPosition(MJ, BPJ, OPJ))
    return false;
  unsigned Reg = MI.getOperand(BPI).getReg();
  if (Reg != MJ.getOperand(BPJ).getReg())
    return false;
  // Make sure that the dependences do not restrict adding MI to the packet.
  // That is, ignore anti dependences, and make sure the only data dependence
  // involves the specific register.
  for (const auto &PI : SUI->Preds)
    if (PI.getKind() != SDep::Anti &&
        (PI.getKind() != SDep::Data || PI.getReg() != Reg))
      return false;
  int Incr;
  if (!HII->getIncrementValue(MJ, Incr))
    return false;

  int64_t Offset = MI.getOperand(OPI).getImm();
  if (!HII->isValidOffset(MI.getOpcode(), Offset+Incr, HRI))
    return false;

  MI.getOperand(OPI).setImm(Offset + Incr);
  ChangedOffset = Offset;
  return true;
}

/// Undo the changed offset. This is needed if the instruction cannot be
/// added to the current packet due to a different instruction.
void HexagonPacketizerList::undoChangedOffset(MachineInstr &MI) {
  unsigned BP, OP;
  if (!HII->getBaseAndOffsetPosition(MI, BP, OP))
    llvm_unreachable("Unable to find base and offset operands.");
  MI.getOperand(OP).setImm(ChangedOffset);
}

enum PredicateKind {
  PK_False,
  PK_True,
  PK_Unknown
};

/// Returns true if an instruction is predicated on p0 and false if it's
/// predicated on !p0.
static PredicateKind getPredicateSense(const MachineInstr &MI,
                                       const HexagonInstrInfo *HII) {
  if (!HII->isPredicated(MI))
    return PK_Unknown;
  if (HII->isPredicatedTrue(MI))
    return PK_True;
  return PK_False;
}

static const MachineOperand &getPostIncrementOperand(const MachineInstr &MI,
      const HexagonInstrInfo *HII) {
  assert(HII->isPostIncrement(MI) && "Not a post increment operation.");
#ifndef NDEBUG
  // Post Increment means duplicates. Use dense map to find duplicates in the
  // list. Caution: Densemap initializes with the minimum of 64 buckets,
  // whereas there are at most 5 operands in the post increment.
  DenseSet<unsigned> DefRegsSet;
  for (auto &MO : MI.operands())
    if (MO.isReg() && MO.isDef())
      DefRegsSet.insert(MO.getReg());

  for (auto &MO : MI.operands())
    if (MO.isReg() && MO.isUse() && DefRegsSet.count(MO.getReg()))
      return MO;
#else
  if (MI.mayLoad()) {
    const MachineOperand &Op1 = MI.getOperand(1);
    // The 2nd operand is always the post increment operand in load.
    assert(Op1.isReg() && "Post increment operand has be to a register.");
    return Op1;
  }
  if (MI.getDesc().mayStore()) {
    const MachineOperand &Op0 = MI.getOperand(0);
    // The 1st operand is always the post increment operand in store.
    assert(Op0.isReg() && "Post increment operand has be to a register.");
    return Op0;
  }
#endif
  // we should never come here.
  llvm_unreachable("mayLoad or mayStore not set for Post Increment operation");
}

// Get the value being stored.
static const MachineOperand& getStoreValueOperand(const MachineInstr &MI) {
  // value being stored is always the last operand.
  return MI.getOperand(MI.getNumOperands()-1);
}

static bool isLoadAbsSet(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::L4_loadrd_ap:
    case Hexagon::L4_loadrb_ap:
    case Hexagon::L4_loadrh_ap:
    case Hexagon::L4_loadrub_ap:
    case Hexagon::L4_loadruh_ap:
    case Hexagon::L4_loadri_ap:
      return true;
  }
  return false;
}

static const MachineOperand &getAbsSetOperand(const MachineInstr &MI) {
  assert(isLoadAbsSet(MI));
  return MI.getOperand(1);
}

// Can be new value store?
// Following restrictions are to be respected in convert a store into
// a new value store.
// 1. If an instruction uses auto-increment, its address register cannot
//    be a new-value register. Arch Spec 5.4.2.1
// 2. If an instruction uses absolute-set addressing mode, its address
//    register cannot be a new-value register. Arch Spec 5.4.2.1.
// 3. If an instruction produces a 64-bit result, its registers cannot be used
//    as new-value registers. Arch Spec 5.4.2.2.
// 4. If the instruction that sets the new-value register is conditional, then
//    the instruction that uses the new-value register must also be conditional,
//    and both must always have their predicates evaluate identically.
//    Arch Spec 5.4.2.3.
// 5. There is an implied restriction that a packet cannot have another store,
//    if there is a new value store in the packet. Corollary: if there is
//    already a store in a packet, there can not be a new value store.
//    Arch Spec: 3.4.4.2
bool HexagonPacketizerList::canPromoteToNewValueStore(const MachineInstr &MI,
      const MachineInstr &PacketMI, unsigned DepReg) {
  // Make sure we are looking at the store, that can be promoted.
  if (!HII->mayBeNewStore(MI))
    return false;

  // Make sure there is dependency and can be new value'd.
  const MachineOperand &Val = getStoreValueOperand(MI);
  if (Val.isReg() && Val.getReg() != DepReg)
    return false;

  const MCInstrDesc& MCID = PacketMI.getDesc();

  // First operand is always the result.
  const TargetRegisterClass *PacketRC = HII->getRegClass(MCID, 0, HRI, MF);
  // Double regs can not feed into new value store: PRM section: 5.4.2.2.
  if (PacketRC == &Hexagon::DoubleRegsRegClass)
    return false;

  // New-value stores are of class NV (slot 0), dual stores require class ST
  // in slot 0 (PRM 5.5).
  for (auto I : CurrentPacketMIs) {
    SUnit *PacketSU = MIToSUnit.find(I)->second;
    if (PacketSU->getInstr()->mayStore())
      return false;
  }

  // Make sure it's NOT the post increment register that we are going to
  // new value.
  if (HII->isPostIncrement(MI) &&
      getPostIncrementOperand(MI, HII).getReg() == DepReg) {
    return false;
  }

  if (HII->isPostIncrement(PacketMI) && PacketMI.mayLoad() &&
      getPostIncrementOperand(PacketMI, HII).getReg() == DepReg) {
    // If source is post_inc, or absolute-set addressing, it can not feed
    // into new value store
    //   r3 = memw(r2++#4)
    //   memw(r30 + #-1404) = r2.new -> can not be new value store
    // arch spec section: 5.4.2.1.
    return false;
  }

  if (isLoadAbsSet(PacketMI) && getAbsSetOperand(PacketMI).getReg() == DepReg)
    return false;

  // If the source that feeds the store is predicated, new value store must
  // also be predicated.
  if (HII->isPredicated(PacketMI)) {
    if (!HII->isPredicated(MI))
      return false;

    // Check to make sure that they both will have their predicates
    // evaluate identically.
    unsigned predRegNumSrc = 0;
    unsigned predRegNumDst = 0;
    const TargetRegisterClass* predRegClass = nullptr;

    // Get predicate register used in the source instruction.
    for (auto &MO : PacketMI.operands()) {
      if (!MO.isReg())
        continue;
      predRegNumSrc = MO.getReg();
      predRegClass = HRI->getMinimalPhysRegClass(predRegNumSrc);
      if (predRegClass == &Hexagon::PredRegsRegClass)
        break;
    }
    assert((predRegClass == &Hexagon::PredRegsRegClass) &&
        "predicate register not found in a predicated PacketMI instruction");

    // Get predicate register used in new-value store instruction.
    for (auto &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      predRegNumDst = MO.getReg();
      predRegClass = HRI->getMinimalPhysRegClass(predRegNumDst);
      if (predRegClass == &Hexagon::PredRegsRegClass)
        break;
    }
    assert((predRegClass == &Hexagon::PredRegsRegClass) &&
           "predicate register not found in a predicated MI instruction");

    // New-value register producer and user (store) need to satisfy these
    // constraints:
    // 1) Both instructions should be predicated on the same register.
    // 2) If producer of the new-value register is .new predicated then store
    // should also be .new predicated and if producer is not .new predicated
    // then store should not be .new predicated.
    // 3) Both new-value register producer and user should have same predicate
    // sense, i.e, either both should be negated or both should be non-negated.
    if (predRegNumDst != predRegNumSrc ||
        HII->isDotNewInst(PacketMI) != HII->isDotNewInst(MI) ||
        getPredicateSense(MI, HII) != getPredicateSense(PacketMI, HII))
      return false;
  }

  // Make sure that other than the new-value register no other store instruction
  // register has been modified in the same packet. Predicate registers can be
  // modified by they should not be modified between the producer and the store
  // instruction as it will make them both conditional on different values.
  // We already know this to be true for all the instructions before and
  // including PacketMI. Howerver, we need to perform the check for the
  // remaining instructions in the packet.

  unsigned StartCheck = 0;

  for (auto I : CurrentPacketMIs) {
    SUnit *TempSU = MIToSUnit.find(I)->second;
    MachineInstr &TempMI = *TempSU->getInstr();

    // Following condition is true for all the instructions until PacketMI is
    // reached (StartCheck is set to 0 before the for loop).
    // StartCheck flag is 1 for all the instructions after PacketMI.
    if (&TempMI != &PacketMI && !StartCheck) // Start processing only after
      continue;                              // encountering PacketMI.

    StartCheck = 1;
    if (&TempMI == &PacketMI) // We don't want to check PacketMI for dependence.
      continue;

    for (auto &MO : MI.operands())
      if (MO.isReg() && TempSU->getInstr()->modifiesRegister(MO.getReg(), HRI))
        return false;
  }

  // Make sure that for non-POST_INC stores:
  // 1. The only use of reg is DepReg and no other registers.
  //    This handles base+index registers.
  //    The following store can not be dot new.
  //    Eg.   r0 = add(r0, #3)
  //          memw(r1+r0<<#2) = r0
  if (!HII->isPostIncrement(MI)) {
    for (unsigned opNum = 0; opNum < MI.getNumOperands()-1; opNum++) {
      const MachineOperand &MO = MI.getOperand(opNum);
      if (MO.isReg() && MO.getReg() == DepReg)
        return false;
    }
  }

  // If data definition is because of implicit definition of the register,
  // do not newify the store. Eg.
  // %r9 = ZXTH %r12, implicit %d6, implicit-def %r12
  // S2_storerh_io %r8, 2, killed %r12; mem:ST2[%scevgep343]
  for (auto &MO : PacketMI.operands()) {
    if (MO.isRegMask() && MO.clobbersPhysReg(DepReg))
      return false;
    if (!MO.isReg() || !MO.isDef() || !MO.isImplicit())
      continue;
    unsigned R = MO.getReg();
    if (R == DepReg || HRI->isSuperRegister(DepReg, R))
      return false;
  }

  // Handle imp-use of super reg case. There is a target independent side
  // change that should prevent this situation but I am handling it for
  // just-in-case. For example, we cannot newify R2 in the following case:
  // %r3 = A2_tfrsi 0;
  // S2_storeri_io killed %r0, 0, killed %r2, implicit killed %d1;
  for (auto &MO : MI.operands()) {
    if (MO.isReg() && MO.isUse() && MO.isImplicit() && MO.getReg() == DepReg)
      return false;
  }

  // Can be dot new store.
  return true;
}

// Can this MI to promoted to either new value store or new value jump.
bool HexagonPacketizerList::canPromoteToNewValue(const MachineInstr &MI,
      const SUnit *PacketSU, unsigned DepReg,
      MachineBasicBlock::iterator &MII) {
  if (!HII->mayBeNewStore(MI))
    return false;

  // Check to see the store can be new value'ed.
  MachineInstr &PacketMI = *PacketSU->getInstr();
  if (canPromoteToNewValueStore(MI, PacketMI, DepReg))
    return true;

  // Check to see the compare/jump can be new value'ed.
  // This is done as a pass on its own. Don't need to check it here.
  return false;
}

static bool isImplicitDependency(const MachineInstr &I, bool CheckDef,
      unsigned DepReg) {
  for (auto &MO : I.operands()) {
    if (CheckDef && MO.isRegMask() && MO.clobbersPhysReg(DepReg))
      return true;
    if (!MO.isReg() || MO.getReg() != DepReg || !MO.isImplicit())
      continue;
    if (CheckDef == MO.isDef())
      return true;
  }
  return false;
}

// Check to see if an instruction can be dot new.
bool HexagonPacketizerList::canPromoteToDotNew(const MachineInstr &MI,
      const SUnit *PacketSU, unsigned DepReg, MachineBasicBlock::iterator &MII,
      const TargetRegisterClass* RC) {
  // Already a dot new instruction.
  if (HII->isDotNewInst(MI) && !HII->mayBeNewStore(MI))
    return false;

  if (!isNewifiable(MI, RC))
    return false;

  const MachineInstr &PI = *PacketSU->getInstr();

  // The "new value" cannot come from inline asm.
  if (PI.isInlineAsm())
    return false;

  // IMPLICIT_DEFs won't materialize as real instructions, so .new makes no
  // sense.
  if (PI.isImplicitDef())
    return false;

  // If dependency is trough an implicitly defined register, we should not
  // newify the use.
  if (isImplicitDependency(PI, true, DepReg) ||
      isImplicitDependency(MI, false, DepReg))
    return false;

  const MCInstrDesc& MCID = PI.getDesc();
  const TargetRegisterClass *VecRC = HII->getRegClass(MCID, 0, HRI, MF);
  if (DisableVecDblNVStores && VecRC == &Hexagon::HvxWRRegClass)
    return false;

  // predicate .new
  if (RC == &Hexagon::PredRegsRegClass)
    return HII->predCanBeUsedAsDotNew(PI, DepReg);

  if (RC != &Hexagon::PredRegsRegClass && !HII->mayBeNewStore(MI))
    return false;

  // Create a dot new machine instruction to see if resources can be
  // allocated. If not, bail out now.
  int NewOpcode = HII->getDotNewOp(MI);
  const MCInstrDesc &D = HII->get(NewOpcode);
  MachineInstr *NewMI = MF.CreateMachineInstr(D, DebugLoc());
  bool ResourcesAvailable = ResourceTracker->canReserveResources(*NewMI);
  MF.DeleteMachineInstr(NewMI);
  if (!ResourcesAvailable)
    return false;

  // New Value Store only. New Value Jump generated as a separate pass.
  if (!canPromoteToNewValue(MI, PacketSU, DepReg, MII))
    return false;

  return true;
}

// Go through the packet instructions and search for an anti dependency between
// them and DepReg from MI. Consider this case:
// Trying to add
// a) %r1 = TFRI_cdNotPt %p3, 2
// to this packet:
// {
//   b) %p0 = C2_or killed %p3, killed %p0
//   c) %p3 = C2_tfrrp %r23
//   d) %r1 = C2_cmovenewit %p3, 4
//  }
// The P3 from a) and d) will be complements after
// a)'s P3 is converted to .new form
// Anti-dep between c) and b) is irrelevant for this case
bool HexagonPacketizerList::restrictingDepExistInPacket(MachineInstr &MI,
                                                        unsigned DepReg) {
  SUnit *PacketSUDep = MIToSUnit.find(&MI)->second;

  for (auto I : CurrentPacketMIs) {
    // We only care for dependencies to predicated instructions
    if (!HII->isPredicated(*I))
      continue;

    // Scheduling Unit for current insn in the packet
    SUnit *PacketSU = MIToSUnit.find(I)->second;

    // Look at dependencies between current members of the packet and
    // predicate defining instruction MI. Make sure that dependency is
    // on the exact register we care about.
    if (PacketSU->isSucc(PacketSUDep)) {
      for (unsigned i = 0; i < PacketSU->Succs.size(); ++i) {
        auto &Dep = PacketSU->Succs[i];
        if (Dep.getSUnit() == PacketSUDep && Dep.getKind() == SDep::Anti &&
            Dep.getReg() == DepReg)
          return true;
      }
    }
  }

  return false;
}

/// Gets the predicate register of a predicated instruction.
static unsigned getPredicatedRegister(MachineInstr &MI,
                                      const HexagonInstrInfo *QII) {
  /// We use the following rule: The first predicate register that is a use is
  /// the predicate register of a predicated instruction.
  assert(QII->isPredicated(MI) && "Must be predicated instruction");

  for (auto &Op : MI.operands()) {
    if (Op.isReg() && Op.getReg() && Op.isUse() &&
        Hexagon::PredRegsRegClass.contains(Op.getReg()))
      return Op.getReg();
  }

  llvm_unreachable("Unknown instruction operand layout");
  return 0;
}

// Given two predicated instructions, this function detects whether
// the predicates are complements.
bool HexagonPacketizerList::arePredicatesComplements(MachineInstr &MI1,
                                                     MachineInstr &MI2) {
  // If we don't know the predicate sense of the instructions bail out early, we
  // need it later.
  if (getPredicateSense(MI1, HII) == PK_Unknown ||
      getPredicateSense(MI2, HII) == PK_Unknown)
    return false;

  // Scheduling unit for candidate.
  SUnit *SU = MIToSUnit[&MI1];

  // One corner case deals with the following scenario:
  // Trying to add
  // a) %r24 = A2_tfrt %p0, %r25
  // to this packet:
  // {
  //   b) %r25 = A2_tfrf %p0, %r24
  //   c) %p0 = C2_cmpeqi %r26, 1
  // }
  //
  // On general check a) and b) are complements, but presence of c) will
  // convert a) to .new form, and then it is not a complement.
  // We attempt to detect it by analyzing existing dependencies in the packet.

  // Analyze relationships between all existing members of the packet.
  // Look for Anti dependecy on the same predicate reg as used in the
  // candidate.
  for (auto I : CurrentPacketMIs) {
    // Scheduling Unit for current insn in the packet.
    SUnit *PacketSU = MIToSUnit.find(I)->second;

    // If this instruction in the packet is succeeded by the candidate...
    if (PacketSU->isSucc(SU)) {
      for (unsigned i = 0; i < PacketSU->Succs.size(); ++i) {
        auto Dep = PacketSU->Succs[i];
        // The corner case exist when there is true data dependency between
        // candidate and one of current packet members, this dep is on
        // predicate reg, and there already exist anti dep on the same pred in
        // the packet.
        if (Dep.getSUnit() == SU && Dep.getKind() == SDep::Data &&
            Hexagon::PredRegsRegClass.contains(Dep.getReg())) {
          // Here I know that I is predicate setting instruction with true
          // data dep to candidate on the register we care about - c) in the
          // above example. Now I need to see if there is an anti dependency
          // from c) to any other instruction in the same packet on the pred
          // reg of interest.
          if (restrictingDepExistInPacket(*I, Dep.getReg()))
            return false;
        }
      }
    }
  }

  // If the above case does not apply, check regular complement condition.
  // Check that the predicate register is the same and that the predicate
  // sense is different We also need to differentiate .old vs. .new: !p0
  // is not complementary to p0.new.
  unsigned PReg1 = getPredicatedRegister(MI1, HII);
  unsigned PReg2 = getPredicatedRegister(MI2, HII);
  return PReg1 == PReg2 &&
         Hexagon::PredRegsRegClass.contains(PReg1) &&
         Hexagon::PredRegsRegClass.contains(PReg2) &&
         getPredicateSense(MI1, HII) != getPredicateSense(MI2, HII) &&
         HII->isDotNewInst(MI1) == HII->isDotNewInst(MI2);
}

// Initialize packetizer flags.
void HexagonPacketizerList::initPacketizerState() {
  Dependence = false;
  PromotedToDotNew = false;
  GlueToNewValueJump = false;
  GlueAllocframeStore = false;
  FoundSequentialDependence = false;
  ChangedOffset = INT64_MAX;
}

// Ignore bundling of pseudo instructions.
bool HexagonPacketizerList::ignorePseudoInstruction(const MachineInstr &MI,
                                                    const MachineBasicBlock *) {
  if (MI.isDebugInstr())
    return true;

  if (MI.isCFIInstruction())
    return false;

  // We must print out inline assembly.
  if (MI.isInlineAsm())
    return false;

  if (MI.isImplicitDef())
    return false;

  // We check if MI has any functional units mapped to it. If it doesn't,
  // we ignore the instruction.
  const MCInstrDesc& TID = MI.getDesc();
  auto *IS = ResourceTracker->getInstrItins()->beginStage(TID.getSchedClass());
  unsigned FuncUnits = IS->getUnits();
  return !FuncUnits;
}

bool HexagonPacketizerList::isSoloInstruction(const MachineInstr &MI) {
  // Ensure any bundles created by gather packetize remain seperate.
  if (MI.isBundle())
    return true;

  if (MI.isEHLabel() || MI.isCFIInstruction())
    return true;

  // Consider inline asm to not be a solo instruction by default.
  // Inline asm will be put in a packet temporarily, but then it will be
  // removed, and placed outside of the packet (before or after, depending
  // on dependencies).  This is to reduce the impact of inline asm as a
  // "packet splitting" instruction.
  if (MI.isInlineAsm() && !ScheduleInlineAsm)
    return true;

  if (isSchedBarrier(MI))
    return true;

  if (HII->isSolo(MI))
    return true;

  if (MI.getOpcode() == Hexagon::A2_nop)
    return true;

  return false;
}

// Quick check if instructions MI and MJ cannot coexist in the same packet.
// Limit the tests to be "one-way", e.g.  "if MI->isBranch and MJ->isInlineAsm",
// but not the symmetric case: "if MJ->isBranch and MI->isInlineAsm".
// For full test call this function twice:
//   cannotCoexistAsymm(MI, MJ) || cannotCoexistAsymm(MJ, MI)
// Doing the test only one way saves the amount of code in this function,
// since every test would need to be repeated with the MI and MJ reversed.
static bool cannotCoexistAsymm(const MachineInstr &MI, const MachineInstr &MJ,
      const HexagonInstrInfo &HII) {
  const MachineFunction *MF = MI.getParent()->getParent();
  if (MF->getSubtarget<HexagonSubtarget>().hasV60OpsOnly() &&
      HII.isHVXMemWithAIndirect(MI, MJ))
    return true;

  // An inline asm cannot be together with a branch, because we may not be
  // able to remove the asm out after packetizing (i.e. if the asm must be
  // moved past the bundle).  Similarly, two asms cannot be together to avoid
  // complications when determining their relative order outside of a bundle.
  if (MI.isInlineAsm())
    return MJ.isInlineAsm() || MJ.isBranch() || MJ.isBarrier() ||
           MJ.isCall() || MJ.isTerminator();

  // New-value stores cannot coexist with any other stores.
  if (HII.isNewValueStore(MI) && MJ.mayStore())
    return true;

  switch (MI.getOpcode()) {
  case Hexagon::S2_storew_locked:
  case Hexagon::S4_stored_locked:
  case Hexagon::L2_loadw_locked:
  case Hexagon::L4_loadd_locked:
  case Hexagon::Y2_dccleana:
  case Hexagon::Y2_dccleaninva:
  case Hexagon::Y2_dcinva:
  case Hexagon::Y2_dczeroa:
  case Hexagon::Y4_l2fetch:
  case Hexagon::Y5_l2fetch: {
    // These instructions can only be grouped with ALU32 or non-floating-point
    // XTYPE instructions.  Since there is no convenient way of identifying fp
    // XTYPE instructions, only allow grouping with ALU32 for now.
    unsigned TJ = HII.getType(MJ);
    if (TJ != HexagonII::TypeALU32_2op &&
        TJ != HexagonII::TypeALU32_3op &&
        TJ != HexagonII::TypeALU32_ADDI)
      return true;
    break;
  }
  default:
    break;
  }

  // "False" really means that the quick check failed to determine if
  // I and J cannot coexist.
  return false;
}

// Full, symmetric check.
bool HexagonPacketizerList::cannotCoexist(const MachineInstr &MI,
      const MachineInstr &MJ) {
  return cannotCoexistAsymm(MI, MJ, *HII) || cannotCoexistAsymm(MJ, MI, *HII);
}

void HexagonPacketizerList::unpacketizeSoloInstrs(MachineFunction &MF) {
  for (auto &B : MF) {
    MachineBasicBlock::iterator BundleIt;
    MachineBasicBlock::instr_iterator NextI;
    for (auto I = B.instr_begin(), E = B.instr_end(); I != E; I = NextI) {
      NextI = std::next(I);
      MachineInstr &MI = *I;
      if (MI.isBundle())
        BundleIt = I;
      if (!MI.isInsideBundle())
        continue;

      // Decide on where to insert the instruction that we are pulling out.
      // Debug instructions always go before the bundle, but the placement of
      // INLINE_ASM depends on potential dependencies.  By default, try to
      // put it before the bundle, but if the asm writes to a register that
      // other instructions in the bundle read, then we need to place it
      // after the bundle (to preserve the bundle semantics).
      bool InsertBeforeBundle;
      if (MI.isInlineAsm())
        InsertBeforeBundle = !hasWriteToReadDep(MI, *BundleIt, HRI);
      else if (MI.isDebugValue())
        InsertBeforeBundle = true;
      else
        continue;

      BundleIt = moveInstrOut(MI, BundleIt, InsertBeforeBundle);
    }
  }
}

// Check if a given instruction is of class "system".
static bool isSystemInstr(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::Y2_barrier:
    case Hexagon::Y2_dcfetchbo:
    case Hexagon::Y4_l2fetch:
    case Hexagon::Y5_l2fetch:
      return true;
  }
  return false;
}

bool HexagonPacketizerList::hasDeadDependence(const MachineInstr &I,
                                              const MachineInstr &J) {
  // The dependence graph may not include edges between dead definitions,
  // so without extra checks, we could end up packetizing two instruction
  // defining the same (dead) register.
  if (I.isCall() || J.isCall())
    return false;
  if (HII->isPredicated(I) || HII->isPredicated(J))
    return false;

  BitVector DeadDefs(Hexagon::NUM_TARGET_REGS);
  for (auto &MO : I.operands()) {
    if (!MO.isReg() || !MO.isDef() || !MO.isDead())
      continue;
    DeadDefs[MO.getReg()] = true;
  }

  for (auto &MO : J.operands()) {
    if (!MO.isReg() || !MO.isDef() || !MO.isDead())
      continue;
    unsigned R = MO.getReg();
    if (R != Hexagon::USR_OVF && DeadDefs[R])
      return true;
  }
  return false;
}

bool HexagonPacketizerList::hasControlDependence(const MachineInstr &I,
                                                 const MachineInstr &J) {
  // A save callee-save register function call can only be in a packet
  // with instructions that don't write to the callee-save registers.
  if ((HII->isSaveCalleeSavedRegsCall(I) &&
       doesModifyCalleeSavedReg(J, HRI)) ||
      (HII->isSaveCalleeSavedRegsCall(J) &&
       doesModifyCalleeSavedReg(I, HRI)))
    return true;

  // Two control flow instructions cannot go in the same packet.
  if (isControlFlow(I) && isControlFlow(J))
    return true;

  // \ref-manual (7.3.4) A loop setup packet in loopN or spNloop0 cannot
  // contain a speculative indirect jump,
  // a new-value compare jump or a dealloc_return.
  auto isBadForLoopN = [this] (const MachineInstr &MI) -> bool {
    if (MI.isCall() || HII->isDeallocRet(MI) || HII->isNewValueJump(MI))
      return true;
    if (HII->isPredicated(MI) && HII->isPredicatedNew(MI) && HII->isJumpR(MI))
      return true;
    return false;
  };

  if (HII->isLoopN(I) && isBadForLoopN(J))
    return true;
  if (HII->isLoopN(J) && isBadForLoopN(I))
    return true;

  // dealloc_return cannot appear in the same packet as a conditional or
  // unconditional jump.
  return HII->isDeallocRet(I) &&
         (J.isBranch() || J.isCall() || J.isBarrier());
}

bool HexagonPacketizerList::hasRegMaskDependence(const MachineInstr &I,
                                                 const MachineInstr &J) {
  // Adding I to a packet that has J.

  // Regmasks are not reflected in the scheduling dependency graph, so
  // we need to check them manually. This code assumes that regmasks only
  // occur on calls, and the problematic case is when we add an instruction
  // defining a register R to a packet that has a call that clobbers R via
  // a regmask. Those cannot be packetized together, because the call will
  // be executed last. That's also a reson why it is ok to add a call
  // clobbering R to a packet that defines R.

  // Look for regmasks in J.
  for (const MachineOperand &OpJ : J.operands()) {
    if (!OpJ.isRegMask())
      continue;
    assert((J.isCall() || HII->isTailCall(J)) && "Regmask on a non-call");
    for (const MachineOperand &OpI : I.operands()) {
      if (OpI.isReg()) {
        if (OpJ.clobbersPhysReg(OpI.getReg()))
          return true;
      } else if (OpI.isRegMask()) {
        // Both are regmasks. Assume that they intersect.
        return true;
      }
    }
  }
  return false;
}

bool HexagonPacketizerList::hasDualStoreDependence(const MachineInstr &I,
                                                   const MachineInstr &J) {
  bool SysI = isSystemInstr(I), SysJ = isSystemInstr(J);
  bool StoreI = I.mayStore(), StoreJ = J.mayStore();
  if ((SysI && StoreJ) || (SysJ && StoreI))
    return true;

  if (StoreI && StoreJ) {
    if (HII->isNewValueInst(J) || HII->isMemOp(J) || HII->isMemOp(I))
      return true;
  } else {
    // A memop cannot be in the same packet with another memop or a store.
    // Two stores can be together, but here I and J cannot both be stores.
    bool MopStI = HII->isMemOp(I) || StoreI;
    bool MopStJ = HII->isMemOp(J) || StoreJ;
    if (MopStI && MopStJ)
      return true;
  }

  return (StoreJ && HII->isDeallocRet(I)) || (StoreI && HII->isDeallocRet(J));
}

// SUI is the current instruction that is out side of the current packet.
// SUJ is the current instruction inside the current packet against which that
// SUI will be packetized.
bool HexagonPacketizerList::isLegalToPacketizeTogether(SUnit *SUI, SUnit *SUJ) {
  assert(SUI->getInstr() && SUJ->getInstr());
  MachineInstr &I = *SUI->getInstr();
  MachineInstr &J = *SUJ->getInstr();

  // Clear IgnoreDepMIs when Packet starts.
  if (CurrentPacketMIs.size() == 1)
    IgnoreDepMIs.clear();

  MachineBasicBlock::iterator II = I.getIterator();

  // Solo instructions cannot go in the packet.
  assert(!isSoloInstruction(I) && "Unexpected solo instr!");

  if (cannotCoexist(I, J))
    return false;

  Dependence = hasDeadDependence(I, J) || hasControlDependence(I, J);
  if (Dependence)
    return false;

  // Regmasks are not accounted for in the scheduling graph, so we need
  // to explicitly check for dependencies caused by them. They should only
  // appear on calls, so it's not too pessimistic to reject all regmask
  // dependencies.
  Dependence = hasRegMaskDependence(I, J);
  if (Dependence)
    return false;

  // Dual-store does not allow second store, if the first store is not
  // in SLOT0. New value store, new value jump, dealloc_return and memop
  // always take SLOT0. Arch spec 3.4.4.2.
  Dependence = hasDualStoreDependence(I, J);
  if (Dependence)
    return false;

  // If an instruction feeds new value jump, glue it.
  MachineBasicBlock::iterator NextMII = I.getIterator();
  ++NextMII;
  if (NextMII != I.getParent()->end() && HII->isNewValueJump(*NextMII)) {
    MachineInstr &NextMI = *NextMII;

    bool secondRegMatch = false;
    const MachineOperand &NOp0 = NextMI.getOperand(0);
    const MachineOperand &NOp1 = NextMI.getOperand(1);

    if (NOp1.isReg() && I.getOperand(0).getReg() == NOp1.getReg())
      secondRegMatch = true;

    for (MachineInstr *PI : CurrentPacketMIs) {
      // NVJ can not be part of the dual jump - Arch Spec: section 7.8.
      if (PI->isCall()) {
        Dependence = true;
        break;
      }
      // Validate:
      // 1. Packet does not have a store in it.
      // 2. If the first operand of the nvj is newified, and the second
      //    operand is also a reg, it (second reg) is not defined in
      //    the same packet.
      // 3. If the second operand of the nvj is newified, (which means
      //    first operand is also a reg), first reg is not defined in
      //    the same packet.
      if (PI->getOpcode() == Hexagon::S2_allocframe || PI->mayStore() ||
          HII->isLoopN(*PI)) {
        Dependence = true;
        break;
      }
      // Check #2/#3.
      const MachineOperand &OpR = secondRegMatch ? NOp0 : NOp1;
      if (OpR.isReg() && PI->modifiesRegister(OpR.getReg(), HRI)) {
        Dependence = true;
        break;
      }
    }

    GlueToNewValueJump = true;
    if (Dependence)
      return false;
  }

  // There no dependency between a prolog instruction and its successor.
  if (!SUJ->isSucc(SUI))
    return true;

  for (unsigned i = 0; i < SUJ->Succs.size(); ++i) {
    if (FoundSequentialDependence)
      break;

    if (SUJ->Succs[i].getSUnit() != SUI)
      continue;

    SDep::Kind DepType = SUJ->Succs[i].getKind();
    // For direct calls:
    // Ignore register dependences for call instructions for packetization
    // purposes except for those due to r31 and predicate registers.
    //
    // For indirect calls:
    // Same as direct calls + check for true dependences to the register
    // used in the indirect call.
    //
    // We completely ignore Order dependences for call instructions.
    //
    // For returns:
    // Ignore register dependences for return instructions like jumpr,
    // dealloc return unless we have dependencies on the explicit uses
    // of the registers used by jumpr (like r31) or dealloc return
    // (like r29 or r30).
    unsigned DepReg = 0;
    const TargetRegisterClass *RC = nullptr;
    if (DepType == SDep::Data) {
      DepReg = SUJ->Succs[i].getReg();
      RC = HRI->getMinimalPhysRegClass(DepReg);
    }

    if (I.isCall() || HII->isJumpR(I) || I.isReturn() || HII->isTailCall(I)) {
      if (!isRegDependence(DepType))
        continue;
      if (!isCallDependent(I, DepType, SUJ->Succs[i].getReg()))
        continue;
    }

    if (DepType == SDep::Data) {
      if (canPromoteToDotCur(J, SUJ, DepReg, II, RC))
        if (promoteToDotCur(J, DepType, II, RC))
          continue;
    }

    // Data dpendence ok if we have load.cur.
    if (DepType == SDep::Data && HII->isDotCurInst(J)) {
      if (HII->isHVXVec(I))
        continue;
    }

    // For instructions that can be promoted to dot-new, try to promote.
    if (DepType == SDep::Data) {
      if (canPromoteToDotNew(I, SUJ, DepReg, II, RC)) {
        if (promoteToDotNew(I, DepType, II, RC)) {
          PromotedToDotNew = true;
          if (cannotCoexist(I, J))
            FoundSequentialDependence = true;
          continue;
        }
      }
      if (HII->isNewValueJump(I))
        continue;
    }

    // For predicated instructions, if the predicates are complements then
    // there can be no dependence.
    if (HII->isPredicated(I) && HII->isPredicated(J) &&
        arePredicatesComplements(I, J)) {
      // Not always safe to do this translation.
      // DAG Builder attempts to reduce dependence edges using transitive
      // nature of dependencies. Here is an example:
      //
      // r0 = tfr_pt ... (1)
      // r0 = tfr_pf ... (2)
      // r0 = tfr_pt ... (3)
      //
      // There will be an output dependence between (1)->(2) and (2)->(3).
      // However, there is no dependence edge between (1)->(3). This results
      // in all 3 instructions going in the same packet. We ignore dependce
      // only once to avoid this situation.
      auto Itr = find(IgnoreDepMIs, &J);
      if (Itr != IgnoreDepMIs.end()) {
        Dependence = true;
        return false;
      }
      IgnoreDepMIs.push_back(&I);
      continue;
    }

    // Ignore Order dependences between unconditional direct branches
    // and non-control-flow instructions.
    if (isDirectJump(I) && !J.isBranch() && !J.isCall() &&
        DepType == SDep::Order)
      continue;

    // Ignore all dependences for jumps except for true and output
    // dependences.
    if (I.isConditionalBranch() && DepType != SDep::Data &&
        DepType != SDep::Output)
      continue;

    if (DepType == SDep::Output) {
      FoundSequentialDependence = true;
      break;
    }

    // For Order dependences:
    // 1. Volatile loads/stores can be packetized together, unless other
    //    rules prevent is.
    // 2. Store followed by a load is not allowed.
    // 3. Store followed by a store is valid.
    // 4. Load followed by any memory operation is allowed.
    if (DepType == SDep::Order) {
      if (!PacketizeVolatiles) {
        bool OrdRefs = I.hasOrderedMemoryRef() || J.hasOrderedMemoryRef();
        if (OrdRefs) {
          FoundSequentialDependence = true;
          break;
        }
      }
      // J is first, I is second.
      bool LoadJ = J.mayLoad(), StoreJ = J.mayStore();
      bool LoadI = I.mayLoad(), StoreI = I.mayStore();
      bool NVStoreJ = HII->isNewValueStore(J);
      bool NVStoreI = HII->isNewValueStore(I);
      bool IsVecJ = HII->isHVXVec(J);
      bool IsVecI = HII->isHVXVec(I);

      if (Slot1Store && MF.getSubtarget<HexagonSubtarget>().hasV65Ops() &&
          ((LoadJ && StoreI && !NVStoreI) ||
           (StoreJ && LoadI && !NVStoreJ)) &&
          (J.getOpcode() != Hexagon::S2_allocframe &&
           I.getOpcode() != Hexagon::S2_allocframe) &&
          (J.getOpcode() != Hexagon::L2_deallocframe &&
           I.getOpcode() != Hexagon::L2_deallocframe) &&
          (!HII->isMemOp(J) && !HII->isMemOp(I)) && (!IsVecJ && !IsVecI))
        setmemShufDisabled(true);
      else
        if (StoreJ && LoadI && alias(J, I)) {
          FoundSequentialDependence = true;
          break;
        }

      if (!StoreJ)
        if (!LoadJ || (!LoadI && !StoreI)) {
          // If J is neither load nor store, assume a dependency.
          // If J is a load, but I is neither, also assume a dependency.
          FoundSequentialDependence = true;
          break;
        }
      // Store followed by store: not OK on V2.
      // Store followed by load: not OK on all.
      // Load followed by store: OK on all.
      // Load followed by load: OK on all.
      continue;
    }

    // Special case for ALLOCFRAME: even though there is dependency
    // between ALLOCFRAME and subsequent store, allow it to be packetized
    // in a same packet. This implies that the store is using the caller's
    // SP. Hence, offset needs to be updated accordingly.
    if (DepType == SDep::Data && J.getOpcode() == Hexagon::S2_allocframe) {
      unsigned Opc = I.getOpcode();
      switch (Opc) {
        case Hexagon::S2_storerd_io:
        case Hexagon::S2_storeri_io:
        case Hexagon::S2_storerh_io:
        case Hexagon::S2_storerb_io:
          if (I.getOperand(0).getReg() == HRI->getStackRegister()) {
            // Since this store is to be glued with allocframe in the same
            // packet, it will use SP of the previous stack frame, i.e.
            // caller's SP. Therefore, we need to recalculate offset
            // according to this change.
            GlueAllocframeStore = useCallersSP(I);
            if (GlueAllocframeStore)
              continue;
          }
          break;
        default:
          break;
      }
    }

    // There are certain anti-dependencies that cannot be ignored.
    // Specifically:
    //   J2_call ... implicit-def %r0   ; SUJ
    //   R0 = ...                   ; SUI
    // Those cannot be packetized together, since the call will observe
    // the effect of the assignment to R0.
    if ((DepType == SDep::Anti || DepType == SDep::Output) && J.isCall()) {
      // Check if I defines any volatile register. We should also check
      // registers that the call may read, but these happen to be a
      // subset of the volatile register set.
      for (const MachineOperand &Op : I.operands()) {
        if (Op.isReg() && Op.isDef()) {
          unsigned R = Op.getReg();
          if (!J.readsRegister(R, HRI) && !J.modifiesRegister(R, HRI))
            continue;
        } else if (!Op.isRegMask()) {
          // If I has a regmask assume dependency.
          continue;
        }
        FoundSequentialDependence = true;
        break;
      }
    }

    // Skip over remaining anti-dependences. Two instructions that are
    // anti-dependent can share a packet, since in most such cases all
    // operands are read before any modifications take place.
    // The exceptions are branch and call instructions, since they are
    // executed after all other instructions have completed (at least
    // conceptually).
    if (DepType != SDep::Anti) {
      FoundSequentialDependence = true;
      break;
    }
  }

  if (FoundSequentialDependence) {
    Dependence = true;
    return false;
  }

  return true;
}

bool HexagonPacketizerList::isLegalToPruneDependencies(SUnit *SUI, SUnit *SUJ) {
  assert(SUI->getInstr() && SUJ->getInstr());
  MachineInstr &I = *SUI->getInstr();
  MachineInstr &J = *SUJ->getInstr();

  bool Coexist = !cannotCoexist(I, J);

  if (Coexist && !Dependence)
    return true;

  // Check if the instruction was promoted to a dot-new. If so, demote it
  // back into a dot-old.
  if (PromotedToDotNew)
    demoteToDotOld(I);

  cleanUpDotCur();
  // Check if the instruction (must be a store) was glued with an allocframe
  // instruction. If so, restore its offset to its original value, i.e. use
  // current SP instead of caller's SP.
  if (GlueAllocframeStore) {
    useCalleesSP(I);
    GlueAllocframeStore = false;
  }

  if (ChangedOffset != INT64_MAX)
    undoChangedOffset(I);

  if (GlueToNewValueJump) {
    // Putting I and J together would prevent the new-value jump from being
    // packetized with the producer. In that case I and J must be separated.
    GlueToNewValueJump = false;
    return false;
  }

  if (!Coexist)
    return false;

  if (ChangedOffset == INT64_MAX && updateOffset(SUI, SUJ)) {
    FoundSequentialDependence = false;
    Dependence = false;
    return true;
  }

  return false;
}


bool HexagonPacketizerList::foundLSInPacket() {
  bool FoundLoad = false;
  bool FoundStore = false;

  for (auto MJ : CurrentPacketMIs) {
    unsigned Opc = MJ->getOpcode();
    if (Opc == Hexagon::S2_allocframe || Opc == Hexagon::L2_deallocframe)
      continue;
    if (HII->isMemOp(*MJ))
      continue;
    if (MJ->mayLoad())
      FoundLoad = true;
    if (MJ->mayStore() && !HII->isNewValueStore(*MJ))
      FoundStore = true;
  }
  return FoundLoad && FoundStore;
}


MachineBasicBlock::iterator
HexagonPacketizerList::addToPacket(MachineInstr &MI) {
  MachineBasicBlock::iterator MII = MI.getIterator();
  MachineBasicBlock *MBB = MI.getParent();

  if (CurrentPacketMIs.empty())
    PacketStalls = false;
  PacketStalls |= producesStall(MI);

  if (MI.isImplicitDef()) {
    // Add to the packet to allow subsequent instructions to be checked
    // properly.
    CurrentPacketMIs.push_back(&MI);
    return MII;
  }
  assert(ResourceTracker->canReserveResources(MI));

  bool ExtMI = HII->isExtended(MI) || HII->isConstExtended(MI);
  bool Good = true;

  if (GlueToNewValueJump) {
    MachineInstr &NvjMI = *++MII;
    // We need to put both instructions in the same packet: MI and NvjMI.
    // Either of them can require a constant extender. Try to add both to
    // the current packet, and if that fails, end the packet and start a
    // new one.
    ResourceTracker->reserveResources(MI);
    if (ExtMI)
      Good = tryAllocateResourcesForConstExt(true);

    bool ExtNvjMI = HII->isExtended(NvjMI) || HII->isConstExtended(NvjMI);
    if (Good) {
      if (ResourceTracker->canReserveResources(NvjMI))
        ResourceTracker->reserveResources(NvjMI);
      else
        Good = false;
    }
    if (Good && ExtNvjMI)
      Good = tryAllocateResourcesForConstExt(true);

    if (!Good) {
      endPacket(MBB, MI);
      assert(ResourceTracker->canReserveResources(MI));
      ResourceTracker->reserveResources(MI);
      if (ExtMI) {
        assert(canReserveResourcesForConstExt());
        tryAllocateResourcesForConstExt(true);
      }
      assert(ResourceTracker->canReserveResources(NvjMI));
      ResourceTracker->reserveResources(NvjMI);
      if (ExtNvjMI) {
        assert(canReserveResourcesForConstExt());
        reserveResourcesForConstExt();
      }
    }
    CurrentPacketMIs.push_back(&MI);
    CurrentPacketMIs.push_back(&NvjMI);
    return MII;
  }

  ResourceTracker->reserveResources(MI);
  if (ExtMI && !tryAllocateResourcesForConstExt(true)) {
    endPacket(MBB, MI);
    if (PromotedToDotNew)
      demoteToDotOld(MI);
    if (GlueAllocframeStore) {
      useCalleesSP(MI);
      GlueAllocframeStore = false;
    }
    ResourceTracker->reserveResources(MI);
    reserveResourcesForConstExt();
  }

  CurrentPacketMIs.push_back(&MI);
  return MII;
}

void HexagonPacketizerList::endPacket(MachineBasicBlock *MBB,
                                      MachineBasicBlock::iterator EndMI) {
  // Replace VLIWPacketizerList::endPacket(MBB, EndMI).

  bool memShufDisabled = getmemShufDisabled();
  if (memShufDisabled && !foundLSInPacket()) {
    setmemShufDisabled(false);
    LLVM_DEBUG(dbgs() << "  Not added to NoShufPacket\n");
  }
  memShufDisabled = getmemShufDisabled();

  OldPacketMIs.clear();
  for (MachineInstr *MI : CurrentPacketMIs) {
    MachineBasicBlock::instr_iterator NextMI = std::next(MI->getIterator());
    for (auto &I : make_range(HII->expandVGatherPseudo(*MI), NextMI))
      OldPacketMIs.push_back(&I);
  }
  CurrentPacketMIs.clear();

  if (OldPacketMIs.size() > 1) {
    MachineBasicBlock::instr_iterator FirstMI(OldPacketMIs.front());
    MachineBasicBlock::instr_iterator LastMI(EndMI.getInstrIterator());
    finalizeBundle(*MBB, FirstMI, LastMI);
    auto BundleMII = std::prev(FirstMI);
    if (memShufDisabled)
      HII->setBundleNoShuf(BundleMII);

    setmemShufDisabled(false);
  }

  ResourceTracker->clearResources();
  LLVM_DEBUG(dbgs() << "End packet\n");
}

bool HexagonPacketizerList::shouldAddToPacket(const MachineInstr &MI) {
  if (Minimal)
    return false;
  return !producesStall(MI);
}

// V60 forward scheduling.
bool HexagonPacketizerList::producesStall(const MachineInstr &I) {
  // If the packet already stalls, then ignore the stall from a subsequent
  // instruction in the same packet.
  if (PacketStalls)
    return false;

  // Check whether the previous packet is in a different loop. If this is the
  // case, there is little point in trying to avoid a stall because that would
  // favor the rare case (loop entry) over the common case (loop iteration).
  //
  // TODO: We should really be able to check all the incoming edges if this is
  // the first packet in a basic block, so we can avoid stalls from the loop
  // backedge.
  if (!OldPacketMIs.empty()) {
    auto *OldBB = OldPacketMIs.front()->getParent();
    auto *ThisBB = I.getParent();
    if (MLI->getLoopFor(OldBB) != MLI->getLoopFor(ThisBB))
      return false;
  }

  SUnit *SUI = MIToSUnit[const_cast<MachineInstr *>(&I)];

  // If the latency is 0 and there is a data dependence between this
  // instruction and any instruction in the current packet, we disregard any
  // potential stalls due to the instructions in the previous packet. Most of
  // the instruction pairs that can go together in the same packet have 0
  // latency between them. The exceptions are
  // 1. NewValueJumps as they're generated much later and the latencies can't
  // be changed at that point.
  // 2. .cur instructions, if its consumer has a 0 latency successor (such as
  // .new). In this case, the latency between .cur and the consumer stays
  // non-zero even though we can have  both .cur and .new in the same packet.
  // Changing the latency to 0 is not an option as it causes software pipeliner
  // to not pipeline in some cases.

  // For Example:
  // {
  //   I1:  v6.cur = vmem(r0++#1)
  //   I2:  v7 = valign(v6,v4,r2)
  //   I3:  vmem(r5++#1) = v7.new
  // }
  // Here I2 and I3 has 0 cycle latency, but I1 and I2 has 2.

  for (auto J : CurrentPacketMIs) {
    SUnit *SUJ = MIToSUnit[J];
    for (auto &Pred : SUI->Preds)
      if (Pred.getSUnit() == SUJ)
        if ((Pred.getLatency() == 0 && Pred.isAssignedRegDep()) ||
            HII->isNewValueJump(I) || HII->isToBeScheduledASAP(*J, I))
          return false;
  }

  // Check if the latency is greater than one between this instruction and any
  // instruction in the previous packet.
  for (auto J : OldPacketMIs) {
    SUnit *SUJ = MIToSUnit[J];
    for (auto &Pred : SUI->Preds)
      if (Pred.getSUnit() == SUJ && Pred.getLatency() > 1)
        return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createHexagonPacketizer(bool Minimal) {
  return new HexagonPacketizer(Minimal);
}
