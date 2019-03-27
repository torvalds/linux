//===- SILoadStoreOptimizer.cpp -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass tries to fuse DS instructions with close by immediate offsets.
// This will fuse operations such as
//  ds_read_b32 v0, v2 offset:16
//  ds_read_b32 v1, v2 offset:32
// ==>
//   ds_read2_b32 v[0:1], v2, offset0:4 offset1:8
//
// The same is done for certain SMEM and VMEM opcodes, e.g.:
//  s_buffer_load_dword s4, s[0:3], 4
//  s_buffer_load_dword s5, s[0:3], 8
// ==>
//  s_buffer_load_dwordx2 s[4:5], s[0:3], 4
//
// This pass also tries to promote constant offset to the immediate by
// adjusting the base. It tries to use a base from the nearby instructions that
// allows it to have a 13bit constant offset and then promotes the 13bit offset
// to the immediate.
// E.g.
//  s_movk_i32 s0, 0x1800
//  v_add_co_u32_e32 v0, vcc, s0, v2
//  v_addc_co_u32_e32 v1, vcc, 0, v6, vcc
//
//  s_movk_i32 s0, 0x1000
//  v_add_co_u32_e32 v5, vcc, s0, v2
//  v_addc_co_u32_e32 v6, vcc, 0, v6, vcc
//  global_load_dwordx2 v[5:6], v[5:6], off
//  global_load_dwordx2 v[0:1], v[0:1], off
// =>
//  s_movk_i32 s0, 0x1000
//  v_add_co_u32_e32 v5, vcc, s0, v2
//  v_addc_co_u32_e32 v6, vcc, 0, v6, vcc
//  global_load_dwordx2 v[5:6], v[5:6], off
//  global_load_dwordx2 v[0:1], v[5:6], off offset:2048
//
// Future improvements:
//
// - This currently relies on the scheduler to place loads and stores next to
//   each other, and then only merges adjacent pairs of instructions. It would
//   be good to be more flexible with interleaved instructions, and possibly run
//   before scheduling. It currently missing stores of constants because loading
//   the constant into the data register is placed between the stores, although
//   this is arguably a scheduling problem.
//
// - Live interval recomputing seems inefficient. This currently only matches
//   one pair, and recomputes live intervals and moves on to the next pair. It
//   would be better to compute a list of all merges that need to occur.
//
// - With a list of instructions to process, we can also merge more. If a
//   cluster of loads have offsets that are too large to fit in the 8-bit
//   offsets, but are close enough to fit in the 8 bits, we can add to the base
//   pointer and use the new reduced offsets.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "SIRegisterInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "si-load-store-opt"

namespace {
enum InstClassEnum {
  UNKNOWN,
  DS_READ,
  DS_WRITE,
  S_BUFFER_LOAD_IMM,
  BUFFER_LOAD_OFFEN = AMDGPU::BUFFER_LOAD_DWORD_OFFEN,
  BUFFER_LOAD_OFFSET = AMDGPU::BUFFER_LOAD_DWORD_OFFSET,
  BUFFER_STORE_OFFEN = AMDGPU::BUFFER_STORE_DWORD_OFFEN,
  BUFFER_STORE_OFFSET = AMDGPU::BUFFER_STORE_DWORD_OFFSET,
  BUFFER_LOAD_OFFEN_exact = AMDGPU::BUFFER_LOAD_DWORD_OFFEN_exact,
  BUFFER_LOAD_OFFSET_exact = AMDGPU::BUFFER_LOAD_DWORD_OFFSET_exact,
  BUFFER_STORE_OFFEN_exact = AMDGPU::BUFFER_STORE_DWORD_OFFEN_exact,
  BUFFER_STORE_OFFSET_exact = AMDGPU::BUFFER_STORE_DWORD_OFFSET_exact,
};

enum RegisterEnum {
  SBASE = 0x1,
  SRSRC = 0x2,
  SOFFSET = 0x4,
  VADDR = 0x8,
  ADDR = 0x10,
};

class SILoadStoreOptimizer : public MachineFunctionPass {
  struct CombineInfo {
    MachineBasicBlock::iterator I;
    MachineBasicBlock::iterator Paired;
    unsigned EltSize;
    unsigned Offset0;
    unsigned Offset1;
    unsigned Width0;
    unsigned Width1;
    unsigned BaseOff;
    InstClassEnum InstClass;
    bool GLC0;
    bool GLC1;
    bool SLC0;
    bool SLC1;
    bool UseST64;
    SmallVector<MachineInstr *, 8> InstsToMove;
  };

  struct BaseRegisters {
    unsigned LoReg = 0;
    unsigned HiReg = 0;

    unsigned LoSubReg = 0;
    unsigned HiSubReg = 0;
  };

  struct MemAddress {
    BaseRegisters Base;
    int64_t Offset = 0;
  };

  using MemInfoMap = DenseMap<MachineInstr *, MemAddress>;

private:
  const GCNSubtarget *STM = nullptr;
  const SIInstrInfo *TII = nullptr;
  const SIRegisterInfo *TRI = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  AliasAnalysis *AA = nullptr;
  bool OptimizeAgain;

  static bool offsetsCanBeCombined(CombineInfo &CI);
  static bool widthsFit(const GCNSubtarget &STM, const CombineInfo &CI);
  static unsigned getNewOpcode(const CombineInfo &CI);
  static std::pair<unsigned, unsigned> getSubRegIdxs(const CombineInfo &CI);
  const TargetRegisterClass *getTargetRegisterClass(const CombineInfo &CI);
  unsigned getOpcodeWidth(const MachineInstr &MI);
  InstClassEnum getInstClass(unsigned Opc);
  unsigned getRegs(unsigned Opc);

  bool findMatchingInst(CombineInfo &CI);

  unsigned read2Opcode(unsigned EltSize) const;
  unsigned read2ST64Opcode(unsigned EltSize) const;
  MachineBasicBlock::iterator mergeRead2Pair(CombineInfo &CI);

  unsigned write2Opcode(unsigned EltSize) const;
  unsigned write2ST64Opcode(unsigned EltSize) const;
  MachineBasicBlock::iterator mergeWrite2Pair(CombineInfo &CI);
  MachineBasicBlock::iterator mergeSBufferLoadImmPair(CombineInfo &CI);
  MachineBasicBlock::iterator mergeBufferLoadPair(CombineInfo &CI);
  MachineBasicBlock::iterator mergeBufferStorePair(CombineInfo &CI);

  void updateBaseAndOffset(MachineInstr &I, unsigned NewBase,
                           int32_t NewOffset);
  unsigned computeBase(MachineInstr &MI, const MemAddress &Addr);
  MachineOperand createRegOrImm(int32_t Val, MachineInstr &MI);
  Optional<int32_t> extractConstOffset(const MachineOperand &Op);
  void processBaseWithConstOffset(const MachineOperand &Base, MemAddress &Addr);
  /// Promotes constant offset to the immediate by adjusting the base. It
  /// tries to use a base from the nearby instructions that allows it to have
  /// a 13bit constant offset which gets promoted to the immediate.
  bool promoteConstantOffsetToImm(MachineInstr &CI,
                                  MemInfoMap &Visited,
                                  SmallPtrSet<MachineInstr *, 4> &Promoted);

public:
  static char ID;

  SILoadStoreOptimizer() : MachineFunctionPass(ID) {
    initializeSILoadStoreOptimizerPass(*PassRegistry::getPassRegistry());
  }

