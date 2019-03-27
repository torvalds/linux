//===-- SanitizerCoverage.cpp - coverage instrumentation for sanitizers ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Coverage instrumentation done on LLVM IR level, works with Sanitizers.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "sancov"

static const char *const SanCovTracePCIndirName =
    "__sanitizer_cov_trace_pc_indir";
static const char *const SanCovTracePCName = "__sanitizer_cov_trace_pc";
static const char *const SanCovTraceCmp1 = "__sanitizer_cov_trace_cmp1";
static const char *const SanCovTraceCmp2 = "__sanitizer_cov_trace_cmp2";
static const char *const SanCovTraceCmp4 = "__sanitizer_cov_trace_cmp4";
static const char *const SanCovTraceCmp8 = "__sanitizer_cov_trace_cmp8";
static const char *const SanCovTraceConstCmp1 =
    "__sanitizer_cov_trace_const_cmp1";
static const char *const SanCovTraceConstCmp2 =
    "__sanitizer_cov_trace_const_cmp2";
static const char *const SanCovTraceConstCmp4 =
    "__sanitizer_cov_trace_const_cmp4";
static const char *const SanCovTraceConstCmp8 =
    "__sanitizer_cov_trace_const_cmp8";
static const char *const SanCovTraceDiv4 = "__sanitizer_cov_trace_div4";
static const char *const SanCovTraceDiv8 = "__sanitizer_cov_trace_div8";
static const char *const SanCovTraceGep = "__sanitizer_cov_trace_gep";
static const char *const SanCovTraceSwitchName = "__sanitizer_cov_trace_switch";
static const char *const SanCovModuleCtorName = "sancov.module_ctor";
static const uint64_t SanCtorAndDtorPriority = 2;

static const char *const SanCovTracePCGuardName =
    "__sanitizer_cov_trace_pc_guard";
static const char *const SanCovTracePCGuardInitName =
    "__sanitizer_cov_trace_pc_guard_init";
static const char *const SanCov8bitCountersInitName =
    "__sanitizer_cov_8bit_counters_init";
static const char *const SanCovPCsInitName = "__sanitizer_cov_pcs_init";

static const char *const SanCovGuardsSectionName = "sancov_guards";
static const char *const SanCovCountersSectionName = "sancov_cntrs";
static const char *const SanCovPCsSectionName = "sancov_pcs";

static const char *const SanCovLowestStackName = "__sancov_lowest_stack";

static cl::opt<int> ClCoverageLevel(
    "sanitizer-coverage-level",
    cl::desc("Sanitizer Coverage. 0: none, 1: entry block, 2: all blocks, "
             "3: all blocks and critical edges"),
    cl::Hidden, cl::init(0));

static cl::opt<bool> ClTracePC("sanitizer-coverage-trace-pc",
                               cl::desc("Experimental pc tracing"), cl::Hidden,
                               cl::init(false));

static cl::opt<bool> ClTracePCGuard("sanitizer-coverage-trace-pc-guard",
                                    cl::desc("pc tracing with a guard"),
                                    cl::Hidden, cl::init(false));

