//===- SpeculativeExecution.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass hoists instructions to enable speculative execution on
// targets where branches are expensive. This is aimed at GPUs. It
// currently works on simple if-then and if-then-else
// patterns.
//
// Removing branches is not the only motivation for this
// pass. E.g. consider this code and assume that there is no
// addressing mode for multiplying by sizeof(*a):
//
//   if (b > 0)
//     c = a[i + 1]
//   if (d > 0)
//     e = a[i + 2]
//
// turns into
//
//   p = &a[i + 1];
//   if (b > 0)
//     c = *p;
//   q = &a[i + 2];
//   if (d > 0)
//     e = *q;
//
// which could later be optimized to
//
//   r = &a[i];
//   if (b > 0)
//     c = r[1];
//   if (d > 0)
//     e = r[2];
//
// Later passes sink back much of the speculated code that did not enable
// further optimization.
//
// This pass is more aggressive than the function SpeculativeyExecuteBB in
// SimplifyCFG. SimplifyCFG will not speculate if no selects are introduced and
// it will speculate at most one instruction. It also will not speculate if
// there is a value defined in the if-block that is only used in the then-block.
// These restrictions make sense since the speculation in SimplifyCFG seems
// aimed at introducing cheap selects, while this pass is intended to do more
// aggressive speculation while counting on later passes to either capitalize on
// that or clean it up.
//
// If the pass was created by calling
// createSpeculativeExecutionIfHasBranchDivergencePass or the
// -spec-exec-only-if-divergent-target option is present, this pass only has an
// effect on targets where TargetTransformInfo::hasBranchDivergence() is true;
// on other targets, it is a nop.
//
// This lets you include this pass unconditionally in the IR pass pipeline, but
// only enable it for relevant targets.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/SpeculativeExecution.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "speculative-execution"

// The risk that speculation will not pay off increases with the
// number of instructions speculated, so we put a limit on that.
static cl::opt<unsigned> SpecExecMaxSpeculationCost(
    "spec-exec-max-speculation-cost", cl::init(7), cl::Hidden,
    cl::desc("Speculative execution is not applied to basic blocks where "
             "the cost of the instructions to speculatively execute "
             "exceeds this limit."));

// Speculating just a few instructions from a larger block tends not
// to be profitable and this limit prevents that. A reason for that is
// that small basic blocks are more likely to be candidates for
// further optimization.
static cl::opt<unsigned> SpecExecMaxNotHoisted(
    "spec-exec-max-not-hoisted", cl::init(5), cl::Hidden,
    cl::desc("Speculative execution is not applied to basic blocks where the "
             "number of instructions that would not be speculatively executed "
             "exceeds this limit."));

static cl::opt<bool> SpecExecOnlyIfDivergentTarget(
    "spec-exec-only-if-divergent-target", cl::init(false), cl::Hidden,
    cl::desc("Speculative execution is applied only to targets with divergent "
             "branches, even if the pass was configured to apply only to all "
             "targets."));

namespace {

class SpeculativeExecutionLegacyPass : public FunctionPass {
public:
  static char ID;
  explicit SpeculativeExecutionLegacyPass(bool OnlyIfDivergentTarget = false)
      : FunctionPass(ID), OnlyIfDivergentTarget(OnlyIfDivergentTarget ||
                                                SpecExecOnlyIfDivergentTarget),
        Impl(OnlyIfDivergentTarget) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    if (OnlyIfDivergentTarget)
      return "Speculatively execute instructions if target has divergent "
             "branches";
    return "Speculatively execute instructions";
  }

private:
  // Variable preserved purely for correct name printing.
  const bool OnlyIfDivergentTarget;

  SpeculativeExecutionPass Impl;
};
} // namespace

char SpeculativeExecutionLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(SpeculativeExecutionLegacyPass, "speculative-execution",
                      "Speculatively execute instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(SpeculativeExecutionLegacyPass, "speculative-execution",
                    "Speculatively execute instructions", false, false)

void SpeculativeExecutionLegacyPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetTransformInfoWrapperPass>();
  AU.addPreserved<GlobalsAAWrapperPass>();
  AU.setPreservesCFG();
}

bool SpeculativeExecutionLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  return Impl.runImpl(F, TTI);
}

