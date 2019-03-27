//===- TypeIndexDiscovery.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEINDEXDISCOVERY_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEINDEXDISCOVERY_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
enum class TiRefKind { TypeRef, IndexRef };
struct TiReference {
  TiRefKind Kind;
  uint32_t Offset;
  uint32_t Count;
};

void discoverTypeIndices(ArrayRef<uint8_t> RecordData,
                         SmallVectorImpl<TiReference> &Refs);
void discoverTypeIndices(const CVType &Type,
                         SmallVectorImpl<TiReference> &Refs);
void discoverTypeIndices(const CVType &Type,
                         SmallVectorImpl<TypeIndex> &Indices);
void discoverTypeIndices(ArrayRef<uint8_t> RecordData,
                         SmallVectorImpl<TypeIndex> &Indices);

/// Discover type indices in symbol records. Returns false if this is an unknown
/// record.
bool discoverTypeIndicesInSymbol(const CVSymbol &Symbol,
                                 SmallVectorImpl<TiReference> &Refs);
bool discoverTypeIndicesInSymbol(ArrayRef<uint8_t> RecordData,
                                 SmallVectorImpl<TiReference> &Refs);
bool discoverTypeIndicesInSymbol(ArrayRef<uint8_t> RecordData,
                                 SmallVectorImpl<TypeIndex> &Indices);
}
}

#endif
