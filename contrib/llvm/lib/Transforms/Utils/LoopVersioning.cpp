//===- LoopVersioning.cpp - Utility to version a loop ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a utility class to perform loop versioning.  The versioned
// loop speculates that otherwise may-aliasing memory accesses don't overlap and
// emits checks to prove this.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LoopVersioning.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

static cl::opt<bool>
    AnnotateNoAlias("loop-version-annotate-no-alias", cl::init(true),
                    cl::Hidden,
                    cl::desc("Add no-alias annotation for instructions that "
                             "are disambiguated by memchecks"));

LoopVersioning::LoopVersioning(const LoopAccessInfo &LAI, Loop *L, LoopInfo *LI,
                               DominatorTree *DT, ScalarEvolution *SE,
                               bool UseLAIChecks)
    : VersionedLoop(L), NonVersionedLoop(nullptr), LAI(LAI), LI(LI), DT(DT),
      SE(SE) {
  assert(L->getExitBlock() && "No single exit block");
  assert(L->isLoopSimplifyForm() && "Loop is not in loop-simplify form");
  if (UseLAIChecks) {
    setAliasChecks(LAI.getRuntimePointerChecking()->getChecks());
    setSCEVChecks(LAI.getPSE().getUnionPredicate());
  }
}

void LoopVersioning::setAliasChecks(
    SmallVector<RuntimePointerChecking::PointerCheck, 4> Checks) {
  AliasChecks = std::move(Checks);
}

void LoopVersioning::setSCEVChecks(SCEVUnionPredicate Check) {
  Preds = std::move(Check);
}

void LoopVersioning::versionLoop(
    const SmallVectorImpl<Instruction *> &DefsUsedOutside) {
  Instruction *FirstCheckInst;
  Instruction *MemRuntimeCheck;
  Value *SCEVRuntimeCheck;
  Value *RuntimeCheck = nullptr;

  // Add the memcheck in the original preheader (this is empty initially).
  BasicBlock *RuntimeCheckBB = VersionedLoop->getLoopPreheader();
  std::tie(FirstCheckInst, MemRuntimeCheck) =
      LAI.addRuntimeChecks(RuntimeCheckBB->getTerminator(), AliasChecks);

  const SCEVUnionPredicate &Pred = LAI.getPSE().getUnionPredicate();
  SCEVExpander Exp(*SE, RuntimeCheckBB->getModule()->getDataLayout(),
                   "scev.check");
  SCEVRuntimeCheck =
      Exp.expandCodeForPredicate(&Pred, RuntimeCheckBB->getTerminator());
  auto *CI = dyn_cast<ConstantInt>(SCEVRuntimeCheck);

  // Discard the SCEV runtime check if it is always true.
  if (CI && CI->isZero())
    SCEVRuntimeCheck = nullptr;

  if (MemRuntimeCheck && SCEVRuntimeCheck) {
    RuntimeCheck = BinaryOperator::Create(Instruction::Or, MemRuntimeCheck,
                                          SCEVRuntimeCheck, "lver.safe");
    if (auto *I = dyn_cast<Instruction>(RuntimeCheck))
      I->insertBefore(RuntimeCheckBB->getTerminator());
  } else
    RuntimeCheck = MemRuntimeCheck ? MemRuntimeCheck : SCEVRuntimeCheck;

  assert(RuntimeCheck && "called even though we don't need "
                         "any runtime checks");

  // Rename the block to make the IR more readable.
  RuntimeCheckBB->setName(VersionedLoop->getHeader()->getName() +
                          ".lver.check");

  // Create empty preheader for the loop (and after cloning for the
  // non-versioned loop).
  BasicBlock *PH =
      SplitBlock(RuntimeCheckBB, RuntimeCheckBB->getTerminator(), DT, LI);
  PH->setName(VersionedLoop->getHeader()->getName() + ".ph");

  // Clone the loop including the preheader.
  //
  // FIXME: This does not currently preserve SimplifyLoop because the exit
  // block is a join between the two loops.
  SmallVector<BasicBlock *, 8> NonVersionedLoopBlocks;
  NonVersionedLoop =
      cloneLoopWithPreheader(PH, RuntimeCheckBB, VersionedLoop, VMap,
                             ".lver.orig", LI, DT, NonVersionedLoopBlocks);
  remapInstructionsInBlocks(NonVersionedLoopBlocks, VMap);

  // Insert the conditional branch based on the result of the memchecks.
  Instruction *OrigTerm = RuntimeCheckBB->getTerminator();
  BranchInst::Create(NonVersionedLoop->getLoopPreheader(),
                     VersionedLoop->getLoopPreheader(), RuntimeCheck, OrigTerm);
  OrigTerm->eraseFromParent();

  // The loops merge in the original exit block.  This is now dominated by the
  // memchecking block.
  DT->changeImmediateDominator(VersionedLoop->getExitBlock(), RuntimeCheckBB);

  // Adds the necessary PHI nodes for the versioned loops based on the
  // loop-defined values used outside of the loop.
  addPHINodes(DefsUsedOutside);
}

