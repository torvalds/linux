//===-- CommandFlags.h - Command Line Flags Interface -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains codegen-specific flags that are shared between different
// command line tools. The tools "llc" and "opt" both use this file to prevent
// flag duplication.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.inc"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Host.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <string>
using namespace llvm;

static cl::opt<std::string>
    MArch("march",
          cl::desc("Architecture to generate code for (see --version)"));

static cl::opt<std::string>
    MCPU("mcpu",
         cl::desc("Target a specific cpu type (-mcpu=help for details)"),
         cl::value_desc("cpu-name"), cl::init(""));

static cl::list<std::string>
    MAttrs("mattr", cl::CommaSeparated,
           cl::desc("Target specific attributes (-mattr=help for details)"),
           cl::value_desc("a1,+a2,-a3,..."));

static cl::opt<Reloc::Model> RelocModel(
    "relocation-model", cl::desc("Choose relocation model"),
    cl::values(
        clEnumValN(Reloc::Static, "static", "Non-relocatable code"),
        clEnumValN(Reloc::PIC_, "pic",
                   "Fully relocatable, position independent code"),
        clEnumValN(Reloc::DynamicNoPIC, "dynamic-no-pic",
                   "Relocatable external references, non-relocatable code"),
        clEnumValN(Reloc::ROPI, "ropi",
                   "Code and read-only data relocatable, accessed PC-relative"),
        clEnumValN(
            Reloc::RWPI, "rwpi",
            "Read-write data relocatable, accessed relative to static base"),
        clEnumValN(Reloc::ROPI_RWPI, "ropi-rwpi",
                   "Combination of ropi and rwpi")));

LLVM_ATTRIBUTE_UNUSED static Optional<Reloc::Model> getRelocModel() {
  if (RelocModel.getNumOccurrences()) {
    Reloc::Model R = RelocModel;
    return R;
  }
  return None;
}

static cl::opt<ThreadModel::Model> TMModel(
    "thread-model", cl::desc("Choose threading model"),
    cl::init(ThreadModel::POSIX),
    cl::values(clEnumValN(ThreadModel::POSIX, "posix", "POSIX thread model"),
               clEnumValN(ThreadModel::Single, "single",
                          "Single thread model")));

static cl::opt<llvm::CodeModel::Model> CMModel(
    "code-model", cl::desc("Choose code model"),
    cl::values(clEnumValN(CodeModel::Tiny, "tiny", "Tiny code model"),
               clEnumValN(CodeModel::Small, "small", "Small code model"),
               clEnumValN(CodeModel::Kernel, "kernel", "Kernel code model"),
               clEnumValN(CodeModel::Medium, "medium", "Medium code model"),
               clEnumValN(CodeModel::Large, "large", "Large code model")));

LLVM_ATTRIBUTE_UNUSED static Optional<CodeModel::Model> getCodeModel() {
  if (CMModel.getNumOccurrences()) {
    CodeModel::Model M = CMModel;
    return M;
  }
  return None;
}

static cl::opt<llvm::ExceptionHandling> ExceptionModel(
    "exception-model", cl::desc("exception model"),
    cl::init(ExceptionHandling::None),
    cl::values(
        clEnumValN(ExceptionHandling::None, "default",
                   "default exception handling model"),
        clEnumValN(ExceptionHandling::DwarfCFI, "dwarf",
                   "DWARF-like CFI based exception handling"),
        clEnumValN(ExceptionHandling::SjLj, "sjlj", "SjLj exception handling"),
        clEnumValN(ExceptionHandling::ARM, "arm", "ARM EHABI exceptions"),
        clEnumValN(ExceptionHandling::WinEH, "wineh",
                   "Windows exception model"),
        clEnumValN(ExceptionHandling::Wasm, "wasm",
                   "WebAssembly exception handling")));

static cl::opt<TargetMachine::CodeGenFileType> FileType(
    "filetype", cl::init(TargetMachine::CGFT_AssemblyFile),
    cl::desc(
        "Choose a file type (not all types are supported by all targets):"),
    cl::values(clEnumValN(TargetMachine::CGFT_AssemblyFile, "asm",
                          "Emit an assembly ('.s') file"),
               clEnumValN(TargetMachine::CGFT_ObjectFile, "obj",
                          "Emit a native object ('.o') file"),
               clEnumValN(TargetMachine::CGFT_Null, "null",
                          "Emit nothing, for performance testing")));

