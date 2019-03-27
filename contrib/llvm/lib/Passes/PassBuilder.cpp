//===- Parsing, selection, and construction of pass pipelines -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the implementation of the PassBuilder based on our
/// static pass registry as well as related functionality. It also provides
/// helpers to aid in analyzing, debugging, and testing passes and pass
/// pipelines.
///
//===----------------------------------------------------------------------===//

#include "llvm/Passes/PassBuilder.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasAnalysisEvaluator.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/CFLAndersAliasAnalysis.h"
#include "llvm/Analysis/CFLSteensAliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/IVUsers.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PhiValues.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/CodeGen/PreISelIntrinsicLowering.h"
#include "llvm/CodeGen/UnreachableBlockElim.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Regex.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/ArgumentPromotion.h"
#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Transforms/IPO/ConstantMerge.h"
#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/ElimAvailExtern.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/GlobalSplit.h"
#include "llvm/Transforms/IPO/HotColdSplitting.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/LowerTypeTests.h"
#include "llvm/Transforms/IPO/PartialInlining.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/IPO/SyntheticCountsPropagation.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/Transforms/Instrumentation/CGProfile.h"
#include "llvm/Transforms/Instrumentation/ControlHeightReduction.h"
#include "llvm/Transforms/Instrumentation/GCOVProfiler.h"
#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/Transforms/Instrumentation/MemorySanitizer.h"
#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Scalar/ADCE.h"
#include "llvm/Transforms/Scalar/AlignmentFromAssumptions.h"
#include "llvm/Transforms/Scalar/BDCE.h"
#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/Transforms/Scalar/ConstantHoisting.h"
#include "llvm/Transforms/Scalar/CorrelatedValuePropagation.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/DivRemPairs.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/Float2Int.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/GuardWidening.h"
#include "llvm/Transforms/Scalar/IVUsersPrinter.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/InductiveRangeCheckElimination.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/JumpThreading.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopAccessAnalysisPrinter.h"
#include "llvm/Transforms/Scalar/LoopDataPrefetch.h"
#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/Transforms/Scalar/LoopDistribute.h"
#include "llvm/Transforms/Scalar/LoopIdiomRecognize.h"
#include "llvm/Transforms/Scalar/LoopInstSimplify.h"
#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Scalar/LoopPredication.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/LoopStrengthReduce.h"
#include "llvm/Transforms/Scalar/LoopUnrollAndJamPass.h"
#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/Transforms/Scalar/LowerAtomic.h"
#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/Transforms/Scalar/LowerGuardIntrinsic.h"
#include "llvm/Transforms/Scalar/MakeGuardsExplicit.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/Transforms/Scalar/NaryReassociate.h"
#include "llvm/Transforms/Scalar/NewGVN.h"
#include "llvm/Transforms/Scalar/PartiallyInlineLibCalls.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/RewriteStatepointsForGC.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/Sink.h"
#include "llvm/Transforms/Scalar/SpeculateAroundPHIs.h"
#include "llvm/Transforms/Scalar/SpeculativeExecution.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Scalar/WarnMissedTransforms.h"
#include "llvm/Transforms/Utils/AddDiscriminators.h"
#include "llvm/Transforms/Utils/BreakCriticalEdges.h"
#include "llvm/Transforms/Utils/CanonicalizeAliases.h"
#include "llvm/Transforms/Utils/EntryExitInstrumenter.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LibCallsShrinkWrap.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LowerInvoke.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/Transforms/Utils/SymbolRewriter.h"
#include "llvm/Transforms/Vectorize/LoadStoreVectorizer.h"
#include "llvm/Transforms/Vectorize/LoopVectorize.h"
#include "llvm/Transforms/Vectorize/SLPVectorizer.h"

using namespace llvm;

static cl::opt<unsigned> MaxDevirtIterations("pm-max-devirt-iterations",
                                             cl::ReallyHidden, cl::init(4));
static cl::opt<bool>
    RunPartialInlining("enable-npm-partial-inlining", cl::init(false),
                       cl::Hidden, cl::ZeroOrMore,
                       cl::desc("Run Partial inlinining pass"));

static cl::opt<bool>
    RunNewGVN("enable-npm-newgvn", cl::init(false),
              cl::Hidden, cl::ZeroOrMore,
              cl::desc("Run NewGVN instead of GVN"));

static cl::opt<bool> EnableEarlyCSEMemSSA(
    "enable-npm-earlycse-memssa", cl::init(true), cl::Hidden,
    cl::desc("Enable the EarlyCSE w/ MemorySSA pass for the new PM (default = on)"));

static cl::opt<bool> EnableGVNHoist(
    "enable-npm-gvn-hoist", cl::init(false), cl::Hidden,
    cl::desc("Enable the GVN hoisting pass for the new PM (default = off)"));

static cl::opt<bool> EnableGVNSink(
    "enable-npm-gvn-sink", cl::init(false), cl::Hidden,
    cl::desc("Enable the GVN hoisting pass for the new PM (default = off)"));

static cl::opt<bool> EnableUnrollAndJam(
    "enable-npm-unroll-and-jam", cl::init(false), cl::Hidden,
    cl::desc("Enable the Unroll and Jam pass for the new PM (default = off)"));

static cl::opt<bool> EnableSyntheticCounts(
    "enable-npm-synthetic-counts", cl::init(false), cl::Hidden, cl::ZeroOrMore,
    cl::desc("Run synthetic function entry count generation "
             "pass"));

static Regex DefaultAliasRegex(
    "^(default|thinlto-pre-link|thinlto|lto-pre-link|lto)<(O[0123sz])>$");

static cl::opt<bool>
    EnableCHR("enable-chr-npm", cl::init(true), cl::Hidden,
              cl::desc("Enable control height reduction optimization (CHR)"));

extern cl::opt<bool> EnableHotColdSplit;

static bool isOptimizingForSize(PassBuilder::OptimizationLevel Level) {
  switch (Level) {
  case PassBuilder::O0:
  case PassBuilder::O1:
  case PassBuilder::O2:
  case PassBuilder::O3:
    return false;

  case PassBuilder::Os:
  case PassBuilder::Oz:
    return true;
  }
  llvm_unreachable("Invalid optimization level!");
}

namespace {

/// No-op module pass which does nothing.
struct NoOpModulePass {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "NoOpModulePass"; }
};

/// No-op module analysis.
class NoOpModuleAnalysis : public AnalysisInfoMixin<NoOpModuleAnalysis> {
  friend AnalysisInfoMixin<NoOpModuleAnalysis>;
  static AnalysisKey Key;

public:
  struct Result {};
  Result run(Module &, ModuleAnalysisManager &) { return Result(); }
  static StringRef name() { return "NoOpModuleAnalysis"; }
};

/// No-op CGSCC pass which does nothing.
struct NoOpCGSCCPass {
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &,
                        LazyCallGraph &, CGSCCUpdateResult &UR) {
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "NoOpCGSCCPass"; }
};

/// No-op CGSCC analysis.
class NoOpCGSCCAnalysis : public AnalysisInfoMixin<NoOpCGSCCAnalysis> {
  friend AnalysisInfoMixin<NoOpCGSCCAnalysis>;
  static AnalysisKey Key;

public:
  struct Result {};
  Result run(LazyCallGraph::SCC &, CGSCCAnalysisManager &, LazyCallGraph &G) {
    return Result();
  }
  static StringRef name() { return "NoOpCGSCCAnalysis"; }
};

/// No-op function pass which does nothing.
struct NoOpFunctionPass {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "NoOpFunctionPass"; }
};

/// No-op function analysis.
class NoOpFunctionAnalysis : public AnalysisInfoMixin<NoOpFunctionAnalysis> {
  friend AnalysisInfoMixin<NoOpFunctionAnalysis>;
  static AnalysisKey Key;

public:
  struct Result {};
  Result run(Function &, FunctionAnalysisManager &) { return Result(); }
  static StringRef name() { return "NoOpFunctionAnalysis"; }
};

/// No-op loop pass which does nothing.
struct NoOpLoopPass {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &,
                        LoopStandardAnalysisResults &, LPMUpdater &) {
    return PreservedAnalyses::all();
  }
  static StringRef name() { return "NoOpLoopPass"; }
};

/// No-op loop analysis.
class NoOpLoopAnalysis : public AnalysisInfoMixin<NoOpLoopAnalysis> {
  friend AnalysisInfoMixin<NoOpLoopAnalysis>;
  static AnalysisKey Key;

public:
  struct Result {};
  Result run(Loop &, LoopAnalysisManager &, LoopStandardAnalysisResults &) {
    return Result();
  }
  static StringRef name() { return "NoOpLoopAnalysis"; }
};

AnalysisKey NoOpModuleAnalysis::Key;
AnalysisKey NoOpCGSCCAnalysis::Key;
AnalysisKey NoOpFunctionAnalysis::Key;
AnalysisKey NoOpLoopAnalysis::Key;

} // End anonymous namespace.

void PassBuilder::invokePeepholeEPCallbacks(
    FunctionPassManager &FPM, PassBuilder::OptimizationLevel Level) {
  for (auto &C : PeepholeEPCallbacks)
    C(FPM, Level);
}