void LoopVersioning::addPHINodes(
    const SmallVectorImpl<Instruction *> &DefsUsedOutside) {
  BasicBlock *PHIBlock = VersionedLoop->getExitBlock();
  assert(PHIBlock && "No single successor to loop exit block");
  PHINode *PN;

  // First add a single-operand PHI for each DefsUsedOutside if one does not
  // exists yet.
  for (auto *Inst : DefsUsedOutside) {
    // See if we have a single-operand PHI with the value defined by the
    // original loop.
    for (auto I = PHIBlock->begin(); (PN = dyn_cast<PHINode>(I)); ++I) {
      if (PN->getIncomingValue(0) == Inst)
        break;
    }
    // If not create it.
    if (!PN) {
      PN = PHINode::Create(Inst->getType(), 2, Inst->getName() + ".lver",
                           &PHIBlock->front());
      SmallVector<User*, 8> UsersToUpdate;
      for (User *U : Inst->users())
        if (!VersionedLoop->contains(cast<Instruction>(U)->getParent()))
          UsersToUpdate.push_back(U);
      for (User *U : UsersToUpdate)
        U->replaceUsesOfWith(Inst, PN);
      PN->addIncoming(Inst, VersionedLoop->getExitingBlock());
    }
  }

  // Then for each PHI add the operand for the edge from the cloned loop.
  for (auto I = PHIBlock->begin(); (PN = dyn_cast<PHINode>(I)); ++I) {
    assert(PN->getNumOperands() == 1 &&
           "Exit block should only have on predecessor");

    // If the definition was cloned used that otherwise use the same value.
    Value *ClonedValue = PN->getIncomingValue(0);
    auto Mapped = VMap.find(ClonedValue);
    if (Mapped != VMap.end())
      ClonedValue = Mapped->second;

    PN->addIncoming(ClonedValue, NonVersionedLoop->getExitingBlock());
  }
}

void LoopVersioning::prepareNoAliasMetadata() {
  // We need to turn the no-alias relation between pointer checking groups into
  // no-aliasing annotations between instructions.
  //
  // We accomplish this by mapping each pointer checking group (a set of
  // pointers memchecked together) to an alias scope and then also mapping each
  // group to the list of scopes it can't alias.

  const RuntimePointerChecking *RtPtrChecking = LAI.getRuntimePointerChecking();
  LLVMContext &Context = VersionedLoop->getHeader()->getContext();

  // First allocate an aliasing scope for each pointer checking group.
  //
  // While traversing through the checking groups in the loop, also create a
  // reverse map from pointers to the pointer checking group they were assigned
  // to.
  MDBuilder MDB(Context);
  MDNode *Domain = MDB.createAnonymousAliasScopeDomain("LVerDomain");

  for (const auto &Group : RtPtrChecking->CheckingGroups) {
    GroupToScope[&Group] = MDB.createAnonymousAliasScope(Domain);

    for (unsigned PtrIdx : Group.Members)
      PtrToGroup[RtPtrChecking->getPointerInfo(PtrIdx).PointerValue] = &Group;
  }

  // Go through the checks and for each pointer group, collect the scopes for
  // each non-aliasing pointer group.
  DenseMap<const RuntimePointerChecking::CheckingPtrGroup *,
           SmallVector<Metadata *, 4>>
      GroupToNonAliasingScopes;

  for (const auto &Check : AliasChecks)
    GroupToNonAliasingScopes[Check.first].push_back(GroupToScope[Check.second]);

  // Finally, transform the above to actually map to scope list which is what
  // the metadata uses.

  for (auto Pair : GroupToNonAliasingScopes)
    GroupToNonAliasingScopeList[Pair.first] = MDNode::get(Context, Pair.second);
}

