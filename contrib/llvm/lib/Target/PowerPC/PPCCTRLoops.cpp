//===-- PPCCTRLoops.cpp - Identify and generate CTR loops -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass identifies loops where we can generate the PPC branch instructions
// that decrement and test the count register (CTR) (bdnz and friends).
//
// The pattern that defines the induction variable can changed depending on
// prior optimizations.  For example, the IndVarSimplify phase run by 'opt'
// normalizes induction variables, and the Loop Strength Reduction pass
// run by 'llc' may also make changes to the induction variable.
//
// Criteria for CTR loops:
//  - Countable loops (w/ ind. var for a trip count)
//  - Try inner-most loops first
//  - No nested CTR loops.
//  - No function calls in loops.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "PPCTargetTransformInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#ifndef NDEBUG
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "ctrloops"

#ifndef NDEBUG
static cl::opt<int> CTRLoopLimit("ppc-max-ctrloop", cl::Hidden, cl::init(-1));
#endif

// The latency of mtctr is only justified if there are more than 4
// comparisons that will be removed as a result.
static cl::opt<unsigned>
SmallCTRLoopThreshold("min-ctr-loop-threshold", cl::init(4), cl::Hidden,
                      cl::desc("Loops with a constant trip count smaller than "
                               "this value will not use the count register."));

STATISTIC(NumCTRLoops, "Number of loops converted to CTR loops");

namespace llvm {
  void initializePPCCTRLoopsPass(PassRegistry&);
#ifndef NDEBUG
  void initializePPCCTRLoopsVerifyPass(PassRegistry&);
#endif
}

namespace {
  struct PPCCTRLoops : public FunctionPass {

#ifndef NDEBUG
    static int Counter;
#endif

  public:
    static char ID;

    PPCCTRLoops() : FunctionPass(ID) {
      initializePPCCTRLoopsPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.addPreserved<LoopInfoWrapperPass>();
      AU.addRequired<DominatorTreeWrapperPass>();
      AU.addPreserved<DominatorTreeWrapperPass>();
      AU.addRequired<ScalarEvolutionWrapperPass>();
      AU.addRequired<AssumptionCacheTracker>();
      AU.addRequired<TargetTransformInfoWrapperPass>();
    }

  private:
    bool mightUseCTR(BasicBlock *BB);
    bool convertToCTRLoop(Loop *L);

  private:
    const PPCTargetMachine *TM;
    const PPCSubtarget *STI;
    const PPCTargetLowering *TLI;
    const DataLayout *DL;
    const TargetLibraryInfo *LibInfo;
    const TargetTransformInfo *TTI;
    LoopInfo *LI;
    ScalarEvolution *SE;
    DominatorTree *DT;
    bool PreserveLCSSA;
    TargetSchedModel SchedModel;
  };

  char PPCCTRLoops::ID = 0;
#ifndef NDEBUG
  int PPCCTRLoops::Counter = 0;
#endif

#ifndef NDEBUG
  struct PPCCTRLoopsVerify : public MachineFunctionPass {
  public:
    static char ID;

    PPCCTRLoopsVerify() : MachineFunctionPass(ID) {
      initializePPCCTRLoopsVerifyPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    MachineDominatorTree *MDT;
  };

