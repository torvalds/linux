//===-- SILowerI1Copies.cpp - Lower I1 Copies -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers all occurrences of i1 values (with a vreg_1 register class)
// to lane masks (64-bit scalar registers). The pass assumes machine SSA form
// and a wave-level control flow graph.
//
// Before this pass, values that are semantically i1 and are defined and used
// within the same basic block are already represented as lane masks in scalar
// registers. However, values that cross basic blocks are always transferred
// between basic blocks in vreg_1 virtual registers and are lowered by this
// pass.
//
// The only instructions that use or define vreg_1 virtual registers are COPY,
// PHI, and IMPLICIT_DEF.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "si-i1-copies"

using namespace llvm;

static unsigned createLaneMaskReg(MachineFunction &MF);
static unsigned insertUndefLaneMask(MachineBasicBlock &MBB);

namespace {

class SILowerI1Copies : public MachineFunctionPass {
public:
  static char ID;

private:
  MachineFunction *MF = nullptr;
  MachineDominatorTree *DT = nullptr;
  MachinePostDominatorTree *PDT = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const GCNSubtarget *ST = nullptr;
  const SIInstrInfo *TII = nullptr;

  DenseSet<unsigned> ConstrainRegs;

public:
  SILowerI1Copies() : MachineFunctionPass(ID) {
    initializeSILowerI1CopiesPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Lower i1 Copies"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachinePostDominatorTree>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  void lowerCopiesFromI1();
  void lowerPhis();
  void lowerCopiesToI1();
  bool isConstantLaneMask(unsigned Reg, bool &Val) const;
  void buildMergeLaneMasks(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I, const DebugLoc &DL,
                           unsigned DstReg, unsigned PrevReg, unsigned CurReg);
  MachineBasicBlock::iterator
  getSaluInsertionAtEnd(MachineBasicBlock &MBB) const;

  bool isLaneMaskReg(unsigned Reg) const {
    return TII->getRegisterInfo().isSGPRReg(*MRI, Reg) &&
           TII->getRegisterInfo().getRegSizeInBits(Reg, *MRI) ==
               ST->getWavefrontSize();
  }
};

/// Helper class that determines the relationship between incoming values of a
/// phi in the control flow graph to determine where an incoming value can
/// simply be taken as a scalar lane mask as-is, and where it needs to be
/// merged with another, previously defined lane mask.
///
/// The approach is as follows:
///  - Determine all basic blocks which, starting from the incoming blocks,
///    a wave may reach before entering the def block (the block containing the
///    phi).
///  - If an incoming block has no predecessors in this set, we can take the
///    incoming value as a scalar lane mask as-is.
///  -- A special case of this is when the def block has a self-loop.
///  - Otherwise, the incoming value needs to be merged with a previously
///    defined lane mask.
///  - If there is a path into the set of reachable blocks that does _not_ go
///    through an incoming block where we can take the scalar lane mask as-is,
///    we need to invent an available value for the SSAUpdater. Choices are
///    0 and undef, with differing consequences for how to merge values etc.
///
/// TODO: We could use region analysis to quickly skip over SESE regions during
///       the traversal.
///
class PhiIncomingAnalysis {
  MachinePostDominatorTree &PDT;

  // For each reachable basic block, whether it is a source in the induced
  // subgraph of the CFG.
  DenseMap<MachineBasicBlock *, bool> ReachableMap;
  SmallVector<MachineBasicBlock *, 4> ReachableOrdered;
  SmallVector<MachineBasicBlock *, 4> Stack;
  SmallVector<MachineBasicBlock *, 4> Predecessors;

public:
  PhiIncomingAnalysis(MachinePostDominatorTree &PDT) : PDT(PDT) {}

  /// Returns whether \p MBB is a source in the induced subgraph of reachable
  /// blocks.
  bool isSource(MachineBasicBlock &MBB) const {
    return ReachableMap.find(&MBB)->second;
  }

  ArrayRef<MachineBasicBlock *> predecessors() const { return Predecessors; }

