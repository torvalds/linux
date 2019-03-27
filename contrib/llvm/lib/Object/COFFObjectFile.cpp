//===- COFFObjectFile.cpp - COFF object file implementation ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the COFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <system_error>

using namespace llvm;
using namespace object;

using support::ulittle16_t;
using support::ulittle32_t;
using support::ulittle64_t;
using support::little16_t;

// Returns false if size is greater than the buffer size. And sets ec.
static bool checkSize(MemoryBufferRef M, std::error_code &EC, uint64_t Size) {
  if (M.getBufferSize() < Size) {
    EC = object_error::unexpected_eof;
    return false;
  }
  return true;
}

// Sets Obj unless any bytes in [addr, addr + size) fall outsize of m.
// Returns unexpected_eof if error.
template <typename T>
static std::error_code getObject(const T *&Obj, MemoryBufferRef M,
                                 const void *Ptr,
                                 const uint64_t Size = sizeof(T)) {
  uintptr_t Addr = uintptr_t(Ptr);
  if (std::error_code EC = Binary::checkOffset(M, Addr, Size))
    return EC;
  Obj = reinterpret_cast<const T *>(Addr);
  return std::error_code();
}

// Decode a string table entry in base 64 (//AAAAAA). Expects \arg Str without
// prefixed slashes.
static bool decodeBase64StringEntry(StringRef Str, uint32_t &Result) {
  assert(Str.size() <= 6 && "String too long, possible overflow.");
  if (Str.size() > 6)
    return true;

  uint64_t Value = 0;
  while (!Str.empty()) {
    unsigned CharVal;
    if (Str[0] >= 'A' && Str[0] <= 'Z') // 0..25
      CharVal = Str[0] - 'A';
    else if (Str[0] >= 'a' && Str[0] <= 'z') // 26..51
      CharVal = Str[0] - 'a' + 26;
    else if (Str[0] >= '0' && Str[0] <= '9') // 52..61
      CharVal = Str[0] - '0' + 52;
    else if (Str[0] == '+') // 62
      CharVal = 62;
    else if (Str[0] == '/') // 63
      CharVal = 63;
    else
      return true;

    Value = (Value * 64) + CharVal;
    Str = Str.substr(1);
  }

  if (Value > std::numeric_limits<uint32_t>::max())
    return true;

  Result = static_cast<uint32_t>(Value);
  return false;
}

template <typename coff_symbol_type>
const coff_symbol_type *COFFObjectFile::toSymb(DataRefImpl Ref) const {
  const coff_symbol_type *Addr =
      reinterpret_cast<const coff_symbol_type *>(Ref.p);

  assert(!checkOffset(Data, uintptr_t(Addr), sizeof(*Addr)));
#ifndef NDEBUG
  // Verify that the symbol points to a valid entry in the symbol table.
  uintptr_t Offset = uintptr_t(Addr) - uintptr_t(base());

  assert((Offset - getPointerToSymbolTable()) % sizeof(coff_symbol_type) == 0 &&
         "Symbol did not point to the beginning of a symbol");
#endif

  return Addr;
}

const coff_section *COFFObjectFile::toSec(DataRefImpl Ref) const {
  const coff_section *Addr = reinterpret_cast<const coff_section*>(Ref.p);

#ifndef NDEBUG
  // Verify that the section points to a valid entry in the section table.
  if (Addr < SectionTable || Addr >= (SectionTable + getNumberOfSections()))
    report_fatal_error("Section was outside of section table.");

  uintptr_t Offset = uintptr_t(Addr) - uintptr_t(SectionTable);
  assert(Offset % sizeof(coff_section) == 0 &&
         "Section did not point to the beginning of a section");
#endif

  return Addr;
}

void COFFObjectFile::moveSymbolNext(DataRefImpl &Ref) const {
  auto End = reinterpret_cast<uintptr_t>(StringTable);
  if (SymbolTable16) {
    const coff_symbol16 *Symb = toSymb<coff_symbol16>(Ref);
    Symb += 1 + Symb->NumberOfAuxSymbols;
    Ref.p = std::min(reinterpret_cast<uintptr_t>(Symb), End);
  } else if (SymbolTable32) {
    const coff_symbol32 *Symb = toSymb<coff_symbol32>(Ref);
    Symb += 1 + Symb->NumberOfAuxSymbols;
    Ref.p = std::min(reinterpret_cast<uintptr_t>(Symb), End);
  } else {
    llvm_unreachable("no symbol table pointer!");
  }
}

Expected<StringRef> COFFObjectFile::getSymbolName(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  StringRef Result;
  if (std::error_code EC = getSymbolName(Symb, Result))
    return errorCodeToError(EC);
  return Result;
}

uint64_t COFFObjectFile::getSymbolValueImpl(DataRefImpl Ref) const {
  return getCOFFSymbol(Ref).getValue();
}

uint32_t COFFObjectFile::getSymbolAlignment(DataRefImpl Ref) const {
  // MSVC/link.exe seems to align symbols to the next-power-of-2
  // up to 32 bytes.
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  return std::min(uint64_t(32), PowerOf2Ceil(Symb.getValue()));
}

Expected<uint64_t> COFFObjectFile::getSymbolAddress(DataRefImpl Ref) const {
  uint64_t Result = getSymbolValue(Ref);
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  int32_t SectionNumber = Symb.getSectionNumber();

  if (Symb.isAnyUndefined() || Symb.isCommon() ||
      COFF::isReservedSectionNumber(SectionNumber))
    return Result;

  const coff_section *Section = nullptr;
  if (std::error_code EC = getSection(SectionNumber, Section))
    return errorCodeToError(EC);
  Result += Section->VirtualAddress;

  // The section VirtualAddress does not include ImageBase, and we want to
  // return virtual addresses.
  Result += getImageBase();

  return Result;
}

Expected<SymbolRef::Type> COFFObjectFile::getSymbolType(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  int32_t SectionNumber = Symb.getSectionNumber();

  if (Symb.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION)
    return SymbolRef::ST_Function;
  if (Symb.isAnyUndefined())
    return SymbolRef::ST_Unknown;
  if (Symb.isCommon())
    return SymbolRef::ST_Data;
  if (Symb.isFileRecord())
    return SymbolRef::ST_File;

  // TODO: perhaps we need a new symbol type ST_Section.
  if (SectionNumber == COFF::IMAGE_SYM_DEBUG || Symb.isSectionDefinition())
    return SymbolRef::ST_Debug;

  if (!COFF::isReservedSectionNumber(SectionNumber))
    return SymbolRef::ST_Data;

  return SymbolRef::ST_Other;
}

uint32_t COFFObjectFile::getSymbolFlags(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  uint32_t Result = SymbolRef::SF_None;

  if (Symb.isExternal() || Symb.isWeakExternal())
    Result |= SymbolRef::SF_Global;

  if (const coff_aux_weak_external *AWE = Symb.getWeakExternal()) {
    Result |= SymbolRef::SF_Weak;
    if (AWE->Characteristics != COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS)
      Result |= SymbolRef::SF_Undefined;
  }

  if (Symb.getSectionNumber() == COFF::IMAGE_SYM_ABSOLUTE)
    Result |= SymbolRef::SF_Absolute;

  if (Symb.isFileRecord())
    Result |= SymbolRef::SF_FormatSpecific;

  if (Symb.isSectionDefinition())
    Result |= SymbolRef::SF_FormatSpecific;

  if (Symb.isCommon())
    Result |= SymbolRef::SF_Common;

  if (Symb.isUndefined())
    Result |= SymbolRef::SF_Undefined;

  return Result;
}

