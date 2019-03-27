//===- llvm/CodeGen/VirtRegMap.cpp - Virtual Register Map -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the VirtRegMap class.
//
// It also contains implementations of the Spiller interface, which, given a
// virtual register map and a machine function, eliminates all virtual
// references by replacing them with physical register references - adding spill
// code as necessary.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/VirtRegMap.h"
#include "LiveDebugVariables.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumSpillSlots, "Number of spill slots allocated");
STATISTIC(NumIdCopies,   "Number of identity moves eliminated after rewriting");

//===----------------------------------------------------------------------===//
//  VirtRegMap implementation
//===----------------------------------------------------------------------===//

char VirtRegMap::ID = 0;

INITIALIZE_PASS(VirtRegMap, "virtregmap", "Virtual Register Map", false, false)

bool VirtRegMap::runOnMachineFunction(MachineFunction &mf) {
  MRI = &mf.getRegInfo();
  TII = mf.getSubtarget().getInstrInfo();
  TRI = mf.getSubtarget().getRegisterInfo();
  MF = &mf;

  Virt2PhysMap.clear();
  Virt2StackSlotMap.clear();
  Virt2SplitMap.clear();

  grow();
  return false;
}

void VirtRegMap::grow() {
  unsigned NumRegs = MF->getRegInfo().getNumVirtRegs();
  Virt2PhysMap.resize(NumRegs);
  Virt2StackSlotMap.resize(NumRegs);
  Virt2SplitMap.resize(NumRegs);
}

void VirtRegMap::assignVirt2Phys(unsigned virtReg, MCPhysReg physReg) {
  assert(TargetRegisterInfo::isVirtualRegister(virtReg) &&
         TargetRegisterInfo::isPhysicalRegister(physReg));
  assert(Virt2PhysMap[virtReg] == NO_PHYS_REG &&
         "attempt to assign physical register to already mapped "
         "virtual register");
  assert(!getRegInfo().isReserved(physReg) &&
         "Attempt to map virtReg to a reserved physReg");
  Virt2PhysMap[virtReg] = physReg;
}

unsigned VirtRegMap::createSpillSlot(const TargetRegisterClass *RC) {
  unsigned Size = TRI->getSpillSize(*RC);
  unsigned Align = TRI->getSpillAlignment(*RC);
  int SS = MF->getFrameInfo().CreateSpillStackObject(Size, Align);
  ++NumSpillSlots;
  return SS;
}

bool VirtRegMap::hasPreferredPhys(unsigned VirtReg) {
  unsigned Hint = MRI->getSimpleHint(VirtReg);
  if (!Hint)
    return false;
  if (TargetRegisterInfo::isVirtualRegister(Hint))
    Hint = getPhys(Hint);
  return getPhys(VirtReg) == Hint;
}

bool VirtRegMap::hasKnownPreference(unsigned VirtReg) {
  std::pair<unsigned, unsigned> Hint = MRI->getRegAllocationHint(VirtReg);
  if (TargetRegisterInfo::isPhysicalRegister(Hint.second))
    return true;
  if (TargetRegisterInfo::isVirtualRegister(Hint.second))
    return hasPhys(Hint.second);
  return false;
}

int VirtRegMap::assignVirt2StackSlot(unsigned virtReg) {
  assert(TargetRegisterInfo::isVirtualRegister(virtReg));
  assert(Virt2StackSlotMap[virtReg] == NO_STACK_SLOT &&
         "attempt to assign stack slot to already spilled register");
  const TargetRegisterClass* RC = MF->getRegInfo().getRegClass(virtReg);
  return Virt2StackSlotMap[virtReg] = createSpillSlot(RC);
}

void VirtRegMap::assignVirt2StackSlot(unsigned virtReg, int SS) {
  assert(TargetRegisterInfo::isVirtualRegister(virtReg));
  assert(Virt2StackSlotMap[virtReg] == NO_STACK_SLOT &&
         "attempt to assign stack slot to already spilled register");
  assert((SS >= 0 ||
          (SS >= MF->getFrameInfo().getObjectIndexBegin())) &&
         "illegal fixed frame index");
  Virt2StackSlotMap[virtReg] = SS;
}