  bool optimizeBlock(MachineBasicBlock &MBB);

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Load Store Optimizer"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AAResultsWrapperPass>();

    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace.

INITIALIZE_PASS_BEGIN(SILoadStoreOptimizer, DEBUG_TYPE,
                      "SI Load Store Optimizer", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(SILoadStoreOptimizer, DEBUG_TYPE, "SI Load Store Optimizer",
                    false, false)

char SILoadStoreOptimizer::ID = 0;

char &llvm::SILoadStoreOptimizerID = SILoadStoreOptimizer::ID;

FunctionPass *llvm::createSILoadStoreOptimizerPass() {
  return new SILoadStoreOptimizer();
}

static void moveInstsAfter(MachineBasicBlock::iterator I,
                           ArrayRef<MachineInstr *> InstsToMove) {
  MachineBasicBlock *MBB = I->getParent();
  ++I;
  for (MachineInstr *MI : InstsToMove) {
    MI->removeFromParent();
    MBB->insert(I, MI);
  }
}

static void addDefsUsesToList(const MachineInstr &MI,
                              DenseSet<unsigned> &RegDefs,
                              DenseSet<unsigned> &PhysRegUses) {
  for (const MachineOperand &Op : MI.operands()) {
    if (Op.isReg()) {
      if (Op.isDef())
        RegDefs.insert(Op.getReg());
      else if (Op.readsReg() &&
               TargetRegisterInfo::isPhysicalRegister(Op.getReg()))
        PhysRegUses.insert(Op.getReg());
    }
  }
}

static bool memAccessesCanBeReordered(MachineBasicBlock::iterator A,
                                      MachineBasicBlock::iterator B,
                                      const SIInstrInfo *TII,
                                      AliasAnalysis *AA) {
  // RAW or WAR - cannot reorder
  // WAW - cannot reorder
  // RAR - safe to reorder
  return !(A->mayStore() || B->mayStore()) ||
         TII->areMemAccessesTriviallyDisjoint(*A, *B, AA);
}

// Add MI and its defs to the lists if MI reads one of the defs that are
// already in the list. Returns true in that case.
static bool addToListsIfDependent(MachineInstr &MI, DenseSet<unsigned> &RegDefs,
                                  DenseSet<unsigned> &PhysRegUses,
                                  SmallVectorImpl<MachineInstr *> &Insts) {
  for (MachineOperand &Use : MI.operands()) {
    // If one of the defs is read, then there is a use of Def between I and the
    // instruction that I will potentially be merged with. We will need to move
    // this instruction after the merged instructions.
    //
    // Similarly, if there is a def which is read by an instruction that is to
    // be moved for merging, then we need to move the def-instruction as well.
    // This can only happen for physical registers such as M0; virtual
    // registers are in SSA form.
    if (Use.isReg() &&
        ((Use.readsReg() && RegDefs.count(Use.getReg())) ||
         (Use.isDef() && TargetRegisterInfo::isPhysicalRegister(Use.getReg()) &&
          PhysRegUses.count(Use.getReg())))) {
      Insts.push_back(&MI);
      addDefsUsesToList(MI, RegDefs, PhysRegUses);
      return true;
    }
  }

  return false;
}

static bool canMoveInstsAcrossMemOp(MachineInstr &MemOp,
                                    ArrayRef<MachineInstr *> InstsToMove,
                                    const SIInstrInfo *TII, AliasAnalysis *AA) {
  assert(MemOp.mayLoadOrStore());

  for (MachineInstr *InstToMove : InstsToMove) {
    if (!InstToMove->mayLoadOrStore())
      continue;
    if (!memAccessesCanBeReordered(MemOp, *InstToMove, TII, AA))
      return false;
  }
  return true;
}

bool SILoadStoreOptimizer::offsetsCanBeCombined(CombineInfo &CI) {
  // XXX - Would the same offset be OK? Is there any reason this would happen or
  // be useful?
  if (CI.Offset0 == CI.Offset1)
    return false;

  // This won't be valid if the offset isn't aligned.
  if ((CI.Offset0 % CI.EltSize != 0) || (CI.Offset1 % CI.EltSize != 0))
    return false;

  unsigned EltOffset0 = CI.Offset0 / CI.EltSize;
  unsigned EltOffset1 = CI.Offset1 / CI.EltSize;
  CI.UseST64 = false;
  CI.BaseOff = 0;

  // Handle SMEM and VMEM instructions.
  if ((CI.InstClass != DS_READ) && (CI.InstClass != DS_WRITE)) {
    return (EltOffset0 + CI.Width0 == EltOffset1 ||
            EltOffset1 + CI.Width1 == EltOffset0) &&
           CI.GLC0 == CI.GLC1 &&
           (CI.InstClass == S_BUFFER_LOAD_IMM || CI.SLC0 == CI.SLC1);
  }

  // If the offset in elements doesn't fit in 8-bits, we might be able to use
  // the stride 64 versions.
  if ((EltOffset0 % 64 == 0) && (EltOffset1 % 64) == 0 &&
      isUInt<8>(EltOffset0 / 64) && isUInt<8>(EltOffset1 / 64)) {
    CI.Offset0 = EltOffset0 / 64;
    CI.Offset1 = EltOffset1 / 64;
    CI.UseST64 = true;
    return true;
  }

  // Check if the new offsets fit in the reduced 8-bit range.
  if (isUInt<8>(EltOffset0) && isUInt<8>(EltOffset1)) {
    CI.Offset0 = EltOffset0;
    CI.Offset1 = EltOffset1;
    return true;
  }

  // Try to shift base address to decrease offsets.
  unsigned OffsetDiff = std::abs((int)EltOffset1 - (int)EltOffset0);
  CI.BaseOff = std::min(CI.Offset0, CI.Offset1);

  if ((OffsetDiff % 64 == 0) && isUInt<8>(OffsetDiff / 64)) {
    CI.Offset0 = (EltOffset0 - CI.BaseOff / CI.EltSize) / 64;
    CI.Offset1 = (EltOffset1 - CI.BaseOff / CI.EltSize) / 64;
    CI.UseST64 = true;
    return true;
  }

  if (isUInt<8>(OffsetDiff)) {
    CI.Offset0 = EltOffset0 - CI.BaseOff / CI.EltSize;
    CI.Offset1 = EltOffset1 - CI.BaseOff / CI.EltSize;
    return true;
  }

  return false;
}

bool SILoadStoreOptimizer::widthsFit(const GCNSubtarget &STM,
                                     const CombineInfo &CI) {
  const unsigned Width = (CI.Width0 + CI.Width1);
  switch (CI.InstClass) {
  default:
    return (Width <= 4) && (STM.hasDwordx3LoadStores() || (Width != 3));
  case S_BUFFER_LOAD_IMM:
    switch (Width) {
    default:
      return false;
    case 2:
    case 4:
      return true;
    }
  }
}

unsigned SILoadStoreOptimizer::getOpcodeWidth(const MachineInstr &MI) {
  const unsigned Opc = MI.getOpcode();

  if (TII->isMUBUF(MI)) {
    return AMDGPU::getMUBUFDwords(Opc);
  }

  switch (Opc) {
  default:
    return 0;
  case AMDGPU::S_BUFFER_LOAD_DWORD_IMM:
    return 1;
  case AMDGPU::S_BUFFER_LOAD_DWORDX2_IMM:
    return 2;
  case AMDGPU::S_BUFFER_LOAD_DWORDX4_IMM:
    return 4;
  }
}

InstClassEnum SILoadStoreOptimizer::getInstClass(unsigned Opc) {
  if (TII->isMUBUF(Opc)) {
    const int baseOpcode = AMDGPU::getMUBUFBaseOpcode(Opc);

    // If we couldn't identify the opcode, bail out.
    if (baseOpcode == -1) {
      return UNKNOWN;
    }

    switch (baseOpcode) {
    default:
      return UNKNOWN;
    case AMDGPU::BUFFER_LOAD_DWORD_OFFEN:
      return BUFFER_LOAD_OFFEN;
    case AMDGPU::BUFFER_LOAD_DWORD_OFFSET:
      return BUFFER_LOAD_OFFSET;
    case AMDGPU::BUFFER_STORE_DWORD_OFFEN:
      return BUFFER_STORE_OFFEN;
    case AMDGPU::BUFFER_STORE_DWORD_OFFSET:
      return BUFFER_STORE_OFFSET;
    case AMDGPU::BUFFER_LOAD_DWORD_OFFEN_exact:
      return BUFFER_LOAD_OFFEN_exact;
    case AMDGPU::BUFFER_LOAD_DWORD_OFFSET_exact:
      return BUFFER_LOAD_OFFSET_exact;
    case AMDGPU::BUFFER_STORE_DWORD_OFFEN_exact:
      return BUFFER_STORE_OFFEN_exact;
    case AMDGPU::BUFFER_STORE_DWORD_OFFSET_exact:
      return BUFFER_STORE_OFFSET_exact;
    }
  }

  switch (Opc) {
  default:
    return UNKNOWN;
  case AMDGPU::S_BUFFER_LOAD_DWORD_IMM:
  case AMDGPU::S_BUFFER_LOAD_DWORDX2_IMM:
  case AMDGPU::S_BUFFER_LOAD_DWORDX4_IMM:
    return S_BUFFER_LOAD_IMM;
  case AMDGPU::DS_READ_B32:
  case AMDGPU::DS_READ_B64:
  case AMDGPU::DS_READ_B32_gfx9:
  case AMDGPU::DS_READ_B64_gfx9:
    return DS_READ;
  case AMDGPU::DS_WRITE_B32:
  case AMDGPU::DS_WRITE_B64:
  case AMDGPU::DS_WRITE_B32_gfx9:
  case AMDGPU::DS_WRITE_B64_gfx9:
    return DS_WRITE;
  }
}

unsigned SILoadStoreOptimizer::getRegs(unsigned Opc) {
  if (TII->isMUBUF(Opc)) {
    unsigned result = 0;

    if (AMDGPU::getMUBUFHasVAddr(Opc)) {
      result |= VADDR;
    }

    if (AMDGPU::getMUBUFHasSrsrc(Opc)) {
      result |= SRSRC;
    }

    if (AMDGPU::getMUBUFHasSoffset(Opc)) {
      result |= SOFFSET;
    }

    return result;
  }

  switch (Opc) {
  default:
    return 0;
  case AMDGPU::S_BUFFER_LOAD_DWORD_IMM:
  case AMDGPU::S_BUFFER_LOAD_DWORDX2_IMM:
  case AMDGPU::S_BUFFER_LOAD_DWORDX4_IMM:
    return SBASE;
  case AMDGPU::DS_READ_B32:
  case AMDGPU::DS_READ_B64:
  case AMDGPU::DS_READ_B32_gfx9:
  case AMDGPU::DS_READ_B64_gfx9:
  case AMDGPU::DS_WRITE_B32:
  case AMDGPU::DS_WRITE_B64:
  case AMDGPU::DS_WRITE_B32_gfx9:
  case AMDGPU::DS_WRITE_B64_gfx9:
    return ADDR;
  }
}

bool SILoadStoreOptimizer::findMatchingInst(CombineInfo &CI) {
  MachineBasicBlock *MBB = CI.I->getParent();
  MachineBasicBlock::iterator E = MBB->end();
  MachineBasicBlock::iterator MBBI = CI.I;

  const unsigned Opc = CI.I->getOpcode();
  const InstClassEnum InstClass = getInstClass(Opc);

  if (InstClass == UNKNOWN) {
    return false;
  }

  const unsigned Regs = getRegs(Opc);

  unsigned AddrOpName[5] = {0};
  int AddrIdx[5];
  const MachineOperand *AddrReg[5];
  unsigned NumAddresses = 0;

  if (Regs & ADDR) {
    AddrOpName[NumAddresses++] = AMDGPU::OpName::addr;
  }

  if (Regs & SBASE) {
    AddrOpName[NumAddresses++] = AMDGPU::OpName::sbase;
  }

  if (Regs & SRSRC) {
    AddrOpName[NumAddresses++] = AMDGPU::OpName::srsrc;
  }

  if (Regs & SOFFSET) {
    AddrOpName[NumAddresses++] = AMDGPU::OpName::soffset;
  }

  if (Regs & VADDR) {
    AddrOpName[NumAddresses++] = AMDGPU::OpName::vaddr;
  }

  for (unsigned i = 0; i < NumAddresses; i++) {
    AddrIdx[i] = AMDGPU::getNamedOperandIdx(CI.I->getOpcode(), AddrOpName[i]);
    AddrReg[i] = &CI.I->getOperand(AddrIdx[i]);

    // We only ever merge operations with the same base address register, so
    // don't bother scanning forward if there are no other uses.
    if (AddrReg[i]->isReg() &&
        (TargetRegisterInfo::isPhysicalRegister(AddrReg[i]->getReg()) ||
         MRI->hasOneNonDBGUse(AddrReg[i]->getReg())))
      return false;
  }

  ++MBBI;

  DenseSet<unsigned> RegDefsToMove;
  DenseSet<unsigned> PhysRegUsesToMove;
  addDefsUsesToList(*CI.I, RegDefsToMove, PhysRegUsesToMove);

  for (; MBBI != E; ++MBBI) {
    const bool IsDS = (InstClass == DS_READ) || (InstClass == DS_WRITE);

    if ((getInstClass(MBBI->getOpcode()) != InstClass) ||
        (IsDS && (MBBI->getOpcode() != Opc))) {
      // This is not a matching DS instruction, but we can keep looking as
      // long as one of these conditions are met:
      // 1. It is safe to move I down past MBBI.
      // 2. It is safe to move MBBI down past the instruction that I will
      //    be merged into.

      if (MBBI->hasUnmodeledSideEffects()) {
        // We can't re-order this instruction with respect to other memory
        // operations, so we fail both conditions mentioned above.
        return false;
      }

      if (MBBI->mayLoadOrStore() &&
          (!memAccessesCanBeReordered(*CI.I, *MBBI, TII, AA) ||
           !canMoveInstsAcrossMemOp(*MBBI, CI.InstsToMove, TII, AA))) {
        // We fail condition #1, but we may still be able to satisfy condition
        // #2.  Add this instruction to the move list and then we will check
        // if condition #2 holds once we have selected the matching instruction.
        CI.InstsToMove.push_back(&*MBBI);
        addDefsUsesToList(*MBBI, RegDefsToMove, PhysRegUsesToMove);
        continue;
      }

      // When we match I with another DS instruction we will be moving I down
      // to the location of the matched instruction any uses of I will need to
      // be moved down as well.
      addToListsIfDependent(*MBBI, RegDefsToMove, PhysRegUsesToMove,
                            CI.InstsToMove);
      continue;
    }

    // Don't merge volatiles.
    if (MBBI->hasOrderedMemoryRef())
      return false;

    // Handle a case like
    //   DS_WRITE_B32 addr, v, idx0
    //   w = DS_READ_B32 addr, idx0
    //   DS_WRITE_B32 addr, f(w), idx1
    // where the DS_READ_B32 ends up in InstsToMove and therefore prevents
    // merging of the two writes.
    if (addToListsIfDependent(*MBBI, RegDefsToMove, PhysRegUsesToMove,
                              CI.InstsToMove))
      continue;

    bool Match = true;
    for (unsigned i = 0; i < NumAddresses; i++) {
      const MachineOperand &AddrRegNext = MBBI->getOperand(AddrIdx[i]);

      if (AddrReg[i]->isImm() || AddrRegNext.isImm()) {
        if (AddrReg[i]->isImm() != AddrRegNext.isImm() ||
            AddrReg[i]->getImm() != AddrRegNext.getImm()) {
          Match = false;
          break;
        }
        continue;
      }

      // Check same base pointer. Be careful of subregisters, which can occur
      // with vectors of pointers.
      if (AddrReg[i]->getReg() != AddrRegNext.getReg() ||
          AddrReg[i]->getSubReg() != AddrRegNext.getSubReg()) {
        Match = false;
        break;
      }
    }

    if (Match) {
      int OffsetIdx =
          AMDGPU::getNamedOperandIdx(CI.I->getOpcode(), AMDGPU::OpName::offset);
      CI.Offset0 = CI.I->getOperand(OffsetIdx).getImm();
      CI.Width0 = getOpcodeWidth(*CI.I);
      CI.Offset1 = MBBI->getOperand(OffsetIdx).getImm();
      CI.Width1 = getOpcodeWidth(*MBBI);
      CI.Paired = MBBI;

      if ((CI.InstClass == DS_READ) || (CI.InstClass == DS_WRITE)) {
        CI.Offset0 &= 0xffff;
        CI.Offset1 &= 0xffff;
      } else {
        CI.GLC0 = TII->getNamedOperand(*CI.I, AMDGPU::OpName::glc)->getImm();
        CI.GLC1 = TII->getNamedOperand(*MBBI, AMDGPU::OpName::glc)->getImm();
        if (CI.InstClass != S_BUFFER_LOAD_IMM) {
          CI.SLC0 = TII->getNamedOperand(*CI.I, AMDGPU::OpName::slc)->getImm();
          CI.SLC1 = TII->getNamedOperand(*MBBI, AMDGPU::OpName::slc)->getImm();
        }
      }

      // Check both offsets fit in the reduced range.
      // We also need to go through the list of instructions that we plan to
      // move and make sure they are all safe to move down past the merged
      // instruction.
      if (widthsFit(*STM, CI) && offsetsCanBeCombined(CI))
        if (canMoveInstsAcrossMemOp(*MBBI, CI.InstsToMove, TII, AA))
          return true;
    }

    // We've found a load/store that we couldn't merge for some reason.
    // We could potentially keep looking, but we'd need to make sure that
    // it was safe to move I and also all the instruction in InstsToMove
    // down past this instruction.
    // check if we can move I across MBBI and if we can move all I's users
    if (!memAccessesCanBeReordered(*CI.I, *MBBI, TII, AA) ||
        !canMoveInstsAcrossMemOp(*MBBI, CI.InstsToMove, TII, AA))
      break;
  }
  return false;
}

unsigned SILoadStoreOptimizer::read2Opcode(unsigned EltSize) const {
  if (STM->ldsRequiresM0Init())
    return (EltSize == 4) ? AMDGPU::DS_READ2_B32 : AMDGPU::DS_READ2_B64;
  return (EltSize == 4) ? AMDGPU::DS_READ2_B32_gfx9 : AMDGPU::DS_READ2_B64_gfx9;
}

unsigned SILoadStoreOptimizer::read2ST64Opcode(unsigned EltSize) const {
  if (STM->ldsRequiresM0Init())
    return (EltSize == 4) ? AMDGPU::DS_READ2ST64_B32 : AMDGPU::DS_READ2ST64_B64;

  return (EltSize == 4) ? AMDGPU::DS_READ2ST64_B32_gfx9
                        : AMDGPU::DS_READ2ST64_B64_gfx9;
}

MachineBasicBlock::iterator
SILoadStoreOptimizer::mergeRead2Pair(CombineInfo &CI) {
  MachineBasicBlock *MBB = CI.I->getParent();

  // Be careful, since the addresses could be subregisters themselves in weird
  // cases, like vectors of pointers.
  const auto *AddrReg = TII->getNamedOperand(*CI.I, AMDGPU::OpName::addr);

  const auto *Dest0 = TII->getNamedOperand(*CI.I, AMDGPU::OpName::vdst);
  const auto *Dest1 = TII->getNamedOperand(*CI.Paired, AMDGPU::OpName::vdst);

  unsigned NewOffset0 = CI.Offset0;
  unsigned NewOffset1 = CI.Offset1;
  unsigned Opc =
      CI.UseST64 ? read2ST64Opcode(CI.EltSize) : read2Opcode(CI.EltSize);

  unsigned SubRegIdx0 = (CI.EltSize == 4) ? AMDGPU::sub0 : AMDGPU::sub0_sub1;
  unsigned SubRegIdx1 = (CI.EltSize == 4) ? AMDGPU::sub1 : AMDGPU::sub2_sub3;

  if (NewOffset0 > NewOffset1) {
    // Canonicalize the merged instruction so the smaller offset comes first.
    std::swap(NewOffset0, NewOffset1);
    std::swap(SubRegIdx0, SubRegIdx1);
  }

  assert((isUInt<8>(NewOffset0) && isUInt<8>(NewOffset1)) &&
         (NewOffset0 != NewOffset1) && "Computed offset doesn't fit");

  const MCInstrDesc &Read2Desc = TII->get(Opc);

  const TargetRegisterClass *SuperRC =
      (CI.EltSize == 4) ? &AMDGPU::VReg_64RegClass : &AMDGPU::VReg_128RegClass;
  unsigned DestReg = MRI->createVirtualRegister(SuperRC);

  DebugLoc DL = CI.I->getDebugLoc();

  unsigned BaseReg = AddrReg->getReg();
  unsigned BaseSubReg = AddrReg->getSubReg();
  unsigned BaseRegFlags = 0;
  if (CI.BaseOff) {
    unsigned ImmReg = MRI->createVirtualRegister(&AMDGPU::SGPR_32RegClass);
    BuildMI(*MBB, CI.Paired, DL, TII->get(AMDGPU::S_MOV_B32), ImmReg)
        .addImm(CI.BaseOff);

    BaseReg = MRI->createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BaseRegFlags = RegState::Kill;

    TII->getAddNoCarry(*MBB, CI.Paired, DL, BaseReg)
        .addReg(ImmReg)
        .addReg(AddrReg->getReg(), 0, BaseSubReg);
    BaseSubReg = 0;
  }

  MachineInstrBuilder Read2 =
      BuildMI(*MBB, CI.Paired, DL, Read2Desc, DestReg)
          .addReg(BaseReg, BaseRegFlags, BaseSubReg) // addr
          .addImm(NewOffset0)                        // offset0
          .addImm(NewOffset1)                        // offset1
          .addImm(0)                                 // gds
          .cloneMergedMemRefs({&*CI.I, &*CI.Paired});

  (void)Read2;

  const MCInstrDesc &CopyDesc = TII->get(TargetOpcode::COPY);

  // Copy to the old destination registers.
  BuildMI(*MBB, CI.Paired, DL, CopyDesc)
      .add(*Dest0) // Copy to same destination including flags and sub reg.
      .addReg(DestReg, 0, SubRegIdx0);
  MachineInstr *Copy1 = BuildMI(*MBB, CI.Paired, DL, CopyDesc)
                            .add(*Dest1)
                            .addReg(DestReg, RegState::Kill, SubRegIdx1);

  moveInstsAfter(Copy1, CI.InstsToMove);

  MachineBasicBlock::iterator Next = std::next(CI.I);
  CI.I->eraseFromParent();
  CI.Paired->eraseFromParent();

  LLVM_DEBUG(dbgs() << "Inserted read2: " << *Read2 << '\n');
  return Next;
}

unsigned SILoadStoreOptimizer::write2Opcode(unsigned EltSize) const {
  if (STM->ldsRequiresM0Init())
    return (EltSize == 4) ? AMDGPU::DS_WRITE2_B32 : AMDGPU::DS_WRITE2_B64;
  return (EltSize == 4) ? AMDGPU::DS_WRITE2_B32_gfx9
                        : AMDGPU::DS_WRITE2_B64_gfx9;
}

unsigned SILoadStoreOptimizer::write2ST64Opcode(unsigned EltSize) const {
  if (STM->ldsRequiresM0Init())
    return (EltSize == 4) ? AMDGPU::DS_WRITE2ST64_B32
                          : AMDGPU::DS_WRITE2ST64_B64;

  return (EltSize == 4) ? AMDGPU::DS_WRITE2ST64_B32_gfx9
                        : AMDGPU::DS_WRITE2ST64_B64_gfx9;
}

MachineBasicBlock::iterator
SILoadStoreOptimizer::mergeWrite2Pair(CombineInfo &CI) {
  MachineBasicBlock *MBB = CI.I->getParent();

  // Be sure to use .addOperand(), and not .addReg() with these. We want to be
  // sure we preserve the subregister index and any register flags set on them.
  const MachineOperand *AddrReg =
      TII->getNamedOperand(*CI.I, AMDGPU::OpName::addr);
  const MachineOperand *Data0 =
      TII->getNamedOperand(*CI.I, AMDGPU::OpName::data0);
  const MachineOperand *Data1 =
      TII->getNamedOperand(*CI.Paired, AMDGPU::OpName::data0);

  unsigned NewOffset0 = CI.Offset0;
  unsigned NewOffset1 = CI.Offset1;
  unsigned Opc =
      CI.UseST64 ? write2ST64Opcode(CI.EltSize) : write2Opcode(CI.EltSize);

  if (NewOffset0 > NewOffset1) {
    // Canonicalize the merged instruction so the smaller offset comes first.
    std::swap(NewOffset0, NewOffset1);
    std::swap(Data0, Data1);
  }

  assert((isUInt<8>(NewOffset0) && isUInt<8>(NewOffset1)) &&
         (NewOffset0 != NewOffset1) && "Computed offset doesn't fit");

  const MCInstrDesc &Write2Desc = TII->get(Opc);
  DebugLoc DL = CI.I->getDebugLoc();

  unsigned BaseReg = AddrReg->getReg();
  unsigned BaseSubReg = AddrReg->getSubReg();
  unsigned BaseRegFlags = 0;
  if (CI.BaseOff) {
    unsigned ImmReg = MRI->createVirtualRegister(&AMDGPU::SGPR_32RegClass);
    BuildMI(*MBB, CI.Paired, DL, TII->get(AMDGPU::S_MOV_B32), ImmReg)
        .addImm(CI.BaseOff);

    BaseReg = MRI->createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BaseRegFlags = RegState::Kill;

    TII->getAddNoCarry(*MBB, CI.Paired, DL, BaseReg)
        .addReg(ImmReg)
        .addReg(AddrReg->getReg(), 0, BaseSubReg);
    BaseSubReg = 0;
  }

  MachineInstrBuilder Write2 =
      BuildMI(*MBB, CI.Paired, DL, Write2Desc)
          .addReg(BaseReg, BaseRegFlags, BaseSubReg) // addr
          .add(*Data0)                               // data0
          .add(*Data1)                               // data1
          .addImm(NewOffset0)                        // offset0
          .addImm(NewOffset1)                        // offset1
          .addImm(0)                                 // gds
          .cloneMergedMemRefs({&*CI.I, &*CI.Paired});

  moveInstsAfter(Write2, CI.InstsToMove);

  MachineBasicBlock::iterator Next = std::next(CI.I);
  CI.I->eraseFromParent();
  CI.Paired->eraseFromParent();

  LLVM_DEBUG(dbgs() << "Inserted write2 inst: " << *Write2 << '\n');
  return Next;
}

MachineBasicBlock::iterator
SILoadStoreOptimizer::mergeSBufferLoadImmPair(CombineInfo &CI) {
  MachineBasicBlock *MBB = CI.I->getParent();
  DebugLoc DL = CI.I->getDebugLoc();
  const unsigned Opcode = getNewOpcode(CI);

  const TargetRegisterClass *SuperRC = getTargetRegisterClass(CI);

  unsigned DestReg = MRI->createVirtualRegister(SuperRC);
  unsigned MergedOffset = std::min(CI.Offset0, CI.Offset1);

  BuildMI(*MBB, CI.Paired, DL, TII->get(Opcode), DestReg)
      .add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::sbase))
      .addImm(MergedOffset) // offset
      .addImm(CI.GLC0)      // glc
      .cloneMergedMemRefs({&*CI.I, &*CI.Paired});

