//===- AArch64LoadStoreOptimizer.cpp - AArch64 load/store opt. pass -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that performs load / store related peephole
// optimizations. This pass should be run after register allocation.
//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>

using namespace llvm;

#define DEBUG_TYPE "aarch64-ldst-opt"

STATISTIC(NumPairCreated, "Number of load/store pair instructions generated");
STATISTIC(NumPostFolded, "Number of post-index updates folded");
STATISTIC(NumPreFolded, "Number of pre-index updates folded");
STATISTIC(NumUnscaledPairCreated,
          "Number of load/store from unscaled generated");
STATISTIC(NumZeroStoresPromoted, "Number of narrow zero stores promoted");
STATISTIC(NumLoadsFromStoresPromoted, "Number of loads from stores promoted");

// The LdStLimit limits how far we search for load/store pairs.
static cl::opt<unsigned> LdStLimit("aarch64-load-store-scan-limit",
                                   cl::init(20), cl::Hidden);

// The UpdateLimit limits how far we search for update instructions when we form
// pre-/post-index instructions.
static cl::opt<unsigned> UpdateLimit("aarch64-update-scan-limit", cl::init(100),
                                     cl::Hidden);

#define AARCH64_LOAD_STORE_OPT_NAME "AArch64 load / store optimization pass"

namespace {

using LdStPairFlags = struct LdStPairFlags {
  // If a matching instruction is found, MergeForward is set to true if the
  // merge is to remove the first instruction and replace the second with
  // a pair-wise insn, and false if the reverse is true.
  bool MergeForward = false;

  // SExtIdx gives the index of the result of the load pair that must be
  // extended. The value of SExtIdx assumes that the paired load produces the
  // value in this order: (I, returned iterator), i.e., -1 means no value has
  // to be extended, 0 means I, and 1 means the returned iterator.
  int SExtIdx = -1;

  LdStPairFlags() = default;

  void setMergeForward(bool V = true) { MergeForward = V; }
  bool getMergeForward() const { return MergeForward; }

  void setSExtIdx(int V) { SExtIdx = V; }
  int getSExtIdx() const { return SExtIdx; }
};

struct AArch64LoadStoreOpt : public MachineFunctionPass {
  static char ID;

  AArch64LoadStoreOpt() : MachineFunctionPass(ID) {
    initializeAArch64LoadStoreOptPass(*PassRegistry::getPassRegistry());
  }

  AliasAnalysis *AA;
  const AArch64InstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const AArch64Subtarget *Subtarget;

  // Track which register units have been modified and used.
  LiveRegUnits ModifiedRegUnits, UsedRegUnits;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AAResultsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  // Scan the instructions looking for a load/store that can be combined
  // with the current instruction into a load/store pair.
  // Return the matching instruction if one is found, else MBB->end().
  MachineBasicBlock::iterator findMatchingInsn(MachineBasicBlock::iterator I,
                                               LdStPairFlags &Flags,
                                               unsigned Limit,
                                               bool FindNarrowMerge);

  // Scan the instructions looking for a store that writes to the address from
  // which the current load instruction reads. Return true if one is found.
  bool findMatchingStore(MachineBasicBlock::iterator I, unsigned Limit,
                         MachineBasicBlock::iterator &StoreI);

  // Merge the two instructions indicated into a wider narrow store instruction.
  MachineBasicBlock::iterator
  mergeNarrowZeroStores(MachineBasicBlock::iterator I,
                        MachineBasicBlock::iterator MergeMI,
                        const LdStPairFlags &Flags);

  // Merge the two instructions indicated into a single pair-wise instruction.
  MachineBasicBlock::iterator
  mergePairedInsns(MachineBasicBlock::iterator I,
                   MachineBasicBlock::iterator Paired,
                   const LdStPairFlags &Flags);

  // Promote the load that reads directly from the address stored to.
  MachineBasicBlock::iterator
  promoteLoadFromStore(MachineBasicBlock::iterator LoadI,
                       MachineBasicBlock::iterator StoreI);

  // Scan the instruction list to find a base register update that can
  // be combined with the current instruction (a load or store) using
  // pre or post indexed addressing with writeback. Scan forwards.
  MachineBasicBlock::iterator
  findMatchingUpdateInsnForward(MachineBasicBlock::iterator I,
                                int UnscaledOffset, unsigned Limit);

  // Scan the instruction list to find a base register update that can
  // be combined with the current instruction (a load or store) using
  // pre or post indexed addressing with writeback. Scan backwards.
  MachineBasicBlock::iterator
  findMatchingUpdateInsnBackward(MachineBasicBlock::iterator I, unsigned Limit);

  // Find an instruction that updates the base register of the ld/st
  // instruction.
  bool isMatchingUpdateInsn(MachineInstr &MemMI, MachineInstr &MI,
                            unsigned BaseReg, int Offset);

  // Merge a pre- or post-index base register update into a ld/st instruction.
  MachineBasicBlock::iterator
  mergeUpdateInsn(MachineBasicBlock::iterator I,
                  MachineBasicBlock::iterator Update, bool IsPreIdx);

  // Find and merge zero store instructions.
  bool tryToMergeZeroStInst(MachineBasicBlock::iterator &MBBI);

  // Find and pair ldr/str instructions.
  bool tryToPairLdStInst(MachineBasicBlock::iterator &MBBI);

  // Find and promote load instructions which read directly from store.
  bool tryToPromoteLoadFromStore(MachineBasicBlock::iterator &MBBI);

  // Find and merge a base register updates before or after a ld/st instruction.
  bool tryToMergeLdStUpdate(MachineBasicBlock::iterator &MBBI);

  bool optimizeBlock(MachineBasicBlock &MBB, bool EnableNarrowZeroStOpt);

  bool runOnMachineFunction(MachineFunction &Fn) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override { return AARCH64_LOAD_STORE_OPT_NAME; }
};

char AArch64LoadStoreOpt::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(AArch64LoadStoreOpt, "aarch64-ldst-opt",
                AARCH64_LOAD_STORE_OPT_NAME, false, false)

static bool isNarrowStore(unsigned Opc) {
  switch (Opc) {
  default:
    return false;
  case AArch64::STRBBui:
  case AArch64::STURBBi:
  case AArch64::STRHHui:
  case AArch64::STURHHi:
    return true;
  }
}

// Scaling factor for unscaled load or store.
static int getMemScale(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Opcode has unknown scale!");
  case AArch64::LDRBBui:
  case AArch64::LDURBBi:
  case AArch64::LDRSBWui:
  case AArch64::LDURSBWi:
  case AArch64::STRBBui:
  case AArch64::STURBBi:
    return 1;
  case AArch64::LDRHHui:
  case AArch64::LDURHHi:
  case AArch64::LDRSHWui:
  case AArch64::LDURSHWi:
  case AArch64::STRHHui:
  case AArch64::STURHHi:
    return 2;
  case AArch64::LDRSui:
  case AArch64::LDURSi:
  case AArch64::LDRSWui:
  case AArch64::LDURSWi:
  case AArch64::LDRWui:
  case AArch64::LDURWi:
  case AArch64::STRSui:
  case AArch64::STURSi:
  case AArch64::STRWui:
  case AArch64::STURWi:
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::STPSi:
  case AArch64::STPWi:
    return 4;
  case AArch64::LDRDui:
  case AArch64::LDURDi:
  case AArch64::LDRXui:
  case AArch64::LDURXi:
  case AArch64::STRDui:
  case AArch64::STURDi:
  case AArch64::STRXui:
  case AArch64::STURXi:
  case AArch64::LDPDi:
  case AArch64::LDPXi:
  case AArch64::STPDi:
  case AArch64::STPXi:
    return 8;
  case AArch64::LDRQui:
  case AArch64::LDURQi:
  case AArch64::STRQui:
  case AArch64::STURQi:
  case AArch64::LDPQi:
  case AArch64::STPQi:
    return 16;
  }
}

