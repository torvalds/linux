//===- llvm/CodeGen/AsmPrinter/DbgEntityHistoryCalculator.cpp -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/DbgEntityHistoryCalculator.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <map>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

// If @MI is a DBG_VALUE with debug value described by a
// defined register, returns the number of this register.
// In the other case, returns 0.
static unsigned isDescribedByReg(const MachineInstr &MI) {
  assert(MI.isDebugValue());
  assert(MI.getNumOperands() == 4);
  // If location of variable is described using a register (directly or
  // indirectly), this register is always a first operand.
  return MI.getOperand(0).isReg() ? MI.getOperand(0).getReg() : 0;
}

void DbgValueHistoryMap::startInstrRange(InlinedEntity Var,
                                         const MachineInstr &MI) {
  // Instruction range should start with a DBG_VALUE instruction for the
  // variable.
  assert(MI.isDebugValue() && "not a DBG_VALUE");
  auto &Ranges = VarInstrRanges[Var];
  if (!Ranges.empty() && Ranges.back().second == nullptr &&
      Ranges.back().first->isIdenticalTo(MI)) {
    LLVM_DEBUG(dbgs() << "Coalescing identical DBG_VALUE entries:\n"
                      << "\t" << Ranges.back().first << "\t" << MI << "\n");
    return;
  }
  Ranges.push_back(std::make_pair(&MI, nullptr));
}

void DbgValueHistoryMap::endInstrRange(InlinedEntity Var,
                                       const MachineInstr &MI) {
  auto &Ranges = VarInstrRanges[Var];
  // Verify that the current instruction range is not yet closed.
  assert(!Ranges.empty() && Ranges.back().second == nullptr);
  // For now, instruction ranges are not allowed to cross basic block
  // boundaries.
  assert(Ranges.back().first->getParent() == MI.getParent());
  Ranges.back().second = &MI;
}

unsigned DbgValueHistoryMap::getRegisterForVar(InlinedEntity Var) const {
  const auto &I = VarInstrRanges.find(Var);
  if (I == VarInstrRanges.end())
    return 0;
  const auto &Ranges = I->second;
  if (Ranges.empty() || Ranges.back().second != nullptr)
    return 0;
  return isDescribedByReg(*Ranges.back().first);
}

void DbgLabelInstrMap::addInstr(InlinedEntity Label, const MachineInstr &MI) {
  assert(MI.isDebugLabel() && "not a DBG_LABEL");
  LabelInstr[Label] = &MI;
}

namespace {

// Maps physreg numbers to the variables they describe.
using InlinedEntity = DbgValueHistoryMap::InlinedEntity;
using RegDescribedVarsMap = std::map<unsigned, SmallVector<InlinedEntity, 1>>;

} // end anonymous namespace

// Claim that @Var is not described by @RegNo anymore.
static void dropRegDescribedVar(RegDescribedVarsMap &RegVars, unsigned RegNo,
                                InlinedEntity Var) {
  const auto &I = RegVars.find(RegNo);
  assert(RegNo != 0U && I != RegVars.end());
  auto &VarSet = I->second;
  const auto &VarPos = llvm::find(VarSet, Var);
  assert(VarPos != VarSet.end());
  VarSet.erase(VarPos);
  // Don't keep empty sets in a map to keep it as small as possible.
  if (VarSet.empty())
    RegVars.erase(I);
}

// Claim that @Var is now described by @RegNo.
static void addRegDescribedVar(RegDescribedVarsMap &RegVars, unsigned RegNo,
                               InlinedEntity Var) {
  assert(RegNo != 0U);
  auto &VarSet = RegVars[RegNo];
  assert(!is_contained(VarSet, Var));
  VarSet.push_back(Var);
}

// Terminate the location range for variables described by register at
// @I by inserting @ClobberingInstr to their history.
static void clobberRegisterUses(RegDescribedVarsMap &RegVars,
                                RegDescribedVarsMap::iterator I,
                                DbgValueHistoryMap &HistMap,
                                const MachineInstr &ClobberingInstr) {
  // Iterate over all variables described by this register and add this
  // instruction to their history, clobbering it.
  for (const auto &Var : I->second)
    HistMap.endInstrRange(Var, ClobberingInstr);
  RegVars.erase(I);
}

