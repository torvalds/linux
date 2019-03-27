//===- PDBFileBuilder.h - PDB File Creation ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBFILEBUILDER_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBFILEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <memory>
#include <vector>

namespace llvm {
namespace msf {
class MSFBuilder;
}
namespace pdb {
class DbiStreamBuilder;
class InfoStreamBuilder;
class GSIStreamBuilder;
class TpiStreamBuilder;

class PDBFileBuilder {
public:
  explicit PDBFileBuilder(BumpPtrAllocator &Allocator);
  ~PDBFileBuilder();
  PDBFileBuilder(const PDBFileBuilder &) = delete;
  PDBFileBuilder &operator=(const PDBFileBuilder &) = delete;

  Error initialize(uint32_t BlockSize);

  msf::MSFBuilder &getMsfBuilder();
  InfoStreamBuilder &getInfoBuilder();
  DbiStreamBuilder &getDbiBuilder();
  TpiStreamBuilder &getTpiBuilder();
  TpiStreamBuilder &getIpiBuilder();
  PDBStringTableBuilder &getStringTableBuilder();
  GSIStreamBuilder &getGsiBuilder();

  // If HashPDBContentsToGUID is true on the InfoStreamBuilder, Guid is filled
  // with the computed PDB GUID on return.
  Error commit(StringRef Filename, codeview::GUID *Guid);

  Expected<uint32_t> getNamedStreamIndex(StringRef Name) const;
  Error addNamedStream(StringRef Name, StringRef Data);
  void addInjectedSource(StringRef Name, std::unique_ptr<MemoryBuffer> Buffer);

private:
  struct InjectedSourceDescriptor {
    // The full name of the stream that contains the contents of this injected
    // source.  This is built as a concatenation of the literal "/src/files"
    // plus the "vname".
    std::string StreamName;

    // The exact name of the file name as specified by the user.
    uint32_t NameIndex;

    // The string table index of the "vname" of the file.  As far as we
    // understand, this is the same as the name, except it is lowercased and
    // forward slashes are converted to backslashes.
    uint32_t VNameIndex;
    std::unique_ptr<MemoryBuffer> Content;
  };

  Error finalizeMsfLayout();
  Expected<uint32_t> allocateNamedStream(StringRef Name, uint32_t Size);

  void commitFpm(WritableBinaryStream &MsfBuffer, const msf::MSFLayout &Layout);
  void commitInjectedSources(WritableBinaryStream &MsfBuffer,
                             const msf::MSFLayout &Layout);
  void commitSrcHeaderBlock(WritableBinaryStream &MsfBuffer,
                            const msf::MSFLayout &Layout);

  BumpPtrAllocator &Allocator;

  std::unique_ptr<msf::MSFBuilder> Msf;
  std::unique_ptr<InfoStreamBuilder> Info;
  std::unique_ptr<DbiStreamBuilder> Dbi;
  std::unique_ptr<GSIStreamBuilder> Gsi;
  std::unique_ptr<TpiStreamBuilder> Tpi;
  std::unique_ptr<TpiStreamBuilder> Ipi;

  PDBStringTableBuilder Strings;
  StringTableHashTraits InjectedSourceHashTraits;
  HashTable<SrcHeaderBlockEntry, StringTableHashTraits> InjectedSourceTable;

  SmallVector<InjectedSourceDescriptor, 2> InjectedSources;

  NamedStreamMap NamedStreams;
  DenseMap<uint32_t, std::string> NamedStreamData;
};
}
}

#endif