void PassBuilder::registerModuleAnalyses(ModuleAnalysisManager &MAM) {
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  MAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : ModuleAnalysisRegistrationCallbacks)
    C(MAM);
}

void PassBuilder::registerCGSCCAnalyses(CGSCCAnalysisManager &CGAM) {
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  CGAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : CGSCCAnalysisRegistrationCallbacks)
    C(CGAM);
}

void PassBuilder::registerFunctionAnalyses(FunctionAnalysisManager &FAM) {
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  FAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : FunctionAnalysisRegistrationCallbacks)
    C(FAM);
}

void PassBuilder::registerLoopAnalyses(LoopAnalysisManager &LAM) {
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  LAM.registerPass([&] { return CREATE_PASS; });
#include "PassRegistry.def"

  for (auto &C : LoopAnalysisRegistrationCallbacks)
    C(LAM);
}

FunctionPassManager
PassBuilder::buildFunctionSimplificationPipeline(OptimizationLevel Level,
                                                 ThinLTOPhase Phase,
                                                 bool DebugLogging) {
  assert(Level != O0 && "Must request optimizations!");
  FunctionPassManager FPM(DebugLogging);

  // Form SSA out of local memory accesses after breaking apart aggregates into
  // scalars.
  FPM.addPass(SROA());

  // Catch trivial redundancies
  FPM.addPass(EarlyCSEPass(EnableEarlyCSEMemSSA));

  // Hoisting of scalars and load expressions.
  if (EnableGVNHoist)
    FPM.addPass(GVNHoistPass());

  // Global value numbering based sinking.
  if (EnableGVNSink) {
    FPM.addPass(GVNSinkPass());
    FPM.addPass(SimplifyCFGPass());
  }

  // Speculative execution if the target has divergent branches; otherwise nop.
  FPM.addPass(SpeculativeExecutionPass());

  // Optimize based on known information about branches, and cleanup afterward.
  FPM.addPass(JumpThreadingPass());
  FPM.addPass(CorrelatedValuePropagationPass());
  FPM.addPass(SimplifyCFGPass());
  if (Level == O3)
    FPM.addPass(AggressiveInstCombinePass());
  FPM.addPass(InstCombinePass());

  if (!isOptimizingForSize(Level))
    FPM.addPass(LibCallsShrinkWrapPass());

  invokePeepholeEPCallbacks(FPM, Level);

  // For PGO use pipeline, try to optimize memory intrinsics such as memcpy
  // using the size value profile. Don't perform this when optimizing for size.
  if (PGOOpt && !PGOOpt->ProfileUseFile.empty() &&
      !isOptimizingForSize(Level))
    FPM.addPass(PGOMemOPSizeOpt());

  FPM.addPass(TailCallElimPass());
  FPM.addPass(SimplifyCFGPass());

  // Form canonically associated expression trees, and simplify the trees using
  // basic mathematical properties. For example, this will form (nearly)
  // minimal multiplication trees.
  FPM.addPass(ReassociatePass());

  // Add the primary loop simplification pipeline.
  // FIXME: Currently this is split into two loop pass pipelines because we run
  // some function passes in between them. These can and should be removed
  // and/or replaced by scheduling the loop pass equivalents in the correct
  // positions. But those equivalent passes aren't powerful enough yet.
  // Specifically, `SimplifyCFGPass` and `InstCombinePass` are currently still
  // used. We have `LoopSimplifyCFGPass` which isn't yet powerful enough yet to
  // fully replace `SimplifyCFGPass`, and the closest to the other we have is
  // `LoopInstSimplify`.
  LoopPassManager LPM1(DebugLogging), LPM2(DebugLogging);

  // Simplify the loop body. We do this initially to clean up after other loop
  // passes run, either when iterating on a loop or on inner loops with
  // implications on the outer loop.
  LPM1.addPass(LoopInstSimplifyPass());
  LPM1.addPass(LoopSimplifyCFGPass());

  // Rotate Loop - disable header duplication at -Oz
  LPM1.addPass(LoopRotatePass(Level != Oz));
  LPM1.addPass(LICMPass());
  LPM1.addPass(SimpleLoopUnswitchPass());
  LPM2.addPass(IndVarSimplifyPass());
  LPM2.addPass(LoopIdiomRecognizePass());

  for (auto &C : LateLoopOptimizationsEPCallbacks)
    C(LPM2, Level);

  LPM2.addPass(LoopDeletionPass());
  // Do not enable unrolling in PreLinkThinLTO phase during sample PGO
  // because it changes IR to makes profile annotation in back compile
  // inaccurate.
  if (Phase != ThinLTOPhase::PreLink ||
      !PGOOpt || PGOOpt->SampleProfileFile.empty())
    LPM2.addPass(LoopFullUnrollPass(Level));

  for (auto &C : LoopOptimizerEndEPCallbacks)
    C(LPM2, Level);

  // We provide the opt remark emitter pass for LICM to use. We only need to do
  // this once as it is immutable.
  FPM.addPass(RequireAnalysisPass<OptimizationRemarkEmitterAnalysis, Function>());
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM1), DebugLogging));
  FPM.addPass(SimplifyCFGPass());
  FPM.addPass(InstCombinePass());
  FPM.addPass(createFunctionToLoopPassAdaptor(std::move(LPM2), DebugLogging));

  // Eliminate redundancies.
  if (Level != O1) {
    // These passes add substantial compile time so skip them at O1.
    FPM.addPass(MergedLoadStoreMotionPass());
    if (RunNewGVN)
      FPM.addPass(NewGVNPass());
    else
      FPM.addPass(GVN());
  }

  // Specially optimize memory movement as it doesn't look like dataflow in SSA.
  FPM.addPass(MemCpyOptPass());

  // Sparse conditional constant propagation.
  // FIXME: It isn't clear why we do this *after* loop passes rather than
  // before...
  FPM.addPass(SCCPPass());

  // Delete dead bit computations (instcombine runs after to fold away the dead
  // computations, and then ADCE will run later to exploit any new DCE
  // opportunities that creates).
  FPM.addPass(BDCEPass());

  // Run instcombine after redundancy and dead bit elimination to exploit
  // opportunities opened up by them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  // Re-consider control flow based optimizations after redundancy elimination,
  // redo DCE, etc.
  FPM.addPass(JumpThreadingPass());
  FPM.addPass(CorrelatedValuePropagationPass());
  FPM.addPass(DSEPass());
  FPM.addPass(createFunctionToLoopPassAdaptor(LICMPass(), DebugLogging));

  for (auto &C : ScalarOptimizerLateEPCallbacks)
    C(FPM, Level);

  // Finally, do an expensive DCE pass to catch all the dead code exposed by
  // the simplifications and basic cleanup after all the simplifications.
  FPM.addPass(ADCEPass());
  FPM.addPass(SimplifyCFGPass());
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  if (EnableCHR && Level == O3 && PGOOpt &&
      (!PGOOpt->ProfileUseFile.empty() || !PGOOpt->SampleProfileFile.empty()))
    FPM.addPass(ControlHeightReductionPass());

  return FPM;
}

void PassBuilder::addPGOInstrPasses(ModulePassManager &MPM, bool DebugLogging,
                                    PassBuilder::OptimizationLevel Level,
                                    bool RunProfileGen,
                                    std::string ProfileGenFile,
                                    std::string ProfileUseFile,
                                    std::string ProfileRemappingFile) {
  // Generally running simplification passes and the inliner with an high
  // threshold results in smaller executables, but there may be cases where
  // the size grows, so let's be conservative here and skip this simplification
  // at -Os/Oz.
  if (!isOptimizingForSize(Level)) {
    InlineParams IP;

    // In the old pass manager, this is a cl::opt. Should still this be one?
    IP.DefaultThreshold = 75;

    // FIXME: The hint threshold has the same value used by the regular inliner.
    // This should probably be lowered after performance testing.
    // FIXME: this comment is cargo culted from the old pass manager, revisit).
    IP.HintThreshold = 325;

    CGSCCPassManager CGPipeline(DebugLogging);

    CGPipeline.addPass(InlinerPass(IP));

    FunctionPassManager FPM;
    FPM.addPass(SROA());
    FPM.addPass(EarlyCSEPass());    // Catch trivial redundancies.
    FPM.addPass(SimplifyCFGPass()); // Merge & remove basic blocks.
    FPM.addPass(InstCombinePass()); // Combine silly sequences.
    invokePeepholeEPCallbacks(FPM, Level);

    CGPipeline.addPass(createCGSCCToFunctionPassAdaptor(std::move(FPM)));

    MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPipeline)));
  }

  // Delete anything that is now dead to make sure that we don't instrument
  // dead code. Instrumentation can end up keeping dead code around and
  // dramatically increase code size.
  MPM.addPass(GlobalDCEPass());

  if (RunProfileGen) {
    MPM.addPass(PGOInstrumentationGen());

    FunctionPassManager FPM;
    FPM.addPass(
        createFunctionToLoopPassAdaptor(LoopRotatePass(), DebugLogging));
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

    // Add the profile lowering pass.
    InstrProfOptions Options;
    if (!ProfileGenFile.empty())
      Options.InstrProfileOutput = ProfileGenFile;
    Options.DoCounterPromotion = true;
    MPM.addPass(InstrProfiling(Options));
  }

  if (!ProfileUseFile.empty())
    MPM.addPass(PGOInstrumentationUse(ProfileUseFile, ProfileRemappingFile));
}

