//===- TargetPassConfig.cpp - Target independent code generation passes ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces to access the target independent code
// generation passes provided by the LLVM backend.
//
//===---------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CFLAndersAliasAnalysis.h"
#include "llvm/Analysis/CFLSteensAliasAnalysis.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/SymbolRewriter.h"
#include <cassert>
#include <string>

using namespace llvm;

cl::opt<bool> EnableIPRA("enable-ipra", cl::init(false), cl::Hidden,
                         cl::desc("Enable interprocedural register allocation "
                                  "to reduce load/store at procedure calls."));
static cl::opt<bool> DisablePostRASched("disable-post-ra", cl::Hidden,
    cl::desc("Disable Post Regalloc Scheduler"));
static cl::opt<bool> DisableBranchFold("disable-branch-fold", cl::Hidden,
    cl::desc("Disable branch folding"));
static cl::opt<bool> DisableTailDuplicate("disable-tail-duplicate", cl::Hidden,
    cl::desc("Disable tail duplication"));
static cl::opt<bool> DisableEarlyTailDup("disable-early-taildup", cl::Hidden,
    cl::desc("Disable pre-register allocation tail duplication"));
static cl::opt<bool> DisableBlockPlacement("disable-block-placement",
    cl::Hidden, cl::desc("Disable probability-driven block placement"));
static cl::opt<bool> EnableBlockPlacementStats("enable-block-placement-stats",
    cl::Hidden, cl::desc("Collect probability-driven block placement stats"));
static cl::opt<bool> DisableSSC("disable-ssc", cl::Hidden,
    cl::desc("Disable Stack Slot Coloring"));
static cl::opt<bool> DisableMachineDCE("disable-machine-dce", cl::Hidden,
    cl::desc("Disable Machine Dead Code Elimination"));
static cl::opt<bool> DisableEarlyIfConversion("disable-early-ifcvt", cl::Hidden,
    cl::desc("Disable Early If-conversion"));
static cl::opt<bool> DisableMachineLICM("disable-machine-licm", cl::Hidden,
    cl::desc("Disable Machine LICM"));
static cl::opt<bool> DisableMachineCSE("disable-machine-cse", cl::Hidden,
    cl::desc("Disable Machine Common Subexpression Elimination"));
static cl::opt<cl::boolOrDefault> OptimizeRegAlloc(
    "optimize-regalloc", cl::Hidden,
    cl::desc("Enable optimized register allocation compilation path."));
static cl::opt<bool> DisablePostRAMachineLICM("disable-postra-machine-licm",
    cl::Hidden,
    cl::desc("Disable Machine LICM"));
static cl::opt<bool> DisableMachineSink("disable-machine-sink", cl::Hidden,
    cl::desc("Disable Machine Sinking"));
static cl::opt<bool> DisablePostRAMachineSink("disable-postra-machine-sink",
    cl::Hidden,
    cl::desc("Disable PostRA Machine Sinking"));
static cl::opt<bool> DisableLSR("disable-lsr", cl::Hidden,
    cl::desc("Disable Loop Strength Reduction Pass"));
static cl::opt<bool> DisableConstantHoisting("disable-constant-hoisting",
    cl::Hidden, cl::desc("Disable ConstantHoisting"));
static cl::opt<bool> DisableCGP("disable-cgp", cl::Hidden,
    cl::desc("Disable Codegen Prepare"));
static cl::opt<bool> DisableCopyProp("disable-copyprop", cl::Hidden,
    cl::desc("Disable Copy Propagation pass"));
static cl::opt<bool> DisablePartialLibcallInlining("disable-partial-libcall-inlining",
    cl::Hidden, cl::desc("Disable Partial Libcall Inlining"));
static cl::opt<bool> EnableImplicitNullChecks(
    "enable-implicit-null-checks",
    cl::desc("Fold null checks into faulting memory operations"),
    cl::init(false), cl::Hidden);
static cl::opt<bool> DisableMergeICmps("disable-mergeicmps",
    cl::desc("Disable MergeICmps Pass"),
    cl::init(false), cl::Hidden);
static cl::opt<bool> PrintLSR("print-lsr-output", cl::Hidden,
    cl::desc("Print LLVM IR produced by the loop-reduce pass"));
static cl::opt<bool> PrintISelInput("print-isel-input", cl::Hidden,
    cl::desc("Print LLVM IR input to isel pass"));
static cl::opt<bool> PrintGCInfo("print-gc", cl::Hidden,
    cl::desc("Dump garbage collector data"));
static cl::opt<cl::boolOrDefault>
    VerifyMachineCode("verify-machineinstrs", cl::Hidden,
                      cl::desc("Verify generated machine code"),
                      cl::ZeroOrMore);
enum RunOutliner { AlwaysOutline, NeverOutline, TargetDefault };
// Enable or disable the MachineOutliner.
static cl::opt<RunOutliner> EnableMachineOutliner(
    "enable-machine-outliner", cl::desc("Enable the machine outliner"),
    cl::Hidden, cl::ValueOptional, cl::init(TargetDefault),
    cl::values(clEnumValN(AlwaysOutline, "always",
                          "Run on all functions guaranteed to be beneficial"),
               clEnumValN(NeverOutline, "never", "Disable all outlining"),
               // Sentinel value for unspecified option.
               clEnumValN(AlwaysOutline, "", "")));
// Enable or disable FastISel. Both options are needed, because
// FastISel is enabled by default with -fast, and we wish to be
// able to enable or disable fast-isel independently from -O0.
static cl::opt<cl::boolOrDefault>
EnableFastISelOption("fast-isel", cl::Hidden,
  cl::desc("Enable the \"fast\" instruction selector"));

static cl::opt<cl::boolOrDefault> EnableGlobalISelOption(
    "global-isel", cl::Hidden,
    cl::desc("Enable the \"global\" instruction selector"));

static cl::opt<std::string> PrintMachineInstrs(
    "print-machineinstrs", cl::ValueOptional, cl::desc("Print machine instrs"),
    cl::value_desc("pass-name"), cl::init("option-unspecified"), cl::Hidden);

