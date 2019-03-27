//===- PDBStringTableBuilder.h - PDB String Table Builder -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file creates the "/names" stream.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBSTRINGTABLEBUILDER_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBSTRINGTABLEBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/Support/Error.h"
#include <vector>

namespace llvm {
class BinaryStreamWriter;
class WritableBinaryStreamRef;

namespace msf {
struct MSFLayout;
}

namespace pdb {

class PDBFileBuilder;
class PDBStringTableBuilder;

struct StringTableHashTraits {
  PDBStringTableBuilder *Table;

  explicit StringTableHashTraits(PDBStringTableBuilder &Table);
  uint32_t hashLookupKey(StringRef S) const;
  StringRef storageKeyToLookupKey(uint32_t Offset) const;
  uint32_t lookupKeyToStorageKey(StringRef S);
};

class PDBStringTableBuilder {
public:
  // If string S does not exist in the string table, insert it.
  // Returns the ID for S.
  uint32_t insert(StringRef S);

  uint32_t getIdForString(StringRef S) const;
  StringRef getStringForId(uint32_t Id) const;

  uint32_t calculateSerializedSize() const;
  Error commit(BinaryStreamWriter &Writer) const;

  void setStrings(const codeview::DebugStringTableSubsection &Strings);

private:
  uint32_t calculateHashTableSize() const;
  Error writeHeader(BinaryStreamWriter &Writer) const;
  Error writeStrings(BinaryStreamWriter &Writer) const;
  Error writeHashTable(BinaryStreamWriter &Writer) const;
  Error writeEpilogue(BinaryStreamWriter &Writer) const;

  codeview::DebugStringTableSubsection Strings;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_RAW_PDBSTRINGTABLEBUILDER_H
