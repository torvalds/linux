//===- RegAllocFast.cpp - A fast register allocator for debug code --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This register allocator allocates registers to a basic block at a
/// time, attempting to keep values in registers and reusing registers as
/// appropriate.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Metadata.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <tuple>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumStores, "Number of stores added");
STATISTIC(NumLoads , "Number of loads added");
STATISTIC(NumCoalesced, "Number of copies coalesced");

static RegisterRegAlloc
  fastRegAlloc("fast", "fast register allocator", createFastRegisterAllocator);

namespace {

  class RegAllocFast : public MachineFunctionPass {
  public:
    static char ID;

    RegAllocFast() : MachineFunctionPass(ID), StackSlotForVirtReg(-1) {}

  private:
    MachineFrameInfo *MFI;
    MachineRegisterInfo *MRI;
    const TargetRegisterInfo *TRI;
    const TargetInstrInfo *TII;
    RegisterClassInfo RegClassInfo;

    /// Basic block currently being allocated.
    MachineBasicBlock *MBB;

    /// Maps virtual regs to the frame index where these values are spilled.
    IndexedMap<int, VirtReg2IndexFunctor> StackSlotForVirtReg;

    /// Everything we know about a live virtual register.
    struct LiveReg {
      MachineInstr *LastUse = nullptr; ///< Last instr to use reg.
      unsigned VirtReg;                ///< Virtual register number.
      MCPhysReg PhysReg = 0;           ///< Currently held here.
      unsigned short LastOpNum = 0;    ///< OpNum on LastUse.
      bool Dirty = false;              ///< Register needs spill.

      explicit LiveReg(unsigned VirtReg) : VirtReg(VirtReg) {}

      unsigned getSparseSetIndex() const {
        return TargetRegisterInfo::virtReg2Index(VirtReg);
      }
    };

    using LiveRegMap = SparseSet<LiveReg>;
    /// This map contains entries for each virtual register that is currently
    /// available in a physical register.
    LiveRegMap LiveVirtRegs;

    DenseMap<unsigned, SmallVector<MachineInstr *, 2>> LiveDbgValueMap;

    /// State of a physical register.
    enum RegState {
      /// A disabled register is not available for allocation, but an alias may
      /// be in use. A register can only be moved out of the disabled state if
      /// all aliases are disabled.
      regDisabled,

      /// A free register is not currently in use and can be allocated
      /// immediately without checking aliases.
      regFree,

      /// A reserved register has been assigned explicitly (e.g., setting up a
      /// call parameter), and it remains reserved until it is used.
      regReserved

      /// A register state may also be a virtual register number, indication
      /// that the physical register is currently allocated to a virtual
      /// register. In that case, LiveVirtRegs contains the inverse mapping.
    };

    /// Maps each physical register to a RegState enum or a virtual register.
    std::vector<unsigned> PhysRegState;

    SmallVector<unsigned, 16> VirtDead;
    SmallVector<MachineInstr *, 32> Coalesced;

    using RegUnitSet = SparseSet<uint16_t, identity<uint16_t>>;
    /// Set of register units that are used in the current instruction, and so
    /// cannot be allocated.
    RegUnitSet UsedInInstr;

    void setPhysRegState(MCPhysReg PhysReg, unsigned NewState);

    /// Mark a physreg as used in this instruction.
    void markRegUsedInInstr(MCPhysReg PhysReg) {
      for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units)
        UsedInInstr.insert(*Units);
    }

    /// Check if a physreg or any of its aliases are used in this instruction.
    bool isRegUsedInInstr(MCPhysReg PhysReg) const {
      for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units)
        if (UsedInInstr.count(*Units))
          return true;
      return false;
    }

    enum : unsigned {
      spillClean = 50,
      spillDirty = 100,
      spillImpossible = ~0u
    };

  public:
    StringRef getPassName() const override { return "Fast Register Allocator"; }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoPHIs);
    }

    MachineFunctionProperties getSetProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

  private:
    bool runOnMachineFunction(MachineFunction &MF) override;

    void allocateBasicBlock(MachineBasicBlock &MBB);
    void allocateInstruction(MachineInstr &MI);
    void handleDebugValue(MachineInstr &MI);
    void handleThroughOperands(MachineInstr &MI,
                               SmallVectorImpl<unsigned> &VirtDead);
    bool isLastUseOfLocalReg(const MachineOperand &MO) const;

    void addKillFlag(const LiveReg &LRI);
    void killVirtReg(LiveReg &LR);
    void killVirtReg(unsigned VirtReg);
    void spillVirtReg(MachineBasicBlock::iterator MI, LiveReg &LR);
    void spillVirtReg(MachineBasicBlock::iterator MI, unsigned VirtReg);

    void usePhysReg(MachineOperand &MO);
    void definePhysReg(MachineBasicBlock::iterator MI, MCPhysReg PhysReg,
                       RegState NewState);
    unsigned calcSpillCost(MCPhysReg PhysReg) const;
    void assignVirtToPhysReg(LiveReg &, MCPhysReg PhysReg);

    LiveRegMap::iterator findLiveVirtReg(unsigned VirtReg) {
      return LiveVirtRegs.find(TargetRegisterInfo::virtReg2Index(VirtReg));
    }

    LiveRegMap::const_iterator findLiveVirtReg(unsigned VirtReg) const {
      return LiveVirtRegs.find(TargetRegisterInfo::virtReg2Index(VirtReg));
    }

    void allocVirtReg(MachineInstr &MI, LiveReg &LR, unsigned Hint);
    MCPhysReg defineVirtReg(MachineInstr &MI, unsigned OpNum, unsigned VirtReg,
                            unsigned Hint);
    LiveReg &reloadVirtReg(MachineInstr &MI, unsigned OpNum, unsigned VirtReg,
                           unsigned Hint);
    void spillAll(MachineBasicBlock::iterator MI);
    bool setPhysReg(MachineInstr &MI, MachineOperand &MO, MCPhysReg PhysReg);

    int getStackSpaceFor(unsigned VirtReg);
    void spill(MachineBasicBlock::iterator Before, unsigned VirtReg,
               MCPhysReg AssignedReg, bool Kill);
    void reload(MachineBasicBlock::iterator Before, unsigned VirtReg,
                MCPhysReg PhysReg);

    void dumpState();
  };

} // end anonymous namespace