static cl::opt<GlobalISelAbortMode> EnableGlobalISelAbort(
    "global-isel-abort", cl::Hidden,
    cl::desc("Enable abort calls when \"global\" instruction selection "
             "fails to lower/select an instruction"),
    cl::values(
        clEnumValN(GlobalISelAbortMode::Disable, "0", "Disable the abort"),
        clEnumValN(GlobalISelAbortMode::Enable, "1", "Enable the abort"),
        clEnumValN(GlobalISelAbortMode::DisableWithDiag, "2",
                   "Disable the abort but emit a diagnostic on failure")));

// Temporary option to allow experimenting with MachineScheduler as a post-RA
// scheduler. Targets can "properly" enable this with
// substitutePass(&PostRASchedulerID, &PostMachineSchedulerID).
// Targets can return true in targetSchedulesPostRAScheduling() and
// insert a PostRA scheduling pass wherever it wants.
cl::opt<bool> MISchedPostRA("misched-postra", cl::Hidden,
  cl::desc("Run MachineScheduler post regalloc (independent of preRA sched)"));

// Experimental option to run live interval analysis early.
static cl::opt<bool> EarlyLiveIntervals("early-live-intervals", cl::Hidden,
    cl::desc("Run live interval analysis earlier in the pipeline"));

// Experimental option to use CFL-AA in codegen
enum class CFLAAType { None, Steensgaard, Andersen, Both };
static cl::opt<CFLAAType> UseCFLAA(
    "use-cfl-aa-in-codegen", cl::init(CFLAAType::None), cl::Hidden,
    cl::desc("Enable the new, experimental CFL alias analysis in CodeGen"),
    cl::values(clEnumValN(CFLAAType::None, "none", "Disable CFL-AA"),
               clEnumValN(CFLAAType::Steensgaard, "steens",
                          "Enable unification-based CFL-AA"),
               clEnumValN(CFLAAType::Andersen, "anders",
                          "Enable inclusion-based CFL-AA"),
               clEnumValN(CFLAAType::Both, "both",
                          "Enable both variants of CFL-AA")));

/// Option names for limiting the codegen pipeline.
/// Those are used in error reporting and we didn't want
/// to duplicate their names all over the place.
const char *StartAfterOptName = "start-after";
const char *StartBeforeOptName = "start-before";
const char *StopAfterOptName = "stop-after";
const char *StopBeforeOptName = "stop-before";

static cl::opt<std::string>
    StartAfterOpt(StringRef(StartAfterOptName),
                  cl::desc("Resume compilation after a specific pass"),
                  cl::value_desc("pass-name"), cl::init(""), cl::Hidden);

static cl::opt<std::string>
    StartBeforeOpt(StringRef(StartBeforeOptName),
                   cl::desc("Resume compilation before a specific pass"),
                   cl::value_desc("pass-name"), cl::init(""), cl::Hidden);

static cl::opt<std::string>
    StopAfterOpt(StringRef(StopAfterOptName),
                 cl::desc("Stop compilation after a specific pass"),
                 cl::value_desc("pass-name"), cl::init(""), cl::Hidden);

static cl::opt<std::string>
    StopBeforeOpt(StringRef(StopBeforeOptName),
                  cl::desc("Stop compilation before a specific pass"),
                  cl::value_desc("pass-name"), cl::init(""), cl::Hidden);

/// Allow standard passes to be disabled by command line options. This supports
/// simple binary flags that either suppress the pass or do nothing.
/// i.e. -disable-mypass=false has no effect.
/// These should be converted to boolOrDefault in order to use applyOverride.
static IdentifyingPassPtr applyDisable(IdentifyingPassPtr PassID,
                                       bool Override) {
  if (Override)
    return IdentifyingPassPtr();
  return PassID;
}

/// Allow standard passes to be disabled by the command line, regardless of who
/// is adding the pass.
///
/// StandardID is the pass identified in the standard pass pipeline and provided
/// to addPass(). It may be a target-specific ID in the case that the target
/// directly adds its own pass, but in that case we harmlessly fall through.
///
/// TargetID is the pass that the target has configured to override StandardID.
///
/// StandardID may be a pseudo ID. In that case TargetID is the name of the real
/// pass to run. This allows multiple options to control a single pass depending
/// on where in the pipeline that pass is added.
static IdentifyingPassPtr overridePass(AnalysisID StandardID,
                                       IdentifyingPassPtr TargetID) {
  if (StandardID == &PostRASchedulerID)
    return applyDisable(TargetID, DisablePostRASched);

  if (StandardID == &BranchFolderPassID)
    return applyDisable(TargetID, DisableBranchFold);

  if (StandardID == &TailDuplicateID)
    return applyDisable(TargetID, DisableTailDuplicate);

  if (StandardID == &EarlyTailDuplicateID)
    return applyDisable(TargetID, DisableEarlyTailDup);

  if (StandardID == &MachineBlockPlacementID)
    return applyDisable(TargetID, DisableBlockPlacement);

  if (StandardID == &StackSlotColoringID)
    return applyDisable(TargetID, DisableSSC);

  if (StandardID == &DeadMachineInstructionElimID)
    return applyDisable(TargetID, DisableMachineDCE);

  if (StandardID == &EarlyIfConverterID)
    return applyDisable(TargetID, DisableEarlyIfConversion);

  if (StandardID == &EarlyMachineLICMID)
    return applyDisable(TargetID, DisableMachineLICM);

  if (StandardID == &MachineCSEID)
    return applyDisable(TargetID, DisableMachineCSE);

  if (StandardID == &MachineLICMID)
    return applyDisable(TargetID, DisablePostRAMachineLICM);

  if (StandardID == &MachineSinkingID)
    return applyDisable(TargetID, DisableMachineSink);

  if (StandardID == &PostRAMachineSinkingID)
    return applyDisable(TargetID, DisablePostRAMachineSink);

  if (StandardID == &MachineCopyPropagationID)
    return applyDisable(TargetID, DisableCopyProp);

  return TargetID;
}