uint64_t COFFObjectFile::getCommonSymbolSizeImpl(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  return Symb.getValue();
}

Expected<section_iterator>
COFFObjectFile::getSymbolSection(DataRefImpl Ref) const {
  COFFSymbolRef Symb = getCOFFSymbol(Ref);
  if (COFF::isReservedSectionNumber(Symb.getSectionNumber()))
    return section_end();
  const coff_section *Sec = nullptr;
  if (std::error_code EC = getSection(Symb.getSectionNumber(), Sec))
    return errorCodeToError(EC);
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(Sec);
  return section_iterator(SectionRef(Ret, this));
}

unsigned COFFObjectFile::getSymbolSectionID(SymbolRef Sym) const {
  COFFSymbolRef Symb = getCOFFSymbol(Sym.getRawDataRefImpl());
  return Symb.getSectionNumber();
}

void COFFObjectFile::moveSectionNext(DataRefImpl &Ref) const {
  const coff_section *Sec = toSec(Ref);
  Sec += 1;
  Ref.p = reinterpret_cast<uintptr_t>(Sec);
}

std::error_code COFFObjectFile::getSectionName(DataRefImpl Ref,
                                               StringRef &Result) const {
  const coff_section *Sec = toSec(Ref);
  return getSectionName(Sec, Result);
}

uint64_t COFFObjectFile::getSectionAddress(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  uint64_t Result = Sec->VirtualAddress;

  // The section VirtualAddress does not include ImageBase, and we want to
  // return virtual addresses.
  Result += getImageBase();
  return Result;
}

uint64_t COFFObjectFile::getSectionIndex(DataRefImpl Sec) const {
  return toSec(Sec) - SectionTable;
}

uint64_t COFFObjectFile::getSectionSize(DataRefImpl Ref) const {
  return getSectionSize(toSec(Ref));
}

std::error_code COFFObjectFile::getSectionContents(DataRefImpl Ref,
                                                   StringRef &Result) const {
  const coff_section *Sec = toSec(Ref);
  ArrayRef<uint8_t> Res;
  std::error_code EC = getSectionContents(Sec, Res);
  Result = StringRef(reinterpret_cast<const char*>(Res.data()), Res.size());
  return EC;
}

uint64_t COFFObjectFile::getSectionAlignment(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return Sec->getAlignment();
}

bool COFFObjectFile::isSectionCompressed(DataRefImpl Sec) const {
  return false;
}

bool COFFObjectFile::isSectionText(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return Sec->Characteristics & COFF::IMAGE_SCN_CNT_CODE;
}

bool COFFObjectFile::isSectionData(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  return Sec->Characteristics & COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
}

bool COFFObjectFile::isSectionBSS(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  const uint32_t BssFlags = COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
                            COFF::IMAGE_SCN_MEM_READ |
                            COFF::IMAGE_SCN_MEM_WRITE;
  return (Sec->Characteristics & BssFlags) == BssFlags;
}

unsigned COFFObjectFile::getSectionID(SectionRef Sec) const {
  uintptr_t Offset =
      uintptr_t(Sec.getRawDataRefImpl().p) - uintptr_t(SectionTable);
  assert((Offset % sizeof(coff_section)) == 0);
  return (Offset / sizeof(coff_section)) + 1;
}

bool COFFObjectFile::isSectionVirtual(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  // In COFF, a virtual section won't have any in-file
  // content, so the file pointer to the content will be zero.
  return Sec->PointerToRawData == 0;
}

static uint32_t getNumberOfRelocations(const coff_section *Sec,
                                       MemoryBufferRef M, const uint8_t *base) {
  // The field for the number of relocations in COFF section table is only
  // 16-bit wide. If a section has more than 65535 relocations, 0xFFFF is set to
  // NumberOfRelocations field, and the actual relocation count is stored in the
  // VirtualAddress field in the first relocation entry.
  if (Sec->hasExtendedRelocations()) {
    const coff_relocation *FirstReloc;
    if (getObject(FirstReloc, M, reinterpret_cast<const coff_relocation*>(
        base + Sec->PointerToRelocations)))
      return 0;
    // -1 to exclude this first relocation entry.
    return FirstReloc->VirtualAddress - 1;
  }
  return Sec->NumberOfRelocations;
}

static const coff_relocation *
getFirstReloc(const coff_section *Sec, MemoryBufferRef M, const uint8_t *Base) {
  uint64_t NumRelocs = getNumberOfRelocations(Sec, M, Base);
  if (!NumRelocs)
    return nullptr;
  auto begin = reinterpret_cast<const coff_relocation *>(
      Base + Sec->PointerToRelocations);
  if (Sec->hasExtendedRelocations()) {
    // Skip the first relocation entry repurposed to store the number of
    // relocations.
    begin++;
  }
  if (Binary::checkOffset(M, uintptr_t(begin),
                          sizeof(coff_relocation) * NumRelocs))
    return nullptr;
  return begin;
}

relocation_iterator COFFObjectFile::section_rel_begin(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  const coff_relocation *begin = getFirstReloc(Sec, Data, base());
  if (begin && Sec->VirtualAddress != 0)
    report_fatal_error("Sections with relocations should have an address of 0");
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(begin);
  return relocation_iterator(RelocationRef(Ret, this));
}

relocation_iterator COFFObjectFile::section_rel_end(DataRefImpl Ref) const {
  const coff_section *Sec = toSec(Ref);
  const coff_relocation *I = getFirstReloc(Sec, Data, base());
  if (I)
    I += getNumberOfRelocations(Sec, Data, base());
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(I);
  return relocation_iterator(RelocationRef(Ret, this));
}

// Initialize the pointer to the symbol table.
std::error_code COFFObjectFile::initSymbolTablePtr() {
  if (COFFHeader)
    if (std::error_code EC = getObject(
            SymbolTable16, Data, base() + getPointerToSymbolTable(),
            (uint64_t)getNumberOfSymbols() * getSymbolTableEntrySize()))
      return EC;

  if (COFFBigObjHeader)
    if (std::error_code EC = getObject(
            SymbolTable32, Data, base() + getPointerToSymbolTable(),
            (uint64_t)getNumberOfSymbols() * getSymbolTableEntrySize()))
      return EC;

  // Find string table. The first four byte of the string table contains the
  // total size of the string table, including the size field itself. If the
  // string table is empty, the value of the first four byte would be 4.
  uint32_t StringTableOffset = getPointerToSymbolTable() +
                               getNumberOfSymbols() * getSymbolTableEntrySize();
  const uint8_t *StringTableAddr = base() + StringTableOffset;
  const ulittle32_t *StringTableSizePtr;
  if (std::error_code EC = getObject(StringTableSizePtr, Data, StringTableAddr))
    return EC;
  StringTableSize = *StringTableSizePtr;
  if (std::error_code EC =
          getObject(StringTable, Data, StringTableAddr, StringTableSize))
    return EC;

  // Treat table sizes < 4 as empty because contrary to the PECOFF spec, some
  // tools like cvtres write a size of 0 for an empty table instead of 4.
  if (StringTableSize < 4)
      StringTableSize = 4;

  // Check that the string table is null terminated if has any in it.
  if (StringTableSize > 4 && StringTable[StringTableSize - 1] != 0)
    return  object_error::parse_failed;
  return std::error_code();
}

uint64_t COFFObjectFile::getImageBase() const {
  if (PE32Header)
    return PE32Header->ImageBase;
  else if (PE32PlusHeader)
    return PE32PlusHeader->ImageBase;
  // This actually comes up in practice.
  return 0;
}

