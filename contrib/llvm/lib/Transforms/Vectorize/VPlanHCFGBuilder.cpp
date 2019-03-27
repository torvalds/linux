//===-- VPlanHCFGBuilder.cpp ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the construction of a VPlan-based Hierarchical CFG
/// (H-CFG) for an incoming IR. This construction comprises the following
/// components and steps:
//
/// 1. PlainCFGBuilder class: builds a plain VPBasicBlock-based CFG that
/// faithfully represents the CFG in the incoming IR. A VPRegionBlock (Top
/// Region) is created to enclose and serve as parent of all the VPBasicBlocks
/// in the plain CFG.
/// NOTE: At this point, there is a direct correspondence between all the
/// VPBasicBlocks created for the initial plain CFG and the incoming
/// BasicBlocks. However, this might change in the future.
///
//===----------------------------------------------------------------------===//

#include "VPlanHCFGBuilder.h"
#include "LoopVectorizationPlanner.h"
#include "llvm/Analysis/LoopIterator.h"

#define DEBUG_TYPE "loop-vectorize"

using namespace llvm;

namespace {
// Class that is used to build the plain CFG for the incoming IR.
class PlainCFGBuilder {
private:
  // The outermost loop of the input loop nest considered for vectorization.
  Loop *TheLoop;

  // Loop Info analysis.
  LoopInfo *LI;

  // Vectorization plan that we are working on.
  VPlan &Plan;

  // Output Top Region.
  VPRegionBlock *TopRegion = nullptr;

  // Builder of the VPlan instruction-level representation.
  VPBuilder VPIRBuilder;

  // NOTE: The following maps are intentionally destroyed after the plain CFG
  // construction because subsequent VPlan-to-VPlan transformation may
  // invalidate them.
  // Map incoming BasicBlocks to their newly-created VPBasicBlocks.
  DenseMap<BasicBlock *, VPBasicBlock *> BB2VPBB;
  // Map incoming Value definitions to their newly-created VPValues.
  DenseMap<Value *, VPValue *> IRDef2VPValue;

  // Hold phi node's that need to be fixed once the plain CFG has been built.
  SmallVector<PHINode *, 8> PhisToFix;

  // Utility functions.
  void setVPBBPredsFromBB(VPBasicBlock *VPBB, BasicBlock *BB);
  void fixPhiNodes();
  VPBasicBlock *getOrCreateVPBB(BasicBlock *BB);
  bool isExternalDef(Value *Val);
  VPValue *getOrCreateVPOperand(Value *IRVal);
  void createVPInstructionsForVPBB(VPBasicBlock *VPBB, BasicBlock *BB);

public:
  PlainCFGBuilder(Loop *Lp, LoopInfo *LI, VPlan &P)
      : TheLoop(Lp), LI(LI), Plan(P) {}

  // Build the plain CFG and return its Top Region.
  VPRegionBlock *buildPlainCFG();
};
} // anonymous namespace

// Set predecessors of \p VPBB in the same order as they are in \p BB. \p VPBB
// must have no predecessors.
void PlainCFGBuilder::setVPBBPredsFromBB(VPBasicBlock *VPBB, BasicBlock *BB) {
  SmallVector<VPBlockBase *, 8> VPBBPreds;
  // Collect VPBB predecessors.
  for (BasicBlock *Pred : predecessors(BB))
    VPBBPreds.push_back(getOrCreateVPBB(Pred));

  VPBB->setPredecessors(VPBBPreds);
}

// Add operands to VPInstructions representing phi nodes from the input IR.
void PlainCFGBuilder::fixPhiNodes() {
  for (auto *Phi : PhisToFix) {
    assert(IRDef2VPValue.count(Phi) && "Missing VPInstruction for PHINode.");
    VPValue *VPVal = IRDef2VPValue[Phi];
    assert(isa<VPInstruction>(VPVal) && "Expected VPInstruction for phi node.");
    auto *VPPhi = cast<VPInstruction>(VPVal);
    assert(VPPhi->getNumOperands() == 0 &&
           "Expected VPInstruction with no operands.");

    for (Value *Op : Phi->operands())
      VPPhi->addOperand(getOrCreateVPOperand(Op));
  }
}