char RegAllocFast::ID = 0;

INITIALIZE_PASS(RegAllocFast, "regallocfast", "Fast Register Allocator", false,
                false)

void RegAllocFast::setPhysRegState(MCPhysReg PhysReg, unsigned NewState) {
  PhysRegState[PhysReg] = NewState;
}

/// This allocates space for the specified virtual register to be held on the
/// stack.
int RegAllocFast::getStackSpaceFor(unsigned VirtReg) {
  // Find the location Reg would belong...
  int SS = StackSlotForVirtReg[VirtReg];
  // Already has space allocated?
  if (SS != -1)
    return SS;

  // Allocate a new stack object for this spill location...
  const TargetRegisterClass &RC = *MRI->getRegClass(VirtReg);
  unsigned Size = TRI->getSpillSize(RC);
  unsigned Align = TRI->getSpillAlignment(RC);
  int FrameIdx = MFI->CreateSpillStackObject(Size, Align);

  // Assign the slot.
  StackSlotForVirtReg[VirtReg] = FrameIdx;
  return FrameIdx;
}

/// Insert spill instruction for \p AssignedReg before \p Before. Update
/// DBG_VALUEs with \p VirtReg operands with the stack slot.
void RegAllocFast::spill(MachineBasicBlock::iterator Before, unsigned VirtReg,
                         MCPhysReg AssignedReg, bool Kill) {
  LLVM_DEBUG(dbgs() << "Spilling " << printReg(VirtReg, TRI)
                    << " in " << printReg(AssignedReg, TRI));
  int FI = getStackSpaceFor(VirtReg);
  LLVM_DEBUG(dbgs() << " to stack slot #" << FI << '\n');

  const TargetRegisterClass &RC = *MRI->getRegClass(VirtReg);
  TII->storeRegToStackSlot(*MBB, Before, AssignedReg, Kill, FI, &RC, TRI);
  ++NumStores;

  // If this register is used by DBG_VALUE then insert new DBG_VALUE to
  // identify spilled location as the place to find corresponding variable's
  // value.
  SmallVectorImpl<MachineInstr *> &LRIDbgValues = LiveDbgValueMap[VirtReg];
  for (MachineInstr *DBG : LRIDbgValues) {
    MachineInstr *NewDV = buildDbgValueForSpill(*MBB, Before, *DBG, FI);
    assert(NewDV->getParent() == MBB && "dangling parent pointer");
    (void)NewDV;
    LLVM_DEBUG(dbgs() << "Inserting debug info due to spill:\n" << *NewDV);
  }
  // Now this register is spilled there is should not be any DBG_VALUE
  // pointing to this register because they are all pointing to spilled value
  // now.
  LRIDbgValues.clear();
}

/// Insert reload instruction for \p PhysReg before \p Before.
void RegAllocFast::reload(MachineBasicBlock::iterator Before, unsigned VirtReg,
                          MCPhysReg PhysReg) {
  LLVM_DEBUG(dbgs() << "Reloading " << printReg(VirtReg, TRI) << " into "
                    << printReg(PhysReg, TRI) << '\n');
  int FI = getStackSpaceFor(VirtReg);
  const TargetRegisterClass &RC = *MRI->getRegClass(VirtReg);
  TII->loadRegFromStackSlot(*MBB, Before, PhysReg, FI, &RC, TRI);
  ++NumLoads;
}

/// Return true if MO is the only remaining reference to its virtual register,
/// and it is guaranteed to be a block-local register.
bool RegAllocFast::isLastUseOfLocalReg(const MachineOperand &MO) const {
  // If the register has ever been spilled or reloaded, we conservatively assume
  // it is a global register used in multiple blocks.
  if (StackSlotForVirtReg[MO.getReg()] != -1)
    return false;

  // Check that the use/def chain has exactly one operand - MO.
  MachineRegisterInfo::reg_nodbg_iterator I = MRI->reg_nodbg_begin(MO.getReg());
  if (&*I != &MO)
    return false;
  return ++I == MRI->reg_nodbg_end();
}

/// Set kill flags on last use of a virtual register.
void RegAllocFast::addKillFlag(const LiveReg &LR) {
  if (!LR.LastUse) return;
  MachineOperand &MO = LR.LastUse->getOperand(LR.LastOpNum);
  if (MO.isUse() && !LR.LastUse->isRegTiedToDefOperand(LR.LastOpNum)) {
    if (MO.getReg() == LR.PhysReg)
      MO.setIsKill();
    // else, don't do anything we are problably redefining a
    // subreg of this register and given we don't track which
    // lanes are actually dead, we cannot insert a kill flag here.
    // Otherwise we may end up in a situation like this:
    // ... = (MO) physreg:sub1, implicit killed physreg
    // ... <== Here we would allow later pass to reuse physreg:sub1
    //         which is potentially wrong.
    // LR:sub0 = ...
    // ... = LR.sub1 <== This is going to use physreg:sub1
  }
}

