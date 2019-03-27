//===- lib/CodeGen/MachineInstr.cpp ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Methods common to all machine instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/GlobalISel/RegisterBank.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Operator.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LowLevelTypeImpl.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>

using namespace llvm;

static const MachineFunction *getMFIfAvailable(const MachineInstr &MI) {
  if (const MachineBasicBlock *MBB = MI.getParent())
    if (const MachineFunction *MF = MBB->getParent())
      return MF;
  return nullptr;
}

// Try to crawl up to the machine function and get TRI and IntrinsicInfo from
// it.
static void tryToGetTargetInfo(const MachineInstr &MI,
                               const TargetRegisterInfo *&TRI,
                               const MachineRegisterInfo *&MRI,
                               const TargetIntrinsicInfo *&IntrinsicInfo,
                               const TargetInstrInfo *&TII) {

  if (const MachineFunction *MF = getMFIfAvailable(MI)) {
    TRI = MF->getSubtarget().getRegisterInfo();
    MRI = &MF->getRegInfo();
    IntrinsicInfo = MF->getTarget().getIntrinsicInfo();
    TII = MF->getSubtarget().getInstrInfo();
  }
}

void MachineInstr::addImplicitDefUseOperands(MachineFunction &MF) {
  if (MCID->ImplicitDefs)
    for (const MCPhysReg *ImpDefs = MCID->getImplicitDefs(); *ImpDefs;
           ++ImpDefs)
      addOperand(MF, MachineOperand::CreateReg(*ImpDefs, true, true));
  if (MCID->ImplicitUses)
    for (const MCPhysReg *ImpUses = MCID->getImplicitUses(); *ImpUses;
           ++ImpUses)
      addOperand(MF, MachineOperand::CreateReg(*ImpUses, false, true));
}

/// MachineInstr ctor - This constructor creates a MachineInstr and adds the
/// implicit operands. It reserves space for the number of operands specified by
/// the MCInstrDesc.
MachineInstr::MachineInstr(MachineFunction &MF, const MCInstrDesc &tid,
                           DebugLoc dl, bool NoImp)
    : MCID(&tid), debugLoc(std::move(dl)) {
  assert(debugLoc.hasTrivialDestructor() && "Expected trivial destructor");

  // Reserve space for the expected number of operands.
  if (unsigned NumOps = MCID->getNumOperands() +
    MCID->getNumImplicitDefs() + MCID->getNumImplicitUses()) {
    CapOperands = OperandCapacity::get(NumOps);
    Operands = MF.allocateOperandArray(CapOperands);
  }

  if (!NoImp)
    addImplicitDefUseOperands(MF);
}

/// MachineInstr ctor - Copies MachineInstr arg exactly
///
MachineInstr::MachineInstr(MachineFunction &MF, const MachineInstr &MI)
    : MCID(&MI.getDesc()), Info(MI.Info), debugLoc(MI.getDebugLoc()) {
  assert(debugLoc.hasTrivialDestructor() && "Expected trivial destructor");

  CapOperands = OperandCapacity::get(MI.getNumOperands());
  Operands = MF.allocateOperandArray(CapOperands);

  // Copy operands.
  for (const MachineOperand &MO : MI.operands())
    addOperand(MF, MO);

  // Copy all the sensible flags.
  setFlags(MI.Flags);
}

/// getRegInfo - If this instruction is embedded into a MachineFunction,
/// return the MachineRegisterInfo object for the current function, otherwise
/// return null.
MachineRegisterInfo *MachineInstr::getRegInfo() {
  if (MachineBasicBlock *MBB = getParent())
    return &MBB->getParent()->getRegInfo();
  return nullptr;
}

/// RemoveRegOperandsFromUseLists - Unlink all of the register operands in
/// this instruction from their respective use lists.  This requires that the
/// operands already be on their use lists.
void MachineInstr::RemoveRegOperandsFromUseLists(MachineRegisterInfo &MRI) {
  for (MachineOperand &MO : operands())
    if (MO.isReg())
      MRI.removeRegOperandFromUseList(&MO);
}

/// AddRegOperandsToUseLists - Add all of the register operands in
/// this instruction from their respective use lists.  This requires that the
/// operands not be on their use lists yet.
void MachineInstr::AddRegOperandsToUseLists(MachineRegisterInfo &MRI) {
  for (MachineOperand &MO : operands())
    if (MO.isReg())
      MRI.addRegOperandToUseList(&MO);
}

void MachineInstr::addOperand(const MachineOperand &Op) {
  MachineBasicBlock *MBB = getParent();
  assert(MBB && "Use MachineInstrBuilder to add operands to dangling instrs");
  MachineFunction *MF = MBB->getParent();
  assert(MF && "Use MachineInstrBuilder to add operands to dangling instrs");
  addOperand(*MF, Op);
}

/// Move NumOps MachineOperands from Src to Dst, with support for overlapping
/// ranges. If MRI is non-null also update use-def chains.
static void moveOperands(MachineOperand *Dst, MachineOperand *Src,
                         unsigned NumOps, MachineRegisterInfo *MRI) {
  if (MRI)
    return MRI->moveOperands(Dst, Src, NumOps);

  // MachineOperand is a trivially copyable type so we can just use memmove.
  std::memmove(Dst, Src, NumOps * sizeof(MachineOperand));
}

/// addOperand - Add the specified operand to the instruction.  If it is an
/// implicit operand, it is added to the end of the operand list.  If it is
/// an explicit operand it is added at the end of the explicit operand list
/// (before the first implicit operand).
void MachineInstr::addOperand(MachineFunction &MF, const MachineOperand &Op) {
  assert(MCID && "Cannot add operands before providing an instr descriptor");

  // Check if we're adding one of our existing operands.
  if (&Op >= Operands && &Op < Operands + NumOperands) {
    // This is unusual: MI->addOperand(MI->getOperand(i)).
    // If adding Op requires reallocating or moving existing operands around,
    // the Op reference could go stale. Support it by copying Op.
    MachineOperand CopyOp(Op);
    return addOperand(MF, CopyOp);
  }

  // Find the insert location for the new operand.  Implicit registers go at
  // the end, everything else goes before the implicit regs.
  //
  // FIXME: Allow mixed explicit and implicit operands on inline asm.
  // InstrEmitter::EmitSpecialNode() is marking inline asm clobbers as
  // implicit-defs, but they must not be moved around.  See the FIXME in
  // InstrEmitter.cpp.
  unsigned OpNo = getNumOperands();
  bool isImpReg = Op.isReg() && Op.isImplicit();
  if (!isImpReg && !isInlineAsm()) {
    while (OpNo && Operands[OpNo-1].isReg() && Operands[OpNo-1].isImplicit()) {
      --OpNo;
      assert(!Operands[OpNo].isTied() && "Cannot move tied operands");
    }
  }

#ifndef NDEBUG
  bool isDebugOp = Op.getType() == MachineOperand::MO_Metadata ||
                   Op.getType() == MachineOperand::MO_MCSymbol;
  // OpNo now points as the desired insertion point.  Unless this is a variadic
  // instruction, only implicit regs are allowed beyond MCID->getNumOperands().
  // RegMask operands go between the explicit and implicit operands.
  assert((isImpReg || Op.isRegMask() || MCID->isVariadic() ||
          OpNo < MCID->getNumOperands() || isDebugOp) &&
         "Trying to add an operand to a machine instr that is already done!");
#endif

  MachineRegisterInfo *MRI = getRegInfo();

  // Determine if the Operands array needs to be reallocated.
  // Save the old capacity and operand array.
  OperandCapacity OldCap = CapOperands;
  MachineOperand *OldOperands = Operands;
  if (!OldOperands || OldCap.getSize() == getNumOperands()) {
    CapOperands = OldOperands ? OldCap.getNext() : OldCap.get(1);
    Operands = MF.allocateOperandArray(CapOperands);
    // Move the operands before the insertion point.
    if (OpNo)
      moveOperands(Operands, OldOperands, OpNo, MRI);
  }

  // Move the operands following the insertion point.
  if (OpNo != NumOperands)
    moveOperands(Operands + OpNo + 1, OldOperands + OpNo, NumOperands - OpNo,
                 MRI);
  ++NumOperands;

  // Deallocate the old operand array.
  if (OldOperands != Operands && OldOperands)
    MF.deallocateOperandArray(OldCap, OldOperands);

  // Copy Op into place. It still needs to be inserted into the MRI use lists.
  MachineOperand *NewMO = new (Operands + OpNo) MachineOperand(Op);
  NewMO->ParentMI = this;

  // When adding a register operand, tell MRI about it.
  if (NewMO->isReg()) {
    // Ensure isOnRegUseList() returns false, regardless of Op's status.
    NewMO->Contents.Reg.Prev = nullptr;
    // Ignore existing ties. This is not a property that can be copied.
    NewMO->TiedTo = 0;
    // Add the new operand to MRI, but only for instructions in an MBB.
    if (MRI)
      MRI->addRegOperandToUseList(NewMO);
    // The MCID operand information isn't accurate until we start adding
    // explicit operands. The implicit operands are added first, then the
    // explicits are inserted before them.
    if (!isImpReg) {
      // Tie uses to defs as indicated in MCInstrDesc.
      if (NewMO->isUse()) {
        int DefIdx = MCID->getOperandConstraint(OpNo, MCOI::TIED_TO);
        if (DefIdx != -1)
          tieOperands(DefIdx, OpNo);
      }
      // If the register operand is flagged as early, mark the operand as such.
      if (MCID->getOperandConstraint(OpNo, MCOI::EARLY_CLOBBER) != -1)
        NewMO->setIsEarlyClobber(true);
    }
  }
}