// If true, we create a global variable that contains PCs of all instrumented
// BBs, put this global into a named section, and pass this section's bounds
// to __sanitizer_cov_pcs_init.
// This way the coverage instrumentation does not need to acquire the PCs
// at run-time. Works with trace-pc-guard and inline-8bit-counters.
static cl::opt<bool> ClCreatePCTable("sanitizer-coverage-pc-table",
                                     cl::desc("create a static PC table"),
                                     cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClInline8bitCounters("sanitizer-coverage-inline-8bit-counters",
                         cl::desc("increments 8-bit counter for every edge"),
                         cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClCMPTracing("sanitizer-coverage-trace-compares",
                 cl::desc("Tracing of CMP and similar instructions"),
                 cl::Hidden, cl::init(false));

static cl::opt<bool> ClDIVTracing("sanitizer-coverage-trace-divs",
                                  cl::desc("Tracing of DIV instructions"),
                                  cl::Hidden, cl::init(false));

static cl::opt<bool> ClGEPTracing("sanitizer-coverage-trace-geps",
                                  cl::desc("Tracing of GEP instructions"),
                                  cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClPruneBlocks("sanitizer-coverage-prune-blocks",
                  cl::desc("Reduce the number of instrumented blocks"),
                  cl::Hidden, cl::init(true));

static cl::opt<bool> ClStackDepth("sanitizer-coverage-stack-depth",
                                  cl::desc("max stack depth tracing"),
                                  cl::Hidden, cl::init(false));

namespace {

SanitizerCoverageOptions getOptions(int LegacyCoverageLevel) {
  SanitizerCoverageOptions Res;
  switch (LegacyCoverageLevel) {
  case 0:
    Res.CoverageType = SanitizerCoverageOptions::SCK_None;
    break;
  case 1:
    Res.CoverageType = SanitizerCoverageOptions::SCK_Function;
    break;
  case 2:
    Res.CoverageType = SanitizerCoverageOptions::SCK_BB;
    break;
  case 3:
    Res.CoverageType = SanitizerCoverageOptions::SCK_Edge;
    break;
  case 4:
    Res.CoverageType = SanitizerCoverageOptions::SCK_Edge;
    Res.IndirectCalls = true;
    break;
  }
  return Res;
}

SanitizerCoverageOptions OverrideFromCL(SanitizerCoverageOptions Options) {
  // Sets CoverageType and IndirectCalls.
  SanitizerCoverageOptions CLOpts = getOptions(ClCoverageLevel);
  Options.CoverageType = std::max(Options.CoverageType, CLOpts.CoverageType);
  Options.IndirectCalls |= CLOpts.IndirectCalls;
  Options.TraceCmp |= ClCMPTracing;
  Options.TraceDiv |= ClDIVTracing;
  Options.TraceGep |= ClGEPTracing;
  Options.TracePC |= ClTracePC;
  Options.TracePCGuard |= ClTracePCGuard;
  Options.Inline8bitCounters |= ClInline8bitCounters;
  Options.PCTable |= ClCreatePCTable;
  Options.NoPrune |= !ClPruneBlocks;
  Options.StackDepth |= ClStackDepth;
  if (!Options.TracePCGuard && !Options.TracePC &&
      !Options.Inline8bitCounters && !Options.StackDepth)
    Options.TracePCGuard = true; // TracePCGuard is default.
  return Options;
}

class SanitizerCoverageModule : public ModulePass {
public:
  SanitizerCoverageModule(
      const SanitizerCoverageOptions &Options = SanitizerCoverageOptions())
      : ModulePass(ID), Options(OverrideFromCL(Options)) {
    initializeSanitizerCoverageModulePass(*PassRegistry::getPassRegistry());
  }
  bool runOnModule(Module &M) override;
  bool runOnFunction(Function &F);
  static char ID; // Pass identification, replacement for typeid
  StringRef getPassName() const override { return "SanitizerCoverageModule"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<PostDominatorTreeWrapperPass>();
  }

private:
  void InjectCoverageForIndirectCalls(Function &F,
                                      ArrayRef<Instruction *> IndirCalls);
  void InjectTraceForCmp(Function &F, ArrayRef<Instruction *> CmpTraceTargets);
  void InjectTraceForDiv(Function &F,
                         ArrayRef<BinaryOperator *> DivTraceTargets);
  void InjectTraceForGep(Function &F,
                         ArrayRef<GetElementPtrInst *> GepTraceTargets);
  void InjectTraceForSwitch(Function &F,
                            ArrayRef<Instruction *> SwitchTraceTargets);
  bool InjectCoverage(Function &F, ArrayRef<BasicBlock *> AllBlocks,
                      bool IsLeafFunc = true);
  GlobalVariable *CreateFunctionLocalArrayInSection(size_t NumElements,
                                                    Function &F, Type *Ty,
                                                    const char *Section);
  GlobalVariable *CreatePCArray(Function &F, ArrayRef<BasicBlock *> AllBlocks);
  void CreateFunctionLocalArrays(Function &F, ArrayRef<BasicBlock *> AllBlocks);
  void InjectCoverageAtBlock(Function &F, BasicBlock &BB, size_t Idx,
                             bool IsLeafFunc = true);
  Function *CreateInitCallsForSections(Module &M, const char *InitFunctionName,
                                       Type *Ty, const char *Section);
  std::pair<Value *, Value *> CreateSecStartEnd(Module &M, const char *Section,
                                                Type *Ty);

  void SetNoSanitizeMetadata(Instruction *I) {
    I->setMetadata(I->getModule()->getMDKindID("nosanitize"),
                   MDNode::get(*C, None));
  }

  std::string getSectionName(const std::string &Section) const;
  std::string getSectionStart(const std::string &Section) const;
  std::string getSectionEnd(const std::string &Section) const;
  Function *SanCovTracePCIndir;
  Function *SanCovTracePC, *SanCovTracePCGuard;
  Function *SanCovTraceCmpFunction[4];
  Function *SanCovTraceConstCmpFunction[4];
  Function *SanCovTraceDivFunction[2];
  Function *SanCovTraceGepFunction;
  Function *SanCovTraceSwitchFunction;
  GlobalVariable *SanCovLowestStack;
  InlineAsm *EmptyAsm;
  Type *IntptrTy, *IntptrPtrTy, *Int64Ty, *Int64PtrTy, *Int32Ty, *Int32PtrTy,
      *Int16Ty, *Int8Ty, *Int8PtrTy;
  Module *CurModule;
  std::string CurModuleUniqueId;
  Triple TargetTriple;
  LLVMContext *C;
  const DataLayout *DL;

  GlobalVariable *FunctionGuardArray;  // for trace-pc-guard.
  GlobalVariable *Function8bitCounterArray;  // for inline-8bit-counters.
  GlobalVariable *FunctionPCsArray;  // for pc-table.
  SmallVector<GlobalValue *, 20> GlobalsToAppendToUsed;
  SmallVector<GlobalValue *, 20> GlobalsToAppendToCompilerUsed;

  SanitizerCoverageOptions Options;
};

} // namespace

std::pair<Value *, Value *>
SanitizerCoverageModule::CreateSecStartEnd(Module &M, const char *Section,
                                           Type *Ty) {
  GlobalVariable *SecStart =
      new GlobalVariable(M, Ty, false, GlobalVariable::ExternalLinkage, nullptr,
                         getSectionStart(Section));
  SecStart->setVisibility(GlobalValue::HiddenVisibility);
  GlobalVariable *SecEnd =
      new GlobalVariable(M, Ty, false, GlobalVariable::ExternalLinkage,
                         nullptr, getSectionEnd(Section));
  SecEnd->setVisibility(GlobalValue::HiddenVisibility);
  IRBuilder<> IRB(M.getContext());
  Value *SecEndPtr = IRB.CreatePointerCast(SecEnd, Ty);
  if (!TargetTriple.isOSBinFormatCOFF())
    return std::make_pair(IRB.CreatePointerCast(SecStart, Ty), SecEndPtr);

  // Account for the fact that on windows-msvc __start_* symbols actually
  // point to a uint64_t before the start of the array.
  auto SecStartI8Ptr = IRB.CreatePointerCast(SecStart, Int8PtrTy);
  auto GEP = IRB.CreateGEP(SecStartI8Ptr,
                           ConstantInt::get(IntptrTy, sizeof(uint64_t)));
  return std::make_pair(IRB.CreatePointerCast(GEP, Ty), SecEndPtr);
}

Function *SanitizerCoverageModule::CreateInitCallsForSections(
    Module &M, const char *InitFunctionName, Type *Ty,
    const char *Section) {
  auto SecStartEnd = CreateSecStartEnd(M, Section, Ty);
  auto SecStart = SecStartEnd.first;
  auto SecEnd = SecStartEnd.second;
  Function *CtorFunc;
  std::tie(CtorFunc, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, SanCovModuleCtorName, InitFunctionName, {Ty, Ty}, {SecStart, SecEnd});

  if (TargetTriple.supportsCOMDAT()) {
    // Use comdat to dedup CtorFunc.
    CtorFunc->setComdat(M.getOrInsertComdat(SanCovModuleCtorName));
    appendToGlobalCtors(M, CtorFunc, SanCtorAndDtorPriority, CtorFunc);
  } else {
    appendToGlobalCtors(M, CtorFunc, SanCtorAndDtorPriority);
  }

  if (TargetTriple.isOSBinFormatCOFF()) {
    // In COFF files, if the contructors are set as COMDAT (they are because
    // COFF supports COMDAT) and the linker flag /OPT:REF (strip unreferenced
    // functions and data) is used, the constructors get stripped. To prevent
    // this, give the constructors weak ODR linkage and ensure the linker knows
    // to include the sancov constructor. This way the linker can deduplicate
    // the constructors but always leave one copy.
    CtorFunc->setLinkage(GlobalValue::WeakODRLinkage);
    appendToUsed(M, CtorFunc);
  }
  return CtorFunc;
}

bool SanitizerCoverageModule::runOnModule(Module &M) {
  if (Options.CoverageType == SanitizerCoverageOptions::SCK_None)
    return false;
  C = &(M.getContext());
  DL = &M.getDataLayout();
  CurModule = &M;
  CurModuleUniqueId = getUniqueModuleId(CurModule);
  TargetTriple = Triple(M.getTargetTriple());
  FunctionGuardArray = nullptr;
  Function8bitCounterArray = nullptr;
  FunctionPCsArray = nullptr;
  IntptrTy = Type::getIntNTy(*C, DL->getPointerSizeInBits());
  IntptrPtrTy = PointerType::getUnqual(IntptrTy);
  Type *VoidTy = Type::getVoidTy(*C);
  IRBuilder<> IRB(*C);
  Int64PtrTy = PointerType::getUnqual(IRB.getInt64Ty());
  Int32PtrTy = PointerType::getUnqual(IRB.getInt32Ty());
  Int8PtrTy = PointerType::getUnqual(IRB.getInt8Ty());
  Int64Ty = IRB.getInt64Ty();
  Int32Ty = IRB.getInt32Ty();
  Int16Ty = IRB.getInt16Ty();
  Int8Ty = IRB.getInt8Ty();

  SanCovTracePCIndir = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction(SanCovTracePCIndirName, VoidTy, IntptrTy));
  SanCovTraceCmpFunction[0] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceCmp1, VoidTy, IRB.getInt8Ty(), IRB.getInt8Ty()));
  SanCovTraceCmpFunction[1] = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction(SanCovTraceCmp2, VoidTy, IRB.getInt16Ty(),
                            IRB.getInt16Ty()));
  SanCovTraceCmpFunction[2] = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction(SanCovTraceCmp4, VoidTy, IRB.getInt32Ty(),
                            IRB.getInt32Ty()));
  SanCovTraceCmpFunction[3] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceCmp8, VoidTy, Int64Ty, Int64Ty));

  SanCovTraceConstCmpFunction[0] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceConstCmp1, VoidTy, Int8Ty, Int8Ty));
  SanCovTraceConstCmpFunction[1] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceConstCmp2, VoidTy, Int16Ty, Int16Ty));
  SanCovTraceConstCmpFunction[2] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceConstCmp4, VoidTy, Int32Ty, Int32Ty));
  SanCovTraceConstCmpFunction[3] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceConstCmp8, VoidTy, Int64Ty, Int64Ty));

  SanCovTraceDivFunction[0] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceDiv4, VoidTy, IRB.getInt32Ty()));
  SanCovTraceDivFunction[1] =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceDiv8, VoidTy, Int64Ty));
  SanCovTraceGepFunction =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceGep, VoidTy, IntptrTy));
  SanCovTraceSwitchFunction =
      checkSanitizerInterfaceFunction(M.getOrInsertFunction(
          SanCovTraceSwitchName, VoidTy, Int64Ty, Int64PtrTy));

  Constant *SanCovLowestStackConstant =
      M.getOrInsertGlobal(SanCovLowestStackName, IntptrTy);
  SanCovLowestStack = cast<GlobalVariable>(SanCovLowestStackConstant);
  SanCovLowestStack->setThreadLocalMode(
      GlobalValue::ThreadLocalMode::InitialExecTLSModel);
  if (Options.StackDepth && !SanCovLowestStack->isDeclaration())
    SanCovLowestStack->setInitializer(Constant::getAllOnesValue(IntptrTy));

  // Make sure smaller parameters are zero-extended to i64 as required by the
  // x86_64 ABI.
  if (TargetTriple.getArch() == Triple::x86_64) {
    for (int i = 0; i < 3; i++) {
      SanCovTraceCmpFunction[i]->addParamAttr(0, Attribute::ZExt);
      SanCovTraceCmpFunction[i]->addParamAttr(1, Attribute::ZExt);
      SanCovTraceConstCmpFunction[i]->addParamAttr(0, Attribute::ZExt);
      SanCovTraceConstCmpFunction[i]->addParamAttr(1, Attribute::ZExt);
    }
    SanCovTraceDivFunction[0]->addParamAttr(0, Attribute::ZExt);
  }


  // We insert an empty inline asm after cov callbacks to avoid callback merge.
  EmptyAsm = InlineAsm::get(FunctionType::get(IRB.getVoidTy(), false),
                            StringRef(""), StringRef(""),
                            /*hasSideEffects=*/true);

  SanCovTracePC = checkSanitizerInterfaceFunction(
      M.getOrInsertFunction(SanCovTracePCName, VoidTy));
  SanCovTracePCGuard = checkSanitizerInterfaceFunction(M.getOrInsertFunction(
      SanCovTracePCGuardName, VoidTy, Int32PtrTy));

  for (auto &F : M)
    runOnFunction(F);

  Function *Ctor = nullptr;

  if (FunctionGuardArray)
    Ctor = CreateInitCallsForSections(M, SanCovTracePCGuardInitName, Int32PtrTy,
                                      SanCovGuardsSectionName);
  if (Function8bitCounterArray)
    Ctor = CreateInitCallsForSections(M, SanCov8bitCountersInitName, Int8PtrTy,
                                      SanCovCountersSectionName);
  if (Ctor && Options.PCTable) {
    auto SecStartEnd = CreateSecStartEnd(M, SanCovPCsSectionName, IntptrPtrTy);
    Function *InitFunction = declareSanitizerInitFunction(
        M, SanCovPCsInitName, {IntptrPtrTy, IntptrPtrTy});
    IRBuilder<> IRBCtor(Ctor->getEntryBlock().getTerminator());
    IRBCtor.CreateCall(InitFunction, {SecStartEnd.first, SecStartEnd.second});
  }
  // We don't reference these arrays directly in any of our runtime functions,
  // so we need to prevent them from being dead stripped.
  if (TargetTriple.isOSBinFormatMachO())
    appendToUsed(M, GlobalsToAppendToUsed);
  appendToCompilerUsed(M, GlobalsToAppendToCompilerUsed);
  return true;
}