  std::pair<unsigned, unsigned> SubRegIdx = getSubRegIdxs(CI);
  const unsigned SubRegIdx0 = std::get<0>(SubRegIdx);
  const unsigned SubRegIdx1 = std::get<1>(SubRegIdx);

  // Copy to the old destination registers.
  const MCInstrDesc &CopyDesc = TII->get(TargetOpcode::COPY);
  const auto *Dest0 = TII->getNamedOperand(*CI.I, AMDGPU::OpName::sdst);
  const auto *Dest1 = TII->getNamedOperand(*CI.Paired, AMDGPU::OpName::sdst);

  BuildMI(*MBB, CI.Paired, DL, CopyDesc)
      .add(*Dest0) // Copy to same destination including flags and sub reg.
      .addReg(DestReg, 0, SubRegIdx0);
  MachineInstr *Copy1 = BuildMI(*MBB, CI.Paired, DL, CopyDesc)
                            .add(*Dest1)
                            .addReg(DestReg, RegState::Kill, SubRegIdx1);

  moveInstsAfter(Copy1, CI.InstsToMove);

  MachineBasicBlock::iterator Next = std::next(CI.I);
  CI.I->eraseFromParent();
  CI.Paired->eraseFromParent();
  return Next;
}

MachineBasicBlock::iterator
SILoadStoreOptimizer::mergeBufferLoadPair(CombineInfo &CI) {
  MachineBasicBlock *MBB = CI.I->getParent();
  DebugLoc DL = CI.I->getDebugLoc();

  const unsigned Opcode = getNewOpcode(CI);

  const TargetRegisterClass *SuperRC = getTargetRegisterClass(CI);

  // Copy to the new source register.
  unsigned DestReg = MRI->createVirtualRegister(SuperRC);
  unsigned MergedOffset = std::min(CI.Offset0, CI.Offset1);

  auto MIB = BuildMI(*MBB, CI.Paired, DL, TII->get(Opcode), DestReg);

  const unsigned Regs = getRegs(Opcode);

  if (Regs & VADDR)
    MIB.add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::vaddr));

  MIB.add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::srsrc))
      .add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::soffset))
      .addImm(MergedOffset) // offset
      .addImm(CI.GLC0)      // glc
      .addImm(CI.SLC0)      // slc
      .addImm(0)            // tfe
      .cloneMergedMemRefs({&*CI.I, &*CI.Paired});

  std::pair<unsigned, unsigned> SubRegIdx = getSubRegIdxs(CI);
  const unsigned SubRegIdx0 = std::get<0>(SubRegIdx);
  const unsigned SubRegIdx1 = std::get<1>(SubRegIdx);

  // Copy to the old destination registers.
  const MCInstrDesc &CopyDesc = TII->get(TargetOpcode::COPY);
  const auto *Dest0 = TII->getNamedOperand(*CI.I, AMDGPU::OpName::vdata);
  const auto *Dest1 = TII->getNamedOperand(*CI.Paired, AMDGPU::OpName::vdata);

  BuildMI(*MBB, CI.Paired, DL, CopyDesc)
      .add(*Dest0) // Copy to same destination including flags and sub reg.
      .addReg(DestReg, 0, SubRegIdx0);
  MachineInstr *Copy1 = BuildMI(*MBB, CI.Paired, DL, CopyDesc)
                            .add(*Dest1)
                            .addReg(DestReg, RegState::Kill, SubRegIdx1);

  moveInstsAfter(Copy1, CI.InstsToMove);

  MachineBasicBlock::iterator Next = std::next(CI.I);
  CI.I->eraseFromParent();
  CI.Paired->eraseFromParent();
  return Next;
}