/// Mark virtreg as no longer available.
void RegAllocFast::killVirtReg(LiveReg &LR) {
  addKillFlag(LR);
  assert(PhysRegState[LR.PhysReg] == LR.VirtReg &&
         "Broken RegState mapping");
  setPhysRegState(LR.PhysReg, regFree);
  LR.PhysReg = 0;
}

/// Mark virtreg as no longer available.
void RegAllocFast::killVirtReg(unsigned VirtReg) {
  assert(TargetRegisterInfo::isVirtualRegister(VirtReg) &&
         "killVirtReg needs a virtual register");
  LiveRegMap::iterator LRI = findLiveVirtReg(VirtReg);
  if (LRI != LiveVirtRegs.end() && LRI->PhysReg)
    killVirtReg(*LRI);
}

/// This method spills the value specified by VirtReg into the corresponding
/// stack slot if needed.
void RegAllocFast::spillVirtReg(MachineBasicBlock::iterator MI,
                                unsigned VirtReg) {
  assert(TargetRegisterInfo::isVirtualRegister(VirtReg) &&
         "Spilling a physical register is illegal!");
  LiveRegMap::iterator LRI = findLiveVirtReg(VirtReg);
  assert(LRI != LiveVirtRegs.end() && LRI->PhysReg &&
         "Spilling unmapped virtual register");
  spillVirtReg(MI, *LRI);
}

/// Do the actual work of spilling.
void RegAllocFast::spillVirtReg(MachineBasicBlock::iterator MI, LiveReg &LR) {
  assert(PhysRegState[LR.PhysReg] == LR.VirtReg && "Broken RegState mapping");

  if (LR.Dirty) {
    // If this physreg is used by the instruction, we want to kill it on the
    // instruction, not on the spill.
    bool SpillKill = MachineBasicBlock::iterator(LR.LastUse) != MI;
    LR.Dirty = false;

    spill(MI, LR.VirtReg, LR.PhysReg, SpillKill);

    if (SpillKill)
      LR.LastUse = nullptr; // Don't kill register again
  }
  killVirtReg(LR);
}

/// Spill all dirty virtregs without killing them.
void RegAllocFast::spillAll(MachineBasicBlock::iterator MI) {
  if (LiveVirtRegs.empty())
    return;
  // The LiveRegMap is keyed by an unsigned (the virtreg number), so the order
  // of spilling here is deterministic, if arbitrary.
  for (LiveReg &LR : LiveVirtRegs) {
    if (!LR.PhysReg)
      continue;
    spillVirtReg(MI, LR);
  }
  LiveVirtRegs.clear();
}

/// Handle the direct use of a physical register.  Check that the register is
/// not used by a virtreg. Kill the physreg, marking it free. This may add
/// implicit kills to MO->getParent() and invalidate MO.
void RegAllocFast::usePhysReg(MachineOperand &MO) {
  // Ignore undef uses.
  if (MO.isUndef())
    return;

  unsigned PhysReg = MO.getReg();
  assert(TargetRegisterInfo::isPhysicalRegister(PhysReg) &&
         "Bad usePhysReg operand");

  markRegUsedInInstr(PhysReg);
  switch (PhysRegState[PhysReg]) {
  case regDisabled:
    break;
  case regReserved:
    PhysRegState[PhysReg] = regFree;
    LLVM_FALLTHROUGH;
  case regFree:
    MO.setIsKill();
    return;
  default:
    // The physreg was allocated to a virtual register. That means the value we
    // wanted has been clobbered.
    llvm_unreachable("Instruction uses an allocated register");
  }

  // Maybe a superregister is reserved?
  for (MCRegAliasIterator AI(PhysReg, TRI, false); AI.isValid(); ++AI) {
    MCPhysReg Alias = *AI;
    switch (PhysRegState[Alias]) {
    case regDisabled:
      break;
    case regReserved:
      // Either PhysReg is a subregister of Alias and we mark the
      // whole register as free, or PhysReg is the superregister of
      // Alias and we mark all the aliases as disabled before freeing
      // PhysReg.
      // In the latter case, since PhysReg was disabled, this means that
      // its value is defined only by physical sub-registers. This check
      // is performed by the assert of the default case in this loop.
      // Note: The value of the superregister may only be partial
      // defined, that is why regDisabled is a valid state for aliases.
      assert((TRI->isSuperRegister(PhysReg, Alias) ||
              TRI->isSuperRegister(Alias, PhysReg)) &&
             "Instruction is not using a subregister of a reserved register");
      LLVM_FALLTHROUGH;
    case regFree:
      if (TRI->isSuperRegister(PhysReg, Alias)) {
        // Leave the superregister in the working set.
        setPhysRegState(Alias, regFree);
        MO.getParent()->addRegisterKilled(Alias, TRI, true);
        return;
      }
      // Some other alias was in the working set - clear it.
      setPhysRegState(Alias, regDisabled);
      break;
    default:
      llvm_unreachable("Instruction uses an alias of an allocated register");
    }
  }

  // All aliases are disabled, bring register into working set.
  setPhysRegState(PhysReg, regFree);
  MO.setIsKill();
}

