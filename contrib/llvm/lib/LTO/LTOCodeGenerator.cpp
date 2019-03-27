//===-LTOCodeGenerator.cpp - LLVM Link Time Optimizer ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Link Time Optimization library. This library is
// intended to be used by linker to optimize code at link time.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/legacy/LTOCodeGenerator.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/ParallelCG.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/LTO/LTO.h"
#include "llvm/LTO/legacy/LTOModule.h"
#include "llvm/LTO/legacy/UpdateCompilerUsed.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <system_error>
using namespace llvm;

const char* LTOCodeGenerator::getVersionString() {
#ifdef LLVM_VERSION_INFO
  return PACKAGE_NAME " version " PACKAGE_VERSION ", " LLVM_VERSION_INFO;
#else
  return PACKAGE_NAME " version " PACKAGE_VERSION;
#endif
}

namespace llvm {
cl::opt<bool> LTODiscardValueNames(
    "lto-discard-value-names",
    cl::desc("Strip names from Value during LTO (other than GlobalValue)."),
#ifdef NDEBUG
    cl::init(true),
#else
    cl::init(false),
#endif
    cl::Hidden);

cl::opt<std::string>
    LTORemarksFilename("lto-pass-remarks-output",
                       cl::desc("Output filename for pass remarks"),
                       cl::value_desc("filename"));

cl::opt<bool> LTOPassRemarksWithHotness(
    "lto-pass-remarks-with-hotness",
    cl::desc("With PGO, include profile count in optimization remarks"),
    cl::Hidden);
}

LTOCodeGenerator::LTOCodeGenerator(LLVMContext &Context)
    : Context(Context), MergedModule(new Module("ld-temp.o", Context)),
      TheLinker(new Linker(*MergedModule)) {
  Context.setDiscardValueNames(LTODiscardValueNames);
  Context.enableDebugTypeODRUniquing();
  initializeLTOPasses();
}

LTOCodeGenerator::~LTOCodeGenerator() {}

// Initialize LTO passes. Please keep this function in sync with
// PassManagerBuilder::populateLTOPassManager(), and make sure all LTO
// passes are initialized.
void LTOCodeGenerator::initializeLTOPasses() {
  PassRegistry &R = *PassRegistry::getPassRegistry();

  initializeInternalizeLegacyPassPass(R);
  initializeIPSCCPLegacyPassPass(R);
  initializeGlobalOptLegacyPassPass(R);
  initializeConstantMergeLegacyPassPass(R);
  initializeDAHPass(R);
  initializeInstructionCombiningPassPass(R);
  initializeSimpleInlinerPass(R);
  initializePruneEHPass(R);
  initializeGlobalDCELegacyPassPass(R);
  initializeArgPromotionPass(R);
  initializeJumpThreadingPass(R);
  initializeSROALegacyPassPass(R);
  initializePostOrderFunctionAttrsLegacyPassPass(R);
  initializeReversePostOrderFunctionAttrsLegacyPassPass(R);
  initializeGlobalsAAWrapperPassPass(R);
  initializeLegacyLICMPassPass(R);
  initializeMergedLoadStoreMotionLegacyPassPass(R);
  initializeGVNLegacyPassPass(R);
  initializeMemCpyOptLegacyPassPass(R);
  initializeDCELegacyPassPass(R);
  initializeCFGSimplifyPassPass(R);
}

void LTOCodeGenerator::setAsmUndefinedRefs(LTOModule *Mod) {
  const std::vector<StringRef> &undefs = Mod->getAsmUndefinedRefs();
  for (int i = 0, e = undefs.size(); i != e; ++i)
    AsmUndefinedRefs[undefs[i]] = 1;
}

bool LTOCodeGenerator::addModule(LTOModule *Mod) {
  assert(&Mod->getModule().getContext() == &Context &&
         "Expected module in same context");

  bool ret = TheLinker->linkInModule(Mod->takeModule());
  setAsmUndefinedRefs(Mod);

  // We've just changed the input, so let's make sure we verify it.
  HasVerifiedInput = false;

  return !ret;
}

void LTOCodeGenerator::setModule(std::unique_ptr<LTOModule> Mod) {
  assert(&Mod->getModule().getContext() == &Context &&
         "Expected module in same context");

  AsmUndefinedRefs.clear();

  MergedModule = Mod->takeModule();
  TheLinker = make_unique<Linker>(*MergedModule);
  setAsmUndefinedRefs(&*Mod);

  // We've just changed the input, so let's make sure we verify it.
  HasVerifiedInput = false;
}