unsigned SILoadStoreOptimizer::getNewOpcode(const CombineInfo &CI) {
  const unsigned Width = CI.Width0 + CI.Width1;

  switch (CI.InstClass) {
  default:
    return AMDGPU::getMUBUFOpcode(CI.InstClass, Width);
  case UNKNOWN:
    llvm_unreachable("Unknown instruction class");
  case S_BUFFER_LOAD_IMM:
    switch (Width) {
    default:
      return 0;
    case 2:
      return AMDGPU::S_BUFFER_LOAD_DWORDX2_IMM;
    case 4:
      return AMDGPU::S_BUFFER_LOAD_DWORDX4_IMM;
    }
  }
}

std::pair<unsigned, unsigned>
SILoadStoreOptimizer::getSubRegIdxs(const CombineInfo &CI) {
  if (CI.Offset0 > CI.Offset1) {
    switch (CI.Width0) {
    default:
      return std::make_pair(0, 0);
    case 1:
      switch (CI.Width1) {
      default:
        return std::make_pair(0, 0);
      case 1:
        return std::make_pair(AMDGPU::sub1, AMDGPU::sub0);
      case 2:
        return std::make_pair(AMDGPU::sub2, AMDGPU::sub0_sub1);
      case 3:
        return std::make_pair(AMDGPU::sub3, AMDGPU::sub0_sub1_sub2);
      }
    case 2:
      switch (CI.Width1) {
      default:
        return std::make_pair(0, 0);
      case 1:
        return std::make_pair(AMDGPU::sub1_sub2, AMDGPU::sub0);
      case 2:
        return std::make_pair(AMDGPU::sub2_sub3, AMDGPU::sub0_sub1);
      }
    case 3:
      switch (CI.Width1) {
      default:
        return std::make_pair(0, 0);
      case 1:
        return std::make_pair(AMDGPU::sub1_sub2_sub3, AMDGPU::sub0);
      }
    }
  } else {
    switch (CI.Width0) {
    default:
      return std::make_pair(0, 0);
    case 1:
      switch (CI.Width1) {
      default:
        return std::make_pair(0, 0);
      case 1:
        return std::make_pair(AMDGPU::sub0, AMDGPU::sub1);
      case 2:
        return std::make_pair(AMDGPU::sub0, AMDGPU::sub1_sub2);
      case 3:
        return std::make_pair(AMDGPU::sub0, AMDGPU::sub1_sub2_sub3);
      }
    case 2:
      switch (CI.Width1) {
      default:
        return std::make_pair(0, 0);
      case 1:
        return std::make_pair(AMDGPU::sub0_sub1, AMDGPU::sub2);
      case 2:
        return std::make_pair(AMDGPU::sub0_sub1, AMDGPU::sub2_sub3);
      }
    case 3:
      switch (CI.Width1) {
      default:
        return std::make_pair(0, 0);
      case 1:
        return std::make_pair(AMDGPU::sub0_sub1_sub2, AMDGPU::sub3);
      }
    }
  }
}