/// Mark PhysReg as reserved or free after spilling any virtregs. This is very
/// similar to defineVirtReg except the physreg is reserved instead of
/// allocated.
void RegAllocFast::definePhysReg(MachineBasicBlock::iterator MI,
                                 MCPhysReg PhysReg, RegState NewState) {
  markRegUsedInInstr(PhysReg);
  switch (unsigned VirtReg = PhysRegState[PhysReg]) {
  case regDisabled:
    break;
  default:
    spillVirtReg(MI, VirtReg);
    LLVM_FALLTHROUGH;
  case regFree:
  case regReserved:
    setPhysRegState(PhysReg, NewState);
    return;
  }

  // This is a disabled register, disable all aliases.
  setPhysRegState(PhysReg, NewState);
  for (MCRegAliasIterator AI(PhysReg, TRI, false); AI.isValid(); ++AI) {
    MCPhysReg Alias = *AI;
    switch (unsigned VirtReg = PhysRegState[Alias]) {
    case regDisabled:
      break;
    default:
      spillVirtReg(MI, VirtReg);
      LLVM_FALLTHROUGH;
    case regFree:
    case regReserved:
      setPhysRegState(Alias, regDisabled);
      if (TRI->isSuperRegister(PhysReg, Alias))
        return;
      break;
    }
  }
}

/// Return the cost of spilling clearing out PhysReg and aliases so it is free
/// for allocation. Returns 0 when PhysReg is free or disabled with all aliases
/// disabled - it can be allocated directly.
/// \returns spillImpossible when PhysReg or an alias can't be spilled.
unsigned RegAllocFast::calcSpillCost(MCPhysReg PhysReg) const {
  if (isRegUsedInInstr(PhysReg)) {
    LLVM_DEBUG(dbgs() << printReg(PhysReg, TRI)
                      << " is already used in instr.\n");
    return spillImpossible;
  }
  switch (unsigned VirtReg = PhysRegState[PhysReg]) {
  case regDisabled:
    break;
  case regFree:
    return 0;
  case regReserved:
    LLVM_DEBUG(dbgs() << printReg(VirtReg, TRI) << " corresponding "
                      << printReg(PhysReg, TRI) << " is reserved already.\n");
    return spillImpossible;
  default: {
    LiveRegMap::const_iterator LRI = findLiveVirtReg(VirtReg);
    assert(LRI != LiveVirtRegs.end() && LRI->PhysReg &&
           "Missing VirtReg entry");
    return LRI->Dirty ? spillDirty : spillClean;
  }
  }

  // This is a disabled register, add up cost of aliases.
  LLVM_DEBUG(dbgs() << printReg(PhysReg, TRI) << " is disabled.\n");
  unsigned Cost = 0;
  for (MCRegAliasIterator AI(PhysReg, TRI, false); AI.isValid(); ++AI) {
    MCPhysReg Alias = *AI;
    switch (unsigned VirtReg = PhysRegState[Alias]) {
    case regDisabled:
      break;
    case regFree:
      ++Cost;
      break;
    case regReserved:
      return spillImpossible;
    default: {
      LiveRegMap::const_iterator LRI = findLiveVirtReg(VirtReg);
      assert(LRI != LiveVirtRegs.end() && LRI->PhysReg &&
             "Missing VirtReg entry");
      Cost += LRI->Dirty ? spillDirty : spillClean;
      break;
    }
    }
  }
  return Cost;
}

/// This method updates local state so that we know that PhysReg is the
/// proper container for VirtReg now.  The physical register must not be used
/// for anything else when this is called.
void RegAllocFast::assignVirtToPhysReg(LiveReg &LR, MCPhysReg PhysReg) {
  unsigned VirtReg = LR.VirtReg;
  LLVM_DEBUG(dbgs() << "Assigning " << printReg(VirtReg, TRI) << " to "
                    << printReg(PhysReg, TRI) << '\n');
  assert(LR.PhysReg == 0 && "Already assigned a physreg");
  assert(PhysReg != 0 && "Trying to assign no register");
  LR.PhysReg = PhysReg;
  setPhysRegState(PhysReg, VirtReg);
}