static unsigned getMatchingNonSExtOpcode(unsigned Opc,
                                         bool *IsValidLdStrOpc = nullptr) {
  if (IsValidLdStrOpc)
    *IsValidLdStrOpc = true;
  switch (Opc) {
  default:
    if (IsValidLdStrOpc)
      *IsValidLdStrOpc = false;
    return std::numeric_limits<unsigned>::max();
  case AArch64::STRDui:
  case AArch64::STURDi:
  case AArch64::STRQui:
  case AArch64::STURQi:
  case AArch64::STRBBui:
  case AArch64::STURBBi:
  case AArch64::STRHHui:
  case AArch64::STURHHi:
  case AArch64::STRWui:
  case AArch64::STURWi:
  case AArch64::STRXui:
  case AArch64::STURXi:
  case AArch64::LDRDui:
  case AArch64::LDURDi:
  case AArch64::LDRQui:
  case AArch64::LDURQi:
  case AArch64::LDRWui:
  case AArch64::LDURWi:
  case AArch64::LDRXui:
  case AArch64::LDURXi:
  case AArch64::STRSui:
  case AArch64::STURSi:
  case AArch64::LDRSui:
  case AArch64::LDURSi:
    return Opc;
  case AArch64::LDRSWui:
    return AArch64::LDRWui;
  case AArch64::LDURSWi:
    return AArch64::LDURWi;
  }
}

static unsigned getMatchingWideOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Opcode has no wide equivalent!");
  case AArch64::STRBBui:
    return AArch64::STRHHui;
  case AArch64::STRHHui:
    return AArch64::STRWui;
  case AArch64::STURBBi:
    return AArch64::STURHHi;
  case AArch64::STURHHi:
    return AArch64::STURWi;
  case AArch64::STURWi:
    return AArch64::STURXi;
  case AArch64::STRWui:
    return AArch64::STRXui;
  }
}

static unsigned getMatchingPairOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Opcode has no pairwise equivalent!");
  case AArch64::STRSui:
  case AArch64::STURSi:
    return AArch64::STPSi;
  case AArch64::STRDui:
  case AArch64::STURDi:
    return AArch64::STPDi;
  case AArch64::STRQui:
  case AArch64::STURQi:
    return AArch64::STPQi;
  case AArch64::STRWui:
  case AArch64::STURWi:
    return AArch64::STPWi;
  case AArch64::STRXui:
  case AArch64::STURXi:
    return AArch64::STPXi;
  case AArch64::LDRSui:
  case AArch64::LDURSi:
    return AArch64::LDPSi;
  case AArch64::LDRDui:
  case AArch64::LDURDi:
    return AArch64::LDPDi;
  case AArch64::LDRQui:
  case AArch64::LDURQi:
    return AArch64::LDPQi;
  case AArch64::LDRWui:
  case AArch64::LDURWi:
    return AArch64::LDPWi;
  case AArch64::LDRXui:
  case AArch64::LDURXi:
    return AArch64::LDPXi;
  case AArch64::LDRSWui:
  case AArch64::LDURSWi:
    return AArch64::LDPSWi;
  }
}

static unsigned isMatchingStore(MachineInstr &LoadInst,
                                MachineInstr &StoreInst) {
  unsigned LdOpc = LoadInst.getOpcode();
  unsigned StOpc = StoreInst.getOpcode();
  switch (LdOpc) {
  default:
    llvm_unreachable("Unsupported load instruction!");
  case AArch64::LDRBBui:
    return StOpc == AArch64::STRBBui || StOpc == AArch64::STRHHui ||
           StOpc == AArch64::STRWui || StOpc == AArch64::STRXui;
  case AArch64::LDURBBi:
    return StOpc == AArch64::STURBBi || StOpc == AArch64::STURHHi ||
           StOpc == AArch64::STURWi || StOpc == AArch64::STURXi;
  case AArch64::LDRHHui:
    return StOpc == AArch64::STRHHui || StOpc == AArch64::STRWui ||
           StOpc == AArch64::STRXui;
  case AArch64::LDURHHi:
    return StOpc == AArch64::STURHHi || StOpc == AArch64::STURWi ||
           StOpc == AArch64::STURXi;
  case AArch64::LDRWui:
    return StOpc == AArch64::STRWui || StOpc == AArch64::STRXui;
  case AArch64::LDURWi:
    return StOpc == AArch64::STURWi || StOpc == AArch64::STURXi;
  case AArch64::LDRXui:
    return StOpc == AArch64::STRXui;
  case AArch64::LDURXi:
    return StOpc == AArch64::STURXi;
  }
}

static unsigned getPreIndexedOpcode(unsigned Opc) {
  // FIXME: We don't currently support creating pre-indexed loads/stores when
  // the load or store is the unscaled version.  If we decide to perform such an
  // optimization in the future the cases for the unscaled loads/stores will
  // need to be added here.
  switch (Opc) {
  default:
    llvm_unreachable("Opcode has no pre-indexed equivalent!");
  case AArch64::STRSui:
    return AArch64::STRSpre;
  case AArch64::STRDui:
    return AArch64::STRDpre;
  case AArch64::STRQui:
    return AArch64::STRQpre;
  case AArch64::STRBBui:
    return AArch64::STRBBpre;
  case AArch64::STRHHui:
    return AArch64::STRHHpre;
  case AArch64::STRWui:
    return AArch64::STRWpre;
  case AArch64::STRXui:
    return AArch64::STRXpre;
  case AArch64::LDRSui:
    return AArch64::LDRSpre;
  case AArch64::LDRDui:
    return AArch64::LDRDpre;
  case AArch64::LDRQui:
    return AArch64::LDRQpre;
  case AArch64::LDRBBui:
    return AArch64::LDRBBpre;
  case AArch64::LDRHHui:
    return AArch64::LDRHHpre;
  case AArch64::LDRWui:
    return AArch64::LDRWpre;
  case AArch64::LDRXui:
    return AArch64::LDRXpre;
  case AArch64::LDRSWui:
    return AArch64::LDRSWpre;
  case AArch64::LDPSi:
    return AArch64::LDPSpre;
  case AArch64::LDPSWi:
    return AArch64::LDPSWpre;
  case AArch64::LDPDi:
    return AArch64::LDPDpre;
  case AArch64::LDPQi:
    return AArch64::LDPQpre;
  case AArch64::LDPWi:
    return AArch64::LDPWpre;
  case AArch64::LDPXi:
    return AArch64::LDPXpre;
  case AArch64::STPSi:
    return AArch64::STPSpre;
  case AArch64::STPDi:
    return AArch64::STPDpre;
  case AArch64::STPQi:
    return AArch64::STPQpre;
  case AArch64::STPWi:
    return AArch64::STPWpre;
  case AArch64::STPXi:
    return AArch64::STPXpre;
  }
}

static unsigned getPostIndexedOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Opcode has no post-indexed wise equivalent!");
  case AArch64::STRSui:
  case AArch64::STURSi:
    return AArch64::STRSpost;
  case AArch64::STRDui:
  case AArch64::STURDi:
    return AArch64::STRDpost;
  case AArch64::STRQui:
  case AArch64::STURQi:
    return AArch64::STRQpost;
  case AArch64::STRBBui:
    return AArch64::STRBBpost;
  case AArch64::STRHHui:
    return AArch64::STRHHpost;
  case AArch64::STRWui:
  case AArch64::STURWi:
    return AArch64::STRWpost;
  case AArch64::STRXui:
  case AArch64::STURXi:
    return AArch64::STRXpost;
  case AArch64::LDRSui:
  case AArch64::LDURSi:
    return AArch64::LDRSpost;
  case AArch64::LDRDui:
  case AArch64::LDURDi:
    return AArch64::LDRDpost;
  case AArch64::LDRQui:
  case AArch64::LDURQi:
    return AArch64::LDRQpost;
  case AArch64::LDRBBui:
    return AArch64::LDRBBpost;
  case AArch64::LDRHHui:
    return AArch64::LDRHHpost;
  case AArch64::LDRWui:
  case AArch64::LDURWi:
    return AArch64::LDRWpost;
  case AArch64::LDRXui:
  case AArch64::LDURXi:
    return AArch64::LDRXpost;
  case AArch64::LDRSWui:
    return AArch64::LDRSWpost;
  case AArch64::LDPSi:
    return AArch64::LDPSpost;
  case AArch64::LDPSWi:
    return AArch64::LDPSWpost;
  case AArch64::LDPDi:
    return AArch64::LDPDpost;
  case AArch64::LDPQi:
    return AArch64::LDPQpost;
  case AArch64::LDPWi:
    return AArch64::LDPWpost;
  case AArch64::LDPXi:
    return AArch64::LDPXpost;
  case AArch64::STPSi:
    return AArch64::STPSpost;
  case AArch64::STPDi:
    return AArch64::STPDpost;
  case AArch64::STPQi:
    return AArch64::STPQpost;
  case AArch64::STPWi:
    return AArch64::STPWpost;
  case AArch64::STPXi:
    return AArch64::STPXpost;
  }
}