// True if block has successors and it dominates all of them.
static bool isFullDominator(const BasicBlock *BB, const DominatorTree *DT) {
  if (succ_begin(BB) == succ_end(BB))
    return false;

  for (const BasicBlock *SUCC : make_range(succ_begin(BB), succ_end(BB))) {
    if (!DT->dominates(BB, SUCC))
      return false;
  }

  return true;
}

// True if block has predecessors and it postdominates all of them.
static bool isFullPostDominator(const BasicBlock *BB,
                                const PostDominatorTree *PDT) {
  if (pred_begin(BB) == pred_end(BB))
    return false;

  for (const BasicBlock *PRED : make_range(pred_begin(BB), pred_end(BB))) {
    if (!PDT->dominates(BB, PRED))
      return false;
  }

  return true;
}

static bool shouldInstrumentBlock(const Function &F, const BasicBlock *BB,
                                  const DominatorTree *DT,
                                  const PostDominatorTree *PDT,
                                  const SanitizerCoverageOptions &Options) {
  // Don't insert coverage for unreachable blocks: we will never call
  // __sanitizer_cov() for them, so counting them in
  // NumberOfInstrumentedBlocks() might complicate calculation of code coverage
  // percentage. Also, unreachable instructions frequently have no debug
  // locations.
  if (isa<UnreachableInst>(BB->getTerminator()))
    return false;

  // Don't insert coverage into blocks without a valid insertion point
  // (catchswitch blocks).
  if (BB->getFirstInsertionPt() == BB->end())
    return false;

  if (Options.NoPrune || &F.getEntryBlock() == BB)
    return true;

  if (Options.CoverageType == SanitizerCoverageOptions::SCK_Function &&
      &F.getEntryBlock() != BB)
    return false;

  // Do not instrument full dominators, or full post-dominators with multiple
  // predecessors.
  return !isFullDominator(BB, DT)
    && !(isFullPostDominator(BB, PDT) && !BB->getSinglePredecessor());
}