  void analyze(MachineBasicBlock &DefBlock,
               ArrayRef<MachineBasicBlock *> IncomingBlocks) {
    assert(Stack.empty());
    ReachableMap.clear();
    ReachableOrdered.clear();
    Predecessors.clear();

    // Insert the def block first, so that it acts as an end point for the
    // traversal.
    ReachableMap.try_emplace(&DefBlock, false);
    ReachableOrdered.push_back(&DefBlock);

    for (MachineBasicBlock *MBB : IncomingBlocks) {
      if (MBB == &DefBlock) {
        ReachableMap[&DefBlock] = true; // self-loop on DefBlock
        continue;
      }

      ReachableMap.try_emplace(MBB, false);
      ReachableOrdered.push_back(MBB);

      // If this block has a divergent terminator and the def block is its
      // post-dominator, the wave may first visit the other successors.
      bool Divergent = false;
      for (MachineInstr &MI : MBB->terminators()) {
        if (MI.getOpcode() == AMDGPU::SI_NON_UNIFORM_BRCOND_PSEUDO ||
            MI.getOpcode() == AMDGPU::SI_IF ||
            MI.getOpcode() == AMDGPU::SI_ELSE ||
            MI.getOpcode() == AMDGPU::SI_LOOP) {
          Divergent = true;
          break;
        }
      }

      if (Divergent && PDT.dominates(&DefBlock, MBB)) {
        for (MachineBasicBlock *Succ : MBB->successors())
          Stack.push_back(Succ);
      }
    }

    while (!Stack.empty()) {
      MachineBasicBlock *MBB = Stack.pop_back_val();
      if (!ReachableMap.try_emplace(MBB, false).second)
        continue;
      ReachableOrdered.push_back(MBB);

      for (MachineBasicBlock *Succ : MBB->successors())
        Stack.push_back(Succ);
    }

    for (MachineBasicBlock *MBB : ReachableOrdered) {
      bool HaveReachablePred = false;
      for (MachineBasicBlock *Pred : MBB->predecessors()) {
        if (ReachableMap.count(Pred)) {
          HaveReachablePred = true;
        } else {
          Stack.push_back(Pred);
        }
      }
      if (!HaveReachablePred)
        ReachableMap[MBB] = true;
      if (HaveReachablePred) {
        for (MachineBasicBlock *UnreachablePred : Stack) {
          if (llvm::find(Predecessors, UnreachablePred) == Predecessors.end())
            Predecessors.push_back(UnreachablePred);
        }
      }
      Stack.clear();
    }
  }
};

/// Helper class that detects loops which require us to lower an i1 COPY into
/// bitwise manipulation.
///
/// Unfortunately, we cannot use LoopInfo because LoopInfo does not distinguish
/// between loops with the same header. Consider this example:
///
///  A-+-+
///  | | |
///  B-+ |
///  |   |
///  C---+
///
/// A is the header of a loop containing A, B, and C as far as LoopInfo is
/// concerned. However, an i1 COPY in B that is used in C must be lowered to
/// bitwise operations to combine results from different loop iterations when
/// B has a divergent branch (since by default we will compile this code such
/// that threads in a wave are merged at the entry of C).
///
/// The following rule is implemented to determine whether bitwise operations
/// are required: use the bitwise lowering for a def in block B if a backward
/// edge to B is reachable without going through the nearest common
/// post-dominator of B and all uses of the def.
///
/// TODO: This rule is conservative because it does not check whether the
///       relevant branches are actually divergent.
///
/// The class is designed to cache the CFG traversal so that it can be re-used
/// for multiple defs within the same basic block.
///
/// TODO: We could use region analysis to quickly skip over SESE regions during
///       the traversal.
///
class LoopFinder {
  MachineDominatorTree &DT;
  MachinePostDominatorTree &PDT;

  // All visited / reachable block, tagged by level (level 0 is the def block,
  // level 1 are all blocks reachable including but not going through the def
  // block's IPDOM, etc.).
  DenseMap<MachineBasicBlock *, unsigned> Visited;

  // Nearest common dominator of all visited blocks by level (level 0 is the
  // def block). Used for seeding the SSAUpdater.
  SmallVector<MachineBasicBlock *, 4> CommonDominators;

