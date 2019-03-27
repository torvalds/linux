//===- LTO.cpp ------------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LTO.h"
#include "Config.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/LTO/Caching.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;

using namespace lld;
using namespace lld::elf;

// Creates an empty file to store a list of object files for final
// linking of distributed ThinLTO.
static std::unique_ptr<raw_fd_ostream> openFile(StringRef File) {
  std::error_code EC;
  auto Ret =
      llvm::make_unique<raw_fd_ostream>(File, EC, sys::fs::OpenFlags::F_None);
  if (EC) {
    error("cannot open " + File + ": " + EC.message());
    return nullptr;
  }
  return Ret;
}

static std::string getThinLTOOutputFile(StringRef ModulePath) {
  return lto::getThinLTOOutputFile(ModulePath,
                                   Config->ThinLTOPrefixReplace.first,
                                   Config->ThinLTOPrefixReplace.second);
}

static lto::Config createConfig() {
  lto::Config C;

  // LLD supports the new relocations and address-significance tables.
  C.Options = InitTargetOptionsFromCodeGenFlags();
  C.Options.RelaxELFRelocations = true;
  C.Options.EmitAddrsig = true;

  // Always emit a section per function/datum with LTO.
  C.Options.FunctionSections = true;
  C.Options.DataSections = true;

  if (Config->Relocatable)
    C.RelocModel = None;
  else if (Config->Pic)
    C.RelocModel = Reloc::PIC_;
  else
    C.RelocModel = Reloc::Static;

  C.CodeModel = GetCodeModelFromCMModel();
  C.DisableVerify = Config->DisableVerify;
  C.DiagHandler = diagnosticHandler;
  C.OptLevel = Config->LTOO;
  C.CPU = GetCPUStr();
  C.MAttrs = GetMAttrs();

  // Set up a custom pipeline if we've been asked to.
  C.OptPipeline = Config->LTONewPmPasses;
  C.AAPipeline = Config->LTOAAPipeline;

  // Set up optimization remarks if we've been asked to.
  C.RemarksFilename = Config->OptRemarksFilename;
  C.RemarksWithHotness = Config->OptRemarksWithHotness;

  C.SampleProfile = Config->LTOSampleProfile;
  C.UseNewPM = Config->LTONewPassManager;
  C.DebugPassManager = Config->LTODebugPassManager;
  C.DwoDir = Config->DwoDir;

  if (Config->EmitLLVM) {
    C.PostInternalizeModuleHook = [](size_t Task, const Module &M) {
      if (std::unique_ptr<raw_fd_ostream> OS = openFile(Config->OutputFile))
        WriteBitcodeToFile(M, *OS, false);
      return false;
    };
  }

  if (Config->SaveTemps)
    checkError(C.addSaveTemps(Config->OutputFile.str() + ".",
                              /*UseInputModulePath*/ true));
  return C;
}

BitcodeCompiler::BitcodeCompiler() {
  // Initialize IndexFile.
  if (!Config->ThinLTOIndexOnlyArg.empty())
    IndexFile = openFile(Config->ThinLTOIndexOnlyArg);

  // Initialize LTOObj.
  lto::ThinBackend Backend;
  if (Config->ThinLTOIndexOnly) {
    auto OnIndexWrite = [&](StringRef S) { ThinIndices.erase(S); };
    Backend = lto::createWriteIndexesThinBackend(
        Config->ThinLTOPrefixReplace.first, Config->ThinLTOPrefixReplace.second,
        Config->ThinLTOEmitImportsFiles, IndexFile.get(), OnIndexWrite);
  } else if (Config->ThinLTOJobs != -1U) {
    Backend = lto::createInProcessThinBackend(Config->ThinLTOJobs);
  }

  LTOObj = llvm::make_unique<lto::LTO>(createConfig(), Backend,
                                       Config->LTOPartitions);

  // Initialize UsedStartStop.
  for (Symbol *Sym : Symtab->getSymbols()) {
    StringRef S = Sym->getName();
    for (StringRef Prefix : {"__start_", "__stop_"})
      if (S.startswith(Prefix))
        UsedStartStop.insert(S.substr(Prefix.size()));
  }
}

BitcodeCompiler::~BitcodeCompiler() = default;

static void undefine(Symbol *S) {
  replaceSymbol<Undefined>(S, nullptr, S->getName(), STB_GLOBAL, STV_DEFAULT,
                           S->Type);
}

