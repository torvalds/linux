//===- SymbolizableObjectFile.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SymbolizableObjectFile class.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIB_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEOBJECTFILE_H
#define LLVM_LIB_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEOBJECTFILE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Support/ErrorOr.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <system_error>

namespace llvm {

class DataExtractor;

namespace symbolize {

class SymbolizableObjectFile : public SymbolizableModule {
public:
  static ErrorOr<std::unique_ptr<SymbolizableObjectFile>>
  create(object::ObjectFile *Obj, std::unique_ptr<DIContext> DICtx);

  DILineInfo symbolizeCode(uint64_t ModuleOffset, FunctionNameKind FNKind,
                           bool UseSymbolTable) const override;
  DIInliningInfo symbolizeInlinedCode(uint64_t ModuleOffset,
                                      FunctionNameKind FNKind,
                                      bool UseSymbolTable) const override;
  DIGlobal symbolizeData(uint64_t ModuleOffset) const override;

  // Return true if this is a 32-bit x86 PE COFF module.
  bool isWin32Module() const override;

  // Returns the preferred base of the module, i.e. where the loader would place
  // it in memory assuming there were no conflicts.
  uint64_t getModulePreferredBase() const override;

private:
  bool shouldOverrideWithSymbolTable(FunctionNameKind FNKind,
                                     bool UseSymbolTable) const;

  bool getNameFromSymbolTable(object::SymbolRef::Type Type, uint64_t Address,
                              std::string &Name, uint64_t &Addr,
                              uint64_t &Size) const;
  // For big-endian PowerPC64 ELF, OpdAddress is the address of the .opd
  // (function descriptor) section and OpdExtractor refers to its contents.
  std::error_code addSymbol(const object::SymbolRef &Symbol,
                            uint64_t SymbolSize,
                            DataExtractor *OpdExtractor = nullptr,
                            uint64_t OpdAddress = 0);
  std::error_code addCoffExportSymbols(const object::COFFObjectFile *CoffObj);

  object::ObjectFile *Module;
  std::unique_ptr<DIContext> DebugInfoContext;

  struct SymbolDesc {
    uint64_t Addr;
    // If size is 0, assume that symbol occupies the whole memory range up to
    // the following symbol.
    uint64_t Size;

    friend bool operator<(const SymbolDesc &s1, const SymbolDesc &s2) {
      return s1.Addr < s2.Addr;
    }
  };
  std::map<SymbolDesc, StringRef> Functions;
  std::map<SymbolDesc, StringRef> Objects;

  SymbolizableObjectFile(object::ObjectFile *Obj,
                         std::unique_ptr<DIContext> DICtx);
};

} // end namespace symbolize

} // end namespace llvm

#endif // LLVM_LIB_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEOBJECTFILE_H