// Returns the file offset for the given VA.
std::error_code COFFObjectFile::getVaPtr(uint64_t Addr, uintptr_t &Res) const {
  uint64_t ImageBase = getImageBase();
  uint64_t Rva = Addr - ImageBase;
  assert(Rva <= UINT32_MAX);
  return getRvaPtr((uint32_t)Rva, Res);
}

// Returns the file offset for the given RVA.
std::error_code COFFObjectFile::getRvaPtr(uint32_t Addr, uintptr_t &Res) const {
  for (const SectionRef &S : sections()) {
    const coff_section *Section = getCOFFSection(S);
    uint32_t SectionStart = Section->VirtualAddress;
    uint32_t SectionEnd = Section->VirtualAddress + Section->VirtualSize;
    if (SectionStart <= Addr && Addr < SectionEnd) {
      uint32_t Offset = Addr - SectionStart;
      Res = uintptr_t(base()) + Section->PointerToRawData + Offset;
      return std::error_code();
    }
  }
  return object_error::parse_failed;
}

std::error_code
COFFObjectFile::getRvaAndSizeAsBytes(uint32_t RVA, uint32_t Size,
                                     ArrayRef<uint8_t> &Contents) const {
  for (const SectionRef &S : sections()) {
    const coff_section *Section = getCOFFSection(S);
    uint32_t SectionStart = Section->VirtualAddress;
    // Check if this RVA is within the section bounds. Be careful about integer
    // overflow.
    uint32_t OffsetIntoSection = RVA - SectionStart;
    if (SectionStart <= RVA && OffsetIntoSection < Section->VirtualSize &&
        Size <= Section->VirtualSize - OffsetIntoSection) {
      uintptr_t Begin =
          uintptr_t(base()) + Section->PointerToRawData + OffsetIntoSection;
      Contents =
          ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Begin), Size);
      return std::error_code();
    }
  }
  return object_error::parse_failed;
}

// Returns hint and name fields, assuming \p Rva is pointing to a Hint/Name
// table entry.
std::error_code COFFObjectFile::getHintName(uint32_t Rva, uint16_t &Hint,
                                            StringRef &Name) const {
  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(Rva, IntPtr))
    return EC;
  const uint8_t *Ptr = reinterpret_cast<const uint8_t *>(IntPtr);
  Hint = *reinterpret_cast<const ulittle16_t *>(Ptr);
  Name = StringRef(reinterpret_cast<const char *>(Ptr + 2));
  return std::error_code();
}

std::error_code
COFFObjectFile::getDebugPDBInfo(const debug_directory *DebugDir,
                                const codeview::DebugInfo *&PDBInfo,
                                StringRef &PDBFileName) const {
  ArrayRef<uint8_t> InfoBytes;
  if (std::error_code EC = getRvaAndSizeAsBytes(
          DebugDir->AddressOfRawData, DebugDir->SizeOfData, InfoBytes))
    return EC;
  if (InfoBytes.size() < sizeof(*PDBInfo) + 1)
    return object_error::parse_failed;
  PDBInfo = reinterpret_cast<const codeview::DebugInfo *>(InfoBytes.data());
  InfoBytes = InfoBytes.drop_front(sizeof(*PDBInfo));
  PDBFileName = StringRef(reinterpret_cast<const char *>(InfoBytes.data()),
                          InfoBytes.size());
  // Truncate the name at the first null byte. Ignore any padding.
  PDBFileName = PDBFileName.split('\0').first;
  return std::error_code();
}

std::error_code
COFFObjectFile::getDebugPDBInfo(const codeview::DebugInfo *&PDBInfo,
                                StringRef &PDBFileName) const {
  for (const debug_directory &D : debug_directories())
    if (D.Type == COFF::IMAGE_DEBUG_TYPE_CODEVIEW)
      return getDebugPDBInfo(&D, PDBInfo, PDBFileName);
  // If we get here, there is no PDB info to return.
  PDBInfo = nullptr;
  PDBFileName = StringRef();
  return std::error_code();
}

// Find the import table.
std::error_code COFFObjectFile::initImportTablePtr() {
  // First, we get the RVA of the import table. If the file lacks a pointer to
  // the import table, do nothing.
  const data_directory *DataEntry;
  if (getDataDirectory(COFF::IMPORT_TABLE, DataEntry))
    return std::error_code();

  // Do nothing if the pointer to import table is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return std::error_code();

  uint32_t ImportTableRva = DataEntry->RelativeVirtualAddress;

  // Find the section that contains the RVA. This is needed because the RVA is
  // the import table's memory address which is different from its file offset.
  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(ImportTableRva, IntPtr))
    return EC;
  if (std::error_code EC = checkOffset(Data, IntPtr, DataEntry->Size))
    return EC;
  ImportDirectory = reinterpret_cast<
      const coff_import_directory_table_entry *>(IntPtr);
  return std::error_code();
}

// Initializes DelayImportDirectory and NumberOfDelayImportDirectory.
std::error_code COFFObjectFile::initDelayImportTablePtr() {
  const data_directory *DataEntry;
  if (getDataDirectory(COFF::DELAY_IMPORT_DESCRIPTOR, DataEntry))
    return std::error_code();
  if (DataEntry->RelativeVirtualAddress == 0)
    return std::error_code();

  uint32_t RVA = DataEntry->RelativeVirtualAddress;
  NumberOfDelayImportDirectory = DataEntry->Size /
      sizeof(delay_import_directory_table_entry) - 1;

  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(RVA, IntPtr))
    return EC;
  DelayImportDirectory = reinterpret_cast<
      const delay_import_directory_table_entry *>(IntPtr);
  return std::error_code();
}

// Find the export table.
std::error_code COFFObjectFile::initExportTablePtr() {
  // First, we get the RVA of the export table. If the file lacks a pointer to
  // the export table, do nothing.
  const data_directory *DataEntry;
  if (getDataDirectory(COFF::EXPORT_TABLE, DataEntry))
    return std::error_code();

  // Do nothing if the pointer to export table is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return std::error_code();

  uint32_t ExportTableRva = DataEntry->RelativeVirtualAddress;
  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(ExportTableRva, IntPtr))
    return EC;
  ExportDirectory =
      reinterpret_cast<const export_directory_table_entry *>(IntPtr);
  return std::error_code();
}

std::error_code COFFObjectFile::initBaseRelocPtr() {
  const data_directory *DataEntry;
  if (getDataDirectory(COFF::BASE_RELOCATION_TABLE, DataEntry))
    return std::error_code();
  if (DataEntry->RelativeVirtualAddress == 0)
    return std::error_code();

  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr))
    return EC;
  BaseRelocHeader = reinterpret_cast<const coff_base_reloc_block_header *>(
      IntPtr);
  BaseRelocEnd = reinterpret_cast<coff_base_reloc_block_header *>(
      IntPtr + DataEntry->Size);
  // FIXME: Verify the section containing BaseRelocHeader has at least
  // DataEntry->Size bytes after DataEntry->RelativeVirtualAddress.
  return std::error_code();
}

std::error_code COFFObjectFile::initDebugDirectoryPtr() {
  // Get the RVA of the debug directory. Do nothing if it does not exist.
  const data_directory *DataEntry;
  if (getDataDirectory(COFF::DEBUG_DIRECTORY, DataEntry))
    return std::error_code();

  // Do nothing if the RVA is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return std::error_code();

  // Check that the size is a multiple of the entry size.
  if (DataEntry->Size % sizeof(debug_directory) != 0)
    return object_error::parse_failed;

  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr))
    return EC;
  DebugDirectoryBegin = reinterpret_cast<const debug_directory *>(IntPtr);
  DebugDirectoryEnd = reinterpret_cast<const debug_directory *>(
      IntPtr + DataEntry->Size);
  // FIXME: Verify the section containing DebugDirectoryBegin has at least
  // DataEntry->Size bytes after DataEntry->RelativeVirtualAddress.
  return std::error_code();
}

