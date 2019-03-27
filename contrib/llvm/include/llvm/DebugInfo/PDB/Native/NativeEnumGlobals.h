//==- NativeEnumGlobals.h - Native Global Enumerator impl --------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMGLOBALS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMGLOBALS_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

#include <vector>

namespace llvm {
namespace pdb {

class NativeSession;

class NativeEnumGlobals : public IPDBEnumChildren<PDBSymbol> {
public:
  NativeEnumGlobals(NativeSession &Session,
                    std::vector<codeview::SymbolKind> Kinds);

  uint32_t getChildCount() const override;
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<PDBSymbol> getNext() override;
  void reset() override;

private:
  std::vector<uint32_t> MatchOffsets;
  uint32_t Index;
  NativeSession &Session;
};

} // namespace pdb
} // namespace llvm

#endif
