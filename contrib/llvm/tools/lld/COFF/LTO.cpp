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
#include "Symbols.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "lld/Common/TargetOptionsCommandFlags.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/LTO/Caching.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::object;

using namespace lld;
using namespace lld::coff;

static std::unique_ptr<lto::LTO> createLTO() {
  lto::Config C;
  C.Options = InitTargetOptionsFromCodeGenFlags();

  // Always emit a section per function/datum with LTO. LLVM LTO should get most
  // of the benefit of linker GC, but there are still opportunities for ICF.
  C.Options.FunctionSections = true;
  C.Options.DataSections = true;

  // Use static reloc model on 32-bit x86 because it usually results in more
  // compact code, and because there are also known code generation bugs when
  // using the PIC model (see PR34306).
  if (Config->Machine == COFF::IMAGE_FILE_MACHINE_I386)
    C.RelocModel = Reloc::Static;
  else
    C.RelocModel = Reloc::PIC_;
  C.DisableVerify = true;
  C.DiagHandler = diagnosticHandler;
  C.OptLevel = Config->LTOO;
  C.CPU = GetCPUStr();
  C.MAttrs = GetMAttrs();

  if (Config->SaveTemps)
    checkError(C.addSaveTemps(std::string(Config->OutputFile) + ".",
                              /*UseInputModulePath*/ true));
  lto::ThinBackend Backend;
  if (Config->ThinLTOJobs != 0)
    Backend = lto::createInProcessThinBackend(Config->ThinLTOJobs);
  return llvm::make_unique<lto::LTO>(std::move(C), Backend,
                                     Config->LTOPartitions);
}

BitcodeCompiler::BitcodeCompiler() : LTOObj(createLTO()) {}

BitcodeCompiler::~BitcodeCompiler() = default;

static void undefine(Symbol *S) { replaceSymbol<Undefined>(S, S->getName()); }

void BitcodeCompiler::add(BitcodeFile &F) {
  lto::InputFile &Obj = *F.Obj;
  unsigned SymNum = 0;
  std::vector<Symbol *> SymBodies = F.getSymbols();
  std::vector<lto::SymbolResolution> Resols(SymBodies.size());

  // Provide a resolution to the LTO API for each symbol.
  for (const lto::InputFile::Symbol &ObjSym : Obj.symbols()) {
    Symbol *Sym = SymBodies[SymNum];
    lto::SymbolResolution &R = Resols[SymNum];
    ++SymNum;

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, lld
    // flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    R.Prevailing = !ObjSym.isUndefined() && Sym->getFile() == &F;
    R.VisibleToRegularObj = Sym->IsUsedInRegularObj;
    if (R.Prevailing)
      undefine(Sym);
  }
  checkError(LTOObj->add(std::move(F.Obj), Resols));
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting objects.
std::vector<StringRef> BitcodeCompiler::compile() {
  unsigned MaxTasks = LTOObj->getMaxTasks();
  Buf.resize(MaxTasks);
  Files.resize(MaxTasks);

  // The /lldltocache option specifies the path to a directory in which to cache
  // native object files for ThinLTO incremental builds. If a path was
  // specified, configure LTO to use it as the cache directory.
  lto::NativeObjectCache Cache;
  if (!Config->LTOCache.empty())
    Cache = check(lto::localCache(
        Config->LTOCache, [&](size_t Task, std::unique_ptr<MemoryBuffer> MB) {
          Files[Task] = std::move(MB);
        }));

  checkError(LTOObj->run(
      [&](size_t Task) {
        return llvm::make_unique<lto::NativeObjectStream>(
            llvm::make_unique<raw_svector_ostream>(Buf[Task]));
      },
      Cache));

  if (!Config->LTOCache.empty())
    pruneCache(Config->LTOCache, Config->LTOCachePolicy);

  std::vector<StringRef> Ret;
  for (unsigned I = 0; I != MaxTasks; ++I) {
    if (Buf[I].empty())
      continue;
    if (Config->SaveTemps) {
      if (I == 0)
        saveBuffer(Buf[I], Config->OutputFile + ".lto.obj");
      else
        saveBuffer(Buf[I], Config->OutputFile + Twine(I) + ".lto.obj");
    }
    Ret.emplace_back(Buf[I].data(), Buf[I].size());
  }

  for (std::unique_ptr<MemoryBuffer> &File : Files)
    if (File)
      Ret.push_back(File->getBuffer());

  return Ret;
}
