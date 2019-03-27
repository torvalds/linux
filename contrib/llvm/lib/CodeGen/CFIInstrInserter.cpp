//===------ CFIInstrInserter.cpp - Insert additional CFI instructions -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This pass verifies incoming and outgoing CFA information of basic
/// blocks. CFA information is information about offset and register set by CFI
/// directives, valid at the start and end of a basic block. This pass checks
/// that outgoing information of predecessors matches incoming information of
/// their successors. Then it checks if blocks have correct CFA calculation rule
/// set and inserts additional CFI instruction at their beginnings if they
/// don't. CFI instructions are inserted if basic blocks have incorrect offset
/// or register set by previous blocks, as a result of a non-linear layout of
/// blocks in a function.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

static cl::opt<bool> VerifyCFI("verify-cfiinstrs",
    cl::desc("Verify Call Frame Information instructions"),
    cl::init(false),
    cl::Hidden);

namespace {
class CFIInstrInserter : public MachineFunctionPass {
 public:
  static char ID;

  CFIInstrInserter() : MachineFunctionPass(ID) {
    initializeCFIInstrInserterPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!MF.getMMI().hasDebugInfo() &&
        !MF.getFunction().needsUnwindTableEntry())
      return false;

    MBBVector.resize(MF.getNumBlockIDs());
    calculateCFAInfo(MF);

    if (VerifyCFI) {
      if (unsigned ErrorNum = verify(MF))
        report_fatal_error("Found " + Twine(ErrorNum) +
                           " in/out CFI information errors.");
    }
    bool insertedCFI = insertCFIInstrs(MF);
    MBBVector.clear();
    return insertedCFI;
  }

 private:
  struct MBBCFAInfo {
    MachineBasicBlock *MBB;
    /// Value of cfa offset valid at basic block entry.
    int IncomingCFAOffset = -1;
    /// Value of cfa offset valid at basic block exit.
    int OutgoingCFAOffset = -1;
    /// Value of cfa register valid at basic block entry.
    unsigned IncomingCFARegister = 0;
    /// Value of cfa register valid at basic block exit.
    unsigned OutgoingCFARegister = 0;
    /// If in/out cfa offset and register values for this block have already
    /// been set or not.
    bool Processed = false;
  };

  /// Contains cfa offset and register values valid at entry and exit of basic
  /// blocks.
  std::vector<MBBCFAInfo> MBBVector;

  /// Calculate cfa offset and register values valid at entry and exit for all
  /// basic blocks in a function.
  void calculateCFAInfo(MachineFunction &MF);
  /// Calculate cfa offset and register values valid at basic block exit by
  /// checking the block for CFI instructions. Block's incoming CFA info remains
  /// the same.
  void calculateOutgoingCFAInfo(MBBCFAInfo &MBBInfo);
  /// Update in/out cfa offset and register values for successors of the basic
  /// block.
  void updateSuccCFAInfo(MBBCFAInfo &MBBInfo);

  /// Check if incoming CFA information of a basic block matches outgoing CFA
  /// information of the previous block. If it doesn't, insert CFI instruction
  /// at the beginning of the block that corrects the CFA calculation rule for
  /// that block.
  bool insertCFIInstrs(MachineFunction &MF);
  /// Return the cfa offset value that should be set at the beginning of a MBB
  /// if needed. The negated value is needed when creating CFI instructions that
  /// set absolute offset.
  int getCorrectCFAOffset(MachineBasicBlock *MBB) {
    return -MBBVector[MBB->getNumber()].IncomingCFAOffset;
  }

  void report(const MBBCFAInfo &Pred, const MBBCFAInfo &Succ);
  /// Go through each MBB in a function and check that outgoing offset and
  /// register of its predecessors match incoming offset and register of that
  /// MBB, as well as that incoming offset and register of its successors match
  /// outgoing offset and register of the MBB.
  unsigned verify(MachineFunction &MF);
};
}  // namespace

char CFIInstrInserter::ID = 0;
INITIALIZE_PASS(CFIInstrInserter, "cfi-instr-inserter",
                "Check CFA info and insert CFI instructions if needed", false,
                false)
FunctionPass *llvm::createCFIInstrInserter() { return new CFIInstrInserter(); }

