//===- WebAssemblyTargetMachine.cpp - Define TargetMachine for WebAssembly -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the WebAssembly-specific subclass of TargetMachine.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetMachine.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyTargetObjectFile.h"
#include "WebAssemblyTargetTransformInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
using namespace llvm;

#define DEBUG_TYPE "wasm"

// Emscripten's asm.js-style exception handling
static cl::opt<bool> EnableEmException(
    "enable-emscripten-cxx-exceptions",
    cl::desc("WebAssembly Emscripten-style exception handling"),
    cl::init(false));

// Emscripten's asm.js-style setjmp/longjmp handling
static cl::opt<bool> EnableEmSjLj(
    "enable-emscripten-sjlj",
    cl::desc("WebAssembly Emscripten-style setjmp/longjmp handling"),
    cl::init(false));

extern "C" void LLVMInitializeWebAssemblyTarget() {
  // Register the target.
  RegisterTargetMachine<WebAssemblyTargetMachine> X(
      getTheWebAssemblyTarget32());
  RegisterTargetMachine<WebAssemblyTargetMachine> Y(
      getTheWebAssemblyTarget64());

  // Register backend passes
  auto &PR = *PassRegistry::getPassRegistry();
  initializeWebAssemblyAddMissingPrototypesPass(PR);
  initializeWebAssemblyLowerEmscriptenEHSjLjPass(PR);
  initializeLowerGlobalDtorsPass(PR);
  initializeFixFunctionBitcastsPass(PR);
  initializeOptimizeReturnedPass(PR);
  initializeWebAssemblyArgumentMovePass(PR);
  initializeWebAssemblySetP2AlignOperandsPass(PR);
  initializeWebAssemblyEHRestoreStackPointerPass(PR);
  initializeWebAssemblyReplacePhysRegsPass(PR);
  initializeWebAssemblyPrepareForLiveIntervalsPass(PR);
  initializeWebAssemblyOptimizeLiveIntervalsPass(PR);
  initializeWebAssemblyMemIntrinsicResultsPass(PR);
  initializeWebAssemblyRegStackifyPass(PR);
  initializeWebAssemblyRegColoringPass(PR);
  initializeWebAssemblyExplicitLocalsPass(PR);
  initializeWebAssemblyFixIrreducibleControlFlowPass(PR);
  initializeWebAssemblyLateEHPreparePass(PR);
  initializeWebAssemblyExceptionInfoPass(PR);
  initializeWebAssemblyCFGSortPass(PR);
  initializeWebAssemblyCFGStackifyPass(PR);
  initializeWebAssemblyLowerBrUnlessPass(PR);
  initializeWebAssemblyRegNumberingPass(PR);
  initializeWebAssemblyPeepholePass(PR);
  initializeWebAssemblyCallIndirectFixupPass(PR);
}

//===----------------------------------------------------------------------===//
// WebAssembly Lowering public interface.
//===----------------------------------------------------------------------===//

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (!RM.hasValue()) {
    // Default to static relocation model.  This should always be more optimial
    // than PIC since the static linker can determine all global addresses and
    // assume direct function calls.
    return Reloc::Static;
  }
  return *RM;
}

/// Create an WebAssembly architecture model.
///
WebAssemblyTargetMachine::WebAssemblyTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, Optional<Reloc::Model> RM,
    Optional<CodeModel::Model> CM, CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T,
                        TT.isArch64Bit() ? "e-m:e-p:64:64-i64:64-n32:64-S128"
                                         : "e-m:e-p:32:32-i64:64-n32:64-S128",
                        TT, CPU, FS, Options, getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Large), OL),
      TLOF(new WebAssemblyTargetObjectFile()) {
  // WebAssembly type-checks instructions, but a noreturn function with a return
  // type that doesn't match the context will cause a check failure. So we lower
  // LLVM 'unreachable' to ISD::TRAP and then lower that to WebAssembly's
  // 'unreachable' instructions which is meant for that case.
  this->Options.TrapUnreachable = true;

  // WebAssembly treats each function as an independent unit. Force
  // -ffunction-sections, effectively, so that we can emit them independently.
  this->Options.FunctionSections = true;
  this->Options.DataSections = true;
  this->Options.UniqueSectionNames = true;

  initAsmInfo();

  // Note that we don't use setRequiresStructuredCFG(true). It disables
  // optimizations than we're ok with, and want, such as critical edge
  // splitting and tail merging.
}