void LTOCodeGenerator::setTargetOptions(const TargetOptions &Options) {
  this->Options = Options;
}

void LTOCodeGenerator::setDebugInfo(lto_debug_model Debug) {
  switch (Debug) {
  case LTO_DEBUG_MODEL_NONE:
    EmitDwarfDebugInfo = false;
    return;

  case LTO_DEBUG_MODEL_DWARF:
    EmitDwarfDebugInfo = true;
    return;
  }
  llvm_unreachable("Unknown debug format!");
}

void LTOCodeGenerator::setOptLevel(unsigned Level) {
  OptLevel = Level;
  switch (OptLevel) {
  case 0:
    CGOptLevel = CodeGenOpt::None;
    return;
  case 1:
    CGOptLevel = CodeGenOpt::Less;
    return;
  case 2:
    CGOptLevel = CodeGenOpt::Default;
    return;
  case 3:
    CGOptLevel = CodeGenOpt::Aggressive;
    return;
  }
  llvm_unreachable("Unknown optimization level!");
}

bool LTOCodeGenerator::writeMergedModules(StringRef Path) {
  if (!determineTarget())
    return false;

  // We always run the verifier once on the merged module.
  verifyMergedModuleOnce();

  // mark which symbols can not be internalized
  applyScopeRestrictions();

  // create output file
  std::error_code EC;
  ToolOutputFile Out(Path, EC, sys::fs::F_None);
  if (EC) {
    std::string ErrMsg = "could not open bitcode file for writing: ";
    ErrMsg += Path.str() + ": " + EC.message();
    emitError(ErrMsg);
    return false;
  }

  // write bitcode to it
  WriteBitcodeToFile(*MergedModule, Out.os(), ShouldEmbedUselists);
  Out.os().close();

  if (Out.os().has_error()) {
    std::string ErrMsg = "could not write bitcode file: ";
    ErrMsg += Path.str() + ": " + Out.os().error().message();
    emitError(ErrMsg);
    Out.os().clear_error();
    return false;
  }

  Out.keep();
  return true;
}

bool LTOCodeGenerator::compileOptimizedToFile(const char **Name) {
  // make unique temp output file to put generated code
  SmallString<128> Filename;
  int FD;

  StringRef Extension
      (FileType == TargetMachine::CGFT_AssemblyFile ? "s" : "o");

  std::error_code EC =
      sys::fs::createTemporaryFile("lto-llvm", Extension, FD, Filename);
  if (EC) {
    emitError(EC.message());
    return false;
  }

  // generate object file
  ToolOutputFile objFile(Filename, FD);

  bool genResult = compileOptimized(&objFile.os());
  objFile.os().close();
  if (objFile.os().has_error()) {
    emitError((Twine("could not write object file: ") + Filename + ": " +
               objFile.os().error().message())
                  .str());
    objFile.os().clear_error();
    sys::fs::remove(Twine(Filename));
    return false;
  }

  objFile.keep();
  if (!genResult) {
    sys::fs::remove(Twine(Filename));
    return false;
  }

  NativeObjectPath = Filename.c_str();
  *Name = NativeObjectPath.c_str();
  return true;
}

std::unique_ptr<MemoryBuffer>
LTOCodeGenerator::compileOptimized() {
  const char *name;
  if (!compileOptimizedToFile(&name))
    return nullptr;

  // read .o file into memory buffer
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(name, -1, false);
  if (std::error_code EC = BufferOrErr.getError()) {
    emitError(EC.message());
    sys::fs::remove(NativeObjectPath);
    return nullptr;
  }

  // remove temp files
  sys::fs::remove(NativeObjectPath);

  return std::move(*BufferOrErr);
}

bool LTOCodeGenerator::compile_to_file(const char **Name, bool DisableVerify,
                                       bool DisableInline,
                                       bool DisableGVNLoadPRE,
                                       bool DisableVectorization) {
  if (!optimize(DisableVerify, DisableInline, DisableGVNLoadPRE,
                DisableVectorization))
    return false;

  return compileOptimizedToFile(Name);
}

std::unique_ptr<MemoryBuffer>
LTOCodeGenerator::compile(bool DisableVerify, bool DisableInline,
                          bool DisableGVNLoadPRE, bool DisableVectorization) {
  if (!optimize(DisableVerify, DisableInline, DisableGVNLoadPRE,
                DisableVectorization))
    return nullptr;

  return compileOptimized();
}