//===---------------------------------------------------------------------===//
/// TargetPassConfig
//===---------------------------------------------------------------------===//

INITIALIZE_PASS(TargetPassConfig, "targetpassconfig",
                "Target Pass Configuration", false, false)
char TargetPassConfig::ID = 0;

namespace {

struct InsertedPass {
  AnalysisID TargetPassID;
  IdentifyingPassPtr InsertedPassID;
  bool VerifyAfter;
  bool PrintAfter;

  InsertedPass(AnalysisID TargetPassID, IdentifyingPassPtr InsertedPassID,
               bool VerifyAfter, bool PrintAfter)
      : TargetPassID(TargetPassID), InsertedPassID(InsertedPassID),
        VerifyAfter(VerifyAfter), PrintAfter(PrintAfter) {}

  Pass *getInsertedPass() const {
    assert(InsertedPassID.isValid() && "Illegal Pass ID!");
    if (InsertedPassID.isInstance())
      return InsertedPassID.getInstance();
    Pass *NP = Pass::createPass(InsertedPassID.getID());
    assert(NP && "Pass ID not registered");
    return NP;
  }
};

} // end anonymous namespace

namespace llvm {

class PassConfigImpl {
public:
  // List of passes explicitly substituted by this target. Normally this is
  // empty, but it is a convenient way to suppress or replace specific passes
  // that are part of a standard pass pipeline without overridding the entire
  // pipeline. This mechanism allows target options to inherit a standard pass's
  // user interface. For example, a target may disable a standard pass by
  // default by substituting a pass ID of zero, and the user may still enable
  // that standard pass with an explicit command line option.
  DenseMap<AnalysisID,IdentifyingPassPtr> TargetPasses;

  /// Store the pairs of <AnalysisID, AnalysisID> of which the second pass
  /// is inserted after each instance of the first one.
  SmallVector<InsertedPass, 4> InsertedPasses;
};

} // end namespace llvm

// Out of line virtual method.
TargetPassConfig::~TargetPassConfig() {
  delete Impl;
}

static const PassInfo *getPassInfo(StringRef PassName) {
  if (PassName.empty())
    return nullptr;

  const PassRegistry &PR = *PassRegistry::getPassRegistry();
  const PassInfo *PI = PR.getPassInfo(PassName);
  if (!PI)
    report_fatal_error(Twine('\"') + Twine(PassName) +
                       Twine("\" pass is not registered."));
  return PI;
}

static AnalysisID getPassIDFromName(StringRef PassName) {
  const PassInfo *PI = getPassInfo(PassName);
  return PI ? PI->getTypeInfo() : nullptr;
}

static std::pair<StringRef, unsigned>
getPassNameAndInstanceNum(StringRef PassName) {
  StringRef Name, InstanceNumStr;
  std::tie(Name, InstanceNumStr) = PassName.split(',');

  unsigned InstanceNum = 0;
  if (!InstanceNumStr.empty() && InstanceNumStr.getAsInteger(10, InstanceNum))
    report_fatal_error("invalid pass instance specifier " + PassName);

  return std::make_pair(Name, InstanceNum);
}

void TargetPassConfig::setStartStopPasses() {
  StringRef StartBeforeName;
  std::tie(StartBeforeName, StartBeforeInstanceNum) =
    getPassNameAndInstanceNum(StartBeforeOpt);

  StringRef StartAfterName;
  std::tie(StartAfterName, StartAfterInstanceNum) =
    getPassNameAndInstanceNum(StartAfterOpt);

  StringRef StopBeforeName;
  std::tie(StopBeforeName, StopBeforeInstanceNum)
    = getPassNameAndInstanceNum(StopBeforeOpt);

  StringRef StopAfterName;
  std::tie(StopAfterName, StopAfterInstanceNum)
    = getPassNameAndInstanceNum(StopAfterOpt);

  StartBefore = getPassIDFromName(StartBeforeName);
  StartAfter = getPassIDFromName(StartAfterName);
  StopBefore = getPassIDFromName(StopBeforeName);
  StopAfter = getPassIDFromName(StopAfterName);
  if (StartBefore && StartAfter)
    report_fatal_error(Twine(StartBeforeOptName) + Twine(" and ") +
                       Twine(StartAfterOptName) + Twine(" specified!"));
  if (StopBefore && StopAfter)
    report_fatal_error(Twine(StopBeforeOptName) + Twine(" and ") +
                       Twine(StopAfterOptName) + Twine(" specified!"));
  Started = (StartAfter == nullptr) && (StartBefore == nullptr);
}

// Out of line constructor provides default values for pass options and
// registers all common codegen passes.
TargetPassConfig::TargetPassConfig(LLVMTargetMachine &TM, PassManagerBase &pm)
    : ImmutablePass(ID), PM(&pm), TM(&TM) {
  Impl = new PassConfigImpl();

  // Register all target independent codegen passes to activate their PassIDs,
  // including this pass itself.
  initializeCodeGen(*PassRegistry::getPassRegistry());

  // Also register alias analysis passes required by codegen passes.
  initializeBasicAAWrapperPassPass(*PassRegistry::getPassRegistry());
  initializeAAResultsWrapperPassPass(*PassRegistry::getPassRegistry());

  if (StringRef(PrintMachineInstrs.getValue()).equals(""))
    TM.Options.PrintMachineCode = true;

  if (EnableIPRA.getNumOccurrences())
    TM.Options.EnableIPRA = EnableIPRA;
  else {
    // If not explicitly specified, use target default.
    TM.Options.EnableIPRA = TM.useIPRA();
  }

  if (TM.Options.EnableIPRA)
    setRequiresCodeGenSCCOrder();

  if (EnableGlobalISelAbort.getNumOccurrences())
    TM.Options.GlobalISelAbort = EnableGlobalISelAbort;

  setStartStopPasses();
}

CodeGenOpt::Level TargetPassConfig::getOptLevel() const {
  return TM->getOptLevel();
}