/// RemoveOperand - Erase an operand  from an instruction, leaving it with one
/// fewer operand than it started with.
///
void MachineInstr::RemoveOperand(unsigned OpNo) {
  assert(OpNo < getNumOperands() && "Invalid operand number");
  untieRegOperand(OpNo);

#ifndef NDEBUG
  // Moving tied operands would break the ties.
  for (unsigned i = OpNo + 1, e = getNumOperands(); i != e; ++i)
    if (Operands[i].isReg())
      assert(!Operands[i].isTied() && "Cannot move tied operands");
#endif

  MachineRegisterInfo *MRI = getRegInfo();
  if (MRI && Operands[OpNo].isReg())
    MRI->removeRegOperandFromUseList(Operands + OpNo);

  // Don't call the MachineOperand destructor. A lot of this code depends on
  // MachineOperand having a trivial destructor anyway, and adding a call here
  // wouldn't make it 'destructor-correct'.

  if (unsigned N = NumOperands - 1 - OpNo)
    moveOperands(Operands + OpNo, Operands + OpNo + 1, N, MRI);
  --NumOperands;
}

void MachineInstr::dropMemRefs(MachineFunction &MF) {
  if (memoperands_empty())
    return;

  // See if we can just drop all of our extra info.
  if (!getPreInstrSymbol() && !getPostInstrSymbol()) {
    Info.clear();
    return;
  }
  if (!getPostInstrSymbol()) {
    Info.set<EIIK_PreInstrSymbol>(getPreInstrSymbol());
    return;
  }
  if (!getPreInstrSymbol()) {
    Info.set<EIIK_PostInstrSymbol>(getPostInstrSymbol());
    return;
  }

  // Otherwise allocate a fresh extra info with just these symbols.
  Info.set<EIIK_OutOfLine>(
      MF.createMIExtraInfo({}, getPreInstrSymbol(), getPostInstrSymbol()));
}

void MachineInstr::setMemRefs(MachineFunction &MF,
                              ArrayRef<MachineMemOperand *> MMOs) {
  if (MMOs.empty()) {
    dropMemRefs(MF);
    return;
  }

  // Try to store a single MMO inline.
  if (MMOs.size() == 1 && !getPreInstrSymbol() && !getPostInstrSymbol()) {
    Info.set<EIIK_MMO>(MMOs[0]);
    return;
  }

  // Otherwise create an extra info struct with all of our info.
  Info.set<EIIK_OutOfLine>(
      MF.createMIExtraInfo(MMOs, getPreInstrSymbol(), getPostInstrSymbol()));
}

void MachineInstr::addMemOperand(MachineFunction &MF,
                                 MachineMemOperand *MO) {
  SmallVector<MachineMemOperand *, 2> MMOs;
  MMOs.append(memoperands_begin(), memoperands_end());
  MMOs.push_back(MO);
  setMemRefs(MF, MMOs);
}

void MachineInstr::cloneMemRefs(MachineFunction &MF, const MachineInstr &MI) {
  if (this == &MI)
    // Nothing to do for a self-clone!
    return;

  assert(&MF == MI.getMF() &&
         "Invalid machine functions when cloning memory refrences!");
  // See if we can just steal the extra info already allocated for the
  // instruction. We can do this whenever the pre- and post-instruction symbols
  // are the same (including null).
  if (getPreInstrSymbol() == MI.getPreInstrSymbol() &&
      getPostInstrSymbol() == MI.getPostInstrSymbol()) {
    Info = MI.Info;
    return;
  }

  // Otherwise, fall back on a copy-based clone.
  setMemRefs(MF, MI.memoperands());
}

/// Check to see if the MMOs pointed to by the two MemRefs arrays are
/// identical.
static bool hasIdenticalMMOs(ArrayRef<MachineMemOperand *> LHS,
                             ArrayRef<MachineMemOperand *> RHS) {
  if (LHS.size() != RHS.size())
    return false;

  auto LHSPointees = make_pointee_range(LHS);
  auto RHSPointees = make_pointee_range(RHS);
  return std::equal(LHSPointees.begin(), LHSPointees.end(),
                    RHSPointees.begin());
}

void MachineInstr::cloneMergedMemRefs(MachineFunction &MF,
                                      ArrayRef<const MachineInstr *> MIs) {
  // Try handling easy numbers of MIs with simpler mechanisms.
  if (MIs.empty()) {
    dropMemRefs(MF);
    return;
  }
  if (MIs.size() == 1) {
    cloneMemRefs(MF, *MIs[0]);
    return;
  }
  // Because an empty memoperands list provides *no* information and must be
  // handled conservatively (assuming the instruction can do anything), the only
  // way to merge with it is to drop all other memoperands.
  if (MIs[0]->memoperands_empty()) {
    dropMemRefs(MF);
    return;
  }

  // Handle the general case.
  SmallVector<MachineMemOperand *, 2> MergedMMOs;
  // Start with the first instruction.
  assert(&MF == MIs[0]->getMF() &&
         "Invalid machine functions when cloning memory references!");
  MergedMMOs.append(MIs[0]->memoperands_begin(), MIs[0]->memoperands_end());
  // Now walk all the other instructions and accumulate any different MMOs.
  for (const MachineInstr &MI : make_pointee_range(MIs.slice(1))) {
    assert(&MF == MI.getMF() &&
           "Invalid machine functions when cloning memory references!");

    // Skip MIs with identical operands to the first. This is a somewhat
    // arbitrary hack but will catch common cases without being quadratic.
    // TODO: We could fully implement merge semantics here if needed.
    if (hasIdenticalMMOs(MIs[0]->memoperands(), MI.memoperands()))
      continue;

    // Because an empty memoperands list provides *no* information and must be
    // handled conservatively (assuming the instruction can do anything), the
    // only way to merge with it is to drop all other memoperands.
    if (MI.memoperands_empty()) {
      dropMemRefs(MF);
      return;
    }

    // Otherwise accumulate these into our temporary buffer of the merged state.
    MergedMMOs.append(MI.memoperands_begin(), MI.memoperands_end());
  }

  setMemRefs(MF, MergedMMOs);
}

void MachineInstr::setPreInstrSymbol(MachineFunction &MF, MCSymbol *Symbol) {
  MCSymbol *OldSymbol = getPreInstrSymbol();
  if (OldSymbol == Symbol)
    return;
  if (OldSymbol && !Symbol) {
    // We're removing a symbol rather than adding one. Try to clean up any
    // extra info carried around.
    if (Info.is<EIIK_PreInstrSymbol>()) {
      Info.clear();
      return;
    }

    if (memoperands_empty()) {
      assert(getPostInstrSymbol() &&
             "Should never have only a single symbol allocated out-of-line!");
      Info.set<EIIK_PostInstrSymbol>(getPostInstrSymbol());
      return;
    }

    // Otherwise fallback on the generic update.
  } else if (!Info || Info.is<EIIK_PreInstrSymbol>()) {
    // If we don't have any other extra info, we can store this inline.
    Info.set<EIIK_PreInstrSymbol>(Symbol);
    return;
  }

  // Otherwise, allocate a full new set of extra info.
  // FIXME: Maybe we should make the symbols in the extra info mutable?
  Info.set<EIIK_OutOfLine>(
      MF.createMIExtraInfo(memoperands(), Symbol, getPostInstrSymbol()));
}

void MachineInstr::setPostInstrSymbol(MachineFunction &MF, MCSymbol *Symbol) {
  MCSymbol *OldSymbol = getPostInstrSymbol();
  if (OldSymbol == Symbol)
    return;
  if (OldSymbol && !Symbol) {
    // We're removing a symbol rather than adding one. Try to clean up any
    // extra info carried around.
    if (Info.is<EIIK_PostInstrSymbol>()) {
      Info.clear();
      return;
    }

    if (memoperands_empty()) {
      assert(getPreInstrSymbol() &&
             "Should never have only a single symbol allocated out-of-line!");
      Info.set<EIIK_PreInstrSymbol>(getPreInstrSymbol());
      return;
    }

    // Otherwise fallback on the generic update.
  } else if (!Info || Info.is<EIIK_PostInstrSymbol>()) {
    // If we don't have any other extra info, we can store this inline.
    Info.set<EIIK_PostInstrSymbol>(Symbol);
    return;
  }

  // Otherwise, allocate a full new set of extra info.
  // FIXME: Maybe we should make the symbols in the extra info mutable?
  Info.set<EIIK_OutOfLine>(
      MF.createMIExtraInfo(memoperands(), getPreInstrSymbol(), Symbol));
}

uint16_t MachineInstr::mergeFlagsWith(const MachineInstr &Other) const {
  // For now, the just return the union of the flags. If the flags get more
  // complicated over time, we might need more logic here.
  return getFlags() | Other.getFlags();
}

void MachineInstr::copyIRFlags(const Instruction &I) {
  // Copy the wrapping flags.
  if (const OverflowingBinaryOperator *OB =
          dyn_cast<OverflowingBinaryOperator>(&I)) {
    if (OB->hasNoSignedWrap())
      setFlag(MachineInstr::MIFlag::NoSWrap);
    if (OB->hasNoUnsignedWrap())
      setFlag(MachineInstr::MIFlag::NoUWrap);
  }

  // Copy the exact flag.
  if (const PossiblyExactOperator *PE = dyn_cast<PossiblyExactOperator>(&I))
    if (PE->isExact())
      setFlag(MachineInstr::MIFlag::IsExact);

  // Copy the fast-math flags.
  if (const FPMathOperator *FP = dyn_cast<FPMathOperator>(&I)) {
    const FastMathFlags Flags = FP->getFastMathFlags();
    if (Flags.noNaNs())
      setFlag(MachineInstr::MIFlag::FmNoNans);
    if (Flags.noInfs())
      setFlag(MachineInstr::MIFlag::FmNoInfs);
    if (Flags.noSignedZeros())
      setFlag(MachineInstr::MIFlag::FmNsz);
    if (Flags.allowReciprocal())
      setFlag(MachineInstr::MIFlag::FmArcp);
    if (Flags.allowContract())
      setFlag(MachineInstr::MIFlag::FmContract);
    if (Flags.approxFunc())
      setFlag(MachineInstr::MIFlag::FmAfn);
    if (Flags.allowReassoc())
      setFlag(MachineInstr::MIFlag::FmReassoc);
  }
}

