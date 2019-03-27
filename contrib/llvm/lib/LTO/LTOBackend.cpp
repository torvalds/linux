//===-LTOBackend.cpp - LLVM Link Time Optimizer Backend -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the "backend" phase of LTO, i.e. it performs
// optimization and code generation on a loaded module. It is generally used
// internally by the LTO class but can also be used independently, for example
// to implement a standalone ThinLTO backend.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/LTOBackend.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/LTO/LTO.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"
#include "llvm/Transforms/Utils/SplitModule.h"

using namespace llvm;
using namespace lto;

LLVM_ATTRIBUTE_NORETURN static void reportOpenError(StringRef Path, Twine Msg) {
  errs() << "failed to open " << Path << ": " << Msg << '\n';
  errs().flush();
  exit(1);
}

Error Config::addSaveTemps(std::string OutputFileName,
                           bool UseInputModulePath) {
  ShouldDiscardValueNames = false;

  std::error_code EC;
  ResolutionFile = llvm::make_unique<raw_fd_ostream>(
      OutputFileName + "resolution.txt", EC, sys::fs::OpenFlags::F_Text);
  if (EC)
    return errorCodeToError(EC);

  auto setHook = [&](std::string PathSuffix, ModuleHookFn &Hook) {
    // Keep track of the hook provided by the linker, which also needs to run.
    ModuleHookFn LinkerHook = Hook;
    Hook = [=](unsigned Task, const Module &M) {
      // If the linker's hook returned false, we need to pass that result
      // through.
      if (LinkerHook && !LinkerHook(Task, M))
        return false;

      std::string PathPrefix;
      // If this is the combined module (not a ThinLTO backend compile) or the
      // user hasn't requested using the input module's path, emit to a file
      // named from the provided OutputFileName with the Task ID appended.
      if (M.getModuleIdentifier() == "ld-temp.o" || !UseInputModulePath) {
        PathPrefix = OutputFileName;
        if (Task != (unsigned)-1)
          PathPrefix += utostr(Task) + ".";
      } else
        PathPrefix = M.getModuleIdentifier() + ".";
      std::string Path = PathPrefix + PathSuffix + ".bc";
      std::error_code EC;
      raw_fd_ostream OS(Path, EC, sys::fs::OpenFlags::F_None);
      // Because -save-temps is a debugging feature, we report the error
      // directly and exit.
      if (EC)
        reportOpenError(Path, EC.message());
      WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder=*/false);
      return true;
    };
  };

  setHook("0.preopt", PreOptModuleHook);
  setHook("1.promote", PostPromoteModuleHook);
  setHook("2.internalize", PostInternalizeModuleHook);
  setHook("3.import", PostImportModuleHook);
  setHook("4.opt", PostOptModuleHook);
  setHook("5.precodegen", PreCodeGenModuleHook);

  CombinedIndexHook = [=](const ModuleSummaryIndex &Index) {
    std::string Path = OutputFileName + "index.bc";
    std::error_code EC;
    raw_fd_ostream OS(Path, EC, sys::fs::OpenFlags::F_None);
    // Because -save-temps is a debugging feature, we report the error
    // directly and exit.
    if (EC)
      reportOpenError(Path, EC.message());
    WriteIndexToFile(Index, OS);

    Path = OutputFileName + "index.dot";
    raw_fd_ostream OSDot(Path, EC, sys::fs::OpenFlags::F_None);
    if (EC)
      reportOpenError(Path, EC.message());
    Index.exportToDot(OSDot);
    return true;
  };

  return Error::success();
}