void LoopVersioning::annotateLoopWithNoAlias() {
  if (!AnnotateNoAlias)
    return;

  // First prepare the maps.
  prepareNoAliasMetadata();

  // Add the scope and no-alias metadata to the instructions.
  for (Instruction *I : LAI.getDepChecker().getMemoryInstructions()) {
    annotateInstWithNoAlias(I);
  }
}

void LoopVersioning::annotateInstWithNoAlias(Instruction *VersionedInst,
                                             const Instruction *OrigInst) {
  if (!AnnotateNoAlias)
    return;

  LLVMContext &Context = VersionedLoop->getHeader()->getContext();
  const Value *Ptr = isa<LoadInst>(OrigInst)
                         ? cast<LoadInst>(OrigInst)->getPointerOperand()
                         : cast<StoreInst>(OrigInst)->getPointerOperand();

  // Find the group for the pointer and then add the scope metadata.
  auto Group = PtrToGroup.find(Ptr);
  if (Group != PtrToGroup.end()) {
    VersionedInst->setMetadata(
        LLVMContext::MD_alias_scope,
        MDNode::concatenate(
            VersionedInst->getMetadata(LLVMContext::MD_alias_scope),
            MDNode::get(Context, GroupToScope[Group->second])));

    // Add the no-alias metadata.
    auto NonAliasingScopeList = GroupToNonAliasingScopeList.find(Group->second);
    if (NonAliasingScopeList != GroupToNonAliasingScopeList.end())
      VersionedInst->setMetadata(
          LLVMContext::MD_noalias,
          MDNode::concatenate(
              VersionedInst->getMetadata(LLVMContext::MD_noalias),
              NonAliasingScopeList->second));
  }
}

namespace {
/// Also expose this is a pass.  Currently this is only used for
/// unit-testing.  It adds all memchecks necessary to remove all may-aliasing
/// array accesses from the loop.
class LoopVersioningPass : public FunctionPass {
public:
  LoopVersioningPass() : FunctionPass(ID) {
    initializeLoopVersioningPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto *LAA = &getAnalysis<LoopAccessLegacyAnalysis>();
    auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();

    // Build up a worklist of inner-loops to version. This is necessary as the
    // act of versioning a loop creates new loops and can invalidate iterators
    // across the loops.
    SmallVector<Loop *, 8> Worklist;

    for (Loop *TopLevelLoop : *LI)
      for (Loop *L : depth_first(TopLevelLoop))
        // We only handle inner-most loops.
        if (L->empty())
          Worklist.push_back(L);

    // Now walk the identified inner loops.
    bool Changed = false;
    for (Loop *L : Worklist) {
      const LoopAccessInfo &LAI = LAA->getInfo(L);
      if (L->isLoopSimplifyForm() && (LAI.getNumRuntimePointerChecks() ||
          !LAI.getPSE().getUnionPredicate().isAlwaysTrue())) {
        LoopVersioning LVer(LAI, L, LI, DT, SE);
        LVer.versionLoop();
        LVer.annotateLoopWithNoAlias();
        Changed = true;
      }
    }

    return Changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<LoopAccessLegacyAnalysis>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
  }

  static char ID;
};
}

#define LVER_OPTION "loop-versioning"
#define DEBUG_TYPE LVER_OPTION

char LoopVersioningPass::ID;
static const char LVer_name[] = "Loop Versioning";

INITIALIZE_PASS_BEGIN(LoopVersioningPass, LVER_OPTION, LVer_name, false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopAccessLegacyAnalysis)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(LoopVersioningPass, LVER_OPTION, LVer_name, false, false)

namespace llvm {
FunctionPass *createLoopVersioningPass() {
  return new LoopVersioningPass();
}
}