bool LTOCodeGenerator::determineTarget() {
  if (TargetMach)
    return true;

  TripleStr = MergedModule->getTargetTriple();
  if (TripleStr.empty()) {
    TripleStr = sys::getDefaultTargetTriple();
    MergedModule->setTargetTriple(TripleStr);
  }
  llvm::Triple Triple(TripleStr);

  // create target machine from info for merged modules
  std::string ErrMsg;
  MArch = TargetRegistry::lookupTarget(TripleStr, ErrMsg);
  if (!MArch) {
    emitError(ErrMsg);
    return false;
  }

  // Construct LTOModule, hand over ownership of module and target. Use MAttr as
  // the default set of features.
  SubtargetFeatures Features(MAttr);
  Features.getDefaultSubtargetFeatures(Triple);
  FeatureStr = Features.getString();
  // Set a default CPU for Darwin triples.
  if (MCpu.empty() && Triple.isOSDarwin()) {
    if (Triple.getArch() == llvm::Triple::x86_64)
      MCpu = "core2";
    else if (Triple.getArch() == llvm::Triple::x86)
      MCpu = "yonah";
    else if (Triple.getArch() == llvm::Triple::aarch64)
      MCpu = "cyclone";
  }

  TargetMach = createTargetMachine();
  return true;
}

std::unique_ptr<TargetMachine> LTOCodeGenerator::createTargetMachine() {
  return std::unique_ptr<TargetMachine>(MArch->createTargetMachine(
      TripleStr, MCpu, FeatureStr, Options, RelocModel, None, CGOptLevel));
}

// If a linkonce global is present in the MustPreserveSymbols, we need to make
// sure we honor this. To force the compiler to not drop it, we add it to the
// "llvm.compiler.used" global.
void LTOCodeGenerator::preserveDiscardableGVs(
    Module &TheModule,
    llvm::function_ref<bool(const GlobalValue &)> mustPreserveGV) {
  std::vector<GlobalValue *> Used;
  auto mayPreserveGlobal = [&](GlobalValue &GV) {
    if (!GV.isDiscardableIfUnused() || GV.isDeclaration() ||
        !mustPreserveGV(GV))
      return;
    if (GV.hasAvailableExternallyLinkage())
      return emitWarning(
          (Twine("Linker asked to preserve available_externally global: '") +
           GV.getName() + "'").str());
    if (GV.hasInternalLinkage())
      return emitWarning((Twine("Linker asked to preserve internal global: '") +
                   GV.getName() + "'").str());
    Used.push_back(&GV);
  };
  for (auto &GV : TheModule)
    mayPreserveGlobal(GV);
  for (auto &GV : TheModule.globals())
    mayPreserveGlobal(GV);
  for (auto &GV : TheModule.aliases())
    mayPreserveGlobal(GV);

  if (Used.empty())
    return;

  appendToCompilerUsed(TheModule, Used);
}

void LTOCodeGenerator::applyScopeRestrictions() {
  if (ScopeRestrictionsDone)
    return;

  // Declare a callback for the internalize pass that will ask for every
  // candidate GlobalValue if it can be internalized or not.
  Mangler Mang;
  SmallString<64> MangledName;
  auto mustPreserveGV = [&](const GlobalValue &GV) -> bool {
    // Unnamed globals can't be mangled, but they can't be preserved either.
    if (!GV.hasName())
      return false;

    // Need to mangle the GV as the "MustPreserveSymbols" StringSet is filled
    // with the linker supplied name, which on Darwin includes a leading
    // underscore.
    MangledName.clear();
    MangledName.reserve(GV.getName().size() + 1);
    Mang.getNameWithPrefix(MangledName, &GV, /*CannotUsePrivateLabel=*/false);
    return MustPreserveSymbols.count(MangledName);
  };

  // Preserve linkonce value on linker request
  preserveDiscardableGVs(*MergedModule, mustPreserveGV);

  if (!ShouldInternalize)
    return;

  if (ShouldRestoreGlobalsLinkage) {
    // Record the linkage type of non-local symbols so they can be restored
    // prior
    // to module splitting.
    auto RecordLinkage = [&](const GlobalValue &GV) {
      if (!GV.hasAvailableExternallyLinkage() && !GV.hasLocalLinkage() &&
          GV.hasName())
        ExternalSymbols.insert(std::make_pair(GV.getName(), GV.getLinkage()));
    };
    for (auto &GV : *MergedModule)
      RecordLinkage(GV);
    for (auto &GV : MergedModule->globals())
      RecordLinkage(GV);
    for (auto &GV : MergedModule->aliases())
      RecordLinkage(GV);
  }

  // Update the llvm.compiler_used globals to force preserving libcalls and
  // symbols referenced from asm
  updateCompilerUsed(*MergedModule, *TargetMach, AsmUndefinedRefs);

  internalizeModule(*MergedModule, mustPreserveGV);

  ScopeRestrictionsDone = true;
}