std::error_code COFFObjectFile::initLoadConfigPtr() {
  // Get the RVA of the debug directory. Do nothing if it does not exist.
  const data_directory *DataEntry;
  if (getDataDirectory(COFF::LOAD_CONFIG_TABLE, DataEntry))
    return std::error_code();

  // Do nothing if the RVA is NULL.
  if (DataEntry->RelativeVirtualAddress == 0)
    return std::error_code();
  uintptr_t IntPtr = 0;
  if (std::error_code EC = getRvaPtr(DataEntry->RelativeVirtualAddress, IntPtr))
    return EC;

  LoadConfig = (const void *)IntPtr;
  return std::error_code();
}

COFFObjectFile::COFFObjectFile(MemoryBufferRef Object, std::error_code &EC)
    : ObjectFile(Binary::ID_COFF, Object), COFFHeader(nullptr),
      COFFBigObjHeader(nullptr), PE32Header(nullptr), PE32PlusHeader(nullptr),
      DataDirectory(nullptr), SectionTable(nullptr), SymbolTable16(nullptr),
      SymbolTable32(nullptr), StringTable(nullptr), StringTableSize(0),
      ImportDirectory(nullptr),
      DelayImportDirectory(nullptr), NumberOfDelayImportDirectory(0),
      ExportDirectory(nullptr), BaseRelocHeader(nullptr), BaseRelocEnd(nullptr),
      DebugDirectoryBegin(nullptr), DebugDirectoryEnd(nullptr) {
  // Check that we at least have enough room for a header.
  if (!checkSize(Data, EC, sizeof(coff_file_header)))
    return;

  // The current location in the file where we are looking at.
  uint64_t CurPtr = 0;

  // PE header is optional and is present only in executables. If it exists,
  // it is placed right after COFF header.
  bool HasPEHeader = false;

  // Check if this is a PE/COFF file.
  if (checkSize(Data, EC, sizeof(dos_header) + sizeof(COFF::PEMagic))) {
    // PE/COFF, seek through MS-DOS compatibility stub and 4-byte
    // PE signature to find 'normal' COFF header.
    const auto *DH = reinterpret_cast<const dos_header *>(base());
    if (DH->Magic[0] == 'M' && DH->Magic[1] == 'Z') {
      CurPtr = DH->AddressOfNewExeHeader;
      // Check the PE magic bytes. ("PE\0\0")
      if (memcmp(base() + CurPtr, COFF::PEMagic, sizeof(COFF::PEMagic)) != 0) {
        EC = object_error::parse_failed;
        return;
      }
      CurPtr += sizeof(COFF::PEMagic); // Skip the PE magic bytes.
      HasPEHeader = true;
    }
  }

  if ((EC = getObject(COFFHeader, Data, base() + CurPtr)))
    return;

  // It might be a bigobj file, let's check.  Note that COFF bigobj and COFF
  // import libraries share a common prefix but bigobj is more restrictive.
  if (!HasPEHeader && COFFHeader->Machine == COFF::IMAGE_FILE_MACHINE_UNKNOWN &&
      COFFHeader->NumberOfSections == uint16_t(0xffff) &&
      checkSize(Data, EC, sizeof(coff_bigobj_file_header))) {
    if ((EC = getObject(COFFBigObjHeader, Data, base() + CurPtr)))
      return;

    // Verify that we are dealing with bigobj.
    if (COFFBigObjHeader->Version >= COFF::BigObjHeader::MinBigObjectVersion &&
        std::memcmp(COFFBigObjHeader->UUID, COFF::BigObjMagic,
                    sizeof(COFF::BigObjMagic)) == 0) {
      COFFHeader = nullptr;
      CurPtr += sizeof(coff_bigobj_file_header);
    } else {
      // It's not a bigobj.
      COFFBigObjHeader = nullptr;
    }
  }
  if (COFFHeader) {
    // The prior checkSize call may have failed.  This isn't a hard error
    // because we were just trying to sniff out bigobj.
    EC = std::error_code();
    CurPtr += sizeof(coff_file_header);

    if (COFFHeader->isImportLibrary())
      return;
  }

  if (HasPEHeader) {
    const pe32_header *Header;
    if ((EC = getObject(Header, Data, base() + CurPtr)))
      return;

    const uint8_t *DataDirAddr;
    uint64_t DataDirSize;
    if (Header->Magic == COFF::PE32Header::PE32) {
      PE32Header = Header;
      DataDirAddr = base() + CurPtr + sizeof(pe32_header);
      DataDirSize = sizeof(data_directory) * PE32Header->NumberOfRvaAndSize;
    } else if (Header->Magic == COFF::PE32Header::PE32_PLUS) {
      PE32PlusHeader = reinterpret_cast<const pe32plus_header *>(Header);
      DataDirAddr = base() + CurPtr + sizeof(pe32plus_header);
      DataDirSize = sizeof(data_directory) * PE32PlusHeader->NumberOfRvaAndSize;
    } else {
      // It's neither PE32 nor PE32+.
      EC = object_error::parse_failed;
      return;
    }
    if ((EC = getObject(DataDirectory, Data, DataDirAddr, DataDirSize)))
      return;
  }

  if (COFFHeader)
    CurPtr += COFFHeader->SizeOfOptionalHeader;

  if ((EC = getObject(SectionTable, Data, base() + CurPtr,
                      (uint64_t)getNumberOfSections() * sizeof(coff_section))))
    return;

  // Initialize the pointer to the symbol table.
  if (getPointerToSymbolTable() != 0) {
    if ((EC = initSymbolTablePtr())) {
      SymbolTable16 = nullptr;
      SymbolTable32 = nullptr;
      StringTable = nullptr;
      StringTableSize = 0;
    }
  } else {
    // We had better not have any symbols if we don't have a symbol table.
    if (getNumberOfSymbols() != 0) {
      EC = object_error::parse_failed;
      return;
    }
  }

  // Initialize the pointer to the beginning of the import table.
  if ((EC = initImportTablePtr()))
    return;
  if ((EC = initDelayImportTablePtr()))
    return;

  // Initialize the pointer to the export table.
  if ((EC = initExportTablePtr()))
    return;

  // Initialize the pointer to the base relocation table.
  if ((EC = initBaseRelocPtr()))
    return;

  // Initialize the pointer to the export table.
  if ((EC = initDebugDirectoryPtr()))
    return;

  if ((EC = initLoadConfigPtr()))
    return;

  EC = std::error_code();
}

basic_symbol_iterator COFFObjectFile::symbol_begin() const {
  DataRefImpl Ret;
  Ret.p = getSymbolTable();
  return basic_symbol_iterator(SymbolRef(Ret, this));
}

basic_symbol_iterator COFFObjectFile::symbol_end() const {
  // The symbol table ends where the string table begins.
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(StringTable);
  return basic_symbol_iterator(SymbolRef(Ret, this));
}

import_directory_iterator COFFObjectFile::import_directory_begin() const {
  if (!ImportDirectory)
    return import_directory_end();
  if (ImportDirectory->isNull())
    return import_directory_end();
  return import_directory_iterator(
      ImportDirectoryEntryRef(ImportDirectory, 0, this));
}

import_directory_iterator COFFObjectFile::import_directory_end() const {
  return import_directory_iterator(
      ImportDirectoryEntryRef(nullptr, -1, this));
}