namespace llvm {

bool SpeculativeExecutionPass::runImpl(Function &F, TargetTransformInfo *TTI) {
  if (OnlyIfDivergentTarget && !TTI->hasBranchDivergence()) {
    LLVM_DEBUG(dbgs() << "Not running SpeculativeExecution because "
                         "TTI->hasBranchDivergence() is false.\n");
    return false;
  }

  this->TTI = TTI;
  bool Changed = false;
  for (auto& B : F) {
    Changed |= runOnBasicBlock(B);
  }
  return Changed;
}

bool SpeculativeExecutionPass::runOnBasicBlock(BasicBlock &B) {
  BranchInst *BI = dyn_cast<BranchInst>(B.getTerminator());
  if (BI == nullptr)
    return false;

  if (BI->getNumSuccessors() != 2)
    return false;
  BasicBlock &Succ0 = *BI->getSuccessor(0);
  BasicBlock &Succ1 = *BI->getSuccessor(1);

  if (&B == &Succ0 || &B == &Succ1 || &Succ0 == &Succ1) {
    return false;
  }

  // Hoist from if-then (triangle).
  if (Succ0.getSinglePredecessor() != nullptr &&
      Succ0.getSingleSuccessor() == &Succ1) {
    return considerHoistingFromTo(Succ0, B);
  }

  // Hoist from if-else (triangle).
  if (Succ1.getSinglePredecessor() != nullptr &&
      Succ1.getSingleSuccessor() == &Succ0) {
    return considerHoistingFromTo(Succ1, B);
  }

  // Hoist from if-then-else (diamond), but only if it is equivalent to
  // an if-else or if-then due to one of the branches doing nothing.
  if (Succ0.getSinglePredecessor() != nullptr &&
      Succ1.getSinglePredecessor() != nullptr &&
      Succ1.getSingleSuccessor() != nullptr &&
      Succ1.getSingleSuccessor() != &B &&
      Succ1.getSingleSuccessor() == Succ0.getSingleSuccessor()) {
    // If a block has only one instruction, then that is a terminator
    // instruction so that the block does nothing. This does happen.
    if (Succ1.size() == 1) // equivalent to if-then
      return considerHoistingFromTo(Succ0, B);
    if (Succ0.size() == 1) // equivalent to if-else
      return considerHoistingFromTo(Succ1, B);
  }

  return false;
}

static unsigned ComputeSpeculationCost(const Instruction *I,
                                       const TargetTransformInfo &TTI) {
  switch (Operator::getOpcode(I)) {
    case Instruction::GetElementPtr:
    case Instruction::Add:
    case Instruction::Mul:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Select:
    case Instruction::Shl:
    case Instruction::Sub:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Xor:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::Call:
    case Instruction::BitCast:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::AddrSpaceCast:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPExt:
    case Instruction::FPTrunc:
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::ICmp:
    case Instruction::FCmp:
      return TTI.getUserCost(I);

    default:
      return UINT_MAX; // Disallow anything not whitelisted.
  }
}

bool SpeculativeExecutionPass::considerHoistingFromTo(
    BasicBlock &FromBlock, BasicBlock &ToBlock) {
  SmallPtrSet<const Instruction *, 8> NotHoisted;
  const auto AllPrecedingUsesFromBlockHoisted = [&NotHoisted](User *U) {
    for (Value* V : U->operand_values()) {
      if (Instruction *I = dyn_cast<Instruction>(V)) {
        if (NotHoisted.count(I) > 0)
          return false;
      }
    }
    return true;
  };

  unsigned TotalSpeculationCost = 0;
  for (auto& I : FromBlock) {
    const unsigned Cost = ComputeSpeculationCost(&I, *TTI);
    if (Cost != UINT_MAX && isSafeToSpeculativelyExecute(&I) &&
        AllPrecedingUsesFromBlockHoisted(&I)) {
      TotalSpeculationCost += Cost;
      if (TotalSpeculationCost > SpecExecMaxSpeculationCost)
        return false;  // too much to hoist
    } else {
      NotHoisted.insert(&I);
      if (NotHoisted.size() > SpecExecMaxNotHoisted)
        return false; // too much left behind
    }
  }

  if (TotalSpeculationCost == 0)
    return false; // nothing to hoist

  for (auto I = FromBlock.begin(); I != FromBlock.end();) {
    // We have to increment I before moving Current as moving Current
    // changes the list that I is iterating through.
    auto Current = I;
    ++I;
    if (!NotHoisted.count(&*Current)) {
      Current->moveBefore(ToBlock.getTerminator());
    }
  }
  return true;
}

FunctionPass *createSpeculativeExecutionPass() {
  return new SpeculativeExecutionLegacyPass();
}

FunctionPass *createSpeculativeExecutionIfHasBranchDivergencePass() {
  return new SpeculativeExecutionLegacyPass(/* OnlyIfDivergentTarget = */ true);
}

SpeculativeExecutionPass::SpeculativeExecutionPass(bool OnlyIfDivergentTarget)
    : OnlyIfDivergentTarget(OnlyIfDivergentTarget ||
                            SpecExecOnlyIfDivergentTarget) {}

PreservedAnalyses SpeculativeExecutionPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  auto *TTI = &AM.getResult<TargetIRAnalysis>(F);

  bool Changed = runImpl(F, TTI);

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<GlobalsAA>();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
}  // namespace llvm