void VirtRegMap::print(raw_ostream &OS, const Module*) const {
  OS << "********** REGISTER MAP **********\n";
  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (Virt2PhysMap[Reg] != (unsigned)VirtRegMap::NO_PHYS_REG) {
      OS << '[' << printReg(Reg, TRI) << " -> "
         << printReg(Virt2PhysMap[Reg], TRI) << "] "
         << TRI->getRegClassName(MRI->getRegClass(Reg)) << "\n";
    }
  }

  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (Virt2StackSlotMap[Reg] != VirtRegMap::NO_STACK_SLOT) {
      OS << '[' << printReg(Reg, TRI) << " -> fi#" << Virt2StackSlotMap[Reg]
         << "] " << TRI->getRegClassName(MRI->getRegClass(Reg)) << "\n";
    }
  }
  OS << '\n';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void VirtRegMap::dump() const {
  print(dbgs());
}
#endif

//===----------------------------------------------------------------------===//
//                              VirtRegRewriter
//===----------------------------------------------------------------------===//
//
// The VirtRegRewriter is the last of the register allocator passes.
// It rewrites virtual registers to physical registers as specified in the
// VirtRegMap analysis. It also updates live-in information on basic blocks
// according to LiveIntervals.
//
namespace {

class VirtRegRewriter : public MachineFunctionPass {
  MachineFunction *MF;
  const TargetRegisterInfo *TRI;
  const TargetInstrInfo *TII;
  MachineRegisterInfo *MRI;
  SlotIndexes *Indexes;
  LiveIntervals *LIS;
  VirtRegMap *VRM;

  void rewrite();
  void addMBBLiveIns();
  bool readsUndefSubreg(const MachineOperand &MO) const;
  void addLiveInsForSubRanges(const LiveInterval &LI, unsigned PhysReg) const;
  void handleIdentityCopy(MachineInstr &MI) const;
  void expandCopyBundle(MachineInstr &MI) const;
  bool subRegLiveThrough(const MachineInstr &MI, unsigned SuperPhysReg) const;

public:
  static char ID;

  VirtRegRewriter() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction&) override;

  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }
};

} // end anonymous namespace

char VirtRegRewriter::ID = 0;

char &llvm::VirtRegRewriterID = VirtRegRewriter::ID;

