//===- PDBStringTable.h - PDB String Table -----------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBSTRINGTABLE_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBSTRINGTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {
class BinaryStreamReader;

namespace msf {
class MappedBlockStream;
}

namespace pdb {

struct PDBStringTableHeader;

class PDBStringTable {
public:
  Error reload(BinaryStreamReader &Reader);

  uint32_t getByteSize() const;
  uint32_t getNameCount() const;
  uint32_t getHashVersion() const;
  uint32_t getSignature() const;

  Expected<StringRef> getStringForID(uint32_t ID) const;
  Expected<uint32_t> getIDForString(StringRef Str) const;

  FixedStreamArray<support::ulittle32_t> name_ids() const;

  const codeview::DebugStringTableSubsectionRef &getStringTable() const;

private:
  Error readHeader(BinaryStreamReader &Reader);
  Error readStrings(BinaryStreamReader &Reader);
  Error readHashTable(BinaryStreamReader &Reader);
  Error readEpilogue(BinaryStreamReader &Reader);

  const PDBStringTableHeader *Header = nullptr;
  codeview::DebugStringTableSubsectionRef Strings;
  FixedStreamArray<support::ulittle32_t> IDs;
  uint32_t NameCount = 0;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_RAW_STRINGTABLE_H