/// Allocates a physical register for VirtReg.
void RegAllocFast::allocVirtReg(MachineInstr &MI, LiveReg &LR, unsigned Hint) {
  const unsigned VirtReg = LR.VirtReg;

  assert(TargetRegisterInfo::isVirtualRegister(VirtReg) &&
         "Can only allocate virtual registers");

  const TargetRegisterClass &RC = *MRI->getRegClass(VirtReg);
  LLVM_DEBUG(dbgs() << "Search register for " << printReg(VirtReg)
                    << " in class " << TRI->getRegClassName(&RC) << '\n');

  // Take hint when possible.
  if (TargetRegisterInfo::isPhysicalRegister(Hint) &&
      MRI->isAllocatable(Hint) && RC.contains(Hint)) {
    // Ignore the hint if we would have to spill a dirty register.
    unsigned Cost = calcSpillCost(Hint);
    if (Cost < spillDirty) {
      if (Cost)
        definePhysReg(MI, Hint, regFree);
      assignVirtToPhysReg(LR, Hint);
      return;
    }
  }

  // First try to find a completely free register.
  ArrayRef<MCPhysReg> AllocationOrder = RegClassInfo.getOrder(&RC);
  for (MCPhysReg PhysReg : AllocationOrder) {
    if (PhysRegState[PhysReg] == regFree && !isRegUsedInInstr(PhysReg)) {
      assignVirtToPhysReg(LR, PhysReg);
      return;
    }
  }

  MCPhysReg BestReg = 0;
  unsigned BestCost = spillImpossible;
  for (MCPhysReg PhysReg : AllocationOrder) {
    LLVM_DEBUG(dbgs() << "\tRegister: " << printReg(PhysReg, TRI) << ' ');
    unsigned Cost = calcSpillCost(PhysReg);
    LLVM_DEBUG(dbgs() << "Cost: " << Cost << " BestCost: " << BestCost << '\n');
    // Immediate take a register with cost 0.
    if (Cost == 0) {
      assignVirtToPhysReg(LR, PhysReg);
      return;
    }
    if (Cost < BestCost) {
      BestReg = PhysReg;
      BestCost = Cost;
    }
  }

  if (!BestReg) {
    // Nothing we can do: Report an error and keep going with an invalid
    // allocation.
    if (MI.isInlineAsm())
      MI.emitError("inline assembly requires more registers than available");
    else
      MI.emitError("ran out of registers during register allocation");
    definePhysReg(MI, *AllocationOrder.begin(), regFree);
    assignVirtToPhysReg(LR, *AllocationOrder.begin());
    return;
  }

  definePhysReg(MI, BestReg, regFree);
  assignVirtToPhysReg(LR, BestReg);
}

/// Allocates a register for VirtReg and mark it as dirty.
MCPhysReg RegAllocFast::defineVirtReg(MachineInstr &MI, unsigned OpNum,
                                      unsigned VirtReg, unsigned Hint) {
  assert(TargetRegisterInfo::isVirtualRegister(VirtReg) &&
         "Not a virtual register");
  LiveRegMap::iterator LRI;
  bool New;
  std::tie(LRI, New) = LiveVirtRegs.insert(LiveReg(VirtReg));
  if (!LRI->PhysReg) {
    // If there is no hint, peek at the only use of this register.
    if ((!Hint || !TargetRegisterInfo::isPhysicalRegister(Hint)) &&
        MRI->hasOneNonDBGUse(VirtReg)) {
      const MachineInstr &UseMI = *MRI->use_instr_nodbg_begin(VirtReg);
      // It's a copy, use the destination register as a hint.
      if (UseMI.isCopyLike())
        Hint = UseMI.getOperand(0).getReg();
    }
    allocVirtReg(MI, *LRI, Hint);
  } else if (LRI->LastUse) {
    // Redefining a live register - kill at the last use, unless it is this
    // instruction defining VirtReg multiple times.
    if (LRI->LastUse != &MI || LRI->LastUse->getOperand(LRI->LastOpNum).isUse())
      addKillFlag(*LRI);
  }
  assert(LRI->PhysReg && "Register not assigned");
  LRI->LastUse = &MI;
  LRI->LastOpNum = OpNum;
  LRI->Dirty = true;
  markRegUsedInInstr(LRI->PhysReg);
  return LRI->PhysReg;
}

/// Make sure VirtReg is available in a physreg and return it.
RegAllocFast::LiveReg &RegAllocFast::reloadVirtReg(MachineInstr &MI,
                                                   unsigned OpNum,
                                                   unsigned VirtReg,
                                                   unsigned Hint) {
  assert(TargetRegisterInfo::isVirtualRegister(VirtReg) &&
         "Not a virtual register");
  LiveRegMap::iterator LRI;
  bool New;
  std::tie(LRI, New) = LiveVirtRegs.insert(LiveReg(VirtReg));
  MachineOperand &MO = MI.getOperand(OpNum);
  if (!LRI->PhysReg) {
    allocVirtReg(MI, *LRI, Hint);
    reload(MI, VirtReg, LRI->PhysReg);
  } else if (LRI->Dirty) {
    if (isLastUseOfLocalReg(MO)) {
      LLVM_DEBUG(dbgs() << "Killing last use: " << MO << '\n');
      if (MO.isUse())
        MO.setIsKill();
      else
        MO.setIsDead();
    } else if (MO.isKill()) {
      LLVM_DEBUG(dbgs() << "Clearing dubious kill: " << MO << '\n');
      MO.setIsKill(false);
    } else if (MO.isDead()) {
      LLVM_DEBUG(dbgs() << "Clearing dubious dead: " << MO << '\n');
      MO.setIsDead(false);
    }
  } else if (MO.isKill()) {
    // We must remove kill flags from uses of reloaded registers because the
    // register would be killed immediately, and there might be a second use:
    //   %foo = OR killed %x, %x
    // This would cause a second reload of %x into a different register.
    LLVM_DEBUG(dbgs() << "Clearing clean kill: " << MO << '\n');
    MO.setIsKill(false);
  } else if (MO.isDead()) {
    LLVM_DEBUG(dbgs() << "Clearing clean dead: " << MO << '\n');
    MO.setIsDead(false);
  }
  assert(LRI->PhysReg && "Register not assigned");
  LRI->LastUse = &MI;
  LRI->LastOpNum = OpNum;
  markRegUsedInInstr(LRI->PhysReg);
  return *LRI;
}