const TargetRegisterClass *
SILoadStoreOptimizer::getTargetRegisterClass(const CombineInfo &CI) {
  if (CI.InstClass == S_BUFFER_LOAD_IMM) {
    switch (CI.Width0 + CI.Width1) {
    default:
      return nullptr;
    case 2:
      return &AMDGPU::SReg_64_XEXECRegClass;
    case 4:
      return &AMDGPU::SReg_128RegClass;
    case 8:
      return &AMDGPU::SReg_256RegClass;
    case 16:
      return &AMDGPU::SReg_512RegClass;
    }
  } else {
    switch (CI.Width0 + CI.Width1) {
    default:
      return nullptr;
    case 2:
      return &AMDGPU::VReg_64RegClass;
    case 3:
      return &AMDGPU::VReg_96RegClass;
    case 4:
      return &AMDGPU::VReg_128RegClass;
    }
  }
}

MachineBasicBlock::iterator
SILoadStoreOptimizer::mergeBufferStorePair(CombineInfo &CI) {
  MachineBasicBlock *MBB = CI.I->getParent();
  DebugLoc DL = CI.I->getDebugLoc();

  const unsigned Opcode = getNewOpcode(CI);

  std::pair<unsigned, unsigned> SubRegIdx = getSubRegIdxs(CI);
  const unsigned SubRegIdx0 = std::get<0>(SubRegIdx);
  const unsigned SubRegIdx1 = std::get<1>(SubRegIdx);

  // Copy to the new source register.
  const TargetRegisterClass *SuperRC = getTargetRegisterClass(CI);
  unsigned SrcReg = MRI->createVirtualRegister(SuperRC);

  const auto *Src0 = TII->getNamedOperand(*CI.I, AMDGPU::OpName::vdata);
  const auto *Src1 = TII->getNamedOperand(*CI.Paired, AMDGPU::OpName::vdata);

  BuildMI(*MBB, CI.Paired, DL, TII->get(AMDGPU::REG_SEQUENCE), SrcReg)
      .add(*Src0)
      .addImm(SubRegIdx0)
      .add(*Src1)
      .addImm(SubRegIdx1);

  auto MIB = BuildMI(*MBB, CI.Paired, DL, TII->get(Opcode))
                 .addReg(SrcReg, RegState::Kill);

  const unsigned Regs = getRegs(Opcode);

  if (Regs & VADDR)
    MIB.add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::vaddr));

  MIB.add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::srsrc))
      .add(*TII->getNamedOperand(*CI.I, AMDGPU::OpName::soffset))
      .addImm(std::min(CI.Offset0, CI.Offset1)) // offset
      .addImm(CI.GLC0)                          // glc
      .addImm(CI.SLC0)                          // slc
      .addImm(0)                                // tfe
      .cloneMergedMemRefs({&*CI.I, &*CI.Paired});

  moveInstsAfter(MIB, CI.InstsToMove);

  MachineBasicBlock::iterator Next = std::next(CI.I);
  CI.I->eraseFromParent();
  CI.Paired->eraseFromParent();
  return Next;
}