// Terminate the location range for variables described by register
// @RegNo by inserting @ClobberingInstr to their history.
static void clobberRegisterUses(RegDescribedVarsMap &RegVars, unsigned RegNo,
                                DbgValueHistoryMap &HistMap,
                                const MachineInstr &ClobberingInstr) {
  const auto &I = RegVars.find(RegNo);
  if (I == RegVars.end())
    return;
  clobberRegisterUses(RegVars, I, HistMap, ClobberingInstr);
}

// Returns the first instruction in @MBB which corresponds to
// the function epilogue, or nullptr if @MBB doesn't contain an epilogue.
static const MachineInstr *getFirstEpilogueInst(const MachineBasicBlock &MBB) {
  auto LastMI = MBB.getLastNonDebugInstr();
  if (LastMI == MBB.end() || !LastMI->isReturn())
    return nullptr;
  // Assume that epilogue starts with instruction having the same debug location
  // as the return instruction.
  DebugLoc LastLoc = LastMI->getDebugLoc();
  auto Res = LastMI;
  for (MachineBasicBlock::const_reverse_iterator I = LastMI.getReverse(),
                                                 E = MBB.rend();
       I != E; ++I) {
    if (I->getDebugLoc() != LastLoc)
      return &*Res;
    Res = &*I;
  }
  // If all instructions have the same debug location, assume whole MBB is
  // an epilogue.
  return &*MBB.begin();
}

// Collect registers that are modified in the function body (their
// contents is changed outside of the prologue and epilogue).
static void collectChangingRegs(const MachineFunction *MF,
                                const TargetRegisterInfo *TRI,
                                BitVector &Regs) {
  for (const auto &MBB : *MF) {
    auto FirstEpilogueInst = getFirstEpilogueInst(MBB);

    for (const auto &MI : MBB) {
      // Avoid looking at prologue or epilogue instructions.
      if (&MI == FirstEpilogueInst)
        break;
      if (MI.getFlag(MachineInstr::FrameSetup))
        continue;

      // Look for register defs and register masks. Register masks are
      // typically on calls and they clobber everything not in the mask.
      for (const MachineOperand &MO : MI.operands()) {
        // Skip virtual registers since they are handled by the parent.
        if (MO.isReg() && MO.isDef() && MO.getReg() &&
            !TRI->isVirtualRegister(MO.getReg())) {
          for (MCRegAliasIterator AI(MO.getReg(), TRI, true); AI.isValid();
               ++AI)
            Regs.set(*AI);
        } else if (MO.isRegMask()) {
          Regs.setBitsNotInMask(MO.getRegMask());
        }
      }
    }
  }
}