/// Changes operand OpNum in MI the refer the PhysReg, considering subregs. This
/// may invalidate any operand pointers.  Return true if the operand kills its
/// register.
bool RegAllocFast::setPhysReg(MachineInstr &MI, MachineOperand &MO,
                              MCPhysReg PhysReg) {
  bool Dead = MO.isDead();
  if (!MO.getSubReg()) {
    MO.setReg(PhysReg);
    MO.setIsRenamable(true);
    return MO.isKill() || Dead;
  }

  // Handle subregister index.
  MO.setReg(PhysReg ? TRI->getSubReg(PhysReg, MO.getSubReg()) : 0);
  MO.setIsRenamable(true);
  MO.setSubReg(0);

  // A kill flag implies killing the full register. Add corresponding super
  // register kill.
  if (MO.isKill()) {
    MI.addRegisterKilled(PhysReg, TRI, true);
    return true;
  }

  // A <def,read-undef> of a sub-register requires an implicit def of the full
  // register.
  if (MO.isDef() && MO.isUndef())
    MI.addRegisterDefined(PhysReg, TRI);

  return Dead;
}

// Handles special instruction operand like early clobbers and tied ops when
// there are additional physreg defines.
void RegAllocFast::handleThroughOperands(MachineInstr &MI,
                                         SmallVectorImpl<unsigned> &VirtDead) {
  LLVM_DEBUG(dbgs() << "Scanning for through registers:");
  SmallSet<unsigned, 8> ThroughRegs;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    if (MO.isEarlyClobber() || (MO.isUse() && MO.isTied()) ||
        (MO.getSubReg() && MI.readsVirtualRegister(Reg))) {
      if (ThroughRegs.insert(Reg).second)
        LLVM_DEBUG(dbgs() << ' ' << printReg(Reg));
    }
  }

  // If any physreg defines collide with preallocated through registers,
  // we must spill and reallocate.
  LLVM_DEBUG(dbgs() << "\nChecking for physdef collisions.\n");
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef()) continue;
    unsigned Reg = MO.getReg();
    if (!Reg || !TargetRegisterInfo::isPhysicalRegister(Reg)) continue;
    markRegUsedInInstr(Reg);
    for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI) {
      if (ThroughRegs.count(PhysRegState[*AI]))
        definePhysReg(MI, *AI, regFree);
    }
  }

  SmallVector<unsigned, 8> PartialDefs;
  LLVM_DEBUG(dbgs() << "Allocating tied uses.\n");
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg)) continue;
    if (MO.isUse()) {
      if (!MO.isTied()) continue;
      LLVM_DEBUG(dbgs() << "Operand " << I << "(" << MO
                        << ") is tied to operand " << MI.findTiedOperandIdx(I)
                        << ".\n");
      LiveReg &LR = reloadVirtReg(MI, I, Reg, 0);
      MCPhysReg PhysReg = LR.PhysReg;
      setPhysReg(MI, MO, PhysReg);
      // Note: we don't update the def operand yet. That would cause the normal
      // def-scan to attempt spilling.
    } else if (MO.getSubReg() && MI.readsVirtualRegister(Reg)) {
      LLVM_DEBUG(dbgs() << "Partial redefine: " << MO << '\n');
      // Reload the register, but don't assign to the operand just yet.
      // That would confuse the later phys-def processing pass.
      LiveReg &LR = reloadVirtReg(MI, I, Reg, 0);
      PartialDefs.push_back(LR.PhysReg);
    }
  }

  LLVM_DEBUG(dbgs() << "Allocating early clobbers.\n");
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg)) continue;
    if (!MO.isEarlyClobber())
      continue;
    // Note: defineVirtReg may invalidate MO.
    MCPhysReg PhysReg = defineVirtReg(MI, I, Reg, 0);
    if (setPhysReg(MI, MI.getOperand(I), PhysReg))
      VirtDead.push_back(Reg);
  }

  // Restore UsedInInstr to a state usable for allocating normal virtual uses.
  UsedInInstr.clear();
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || (MO.isDef() && !MO.isEarlyClobber())) continue;
    unsigned Reg = MO.getReg();
    if (!Reg || !TargetRegisterInfo::isPhysicalRegister(Reg)) continue;
    LLVM_DEBUG(dbgs() << "\tSetting " << printReg(Reg, TRI)
                      << " as used in instr\n");
    markRegUsedInInstr(Reg);
  }

  // Also mark PartialDefs as used to avoid reallocation.
  for (unsigned PartialDef : PartialDefs)
    markRegUsedInInstr(PartialDef);
}

#ifndef NDEBUG
void RegAllocFast::dumpState() {
  for (unsigned Reg = 1, E = TRI->getNumRegs(); Reg != E; ++Reg) {
    if (PhysRegState[Reg] == regDisabled) continue;
    dbgs() << " " << printReg(Reg, TRI);
    switch(PhysRegState[Reg]) {
    case regFree:
      break;
    case regReserved:
      dbgs() << "*";
      break;
    default: {
      dbgs() << '=' << printReg(PhysRegState[Reg]);
      LiveRegMap::iterator LRI = findLiveVirtReg(PhysRegState[Reg]);
      assert(LRI != LiveVirtRegs.end() && LRI->PhysReg &&
             "Missing VirtReg entry");
      if (LRI->Dirty)
        dbgs() << "*";
      assert(LRI->PhysReg == Reg && "Bad inverse map");
      break;
    }
    }
  }
  dbgs() << '\n';
  // Check that LiveVirtRegs is the inverse.
  for (LiveRegMap::iterator i = LiveVirtRegs.begin(),
       e = LiveVirtRegs.end(); i != e; ++i) {
    if (!i->PhysReg)
      continue;
    assert(TargetRegisterInfo::isVirtualRegister(i->VirtReg) &&
           "Bad map key");
    assert(TargetRegisterInfo::isPhysicalRegister(i->PhysReg) &&
           "Bad map value");
    assert(PhysRegState[i->PhysReg] == i->VirtReg && "Bad inverse map");
  }
}
#endif

