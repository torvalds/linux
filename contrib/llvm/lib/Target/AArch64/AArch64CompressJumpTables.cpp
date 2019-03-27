//==-- AArch64CompressJumpTables.cpp - Compress jump tables for AArch64 --====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// This pass looks at the basic blocks each jump-table refers to and works out
// whether they can be emitted in a compressed form (with 8 or 16-bit
// entries). If so, it changes the opcode and flags them in the associated
// AArch64FunctionInfo.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-jump-tables"

STATISTIC(NumJT8, "Number of jump-tables with 1-byte entries");
STATISTIC(NumJT16, "Number of jump-tables with 2-byte entries");
STATISTIC(NumJT32, "Number of jump-tables with 4-byte entries");

namespace {
class AArch64CompressJumpTables : public MachineFunctionPass {
  const TargetInstrInfo *TII;
  MachineFunction *MF;
  SmallVector<int, 8> BlockInfo;

  int computeBlockSize(MachineBasicBlock &MBB);
  void scanFunction();

  bool compressJumpTable(MachineInstr &MI, int Offset);

public:
  static char ID;
  AArch64CompressJumpTables() : MachineFunctionPass(ID) {
    initializeAArch64CompressJumpTablesPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }
  StringRef getPassName() const override {
    return "AArch64 Compress Jump Tables";
  }
};
char AArch64CompressJumpTables::ID = 0;
}

INITIALIZE_PASS(AArch64CompressJumpTables, DEBUG_TYPE,
                "AArch64 compress jump tables pass", false, false)

int AArch64CompressJumpTables::computeBlockSize(MachineBasicBlock &MBB) {
  int Size = 0;
  for (const MachineInstr &MI : MBB)
    Size += TII->getInstSizeInBytes(MI);
  return Size;
}

void AArch64CompressJumpTables::scanFunction() {
  BlockInfo.clear();
  BlockInfo.resize(MF->getNumBlockIDs());

  int Offset = 0;
  for (MachineBasicBlock &MBB : *MF) {
    BlockInfo[MBB.getNumber()] = Offset;
    Offset += computeBlockSize(MBB);
  }
}

bool AArch64CompressJumpTables::compressJumpTable(MachineInstr &MI,
                                                  int Offset) {
  if (MI.getOpcode() != AArch64::JumpTableDest32)
    return false;

  int JTIdx = MI.getOperand(4).getIndex();
  auto &JTInfo = *MF->getJumpTableInfo();
  const MachineJumpTableEntry &JT = JTInfo.getJumpTables()[JTIdx];

  // The jump-table might have been optimized away.
  if (JT.MBBs.empty())
    return false;

  int MaxOffset = std::numeric_limits<int>::min(),
      MinOffset = std::numeric_limits<int>::max();
  MachineBasicBlock *MinBlock = nullptr;
  for (auto Block : JT.MBBs) {
    int BlockOffset = BlockInfo[Block->getNumber()];
    assert(BlockOffset % 4 == 0 && "misaligned basic block");

    MaxOffset = std::max(MaxOffset, BlockOffset);
    if (BlockOffset <= MinOffset) {
      MinOffset = BlockOffset;
      MinBlock = Block;
    }
  }

  // The ADR instruction needed to calculate the address of the first reachable
  // basic block can address +/-1MB.
  if (!isInt<21>(MinOffset - Offset)) {
    ++NumJT32;
    return false;
  }

  int Span = MaxOffset - MinOffset;
  auto AFI = MF->getInfo<AArch64FunctionInfo>();
  if (isUInt<8>(Span / 4)) {
    AFI->setJumpTableEntryInfo(JTIdx, 1, MinBlock->getSymbol());
    MI.setDesc(TII->get(AArch64::JumpTableDest8));
    ++NumJT8;
    return true;
  } else if (isUInt<16>(Span / 4)) {
    AFI->setJumpTableEntryInfo(JTIdx, 2, MinBlock->getSymbol());
    MI.setDesc(TII->get(AArch64::JumpTableDest16));
    ++NumJT16;
    return true;
  }

  ++NumJT32;
  return false;
}

bool AArch64CompressJumpTables::runOnMachineFunction(MachineFunction &MFIn) {
  bool Changed = false;
  MF = &MFIn;

  const auto &ST = MF->getSubtarget<AArch64Subtarget>();
  TII = ST.getInstrInfo();

  if (ST.force32BitJumpTables() && !MF->getFunction().optForMinSize())
    return false;

  scanFunction();

  for (MachineBasicBlock &MBB : *MF) {
    int Offset = BlockInfo[MBB.getNumber()];
    for (MachineInstr &MI : MBB) {
      Changed |= compressJumpTable(MI, Offset);
      Offset += TII->getInstSizeInBytes(MI);
    }
  }

  return Changed;
}

FunctionPass *llvm::createAArch64CompressJumpTablesPass() {
  return new AArch64CompressJumpTables();
}
