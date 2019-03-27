//===- InputFile.h -------------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_INPUTFILE_H
#define LLVM_TOOLS_LLVMPDBDUMP_INPUTFILE_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}
namespace object {
class COFFObjectFile;
class SectionRef;
} // namespace object

namespace pdb {
class InputFile;
class LinePrinter;
class PDBFile;
class NativeSession;
class SymbolGroupIterator;
class SymbolGroup;

class InputFile {
  InputFile();

  std::unique_ptr<NativeSession> PdbSession;
  object::OwningBinary<object::Binary> CoffObject;
  std::unique_ptr<MemoryBuffer> UnknownFile;
  PointerUnion3<PDBFile *, object::COFFObjectFile *, MemoryBuffer *> PdbOrObj;

  using TypeCollectionPtr = std::unique_ptr<codeview::LazyRandomTypeCollection>;

  TypeCollectionPtr Types;
  TypeCollectionPtr Ids;

  enum TypeCollectionKind { kTypes, kIds };
  codeview::LazyRandomTypeCollection &
  getOrCreateTypeCollection(TypeCollectionKind Kind);

public:
  ~InputFile();
  InputFile(InputFile &&Other) = default;

  static Expected<InputFile> open(StringRef Path,
                                  bool AllowUnknownFile = false);

  PDBFile &pdb();
  const PDBFile &pdb() const;
  object::COFFObjectFile &obj();
  const object::COFFObjectFile &obj() const;
  MemoryBuffer &unknown();
  const MemoryBuffer &unknown() const;

  StringRef getFilePath() const;

  bool hasTypes() const;
  bool hasIds() const;

  codeview::LazyRandomTypeCollection &types();
  codeview::LazyRandomTypeCollection &ids();

  iterator_range<SymbolGroupIterator> symbol_groups();
  SymbolGroupIterator symbol_groups_begin();
  SymbolGroupIterator symbol_groups_end();

  bool isPdb() const;
  bool isObj() const;
  bool isUnknown() const;
};

class SymbolGroup {
  friend class SymbolGroupIterator;

public:
  explicit SymbolGroup(InputFile *File, uint32_t GroupIndex = 0);

  Expected<StringRef> getNameFromStringTable(uint32_t Offset) const;

  void formatFromFileName(LinePrinter &Printer, StringRef File,
                          bool Append = false) const;

  void formatFromChecksumsOffset(LinePrinter &Printer, uint32_t Offset,
                                 bool Append = false) const;

  StringRef name() const;

  codeview::DebugSubsectionArray getDebugSubsections() const {
    return Subsections;
  }
  const ModuleDebugStreamRef &getPdbModuleStream() const;

  const InputFile &getFile() const { return *File; }
  InputFile &getFile() { return *File; }

  bool hasDebugStream() const { return DebugStream != nullptr; }

private:
  void initializeForPdb(uint32_t Modi);
  void updatePdbModi(uint32_t Modi);
  void updateDebugS(const codeview::DebugSubsectionArray &SS);

  void rebuildChecksumMap();
  InputFile *File = nullptr;
  StringRef Name;
  codeview::DebugSubsectionArray Subsections;
  std::shared_ptr<ModuleDebugStreamRef> DebugStream;
  codeview::StringsAndChecksumsRef SC;
  StringMap<codeview::FileChecksumEntry> ChecksumsByFile;
};

class SymbolGroupIterator
    : public iterator_facade_base<SymbolGroupIterator,
                                  std::forward_iterator_tag, SymbolGroup> {
public:
  SymbolGroupIterator();
  explicit SymbolGroupIterator(InputFile &File);
  SymbolGroupIterator(const SymbolGroupIterator &Other) = default;
  SymbolGroupIterator &operator=(const SymbolGroupIterator &R) = default;

  const SymbolGroup &operator*() const;
  SymbolGroup &operator*();

  bool operator==(const SymbolGroupIterator &R) const;
  SymbolGroupIterator &operator++();

private:
  void scanToNextDebugS();
  bool isEnd() const;

  uint32_t Index = 0;
  Optional<object::section_iterator> SectionIter;
  SymbolGroup Value;
};

} // namespace pdb
} // namespace llvm

#endif
