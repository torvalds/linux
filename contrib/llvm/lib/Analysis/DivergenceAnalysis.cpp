//===- DivergenceAnalysis.cpp --------- Divergence Analysis Implementation -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a general divergence analysis for loop vectorization
// and GPU programs. It determines which branches and values in a loop or GPU
// program are divergent. It can help branch optimizations such as jump
// threading and loop unswitching to make better decisions.
//
// GPU programs typically use the SIMD execution model, where multiple threads
// in the same execution group have to execute in lock-step. Therefore, if the
// code contains divergent branches (i.e., threads in a group do not agree on
// which path of the branch to take), the group of threads has to execute all
// the paths from that branch with different subsets of threads enabled until
// they re-converge.
//
// Due to this execution model, some optimizations such as jump
// threading and loop unswitching can interfere with thread re-convergence.
// Therefore, an analysis that computes which branches in a GPU program are
// divergent can help the compiler to selectively run these optimizations.
//
// This implementation is derived from the Vectorization Analysis of the
// Region Vectorizer (RV). That implementation in turn is based on the approach
// described in
//
//   Improving Performance of OpenCL on CPUs
//   Ralf Karrenberg and Sebastian Hack
//   CC '12
//
// This DivergenceAnalysis implementation is generic in the sense that it does
// not itself identify original sources of divergence.
// Instead specialized adapter classes, (LoopDivergenceAnalysis) for loops and
// (GPUDivergenceAnalysis) for GPU programs, identify the sources of divergence
// (e.g., special variables that hold the thread ID or the iteration variable).
//
// The generic implementation propagates divergence to variables that are data
// or sync dependent on a source of divergence.
//
// While data dependency is a well-known concept, the notion of sync dependency
// is worth more explanation. Sync dependence characterizes the control flow
// aspect of the propagation of branch divergence. For example,
//
//   %cond = icmp slt i32 %tid, 10
//   br i1 %cond, label %then, label %else
// then:
//   br label %merge
// else:
//   br label %merge
// merge:
//   %a = phi i32 [ 0, %then ], [ 1, %else ]
//
// Suppose %tid holds the thread ID. Although %a is not data dependent on %tid
// because %tid is not on its use-def chains, %a is sync dependent on %tid
// because the branch "br i1 %cond" depends on %tid and affects which value %a
// is assigned to.
//
// The sync dependence detection (which branch induces divergence in which join
// points) is implemented in the SyncDependenceAnalysis.
//
// The current DivergenceAnalysis implementation has the following limitations:
// 1. intra-procedural. It conservatively considers the arguments of a
//    non-kernel-entry function and the return value of a function call as
//    divergent.
// 2. memory as black box. It conservatively considers values loaded from
//    generic or local address as divergent. This can be improved by leveraging
//    pointer analysis and/or by modelling non-escaping memory objects in SSA
//    as done in RV.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DivergenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "divergence-analysis"

// class DivergenceAnalysis
DivergenceAnalysis::DivergenceAnalysis(
    const Function &F, const Loop *RegionLoop, const DominatorTree &DT,
    const LoopInfo &LI, SyncDependenceAnalysis &SDA, bool IsLCSSAForm)
    : F(F), RegionLoop(RegionLoop), DT(DT), LI(LI), SDA(SDA),
      IsLCSSAForm(IsLCSSAForm) {}

void DivergenceAnalysis::markDivergent(const Value &DivVal) {
  assert(isa<Instruction>(DivVal) || isa<Argument>(DivVal));
  assert(!isAlwaysUniform(DivVal) && "cannot be a divergent");
  DivergentValues.insert(&DivVal);
}

void DivergenceAnalysis::addUniformOverride(const Value &UniVal) {
  UniformOverrides.insert(&UniVal);
}