static bool isPairedLdSt(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPWi:
  case AArch64::LDPXi:
  case AArch64::STPSi:
  case AArch64::STPDi:
  case AArch64::STPQi:
  case AArch64::STPWi:
  case AArch64::STPXi:
    return true;
  }
}

static const MachineOperand &getLdStRegOp(const MachineInstr &MI,
                                          unsigned PairedRegOp = 0) {
  assert(PairedRegOp < 2 && "Unexpected register operand idx.");
  unsigned Idx = isPairedLdSt(MI) ? PairedRegOp : 0;
  return MI.getOperand(Idx);
}

static const MachineOperand &getLdStBaseOp(const MachineInstr &MI) {
  unsigned Idx = isPairedLdSt(MI) ? 2 : 1;
  return MI.getOperand(Idx);
}

static const MachineOperand &getLdStOffsetOp(const MachineInstr &MI) {
  unsigned Idx = isPairedLdSt(MI) ? 3 : 2;
  return MI.getOperand(Idx);
}

static bool isLdOffsetInRangeOfSt(MachineInstr &LoadInst,
                                  MachineInstr &StoreInst,
                                  const AArch64InstrInfo *TII) {
  assert(isMatchingStore(LoadInst, StoreInst) && "Expect only matched ld/st.");
  int LoadSize = getMemScale(LoadInst);
  int StoreSize = getMemScale(StoreInst);
  int UnscaledStOffset = TII->isUnscaledLdSt(StoreInst)
                             ? getLdStOffsetOp(StoreInst).getImm()
                             : getLdStOffsetOp(StoreInst).getImm() * StoreSize;
  int UnscaledLdOffset = TII->isUnscaledLdSt(LoadInst)
                             ? getLdStOffsetOp(LoadInst).getImm()
                             : getLdStOffsetOp(LoadInst).getImm() * LoadSize;
  return (UnscaledStOffset <= UnscaledLdOffset) &&
         (UnscaledLdOffset + LoadSize <= (UnscaledStOffset + StoreSize));
}

static bool isPromotableZeroStoreInst(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return (Opc == AArch64::STRWui || Opc == AArch64::STURWi ||
          isNarrowStore(Opc)) &&
         getLdStRegOp(MI).getReg() == AArch64::WZR;
}

static bool isPromotableLoadFromStore(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  // Scaled instructions.
  case AArch64::LDRBBui:
  case AArch64::LDRHHui:
  case AArch64::LDRWui:
  case AArch64::LDRXui:
  // Unscaled instructions.
  case AArch64::LDURBBi:
  case AArch64::LDURHHi:
  case AArch64::LDURWi:
  case AArch64::LDURXi:
    return true;
  }
}

static bool isMergeableLdStUpdate(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  default:
    return false;
  // Scaled instructions.
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STRQui:
  case AArch64::STRXui:
  case AArch64::STRWui:
  case AArch64::STRHHui:
  case AArch64::STRBBui:
  case AArch64::LDRSui:
  case AArch64::LDRDui:
  case AArch64::LDRQui:
  case AArch64::LDRXui:
  case AArch64::LDRWui:
  case AArch64::LDRHHui:
  case AArch64::LDRBBui:
  // Unscaled instructions.
  case AArch64::STURSi:
  case AArch64::STURDi:
  case AArch64::STURQi:
  case AArch64::STURWi:
  case AArch64::STURXi:
  case AArch64::LDURSi:
  case AArch64::LDURDi:
  case AArch64::LDURQi:
  case AArch64::LDURWi:
  case AArch64::LDURXi:
  // Paired instructions.
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPWi:
  case AArch64::LDPXi:
  case AArch64::STPSi:
  case AArch64::STPDi:
  case AArch64::STPQi:
  case AArch64::STPWi:
  case AArch64::STPXi:
    // Make sure this is a reg+imm (as opposed to an address reloc).
    if (!getLdStOffsetOp(MI).isImm())
      return false;

    return true;
  }
}

MachineBasicBlock::iterator
AArch64LoadStoreOpt::mergeNarrowZeroStores(MachineBasicBlock::iterator I,
                                           MachineBasicBlock::iterator MergeMI,
                                           const LdStPairFlags &Flags) {
  assert(isPromotableZeroStoreInst(*I) && isPromotableZeroStoreInst(*MergeMI) &&
         "Expected promotable zero stores.");

  MachineBasicBlock::iterator NextI = I;
  ++NextI;
  // If NextI is the second of the two instructions to be merged, we need
  // to skip one further. Either way we merge will invalidate the iterator,
  // and we don't need to scan the new instruction, as it's a pairwise
  // instruction, which we're not considering for further action anyway.
  if (NextI == MergeMI)
    ++NextI;

  unsigned Opc = I->getOpcode();
  bool IsScaled = !TII->isUnscaledLdSt(Opc);
  int OffsetStride = IsScaled ? 1 : getMemScale(*I);

  bool MergeForward = Flags.getMergeForward();
  // Insert our new paired instruction after whichever of the paired
  // instructions MergeForward indicates.
  MachineBasicBlock::iterator InsertionPoint = MergeForward ? MergeMI : I;
  // Also based on MergeForward is from where we copy the base register operand
  // so we get the flags compatible with the input code.
  const MachineOperand &BaseRegOp =
      MergeForward ? getLdStBaseOp(*MergeMI) : getLdStBaseOp(*I);

  // Which register is Rt and which is Rt2 depends on the offset order.
  MachineInstr *RtMI;
  if (getLdStOffsetOp(*I).getImm() ==
      getLdStOffsetOp(*MergeMI).getImm() + OffsetStride)
    RtMI = &*MergeMI;
  else
    RtMI = &*I;

  int OffsetImm = getLdStOffsetOp(*RtMI).getImm();
  // Change the scaled offset from small to large type.
  if (IsScaled) {
    assert(((OffsetImm & 1) == 0) && "Unexpected offset to merge");
    OffsetImm /= 2;
  }

  // Construct the new instruction.
  DebugLoc DL = I->getDebugLoc();
  MachineBasicBlock *MBB = I->getParent();
  MachineInstrBuilder MIB;
  MIB = BuildMI(*MBB, InsertionPoint, DL, TII->get(getMatchingWideOpcode(Opc)))
            .addReg(isNarrowStore(Opc) ? AArch64::WZR : AArch64::XZR)
            .add(BaseRegOp)
            .addImm(OffsetImm)
            .cloneMergedMemRefs({&*I, &*MergeMI})
            .setMIFlags(I->mergeFlagsWith(*MergeMI));
  (void)MIB;

  LLVM_DEBUG(dbgs() << "Creating wider store. Replacing instructions:\n    ");
  LLVM_DEBUG(I->print(dbgs()));
  LLVM_DEBUG(dbgs() << "    ");
  LLVM_DEBUG(MergeMI->print(dbgs()));
  LLVM_DEBUG(dbgs() << "  with instruction:\n    ");
  LLVM_DEBUG(((MachineInstr *)MIB)->print(dbgs()));
  LLVM_DEBUG(dbgs() << "\n");

  // Erase the old instructions.
  I->eraseFromParent();
  MergeMI->eraseFromParent();
  return NextI;
}