static cl::opt<llvm::FramePointer::FP> FramePointerUsage(
    "frame-pointer", cl::desc("Specify frame pointer elimination optimization"),
    cl::init(llvm::FramePointer::None),
    cl::values(
        clEnumValN(llvm::FramePointer::All, "all",
                   "Disable frame pointer elimination"),
        clEnumValN(llvm::FramePointer::NonLeaf, "non-leaf",
                   "Disable frame pointer elimination for non-leaf frame"),
        clEnumValN(llvm::FramePointer::None, "none",
                   "Enable frame pointer elimination")));

static cl::opt<bool> EnableUnsafeFPMath(
    "enable-unsafe-fp-math",
    cl::desc("Enable optimizations that may decrease FP precision"),
    cl::init(false));

static cl::opt<bool> EnableNoInfsFPMath(
    "enable-no-infs-fp-math",
    cl::desc("Enable FP math optimizations that assume no +-Infs"),
    cl::init(false));

static cl::opt<bool> EnableNoNaNsFPMath(
    "enable-no-nans-fp-math",
    cl::desc("Enable FP math optimizations that assume no NaNs"),
    cl::init(false));

static cl::opt<bool> EnableNoSignedZerosFPMath(
    "enable-no-signed-zeros-fp-math",
    cl::desc("Enable FP math optimizations that assume "
             "the sign of 0 is insignificant"),
    cl::init(false));

static cl::opt<bool>
    EnableNoTrappingFPMath("enable-no-trapping-fp-math",
                           cl::desc("Enable setting the FP exceptions build "
                                    "attribute not to use exceptions"),
                           cl::init(false));

static cl::opt<llvm::FPDenormal::DenormalMode> DenormalMode(
    "denormal-fp-math",
    cl::desc("Select which denormal numbers the code is permitted to require"),
    cl::init(FPDenormal::IEEE),
    cl::values(clEnumValN(FPDenormal::IEEE, "ieee",
                          "IEEE 754 denormal numbers"),
               clEnumValN(FPDenormal::PreserveSign, "preserve-sign",
                          "the sign of a  flushed-to-zero number is preserved "
                          "in the sign of 0"),
               clEnumValN(FPDenormal::PositiveZero, "positive-zero",
                          "denormals are flushed to positive zero")));

static cl::opt<bool> EnableHonorSignDependentRoundingFPMath(
    "enable-sign-dependent-rounding-fp-math", cl::Hidden,
    cl::desc("Force codegen to assume rounding mode can change dynamically"),
    cl::init(false));

static cl::opt<llvm::FloatABI::ABIType> FloatABIForCalls(
    "float-abi", cl::desc("Choose float ABI type"), cl::init(FloatABI::Default),
    cl::values(clEnumValN(FloatABI::Default, "default",
                          "Target default float ABI type"),
               clEnumValN(FloatABI::Soft, "soft",
                          "Soft float ABI (implied by -soft-float)"),
               clEnumValN(FloatABI::Hard, "hard",
                          "Hard float ABI (uses FP registers)")));

static cl::opt<llvm::FPOpFusion::FPOpFusionMode> FuseFPOps(
    "fp-contract", cl::desc("Enable aggressive formation of fused FP ops"),
    cl::init(FPOpFusion::Standard),
    cl::values(
        clEnumValN(FPOpFusion::Fast, "fast", "Fuse FP ops whenever profitable"),
        clEnumValN(FPOpFusion::Standard, "on", "Only fuse 'blessed' FP ops."),
        clEnumValN(FPOpFusion::Strict, "off",
                   "Only fuse FP ops when the result won't be affected.")));

static cl::opt<bool> DontPlaceZerosInBSS(
    "nozero-initialized-in-bss",
    cl::desc("Don't place zero-initialized symbols into bss section"),
    cl::init(false));

static cl::opt<bool> EnableGuaranteedTailCallOpt(
    "tailcallopt",
    cl::desc(
        "Turn fastcc calls into tail calls by (potentially) changing ABI."),
    cl::init(false));

static cl::opt<bool> DisableTailCalls("disable-tail-calls",
                                      cl::desc("Never emit tail calls"),
                                      cl::init(false));

static cl::opt<bool> StackSymbolOrdering("stack-symbol-ordering",
                                         cl::desc("Order local stack symbols."),
                                         cl::init(true));