  // Post-dominator of all visited blocks.
  MachineBasicBlock *VisitedPostDom = nullptr;

  // Level at which a loop was found: 0 is not possible; 1 = a backward edge is
  // reachable without going through the IPDOM of the def block (if the IPDOM
  // itself has an edge to the def block, the loop level is 2), etc.
  unsigned FoundLoopLevel = ~0u;

  MachineBasicBlock *DefBlock = nullptr;
  SmallVector<MachineBasicBlock *, 4> Stack;
  SmallVector<MachineBasicBlock *, 4> NextLevel;

public:
  LoopFinder(MachineDominatorTree &DT, MachinePostDominatorTree &PDT)
      : DT(DT), PDT(PDT) {}

  void initialize(MachineBasicBlock &MBB) {
    Visited.clear();
    CommonDominators.clear();
    Stack.clear();
    NextLevel.clear();
    VisitedPostDom = nullptr;
    FoundLoopLevel = ~0u;

    DefBlock = &MBB;
  }

  /// Check whether a backward edge can be reached without going through the
  /// given \p PostDom of the def block.
  ///
  /// Return the level of \p PostDom if a loop was found, or 0 otherwise.
  unsigned findLoop(MachineBasicBlock *PostDom) {
    MachineDomTreeNode *PDNode = PDT.getNode(DefBlock);

    if (!VisitedPostDom)
      advanceLevel();

    unsigned Level = 0;
    while (PDNode->getBlock() != PostDom) {
      if (PDNode->getBlock() == VisitedPostDom)
        advanceLevel();
      PDNode = PDNode->getIDom();
      Level++;
      if (FoundLoopLevel == Level)
        return Level;
    }

    return 0;
  }

  /// Add undef values dominating the loop and the optionally given additional
  /// blocks, so that the SSA updater doesn't have to search all the way to the
  /// function entry.
  void addLoopEntries(unsigned LoopLevel, MachineSSAUpdater &SSAUpdater,
                      ArrayRef<MachineBasicBlock *> Blocks = {}) {
    assert(LoopLevel < CommonDominators.size());

    MachineBasicBlock *Dom = CommonDominators[LoopLevel];
    for (MachineBasicBlock *MBB : Blocks)
      Dom = DT.findNearestCommonDominator(Dom, MBB);

    if (!inLoopLevel(*Dom, LoopLevel, Blocks)) {
      SSAUpdater.AddAvailableValue(Dom, insertUndefLaneMask(*Dom));
    } else {
      // The dominator is part of the loop or the given blocks, so add the
      // undef value to unreachable predecessors instead.
      for (MachineBasicBlock *Pred : Dom->predecessors()) {
        if (!inLoopLevel(*Pred, LoopLevel, Blocks))
          SSAUpdater.AddAvailableValue(Pred, insertUndefLaneMask(*Pred));
      }
    }
  }

private:
  bool inLoopLevel(MachineBasicBlock &MBB, unsigned LoopLevel,
                   ArrayRef<MachineBasicBlock *> Blocks) const {
    auto DomIt = Visited.find(&MBB);
    if (DomIt != Visited.end() && DomIt->second <= LoopLevel)
      return true;

    if (llvm::find(Blocks, &MBB) != Blocks.end())
      return true;

    return false;
  }