MachineBasicBlock::iterator
AArch64LoadStoreOpt::mergePairedInsns(MachineBasicBlock::iterator I,
                                      MachineBasicBlock::iterator Paired,
                                      const LdStPairFlags &Flags) {
  MachineBasicBlock::iterator NextI = I;
  ++NextI;
  // If NextI is the second of the two instructions to be merged, we need
  // to skip one further. Either way we merge will invalidate the iterator,
  // and we don't need to scan the new instruction, as it's a pairwise
  // instruction, which we're not considering for further action anyway.
  if (NextI == Paired)
    ++NextI;

  int SExtIdx = Flags.getSExtIdx();
  unsigned Opc =
      SExtIdx == -1 ? I->getOpcode() : getMatchingNonSExtOpcode(I->getOpcode());
  bool IsUnscaled = TII->isUnscaledLdSt(Opc);
  int OffsetStride = IsUnscaled ? getMemScale(*I) : 1;

  bool MergeForward = Flags.getMergeForward();
  // Insert our new paired instruction after whichever of the paired
  // instructions MergeForward indicates.
  MachineBasicBlock::iterator InsertionPoint = MergeForward ? Paired : I;
  // Also based on MergeForward is from where we copy the base register operand
  // so we get the flags compatible with the input code.
  const MachineOperand &BaseRegOp =
      MergeForward ? getLdStBaseOp(*Paired) : getLdStBaseOp(*I);

  int Offset = getLdStOffsetOp(*I).getImm();
  int PairedOffset = getLdStOffsetOp(*Paired).getImm();
  bool PairedIsUnscaled = TII->isUnscaledLdSt(Paired->getOpcode());
  if (IsUnscaled != PairedIsUnscaled) {
    // We're trying to pair instructions that differ in how they are scaled.  If
    // I is scaled then scale the offset of Paired accordingly.  Otherwise, do
    // the opposite (i.e., make Paired's offset unscaled).
    int MemSize = getMemScale(*Paired);
    if (PairedIsUnscaled) {
      // If the unscaled offset isn't a multiple of the MemSize, we can't
      // pair the operations together.
      assert(!(PairedOffset % getMemScale(*Paired)) &&
             "Offset should be a multiple of the stride!");
      PairedOffset /= MemSize;
    } else {
      PairedOffset *= MemSize;
    }
  }

  // Which register is Rt and which is Rt2 depends on the offset order.
  MachineInstr *RtMI, *Rt2MI;
  if (Offset == PairedOffset + OffsetStride) {
    RtMI = &*Paired;
    Rt2MI = &*I;
    // Here we swapped the assumption made for SExtIdx.
    // I.e., we turn ldp I, Paired into ldp Paired, I.
    // Update the index accordingly.
    if (SExtIdx != -1)
      SExtIdx = (SExtIdx + 1) % 2;
  } else {
    RtMI = &*I;
    Rt2MI = &*Paired;
  }
  int OffsetImm = getLdStOffsetOp(*RtMI).getImm();
  // Scale the immediate offset, if necessary.
  if (TII->isUnscaledLdSt(RtMI->getOpcode())) {
    assert(!(OffsetImm % getMemScale(*RtMI)) &&
           "Unscaled offset cannot be scaled.");
    OffsetImm /= getMemScale(*RtMI);
  }

  // Construct the new instruction.
  MachineInstrBuilder MIB;
  DebugLoc DL = I->getDebugLoc();
  MachineBasicBlock *MBB = I->getParent();
  MachineOperand RegOp0 = getLdStRegOp(*RtMI);
  MachineOperand RegOp1 = getLdStRegOp(*Rt2MI);
  // Kill flags may become invalid when moving stores for pairing.
  if (RegOp0.isUse()) {
    if (!MergeForward) {
      // Clear kill flags on store if moving upwards. Example:
      //   STRWui %w0, ...
      //   USE %w1
      //   STRWui kill %w1  ; need to clear kill flag when moving STRWui upwards
      RegOp0.setIsKill(false);
      RegOp1.setIsKill(false);
    } else {
      // Clear kill flags of the first stores register. Example:
      //   STRWui %w1, ...
      //   USE kill %w1   ; need to clear kill flag when moving STRWui downwards
      //   STRW %w0
      unsigned Reg = getLdStRegOp(*I).getReg();
      for (MachineInstr &MI : make_range(std::next(I), Paired))
        MI.clearRegisterKills(Reg, TRI);
    }
  }
  MIB = BuildMI(*MBB, InsertionPoint, DL, TII->get(getMatchingPairOpcode(Opc)))
            .add(RegOp0)
            .add(RegOp1)
            .add(BaseRegOp)
            .addImm(OffsetImm)
            .cloneMergedMemRefs({&*I, &*Paired})
            .setMIFlags(I->mergeFlagsWith(*Paired));

  (void)MIB;

  LLVM_DEBUG(
      dbgs() << "Creating pair load/store. Replacing instructions:\n    ");
  LLVM_DEBUG(I->print(dbgs()));
  LLVM_DEBUG(dbgs() << "    ");
  LLVM_DEBUG(Paired->print(dbgs()));
  LLVM_DEBUG(dbgs() << "  with instruction:\n    ");
  if (SExtIdx != -1) {
    // Generate the sign extension for the proper result of the ldp.
    // I.e., with X1, that would be:
    // %w1 = KILL %w1, implicit-def %x1
    // %x1 = SBFMXri killed %x1, 0, 31
    MachineOperand &DstMO = MIB->getOperand(SExtIdx);
    // Right now, DstMO has the extended register, since it comes from an
    // extended opcode.
    unsigned DstRegX = DstMO.getReg();
    // Get the W variant of that register.
    unsigned DstRegW = TRI->getSubReg(DstRegX, AArch64::sub_32);
    // Update the result of LDP to use the W instead of the X variant.
    DstMO.setReg(DstRegW);
    LLVM_DEBUG(((MachineInstr *)MIB)->print(dbgs()));
    LLVM_DEBUG(dbgs() << "\n");
    // Make the machine verifier happy by providing a definition for
    // the X register.
    // Insert this definition right after the generated LDP, i.e., before
    // InsertionPoint.
    MachineInstrBuilder MIBKill =
        BuildMI(*MBB, InsertionPoint, DL, TII->get(TargetOpcode::KILL), DstRegW)
            .addReg(DstRegW)
            .addReg(DstRegX, RegState::Define);
    MIBKill->getOperand(2).setImplicit();
    // Create the sign extension.
    MachineInstrBuilder MIBSXTW =
        BuildMI(*MBB, InsertionPoint, DL, TII->get(AArch64::SBFMXri), DstRegX)
            .addReg(DstRegX)
            .addImm(0)
            .addImm(31);
    (void)MIBSXTW;
    LLVM_DEBUG(dbgs() << "  Extend operand:\n    ");
    LLVM_DEBUG(((MachineInstr *)MIBSXTW)->print(dbgs()));
  } else {
    LLVM_DEBUG(((MachineInstr *)MIB)->print(dbgs()));
  }
  LLVM_DEBUG(dbgs() << "\n");

  // Erase the old instructions.
  I->eraseFromParent();
  Paired->eraseFromParent();

  return NextI;
}