void CFIInstrInserter::calculateCFAInfo(MachineFunction &MF) {
  // Initial CFA offset value i.e. the one valid at the beginning of the
  // function.
  int InitialOffset =
      MF.getSubtarget().getFrameLowering()->getInitialCFAOffset(MF);
  // Initial CFA register value i.e. the one valid at the beginning of the
  // function.
  unsigned InitialRegister =
      MF.getSubtarget().getFrameLowering()->getInitialCFARegister(MF);

  // Initialize MBBMap.
  for (MachineBasicBlock &MBB : MF) {
    MBBCFAInfo MBBInfo;
    MBBInfo.MBB = &MBB;
    MBBInfo.IncomingCFAOffset = InitialOffset;
    MBBInfo.OutgoingCFAOffset = InitialOffset;
    MBBInfo.IncomingCFARegister = InitialRegister;
    MBBInfo.OutgoingCFARegister = InitialRegister;
    MBBVector[MBB.getNumber()] = MBBInfo;
  }

  // Set in/out cfa info for all blocks in the function. This traversal is based
  // on the assumption that the first block in the function is the entry block
  // i.e. that it has initial cfa offset and register values as incoming CFA
  // information.
  for (MachineBasicBlock &MBB : MF) {
    if (MBBVector[MBB.getNumber()].Processed) continue;
    updateSuccCFAInfo(MBBVector[MBB.getNumber()]);
  }
}

void CFIInstrInserter::calculateOutgoingCFAInfo(MBBCFAInfo &MBBInfo) {
  // Outgoing cfa offset set by the block.
  int SetOffset = MBBInfo.IncomingCFAOffset;
  // Outgoing cfa register set by the block.
  unsigned SetRegister = MBBInfo.IncomingCFARegister;
  const std::vector<MCCFIInstruction> &Instrs =
      MBBInfo.MBB->getParent()->getFrameInstructions();

  // Determine cfa offset and register set by the block.
  for (MachineInstr &MI : *MBBInfo.MBB) {
    if (MI.isCFIInstruction()) {
      unsigned CFIIndex = MI.getOperand(0).getCFIIndex();
      const MCCFIInstruction &CFI = Instrs[CFIIndex];
      switch (CFI.getOperation()) {
      case MCCFIInstruction::OpDefCfaRegister:
        SetRegister = CFI.getRegister();
        break;
      case MCCFIInstruction::OpDefCfaOffset:
        SetOffset = CFI.getOffset();
        break;
      case MCCFIInstruction::OpAdjustCfaOffset:
        SetOffset += CFI.getOffset();
        break;
      case MCCFIInstruction::OpDefCfa:
        SetRegister = CFI.getRegister();
        SetOffset = CFI.getOffset();
        break;
      case MCCFIInstruction::OpRememberState:
        // TODO: Add support for handling cfi_remember_state.
#ifndef NDEBUG
        report_fatal_error(
            "Support for cfi_remember_state not implemented! Value of CFA "
            "may be incorrect!\n");
#endif
        break;
      case MCCFIInstruction::OpRestoreState:
        // TODO: Add support for handling cfi_restore_state.
#ifndef NDEBUG
        report_fatal_error(
            "Support for cfi_restore_state not implemented! Value of CFA may "
            "be incorrect!\n");
#endif
        break;
      // Other CFI directives do not affect CFA value.
      case MCCFIInstruction::OpSameValue:
      case MCCFIInstruction::OpOffset:
      case MCCFIInstruction::OpRelOffset:
      case MCCFIInstruction::OpEscape:
      case MCCFIInstruction::OpRestore:
      case MCCFIInstruction::OpUndefined:
      case MCCFIInstruction::OpRegister:
      case MCCFIInstruction::OpWindowSave:
      case MCCFIInstruction::OpNegateRAState:
      case MCCFIInstruction::OpGnuArgsSize:
        break;
      }
    }
  }

  MBBInfo.Processed = true;

  // Update outgoing CFA info.
  MBBInfo.OutgoingCFAOffset = SetOffset;
  MBBInfo.OutgoingCFARegister = SetRegister;
}

void CFIInstrInserter::updateSuccCFAInfo(MBBCFAInfo &MBBInfo) {
  SmallVector<MachineBasicBlock *, 4> Stack;
  Stack.push_back(MBBInfo.MBB);

  do {
    MachineBasicBlock *Current = Stack.pop_back_val();
    MBBCFAInfo &CurrentInfo = MBBVector[Current->getNumber()];
    if (CurrentInfo.Processed)
      continue;

    calculateOutgoingCFAInfo(CurrentInfo);
    for (auto *Succ : CurrentInfo.MBB->successors()) {
      MBBCFAInfo &SuccInfo = MBBVector[Succ->getNumber()];
      if (!SuccInfo.Processed) {
        SuccInfo.IncomingCFAOffset = CurrentInfo.OutgoingCFAOffset;
        SuccInfo.IncomingCFARegister = CurrentInfo.OutgoingCFARegister;
        Stack.push_back(Succ);
      }
    }
  } while (!Stack.empty());
}