MachineOperand
SILoadStoreOptimizer::createRegOrImm(int32_t Val, MachineInstr &MI) {
  APInt V(32, Val, true);
  if (TII->isInlineConstant(V))
    return MachineOperand::CreateImm(Val);

  unsigned Reg = MRI->createVirtualRegister(&AMDGPU::SReg_32RegClass);
  MachineInstr *Mov =
  BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
          TII->get(AMDGPU::S_MOV_B32), Reg)
    .addImm(Val);
  (void)Mov;
  LLVM_DEBUG(dbgs() << "    "; Mov->dump());
  return MachineOperand::CreateReg(Reg, false);
}

// Compute base address using Addr and return the final register.
unsigned SILoadStoreOptimizer::computeBase(MachineInstr &MI,
                                           const MemAddress &Addr) {
  MachineBasicBlock *MBB = MI.getParent();
  MachineBasicBlock::iterator MBBI = MI.getIterator();
  DebugLoc DL = MI.getDebugLoc();

  assert((TRI->getRegSizeInBits(Addr.Base.LoReg, *MRI) == 32 ||
          Addr.Base.LoSubReg) &&
         "Expected 32-bit Base-Register-Low!!");

  assert((TRI->getRegSizeInBits(Addr.Base.HiReg, *MRI) == 32 ||
          Addr.Base.HiSubReg) &&
         "Expected 32-bit Base-Register-Hi!!");

  LLVM_DEBUG(dbgs() << "  Re-Computed Anchor-Base:\n");
  MachineOperand OffsetLo = createRegOrImm(static_cast<int32_t>(Addr.Offset), MI);
  MachineOperand OffsetHi =
    createRegOrImm(static_cast<int32_t>(Addr.Offset >> 32), MI);
  unsigned CarryReg = MRI->createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);
  unsigned DeadCarryReg =
    MRI->createVirtualRegister(&AMDGPU::SReg_64_XEXECRegClass);

  unsigned DestSub0 = MRI->createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  unsigned DestSub1 = MRI->createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  MachineInstr *LoHalf =
    BuildMI(*MBB, MBBI, DL, TII->get(AMDGPU::V_ADD_I32_e64), DestSub0)
      .addReg(CarryReg, RegState::Define)
      .addReg(Addr.Base.LoReg, 0, Addr.Base.LoSubReg)
    .add(OffsetLo);
  (void)LoHalf;
  LLVM_DEBUG(dbgs() << "    "; LoHalf->dump(););

  MachineInstr *HiHalf =
  BuildMI(*MBB, MBBI, DL, TII->get(AMDGPU::V_ADDC_U32_e64), DestSub1)
    .addReg(DeadCarryReg, RegState::Define | RegState::Dead)
    .addReg(Addr.Base.HiReg, 0, Addr.Base.HiSubReg)
    .add(OffsetHi)
    .addReg(CarryReg, RegState::Kill);
  (void)HiHalf;
  LLVM_DEBUG(dbgs() << "    "; HiHalf->dump(););

  unsigned FullDestReg = MRI->createVirtualRegister(&AMDGPU::VReg_64RegClass);
  MachineInstr *FullBase =
    BuildMI(*MBB, MBBI, DL, TII->get(TargetOpcode::REG_SEQUENCE), FullDestReg)
      .addReg(DestSub0)
      .addImm(AMDGPU::sub0)
      .addReg(DestSub1)
      .addImm(AMDGPU::sub1);
  (void)FullBase;
  LLVM_DEBUG(dbgs() << "    "; FullBase->dump(); dbgs() << "\n";);

  return FullDestReg;
}