  char PPCCTRLoopsVerify::ID = 0;
#endif // NDEBUG
} // end anonymous namespace

INITIALIZE_PASS_BEGIN(PPCCTRLoops, "ppc-ctr-loops", "PowerPC CTR Loops",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(PPCCTRLoops, "ppc-ctr-loops", "PowerPC CTR Loops",
                    false, false)

FunctionPass *llvm::createPPCCTRLoops() { return new PPCCTRLoops(); }

#ifndef NDEBUG
INITIALIZE_PASS_BEGIN(PPCCTRLoopsVerify, "ppc-ctr-loops-verify",
                      "PowerPC CTR Loops Verify", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(PPCCTRLoopsVerify, "ppc-ctr-loops-verify",
                    "PowerPC CTR Loops Verify", false, false)

FunctionPass *llvm::createPPCCTRLoopsVerify() {
  return new PPCCTRLoopsVerify();
}
#endif // NDEBUG

bool PPCCTRLoops::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  TM = &TPC->getTM<PPCTargetMachine>();
  STI = TM->getSubtargetImpl(F);
  TLI = STI->getTargetLowering();

  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  DL = &F.getParent()->getDataLayout();
  auto *TLIP = getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
  LibInfo = TLIP ? &TLIP->getTLI() : nullptr;
  PreserveLCSSA = mustPreserveAnalysisID(LCSSAID);

  bool MadeChange = false;

  for (LoopInfo::iterator I = LI->begin(), E = LI->end();
       I != E; ++I) {
    Loop *L = *I;
    if (!L->getParentLoop())
      MadeChange |= convertToCTRLoop(L);
  }

  return MadeChange;
}

static bool isLargeIntegerTy(bool Is32Bit, Type *Ty) {
  if (IntegerType *ITy = dyn_cast<IntegerType>(Ty))
    return ITy->getBitWidth() > (Is32Bit ? 32U : 64U);

  return false;
}

// Determining the address of a TLS variable results in a function call in
// certain TLS models.
static bool memAddrUsesCTR(const PPCTargetMachine &TM, const Value *MemAddr) {
  const auto *GV = dyn_cast<GlobalValue>(MemAddr);
  if (!GV) {
    // Recurse to check for constants that refer to TLS global variables.
    if (const auto *CV = dyn_cast<Constant>(MemAddr))
      for (const auto &CO : CV->operands())
        if (memAddrUsesCTR(TM, CO))
          return true;

    return false;
  }

  if (!GV->isThreadLocal())
    return false;
  TLSModel::Model Model = TM.getTLSModel(GV);
  return Model == TLSModel::GeneralDynamic || Model == TLSModel::LocalDynamic;
}

// Loop through the inline asm constraints and look for something that clobbers
// ctr.
static bool asmClobbersCTR(InlineAsm *IA) {
  InlineAsm::ConstraintInfoVector CIV = IA->ParseConstraints();
  for (unsigned i = 0, ie = CIV.size(); i < ie; ++i) {
    InlineAsm::ConstraintInfo &C = CIV[i];
    if (C.Type != InlineAsm::isInput)
      for (unsigned j = 0, je = C.Codes.size(); j < je; ++j)
        if (StringRef(C.Codes[j]).equals_lower("{ctr}"))
          return true;
  }
  return false;
}

bool PPCCTRLoops::mightUseCTR(BasicBlock *BB) {
  for (BasicBlock::iterator J = BB->begin(), JE = BB->end();
       J != JE; ++J) {
    if (CallInst *CI = dyn_cast<CallInst>(J)) {
      // Inline ASM is okay, unless it clobbers the ctr register.
      if (InlineAsm *IA = dyn_cast<InlineAsm>(CI->getCalledValue())) {
        if (asmClobbersCTR(IA))
          return true;
        continue;
      }

      if (Function *F = CI->getCalledFunction()) {
        // Most intrinsics don't become function calls, but some might.
        // sin, cos, exp and log are always calls.
        unsigned Opcode = 0;
        if (F->getIntrinsicID() != Intrinsic::not_intrinsic) {
          switch (F->getIntrinsicID()) {
          default: continue;
          // If we have a call to ppc_is_decremented_ctr_nonzero, or ppc_mtctr
          // we're definitely using CTR.
          case Intrinsic::ppc_is_decremented_ctr_nonzero:
          case Intrinsic::ppc_mtctr:
            return true;

// VisualStudio defines setjmp as _setjmp
#if defined(_MSC_VER) && defined(setjmp) && \
                       !defined(setjmp_undefined_for_msvc)
#  pragma push_macro("setjmp")
#  undef setjmp
#  define setjmp_undefined_for_msvc
#endif

          case Intrinsic::setjmp:

#if defined(_MSC_VER) && defined(setjmp_undefined_for_msvc)
 // let's return it to _setjmp state
#  pragma pop_macro("setjmp")
#  undef setjmp_undefined_for_msvc
#endif

          case Intrinsic::longjmp:

          // Exclude eh_sjlj_setjmp; we don't need to exclude eh_sjlj_longjmp
          // because, although it does clobber the counter register, the
          // control can't then return to inside the loop unless there is also
          // an eh_sjlj_setjmp.
          case Intrinsic::eh_sjlj_setjmp:

          case Intrinsic::memcpy:
          case Intrinsic::memmove:
          case Intrinsic::memset:
          case Intrinsic::powi:
          case Intrinsic::log:
          case Intrinsic::log2:
          case Intrinsic::log10:
          case Intrinsic::exp:
          case Intrinsic::exp2:
          case Intrinsic::pow:
          case Intrinsic::sin:
          case Intrinsic::cos:
            return true;
          case Intrinsic::copysign:
            if (CI->getArgOperand(0)->getType()->getScalarType()->
                isPPC_FP128Ty())
              return true;
            else
              continue; // ISD::FCOPYSIGN is never a library call.
          case Intrinsic::sqrt:               Opcode = ISD::FSQRT;      break;
          case Intrinsic::floor:              Opcode = ISD::FFLOOR;     break;
          case Intrinsic::ceil:               Opcode = ISD::FCEIL;      break;
          case Intrinsic::trunc:              Opcode = ISD::FTRUNC;     break;
          case Intrinsic::rint:               Opcode = ISD::FRINT;      break;
          case Intrinsic::nearbyint:          Opcode = ISD::FNEARBYINT; break;
          case Intrinsic::round:              Opcode = ISD::FROUND;     break;
          case Intrinsic::minnum:             Opcode = ISD::FMINNUM;    break;
          case Intrinsic::maxnum:             Opcode = ISD::FMAXNUM;    break;
          case Intrinsic::umul_with_overflow: Opcode = ISD::UMULO;      break;
          case Intrinsic::smul_with_overflow: Opcode = ISD::SMULO;      break;
          }
        }

        // PowerPC does not use [US]DIVREM or other library calls for
        // operations on regular types which are not otherwise library calls
        // (i.e. soft float or atomics). If adapting for targets that do,
        // additional care is required here.

        LibFunc Func;
        if (!F->hasLocalLinkage() && F->hasName() && LibInfo &&
            LibInfo->getLibFunc(F->getName(), Func) &&
            LibInfo->hasOptimizedCodeGen(Func)) {
          // Non-read-only functions are never treated as intrinsics.
          if (!CI->onlyReadsMemory())
            return true;

          // Conversion happens only for FP calls.
          if (!CI->getArgOperand(0)->getType()->isFloatingPointTy())
            return true;

          switch (Func) {
          default: return true;
          case LibFunc_copysign:
          case LibFunc_copysignf:
            continue; // ISD::FCOPYSIGN is never a library call.
          case LibFunc_copysignl:
            return true;
          case LibFunc_fabs:
          case LibFunc_fabsf:
          case LibFunc_fabsl:
            continue; // ISD::FABS is never a library call.
          case LibFunc_sqrt:
          case LibFunc_sqrtf:
          case LibFunc_sqrtl:
            Opcode = ISD::FSQRT; break;
          case LibFunc_floor:
          case LibFunc_floorf:
          case LibFunc_floorl:
            Opcode = ISD::FFLOOR; break;
          case LibFunc_nearbyint:
          case LibFunc_nearbyintf:
          case LibFunc_nearbyintl:
            Opcode = ISD::FNEARBYINT; break;
          case LibFunc_ceil:
          case LibFunc_ceilf:
          case LibFunc_ceill:
            Opcode = ISD::FCEIL; break;
          case LibFunc_rint:
          case LibFunc_rintf:
          case LibFunc_rintl:
            Opcode = ISD::FRINT; break;
          case LibFunc_round:
          case LibFunc_roundf:
          case LibFunc_roundl:
            Opcode = ISD::FROUND; break;
          case LibFunc_trunc:
          case LibFunc_truncf:
          case LibFunc_truncl:
            Opcode = ISD::FTRUNC; break;
          case LibFunc_fmin:
          case LibFunc_fminf:
          case LibFunc_fminl:
            Opcode = ISD::FMINNUM; break;
          case LibFunc_fmax:
          case LibFunc_fmaxf:
          case LibFunc_fmaxl:
            Opcode = ISD::FMAXNUM; break;
          }
        }

        if (Opcode) {
          EVT EVTy =
              TLI->getValueType(*DL, CI->getArgOperand(0)->getType(), true);

          if (EVTy == MVT::Other)
            return true;

          if (TLI->isOperationLegalOrCustom(Opcode, EVTy))
            continue;
          else if (EVTy.isVector() &&
                   TLI->isOperationLegalOrCustom(Opcode, EVTy.getScalarType()))
            continue;

          return true;
        }
      }

      return true;
    } else if (isa<BinaryOperator>(J) &&
               J->getType()->getScalarType()->isPPC_FP128Ty()) {
      // Most operations on ppc_f128 values become calls.
      return true;
    } else if (isa<UIToFPInst>(J) || isa<SIToFPInst>(J) ||
               isa<FPToUIInst>(J) || isa<FPToSIInst>(J)) {
      CastInst *CI = cast<CastInst>(J);
      if (CI->getSrcTy()->getScalarType()->isPPC_FP128Ty() ||
          CI->getDestTy()->getScalarType()->isPPC_FP128Ty() ||
          isLargeIntegerTy(!TM->isPPC64(), CI->getSrcTy()->getScalarType()) ||
          isLargeIntegerTy(!TM->isPPC64(), CI->getDestTy()->getScalarType()))
        return true;
    } else if (isLargeIntegerTy(!TM->isPPC64(),
                                J->getType()->getScalarType()) &&
               (J->getOpcode() == Instruction::UDiv ||
                J->getOpcode() == Instruction::SDiv ||
                J->getOpcode() == Instruction::URem ||
                J->getOpcode() == Instruction::SRem)) {
      return true;
    } else if (!TM->isPPC64() &&
               isLargeIntegerTy(false, J->getType()->getScalarType()) &&
               (J->getOpcode() == Instruction::Shl ||
                J->getOpcode() == Instruction::AShr ||
                J->getOpcode() == Instruction::LShr)) {
      // Only on PPC32, for 128-bit integers (specifically not 64-bit
      // integers), these might be runtime calls.
      return true;
    } else if (isa<IndirectBrInst>(J) || isa<InvokeInst>(J)) {
      // On PowerPC, indirect jumps use the counter register.
      return true;
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(J)) {
      if (SI->getNumCases() + 1 >= (unsigned)TLI->getMinimumJumpTableEntries())
        return true;
    }

    // FREM is always a call.
    if (J->getOpcode() == Instruction::FRem)
      return true;

    if (STI->useSoftFloat()) {
      switch(J->getOpcode()) {
      case Instruction::FAdd:
      case Instruction::FSub:
      case Instruction::FMul:
      case Instruction::FDiv:
      case Instruction::FPTrunc:
      case Instruction::FPExt:
      case Instruction::FPToUI:
      case Instruction::FPToSI:
      case Instruction::UIToFP:
      case Instruction::SIToFP:
      case Instruction::FCmp:
        return true;
      }
    }

    for (Value *Operand : J->operands())
      if (memAddrUsesCTR(*TM, Operand))
        return true;
  }

  return false;
}
bool PPCCTRLoops::convertToCTRLoop(Loop *L) {
  bool MadeChange = false;

  // Do not convert small short loops to CTR loop.
  unsigned ConstTripCount = SE->getSmallConstantTripCount(L);
  if (ConstTripCount && ConstTripCount < SmallCTRLoopThreshold) {
    SmallPtrSet<const Value *, 32> EphValues;
    auto AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(
        *L->getHeader()->getParent());
    CodeMetrics::collectEphemeralValues(L, &AC, EphValues);
    CodeMetrics Metrics;
    for (BasicBlock *BB : L->blocks())
      Metrics.analyzeBasicBlock(BB, *TTI, EphValues);
    // 6 is an approximate latency for the mtctr instruction.
    if (Metrics.NumInsts <= (6 * SchedModel.getIssueWidth()))
      return false;
  }

  // Process nested loops first.
  for (Loop::iterator I = L->begin(), E = L->end(); I != E; ++I) {
    MadeChange |= convertToCTRLoop(*I);
    LLVM_DEBUG(dbgs() << "Nested loop converted\n");
  }

  // If a nested loop has been converted, then we can't convert this loop.
  if (MadeChange)
    return MadeChange;

  // Bail out if the loop has irreducible control flow.
  LoopBlocksRPO RPOT(L);
  RPOT.perform(LI);
  if (containsIrreducibleCFG<const BasicBlock *>(RPOT, *LI))
    return false;

#ifndef NDEBUG
  // Stop trying after reaching the limit (if any).
  int Limit = CTRLoopLimit;
  if (Limit >= 0) {
    if (Counter >= CTRLoopLimit)
      return false;
    Counter++;
  }
#endif

  // We don't want to spill/restore the counter register, and so we don't
  // want to use the counter register if the loop contains calls.
  for (Loop::block_iterator I = L->block_begin(), IE = L->block_end();
       I != IE; ++I)
    if (mightUseCTR(*I))
      return MadeChange;

  SmallVector<BasicBlock*, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // If there is an exit edge known to be frequently taken,
  // we should not transform this loop.
  for (auto &BB : ExitingBlocks) {
    Instruction *TI = BB->getTerminator();
    if (!TI) continue;

    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      uint64_t TrueWeight = 0, FalseWeight = 0;
      if (!BI->isConditional() ||
          !BI->extractProfMetadata(TrueWeight, FalseWeight))
        continue;

      // If the exit path is more frequent than the loop path,
      // we return here without further analysis for this loop.
      bool TrueIsExit = !L->contains(BI->getSuccessor(0));
      if (( TrueIsExit && FalseWeight < TrueWeight) ||
          (!TrueIsExit && FalseWeight > TrueWeight))
        return MadeChange;
    }
  }

  BasicBlock *CountedExitBlock = nullptr;
  const SCEV *ExitCount = nullptr;
  BranchInst *CountedExitBranch = nullptr;
  for (SmallVectorImpl<BasicBlock *>::iterator I = ExitingBlocks.begin(),
       IE = ExitingBlocks.end(); I != IE; ++I) {
    const SCEV *EC = SE->getExitCount(L, *I);
    LLVM_DEBUG(dbgs() << "Exit Count for " << *L << " from block "
                      << (*I)->getName() << ": " << *EC << "\n");
    if (isa<SCEVCouldNotCompute>(EC))
      continue;
    if (const SCEVConstant *ConstEC = dyn_cast<SCEVConstant>(EC)) {
      if (ConstEC->getValue()->isZero())
        continue;
    } else if (!SE->isLoopInvariant(EC, L))
      continue;

    if (SE->getTypeSizeInBits(EC->getType()) > (TM->isPPC64() ? 64 : 32))
      continue;

    // If this exiting block is contained in a nested loop, it is not eligible
    // for insertion of the branch-and-decrement since the inner loop would
    // end up messing up the value in the CTR.
    if (LI->getLoopFor(*I) != L)
      continue;

    // We now have a loop-invariant count of loop iterations (which is not the
    // constant zero) for which we know that this loop will not exit via this
    // existing block.

    // We need to make sure that this block will run on every loop iteration.
    // For this to be true, we must dominate all blocks with backedges. Such
    // blocks are in-loop predecessors to the header block.
    bool NotAlways = false;
    for (pred_iterator PI = pred_begin(L->getHeader()),
         PIE = pred_end(L->getHeader()); PI != PIE; ++PI) {
      if (!L->contains(*PI))
        continue;

      if (!DT->dominates(*I, *PI)) {
        NotAlways = true;
        break;
      }
    }

    if (NotAlways)
      continue;

    // Make sure this blocks ends with a conditional branch.
    Instruction *TI = (*I)->getTerminator();
    if (!TI)
      continue;

    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      if (!BI->isConditional())
        continue;

      CountedExitBranch = BI;
    } else
      continue;

    // Note that this block may not be the loop latch block, even if the loop
    // has a latch block.
    CountedExitBlock = *I;
    ExitCount = EC;
    break;
  }