bool MachineInstr::hasPropertyInBundle(uint64_t Mask, QueryType Type) const {
  assert(!isBundledWithPred() && "Must be called on bundle header");
  for (MachineBasicBlock::const_instr_iterator MII = getIterator();; ++MII) {
    if (MII->getDesc().getFlags() & Mask) {
      if (Type == AnyInBundle)
        return true;
    } else {
      if (Type == AllInBundle && !MII->isBundle())
        return false;
    }
    // This was the last instruction in the bundle.
    if (!MII->isBundledWithSucc())
      return Type == AllInBundle;
  }
}

bool MachineInstr::isIdenticalTo(const MachineInstr &Other,
                                 MICheckType Check) const {
  // If opcodes or number of operands are not the same then the two
  // instructions are obviously not identical.
  if (Other.getOpcode() != getOpcode() ||
      Other.getNumOperands() != getNumOperands())
    return false;

  if (isBundle()) {
    // We have passed the test above that both instructions have the same
    // opcode, so we know that both instructions are bundles here. Let's compare
    // MIs inside the bundle.
    assert(Other.isBundle() && "Expected that both instructions are bundles.");
    MachineBasicBlock::const_instr_iterator I1 = getIterator();
    MachineBasicBlock::const_instr_iterator I2 = Other.getIterator();
    // Loop until we analysed the last intruction inside at least one of the
    // bundles.
    while (I1->isBundledWithSucc() && I2->isBundledWithSucc()) {
      ++I1;
      ++I2;
      if (!I1->isIdenticalTo(*I2, Check))
        return false;
    }
    // If we've reached the end of just one of the two bundles, but not both,
    // the instructions are not identical.
    if (I1->isBundledWithSucc() || I2->isBundledWithSucc())
      return false;
  }

  // Check operands to make sure they match.
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    const MachineOperand &OMO = Other.getOperand(i);
    if (!MO.isReg()) {
      if (!MO.isIdenticalTo(OMO))
        return false;
      continue;
    }

    // Clients may or may not want to ignore defs when testing for equality.
    // For example, machine CSE pass only cares about finding common
    // subexpressions, so it's safe to ignore virtual register defs.
    if (MO.isDef()) {
      if (Check == IgnoreDefs)
        continue;
      else if (Check == IgnoreVRegDefs) {
        if (!TargetRegisterInfo::isVirtualRegister(MO.getReg()) ||
            !TargetRegisterInfo::isVirtualRegister(OMO.getReg()))
          if (!MO.isIdenticalTo(OMO))
            return false;
      } else {
        if (!MO.isIdenticalTo(OMO))
          return false;
        if (Check == CheckKillDead && MO.isDead() != OMO.isDead())
          return false;
      }
    } else {
      if (!MO.isIdenticalTo(OMO))
        return false;
      if (Check == CheckKillDead && MO.isKill() != OMO.isKill())
        return false;
    }
  }
  // If DebugLoc does not match then two debug instructions are not identical.
  if (isDebugInstr())
    if (getDebugLoc() && Other.getDebugLoc() &&
        getDebugLoc() != Other.getDebugLoc())
      return false;
  return true;
}

const MachineFunction *MachineInstr::getMF() const {
  return getParent()->getParent();
}

MachineInstr *MachineInstr::removeFromParent() {
  assert(getParent() && "Not embedded in a basic block!");
  return getParent()->remove(this);
}

MachineInstr *MachineInstr::removeFromBundle() {
  assert(getParent() && "Not embedded in a basic block!");
  return getParent()->remove_instr(this);
}

void MachineInstr::eraseFromParent() {
  assert(getParent() && "Not embedded in a basic block!");
  getParent()->erase(this);
}

void MachineInstr::eraseFromParentAndMarkDBGValuesForRemoval() {
  assert(getParent() && "Not embedded in a basic block!");
  MachineBasicBlock *MBB = getParent();
  MachineFunction *MF = MBB->getParent();
  assert(MF && "Not embedded in a function!");

  MachineInstr *MI = (MachineInstr *)this;
  MachineRegisterInfo &MRI = MF->getRegInfo();

  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    MRI.markUsesInDebugValueAsUndef(Reg);
  }
  MI->eraseFromParent();
}

void MachineInstr::eraseFromBundle() {
  assert(getParent() && "Not embedded in a basic block!");
  getParent()->erase_instr(this);
}

unsigned MachineInstr::getNumExplicitOperands() const {
  unsigned NumOperands = MCID->getNumOperands();
  if (!MCID->isVariadic())
    return NumOperands;

  for (unsigned I = NumOperands, E = getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = getOperand(I);
    // The operands must always be in the following order:
    // - explicit reg defs,
    // - other explicit operands (reg uses, immediates, etc.),
    // - implicit reg defs
    // - implicit reg uses
    if (MO.isReg() && MO.isImplicit())
      break;
    ++NumOperands;
  }
  return NumOperands;
}

unsigned MachineInstr::getNumExplicitDefs() const {
  unsigned NumDefs = MCID->getNumDefs();
  if (!MCID->isVariadic())
    return NumDefs;

  for (unsigned I = NumDefs, E = getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = getOperand(I);
    if (!MO.isReg() || !MO.isDef() || MO.isImplicit())
      break;
    ++NumDefs;
  }
  return NumDefs;
}

void MachineInstr::bundleWithPred() {
  assert(!isBundledWithPred() && "MI is already bundled with its predecessor");
  setFlag(BundledPred);
  MachineBasicBlock::instr_iterator Pred = getIterator();
  --Pred;
  assert(!Pred->isBundledWithSucc() && "Inconsistent bundle flags");
  Pred->setFlag(BundledSucc);
}

void MachineInstr::bundleWithSucc() {
  assert(!isBundledWithSucc() && "MI is already bundled with its successor");
  setFlag(BundledSucc);
  MachineBasicBlock::instr_iterator Succ = getIterator();
  ++Succ;
  assert(!Succ->isBundledWithPred() && "Inconsistent bundle flags");
  Succ->setFlag(BundledPred);
}

void MachineInstr::unbundleFromPred() {
  assert(isBundledWithPred() && "MI isn't bundled with its predecessor");
  clearFlag(BundledPred);
  MachineBasicBlock::instr_iterator Pred = getIterator();
  --Pred;
  assert(Pred->isBundledWithSucc() && "Inconsistent bundle flags");
  Pred->clearFlag(BundledSucc);
}

void MachineInstr::unbundleFromSucc() {
  assert(isBundledWithSucc() && "MI isn't bundled with its successor");
  clearFlag(BundledSucc);
  MachineBasicBlock::instr_iterator Succ = getIterator();
  ++Succ;
  assert(Succ->isBundledWithPred() && "Inconsistent bundle flags");
  Succ->clearFlag(BundledPred);
}

bool MachineInstr::isStackAligningInlineAsm() const {
  if (isInlineAsm()) {
    unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
    if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
      return true;
  }
  return false;
}

InlineAsm::AsmDialect MachineInstr::getInlineAsmDialect() const {
  assert(isInlineAsm() && "getInlineAsmDialect() only works for inline asms!");
  unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
  return InlineAsm::AsmDialect((ExtraInfo & InlineAsm::Extra_AsmDialect) != 0);
}

int MachineInstr::findInlineAsmFlagIdx(unsigned OpIdx,
                                       unsigned *GroupNo) const {
  assert(isInlineAsm() && "Expected an inline asm instruction");
  assert(OpIdx < getNumOperands() && "OpIdx out of range");

  // Ignore queries about the initial operands.
  if (OpIdx < InlineAsm::MIOp_FirstOperand)
    return -1;

  unsigned Group = 0;
  unsigned NumOps;
  for (unsigned i = InlineAsm::MIOp_FirstOperand, e = getNumOperands(); i < e;
       i += NumOps) {
    const MachineOperand &FlagMO = getOperand(i);
    // If we reach the implicit register operands, stop looking.
    if (!FlagMO.isImm())
      return -1;
    NumOps = 1 + InlineAsm::getNumOperandRegisters(FlagMO.getImm());
    if (i + NumOps > OpIdx) {
      if (GroupNo)
        *GroupNo = Group;
      return i;
    }
    ++Group;
  }
  return -1;
}

const DILabel *MachineInstr::getDebugLabel() const {
  assert(isDebugLabel() && "not a DBG_LABEL");
  return cast<DILabel>(getOperand(0).getMetadata());
}

const DILocalVariable *MachineInstr::getDebugVariable() const {
  assert(isDebugValue() && "not a DBG_VALUE");
  return cast<DILocalVariable>(getOperand(2).getMetadata());
}

const DIExpression *MachineInstr::getDebugExpression() const {
  assert(isDebugValue() && "not a DBG_VALUE");
  return cast<DIExpression>(getOperand(3).getMetadata());
}