MachineBasicBlock::iterator
AArch64LoadStoreOpt::promoteLoadFromStore(MachineBasicBlock::iterator LoadI,
                                          MachineBasicBlock::iterator StoreI) {
  MachineBasicBlock::iterator NextI = LoadI;
  ++NextI;

  int LoadSize = getMemScale(*LoadI);
  int StoreSize = getMemScale(*StoreI);
  unsigned LdRt = getLdStRegOp(*LoadI).getReg();
  const MachineOperand &StMO = getLdStRegOp(*StoreI);
  unsigned StRt = getLdStRegOp(*StoreI).getReg();
  bool IsStoreXReg = TRI->getRegClass(AArch64::GPR64RegClassID)->contains(StRt);

  assert((IsStoreXReg ||
          TRI->getRegClass(AArch64::GPR32RegClassID)->contains(StRt)) &&
         "Unexpected RegClass");

  MachineInstr *BitExtMI;
  if (LoadSize == StoreSize && (LoadSize == 4 || LoadSize == 8)) {
    // Remove the load, if the destination register of the loads is the same
    // register for stored value.
    if (StRt == LdRt && LoadSize == 8) {
      for (MachineInstr &MI : make_range(StoreI->getIterator(),
                                         LoadI->getIterator())) {
        if (MI.killsRegister(StRt, TRI)) {
          MI.clearRegisterKills(StRt, TRI);
          break;
        }
      }
      LLVM_DEBUG(dbgs() << "Remove load instruction:\n    ");
      LLVM_DEBUG(LoadI->print(dbgs()));
      LLVM_DEBUG(dbgs() << "\n");
      LoadI->eraseFromParent();
      return NextI;
    }
    // Replace the load with a mov if the load and store are in the same size.
    BitExtMI =
        BuildMI(*LoadI->getParent(), LoadI, LoadI->getDebugLoc(),
                TII->get(IsStoreXReg ? AArch64::ORRXrs : AArch64::ORRWrs), LdRt)
            .addReg(IsStoreXReg ? AArch64::XZR : AArch64::WZR)
            .add(StMO)
            .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0))
            .setMIFlags(LoadI->getFlags());
  } else {
    // FIXME: Currently we disable this transformation in big-endian targets as
    // performance and correctness are verified only in little-endian.
    if (!Subtarget->isLittleEndian())
      return NextI;
    bool IsUnscaled = TII->isUnscaledLdSt(*LoadI);
    assert(IsUnscaled == TII->isUnscaledLdSt(*StoreI) &&
           "Unsupported ld/st match");
    assert(LoadSize <= StoreSize && "Invalid load size");
    int UnscaledLdOffset = IsUnscaled
                               ? getLdStOffsetOp(*LoadI).getImm()
                               : getLdStOffsetOp(*LoadI).getImm() * LoadSize;
    int UnscaledStOffset = IsUnscaled
                               ? getLdStOffsetOp(*StoreI).getImm()
                               : getLdStOffsetOp(*StoreI).getImm() * StoreSize;
    int Width = LoadSize * 8;
    int Immr = 8 * (UnscaledLdOffset - UnscaledStOffset);
    int Imms = Immr + Width - 1;
    unsigned DestReg = IsStoreXReg
                           ? TRI->getMatchingSuperReg(LdRt, AArch64::sub_32,
                                                      &AArch64::GPR64RegClass)
                           : LdRt;

    assert((UnscaledLdOffset >= UnscaledStOffset &&
            (UnscaledLdOffset + LoadSize) <= UnscaledStOffset + StoreSize) &&
           "Invalid offset");

    Immr = 8 * (UnscaledLdOffset - UnscaledStOffset);
    Imms = Immr + Width - 1;
    if (UnscaledLdOffset == UnscaledStOffset) {
      uint32_t AndMaskEncoded = ((IsStoreXReg ? 1 : 0) << 12) // N
                                | ((Immr) << 6)               // immr
                                | ((Imms) << 0)               // imms
          ;

      BitExtMI =
          BuildMI(*LoadI->getParent(), LoadI, LoadI->getDebugLoc(),
                  TII->get(IsStoreXReg ? AArch64::ANDXri : AArch64::ANDWri),
                  DestReg)
              .add(StMO)
              .addImm(AndMaskEncoded)
              .setMIFlags(LoadI->getFlags());
    } else {
      BitExtMI =
          BuildMI(*LoadI->getParent(), LoadI, LoadI->getDebugLoc(),
                  TII->get(IsStoreXReg ? AArch64::UBFMXri : AArch64::UBFMWri),
                  DestReg)
              .add(StMO)
              .addImm(Immr)
              .addImm(Imms)
              .setMIFlags(LoadI->getFlags());
    }
  }

  // Clear kill flags between store and load.
  for (MachineInstr &MI : make_range(StoreI->getIterator(),
                                     BitExtMI->getIterator()))
    if (MI.killsRegister(StRt, TRI)) {
      MI.clearRegisterKills(StRt, TRI);
      break;
    }

  LLVM_DEBUG(dbgs() << "Promoting load by replacing :\n    ");
  LLVM_DEBUG(StoreI->print(dbgs()));
  LLVM_DEBUG(dbgs() << "    ");
  LLVM_DEBUG(LoadI->print(dbgs()));
  LLVM_DEBUG(dbgs() << "  with instructions:\n    ");
  LLVM_DEBUG(StoreI->print(dbgs()));
  LLVM_DEBUG(dbgs() << "    ");
  LLVM_DEBUG((BitExtMI)->print(dbgs()));
  LLVM_DEBUG(dbgs() << "\n");

  // Erase the old instructions.
  LoadI->eraseFromParent();
  return NextI;
}

static bool inBoundsForPair(bool IsUnscaled, int Offset, int OffsetStride) {
  // Convert the byte-offset used by unscaled into an "element" offset used
  // by the scaled pair load/store instructions.
  if (IsUnscaled) {
    // If the byte-offset isn't a multiple of the stride, there's no point
    // trying to match it.
    if (Offset % OffsetStride)
      return false;
    Offset /= OffsetStride;
  }
  return Offset <= 63 && Offset >= -64;
}

// Do alignment, specialized to power of 2 and for signed ints,
// avoiding having to do a C-style cast from uint_64t to int when
// using alignTo from include/llvm/Support/MathExtras.h.
// FIXME: Move this function to include/MathExtras.h?
static int alignTo(int Num, int PowOf2) {
  return (Num + PowOf2 - 1) & ~(PowOf2 - 1);
}

static bool mayAlias(MachineInstr &MIa, MachineInstr &MIb,
                     AliasAnalysis *AA) {
  // One of the instructions must modify memory.
  if (!MIa.mayStore() && !MIb.mayStore())
    return false;

  // Both instructions must be memory operations.
  if (!MIa.mayLoadOrStore() && !MIb.mayLoadOrStore())
    return false;

  return MIa.mayAlias(AA, MIb, /*UseTBAA*/false);
}

static bool mayAlias(MachineInstr &MIa,
                     SmallVectorImpl<MachineInstr *> &MemInsns,
                     AliasAnalysis *AA) {
  for (MachineInstr *MIb : MemInsns)
    if (mayAlias(MIa, *MIb, AA))
      return true;

  return false;
}

bool AArch64LoadStoreOpt::findMatchingStore(
    MachineBasicBlock::iterator I, unsigned Limit,
    MachineBasicBlock::iterator &StoreI) {
  MachineBasicBlock::iterator B = I->getParent()->begin();
  MachineBasicBlock::iterator MBBI = I;
  MachineInstr &LoadMI = *I;
  unsigned BaseReg = getLdStBaseOp(LoadMI).getReg();

  // If the load is the first instruction in the block, there's obviously
  // not any matching store.
  if (MBBI == B)
    return false;

  // Track which register units have been modified and used between the first
  // insn and the second insn.
  ModifiedRegUnits.clear();
  UsedRegUnits.clear();

  unsigned Count = 0;
  do {
    --MBBI;
    MachineInstr &MI = *MBBI;

    // Don't count transient instructions towards the search limit since there
    // may be different numbers of them if e.g. debug information is present.
    if (!MI.isTransient())
      ++Count;

    // If the load instruction reads directly from the address to which the
    // store instruction writes and the stored value is not modified, we can
    // promote the load. Since we do not handle stores with pre-/post-index,
    // it's unnecessary to check if BaseReg is modified by the store itself.
    if (MI.mayStore() && isMatchingStore(LoadMI, MI) &&
        BaseReg == getLdStBaseOp(MI).getReg() &&
        isLdOffsetInRangeOfSt(LoadMI, MI, TII) &&
        ModifiedRegUnits.available(getLdStRegOp(MI).getReg())) {
      StoreI = MBBI;
      return true;
    }

    if (MI.isCall())
      return false;

    // Update modified / uses register units.
    LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits, UsedRegUnits, TRI);

    // Otherwise, if the base register is modified, we have no match, so
    // return early.
    if (!ModifiedRegUnits.available(BaseReg))
      return false;

    // If we encounter a store aliased with the load, return early.
    if (MI.mayStore() && mayAlias(LoadMI, MI, AA))
      return false;
  } while (MBBI != B && Count < Limit);
  return false;
}

// Returns true if FirstMI and MI are candidates for merging or pairing.
// Otherwise, returns false.
static bool areCandidatesToMergeOrPair(MachineInstr &FirstMI, MachineInstr &MI,
                                       LdStPairFlags &Flags,
                                       const AArch64InstrInfo *TII) {
  // If this is volatile or if pairing is suppressed, not a candidate.
  if (MI.hasOrderedMemoryRef() || TII->isLdStPairSuppressed(MI))
    return false;

  // We should have already checked FirstMI for pair suppression and volatility.
  assert(!FirstMI.hasOrderedMemoryRef() &&
         !TII->isLdStPairSuppressed(FirstMI) &&
         "FirstMI shouldn't get here if either of these checks are true.");

  unsigned OpcA = FirstMI.getOpcode();
  unsigned OpcB = MI.getOpcode();

  // Opcodes match: nothing more to check.
  if (OpcA == OpcB)
    return true;

  // Try to match a sign-extended load/store with a zero-extended load/store.
  bool IsValidLdStrOpc, PairIsValidLdStrOpc;
  unsigned NonSExtOpc = getMatchingNonSExtOpcode(OpcA, &IsValidLdStrOpc);
  assert(IsValidLdStrOpc &&
         "Given Opc should be a Load or Store with an immediate");
  // OpcA will be the first instruction in the pair.
  if (NonSExtOpc == getMatchingNonSExtOpcode(OpcB, &PairIsValidLdStrOpc)) {
    Flags.setSExtIdx(NonSExtOpc == (unsigned)OpcA ? 1 : 0);
    return true;
  }

  // If the second instruction isn't even a mergable/pairable load/store, bail
  // out.
  if (!PairIsValidLdStrOpc)
    return false;

  // FIXME: We don't support merging narrow stores with mixed scaled/unscaled
  // offsets.
  if (isNarrowStore(OpcA) || isNarrowStore(OpcB))
    return false;

  // Try to match an unscaled load/store with a scaled load/store.
  return TII->isUnscaledLdSt(OpcA) != TII->isUnscaledLdSt(OpcB) &&
         getMatchingPairOpcode(OpcA) == getMatchingPairOpcode(OpcB);

  // FIXME: Can we also match a mixed sext/zext unscaled/scaled pair?
}