  if (!CountedExitBlock)
    return MadeChange;

  BasicBlock *Preheader = L->getLoopPreheader();

  // If we don't have a preheader, then insert one. If we already have a
  // preheader, then we can use it (except if the preheader contains a use of
  // the CTR register because some such uses might be reordered by the
  // selection DAG after the mtctr instruction).
  if (!Preheader || mightUseCTR(Preheader))
    Preheader = InsertPreheaderForLoop(L, DT, LI, PreserveLCSSA);
  if (!Preheader)
    return MadeChange;

  LLVM_DEBUG(dbgs() << "Preheader for exit count: " << Preheader->getName()
                    << "\n");

  // Insert the count into the preheader and replace the condition used by the
  // selected branch.
  MadeChange = true;

  SCEVExpander SCEVE(*SE, *DL, "loopcnt");
  LLVMContext &C = SE->getContext();
  Type *CountType = TM->isPPC64() ? Type::getInt64Ty(C) : Type::getInt32Ty(C);
  if (!ExitCount->getType()->isPointerTy() &&
      ExitCount->getType() != CountType)
    ExitCount = SE->getZeroExtendExpr(ExitCount, CountType);
  ExitCount = SE->getAddExpr(ExitCount, SE->getOne(CountType));
  Value *ECValue =
      SCEVE.expandCodeFor(ExitCount, CountType, Preheader->getTerminator());

