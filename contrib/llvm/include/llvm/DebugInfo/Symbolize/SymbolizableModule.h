//===- SymbolizableModule.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SymbolizableModule interface.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H
#define LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H

#include "llvm/DebugInfo/DIContext.h"
#include <cstdint>

namespace llvm {
namespace symbolize {

using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;

class SymbolizableModule {
public:
  virtual ~SymbolizableModule() = default;

  virtual DILineInfo symbolizeCode(uint64_t ModuleOffset,
                                   FunctionNameKind FNKind,
                                   bool UseSymbolTable) const = 0;
  virtual DIInliningInfo symbolizeInlinedCode(uint64_t ModuleOffset,
                                              FunctionNameKind FNKind,
                                              bool UseSymbolTable) const = 0;
  virtual DIGlobal symbolizeData(uint64_t ModuleOffset) const = 0;

  // Return true if this is a 32-bit x86 PE COFF module.
  virtual bool isWin32Module() const = 0;

  // Returns the preferred base of the module, i.e. where the loader would place
  // it in memory assuming there were no conflicts.
  virtual uint64_t getModulePreferredBase() const = 0;
};

} // end namespace symbolize
} // end namespace llvm

#endif  // LLVM_DEBUGINFO_SYMBOLIZE_SYMBOLIZABLEMODULE_H