void RegAllocFast::allocateInstruction(MachineInstr &MI) {
  const MCInstrDesc &MCID = MI.getDesc();

  // If this is a copy, we may be able to coalesce.
  unsigned CopySrcReg = 0;
  unsigned CopyDstReg = 0;
  unsigned CopySrcSub = 0;
  unsigned CopyDstSub = 0;
  if (MI.isCopy()) {
    CopyDstReg = MI.getOperand(0).getReg();
    CopySrcReg = MI.getOperand(1).getReg();
    CopyDstSub = MI.getOperand(0).getSubReg();
    CopySrcSub = MI.getOperand(1).getSubReg();
  }

  // Track registers used by instruction.
  UsedInInstr.clear();

  // First scan.
  // Mark physreg uses and early clobbers as used.
  // Find the end of the virtreg operands
  unsigned VirtOpEnd = 0;
  bool hasTiedOps = false;
  bool hasEarlyClobbers = false;
  bool hasPartialRedefs = false;
  bool hasPhysDefs = false;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    // Make sure MRI knows about registers clobbered by regmasks.
    if (MO.isRegMask()) {
      MRI->addPhysRegsUsedFromRegMask(MO.getRegMask());
      continue;
    }
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (!Reg) continue;
    if (TargetRegisterInfo::isVirtualRegister(Reg)) {
      VirtOpEnd = i+1;
      if (MO.isUse()) {
        hasTiedOps = hasTiedOps ||
                            MCID.getOperandConstraint(i, MCOI::TIED_TO) != -1;
      } else {
        if (MO.isEarlyClobber())
          hasEarlyClobbers = true;
        if (MO.getSubReg() && MI.readsVirtualRegister(Reg))
          hasPartialRedefs = true;
      }
      continue;
    }
    if (!MRI->isAllocatable(Reg)) continue;
    if (MO.isUse()) {
      usePhysReg(MO);
    } else if (MO.isEarlyClobber()) {
      definePhysReg(MI, Reg,
                    (MO.isImplicit() || MO.isDead()) ? regFree : regReserved);
      hasEarlyClobbers = true;
    } else
      hasPhysDefs = true;
  }

  // The instruction may have virtual register operands that must be allocated
  // the same register at use-time and def-time: early clobbers and tied
  // operands. If there are also physical defs, these registers must avoid
  // both physical defs and uses, making them more constrained than normal
  // operands.
  // Similarly, if there are multiple defs and tied operands, we must make
  // sure the same register is allocated to uses and defs.
  // We didn't detect inline asm tied operands above, so just make this extra
  // pass for all inline asm.
  if (MI.isInlineAsm() || hasEarlyClobbers || hasPartialRedefs ||
      (hasTiedOps && (hasPhysDefs || MCID.getNumDefs() > 1))) {
    handleThroughOperands(MI, VirtDead);
    // Don't attempt coalescing when we have funny stuff going on.
    CopyDstReg = 0;
    // Pretend we have early clobbers so the use operands get marked below.
    // This is not necessary for the common case of a single tied use.
    hasEarlyClobbers = true;
  }

  // Second scan.
  // Allocate virtreg uses.
  for (unsigned I = 0; I != VirtOpEnd; ++I) {
    MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg)) continue;
    if (MO.isUse()) {
      LiveReg &LR = reloadVirtReg(MI, I, Reg, CopyDstReg);
      MCPhysReg PhysReg = LR.PhysReg;
      CopySrcReg = (CopySrcReg == Reg || CopySrcReg == PhysReg) ? PhysReg : 0;
      if (setPhysReg(MI, MO, PhysReg))
        killVirtReg(LR);
    }
  }

  // Track registers defined by instruction - early clobbers and tied uses at
  // this point.
  UsedInInstr.clear();
  if (hasEarlyClobbers) {
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg()) continue;
      unsigned Reg = MO.getReg();
      if (!Reg || !TargetRegisterInfo::isPhysicalRegister(Reg)) continue;
      // Look for physreg defs and tied uses.
      if (!MO.isDef() && !MO.isTied()) continue;
      markRegUsedInInstr(Reg);
    }
  }

  unsigned DefOpEnd = MI.getNumOperands();
  if (MI.isCall()) {
    // Spill all virtregs before a call. This serves one purpose: If an
    // exception is thrown, the landing pad is going to expect to find
    // registers in their spill slots.
    // Note: although this is appealing to just consider all definitions
    // as call-clobbered, this is not correct because some of those
    // definitions may be used later on and we do not want to reuse
    // those for virtual registers in between.
    LLVM_DEBUG(dbgs() << "  Spilling remaining registers before call.\n");
    spillAll(MI);
  }

  // Third scan.
  // Allocate defs and collect dead defs.
  for (unsigned I = 0; I != DefOpEnd; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg() || !MO.isDef() || !MO.getReg() || MO.isEarlyClobber())
      continue;
    unsigned Reg = MO.getReg();

    if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
      if (!MRI->isAllocatable(Reg)) continue;
      definePhysReg(MI, Reg, MO.isDead() ? regFree : regReserved);
      continue;
    }
    MCPhysReg PhysReg = defineVirtReg(MI, I, Reg, CopySrcReg);
    if (setPhysReg(MI, MI.getOperand(I), PhysReg)) {
      VirtDead.push_back(Reg);
      CopyDstReg = 0; // cancel coalescing;
    } else
      CopyDstReg = (CopyDstReg == Reg || CopyDstReg == PhysReg) ? PhysReg : 0;
  }

  // Kill dead defs after the scan to ensure that multiple defs of the same
  // register are allocated identically. We didn't need to do this for uses
  // because we are crerating our own kill flags, and they are always at the
  // last use.
  for (unsigned VirtReg : VirtDead)
    killVirtReg(VirtReg);
  VirtDead.clear();

  LLVM_DEBUG(dbgs() << "<< " << MI);
  if (CopyDstReg && CopyDstReg == CopySrcReg && CopyDstSub == CopySrcSub) {
    LLVM_DEBUG(dbgs() << "Mark identity copy for removal\n");
    Coalesced.push_back(&MI);
  }
}