// Create a new empty VPBasicBlock for an incoming BasicBlock or retrieve an
// existing one if it was already created.
VPBasicBlock *PlainCFGBuilder::getOrCreateVPBB(BasicBlock *BB) {
  auto BlockIt = BB2VPBB.find(BB);
  if (BlockIt != BB2VPBB.end())
    // Retrieve existing VPBB.
    return BlockIt->second;

  // Create new VPBB.
  LLVM_DEBUG(dbgs() << "Creating VPBasicBlock for " << BB->getName() << "\n");
  VPBasicBlock *VPBB = new VPBasicBlock(BB->getName());
  BB2VPBB[BB] = VPBB;
  VPBB->setParent(TopRegion);
  return VPBB;
}

// Return true if \p Val is considered an external definition. An external
// definition is either:
// 1. A Value that is not an Instruction. This will be refined in the future.
// 2. An Instruction that is outside of the CFG snippet represented in VPlan,
// i.e., is not part of: a) the loop nest, b) outermost loop PH and, c)
// outermost loop exits.
bool PlainCFGBuilder::isExternalDef(Value *Val) {
  // All the Values that are not Instructions are considered external
  // definitions for now.
  Instruction *Inst = dyn_cast<Instruction>(Val);
  if (!Inst)
    return true;

  BasicBlock *InstParent = Inst->getParent();
  assert(InstParent && "Expected instruction parent.");

  // Check whether Instruction definition is in loop PH.
  BasicBlock *PH = TheLoop->getLoopPreheader();
  assert(PH && "Expected loop pre-header.");

  if (InstParent == PH)
    // Instruction definition is in outermost loop PH.
    return false;

  // Check whether Instruction definition is in the loop exit.
  BasicBlock *Exit = TheLoop->getUniqueExitBlock();
  assert(Exit && "Expected loop with single exit.");
  if (InstParent == Exit) {
    // Instruction definition is in outermost loop exit.
    return false;
  }

  // Check whether Instruction definition is in loop body.
  return !TheLoop->contains(Inst);
}

// Create a new VPValue or retrieve an existing one for the Instruction's
// operand \p IRVal. This function must only be used to create/retrieve VPValues
// for *Instruction's operands* and not to create regular VPInstruction's. For
// the latter, please, look at 'createVPInstructionsForVPBB'.
VPValue *PlainCFGBuilder::getOrCreateVPOperand(Value *IRVal) {
  auto VPValIt = IRDef2VPValue.find(IRVal);
  if (VPValIt != IRDef2VPValue.end())
    // Operand has an associated VPInstruction or VPValue that was previously
    // created.
    return VPValIt->second;

  // Operand doesn't have a previously created VPInstruction/VPValue. This
  // means that operand is:
  //   A) a definition external to VPlan,
  //   B) any other Value without specific representation in VPlan.
  // For now, we use VPValue to represent A and B and classify both as external
  // definitions. We may introduce specific VPValue subclasses for them in the
  // future.
  assert(isExternalDef(IRVal) && "Expected external definition as operand.");

  // A and B: Create VPValue and add it to the pool of external definitions and
  // to the Value->VPValue map.
  VPValue *NewVPVal = new VPValue(IRVal);
  Plan.addExternalDef(NewVPVal);
  IRDef2VPValue[IRVal] = NewVPVal;
  return NewVPVal;
}