WebAssemblyTargetMachine::~WebAssemblyTargetMachine() {}

const WebAssemblySubtarget *
WebAssemblyTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

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
    I = llvm::make_unique<WebAssemblySubtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

namespace {
class StripThreadLocal final : public ModulePass {
  // The default thread model for wasm is single, where thread-local variables
  // are identical to regular globals and should be treated the same. So this
  // pass just converts all GlobalVariables to NotThreadLocal
  static char ID;

public:
  StripThreadLocal() : ModulePass(ID) {}
  bool runOnModule(Module &M) override {
    for (auto &GV : M.globals())
      GV.setThreadLocalMode(GlobalValue::ThreadLocalMode::NotThreadLocal);
    return true;
  }
};
char StripThreadLocal::ID = 0;

/// WebAssembly Code Generator Pass Configuration Options.
class WebAssemblyPassConfig final : public TargetPassConfig {
public:
  WebAssemblyPassConfig(WebAssemblyTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  WebAssemblyTargetMachine &getWebAssemblyTargetMachine() const {
    return getTM<WebAssemblyTargetMachine>();
  }

  FunctionPass *createTargetRegisterAllocator(bool) override;

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPostRegAlloc() override;
  bool addGCPasses() override { return false; }
  void addPreEmitPass() override;
};
} // end anonymous namespace

TargetTransformInfo
WebAssemblyTargetMachine::getTargetTransformInfo(const Function &F) {
  return TargetTransformInfo(WebAssemblyTTIImpl(this, F));
}

TargetPassConfig *
WebAssemblyTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new WebAssemblyPassConfig(*this, PM);
}

FunctionPass *WebAssemblyPassConfig::createTargetRegisterAllocator(bool) {
  return nullptr; // No reg alloc
}

//===----------------------------------------------------------------------===//
// The following functions are called from lib/CodeGen/Passes.cpp to modify
// the CodeGen pass sequence.
//===----------------------------------------------------------------------===//

void WebAssemblyPassConfig::addIRPasses() {
  if (TM->Options.ThreadModel == ThreadModel::Single) {
    // In "single" mode, atomics get lowered to non-atomics.
    addPass(createLowerAtomicPass());
    addPass(new StripThreadLocal());
  } else {
    // Expand some atomic operations. WebAssemblyTargetLowering has hooks which
    // control specifically what gets lowered.
    addPass(createAtomicExpandPass());
  }

  // Add signatures to prototype-less function declarations
  addPass(createWebAssemblyAddMissingPrototypes());

  // Lower .llvm.global_dtors into .llvm_global_ctors with __cxa_atexit calls.
  addPass(createWebAssemblyLowerGlobalDtors());

  // Fix function bitcasts, as WebAssembly requires caller and callee signatures
  // to match.
  addPass(createWebAssemblyFixFunctionBitcasts());

  // Optimize "returned" function attributes.
  if (getOptLevel() != CodeGenOpt::None)
    addPass(createWebAssemblyOptimizeReturned());

  // If exception handling is not enabled and setjmp/longjmp handling is
  // enabled, we lower invokes into calls and delete unreachable landingpad
  // blocks. Lowering invokes when there is no EH support is done in
  // TargetPassConfig::addPassesToHandleExceptions, but this runs after this
  // function and SjLj handling expects all invokes to be lowered before.
  if (!EnableEmException &&
      TM->Options.ExceptionModel == ExceptionHandling::None) {
    addPass(createLowerInvokePass());
    // The lower invoke pass may create unreachable code. Remove it in order not
    // to process dead blocks in setjmp/longjmp handling.
    addPass(createUnreachableBlockEliminationPass());
  }

  // Handle exceptions and setjmp/longjmp if enabled.
  if (EnableEmException || EnableEmSjLj)
    addPass(createWebAssemblyLowerEmscriptenEHSjLj(EnableEmException,
                                                   EnableEmSjLj));

  TargetPassConfig::addIRPasses();
}

