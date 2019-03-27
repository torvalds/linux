//===- Symbolize.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Header for LLVM symbolization library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H
#define LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H

#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace symbolize {

using namespace object;

using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;

class LLVMSymbolizer {
public:
  struct Options {
    FunctionNameKind PrintFunctions;
    bool UseSymbolTable : 1;
    bool Demangle : 1;
    bool RelativeAddresses : 1;
    std::string DefaultArch;
    std::vector<std::string> DsymHints;

    Options(FunctionNameKind PrintFunctions = FunctionNameKind::LinkageName,
            bool UseSymbolTable = true, bool Demangle = true,
            bool RelativeAddresses = false, std::string DefaultArch = "")
        : PrintFunctions(PrintFunctions), UseSymbolTable(UseSymbolTable),
          Demangle(Demangle), RelativeAddresses(RelativeAddresses),
          DefaultArch(std::move(DefaultArch)) {}
  };

  LLVMSymbolizer(const Options &Opts = Options()) : Opts(Opts) {}

  ~LLVMSymbolizer() {
    flush();
  }

  Expected<DILineInfo> symbolizeCode(const std::string &ModuleName,
                                     uint64_t ModuleOffset,
                                     StringRef DWPName = "");
  Expected<DIInliningInfo> symbolizeInlinedCode(const std::string &ModuleName,
                                                uint64_t ModuleOffset,
                                                StringRef DWPName = "");
  Expected<DIGlobal> symbolizeData(const std::string &ModuleName,
                                   uint64_t ModuleOffset);
  void flush();

  static std::string
  DemangleName(const std::string &Name,
               const SymbolizableModule *DbiModuleDescriptor);

private:
  // Bundles together object file with code/data and object file with
  // corresponding debug info. These objects can be the same.
  using ObjectPair = std::pair<ObjectFile *, ObjectFile *>;

  /// Returns a SymbolizableModule or an error if loading debug info failed.
  /// Only one attempt is made to load a module, and errors during loading are
  /// only reported once. Subsequent calls to get module info for a module that
  /// failed to load will return nullptr.
  Expected<SymbolizableModule *>
  getOrCreateModuleInfo(const std::string &ModuleName, StringRef DWPName = "");

  ObjectFile *lookUpDsymFile(const std::string &Path,
                             const MachOObjectFile *ExeObj,
                             const std::string &ArchName);
  ObjectFile *lookUpDebuglinkObject(const std::string &Path,
                                    const ObjectFile *Obj,
                                    const std::string &ArchName);

  /// Returns pair of pointers to object and debug object.
  Expected<ObjectPair> getOrCreateObjectPair(const std::string &Path,
                                            const std::string &ArchName);

  /// Return a pointer to object file at specified path, for a specified
  /// architecture (e.g. if path refers to a Mach-O universal binary, only one
  /// object file from it will be returned).
  Expected<ObjectFile *> getOrCreateObject(const std::string &Path,
                                          const std::string &ArchName);

  std::map<std::string, std::unique_ptr<SymbolizableModule>> Modules;

  /// Contains cached results of getOrCreateObjectPair().
  std::map<std::pair<std::string, std::string>, ObjectPair>
      ObjectPairForPathArch;

  /// Contains parsed binary for each path, or parsing error.
  std::map<std::string, OwningBinary<Binary>> BinaryForPath;

  /// Parsed object file for path/architecture pair, where "path" refers
  /// to Mach-O universal binary.
  std::map<std::pair<std::string, std::string>, std::unique_ptr<ObjectFile>>
      ObjectForUBPathAndArch;

  Options Opts;
};

} // end namespace symbolize
} // end namespace llvm

#endif // LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZE_H
