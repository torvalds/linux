//===-- HexagonTargetMachine.cpp - Define TargetMachine for Hexagon -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Hexagon target spec.
//
//===----------------------------------------------------------------------===//

#include "HexagonTargetMachine.h"
#include "Hexagon.h"
#include "HexagonISelLowering.h"
#include "HexagonMachineScheduler.h"
#include "HexagonTargetObjectFile.h"
#include "HexagonTargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar.h"

using namespace llvm;

static cl::opt<bool> EnableCExtOpt("hexagon-cext", cl::Hidden, cl::ZeroOrMore,
  cl::init(true), cl::desc("Enable Hexagon constant-extender optimization"));

static cl::opt<bool> EnableRDFOpt("rdf-opt", cl::Hidden, cl::ZeroOrMore,
  cl::init(true), cl::desc("Enable RDF-based optimizations"));

static cl::opt<bool> DisableHardwareLoops("disable-hexagon-hwloops",
  cl::Hidden, cl::desc("Disable Hardware Loops for Hexagon target"));

static cl::opt<bool> DisableAModeOpt("disable-hexagon-amodeopt",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Disable Hexagon Addressing Mode Optimization"));

static cl::opt<bool> DisableHexagonCFGOpt("disable-hexagon-cfgopt",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Disable Hexagon CFG Optimization"));

static cl::opt<bool> DisableHCP("disable-hcp", cl::init(false), cl::Hidden,
  cl::ZeroOrMore, cl::desc("Disable Hexagon constant propagation"));

static cl::opt<bool> DisableStoreWidening("disable-store-widen",
  cl::Hidden, cl::init(false), cl::desc("Disable store widening"));

static cl::opt<bool> EnableExpandCondsets("hexagon-expand-condsets",
  cl::init(true), cl::Hidden, cl::ZeroOrMore,
  cl::desc("Early expansion of MUX"));

static cl::opt<bool> EnableEarlyIf("hexagon-eif", cl::init(true), cl::Hidden,
  cl::ZeroOrMore, cl::desc("Enable early if-conversion"));

static cl::opt<bool> EnableGenInsert("hexagon-insert", cl::init(true),
  cl::Hidden, cl::desc("Generate \"insert\" instructions"));

static cl::opt<bool> EnableCommGEP("hexagon-commgep", cl::init(true),
  cl::Hidden, cl::ZeroOrMore, cl::desc("Enable commoning of GEP instructions"));

static cl::opt<bool> EnableGenExtract("hexagon-extract", cl::init(true),
  cl::Hidden, cl::desc("Generate \"extract\" instructions"));

static cl::opt<bool> EnableGenMux("hexagon-mux", cl::init(true), cl::Hidden,
  cl::desc("Enable converting conditional transfers into MUX instructions"));

static cl::opt<bool> EnableGenPred("hexagon-gen-pred", cl::init(true),
  cl::Hidden, cl::desc("Enable conversion of arithmetic operations to "
  "predicate instructions"));

static cl::opt<bool> EnableLoopPrefetch("hexagon-loop-prefetch",
  cl::init(false), cl::Hidden, cl::ZeroOrMore,
  cl::desc("Enable loop data prefetch on Hexagon"));

static cl::opt<bool> DisableHSDR("disable-hsdr", cl::init(false), cl::Hidden,
  cl::desc("Disable splitting double registers"));

static cl::opt<bool> EnableBitSimplify("hexagon-bit", cl::init(true),
  cl::Hidden, cl::desc("Bit simplification"));

static cl::opt<bool> EnableLoopResched("hexagon-loop-resched", cl::init(true),
  cl::Hidden, cl::desc("Loop rescheduling"));

static cl::opt<bool> HexagonNoOpt("hexagon-noopt", cl::init(false),
  cl::Hidden, cl::desc("Disable backend optimizations"));

static cl::opt<bool> EnableVectorPrint("enable-hexagon-vector-print",
  cl::Hidden, cl::ZeroOrMore, cl::init(false),
  cl::desc("Enable Hexagon Vector print instr pass"));

static cl::opt<bool> EnableVExtractOpt("hexagon-opt-vextract", cl::Hidden,
  cl::ZeroOrMore, cl::init(true), cl::desc("Enable vextract optimization"));

static cl::opt<bool> EnableInitialCFGCleanup("hexagon-initial-cfg-cleanup",
  cl::Hidden, cl::ZeroOrMore, cl::init(true),
  cl::desc("Simplify the CFG after atomic expansion pass"));

/// HexagonTargetMachineModule - Note that this is used on hosts that
/// cannot link in a library unless there are references into the
/// library.  In particular, it seems that it is not possible to get
/// things to work on Win32 without this.  Though it is unused, do not
/// remove it.
extern "C" int HexagonTargetMachineModule;
int HexagonTargetMachineModule = 0;

static ScheduleDAGInstrs *createVLIWMachineSched(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG =
    new VLIWMachineScheduler(C, make_unique<ConvergingVLIWScheduler>());
  DAG->addMutation(make_unique<HexagonSubtarget::UsrOverflowMutation>());
  DAG->addMutation(make_unique<HexagonSubtarget::HVXMemLatencyMutation>());
  DAG->addMutation(make_unique<HexagonSubtarget::CallMutation>());
  DAG->addMutation(createCopyConstrainDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

static MachineSchedRegistry
SchedCustomRegistry("hexagon", "Run Hexagon's custom scheduler",
                    createVLIWMachineSched);

namespace llvm {
  extern char &HexagonExpandCondsetsID;
  void initializeHexagonBitSimplifyPass(PassRegistry&);
  void initializeHexagonConstExtendersPass(PassRegistry&);
  void initializeHexagonConstPropagationPass(PassRegistry&);
  void initializeHexagonEarlyIfConversionPass(PassRegistry&);
  void initializeHexagonExpandCondsetsPass(PassRegistry&);
  void initializeHexagonGenMuxPass(PassRegistry&);
  void initializeHexagonHardwareLoopsPass(PassRegistry&);
  void initializeHexagonLoopIdiomRecognizePass(PassRegistry&);
  void initializeHexagonVectorLoopCarriedReusePass(PassRegistry&);
  void initializeHexagonNewValueJumpPass(PassRegistry&);
  void initializeHexagonOptAddrModePass(PassRegistry&);
  void initializeHexagonPacketizerPass(PassRegistry&);
  void initializeHexagonRDFOptPass(PassRegistry&);
  void initializeHexagonSplitDoubleRegsPass(PassRegistry&);
  void initializeHexagonVExtractPass(PassRegistry&);
  Pass *createHexagonLoopIdiomPass();
  Pass *createHexagonVectorLoopCarriedReusePass();

  FunctionPass *createHexagonBitSimplify();
  FunctionPass *createHexagonBranchRelaxation();
  FunctionPass *createHexagonCallFrameInformation();
  FunctionPass *createHexagonCFGOptimizer();
  FunctionPass *createHexagonCommonGEP();
  FunctionPass *createHexagonConstExtenders();
  FunctionPass *createHexagonConstPropagationPass();
  FunctionPass *createHexagonCopyToCombine();
  FunctionPass *createHexagonEarlyIfConversion();
  FunctionPass *createHexagonFixupHwLoops();
  FunctionPass *createHexagonGenExtract();
  FunctionPass *createHexagonGenInsert();
  FunctionPass *createHexagonGenMux();
  FunctionPass *createHexagonGenPredicate();
  FunctionPass *createHexagonHardwareLoops();
  FunctionPass *createHexagonISelDag(HexagonTargetMachine &TM,
                                     CodeGenOpt::Level OptLevel);
  FunctionPass *createHexagonLoopRescheduling();
  FunctionPass *createHexagonNewValueJump();
  FunctionPass *createHexagonOptimizeSZextends();
  FunctionPass *createHexagonOptAddrMode();
  FunctionPass *createHexagonPacketizer(bool Minimal);
  FunctionPass *createHexagonPeephole();
  FunctionPass *createHexagonRDFOpt();
  FunctionPass *createHexagonSplitConst32AndConst64();
  FunctionPass *createHexagonSplitDoubleRegs();
  FunctionPass *createHexagonStoreWidening();
  FunctionPass *createHexagonVectorPrint();
  FunctionPass *createHexagonVExtract();
} // end namespace llvm;

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    return Reloc::Static;
  return *RM;
}

extern "C" void LLVMInitializeHexagonTarget() {
  // Register the target.
  RegisterTargetMachine<HexagonTargetMachine> X(getTheHexagonTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeHexagonBitSimplifyPass(PR);
  initializeHexagonConstExtendersPass(PR);
  initializeHexagonConstPropagationPass(PR);
  initializeHexagonEarlyIfConversionPass(PR);
  initializeHexagonGenMuxPass(PR);
  initializeHexagonHardwareLoopsPass(PR);
  initializeHexagonLoopIdiomRecognizePass(PR);
  initializeHexagonVectorLoopCarriedReusePass(PR);
  initializeHexagonNewValueJumpPass(PR);
  initializeHexagonOptAddrModePass(PR);
  initializeHexagonPacketizerPass(PR);
  initializeHexagonRDFOptPass(PR);
  initializeHexagonSplitDoubleRegsPass(PR);
  initializeHexagonVExtractPass(PR);
}

HexagonTargetMachine::HexagonTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           Optional<Reloc::Model> RM,
                                           Optional<CodeModel::Model> CM,
                                           CodeGenOpt::Level OL, bool JIT)
    // Specify the vector alignment explicitly. For v512x1, the calculated
    // alignment would be 512*alignment(i1), which is 512 bytes, instead of
    // the required minimum of 64 bytes.
    : LLVMTargetMachine(
          T,
          "e-m:e-p:32:32:32-a:0-n16:32-"
          "i64:64:64-i32:32:32-i16:16:16-i1:8:8-f32:32:32-f64:64:64-"
          "v32:32:32-v64:64:64-v512:512:512-v1024:1024:1024-v2048:2048:2048",
          TT, CPU, FS, Options, getEffectiveRelocModel(RM),
          getEffectiveCodeModel(CM, CodeModel::Small),
          (HexagonNoOpt ? CodeGenOpt::None : OL)),
      TLOF(make_unique<HexagonTargetObjectFile>()) {
  initializeHexagonExpandCondsetsPass(*PassRegistry::getPassRegistry());
  initAsmInfo();
}

const HexagonSubtarget *
HexagonTargetMachine::getSubtargetImpl(const Function &F) const {
  AttributeList FnAttrs = F.getAttributes();
  Attribute CPUAttr =
      FnAttrs.getAttribute(AttributeList::FunctionIndex, "target-cpu");
  Attribute FSAttr =
      FnAttrs.getAttribute(AttributeList::FunctionIndex, "target-features");

  std::string CPU = !CPUAttr.hasAttribute(Attribute::None)
                        ? CPUAttr.getValueAsString().str()
                        : TargetCPU;
  std::string FS = !FSAttr.hasAttribute(Attribute::None)
                       ? FSAttr.getValueAsString().str()
                       : TargetFS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = llvm::make_unique<HexagonSubtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

void HexagonTargetMachine::adjustPassManager(PassManagerBuilder &PMB) {
  PMB.addExtension(
    PassManagerBuilder::EP_LateLoopOptimizations,
    [&](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
      PM.add(createHexagonLoopIdiomPass());
    });
  PMB.addExtension(
    PassManagerBuilder::EP_LoopOptimizerEnd,
    [&](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
      PM.add(createHexagonVectorLoopCarriedReusePass());
    });
}

TargetTransformInfo
HexagonTargetMachine::getTargetTransformInfo(const Function &F) {
  return TargetTransformInfo(HexagonTTIImpl(this, F));
}


HexagonTargetMachine::~HexagonTargetMachine() {}

namespace {
/// Hexagon Code Generator Pass Configuration Options.
class HexagonPassConfig : public TargetPassConfig {
public:
  HexagonPassConfig(HexagonTargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  HexagonTargetMachine &getHexagonTargetMachine() const {
    return getTM<HexagonTargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    return createVLIWMachineSched(C);
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *HexagonTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new HexagonPassConfig(*this, PM);
}

void HexagonPassConfig::addIRPasses() {
  TargetPassConfig::addIRPasses();
  bool NoOpt = (getOptLevel() == CodeGenOpt::None);

  if (!NoOpt) {
    addPass(createConstantPropagationPass());
    addPass(createDeadCodeEliminationPass());
  }

  addPass(createAtomicExpandPass());

  if (!NoOpt) {
    if (EnableInitialCFGCleanup)
      addPass(createCFGSimplificationPass(1, true, true, false, true));
    if (EnableLoopPrefetch)
      addPass(createLoopDataPrefetchPass());
    if (EnableCommGEP)
      addPass(createHexagonCommonGEP());
    // Replace certain combinations of shifts and ands with extracts.
    if (EnableGenExtract)
      addPass(createHexagonGenExtract());
  }
}

bool HexagonPassConfig::addInstSelector() {
  HexagonTargetMachine &TM = getHexagonTargetMachine();
  bool NoOpt = (getOptLevel() == CodeGenOpt::None);

  if (!NoOpt)
    addPass(createHexagonOptimizeSZextends());

  addPass(createHexagonISelDag(TM, getOptLevel()));

  if (!NoOpt) {
    if (EnableVExtractOpt)
      addPass(createHexagonVExtract());
    // Create logical operations on predicate registers.
    if (EnableGenPred)
      addPass(createHexagonGenPredicate());
    // Rotate loops to expose bit-simplification opportunities.
    if (EnableLoopResched)
      addPass(createHexagonLoopRescheduling());
    // Split double registers.
    if (!DisableHSDR)
      addPass(createHexagonSplitDoubleRegs());
    // Bit simplification.
    if (EnableBitSimplify)
      addPass(createHexagonBitSimplify());
    addPass(createHexagonPeephole());
    // Constant propagation.
    if (!DisableHCP) {
      addPass(createHexagonConstPropagationPass());
      addPass(&UnreachableMachineBlockElimID);
    }
    if (EnableGenInsert)
      addPass(createHexagonGenInsert());
    if (EnableEarlyIf)
      addPass(createHexagonEarlyIfConversion());
  }

  return false;
}

void HexagonPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOpt::None) {
    if (EnableCExtOpt)
      addPass(createHexagonConstExtenders());
    if (EnableExpandCondsets)
      insertPass(&RegisterCoalescerID, &HexagonExpandCondsetsID);
    if (!DisableStoreWidening)
      addPass(createHexagonStoreWidening());
    if (!DisableHardwareLoops)
      addPass(createHexagonHardwareLoops());
  }
  if (TM->getOptLevel() >= CodeGenOpt::Default)
    addPass(&MachinePipelinerID);
}

void HexagonPassConfig::addPostRegAlloc() {
  if (getOptLevel() != CodeGenOpt::None) {
    if (EnableRDFOpt)
      addPass(createHexagonRDFOpt());
    if (!DisableHexagonCFGOpt)
      addPass(createHexagonCFGOptimizer());
    if (!DisableAModeOpt)
      addPass(createHexagonOptAddrMode());
  }
}

void HexagonPassConfig::addPreSched2() {
  addPass(createHexagonCopyToCombine());
  if (getOptLevel() != CodeGenOpt::None)
    addPass(&IfConverterID);
  addPass(createHexagonSplitConst32AndConst64());
}

void HexagonPassConfig::addPreEmitPass() {
  bool NoOpt = (getOptLevel() == CodeGenOpt::None);

  if (!NoOpt)
    addPass(createHexagonNewValueJump());

  addPass(createHexagonBranchRelaxation());

  if (!NoOpt) {
    if (!DisableHardwareLoops)
      addPass(createHexagonFixupHwLoops());
    // Generate MUX from pairs of conditional transfers.
    if (EnableGenMux)
      addPass(createHexagonGenMux());
  }

  // Packetization is mandatory: it handles gather/scatter at all opt levels.
  addPass(createHexagonPacketizer(NoOpt), false);

  if (EnableVectorPrint)
    addPass(createHexagonVectorPrint(), false);

  // Add CFI instructions if necessary.
  addPass(createHexagonCallFrameInformation(), false);
}