static InlineParams
getInlineParamsFromOptLevel(PassBuilder::OptimizationLevel Level) {
  auto O3 = PassBuilder::O3;
  unsigned OptLevel = Level > O3 ? 2 : Level;
  unsigned SizeLevel = Level > O3 ? Level - O3 : 0;
  return getInlineParams(OptLevel, SizeLevel);
}

ModulePassManager
PassBuilder::buildModuleSimplificationPipeline(OptimizationLevel Level,
                                               ThinLTOPhase Phase,
                                               bool DebugLogging) {
  ModulePassManager MPM(DebugLogging);

  // Do basic inference of function attributes from known properties of system
  // libraries and other oracles.
  MPM.addPass(InferFunctionAttrsPass());

  // Create an early function pass manager to cleanup the output of the
  // frontend.
  FunctionPassManager EarlyFPM(DebugLogging);
  EarlyFPM.addPass(SimplifyCFGPass());
  EarlyFPM.addPass(SROA());
  EarlyFPM.addPass(EarlyCSEPass());
  EarlyFPM.addPass(LowerExpectIntrinsicPass());
  if (Level == O3)
    EarlyFPM.addPass(CallSiteSplittingPass());

  // In SamplePGO ThinLTO backend, we need instcombine before profile annotation
  // to convert bitcast to direct calls so that they can be inlined during the
  // profile annotation prepration step.
  // More details about SamplePGO design can be found in:
  // https://research.google.com/pubs/pub45290.html
  // FIXME: revisit how SampleProfileLoad/Inliner/ICP is structured.
  if (PGOOpt && !PGOOpt->SampleProfileFile.empty() &&
      Phase == ThinLTOPhase::PostLink)
    EarlyFPM.addPass(InstCombinePass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(EarlyFPM)));

  if (PGOOpt && !PGOOpt->SampleProfileFile.empty()) {
    // Annotate sample profile right after early FPM to ensure freshness of
    // the debug info.
    MPM.addPass(SampleProfileLoaderPass(PGOOpt->SampleProfileFile,
                                        PGOOpt->ProfileRemappingFile,
                                        Phase == ThinLTOPhase::PreLink));
    // Do not invoke ICP in the ThinLTOPrelink phase as it makes it hard
    // for the profile annotation to be accurate in the ThinLTO backend.
    if (Phase != ThinLTOPhase::PreLink)
      // We perform early indirect call promotion here, before globalopt.
      // This is important for the ThinLTO backend phase because otherwise
      // imported available_externally functions look unreferenced and are
      // removed.
      MPM.addPass(PGOIndirectCallPromotion(Phase == ThinLTOPhase::PostLink,
                                           true));
  }

  // Interprocedural constant propagation now that basic cleanup has occurred
  // and prior to optimizing globals.
  // FIXME: This position in the pipeline hasn't been carefully considered in
  // years, it should be re-analyzed.
  MPM.addPass(IPSCCPPass());

  // Attach metadata to indirect call sites indicating the set of functions
  // they may target at run-time. This should follow IPSCCP.
  MPM.addPass(CalledValuePropagationPass());

  // Optimize globals to try and fold them into constants.
  MPM.addPass(GlobalOptPass());

  // Promote any localized globals to SSA registers.
  // FIXME: Should this instead by a run of SROA?
  // FIXME: We should probably run instcombine and simplify-cfg afterward to
  // delete control flows that are dead once globals have been folded to
  // constants.
  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));

  // Remove any dead arguments exposed by cleanups and constand folding
  // globals.
  MPM.addPass(DeadArgumentEliminationPass());

  // Create a small function pass pipeline to cleanup after all the global
  // optimizations.
  FunctionPassManager GlobalCleanupPM(DebugLogging);
  GlobalCleanupPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(GlobalCleanupPM, Level);

  GlobalCleanupPM.addPass(SimplifyCFGPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(GlobalCleanupPM)));

  // Add all the requested passes for instrumentation PGO, if requested.
  if (PGOOpt && Phase != ThinLTOPhase::PostLink &&
      (!PGOOpt->ProfileGenFile.empty() || !PGOOpt->ProfileUseFile.empty())) {
    addPGOInstrPasses(MPM, DebugLogging, Level, PGOOpt->RunProfileGen,
                      PGOOpt->ProfileGenFile, PGOOpt->ProfileUseFile,
                      PGOOpt->ProfileRemappingFile);
    MPM.addPass(PGOIndirectCallPromotion(false, false));
  }

  // Synthesize function entry counts for non-PGO compilation.
  if (EnableSyntheticCounts && !PGOOpt)
    MPM.addPass(SyntheticCountsPropagation());

  // Require the GlobalsAA analysis for the module so we can query it within
  // the CGSCC pipeline.
  MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());

  // Require the ProfileSummaryAnalysis for the module so we can query it within
  // the inliner pass.
  MPM.addPass(RequireAnalysisPass<ProfileSummaryAnalysis, Module>());

  // Now begin the main postorder CGSCC pipeline.
  // FIXME: The current CGSCC pipeline has its origins in the legacy pass
  // manager and trying to emulate its precise behavior. Much of this doesn't
  // make a lot of sense and we should revisit the core CGSCC structure.
  CGSCCPassManager MainCGPipeline(DebugLogging);

  // Note: historically, the PruneEH pass was run first to deduce nounwind and
  // generally clean up exception handling overhead. It isn't clear this is
  // valuable as the inliner doesn't currently care whether it is inlining an
  // invoke or a call.

  // Run the inliner first. The theory is that we are walking bottom-up and so
  // the callees have already been fully optimized, and we want to inline them
  // into the callers so that our optimizations can reflect that.
  // For PreLinkThinLTO pass, we disable hot-caller heuristic for sample PGO
  // because it makes profile annotation in the backend inaccurate.
  InlineParams IP = getInlineParamsFromOptLevel(Level);
  if (Phase == ThinLTOPhase::PreLink &&
      PGOOpt && !PGOOpt->SampleProfileFile.empty())
    IP.HotCallSiteThreshold = 0;
  MainCGPipeline.addPass(InlinerPass(IP));

  // Now deduce any function attributes based in the current code.
  MainCGPipeline.addPass(PostOrderFunctionAttrsPass());

  // When at O3 add argument promotion to the pass pipeline.
  // FIXME: It isn't at all clear why this should be limited to O3.
  if (Level == O3)
    MainCGPipeline.addPass(ArgumentPromotionPass());

  // Lastly, add the core function simplification pipeline nested inside the
  // CGSCC walk.
  MainCGPipeline.addPass(createCGSCCToFunctionPassAdaptor(
      buildFunctionSimplificationPipeline(Level, Phase, DebugLogging)));

  // We only want to do hot cold splitting once for ThinLTO, during the
  // post-link ThinLTO.
  if (EnableHotColdSplit && Phase != ThinLTOPhase::PreLink)
    MPM.addPass(HotColdSplittingPass());

  for (auto &C : CGSCCOptimizerLateEPCallbacks)
    C(MainCGPipeline, Level);

  // We wrap the CGSCC pipeline in a devirtualization repeater. This will try
  // to detect when we devirtualize indirect calls and iterate the SCC passes
  // in that case to try and catch knock-on inlining or function attrs
  // opportunities. Then we add it to the module pipeline by walking the SCCs
  // in postorder (or bottom-up).
  MPM.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(createDevirtSCCRepeatedPass(
          std::move(MainCGPipeline), MaxDevirtIterations)));

  return MPM;
}