bool DivergenceAnalysis::updateTerminator(const Instruction &Term) const {
  if (Term.getNumSuccessors() <= 1)
    return false;
  if (auto *BranchTerm = dyn_cast<BranchInst>(&Term)) {
    assert(BranchTerm->isConditional());
    return isDivergent(*BranchTerm->getCondition());
  }
  if (auto *SwitchTerm = dyn_cast<SwitchInst>(&Term)) {
    return isDivergent(*SwitchTerm->getCondition());
  }
  if (isa<InvokeInst>(Term)) {
    return false; // ignore abnormal executions through landingpad
  }

  llvm_unreachable("unexpected terminator");
}

bool DivergenceAnalysis::updateNormalInstruction(const Instruction &I) const {
  // TODO function calls with side effects, etc
  for (const auto &Op : I.operands()) {
    if (isDivergent(*Op))
      return true;
  }
  return false;
}

bool DivergenceAnalysis::isTemporalDivergent(const BasicBlock &ObservingBlock,
                                             const Value &Val) const {
  const auto *Inst = dyn_cast<const Instruction>(&Val);
  if (!Inst)
    return false;
  // check whether any divergent loop carrying Val terminates before control
  // proceeds to ObservingBlock
  for (const auto *Loop = LI.getLoopFor(Inst->getParent());
       Loop != RegionLoop && !Loop->contains(&ObservingBlock);
       Loop = Loop->getParentLoop()) {
    if (DivergentLoops.find(Loop) != DivergentLoops.end())
      return true;
  }

  return false;
}

bool DivergenceAnalysis::updatePHINode(const PHINode &Phi) const {
  // joining divergent disjoint path in Phi parent block
  if (!Phi.hasConstantOrUndefValue() && isJoinDivergent(*Phi.getParent())) {
    return true;
  }

  // An incoming value could be divergent by itself.
  // Otherwise, an incoming value could be uniform within the loop
  // that carries its definition but it may appear divergent
  // from outside the loop. This happens when divergent loop exits
  // drop definitions of that uniform value in different iterations.
  //
  // for (int i = 0; i < n; ++i) { // 'i' is uniform inside the loop
  //   if (i % thread_id == 0) break;    // divergent loop exit
  // }
  // int divI = i;                 // divI is divergent
  for (size_t i = 0; i < Phi.getNumIncomingValues(); ++i) {
    const auto *InVal = Phi.getIncomingValue(i);
    if (isDivergent(*Phi.getIncomingValue(i)) ||
        isTemporalDivergent(*Phi.getParent(), *InVal)) {
      return true;
    }
  }
  return false;
}

bool DivergenceAnalysis::inRegion(const Instruction &I) const {
  return I.getParent() && inRegion(*I.getParent());
}

bool DivergenceAnalysis::inRegion(const BasicBlock &BB) const {
  return (!RegionLoop && BB.getParent() == &F) || RegionLoop->contains(&BB);
}

// marks all users of loop-carried values of the loop headed by LoopHeader as
// divergent
void DivergenceAnalysis::taintLoopLiveOuts(const BasicBlock &LoopHeader) {
  auto *DivLoop = LI.getLoopFor(&LoopHeader);
  assert(DivLoop && "loopHeader is not actually part of a loop");

  SmallVector<BasicBlock *, 8> TaintStack;
  DivLoop->getExitBlocks(TaintStack);

  // Otherwise potential users of loop-carried values could be anywhere in the
  // dominance region of DivLoop (including its fringes for phi nodes)
  DenseSet<const BasicBlock *> Visited;
  for (auto *Block : TaintStack) {
    Visited.insert(Block);
  }
  Visited.insert(&LoopHeader);

  while (!TaintStack.empty()) {
    auto *UserBlock = TaintStack.back();
    TaintStack.pop_back();

    // don't spread divergence beyond the region
    if (!inRegion(*UserBlock))
      continue;

    assert(!DivLoop->contains(UserBlock) &&
           "irreducible control flow detected");

    // phi nodes at the fringes of the dominance region
    if (!DT.dominates(&LoopHeader, UserBlock)) {
      // all PHI nodes of UserBlock become divergent
      for (auto &Phi : UserBlock->phis()) {
        Worklist.push_back(&Phi);
      }
      continue;
    }

    // taint outside users of values carried by DivLoop
    for (auto &I : *UserBlock) {
      if (isAlwaysUniform(I))
        continue;
      if (isDivergent(I))
        continue;

      for (auto &Op : I.operands()) {
        auto *OpInst = dyn_cast<Instruction>(&Op);
        if (!OpInst)
          continue;
        if (DivLoop->contains(OpInst->getParent())) {
          markDivergent(I);
          pushUsers(I);
          break;
        }
      }
    }

    // visit all blocks in the dominance region
    for (auto *SuccBlock : successors(UserBlock)) {
      if (!Visited.insert(SuccBlock).second) {
        continue;
      }
      TaintStack.push_back(SuccBlock);
    }
  }
}