bool SanitizerCoverageModule::runOnFunction(Function &F) {
  if (F.empty())
    return false;
  if (F.getName().find(".module_ctor") != std::string::npos)
    return false; // Should not instrument sanitizer init functions.
  if (F.getName().startswith("__sanitizer_"))
    return false;  // Don't instrument __sanitizer_* callbacks.
  // Don't touch available_externally functions, their actual body is elewhere.
  if (F.getLinkage() == GlobalValue::AvailableExternallyLinkage)
    return false;
  // Don't instrument MSVC CRT configuration helpers. They may run before normal
  // initialization.
  if (F.getName() == "__local_stdio_printf_options" ||
      F.getName() == "__local_stdio_scanf_options")
    return false;
  if (isa<UnreachableInst>(F.getEntryBlock().getTerminator()))
    return false;
  // Don't instrument functions using SEH for now. Splitting basic blocks like
  // we do for coverage breaks WinEHPrepare.
  // FIXME: Remove this when SEH no longer uses landingpad pattern matching.
  if (F.hasPersonalityFn() &&
      isAsynchronousEHPersonality(classifyEHPersonality(F.getPersonalityFn())))
    return false;
  if (Options.CoverageType >= SanitizerCoverageOptions::SCK_Edge)
    SplitAllCriticalEdges(F);
  SmallVector<Instruction *, 8> IndirCalls;
  SmallVector<BasicBlock *, 16> BlocksToInstrument;
  SmallVector<Instruction *, 8> CmpTraceTargets;
  SmallVector<Instruction *, 8> SwitchTraceTargets;
  SmallVector<BinaryOperator *, 8> DivTraceTargets;
  SmallVector<GetElementPtrInst *, 8> GepTraceTargets;

  const DominatorTree *DT =
      &getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
  const PostDominatorTree *PDT =
      &getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();
  bool IsLeafFunc = true;

  for (auto &BB : F) {
    if (shouldInstrumentBlock(F, &BB, DT, PDT, Options))
      BlocksToInstrument.push_back(&BB);
    for (auto &Inst : BB) {
      if (Options.IndirectCalls) {
        CallSite CS(&Inst);
        if (CS && !CS.getCalledFunction())
          IndirCalls.push_back(&Inst);
      }
      if (Options.TraceCmp) {
        if (isa<ICmpInst>(&Inst))
          CmpTraceTargets.push_back(&Inst);
        if (isa<SwitchInst>(&Inst))
          SwitchTraceTargets.push_back(&Inst);
      }
      if (Options.TraceDiv)
        if (BinaryOperator *BO = dyn_cast<BinaryOperator>(&Inst))
          if (BO->getOpcode() == Instruction::SDiv ||
              BO->getOpcode() == Instruction::UDiv)
            DivTraceTargets.push_back(BO);
      if (Options.TraceGep)
        if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(&Inst))
          GepTraceTargets.push_back(GEP);
      if (Options.StackDepth)
        if (isa<InvokeInst>(Inst) ||
            (isa<CallInst>(Inst) && !isa<IntrinsicInst>(Inst)))
          IsLeafFunc = false;
    }
  }

  InjectCoverage(F, BlocksToInstrument, IsLeafFunc);
  InjectCoverageForIndirectCalls(F, IndirCalls);
  InjectTraceForCmp(F, CmpTraceTargets);
  InjectTraceForSwitch(F, SwitchTraceTargets);
  InjectTraceForDiv(F, DivTraceTargets);
  InjectTraceForGep(F, GepTraceTargets);
  return true;
}