static cl::opt<unsigned>
    OverrideStackAlignment("stack-alignment",
                           cl::desc("Override default stack alignment"),
                           cl::init(0));

static cl::opt<bool>
    StackRealign("stackrealign",
                 cl::desc("Force align the stack to the minimum alignment"),
                 cl::init(false));

static cl::opt<std::string> TrapFuncName(
    "trap-func", cl::Hidden,
    cl::desc("Emit a call to trap function rather than a trap instruction"),
    cl::init(""));

static cl::opt<bool> UseCtors("use-ctors",
                              cl::desc("Use .ctors instead of .init_array."),
                              cl::init(false));

static cl::opt<bool> RelaxELFRelocations(
    "relax-elf-relocations",
    cl::desc("Emit GOTPCRELX/REX_GOTPCRELX instead of GOTPCREL on x86-64 ELF"),
    cl::init(false));

static cl::opt<bool> DataSections("data-sections",
                                  cl::desc("Emit data into separate sections"),
                                  cl::init(false));

static cl::opt<bool>
    FunctionSections("function-sections",
                     cl::desc("Emit functions into separate sections"),
                     cl::init(false));

static cl::opt<bool> EmulatedTLS("emulated-tls",
                                 cl::desc("Use emulated TLS model"),
                                 cl::init(false));

static cl::opt<bool>
    UniqueSectionNames("unique-section-names",
                       cl::desc("Give unique names to every section"),
                       cl::init(true));

static cl::opt<llvm::EABI>
    EABIVersion("meabi", cl::desc("Set EABI type (default depends on triple):"),
                cl::init(EABI::Default),
                cl::values(clEnumValN(EABI::Default, "default",
                                      "Triple default EABI version"),
                           clEnumValN(EABI::EABI4, "4", "EABI version 4"),
                           clEnumValN(EABI::EABI5, "5", "EABI version 5"),
                           clEnumValN(EABI::GNU, "gnu", "EABI GNU")));

static cl::opt<DebuggerKind> DebuggerTuningOpt(
    "debugger-tune", cl::desc("Tune debug info for a particular debugger"),
    cl::init(DebuggerKind::Default),
    cl::values(clEnumValN(DebuggerKind::GDB, "gdb", "gdb"),
               clEnumValN(DebuggerKind::LLDB, "lldb", "lldb"),
               clEnumValN(DebuggerKind::SCE, "sce", "SCE targets (e.g. PS4)")));

static cl::opt<bool> EnableStackSizeSection(
    "stack-size-section",
    cl::desc("Emit a section containing stack size metadata"), cl::init(false));

static cl::opt<bool>
    EnableAddrsig("addrsig", cl::desc("Emit an address-significance table"),
                  cl::init(false));

// Common utility function tightly tied to the options listed here. Initializes
// a TargetOptions object with CodeGen flags and returns it.
static TargetOptions InitTargetOptionsFromCodeGenFlags() {
  TargetOptions Options;
  Options.AllowFPOpFusion = FuseFPOps;
  Options.UnsafeFPMath = EnableUnsafeFPMath;
  Options.NoInfsFPMath = EnableNoInfsFPMath;
  Options.NoNaNsFPMath = EnableNoNaNsFPMath;
  Options.NoSignedZerosFPMath = EnableNoSignedZerosFPMath;
  Options.NoTrappingFPMath = EnableNoTrappingFPMath;
  Options.FPDenormalMode = DenormalMode;
  Options.HonorSignDependentRoundingFPMathOption =
      EnableHonorSignDependentRoundingFPMath;
  if (FloatABIForCalls != FloatABI::Default)
    Options.FloatABIType = FloatABIForCalls;
  Options.NoZerosInBSS = DontPlaceZerosInBSS;
  Options.GuaranteedTailCallOpt = EnableGuaranteedTailCallOpt;
  Options.StackAlignmentOverride = OverrideStackAlignment;
  Options.StackSymbolOrdering = StackSymbolOrdering;
  Options.UseInitArray = !UseCtors;
  Options.RelaxELFRelocations = RelaxELFRelocations;
  Options.DataSections = DataSections;
  Options.FunctionSections = FunctionSections;
  Options.UniqueSectionNames = UniqueSectionNames;
  Options.EmulatedTLS = EmulatedTLS;
  Options.ExplicitEmulatedTLS = EmulatedTLS.getNumOccurrences() > 0;
  Options.ExceptionModel = ExceptionModel;
  Options.EmitStackSizeSection = EnableStackSizeSection;
  Options.EmitAddrsig = EnableAddrsig;

  Options.MCOptions = InitMCTargetOptionsFromFlags();

  Options.ThreadModel = TMModel;
  Options.EABIVersion = EABIVersion;
  Options.DebuggerTuning = DebuggerTuningOpt;

  return Options;
}