void DivergenceAnalysis::pushPHINodes(const BasicBlock &Block) {
  for (const auto &Phi : Block.phis()) {
    if (isDivergent(Phi))
      continue;
    Worklist.push_back(&Phi);
  }
}

void DivergenceAnalysis::pushUsers(const Value &V) {
  for (const auto *User : V.users()) {
    const auto *UserInst = dyn_cast<const Instruction>(User);
    if (!UserInst)
      continue;

    if (isDivergent(*UserInst))
      continue;

    // only compute divergent inside loop
    if (!inRegion(*UserInst))
      continue;
    Worklist.push_back(UserInst);
  }
}

bool DivergenceAnalysis::propagateJoinDivergence(const BasicBlock &JoinBlock,
                                                 const Loop *BranchLoop) {
  LLVM_DEBUG(dbgs() << "\tpropJoinDiv " << JoinBlock.getName() << "\n");

  // ignore divergence outside the region
  if (!inRegion(JoinBlock)) {
    return false;
  }

  // push non-divergent phi nodes in JoinBlock to the worklist
  pushPHINodes(JoinBlock);

  // JoinBlock is a divergent loop exit
  if (BranchLoop && !BranchLoop->contains(&JoinBlock)) {
    return true;
  }

  // disjoint-paths divergent at JoinBlock
  markBlockJoinDivergent(JoinBlock);
  return false;
}

void DivergenceAnalysis::propagateBranchDivergence(const Instruction &Term) {
  LLVM_DEBUG(dbgs() << "propBranchDiv " << Term.getParent()->getName() << "\n");

  markDivergent(Term);

  const auto *BranchLoop = LI.getLoopFor(Term.getParent());

  // whether there is a divergent loop exit from BranchLoop (if any)
  bool IsBranchLoopDivergent = false;

  // iterate over all blocks reachable by disjoint from Term within the loop
  // also iterates over loop exits that become divergent due to Term.
  for (const auto *JoinBlock : SDA.join_blocks(Term)) {
    IsBranchLoopDivergent |= propagateJoinDivergence(*JoinBlock, BranchLoop);
  }

  // Branch loop is a divergent loop due to the divergent branch in Term
  if (IsBranchLoopDivergent) {
    assert(BranchLoop);
    if (!DivergentLoops.insert(BranchLoop).second) {
      return;
    }
    propagateLoopDivergence(*BranchLoop);
  }
}