delay_import_directory_iterator
COFFObjectFile::delay_import_directory_begin() const {
  return delay_import_directory_iterator(
      DelayImportDirectoryEntryRef(DelayImportDirectory, 0, this));
}

delay_import_directory_iterator
COFFObjectFile::delay_import_directory_end() const {
  return delay_import_directory_iterator(
      DelayImportDirectoryEntryRef(
          DelayImportDirectory, NumberOfDelayImportDirectory, this));
}

export_directory_iterator COFFObjectFile::export_directory_begin() const {
  return export_directory_iterator(
      ExportDirectoryEntryRef(ExportDirectory, 0, this));
}

export_directory_iterator COFFObjectFile::export_directory_end() const {
  if (!ExportDirectory)
    return export_directory_iterator(ExportDirectoryEntryRef(nullptr, 0, this));
  ExportDirectoryEntryRef Ref(ExportDirectory,
                              ExportDirectory->AddressTableEntries, this);
  return export_directory_iterator(Ref);
}

section_iterator COFFObjectFile::section_begin() const {
  DataRefImpl Ret;
  Ret.p = reinterpret_cast<uintptr_t>(SectionTable);
  return section_iterator(SectionRef(Ret, this));
}

section_iterator COFFObjectFile::section_end() const {
  DataRefImpl Ret;
  int NumSections =
      COFFHeader && COFFHeader->isImportLibrary() ? 0 : getNumberOfSections();
  Ret.p = reinterpret_cast<uintptr_t>(SectionTable + NumSections);
  return section_iterator(SectionRef(Ret, this));
}

base_reloc_iterator COFFObjectFile::base_reloc_begin() const {
  return base_reloc_iterator(BaseRelocRef(BaseRelocHeader, this));
}

base_reloc_iterator COFFObjectFile::base_reloc_end() const {
  return base_reloc_iterator(BaseRelocRef(BaseRelocEnd, this));
}

uint8_t COFFObjectFile::getBytesInAddress() const {
  return getArch() == Triple::x86_64 || getArch() == Triple::aarch64 ? 8 : 4;
}

StringRef COFFObjectFile::getFileFormatName() const {
  switch(getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return "COFF-i386";
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return "COFF-x86-64";
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return "COFF-ARM";
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return "COFF-ARM64";
  default:
    return "COFF-<unknown arch>";
  }
}

Triple::ArchType COFFObjectFile::getArch() const {
  switch (getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return Triple::x86;
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return Triple::x86_64;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return Triple::thumb;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return Triple::aarch64;
  default:
    return Triple::UnknownArch;
  }
}

Expected<uint64_t> COFFObjectFile::getStartAddress() const {
  if (PE32Header)
    return PE32Header->AddressOfEntryPoint;
  return 0;
}

iterator_range<import_directory_iterator>
COFFObjectFile::import_directories() const {
  return make_range(import_directory_begin(), import_directory_end());
}

iterator_range<delay_import_directory_iterator>
COFFObjectFile::delay_import_directories() const {
  return make_range(delay_import_directory_begin(),
                    delay_import_directory_end());
}

iterator_range<export_directory_iterator>
COFFObjectFile::export_directories() const {
  return make_range(export_directory_begin(), export_directory_end());
}

iterator_range<base_reloc_iterator> COFFObjectFile::base_relocs() const {
  return make_range(base_reloc_begin(), base_reloc_end());
}

std::error_code
COFFObjectFile::getCOFFHeader(const coff_file_header *&Res) const {
  Res = COFFHeader;
  return std::error_code();
}

std::error_code
COFFObjectFile::getCOFFBigObjHeader(const coff_bigobj_file_header *&Res) const {
  Res = COFFBigObjHeader;
  return std::error_code();
}

std::error_code COFFObjectFile::getPE32Header(const pe32_header *&Res) const {
  Res = PE32Header;
  return std::error_code();
}

std::error_code
COFFObjectFile::getPE32PlusHeader(const pe32plus_header *&Res) const {
  Res = PE32PlusHeader;
  return std::error_code();
}

std::error_code
COFFObjectFile::getDataDirectory(uint32_t Index,
                                 const data_directory *&Res) const {
  // Error if there's no data directory or the index is out of range.
  if (!DataDirectory) {
    Res = nullptr;
    return object_error::parse_failed;
  }
  assert(PE32Header || PE32PlusHeader);
  uint32_t NumEnt = PE32Header ? PE32Header->NumberOfRvaAndSize
                               : PE32PlusHeader->NumberOfRvaAndSize;
  if (Index >= NumEnt) {
    Res = nullptr;
    return object_error::parse_failed;
  }
  Res = &DataDirectory[Index];
  return std::error_code();
}

std::error_code COFFObjectFile::getSection(int32_t Index,
                                           const coff_section *&Result) const {
  Result = nullptr;
  if (COFF::isReservedSectionNumber(Index))
    return std::error_code();
  if (static_cast<uint32_t>(Index) <= getNumberOfSections()) {
    // We already verified the section table data, so no need to check again.
    Result = SectionTable + (Index - 1);
    return std::error_code();
  }
  return object_error::parse_failed;
}

std::error_code COFFObjectFile::getSection(StringRef SectionName,
                                           const coff_section *&Result) const {
  Result = nullptr;
  StringRef SecName;
  for (const SectionRef &Section : sections()) {
    if (std::error_code E = Section.getName(SecName))
      return E;
    if (SecName == SectionName) {
      Result = getCOFFSection(Section);
      return std::error_code();
    }
  }
  return object_error::parse_failed;
}

std::error_code COFFObjectFile::getString(uint32_t Offset,
                                          StringRef &Result) const {
  if (StringTableSize <= 4)
    // Tried to get a string from an empty string table.
    return object_error::parse_failed;
  if (Offset >= StringTableSize)
    return object_error::unexpected_eof;
  Result = StringRef(StringTable + Offset);
  return std::error_code();
}

std::error_code COFFObjectFile::getSymbolName(COFFSymbolRef Symbol,
                                              StringRef &Res) const {
  return getSymbolName(Symbol.getGeneric(), Res);
}

std::error_code COFFObjectFile::getSymbolName(const coff_symbol_generic *Symbol,
                                              StringRef &Res) const {
  // Check for string table entry. First 4 bytes are 0.
  if (Symbol->Name.Offset.Zeroes == 0) {
    if (std::error_code EC = getString(Symbol->Name.Offset.Offset, Res))
      return EC;
    return std::error_code();
  }

  if (Symbol->Name.ShortName[COFF::NameSize - 1] == 0)
    // Null terminated, let ::strlen figure out the length.
    Res = StringRef(Symbol->Name.ShortName);
  else
    // Not null terminated, use all 8 bytes.
    Res = StringRef(Symbol->Name.ShortName, COFF::NameSize);
  return std::error_code();
}

ArrayRef<uint8_t>
COFFObjectFile::getSymbolAuxData(COFFSymbolRef Symbol) const {
  const uint8_t *Aux = nullptr;

  size_t SymbolSize = getSymbolTableEntrySize();
  if (Symbol.getNumberOfAuxSymbols() > 0) {
    // AUX data comes immediately after the symbol in COFF
    Aux = reinterpret_cast<const uint8_t *>(Symbol.getRawPtr()) + SymbolSize;
#ifndef NDEBUG
    // Verify that the Aux symbol points to a valid entry in the symbol table.
    uintptr_t Offset = uintptr_t(Aux) - uintptr_t(base());
    if (Offset < getPointerToSymbolTable() ||
        Offset >=
            getPointerToSymbolTable() + (getNumberOfSymbols() * SymbolSize))
      report_fatal_error("Aux Symbol data was outside of symbol table.");

    assert((Offset - getPointerToSymbolTable()) % SymbolSize == 0 &&
           "Aux Symbol data did not point to the beginning of a symbol");
#endif
  }
  return makeArrayRef(Aux, Symbol.getNumberOfAuxSymbols() * SymbolSize);
}