/// Restore original linkage for symbols that may have been internalized
void LTOCodeGenerator::restoreLinkageForExternals() {
  if (!ShouldInternalize || !ShouldRestoreGlobalsLinkage)
    return;

  assert(ScopeRestrictionsDone &&
         "Cannot externalize without internalization!");

  if (ExternalSymbols.empty())
    return;

  auto externalize = [this](GlobalValue &GV) {
    if (!GV.hasLocalLinkage() || !GV.hasName())
      return;

    auto I = ExternalSymbols.find(GV.getName());
    if (I == ExternalSymbols.end())
      return;

    GV.setLinkage(I->second);
  };

  llvm::for_each(MergedModule->functions(), externalize);
  llvm::for_each(MergedModule->globals(), externalize);
  llvm::for_each(MergedModule->aliases(), externalize);
}

void LTOCodeGenerator::verifyMergedModuleOnce() {
  // Only run on the first call.
  if (HasVerifiedInput)
    return;
  HasVerifiedInput = true;

  bool BrokenDebugInfo = false;
  if (verifyModule(*MergedModule, &dbgs(), &BrokenDebugInfo))
    report_fatal_error("Broken module found, compilation aborted!");
  if (BrokenDebugInfo) {
    emitWarning("Invalid debug info found, debug info will be stripped");
    StripDebugInfo(*MergedModule);
  }
}

void LTOCodeGenerator::finishOptimizationRemarks() {
  if (DiagnosticOutputFile) {
    DiagnosticOutputFile->keep();
    // FIXME: LTOCodeGenerator dtor is not invoked on Darwin
    DiagnosticOutputFile->os().flush();
  }
}

/// Optimize merged modules using various IPO passes
bool LTOCodeGenerator::optimize(bool DisableVerify, bool DisableInline,
                                bool DisableGVNLoadPRE,
                                bool DisableVectorization) {
  if (!this->determineTarget())
    return false;

  auto DiagFileOrErr = lto::setupOptimizationRemarks(
      Context, LTORemarksFilename, LTOPassRemarksWithHotness);
  if (!DiagFileOrErr) {
    errs() << "Error: " << toString(DiagFileOrErr.takeError()) << "\n";
    report_fatal_error("Can't get an output file for the remarks");
  }
  DiagnosticOutputFile = std::move(*DiagFileOrErr);

  // We always run the verifier once on the merged module, the `DisableVerify`
  // parameter only applies to subsequent verify.
  verifyMergedModuleOnce();

  // Mark which symbols can not be internalized
  this->applyScopeRestrictions();

  // Instantiate the pass manager to organize the passes.
  legacy::PassManager passes;

  // Add an appropriate DataLayout instance for this module...
  MergedModule->setDataLayout(TargetMach->createDataLayout());

  passes.add(
      createTargetTransformInfoWrapperPass(TargetMach->getTargetIRAnalysis()));

  Triple TargetTriple(TargetMach->getTargetTriple());
  PassManagerBuilder PMB;
  PMB.DisableGVNLoadPRE = DisableGVNLoadPRE;
  PMB.LoopVectorize = !DisableVectorization;
  PMB.SLPVectorize = !DisableVectorization;
  if (!DisableInline)
    PMB.Inliner = createFunctionInliningPass();
  PMB.LibraryInfo = new TargetLibraryInfoImpl(TargetTriple);
  if (Freestanding)
    PMB.LibraryInfo->disableAllFunctions();
  PMB.OptLevel = OptLevel;
  PMB.VerifyInput = !DisableVerify;
  PMB.VerifyOutput = !DisableVerify;

  PMB.populateLTOPassManager(passes);

  // Run our queue of passes all at once now, efficiently.
  passes.run(*MergedModule);

  return true;
}