// Update base and offset with the NewBase and NewOffset in MI.
void SILoadStoreOptimizer::updateBaseAndOffset(MachineInstr &MI,
                                               unsigned NewBase,
                                               int32_t NewOffset) {
  TII->getNamedOperand(MI, AMDGPU::OpName::vaddr)->setReg(NewBase);
  TII->getNamedOperand(MI, AMDGPU::OpName::offset)->setImm(NewOffset);
}

Optional<int32_t>
SILoadStoreOptimizer::extractConstOffset(const MachineOperand &Op) {
  if (Op.isImm())
    return Op.getImm();

  if (!Op.isReg())
    return None;

  MachineInstr *Def = MRI->getUniqueVRegDef(Op.getReg());
  if (!Def || Def->getOpcode() != AMDGPU::S_MOV_B32 ||
      !Def->getOperand(1).isImm())
    return None;

  return Def->getOperand(1).getImm();
}

// Analyze Base and extracts:
//  - 32bit base registers, subregisters
//  - 64bit constant offset
// Expecting base computation as:
//   %OFFSET0:sgpr_32 = S_MOV_B32 8000
//   %LO:vgpr_32, %c:sreg_64_xexec =
//       V_ADD_I32_e64 %BASE_LO:vgpr_32, %103:sgpr_32,
//   %HI:vgpr_32, = V_ADDC_U32_e64 %BASE_HI:vgpr_32, 0, killed %c:sreg_64_xexec
//   %Base:vreg_64 =
//       REG_SEQUENCE %LO:vgpr_32, %subreg.sub0, %HI:vgpr_32, %subreg.sub1
void SILoadStoreOptimizer::processBaseWithConstOffset(const MachineOperand &Base,
                                                      MemAddress &Addr) {
  if (!Base.isReg())
    return;

  MachineInstr *Def = MRI->getUniqueVRegDef(Base.getReg());
  if (!Def || Def->getOpcode() != AMDGPU::REG_SEQUENCE
      || Def->getNumOperands() != 5)
    return;

  MachineOperand BaseLo = Def->getOperand(1);
  MachineOperand BaseHi = Def->getOperand(3);
  if (!BaseLo.isReg() || !BaseHi.isReg())
    return;

  MachineInstr *BaseLoDef = MRI->getUniqueVRegDef(BaseLo.getReg());
  MachineInstr *BaseHiDef = MRI->getUniqueVRegDef(BaseHi.getReg());

  if (!BaseLoDef || BaseLoDef->getOpcode() != AMDGPU::V_ADD_I32_e64 ||
      !BaseHiDef || BaseHiDef->getOpcode() != AMDGPU::V_ADDC_U32_e64)
    return;

  const auto *Src0 = TII->getNamedOperand(*BaseLoDef, AMDGPU::OpName::src0);
  const auto *Src1 = TII->getNamedOperand(*BaseLoDef, AMDGPU::OpName::src1);

  auto Offset0P = extractConstOffset(*Src0);
  if (Offset0P)
    BaseLo = *Src1;
  else {
    if (!(Offset0P = extractConstOffset(*Src1)))
      return;
    BaseLo = *Src0;
  }

  Src0 = TII->getNamedOperand(*BaseHiDef, AMDGPU::OpName::src0);
  Src1 = TII->getNamedOperand(*BaseHiDef, AMDGPU::OpName::src1);

  if (Src0->isImm())
    std::swap(Src0, Src1);

  if (!Src1->isImm())
    return;

  uint64_t Offset1 = Src1->getImm();
  BaseHi = *Src0;

  Addr.Base.LoReg = BaseLo.getReg();
  Addr.Base.HiReg = BaseHi.getReg();
  Addr.Base.LoSubReg = BaseLo.getSubReg();
  Addr.Base.HiSubReg = BaseHi.getSubReg();
  Addr.Offset = (*Offset0P & 0x00000000ffffffff) | (Offset1 << 32);
}

