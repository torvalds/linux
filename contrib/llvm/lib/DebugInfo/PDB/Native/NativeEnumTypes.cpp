//==- NativeEnumTypes.cpp - Native Type Enumerator impl ----------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumTypes.h"

#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeEnumTypes::NativeEnumTypes(NativeSession &PDBSession,
                                 LazyRandomTypeCollection &Types,
                                 std::vector<codeview::TypeLeafKind> Kinds)
    : Matches(), Index(0), Session(PDBSession) {
  Optional<TypeIndex> TI = Types.getFirst();
  while (TI) {
    CVType CVT = Types.getType(*TI);
    TypeLeafKind K = CVT.kind();
    if (llvm::is_contained(Kinds, K)) {
      // Don't add forward refs, we'll find those later while enumerating.
      if (!isUdtForwardRef(CVT))
        Matches.push_back(*TI);
    } else if (K == TypeLeafKind::LF_MODIFIER) {
      TypeIndex ModifiedTI = getModifiedType(CVT);
      if (!ModifiedTI.isSimple()) {
        CVType UnmodifiedCVT = Types.getType(ModifiedTI);
        // LF_MODIFIERs point to forward refs, but don't worry about that
        // here.  We're pushing the TypeIndex of the LF_MODIFIER itself,
        // so we'll worry about resolving forward refs later.
        if (llvm::is_contained(Kinds, UnmodifiedCVT.kind()))
          Matches.push_back(*TI);
      }
    }
    TI = Types.getNext(*TI);
  }
}

NativeEnumTypes::NativeEnumTypes(NativeSession &PDBSession,
                                 std::vector<codeview::TypeIndex> Indices)
    : Matches(std::move(Indices)), Index(0), Session(PDBSession) {}

uint32_t NativeEnumTypes::getChildCount() const {
  return static_cast<uint32_t>(Matches.size());
}

std::unique_ptr<PDBSymbol> NativeEnumTypes::getChildAtIndex(uint32_t N) const {
  if (N < Matches.size()) {
    SymIndexId Id = Session.getSymbolCache().findSymbolByTypeIndex(Matches[N]);
    return Session.getSymbolCache().getSymbolById(Id);
  }
  return nullptr;
}

std::unique_ptr<PDBSymbol> NativeEnumTypes::getNext() {
  return getChildAtIndex(Index++);
}

void NativeEnumTypes::reset() { Index = 0; }