bool LTOCodeGenerator::compileOptimized(ArrayRef<raw_pwrite_stream *> Out) {
  if (!this->determineTarget())
    return false;

  // We always run the verifier once on the merged module.  If it has already
  // been called in optimize(), this call will return early.
  verifyMergedModuleOnce();

  legacy::PassManager preCodeGenPasses;

  // If the bitcode files contain ARC code and were compiled with optimization,
  // the ObjCARCContractPass must be run, so do it unconditionally here.
  preCodeGenPasses.add(createObjCARCContractPass());
  preCodeGenPasses.run(*MergedModule);

  // Re-externalize globals that may have been internalized to increase scope
  // for splitting
  restoreLinkageForExternals();

  // Do code generation. We need to preserve the module in case the client calls
  // writeMergedModules() after compilation, but we only need to allow this at
  // parallelism level 1. This is achieved by having splitCodeGen return the
  // original module at parallelism level 1 which we then assign back to
  // MergedModule.
  MergedModule = splitCodeGen(std::move(MergedModule), Out, {},
                              [&]() { return createTargetMachine(); }, FileType,
                              ShouldRestoreGlobalsLinkage);

  // If statistics were requested, print them out after codegen.
  if (llvm::AreStatisticsEnabled())
    llvm::PrintStatistics();
  reportAndResetTimings();

  finishOptimizationRemarks();

  return true;
}

/// setCodeGenDebugOptions - Set codegen debugging options to aid in debugging
/// LTO problems.
void LTOCodeGenerator::setCodeGenDebugOptions(StringRef Options) {
  for (std::pair<StringRef, StringRef> o = getToken(Options); !o.first.empty();
       o = getToken(o.second))
    CodegenOptions.push_back(o.first);
}

void LTOCodeGenerator::parseCodeGenDebugOptions() {
  // if options were requested, set them
  if (!CodegenOptions.empty()) {
    // ParseCommandLineOptions() expects argv[0] to be program name.
    std::vector<const char *> CodegenArgv(1, "libLLVMLTO");
    for (std::string &Arg : CodegenOptions)
      CodegenArgv.push_back(Arg.c_str());
    cl::ParseCommandLineOptions(CodegenArgv.size(), CodegenArgv.data());
  }
}


void LTOCodeGenerator::DiagnosticHandler(const DiagnosticInfo &DI) {
  // Map the LLVM internal diagnostic severity to the LTO diagnostic severity.
  lto_codegen_diagnostic_severity_t Severity;
  switch (DI.getSeverity()) {
  case DS_Error:
    Severity = LTO_DS_ERROR;
    break;
  case DS_Warning:
    Severity = LTO_DS_WARNING;
    break;
  case DS_Remark:
    Severity = LTO_DS_REMARK;
    break;
  case DS_Note:
    Severity = LTO_DS_NOTE;
    break;
  }
  // Create the string that will be reported to the external diagnostic handler.
  std::string MsgStorage;
  raw_string_ostream Stream(MsgStorage);
  DiagnosticPrinterRawOStream DP(Stream);
  DI.print(DP);
  Stream.flush();

  // If this method has been called it means someone has set up an external
  // diagnostic handler. Assert on that.
  assert(DiagHandler && "Invalid diagnostic handler");
  (*DiagHandler)(Severity, MsgStorage.c_str(), DiagContext);
}

namespace {
struct LTODiagnosticHandler : public DiagnosticHandler {
  LTOCodeGenerator *CodeGenerator;
  LTODiagnosticHandler(LTOCodeGenerator *CodeGenPtr)
      : CodeGenerator(CodeGenPtr) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    CodeGenerator->DiagnosticHandler(DI);
    return true;
  }
};
}

void
LTOCodeGenerator::setDiagnosticHandler(lto_diagnostic_handler_t DiagHandler,
                                       void *Ctxt) {
  this->DiagHandler = DiagHandler;
  this->DiagContext = Ctxt;
  if (!DiagHandler)
    return Context.setDiagnosticHandler(nullptr);
  // Register the LTOCodeGenerator stub in the LLVMContext to forward the
  // diagnostic to the external DiagHandler.
  Context.setDiagnosticHandler(llvm::make_unique<LTODiagnosticHandler>(this),
                               true);
}

namespace {
class LTODiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;
public:
  LTODiagnosticInfo(const Twine &DiagMsg, DiagnosticSeverity Severity=DS_Error)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
}

void LTOCodeGenerator::emitError(const std::string &ErrMsg) {
  if (DiagHandler)
    (*DiagHandler)(LTO_DS_ERROR, ErrMsg.c_str(), DiagContext);
  else
    Context.diagnose(LTODiagnosticInfo(ErrMsg));
}

void LTOCodeGenerator::emitWarning(const std::string &ErrMsg) {
  if (DiagHandler)
    (*DiagHandler)(LTO_DS_WARNING, ErrMsg.c_str(), DiagContext);
  else
    Context.diagnose(LTODiagnosticInfo(ErrMsg, DS_Warning));
}
