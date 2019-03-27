//===-- ARMTargetMachine.cpp - Define TargetMachine for ARM ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "ARMTargetMachine.h"
#include "ARM.h"
#include "ARMMacroFusion.h"
#include "ARMSubtarget.h"
#include "ARMTargetObjectFile.h"
#include "ARMTargetTransformInfo.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/ExecutionDomainFix.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar.h"
#include <cassert>
#include <memory>
#include <string>

using namespace llvm;

static cl::opt<bool>
DisableA15SDOptimization("disable-a15-sd-optimization", cl::Hidden,
                   cl::desc("Inhibit optimization of S->D register accesses on A15"),
                   cl::init(false));

static cl::opt<bool>
EnableAtomicTidy("arm-atomic-cfg-tidy", cl::Hidden,
                 cl::desc("Run SimplifyCFG after expanding atomic operations"
                          " to make use of cmpxchg flow-based information"),
                 cl::init(true));

static cl::opt<bool>
EnableARMLoadStoreOpt("arm-load-store-opt", cl::Hidden,
                      cl::desc("Enable ARM load/store optimization pass"),
                      cl::init(true));

// FIXME: Unify control over GlobalMerge.
static cl::opt<cl::boolOrDefault>
EnableGlobalMerge("arm-global-merge", cl::Hidden,
                  cl::desc("Enable the global merge pass"));

namespace llvm {
  void initializeARMExecutionDomainFixPass(PassRegistry&);
}

extern "C" void LLVMInitializeARMTarget() {
  // Register the target.
  RegisterTargetMachine<ARMLETargetMachine> X(getTheARMLETarget());
  RegisterTargetMachine<ARMLETargetMachine> A(getTheThumbLETarget());
  RegisterTargetMachine<ARMBETargetMachine> Y(getTheARMBETarget());
  RegisterTargetMachine<ARMBETargetMachine> B(getTheThumbBETarget());

  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeGlobalISel(Registry);
  initializeARMLoadStoreOptPass(Registry);
  initializeARMPreAllocLoadStoreOptPass(Registry);
  initializeARMParallelDSPPass(Registry);
  initializeARMCodeGenPreparePass(Registry);
  initializeARMConstantIslandsPass(Registry);
  initializeARMExecutionDomainFixPass(Registry);
  initializeARMExpandPseudoPass(Registry);
  initializeThumb2SizeReducePass(Registry);
}

static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  if (TT.isOSBinFormatMachO())
    return llvm::make_unique<TargetLoweringObjectFileMachO>();
  if (TT.isOSWindows())
    return llvm::make_unique<TargetLoweringObjectFileCOFF>();
  return llvm::make_unique<ARMElfTargetObjectFile>();
}

static ARMBaseTargetMachine::ARMABI
computeTargetABI(const Triple &TT, StringRef CPU,
                 const TargetOptions &Options) {
  StringRef ABIName = Options.MCOptions.getABIName();

  if (ABIName.empty())
    ABIName = ARM::computeDefaultTargetABI(TT, CPU);

  if (ABIName == "aapcs16")
    return ARMBaseTargetMachine::ARM_ABI_AAPCS16;
  else if (ABIName.startswith("aapcs"))
    return ARMBaseTargetMachine::ARM_ABI_AAPCS;
  else if (ABIName.startswith("apcs"))
    return ARMBaseTargetMachine::ARM_ABI_APCS;

  llvm_unreachable("Unhandled/unknown ABI Name!");
  return ARMBaseTargetMachine::ARM_ABI_UNKNOWN;
}