const TargetRegisterClass*
MachineInstr::getRegClassConstraint(unsigned OpIdx,
                                    const TargetInstrInfo *TII,
                                    const TargetRegisterInfo *TRI) const {
  assert(getParent() && "Can't have an MBB reference here!");
  assert(getMF() && "Can't have an MF reference here!");
  const MachineFunction &MF = *getMF();

  // Most opcodes have fixed constraints in their MCInstrDesc.
  if (!isInlineAsm())
    return TII->getRegClass(getDesc(), OpIdx, TRI, MF);

  if (!getOperand(OpIdx).isReg())
    return nullptr;

  // For tied uses on inline asm, get the constraint from the def.
  unsigned DefIdx;
  if (getOperand(OpIdx).isUse() && isRegTiedToDefOperand(OpIdx, &DefIdx))
    OpIdx = DefIdx;

  // Inline asm stores register class constraints in the flag word.
  int FlagIdx = findInlineAsmFlagIdx(OpIdx);
  if (FlagIdx < 0)
    return nullptr;

  unsigned Flag = getOperand(FlagIdx).getImm();
  unsigned RCID;
  if ((InlineAsm::getKind(Flag) == InlineAsm::Kind_RegUse ||
       InlineAsm::getKind(Flag) == InlineAsm::Kind_RegDef ||
       InlineAsm::getKind(Flag) == InlineAsm::Kind_RegDefEarlyClobber) &&
      InlineAsm::hasRegClassConstraint(Flag, RCID))
    return TRI->getRegClass(RCID);

  // Assume that all registers in a memory operand are pointers.
  if (InlineAsm::getKind(Flag) == InlineAsm::Kind_Mem)
    return TRI->getPointerRegClass(MF);

  return nullptr;
}

const TargetRegisterClass *MachineInstr::getRegClassConstraintEffectForVReg(
    unsigned Reg, const TargetRegisterClass *CurRC, const TargetInstrInfo *TII,
    const TargetRegisterInfo *TRI, bool ExploreBundle) const {
  // Check every operands inside the bundle if we have
  // been asked to.
  if (ExploreBundle)
    for (ConstMIBundleOperands OpndIt(*this); OpndIt.isValid() && CurRC;
         ++OpndIt)
      CurRC = OpndIt->getParent()->getRegClassConstraintEffectForVRegImpl(
          OpndIt.getOperandNo(), Reg, CurRC, TII, TRI);
  else
    // Otherwise, just check the current operands.
    for (unsigned i = 0, e = NumOperands; i < e && CurRC; ++i)
      CurRC = getRegClassConstraintEffectForVRegImpl(i, Reg, CurRC, TII, TRI);
  return CurRC;
}

const TargetRegisterClass *MachineInstr::getRegClassConstraintEffectForVRegImpl(
    unsigned OpIdx, unsigned Reg, const TargetRegisterClass *CurRC,
    const TargetInstrInfo *TII, const TargetRegisterInfo *TRI) const {
  assert(CurRC && "Invalid initial register class");
  // Check if Reg is constrained by some of its use/def from MI.
  const MachineOperand &MO = getOperand(OpIdx);
  if (!MO.isReg() || MO.getReg() != Reg)
    return CurRC;
  // If yes, accumulate the constraints through the operand.
  return getRegClassConstraintEffect(OpIdx, CurRC, TII, TRI);
}

const TargetRegisterClass *MachineInstr::getRegClassConstraintEffect(
    unsigned OpIdx, const TargetRegisterClass *CurRC,
    const TargetInstrInfo *TII, const TargetRegisterInfo *TRI) const {
  const TargetRegisterClass *OpRC = getRegClassConstraint(OpIdx, TII, TRI);
  const MachineOperand &MO = getOperand(OpIdx);
  assert(MO.isReg() &&
         "Cannot get register constraints for non-register operand");
  assert(CurRC && "Invalid initial register class");
  if (unsigned SubIdx = MO.getSubReg()) {
    if (OpRC)
      CurRC = TRI->getMatchingSuperRegClass(CurRC, OpRC, SubIdx);
    else
      CurRC = TRI->getSubClassWithSubReg(CurRC, SubIdx);
  } else if (OpRC)
    CurRC = TRI->getCommonSubClass(CurRC, OpRC);
  return CurRC;
}

/// Return the number of instructions inside the MI bundle, not counting the
/// header instruction.
unsigned MachineInstr::getBundleSize() const {
  MachineBasicBlock::const_instr_iterator I = getIterator();
  unsigned Size = 0;
  while (I->isBundledWithSucc()) {
    ++Size;
    ++I;
  }
  return Size;
}

/// Returns true if the MachineInstr has an implicit-use operand of exactly
/// the given register (not considering sub/super-registers).
bool MachineInstr::hasRegisterImplicitUseOperand(unsigned Reg) const {
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    if (MO.isReg() && MO.isUse() && MO.isImplicit() && MO.getReg() == Reg)
      return true;
  }
  return false;
}

/// findRegisterUseOperandIdx() - Returns the MachineOperand that is a use of
/// the specific register or -1 if it is not found. It further tightens
/// the search criteria to a use that kills the register if isKill is true.
int MachineInstr::findRegisterUseOperandIdx(
    unsigned Reg, bool isKill, const TargetRegisterInfo *TRI) const {
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || !MO.isUse())
      continue;
    unsigned MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MOReg == Reg || (TRI && Reg && MOReg && TRI->regsOverlap(MOReg, Reg)))
      if (!isKill || MO.isKill())
        return i;
  }
  return -1;
}

/// readsWritesVirtualRegister - Return a pair of bools (reads, writes)
/// indicating if this instruction reads or writes Reg. This also considers
/// partial defines.
std::pair<bool,bool>
MachineInstr::readsWritesVirtualRegister(unsigned Reg,
                                         SmallVectorImpl<unsigned> *Ops) const {
  bool PartDef = false; // Partial redefine.
  bool FullDef = false; // Full define.
  bool Use = false;

  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || MO.getReg() != Reg)
      continue;
    if (Ops)
      Ops->push_back(i);
    if (MO.isUse())
      Use |= !MO.isUndef();
    else if (MO.getSubReg() && !MO.isUndef())
      // A partial def undef doesn't count as reading the register.
      PartDef = true;
    else
      FullDef = true;
  }
  // A partial redefine uses Reg unless there is also a full define.
  return std::make_pair(Use || (PartDef && !FullDef), PartDef || FullDef);
}

/// findRegisterDefOperandIdx() - Returns the operand index that is a def of
/// the specified register or -1 if it is not found. If isDead is true, defs
/// that are not dead are skipped. If TargetRegisterInfo is non-null, then it
/// also checks if there is a def of a super-register.
int
MachineInstr::findRegisterDefOperandIdx(unsigned Reg, bool isDead, bool Overlap,
                                        const TargetRegisterInfo *TRI) const {
  bool isPhys = TargetRegisterInfo::isPhysicalRegister(Reg);
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);
    // Accept regmask operands when Overlap is set.
    // Ignore them when looking for a specific def operand (Overlap == false).
    if (isPhys && Overlap && MO.isRegMask() && MO.clobbersPhysReg(Reg))
      return i;
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned MOReg = MO.getReg();
    bool Found = (MOReg == Reg);
    if (!Found && TRI && isPhys &&
        TargetRegisterInfo::isPhysicalRegister(MOReg)) {
      if (Overlap)
        Found = TRI->regsOverlap(MOReg, Reg);
      else
        Found = TRI->isSubRegister(MOReg, Reg);
    }
    if (Found && (!isDead || MO.isDead()))
      return i;
  }
  return -1;
}

/// findFirstPredOperandIdx() - Find the index of the first operand in the
/// operand list that is used to represent the predicate. It returns -1 if
/// none is found.
int MachineInstr::findFirstPredOperandIdx() const {
  // Don't call MCID.findFirstPredOperandIdx() because this variant
  // is sometimes called on an instruction that's not yet complete, and
  // so the number of operands is less than the MCID indicates. In
  // particular, the PTX target does this.
  const MCInstrDesc &MCID = getDesc();
  if (MCID.isPredicable()) {
    for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
      if (MCID.OpInfo[i].isPredicate())
        return i;
  }

  return -1;
}

// MachineOperand::TiedTo is 4 bits wide.
const unsigned TiedMax = 15;

/// tieOperands - Mark operands at DefIdx and UseIdx as tied to each other.
///
/// Use and def operands can be tied together, indicated by a non-zero TiedTo
/// field. TiedTo can have these values:
///
/// 0:              Operand is not tied to anything.
/// 1 to TiedMax-1: Tied to getOperand(TiedTo-1).
/// TiedMax:        Tied to an operand >= TiedMax-1.
///
/// The tied def must be one of the first TiedMax operands on a normal
/// instruction. INLINEASM instructions allow more tied defs.
///
void MachineInstr::tieOperands(unsigned DefIdx, unsigned UseIdx) {
  MachineOperand &DefMO = getOperand(DefIdx);
  MachineOperand &UseMO = getOperand(UseIdx);
  assert(DefMO.isDef() && "DefIdx must be a def operand");
  assert(UseMO.isUse() && "UseIdx must be a use operand");
  assert(!DefMO.isTied() && "Def is already tied to another use");
  assert(!UseMO.isTied() && "Use is already tied to another def");

  if (DefIdx < TiedMax)
    UseMO.TiedTo = DefIdx + 1;
  else {
    // Inline asm can use the group descriptors to find tied operands, but on
    // normal instruction, the tied def must be within the first TiedMax
    // operands.
    assert(isInlineAsm() && "DefIdx out of range");
    UseMO.TiedTo = TiedMax;
  }

  // UseIdx can be out of range, we'll search for it in findTiedOperandIdx().
  DefMO.TiedTo = std::min(UseIdx + 1, TiedMax);
}