/// Scan the instructions looking for a load/store that can be combined with the
/// current instruction into a wider equivalent or a load/store pair.
MachineBasicBlock::iterator
AArch64LoadStoreOpt::findMatchingInsn(MachineBasicBlock::iterator I,
                                      LdStPairFlags &Flags, unsigned Limit,
                                      bool FindNarrowMerge) {
  MachineBasicBlock::iterator E = I->getParent()->end();
  MachineBasicBlock::iterator MBBI = I;
  MachineInstr &FirstMI = *I;
  ++MBBI;

  bool MayLoad = FirstMI.mayLoad();
  bool IsUnscaled = TII->isUnscaledLdSt(FirstMI);
  unsigned Reg = getLdStRegOp(FirstMI).getReg();
  unsigned BaseReg = getLdStBaseOp(FirstMI).getReg();
  int Offset = getLdStOffsetOp(FirstMI).getImm();
  int OffsetStride = IsUnscaled ? getMemScale(FirstMI) : 1;
  bool IsPromotableZeroStore = isPromotableZeroStoreInst(FirstMI);

  // Track which register units have been modified and used between the first
  // insn (inclusive) and the second insn.
  ModifiedRegUnits.clear();
  UsedRegUnits.clear();

  // Remember any instructions that read/write memory between FirstMI and MI.
  SmallVector<MachineInstr *, 4> MemInsns;

  for (unsigned Count = 0; MBBI != E && Count < Limit; ++MBBI) {
    MachineInstr &MI = *MBBI;

    // Don't count transient instructions towards the search limit since there
    // may be different numbers of them if e.g. debug information is present.
    if (!MI.isTransient())
      ++Count;

    Flags.setSExtIdx(-1);
    if (areCandidatesToMergeOrPair(FirstMI, MI, Flags, TII) &&
        getLdStOffsetOp(MI).isImm()) {
      assert(MI.mayLoadOrStore() && "Expected memory operation.");
      // If we've found another instruction with the same opcode, check to see
      // if the base and offset are compatible with our starting instruction.
      // These instructions all have scaled immediate operands, so we just
      // check for +1/-1. Make sure to check the new instruction offset is
      // actually an immediate and not a symbolic reference destined for
      // a relocation.
      unsigned MIBaseReg = getLdStBaseOp(MI).getReg();
      int MIOffset = getLdStOffsetOp(MI).getImm();
      bool MIIsUnscaled = TII->isUnscaledLdSt(MI);
      if (IsUnscaled != MIIsUnscaled) {
        // We're trying to pair instructions that differ in how they are scaled.
        // If FirstMI is scaled then scale the offset of MI accordingly.
        // Otherwise, do the opposite (i.e., make MI's offset unscaled).
        int MemSize = getMemScale(MI);
        if (MIIsUnscaled) {
          // If the unscaled offset isn't a multiple of the MemSize, we can't
          // pair the operations together: bail and keep looking.
          if (MIOffset % MemSize) {
            LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits,
                                              UsedRegUnits, TRI);
            MemInsns.push_back(&MI);
            continue;
          }
          MIOffset /= MemSize;
        } else {
          MIOffset *= MemSize;
        }
      }

      if (BaseReg == MIBaseReg && ((Offset == MIOffset + OffsetStride) ||
                                   (Offset + OffsetStride == MIOffset))) {
        int MinOffset = Offset < MIOffset ? Offset : MIOffset;
        if (FindNarrowMerge) {
          // If the alignment requirements of the scaled wide load/store
          // instruction can't express the offset of the scaled narrow input,
          // bail and keep looking. For promotable zero stores, allow only when
          // the stored value is the same (i.e., WZR).
          if ((!IsUnscaled && alignTo(MinOffset, 2) != MinOffset) ||
              (IsPromotableZeroStore && Reg != getLdStRegOp(MI).getReg())) {
            LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits,
                                              UsedRegUnits, TRI);
            MemInsns.push_back(&MI);
            continue;
          }
        } else {
          // Pairwise instructions have a 7-bit signed offset field. Single
          // insns have a 12-bit unsigned offset field.  If the resultant
          // immediate offset of merging these instructions is out of range for
          // a pairwise instruction, bail and keep looking.
          if (!inBoundsForPair(IsUnscaled, MinOffset, OffsetStride)) {
            LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits,
                                              UsedRegUnits, TRI);
            MemInsns.push_back(&MI);
            continue;
          }
          // If the alignment requirements of the paired (scaled) instruction
          // can't express the offset of the unscaled input, bail and keep
          // looking.
          if (IsUnscaled && (alignTo(MinOffset, OffsetStride) != MinOffset)) {
            LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits,
                                              UsedRegUnits, TRI);
            MemInsns.push_back(&MI);
            continue;
          }
        }
        // If the destination register of the loads is the same register, bail
        // and keep looking. A load-pair instruction with both destination
        // registers the same is UNPREDICTABLE and will result in an exception.
        if (MayLoad && Reg == getLdStRegOp(MI).getReg()) {
          LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits, UsedRegUnits,
                                            TRI);
          MemInsns.push_back(&MI);
          continue;
        }

        // If the Rt of the second instruction was not modified or used between
        // the two instructions and none of the instructions between the second
        // and first alias with the second, we can combine the second into the
        // first.
        if (ModifiedRegUnits.available(getLdStRegOp(MI).getReg()) &&
            !(MI.mayLoad() &&
              !UsedRegUnits.available(getLdStRegOp(MI).getReg())) &&
            !mayAlias(MI, MemInsns, AA)) {
          Flags.setMergeForward(false);
          return MBBI;
        }

        // Likewise, if the Rt of the first instruction is not modified or used
        // between the two instructions and none of the instructions between the
        // first and the second alias with the first, we can combine the first
        // into the second.
        if (ModifiedRegUnits.available(getLdStRegOp(FirstMI).getReg()) &&
            !(MayLoad &&
              !UsedRegUnits.available(getLdStRegOp(FirstMI).getReg())) &&
            !mayAlias(FirstMI, MemInsns, AA)) {
          Flags.setMergeForward(true);
          return MBBI;
        }
        // Unable to combine these instructions due to interference in between.
        // Keep looking.
      }
    }

    // If the instruction wasn't a matching load or store.  Stop searching if we
    // encounter a call instruction that might modify memory.
    if (MI.isCall())
      return E;

    // Update modified / uses register units.
    LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits, UsedRegUnits, TRI);

    // Otherwise, if the base register is modified, we have no match, so
    // return early.
    if (!ModifiedRegUnits.available(BaseReg))
      return E;

    // Update list of instructions that read/write memory.
    if (MI.mayLoadOrStore())
      MemInsns.push_back(&MI);
  }
  return E;
}