bool WebAssemblyPassConfig::addInstSelector() {
  (void)TargetPassConfig::addInstSelector();
  addPass(
      createWebAssemblyISelDag(getWebAssemblyTargetMachine(), getOptLevel()));
  // Run the argument-move pass immediately after the ScheduleDAG scheduler
  // so that we can fix up the ARGUMENT instructions before anything else
  // sees them in the wrong place.
  addPass(createWebAssemblyArgumentMove());
  // Set the p2align operands. This information is present during ISel, however
  // it's inconvenient to collect. Collect it now, and update the immediate
  // operands.
  addPass(createWebAssemblySetP2AlignOperands());
  return false;
}

void WebAssemblyPassConfig::addPostRegAlloc() {
  // TODO: The following CodeGen passes don't currently support code containing
  // virtual registers. Consider removing their restrictions and re-enabling
  // them.

  // These functions all require the NoVRegs property.
  disablePass(&MachineCopyPropagationID);
  disablePass(&PostRAMachineSinkingID);
  disablePass(&PostRASchedulerID);
  disablePass(&FuncletLayoutID);
  disablePass(&StackMapLivenessID);
  disablePass(&LiveDebugValuesID);
  disablePass(&PatchableFunctionID);
  disablePass(&ShrinkWrapID);

  TargetPassConfig::addPostRegAlloc();
}

void WebAssemblyPassConfig::addPreEmitPass() {
  TargetPassConfig::addPreEmitPass();

  // Restore __stack_pointer global after an exception is thrown.
  addPass(createWebAssemblyEHRestoreStackPointer());

  // Now that we have a prologue and epilogue and all frame indices are
  // rewritten, eliminate SP and FP. This allows them to be stackified,
  // colored, and numbered with the rest of the registers.
  addPass(createWebAssemblyReplacePhysRegs());

  // Rewrite pseudo call_indirect instructions as real instructions.
  // This needs to run before register stackification, because we change the
  // order of the arguments.
  addPass(createWebAssemblyCallIndirectFixup());

  // Eliminate multiple-entry loops.
  addPass(createWebAssemblyFixIrreducibleControlFlow());

  // Do various transformations for exception handling.
  addPass(createWebAssemblyLateEHPrepare());

  if (getOptLevel() != CodeGenOpt::None) {
    // LiveIntervals isn't commonly run this late. Re-establish preconditions.
    addPass(createWebAssemblyPrepareForLiveIntervals());

    // Depend on LiveIntervals and perform some optimizations on it.
    addPass(createWebAssemblyOptimizeLiveIntervals());

    // Prepare memory intrinsic calls for register stackifying.
    addPass(createWebAssemblyMemIntrinsicResults());

    // Mark registers as representing wasm's value stack. This is a key
    // code-compression technique in WebAssembly. We run this pass (and
    // MemIntrinsicResults above) very late, so that it sees as much code as
    // possible, including code emitted by PEI and expanded by late tail
    // duplication.
    addPass(createWebAssemblyRegStackify());

    // Run the register coloring pass to reduce the total number of registers.
    // This runs after stackification so that it doesn't consider registers
    // that become stackified.
    addPass(createWebAssemblyRegColoring());
  }

  // Insert explicit local.get and local.set operators.
  addPass(createWebAssemblyExplicitLocals());

  // Sort the blocks of the CFG into topological order, a prerequisite for
  // BLOCK and LOOP markers.
  addPass(createWebAssemblyCFGSort());

  // Insert BLOCK and LOOP markers.
  addPass(createWebAssemblyCFGStackify());

  // Lower br_unless into br_if.
  addPass(createWebAssemblyLowerBrUnless());

  // Perform the very last peephole optimizations on the code.
  if (getOptLevel() != CodeGenOpt::None)
    addPass(createWebAssemblyPeephole());

  // Create a mapping from LLVM CodeGen virtual registers to wasm registers.
  addPass(createWebAssemblyRegNumbering());
}