ModulePassManager
PassBuilder::buildModuleOptimizationPipeline(OptimizationLevel Level,
                                             bool DebugLogging) {
  ModulePassManager MPM(DebugLogging);

  // Optimize globals now that the module is fully simplified.
  MPM.addPass(GlobalOptPass());
  MPM.addPass(GlobalDCEPass());

  // Run partial inlining pass to partially inline functions that have
  // large bodies.
  if (RunPartialInlining)
    MPM.addPass(PartialInlinerPass());

  // Remove avail extern fns and globals definitions since we aren't compiling
  // an object file for later LTO. For LTO we want to preserve these so they
  // are eligible for inlining at link-time. Note if they are unreferenced they
  // will be removed by GlobalDCE later, so this only impacts referenced
  // available externally globals. Eventually they will be suppressed during
  // codegen, but eliminating here enables more opportunity for GlobalDCE as it
  // may make globals referenced by available external functions dead and saves
  // running remaining passes on the eliminated functions.
  MPM.addPass(EliminateAvailableExternallyPass());

  // Do RPO function attribute inference across the module to forward-propagate
  // attributes where applicable.
  // FIXME: Is this really an optimization rather than a canonicalization?
  MPM.addPass(ReversePostOrderFunctionAttrsPass());

  // Re-require GloblasAA here prior to function passes. This is particularly
  // useful as the above will have inlined, DCE'ed, and function-attr
  // propagated everything. We should at this point have a reasonably minimal
  // and richly annotated call graph. By computing aliasing and mod/ref
  // information for all local globals here, the late loop passes and notably
  // the vectorizer will be able to use them to help recognize vectorizable
  // memory operations.
  MPM.addPass(RequireAnalysisPass<GlobalsAA, Module>());

  FunctionPassManager OptimizePM(DebugLogging);
  OptimizePM.addPass(Float2IntPass());
  // FIXME: We need to run some loop optimizations to re-rotate loops after
  // simplify-cfg and others undo their rotation.

  // Optimize the loop execution. These passes operate on entire loop nests
  // rather than on each loop in an inside-out manner, and so they are actually
  // function passes.

  for (auto &C : VectorizerStartEPCallbacks)
    C(OptimizePM, Level);

  // First rotate loops that may have been un-rotated by prior passes.
  OptimizePM.addPass(
      createFunctionToLoopPassAdaptor(LoopRotatePass(), DebugLogging));

  // Distribute loops to allow partial vectorization.  I.e. isolate dependences
  // into separate loop that would otherwise inhibit vectorization.  This is
  // currently only performed for loops marked with the metadata
  // llvm.loop.distribute=true or when -enable-loop-distribute is specified.
  OptimizePM.addPass(LoopDistributePass());

  // Now run the core loop vectorizer.
  OptimizePM.addPass(LoopVectorizePass());

  // Eliminate loads by forwarding stores from the previous iteration to loads
  // of the current iteration.
  OptimizePM.addPass(LoopLoadEliminationPass());

  // Cleanup after the loop optimization passes.
  OptimizePM.addPass(InstCombinePass());

  // Now that we've formed fast to execute loop structures, we do further
  // optimizations. These are run afterward as they might block doing complex
  // analyses and transforms such as what are needed for loop vectorization.

  // Cleanup after loop vectorization, etc. Simplification passes like CVP and
  // GVN, loop transforms, and others have already run, so it's now better to
  // convert to more optimized IR using more aggressive simplify CFG options.
  // The extra sinking transform can create larger basic blocks, so do this
  // before SLP vectorization.
  OptimizePM.addPass(SimplifyCFGPass(SimplifyCFGOptions().
                                     forwardSwitchCondToPhi(true).
                                     convertSwitchToLookupTable(true).
                                     needCanonicalLoops(false).
                                     sinkCommonInsts(true)));

  // Optimize parallel scalar instruction chains into SIMD instructions.
  OptimizePM.addPass(SLPVectorizerPass());

  OptimizePM.addPass(InstCombinePass());

  // Unroll small loops to hide loop backedge latency and saturate any parallel
  // execution resources of an out-of-order processor. We also then need to
  // clean up redundancies and loop invariant code.
  // FIXME: It would be really good to use a loop-integrated instruction
  // combiner for cleanup here so that the unrolling and LICM can be pipelined
  // across the loop nests.
  // We do UnrollAndJam in a separate LPM to ensure it happens before unroll
  if (EnableUnrollAndJam) {
    OptimizePM.addPass(
        createFunctionToLoopPassAdaptor(LoopUnrollAndJamPass(Level)));
  }
  OptimizePM.addPass(LoopUnrollPass(LoopUnrollOptions(Level)));
  OptimizePM.addPass(WarnMissedTransformationsPass());
  OptimizePM.addPass(InstCombinePass());
  OptimizePM.addPass(RequireAnalysisPass<OptimizationRemarkEmitterAnalysis, Function>());
  OptimizePM.addPass(createFunctionToLoopPassAdaptor(LICMPass(), DebugLogging));

  // Now that we've vectorized and unrolled loops, we may have more refined
  // alignment information, try to re-derive it here.
  OptimizePM.addPass(AlignmentFromAssumptionsPass());

  // LoopSink pass sinks instructions hoisted by LICM, which serves as a
  // canonicalization pass that enables other optimizations. As a result,
  // LoopSink pass needs to be a very late IR pass to avoid undoing LICM
  // result too early.
  OptimizePM.addPass(LoopSinkPass());

  // And finally clean up LCSSA form before generating code.
  OptimizePM.addPass(InstSimplifyPass());

  // This hoists/decomposes div/rem ops. It should run after other sink/hoist
  // passes to avoid re-sinking, but before SimplifyCFG because it can allow
  // flattening of blocks.
  OptimizePM.addPass(DivRemPairsPass());

  // LoopSink (and other loop passes since the last simplifyCFG) might have
  // resulted in single-entry-single-exit or empty blocks. Clean up the CFG.
  OptimizePM.addPass(SimplifyCFGPass());

  // Optimize PHIs by speculating around them when profitable. Note that this
  // pass needs to be run after any PRE or similar pass as it is essentially
  // inserting redudnancies into the progrem. This even includes SimplifyCFG.
  OptimizePM.addPass(SpeculateAroundPHIsPass());

  for (auto &C : OptimizerLastEPCallbacks)
    C(OptimizePM, Level);

  // Add the core optimizing pipeline.
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(OptimizePM)));

  MPM.addPass(CGProfilePass());

  // Now we need to do some global optimization transforms.
  // FIXME: It would seem like these should come first in the optimization
  // pipeline and maybe be the bottom of the canonicalization pipeline? Weird
  // ordering here.
  MPM.addPass(GlobalDCEPass());
  MPM.addPass(ConstantMergePass());

  return MPM;
}

ModulePassManager
PassBuilder::buildPerModuleDefaultPipeline(OptimizationLevel Level,
                                           bool DebugLogging) {
  assert(Level != O0 && "Must request optimizations for the default pipeline!");

  ModulePassManager MPM(DebugLogging);

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  // Apply module pipeline start EP callback.
  for (auto &C : PipelineStartEPCallbacks)
    C(MPM);

  if (PGOOpt && PGOOpt->SamplePGOSupport)
    MPM.addPass(createModuleToFunctionPassAdaptor(AddDiscriminatorsPass()));

  // Add the core simplification pipeline.
  MPM.addPass(buildModuleSimplificationPipeline(Level, ThinLTOPhase::None,
                                                DebugLogging));

  // Now add the optimization pipeline.
  MPM.addPass(buildModuleOptimizationPipeline(Level, DebugLogging));

  return MPM;
}

ModulePassManager
PassBuilder::buildThinLTOPreLinkDefaultPipeline(OptimizationLevel Level,
                                                bool DebugLogging) {
  assert(Level != O0 && "Must request optimizations for the default pipeline!");

  ModulePassManager MPM(DebugLogging);

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  if (PGOOpt && PGOOpt->SamplePGOSupport)
    MPM.addPass(createModuleToFunctionPassAdaptor(AddDiscriminatorsPass()));

  // Apply module pipeline start EP callback.
  for (auto &C : PipelineStartEPCallbacks)
    C(MPM);

  // If we are planning to perform ThinLTO later, we don't bloat the code with
  // unrolling/vectorization/... now. Just simplify the module as much as we
  // can.
  MPM.addPass(buildModuleSimplificationPipeline(Level, ThinLTOPhase::PreLink,
                                                DebugLogging));

  // Run partial inlining pass to partially inline functions that have
  // large bodies.
  // FIXME: It isn't clear whether this is really the right place to run this
  // in ThinLTO. Because there is another canonicalization and simplification
  // phase that will run after the thin link, running this here ends up with
  // less information than will be available later and it may grow functions in
  // ways that aren't beneficial.
  if (RunPartialInlining)
    MPM.addPass(PartialInlinerPass());

  // Reduce the size of the IR as much as possible.
  MPM.addPass(GlobalOptPass());

  return MPM;
}

ModulePassManager PassBuilder::buildThinLTODefaultPipeline(
    OptimizationLevel Level, bool DebugLogging,
    const ModuleSummaryIndex *ImportSummary) {
  ModulePassManager MPM(DebugLogging);

  if (ImportSummary) {
    // These passes import type identifier resolutions for whole-program
    // devirtualization and CFI. They must run early because other passes may
    // disturb the specific instruction patterns that these passes look for,
    // creating dependencies on resolutions that may not appear in the summary.
    //
    // For example, GVN may transform the pattern assume(type.test) appearing in
    // two basic blocks into assume(phi(type.test, type.test)), which would
    // transform a dependency on a WPD resolution into a dependency on a type
    // identifier resolution for CFI.
    //
    // Also, WPD has access to more precise information than ICP and can
    // devirtualize more effectively, so it should operate on the IR first.
    MPM.addPass(WholeProgramDevirtPass(nullptr, ImportSummary));
    MPM.addPass(LowerTypeTestsPass(nullptr, ImportSummary));
  }

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  // During the ThinLTO backend phase we perform early indirect call promotion
  // here, before globalopt. Otherwise imported available_externally functions
  // look unreferenced and are removed.
  // FIXME: move this into buildModuleSimplificationPipeline to merge the logic
  //        with SamplePGO.
  if (!PGOOpt || PGOOpt->SampleProfileFile.empty())
    MPM.addPass(PGOIndirectCallPromotion(true /* InLTO */,
                                         false /* SamplePGO */));

  // Add the core simplification pipeline.
  MPM.addPass(buildModuleSimplificationPipeline(Level, ThinLTOPhase::PostLink,
                                                DebugLogging));

  // Now add the optimization pipeline.
  MPM.addPass(buildModuleOptimizationPipeline(Level, DebugLogging));

  return MPM;
}