INITIALIZE_PASS_BEGIN(VirtRegRewriter, "virtregrewriter",
                      "Virtual Register Rewriter", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(LiveDebugVariables)
INITIALIZE_PASS_DEPENDENCY(LiveStacks)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_END(VirtRegRewriter, "virtregrewriter",
                    "Virtual Register Rewriter", false, false)

void VirtRegRewriter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<LiveIntervals>();
  AU.addRequired<SlotIndexes>();
  AU.addPreserved<SlotIndexes>();
  AU.addRequired<LiveDebugVariables>();
  AU.addRequired<LiveStacks>();
  AU.addPreserved<LiveStacks>();
  AU.addRequired<VirtRegMap>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool VirtRegRewriter::runOnMachineFunction(MachineFunction &fn) {
  MF = &fn;
  TRI = MF->getSubtarget().getRegisterInfo();
  TII = MF->getSubtarget().getInstrInfo();
  MRI = &MF->getRegInfo();
  Indexes = &getAnalysis<SlotIndexes>();
  LIS = &getAnalysis<LiveIntervals>();
  VRM = &getAnalysis<VirtRegMap>();
  LLVM_DEBUG(dbgs() << "********** REWRITE VIRTUAL REGISTERS **********\n"
                    << "********** Function: " << MF->getName() << '\n');
  LLVM_DEBUG(VRM->dump());

  // Add kill flags while we still have virtual registers.
  LIS->addKillFlags(VRM);

  // Live-in lists on basic blocks are required for physregs.
  addMBBLiveIns();

  // Rewrite virtual registers.
  rewrite();

  // Write out new DBG_VALUE instructions.
  getAnalysis<LiveDebugVariables>().emitDebugValues(VRM);

  // All machine operands and other references to virtual registers have been
  // replaced. Remove the virtual registers and release all the transient data.
  VRM->clearAllVirt();
  MRI->clearVirtRegs();
  return true;
}

void VirtRegRewriter::addLiveInsForSubRanges(const LiveInterval &LI,
                                             unsigned PhysReg) const {
  assert(!LI.empty());
  assert(LI.hasSubRanges());

  using SubRangeIteratorPair =
      std::pair<const LiveInterval::SubRange *, LiveInterval::const_iterator>;

  SmallVector<SubRangeIteratorPair, 4> SubRanges;
  SlotIndex First;
  SlotIndex Last;
  for (const LiveInterval::SubRange &SR : LI.subranges()) {
    SubRanges.push_back(std::make_pair(&SR, SR.begin()));
    if (!First.isValid() || SR.segments.front().start < First)
      First = SR.segments.front().start;
    if (!Last.isValid() || SR.segments.back().end > Last)
      Last = SR.segments.back().end;
  }

  // Check all mbb start positions between First and Last while
  // simulatenously advancing an iterator for each subrange.
  for (SlotIndexes::MBBIndexIterator MBBI = Indexes->findMBBIndex(First);
       MBBI != Indexes->MBBIndexEnd() && MBBI->first <= Last; ++MBBI) {
    SlotIndex MBBBegin = MBBI->first;
    // Advance all subrange iterators so that their end position is just
    // behind MBBBegin (or the iterator is at the end).
    LaneBitmask LaneMask;
    for (auto &RangeIterPair : SubRanges) {
      const LiveInterval::SubRange *SR = RangeIterPair.first;
      LiveInterval::const_iterator &SRI = RangeIterPair.second;
      while (SRI != SR->end() && SRI->end <= MBBBegin)
        ++SRI;
      if (SRI == SR->end())
        continue;
      if (SRI->start <= MBBBegin)
        LaneMask |= SR->LaneMask;
    }
    if (LaneMask.none())
      continue;
    MachineBasicBlock *MBB = MBBI->second;
    MBB->addLiveIn(PhysReg, LaneMask);
  }
}

// Compute MBB live-in lists from virtual register live ranges and their
// assignments.
void VirtRegRewriter::addMBBLiveIns() {
  for (unsigned Idx = 0, IdxE = MRI->getNumVirtRegs(); Idx != IdxE; ++Idx) {
    unsigned VirtReg = TargetRegisterInfo::index2VirtReg(Idx);
    if (MRI->reg_nodbg_empty(VirtReg))
      continue;
    LiveInterval &LI = LIS->getInterval(VirtReg);
    if (LI.empty() || LIS->intervalIsInOneMBB(LI))
      continue;
    // This is a virtual register that is live across basic blocks. Its
    // assigned PhysReg must be marked as live-in to those blocks.
    unsigned PhysReg = VRM->getPhys(VirtReg);
    assert(PhysReg != VirtRegMap::NO_PHYS_REG && "Unmapped virtual register.");

    if (LI.hasSubRanges()) {
      addLiveInsForSubRanges(LI, PhysReg);
    } else {
      // Go over MBB begin positions and see if we have segments covering them.
      // The following works because segments and the MBBIndex list are both
      // sorted by slot indexes.
      SlotIndexes::MBBIndexIterator I = Indexes->MBBIndexBegin();
      for (const auto &Seg : LI) {
        I = Indexes->advanceMBBIndex(I, Seg.start);
        for (; I != Indexes->MBBIndexEnd() && I->first < Seg.end; ++I) {
          MachineBasicBlock *MBB = I->second;
          MBB->addLiveIn(PhysReg);
        }
      }
    }
  }

  // Sort and unique MBB LiveIns as we've not checked if SubReg/PhysReg were in
  // each MBB's LiveIns set before calling addLiveIn on them.
  for (MachineBasicBlock &MBB : *MF)
    MBB.sortUniqueLiveIns();
}

/// Returns true if the given machine operand \p MO only reads undefined lanes.
/// The function only works for use operands with a subregister set.
bool VirtRegRewriter::readsUndefSubreg(const MachineOperand &MO) const {
  // Shortcut if the operand is already marked undef.
  if (MO.isUndef())
    return true;

  unsigned Reg = MO.getReg();
  const LiveInterval &LI = LIS->getInterval(Reg);
  const MachineInstr &MI = *MO.getParent();
  SlotIndex BaseIndex = LIS->getInstructionIndex(MI);
  // This code is only meant to handle reading undefined subregisters which
  // we couldn't properly detect before.
  assert(LI.liveAt(BaseIndex) &&
         "Reads of completely dead register should be marked undef already");
  unsigned SubRegIdx = MO.getSubReg();
  assert(SubRegIdx != 0 && LI.hasSubRanges());
  LaneBitmask UseMask = TRI->getSubRegIndexLaneMask(SubRegIdx);
  // See if any of the relevant subregister liveranges is defined at this point.
  for (const LiveInterval::SubRange &SR : LI.subranges()) {
    if ((SR.LaneMask & UseMask).any() && SR.liveAt(BaseIndex))
      return false;
  }
  return true;
}

void VirtRegRewriter::handleIdentityCopy(MachineInstr &MI) const {
  if (!MI.isIdentityCopy())
    return;
  LLVM_DEBUG(dbgs() << "Identity copy: " << MI);
  ++NumIdCopies;

  // Copies like:
  //    %r0 = COPY undef %r0
  //    %al = COPY %al, implicit-def %eax
  // give us additional liveness information: The target (super-)register
  // must not be valid before this point. Replace the COPY with a KILL
  // instruction to maintain this information.
  if (MI.getOperand(0).isUndef() || MI.getNumOperands() > 2) {
    MI.setDesc(TII->get(TargetOpcode::KILL));
    LLVM_DEBUG(dbgs() << "  replace by: " << MI);
    return;
  }

  if (Indexes)
    Indexes->removeSingleMachineInstrFromMaps(MI);
  MI.eraseFromBundle();
  LLVM_DEBUG(dbgs() << "  deleted.\n");
}

/// The liverange splitting logic sometimes produces bundles of copies when
/// subregisters are involved. Expand these into a sequence of copy instructions
/// after processing the last in the bundle. Does not update LiveIntervals
/// which we shouldn't need for this instruction anymore.
void VirtRegRewriter::expandCopyBundle(MachineInstr &MI) const {
  if (!MI.isCopy())
    return;

  if (MI.isBundledWithPred() && !MI.isBundledWithSucc()) {
    SmallVector<MachineInstr *, 2> MIs({&MI});

    // Only do this when the complete bundle is made out of COPYs.
    MachineBasicBlock &MBB = *MI.getParent();
    for (MachineBasicBlock::reverse_instr_iterator I =
         std::next(MI.getReverseIterator()), E = MBB.instr_rend();
         I != E && I->isBundledWithSucc(); ++I) {
      if (!I->isCopy())
        return;
      MIs.push_back(&*I);
    }
    MachineInstr *FirstMI = MIs.back();

    auto anyRegsAlias = [](const MachineInstr *Dst,
                           ArrayRef<MachineInstr *> Srcs,
                           const TargetRegisterInfo *TRI) {
      for (const MachineInstr *Src : Srcs)
        if (Src != Dst)
          if (TRI->regsOverlap(Dst->getOperand(0).getReg(),
                               Src->getOperand(1).getReg()))
            return true;
      return false;
    };

    // If any of the destination registers in the bundle of copies alias any of
    // the source registers, try to schedule the instructions to avoid any
    // clobbering.
    for (int E = MIs.size(), PrevE = E; E > 1; PrevE = E) {
      for (int I = E; I--; )
        if (!anyRegsAlias(MIs[I], makeArrayRef(MIs).take_front(E), TRI)) {
          if (I + 1 != E)
            std::swap(MIs[I], MIs[E - 1]);
          --E;
        }
      if (PrevE == E) {
        MF->getFunction().getContext().emitError(
            "register rewriting failed: cycle in copy bundle");
        break;
      }
    }

    MachineInstr *BundleStart = FirstMI;
    for (MachineInstr *BundledMI : llvm::reverse(MIs)) {
      // If instruction is in the middle of the bundle, move it before the
      // bundle starts, otherwise, just unbundle it. When we get to the last
      // instruction, the bundle will have been completely undone.
      if (BundledMI != BundleStart) {
        BundledMI->removeFromBundle();
        MBB.insert(FirstMI, BundledMI);
      } else if (BundledMI->isBundledWithSucc()) {
        BundledMI->unbundleFromSucc();
        BundleStart = &*std::next(BundledMI->getIterator());
      }

      if (Indexes && BundledMI != FirstMI)
        Indexes->insertMachineInstrInMaps(*BundledMI);
    }
  }
}

/// Check whether (part of) \p SuperPhysReg is live through \p MI.
/// \pre \p MI defines a subregister of a virtual register that
/// has been assigned to \p SuperPhysReg.
bool VirtRegRewriter::subRegLiveThrough(const MachineInstr &MI,
                                        unsigned SuperPhysReg) const {
  SlotIndex MIIndex = LIS->getInstructionIndex(MI);
  SlotIndex BeforeMIUses = MIIndex.getBaseIndex();
  SlotIndex AfterMIDefs = MIIndex.getBoundaryIndex();
  for (MCRegUnitIterator Unit(SuperPhysReg, TRI); Unit.isValid(); ++Unit) {
    const LiveRange &UnitRange = LIS->getRegUnit(*Unit);
    // If the regunit is live both before and after MI,
    // we assume it is live through.
    // Generally speaking, this is not true, because something like
    // "RU = op RU" would match that description.
    // However, we know that we are trying to assess whether
    // a def of a virtual reg, vreg, is live at the same time of RU.
    // If we are in the "RU = op RU" situation, that means that vreg
    // is defined at the same time as RU (i.e., "vreg, RU = op RU").
    // Thus, vreg and RU interferes and vreg cannot be assigned to
    // SuperPhysReg. Therefore, this situation cannot happen.
    if (UnitRange.liveAt(AfterMIDefs) && UnitRange.liveAt(BeforeMIUses))
      return true;
  }
  return false;
}

void VirtRegRewriter::rewrite() {
  bool NoSubRegLiveness = !MRI->subRegLivenessEnabled();
  SmallVector<unsigned, 8> SuperDeads;
  SmallVector<unsigned, 8> SuperDefs;
  SmallVector<unsigned, 8> SuperKills;

  for (MachineFunction::iterator MBBI = MF->begin(), MBBE = MF->end();
       MBBI != MBBE; ++MBBI) {
    LLVM_DEBUG(MBBI->print(dbgs(), Indexes));
    for (MachineBasicBlock::instr_iterator
           MII = MBBI->instr_begin(), MIE = MBBI->instr_end(); MII != MIE;) {
      MachineInstr *MI = &*MII;
      ++MII;

      for (MachineInstr::mop_iterator MOI = MI->operands_begin(),
           MOE = MI->operands_end(); MOI != MOE; ++MOI) {
        MachineOperand &MO = *MOI;

        // Make sure MRI knows about registers clobbered by regmasks.
        if (MO.isRegMask())
          MRI->addPhysRegsUsedFromRegMask(MO.getRegMask());

        if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
          continue;
        unsigned VirtReg = MO.getReg();
        unsigned PhysReg = VRM->getPhys(VirtReg);
        assert(PhysReg != VirtRegMap::NO_PHYS_REG &&
               "Instruction uses unmapped VirtReg");
        assert(!MRI->isReserved(PhysReg) && "Reserved register assignment");

        // Preserve semantics of sub-register operands.
        unsigned SubReg = MO.getSubReg();
        if (SubReg != 0) {
          if (NoSubRegLiveness || !MRI->shouldTrackSubRegLiveness(VirtReg)) {
            // A virtual register kill refers to the whole register, so we may
            // have to add implicit killed operands for the super-register.  A
            // partial redef always kills and redefines the super-register.
            if ((MO.readsReg() && (MO.isDef() || MO.isKill())) ||
                (MO.isDef() && subRegLiveThrough(*MI, PhysReg)))
              SuperKills.push_back(PhysReg);

            if (MO.isDef()) {
              // Also add implicit defs for the super-register.
              if (MO.isDead())
                SuperDeads.push_back(PhysReg);
              else
                SuperDefs.push_back(PhysReg);
            }
          } else {
            if (MO.isUse()) {
              if (readsUndefSubreg(MO))
                // We need to add an <undef> flag if the subregister is
                // completely undefined (and we are not adding super-register
                // defs).
                MO.setIsUndef(true);
            } else if (!MO.isDead()) {
              assert(MO.isDef());
            }
          }

          // The def undef and def internal flags only make sense for
          // sub-register defs, and we are substituting a full physreg.  An
          // implicit killed operand from the SuperKills list will represent the
          // partial read of the super-register.
          if (MO.isDef()) {
            MO.setIsUndef(false);
            MO.setIsInternalRead(false);
          }

          // PhysReg operands cannot have subregister indexes.
          PhysReg = TRI->getSubReg(PhysReg, SubReg);
          assert(PhysReg && "Invalid SubReg for physical register");
          MO.setSubReg(0);
        }
        // Rewrite. Note we could have used MachineOperand::substPhysReg(), but
        // we need the inlining here.
        MO.setReg(PhysReg);
        MO.setIsRenamable(true);
      }

      // Add any missing super-register kills after rewriting the whole
      // instruction.
      while (!SuperKills.empty())
        MI->addRegisterKilled(SuperKills.pop_back_val(), TRI, true);

      while (!SuperDeads.empty())
        MI->addRegisterDead(SuperDeads.pop_back_val(), TRI, true);

      while (!SuperDefs.empty())
        MI->addRegisterDefined(SuperDefs.pop_back_val(), TRI);

      LLVM_DEBUG(dbgs() << "> " << *MI);

      expandCopyBundle(*MI);

      // We can remove identity copies right now.
      handleIdentityCopy(*MI);
    }
  }
}
