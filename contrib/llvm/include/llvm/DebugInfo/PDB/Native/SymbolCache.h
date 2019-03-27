//==- SymbolCache.h - Cache of native symbols and ids ------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLCACHE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLCACHE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/Support/Allocator.h"

#include <memory>
#include <vector>

namespace llvm {
namespace pdb {
class DbiStream;
class PDBFile;

class SymbolCache {
  NativeSession &Session;
  DbiStream *Dbi = nullptr;

  /// Cache of all stable symbols, indexed by SymIndexId.  Just because a
  /// symbol has been parsed does not imply that it will be stable and have
  /// an Id.  Id allocation is an implementation, with the only guarantee
  /// being that once an Id is allocated, the symbol can be assumed to be
  /// cached.
  std::vector<std::unique_ptr<NativeRawSymbol>> Cache;

  /// For type records from the TPI stream which have been paresd and cached,
  /// stores a mapping to SymIndexId of the cached symbol.
  DenseMap<codeview::TypeIndex, SymIndexId> TypeIndexToSymbolId;

  /// For field list members which have been parsed and cached, stores a mapping
  /// from (IndexOfClass, MemberIndex) to the corresponding SymIndexId of the
  /// cached symbol.
  DenseMap<std::pair<codeview::TypeIndex, uint32_t>, SymIndexId>
      FieldListMembersToSymbolId;

  /// List of SymIndexIds for each compiland, indexed by compiland index as they
  /// appear in the PDB file.
  std::vector<SymIndexId> Compilands;

  /// Map from global symbol offset to SymIndexId.
  DenseMap<uint32_t, SymIndexId> GlobalOffsetToSymbolId;

  SymIndexId createSymbolPlaceholder() {
    SymIndexId Id = Cache.size();
    Cache.push_back(nullptr);
    return Id;
  }

  template <typename ConcreteSymbolT, typename CVRecordT, typename... Args>
  SymIndexId createSymbolForType(codeview::TypeIndex TI, codeview::CVType CVT,
                                 Args &&... ConstructorArgs) {
    CVRecordT Record;
    if (auto EC =
            codeview::TypeDeserializer::deserializeAs<CVRecordT>(CVT, Record)) {
      consumeError(std::move(EC));
      return 0;
    }

    return createSymbol<ConcreteSymbolT>(
        TI, std::move(Record), std::forward<Args>(ConstructorArgs)...);
  }

  SymIndexId createSymbolForModifiedType(codeview::TypeIndex ModifierTI,
                                         codeview::CVType CVT);

  SymIndexId createSimpleType(codeview::TypeIndex TI,
                              codeview::ModifierOptions Mods);

public:
  SymbolCache(NativeSession &Session, DbiStream *Dbi);

  template <typename ConcreteSymbolT, typename... Args>
  SymIndexId createSymbol(Args &&... ConstructorArgs) {
    SymIndexId Id = Cache.size();

    // Initial construction must not access the cache, since it must be done
    // atomically.
    auto Result = llvm::make_unique<ConcreteSymbolT>(
        Session, Id, std::forward<Args>(ConstructorArgs)...);
    Result->SymbolId = Id;

    NativeRawSymbol *NRS = static_cast<NativeRawSymbol *>(Result.get());
    Cache.push_back(std::move(Result));

    // After the item is in the cache, we can do further initialization which
    // is then allowed to access the cache.
    NRS->initialize();
    return Id;
  }

  std::unique_ptr<IPDBEnumSymbols>
  createTypeEnumerator(codeview::TypeLeafKind Kind);

  std::unique_ptr<IPDBEnumSymbols>
  createTypeEnumerator(std::vector<codeview::TypeLeafKind> Kinds);

  std::unique_ptr<IPDBEnumSymbols>
  createGlobalsEnumerator(codeview::SymbolKind Kind);

  SymIndexId findSymbolByTypeIndex(codeview::TypeIndex TI);

  template <typename ConcreteSymbolT, typename... Args>
  SymIndexId getOrCreateFieldListMember(codeview::TypeIndex FieldListTI,
                                        uint32_t Index,
                                        Args &&... ConstructorArgs) {
    SymIndexId SymId = Cache.size();
    std::pair<codeview::TypeIndex, uint32_t> Key{FieldListTI, Index};
    auto Result = FieldListMembersToSymbolId.try_emplace(Key, SymId);
    if (Result.second)
      SymId =
          createSymbol<ConcreteSymbolT>(std::forward<Args>(ConstructorArgs)...);
    else
      SymId = Result.first->second;
    return SymId;
  }

  SymIndexId getOrCreateGlobalSymbolByOffset(uint32_t Offset);

  std::unique_ptr<PDBSymbolCompiland> getOrCreateCompiland(uint32_t Index);
  uint32_t getNumCompilands() const;

  std::unique_ptr<PDBSymbol> getSymbolById(SymIndexId SymbolId) const;

  NativeRawSymbol &getNativeSymbolById(SymIndexId SymbolId) const;

  template <typename ConcreteT>
  ConcreteT &getNativeSymbolById(SymIndexId SymbolId) const {
    return static_cast<ConcreteT &>(getNativeSymbolById(SymbolId));
  }
};

} // namespace pdb
} // namespace llvm

#endif
