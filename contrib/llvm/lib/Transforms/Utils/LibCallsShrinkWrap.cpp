//===-- LibCallsShrinkWrap.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass shrink-wraps a call to function if the result is not used.
// The call can set errno but is otherwise side effect free. For example:
//    sqrt(val);
//  is transformed to
//    if (val < 0)
//      sqrt(val);
//  Even if the result of library call is not being used, the compiler cannot
//  safely delete the call because the function can set errno on error
//  conditions.
//  Note in many functions, the error condition solely depends on the incoming
//  parameter. In this optimization, we can generate the condition can lead to
//  the errno to shrink-wrap the call. Since the chances of hitting the error
//  condition is low, the runtime call is effectively eliminated.
//
//  These partially dead calls are usually results of C++ abstraction penalty
//  exposed by inlining.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

#define DEBUG_TYPE "libcalls-shrinkwrap"

STATISTIC(NumWrappedOneCond, "Number of One-Condition Wrappers Inserted");
STATISTIC(NumWrappedTwoCond, "Number of Two-Condition Wrappers Inserted");

namespace {
class LibCallsShrinkWrapLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit LibCallsShrinkWrapLegacyPass() : FunctionPass(ID) {
    initializeLibCallsShrinkWrapLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};
}

char LibCallsShrinkWrapLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(LibCallsShrinkWrapLegacyPass, "libcalls-shrinkwrap",
                      "Conditionally eliminate dead library calls", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(LibCallsShrinkWrapLegacyPass, "libcalls-shrinkwrap",
                    "Conditionally eliminate dead library calls", false, false)

namespace {
class LibCallsShrinkWrap : public InstVisitor<LibCallsShrinkWrap> {
public:
  LibCallsShrinkWrap(const TargetLibraryInfo &TLI, DominatorTree *DT)
      : TLI(TLI), DT(DT){};
  void visitCallInst(CallInst &CI) { checkCandidate(CI); }
  bool perform() {
    bool Changed = false;
    for (auto &CI : WorkList) {
      LLVM_DEBUG(dbgs() << "CDCE calls: " << CI->getCalledFunction()->getName()
                        << "\n");
      if (perform(CI)) {
        Changed = true;
        LLVM_DEBUG(dbgs() << "Transformed\n");
      }
    }
    return Changed;
  }

private:
  bool perform(CallInst *CI);
  void checkCandidate(CallInst &CI);
  void shrinkWrapCI(CallInst *CI, Value *Cond);
  bool performCallDomainErrorOnly(CallInst *CI, const LibFunc &Func);
  bool performCallErrors(CallInst *CI, const LibFunc &Func);
  bool performCallRangeErrorOnly(CallInst *CI, const LibFunc &Func);
  Value *generateOneRangeCond(CallInst *CI, const LibFunc &Func);
  Value *generateTwoRangeCond(CallInst *CI, const LibFunc &Func);
  Value *generateCondForPow(CallInst *CI, const LibFunc &Func);

  // Create an OR of two conditions.
  Value *createOrCond(CallInst *CI, CmpInst::Predicate Cmp, float Val,
                      CmpInst::Predicate Cmp2, float Val2) {
    IRBuilder<> BBBuilder(CI);
    Value *Arg = CI->getArgOperand(0);
    auto Cond2 = createCond(BBBuilder, Arg, Cmp2, Val2);
    auto Cond1 = createCond(BBBuilder, Arg, Cmp, Val);
    return BBBuilder.CreateOr(Cond1, Cond2);
  }

  // Create a single condition using IRBuilder.
  Value *createCond(IRBuilder<> &BBBuilder, Value *Arg, CmpInst::Predicate Cmp,
                    float Val) {
    Constant *V = ConstantFP::get(BBBuilder.getContext(), APFloat(Val));
    if (!Arg->getType()->isFloatTy())
      V = ConstantExpr::getFPExtend(V, Arg->getType());
    return BBBuilder.CreateFCmp(Cmp, Arg, V);
  }

  // Create a single condition.
  Value *createCond(CallInst *CI, CmpInst::Predicate Cmp, float Val) {
    IRBuilder<> BBBuilder(CI);
    Value *Arg = CI->getArgOperand(0);
    return createCond(BBBuilder, Arg, Cmp, Val);
  }

  const TargetLibraryInfo &TLI;
  DominatorTree *DT;
  SmallVector<CallInst *, 16> WorkList;
};
} // end anonymous namespace

// Perform the transformation to calls with errno set by domain error.
bool LibCallsShrinkWrap::performCallDomainErrorOnly(CallInst *CI,
                                                    const LibFunc &Func) {
  Value *Cond = nullptr;

  switch (Func) {
  case LibFunc_acos:  // DomainError: (x < -1 || x > 1)
  case LibFunc_acosf: // Same as acos
  case LibFunc_acosl: // Same as acos
  case LibFunc_asin:  // DomainError: (x < -1 || x > 1)
  case LibFunc_asinf: // Same as asin
  case LibFunc_asinl: // Same as asin
  {
    ++NumWrappedTwoCond;
    Cond = createOrCond(CI, CmpInst::FCMP_OLT, -1.0f, CmpInst::FCMP_OGT, 1.0f);
    break;
  }
  case LibFunc_cos:  // DomainError: (x == +inf || x == -inf)
  case LibFunc_cosf: // Same as cos
  case LibFunc_cosl: // Same as cos
  case LibFunc_sin:  // DomainError: (x == +inf || x == -inf)
  case LibFunc_sinf: // Same as sin
  case LibFunc_sinl: // Same as sin
  {
    ++NumWrappedTwoCond;
    Cond = createOrCond(CI, CmpInst::FCMP_OEQ, INFINITY, CmpInst::FCMP_OEQ,
                        -INFINITY);
    break;
  }
  case LibFunc_acosh:  // DomainError: (x < 1)
  case LibFunc_acoshf: // Same as acosh
  case LibFunc_acoshl: // Same as acosh
  {
    ++NumWrappedOneCond;
    Cond = createCond(CI, CmpInst::FCMP_OLT, 1.0f);
    break;
  }
  case LibFunc_sqrt:  // DomainError: (x < 0)
  case LibFunc_sqrtf: // Same as sqrt
  case LibFunc_sqrtl: // Same as sqrt
  {
    ++NumWrappedOneCond;
    Cond = createCond(CI, CmpInst::FCMP_OLT, 0.0f);
    break;
  }
  default:
    return false;
  }
  shrinkWrapCI(CI, Cond);
  return true;
}

// Perform the transformation to calls with errno set by range error.
bool LibCallsShrinkWrap::performCallRangeErrorOnly(CallInst *CI,
                                                   const LibFunc &Func) {
  Value *Cond = nullptr;

  switch (Func) {
  case LibFunc_cosh:
  case LibFunc_coshf:
  case LibFunc_coshl:
  case LibFunc_exp:
  case LibFunc_expf:
  case LibFunc_expl:
  case LibFunc_exp10:
  case LibFunc_exp10f:
  case LibFunc_exp10l:
  case LibFunc_exp2:
  case LibFunc_exp2f:
  case LibFunc_exp2l:
  case LibFunc_sinh:
  case LibFunc_sinhf:
  case LibFunc_sinhl: {
    Cond = generateTwoRangeCond(CI, Func);
    break;
  }
  case LibFunc_expm1:  // RangeError: (709, inf)
  case LibFunc_expm1f: // RangeError: (88, inf)
  case LibFunc_expm1l: // RangeError: (11356, inf)
  {
    Cond = generateOneRangeCond(CI, Func);
    break;
  }
  default:
    return false;
  }
  shrinkWrapCI(CI, Cond);
  return true;
}

// Perform the transformation to calls with errno set by combination of errors.
bool LibCallsShrinkWrap::performCallErrors(CallInst *CI,
                                           const LibFunc &Func) {
  Value *Cond = nullptr;

  switch (Func) {
  case LibFunc_atanh:  // DomainError: (x < -1 || x > 1)
                        // PoleError:   (x == -1 || x == 1)
                        // Overall Cond: (x <= -1 || x >= 1)
  case LibFunc_atanhf: // Same as atanh
  case LibFunc_atanhl: // Same as atanh
  {
    ++NumWrappedTwoCond;
    Cond = createOrCond(CI, CmpInst::FCMP_OLE, -1.0f, CmpInst::FCMP_OGE, 1.0f);
    break;
  }
  case LibFunc_log:    // DomainError: (x < 0)
                        // PoleError:   (x == 0)
                        // Overall Cond: (x <= 0)
  case LibFunc_logf:   // Same as log
  case LibFunc_logl:   // Same as log
  case LibFunc_log10:  // Same as log
  case LibFunc_log10f: // Same as log
  case LibFunc_log10l: // Same as log
  case LibFunc_log2:   // Same as log
  case LibFunc_log2f:  // Same as log
  case LibFunc_log2l:  // Same as log
  case LibFunc_logb:   // Same as log
  case LibFunc_logbf:  // Same as log
  case LibFunc_logbl:  // Same as log
  {
    ++NumWrappedOneCond;
    Cond = createCond(CI, CmpInst::FCMP_OLE, 0.0f);
    break;
  }
  case LibFunc_log1p:  // DomainError: (x < -1)
                        // PoleError:   (x == -1)
                        // Overall Cond: (x <= -1)
  case LibFunc_log1pf: // Same as log1p
  case LibFunc_log1pl: // Same as log1p
  {
    ++NumWrappedOneCond;
    Cond = createCond(CI, CmpInst::FCMP_OLE, -1.0f);
    break;
  }
  case LibFunc_pow: // DomainError: x < 0 and y is noninteger
                     // PoleError:   x == 0 and y < 0
                     // RangeError:  overflow or underflow
  case LibFunc_powf:
  case LibFunc_powl: {
    Cond = generateCondForPow(CI, Func);
    if (Cond == nullptr)
      return false;
    break;
  }
  default:
    return false;
  }
  assert(Cond && "performCallErrors should not see an empty condition");
  shrinkWrapCI(CI, Cond);
  return true;
}

// Checks if CI is a candidate for shrinkwrapping and put it into work list if
// true.
void LibCallsShrinkWrap::checkCandidate(CallInst &CI) {
  if (CI.isNoBuiltin())
    return;
  // A possible improvement is to handle the calls with the return value being
  // used. If there is API for fast libcall implementation without setting
  // errno, we can use the same framework to direct/wrap the call to the fast
  // API in the error free path, and leave the original call in the slow path.
  if (!CI.use_empty())
    return;

  LibFunc Func;
  Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return;
  if (!TLI.getLibFunc(*Callee, Func) || !TLI.has(Func))
    return;

  if (CI.getNumArgOperands() == 0)
    return;
  // TODO: Handle long double in other formats.
  Type *ArgType = CI.getArgOperand(0)->getType();
  if (!(ArgType->isFloatTy() || ArgType->isDoubleTy() ||
        ArgType->isX86_FP80Ty()))
    return;

  WorkList.push_back(&CI);
}

// Generate the upper bound condition for RangeError.
Value *LibCallsShrinkWrap::generateOneRangeCond(CallInst *CI,
                                                const LibFunc &Func) {
  float UpperBound;
  switch (Func) {
  case LibFunc_expm1: // RangeError: (709, inf)
    UpperBound = 709.0f;
    break;
  case LibFunc_expm1f: // RangeError: (88, inf)
    UpperBound = 88.0f;
    break;
  case LibFunc_expm1l: // RangeError: (11356, inf)
    UpperBound = 11356.0f;
    break;
  default:
    llvm_unreachable("Unhandled library call!");
  }

  ++NumWrappedOneCond;
  return createCond(CI, CmpInst::FCMP_OGT, UpperBound);
}

// Generate the lower and upper bound condition for RangeError.
Value *LibCallsShrinkWrap::generateTwoRangeCond(CallInst *CI,
                                                const LibFunc &Func) {
  float UpperBound, LowerBound;
  switch (Func) {
  case LibFunc_cosh: // RangeError: (x < -710 || x > 710)
  case LibFunc_sinh: // Same as cosh
    LowerBound = -710.0f;
    UpperBound = 710.0f;
    break;
  case LibFunc_coshf: // RangeError: (x < -89 || x > 89)
  case LibFunc_sinhf: // Same as coshf
    LowerBound = -89.0f;
    UpperBound = 89.0f;
    break;
  case LibFunc_coshl: // RangeError: (x < -11357 || x > 11357)
  case LibFunc_sinhl: // Same as coshl
    LowerBound = -11357.0f;
    UpperBound = 11357.0f;
    break;
  case LibFunc_exp: // RangeError: (x < -745 || x > 709)
    LowerBound = -745.0f;
    UpperBound = 709.0f;
    break;
  case LibFunc_expf: // RangeError: (x < -103 || x > 88)
    LowerBound = -103.0f;
    UpperBound = 88.0f;
    break;
  case LibFunc_expl: // RangeError: (x < -11399 || x > 11356)
    LowerBound = -11399.0f;
    UpperBound = 11356.0f;
    break;
  case LibFunc_exp10: // RangeError: (x < -323 || x > 308)
    LowerBound = -323.0f;
    UpperBound = 308.0f;
    break;
  case LibFunc_exp10f: // RangeError: (x < -45 || x > 38)
    LowerBound = -45.0f;
    UpperBound = 38.0f;
    break;
  case LibFunc_exp10l: // RangeError: (x < -4950 || x > 4932)
    LowerBound = -4950.0f;
    UpperBound = 4932.0f;
    break;
  case LibFunc_exp2: // RangeError: (x < -1074 || x > 1023)
    LowerBound = -1074.0f;
    UpperBound = 1023.0f;
    break;
  case LibFunc_exp2f: // RangeError: (x < -149 || x > 127)
    LowerBound = -149.0f;
    UpperBound = 127.0f;
    break;
  case LibFunc_exp2l: // RangeError: (x < -16445 || x > 11383)
    LowerBound = -16445.0f;
    UpperBound = 11383.0f;
    break;
  default:
    llvm_unreachable("Unhandled library call!");
  }

  ++NumWrappedTwoCond;
  return createOrCond(CI, CmpInst::FCMP_OGT, UpperBound, CmpInst::FCMP_OLT,
                      LowerBound);
}

// For pow(x,y), We only handle the following cases:
// (1) x is a constant && (x >= 1) && (x < MaxUInt8)
//     Cond is: (y > 127)
// (2) x is a value coming from an integer type.
//   (2.1) if x's bit_size == 8
//         Cond: (x <= 0 || y > 128)
//   (2.2) if x's bit_size is 16
//         Cond: (x <= 0 || y > 64)
//   (2.3) if x's bit_size is 32
//         Cond: (x <= 0 || y > 32)
// Support for powl(x,y) and powf(x,y) are TBD.
//
// Note that condition can be more conservative than the actual condition
// (i.e. we might invoke the calls that will not set the errno.).
//
Value *LibCallsShrinkWrap::generateCondForPow(CallInst *CI,
                                              const LibFunc &Func) {
  // FIXME: LibFunc_powf and powl TBD.
  if (Func != LibFunc_pow) {
    LLVM_DEBUG(dbgs() << "Not handled powf() and powl()\n");
    return nullptr;
  }

  Value *Base = CI->getArgOperand(0);
  Value *Exp = CI->getArgOperand(1);
  IRBuilder<> BBBuilder(CI);

  // Constant Base case.
  if (ConstantFP *CF = dyn_cast<ConstantFP>(Base)) {
    double D = CF->getValueAPF().convertToDouble();
    if (D < 1.0f || D > APInt::getMaxValue(8).getZExtValue()) {
      LLVM_DEBUG(dbgs() << "Not handled pow(): constant base out of range\n");
      return nullptr;
    }

    ++NumWrappedOneCond;
    Constant *V = ConstantFP::get(CI->getContext(), APFloat(127.0f));
    if (!Exp->getType()->isFloatTy())
      V = ConstantExpr::getFPExtend(V, Exp->getType());
    return BBBuilder.CreateFCmp(CmpInst::FCMP_OGT, Exp, V);
  }

  // If the Base value coming from an integer type.
  Instruction *I = dyn_cast<Instruction>(Base);
  if (!I) {
    LLVM_DEBUG(dbgs() << "Not handled pow(): FP type base\n");
    return nullptr;
  }
  unsigned Opcode = I->getOpcode();
  if (Opcode == Instruction::UIToFP || Opcode == Instruction::SIToFP) {
    unsigned BW = I->getOperand(0)->getType()->getPrimitiveSizeInBits();
    float UpperV = 0.0f;
    if (BW == 8)
      UpperV = 128.0f;
    else if (BW == 16)
      UpperV = 64.0f;
    else if (BW == 32)
      UpperV = 32.0f;
    else {
      LLVM_DEBUG(dbgs() << "Not handled pow(): type too wide\n");
      return nullptr;
    }

    ++NumWrappedTwoCond;
    Constant *V = ConstantFP::get(CI->getContext(), APFloat(UpperV));
    Constant *V0 = ConstantFP::get(CI->getContext(), APFloat(0.0f));
    if (!Exp->getType()->isFloatTy())
      V = ConstantExpr::getFPExtend(V, Exp->getType());
    if (!Base->getType()->isFloatTy())
      V0 = ConstantExpr::getFPExtend(V0, Exp->getType());

    Value *Cond = BBBuilder.CreateFCmp(CmpInst::FCMP_OGT, Exp, V);
    Value *Cond0 = BBBuilder.CreateFCmp(CmpInst::FCMP_OLE, Base, V0);
    return BBBuilder.CreateOr(Cond0, Cond);
  }
  LLVM_DEBUG(dbgs() << "Not handled pow(): base not from integer convert\n");
  return nullptr;
}

// Wrap conditions that can potentially generate errno to the library call.
void LibCallsShrinkWrap::shrinkWrapCI(CallInst *CI, Value *Cond) {
  assert(Cond != nullptr && "ShrinkWrapCI is not expecting an empty call inst");
  MDNode *BranchWeights =
      MDBuilder(CI->getContext()).createBranchWeights(1, 2000);

  Instruction *NewInst =
      SplitBlockAndInsertIfThen(Cond, CI, false, BranchWeights, DT);
  BasicBlock *CallBB = NewInst->getParent();
  CallBB->setName("cdce.call");
  BasicBlock *SuccBB = CallBB->getSingleSuccessor();
  assert(SuccBB && "The split block should have a single successor");
  SuccBB->setName("cdce.end");
  CI->removeFromParent();
  CallBB->getInstList().insert(CallBB->getFirstInsertionPt(), CI);
  LLVM_DEBUG(dbgs() << "== Basic Block After ==");
  LLVM_DEBUG(dbgs() << *CallBB->getSinglePredecessor() << *CallBB
                    << *CallBB->getSingleSuccessor() << "\n");
}

// Perform the transformation to a single candidate.
bool LibCallsShrinkWrap::perform(CallInst *CI) {
  LibFunc Func;
  Function *Callee = CI->getCalledFunction();
  assert(Callee && "perform() should apply to a non-empty callee");
  TLI.getLibFunc(*Callee, Func);
  assert(Func && "perform() is not expecting an empty function");

  if (performCallDomainErrorOnly(CI, Func) || performCallRangeErrorOnly(CI, Func))
    return true;
  return performCallErrors(CI, Func);
}

void LibCallsShrinkWrapLegacyPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addPreserved<DominatorTreeWrapperPass>();
  AU.addPreserved<GlobalsAAWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

static bool runImpl(Function &F, const TargetLibraryInfo &TLI,
                    DominatorTree *DT) {
  if (F.hasFnAttribute(Attribute::OptimizeForSize))
    return false;
  LibCallsShrinkWrap CCDCE(TLI, DT);
  CCDCE.visit(F);
  bool Changed = CCDCE.perform();

// Verify the dominator after we've updated it locally.
  assert(!DT || DT->verify(DominatorTree::VerificationLevel::Fast));
  return Changed;
}

bool LibCallsShrinkWrapLegacyPass::runOnFunction(Function &F) {
  auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;
  return runImpl(F, TLI, DT);
}

namespace llvm {
char &LibCallsShrinkWrapPassID = LibCallsShrinkWrapLegacyPass::ID;

// Public interface to LibCallsShrinkWrap pass.
FunctionPass *createLibCallsShrinkWrapPass() {
  return new LibCallsShrinkWrapLegacyPass();
}

PreservedAnalyses LibCallsShrinkWrapPass::run(Function &F,
                                              FunctionAnalysisManager &FAM) {
  auto &TLI = FAM.getResult<TargetLibraryAnalysis>(F);
  auto *DT = FAM.getCachedResult<DominatorTreeAnalysis>(F);
  if (!runImpl(F, TLI, DT))
    return PreservedAnalyses::all();
  auto PA = PreservedAnalyses();
  PA.preserve<GlobalsAA>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
}