void RegAllocFast::handleDebugValue(MachineInstr &MI) {
  MachineOperand &MO = MI.getOperand(0);

  // Ignore DBG_VALUEs that aren't based on virtual registers. These are
  // mostly constants and frame indices.
  if (!MO.isReg())
    return;
  unsigned Reg = MO.getReg();
  if (!TargetRegisterInfo::isVirtualRegister(Reg))
    return;

  // See if this virtual register has already been allocated to a physical
  // register or spilled to a stack slot.
  LiveRegMap::iterator LRI = findLiveVirtReg(Reg);
  if (LRI != LiveVirtRegs.end() && LRI->PhysReg) {
    setPhysReg(MI, MO, LRI->PhysReg);
  } else {
    int SS = StackSlotForVirtReg[Reg];
    if (SS != -1) {
      // Modify DBG_VALUE now that the value is in a spill slot.
      updateDbgValueForSpill(MI, SS);
      LLVM_DEBUG(dbgs() << "Modifying debug info due to spill:" << "\t" << MI);
      return;
    }

    // We can't allocate a physreg for a DebugValue, sorry!
    LLVM_DEBUG(dbgs() << "Unable to allocate vreg used by DBG_VALUE");
    MO.setReg(0);
  }

  // If Reg hasn't been spilled, put this DBG_VALUE in LiveDbgValueMap so
  // that future spills of Reg will have DBG_VALUEs.
  LiveDbgValueMap[Reg].push_back(&MI);
}

void RegAllocFast::allocateBasicBlock(MachineBasicBlock &MBB) {
  this->MBB = &MBB;
  LLVM_DEBUG(dbgs() << "\nAllocating " << MBB);

  PhysRegState.assign(TRI->getNumRegs(), regDisabled);
  assert(LiveVirtRegs.empty() && "Mapping not cleared from last block?");

  MachineBasicBlock::iterator MII = MBB.begin();

  // Add live-in registers as live.
  for (const MachineBasicBlock::RegisterMaskPair LI : MBB.liveins())
    if (MRI->isAllocatable(LI.PhysReg))
      definePhysReg(MII, LI.PhysReg, regReserved);

  VirtDead.clear();
  Coalesced.clear();

  // Otherwise, sequentially allocate each instruction in the MBB.
  for (MachineInstr &MI : MBB) {
    LLVM_DEBUG(
      dbgs() << "\n>> " << MI << "Regs:";
      dumpState()
    );

    // Special handling for debug values. Note that they are not allowed to
    // affect codegen of the other instructions in any way.
    if (MI.isDebugValue()) {
      handleDebugValue(MI);
      continue;
    }

    allocateInstruction(MI);
  }

  // Spill all physical registers holding virtual registers now.
  LLVM_DEBUG(dbgs() << "Spilling live registers at end of block.\n");
  spillAll(MBB.getFirstTerminator());

  // Erase all the coalesced copies. We are delaying it until now because
  // LiveVirtRegs might refer to the instrs.
  for (MachineInstr *MI : Coalesced)
    MBB.erase(MI);
  NumCoalesced += Coalesced.size();

  LLVM_DEBUG(MBB.dump());
}

bool RegAllocFast::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** FAST REGISTER ALLOCATION **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  MRI = &MF.getRegInfo();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  TRI = STI.getRegisterInfo();
  TII = STI.getInstrInfo();
  MFI = &MF.getFrameInfo();
  MRI->freezeReservedRegs(MF);
  RegClassInfo.runOnMachineFunction(MF);
  UsedInInstr.clear();
  UsedInInstr.setUniverse(TRI->getNumRegUnits());

  // initialize the virtual->physical register map to have a 'null'
  // mapping for all virtual registers
  unsigned NumVirtRegs = MRI->getNumVirtRegs();
  StackSlotForVirtReg.resize(NumVirtRegs);
  LiveVirtRegs.setUniverse(NumVirtRegs);

  // Loop over all of the basic blocks, eliminating virtual register references
  for (MachineBasicBlock &MBB : MF)
    allocateBasicBlock(MBB);

  // All machine operands and other references to virtual registers have been
  // replaced. Remove the virtual registers.
  MRI->clearVirtRegs();

  StackSlotForVirtReg.clear();
  LiveDbgValueMap.clear();
  return true;
}

FunctionPass *llvm::createFastRegisterAllocator() {
  return new RegAllocFast();
}