uint32_t COFFObjectFile::getSymbolIndex(COFFSymbolRef Symbol) const {
  uintptr_t Offset =
      reinterpret_cast<uintptr_t>(Symbol.getRawPtr()) - getSymbolTable();
  assert(Offset % getSymbolTableEntrySize() == 0 &&
         "Symbol did not point to the beginning of a symbol");
  size_t Index = Offset / getSymbolTableEntrySize();
  assert(Index < getNumberOfSymbols());
  return Index;
}

std::error_code COFFObjectFile::getSectionName(const coff_section *Sec,
                                               StringRef &Res) const {
  StringRef Name;
  if (Sec->Name[COFF::NameSize - 1] == 0)
    // Null terminated, let ::strlen figure out the length.
    Name = Sec->Name;
  else
    // Not null terminated, use all 8 bytes.
    Name = StringRef(Sec->Name, COFF::NameSize);

  // Check for string table entry. First byte is '/'.
  if (Name.startswith("/")) {
    uint32_t Offset;
    if (Name.startswith("//")) {
      if (decodeBase64StringEntry(Name.substr(2), Offset))
        return object_error::parse_failed;
    } else {
      if (Name.substr(1).getAsInteger(10, Offset))
        return object_error::parse_failed;
    }
    if (std::error_code EC = getString(Offset, Name))
      return EC;
  }

  Res = Name;
  return std::error_code();
}

uint64_t COFFObjectFile::getSectionSize(const coff_section *Sec) const {
  // SizeOfRawData and VirtualSize change what they represent depending on
  // whether or not we have an executable image.
  //
  // For object files, SizeOfRawData contains the size of section's data;
  // VirtualSize should be zero but isn't due to buggy COFF writers.
  //
  // For executables, SizeOfRawData *must* be a multiple of FileAlignment; the
  // actual section size is in VirtualSize.  It is possible for VirtualSize to
  // be greater than SizeOfRawData; the contents past that point should be
  // considered to be zero.
  if (getDOSHeader())
    return std::min(Sec->VirtualSize, Sec->SizeOfRawData);
  return Sec->SizeOfRawData;
}

std::error_code
COFFObjectFile::getSectionContents(const coff_section *Sec,
                                   ArrayRef<uint8_t> &Res) const {
  // In COFF, a virtual section won't have any in-file
  // content, so the file pointer to the content will be zero.
  if (Sec->PointerToRawData == 0)
    return std::error_code();
  // The only thing that we need to verify is that the contents is contained
  // within the file bounds. We don't need to make sure it doesn't cover other
  // data, as there's nothing that says that is not allowed.
  uintptr_t ConStart = uintptr_t(base()) + Sec->PointerToRawData;
  uint32_t SectionSize = getSectionSize(Sec);
  if (checkOffset(Data, ConStart, SectionSize))
    return object_error::parse_failed;
  Res = makeArrayRef(reinterpret_cast<const uint8_t *>(ConStart), SectionSize);
  return std::error_code();
}

const coff_relocation *COFFObjectFile::toRel(DataRefImpl Rel) const {
  return reinterpret_cast<const coff_relocation*>(Rel.p);
}

void COFFObjectFile::moveRelocationNext(DataRefImpl &Rel) const {
  Rel.p = reinterpret_cast<uintptr_t>(
            reinterpret_cast<const coff_relocation*>(Rel.p) + 1);
}

uint64_t COFFObjectFile::getRelocationOffset(DataRefImpl Rel) const {
  const coff_relocation *R = toRel(Rel);
  return R->VirtualAddress;
}

symbol_iterator COFFObjectFile::getRelocationSymbol(DataRefImpl Rel) const {
  const coff_relocation *R = toRel(Rel);
  DataRefImpl Ref;
  if (R->SymbolTableIndex >= getNumberOfSymbols())
    return symbol_end();
  if (SymbolTable16)
    Ref.p = reinterpret_cast<uintptr_t>(SymbolTable16 + R->SymbolTableIndex);
  else if (SymbolTable32)
    Ref.p = reinterpret_cast<uintptr_t>(SymbolTable32 + R->SymbolTableIndex);
  else
    llvm_unreachable("no symbol table pointer!");
  return symbol_iterator(SymbolRef(Ref, this));
}

uint64_t COFFObjectFile::getRelocationType(DataRefImpl Rel) const {
  const coff_relocation* R = toRel(Rel);
  return R->Type;
}

const coff_section *
COFFObjectFile::getCOFFSection(const SectionRef &Section) const {
  return toSec(Section.getRawDataRefImpl());
}

COFFSymbolRef COFFObjectFile::getCOFFSymbol(const DataRefImpl &Ref) const {
  if (SymbolTable16)
    return toSymb<coff_symbol16>(Ref);
  if (SymbolTable32)
    return toSymb<coff_symbol32>(Ref);
  llvm_unreachable("no symbol table pointer!");
}

COFFSymbolRef COFFObjectFile::getCOFFSymbol(const SymbolRef &Symbol) const {
  return getCOFFSymbol(Symbol.getRawDataRefImpl());
}

const coff_relocation *
COFFObjectFile::getCOFFRelocation(const RelocationRef &Reloc) const {
  return toRel(Reloc.getRawDataRefImpl());
}

ArrayRef<coff_relocation>
COFFObjectFile::getRelocations(const coff_section *Sec) const {
  return {getFirstReloc(Sec, Data, base()),
          getNumberOfRelocations(Sec, Data, base())};
}

#define LLVM_COFF_SWITCH_RELOC_TYPE_NAME(reloc_type)                           \
  case COFF::reloc_type:                                                       \
    return #reloc_type;

StringRef COFFObjectFile::getRelocationTypeName(uint16_t Type) const {
  switch (getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ADDR64);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ADDR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_ADDR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_1);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_2);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_3);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_4);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_REL32_5);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SECREL7);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SREL32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_PAIR);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_AMD64_SSPAN32);
    default:
      return "Unknown";
    }
    break;
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_ADDR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_ADDR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH24);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH11);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BLX24);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BLX11);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_MOV32A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_MOV32T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH20T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BRANCH24T);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM_BLX23T);
    default:
      return "Unknown";
    }
    break;
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ADDR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ADDR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_BRANCH26);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_PAGEBASE_REL21);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_REL21);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_PAGEOFFSET_12A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_PAGEOFFSET_12L);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL_LOW12A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL_HIGH12A);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECREL_LOW12L);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_ADDR64);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_BRANCH19);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_ARM64_BRANCH14);
    default:
      return "Unknown";
    }
    break;
  case COFF::IMAGE_FILE_MACHINE_I386:
    switch (Type) {
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_ABSOLUTE);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_DIR16);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_REL16);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_DIR32);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_DIR32NB);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SEG12);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SECTION);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SECREL);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_TOKEN);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_SECREL7);
    LLVM_COFF_SWITCH_RELOC_TYPE_NAME(IMAGE_REL_I386_REL32);
    default:
      return "Unknown";
    }
    break;
  default:
    return "Unknown";
  }
}

#undef LLVM_COFF_SWITCH_RELOC_TYPE_NAME

void COFFObjectFile::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  const coff_relocation *Reloc = toRel(Rel);
  StringRef Res = getRelocationTypeName(Reloc->Type);
  Result.append(Res.begin(), Res.end());
}