static std::string computeDataLayout(const Triple &TT, StringRef CPU,
                                     const TargetOptions &Options,
                                     bool isLittle) {
  auto ABI = computeTargetABI(TT, CPU, Options);
  std::string Ret;

  if (isLittle)
    // Little endian.
    Ret += "e";
  else
    // Big endian.
    Ret += "E";

  Ret += DataLayout::getManglingComponent(TT);

  // Pointers are 32 bits and aligned to 32 bits.
  Ret += "-p:32:32";

  // ABIs other than APCS have 64 bit integers with natural alignment.
  if (ABI != ARMBaseTargetMachine::ARM_ABI_APCS)
    Ret += "-i64:64";

  // We have 64 bits floats. The APCS ABI requires them to be aligned to 32
  // bits, others to 64 bits. We always try to align to 64 bits.
  if (ABI == ARMBaseTargetMachine::ARM_ABI_APCS)
    Ret += "-f64:32:64";

  // We have 128 and 64 bit vectors. The APCS ABI aligns them to 32 bits, others
  // to 64. We always ty to give them natural alignment.
  if (ABI == ARMBaseTargetMachine::ARM_ABI_APCS)
    Ret += "-v64:32:64-v128:32:128";
  else if (ABI != ARMBaseTargetMachine::ARM_ABI_AAPCS16)
    Ret += "-v128:64:128";

  // Try to align aggregates to 32 bits (the default is 64 bits, which has no
  // particular hardware support on 32-bit ARM).
  Ret += "-a:0:32";

  // Integer registers are 32 bits.
  Ret += "-n32";

  // The stack is 128 bit aligned on NaCl, 64 bit aligned on AAPCS and 32 bit
  // aligned everywhere else.
  if (TT.isOSNaCl() || ABI == ARMBaseTargetMachine::ARM_ABI_AAPCS16)
    Ret += "-S128";
  else if (ABI == ARMBaseTargetMachine::ARM_ABI_AAPCS)
    Ret += "-S64";
  else
    Ret += "-S32";

  return Ret;
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    // Default relocation model on Darwin is PIC.
    return TT.isOSBinFormatMachO() ? Reloc::PIC_ : Reloc::Static;

  if (*RM == Reloc::ROPI || *RM == Reloc::RWPI || *RM == Reloc::ROPI_RWPI)
    assert(TT.isOSBinFormatELF() &&
           "ROPI/RWPI currently only supported for ELF");

  // DynamicNoPIC is only used on darwin.
  if (*RM == Reloc::DynamicNoPIC && !TT.isOSDarwin())
    return Reloc::Static;

  return *RM;
}

/// Create an ARM architecture model.
///
ARMBaseTargetMachine::ARMBaseTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           Optional<Reloc::Model> RM,
                                           Optional<CodeModel::Model> CM,
                                           CodeGenOpt::Level OL, bool isLittle)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options, isLittle), TT,
                        CPU, FS, Options, getEffectiveRelocModel(TT, RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TargetABI(computeTargetABI(TT, CPU, Options)),
      TLOF(createTLOF(getTargetTriple())), isLittle(isLittle) {

  // Default to triple-appropriate float ABI
  if (Options.FloatABIType == FloatABI::Default) {
    if (isTargetHardFloat())
      this->Options.FloatABIType = FloatABI::Hard;
    else
      this->Options.FloatABIType = FloatABI::Soft;
  }

  // Default to triple-appropriate EABI
  if (Options.EABIVersion == EABI::Default ||
      Options.EABIVersion == EABI::Unknown) {
    // musl is compatible with glibc with regard to EABI version
    if ((TargetTriple.getEnvironment() == Triple::GNUEABI ||
         TargetTriple.getEnvironment() == Triple::GNUEABIHF ||
         TargetTriple.getEnvironment() == Triple::MuslEABI ||
         TargetTriple.getEnvironment() == Triple::MuslEABIHF) &&
        !(TargetTriple.isOSWindows() || TargetTriple.isOSDarwin()))
      this->Options.EABIVersion = EABI::GNU;
    else
      this->Options.EABIVersion = EABI::EABI5;
  }

  if (TT.isOSBinFormatMachO()) {
    this->Options.TrapUnreachable = true;
    this->Options.NoTrapAfterNoreturn = true;
  }

  initAsmInfo();
}

ARMBaseTargetMachine::~ARMBaseTargetMachine() = default;

const ARMSubtarget *
ARMBaseTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU = !CPUAttr.hasAttribute(Attribute::None)
                        ? CPUAttr.getValueAsString().str()
                        : TargetCPU;
  std::string FS = !FSAttr.hasAttribute(Attribute::None)
                       ? FSAttr.getValueAsString().str()
                       : TargetFS;

  // FIXME: This is related to the code below to reset the target options,
  // we need to know whether or not the soft float flag is set on the
  // function before we can generate a subtarget. We also need to use
  // it as a key for the subtarget since that can be the only difference
  // between two functions.
  bool SoftFloat =
      F.getFnAttribute("use-soft-float").getValueAsString() == "true";
  // If the soft float attribute is set on the function turn on the soft float
  // subtarget feature.
  if (SoftFloat)
    FS += FS.empty() ? "+soft-float" : ",+soft-float";

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = llvm::make_unique<ARMSubtarget>(TargetTriple, CPU, FS, *this, isLittle);

    if (!I->isThumb() && !I->hasARMOps())
      F.getContext().emitError("Function '" + F.getName() + "' uses ARM "
          "instructions, but the target does not support ARM mode execution.");
  }

  return I.get();
}