MachineBasicBlock::iterator
AArch64LoadStoreOpt::mergeUpdateInsn(MachineBasicBlock::iterator I,
                                     MachineBasicBlock::iterator Update,
                                     bool IsPreIdx) {
  assert((Update->getOpcode() == AArch64::ADDXri ||
          Update->getOpcode() == AArch64::SUBXri) &&
         "Unexpected base register update instruction to merge!");
  MachineBasicBlock::iterator NextI = I;
  // Return the instruction following the merged instruction, which is
  // the instruction following our unmerged load. Unless that's the add/sub
  // instruction we're merging, in which case it's the one after that.
  if (++NextI == Update)
    ++NextI;

  int Value = Update->getOperand(2).getImm();
  assert(AArch64_AM::getShiftValue(Update->getOperand(3).getImm()) == 0 &&
         "Can't merge 1 << 12 offset into pre-/post-indexed load / store");
  if (Update->getOpcode() == AArch64::SUBXri)
    Value = -Value;

  unsigned NewOpc = IsPreIdx ? getPreIndexedOpcode(I->getOpcode())
                             : getPostIndexedOpcode(I->getOpcode());
  MachineInstrBuilder MIB;
  if (!isPairedLdSt(*I)) {
    // Non-paired instruction.
    MIB = BuildMI(*I->getParent(), I, I->getDebugLoc(), TII->get(NewOpc))
              .add(getLdStRegOp(*Update))
              .add(getLdStRegOp(*I))
              .add(getLdStBaseOp(*I))
              .addImm(Value)
              .setMemRefs(I->memoperands())
              .setMIFlags(I->mergeFlagsWith(*Update));
  } else {
    // Paired instruction.
    int Scale = getMemScale(*I);
    MIB = BuildMI(*I->getParent(), I, I->getDebugLoc(), TII->get(NewOpc))
              .add(getLdStRegOp(*Update))
              .add(getLdStRegOp(*I, 0))
              .add(getLdStRegOp(*I, 1))
              .add(getLdStBaseOp(*I))
              .addImm(Value / Scale)
              .setMemRefs(I->memoperands())
              .setMIFlags(I->mergeFlagsWith(*Update));
  }
  (void)MIB;

  if (IsPreIdx) {
    ++NumPreFolded;
    LLVM_DEBUG(dbgs() << "Creating pre-indexed load/store.");
  } else {
    ++NumPostFolded;
    LLVM_DEBUG(dbgs() << "Creating post-indexed load/store.");
  }
  LLVM_DEBUG(dbgs() << "    Replacing instructions:\n    ");
  LLVM_DEBUG(I->print(dbgs()));
  LLVM_DEBUG(dbgs() << "    ");
  LLVM_DEBUG(Update->print(dbgs()));
  LLVM_DEBUG(dbgs() << "  with instruction:\n    ");
  LLVM_DEBUG(((MachineInstr *)MIB)->print(dbgs()));
  LLVM_DEBUG(dbgs() << "\n");

  // Erase the old instructions for the block.
  I->eraseFromParent();
  Update->eraseFromParent();

  return NextI;
}

bool AArch64LoadStoreOpt::isMatchingUpdateInsn(MachineInstr &MemMI,
                                               MachineInstr &MI,
                                               unsigned BaseReg, int Offset) {
  switch (MI.getOpcode()) {
  default:
    break;
  case AArch64::SUBXri:
  case AArch64::ADDXri:
    // Make sure it's a vanilla immediate operand, not a relocation or
    // anything else we can't handle.
    if (!MI.getOperand(2).isImm())
      break;
    // Watch out for 1 << 12 shifted value.
    if (AArch64_AM::getShiftValue(MI.getOperand(3).getImm()))
      break;

    // The update instruction source and destination register must be the
    // same as the load/store base register.
    if (MI.getOperand(0).getReg() != BaseReg ||
        MI.getOperand(1).getReg() != BaseReg)
      break;

    bool IsPairedInsn = isPairedLdSt(MemMI);
    int UpdateOffset = MI.getOperand(2).getImm();
    if (MI.getOpcode() == AArch64::SUBXri)
      UpdateOffset = -UpdateOffset;

    // For non-paired load/store instructions, the immediate must fit in a
    // signed 9-bit integer.
    if (!IsPairedInsn && (UpdateOffset > 255 || UpdateOffset < -256))
      break;

    // For paired load/store instructions, the immediate must be a multiple of
    // the scaling factor.  The scaled offset must also fit into a signed 7-bit
    // integer.
    if (IsPairedInsn) {
      int Scale = getMemScale(MemMI);
      if (UpdateOffset % Scale != 0)
        break;

      int ScaledOffset = UpdateOffset / Scale;
      if (ScaledOffset > 63 || ScaledOffset < -64)
        break;
    }

    // If we have a non-zero Offset, we check that it matches the amount
    // we're adding to the register.
    if (!Offset || Offset == UpdateOffset)
      return true;
    break;
  }
  return false;
}

MachineBasicBlock::iterator AArch64LoadStoreOpt::findMatchingUpdateInsnForward(
    MachineBasicBlock::iterator I, int UnscaledOffset, unsigned Limit) {
  MachineBasicBlock::iterator E = I->getParent()->end();
  MachineInstr &MemMI = *I;
  MachineBasicBlock::iterator MBBI = I;

  unsigned BaseReg = getLdStBaseOp(MemMI).getReg();
  int MIUnscaledOffset = getLdStOffsetOp(MemMI).getImm() * getMemScale(MemMI);

  // Scan forward looking for post-index opportunities.  Updating instructions
  // can't be formed if the memory instruction doesn't have the offset we're
  // looking for.
  if (MIUnscaledOffset != UnscaledOffset)
    return E;

  // If the base register overlaps a destination register, we can't
  // merge the update.
  bool IsPairedInsn = isPairedLdSt(MemMI);
  for (unsigned i = 0, e = IsPairedInsn ? 2 : 1; i != e; ++i) {
    unsigned DestReg = getLdStRegOp(MemMI, i).getReg();
    if (DestReg == BaseReg || TRI->isSubRegister(BaseReg, DestReg))
      return E;
  }

  // Track which register units have been modified and used between the first
  // insn (inclusive) and the second insn.
  ModifiedRegUnits.clear();
  UsedRegUnits.clear();
  ++MBBI;
  for (unsigned Count = 0; MBBI != E && Count < Limit; ++MBBI) {
    MachineInstr &MI = *MBBI;

    // Don't count transient instructions towards the search limit since there
    // may be different numbers of them if e.g. debug information is present.
    if (!MI.isTransient())
      ++Count;

    // If we found a match, return it.
    if (isMatchingUpdateInsn(*I, MI, BaseReg, UnscaledOffset))
      return MBBI;

    // Update the status of what the instruction clobbered and used.
    LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits, UsedRegUnits, TRI);

    // Otherwise, if the base register is used or modified, we have no match, so
    // return early.
    if (!ModifiedRegUnits.available(BaseReg) ||
        !UsedRegUnits.available(BaseReg))
      return E;
  }
  return E;
}

MachineBasicBlock::iterator AArch64LoadStoreOpt::findMatchingUpdateInsnBackward(
    MachineBasicBlock::iterator I, unsigned Limit) {
  MachineBasicBlock::iterator B = I->getParent()->begin();
  MachineBasicBlock::iterator E = I->getParent()->end();
  MachineInstr &MemMI = *I;
  MachineBasicBlock::iterator MBBI = I;

  unsigned BaseReg = getLdStBaseOp(MemMI).getReg();
  int Offset = getLdStOffsetOp(MemMI).getImm();

  // If the load/store is the first instruction in the block, there's obviously
  // not any matching update. Ditto if the memory offset isn't zero.
  if (MBBI == B || Offset != 0)
    return E;
  // If the base register overlaps a destination register, we can't
  // merge the update.
  bool IsPairedInsn = isPairedLdSt(MemMI);
  for (unsigned i = 0, e = IsPairedInsn ? 2 : 1; i != e; ++i) {
    unsigned DestReg = getLdStRegOp(MemMI, i).getReg();
    if (DestReg == BaseReg || TRI->isSubRegister(BaseReg, DestReg))
      return E;
  }

  // Track which register units have been modified and used between the first
  // insn (inclusive) and the second insn.
  ModifiedRegUnits.clear();
  UsedRegUnits.clear();
  unsigned Count = 0;
  do {
    --MBBI;
    MachineInstr &MI = *MBBI;

    // Don't count transient instructions towards the search limit since there
    // may be different numbers of them if e.g. debug information is present.
    if (!MI.isTransient())
      ++Count;

    // If we found a match, return it.
    if (isMatchingUpdateInsn(*I, MI, BaseReg, Offset))
      return MBBI;

    // Update the status of what the instruction clobbered and used.
    LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits, UsedRegUnits, TRI);

    // Otherwise, if the base register is used or modified, we have no match, so
    // return early.
    if (!ModifiedRegUnits.available(BaseReg) ||
        !UsedRegUnits.available(BaseReg))
      return E;
  } while (MBBI != B && Count < Limit);
  return E;
}

bool AArch64LoadStoreOpt::tryToPromoteLoadFromStore(
    MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  // If this is a volatile load, don't mess with it.
  if (MI.hasOrderedMemoryRef())
    return false;

  // Make sure this is a reg+imm.
  // FIXME: It is possible to extend it to handle reg+reg cases.
  if (!getLdStOffsetOp(MI).isImm())
    return false;

  // Look backward up to LdStLimit instructions.
  MachineBasicBlock::iterator StoreI;
  if (findMatchingStore(MBBI, LdStLimit, StoreI)) {
    ++NumLoadsFromStoresPromoted;
    // Promote the load. Keeping the iterator straight is a
    // pain, so we let the merge routine tell us what the next instruction
    // is after it's done mucking about.
    MBBI = promoteLoadFromStore(MBBI, StoreI);
    return true;
  }
  return false;
}

