//===- Writer.cpp ---------------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Writer.h"
#include "Object.h"
#include "llvm-objcopy.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>
#include <cstdint>

namespace llvm {
namespace objcopy {
namespace coff {

using namespace object;
using namespace COFF;

Error COFFWriter::finalizeRelocTargets() {
  for (Section &Sec : Obj.Sections) {
    for (Relocation &R : Sec.Relocs) {
      const Symbol *Sym = Obj.findSymbol(R.Target);
      if (Sym == nullptr)
        return make_error<StringError>("Relocation target " + R.TargetName +
                                           " (" + Twine(R.Target) +
                                           ") not found",
                                       object_error::invalid_symbol_index);
      R.Reloc.SymbolTableIndex = Sym->RawIndex;
    }
  }
  return Error::success();
}

void COFFWriter::layoutSections() {
  for (auto &S : Obj.Sections) {
    if (S.Header.SizeOfRawData > 0)
      S.Header.PointerToRawData = FileSize;
    FileSize += S.Header.SizeOfRawData; // For executables, this is already
                                        // aligned to FileAlignment.
    S.Header.NumberOfRelocations = S.Relocs.size();
    S.Header.PointerToRelocations =
        S.Header.NumberOfRelocations > 0 ? FileSize : 0;
    FileSize += S.Relocs.size() * sizeof(coff_relocation);
    FileSize = alignTo(FileSize, FileAlignment);

    if (S.Header.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
      SizeOfInitializedData += S.Header.SizeOfRawData;
  }
}

size_t COFFWriter::finalizeStringTable() {
  for (auto &S : Obj.Sections)
    if (S.Name.size() > COFF::NameSize)
      StrTabBuilder.add(S.Name);

  for (const auto &S : Obj.getSymbols())
    if (S.Name.size() > COFF::NameSize)
      StrTabBuilder.add(S.Name);

  StrTabBuilder.finalize();

  for (auto &S : Obj.Sections) {
    if (S.Name.size() > COFF::NameSize) {
      snprintf(S.Header.Name, sizeof(S.Header.Name), "/%d",
               (int)StrTabBuilder.getOffset(S.Name));
    } else {
      strncpy(S.Header.Name, S.Name.data(), COFF::NameSize);
    }
  }
  for (auto &S : Obj.getMutableSymbols()) {
    if (S.Name.size() > COFF::NameSize) {
      S.Sym.Name.Offset.Zeroes = 0;
      S.Sym.Name.Offset.Offset = StrTabBuilder.getOffset(S.Name);
    } else {
      strncpy(S.Sym.Name.ShortName, S.Name.data(), COFF::NameSize);
    }
  }
  return StrTabBuilder.getSize();
}

template <class SymbolTy>
std::pair<size_t, size_t> COFFWriter::finalizeSymbolTable() {
  size_t SymTabSize = Obj.getSymbols().size() * sizeof(SymbolTy);
  for (const auto &S : Obj.getSymbols())
    SymTabSize += S.AuxData.size();
  return std::make_pair(SymTabSize, sizeof(SymbolTy));
}

Error COFFWriter::finalize(bool IsBigObj) {
  if (Error E = finalizeRelocTargets())
    return E;

  size_t SizeOfHeaders = 0;
  FileAlignment = 1;
  size_t PeHeaderSize = 0;
  if (Obj.IsPE) {
    Obj.DosHeader.AddressOfNewExeHeader =
        sizeof(Obj.DosHeader) + Obj.DosStub.size();
    SizeOfHeaders += Obj.DosHeader.AddressOfNewExeHeader + sizeof(PEMagic);

    FileAlignment = Obj.PeHeader.FileAlignment;
    Obj.PeHeader.NumberOfRvaAndSize = Obj.DataDirectories.size();

    PeHeaderSize = Obj.Is64 ? sizeof(pe32plus_header) : sizeof(pe32_header);
    SizeOfHeaders +=
        PeHeaderSize + sizeof(data_directory) * Obj.DataDirectories.size();
  }
  Obj.CoffFileHeader.NumberOfSections = Obj.Sections.size();
  SizeOfHeaders +=
      IsBigObj ? sizeof(coff_bigobj_file_header) : sizeof(coff_file_header);
  SizeOfHeaders += sizeof(coff_section) * Obj.Sections.size();
  SizeOfHeaders = alignTo(SizeOfHeaders, FileAlignment);

  Obj.CoffFileHeader.SizeOfOptionalHeader =
      PeHeaderSize + sizeof(data_directory) * Obj.DataDirectories.size();

  FileSize = SizeOfHeaders;
  SizeOfInitializedData = 0;

  layoutSections();

  if (Obj.IsPE) {
    Obj.PeHeader.SizeOfHeaders = SizeOfHeaders;
    Obj.PeHeader.SizeOfInitializedData = SizeOfInitializedData;

    if (!Obj.Sections.empty()) {
      const Section &S = Obj.Sections.back();
      Obj.PeHeader.SizeOfImage =
          alignTo(S.Header.VirtualAddress + S.Header.VirtualSize,
                  Obj.PeHeader.SectionAlignment);
    }

    // If the PE header had a checksum, clear it, since it isn't valid
    // any longer. (We don't calculate a new one.)
    Obj.PeHeader.CheckSum = 0;
  }

  size_t StrTabSize = finalizeStringTable();
  size_t SymTabSize, SymbolSize;
  std::tie(SymTabSize, SymbolSize) = IsBigObj
                                         ? finalizeSymbolTable<coff_symbol32>()
                                         : finalizeSymbolTable<coff_symbol16>();

  size_t PointerToSymbolTable = FileSize;
  // StrTabSize <= 4 is the size of an empty string table, only consisting
  // of the length field.
  if (SymTabSize == 0 && StrTabSize <= 4 && Obj.IsPE) {
    // For executables, don't point to the symbol table and skip writing
    // the length field, if both the symbol and string tables are empty.
    PointerToSymbolTable = 0;
    StrTabSize = 0;
  }

  size_t NumRawSymbols = SymTabSize / SymbolSize;
  Obj.CoffFileHeader.PointerToSymbolTable = PointerToSymbolTable;
  Obj.CoffFileHeader.NumberOfSymbols = NumRawSymbols;
  FileSize += SymTabSize + StrTabSize;
  FileSize = alignTo(FileSize, FileAlignment);

  return Error::success();
}

void COFFWriter::writeHeaders(bool IsBigObj) {
  uint8_t *Ptr = Buf.getBufferStart();
  if (Obj.IsPE) {
    memcpy(Ptr, &Obj.DosHeader, sizeof(Obj.DosHeader));
    Ptr += sizeof(Obj.DosHeader);
    memcpy(Ptr, Obj.DosStub.data(), Obj.DosStub.size());
    Ptr += Obj.DosStub.size();
    memcpy(Ptr, PEMagic, sizeof(PEMagic));
    Ptr += sizeof(PEMagic);
  }
  if (!IsBigObj) {
    memcpy(Ptr, &Obj.CoffFileHeader, sizeof(Obj.CoffFileHeader));
    Ptr += sizeof(Obj.CoffFileHeader);
  } else {
    // Generate a coff_bigobj_file_header, filling it in with the values
    // from Obj.CoffFileHeader. All extra fields that don't exist in
    // coff_file_header can be set to hardcoded values.
    coff_bigobj_file_header BigObjHeader;
    BigObjHeader.Sig1 = IMAGE_FILE_MACHINE_UNKNOWN;
    BigObjHeader.Sig2 = 0xffff;
    BigObjHeader.Version = BigObjHeader::MinBigObjectVersion;
    BigObjHeader.Machine = Obj.CoffFileHeader.Machine;
    BigObjHeader.TimeDateStamp = Obj.CoffFileHeader.TimeDateStamp;
    memcpy(BigObjHeader.UUID, BigObjMagic, sizeof(BigObjMagic));
    BigObjHeader.unused1 = 0;
    BigObjHeader.unused2 = 0;
    BigObjHeader.unused3 = 0;
    BigObjHeader.unused4 = 0;
    // The value in Obj.CoffFileHeader.NumberOfSections is truncated, thus
    // get the original one instead.
    BigObjHeader.NumberOfSections = Obj.Sections.size();
    BigObjHeader.PointerToSymbolTable = Obj.CoffFileHeader.PointerToSymbolTable;
    BigObjHeader.NumberOfSymbols = Obj.CoffFileHeader.NumberOfSymbols;

    memcpy(Ptr, &BigObjHeader, sizeof(BigObjHeader));
    Ptr += sizeof(BigObjHeader);
  }
  if (Obj.IsPE) {
    if (Obj.Is64) {
      memcpy(Ptr, &Obj.PeHeader, sizeof(Obj.PeHeader));
      Ptr += sizeof(Obj.PeHeader);
    } else {
      pe32_header PeHeader;
      copyPeHeader(PeHeader, Obj.PeHeader);
      // The pe32plus_header (stored in Object) lacks the BaseOfData field.
      PeHeader.BaseOfData = Obj.BaseOfData;

      memcpy(Ptr, &PeHeader, sizeof(PeHeader));
      Ptr += sizeof(PeHeader);
    }
    for (const auto &DD : Obj.DataDirectories) {
      memcpy(Ptr, &DD, sizeof(DD));
      Ptr += sizeof(DD);
    }
  }
  for (const auto &S : Obj.Sections) {
    memcpy(Ptr, &S.Header, sizeof(S.Header));
    Ptr += sizeof(S.Header);
  }
}

void COFFWriter::writeSections() {
  for (const auto &S : Obj.Sections) {
    uint8_t *Ptr = Buf.getBufferStart() + S.Header.PointerToRawData;
    std::copy(S.Contents.begin(), S.Contents.end(), Ptr);

    // For executable sections, pad the remainder of the raw data size with
    // 0xcc, which is int3 on x86.
    if ((S.Header.Characteristics & IMAGE_SCN_CNT_CODE) &&
        S.Header.SizeOfRawData > S.Contents.size())
      memset(Ptr + S.Contents.size(), 0xcc,
             S.Header.SizeOfRawData - S.Contents.size());

    Ptr += S.Header.SizeOfRawData;
    for (const auto &R : S.Relocs) {
      memcpy(Ptr, &R.Reloc, sizeof(R.Reloc));
      Ptr += sizeof(R.Reloc);
    }
  }
}

template <class SymbolTy> void COFFWriter::writeSymbolStringTables() {
  uint8_t *Ptr = Buf.getBufferStart() + Obj.CoffFileHeader.PointerToSymbolTable;
  for (const auto &S : Obj.getSymbols()) {
    // Convert symbols back to the right size, from coff_symbol32.
    copySymbol<SymbolTy, coff_symbol32>(*reinterpret_cast<SymbolTy *>(Ptr),
                                        S.Sym);
    Ptr += sizeof(SymbolTy);
    std::copy(S.AuxData.begin(), S.AuxData.end(), Ptr);
    Ptr += S.AuxData.size();
  }
  if (StrTabBuilder.getSize() > 4 || !Obj.IsPE) {
    // Always write a string table in object files, even an empty one.
    StrTabBuilder.write(Ptr);
    Ptr += StrTabBuilder.getSize();
  }
}

Error COFFWriter::write(bool IsBigObj) {
  if (Error E = finalize(IsBigObj))
    return E;

  Buf.allocate(FileSize);

  writeHeaders(IsBigObj);
  writeSections();
  if (IsBigObj)
    writeSymbolStringTables<coff_symbol32>();
  else
    writeSymbolStringTables<coff_symbol16>();

  if (Obj.IsPE)
    if (Error E = patchDebugDirectory())
      return E;

  return Buf.commit();
}

// Locate which sections contain the debug directories, iterate over all
// the debug_directory structs in there, and set the PointerToRawData field
// in all of them, according to their new physical location in the file.
Error COFFWriter::patchDebugDirectory() {
  if (Obj.DataDirectories.size() < DEBUG_DIRECTORY)
    return Error::success();
  const data_directory *Dir = &Obj.DataDirectories[DEBUG_DIRECTORY];
  if (Dir->Size <= 0)
    return Error::success();
  for (const auto &S : Obj.Sections) {
    if (Dir->RelativeVirtualAddress >= S.Header.VirtualAddress &&
        Dir->RelativeVirtualAddress <
            S.Header.VirtualAddress + S.Header.SizeOfRawData) {
      if (Dir->RelativeVirtualAddress + Dir->Size >
          S.Header.VirtualAddress + S.Header.SizeOfRawData)
        return make_error<StringError>(
            "Debug directory extends past end of section",
            object_error::parse_failed);

      size_t Offset = Dir->RelativeVirtualAddress - S.Header.VirtualAddress;
      uint8_t *Ptr = Buf.getBufferStart() + S.Header.PointerToRawData + Offset;
      uint8_t *End = Ptr + Dir->Size;
      while (Ptr < End) {
        debug_directory *Debug = reinterpret_cast<debug_directory *>(Ptr);
        Debug->PointerToRawData =
            S.Header.PointerToRawData + Offset + sizeof(debug_directory);
        Ptr += sizeof(debug_directory) + Debug->SizeOfData;
        Offset += sizeof(debug_directory) + Debug->SizeOfData;
      }
      // Debug directory found and patched, all done.
      return Error::success();
    }
  }
  return make_error<StringError>("Debug directory not found",
                                 object_error::parse_failed);
}

Error COFFWriter::write() {
  bool IsBigObj = Obj.Sections.size() > MaxNumberOfSections16;
  if (IsBigObj && Obj.IsPE)
    return make_error<StringError>("Too many sections for executable",
                                   object_error::parse_failed);
  return write(IsBigObj);
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm
