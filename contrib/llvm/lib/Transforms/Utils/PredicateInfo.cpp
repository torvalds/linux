//===-- PredicateInfo.cpp - PredicateInfo Builder--------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------===//
//
// This file implements the PredicateInfo class.
//
//===----------------------------------------------------------------===//

#include "llvm/Transforms/Utils/PredicateInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Transforms/Utils.h"
#include <algorithm>
#define DEBUG_TYPE "predicateinfo"
using namespace llvm;
using namespace PatternMatch;
using namespace llvm::PredicateInfoClasses;

INITIALIZE_PASS_BEGIN(PredicateInfoPrinterLegacyPass, "print-predicateinfo",
                      "PredicateInfo Printer", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_END(PredicateInfoPrinterLegacyPass, "print-predicateinfo",
                    "PredicateInfo Printer", false, false)
static cl::opt<bool> VerifyPredicateInfo(
    "verify-predicateinfo", cl::init(false), cl::Hidden,
    cl::desc("Verify PredicateInfo in legacy printer pass."));
DEBUG_COUNTER(RenameCounter, "predicateinfo-rename",
              "Controls which variables are renamed with predicateinfo");

namespace {
// Given a predicate info that is a type of branching terminator, get the
// branching block.
const BasicBlock *getBranchBlock(const PredicateBase *PB) {
  assert(isa<PredicateWithEdge>(PB) &&
         "Only branches and switches should have PHIOnly defs that "
         "require branch blocks.");
  return cast<PredicateWithEdge>(PB)->From;
}

// Given a predicate info that is a type of branching terminator, get the
// branching terminator.
static Instruction *getBranchTerminator(const PredicateBase *PB) {
  assert(isa<PredicateWithEdge>(PB) &&
         "Not a predicate info type we know how to get a terminator from.");
  return cast<PredicateWithEdge>(PB)->From->getTerminator();
}

// Given a predicate info that is a type of branching terminator, get the
// edge this predicate info represents
const std::pair<BasicBlock *, BasicBlock *>
getBlockEdge(const PredicateBase *PB) {
  assert(isa<PredicateWithEdge>(PB) &&
         "Not a predicate info type we know how to get an edge from.");
  const auto *PEdge = cast<PredicateWithEdge>(PB);
  return std::make_pair(PEdge->From, PEdge->To);
}
}

namespace llvm {
namespace PredicateInfoClasses {
enum LocalNum {
  // Operations that must appear first in the block.
  LN_First,
  // Operations that are somewhere in the middle of the block, and are sorted on
  // demand.
  LN_Middle,
  // Operations that must appear last in a block, like successor phi node uses.
  LN_Last
};

// Associate global and local DFS info with defs and uses, so we can sort them
// into a global domination ordering.
struct ValueDFS {
  int DFSIn = 0;
  int DFSOut = 0;
  unsigned int LocalNum = LN_Middle;
  // Only one of Def or Use will be set.
  Value *Def = nullptr;
  Use *U = nullptr;
  // Neither PInfo nor EdgeOnly participate in the ordering
  PredicateBase *PInfo = nullptr;
  bool EdgeOnly = false;
};

// Perform a strict weak ordering on instructions and arguments.
static bool valueComesBefore(OrderedInstructions &OI, const Value *A,
                             const Value *B) {
  auto *ArgA = dyn_cast_or_null<Argument>(A);
  auto *ArgB = dyn_cast_or_null<Argument>(B);
  if (ArgA && !ArgB)
    return true;
  if (ArgB && !ArgA)
    return false;
  if (ArgA && ArgB)
    return ArgA->getArgNo() < ArgB->getArgNo();
  return OI.dfsBefore(cast<Instruction>(A), cast<Instruction>(B));
}

// This compares ValueDFS structures, creating OrderedBasicBlocks where
// necessary to compare uses/defs in the same block.  Doing so allows us to walk
// the minimum number of instructions necessary to compute our def/use ordering.
struct ValueDFS_Compare {
  OrderedInstructions &OI;
  ValueDFS_Compare(OrderedInstructions &OI) : OI(OI) {}

  bool operator()(const ValueDFS &A, const ValueDFS &B) const {
    if (&A == &B)
      return false;
    // The only case we can't directly compare them is when they in the same
    // block, and both have localnum == middle.  In that case, we have to use
    // comesbefore to see what the real ordering is, because they are in the
    // same basic block.

    bool SameBlock = std::tie(A.DFSIn, A.DFSOut) == std::tie(B.DFSIn, B.DFSOut);

    // We want to put the def that will get used for a given set of phi uses,
    // before those phi uses.
    // So we sort by edge, then by def.
    // Note that only phi nodes uses and defs can come last.
    if (SameBlock && A.LocalNum == LN_Last && B.LocalNum == LN_Last)
      return comparePHIRelated(A, B);

    if (!SameBlock || A.LocalNum != LN_Middle || B.LocalNum != LN_Middle)
      return std::tie(A.DFSIn, A.DFSOut, A.LocalNum, A.Def, A.U) <
             std::tie(B.DFSIn, B.DFSOut, B.LocalNum, B.Def, B.U);
    return localComesBefore(A, B);
  }

  // For a phi use, or a non-materialized def, return the edge it represents.
  const std::pair<BasicBlock *, BasicBlock *>
  getBlockEdge(const ValueDFS &VD) const {
    if (!VD.Def && VD.U) {
      auto *PHI = cast<PHINode>(VD.U->getUser());
      return std::make_pair(PHI->getIncomingBlock(*VD.U), PHI->getParent());
    }
    // This is really a non-materialized def.
    return ::getBlockEdge(VD.PInfo);
  }

  // For two phi related values, return the ordering.
  bool comparePHIRelated(const ValueDFS &A, const ValueDFS &B) const {
    auto &ABlockEdge = getBlockEdge(A);
    auto &BBlockEdge = getBlockEdge(B);
    // Now sort by block edge and then defs before uses.
    return std::tie(ABlockEdge, A.Def, A.U) < std::tie(BBlockEdge, B.Def, B.U);
  }

  // Get the definition of an instruction that occurs in the middle of a block.
  Value *getMiddleDef(const ValueDFS &VD) const {
    if (VD.Def)
      return VD.Def;
    // It's possible for the defs and uses to be null.  For branches, the local
    // numbering will say the placed predicaeinfos should go first (IE
    // LN_beginning), so we won't be in this function. For assumes, we will end
    // up here, beause we need to order the def we will place relative to the
    // assume.  So for the purpose of ordering, we pretend the def is the assume
    // because that is where we will insert the info.
    if (!VD.U) {
      assert(VD.PInfo &&
             "No def, no use, and no predicateinfo should not occur");
      assert(isa<PredicateAssume>(VD.PInfo) &&
             "Middle of block should only occur for assumes");
      return cast<PredicateAssume>(VD.PInfo)->AssumeInst;
    }
    return nullptr;
  }

  // Return either the Def, if it's not null, or the user of the Use, if the def
  // is null.
  const Instruction *getDefOrUser(const Value *Def, const Use *U) const {
    if (Def)
      return cast<Instruction>(Def);
    return cast<Instruction>(U->getUser());
  }

  // This performs the necessary local basic block ordering checks to tell
  // whether A comes before B, where both are in the same basic block.
  bool localComesBefore(const ValueDFS &A, const ValueDFS &B) const {
    auto *ADef = getMiddleDef(A);
    auto *BDef = getMiddleDef(B);

    // See if we have real values or uses. If we have real values, we are
    // guaranteed they are instructions or arguments. No matter what, we are
    // guaranteed they are in the same block if they are instructions.
    auto *ArgA = dyn_cast_or_null<Argument>(ADef);
    auto *ArgB = dyn_cast_or_null<Argument>(BDef);

    if (ArgA || ArgB)
      return valueComesBefore(OI, ArgA, ArgB);

    auto *AInst = getDefOrUser(ADef, A.U);
    auto *BInst = getDefOrUser(BDef, B.U);
    return valueComesBefore(OI, AInst, BInst);
  }
};

} // namespace PredicateInfoClasses

bool PredicateInfo::stackIsInScope(const ValueDFSStack &Stack,
                                   const ValueDFS &VDUse) const {
  if (Stack.empty())
    return false;
  // If it's a phi only use, make sure it's for this phi node edge, and that the
  // use is in a phi node.  If it's anything else, and the top of the stack is
  // EdgeOnly, we need to pop the stack.  We deliberately sort phi uses next to
  // the defs they must go with so that we can know it's time to pop the stack
  // when we hit the end of the phi uses for a given def.
  if (Stack.back().EdgeOnly) {
    if (!VDUse.U)
      return false;
    auto *PHI = dyn_cast<PHINode>(VDUse.U->getUser());
    if (!PHI)
      return false;
    // Check edge
    BasicBlock *EdgePred = PHI->getIncomingBlock(*VDUse.U);
    if (EdgePred != getBranchBlock(Stack.back().PInfo))
      return false;

    // Use dominates, which knows how to handle edge dominance.
    return DT.dominates(getBlockEdge(Stack.back().PInfo), *VDUse.U);
  }

  return (VDUse.DFSIn >= Stack.back().DFSIn &&
          VDUse.DFSOut <= Stack.back().DFSOut);
}

void PredicateInfo::popStackUntilDFSScope(ValueDFSStack &Stack,
                                          const ValueDFS &VD) {
  while (!Stack.empty() && !stackIsInScope(Stack, VD))
    Stack.pop_back();
}

// Convert the uses of Op into a vector of uses, associating global and local
// DFS info with each one.
void PredicateInfo::convertUsesToDFSOrdered(
    Value *Op, SmallVectorImpl<ValueDFS> &DFSOrderedSet) {
  for (auto &U : Op->uses()) {
    if (auto *I = dyn_cast<Instruction>(U.getUser())) {
      ValueDFS VD;
      // Put the phi node uses in the incoming block.
      BasicBlock *IBlock;
      if (auto *PN = dyn_cast<PHINode>(I)) {
        IBlock = PN->getIncomingBlock(U);
        // Make phi node users appear last in the incoming block
        // they are from.
        VD.LocalNum = LN_Last;
      } else {
        // If it's not a phi node use, it is somewhere in the middle of the
        // block.
        IBlock = I->getParent();
        VD.LocalNum = LN_Middle;
      }
      DomTreeNode *DomNode = DT.getNode(IBlock);
      // It's possible our use is in an unreachable block. Skip it if so.
      if (!DomNode)
        continue;
      VD.DFSIn = DomNode->getDFSNumIn();
      VD.DFSOut = DomNode->getDFSNumOut();
      VD.U = &U;
      DFSOrderedSet.push_back(VD);
    }
  }
}

// Collect relevant operations from Comparison that we may want to insert copies
// for.
void collectCmpOps(CmpInst *Comparison, SmallVectorImpl<Value *> &CmpOperands) {
  auto *Op0 = Comparison->getOperand(0);
  auto *Op1 = Comparison->getOperand(1);
  if (Op0 == Op1)
    return;
  CmpOperands.push_back(Comparison);
  // Only want real values, not constants.  Additionally, operands with one use
  // are only being used in the comparison, which means they will not be useful
  // for us to consider for predicateinfo.
  //
  if ((isa<Instruction>(Op0) || isa<Argument>(Op0)) && !Op0->hasOneUse())
    CmpOperands.push_back(Op0);
  if ((isa<Instruction>(Op1) || isa<Argument>(Op1)) && !Op1->hasOneUse())
    CmpOperands.push_back(Op1);
}

// Add Op, PB to the list of value infos for Op, and mark Op to be renamed.
void PredicateInfo::addInfoFor(SmallPtrSetImpl<Value *> &OpsToRename, Value *Op,
                               PredicateBase *PB) {
  OpsToRename.insert(Op);
  auto &OperandInfo = getOrCreateValueInfo(Op);
  AllInfos.push_back(PB);
  OperandInfo.Infos.push_back(PB);
}

// Process an assume instruction and place relevant operations we want to rename
// into OpsToRename.
void PredicateInfo::processAssume(IntrinsicInst *II, BasicBlock *AssumeBB,
                                  SmallPtrSetImpl<Value *> &OpsToRename) {
  // See if we have a comparison we support
  SmallVector<Value *, 8> CmpOperands;
  SmallVector<Value *, 2> ConditionsToProcess;
  CmpInst::Predicate Pred;
  Value *Operand = II->getOperand(0);
  if (m_c_And(m_Cmp(Pred, m_Value(), m_Value()),
              m_Cmp(Pred, m_Value(), m_Value()))
          .match(II->getOperand(0))) {
    ConditionsToProcess.push_back(cast<BinaryOperator>(Operand)->getOperand(0));
    ConditionsToProcess.push_back(cast<BinaryOperator>(Operand)->getOperand(1));
    ConditionsToProcess.push_back(Operand);
  } else if (isa<CmpInst>(Operand)) {

    ConditionsToProcess.push_back(Operand);
  }
  for (auto Cond : ConditionsToProcess) {
    if (auto *Cmp = dyn_cast<CmpInst>(Cond)) {
      collectCmpOps(Cmp, CmpOperands);
      // Now add our copy infos for our operands
      for (auto *Op : CmpOperands) {
        auto *PA = new PredicateAssume(Op, II, Cmp);
        addInfoFor(OpsToRename, Op, PA);
      }
      CmpOperands.clear();
    } else if (auto *BinOp = dyn_cast<BinaryOperator>(Cond)) {
      // Otherwise, it should be an AND.
      assert(BinOp->getOpcode() == Instruction::And &&
             "Should have been an AND");
      auto *PA = new PredicateAssume(BinOp, II, BinOp);
      addInfoFor(OpsToRename, BinOp, PA);
    } else {
      llvm_unreachable("Unknown type of condition");
    }
  }
}

// Process a block terminating branch, and place relevant operations to be
// renamed into OpsToRename.
void PredicateInfo::processBranch(BranchInst *BI, BasicBlock *BranchBB,
                                  SmallPtrSetImpl<Value *> &OpsToRename) {
  BasicBlock *FirstBB = BI->getSuccessor(0);
  BasicBlock *SecondBB = BI->getSuccessor(1);
  SmallVector<BasicBlock *, 2> SuccsToProcess;
  SuccsToProcess.push_back(FirstBB);
  SuccsToProcess.push_back(SecondBB);
  SmallVector<Value *, 2> ConditionsToProcess;

  auto InsertHelper = [&](Value *Op, bool isAnd, bool isOr, Value *Cond) {
    for (auto *Succ : SuccsToProcess) {
      // Don't try to insert on a self-edge. This is mainly because we will
      // eliminate during renaming anyway.
      if (Succ == BranchBB)
        continue;
      bool TakenEdge = (Succ == FirstBB);
      // For and, only insert on the true edge
      // For or, only insert on the false edge
      if ((isAnd && !TakenEdge) || (isOr && TakenEdge))
        continue;
      PredicateBase *PB =
          new PredicateBranch(Op, BranchBB, Succ, Cond, TakenEdge);
      addInfoFor(OpsToRename, Op, PB);
      if (!Succ->getSinglePredecessor())
        EdgeUsesOnly.insert({BranchBB, Succ});
    }
  };

  // Match combinations of conditions.
  CmpInst::Predicate Pred;
  bool isAnd = false;
  bool isOr = false;
  SmallVector<Value *, 8> CmpOperands;
  if (match(BI->getCondition(), m_And(m_Cmp(Pred, m_Value(), m_Value()),
                                      m_Cmp(Pred, m_Value(), m_Value()))) ||
      match(BI->getCondition(), m_Or(m_Cmp(Pred, m_Value(), m_Value()),
                                     m_Cmp(Pred, m_Value(), m_Value())))) {
    auto *BinOp = cast<BinaryOperator>(BI->getCondition());
    if (BinOp->getOpcode() == Instruction::And)
      isAnd = true;
    else if (BinOp->getOpcode() == Instruction::Or)
      isOr = true;
    ConditionsToProcess.push_back(BinOp->getOperand(0));
    ConditionsToProcess.push_back(BinOp->getOperand(1));
    ConditionsToProcess.push_back(BI->getCondition());
  } else if (isa<CmpInst>(BI->getCondition())) {
    ConditionsToProcess.push_back(BI->getCondition());
  }
  for (auto Cond : ConditionsToProcess) {
    if (auto *Cmp = dyn_cast<CmpInst>(Cond)) {
      collectCmpOps(Cmp, CmpOperands);
      // Now add our copy infos for our operands
      for (auto *Op : CmpOperands)
        InsertHelper(Op, isAnd, isOr, Cmp);
    } else if (auto *BinOp = dyn_cast<BinaryOperator>(Cond)) {
      // This must be an AND or an OR.
      assert((BinOp->getOpcode() == Instruction::And ||
              BinOp->getOpcode() == Instruction::Or) &&
             "Should have been an AND or an OR");
      // The actual value of the binop is not subject to the same restrictions
      // as the comparison. It's either true or false on the true/false branch.
      InsertHelper(BinOp, false, false, BinOp);
    } else {
      llvm_unreachable("Unknown type of condition");
    }
    CmpOperands.clear();
  }
}
// Process a block terminating switch, and place relevant operations to be
// renamed into OpsToRename.
void PredicateInfo::processSwitch(SwitchInst *SI, BasicBlock *BranchBB,
                                  SmallPtrSetImpl<Value *> &OpsToRename) {
  Value *Op = SI->getCondition();
  if ((!isa<Instruction>(Op) && !isa<Argument>(Op)) || Op->hasOneUse())
    return;

  // Remember how many outgoing edges there are to every successor.
  SmallDenseMap<BasicBlock *, unsigned, 16> SwitchEdges;
  for (unsigned i = 0, e = SI->getNumSuccessors(); i != e; ++i) {
    BasicBlock *TargetBlock = SI->getSuccessor(i);
    ++SwitchEdges[TargetBlock];
  }

  // Now propagate info for each case value
  for (auto C : SI->cases()) {
    BasicBlock *TargetBlock = C.getCaseSuccessor();
    if (SwitchEdges.lookup(TargetBlock) == 1) {
      PredicateSwitch *PS = new PredicateSwitch(
          Op, SI->getParent(), TargetBlock, C.getCaseValue(), SI);
      addInfoFor(OpsToRename, Op, PS);
      if (!TargetBlock->getSinglePredecessor())
        EdgeUsesOnly.insert({BranchBB, TargetBlock});
    }
  }
}

// Build predicate info for our function
void PredicateInfo::buildPredicateInfo() {
  DT.updateDFSNumbers();
  // Collect operands to rename from all conditional branch terminators, as well
  // as assume statements.
  SmallPtrSet<Value *, 8> OpsToRename;
  for (auto DTN : depth_first(DT.getRootNode())) {
    BasicBlock *BranchBB = DTN->getBlock();
    if (auto *BI = dyn_cast<BranchInst>(BranchBB->getTerminator())) {
      if (!BI->isConditional())
        continue;
      // Can't insert conditional information if they all go to the same place.
      if (BI->getSuccessor(0) == BI->getSuccessor(1))
        continue;
      processBranch(BI, BranchBB, OpsToRename);
    } else if (auto *SI = dyn_cast<SwitchInst>(BranchBB->getTerminator())) {
      processSwitch(SI, BranchBB, OpsToRename);
    }
  }
  for (auto &Assume : AC.assumptions()) {
    if (auto *II = dyn_cast_or_null<IntrinsicInst>(Assume))
      processAssume(II, II->getParent(), OpsToRename);
  }
  // Now rename all our operations.
  renameUses(OpsToRename);
}

// Create a ssa_copy declaration with custom mangling, because
// Intrinsic::getDeclaration does not handle overloaded unnamed types properly:
// all unnamed types get mangled to the same string. We use the pointer
// to the type as name here, as it guarantees unique names for different
// types and we remove the declarations when destroying PredicateInfo.
// It is a workaround for PR38117, because solving it in a fully general way is
// tricky (FIXME).
static Function *getCopyDeclaration(Module *M, Type *Ty) {
  std::string Name = "llvm.ssa.copy." + utostr((uintptr_t) Ty);
  return cast<Function>(M->getOrInsertFunction(
      Name, getType(M->getContext(), Intrinsic::ssa_copy, Ty)));
}

// Given the renaming stack, make all the operands currently on the stack real
// by inserting them into the IR.  Return the last operation's value.
Value *PredicateInfo::materializeStack(unsigned int &Counter,
                                       ValueDFSStack &RenameStack,
                                       Value *OrigOp) {
  // Find the first thing we have to materialize
  auto RevIter = RenameStack.rbegin();
  for (; RevIter != RenameStack.rend(); ++RevIter)
    if (RevIter->Def)
      break;

  size_t Start = RevIter - RenameStack.rbegin();
  // The maximum number of things we should be trying to materialize at once
  // right now is 4, depending on if we had an assume, a branch, and both used
  // and of conditions.
  for (auto RenameIter = RenameStack.end() - Start;
       RenameIter != RenameStack.end(); ++RenameIter) {
    auto *Op =
        RenameIter == RenameStack.begin() ? OrigOp : (RenameIter - 1)->Def;
    ValueDFS &Result = *RenameIter;
    auto *ValInfo = Result.PInfo;
    // For edge predicates, we can just place the operand in the block before
    // the terminator.  For assume, we have to place it right before the assume
    // to ensure we dominate all of our uses.  Always insert right before the
    // relevant instruction (terminator, assume), so that we insert in proper
    // order in the case of multiple predicateinfo in the same block.
    if (isa<PredicateWithEdge>(ValInfo)) {
      IRBuilder<> B(getBranchTerminator(ValInfo));
      Function *IF = getCopyDeclaration(F.getParent(), Op->getType());
      if (empty(IF->users()))
        CreatedDeclarations.insert(IF);
      CallInst *PIC =
          B.CreateCall(IF, Op, Op->getName() + "." + Twine(Counter++));
      PredicateMap.insert({PIC, ValInfo});
      Result.Def = PIC;
    } else {
      auto *PAssume = dyn_cast<PredicateAssume>(ValInfo);
      assert(PAssume &&
             "Should not have gotten here without it being an assume");
      IRBuilder<> B(PAssume->AssumeInst);
      Function *IF = getCopyDeclaration(F.getParent(), Op->getType());
      if (empty(IF->users()))
        CreatedDeclarations.insert(IF);
      CallInst *PIC = B.CreateCall(IF, Op);
      PredicateMap.insert({PIC, ValInfo});
      Result.Def = PIC;
    }
  }
  return RenameStack.back().Def;
}

// Instead of the standard SSA renaming algorithm, which is O(Number of
// instructions), and walks the entire dominator tree, we walk only the defs +
// uses.  The standard SSA renaming algorithm does not really rely on the
// dominator tree except to order the stack push/pops of the renaming stacks, so
// that defs end up getting pushed before hitting the correct uses.  This does
// not require the dominator tree, only the *order* of the dominator tree. The
// complete and correct ordering of the defs and uses, in dominator tree is
// contained in the DFS numbering of the dominator tree. So we sort the defs and
// uses into the DFS ordering, and then just use the renaming stack as per
// normal, pushing when we hit a def (which is a predicateinfo instruction),
// popping when we are out of the dfs scope for that def, and replacing any uses
// with top of stack if it exists.  In order to handle liveness without
// propagating liveness info, we don't actually insert the predicateinfo
// instruction def until we see a use that it would dominate.  Once we see such
// a use, we materialize the predicateinfo instruction in the right place and
// use it.
//
// TODO: Use this algorithm to perform fast single-variable renaming in
// promotememtoreg and memoryssa.
void PredicateInfo::renameUses(SmallPtrSetImpl<Value *> &OpSet) {
  // Sort OpsToRename since we are going to iterate it.
  SmallVector<Value *, 8> OpsToRename(OpSet.begin(), OpSet.end());
  auto Comparator = [&](const Value *A, const Value *B) {
    return valueComesBefore(OI, A, B);
  };
  llvm::sort(OpsToRename, Comparator);
  ValueDFS_Compare Compare(OI);
  // Compute liveness, and rename in O(uses) per Op.
  for (auto *Op : OpsToRename) {
    LLVM_DEBUG(dbgs() << "Visiting " << *Op << "\n");
    unsigned Counter = 0;
    SmallVector<ValueDFS, 16> OrderedUses;
    const auto &ValueInfo = getValueInfo(Op);
    // Insert the possible copies into the def/use list.
    // They will become real copies if we find a real use for them, and never
    // created otherwise.
    for (auto &PossibleCopy : ValueInfo.Infos) {
      ValueDFS VD;
      // Determine where we are going to place the copy by the copy type.
      // The predicate info for branches always come first, they will get
      // materialized in the split block at the top of the block.
      // The predicate info for assumes will be somewhere in the middle,
      // it will get materialized in front of the assume.
      if (const auto *PAssume = dyn_cast<PredicateAssume>(PossibleCopy)) {
        VD.LocalNum = LN_Middle;
        DomTreeNode *DomNode = DT.getNode(PAssume->AssumeInst->getParent());
        if (!DomNode)
          continue;
        VD.DFSIn = DomNode->getDFSNumIn();
        VD.DFSOut = DomNode->getDFSNumOut();
        VD.PInfo = PossibleCopy;
        OrderedUses.push_back(VD);
      } else if (isa<PredicateWithEdge>(PossibleCopy)) {
        // If we can only do phi uses, we treat it like it's in the branch
        // block, and handle it specially. We know that it goes last, and only
        // dominate phi uses.
        auto BlockEdge = getBlockEdge(PossibleCopy);
        if (EdgeUsesOnly.count(BlockEdge)) {
          VD.LocalNum = LN_Last;
          auto *DomNode = DT.getNode(BlockEdge.first);
          if (DomNode) {
            VD.DFSIn = DomNode->getDFSNumIn();
            VD.DFSOut = DomNode->getDFSNumOut();
            VD.PInfo = PossibleCopy;
            VD.EdgeOnly = true;
            OrderedUses.push_back(VD);
          }
        } else {
          // Otherwise, we are in the split block (even though we perform
          // insertion in the branch block).
          // Insert a possible copy at the split block and before the branch.
          VD.LocalNum = LN_First;
          auto *DomNode = DT.getNode(BlockEdge.second);
          if (DomNode) {
            VD.DFSIn = DomNode->getDFSNumIn();
            VD.DFSOut = DomNode->getDFSNumOut();
            VD.PInfo = PossibleCopy;
            OrderedUses.push_back(VD);
          }
        }
      }
    }

    convertUsesToDFSOrdered(Op, OrderedUses);
    // Here we require a stable sort because we do not bother to try to
    // assign an order to the operands the uses represent. Thus, two
    // uses in the same instruction do not have a strict sort order
    // currently and will be considered equal. We could get rid of the
    // stable sort by creating one if we wanted.
    std::stable_sort(OrderedUses.begin(), OrderedUses.end(), Compare);
    SmallVector<ValueDFS, 8> RenameStack;
    // For each use, sorted into dfs order, push values and replaces uses with
    // top of stack, which will represent the reaching def.
    for (auto &VD : OrderedUses) {
      // We currently do not materialize copy over copy, but we should decide if
      // we want to.
      bool PossibleCopy = VD.PInfo != nullptr;
      if (RenameStack.empty()) {
        LLVM_DEBUG(dbgs() << "Rename Stack is empty\n");
      } else {
        LLVM_DEBUG(dbgs() << "Rename Stack Top DFS numbers are ("
                          << RenameStack.back().DFSIn << ","
                          << RenameStack.back().DFSOut << ")\n");
      }

      LLVM_DEBUG(dbgs() << "Current DFS numbers are (" << VD.DFSIn << ","
                        << VD.DFSOut << ")\n");

      bool ShouldPush = (VD.Def || PossibleCopy);
      bool OutOfScope = !stackIsInScope(RenameStack, VD);
      if (OutOfScope || ShouldPush) {
        // Sync to our current scope.
        popStackUntilDFSScope(RenameStack, VD);
        if (ShouldPush) {
          RenameStack.push_back(VD);
        }
      }
      // If we get to this point, and the stack is empty we must have a use
      // with no renaming needed, just skip it.
      if (RenameStack.empty())
        continue;
      // Skip values, only want to rename the uses
      if (VD.Def || PossibleCopy)
        continue;
      if (!DebugCounter::shouldExecute(RenameCounter)) {
        LLVM_DEBUG(dbgs() << "Skipping execution due to debug counter\n");
        continue;
      }
      ValueDFS &Result = RenameStack.back();

      // If the possible copy dominates something, materialize our stack up to
      // this point. This ensures every comparison that affects our operation
      // ends up with predicateinfo.
      if (!Result.Def)
        Result.Def = materializeStack(Counter, RenameStack, Op);

      LLVM_DEBUG(dbgs() << "Found replacement " << *Result.Def << " for "
                        << *VD.U->get() << " in " << *(VD.U->getUser())
                        << "\n");
      assert(DT.dominates(cast<Instruction>(Result.Def), *VD.U) &&
             "Predicateinfo def should have dominated this use");
      VD.U->set(Result.Def);
    }
  }
}

PredicateInfo::ValueInfo &PredicateInfo::getOrCreateValueInfo(Value *Operand) {
  auto OIN = ValueInfoNums.find(Operand);
  if (OIN == ValueInfoNums.end()) {
    // This will grow it
    ValueInfos.resize(ValueInfos.size() + 1);
    // This will use the new size and give us a 0 based number of the info
    auto InsertResult = ValueInfoNums.insert({Operand, ValueInfos.size() - 1});
    assert(InsertResult.second && "Value info number already existed?");
    return ValueInfos[InsertResult.first->second];
  }
  return ValueInfos[OIN->second];
}

const PredicateInfo::ValueInfo &
PredicateInfo::getValueInfo(Value *Operand) const {
  auto OINI = ValueInfoNums.lookup(Operand);
  assert(OINI != 0 && "Operand was not really in the Value Info Numbers");
  assert(OINI < ValueInfos.size() &&
         "Value Info Number greater than size of Value Info Table");
  return ValueInfos[OINI];
}

PredicateInfo::PredicateInfo(Function &F, DominatorTree &DT,
                             AssumptionCache &AC)
    : F(F), DT(DT), AC(AC), OI(&DT) {
  // Push an empty operand info so that we can detect 0 as not finding one
  ValueInfos.resize(1);
  buildPredicateInfo();
}

// Remove all declarations we created . The PredicateInfo consumers are
// responsible for remove the ssa_copy calls created.
PredicateInfo::~PredicateInfo() {
  // Collect function pointers in set first, as SmallSet uses a SmallVector
  // internally and we have to remove the asserting value handles first.
  SmallPtrSet<Function *, 20> FunctionPtrs;
  for (auto &F : CreatedDeclarations)
    FunctionPtrs.insert(&*F);
  CreatedDeclarations.clear();

  for (Function *F : FunctionPtrs) {
    assert(F->user_begin() == F->user_end() &&
           "PredicateInfo consumer did not remove all SSA copies.");
    F->eraseFromParent();
  }
}

void PredicateInfo::verifyPredicateInfo() const {}

char PredicateInfoPrinterLegacyPass::ID = 0;

PredicateInfoPrinterLegacyPass::PredicateInfoPrinterLegacyPass()
    : FunctionPass(ID) {
  initializePredicateInfoPrinterLegacyPassPass(
      *PassRegistry::getPassRegistry());
}

void PredicateInfoPrinterLegacyPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequired<AssumptionCacheTracker>();
}

// Replace ssa_copy calls created by PredicateInfo with their operand.
static void replaceCreatedSSACopys(PredicateInfo &PredInfo, Function &F) {
  for (auto I = inst_begin(F), E = inst_end(F); I != E;) {
    Instruction *Inst = &*I++;
    const auto *PI = PredInfo.getPredicateInfoFor(Inst);
    auto *II = dyn_cast<IntrinsicInst>(Inst);
    if (!PI || !II || II->getIntrinsicID() != Intrinsic::ssa_copy)
      continue;

    Inst->replaceAllUsesWith(II->getOperand(0));
    Inst->eraseFromParent();
  }
}

bool PredicateInfoPrinterLegacyPass::runOnFunction(Function &F) {
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  auto PredInfo = make_unique<PredicateInfo>(F, DT, AC);
  PredInfo->print(dbgs());
  if (VerifyPredicateInfo)
    PredInfo->verifyPredicateInfo();

  replaceCreatedSSACopys(*PredInfo, F);
  return false;
}

PreservedAnalyses PredicateInfoPrinterPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  OS << "PredicateInfo for function: " << F.getName() << "\n";
  auto PredInfo = make_unique<PredicateInfo>(F, DT, AC);
  PredInfo->print(OS);

  replaceCreatedSSACopys(*PredInfo, F);
  return PreservedAnalyses::all();
}