ModulePassManager
PassBuilder::buildLTOPreLinkDefaultPipeline(OptimizationLevel Level,
                                            bool DebugLogging) {
  assert(Level != O0 && "Must request optimizations for the default pipeline!");
  // FIXME: We should use a customized pre-link pipeline!
  return buildPerModuleDefaultPipeline(Level, DebugLogging);
}

ModulePassManager
PassBuilder::buildLTODefaultPipeline(OptimizationLevel Level, bool DebugLogging,
                                     ModuleSummaryIndex *ExportSummary) {
  assert(Level != O0 && "Must request optimizations for the default pipeline!");
  ModulePassManager MPM(DebugLogging);

  if (PGOOpt && !PGOOpt->SampleProfileFile.empty()) {
    // Load sample profile before running the LTO optimization pipeline.
    MPM.addPass(SampleProfileLoaderPass(PGOOpt->SampleProfileFile,
                                        PGOOpt->ProfileRemappingFile,
                                        false /* ThinLTOPhase::PreLink */));
  }

  // Remove unused virtual tables to improve the quality of code generated by
  // whole-program devirtualization and bitset lowering.
  MPM.addPass(GlobalDCEPass());

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());

  // Do basic inference of function attributes from known properties of system
  // libraries and other oracles.
  MPM.addPass(InferFunctionAttrsPass());

  if (Level > 1) {
    FunctionPassManager EarlyFPM(DebugLogging);
    EarlyFPM.addPass(CallSiteSplittingPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(EarlyFPM)));

    // Indirect call promotion. This should promote all the targets that are
    // left by the earlier promotion pass that promotes intra-module targets.
    // This two-step promotion is to save the compile time. For LTO, it should
    // produce the same result as if we only do promotion here.
    MPM.addPass(PGOIndirectCallPromotion(
        true /* InLTO */, PGOOpt && !PGOOpt->SampleProfileFile.empty()));
    // Propagate constants at call sites into the functions they call.  This
    // opens opportunities for globalopt (and inlining) by substituting function
    // pointers passed as arguments to direct uses of functions.
   MPM.addPass(IPSCCPPass());

   // Attach metadata to indirect call sites indicating the set of functions
   // they may target at run-time. This should follow IPSCCP.
   MPM.addPass(CalledValuePropagationPass());
  }

  // Now deduce any function attributes based in the current code.
  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(
              PostOrderFunctionAttrsPass()));

  // Do RPO function attribute inference across the module to forward-propagate
  // attributes where applicable.
  // FIXME: Is this really an optimization rather than a canonicalization?
  MPM.addPass(ReversePostOrderFunctionAttrsPass());

  // Use inragne annotations on GEP indices to split globals where beneficial.
  MPM.addPass(GlobalSplitPass());

  // Run whole program optimization of virtual call when the list of callees
  // is fixed.
  MPM.addPass(WholeProgramDevirtPass(ExportSummary, nullptr));

  // Stop here at -O1.
  if (Level == 1) {
    // The LowerTypeTestsPass needs to run to lower type metadata and the
    // type.test intrinsics. The pass does nothing if CFI is disabled.
    MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));
    return MPM;
  }

  // Optimize globals to try and fold them into constants.
  MPM.addPass(GlobalOptPass());

  // Promote any localized globals to SSA registers.
  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));

  // Linking modules together can lead to duplicate global constant, only
  // keep one copy of each constant.
  MPM.addPass(ConstantMergePass());

  // Remove unused arguments from functions.
  MPM.addPass(DeadArgumentEliminationPass());

  // Reduce the code after globalopt and ipsccp.  Both can open up significant
  // simplification opportunities, and both can propagate functions through
  // function pointers.  When this happens, we often have to resolve varargs
  // calls, etc, so let instcombine do this.
  FunctionPassManager PeepholeFPM(DebugLogging);
  if (Level == O3)
    PeepholeFPM.addPass(AggressiveInstCombinePass());
  PeepholeFPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(PeepholeFPM, Level);

  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(PeepholeFPM)));

  // Note: historically, the PruneEH pass was run first to deduce nounwind and
  // generally clean up exception handling overhead. It isn't clear this is
  // valuable as the inliner doesn't currently care whether it is inlining an
  // invoke or a call.
  // Run the inliner now.
  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(
      InlinerPass(getInlineParamsFromOptLevel(Level))));

  // Optimize globals again after we ran the inliner.
  MPM.addPass(GlobalOptPass());

  // Garbage collect dead functions.
  // FIXME: Add ArgumentPromotion pass after once it's ported.
  MPM.addPass(GlobalDCEPass());

  FunctionPassManager FPM(DebugLogging);
  // The IPO Passes may leave cruft around. Clean up after them.
  FPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(FPM, Level);

  FPM.addPass(JumpThreadingPass());

  // Break up allocas
  FPM.addPass(SROA());

  // Run a few AA driver optimizations here and now to cleanup the code.
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

  MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(
              PostOrderFunctionAttrsPass()));
  // FIXME: here we run IP alias analysis in the legacy PM.

  FunctionPassManager MainFPM;

  // FIXME: once we fix LoopPass Manager, add LICM here.
  // FIXME: once we provide support for enabling MLSM, add it here.
  // FIXME: once we provide support for enabling NewGVN, add it here.
  if (RunNewGVN)
    MainFPM.addPass(NewGVNPass());
  else
    MainFPM.addPass(GVN());

  // Remove dead memcpy()'s.
  MainFPM.addPass(MemCpyOptPass());

  // Nuke dead stores.
  MainFPM.addPass(DSEPass());

  // FIXME: at this point, we run a bunch of loop passes:
  // indVarSimplify, loopDeletion, loopInterchange, loopUnrool,
  // loopVectorize. Enable them once the remaining issue with LPM
  // are sorted out.

  MainFPM.addPass(InstCombinePass());
  MainFPM.addPass(SimplifyCFGPass());
  MainFPM.addPass(SCCPPass());
  MainFPM.addPass(InstCombinePass());
  MainFPM.addPass(BDCEPass());

  // FIXME: We may want to run SLPVectorizer here.
  // After vectorization, assume intrinsics may tell us more
  // about pointer alignments.
#if 0
  MainFPM.add(AlignmentFromAssumptionsPass());
#endif

  // FIXME: Conditionally run LoadCombine here, after it's ported
  // (in case we still have this pass, given its questionable usefulness).

  MainFPM.addPass(InstCombinePass());
  invokePeepholeEPCallbacks(MainFPM, Level);
  MainFPM.addPass(JumpThreadingPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(MainFPM)));

  // Create a function that performs CFI checks for cross-DSO calls with
  // targets in the current module.
  MPM.addPass(CrossDSOCFIPass());

  // Lower type metadata and the type.test intrinsic. This pass supports
  // clang's control flow integrity mechanisms (-fsanitize=cfi*) and needs
  // to be run at link time if CFI is enabled. This pass does nothing if
  // CFI is disabled.
  MPM.addPass(LowerTypeTestsPass(ExportSummary, nullptr));

  // Add late LTO optimization passes.
  // Delete basic blocks, which optimization passes may have killed.
  MPM.addPass(createModuleToFunctionPassAdaptor(SimplifyCFGPass()));

  // Drop bodies of available eternally objects to improve GlobalDCE.
  MPM.addPass(EliminateAvailableExternallyPass());

  // Now that we have optimized the program, discard unreachable functions.
  MPM.addPass(GlobalDCEPass());

  // FIXME: Enable MergeFuncs, conditionally, after ported, maybe.
  return MPM;
}

AAManager PassBuilder::buildDefaultAAPipeline() {
  AAManager AA;

  // The order in which these are registered determines their priority when
  // being queried.

  // First we register the basic alias analysis that provides the majority of
  // per-function local AA logic. This is a stateless, on-demand local set of
  // AA techniques.
  AA.registerFunctionAnalysis<BasicAA>();

  // Next we query fast, specialized alias analyses that wrap IR-embedded
  // information about aliasing.
  AA.registerFunctionAnalysis<ScopedNoAliasAA>();
  AA.registerFunctionAnalysis<TypeBasedAA>();

  // Add support for querying global aliasing information when available.
  // Because the `AAManager` is a function analysis and `GlobalsAA` is a module
  // analysis, all that the `AAManager` can do is query for any *cached*
  // results from `GlobalsAA` through a readonly proxy.
  AA.registerModuleAnalysis<GlobalsAA>();

  return AA;
}

static Optional<int> parseRepeatPassName(StringRef Name) {
  if (!Name.consume_front("repeat<") || !Name.consume_back(">"))
    return None;
  int Count;
  if (Name.getAsInteger(0, Count) || Count <= 0)
    return None;
  return Count;
}

static Optional<int> parseDevirtPassName(StringRef Name) {
  if (!Name.consume_front("devirt<") || !Name.consume_back(">"))
    return None;
  int Count;
  if (Name.getAsInteger(0, Count) || Count <= 0)
    return None;
  return Count;
}

static bool checkParametrizedPassName(StringRef Name, StringRef PassName) {
  if (!Name.consume_front(PassName))
    return false;
  // normal pass name w/o parameters == default parameters
  if (Name.empty())
    return true;
  return Name.startswith("<") && Name.endswith(">");
}