namespace {

std::unique_ptr<TargetMachine>
createTargetMachine(Config &Conf, const Target *TheTarget, Module &M) {
  StringRef TheTriple = M.getTargetTriple();
  SubtargetFeatures Features;
  Features.getDefaultSubtargetFeatures(Triple(TheTriple));
  for (const std::string &A : Conf.MAttrs)
    Features.AddFeature(A);

  Reloc::Model RelocModel;
  if (Conf.RelocModel)
    RelocModel = *Conf.RelocModel;
  else
    RelocModel =
        M.getPICLevel() == PICLevel::NotPIC ? Reloc::Static : Reloc::PIC_;

  Optional<CodeModel::Model> CodeModel;
  if (Conf.CodeModel)
    CodeModel = *Conf.CodeModel;
  else
    CodeModel = M.getCodeModel();

  return std::unique_ptr<TargetMachine>(TheTarget->createTargetMachine(
      TheTriple, Conf.CPU, Features.getString(), Conf.Options, RelocModel,
      CodeModel, Conf.CGOptLevel));
}

static void runNewPMPasses(Config &Conf, Module &Mod, TargetMachine *TM,
                           unsigned OptLevel, bool IsThinLTO,
                           ModuleSummaryIndex *ExportSummary,
                           const ModuleSummaryIndex *ImportSummary) {
  Optional<PGOOptions> PGOOpt;
  if (!Conf.SampleProfile.empty())
    PGOOpt = PGOOptions("", "", Conf.SampleProfile, Conf.ProfileRemapping,
                        false, true);

  PassBuilder PB(TM, PGOOpt);
  AAManager AA;

  // Parse a custom AA pipeline if asked to.
  if (auto Err = PB.parseAAPipeline(AA, "default"))
    report_fatal_error("Error parsing default AA pipeline");

  LoopAnalysisManager LAM(Conf.DebugPassManager);
  FunctionAnalysisManager FAM(Conf.DebugPassManager);
  CGSCCAnalysisManager CGAM(Conf.DebugPassManager);
  ModuleAnalysisManager MAM(Conf.DebugPassManager);

  // Register the AA manager first so that our version is the one used.
  FAM.registerPass([&] { return std::move(AA); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM(Conf.DebugPassManager);
  // FIXME (davide): verify the input.

  PassBuilder::OptimizationLevel OL;

  switch (OptLevel) {
  default:
    llvm_unreachable("Invalid optimization level");
  case 0:
    OL = PassBuilder::O0;
    break;
  case 1:
    OL = PassBuilder::O1;
    break;
  case 2:
    OL = PassBuilder::O2;
    break;
  case 3:
    OL = PassBuilder::O3;
    break;
  }

  if (IsThinLTO)
    MPM = PB.buildThinLTODefaultPipeline(OL, Conf.DebugPassManager,
                                         ImportSummary);
  else
    MPM = PB.buildLTODefaultPipeline(OL, Conf.DebugPassManager, ExportSummary);
  MPM.run(Mod, MAM);

  // FIXME (davide): verify the output.
}

static void runNewPMCustomPasses(Module &Mod, TargetMachine *TM,
                                 std::string PipelineDesc,
                                 std::string AAPipelineDesc,
                                 bool DisableVerify) {
  PassBuilder PB(TM);
  AAManager AA;

  // Parse a custom AA pipeline if asked to.
  if (!AAPipelineDesc.empty())
    if (auto Err = PB.parseAAPipeline(AA, AAPipelineDesc))
      report_fatal_error("unable to parse AA pipeline description '" +
                         AAPipelineDesc + "': " + toString(std::move(Err)));

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  // Register the AA manager first so that our version is the one used.
  FAM.registerPass([&] { return std::move(AA); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;

  // Always verify the input.
  MPM.addPass(VerifierPass());

  // Now, add all the passes we've been requested to.
  if (auto Err = PB.parsePassPipeline(MPM, PipelineDesc))
    report_fatal_error("unable to parse pass pipeline description '" +
                       PipelineDesc + "': " + toString(std::move(Err)));

  if (!DisableVerify)
    MPM.addPass(VerifierPass());
  MPM.run(Mod, MAM);
}

static void runOldPMPasses(Config &Conf, Module &Mod, TargetMachine *TM,
                           bool IsThinLTO, ModuleSummaryIndex *ExportSummary,
                           const ModuleSummaryIndex *ImportSummary) {
  legacy::PassManager passes;
  passes.add(createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));

  PassManagerBuilder PMB;
  PMB.LibraryInfo = new TargetLibraryInfoImpl(Triple(TM->getTargetTriple()));
  PMB.Inliner = createFunctionInliningPass();
  PMB.ExportSummary = ExportSummary;
  PMB.ImportSummary = ImportSummary;
  // Unconditionally verify input since it is not verified before this
  // point and has unknown origin.
  PMB.VerifyInput = true;
  PMB.VerifyOutput = !Conf.DisableVerify;
  PMB.LoopVectorize = true;
  PMB.SLPVectorize = true;
  PMB.OptLevel = Conf.OptLevel;
  PMB.PGOSampleUse = Conf.SampleProfile;
  if (IsThinLTO)
    PMB.populateThinLTOPassManager(passes);
  else
    PMB.populateLTOPassManager(passes);
  passes.run(Mod);
}

bool opt(Config &Conf, TargetMachine *TM, unsigned Task, Module &Mod,
         bool IsThinLTO, ModuleSummaryIndex *ExportSummary,
         const ModuleSummaryIndex *ImportSummary) {
  // FIXME: Plumb the combined index into the new pass manager.
  if (!Conf.OptPipeline.empty())
    runNewPMCustomPasses(Mod, TM, Conf.OptPipeline, Conf.AAPipeline,
                         Conf.DisableVerify);
  else if (Conf.UseNewPM)
    runNewPMPasses(Conf, Mod, TM, Conf.OptLevel, IsThinLTO, ExportSummary,
                   ImportSummary);
  else
    runOldPMPasses(Conf, Mod, TM, IsThinLTO, ExportSummary, ImportSummary);
  return !Conf.PostOptModuleHook || Conf.PostOptModuleHook(Task, Mod);
}

void codegen(Config &Conf, TargetMachine *TM, AddStreamFn AddStream,
             unsigned Task, Module &Mod) {
  if (Conf.PreCodeGenModuleHook && !Conf.PreCodeGenModuleHook(Task, Mod))
    return;

  std::unique_ptr<ToolOutputFile> DwoOut;
  SmallString<1024> DwoFile(Conf.DwoPath);
  if (!Conf.DwoDir.empty()) {
    std::error_code EC;
    if (auto EC = llvm::sys::fs::create_directories(Conf.DwoDir))
      report_fatal_error("Failed to create directory " + Conf.DwoDir + ": " +
                         EC.message());

    DwoFile = Conf.DwoDir;
    sys::path::append(DwoFile, std::to_string(Task) + ".dwo");
  }

  if (!DwoFile.empty()) {
    std::error_code EC;
    TM->Options.MCOptions.SplitDwarfFile = DwoFile.str().str();
    DwoOut = llvm::make_unique<ToolOutputFile>(DwoFile, EC, sys::fs::F_None);
    if (EC)
      report_fatal_error("Failed to open " + DwoFile + ": " + EC.message());
  }

  auto Stream = AddStream(Task);
  legacy::PassManager CodeGenPasses;
  if (TM->addPassesToEmitFile(CodeGenPasses, *Stream->OS,
                              DwoOut ? &DwoOut->os() : nullptr,
                              Conf.CGFileType))
    report_fatal_error("Failed to setup codegen");
  CodeGenPasses.run(Mod);

  if (DwoOut)
    DwoOut->keep();
}

void splitCodeGen(Config &C, TargetMachine *TM, AddStreamFn AddStream,
                  unsigned ParallelCodeGenParallelismLevel,
                  std::unique_ptr<Module> Mod) {
  ThreadPool CodegenThreadPool(ParallelCodeGenParallelismLevel);
  unsigned ThreadCount = 0;
  const Target *T = &TM->getTarget();

  SplitModule(
      std::move(Mod), ParallelCodeGenParallelismLevel,
      [&](std::unique_ptr<Module> MPart) {
        // We want to clone the module in a new context to multi-thread the
        // codegen. We do it by serializing partition modules to bitcode
        // (while still on the main thread, in order to avoid data races) and
        // spinning up new threads which deserialize the partitions into
        // separate contexts.
        // FIXME: Provide a more direct way to do this in LLVM.
        SmallString<0> BC;
        raw_svector_ostream BCOS(BC);
        WriteBitcodeToFile(*MPart, BCOS);

        // Enqueue the task
        CodegenThreadPool.async(
            [&](const SmallString<0> &BC, unsigned ThreadId) {
              LTOLLVMContext Ctx(C);
              Expected<std::unique_ptr<Module>> MOrErr = parseBitcodeFile(
                  MemoryBufferRef(StringRef(BC.data(), BC.size()), "ld-temp.o"),
                  Ctx);
              if (!MOrErr)
                report_fatal_error("Failed to read bitcode");
              std::unique_ptr<Module> MPartInCtx = std::move(MOrErr.get());

              std::unique_ptr<TargetMachine> TM =
                  createTargetMachine(C, T, *MPartInCtx);

              codegen(C, TM.get(), AddStream, ThreadId, *MPartInCtx);
            },
            // Pass BC using std::move to ensure that it get moved rather than
            // copied into the thread's context.
            std::move(BC), ThreadCount++);
      },
      false);

  // Because the inner lambda (which runs in a worker thread) captures our local
  // variables, we need to wait for the worker threads to terminate before we
  // can leave the function scope.
  CodegenThreadPool.wait();
}

Expected<const Target *> initAndLookupTarget(Config &C, Module &Mod) {
  if (!C.OverrideTriple.empty())
    Mod.setTargetTriple(C.OverrideTriple);
  else if (Mod.getTargetTriple().empty())
    Mod.setTargetTriple(C.DefaultTriple);

  std::string Msg;
  const Target *T = TargetRegistry::lookupTarget(Mod.getTargetTriple(), Msg);
  if (!T)
    return make_error<StringError>(Msg, inconvertibleErrorCode());
  return T;
}

}

static Error
finalizeOptimizationRemarks(std::unique_ptr<ToolOutputFile> DiagOutputFile) {
  // Make sure we flush the diagnostic remarks file in case the linker doesn't
  // call the global destructors before exiting.
  if (!DiagOutputFile)
    return Error::success();
  DiagOutputFile->keep();
  DiagOutputFile->os().flush();
  return Error::success();
}

Error lto::backend(Config &C, AddStreamFn AddStream,
                   unsigned ParallelCodeGenParallelismLevel,
                   std::unique_ptr<Module> Mod,
                   ModuleSummaryIndex &CombinedIndex) {
  Expected<const Target *> TOrErr = initAndLookupTarget(C, *Mod);
  if (!TOrErr)
    return TOrErr.takeError();

  std::unique_ptr<TargetMachine> TM = createTargetMachine(C, *TOrErr, *Mod);

  // Setup optimization remarks.
  auto DiagFileOrErr = lto::setupOptimizationRemarks(
      Mod->getContext(), C.RemarksFilename, C.RemarksWithHotness);
  if (!DiagFileOrErr)
    return DiagFileOrErr.takeError();
  auto DiagnosticOutputFile = std::move(*DiagFileOrErr);

  if (!C.CodeGenOnly) {
    if (!opt(C, TM.get(), 0, *Mod, /*IsThinLTO=*/false,
             /*ExportSummary=*/&CombinedIndex, /*ImportSummary=*/nullptr))
      return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));
  }

  if (ParallelCodeGenParallelismLevel == 1) {
    codegen(C, TM.get(), AddStream, 0, *Mod);
  } else {
    splitCodeGen(C, TM.get(), AddStream, ParallelCodeGenParallelismLevel,
                 std::move(Mod));
  }
  return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));
}