  void advanceLevel() {
    MachineBasicBlock *VisitedDom;

    if (!VisitedPostDom) {
      VisitedPostDom = DefBlock;
      VisitedDom = DefBlock;
      Stack.push_back(DefBlock);
    } else {
      VisitedPostDom = PDT.getNode(VisitedPostDom)->getIDom()->getBlock();
      VisitedDom = CommonDominators.back();

      for (unsigned i = 0; i < NextLevel.size();) {
        if (PDT.dominates(VisitedPostDom, NextLevel[i])) {
          Stack.push_back(NextLevel[i]);

          NextLevel[i] = NextLevel.back();
          NextLevel.pop_back();
        } else {
          i++;
        }
      }
    }

    unsigned Level = CommonDominators.size();
    while (!Stack.empty()) {
      MachineBasicBlock *MBB = Stack.pop_back_val();
      if (!PDT.dominates(VisitedPostDom, MBB))
        NextLevel.push_back(MBB);

      Visited[MBB] = Level;
      VisitedDom = DT.findNearestCommonDominator(VisitedDom, MBB);

      for (MachineBasicBlock *Succ : MBB->successors()) {
        if (Succ == DefBlock) {
          if (MBB == VisitedPostDom)
            FoundLoopLevel = std::min(FoundLoopLevel, Level + 1);
          else
            FoundLoopLevel = std::min(FoundLoopLevel, Level);
          continue;
        }

        if (Visited.try_emplace(Succ, ~0u).second) {
          if (MBB == VisitedPostDom)
            NextLevel.push_back(Succ);
          else
            Stack.push_back(Succ);
        }
      }
    }

    CommonDominators.push_back(VisitedDom);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SILowerI1Copies, DEBUG_TYPE, "SI Lower i1 Copies", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_END(SILowerI1Copies, DEBUG_TYPE, "SI Lower i1 Copies", false,
                    false)

char SILowerI1Copies::ID = 0;

char &llvm::SILowerI1CopiesID = SILowerI1Copies::ID;

FunctionPass *llvm::createSILowerI1CopiesPass() {
  return new SILowerI1Copies();
}

static unsigned createLaneMaskReg(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  return MRI.createVirtualRegister(&AMDGPU::SReg_64RegClass);
}

static unsigned insertUndefLaneMask(MachineBasicBlock &MBB) {
  MachineFunction &MF = *MBB.getParent();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  unsigned UndefReg = createLaneMaskReg(MF);
  BuildMI(MBB, MBB.getFirstTerminator(), {}, TII->get(AMDGPU::IMPLICIT_DEF),
          UndefReg);
  return UndefReg;
}

/// Lower all instructions that def or use vreg_1 registers.
///
/// In a first pass, we lower COPYs from vreg_1 to vector registers, as can
/// occur around inline assembly. We do this first, before vreg_1 registers
/// are changed to scalar mask registers.
///
/// Then we lower all defs of vreg_1 registers. Phi nodes are lowered before
/// all others, because phi lowering looks through copies and can therefore
/// often make copy lowering unnecessary.
bool SILowerI1Copies::runOnMachineFunction(MachineFunction &TheMF) {
  MF = &TheMF;
  MRI = &MF->getRegInfo();
  DT = &getAnalysis<MachineDominatorTree>();
  PDT = &getAnalysis<MachinePostDominatorTree>();

  ST = &MF->getSubtarget<GCNSubtarget>();
  TII = ST->getInstrInfo();

  lowerCopiesFromI1();
  lowerPhis();
  lowerCopiesToI1();

  for (unsigned Reg : ConstrainRegs)
    MRI->constrainRegClass(Reg, &AMDGPU::SReg_64_XEXECRegClass);
  ConstrainRegs.clear();

  return true;
}

void SILowerI1Copies::lowerCopiesFromI1() {
  SmallVector<MachineInstr *, 4> DeadCopies;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != AMDGPU::COPY)
        continue;

      unsigned DstReg = MI.getOperand(0).getReg();
      unsigned SrcReg = MI.getOperand(1).getReg();
      if (!TargetRegisterInfo::isVirtualRegister(SrcReg) ||
          MRI->getRegClass(SrcReg) != &AMDGPU::VReg_1RegClass)
        continue;

      if (isLaneMaskReg(DstReg) ||
          (TargetRegisterInfo::isVirtualRegister(DstReg) &&
           MRI->getRegClass(DstReg) == &AMDGPU::VReg_1RegClass))
        continue;

      // Copy into a 32-bit vector register.
      LLVM_DEBUG(dbgs() << "Lower copy from i1: " << MI);
      DebugLoc DL = MI.getDebugLoc();

      assert(TII->getRegisterInfo().getRegSizeInBits(DstReg, *MRI) == 32);
      assert(!MI.getOperand(0).getSubReg());

      ConstrainRegs.insert(SrcReg);
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
          .addImm(0)
          .addImm(-1)
          .addReg(SrcReg);
      DeadCopies.push_back(&MI);
    }

