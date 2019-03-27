//===- MinimalSymbolDumper.h ---------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBUTIL_MINIMAL_SYMBOL_DUMPER_H
#define LLVM_TOOLS_LLVMPDBUTIL_MINIMAL_SYMBOL_DUMPER_H

#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}

namespace pdb {
class LinePrinter;
class SymbolGroup;

class MinimalSymbolDumper : public codeview::SymbolVisitorCallbacks {
public:
  MinimalSymbolDumper(LinePrinter &P, bool RecordBytes,
                      codeview::LazyRandomTypeCollection &Ids,
                      codeview::LazyRandomTypeCollection &Types)
      : P(P), RecordBytes(RecordBytes), Ids(Ids), Types(Types) {}
  MinimalSymbolDumper(LinePrinter &P, bool RecordBytes,
                      const SymbolGroup &SymGroup,
                      codeview::LazyRandomTypeCollection &Ids,
                      codeview::LazyRandomTypeCollection &Types)
      : P(P), RecordBytes(RecordBytes), SymGroup(&SymGroup), Ids(Ids),
        Types(Types) {}

  Error visitSymbolBegin(codeview::CVSymbol &Record) override;
  Error visitSymbolBegin(codeview::CVSymbol &Record, uint32_t Offset) override;
  Error visitSymbolEnd(codeview::CVSymbol &Record) override;

  void setSymbolGroup(const SymbolGroup *Group) { SymGroup = Group; }

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  virtual Error visitKnownRecord(codeview::CVSymbol &CVR,                      \
                                 codeview::Name &Record) override;
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"

private:
  std::string typeOrIdIndex(codeview::TypeIndex TI, bool IsType) const;

  std::string typeIndex(codeview::TypeIndex TI) const;
  std::string idIndex(codeview::TypeIndex TI) const;

  LinePrinter &P;

  /// Dumping certain records requires knowing what machine this is. The
  /// S_COMPILE3 record will tell us, but if we don't see one, default to X64.
  codeview::CPUType CompilationCPU = codeview::CPUType::X64;

  bool RecordBytes;
  const SymbolGroup *SymGroup = nullptr;
  codeview::LazyRandomTypeCollection &Ids;
  codeview::LazyRandomTypeCollection &Types;
};
} // namespace pdb
} // namespace llvm

#endif