bool SILoadStoreOptimizer::promoteConstantOffsetToImm(
    MachineInstr &MI,
    MemInfoMap &Visited,
    SmallPtrSet<MachineInstr *, 4> &AnchorList) {

  // TODO: Support flat and scratch.
  if (AMDGPU::getGlobalSaddrOp(MI.getOpcode()) < 0 ||
      TII->getNamedOperand(MI, AMDGPU::OpName::vdata) != NULL)
    return false;

  // TODO: Support Store.
  if (!MI.mayLoad())
    return false;

  if (AnchorList.count(&MI))
    return false;

  LLVM_DEBUG(dbgs() << "\nTryToPromoteConstantOffsetToImmFor "; MI.dump());

  if (TII->getNamedOperand(MI, AMDGPU::OpName::offset)->getImm()) {
    LLVM_DEBUG(dbgs() << "  Const-offset is already promoted.\n";);
    return false;
  }

  // Step1: Find the base-registers and a 64bit constant offset.
  MachineOperand &Base = *TII->getNamedOperand(MI, AMDGPU::OpName::vaddr);
  MemAddress MAddr;
  if (Visited.find(&MI) == Visited.end()) {
    processBaseWithConstOffset(Base, MAddr);
    Visited[&MI] = MAddr;
  } else
    MAddr = Visited[&MI];

  if (MAddr.Offset == 0) {
    LLVM_DEBUG(dbgs() << "  Failed to extract constant-offset or there are no"
                         " constant offsets that can be promoted.\n";);
    return false;
  }

  LLVM_DEBUG(dbgs() << "  BASE: {" << MAddr.Base.HiReg << ", "
             << MAddr.Base.LoReg << "} Offset: " << MAddr.Offset << "\n\n";);

  // Step2: Traverse through MI's basic block and find an anchor(that has the
  // same base-registers) with the highest 13bit distance from MI's offset.
  // E.g. (64bit loads)
  // bb:
  //   addr1 = &a + 4096;   load1 = load(addr1,  0)
  //   addr2 = &a + 6144;   load2 = load(addr2,  0)
  //   addr3 = &a + 8192;   load3 = load(addr3,  0)
  //   addr4 = &a + 10240;  load4 = load(addr4,  0)
  //   addr5 = &a + 12288;  load5 = load(addr5,  0)
  //
  // Starting from the first load, the optimization will try to find a new base
  // from which (&a + 4096) has 13 bit distance. Both &a + 6144 and &a + 8192
  // has 13bit distance from &a + 4096. The heuristic considers &a + 8192
  // as the new-base(anchor) because of the maximum distance which can
  // accomodate more intermediate bases presumeably.
  //
  // Step3: move (&a + 8192) above load1. Compute and promote offsets from
  // (&a + 8192) for load1, load2, load4.
  //   addr = &a + 8192
  //   load1 = load(addr,       -4096)
  //   load2 = load(addr,       -2048)
  //   load3 = load(addr,       0)
  //   load4 = load(addr,       2048)
  //   addr5 = &a + 12288;  load5 = load(addr5,  0)
  //
  MachineInstr *AnchorInst = nullptr;
  MemAddress AnchorAddr;
  uint32_t MaxDist = std::numeric_limits<uint32_t>::min();
  SmallVector<std::pair<MachineInstr *, int64_t>, 4> InstsWCommonBase;

  MachineBasicBlock *MBB = MI.getParent();
  MachineBasicBlock::iterator E = MBB->end();
  MachineBasicBlock::iterator MBBI = MI.getIterator();
  ++MBBI;
  const SITargetLowering *TLI =
    static_cast<const SITargetLowering *>(STM->getTargetLowering());

  for ( ; MBBI != E; ++MBBI) {
    MachineInstr &MINext = *MBBI;
    // TODO: Support finding an anchor(with same base) from store addresses or
    // any other load addresses where the opcodes are different.
    if (MINext.getOpcode() != MI.getOpcode() ||
        TII->getNamedOperand(MINext, AMDGPU::OpName::offset)->getImm())
      continue;

    const MachineOperand &BaseNext =
      *TII->getNamedOperand(MINext, AMDGPU::OpName::vaddr);
    MemAddress MAddrNext;
    if (Visited.find(&MINext) == Visited.end()) {
      processBaseWithConstOffset(BaseNext, MAddrNext);
      Visited[&MINext] = MAddrNext;
    } else
      MAddrNext = Visited[&MINext];

    if (MAddrNext.Base.LoReg != MAddr.Base.LoReg ||
        MAddrNext.Base.HiReg != MAddr.Base.HiReg ||
        MAddrNext.Base.LoSubReg != MAddr.Base.LoSubReg ||
        MAddrNext.Base.HiSubReg != MAddr.Base.HiSubReg)
      continue;

    InstsWCommonBase.push_back(std::make_pair(&MINext, MAddrNext.Offset));

    int64_t Dist = MAddr.Offset - MAddrNext.Offset;
    TargetLoweringBase::AddrMode AM;
    AM.HasBaseReg = true;
    AM.BaseOffs = Dist;
    if (TLI->isLegalGlobalAddressingMode(AM) &&
        (uint32_t)std::abs(Dist) > MaxDist) {
      MaxDist = std::abs(Dist);

      AnchorAddr = MAddrNext;
      AnchorInst = &MINext;
    }
  }

  if (AnchorInst) {
    LLVM_DEBUG(dbgs() << "  Anchor-Inst(with max-distance from Offset): ";
               AnchorInst->dump());
    LLVM_DEBUG(dbgs() << "  Anchor-Offset from BASE: "
               <<  AnchorAddr.Offset << "\n\n");

    // Instead of moving up, just re-compute anchor-instruction's base address.
    unsigned Base = computeBase(MI, AnchorAddr);

    updateBaseAndOffset(MI, Base, MAddr.Offset - AnchorAddr.Offset);
    LLVM_DEBUG(dbgs() << "  After promotion: "; MI.dump(););

    for (auto P : InstsWCommonBase) {
      TargetLoweringBase::AddrMode AM;
      AM.HasBaseReg = true;
      AM.BaseOffs = P.second - AnchorAddr.Offset;

      if (TLI->isLegalGlobalAddressingMode(AM)) {
        LLVM_DEBUG(dbgs() << "  Promote Offset(" << P.second;
                   dbgs() << ")"; P.first->dump());
        updateBaseAndOffset(*P.first, Base, P.second - AnchorAddr.Offset);
        LLVM_DEBUG(dbgs() << "     After promotion: "; P.first->dump());
      }
    }
    AnchorList.insert(AnchorInst);
    return true;
  }

  return false;
}

// Scan through looking for adjacent LDS operations with constant offsets from
// the same base register. We rely on the scheduler to do the hard work of
// clustering nearby loads, and assume these are all adjacent.
bool SILoadStoreOptimizer::optimizeBlock(MachineBasicBlock &MBB) {
  bool Modified = false;

  // Contain the list
  MemInfoMap Visited;
  // Contains the list of instructions for which constant offsets are being
  // promoted to the IMM.
  SmallPtrSet<MachineInstr *, 4> AnchorList;

  for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
    MachineInstr &MI = *I;

    if (promoteConstantOffsetToImm(MI, Visited, AnchorList))
      Modified = true;

    // Don't combine if volatile.
    if (MI.hasOrderedMemoryRef()) {
      ++I;
      continue;
    }

    const unsigned Opc = MI.getOpcode();

    CombineInfo CI;
    CI.I = I;
    CI.InstClass = getInstClass(Opc);

    switch (CI.InstClass) {
    default:
      break;
    case DS_READ:
      CI.EltSize =
          (Opc == AMDGPU::DS_READ_B64 || Opc == AMDGPU::DS_READ_B64_gfx9) ? 8
                                                                          : 4;
      if (findMatchingInst(CI)) {
        Modified = true;
        I = mergeRead2Pair(CI);
      } else {
        ++I;
      }
      continue;
    case DS_WRITE:
      CI.EltSize =
          (Opc == AMDGPU::DS_WRITE_B64 || Opc == AMDGPU::DS_WRITE_B64_gfx9) ? 8
                                                                            : 4;
      if (findMatchingInst(CI)) {
        Modified = true;
        I = mergeWrite2Pair(CI);
      } else {
        ++I;
      }
      continue;
    case S_BUFFER_LOAD_IMM:
      CI.EltSize = AMDGPU::getSMRDEncodedOffset(*STM, 4);
      if (findMatchingInst(CI)) {
        Modified = true;
        I = mergeSBufferLoadImmPair(CI);
        OptimizeAgain |= (CI.Width0 + CI.Width1) < 16;
      } else {
        ++I;
      }
      continue;
    case BUFFER_LOAD_OFFEN:
    case BUFFER_LOAD_OFFSET:
    case BUFFER_LOAD_OFFEN_exact:
    case BUFFER_LOAD_OFFSET_exact:
      CI.EltSize = 4;
      if (findMatchingInst(CI)) {
        Modified = true;
        I = mergeBufferLoadPair(CI);
        OptimizeAgain |= (CI.Width0 + CI.Width1) < 4;
      } else {
        ++I;
      }
      continue;
    case BUFFER_STORE_OFFEN:
    case BUFFER_STORE_OFFSET:
    case BUFFER_STORE_OFFEN_exact:
    case BUFFER_STORE_OFFSET_exact:
      CI.EltSize = 4;
      if (findMatchingInst(CI)) {
        Modified = true;
        I = mergeBufferStorePair(CI);
        OptimizeAgain |= (CI.Width0 + CI.Width1) < 4;
      } else {
        ++I;
      }
      continue;
    }

    ++I;
  }

  return Modified;
}

bool SILoadStoreOptimizer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  STM = &MF.getSubtarget<GCNSubtarget>();
  if (!STM->loadStoreOptEnabled())
    return false;

  TII = STM->getInstrInfo();
  TRI = &TII->getRegisterInfo();

  MRI = &MF.getRegInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  assert(MRI->isSSA() && "Must be run on SSA");

  LLVM_DEBUG(dbgs() << "Running SILoadStoreOptimizer\n");

  bool Modified = false;

  for (MachineBasicBlock &MBB : MF) {
    do {
      OptimizeAgain = false;
      Modified |= optimizeBlock(MBB);
    } while (OptimizeAgain);
  }

  return Modified;
}