    for (MachineInstr *MI : DeadCopies)
      MI->eraseFromParent();
    DeadCopies.clear();
  }
}

void SILowerI1Copies::lowerPhis() {
  MachineSSAUpdater SSAUpdater(*MF);
  LoopFinder LF(*DT, *PDT);
  PhiIncomingAnalysis PIA(*PDT);
  SmallVector<MachineInstr *, 4> DeadPhis;
  SmallVector<MachineBasicBlock *, 4> IncomingBlocks;
  SmallVector<unsigned, 4> IncomingRegs;
  SmallVector<unsigned, 4> IncomingUpdated;

  for (MachineBasicBlock &MBB : *MF) {
    LF.initialize(MBB);

    for (MachineInstr &MI : MBB.phis()) {
      unsigned DstReg = MI.getOperand(0).getReg();
      if (MRI->getRegClass(DstReg) != &AMDGPU::VReg_1RegClass)
        continue;

      LLVM_DEBUG(dbgs() << "Lower PHI: " << MI);

      MRI->setRegClass(DstReg, &AMDGPU::SReg_64RegClass);

      // Collect incoming values.
      for (unsigned i = 1; i < MI.getNumOperands(); i += 2) {
        assert(i + 1 < MI.getNumOperands());
        unsigned IncomingReg = MI.getOperand(i).getReg();
        MachineBasicBlock *IncomingMBB = MI.getOperand(i + 1).getMBB();
        MachineInstr *IncomingDef = MRI->getUniqueVRegDef(IncomingReg);

        if (IncomingDef->getOpcode() == AMDGPU::COPY) {
          IncomingReg = IncomingDef->getOperand(1).getReg();
          assert(isLaneMaskReg(IncomingReg));
          assert(!IncomingDef->getOperand(1).getSubReg());
        } else if (IncomingDef->getOpcode() == AMDGPU::IMPLICIT_DEF) {
          continue;
        } else {
          assert(IncomingDef->isPHI());
        }

        IncomingBlocks.push_back(IncomingMBB);
        IncomingRegs.push_back(IncomingReg);
      }

      // Phis in a loop that are observed outside the loop receive a simple but
      // conservatively correct treatment.
      MachineBasicBlock *PostDomBound = &MBB;
      for (MachineInstr &Use : MRI->use_instructions(DstReg)) {
        PostDomBound =
            PDT->findNearestCommonDominator(PostDomBound, Use.getParent());
      }

      unsigned FoundLoopLevel = LF.findLoop(PostDomBound);

      SSAUpdater.Initialize(DstReg);

      if (FoundLoopLevel) {
        LF.addLoopEntries(FoundLoopLevel, SSAUpdater, IncomingBlocks);

        for (unsigned i = 0; i < IncomingRegs.size(); ++i) {
          IncomingUpdated.push_back(createLaneMaskReg(*MF));
          SSAUpdater.AddAvailableValue(IncomingBlocks[i],
                                       IncomingUpdated.back());
        }

        for (unsigned i = 0; i < IncomingRegs.size(); ++i) {
          MachineBasicBlock &IMBB = *IncomingBlocks[i];
          buildMergeLaneMasks(
              IMBB, getSaluInsertionAtEnd(IMBB), {}, IncomingUpdated[i],
              SSAUpdater.GetValueInMiddleOfBlock(&IMBB), IncomingRegs[i]);
        }
      } else {
        // The phi is not observed from outside a loop. Use a more accurate
        // lowering.
        PIA.analyze(MBB, IncomingBlocks);

        for (MachineBasicBlock *MBB : PIA.predecessors())
          SSAUpdater.AddAvailableValue(MBB, insertUndefLaneMask(*MBB));

        for (unsigned i = 0; i < IncomingRegs.size(); ++i) {
          MachineBasicBlock &IMBB = *IncomingBlocks[i];
          if (PIA.isSource(IMBB)) {
            IncomingUpdated.push_back(0);
            SSAUpdater.AddAvailableValue(&IMBB, IncomingRegs[i]);
          } else {
            IncomingUpdated.push_back(createLaneMaskReg(*MF));
            SSAUpdater.AddAvailableValue(&IMBB, IncomingUpdated.back());
          }
        }

        for (unsigned i = 0; i < IncomingRegs.size(); ++i) {
          if (!IncomingUpdated[i])
            continue;

          MachineBasicBlock &IMBB = *IncomingBlocks[i];
          buildMergeLaneMasks(
              IMBB, getSaluInsertionAtEnd(IMBB), {}, IncomingUpdated[i],
              SSAUpdater.GetValueInMiddleOfBlock(&IMBB), IncomingRegs[i]);
        }
      }

      unsigned NewReg = SSAUpdater.GetValueInMiddleOfBlock(&MBB);
      if (NewReg != DstReg) {
        MRI->replaceRegWith(NewReg, DstReg);

        // Ensure that DstReg has a single def and mark the old PHI node for
        // deletion.
        MI.getOperand(0).setReg(NewReg);
        DeadPhis.push_back(&MI);
      }

      IncomingBlocks.clear();
      IncomingRegs.clear();
      IncomingUpdated.clear();
    }

    for (MachineInstr *MI : DeadPhis)
      MI->eraseFromParent();
    DeadPhis.clear();
  }
}

void SILowerI1Copies::lowerCopiesToI1() {
  MachineSSAUpdater SSAUpdater(*MF);
  LoopFinder LF(*DT, *PDT);
  SmallVector<MachineInstr *, 4> DeadCopies;

  for (MachineBasicBlock &MBB : *MF) {
    LF.initialize(MBB);

    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != AMDGPU::IMPLICIT_DEF &&
          MI.getOpcode() != AMDGPU::COPY)
        continue;

      unsigned DstReg = MI.getOperand(0).getReg();
      if (!TargetRegisterInfo::isVirtualRegister(DstReg) ||
          MRI->getRegClass(DstReg) != &AMDGPU::VReg_1RegClass)
        continue;

      if (MRI->use_empty(DstReg)) {
        DeadCopies.push_back(&MI);
        continue;
      }

      LLVM_DEBUG(dbgs() << "Lower Other: " << MI);

      MRI->setRegClass(DstReg, &AMDGPU::SReg_64RegClass);
      if (MI.getOpcode() == AMDGPU::IMPLICIT_DEF)
        continue;

      DebugLoc DL = MI.getDebugLoc();
      unsigned SrcReg = MI.getOperand(1).getReg();
      assert(!MI.getOperand(1).getSubReg());

      if (!TargetRegisterInfo::isVirtualRegister(SrcReg) ||
          !isLaneMaskReg(SrcReg)) {
        assert(TII->getRegisterInfo().getRegSizeInBits(SrcReg, *MRI) == 32);
        unsigned TmpReg = createLaneMaskReg(*MF);
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_CMP_NE_U32_e64), TmpReg)
            .addReg(SrcReg)
            .addImm(0);
        MI.getOperand(1).setReg(TmpReg);
        SrcReg = TmpReg;
      }

      // Defs in a loop that are observed outside the loop must be transformed
      // into appropriate bit manipulation.
      MachineBasicBlock *PostDomBound = &MBB;
      for (MachineInstr &Use : MRI->use_instructions(DstReg)) {
        PostDomBound =
            PDT->findNearestCommonDominator(PostDomBound, Use.getParent());
      }

      unsigned FoundLoopLevel = LF.findLoop(PostDomBound);
      if (FoundLoopLevel) {
        SSAUpdater.Initialize(DstReg);
        SSAUpdater.AddAvailableValue(&MBB, DstReg);
        LF.addLoopEntries(FoundLoopLevel, SSAUpdater);

        buildMergeLaneMasks(MBB, MI, DL, DstReg,
                            SSAUpdater.GetValueInMiddleOfBlock(&MBB), SrcReg);
        DeadCopies.push_back(&MI);
      }
    }

    for (MachineInstr *MI : DeadCopies)
      MI->eraseFromParent();
    DeadCopies.clear();
  }
}

