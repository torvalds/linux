//===- MinGW.cpp ----------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MinGW.h"
#include "SymbolTable.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace lld;
using namespace lld::coff;
using namespace llvm;
using namespace llvm::COFF;

void AutoExporter::initSymbolExcludes() {
  ExcludeSymbolPrefixes = {
      // Import symbols
      "__imp_",
      "__IMPORT_DESCRIPTOR_",
      // Extra import symbols from GNU import libraries
      "__nm_",
      // C++ symbols
      "__rtti_",
      "__builtin_",
      // Artifical symbols such as .refptr
      ".",
  };
  ExcludeSymbolSuffixes = {
      "_iname",
      "_NULL_THUNK_DATA",
  };
  if (Config->Machine == I386) {
    ExcludeSymbols = {
        "__NULL_IMPORT_DESCRIPTOR",
        "__pei386_runtime_relocator",
        "_do_pseudo_reloc",
        "_impure_ptr",
        "__impure_ptr",
        "__fmode",
        "_environ",
        "___dso_handle",
        // These are the MinGW names that differ from the standard
        // ones (lacking an extra underscore).
        "_DllMain@12",
        "_DllEntryPoint@12",
        "_DllMainCRTStartup@12",
    };
    ExcludeSymbolPrefixes.insert("__head_");
  } else {
    ExcludeSymbols = {
        "__NULL_IMPORT_DESCRIPTOR",
        "_pei386_runtime_relocator",
        "do_pseudo_reloc",
        "impure_ptr",
        "_impure_ptr",
        "_fmode",
        "environ",
        "__dso_handle",
        // These are the MinGW names that differ from the standard
        // ones (lacking an extra underscore).
        "DllMain",
        "DllEntryPoint",
        "DllMainCRTStartup",
    };
    ExcludeSymbolPrefixes.insert("_head_");
  }
}

AutoExporter::AutoExporter() {
  ExcludeLibs = {
      "libgcc",
      "libgcc_s",
      "libstdc++",
      "libmingw32",
      "libmingwex",
      "libg2c",
      "libsupc++",
      "libobjc",
      "libgcj",
      "libclang_rt.builtins",
      "libclang_rt.builtins-aarch64",
      "libclang_rt.builtins-arm",
      "libclang_rt.builtins-i386",
      "libclang_rt.builtins-x86_64",
      "libc++",
      "libc++abi",
      "libunwind",
      "libmsvcrt",
      "libucrtbase",
  };
  ExcludeObjects = {
      "crt0.o",
      "crt1.o",
      "crt1u.o",
      "crt2.o",
      "crt2u.o",
      "dllcrt1.o",
      "dllcrt2.o",
      "gcrt0.o",
      "gcrt1.o",
      "gcrt2.o",
      "crtbegin.o",
      "crtend.o",
  };
}

void AutoExporter::addWholeArchive(StringRef Path) {
  StringRef LibName = sys::path::filename(Path);
  // Drop the file extension, to match the processing below.
  LibName = LibName.substr(0, LibName.rfind('.'));
  ExcludeLibs.erase(LibName);
}

bool AutoExporter::shouldExport(Defined *Sym) const {
  if (!Sym || !Sym->isLive() || !Sym->getChunk())
    return false;

  // Only allow the symbol kinds that make sense to export; in particular,
  // disallow import symbols.
  if (!isa<DefinedRegular>(Sym) && !isa<DefinedCommon>(Sym))
    return false;
  if (ExcludeSymbols.count(Sym->getName()))
    return false;

  for (StringRef Prefix : ExcludeSymbolPrefixes.keys())
    if (Sym->getName().startswith(Prefix))
      return false;
  for (StringRef Suffix : ExcludeSymbolSuffixes.keys())
    if (Sym->getName().endswith(Suffix))
      return false;

  // If a corresponding __imp_ symbol exists and is defined, don't export it.
  if (Symtab->find(("__imp_" + Sym->getName()).str()))
    return false;

  // Check that file is non-null before dereferencing it, symbols not
  // originating in regular object files probably shouldn't be exported.
  if (!Sym->getFile())
    return false;

  StringRef LibName = sys::path::filename(Sym->getFile()->ParentName);

  // Drop the file extension.
  LibName = LibName.substr(0, LibName.rfind('.'));
  if (!LibName.empty())
    return !ExcludeLibs.count(LibName);

  StringRef FileName = sys::path::filename(Sym->getFile()->getName());
  return !ExcludeObjects.count(FileName);
}

void coff::writeDefFile(StringRef Name) {
  std::error_code EC;
  raw_fd_ostream OS(Name, EC, sys::fs::F_None);
  if (EC)
    fatal("cannot open " + Name + ": " + EC.message());

  OS << "EXPORTS\n";
  for (Export &E : Config->Exports) {
    OS << "    " << E.ExportName << " "
       << "@" << E.Ordinal;
    if (auto *Def = dyn_cast_or_null<Defined>(E.Sym)) {
      if (Def && Def->getChunk() &&
          !(Def->getChunk()->getOutputCharacteristics() & IMAGE_SCN_MEM_EXECUTE))
        OS << " DATA";
    }
    OS << "\n";
  }
}
