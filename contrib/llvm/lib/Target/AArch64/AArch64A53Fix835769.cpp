//===-- AArch64A53Fix835769.cpp -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass changes code to work around Cortex-A53 erratum 835769.
// It works around it by inserting a nop instruction in code sequences that
// in some circumstances may trigger the erratum.
// It inserts a nop instruction between a sequence of the following 2 classes
// of instructions:
// instr 1: mem-instr (including loads, stores and prefetches).
// instr 2: non-SIMD integer multiply-accumulate writing 64-bit X registers.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-fix-cortex-a53-835769"

STATISTIC(NumNopsAdded, "Number of Nops added to work around erratum 835769");

//===----------------------------------------------------------------------===//
// Helper functions

// Is the instruction a match for the instruction that comes first in the
// sequence of instructions that can trigger the erratum?
static bool isFirstInstructionInSequence(MachineInstr *MI) {
  // Must return true if this instruction is a load, a store or a prefetch.
  switch (MI->getOpcode()) {
  case AArch64::PRFMl:
  case AArch64::PRFMroW:
  case AArch64::PRFMroX:
  case AArch64::PRFMui:
  case AArch64::PRFUMi:
    return true;
  default:
    return MI->mayLoadOrStore();
  }
}

// Is the instruction a match for the instruction that comes second in the
// sequence that can trigger the erratum?
static bool isSecondInstructionInSequence(MachineInstr *MI) {
  // Must return true for non-SIMD integer multiply-accumulates, writing
  // to a 64-bit register.
  switch (MI->getOpcode()) {
  // Erratum cannot be triggered when the destination register is 32 bits,
  // therefore only include the following.
  case AArch64::MSUBXrrr:
  case AArch64::MADDXrrr:
  case AArch64::SMADDLrrr:
  case AArch64::SMSUBLrrr:
  case AArch64::UMADDLrrr:
  case AArch64::UMSUBLrrr:
    // Erratum can only be triggered by multiply-adds, not by regular
    // non-accumulating multiplies, i.e. when Ra=XZR='11111'
    return MI->getOperand(3).getReg() != AArch64::XZR;
  default:
    return false;
  }
}


//===----------------------------------------------------------------------===//