void BitcodeCompiler::add(BitcodeFile &F) {
  lto::InputFile &Obj = *F.Obj;
  bool IsExec = !Config->Shared && !Config->Relocatable;

  if (Config->ThinLTOIndexOnly)
    ThinIndices.insert(Obj.getName());

  ArrayRef<Symbol *> Syms = F.getSymbols();
  ArrayRef<lto::InputFile::Symbol> ObjSyms = Obj.symbols();
  std::vector<lto::SymbolResolution> Resols(Syms.size());

  // Provide a resolution to the LTO API for each symbol.
  for (size_t I = 0, E = Syms.size(); I != E; ++I) {
    Symbol *Sym = Syms[I];
    const lto::InputFile::Symbol &ObjSym = ObjSyms[I];
    lto::SymbolResolution &R = Resols[I];

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    R.Prevailing = !ObjSym.isUndefined() && Sym->File == &F;

    // We ask LTO to preserve following global symbols:
    // 1) All symbols when doing relocatable link, so that them can be used
    //    for doing final link.
    // 2) Symbols that are used in regular objects.
    // 3) C named sections if we have corresponding __start_/__stop_ symbol.
    // 4) Symbols that are defined in bitcode files and used for dynamic linking.
    R.VisibleToRegularObj = Config->Relocatable || Sym->IsUsedInRegularObj ||
                            (R.Prevailing && Sym->includeInDynsym()) ||
                            UsedStartStop.count(ObjSym.getSectionName());
    const auto *DR = dyn_cast<Defined>(Sym);
    R.FinalDefinitionInLinkageUnit =
        (IsExec || Sym->Visibility != STV_DEFAULT) && DR &&
        // Skip absolute symbols from ELF objects, otherwise PC-rel relocations
        // will be generated by for them, triggering linker errors.
        // Symbol section is always null for bitcode symbols, hence the check
        // for isElf(). Skip linker script defined symbols as well: they have
        // no File defined.
        !(DR->Section == nullptr && (!Sym->File || Sym->File->isElf()));

    if (R.Prevailing)
      undefine(Sym);

    // We tell LTO to not apply interprocedural optimization for wrapped
    // (with --wrap) symbols because otherwise LTO would inline them while
    // their values are still not final.
    R.LinkerRedefined = !Sym->CanInline;
  }
  checkError(LTOObj->add(std::move(F.Obj), Resols));
}

static void createEmptyIndex(StringRef ModulePath) {
  std::string Path = replaceThinLTOSuffix(getThinLTOOutputFile(ModulePath));
  std::unique_ptr<raw_fd_ostream> OS = openFile(Path + ".thinlto.bc");
  if (!OS)
    return;

  ModuleSummaryIndex M(/*HaveGVs*/ false);
  M.setSkipModuleByDistributedBackend();
  WriteIndexToFile(M, *OS);

  if (Config->ThinLTOEmitImportsFiles)
    openFile(Path + ".imports");
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting ObjectFile(s).
std::vector<InputFile *> BitcodeCompiler::compile() {
  unsigned MaxTasks = LTOObj->getMaxTasks();
  Buf.resize(MaxTasks);
  Files.resize(MaxTasks);

  // The --thinlto-cache-dir option specifies the path to a directory in which
  // to cache native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  lto::NativeObjectCache Cache;
  if (!Config->ThinLTOCacheDir.empty())
    Cache = check(
        lto::localCache(Config->ThinLTOCacheDir,
                        [&](size_t Task, std::unique_ptr<MemoryBuffer> MB) {
                          Files[Task] = std::move(MB);
                        }));

  checkError(LTOObj->run(
      [&](size_t Task) {
        return llvm::make_unique<lto::NativeObjectStream>(
            llvm::make_unique<raw_svector_ostream>(Buf[Task]));
      },
      Cache));

  // Emit empty index files for non-indexed files
  for (StringRef S : ThinIndices) {
    std::string Path = getThinLTOOutputFile(S);
    openFile(Path + ".thinlto.bc");
    if (Config->ThinLTOEmitImportsFiles)
      openFile(Path + ".imports");
  }

  // If LazyObjFile has not been added to link, emit empty index files.
  // This is needed because this is what GNU gold plugin does and we have a
  // distributed build system that depends on that behavior.
  if (Config->ThinLTOIndexOnly) {
    for (LazyObjFile *F : LazyObjFiles)
      if (!F->AddedToLink && isBitcode(F->MB))
        createEmptyIndex(F->getName());

    if (!Config->LTOObjPath.empty())
      saveBuffer(Buf[0], Config->LTOObjPath);

    // ThinLTO with index only option is required to generate only the index
    // files. After that, we exit from linker and ThinLTO backend runs in a
    // distributed environment.
    if (IndexFile)
      IndexFile->close();
    return {};
  }

  if (!Config->ThinLTOCacheDir.empty())
    pruneCache(Config->ThinLTOCacheDir, Config->ThinLTOCachePolicy);

  std::vector<InputFile *> Ret;
  for (unsigned I = 0; I != MaxTasks; ++I) {
    if (Buf[I].empty())
      continue;
    if (Config->SaveTemps) {
      if (I == 0)
        saveBuffer(Buf[I], Config->OutputFile + ".lto.o");
      else
        saveBuffer(Buf[I], Config->OutputFile + Twine(I) + ".lto.o");
    }
    InputFile *Obj = createObjectFile(MemoryBufferRef(Buf[I], "lto.tmp"));
    Ret.push_back(Obj);
  }

  for (std::unique_ptr<MemoryBuffer> &File : Files)
    if (File)
      Ret.push_back(createObjectFile(*File));
  return Ret;
}