/// Insert InsertedPassID pass after TargetPassID.
void TargetPassConfig::insertPass(AnalysisID TargetPassID,
                                  IdentifyingPassPtr InsertedPassID,
                                  bool VerifyAfter, bool PrintAfter) {
  assert(((!InsertedPassID.isInstance() &&
           TargetPassID != InsertedPassID.getID()) ||
          (InsertedPassID.isInstance() &&
           TargetPassID != InsertedPassID.getInstance()->getPassID())) &&
         "Insert a pass after itself!");
  Impl->InsertedPasses.emplace_back(TargetPassID, InsertedPassID, VerifyAfter,
                                    PrintAfter);
}

/// createPassConfig - Create a pass configuration object to be used by
/// addPassToEmitX methods for generating a pipeline of CodeGen passes.
///
/// Targets may override this to extend TargetPassConfig.
TargetPassConfig *LLVMTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new TargetPassConfig(*this, PM);
}

TargetPassConfig::TargetPassConfig()
  : ImmutablePass(ID) {
  report_fatal_error("Trying to construct TargetPassConfig without a target "
                     "machine. Scheduling a CodeGen pass without a target "
                     "triple set?");
}

bool TargetPassConfig::willCompleteCodeGenPipeline() {
  return StopBeforeOpt.empty() && StopAfterOpt.empty();
}

bool TargetPassConfig::hasLimitedCodeGenPipeline() {
  return !StartBeforeOpt.empty() || !StartAfterOpt.empty() ||
         !willCompleteCodeGenPipeline();
}

std::string
TargetPassConfig::getLimitedCodeGenPipelineReason(const char *Separator) const {
  if (!hasLimitedCodeGenPipeline())
    return std::string();
  std::string Res;
  static cl::opt<std::string> *PassNames[] = {&StartAfterOpt, &StartBeforeOpt,
                                              &StopAfterOpt, &StopBeforeOpt};
  static const char *OptNames[] = {StartAfterOptName, StartBeforeOptName,
                                   StopAfterOptName, StopBeforeOptName};
  bool IsFirst = true;
  for (int Idx = 0; Idx < 4; ++Idx)
    if (!PassNames[Idx]->empty()) {
      if (!IsFirst)
        Res += Separator;
      IsFirst = false;
      Res += OptNames[Idx];
    }
  return Res;
}

// Helper to verify the analysis is really immutable.
void TargetPassConfig::setOpt(bool &Opt, bool Val) {
  assert(!Initialized && "PassConfig is immutable");
  Opt = Val;
}

void TargetPassConfig::substitutePass(AnalysisID StandardID,
                                      IdentifyingPassPtr TargetID) {
  Impl->TargetPasses[StandardID] = TargetID;
}

IdentifyingPassPtr TargetPassConfig::getPassSubstitution(AnalysisID ID) const {
  DenseMap<AnalysisID, IdentifyingPassPtr>::const_iterator
    I = Impl->TargetPasses.find(ID);
  if (I == Impl->TargetPasses.end())
    return ID;
  return I->second;
}

bool TargetPassConfig::isPassSubstitutedOrOverridden(AnalysisID ID) const {
  IdentifyingPassPtr TargetID = getPassSubstitution(ID);
  IdentifyingPassPtr FinalPtr = overridePass(ID, TargetID);
  return !FinalPtr.isValid() || FinalPtr.isInstance() ||
      FinalPtr.getID() != ID;
}

/// Add a pass to the PassManager if that pass is supposed to be run.  If the
/// Started/Stopped flags indicate either that the compilation should start at
/// a later pass or that it should stop after an earlier pass, then do not add
/// the pass.  Finally, compare the current pass against the StartAfter
/// and StopAfter options and change the Started/Stopped flags accordingly.
void TargetPassConfig::addPass(Pass *P, bool verifyAfter, bool printAfter) {
  assert(!Initialized && "PassConfig is immutable");

  // Cache the Pass ID here in case the pass manager finds this pass is
  // redundant with ones already scheduled / available, and deletes it.
  // Fundamentally, once we add the pass to the manager, we no longer own it
  // and shouldn't reference it.
  AnalysisID PassID = P->getPassID();

  if (StartBefore == PassID && StartBeforeCount++ == StartBeforeInstanceNum)
    Started = true;
  if (StopBefore == PassID && StopBeforeCount++ == StopBeforeInstanceNum)
    Stopped = true;
  if (Started && !Stopped) {
    std::string Banner;
    // Construct banner message before PM->add() as that may delete the pass.
    if (AddingMachinePasses && (printAfter || verifyAfter))
      Banner = std::string("After ") + std::string(P->getPassName());
    PM->add(P);
    if (AddingMachinePasses) {
      if (printAfter)
        addPrintPass(Banner);
      if (verifyAfter)
        addVerifyPass(Banner);
    }

    // Add the passes after the pass P if there is any.
    for (auto IP : Impl->InsertedPasses) {
      if (IP.TargetPassID == PassID)
        addPass(IP.getInsertedPass(), IP.VerifyAfter, IP.PrintAfter);
    }
  } else {
    delete P;
  }

  if (StopAfter == PassID && StopAfterCount++ == StopAfterInstanceNum)
    Stopped = true;

  if (StartAfter == PassID && StartAfterCount++ == StartAfterInstanceNum)
    Started = true;
  if (Stopped && !Started)
    report_fatal_error("Cannot stop compilation after pass that is not run");
}

/// Add a CodeGen pass at this point in the pipeline after checking for target
/// and command line overrides.
///
/// addPass cannot return a pointer to the pass instance because is internal the
/// PassManager and the instance we create here may already be freed.
AnalysisID TargetPassConfig::addPass(AnalysisID PassID, bool verifyAfter,
                                     bool printAfter) {
  IdentifyingPassPtr TargetID = getPassSubstitution(PassID);
  IdentifyingPassPtr FinalPtr = overridePass(PassID, TargetID);
  if (!FinalPtr.isValid())
    return nullptr;

  Pass *P;
  if (FinalPtr.isInstance())
    P = FinalPtr.getInstance();
  else {
    P = Pass::createPass(FinalPtr.getID());
    if (!P)
      llvm_unreachable("Pass ID not registered");
  }
  AnalysisID FinalID = P->getPassID();
  addPass(P, verifyAfter, printAfter); // Ends the lifetime of P.

  return FinalID;
}

