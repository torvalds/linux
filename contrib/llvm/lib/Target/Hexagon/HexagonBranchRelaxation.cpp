//===--- HexagonBranchRelaxation.cpp - Identify and relax long jumps ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "hexagon-brelax"

#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>

using namespace llvm;

// Since we have no exact knowledge of code layout, allow some safety buffer
// for jump target. This is measured in bytes.
static cl::opt<uint32_t> BranchRelaxSafetyBuffer("branch-relax-safety-buffer",
  cl::init(200), cl::Hidden, cl::ZeroOrMore, cl::desc("safety buffer size"));

namespace llvm {

  FunctionPass *createHexagonBranchRelaxation();
  void initializeHexagonBranchRelaxationPass(PassRegistry&);

} // end namespace llvm

namespace {

  struct HexagonBranchRelaxation : public MachineFunctionPass {
  public:
    static char ID;

    HexagonBranchRelaxation() : MachineFunctionPass(ID) {
      initializeHexagonBranchRelaxationPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    StringRef getPassName() const override {
      return "Hexagon Branch Relaxation";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

  private:
    const HexagonInstrInfo *HII;
    const HexagonRegisterInfo *HRI;

    bool relaxBranches(MachineFunction &MF);
    void computeOffset(MachineFunction &MF,
          DenseMap<MachineBasicBlock*, unsigned> &BlockToInstOffset);
    bool reGenerateBranch(MachineFunction &MF,
          DenseMap<MachineBasicBlock*, unsigned> &BlockToInstOffset);
    bool isJumpOutOfRange(MachineInstr &MI,
          DenseMap<MachineBasicBlock*, unsigned> &BlockToInstOffset);
  };

  char HexagonBranchRelaxation::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(HexagonBranchRelaxation, "hexagon-brelax",
                "Hexagon Branch Relaxation", false, false)

FunctionPass *llvm::createHexagonBranchRelaxation() {
  return new HexagonBranchRelaxation();
}

bool HexagonBranchRelaxation::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "****** Hexagon Branch Relaxation ******\n");

  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  HII = HST.getInstrInfo();
  HRI = HST.getRegisterInfo();

  bool Changed = false;
  Changed = relaxBranches(MF);
  return Changed;
}

void HexagonBranchRelaxation::computeOffset(MachineFunction &MF,
      DenseMap<MachineBasicBlock*, unsigned> &OffsetMap) {
  // offset of the current instruction from the start.
  unsigned InstOffset = 0;
  for (auto &B : MF) {
    if (B.getAlignment()) {
      // Although we don't know the exact layout of the final code, we need
      // to account for alignment padding somehow. This heuristic pads each
      // aligned basic block according to the alignment value.
      int ByteAlign = (1u << B.getAlignment()) - 1;
      InstOffset = (InstOffset + ByteAlign) & ~(ByteAlign);
    }
    OffsetMap[&B] = InstOffset;
    for (auto &MI : B.instrs()) {
      InstOffset += HII->getSize(MI);
      // Assume that all extendable branches will be extended.
      if (MI.isBranch() && HII->isExtendable(MI))
        InstOffset += HEXAGON_INSTR_SIZE;
    }
  }
}

/// relaxBranches - For Hexagon, if the jump target/loop label is too far from
/// the jump/loop instruction then, we need to make sure that we have constant
/// extenders set for jumps and loops.

/// There are six iterations in this phase. It's self explanatory below.
bool HexagonBranchRelaxation::relaxBranches(MachineFunction &MF) {
  // Compute the offset of each basic block
  // offset of the current instruction from the start.
  // map for each instruction to the beginning of the function
  DenseMap<MachineBasicBlock*, unsigned> BlockToInstOffset;
  computeOffset(MF, BlockToInstOffset);

  return reGenerateBranch(MF, BlockToInstOffset);
}

/// Check if a given instruction is:
/// - a jump to a distant target
/// - that exceeds its immediate range
/// If both conditions are true, it requires constant extension.
bool HexagonBranchRelaxation::isJumpOutOfRange(MachineInstr &MI,
      DenseMap<MachineBasicBlock*, unsigned> &BlockToInstOffset) {
  MachineBasicBlock &B = *MI.getParent();
  auto FirstTerm = B.getFirstInstrTerminator();
  if (FirstTerm == B.instr_end())
    return false;

  if (HII->isExtended(MI))
    return false;

  unsigned InstOffset = BlockToInstOffset[&B];
  unsigned Distance = 0;

  // To save time, estimate exact position of a branch instruction
  // as one at the end of the MBB.
  // Number of instructions times typical instruction size.
  InstOffset += HII->nonDbgBBSize(&B) * HEXAGON_INSTR_SIZE;

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;

  // Try to analyze this branch.
  if (HII->analyzeBranch(B, TBB, FBB, Cond, false)) {
    // Could not analyze it. See if this is something we can recognize.
    // If it is a NVJ, it should always have its target in
    // a fixed location.
    if (HII->isNewValueJump(*FirstTerm))
      TBB = FirstTerm->getOperand(HII->getCExtOpNum(*FirstTerm)).getMBB();
  }
  if (TBB && &MI == &*FirstTerm) {
    Distance = std::abs((long long)InstOffset - BlockToInstOffset[TBB])
                + BranchRelaxSafetyBuffer;
    return !HII->isJumpWithinBranchRange(*FirstTerm, Distance);
  }
  if (FBB) {
    // Look for second terminator.
    auto SecondTerm = std::next(FirstTerm);
    assert(SecondTerm != B.instr_end() &&
          (SecondTerm->isBranch() || SecondTerm->isCall()) &&
          "Bad second terminator");
    if (&MI != &*SecondTerm)
      return false;
    // Analyze the second branch in the BB.
    Distance = std::abs((long long)InstOffset - BlockToInstOffset[FBB])
                + BranchRelaxSafetyBuffer;
    return !HII->isJumpWithinBranchRange(*SecondTerm, Distance);
  }
  return false;
}

bool HexagonBranchRelaxation::reGenerateBranch(MachineFunction &MF,
      DenseMap<MachineBasicBlock*, unsigned> &BlockToInstOffset) {
  bool Changed = false;

  for (auto &B : MF) {
    for (auto &MI : B) {
      if (!MI.isBranch() || !isJumpOutOfRange(MI, BlockToInstOffset))
        continue;
      LLVM_DEBUG(dbgs() << "Long distance jump. isExtendable("
                        << HII->isExtendable(MI) << ") isConstExtended("
                        << HII->isConstExtended(MI) << ") " << MI);

      // Since we have not merged HW loops relaxation into
      // this code (yet), soften our approach for the moment.
      if (!HII->isExtendable(MI) && !HII->isExtended(MI)) {
        LLVM_DEBUG(dbgs() << "\tUnderimplemented relax branch instruction.\n");
      } else {
        // Find which operand is expandable.
        int ExtOpNum = HII->getCExtOpNum(MI);
        MachineOperand &MO = MI.getOperand(ExtOpNum);
        // This need to be something we understand. So far we assume all
        // branches have only MBB address as expandable field.
        // If it changes, this will need to be expanded.
        assert(MO.isMBB() && "Branch with unknown expandable field type");
        // Mark given operand as extended.
        MO.addTargetFlag(HexagonII::HMOTF_ConstExtended);
        Changed = true;
      }
    }
  }
  return Changed;
}