static void dropDeadSymbols(Module &Mod, const GVSummaryMapTy &DefinedGlobals,
                            const ModuleSummaryIndex &Index) {
  std::vector<GlobalValue*> DeadGVs;
  for (auto &GV : Mod.global_values())
    if (GlobalValueSummary *GVS = DefinedGlobals.lookup(GV.getGUID()))
      if (!Index.isGlobalValueLive(GVS)) {
        DeadGVs.push_back(&GV);
        convertToDeclaration(GV);
      }

  // Now that all dead bodies have been dropped, delete the actual objects
  // themselves when possible.
  for (GlobalValue *GV : DeadGVs) {
    GV->removeDeadConstantUsers();
    // Might reference something defined in native object (i.e. dropped a
    // non-prevailing IR def, but we need to keep the declaration).
    if (GV->use_empty())
      GV->eraseFromParent();
  }
}

Error lto::thinBackend(Config &Conf, unsigned Task, AddStreamFn AddStream,
                       Module &Mod, const ModuleSummaryIndex &CombinedIndex,
                       const FunctionImporter::ImportMapTy &ImportList,
                       const GVSummaryMapTy &DefinedGlobals,
                       MapVector<StringRef, BitcodeModule> &ModuleMap) {
  Expected<const Target *> TOrErr = initAndLookupTarget(Conf, Mod);
  if (!TOrErr)
    return TOrErr.takeError();

  std::unique_ptr<TargetMachine> TM = createTargetMachine(Conf, *TOrErr, Mod);

  // Setup optimization remarks.
  auto DiagFileOrErr = lto::setupOptimizationRemarks(
      Mod.getContext(), Conf.RemarksFilename, Conf.RemarksWithHotness, Task);
  if (!DiagFileOrErr)
    return DiagFileOrErr.takeError();
  auto DiagnosticOutputFile = std::move(*DiagFileOrErr);

  if (Conf.CodeGenOnly) {
    codegen(Conf, TM.get(), AddStream, Task, Mod);
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));
  }

  if (Conf.PreOptModuleHook && !Conf.PreOptModuleHook(Task, Mod))
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));

  renameModuleForThinLTO(Mod, CombinedIndex);

  dropDeadSymbols(Mod, DefinedGlobals, CombinedIndex);

  thinLTOResolvePrevailingInModule(Mod, DefinedGlobals);

  if (Conf.PostPromoteModuleHook && !Conf.PostPromoteModuleHook(Task, Mod))
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));

  if (!DefinedGlobals.empty())
    thinLTOInternalizeModule(Mod, DefinedGlobals);

  if (Conf.PostInternalizeModuleHook &&
      !Conf.PostInternalizeModuleHook(Task, Mod))
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));

  auto ModuleLoader = [&](StringRef Identifier) {
    assert(Mod.getContext().isODRUniquingDebugTypes() &&
           "ODR Type uniquing should be enabled on the context");
    auto I = ModuleMap.find(Identifier);
    assert(I != ModuleMap.end());
    return I->second.getLazyModule(Mod.getContext(),
                                   /*ShouldLazyLoadMetadata=*/true,
                                   /*IsImporting*/ true);
  };

  FunctionImporter Importer(CombinedIndex, ModuleLoader);
  if (Error Err = Importer.importFunctions(Mod, ImportList).takeError())
    return Err;

  if (Conf.PostImportModuleHook && !Conf.PostImportModuleHook(Task, Mod))
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));

  if (!opt(Conf, TM.get(), Task, Mod, /*IsThinLTO=*/true,
           /*ExportSummary=*/nullptr, /*ImportSummary=*/&CombinedIndex))
    return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));

  codegen(Conf, TM.get(), AddStream, Task, Mod);
  return finalizeOptimizationRemarks(std::move(DiagnosticOutputFile));
}