bool COFFObjectFile::isRelocatableObject() const {
  return !DataDirectory;
}

StringRef COFFObjectFile::mapDebugSectionName(StringRef Name) const {
  return StringSwitch<StringRef>(Name)
      .Case("eh_fram", "eh_frame")
      .Default(Name);
}

bool ImportDirectoryEntryRef::
operator==(const ImportDirectoryEntryRef &Other) const {
  return ImportTable == Other.ImportTable && Index == Other.Index;
}

void ImportDirectoryEntryRef::moveNext() {
  ++Index;
  if (ImportTable[Index].isNull()) {
    Index = -1;
    ImportTable = nullptr;
  }
}

std::error_code ImportDirectoryEntryRef::getImportTableEntry(
    const coff_import_directory_table_entry *&Result) const {
  return getObject(Result, OwningObject->Data, ImportTable + Index);
}

static imported_symbol_iterator
makeImportedSymbolIterator(const COFFObjectFile *Object,
                           uintptr_t Ptr, int Index) {
  if (Object->getBytesInAddress() == 4) {
    auto *P = reinterpret_cast<const import_lookup_table_entry32 *>(Ptr);
    return imported_symbol_iterator(ImportedSymbolRef(P, Index, Object));
  }
  auto *P = reinterpret_cast<const import_lookup_table_entry64 *>(Ptr);
  return imported_symbol_iterator(ImportedSymbolRef(P, Index, Object));
}

static imported_symbol_iterator
importedSymbolBegin(uint32_t RVA, const COFFObjectFile *Object) {
  uintptr_t IntPtr = 0;
  Object->getRvaPtr(RVA, IntPtr);
  return makeImportedSymbolIterator(Object, IntPtr, 0);
}

static imported_symbol_iterator
importedSymbolEnd(uint32_t RVA, const COFFObjectFile *Object) {
  uintptr_t IntPtr = 0;
  Object->getRvaPtr(RVA, IntPtr);
  // Forward the pointer to the last entry which is null.
  int Index = 0;
  if (Object->getBytesInAddress() == 4) {
    auto *Entry = reinterpret_cast<ulittle32_t *>(IntPtr);
    while (*Entry++)
      ++Index;
  } else {
    auto *Entry = reinterpret_cast<ulittle64_t *>(IntPtr);
    while (*Entry++)
      ++Index;
  }
  return makeImportedSymbolIterator(Object, IntPtr, Index);
}

imported_symbol_iterator
ImportDirectoryEntryRef::imported_symbol_begin() const {
  return importedSymbolBegin(ImportTable[Index].ImportAddressTableRVA,
                             OwningObject);
}

imported_symbol_iterator
ImportDirectoryEntryRef::imported_symbol_end() const {
  return importedSymbolEnd(ImportTable[Index].ImportAddressTableRVA,
                           OwningObject);
}

iterator_range<imported_symbol_iterator>
ImportDirectoryEntryRef::imported_symbols() const {
  return make_range(imported_symbol_begin(), imported_symbol_end());
}

imported_symbol_iterator ImportDirectoryEntryRef::lookup_table_begin() const {
  return importedSymbolBegin(ImportTable[Index].ImportLookupTableRVA,
                             OwningObject);
}

imported_symbol_iterator ImportDirectoryEntryRef::lookup_table_end() const {
  return importedSymbolEnd(ImportTable[Index].ImportLookupTableRVA,
                           OwningObject);
}

iterator_range<imported_symbol_iterator>
ImportDirectoryEntryRef::lookup_table_symbols() const {
  return make_range(lookup_table_begin(), lookup_table_end());
}

std::error_code ImportDirectoryEntryRef::getName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (std::error_code EC =
          OwningObject->getRvaPtr(ImportTable[Index].NameRVA, IntPtr))
    return EC;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return std::error_code();
}

std::error_code
ImportDirectoryEntryRef::getImportLookupTableRVA(uint32_t  &Result) const {
  Result = ImportTable[Index].ImportLookupTableRVA;
  return std::error_code();
}

std::error_code
ImportDirectoryEntryRef::getImportAddressTableRVA(uint32_t &Result) const {
  Result = ImportTable[Index].ImportAddressTableRVA;
  return std::error_code();
}

bool DelayImportDirectoryEntryRef::
operator==(const DelayImportDirectoryEntryRef &Other) const {
  return Table == Other.Table && Index == Other.Index;
}

void DelayImportDirectoryEntryRef::moveNext() {
  ++Index;
}

imported_symbol_iterator
DelayImportDirectoryEntryRef::imported_symbol_begin() const {
  return importedSymbolBegin(Table[Index].DelayImportNameTable,
                             OwningObject);
}

imported_symbol_iterator
DelayImportDirectoryEntryRef::imported_symbol_end() const {
  return importedSymbolEnd(Table[Index].DelayImportNameTable,
                           OwningObject);
}

iterator_range<imported_symbol_iterator>
DelayImportDirectoryEntryRef::imported_symbols() const {
  return make_range(imported_symbol_begin(), imported_symbol_end());
}

std::error_code DelayImportDirectoryEntryRef::getName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (std::error_code EC = OwningObject->getRvaPtr(Table[Index].Name, IntPtr))
    return EC;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return std::error_code();
}

std::error_code DelayImportDirectoryEntryRef::
getDelayImportTable(const delay_import_directory_table_entry *&Result) const {
  Result = Table;
  return std::error_code();
}

std::error_code DelayImportDirectoryEntryRef::
getImportAddress(int AddrIndex, uint64_t &Result) const {
  uint32_t RVA = Table[Index].DelayImportAddressTable +
      AddrIndex * (OwningObject->is64() ? 8 : 4);
  uintptr_t IntPtr = 0;
  if (std::error_code EC = OwningObject->getRvaPtr(RVA, IntPtr))
    return EC;
  if (OwningObject->is64())
    Result = *reinterpret_cast<const ulittle64_t *>(IntPtr);
  else
    Result = *reinterpret_cast<const ulittle32_t *>(IntPtr);
  return std::error_code();
}

bool ExportDirectoryEntryRef::
operator==(const ExportDirectoryEntryRef &Other) const {
  return ExportTable == Other.ExportTable && Index == Other.Index;
}

void ExportDirectoryEntryRef::moveNext() {
  ++Index;
}

// Returns the name of the current export symbol. If the symbol is exported only
// by ordinal, the empty string is set as a result.
std::error_code ExportDirectoryEntryRef::getDllName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (std::error_code EC =
          OwningObject->getRvaPtr(ExportTable->NameRVA, IntPtr))
    return EC;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return std::error_code();
}

// Returns the starting ordinal number.
std::error_code
ExportDirectoryEntryRef::getOrdinalBase(uint32_t &Result) const {
  Result = ExportTable->OrdinalBase;
  return std::error_code();
}

// Returns the export ordinal of the current export symbol.
std::error_code ExportDirectoryEntryRef::getOrdinal(uint32_t &Result) const {
  Result = ExportTable->OrdinalBase + Index;
  return std::error_code();
}

// Returns the address of the current export symbol.
std::error_code ExportDirectoryEntryRef::getExportRVA(uint32_t &Result) const {
  uintptr_t IntPtr = 0;
  if (std::error_code EC =
          OwningObject->getRvaPtr(ExportTable->ExportAddressTableRVA, IntPtr))
    return EC;
  const export_address_table_entry *entry =
      reinterpret_cast<const export_address_table_entry *>(IntPtr);
  Result = entry[Index].ExportRVA;
  return std::error_code();
}