bool SILowerI1Copies::isConstantLaneMask(unsigned Reg, bool &Val) const {
  const MachineInstr *MI;
  for (;;) {
    MI = MRI->getUniqueVRegDef(Reg);
    if (MI->getOpcode() != AMDGPU::COPY)
      break;

    Reg = MI->getOperand(1).getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      return false;
    if (!isLaneMaskReg(Reg))
      return false;
  }

  if (MI->getOpcode() != AMDGPU::S_MOV_B64)
    return false;

  if (!MI->getOperand(1).isImm())
    return false;

  int64_t Imm = MI->getOperand(1).getImm();
  if (Imm == 0) {
    Val = false;
    return true;
  }
  if (Imm == -1) {
    Val = true;
    return true;
  }

  return false;
}

static void instrDefsUsesSCC(const MachineInstr &MI, bool &Def, bool &Use) {
  Def = false;
  Use = false;

  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.getReg() == AMDGPU::SCC) {
      if (MO.isUse())
        Use = true;
      else
        Def = true;
    }
  }
}

/// Return a point at the end of the given \p MBB to insert SALU instructions
/// for lane mask calculation. Take terminators and SCC into account.
MachineBasicBlock::iterator
SILowerI1Copies::getSaluInsertionAtEnd(MachineBasicBlock &MBB) const {
  auto InsertionPt = MBB.getFirstTerminator();
  bool TerminatorsUseSCC = false;
  for (auto I = InsertionPt, E = MBB.end(); I != E; ++I) {
    bool DefsSCC;
    instrDefsUsesSCC(*I, DefsSCC, TerminatorsUseSCC);
    if (TerminatorsUseSCC || DefsSCC)
      break;
  }

  if (!TerminatorsUseSCC)
    return InsertionPt;

  while (InsertionPt != MBB.begin()) {
    InsertionPt--;

    bool DefSCC, UseSCC;
    instrDefsUsesSCC(*InsertionPt, DefSCC, UseSCC);
    if (DefSCC)
      return InsertionPt;
  }

  // We should have at least seen an IMPLICIT_DEF or COPY
  llvm_unreachable("SCC used by terminator but no def in block");
}