GlobalVariable *SanitizerCoverageModule::CreateFunctionLocalArrayInSection(
    size_t NumElements, Function &F, Type *Ty, const char *Section) {
  ArrayType *ArrayTy = ArrayType::get(Ty, NumElements);
  auto Array = new GlobalVariable(
      *CurModule, ArrayTy, false, GlobalVariable::PrivateLinkage,
      Constant::getNullValue(ArrayTy), "__sancov_gen_");

  if (TargetTriple.supportsCOMDAT() && !F.isInterposable())
    if (auto Comdat =
            GetOrCreateFunctionComdat(F, TargetTriple, CurModuleUniqueId))
      Array->setComdat(Comdat);
  Array->setSection(getSectionName(Section));
  Array->setAlignment(Ty->isPointerTy() ? DL->getPointerSize()
                                        : Ty->getPrimitiveSizeInBits() / 8);
  GlobalsToAppendToUsed.push_back(Array);
  GlobalsToAppendToCompilerUsed.push_back(Array);
  MDNode *MD = MDNode::get(F.getContext(), ValueAsMetadata::get(&F));
  Array->addMetadata(LLVMContext::MD_associated, *MD);

  return Array;
}

GlobalVariable *
SanitizerCoverageModule::CreatePCArray(Function &F,
                                       ArrayRef<BasicBlock *> AllBlocks) {
  size_t N = AllBlocks.size();
  assert(N);
  SmallVector<Constant *, 32> PCs;
  IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
  for (size_t i = 0; i < N; i++) {
    if (&F.getEntryBlock() == AllBlocks[i]) {
      PCs.push_back((Constant *)IRB.CreatePointerCast(&F, IntptrPtrTy));
      PCs.push_back((Constant *)IRB.CreateIntToPtr(
          ConstantInt::get(IntptrTy, 1), IntptrPtrTy));
    } else {
      PCs.push_back((Constant *)IRB.CreatePointerCast(
          BlockAddress::get(AllBlocks[i]), IntptrPtrTy));
      PCs.push_back((Constant *)IRB.CreateIntToPtr(
          ConstantInt::get(IntptrTy, 0), IntptrPtrTy));
    }
  }
  auto *PCArray = CreateFunctionLocalArrayInSection(N * 2, F, IntptrPtrTy,
                                                    SanCovPCsSectionName);
  PCArray->setInitializer(
      ConstantArray::get(ArrayType::get(IntptrPtrTy, N * 2), PCs));
  PCArray->setConstant(true);

  return PCArray;
}