/// Given the index of a tied register operand, find the operand it is tied to.
/// Defs are tied to uses and vice versa. Returns the index of the tied operand
/// which must exist.
unsigned MachineInstr::findTiedOperandIdx(unsigned OpIdx) const {
  const MachineOperand &MO = getOperand(OpIdx);
  assert(MO.isTied() && "Operand isn't tied");

  // Normally TiedTo is in range.
  if (MO.TiedTo < TiedMax)
    return MO.TiedTo - 1;

  // Uses on normal instructions can be out of range.
  if (!isInlineAsm()) {
    // Normal tied defs must be in the 0..TiedMax-1 range.
    if (MO.isUse())
      return TiedMax - 1;
    // MO is a def. Search for the tied use.
    for (unsigned i = TiedMax - 1, e = getNumOperands(); i != e; ++i) {
      const MachineOperand &UseMO = getOperand(i);
      if (UseMO.isReg() && UseMO.isUse() && UseMO.TiedTo == OpIdx + 1)
        return i;
    }
    llvm_unreachable("Can't find tied use");
  }

  // Now deal with inline asm by parsing the operand group descriptor flags.
  // Find the beginning of each operand group.
  SmallVector<unsigned, 8> GroupIdx;
  unsigned OpIdxGroup = ~0u;
  unsigned NumOps;
  for (unsigned i = InlineAsm::MIOp_FirstOperand, e = getNumOperands(); i < e;
       i += NumOps) {
    const MachineOperand &FlagMO = getOperand(i);
    assert(FlagMO.isImm() && "Invalid tied operand on inline asm");
    unsigned CurGroup = GroupIdx.size();
    GroupIdx.push_back(i);
    NumOps = 1 + InlineAsm::getNumOperandRegisters(FlagMO.getImm());
    // OpIdx belongs to this operand group.
    if (OpIdx > i && OpIdx < i + NumOps)
      OpIdxGroup = CurGroup;
    unsigned TiedGroup;
    if (!InlineAsm::isUseOperandTiedToDef(FlagMO.getImm(), TiedGroup))
      continue;
    // Operands in this group are tied to operands in TiedGroup which must be
    // earlier. Find the number of operands between the two groups.
    unsigned Delta = i - GroupIdx[TiedGroup];

    // OpIdx is a use tied to TiedGroup.
    if (OpIdxGroup == CurGroup)
      return OpIdx - Delta;

    // OpIdx is a def tied to this use group.
    if (OpIdxGroup == TiedGroup)
      return OpIdx + Delta;
  }
  llvm_unreachable("Invalid tied operand on inline asm");
}

/// clearKillInfo - Clears kill flags on all operands.
///
void MachineInstr::clearKillInfo() {
  for (MachineOperand &MO : operands()) {
    if (MO.isReg() && MO.isUse())
      MO.setIsKill(false);
  }
}

void MachineInstr::substituteRegister(unsigned FromReg, unsigned ToReg,
                                      unsigned SubIdx,
                                      const TargetRegisterInfo &RegInfo) {
  if (TargetRegisterInfo::isPhysicalRegister(ToReg)) {
    if (SubIdx)
      ToReg = RegInfo.getSubReg(ToReg, SubIdx);
    for (MachineOperand &MO : operands()) {
      if (!MO.isReg() || MO.getReg() != FromReg)
        continue;
      MO.substPhysReg(ToReg, RegInfo);
    }
  } else {
    for (MachineOperand &MO : operands()) {
      if (!MO.isReg() || MO.getReg() != FromReg)
        continue;
      MO.substVirtReg(ToReg, SubIdx, RegInfo);
    }
  }
}

/// isSafeToMove - Return true if it is safe to move this instruction. If
/// SawStore is set to true, it means that there is a store (or call) between
/// the instruction's location and its intended destination.
bool MachineInstr::isSafeToMove(AliasAnalysis *AA, bool &SawStore) const {
  // Ignore stuff that we obviously can't move.
  //
  // Treat volatile loads as stores. This is not strictly necessary for
  // volatiles, but it is required for atomic loads. It is not allowed to move
  // a load across an atomic load with Ordering > Monotonic.
  if (mayStore() || isCall() || isPHI() ||
      (mayLoad() && hasOrderedMemoryRef())) {
    SawStore = true;
    return false;
  }

  if (isPosition() || isDebugInstr() || isTerminator() ||
      hasUnmodeledSideEffects())
    return false;

  // See if this instruction does a load.  If so, we have to guarantee that the
  // loaded value doesn't change between the load and the its intended
  // destination. The check for isInvariantLoad gives the targe the chance to
  // classify the load as always returning a constant, e.g. a constant pool
  // load.
  if (mayLoad() && !isDereferenceableInvariantLoad(AA))
    // Otherwise, this is a real load.  If there is a store between the load and
    // end of block, we can't move it.
    return !SawStore;

  return true;
}

bool MachineInstr::mayAlias(AliasAnalysis *AA, MachineInstr &Other,
                            bool UseTBAA) {
  const MachineFunction *MF = getMF();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();

  // If neither instruction stores to memory, they can't alias in any
  // meaningful way, even if they read from the same address.
  if (!mayStore() && !Other.mayStore())
    return false;

  // Let the target decide if memory accesses cannot possibly overlap.
  if (TII->areMemAccessesTriviallyDisjoint(*this, Other, AA))
    return false;

  // FIXME: Need to handle multiple memory operands to support all targets.
  if (!hasOneMemOperand() || !Other.hasOneMemOperand())
    return true;

  MachineMemOperand *MMOa = *memoperands_begin();
  MachineMemOperand *MMOb = *Other.memoperands_begin();

  // The following interface to AA is fashioned after DAGCombiner::isAlias
  // and operates with MachineMemOperand offset with some important
  // assumptions:
  //   - LLVM fundamentally assumes flat address spaces.
  //   - MachineOperand offset can *only* result from legalization and
  //     cannot affect queries other than the trivial case of overlap
  //     checking.
  //   - These offsets never wrap and never step outside
  //     of allocated objects.
  //   - There should never be any negative offsets here.
  //
  // FIXME: Modify API to hide this math from "user"
  // Even before we go to AA we can reason locally about some
  // memory objects. It can save compile time, and possibly catch some
  // corner cases not currently covered.

  int64_t OffsetA = MMOa->getOffset();
  int64_t OffsetB = MMOb->getOffset();
  int64_t MinOffset = std::min(OffsetA, OffsetB);

  uint64_t WidthA = MMOa->getSize();
  uint64_t WidthB = MMOb->getSize();
  bool KnownWidthA = WidthA != MemoryLocation::UnknownSize;
  bool KnownWidthB = WidthB != MemoryLocation::UnknownSize;

  const Value *ValA = MMOa->getValue();
  const Value *ValB = MMOb->getValue();
  bool SameVal = (ValA && ValB && (ValA == ValB));
  if (!SameVal) {
    const PseudoSourceValue *PSVa = MMOa->getPseudoValue();
    const PseudoSourceValue *PSVb = MMOb->getPseudoValue();
    if (PSVa && ValB && !PSVa->mayAlias(&MFI))
      return false;
    if (PSVb && ValA && !PSVb->mayAlias(&MFI))
      return false;
    if (PSVa && PSVb && (PSVa == PSVb))
      SameVal = true;
  }

  if (SameVal) {
    if (!KnownWidthA || !KnownWidthB)
      return true;
    int64_t MaxOffset = std::max(OffsetA, OffsetB);
    int64_t LowWidth = (MinOffset == OffsetA) ? WidthA : WidthB;
    return (MinOffset + LowWidth > MaxOffset);
  }

  if (!AA)
    return true;

  if (!ValA || !ValB)
    return true;

  assert((OffsetA >= 0) && "Negative MachineMemOperand offset");
  assert((OffsetB >= 0) && "Negative MachineMemOperand offset");

  int64_t OverlapA = KnownWidthA ? WidthA + OffsetA - MinOffset
                                 : MemoryLocation::UnknownSize;
  int64_t OverlapB = KnownWidthB ? WidthB + OffsetB - MinOffset
                                 : MemoryLocation::UnknownSize;

  AliasResult AAResult = AA->alias(
      MemoryLocation(ValA, OverlapA,
                     UseTBAA ? MMOa->getAAInfo() : AAMDNodes()),
      MemoryLocation(ValB, OverlapB,
                     UseTBAA ? MMOb->getAAInfo() : AAMDNodes()));

  return (AAResult != NoAlias);
}

/// hasOrderedMemoryRef - Return true if this instruction may have an ordered
/// or volatile memory reference, or if the information describing the memory
/// reference is not available. Return false if it is known to have no ordered
/// memory references.
bool MachineInstr::hasOrderedMemoryRef() const {
  // An instruction known never to access memory won't have a volatile access.
  if (!mayStore() &&
      !mayLoad() &&
      !isCall() &&
      !hasUnmodeledSideEffects())
    return false;

  // Otherwise, if the instruction has no memory reference information,
  // conservatively assume it wasn't preserved.
  if (memoperands_empty())
    return true;

  // Check if any of our memory operands are ordered.
  return llvm::any_of(memoperands(), [](const MachineMemOperand *MMO) {
    return !MMO->isUnordered();
  });
}

/// isDereferenceableInvariantLoad - Return true if this instruction will never
/// trap and is loading from a location whose value is invariant across a run of
/// this function.
bool MachineInstr::isDereferenceableInvariantLoad(AliasAnalysis *AA) const {
  // If the instruction doesn't load at all, it isn't an invariant load.
  if (!mayLoad())
    return false;

  // If the instruction has lost its memoperands, conservatively assume that
  // it may not be an invariant load.
  if (memoperands_empty())
    return false;

  const MachineFrameInfo &MFI = getParent()->getParent()->getFrameInfo();

  for (MachineMemOperand *MMO : memoperands()) {
    if (MMO->isVolatile()) return false;
    if (MMO->isStore()) return false;
    if (MMO->isInvariant() && MMO->isDereferenceable())
      continue;

    // A load from a constant PseudoSourceValue is invariant.
    if (const PseudoSourceValue *PSV = MMO->getPseudoValue())
      if (PSV->isConstant(&MFI))
        continue;

    if (const Value *V = MMO->getValue()) {
      // If we have an AliasAnalysis, ask it whether the memory is constant.
      if (AA &&
          AA->pointsToConstantMemory(
              MemoryLocation(V, MMO->getSize(), MMO->getAAInfo())))
        continue;
    }

    // Otherwise assume conservatively.
    return false;
  }

  // Everything checks out.
  return true;
}