// Returns the name of the current export symbol. If the symbol is exported only
// by ordinal, the empty string is set as a result.
std::error_code
ExportDirectoryEntryRef::getSymbolName(StringRef &Result) const {
  uintptr_t IntPtr = 0;
  if (std::error_code EC =
          OwningObject->getRvaPtr(ExportTable->OrdinalTableRVA, IntPtr))
    return EC;
  const ulittle16_t *Start = reinterpret_cast<const ulittle16_t *>(IntPtr);

  uint32_t NumEntries = ExportTable->NumberOfNamePointers;
  int Offset = 0;
  for (const ulittle16_t *I = Start, *E = Start + NumEntries;
       I < E; ++I, ++Offset) {
    if (*I != Index)
      continue;
    if (std::error_code EC =
            OwningObject->getRvaPtr(ExportTable->NamePointerRVA, IntPtr))
      return EC;
    const ulittle32_t *NamePtr = reinterpret_cast<const ulittle32_t *>(IntPtr);
    if (std::error_code EC = OwningObject->getRvaPtr(NamePtr[Offset], IntPtr))
      return EC;
    Result = StringRef(reinterpret_cast<const char *>(IntPtr));
    return std::error_code();
  }
  Result = "";
  return std::error_code();
}

std::error_code ExportDirectoryEntryRef::isForwarder(bool &Result) const {
  const data_directory *DataEntry;
  if (auto EC = OwningObject->getDataDirectory(COFF::EXPORT_TABLE, DataEntry))
    return EC;
  uint32_t RVA;
  if (auto EC = getExportRVA(RVA))
    return EC;
  uint32_t Begin = DataEntry->RelativeVirtualAddress;
  uint32_t End = DataEntry->RelativeVirtualAddress + DataEntry->Size;
  Result = (Begin <= RVA && RVA < End);
  return std::error_code();
}

std::error_code ExportDirectoryEntryRef::getForwardTo(StringRef &Result) const {
  uint32_t RVA;
  if (auto EC = getExportRVA(RVA))
    return EC;
  uintptr_t IntPtr = 0;
  if (auto EC = OwningObject->getRvaPtr(RVA, IntPtr))
    return EC;
  Result = StringRef(reinterpret_cast<const char *>(IntPtr));
  return std::error_code();
}

bool ImportedSymbolRef::
operator==(const ImportedSymbolRef &Other) const {
  return Entry32 == Other.Entry32 && Entry64 == Other.Entry64
      && Index == Other.Index;
}

void ImportedSymbolRef::moveNext() {
  ++Index;
}

std::error_code
ImportedSymbolRef::getSymbolName(StringRef &Result) const {
  uint32_t RVA;
  if (Entry32) {
    // If a symbol is imported only by ordinal, it has no name.
    if (Entry32[Index].isOrdinal())
      return std::error_code();
    RVA = Entry32[Index].getHintNameRVA();
  } else {
    if (Entry64[Index].isOrdinal())
      return std::error_code();
    RVA = Entry64[Index].getHintNameRVA();
  }
  uintptr_t IntPtr = 0;
  if (std::error_code EC = OwningObject->getRvaPtr(RVA, IntPtr))
    return EC;
  // +2 because the first two bytes is hint.
  Result = StringRef(reinterpret_cast<const char *>(IntPtr + 2));
  return std::error_code();
}

std::error_code ImportedSymbolRef::isOrdinal(bool &Result) const {
  if (Entry32)
    Result = Entry32[Index].isOrdinal();
  else
    Result = Entry64[Index].isOrdinal();
  return std::error_code();
}

std::error_code ImportedSymbolRef::getHintNameRVA(uint32_t &Result) const {
  if (Entry32)
    Result = Entry32[Index].getHintNameRVA();
  else
    Result = Entry64[Index].getHintNameRVA();
  return std::error_code();
}

std::error_code ImportedSymbolRef::getOrdinal(uint16_t &Result) const {
  uint32_t RVA;
  if (Entry32) {
    if (Entry32[Index].isOrdinal()) {
      Result = Entry32[Index].getOrdinal();
      return std::error_code();
    }
    RVA = Entry32[Index].getHintNameRVA();
  } else {
    if (Entry64[Index].isOrdinal()) {
      Result = Entry64[Index].getOrdinal();
      return std::error_code();
    }
    RVA = Entry64[Index].getHintNameRVA();
  }
  uintptr_t IntPtr = 0;
  if (std::error_code EC = OwningObject->getRvaPtr(RVA, IntPtr))
    return EC;
  Result = *reinterpret_cast<const ulittle16_t *>(IntPtr);
  return std::error_code();
}

Expected<std::unique_ptr<COFFObjectFile>>
ObjectFile::createCOFFObjectFile(MemoryBufferRef Object) {
  std::error_code EC;
  std::unique_ptr<COFFObjectFile> Ret(new COFFObjectFile(Object, EC));
  if (EC)
    return errorCodeToError(EC);
  return std::move(Ret);
}

bool BaseRelocRef::operator==(const BaseRelocRef &Other) const {
  return Header == Other.Header && Index == Other.Index;
}

void BaseRelocRef::moveNext() {
  // Header->BlockSize is the size of the current block, including the
  // size of the header itself.
  uint32_t Size = sizeof(*Header) +
      sizeof(coff_base_reloc_block_entry) * (Index + 1);
  if (Size == Header->BlockSize) {
    // .reloc contains a list of base relocation blocks. Each block
    // consists of the header followed by entries. The header contains
    // how many entories will follow. When we reach the end of the
    // current block, proceed to the next block.
    Header = reinterpret_cast<const coff_base_reloc_block_header *>(
        reinterpret_cast<const uint8_t *>(Header) + Size);
    Index = 0;
  } else {
    ++Index;
  }
}

std::error_code BaseRelocRef::getType(uint8_t &Type) const {
  auto *Entry = reinterpret_cast<const coff_base_reloc_block_entry *>(Header + 1);
  Type = Entry[Index].getType();
  return std::error_code();
}

std::error_code BaseRelocRef::getRVA(uint32_t &Result) const {
  auto *Entry = reinterpret_cast<const coff_base_reloc_block_entry *>(Header + 1);
  Result = Header->PageRVA + Entry[Index].getOffset();
  return std::error_code();
}

#define RETURN_IF_ERROR(E)                                                     \
  if (E)                                                                       \
    return E;

Expected<ArrayRef<UTF16>>
ResourceSectionRef::getDirStringAtOffset(uint32_t Offset) {
  BinaryStreamReader Reader = BinaryStreamReader(BBS);
  Reader.setOffset(Offset);
  uint16_t Length;
  RETURN_IF_ERROR(Reader.readInteger(Length));
  ArrayRef<UTF16> RawDirString;
  RETURN_IF_ERROR(Reader.readArray(RawDirString, Length));
  return RawDirString;
}

Expected<ArrayRef<UTF16>>
ResourceSectionRef::getEntryNameString(const coff_resource_dir_entry &Entry) {
  return getDirStringAtOffset(Entry.Identifier.getNameOffset());
}

Expected<const coff_resource_dir_table &>
ResourceSectionRef::getTableAtOffset(uint32_t Offset) {
  const coff_resource_dir_table *Table = nullptr;

  BinaryStreamReader Reader(BBS);
  Reader.setOffset(Offset);
  RETURN_IF_ERROR(Reader.readObject(Table));
  assert(Table != nullptr);
  return *Table;
}

Expected<const coff_resource_dir_table &>
ResourceSectionRef::getEntrySubDir(const coff_resource_dir_entry &Entry) {
  return getTableAtOffset(Entry.Offset.value());
}

Expected<const coff_resource_dir_table &> ResourceSectionRef::getBaseTable() {
  return getTableAtOffset(0);
}