TargetTransformInfo
ARMBaseTargetMachine::getTargetTransformInfo(const Function &F) {
  return TargetTransformInfo(ARMTTIImpl(this, F));
}

ARMLETargetMachine::ARMLETargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       Optional<Reloc::Model> RM,
                                       Optional<CodeModel::Model> CM,
                                       CodeGenOpt::Level OL, bool JIT)
    : ARMBaseTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, true) {}

ARMBETargetMachine::ARMBETargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       Optional<Reloc::Model> RM,
                                       Optional<CodeModel::Model> CM,
                                       CodeGenOpt::Level OL, bool JIT)
    : ARMBaseTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, false) {}

namespace {

/// ARM Code Generator Pass Configuration Options.
class ARMPassConfig : public TargetPassConfig {
public:
  ARMPassConfig(ARMBaseTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    if (TM.getOptLevel() != CodeGenOpt::None) {
      ARMGenSubtargetInfo STI(TM.getTargetTriple(), TM.getTargetCPU(),
                              TM.getTargetFeatureString());
      if (STI.hasFeature(ARM::FeatureUseMISched))
        substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
    }
  }

  ARMBaseTargetMachine &getARMTargetMachine() const {
    return getTM<ARMBaseTargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    ScheduleDAGMILive *DAG = createGenericSchedLive(C);
    // add DAG Mutations here.
    const ARMSubtarget &ST = C->MF->getSubtarget<ARMSubtarget>();
    if (ST.hasFusion())
      DAG->addMutation(createARMMacroFusionDAGMutation());
    return DAG;
  }

  ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const override {
    ScheduleDAGMI *DAG = createGenericSchedPostRA(C);
    // add DAG Mutations here.
    const ARMSubtarget &ST = C->MF->getSubtarget<ARMSubtarget>();
    if (ST.hasFusion())
      DAG->addMutation(createARMMacroFusionDAGMutation());
    return DAG;
  }

  void addIRPasses() override;
  void addCodeGenPrepare() override;
  bool addPreISel() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  bool addLegalizeMachineIR() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};

class ARMExecutionDomainFix : public ExecutionDomainFix {
public:
  static char ID;
  ARMExecutionDomainFix() : ExecutionDomainFix(ID, ARM::DPRRegClass) {}
  StringRef getPassName() const override {
    return "ARM Execution Domain Fix";
  }
};
char ARMExecutionDomainFix::ID;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(ARMExecutionDomainFix, "arm-execution-domain-fix",
  "ARM Execution Domain Fix", false, false)
INITIALIZE_PASS_DEPENDENCY(ReachingDefAnalysis)
INITIALIZE_PASS_END(ARMExecutionDomainFix, "arm-execution-domain-fix",
  "ARM Execution Domain Fix", false, false)

TargetPassConfig *ARMBaseTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new ARMPassConfig(*this, PM);
}