/// isConstantValuePHI - If the specified instruction is a PHI that always
/// merges together the same virtual register, return the register, otherwise
/// return 0.
unsigned MachineInstr::isConstantValuePHI() const {
  if (!isPHI())
    return 0;
  assert(getNumOperands() >= 3 &&
         "It's illegal to have a PHI without source operands");

  unsigned Reg = getOperand(1).getReg();
  for (unsigned i = 3, e = getNumOperands(); i < e; i += 2)
    if (getOperand(i).getReg() != Reg)
      return 0;
  return Reg;
}

bool MachineInstr::hasUnmodeledSideEffects() const {
  if (hasProperty(MCID::UnmodeledSideEffects))
    return true;
  if (isInlineAsm()) {
    unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
    if (ExtraInfo & InlineAsm::Extra_HasSideEffects)
      return true;
  }

  return false;
}

bool MachineInstr::isLoadFoldBarrier() const {
  return mayStore() || isCall() || hasUnmodeledSideEffects();
}

/// allDefsAreDead - Return true if all the defs of this instruction are dead.
///
bool MachineInstr::allDefsAreDead() const {
  for (const MachineOperand &MO : operands()) {
    if (!MO.isReg() || MO.isUse())
      continue;
    if (!MO.isDead())
      return false;
  }
  return true;
}

/// copyImplicitOps - Copy implicit register operands from specified
/// instruction to this instruction.
void MachineInstr::copyImplicitOps(MachineFunction &MF,
                                   const MachineInstr &MI) {
  for (unsigned i = MI.getDesc().getNumOperands(), e = MI.getNumOperands();
       i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if ((MO.isReg() && MO.isImplicit()) || MO.isRegMask())
      addOperand(MF, MO);
  }
}

bool MachineInstr::hasComplexRegisterTies() const {
  const MCInstrDesc &MCID = getDesc();
  for (unsigned I = 0, E = getNumOperands(); I < E; ++I) {
    const auto &Operand = getOperand(I);
    if (!Operand.isReg() || Operand.isDef())
      // Ignore the defined registers as MCID marks only the uses as tied.
      continue;
    int ExpectedTiedIdx = MCID.getOperandConstraint(I, MCOI::TIED_TO);
    int TiedIdx = Operand.isTied() ? int(findTiedOperandIdx(I)) : -1;
    if (ExpectedTiedIdx != TiedIdx)
      return true;
  }
  return false;
}