void TargetPassConfig::printAndVerify(const std::string &Banner) {
  addPrintPass(Banner);
  addVerifyPass(Banner);
}

void TargetPassConfig::addPrintPass(const std::string &Banner) {
  if (TM->shouldPrintMachineCode())
    PM->add(createMachineFunctionPrinterPass(dbgs(), Banner));
}

void TargetPassConfig::addVerifyPass(const std::string &Banner) {
  bool Verify = VerifyMachineCode == cl::BOU_TRUE;
#ifdef EXPENSIVE_CHECKS
  if (VerifyMachineCode == cl::BOU_UNSET)
    Verify = TM->isMachineVerifierClean();
#endif
  if (Verify)
    PM->add(createMachineVerifierPass(Banner));
}

/// Add common target configurable passes that perform LLVM IR to IR transforms
/// following machine independent optimization.
void TargetPassConfig::addIRPasses() {
  switch (UseCFLAA) {
  case CFLAAType::Steensgaard:
    addPass(createCFLSteensAAWrapperPass());
    break;
  case CFLAAType::Andersen:
    addPass(createCFLAndersAAWrapperPass());
    break;
  case CFLAAType::Both:
    addPass(createCFLAndersAAWrapperPass());
    addPass(createCFLSteensAAWrapperPass());
    break;
  default:
    break;
  }

  // Basic AliasAnalysis support.
  // Add TypeBasedAliasAnalysis before BasicAliasAnalysis so that
  // BasicAliasAnalysis wins if they disagree. This is intended to help
  // support "obvious" type-punning idioms.
  addPass(createTypeBasedAAWrapperPass());
  addPass(createScopedNoAliasAAWrapperPass());
  addPass(createBasicAAWrapperPass());

  // Before running any passes, run the verifier to determine if the input
  // coming from the front-end and/or optimizer is valid.
  if (!DisableVerify)
    addPass(createVerifierPass());

  // Run loop strength reduction before anything else.
  if (getOptLevel() != CodeGenOpt::None && !DisableLSR) {
    addPass(createLoopStrengthReducePass());
    if (PrintLSR)
      addPass(createPrintFunctionPass(dbgs(), "\n\n*** Code after LSR ***\n"));
  }

  if (getOptLevel() != CodeGenOpt::None) {
    // The MergeICmpsPass tries to create memcmp calls by grouping sequences of
    // loads and compares. ExpandMemCmpPass then tries to expand those calls
    // into optimally-sized loads and compares. The transforms are enabled by a
    // target lowering hook.
    if (!DisableMergeICmps)
      addPass(createMergeICmpsPass());
    addPass(createExpandMemCmpPass());
  }

  // Run GC lowering passes for builtin collectors
  // TODO: add a pass insertion point here
  addPass(createGCLoweringPass());
  addPass(createShadowStackGCLoweringPass());

  // Make sure that no unreachable blocks are instruction selected.
  addPass(createUnreachableBlockEliminationPass());

  // Prepare expensive constants for SelectionDAG.
  if (getOptLevel() != CodeGenOpt::None && !DisableConstantHoisting)
    addPass(createConstantHoistingPass());

  if (getOptLevel() != CodeGenOpt::None && !DisablePartialLibcallInlining)
    addPass(createPartiallyInlineLibCallsPass());

  // Instrument function entry and exit, e.g. with calls to mcount().
  addPass(createPostInlineEntryExitInstrumenterPass());

  // Add scalarization of target's unsupported masked memory intrinsics pass.
  // the unsupported intrinsic will be replaced with a chain of basic blocks,
  // that stores/loads element one-by-one if the appropriate mask bit is set.
  addPass(createScalarizeMaskedMemIntrinPass());

  // Expand reduction intrinsics into shuffle sequences if the target wants to.
  addPass(createExpandReductionsPass());
}

/// Turn exception handling constructs into something the code generators can
/// handle.
void TargetPassConfig::addPassesToHandleExceptions() {
  const MCAsmInfo *MCAI = TM->getMCAsmInfo();
  assert(MCAI && "No MCAsmInfo");
  switch (MCAI->getExceptionHandlingType()) {
  case ExceptionHandling::SjLj:
    // SjLj piggy-backs on dwarf for this bit. The cleanups done apply to both
    // Dwarf EH prepare needs to be run after SjLj prepare. Otherwise,
    // catch info can get misplaced when a selector ends up more than one block
    // removed from the parent invoke(s). This could happen when a landing
    // pad is shared by multiple invokes and is also a target of a normal
    // edge from elsewhere.
    addPass(createSjLjEHPreparePass());
    LLVM_FALLTHROUGH;
  case ExceptionHandling::DwarfCFI:
  case ExceptionHandling::ARM:
    addPass(createDwarfEHPass());
    break;
  case ExceptionHandling::WinEH:
    // We support using both GCC-style and MSVC-style exceptions on Windows, so
    // add both preparation passes. Each pass will only actually run if it
    // recognizes the personality function.
    addPass(createWinEHPass());
    addPass(createDwarfEHPass());
    break;
  case ExceptionHandling::Wasm:
    // Wasm EH uses Windows EH instructions, but it does not need to demote PHIs
    // on catchpads and cleanuppads because it does not outline them into
    // funclets. Catchswitch blocks are not lowered in SelectionDAG, so we
    // should remove PHIs there.
    addPass(createWinEHPass(/*DemoteCatchSwitchPHIOnly=*/false));
    addPass(createWasmEHPass());
    break;
  case ExceptionHandling::None:
    addPass(createLowerInvokePass());

    // The lower invoke pass may create unreachable code. Remove it.
    addPass(createUnreachableBlockEliminationPass());
    break;
  }
}

/// Add pass to prepare the LLVM IR for code generation. This should be done
/// before exception handling preparation passes.
void TargetPassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOpt::None && !DisableCGP)
    addPass(createCodeGenPreparePass());
  addPass(createRewriteSymbolsPass());
}

