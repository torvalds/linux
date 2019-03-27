//===- Object.h -------------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OBJCOPY_COFF_OBJECT_H
#define LLVM_TOOLS_OBJCOPY_COFF_OBJECT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {
namespace objcopy {
namespace coff {

struct Relocation {
  Relocation() {}
  Relocation(const object::coff_relocation& R) : Reloc(R) {}

  object::coff_relocation Reloc;
  size_t Target;
  StringRef TargetName; // Used for diagnostics only
};

struct Section {
  object::coff_section Header;
  ArrayRef<uint8_t> Contents;
  std::vector<Relocation> Relocs;
  StringRef Name;
};

struct Symbol {
  object::coff_symbol32 Sym;
  StringRef Name;
  ArrayRef<uint8_t> AuxData;
  size_t UniqueId;
  size_t RawIndex;
  bool Referenced;
};

struct Object {
  bool IsPE = false;

  object::dos_header DosHeader;
  ArrayRef<uint8_t> DosStub;

  object::coff_file_header CoffFileHeader;

  bool Is64 = false;
  object::pe32plus_header PeHeader;
  uint32_t BaseOfData = 0; // pe32plus_header lacks this field.

  std::vector<object::data_directory> DataDirectories;
  std::vector<Section> Sections;

  ArrayRef<Symbol> getSymbols() const { return Symbols; }
  // This allows mutating individual Symbols, but not mutating the list
  // of symbols itself.
  iterator_range<std::vector<Symbol>::iterator> getMutableSymbols() {
    return make_range(Symbols.begin(), Symbols.end());
  }

  const Symbol *findSymbol(size_t UniqueId) const;

  void addSymbols(ArrayRef<Symbol> NewSymbols);
  void removeSymbols(function_ref<bool(const Symbol &)> ToRemove);

  // Set the Referenced field on all Symbols, based on relocations in
  // all sections.
  Error markSymbols();

private:
  std::vector<Symbol> Symbols;
  DenseMap<size_t, Symbol *> SymbolMap;

  size_t NextSymbolUniqueId = 0;

  // Update SymbolMap and RawIndex in each Symbol.
  void updateSymbols();
};

// Copy between coff_symbol16 and coff_symbol32.
// The source and destination files can use either coff_symbol16 or
// coff_symbol32, while we always store them as coff_symbol32 in the
// intermediate data structure.
template <class Symbol1Ty, class Symbol2Ty>
void copySymbol(Symbol1Ty &Dest, const Symbol2Ty &Src) {
  static_assert(sizeof(Dest.Name.ShortName) == sizeof(Src.Name.ShortName),
                "Mismatched name sizes");
  memcpy(Dest.Name.ShortName, Src.Name.ShortName, sizeof(Dest.Name.ShortName));
  Dest.Value = Src.Value;
  Dest.SectionNumber = Src.SectionNumber;
  Dest.Type = Src.Type;
  Dest.StorageClass = Src.StorageClass;
  Dest.NumberOfAuxSymbols = Src.NumberOfAuxSymbols;
}

// Copy between pe32_header and pe32plus_header.
// We store the intermediate state in a pe32plus_header.
template <class PeHeader1Ty, class PeHeader2Ty>
void copyPeHeader(PeHeader1Ty &Dest, const PeHeader2Ty &Src) {
  Dest.Magic = Src.Magic;
  Dest.MajorLinkerVersion = Src.MajorLinkerVersion;
  Dest.MinorLinkerVersion = Src.MinorLinkerVersion;
  Dest.SizeOfCode = Src.SizeOfCode;
  Dest.SizeOfInitializedData = Src.SizeOfInitializedData;
  Dest.SizeOfUninitializedData = Src.SizeOfUninitializedData;
  Dest.AddressOfEntryPoint = Src.AddressOfEntryPoint;
  Dest.BaseOfCode = Src.BaseOfCode;
  Dest.ImageBase = Src.ImageBase;
  Dest.SectionAlignment = Src.SectionAlignment;
  Dest.FileAlignment = Src.FileAlignment;
  Dest.MajorOperatingSystemVersion = Src.MajorOperatingSystemVersion;
  Dest.MinorOperatingSystemVersion = Src.MinorOperatingSystemVersion;
  Dest.MajorImageVersion = Src.MajorImageVersion;
  Dest.MinorImageVersion = Src.MinorImageVersion;
  Dest.MajorSubsystemVersion = Src.MajorSubsystemVersion;
  Dest.MinorSubsystemVersion = Src.MinorSubsystemVersion;
  Dest.Win32VersionValue = Src.Win32VersionValue;
  Dest.SizeOfImage = Src.SizeOfImage;
  Dest.SizeOfHeaders = Src.SizeOfHeaders;
  Dest.CheckSum = Src.CheckSum;
  Dest.Subsystem = Src.Subsystem;
  Dest.DLLCharacteristics = Src.DLLCharacteristics;
  Dest.SizeOfStackReserve = Src.SizeOfStackReserve;
  Dest.SizeOfStackCommit = Src.SizeOfStackCommit;
  Dest.SizeOfHeapReserve = Src.SizeOfHeapReserve;
  Dest.SizeOfHeapCommit = Src.SizeOfHeapCommit;
  Dest.LoaderFlags = Src.LoaderFlags;
  Dest.NumberOfRvaAndSize = Src.NumberOfRvaAndSize;
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_TOOLS_OBJCOPY_COFF_OBJECT_H