LLT MachineInstr::getTypeToPrint(unsigned OpIdx, SmallBitVector &PrintedTypes,
                                 const MachineRegisterInfo &MRI) const {
  const MachineOperand &Op = getOperand(OpIdx);
  if (!Op.isReg())
    return LLT{};

  if (isVariadic() || OpIdx >= getNumExplicitOperands())
    return MRI.getType(Op.getReg());

  auto &OpInfo = getDesc().OpInfo[OpIdx];
  if (!OpInfo.isGenericType())
    return MRI.getType(Op.getReg());

  if (PrintedTypes[OpInfo.getGenericTypeIndex()])
    return LLT{};

  LLT TypeToPrint = MRI.getType(Op.getReg());
  // Don't mark the type index printed if it wasn't actually printed: maybe
  // another operand with the same type index has an actual type attached:
  if (TypeToPrint.isValid())
    PrintedTypes.set(OpInfo.getGenericTypeIndex());
  return TypeToPrint;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineInstr::dump() const {
  dbgs() << "  ";
  print(dbgs());
}
#endif

void MachineInstr::print(raw_ostream &OS, bool IsStandalone, bool SkipOpers,
                         bool SkipDebugLoc, bool AddNewLine,
                         const TargetInstrInfo *TII) const {
  const Module *M = nullptr;
  const Function *F = nullptr;
  if (const MachineFunction *MF = getMFIfAvailable(*this)) {
    F = &MF->getFunction();
    M = F->getParent();
    if (!TII)
      TII = MF->getSubtarget().getInstrInfo();
  }

  ModuleSlotTracker MST(M);
  if (F)
    MST.incorporateFunction(*F);
  print(OS, MST, IsStandalone, SkipOpers, SkipDebugLoc, TII);
}

void MachineInstr::print(raw_ostream &OS, ModuleSlotTracker &MST,
                         bool IsStandalone, bool SkipOpers, bool SkipDebugLoc,
                         bool AddNewLine, const TargetInstrInfo *TII) const {
  // We can be a bit tidier if we know the MachineFunction.
  const MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const MachineRegisterInfo *MRI = nullptr;
  const TargetIntrinsicInfo *IntrinsicInfo = nullptr;
  tryToGetTargetInfo(*this, TRI, MRI, IntrinsicInfo, TII);

  if (isCFIInstruction())
    assert(getNumOperands() == 1 && "Expected 1 operand in CFI instruction");

  SmallBitVector PrintedTypes(8);
  bool ShouldPrintRegisterTies = IsStandalone || hasComplexRegisterTies();
  auto getTiedOperandIdx = [&](unsigned OpIdx) {
    if (!ShouldPrintRegisterTies)
      return 0U;
    const MachineOperand &MO = getOperand(OpIdx);
    if (MO.isReg() && MO.isTied() && !MO.isDef())
      return findTiedOperandIdx(OpIdx);
    return 0U;
  };
  unsigned StartOp = 0;
  unsigned e = getNumOperands();

  // Print explicitly defined operands on the left of an assignment syntax.
  while (StartOp < e) {
    const MachineOperand &MO = getOperand(StartOp);
    if (!MO.isReg() || !MO.isDef() || MO.isImplicit())
      break;

    if (StartOp != 0)
      OS << ", ";

    LLT TypeToPrint = MRI ? getTypeToPrint(StartOp, PrintedTypes, *MRI) : LLT{};
    unsigned TiedOperandIdx = getTiedOperandIdx(StartOp);
    MO.print(OS, MST, TypeToPrint, /*PrintDef=*/false, IsStandalone,
             ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
    ++StartOp;
  }

  if (StartOp != 0)
    OS << " = ";

  if (getFlag(MachineInstr::FrameSetup))
    OS << "frame-setup ";
  if (getFlag(MachineInstr::FrameDestroy))
    OS << "frame-destroy ";
  if (getFlag(MachineInstr::FmNoNans))
    OS << "nnan ";
  if (getFlag(MachineInstr::FmNoInfs))
    OS << "ninf ";
  if (getFlag(MachineInstr::FmNsz))
    OS << "nsz ";
  if (getFlag(MachineInstr::FmArcp))
    OS << "arcp ";
  if (getFlag(MachineInstr::FmContract))
    OS << "contract ";
  if (getFlag(MachineInstr::FmAfn))
    OS << "afn ";
  if (getFlag(MachineInstr::FmReassoc))
    OS << "reassoc ";
  if (getFlag(MachineInstr::NoUWrap))
    OS << "nuw ";
  if (getFlag(MachineInstr::NoSWrap))
    OS << "nsw ";
  if (getFlag(MachineInstr::IsExact))
    OS << "exact ";

  // Print the opcode name.
  if (TII)
    OS << TII->getName(getOpcode());
  else
    OS << "UNKNOWN";

  if (SkipOpers)
    return;

  // Print the rest of the operands.
  bool FirstOp = true;
  unsigned AsmDescOp = ~0u;
  unsigned AsmOpCount = 0;

  if (isInlineAsm() && e >= InlineAsm::MIOp_FirstOperand) {
    // Print asm string.
    OS << " ";
    const unsigned OpIdx = InlineAsm::MIOp_AsmString;
    LLT TypeToPrint = MRI ? getTypeToPrint(OpIdx, PrintedTypes, *MRI) : LLT{};
    unsigned TiedOperandIdx = getTiedOperandIdx(OpIdx);
    getOperand(OpIdx).print(OS, MST, TypeToPrint, /*PrintDef=*/true, IsStandalone,
                            ShouldPrintRegisterTies, TiedOperandIdx, TRI,
                            IntrinsicInfo);

    // Print HasSideEffects, MayLoad, MayStore, IsAlignStack
    unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
    if (ExtraInfo & InlineAsm::Extra_HasSideEffects)
      OS << " [sideeffect]";
    if (ExtraInfo & InlineAsm::Extra_MayLoad)
      OS << " [mayload]";
    if (ExtraInfo & InlineAsm::Extra_MayStore)
      OS << " [maystore]";
    if (ExtraInfo & InlineAsm::Extra_IsConvergent)
      OS << " [isconvergent]";
    if (ExtraInfo & InlineAsm::Extra_IsAlignStack)
      OS << " [alignstack]";
    if (getInlineAsmDialect() == InlineAsm::AD_ATT)
      OS << " [attdialect]";
    if (getInlineAsmDialect() == InlineAsm::AD_Intel)
      OS << " [inteldialect]";

    StartOp = AsmDescOp = InlineAsm::MIOp_FirstOperand;
    FirstOp = false;
  }

  for (unsigned i = StartOp, e = getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = getOperand(i);

    if (FirstOp) FirstOp = false; else OS << ",";
    OS << " ";

    if (isDebugValue() && MO.isMetadata()) {
      // Pretty print DBG_VALUE instructions.
      auto *DIV = dyn_cast<DILocalVariable>(MO.getMetadata());
      if (DIV && !DIV->getName().empty())
        OS << "!\"" << DIV->getName() << '\"';
      else {
        LLT TypeToPrint = MRI ? getTypeToPrint(i, PrintedTypes, *MRI) : LLT{};
        unsigned TiedOperandIdx = getTiedOperandIdx(i);
        MO.print(OS, MST, TypeToPrint, /*PrintDef=*/true, IsStandalone,
                 ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
      }
    } else if (isDebugLabel() && MO.isMetadata()) {
      // Pretty print DBG_LABEL instructions.
      auto *DIL = dyn_cast<DILabel>(MO.getMetadata());
      if (DIL && !DIL->getName().empty())
        OS << "\"" << DIL->getName() << '\"';
      else {
        LLT TypeToPrint = MRI ? getTypeToPrint(i, PrintedTypes, *MRI) : LLT{};
        unsigned TiedOperandIdx = getTiedOperandIdx(i);
        MO.print(OS, MST, TypeToPrint, /*PrintDef=*/true, IsStandalone,
                 ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
      }
    } else if (i == AsmDescOp && MO.isImm()) {
      // Pretty print the inline asm operand descriptor.
      OS << '$' << AsmOpCount++;
      unsigned Flag = MO.getImm();
      switch (InlineAsm::getKind(Flag)) {
      case InlineAsm::Kind_RegUse:             OS << ":[reguse"; break;
      case InlineAsm::Kind_RegDef:             OS << ":[regdef"; break;
      case InlineAsm::Kind_RegDefEarlyClobber: OS << ":[regdef-ec"; break;
      case InlineAsm::Kind_Clobber:            OS << ":[clobber"; break;
      case InlineAsm::Kind_Imm:                OS << ":[imm"; break;
      case InlineAsm::Kind_Mem:                OS << ":[mem"; break;
      default: OS << ":[??" << InlineAsm::getKind(Flag); break;
      }

      unsigned RCID = 0;
      if (!InlineAsm::isImmKind(Flag) && !InlineAsm::isMemKind(Flag) &&
          InlineAsm::hasRegClassConstraint(Flag, RCID)) {
        if (TRI) {
          OS << ':' << TRI->getRegClassName(TRI->getRegClass(RCID));
        } else
          OS << ":RC" << RCID;
      }

      if (InlineAsm::isMemKind(Flag)) {
        unsigned MCID = InlineAsm::getMemoryConstraintID(Flag);
        switch (MCID) {
        case InlineAsm::Constraint_es: OS << ":es"; break;
        case InlineAsm::Constraint_i:  OS << ":i"; break;
        case InlineAsm::Constraint_m:  OS << ":m"; break;
        case InlineAsm::Constraint_o:  OS << ":o"; break;
        case InlineAsm::Constraint_v:  OS << ":v"; break;
        case InlineAsm::Constraint_Q:  OS << ":Q"; break;
        case InlineAsm::Constraint_R:  OS << ":R"; break;
        case InlineAsm::Constraint_S:  OS << ":S"; break;
        case InlineAsm::Constraint_T:  OS << ":T"; break;
        case InlineAsm::Constraint_Um: OS << ":Um"; break;
        case InlineAsm::Constraint_Un: OS << ":Un"; break;
        case InlineAsm::Constraint_Uq: OS << ":Uq"; break;
        case InlineAsm::Constraint_Us: OS << ":Us"; break;
        case InlineAsm::Constraint_Ut: OS << ":Ut"; break;
        case InlineAsm::Constraint_Uv: OS << ":Uv"; break;
        case InlineAsm::Constraint_Uy: OS << ":Uy"; break;
        case InlineAsm::Constraint_X:  OS << ":X"; break;
        case InlineAsm::Constraint_Z:  OS << ":Z"; break;
        case InlineAsm::Constraint_ZC: OS << ":ZC"; break;
        case InlineAsm::Constraint_Zy: OS << ":Zy"; break;
        default: OS << ":?"; break;
        }
      }

      unsigned TiedTo = 0;
      if (InlineAsm::isUseOperandTiedToDef(Flag, TiedTo))
        OS << " tiedto:$" << TiedTo;

      OS << ']';

      // Compute the index of the next operand descriptor.
      AsmDescOp += 1 + InlineAsm::getNumOperandRegisters(Flag);
    } else {
      LLT TypeToPrint = MRI ? getTypeToPrint(i, PrintedTypes, *MRI) : LLT{};
      unsigned TiedOperandIdx = getTiedOperandIdx(i);
      if (MO.isImm() && isOperandSubregIdx(i))
        MachineOperand::printSubRegIdx(OS, MO.getImm(), TRI);
      else
        MO.print(OS, MST, TypeToPrint, /*PrintDef=*/true, IsStandalone,
                 ShouldPrintRegisterTies, TiedOperandIdx, TRI, IntrinsicInfo);
    }
  }

  // Print any optional symbols attached to this instruction as-if they were
  // operands.
  if (MCSymbol *PreInstrSymbol = getPreInstrSymbol()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " pre-instr-symbol ";
    MachineOperand::printSymbol(OS, *PreInstrSymbol);
  }
  if (MCSymbol *PostInstrSymbol = getPostInstrSymbol()) {
    if (!FirstOp) {
      FirstOp = false;
      OS << ',';
    }
    OS << " post-instr-symbol ";
    MachineOperand::printSymbol(OS, *PostInstrSymbol);
  }

  if (!SkipDebugLoc) {
    if (const DebugLoc &DL = getDebugLoc()) {
      if (!FirstOp)
        OS << ',';
      OS << " debug-location ";
      DL->printAsOperand(OS, MST);
    }
  }

  if (!memoperands_empty()) {
    SmallVector<StringRef, 0> SSNs;
    const LLVMContext *Context = nullptr;
    std::unique_ptr<LLVMContext> CtxPtr;
    const MachineFrameInfo *MFI = nullptr;
    if (const MachineFunction *MF = getMFIfAvailable(*this)) {
      MFI = &MF->getFrameInfo();
      Context = &MF->getFunction().getContext();
    } else {
      CtxPtr = llvm::make_unique<LLVMContext>();
      Context = CtxPtr.get();
    }

    OS << " :: ";
    bool NeedComma = false;
    for (const MachineMemOperand *Op : memoperands()) {
      if (NeedComma)
        OS << ", ";
      Op->print(OS, MST, SSNs, *Context, MFI, TII);
      NeedComma = true;
    }
  }

  if (SkipDebugLoc)
    return;

  bool HaveSemi = false;

  // Print debug location information.
  if (const DebugLoc &DL = getDebugLoc()) {
    if (!HaveSemi) {
      OS << ';';
      HaveSemi = true;
    }
    OS << ' ';
    DL.print(OS);
  }

  // Print extra comments for DEBUG_VALUE.
  if (isDebugValue() && getOperand(e - 2).isMetadata()) {
    if (!HaveSemi) {
      OS << ";";
      HaveSemi = true;
    }
    auto *DV = cast<DILocalVariable>(getOperand(e - 2).getMetadata());
    OS << " line no:" <<  DV->getLine();
    if (auto *InlinedAt = debugLoc->getInlinedAt()) {
      DebugLoc InlinedAtDL(InlinedAt);
      if (InlinedAtDL && MF) {
        OS << " inlined @[ ";
        InlinedAtDL.print(OS);
        OS << " ]";
      }
    }
    if (isIndirectDebugValue())
      OS << " indirect";
  }
  // TODO: DBG_LABEL

  if (AddNewLine)
    OS << '\n';
}

bool MachineInstr::addRegisterKilled(unsigned IncomingReg,
                                     const TargetRegisterInfo *RegInfo,
                                     bool AddIfNotFound) {
  bool isPhysReg = TargetRegisterInfo::isPhysicalRegister(IncomingReg);
  bool hasAliases = isPhysReg &&
    MCRegAliasIterator(IncomingReg, RegInfo, false).isValid();
  bool Found = false;
  SmallVector<unsigned,4> DeadOps;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || !MO.isUse() || MO.isUndef())
      continue;

    // DEBUG_VALUE nodes do not contribute to code generation and should
    // always be ignored. Failure to do so may result in trying to modify
    // KILL flags on DEBUG_VALUE nodes.
    if (MO.isDebug())
      continue;

    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;

    if (Reg == IncomingReg) {
      if (!Found) {
        if (MO.isKill())
          // The register is already marked kill.
          return true;
        if (isPhysReg && isRegTiedToDefOperand(i))
          // Two-address uses of physregs must not be marked kill.
          return true;
        MO.setIsKill();
        Found = true;
      }
    } else if (hasAliases && MO.isKill() &&
               TargetRegisterInfo::isPhysicalRegister(Reg)) {
      // A super-register kill already exists.
      if (RegInfo->isSuperRegister(IncomingReg, Reg))
        return true;
      if (RegInfo->isSubRegister(IncomingReg, Reg))
        DeadOps.push_back(i);
    }
  }

  // Trim unneeded kill operands.
  while (!DeadOps.empty()) {
    unsigned OpIdx = DeadOps.back();
    if (getOperand(OpIdx).isImplicit() &&
        (!isInlineAsm() || findInlineAsmFlagIdx(OpIdx) < 0))
      RemoveOperand(OpIdx);
    else
      getOperand(OpIdx).setIsKill(false);
    DeadOps.pop_back();
  }

  // If not found, this means an alias of one of the operands is killed. Add a
  // new implicit operand if required.
  if (!Found && AddIfNotFound) {
    addOperand(MachineOperand::CreateReg(IncomingReg,
                                         false /*IsDef*/,
                                         true  /*IsImp*/,
                                         true  /*IsKill*/));
    return true;
  }
  return Found;
}

void MachineInstr::clearRegisterKills(unsigned Reg,
                                      const TargetRegisterInfo *RegInfo) {
  if (!TargetRegisterInfo::isPhysicalRegister(Reg))
    RegInfo = nullptr;
  for (MachineOperand &MO : operands()) {
    if (!MO.isReg() || !MO.isUse() || !MO.isKill())
      continue;
    unsigned OpReg = MO.getReg();
    if ((RegInfo && RegInfo->regsOverlap(Reg, OpReg)) || Reg == OpReg)
      MO.setIsKill(false);
  }
}

bool MachineInstr::addRegisterDead(unsigned Reg,
                                   const TargetRegisterInfo *RegInfo,
                                   bool AddIfNotFound) {
  bool isPhysReg = TargetRegisterInfo::isPhysicalRegister(Reg);
  bool hasAliases = isPhysReg &&
    MCRegAliasIterator(Reg, RegInfo, false).isValid();
  bool Found = false;
  SmallVector<unsigned,4> DeadOps;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    MachineOperand &MO = getOperand(i);
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned MOReg = MO.getReg();
    if (!MOReg)
      continue;

    if (MOReg == Reg) {
      MO.setIsDead();
      Found = true;
    } else if (hasAliases && MO.isDead() &&
               TargetRegisterInfo::isPhysicalRegister(MOReg)) {
      // There exists a super-register that's marked dead.
      if (RegInfo->isSuperRegister(Reg, MOReg))
        return true;
      if (RegInfo->isSubRegister(Reg, MOReg))
        DeadOps.push_back(i);
    }
  }

  // Trim unneeded dead operands.
  while (!DeadOps.empty()) {
    unsigned OpIdx = DeadOps.back();
    if (getOperand(OpIdx).isImplicit() &&
        (!isInlineAsm() || findInlineAsmFlagIdx(OpIdx) < 0))
      RemoveOperand(OpIdx);
    else
      getOperand(OpIdx).setIsDead(false);
    DeadOps.pop_back();
  }

  // If not found, this means an alias of one of the operands is dead. Add a
  // new implicit operand if required.
  if (Found || !AddIfNotFound)
    return Found;

  addOperand(MachineOperand::CreateReg(Reg,
                                       true  /*IsDef*/,
                                       true  /*IsImp*/,
                                       false /*IsKill*/,
                                       true  /*IsDead*/));
  return true;
}

void MachineInstr::clearRegisterDeads(unsigned Reg) {
  for (MachineOperand &MO : operands()) {
    if (!MO.isReg() || !MO.isDef() || MO.getReg() != Reg)
      continue;
    MO.setIsDead(false);
  }
}

void MachineInstr::setRegisterDefReadUndef(unsigned Reg, bool IsUndef) {
  for (MachineOperand &MO : operands()) {
    if (!MO.isReg() || !MO.isDef() || MO.getReg() != Reg || MO.getSubReg() == 0)
      continue;
    MO.setIsUndef(IsUndef);
  }
}

void MachineInstr::addRegisterDefined(unsigned Reg,
                                      const TargetRegisterInfo *RegInfo) {
  if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
    MachineOperand *MO = findRegisterDefOperand(Reg, false, RegInfo);
    if (MO)
      return;
  } else {
    for (const MachineOperand &MO : operands()) {
      if (MO.isReg() && MO.getReg() == Reg && MO.isDef() &&
          MO.getSubReg() == 0)
        return;
    }
  }
  addOperand(MachineOperand::CreateReg(Reg,
                                       true  /*IsDef*/,
                                       true  /*IsImp*/));
}