  IRBuilder<> CountBuilder(Preheader->getTerminator());
  Module *M = Preheader->getParent()->getParent();
  Value *MTCTRFunc = Intrinsic::getDeclaration(M, Intrinsic::ppc_mtctr,
                                               CountType);
  CountBuilder.CreateCall(MTCTRFunc, ECValue);

  IRBuilder<> CondBuilder(CountedExitBranch);
  Value *DecFunc =
    Intrinsic::getDeclaration(M, Intrinsic::ppc_is_decremented_ctr_nonzero);
  Value *NewCond = CondBuilder.CreateCall(DecFunc, {});
  Value *OldCond = CountedExitBranch->getCondition();
  CountedExitBranch->setCondition(NewCond);

  // The false branch must exit the loop.
  if (!L->contains(CountedExitBranch->getSuccessor(0)))
    CountedExitBranch->swapSuccessors();

  // The old condition may be dead now, and may have even created a dead PHI
  // (the original induction variable).
  RecursivelyDeleteTriviallyDeadInstructions(OldCond);
  // Run through the basic blocks of the loop and see if any of them have dead
  // PHIs that can be removed.
  for (auto I : L->blocks())
    DeleteDeadPHIs(I);

  ++NumCTRLoops;
  return MadeChange;
}

#ifndef NDEBUG
static bool clobbersCTR(const MachineInstr &MI) {
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg()) {
      if (MO.isDef() && (MO.getReg() == PPC::CTR || MO.getReg() == PPC::CTR8))
        return true;
    } else if (MO.isRegMask()) {
      if (MO.clobbersPhysReg(PPC::CTR) || MO.clobbersPhysReg(PPC::CTR8))
        return true;
    }
  }

  return false;
}