// Create new VPInstructions in a VPBasicBlock, given its BasicBlock
// counterpart. This function must be invoked in RPO so that the operands of a
// VPInstruction in \p BB have been visited before (except for Phi nodes).
void PlainCFGBuilder::createVPInstructionsForVPBB(VPBasicBlock *VPBB,
                                                  BasicBlock *BB) {
  VPIRBuilder.setInsertPoint(VPBB);
  for (Instruction &InstRef : *BB) {
    Instruction *Inst = &InstRef;

    // There shouldn't be any VPValue for Inst at this point. Otherwise, we
    // visited Inst when we shouldn't, breaking the RPO traversal order.
    assert(!IRDef2VPValue.count(Inst) &&
           "Instruction shouldn't have been visited.");

    if (auto *Br = dyn_cast<BranchInst>(Inst)) {
      // Branch instruction is not explicitly represented in VPlan but we need
      // to represent its condition bit when it's conditional.
      if (Br->isConditional())
        getOrCreateVPOperand(Br->getCondition());

      // Skip the rest of the Instruction processing for Branch instructions.
      continue;
    }

    VPInstruction *NewVPInst;
    if (auto *Phi = dyn_cast<PHINode>(Inst)) {
      // Phi node's operands may have not been visited at this point. We create
      // an empty VPInstruction that we will fix once the whole plain CFG has
      // been built.
      NewVPInst = cast<VPInstruction>(VPIRBuilder.createNaryOp(
          Inst->getOpcode(), {} /*No operands*/, Inst));
      PhisToFix.push_back(Phi);
    } else {
      // Translate LLVM-IR operands into VPValue operands and set them in the
      // new VPInstruction.
      SmallVector<VPValue *, 4> VPOperands;
      for (Value *Op : Inst->operands())
        VPOperands.push_back(getOrCreateVPOperand(Op));

      // Build VPInstruction for any arbitraty Instruction without specific
      // representation in VPlan.
      NewVPInst = cast<VPInstruction>(
          VPIRBuilder.createNaryOp(Inst->getOpcode(), VPOperands, Inst));
    }

    IRDef2VPValue[Inst] = NewVPInst;
  }
}