bool CFIInstrInserter::insertCFIInstrs(MachineFunction &MF) {
  const MBBCFAInfo *PrevMBBInfo = &MBBVector[MF.front().getNumber()];
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  bool InsertedCFIInstr = false;

  for (MachineBasicBlock &MBB : MF) {
    // Skip the first MBB in a function
    if (MBB.getNumber() == MF.front().getNumber()) continue;

    const MBBCFAInfo &MBBInfo = MBBVector[MBB.getNumber()];
    auto MBBI = MBBInfo.MBB->begin();
    DebugLoc DL = MBBInfo.MBB->findDebugLoc(MBBI);

    if (PrevMBBInfo->OutgoingCFAOffset != MBBInfo.IncomingCFAOffset) {
      // If both outgoing offset and register of a previous block don't match
      // incoming offset and register of this block, add a def_cfa instruction
      // with the correct offset and register for this block.
      if (PrevMBBInfo->OutgoingCFARegister != MBBInfo.IncomingCFARegister) {
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfa(
            nullptr, MBBInfo.IncomingCFARegister, getCorrectCFAOffset(&MBB)));
        BuildMI(*MBBInfo.MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
        // If outgoing offset of a previous block doesn't match incoming offset
        // of this block, add a def_cfa_offset instruction with the correct
        // offset for this block.
      } else {
        unsigned CFIIndex =
            MF.addFrameInst(MCCFIInstruction::createDefCfaOffset(
                nullptr, getCorrectCFAOffset(&MBB)));
        BuildMI(*MBBInfo.MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }
      InsertedCFIInstr = true;
      // If outgoing register of a previous block doesn't match incoming
      // register of this block, add a def_cfa_register instruction with the
      // correct register for this block.
    } else if (PrevMBBInfo->OutgoingCFARegister !=
               MBBInfo.IncomingCFARegister) {
      unsigned CFIIndex =
          MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
              nullptr, MBBInfo.IncomingCFARegister));
      BuildMI(*MBBInfo.MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
      InsertedCFIInstr = true;
    }
    PrevMBBInfo = &MBBInfo;
  }
  return InsertedCFIInstr;
}

void CFIInstrInserter::report(const MBBCFAInfo &Pred, const MBBCFAInfo &Succ) {
  errs() << "*** Inconsistent CFA register and/or offset between pred and succ "
            "***\n";
  errs() << "Pred: " << Pred.MBB->getName() << " #" << Pred.MBB->getNumber()
         << " in " << Pred.MBB->getParent()->getName()
         << " outgoing CFA Reg:" << Pred.OutgoingCFARegister << "\n";
  errs() << "Pred: " << Pred.MBB->getName() << " #" << Pred.MBB->getNumber()
         << " in " << Pred.MBB->getParent()->getName()
         << " outgoing CFA Offset:" << Pred.OutgoingCFAOffset << "\n";
  errs() << "Succ: " << Succ.MBB->getName() << " #" << Succ.MBB->getNumber()
         << " incoming CFA Reg:" << Succ.IncomingCFARegister << "\n";
  errs() << "Succ: " << Succ.MBB->getName() << " #" << Succ.MBB->getNumber()
         << " incoming CFA Offset:" << Succ.IncomingCFAOffset << "\n";
}

unsigned CFIInstrInserter::verify(MachineFunction &MF) {
  unsigned ErrorNum = 0;
  for (auto *CurrMBB : depth_first(&MF)) {
    const MBBCFAInfo &CurrMBBInfo = MBBVector[CurrMBB->getNumber()];
    for (MachineBasicBlock *Succ : CurrMBB->successors()) {
      const MBBCFAInfo &SuccMBBInfo = MBBVector[Succ->getNumber()];
      // Check that incoming offset and register values of successors match the
      // outgoing offset and register values of CurrMBB
      if (SuccMBBInfo.IncomingCFAOffset != CurrMBBInfo.OutgoingCFAOffset ||
          SuccMBBInfo.IncomingCFARegister != CurrMBBInfo.OutgoingCFARegister) {
        // Inconsistent offsets/registers are ok for 'noreturn' blocks because
        // we don't generate epilogues inside such blocks.
        if (SuccMBBInfo.MBB->succ_empty() && !SuccMBBInfo.MBB->isReturnBlock())
          continue;
        report(CurrMBBInfo, SuccMBBInfo);
        ErrorNum++;
      }
    }
  }
  return ErrorNum;
}