void SanitizerCoverageModule::CreateFunctionLocalArrays(
    Function &F, ArrayRef<BasicBlock *> AllBlocks) {
  if (Options.TracePCGuard)
    FunctionGuardArray = CreateFunctionLocalArrayInSection(
        AllBlocks.size(), F, Int32Ty, SanCovGuardsSectionName);

  if (Options.Inline8bitCounters)
    Function8bitCounterArray = CreateFunctionLocalArrayInSection(
        AllBlocks.size(), F, Int8Ty, SanCovCountersSectionName);

  if (Options.PCTable)
    FunctionPCsArray = CreatePCArray(F, AllBlocks);
}

bool SanitizerCoverageModule::InjectCoverage(Function &F,
                                             ArrayRef<BasicBlock *> AllBlocks,
                                             bool IsLeafFunc) {
  if (AllBlocks.empty()) return false;
  CreateFunctionLocalArrays(F, AllBlocks);
  for (size_t i = 0, N = AllBlocks.size(); i < N; i++)
    InjectCoverageAtBlock(F, *AllBlocks[i], i, IsLeafFunc);
  return true;
}

// On every indirect call we call a run-time function
// __sanitizer_cov_indir_call* with two parameters:
//   - callee address,
//   - global cache array that contains CacheSize pointers (zero-initialized).
//     The cache is used to speed up recording the caller-callee pairs.
// The address of the caller is passed implicitly via caller PC.
// CacheSize is encoded in the name of the run-time function.
void SanitizerCoverageModule::InjectCoverageForIndirectCalls(
    Function &F, ArrayRef<Instruction *> IndirCalls) {
  if (IndirCalls.empty())
    return;
  assert(Options.TracePC || Options.TracePCGuard || Options.Inline8bitCounters);
  for (auto I : IndirCalls) {
    IRBuilder<> IRB(I);
    CallSite CS(I);
    Value *Callee = CS.getCalledValue();
    if (isa<InlineAsm>(Callee))
      continue;
    IRB.CreateCall(SanCovTracePCIndir, IRB.CreatePointerCast(Callee, IntptrTy));
  }
}

// For every switch statement we insert a call:
// __sanitizer_cov_trace_switch(CondValue,
//      {NumCases, ValueSizeInBits, Case0Value, Case1Value, Case2Value, ... })

