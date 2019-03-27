//===-- PdbUtil.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SYMBOLFILENATIVEPDB_PDBUTIL_H
#define LLDB_PLUGINS_SYMBOLFILENATIVEPDB_PDBUTIL_H

#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "PdbSymUid.h"

#include <tuple>
#include <utility>

namespace llvm {
namespace pdb {
class TpiStream;
}
} // namespace llvm

namespace lldb_private {
namespace npdb {

class PdbIndex;

struct CVTagRecord {
  enum Kind { Class, Struct, Union, Enum };

  static CVTagRecord create(llvm::codeview::CVType type);

  Kind kind() const { return m_kind; }

  const llvm::codeview::TagRecord &asTag() const {
    if (m_kind == Struct || m_kind == Class)
      return cvclass;
    if (m_kind == Enum)
      return cvenum;
    return cvunion;
  }

  const llvm::codeview::ClassRecord &asClass() const {
    assert(m_kind == Struct || m_kind == Class);
    return cvclass;
  }

  const llvm::codeview::EnumRecord &asEnum() const {
    assert(m_kind == Enum);
    return cvenum;
  }

  const llvm::codeview::UnionRecord &asUnion() const {
    assert(m_kind == Union);
    return cvunion;
  }

  llvm::StringRef name() const {
    if (m_kind == Struct || m_kind == Union)
      return cvclass.Name;
    if (m_kind == Enum)
      return cvenum.Name;
    return cvunion.Name;
  }

private:
  CVTagRecord(llvm::codeview::ClassRecord &&c);
  CVTagRecord(llvm::codeview::UnionRecord &&u);
  CVTagRecord(llvm::codeview::EnumRecord &&e);
  union {
    llvm::codeview::ClassRecord cvclass;
    llvm::codeview::EnumRecord cvenum;
    llvm::codeview::UnionRecord cvunion;
  };
  Kind m_kind;
};

struct SegmentOffset {
  SegmentOffset() = default;
  SegmentOffset(uint16_t s, uint32_t o) : segment(s), offset(o) {}
  uint16_t segment = 0;
  uint32_t offset = 0;
};

struct SegmentOffsetLength {
  SegmentOffsetLength() = default;
  SegmentOffsetLength(uint16_t s, uint32_t o, uint32_t l)
      : so(s, o), length(l) {}
  SegmentOffset so;
  uint32_t length = 0;
};

struct VariableInfo {
  llvm::StringRef name;
  llvm::codeview::TypeIndex type;
  llvm::Optional<DWARFExpression> location;
  llvm::Optional<Variable::RangeList> ranges;
};

llvm::pdb::PDB_SymType CVSymToPDBSym(llvm::codeview::SymbolKind kind);
llvm::pdb::PDB_SymType CVTypeToPDBType(llvm::codeview::TypeLeafKind kind);

bool SymbolHasAddress(const llvm::codeview::CVSymbol &sym);
bool SymbolIsCode(const llvm::codeview::CVSymbol &sym);

SegmentOffset GetSegmentAndOffset(const llvm::codeview::CVSymbol &sym);
SegmentOffsetLength
GetSegmentOffsetAndLength(const llvm::codeview::CVSymbol &sym);

template <typename RecordT> bool IsValidRecord(const RecordT &sym) {
  return true;
}

inline bool IsValidRecord(const llvm::codeview::ProcRefSym &sym) {
  // S_PROCREF symbols have 1-based module indices.
  return sym.Module > 0;
}

bool IsForwardRefUdt(llvm::codeview::CVType cvt);
bool IsTagRecord(llvm::codeview::CVType cvt);
bool IsClassStructUnion(llvm::codeview::CVType cvt);

bool IsForwardRefUdt(const PdbTypeSymId &id, llvm::pdb::TpiStream &tpi);
bool IsTagRecord(const PdbTypeSymId &id, llvm::pdb::TpiStream &tpi);

lldb::AccessType TranslateMemberAccess(llvm::codeview::MemberAccess access);
llvm::codeview::TypeIndex GetFieldListIndex(llvm::codeview::CVType cvt);
llvm::codeview::TypeIndex
LookThroughModifierRecord(llvm::codeview::CVType modifier);

llvm::StringRef DropNameScope(llvm::StringRef name);

VariableInfo GetVariableNameInfo(llvm::codeview::CVSymbol symbol);
VariableInfo GetVariableLocationInfo(PdbIndex &index, PdbCompilandSymId var_id,
                                     lldb::ModuleSP module);

size_t GetTypeSizeForSimpleKind(llvm::codeview::SimpleTypeKind kind);
lldb::BasicType
GetCompilerTypeForSimpleKind(llvm::codeview::SimpleTypeKind kind);

PdbTypeSymId GetBestPossibleDecl(PdbTypeSymId id, llvm::pdb::TpiStream &tpi);

size_t GetSizeOfType(PdbTypeSymId id, llvm::pdb::TpiStream &tpi);

} // namespace npdb
} // namespace lldb_private

#endif