namespace {

/// This performs customized parsing of pass name with parameters.
///
/// We do not need parametrization of passes in textual pipeline very often,
/// yet on a rare occasion ability to specify parameters right there can be
/// useful.
///
/// \p Name - parameterized specification of a pass from a textual pipeline
/// is a string in a form of :
///      PassName '<' parameter-list '>'
///
/// Parameter list is being parsed by the parser callable argument, \p Parser,
/// It takes a string-ref of parameters and returns either StringError or a
/// parameter list in a form of a custom parameters type, all wrapped into
/// Expected<> template class.
///
template <typename ParametersParseCallableT>
auto parsePassParameters(ParametersParseCallableT &&Parser, StringRef Name,
                         StringRef PassName) -> decltype(Parser(StringRef{})) {
  using ParametersT = typename decltype(Parser(StringRef{}))::value_type;

  StringRef Params = Name;
  if (!Params.consume_front(PassName)) {
    assert(false &&
           "unable to strip pass name from parametrized pass specification");
  }
  if (Params.empty())
    return ParametersT{};
  if (!Params.consume_front("<") || !Params.consume_back(">")) {
    assert(false && "invalid format for parametrized pass name");
  }

  Expected<ParametersT> Result = Parser(Params);
  assert((Result || Result.template errorIsA<StringError>()) &&
         "Pass parameter parser can only return StringErrors.");
  return std::move(Result);
}

/// Parser of parameters for LoopUnroll pass.
Expected<LoopUnrollOptions> parseLoopUnrollOptions(StringRef Params) {
  LoopUnrollOptions UnrollOpts;
  while (!Params.empty()) {
    StringRef ParamName;
    std::tie(ParamName, Params) = Params.split(';');
    int OptLevel = StringSwitch<int>(ParamName)
                       .Case("O0", 0)
                       .Case("O1", 1)
                       .Case("O2", 2)
                       .Case("O3", 3)
                       .Default(-1);
    if (OptLevel >= 0) {
      UnrollOpts.setOptLevel(OptLevel);
      continue;
    }

    bool Enable = !ParamName.consume_front("no-");
    if (ParamName == "partial") {
      UnrollOpts.setPartial(Enable);
    } else if (ParamName == "peeling") {
      UnrollOpts.setPeeling(Enable);
    } else if (ParamName == "runtime") {
      UnrollOpts.setRuntime(Enable);
    } else if (ParamName == "upperbound") {
      UnrollOpts.setUpperBound(Enable);
    } else {
      return make_error<StringError>(
          formatv("invalid LoopUnrollPass parameter '{0}' ", ParamName).str(),
          inconvertibleErrorCode());
    }
  }
  return UnrollOpts;
}

} // namespace

/// Tests whether a pass name starts with a valid prefix for a default pipeline
/// alias.
static bool startsWithDefaultPipelineAliasPrefix(StringRef Name) {
  return Name.startswith("default") || Name.startswith("thinlto") ||
         Name.startswith("lto");
}

/// Tests whether registered callbacks will accept a given pass name.
///
/// When parsing a pipeline text, the type of the outermost pipeline may be
/// omitted, in which case the type is automatically determined from the first
/// pass name in the text. This may be a name that is handled through one of the
/// callbacks. We check this through the oridinary parsing callbacks by setting
/// up a dummy PassManager in order to not force the client to also handle this
/// type of query.
template <typename PassManagerT, typename CallbacksT>
static bool callbacksAcceptPassName(StringRef Name, CallbacksT &Callbacks) {
  if (!Callbacks.empty()) {
    PassManagerT DummyPM;
    for (auto &CB : Callbacks)
      if (CB(Name, DummyPM, {}))
        return true;
  }
  return false;
}