void ARMPassConfig::addIRPasses() {
  if (TM->Options.ThreadModel == ThreadModel::Single)
    addPass(createLowerAtomicPass());
  else
    addPass(createAtomicExpandPass());

  // Cmpxchg instructions are often used with a subsequent comparison to
  // determine whether it succeeded. We can exploit existing control-flow in
  // ldrex/strex loops to simplify this, but it needs tidying up.
  if (TM->getOptLevel() != CodeGenOpt::None && EnableAtomicTidy)
    addPass(createCFGSimplificationPass(
        1, false, false, true, true, [this](const Function &F) {
          const auto &ST = this->TM->getSubtarget<ARMSubtarget>(F);
          return ST.hasAnyDataBarrier() && !ST.isThumb1Only();
        }));

  TargetPassConfig::addIRPasses();

  // Match interleaved memory accesses to ldN/stN intrinsics.
  if (TM->getOptLevel() != CodeGenOpt::None)
    addPass(createInterleavedAccessPass());
}

void ARMPassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOpt::None)
    addPass(createARMCodeGenPreparePass());
  TargetPassConfig::addCodeGenPrepare();
}

bool ARMPassConfig::addPreISel() {
  if (getOptLevel() != CodeGenOpt::None)
    addPass(createARMParallelDSPPass());

  if ((TM->getOptLevel() != CodeGenOpt::None &&
       EnableGlobalMerge == cl::BOU_UNSET) ||
      EnableGlobalMerge == cl::BOU_TRUE) {
    // FIXME: This is using the thumb1 only constant value for
    // maximal global offset for merging globals. We may want
    // to look into using the old value for non-thumb1 code of
    // 4095 based on the TargetMachine, but this starts to become
    // tricky when doing code gen per function.
    bool OnlyOptimizeForSize = (TM->getOptLevel() < CodeGenOpt::Aggressive) &&
                               (EnableGlobalMerge == cl::BOU_UNSET);
    // Merging of extern globals is enabled by default on non-Mach-O as we
    // expect it to be generally either beneficial or harmless. On Mach-O it
    // is disabled as we emit the .subsections_via_symbols directive which
    // means that merging extern globals is not safe.
    bool MergeExternalByDefault = !TM->getTargetTriple().isOSBinFormatMachO();
    addPass(createGlobalMergePass(TM, 127, OnlyOptimizeForSize,
                                  MergeExternalByDefault));
  }

  return false;
}

bool ARMPassConfig::addInstSelector() {
  addPass(createARMISelDag(getARMTargetMachine(), getOptLevel()));
  return false;
}

bool ARMPassConfig::addIRTranslator() {
  addPass(new IRTranslator());
  return false;
}

bool ARMPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

bool ARMPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool ARMPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect());
  return false;
}

void ARMPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOpt::None) {
    addPass(createMLxExpansionPass());

    if (EnableARMLoadStoreOpt)
      addPass(createARMLoadStoreOptimizationPass(/* pre-register alloc */ true));

    if (!DisableA15SDOptimization)
      addPass(createA15SDOptimizerPass());
  }
}

void ARMPassConfig::addPreSched2() {
  if (getOptLevel() != CodeGenOpt::None) {
    if (EnableARMLoadStoreOpt)
      addPass(createARMLoadStoreOptimizationPass());

    addPass(new ARMExecutionDomainFix());
    addPass(createBreakFalseDeps());
  }

  // Expand some pseudo instructions into multiple instructions to allow
  // proper scheduling.
  addPass(createARMExpandPseudoPass());

  if (getOptLevel() != CodeGenOpt::None) {
    // in v8, IfConversion depends on Thumb instruction widths
    addPass(createThumb2SizeReductionPass([this](const Function &F) {
      return this->TM->getSubtarget<ARMSubtarget>(F).restrictIT();
    }));

    addPass(createIfConverter([](const MachineFunction &MF) {
      return !MF.getSubtarget<ARMSubtarget>().isThumb1Only();
    }));
  }
  addPass(createThumb2ITBlockPass());
}

void ARMPassConfig::addPreEmitPass() {
  addPass(createThumb2SizeReductionPass());

  // Constant island pass work on unbundled instructions.
  addPass(createUnpackMachineBundles([](const MachineFunction &MF) {
    return MF.getSubtarget<ARMSubtarget>().isThumb2();
  }));

  // Don't optimize barriers at -O0.
  if (getOptLevel() != CodeGenOpt::None)
    addPass(createARMOptimizeBarriersPass());

  addPass(createARMConstantIslandPass());
}