/// Add common passes that perform LLVM IR to IR transforms in preparation for
/// instruction selection.
void TargetPassConfig::addISelPrepare() {
  addPreISel();

  // Force codegen to run according to the callgraph.
  if (requiresCodeGenSCCOrder())
    addPass(new DummyCGSCCPass);

  // Add both the safe stack and the stack protection passes: each of them will
  // only protect functions that have corresponding attributes.
  addPass(createSafeStackPass());
  addPass(createStackProtectorPass());

  if (PrintISelInput)
    addPass(createPrintFunctionPass(
        dbgs(), "\n\n*** Final LLVM Code input to ISel ***\n"));

  // All passes which modify the LLVM IR are now complete; run the verifier
  // to ensure that the IR is valid.
  if (!DisableVerify)
    addPass(createVerifierPass());
}

bool TargetPassConfig::addCoreISelPasses() {
  // Enable FastISel with -fast-isel, but allow that to be overridden.
  TM->setO0WantsFastISel(EnableFastISelOption != cl::BOU_FALSE);

  // Determine an instruction selector.
  enum class SelectorType { SelectionDAG, FastISel, GlobalISel };
  SelectorType Selector;

  if (EnableFastISelOption == cl::BOU_TRUE)
    Selector = SelectorType::FastISel;
  else if (EnableGlobalISelOption == cl::BOU_TRUE ||
           (TM->Options.EnableGlobalISel &&
            EnableGlobalISelOption != cl::BOU_FALSE))
    Selector = SelectorType::GlobalISel;
  else if (TM->getOptLevel() == CodeGenOpt::None && TM->getO0WantsFastISel())
    Selector = SelectorType::FastISel;
  else
    Selector = SelectorType::SelectionDAG;

  // Set consistently TM->Options.EnableFastISel and EnableGlobalISel.
  if (Selector == SelectorType::FastISel) {
    TM->setFastISel(true);
    TM->setGlobalISel(false);
  } else if (Selector == SelectorType::GlobalISel) {
    TM->setFastISel(false);
    TM->setGlobalISel(true);
  }

  // Add instruction selector passes.
  if (Selector == SelectorType::GlobalISel) {
    SaveAndRestore<bool> SavedAddingMachinePasses(AddingMachinePasses, true);
    if (addIRTranslator())
      return true;

    addPreLegalizeMachineIR();

    if (addLegalizeMachineIR())
      return true;

    // Before running the register bank selector, ask the target if it
    // wants to run some passes.
    addPreRegBankSelect();

    if (addRegBankSelect())
      return true;

    addPreGlobalInstructionSelect();

    if (addGlobalInstructionSelect())
      return true;

    // Pass to reset the MachineFunction if the ISel failed.
    addPass(createResetMachineFunctionPass(
        reportDiagnosticWhenGlobalISelFallback(), isGlobalISelAbortEnabled()));

    // Provide a fallback path when we do not want to abort on
    // not-yet-supported input.
    if (!isGlobalISelAbortEnabled() && addInstSelector())
      return true;

  } else if (addInstSelector())
    return true;

  return false;
}

bool TargetPassConfig::addISelPasses() {
  if (TM->useEmulatedTLS())
    addPass(createLowerEmuTLSPass());

  addPass(createPreISelIntrinsicLoweringPass());
  addPass(createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));
  addIRPasses();
  addCodeGenPrepare();
  addPassesToHandleExceptions();
  addISelPrepare();

  return addCoreISelPasses();
}

/// -regalloc=... command line option.
static FunctionPass *useDefaultRegisterAllocator() { return nullptr; }
static cl::opt<RegisterRegAlloc::FunctionPassCtor, false,
               RegisterPassParser<RegisterRegAlloc>>
    RegAlloc("regalloc", cl::Hidden, cl::init(&useDefaultRegisterAllocator),
             cl::desc("Register allocator to use"));

/// Add the complete set of target-independent postISel code generator passes.
///
/// This can be read as the standard order of major LLVM CodeGen stages. Stages
/// with nontrivial configuration or multiple passes are broken out below in
/// add%Stage routines.
///
/// Any TargetPassConfig::addXX routine may be overriden by the Target. The
/// addPre/Post methods with empty header implementations allow injecting
/// target-specific fixups just before or after major stages. Additionally,
/// targets have the flexibility to change pass order within a stage by
/// overriding default implementation of add%Stage routines below. Each
/// technique has maintainability tradeoffs because alternate pass orders are
/// not well supported. addPre/Post works better if the target pass is easily
/// tied to a common pass. But if it has subtle dependencies on multiple passes,
/// the target should override the stage instead.
///
/// TODO: We could use a single addPre/Post(ID) hook to allow pass injection
/// before/after any target-independent pass. But it's currently overkill.
void TargetPassConfig::addMachinePasses() {
  AddingMachinePasses = true;

  // Insert a machine instr printer pass after the specified pass.
  StringRef PrintMachineInstrsPassName = PrintMachineInstrs.getValue();
  if (!PrintMachineInstrsPassName.equals("") &&
      !PrintMachineInstrsPassName.equals("option-unspecified")) {
    if (const PassInfo *TPI = getPassInfo(PrintMachineInstrsPassName)) {
      const PassRegistry *PR = PassRegistry::getPassRegistry();
      const PassInfo *IPI = PR->getPassInfo(StringRef("machineinstr-printer"));
      assert(IPI && "failed to get \"machineinstr-printer\" PassInfo!");
      const char *TID = (const char *)(TPI->getTypeInfo());
      const char *IID = (const char *)(IPI->getTypeInfo());
      insertPass(TID, IID);
    }
  }

  // Print the instruction selected machine code...
  printAndVerify("After Instruction Selection");

  // Expand pseudo-instructions emitted by ISel.
  addPass(&ExpandISelPseudosID);

  // Add passes that optimize machine instructions in SSA form.
  if (getOptLevel() != CodeGenOpt::None) {
    addMachineSSAOptimization();
  } else {
    // If the target requests it, assign local variables to stack slots relative
    // to one another and simplify frame index references where possible.
    addPass(&LocalStackSlotAllocationID, false);
  }

  if (TM->Options.EnableIPRA)
    addPass(createRegUsageInfoPropPass());

  // Run pre-ra passes.
  addPreRegAlloc();

  // Run register allocation and passes that are tightly coupled with it,
  // including phi elimination and scheduling.
  if (getOptimizeRegAlloc())
    addOptimizedRegAlloc(createRegAllocPass(true));
  else {
    if (RegAlloc != &useDefaultRegisterAllocator &&
        RegAlloc != &createFastRegisterAllocator)
      report_fatal_error("Must use fast (default) register allocator for unoptimized regalloc.");
    addFastRegAlloc(createRegAllocPass(false));
  }

  // Run post-ra passes.
  addPostRegAlloc();

  // Insert prolog/epilog code.  Eliminate abstract frame index references...
  if (getOptLevel() != CodeGenOpt::None) {
    addPass(&PostRAMachineSinkingID);
    addPass(&ShrinkWrapID);
  }

  // Prolog/Epilog inserter needs a TargetMachine to instantiate. But only
  // do so if it hasn't been disabled, substituted, or overridden.
  if (!isPassSubstitutedOrOverridden(&PrologEpilogCodeInserterID))
      addPass(createPrologEpilogInserterPass());

  /// Add passes that optimize machine instructions after register allocation.
  if (getOptLevel() != CodeGenOpt::None)
    addMachineLateOptimization();

  // Expand pseudo instructions before second scheduling pass.
  addPass(&ExpandPostRAPseudosID);

  // Run pre-sched2 passes.
  addPreSched2();

  if (EnableImplicitNullChecks)
    addPass(&ImplicitNullChecksID);

  // Second pass scheduler.
  // Let Target optionally insert this pass by itself at some other
  // point.
  if (getOptLevel() != CodeGenOpt::None &&
      !TM->targetSchedulesPostRAScheduling()) {
    if (MISchedPostRA)
      addPass(&PostMachineSchedulerID);
    else
      addPass(&PostRASchedulerID);
  }

  // GC
  if (addGCPasses()) {
    if (PrintGCInfo)
      addPass(createGCInfoPrinter(dbgs()), false, false);
  }

  // Basic block placement.
  if (getOptLevel() != CodeGenOpt::None)
    addBlockPlacement();

  addPreEmitPass();

  if (TM->Options.EnableIPRA)
    // Collect register usage information and produce a register mask of
    // clobbered registers, to be used to optimize call sites.
    addPass(createRegUsageInfoCollector());

  addPass(&FuncletLayoutID, false);

  addPass(&StackMapLivenessID, false);
  addPass(&LiveDebugValuesID, false);

  // Insert before XRay Instrumentation.
  addPass(&FEntryInserterID, false);

  addPass(&XRayInstrumentationID, false);
  addPass(&PatchableFunctionID, false);

  if (TM->Options.EnableMachineOutliner && getOptLevel() != CodeGenOpt::None &&
      EnableMachineOutliner != NeverOutline) {
    bool RunOnAllFunctions = (EnableMachineOutliner == AlwaysOutline);
    bool AddOutliner = RunOnAllFunctions ||
                       TM->Options.SupportsDefaultOutlining;
    if (AddOutliner)
      addPass(createMachineOutlinerPass(RunOnAllFunctions));
  }

  // Add passes that directly emit MI after all other MI passes.
  addPreEmitPass2();

  AddingMachinePasses = false;
}

/// Add passes that optimize machine instructions in SSA form.
void TargetPassConfig::addMachineSSAOptimization() {
  // Pre-ra tail duplication.
  addPass(&EarlyTailDuplicateID);

  // Optimize PHIs before DCE: removing dead PHI cycles may make more
  // instructions dead.
  addPass(&OptimizePHIsID, false);

  // This pass merges large allocas. StackSlotColoring is a different pass
  // which merges spill slots.
  addPass(&StackColoringID, false);

  // If the target requests it, assign local variables to stack slots relative
  // to one another and simplify frame index references where possible.
  addPass(&LocalStackSlotAllocationID, false);

  // With optimization, dead code should already be eliminated. However
  // there is one known exception: lowered code for arguments that are only
  // used by tail calls, where the tail calls reuse the incoming stack
  // arguments directly (see t11 in test/CodeGen/X86/sibcall.ll).
  addPass(&DeadMachineInstructionElimID);

  // Allow targets to insert passes that improve instruction level parallelism,
  // like if-conversion. Such passes will typically need dominator trees and
  // loop info, just like LICM and CSE below.
  addILPOpts();

  addPass(&EarlyMachineLICMID, false);
  addPass(&MachineCSEID, false);

  addPass(&MachineSinkingID);

  addPass(&PeepholeOptimizerID);
  // Clean-up the dead code that may have been generated by peephole
  // rewriting.
  addPass(&DeadMachineInstructionElimID);
}

//===---------------------------------------------------------------------===//
/// Register Allocation Pass Configuration
//===---------------------------------------------------------------------===//

bool TargetPassConfig::getOptimizeRegAlloc() const {
  switch (OptimizeRegAlloc) {
  case cl::BOU_UNSET: return getOptLevel() != CodeGenOpt::None;
  case cl::BOU_TRUE:  return true;
  case cl::BOU_FALSE: return false;
  }
  llvm_unreachable("Invalid optimize-regalloc state");
}

/// RegisterRegAlloc's global Registry tracks allocator registration.
MachinePassRegistry<RegisterRegAlloc::FunctionPassCtor>
    RegisterRegAlloc::Registry;

/// A dummy default pass factory indicates whether the register allocator is
/// overridden on the command line.
static llvm::once_flag InitializeDefaultRegisterAllocatorFlag;

static RegisterRegAlloc
defaultRegAlloc("default",
                "pick register allocator based on -O option",
                useDefaultRegisterAllocator);

static void initializeDefaultRegisterAllocatorOnce() {
  RegisterRegAlloc::FunctionPassCtor Ctor = RegisterRegAlloc::getDefault();

  if (!Ctor) {
    Ctor = RegAlloc;
    RegisterRegAlloc::setDefault(RegAlloc);
  }
}

/// Instantiate the default register allocator pass for this target for either
/// the optimized or unoptimized allocation path. This will be added to the pass
/// manager by addFastRegAlloc in the unoptimized case or addOptimizedRegAlloc
/// in the optimized case.
///
/// A target that uses the standard regalloc pass order for fast or optimized
/// allocation may still override this for per-target regalloc
/// selection. But -regalloc=... always takes precedence.
FunctionPass *TargetPassConfig::createTargetRegisterAllocator(bool Optimized) {
  if (Optimized)
    return createGreedyRegisterAllocator();
  else
    return createFastRegisterAllocator();
}

/// Find and instantiate the register allocation pass requested by this target
/// at the current optimization level.  Different register allocators are
/// defined as separate passes because they may require different analysis.
///
/// This helper ensures that the regalloc= option is always available,
/// even for targets that override the default allocator.
///
/// FIXME: When MachinePassRegistry register pass IDs instead of function ptrs,
/// this can be folded into addPass.
FunctionPass *TargetPassConfig::createRegAllocPass(bool Optimized) {
  // Initialize the global default.
  llvm::call_once(InitializeDefaultRegisterAllocatorFlag,
                  initializeDefaultRegisterAllocatorOnce);

  RegisterRegAlloc::FunctionPassCtor Ctor = RegisterRegAlloc::getDefault();
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  // With no -regalloc= override, ask the target for a regalloc pass.
  return createTargetRegisterAllocator(Optimized);
}

/// Return true if the default global register allocator is in use and
/// has not be overriden on the command line with '-regalloc=...'
bool TargetPassConfig::usingDefaultRegAlloc() const {
  return RegAlloc.getNumOccurrences() == 0;
}

/// Add the minimum set of target-independent passes that are required for
/// register allocation. No coalescing or scheduling.
void TargetPassConfig::addFastRegAlloc(FunctionPass *RegAllocPass) {
  addPass(&PHIEliminationID, false);
  addPass(&TwoAddressInstructionPassID, false);

  if (RegAllocPass)
    addPass(RegAllocPass);
}

/// Add standard target-independent passes that are tightly coupled with
/// optimized register allocation, including coalescing, machine instruction
/// scheduling, and register allocation itself.
void TargetPassConfig::addOptimizedRegAlloc(FunctionPass *RegAllocPass) {
  addPass(&DetectDeadLanesID, false);

  addPass(&ProcessImplicitDefsID, false);

  // LiveVariables currently requires pure SSA form.
  //
  // FIXME: Once TwoAddressInstruction pass no longer uses kill flags,
  // LiveVariables can be removed completely, and LiveIntervals can be directly
  // computed. (We still either need to regenerate kill flags after regalloc, or
  // preferably fix the scavenger to not depend on them).
  addPass(&LiveVariablesID, false);

  // Edge splitting is smarter with machine loop info.
  addPass(&MachineLoopInfoID, false);
  addPass(&PHIEliminationID, false);

  // Eventually, we want to run LiveIntervals before PHI elimination.
  if (EarlyLiveIntervals)
    addPass(&LiveIntervalsID, false);

  addPass(&TwoAddressInstructionPassID, false);
  addPass(&RegisterCoalescerID);

  // The machine scheduler may accidentally create disconnected components
  // when moving subregister definitions around, avoid this by splitting them to
  // separate vregs before. Splitting can also improve reg. allocation quality.
  addPass(&RenameIndependentSubregsID);

  // PreRA instruction scheduling.
  addPass(&MachineSchedulerID);

  if (RegAllocPass) {
    // Add the selected register allocation pass.
    addPass(RegAllocPass);

    // Allow targets to change the register assignments before rewriting.
    addPreRewrite();

    // Finally rewrite virtual registers.
    addPass(&VirtRegRewriterID);

    // Perform stack slot coloring and post-ra machine LICM.
    //
    // FIXME: Re-enable coloring with register when it's capable of adding
    // kill markers.
    addPass(&StackSlotColoringID);

    // Copy propagate to forward register uses and try to eliminate COPYs that
    // were not coalesced.
    addPass(&MachineCopyPropagationID);

    // Run post-ra machine LICM to hoist reloads / remats.
    //
    // FIXME: can this move into MachineLateOptimization?
    addPass(&MachineLICMID);
  }
}

//===---------------------------------------------------------------------===//
/// Post RegAlloc Pass Configuration
//===---------------------------------------------------------------------===//

/// Add passes that optimize machine instructions after register allocation.
void TargetPassConfig::addMachineLateOptimization() {
  // Branch folding must be run after regalloc and prolog/epilog insertion.
  addPass(&BranchFolderPassID);

  // Tail duplication.
  // Note that duplicating tail just increases code size and degrades
  // performance for targets that require Structured Control Flow.
  // In addition it can also make CFG irreducible. Thus we disable it.
  if (!TM->requiresStructuredCFG())
    addPass(&TailDuplicateID);

  // Copy propagation.
  addPass(&MachineCopyPropagationID);
}

/// Add standard GC passes.
bool TargetPassConfig::addGCPasses() {
  addPass(&GCMachineCodeAnalysisID, false);
  return true;
}

/// Add standard basic block placement passes.
void TargetPassConfig::addBlockPlacement() {
  if (addPass(&MachineBlockPlacementID)) {
    // Run a separate pass to collect block placement statistics.
    if (EnableBlockPlacementStats)
      addPass(&MachineBlockPlacementStatsID);
  }
}

//===---------------------------------------------------------------------===//
/// GlobalISel Configuration
//===---------------------------------------------------------------------===//
bool TargetPassConfig::isGlobalISelAbortEnabled() const {
  return TM->Options.GlobalISelAbort == GlobalISelAbortMode::Enable;
}

bool TargetPassConfig::reportDiagnosticWhenGlobalISelFallback() const {
  return TM->Options.GlobalISelAbort == GlobalISelAbortMode::DisableWithDiag;
}