LLVM_ATTRIBUTE_UNUSED static std::string getCPUStr() {
  // If user asked for the 'native' CPU, autodetect here. If autodection fails,
  // this will set the CPU to an empty string which tells the target to
  // pick a basic default.
  if (MCPU == "native")
    return sys::getHostCPUName();

  return MCPU;
}

LLVM_ATTRIBUTE_UNUSED static std::string getFeaturesStr() {
  SubtargetFeatures Features;

  // If user asked for the 'native' CPU, we need to autodetect features.
  // This is necessary for x86 where the CPU might not support all the
  // features the autodetected CPU name lists in the target. For example,
  // not all Sandybridge processors support AVX.
  if (MCPU == "native") {
    StringMap<bool> HostFeatures;
    if (sys::getHostCPUFeatures(HostFeatures))
      for (auto &F : HostFeatures)
        Features.AddFeature(F.first(), F.second);
  }

  for (unsigned i = 0; i != MAttrs.size(); ++i)
    Features.AddFeature(MAttrs[i]);

  return Features.getString();
}

LLVM_ATTRIBUTE_UNUSED static std::vector<std::string> getFeatureList() {
  SubtargetFeatures Features;

  // If user asked for the 'native' CPU, we need to autodetect features.
  // This is necessary for x86 where the CPU might not support all the
  // features the autodetected CPU name lists in the target. For example,
  // not all Sandybridge processors support AVX.
  if (MCPU == "native") {
    StringMap<bool> HostFeatures;
    if (sys::getHostCPUFeatures(HostFeatures))
      for (auto &F : HostFeatures)
        Features.AddFeature(F.first(), F.second);
  }

  for (unsigned i = 0; i != MAttrs.size(); ++i)
    Features.AddFeature(MAttrs[i]);

  return Features.getFeatures();
}

/// Set function attributes of functions in Module M based on CPU,
/// Features, and command line flags.
LLVM_ATTRIBUTE_UNUSED static void
setFunctionAttributes(StringRef CPU, StringRef Features, Module &M) {
  for (auto &F : M) {
    auto &Ctx = F.getContext();
    AttributeList Attrs = F.getAttributes();
    AttrBuilder NewAttrs;

    if (!CPU.empty())
      NewAttrs.addAttribute("target-cpu", CPU);
    if (!Features.empty())
      NewAttrs.addAttribute("target-features", Features);
    if (FramePointerUsage.getNumOccurrences() > 0) {
      if (FramePointerUsage == llvm::FramePointer::All)
        NewAttrs.addAttribute("frame-pointer", "all");
      else if (FramePointerUsage == llvm::FramePointer::NonLeaf)
        NewAttrs.addAttribute("frame-pointer", "non-leaf");
      else if (FramePointerUsage == llvm::FramePointer::None)
        NewAttrs.addAttribute("frame-pointer", "none");
    }
    if (DisableTailCalls.getNumOccurrences() > 0)
      NewAttrs.addAttribute("disable-tail-calls",
                            toStringRef(DisableTailCalls));
    if (StackRealign)
      NewAttrs.addAttribute("stackrealign");

    if (TrapFuncName.getNumOccurrences() > 0)
      for (auto &B : F)
        for (auto &I : B)
          if (auto *Call = dyn_cast<CallInst>(&I))
            if (const auto *F = Call->getCalledFunction())
              if (F->getIntrinsicID() == Intrinsic::debugtrap ||
                  F->getIntrinsicID() == Intrinsic::trap)
                Call->addAttribute(
                    llvm::AttributeList::FunctionIndex,
                    Attribute::get(Ctx, "trap-func-name", TrapFuncName));

    // Let NewAttrs override Attrs.
    F.setAttributes(
        Attrs.addAttributes(Ctx, AttributeList::FunctionIndex, NewAttrs));
  }
}