namespace {
class AArch64A53Fix835769 : public MachineFunctionPass {
  const TargetInstrInfo *TII;

public:
  static char ID;
  explicit AArch64A53Fix835769() : MachineFunctionPass(ID) {
    initializeAArch64A53Fix835769Pass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override {
    return "Workaround A53 erratum 835769 pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool runOnBasicBlock(MachineBasicBlock &MBB);
};
char AArch64A53Fix835769::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(AArch64A53Fix835769, "aarch64-fix-cortex-a53-835769-pass",
                "AArch64 fix for A53 erratum 835769", false, false)

//===----------------------------------------------------------------------===//

bool
AArch64A53Fix835769::runOnMachineFunction(MachineFunction &F) {
  LLVM_DEBUG(dbgs() << "***** AArch64A53Fix835769 *****\n");
  bool Changed = false;
  TII = F.getSubtarget().getInstrInfo();

  for (auto &MBB : F) {
    Changed |= runOnBasicBlock(MBB);
  }
  return Changed;
}

// Return the block that was fallen through to get to MBB, if any,
// otherwise nullptr.
static MachineBasicBlock *getBBFallenThrough(MachineBasicBlock *MBB,
                                             const TargetInstrInfo *TII) {
  // Get the previous machine basic block in the function.
  MachineFunction::iterator MBBI(MBB);

  // Can't go off top of function.
  if (MBBI == MBB->getParent()->begin())
    return nullptr;

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 2> Cond;

  MachineBasicBlock *PrevBB = &*std::prev(MBBI);
  for (MachineBasicBlock *S : MBB->predecessors())
    if (S == PrevBB && !TII->analyzeBranch(*PrevBB, TBB, FBB, Cond) && !TBB &&
        !FBB)
      return S;

  return nullptr;
}

// Iterate through fallen through blocks trying to find a previous non-pseudo if
// there is one, otherwise return nullptr. Only look for instructions in
// previous blocks, not the current block, since we only use this to look at
// previous blocks.
static MachineInstr *getLastNonPseudo(MachineBasicBlock &MBB,
                                      const TargetInstrInfo *TII) {
  MachineBasicBlock *FMBB = &MBB;

  // If there is no non-pseudo in the current block, loop back around and try
  // the previous block (if there is one).
  while ((FMBB = getBBFallenThrough(FMBB, TII))) {
    for (MachineInstr &I : make_range(FMBB->rbegin(), FMBB->rend()))
      if (!I.isPseudo())
        return &I;
  }

  // There was no previous non-pseudo in the fallen through blocks
  return nullptr;
}

static void insertNopBeforeInstruction(MachineBasicBlock &MBB, MachineInstr* MI,
                                       const TargetInstrInfo *TII) {
  // If we are the first instruction of the block, put the NOP at the end of
  // the previous fallthrough block
  if (MI == &MBB.front()) {
    MachineInstr *I = getLastNonPseudo(MBB, TII);
    assert(I && "Expected instruction");
    DebugLoc DL = I->getDebugLoc();
    BuildMI(I->getParent(), DL, TII->get(AArch64::HINT)).addImm(0);
  }
  else {
    DebugLoc DL = MI->getDebugLoc();
    BuildMI(MBB, MI, DL, TII->get(AArch64::HINT)).addImm(0);
  }

  ++NumNopsAdded;
}

bool
AArch64A53Fix835769::runOnBasicBlock(MachineBasicBlock &MBB) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Running on MBB: " << MBB
                    << " - scanning instructions...\n");

  // First, scan the basic block, looking for a sequence of 2 instructions
  // that match the conditions under which the erratum may trigger.

  // List of terminating instructions in matching sequences
  std::vector<MachineInstr*> Sequences;
  unsigned Idx = 0;
  MachineInstr *PrevInstr = nullptr;

  // Try and find the last non-pseudo instruction in any fallen through blocks,
  // if there isn't one, then we use nullptr to represent that.
  PrevInstr = getLastNonPseudo(MBB, TII);

  for (auto &MI : MBB) {
    MachineInstr *CurrInstr = &MI;
    LLVM_DEBUG(dbgs() << "  Examining: " << MI);
    if (PrevInstr) {
      LLVM_DEBUG(dbgs() << "    PrevInstr: " << *PrevInstr
                        << "    CurrInstr: " << *CurrInstr
                        << "    isFirstInstructionInSequence(PrevInstr): "
                        << isFirstInstructionInSequence(PrevInstr) << "\n"
                        << "    isSecondInstructionInSequence(CurrInstr): "
                        << isSecondInstructionInSequence(CurrInstr) << "\n");
      if (isFirstInstructionInSequence(PrevInstr) &&
          isSecondInstructionInSequence(CurrInstr)) {
        LLVM_DEBUG(dbgs() << "   ** pattern found at Idx " << Idx << "!\n");
        Sequences.push_back(CurrInstr);
      }
    }
    if (!CurrInstr->isPseudo())
      PrevInstr = CurrInstr;
    ++Idx;
  }

  LLVM_DEBUG(dbgs() << "Scan complete, " << Sequences.size()
                    << " occurrences of pattern found.\n");

  // Then update the basic block, inserting nops between the detected sequences.
  for (auto &MI : Sequences) {
    Changed = true;
    insertNopBeforeInstruction(MBB, MI, TII);
  }

  return Changed;
}

// Factory function used by AArch64TargetMachine to add the pass to
// the passmanager.
FunctionPass *llvm::createAArch64A53Fix835769() {
  return new AArch64A53Fix835769();
}