void llvm::calculateDbgEntityHistory(const MachineFunction *MF,
                                     const TargetRegisterInfo *TRI,
                                     DbgValueHistoryMap &DbgValues,
                                     DbgLabelInstrMap &DbgLabels) {
  BitVector ChangingRegs(TRI->getNumRegs());
  collectChangingRegs(MF, TRI, ChangingRegs);

  const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
  unsigned SP = TLI->getStackPointerRegisterToSaveRestore();
  RegDescribedVarsMap RegVars;
  for (const auto &MBB : *MF) {
    for (const auto &MI : MBB) {
      if (!MI.isDebugInstr()) {
        // Not a DBG_VALUE instruction. It may clobber registers which describe
        // some variables.
        for (const MachineOperand &MO : MI.operands()) {
          if (MO.isReg() && MO.isDef() && MO.getReg()) {
            // Ignore call instructions that claim to clobber SP. The AArch64
            // backend does this for aggregate function arguments.
            if (MI.isCall() && MO.getReg() == SP)
              continue;
            // If this is a virtual register, only clobber it since it doesn't
            // have aliases.
            if (TRI->isVirtualRegister(MO.getReg()))
              clobberRegisterUses(RegVars, MO.getReg(), DbgValues, MI);
            // If this is a register def operand, it may end a debug value
            // range.
            else {
              for (MCRegAliasIterator AI(MO.getReg(), TRI, true); AI.isValid();
                   ++AI)
                if (ChangingRegs.test(*AI))
                  clobberRegisterUses(RegVars, *AI, DbgValues, MI);
            }
          } else if (MO.isRegMask()) {
            // If this is a register mask operand, clobber all debug values in
            // non-CSRs.
            for (unsigned I : ChangingRegs.set_bits()) {
              // Don't consider SP to be clobbered by register masks.
              if (unsigned(I) != SP && TRI->isPhysicalRegister(I) &&
                  MO.clobbersPhysReg(I)) {
                clobberRegisterUses(RegVars, I, DbgValues, MI);
              }
            }
          }
        }
        continue;
      }

      if (MI.isDebugValue()) {
        assert(MI.getNumOperands() > 1 && "Invalid DBG_VALUE instruction!");
        // Use the base variable (without any DW_OP_piece expressions)
        // as index into History. The full variables including the
        // piece expressions are attached to the MI.
        const DILocalVariable *RawVar = MI.getDebugVariable();
        assert(RawVar->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
               "Expected inlined-at fields to agree");
        InlinedEntity Var(RawVar, MI.getDebugLoc()->getInlinedAt());

        if (unsigned PrevReg = DbgValues.getRegisterForVar(Var))
          dropRegDescribedVar(RegVars, PrevReg, Var);

        DbgValues.startInstrRange(Var, MI);

        if (unsigned NewReg = isDescribedByReg(MI))
          addRegDescribedVar(RegVars, NewReg, Var);
      } else if (MI.isDebugLabel()) {
        assert(MI.getNumOperands() == 1 && "Invalid DBG_LABEL instruction!");
        const DILabel *RawLabel = MI.getDebugLabel();
        assert(RawLabel->isValidLocationForIntrinsic(MI.getDebugLoc()) &&
            "Expected inlined-at fields to agree");
        // When collecting debug information for labels, there is no MCSymbol
        // generated for it. So, we keep MachineInstr in DbgLabels in order
        // to query MCSymbol afterward.
        InlinedEntity L(RawLabel, MI.getDebugLoc()->getInlinedAt());
        DbgLabels.addInstr(L, MI);
      }
    }

    // Make sure locations for register-described variables are valid only
    // until the end of the basic block (unless it's the last basic block, in
    // which case let their liveness run off to the end of the function).
    if (!MBB.empty() && &MBB != &MF->back()) {
      for (auto I = RegVars.begin(), E = RegVars.end(); I != E;) {
        auto CurElem = I++; // CurElem can be erased below.
        if (TRI->isVirtualRegister(CurElem->first) ||
            ChangingRegs.test(CurElem->first))
          clobberRegisterUses(RegVars, CurElem, DbgValues, MBB.back());
      }
    }
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void DbgValueHistoryMap::dump() const {
  dbgs() << "DbgValueHistoryMap:\n";
  for (const auto &VarRangePair : *this) {
    const InlinedEntity &Var = VarRangePair.first;
    const InstrRanges &Ranges = VarRangePair.second;

    const DILocalVariable *LocalVar = cast<DILocalVariable>(Var.first);
    const DILocation *Location = Var.second;

    dbgs() << " - " << LocalVar->getName() << " at ";

    if (Location)
      dbgs() << Location->getFilename() << ":" << Location->getLine() << ":"
             << Location->getColumn();
    else
      dbgs() << "<unknown location>";

    dbgs() << " --\n";

    for (const InstrRange &Range : Ranges) {
      dbgs() << "   Begin: " << *Range.first;
      if (Range.second)
        dbgs() << "   End  : " << *Range.second;
      dbgs() << "\n";
    }
  }
}
#endif