template <typename CallbacksT>
static bool isModulePassName(StringRef Name, CallbacksT &Callbacks) {
  // Manually handle aliases for pre-configured pipeline fragments.
  if (startsWithDefaultPipelineAliasPrefix(Name))
    return DefaultAliasRegex.match(Name);

  // Explicitly handle pass manager names.
  if (Name == "module")
    return true;
  if (Name == "cgscc")
    return true;
  if (Name == "function")
    return true;

  // Explicitly handle custom-parsed pass names.
  if (parseRepeatPassName(Name))
    return true;

#define MODULE_PASS(NAME, CREATE_PASS)                                         \
  if (Name == NAME)                                                            \
    return true;
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<ModulePassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isCGSCCPassName(StringRef Name, CallbacksT &Callbacks) {
  // Explicitly handle pass manager names.
  if (Name == "cgscc")
    return true;
  if (Name == "function")
    return true;

  // Explicitly handle custom-parsed pass names.
  if (parseRepeatPassName(Name))
    return true;
  if (parseDevirtPassName(Name))
    return true;

#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  if (Name == NAME)                                                            \
    return true;
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<CGSCCPassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isFunctionPassName(StringRef Name, CallbacksT &Callbacks) {
  // Explicitly handle pass manager names.
  if (Name == "function")
    return true;
  if (Name == "loop")
    return true;

  // Explicitly handle custom-parsed pass names.
  if (parseRepeatPassName(Name))
    return true;

#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME)                                                            \
    return true;
#define FUNCTION_PASS_WITH_PARAMS(NAME, CREATE_PASS, PARSER)                   \
  if (checkParametrizedPassName(Name, NAME))                                   \
    return true;
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<FunctionPassManager>(Name, Callbacks);
}

template <typename CallbacksT>
static bool isLoopPassName(StringRef Name, CallbacksT &Callbacks) {
  // Explicitly handle pass manager names.
  if (Name == "loop")
    return true;

  // Explicitly handle custom-parsed pass names.
  if (parseRepeatPassName(Name))
    return true;

#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME)                                                            \
    return true;
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  if (Name == "require<" NAME ">" || Name == "invalidate<" NAME ">")           \
    return true;
#include "PassRegistry.def"

  return callbacksAcceptPassName<LoopPassManager>(Name, Callbacks);
}

Optional<std::vector<PassBuilder::PipelineElement>>
PassBuilder::parsePipelineText(StringRef Text) {
  std::vector<PipelineElement> ResultPipeline;

  SmallVector<std::vector<PipelineElement> *, 4> PipelineStack = {
      &ResultPipeline};
  for (;;) {
    std::vector<PipelineElement> &Pipeline = *PipelineStack.back();
    size_t Pos = Text.find_first_of(",()");
    Pipeline.push_back({Text.substr(0, Pos), {}});

    // If we have a single terminating name, we're done.
    if (Pos == Text.npos)
      break;

    char Sep = Text[Pos];
    Text = Text.substr(Pos + 1);
    if (Sep == ',')
      // Just a name ending in a comma, continue.
      continue;

    if (Sep == '(') {
      // Push the inner pipeline onto the stack to continue processing.
      PipelineStack.push_back(&Pipeline.back().InnerPipeline);
      continue;
    }

    assert(Sep == ')' && "Bogus separator!");
    // When handling the close parenthesis, we greedily consume them to avoid
    // empty strings in the pipeline.
    do {
      // If we try to pop the outer pipeline we have unbalanced parentheses.
      if (PipelineStack.size() == 1)
        return None;

      PipelineStack.pop_back();
    } while (Text.consume_front(")"));

    // Check if we've finished parsing.
    if (Text.empty())
      break;

    // Otherwise, the end of an inner pipeline always has to be followed by
    // a comma, and then we can continue.
    if (!Text.consume_front(","))
      return None;
  }

  if (PipelineStack.size() > 1)
    // Unbalanced paretheses.
    return None;

  assert(PipelineStack.back() == &ResultPipeline &&
         "Wrong pipeline at the bottom of the stack!");
  return {std::move(ResultPipeline)};
}

Error PassBuilder::parseModulePass(ModulePassManager &MPM,
                                   const PipelineElement &E,
                                   bool VerifyEachPass, bool DebugLogging) {
  auto &Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "module") {
      ModulePassManager NestedMPM(DebugLogging);
      if (auto Err = parseModulePassPipeline(NestedMPM, InnerPipeline,
                                             VerifyEachPass, DebugLogging))
        return Err;
      MPM.addPass(std::move(NestedMPM));
      return Error::success();
    }
    if (Name == "cgscc") {
      CGSCCPassManager CGPM(DebugLogging);
      if (auto Err = parseCGSCCPassPipeline(CGPM, InnerPipeline, VerifyEachPass,
                                            DebugLogging))
        return Err;
      MPM.addPass(createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));
      return Error::success();
    }
    if (Name == "function") {
      FunctionPassManager FPM(DebugLogging);
      if (auto Err = parseFunctionPassPipeline(FPM, InnerPipeline,
                                               VerifyEachPass, DebugLogging))
        return Err;
      MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
      return Error::success();
    }
    if (auto Count = parseRepeatPassName(Name)) {
      ModulePassManager NestedMPM(DebugLogging);
      if (auto Err = parseModulePassPipeline(NestedMPM, InnerPipeline,
                                             VerifyEachPass, DebugLogging))
        return Err;
      MPM.addPass(createRepeatedPass(*Count, std::move(NestedMPM)));
      return Error::success();
    }

    for (auto &C : ModulePipelineParsingCallbacks)
      if (C(Name, MPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as module pipeline", Name).str(),
        inconvertibleErrorCode());
    ;
  }

  // Manually handle aliases for pre-configured pipeline fragments.
  if (startsWithDefaultPipelineAliasPrefix(Name)) {
    SmallVector<StringRef, 3> Matches;
    if (!DefaultAliasRegex.match(Name, &Matches))
      return make_error<StringError>(
          formatv("unknown default pipeline alias '{0}'", Name).str(),
          inconvertibleErrorCode());

    assert(Matches.size() == 3 && "Must capture two matched strings!");

    OptimizationLevel L = StringSwitch<OptimizationLevel>(Matches[2])
                              .Case("O0", O0)
                              .Case("O1", O1)
                              .Case("O2", O2)
                              .Case("O3", O3)
                              .Case("Os", Os)
                              .Case("Oz", Oz);
    if (L == O0)
      // At O0 we do nothing at all!
      return Error::success();

    if (Matches[1] == "default") {
      MPM.addPass(buildPerModuleDefaultPipeline(L, DebugLogging));
    } else if (Matches[1] == "thinlto-pre-link") {
      MPM.addPass(buildThinLTOPreLinkDefaultPipeline(L, DebugLogging));
    } else if (Matches[1] == "thinlto") {
      MPM.addPass(buildThinLTODefaultPipeline(L, DebugLogging, nullptr));
    } else if (Matches[1] == "lto-pre-link") {
      MPM.addPass(buildLTOPreLinkDefaultPipeline(L, DebugLogging));
    } else {
      assert(Matches[1] == "lto" && "Not one of the matched options!");
      MPM.addPass(buildLTODefaultPipeline(L, DebugLogging, nullptr));
    }
    return Error::success();
  }

  // Finally expand the basic registered passes from the .inc file.
#define MODULE_PASS(NAME, CREATE_PASS)                                         \
  if (Name == NAME) {                                                          \
    MPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define MODULE_ANALYSIS(NAME, CREATE_PASS)                                     \
  if (Name == "require<" NAME ">") {                                           \
    MPM.addPass(                                                               \
        RequireAnalysisPass<                                                   \
            std::remove_reference<decltype(CREATE_PASS)>::type, Module>());    \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    MPM.addPass(InvalidateAnalysisPass<                                        \
                std::remove_reference<decltype(CREATE_PASS)>::type>());        \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : ModulePipelineParsingCallbacks)
    if (C(Name, MPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown module pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

Error PassBuilder::parseCGSCCPass(CGSCCPassManager &CGPM,
                                  const PipelineElement &E, bool VerifyEachPass,
                                  bool DebugLogging) {
  auto &Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "cgscc") {
      CGSCCPassManager NestedCGPM(DebugLogging);
      if (auto Err = parseCGSCCPassPipeline(NestedCGPM, InnerPipeline,
                                            VerifyEachPass, DebugLogging))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      CGPM.addPass(std::move(NestedCGPM));
      return Error::success();
    }
    if (Name == "function") {
      FunctionPassManager FPM(DebugLogging);
      if (auto Err = parseFunctionPassPipeline(FPM, InnerPipeline,
                                               VerifyEachPass, DebugLogging))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      CGPM.addPass(createCGSCCToFunctionPassAdaptor(std::move(FPM)));
      return Error::success();
    }
    if (auto Count = parseRepeatPassName(Name)) {
      CGSCCPassManager NestedCGPM(DebugLogging);
      if (auto Err = parseCGSCCPassPipeline(NestedCGPM, InnerPipeline,
                                            VerifyEachPass, DebugLogging))
        return Err;
      CGPM.addPass(createRepeatedPass(*Count, std::move(NestedCGPM)));
      return Error::success();
    }
    if (auto MaxRepetitions = parseDevirtPassName(Name)) {
      CGSCCPassManager NestedCGPM(DebugLogging);
      if (auto Err = parseCGSCCPassPipeline(NestedCGPM, InnerPipeline,
                                            VerifyEachPass, DebugLogging))
        return Err;
      CGPM.addPass(
          createDevirtSCCRepeatedPass(std::move(NestedCGPM), *MaxRepetitions));
      return Error::success();
    }

    for (auto &C : CGSCCPipelineParsingCallbacks)
      if (C(Name, CGPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as cgscc pipeline", Name).str(),
        inconvertibleErrorCode());
  }

// Now expand the basic registered passes from the .inc file.
#define CGSCC_PASS(NAME, CREATE_PASS)                                          \
  if (Name == NAME) {                                                          \
    CGPM.addPass(CREATE_PASS);                                                 \
    return Error::success();                                                   \
  }
#define CGSCC_ANALYSIS(NAME, CREATE_PASS)                                      \
  if (Name == "require<" NAME ">") {                                           \
    CGPM.addPass(RequireAnalysisPass<                                          \
                 std::remove_reference<decltype(CREATE_PASS)>::type,           \
                 LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,    \
                 CGSCCUpdateResult &>());                                      \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    CGPM.addPass(InvalidateAnalysisPass<                                       \
                 std::remove_reference<decltype(CREATE_PASS)>::type>());       \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : CGSCCPipelineParsingCallbacks)
    if (C(Name, CGPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown cgscc pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

Error PassBuilder::parseFunctionPass(FunctionPassManager &FPM,
                                     const PipelineElement &E,
                                     bool VerifyEachPass, bool DebugLogging) {
  auto &Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "function") {
      FunctionPassManager NestedFPM(DebugLogging);
      if (auto Err = parseFunctionPassPipeline(NestedFPM, InnerPipeline,
                                               VerifyEachPass, DebugLogging))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      FPM.addPass(std::move(NestedFPM));
      return Error::success();
    }
    if (Name == "loop") {
      LoopPassManager LPM(DebugLogging);
      if (auto Err = parseLoopPassPipeline(LPM, InnerPipeline, VerifyEachPass,
                                           DebugLogging))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      FPM.addPass(
          createFunctionToLoopPassAdaptor(std::move(LPM), DebugLogging));
      return Error::success();
    }
    if (auto Count = parseRepeatPassName(Name)) {
      FunctionPassManager NestedFPM(DebugLogging);
      if (auto Err = parseFunctionPassPipeline(NestedFPM, InnerPipeline,
                                               VerifyEachPass, DebugLogging))
        return Err;
      FPM.addPass(createRepeatedPass(*Count, std::move(NestedFPM)));
      return Error::success();
    }

    for (auto &C : FunctionPipelineParsingCallbacks)
      if (C(Name, FPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as function pipeline", Name).str(),
        inconvertibleErrorCode());
  }

// Now expand the basic registered passes from the .inc file.
#define FUNCTION_PASS(NAME, CREATE_PASS)                                       \
  if (Name == NAME) {                                                          \
    FPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define FUNCTION_PASS_WITH_PARAMS(NAME, CREATE_PASS, PARSER)                   \
  if (checkParametrizedPassName(Name, NAME)) {                                 \
    auto Params = parsePassParameters(PARSER, Name, NAME);                     \
    if (!Params)                                                               \
      return Params.takeError();                                               \
    FPM.addPass(CREATE_PASS(Params.get()));                                    \
    return Error::success();                                                   \
  }
#define FUNCTION_ANALYSIS(NAME, CREATE_PASS)                                   \
  if (Name == "require<" NAME ">") {                                           \
    FPM.addPass(                                                               \
        RequireAnalysisPass<                                                   \
            std::remove_reference<decltype(CREATE_PASS)>::type, Function>());  \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    FPM.addPass(InvalidateAnalysisPass<                                        \
                std::remove_reference<decltype(CREATE_PASS)>::type>());        \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : FunctionPipelineParsingCallbacks)
    if (C(Name, FPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(
      formatv("unknown function pass '{0}'", Name).str(),
      inconvertibleErrorCode());
}

Error PassBuilder::parseLoopPass(LoopPassManager &LPM, const PipelineElement &E,
                                 bool VerifyEachPass, bool DebugLogging) {
  StringRef Name = E.Name;
  auto &InnerPipeline = E.InnerPipeline;

  // First handle complex passes like the pass managers which carry pipelines.
  if (!InnerPipeline.empty()) {
    if (Name == "loop") {
      LoopPassManager NestedLPM(DebugLogging);
      if (auto Err = parseLoopPassPipeline(NestedLPM, InnerPipeline,
                                           VerifyEachPass, DebugLogging))
        return Err;
      // Add the nested pass manager with the appropriate adaptor.
      LPM.addPass(std::move(NestedLPM));
      return Error::success();
    }
    if (auto Count = parseRepeatPassName(Name)) {
      LoopPassManager NestedLPM(DebugLogging);
      if (auto Err = parseLoopPassPipeline(NestedLPM, InnerPipeline,
                                           VerifyEachPass, DebugLogging))
        return Err;
      LPM.addPass(createRepeatedPass(*Count, std::move(NestedLPM)));
      return Error::success();
    }

    for (auto &C : LoopPipelineParsingCallbacks)
      if (C(Name, LPM, InnerPipeline))
        return Error::success();

    // Normal passes can't have pipelines.
    return make_error<StringError>(
        formatv("invalid use of '{0}' pass as loop pipeline", Name).str(),
        inconvertibleErrorCode());
  }

// Now expand the basic registered passes from the .inc file.
#define LOOP_PASS(NAME, CREATE_PASS)                                           \
  if (Name == NAME) {                                                          \
    LPM.addPass(CREATE_PASS);                                                  \
    return Error::success();                                                   \
  }
#define LOOP_ANALYSIS(NAME, CREATE_PASS)                                       \
  if (Name == "require<" NAME ">") {                                           \
    LPM.addPass(RequireAnalysisPass<                                           \
                std::remove_reference<decltype(CREATE_PASS)>::type, Loop,      \
                LoopAnalysisManager, LoopStandardAnalysisResults &,            \
                LPMUpdater &>());                                              \
    return Error::success();                                                   \
  }                                                                            \
  if (Name == "invalidate<" NAME ">") {                                        \
    LPM.addPass(InvalidateAnalysisPass<                                        \
                std::remove_reference<decltype(CREATE_PASS)>::type>());        \
    return Error::success();                                                   \
  }
#include "PassRegistry.def"

  for (auto &C : LoopPipelineParsingCallbacks)
    if (C(Name, LPM, InnerPipeline))
      return Error::success();
  return make_error<StringError>(formatv("unknown loop pass '{0}'", Name).str(),
                                 inconvertibleErrorCode());
}

bool PassBuilder::parseAAPassName(AAManager &AA, StringRef Name) {
#define MODULE_ALIAS_ANALYSIS(NAME, CREATE_PASS)                               \
  if (Name == NAME) {                                                          \
    AA.registerModuleAnalysis<                                                 \
        std::remove_reference<decltype(CREATE_PASS)>::type>();                 \
    return true;                                                               \
  }
#define FUNCTION_ALIAS_ANALYSIS(NAME, CREATE_PASS)                             \
  if (Name == NAME) {                                                          \
    AA.registerFunctionAnalysis<                                               \
        std::remove_reference<decltype(CREATE_PASS)>::type>();                 \
    return true;                                                               \
  }
#include "PassRegistry.def"

  for (auto &C : AAParsingCallbacks)
    if (C(Name, AA))
      return true;
  return false;
}

Error PassBuilder::parseLoopPassPipeline(LoopPassManager &LPM,
                                         ArrayRef<PipelineElement> Pipeline,
                                         bool VerifyEachPass,
                                         bool DebugLogging) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseLoopPass(LPM, Element, VerifyEachPass, DebugLogging))
      return Err;
    // FIXME: No verifier support for Loop passes!
  }
  return Error::success();
}

Error PassBuilder::parseFunctionPassPipeline(FunctionPassManager &FPM,
                                             ArrayRef<PipelineElement> Pipeline,
                                             bool VerifyEachPass,
                                             bool DebugLogging) {
  for (const auto &Element : Pipeline) {
    if (auto Err =
            parseFunctionPass(FPM, Element, VerifyEachPass, DebugLogging))
      return Err;
    if (VerifyEachPass)
      FPM.addPass(VerifierPass());
  }
  return Error::success();
}

Error PassBuilder::parseCGSCCPassPipeline(CGSCCPassManager &CGPM,
                                          ArrayRef<PipelineElement> Pipeline,
                                          bool VerifyEachPass,
                                          bool DebugLogging) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseCGSCCPass(CGPM, Element, VerifyEachPass, DebugLogging))
      return Err;
    // FIXME: No verifier support for CGSCC passes!
  }
  return Error::success();
}

void PassBuilder::crossRegisterProxies(LoopAnalysisManager &LAM,
                                       FunctionAnalysisManager &FAM,
                                       CGSCCAnalysisManager &CGAM,
                                       ModuleAnalysisManager &MAM) {
  MAM.registerPass([&] { return FunctionAnalysisManagerModuleProxy(FAM); });
  MAM.registerPass([&] { return CGSCCAnalysisManagerModuleProxy(CGAM); });
  CGAM.registerPass([&] { return ModuleAnalysisManagerCGSCCProxy(MAM); });
  FAM.registerPass([&] { return CGSCCAnalysisManagerFunctionProxy(CGAM); });
  FAM.registerPass([&] { return ModuleAnalysisManagerFunctionProxy(MAM); });
  FAM.registerPass([&] { return LoopAnalysisManagerFunctionProxy(LAM); });
  LAM.registerPass([&] { return FunctionAnalysisManagerLoopProxy(FAM); });
}

Error PassBuilder::parseModulePassPipeline(ModulePassManager &MPM,
                                           ArrayRef<PipelineElement> Pipeline,
                                           bool VerifyEachPass,
                                           bool DebugLogging) {
  for (const auto &Element : Pipeline) {
    if (auto Err = parseModulePass(MPM, Element, VerifyEachPass, DebugLogging))
      return Err;
    if (VerifyEachPass)
      MPM.addPass(VerifierPass());
  }
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c ModulePassManager
// FIXME: Should this routine accept a TargetMachine or require the caller to
// pre-populate the analysis managers with target-specific stuff?
Error PassBuilder::parsePassPipeline(ModulePassManager &MPM,
                                     StringRef PipelineText,
                                     bool VerifyEachPass, bool DebugLogging) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  // If the first name isn't at the module layer, wrap the pipeline up
  // automatically.
  StringRef FirstName = Pipeline->front().Name;

  if (!isModulePassName(FirstName, ModulePipelineParsingCallbacks)) {
    if (isCGSCCPassName(FirstName, CGSCCPipelineParsingCallbacks)) {
      Pipeline = {{"cgscc", std::move(*Pipeline)}};
    } else if (isFunctionPassName(FirstName,
                                  FunctionPipelineParsingCallbacks)) {
      Pipeline = {{"function", std::move(*Pipeline)}};
    } else if (isLoopPassName(FirstName, LoopPipelineParsingCallbacks)) {
      Pipeline = {{"function", {{"loop", std::move(*Pipeline)}}}};
    } else {
      for (auto &C : TopLevelPipelineParsingCallbacks)
        if (C(MPM, *Pipeline, VerifyEachPass, DebugLogging))
          return Error::success();

      // Unknown pass or pipeline name!
      auto &InnerPipeline = Pipeline->front().InnerPipeline;
      return make_error<StringError>(
          formatv("unknown {0} name '{1}'",
                  (InnerPipeline.empty() ? "pass" : "pipeline"), FirstName)
              .str(),
          inconvertibleErrorCode());
    }
  }

  if (auto Err =
          parseModulePassPipeline(MPM, *Pipeline, VerifyEachPass, DebugLogging))
    return Err;
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c CGSCCPassManager
Error PassBuilder::parsePassPipeline(CGSCCPassManager &CGPM,
                                     StringRef PipelineText,
                                     bool VerifyEachPass, bool DebugLogging) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  StringRef FirstName = Pipeline->front().Name;
  if (!isCGSCCPassName(FirstName, CGSCCPipelineParsingCallbacks))
    return make_error<StringError>(
        formatv("unknown cgscc pass '{0}' in pipeline '{1}'", FirstName,
                PipelineText)
            .str(),
        inconvertibleErrorCode());

  if (auto Err =
          parseCGSCCPassPipeline(CGPM, *Pipeline, VerifyEachPass, DebugLogging))
    return Err;
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c
// FunctionPassManager
Error PassBuilder::parsePassPipeline(FunctionPassManager &FPM,
                                     StringRef PipelineText,
                                     bool VerifyEachPass, bool DebugLogging) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  StringRef FirstName = Pipeline->front().Name;
  if (!isFunctionPassName(FirstName, FunctionPipelineParsingCallbacks))
    return make_error<StringError>(
        formatv("unknown function pass '{0}' in pipeline '{1}'", FirstName,
                PipelineText)
            .str(),
        inconvertibleErrorCode());

  if (auto Err = parseFunctionPassPipeline(FPM, *Pipeline, VerifyEachPass,
                                           DebugLogging))
    return Err;
  return Error::success();
}

// Primary pass pipeline description parsing routine for a \c LoopPassManager
Error PassBuilder::parsePassPipeline(LoopPassManager &CGPM,
                                     StringRef PipelineText,
                                     bool VerifyEachPass, bool DebugLogging) {
  auto Pipeline = parsePipelineText(PipelineText);
  if (!Pipeline || Pipeline->empty())
    return make_error<StringError>(
        formatv("invalid pipeline '{0}'", PipelineText).str(),
        inconvertibleErrorCode());

  if (auto Err =
          parseLoopPassPipeline(CGPM, *Pipeline, VerifyEachPass, DebugLogging))
    return Err;

  return Error::success();
}

Error PassBuilder::parseAAPipeline(AAManager &AA, StringRef PipelineText) {
  // If the pipeline just consists of the word 'default' just replace the AA
  // manager with our default one.
  if (PipelineText == "default") {
    AA = buildDefaultAAPipeline();
    return Error::success();
  }

  while (!PipelineText.empty()) {
    StringRef Name;
    std::tie(Name, PipelineText) = PipelineText.split(',');
    if (!parseAAPassName(AA, Name))
      return make_error<StringError>(
          formatv("unknown alias analysis name '{0}'", Name).str(),
          inconvertibleErrorCode());
  }

  return Error::success();
}