/// An assembly annotator class to print PredicateInfo information in
/// comments.
class PredicateInfoAnnotatedWriter : public AssemblyAnnotationWriter {
  friend class PredicateInfo;
  const PredicateInfo *PredInfo;

public:
  PredicateInfoAnnotatedWriter(const PredicateInfo *M) : PredInfo(M) {}

  virtual void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                        formatted_raw_ostream &OS) {}

  virtual void emitInstructionAnnot(const Instruction *I,
                                    formatted_raw_ostream &OS) {
    if (const auto *PI = PredInfo->getPredicateInfoFor(I)) {
      OS << "; Has predicate info\n";
      if (const auto *PB = dyn_cast<PredicateBranch>(PI)) {
        OS << "; branch predicate info { TrueEdge: " << PB->TrueEdge
           << " Comparison:" << *PB->Condition << " Edge: [";
        PB->From->printAsOperand(OS);
        OS << ",";
        PB->To->printAsOperand(OS);
        OS << "] }\n";
      } else if (const auto *PS = dyn_cast<PredicateSwitch>(PI)) {
        OS << "; switch predicate info { CaseValue: " << *PS->CaseValue
           << " Switch:" << *PS->Switch << " Edge: [";
        PS->From->printAsOperand(OS);
        OS << ",";
        PS->To->printAsOperand(OS);
        OS << "] }\n";
      } else if (const auto *PA = dyn_cast<PredicateAssume>(PI)) {
        OS << "; assume predicate info {"
           << " Comparison:" << *PA->Condition << " }\n";
      }
    }
  }
};

void PredicateInfo::print(raw_ostream &OS) const {
  PredicateInfoAnnotatedWriter Writer(this);
  F.print(OS, &Writer);
}

void PredicateInfo::dump() const {
  PredicateInfoAnnotatedWriter Writer(this);
  F.print(dbgs(), &Writer);
}

PreservedAnalyses PredicateInfoVerifierPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  make_unique<PredicateInfo>(F, DT, AC)->verifyPredicateInfo();

  return PreservedAnalyses::all();
}
}