void SILowerI1Copies::buildMergeLaneMasks(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          const DebugLoc &DL, unsigned DstReg,
                                          unsigned PrevReg, unsigned CurReg) {
  bool PrevVal;
  bool PrevConstant = isConstantLaneMask(PrevReg, PrevVal);
  bool CurVal;
  bool CurConstant = isConstantLaneMask(CurReg, CurVal);

  if (PrevConstant && CurConstant) {
    if (PrevVal == CurVal) {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg).addReg(CurReg);
    } else if (CurVal) {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg).addReg(AMDGPU::EXEC);
    } else {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_XOR_B64), DstReg)
          .addReg(AMDGPU::EXEC)
          .addImm(-1);
    }
    return;
  }

  unsigned PrevMaskedReg = 0;
  unsigned CurMaskedReg = 0;
  if (!PrevConstant) {
    if (CurConstant && CurVal) {
      PrevMaskedReg = PrevReg;
    } else {
      PrevMaskedReg = createLaneMaskReg(*MF);
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ANDN2_B64), PrevMaskedReg)
          .addReg(PrevReg)
          .addReg(AMDGPU::EXEC);
    }
  }
  if (!CurConstant) {
    // TODO: check whether CurReg is already masked by EXEC
    if (PrevConstant && PrevVal) {
      CurMaskedReg = CurReg;
    } else {
      CurMaskedReg = createLaneMaskReg(*MF);
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_AND_B64), CurMaskedReg)
          .addReg(CurReg)
          .addReg(AMDGPU::EXEC);
    }
  }

  if (PrevConstant && !PrevVal) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg)
        .addReg(CurMaskedReg);
  } else if (CurConstant && !CurVal) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg)
        .addReg(PrevMaskedReg);
  } else if (PrevConstant && PrevVal) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ORN2_B64), DstReg)
        .addReg(CurMaskedReg)
        .addReg(AMDGPU::EXEC);
  } else {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_OR_B64), DstReg)
        .addReg(PrevMaskedReg)
        .addReg(CurMaskedReg ? CurMaskedReg : (unsigned)AMDGPU::EXEC);
  }
}
