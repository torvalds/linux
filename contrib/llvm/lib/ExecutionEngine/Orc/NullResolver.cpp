//===---------- NullResolver.cpp - Reject symbol lookup requests ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/NullResolver.h"

#include "llvm/Support/ErrorHandling.h"

namespace llvm {
namespace orc {

SymbolNameSet NullResolver::getResponsibilitySet(const SymbolNameSet &Symbols) {
  return Symbols;
}

SymbolNameSet
NullResolver::lookup(std::shared_ptr<AsynchronousSymbolQuery> Query,
                     SymbolNameSet Symbols) {
  assert(Symbols.empty() && "Null resolver: Symbols must be empty");
  return Symbols;
}

JITSymbol NullLegacyResolver::findSymbol(const std::string &Name) {
  llvm_unreachable("Unexpected cross-object symbol reference");
}

JITSymbol
NullLegacyResolver::findSymbolInLogicalDylib(const std::string &Name) {
  llvm_unreachable("Unexpected cross-object symbol reference");
}

} // End namespace orc.
} // End namespace llvm.