static bool verifyCTRBranch(MachineBasicBlock *MBB,
                            MachineBasicBlock::iterator I) {
  MachineBasicBlock::iterator BI = I;
  SmallSet<MachineBasicBlock *, 16>   Visited;
  SmallVector<MachineBasicBlock *, 8> Preds;
  bool CheckPreds;

  if (I == MBB->begin()) {
    Visited.insert(MBB);
    goto queue_preds;
  } else
    --I;

check_block:
  Visited.insert(MBB);
  if (I == MBB->end())
    goto queue_preds;

  CheckPreds = true;
  for (MachineBasicBlock::iterator IE = MBB->begin();; --I) {
    unsigned Opc = I->getOpcode();
    if (Opc == PPC::MTCTRloop || Opc == PPC::MTCTR8loop) {
      CheckPreds = false;
      break;
    }

    if (I != BI && clobbersCTR(*I)) {
      LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " (" << MBB->getFullName()
                        << ") instruction " << *I
                        << " clobbers CTR, invalidating "
                        << printMBBReference(*BI->getParent()) << " ("
                        << BI->getParent()->getFullName() << ") instruction "
                        << *BI << "\n");
      return false;
    }

    if (I == IE)
      break;
  }

  if (!CheckPreds && Preds.empty())
    return true;

  if (CheckPreds) {
queue_preds:
    if (MachineFunction::iterator(MBB) == MBB->getParent()->begin()) {
      LLVM_DEBUG(dbgs() << "Unable to find a MTCTR instruction for "
                        << printMBBReference(*BI->getParent()) << " ("
                        << BI->getParent()->getFullName() << ") instruction "
                        << *BI << "\n");
      return false;
    }

    for (MachineBasicBlock::pred_iterator PI = MBB->pred_begin(),
         PIE = MBB->pred_end(); PI != PIE; ++PI)
      Preds.push_back(*PI);
  }

  do {
    MBB = Preds.pop_back_val();
    if (!Visited.count(MBB)) {
      I = MBB->getLastNonDebugInstr();
      goto check_block;
    }
  } while (!Preds.empty());

  return true;
}

bool PPCCTRLoopsVerify::runOnMachineFunction(MachineFunction &MF) {
  MDT = &getAnalysis<MachineDominatorTree>();

  // Verify that all bdnz/bdz instructions are dominated by a loop mtctr before
  // any other instructions that might clobber the ctr register.
  for (MachineFunction::iterator I = MF.begin(), IE = MF.end();
       I != IE; ++I) {
    MachineBasicBlock *MBB = &*I;
    if (!MDT->isReachableFromEntry(MBB))
      continue;

    for (MachineBasicBlock::iterator MII = MBB->getFirstTerminator(),
      MIIE = MBB->end(); MII != MIIE; ++MII) {
      unsigned Opc = MII->getOpcode();
      if (Opc == PPC::BDNZ8 || Opc == PPC::BDNZ ||
          Opc == PPC::BDZ8  || Opc == PPC::BDZ)
        if (!verifyCTRBranch(MBB, MII))
          llvm_unreachable("Invalid PPC CTR loop!");
    }
  }

  return false;
}
#endif // NDEBUG