void MachineInstr::setPhysRegsDeadExcept(ArrayRef<unsigned> UsedRegs,
                                         const TargetRegisterInfo &TRI) {
  bool HasRegMask = false;
  for (MachineOperand &MO : operands()) {
    if (MO.isRegMask()) {
      HasRegMask = true;
      continue;
    }
    if (!MO.isReg() || !MO.isDef()) continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isPhysicalRegister(Reg)) continue;
    // If there are no uses, including partial uses, the def is dead.
    if (llvm::none_of(UsedRegs,
                      [&](unsigned Use) { return TRI.regsOverlap(Use, Reg); }))
      MO.setIsDead();
  }

  // This is a call with a register mask operand.
  // Mask clobbers are always dead, so add defs for the non-dead defines.
  if (HasRegMask)
    for (ArrayRef<unsigned>::iterator I = UsedRegs.begin(), E = UsedRegs.end();
         I != E; ++I)
      addRegisterDefined(*I, &TRI);
}

unsigned
MachineInstrExpressionTrait::getHashValue(const MachineInstr* const &MI) {
  // Build up a buffer of hash code components.
  SmallVector<size_t, 8> HashComponents;
  HashComponents.reserve(MI->getNumOperands() + 1);
  HashComponents.push_back(MI->getOpcode());
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && MO.isDef() &&
        TargetRegisterInfo::isVirtualRegister(MO.getReg()))
      continue;  // Skip virtual register defs.

    HashComponents.push_back(hash_value(MO));
  }
  return hash_combine_range(HashComponents.begin(), HashComponents.end());
}

void MachineInstr::emitError(StringRef Msg) const {
  // Find the source location cookie.
  unsigned LocCookie = 0;
  const MDNode *LocMD = nullptr;
  for (unsigned i = getNumOperands(); i != 0; --i) {
    if (getOperand(i-1).isMetadata() &&
        (LocMD = getOperand(i-1).getMetadata()) &&
        LocMD->getNumOperands() != 0) {
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(LocMD->getOperand(0))) {
        LocCookie = CI->getZExtValue();
        break;
      }
    }
  }

  if (const MachineBasicBlock *MBB = getParent())
    if (const MachineFunction *MF = MBB->getParent())
      return MF->getMMI().getModule()->getContext().emitError(LocCookie, Msg);
  report_fatal_error(Msg);
}

MachineInstrBuilder llvm::BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                  const MCInstrDesc &MCID, bool IsIndirect,
                                  unsigned Reg, const MDNode *Variable,
                                  const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  auto MIB = BuildMI(MF, DL, MCID).addReg(Reg, RegState::Debug);
  if (IsIndirect)
    MIB.addImm(0U);
  else
    MIB.addReg(0U, RegState::Debug);
  return MIB.addMetadata(Variable).addMetadata(Expr);
}

MachineInstrBuilder llvm::BuildMI(MachineFunction &MF, const DebugLoc &DL,
                                  const MCInstrDesc &MCID, bool IsIndirect,
                                  MachineOperand &MO, const MDNode *Variable,
                                  const MDNode *Expr) {
  assert(isa<DILocalVariable>(Variable) && "not a variable");
  assert(cast<DIExpression>(Expr)->isValid() && "not an expression");
  assert(cast<DILocalVariable>(Variable)->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  if (MO.isReg())
    return BuildMI(MF, DL, MCID, IsIndirect, MO.getReg(), Variable, Expr);

  auto MIB = BuildMI(MF, DL, MCID).add(MO);
  if (IsIndirect)
    MIB.addImm(0U);
  else
    MIB.addReg(0U, RegState::Debug);
  return MIB.addMetadata(Variable).addMetadata(Expr);
 }

MachineInstrBuilder llvm::BuildMI(MachineBasicBlock &BB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, const MCInstrDesc &MCID,
                                  bool IsIndirect, unsigned Reg,
                                  const MDNode *Variable, const MDNode *Expr) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = BuildMI(MF, DL, MCID, IsIndirect, Reg, Variable, Expr);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, MI);
}

MachineInstrBuilder llvm::BuildMI(MachineBasicBlock &BB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, const MCInstrDesc &MCID,
                                  bool IsIndirect, MachineOperand &MO,
                                  const MDNode *Variable, const MDNode *Expr) {
  MachineFunction &MF = *BB.getParent();
  MachineInstr *MI = BuildMI(MF, DL, MCID, IsIndirect, MO, Variable, Expr);
  BB.insert(I, MI);
  return MachineInstrBuilder(MF, *MI);
}

/// Compute the new DIExpression to use with a DBG_VALUE for a spill slot.
/// This prepends DW_OP_deref when spilling an indirect DBG_VALUE.
static const DIExpression *computeExprForSpill(const MachineInstr &MI) {
  assert(MI.getOperand(0).isReg() && "can't spill non-register");
  assert(MI.getDebugVariable()->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
         "Expected inlined-at fields to agree");

  const DIExpression *Expr = MI.getDebugExpression();
  if (MI.isIndirectDebugValue()) {
    assert(MI.getOperand(1).getImm() == 0 && "DBG_VALUE with nonzero offset");
    Expr = DIExpression::prepend(Expr, DIExpression::WithDeref);
  }
  return Expr;
}

MachineInstr *llvm::buildDbgValueForSpill(MachineBasicBlock &BB,
                                          MachineBasicBlock::iterator I,
                                          const MachineInstr &Orig,
                                          int FrameIndex) {
  const DIExpression *Expr = computeExprForSpill(Orig);
  return BuildMI(BB, I, Orig.getDebugLoc(), Orig.getDesc())
      .addFrameIndex(FrameIndex)
      .addImm(0U)
      .addMetadata(Orig.getDebugVariable())
      .addMetadata(Expr);
}

void llvm::updateDbgValueForSpill(MachineInstr &Orig, int FrameIndex) {
  const DIExpression *Expr = computeExprForSpill(Orig);
  Orig.getOperand(0).ChangeToFrameIndex(FrameIndex);
  Orig.getOperand(1).ChangeToImmediate(0U);
  Orig.getOperand(3).setMetadata(Expr);
}

void MachineInstr::collectDebugValues(
                                SmallVectorImpl<MachineInstr *> &DbgValues) {
  MachineInstr &MI = *this;
  if (!MI.getOperand(0).isReg())
    return;

  MachineBasicBlock::iterator DI = MI; ++DI;
  for (MachineBasicBlock::iterator DE = MI.getParent()->end();
       DI != DE; ++DI) {
    if (!DI->isDebugValue())
      return;
    if (DI->getOperand(0).isReg() &&
        DI->getOperand(0).getReg() == MI.getOperand(0).getReg())
      DbgValues.push_back(&*DI);
  }
}

void MachineInstr::changeDebugValuesDefReg(unsigned Reg) {
  // Collect matching debug values.
  SmallVector<MachineInstr *, 2> DbgValues;
  collectDebugValues(DbgValues);

  // Propagate Reg to debug value instructions.
  for (auto *DBI : DbgValues)
    DBI->getOperand(0).setReg(Reg);
}