// Merge adjacent zero stores into a wider store.
bool AArch64LoadStoreOpt::tryToMergeZeroStInst(
    MachineBasicBlock::iterator &MBBI) {
  assert(isPromotableZeroStoreInst(*MBBI) && "Expected narrow store.");
  MachineInstr &MI = *MBBI;
  MachineBasicBlock::iterator E = MI.getParent()->end();

  if (!TII->isCandidateToMergeOrPair(MI))
    return false;

  // Look ahead up to LdStLimit instructions for a mergable instruction.
  LdStPairFlags Flags;
  MachineBasicBlock::iterator MergeMI =
      findMatchingInsn(MBBI, Flags, LdStLimit, /* FindNarrowMerge = */ true);
  if (MergeMI != E) {
    ++NumZeroStoresPromoted;

    // Keeping the iterator straight is a pain, so we let the merge routine tell
    // us what the next instruction is after it's done mucking about.
    MBBI = mergeNarrowZeroStores(MBBI, MergeMI, Flags);
    return true;
  }
  return false;
}

// Find loads and stores that can be merged into a single load or store pair
// instruction.
bool AArch64LoadStoreOpt::tryToPairLdStInst(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock::iterator E = MI.getParent()->end();

  if (!TII->isCandidateToMergeOrPair(MI))
    return false;

  // Early exit if the offset is not possible to match. (6 bits of positive
  // range, plus allow an extra one in case we find a later insn that matches
  // with Offset-1)
  bool IsUnscaled = TII->isUnscaledLdSt(MI);
  int Offset = getLdStOffsetOp(MI).getImm();
  int OffsetStride = IsUnscaled ? getMemScale(MI) : 1;
  // Allow one more for offset.
  if (Offset > 0)
    Offset -= OffsetStride;
  if (!inBoundsForPair(IsUnscaled, Offset, OffsetStride))
    return false;

  // Look ahead up to LdStLimit instructions for a pairable instruction.
  LdStPairFlags Flags;
  MachineBasicBlock::iterator Paired =
      findMatchingInsn(MBBI, Flags, LdStLimit, /* FindNarrowMerge = */ false);
  if (Paired != E) {
    ++NumPairCreated;
    if (TII->isUnscaledLdSt(MI))
      ++NumUnscaledPairCreated;
    // Keeping the iterator straight is a pain, so we let the merge routine tell
    // us what the next instruction is after it's done mucking about.
    MBBI = mergePairedInsns(MBBI, Paired, Flags);
    return true;
  }
  return false;
}

bool AArch64LoadStoreOpt::tryToMergeLdStUpdate
    (MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock::iterator E = MI.getParent()->end();
  MachineBasicBlock::iterator Update;

  // Look forward to try to form a post-index instruction. For example,
  // ldr x0, [x20]
  // add x20, x20, #32
  //   merged into:
  // ldr x0, [x20], #32
  Update = findMatchingUpdateInsnForward(MBBI, 0, UpdateLimit);
  if (Update != E) {
    // Merge the update into the ld/st.
    MBBI = mergeUpdateInsn(MBBI, Update, /*IsPreIdx=*/false);
    return true;
  }

  // Don't know how to handle unscaled pre/post-index versions below, so bail.
  if (TII->isUnscaledLdSt(MI.getOpcode()))
    return false;

  // Look back to try to find a pre-index instruction. For example,
  // add x0, x0, #8
  // ldr x1, [x0]
  //   merged into:
  // ldr x1, [x0, #8]!
  Update = findMatchingUpdateInsnBackward(MBBI, UpdateLimit);
  if (Update != E) {
    // Merge the update into the ld/st.
    MBBI = mergeUpdateInsn(MBBI, Update, /*IsPreIdx=*/true);
    return true;
  }

  // The immediate in the load/store is scaled by the size of the memory
  // operation. The immediate in the add we're looking for,
  // however, is not, so adjust here.
  int UnscaledOffset = getLdStOffsetOp(MI).getImm() * getMemScale(MI);

  // Look forward to try to find a post-index instruction. For example,
  // ldr x1, [x0, #64]
  // add x0, x0, #64
  //   merged into:
  // ldr x1, [x0, #64]!
  Update = findMatchingUpdateInsnForward(MBBI, UnscaledOffset, UpdateLimit);
  if (Update != E) {
    // Merge the update into the ld/st.
    MBBI = mergeUpdateInsn(MBBI, Update, /*IsPreIdx=*/true);
    return true;
  }

  return false;
}

bool AArch64LoadStoreOpt::optimizeBlock(MachineBasicBlock &MBB,
                                        bool EnableNarrowZeroStOpt) {
  bool Modified = false;
  // Four tranformations to do here:
  // 1) Find loads that directly read from stores and promote them by
  //    replacing with mov instructions. If the store is wider than the load,
  //    the load will be replaced with a bitfield extract.
  //      e.g.,
  //        str w1, [x0, #4]
  //        ldrh w2, [x0, #6]
  //        ; becomes
  //        str w1, [x0, #4]
  //        lsr w2, w1, #16
  for (MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
       MBBI != E;) {
    if (isPromotableLoadFromStore(*MBBI) && tryToPromoteLoadFromStore(MBBI))
      Modified = true;
    else
      ++MBBI;
  }
  // 2) Merge adjacent zero stores into a wider store.
  //      e.g.,
  //        strh wzr, [x0]
  //        strh wzr, [x0, #2]
  //        ; becomes
  //        str wzr, [x0]
  //      e.g.,
  //        str wzr, [x0]
  //        str wzr, [x0, #4]
  //        ; becomes
  //        str xzr, [x0]
  if (EnableNarrowZeroStOpt)
    for (MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
         MBBI != E;) {
      if (isPromotableZeroStoreInst(*MBBI) && tryToMergeZeroStInst(MBBI))
        Modified = true;
      else
        ++MBBI;
    }
  // 3) Find loads and stores that can be merged into a single load or store
  //    pair instruction.
  //      e.g.,
  //        ldr x0, [x2]
  //        ldr x1, [x2, #8]
  //        ; becomes
  //        ldp x0, x1, [x2]
  for (MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
       MBBI != E;) {
    if (TII->isPairableLdStInst(*MBBI) && tryToPairLdStInst(MBBI))
      Modified = true;
    else
      ++MBBI;
  }
  // 4) Find base register updates that can be merged into the load or store
  //    as a base-reg writeback.
  //      e.g.,
  //        ldr x0, [x2]
  //        add x2, x2, #4
  //        ; becomes
  //        ldr x0, [x2], #4
  for (MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
       MBBI != E;) {
    if (isMergeableLdStUpdate(*MBBI) && tryToMergeLdStUpdate(MBBI))
      Modified = true;
    else
      ++MBBI;
  }

  return Modified;
}

bool AArch64LoadStoreOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  Subtarget = &static_cast<const AArch64Subtarget &>(Fn.getSubtarget());
  TII = static_cast<const AArch64InstrInfo *>(Subtarget->getInstrInfo());
  TRI = Subtarget->getRegisterInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  // Resize the modified and used register unit trackers.  We do this once
  // per function and then clear the register units each time we optimize a load
  // or store.
  ModifiedRegUnits.init(*TRI);
  UsedRegUnits.init(*TRI);

  bool Modified = false;
  bool enableNarrowZeroStOpt = !Subtarget->requiresStrictAlign();
  for (auto &MBB : Fn)
    Modified |= optimizeBlock(MBB, enableNarrowZeroStOpt);

  return Modified;
}

// FIXME: Do we need/want a pre-alloc pass like ARM has to try to keep loads and
// stores near one another?  Note: The pre-RA instruction scheduler already has
// hooks to try and schedule pairable loads/stores together to improve pairing
// opportunities.  Thus, pre-RA pairing pass may not be worth the effort.

// FIXME: When pairing store instructions it's very possible for this pass to
// hoist a store with a KILL marker above another use (without a KILL marker).
// The resulting IR is invalid, but nothing uses the KILL markers after this
// pass, so it's never caused a problem in practice.

/// createAArch64LoadStoreOptimizationPass - returns an instance of the
/// load / store optimization pass.
FunctionPass *llvm::createAArch64LoadStoreOptimizationPass() {
  return new AArch64LoadStoreOpt();
}