void SanitizerCoverageModule::InjectTraceForSwitch(
    Function &, ArrayRef<Instruction *> SwitchTraceTargets) {
  for (auto I : SwitchTraceTargets) {
    if (SwitchInst *SI = dyn_cast<SwitchInst>(I)) {
      IRBuilder<> IRB(I);
      SmallVector<Constant *, 16> Initializers;
      Value *Cond = SI->getCondition();
      if (Cond->getType()->getScalarSizeInBits() >
          Int64Ty->getScalarSizeInBits())
        continue;
      Initializers.push_back(ConstantInt::get(Int64Ty, SI->getNumCases()));
      Initializers.push_back(
          ConstantInt::get(Int64Ty, Cond->getType()->getScalarSizeInBits()));
      if (Cond->getType()->getScalarSizeInBits() <
          Int64Ty->getScalarSizeInBits())
        Cond = IRB.CreateIntCast(Cond, Int64Ty, false);
      for (auto It : SI->cases()) {
        Constant *C = It.getCaseValue();
        if (C->getType()->getScalarSizeInBits() <
            Int64Ty->getScalarSizeInBits())
          C = ConstantExpr::getCast(CastInst::ZExt, It.getCaseValue(), Int64Ty);
        Initializers.push_back(C);
      }
      llvm::sort(Initializers.begin() + 2, Initializers.end(),
                 [](const Constant *A, const Constant *B) {
                   return cast<ConstantInt>(A)->getLimitedValue() <
                          cast<ConstantInt>(B)->getLimitedValue();
                 });
      ArrayType *ArrayOfInt64Ty = ArrayType::get(Int64Ty, Initializers.size());
      GlobalVariable *GV = new GlobalVariable(
          *CurModule, ArrayOfInt64Ty, false, GlobalVariable::InternalLinkage,
          ConstantArray::get(ArrayOfInt64Ty, Initializers),
          "__sancov_gen_cov_switch_values");
      IRB.CreateCall(SanCovTraceSwitchFunction,
                     {Cond, IRB.CreatePointerCast(GV, Int64PtrTy)});
    }
  }
}

void SanitizerCoverageModule::InjectTraceForDiv(
    Function &, ArrayRef<BinaryOperator *> DivTraceTargets) {
  for (auto BO : DivTraceTargets) {
    IRBuilder<> IRB(BO);
    Value *A1 = BO->getOperand(1);
    if (isa<ConstantInt>(A1)) continue;
    if (!A1->getType()->isIntegerTy())
      continue;
    uint64_t TypeSize = DL->getTypeStoreSizeInBits(A1->getType());
    int CallbackIdx = TypeSize == 32 ? 0 :
        TypeSize == 64 ? 1 : -1;
    if (CallbackIdx < 0) continue;
    auto Ty = Type::getIntNTy(*C, TypeSize);
    IRB.CreateCall(SanCovTraceDivFunction[CallbackIdx],
                   {IRB.CreateIntCast(A1, Ty, true)});
  }
}

void SanitizerCoverageModule::InjectTraceForGep(
    Function &, ArrayRef<GetElementPtrInst *> GepTraceTargets) {
  for (auto GEP : GepTraceTargets) {
    IRBuilder<> IRB(GEP);
    for (auto I = GEP->idx_begin(); I != GEP->idx_end(); ++I)
      if (!isa<ConstantInt>(*I) && (*I)->getType()->isIntegerTy())
        IRB.CreateCall(SanCovTraceGepFunction,
                       {IRB.CreateIntCast(*I, IntptrTy, true)});
  }
}

void SanitizerCoverageModule::InjectTraceForCmp(
    Function &, ArrayRef<Instruction *> CmpTraceTargets) {
  for (auto I : CmpTraceTargets) {
    if (ICmpInst *ICMP = dyn_cast<ICmpInst>(I)) {
      IRBuilder<> IRB(ICMP);
      Value *A0 = ICMP->getOperand(0);
      Value *A1 = ICMP->getOperand(1);
      if (!A0->getType()->isIntegerTy())
        continue;
      uint64_t TypeSize = DL->getTypeStoreSizeInBits(A0->getType());
      int CallbackIdx = TypeSize == 8 ? 0 :
                        TypeSize == 16 ? 1 :
                        TypeSize == 32 ? 2 :
                        TypeSize == 64 ? 3 : -1;
      if (CallbackIdx < 0) continue;
      // __sanitizer_cov_trace_cmp((type_size << 32) | predicate, A0, A1);
      auto CallbackFunc = SanCovTraceCmpFunction[CallbackIdx];
      bool FirstIsConst = isa<ConstantInt>(A0);
      bool SecondIsConst = isa<ConstantInt>(A1);
      // If both are const, then we don't need such a comparison.
      if (FirstIsConst && SecondIsConst) continue;
      // If only one is const, then make it the first callback argument.
      if (FirstIsConst || SecondIsConst) {
        CallbackFunc = SanCovTraceConstCmpFunction[CallbackIdx];
        if (SecondIsConst)
          std::swap(A0, A1);
      }

      auto Ty = Type::getIntNTy(*C, TypeSize);
      IRB.CreateCall(CallbackFunc, {IRB.CreateIntCast(A0, Ty, true),
              IRB.CreateIntCast(A1, Ty, true)});
    }
  }
}