void DivergenceAnalysis::propagateLoopDivergence(const Loop &ExitingLoop) {
  LLVM_DEBUG(dbgs() << "propLoopDiv " << ExitingLoop.getName() << "\n");

  // don't propagate beyond region
  if (!inRegion(*ExitingLoop.getHeader()))
    return;

  const auto *BranchLoop = ExitingLoop.getParentLoop();

  // Uses of loop-carried values could occur anywhere
  // within the dominance region of the definition. All loop-carried
  // definitions are dominated by the loop header (reducible control).
  // Thus all users have to be in the dominance region of the loop header,
  // except PHI nodes that can also live at the fringes of the dom region
  // (incoming defining value).
  if (!IsLCSSAForm)
    taintLoopLiveOuts(*ExitingLoop.getHeader());

  // whether there is a divergent loop exit from BranchLoop (if any)
  bool IsBranchLoopDivergent = false;

  // iterate over all blocks reachable by disjoint paths from exits of
  // ExitingLoop also iterates over loop exits (of BranchLoop) that in turn
  // become divergent.
  for (const auto *JoinBlock : SDA.join_blocks(ExitingLoop)) {
    IsBranchLoopDivergent |= propagateJoinDivergence(*JoinBlock, BranchLoop);
  }

  // Branch loop is a divergent due to divergent loop exit in ExitingLoop
  if (IsBranchLoopDivergent) {
    assert(BranchLoop);
    if (!DivergentLoops.insert(BranchLoop).second) {
      return;
    }
    propagateLoopDivergence(*BranchLoop);
  }
}

void DivergenceAnalysis::compute() {
  for (auto *DivVal : DivergentValues) {
    pushUsers(*DivVal);
  }

  // propagate divergence
  while (!Worklist.empty()) {
    const Instruction &I = *Worklist.back();
    Worklist.pop_back();

    // maintain uniformity of overrides
    if (isAlwaysUniform(I))
      continue;

    bool WasDivergent = isDivergent(I);
    if (WasDivergent)
      continue;

    // propagate divergence caused by terminator
    if (I.isTerminator()) {
      if (updateTerminator(I)) {
        // propagate control divergence to affected instructions
        propagateBranchDivergence(I);
        continue;
      }
    }

    // update divergence of I due to divergent operands
    bool DivergentUpd = false;
    const auto *Phi = dyn_cast<const PHINode>(&I);
    if (Phi) {
      DivergentUpd = updatePHINode(*Phi);
    } else {
      DivergentUpd = updateNormalInstruction(I);
    }

    // propagate value divergence to users
    if (DivergentUpd) {
      markDivergent(I);
      pushUsers(I);
    }
  }
}

bool DivergenceAnalysis::isAlwaysUniform(const Value &V) const {
  return UniformOverrides.find(&V) != UniformOverrides.end();
}

bool DivergenceAnalysis::isDivergent(const Value &V) const {
  return DivergentValues.find(&V) != DivergentValues.end();
}

void DivergenceAnalysis::print(raw_ostream &OS, const Module *) const {
  if (DivergentValues.empty())
    return;
  // iterate instructions using instructions() to ensure a deterministic order.
  for (auto &I : instructions(F)) {
    if (isDivergent(I))
      OS << "DIVERGENT:" << I << '\n';
  }
}

// class GPUDivergenceAnalysis
GPUDivergenceAnalysis::GPUDivergenceAnalysis(Function &F,
                                             const DominatorTree &DT,
                                             const PostDominatorTree &PDT,
                                             const LoopInfo &LI,
                                             const TargetTransformInfo &TTI)
    : SDA(DT, PDT, LI), DA(F, nullptr, DT, LI, SDA, false) {
  for (auto &I : instructions(F)) {
    if (TTI.isSourceOfDivergence(&I)) {
      DA.markDivergent(I);
    } else if (TTI.isAlwaysUniform(&I)) {
      DA.addUniformOverride(I);
    }
  }
  for (auto &Arg : F.args()) {
    if (TTI.isSourceOfDivergence(&Arg)) {
      DA.markDivergent(Arg);
    }
  }

  DA.compute();
}

bool GPUDivergenceAnalysis::isDivergent(const Value &val) const {
  return DA.isDivergent(val);
}

void GPUDivergenceAnalysis::print(raw_ostream &OS, const Module *mod) const {
  OS << "Divergence of kernel " << DA.getFunction().getName() << " {\n";
  DA.print(OS, mod);
  OS << "}\n";
}