// Main interface to build the plain CFG.
VPRegionBlock *PlainCFGBuilder::buildPlainCFG() {
  // 1. Create the Top Region. It will be the parent of all VPBBs.
  TopRegion = new VPRegionBlock("TopRegion", false /*isReplicator*/);

  // 2. Scan the body of the loop in a topological order to visit each basic
  // block after having visited its predecessor basic blocks. Create a VPBB for
  // each BB and link it to its successor and predecessor VPBBs. Note that
  // predecessors must be set in the same order as they are in the incomming IR.
  // Otherwise, there might be problems with existing phi nodes and algorithm
  // based on predecessors traversal.

  // Loop PH needs to be explicitly visited since it's not taken into account by
  // LoopBlocksDFS.
  BasicBlock *PreheaderBB = TheLoop->getLoopPreheader();
  assert((PreheaderBB->getTerminator()->getNumSuccessors() == 1) &&
         "Unexpected loop preheader");
  VPBasicBlock *PreheaderVPBB = getOrCreateVPBB(PreheaderBB);
  createVPInstructionsForVPBB(PreheaderVPBB, PreheaderBB);
  // Create empty VPBB for Loop H so that we can link PH->H.
  VPBlockBase *HeaderVPBB = getOrCreateVPBB(TheLoop->getHeader());
  // Preheader's predecessors will be set during the loop RPO traversal below.
  PreheaderVPBB->setOneSuccessor(HeaderVPBB);

  LoopBlocksRPO RPO(TheLoop);
  RPO.perform(LI);

  for (BasicBlock *BB : RPO) {
    // Create or retrieve the VPBasicBlock for this BB and create its
    // VPInstructions.
    VPBasicBlock *VPBB = getOrCreateVPBB(BB);
    createVPInstructionsForVPBB(VPBB, BB);

    // Set VPBB successors. We create empty VPBBs for successors if they don't
    // exist already. Recipes will be created when the successor is visited
    // during the RPO traversal.
    Instruction *TI = BB->getTerminator();
    assert(TI && "Terminator expected.");
    unsigned NumSuccs = TI->getNumSuccessors();

    if (NumSuccs == 1) {
      VPBasicBlock *SuccVPBB = getOrCreateVPBB(TI->getSuccessor(0));
      assert(SuccVPBB && "VPBB Successor not found.");
      VPBB->setOneSuccessor(SuccVPBB);
    } else if (NumSuccs == 2) {
      VPBasicBlock *SuccVPBB0 = getOrCreateVPBB(TI->getSuccessor(0));
      assert(SuccVPBB0 && "Successor 0 not found.");
      VPBasicBlock *SuccVPBB1 = getOrCreateVPBB(TI->getSuccessor(1));
      assert(SuccVPBB1 && "Successor 1 not found.");

      // Get VPBB's condition bit.
      assert(isa<BranchInst>(TI) && "Unsupported terminator!");
      auto *Br = cast<BranchInst>(TI);
      Value *BrCond = Br->getCondition();
      // Look up the branch condition to get the corresponding VPValue
      // representing the condition bit in VPlan (which may be in another VPBB).
      assert(IRDef2VPValue.count(BrCond) &&
             "Missing condition bit in IRDef2VPValue!");
      VPValue *VPCondBit = IRDef2VPValue[BrCond];

      // Link successors using condition bit.
      VPBB->setTwoSuccessors(SuccVPBB0, SuccVPBB1, VPCondBit);
    } else
      llvm_unreachable("Number of successors not supported.");

    // Set VPBB predecessors in the same order as they are in the incoming BB.
    setVPBBPredsFromBB(VPBB, BB);
  }

  // 3. Process outermost loop exit. We created an empty VPBB for the loop
  // single exit BB during the RPO traversal of the loop body but Instructions
  // weren't visited because it's not part of the the loop.
  BasicBlock *LoopExitBB = TheLoop->getUniqueExitBlock();
  assert(LoopExitBB && "Loops with multiple exits are not supported.");
  VPBasicBlock *LoopExitVPBB = BB2VPBB[LoopExitBB];
  createVPInstructionsForVPBB(LoopExitVPBB, LoopExitBB);
  // Loop exit was already set as successor of the loop exiting BB.
  // We only set its predecessor VPBB now.
  setVPBBPredsFromBB(LoopExitVPBB, LoopExitBB);

  // 4. The whole CFG has been built at this point so all the input Values must
  // have a VPlan couterpart. Fix VPlan phi nodes by adding their corresponding
  // VPlan operands.
  fixPhiNodes();

  // 5. Final Top Region setup. Set outermost loop pre-header and single exit as
  // Top Region entry and exit.
  TopRegion->setEntry(PreheaderVPBB);
  TopRegion->setExit(LoopExitVPBB);
  return TopRegion;
}

VPRegionBlock *VPlanHCFGBuilder::buildPlainCFG() {
  PlainCFGBuilder PCFGBuilder(TheLoop, LI, Plan);
  return PCFGBuilder.buildPlainCFG();
}

// Public interface to build a H-CFG.
void VPlanHCFGBuilder::buildHierarchicalCFG() {
  // Build Top Region enclosing the plain CFG and set it as VPlan entry.
  VPRegionBlock *TopRegion = buildPlainCFG();
  Plan.setEntry(TopRegion);
  LLVM_DEBUG(Plan.setName("HCFGBuilder: Plain CFG\n"); dbgs() << Plan);

  Verifier.verifyHierarchicalCFG(TopRegion);

  // Compute plain CFG dom tree for VPLInfo.
  VPDomTree.recalculate(*TopRegion);
  LLVM_DEBUG(dbgs() << "Dominator Tree after building the plain CFG.\n";
             VPDomTree.print(dbgs()));

  // Compute VPLInfo and keep it in Plan.
  VPLoopInfo &VPLInfo = Plan.getVPLoopInfo();
  VPLInfo.analyze(VPDomTree);
  LLVM_DEBUG(dbgs() << "VPLoop Info After buildPlainCFG:\n";
             VPLInfo.print(dbgs()));
}