void SanitizerCoverageModule::InjectCoverageAtBlock(Function &F, BasicBlock &BB,
                                                    size_t Idx,
                                                    bool IsLeafFunc) {
  BasicBlock::iterator IP = BB.getFirstInsertionPt();
  bool IsEntryBB = &BB == &F.getEntryBlock();
  DebugLoc EntryLoc;
  if (IsEntryBB) {
    if (auto SP = F.getSubprogram())
      EntryLoc = DebugLoc::get(SP->getScopeLine(), 0, SP);
    // Keep static allocas and llvm.localescape calls in the entry block.  Even
    // if we aren't splitting the block, it's nice for allocas to be before
    // calls.
    IP = PrepareToSplitEntryBlock(BB, IP);
  } else {
    EntryLoc = IP->getDebugLoc();
  }

  IRBuilder<> IRB(&*IP);
  IRB.SetCurrentDebugLocation(EntryLoc);
  if (Options.TracePC) {
    IRB.CreateCall(SanCovTracePC); // gets the PC using GET_CALLER_PC.
    IRB.CreateCall(EmptyAsm, {}); // Avoids callback merge.
  }
  if (Options.TracePCGuard) {
    auto GuardPtr = IRB.CreateIntToPtr(
        IRB.CreateAdd(IRB.CreatePointerCast(FunctionGuardArray, IntptrTy),
                      ConstantInt::get(IntptrTy, Idx * 4)),
        Int32PtrTy);
    IRB.CreateCall(SanCovTracePCGuard, GuardPtr);
    IRB.CreateCall(EmptyAsm, {}); // Avoids callback merge.
  }
  if (Options.Inline8bitCounters) {
    auto CounterPtr = IRB.CreateGEP(
        Function8bitCounterArray,
        {ConstantInt::get(IntptrTy, 0), ConstantInt::get(IntptrTy, Idx)});
    auto Load = IRB.CreateLoad(CounterPtr);
    auto Inc = IRB.CreateAdd(Load, ConstantInt::get(Int8Ty, 1));
    auto Store = IRB.CreateStore(Inc, CounterPtr);
    SetNoSanitizeMetadata(Load);
    SetNoSanitizeMetadata(Store);
  }
  if (Options.StackDepth && IsEntryBB && !IsLeafFunc) {
    // Check stack depth.  If it's the deepest so far, record it.
    Function *GetFrameAddr =
        Intrinsic::getDeclaration(F.getParent(), Intrinsic::frameaddress);
    auto FrameAddrPtr =
        IRB.CreateCall(GetFrameAddr, {Constant::getNullValue(Int32Ty)});
    auto FrameAddrInt = IRB.CreatePtrToInt(FrameAddrPtr, IntptrTy);
    auto LowestStack = IRB.CreateLoad(SanCovLowestStack);
    auto IsStackLower = IRB.CreateICmpULT(FrameAddrInt, LowestStack);
    auto ThenTerm = SplitBlockAndInsertIfThen(IsStackLower, &*IP, false);
    IRBuilder<> ThenIRB(ThenTerm);
    auto Store = ThenIRB.CreateStore(FrameAddrInt, SanCovLowestStack);
    SetNoSanitizeMetadata(LowestStack);
    SetNoSanitizeMetadata(Store);
  }
}

std::string
SanitizerCoverageModule::getSectionName(const std::string &Section) const {
  if (TargetTriple.isOSBinFormatCOFF()) {
    if (Section == SanCovCountersSectionName)
      return ".SCOV$CM";
    if (Section == SanCovPCsSectionName)
      return ".SCOVP$M";
    return ".SCOV$GM"; // For SanCovGuardsSectionName.
  }
  if (TargetTriple.isOSBinFormatMachO())
    return "__DATA,__" + Section;
  return "__" + Section;
}

std::string
SanitizerCoverageModule::getSectionStart(const std::string &Section) const {
  if (TargetTriple.isOSBinFormatMachO())
    return "\1section$start$__DATA$__" + Section;
  return "__start___" + Section;
}

std::string
SanitizerCoverageModule::getSectionEnd(const std::string &Section) const {
  if (TargetTriple.isOSBinFormatMachO())
    return "\1section$end$__DATA$__" + Section;
  return "__stop___" + Section;
}


char SanitizerCoverageModule::ID = 0;
INITIALIZE_PASS_BEGIN(SanitizerCoverageModule, "sancov",
                      "SanitizerCoverage: TODO."
                      "ModulePass",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(SanitizerCoverageModule, "sancov",
                    "SanitizerCoverage: TODO."
                    "ModulePass",
                    false, false)
ModulePass *llvm::createSanitizerCoverageModulePass(
    const SanitizerCoverageOptions &Options) {
  return new SanitizerCoverageModule(Options);
}
