//===- Object.cpp ---------------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Object.h"
#include "llvm-objcopy.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace llvm {
namespace objcopy {
namespace elf {

using namespace object;
using namespace ELF;

template <class ELFT> void ELFWriter<ELFT>::writePhdr(const Segment &Seg) {
  uint8_t *B = Buf.getBufferStart();
  B += Obj.ProgramHdrSegment.Offset + Seg.Index * sizeof(Elf_Phdr);
  Elf_Phdr &Phdr = *reinterpret_cast<Elf_Phdr *>(B);
  Phdr.p_type = Seg.Type;
  Phdr.p_flags = Seg.Flags;
  Phdr.p_offset = Seg.Offset;
  Phdr.p_vaddr = Seg.VAddr;
  Phdr.p_paddr = Seg.PAddr;
  Phdr.p_filesz = Seg.FileSize;
  Phdr.p_memsz = Seg.MemSize;
  Phdr.p_align = Seg.Align;
}

void SectionBase::removeSectionReferences(const SectionBase *Sec) {}
void SectionBase::removeSymbols(function_ref<bool(const Symbol &)> ToRemove) {}
void SectionBase::initialize(SectionTableRef SecTable) {}
void SectionBase::finalize() {}
void SectionBase::markSymbols() {}

template <class ELFT> void ELFWriter<ELFT>::writeShdr(const SectionBase &Sec) {
  uint8_t *B = Buf.getBufferStart();
  B += Sec.HeaderOffset;
  Elf_Shdr &Shdr = *reinterpret_cast<Elf_Shdr *>(B);
  Shdr.sh_name = Sec.NameIndex;
  Shdr.sh_type = Sec.Type;
  Shdr.sh_flags = Sec.Flags;
  Shdr.sh_addr = Sec.Addr;
  Shdr.sh_offset = Sec.Offset;
  Shdr.sh_size = Sec.Size;
  Shdr.sh_link = Sec.Link;
  Shdr.sh_info = Sec.Info;
  Shdr.sh_addralign = Sec.Align;
  Shdr.sh_entsize = Sec.EntrySize;
}

template <class ELFT> void ELFSectionSizer<ELFT>::visit(Section &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(OwnedDataSection &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(StringTableSection &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(DynamicRelocationSection &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(SymbolTableSection &Sec) {
  Sec.EntrySize = sizeof(Elf_Sym);
  Sec.Size = Sec.Symbols.size() * Sec.EntrySize;
  // Align to the largest field in Elf_Sym.
  Sec.Align = ELFT::Is64Bits ? sizeof(Elf_Xword) : sizeof(Elf_Word);
}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(RelocationSection &Sec) {
  Sec.EntrySize = Sec.Type == SHT_REL ? sizeof(Elf_Rel) : sizeof(Elf_Rela);
  Sec.Size = Sec.Relocations.size() * Sec.EntrySize;
  // Align to the largest field in Elf_Rel(a).
  Sec.Align = ELFT::Is64Bits ? sizeof(Elf_Xword) : sizeof(Elf_Word);
}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(GnuDebugLinkSection &Sec) {}

template <class ELFT> void ELFSectionSizer<ELFT>::visit(GroupSection &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(SectionIndexSection &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(CompressedSection &Sec) {}

template <class ELFT>
void ELFSectionSizer<ELFT>::visit(DecompressedSection &Sec) {}

void BinarySectionWriter::visit(const SectionIndexSection &Sec) {
  error("Cannot write symbol section index table '" + Sec.Name + "' ");
}

void BinarySectionWriter::visit(const SymbolTableSection &Sec) {
  error("Cannot write symbol table '" + Sec.Name + "' out to binary");
}

void BinarySectionWriter::visit(const RelocationSection &Sec) {
  error("Cannot write relocation section '" + Sec.Name + "' out to binary");
}

void BinarySectionWriter::visit(const GnuDebugLinkSection &Sec) {
  error("Cannot write '" + Sec.Name + "' out to binary");
}

void BinarySectionWriter::visit(const GroupSection &Sec) {
  error("Cannot write '" + Sec.Name + "' out to binary");
}

void SectionWriter::visit(const Section &Sec) {
  if (Sec.Type == SHT_NOBITS)
    return;
  uint8_t *Buf = Out.getBufferStart() + Sec.Offset;
  llvm::copy(Sec.Contents, Buf);
}

void Section::accept(SectionVisitor &Visitor) const { Visitor.visit(*this); }

void Section::accept(MutableSectionVisitor &Visitor) { Visitor.visit(*this); }

void SectionWriter::visit(const OwnedDataSection &Sec) {
  uint8_t *Buf = Out.getBufferStart() + Sec.Offset;
  llvm::copy(Sec.Data, Buf);
}

static const std::vector<uint8_t> ZlibGnuMagic = {'Z', 'L', 'I', 'B'};

static bool isDataGnuCompressed(ArrayRef<uint8_t> Data) {
  return Data.size() > ZlibGnuMagic.size() &&
         std::equal(ZlibGnuMagic.begin(), ZlibGnuMagic.end(), Data.data());
}

template <class ELFT>
static std::tuple<uint64_t, uint64_t>
getDecompressedSizeAndAlignment(ArrayRef<uint8_t> Data) {
  const bool IsGnuDebug = isDataGnuCompressed(Data);
  const uint64_t DecompressedSize =
      IsGnuDebug
          ? support::endian::read64be(reinterpret_cast<const uint64_t *>(
                Data.data() + ZlibGnuMagic.size()))
          : reinterpret_cast<const Elf_Chdr_Impl<ELFT> *>(Data.data())->ch_size;
  const uint64_t DecompressedAlign =
      IsGnuDebug ? 1
                 : reinterpret_cast<const Elf_Chdr_Impl<ELFT> *>(Data.data())
                       ->ch_addralign;

  return std::make_tuple(DecompressedSize, DecompressedAlign);
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const DecompressedSection &Sec) {
  uint8_t *Buf = Out.getBufferStart() + Sec.Offset;

  if (!zlib::isAvailable()) {
    std::copy(Sec.OriginalData.begin(), Sec.OriginalData.end(), Buf);
    return;
  }

  const size_t DataOffset = isDataGnuCompressed(Sec.OriginalData)
                                ? (ZlibGnuMagic.size() + sizeof(Sec.Size))
                                : sizeof(Elf_Chdr_Impl<ELFT>);

  StringRef CompressedContent(
      reinterpret_cast<const char *>(Sec.OriginalData.data()) + DataOffset,
      Sec.OriginalData.size() - DataOffset);

  SmallVector<char, 128> DecompressedContent;
  if (Error E = zlib::uncompress(CompressedContent, DecompressedContent,
                                 static_cast<size_t>(Sec.Size)))
    reportError(Sec.Name, std::move(E));

  std::copy(DecompressedContent.begin(), DecompressedContent.end(), Buf);
}

void BinarySectionWriter::visit(const DecompressedSection &Sec) {
  error("Cannot write compressed section '" + Sec.Name + "' ");
}

void DecompressedSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void DecompressedSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

void OwnedDataSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void OwnedDataSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

void BinarySectionWriter::visit(const CompressedSection &Sec) {
  error("Cannot write compressed section '" + Sec.Name + "' ");
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const CompressedSection &Sec) {
  uint8_t *Buf = Out.getBufferStart();
  Buf += Sec.Offset;

  if (Sec.CompressionType == DebugCompressionType::None) {
    std::copy(Sec.OriginalData.begin(), Sec.OriginalData.end(), Buf);
    return;
  }

  if (Sec.CompressionType == DebugCompressionType::GNU) {
    const char *Magic = "ZLIB";
    memcpy(Buf, Magic, strlen(Magic));
    Buf += strlen(Magic);
    const uint64_t DecompressedSize =
        support::endian::read64be(&Sec.DecompressedSize);
    memcpy(Buf, &DecompressedSize, sizeof(DecompressedSize));
    Buf += sizeof(DecompressedSize);
  } else {
    Elf_Chdr_Impl<ELFT> Chdr;
    Chdr.ch_type = ELF::ELFCOMPRESS_ZLIB;
    Chdr.ch_size = Sec.DecompressedSize;
    Chdr.ch_addralign = Sec.DecompressedAlign;
    memcpy(Buf, &Chdr, sizeof(Chdr));
    Buf += sizeof(Chdr);
  }

  std::copy(Sec.CompressedData.begin(), Sec.CompressedData.end(), Buf);
}

CompressedSection::CompressedSection(const SectionBase &Sec,
                                     DebugCompressionType CompressionType)
    : SectionBase(Sec), CompressionType(CompressionType),
      DecompressedSize(Sec.OriginalData.size()), DecompressedAlign(Sec.Align) {

  if (!zlib::isAvailable()) {
    CompressionType = DebugCompressionType::None;
    return;
  }

  if (Error E = zlib::compress(
          StringRef(reinterpret_cast<const char *>(OriginalData.data()),
                    OriginalData.size()),
          CompressedData))
    reportError(Name, std::move(E));

  size_t ChdrSize;
  if (CompressionType == DebugCompressionType::GNU) {
    Name = ".z" + Sec.Name.substr(1);
    ChdrSize = sizeof("ZLIB") - 1 + sizeof(uint64_t);
  } else {
    Flags |= ELF::SHF_COMPRESSED;
    ChdrSize =
        std::max(std::max(sizeof(object::Elf_Chdr_Impl<object::ELF64LE>),
                          sizeof(object::Elf_Chdr_Impl<object::ELF64BE>)),
                 std::max(sizeof(object::Elf_Chdr_Impl<object::ELF32LE>),
                          sizeof(object::Elf_Chdr_Impl<object::ELF32BE>)));
  }
  Size = ChdrSize + CompressedData.size();
  Align = 8;
}

CompressedSection::CompressedSection(ArrayRef<uint8_t> CompressedData,
                                     uint64_t DecompressedSize,
                                     uint64_t DecompressedAlign)
    : CompressionType(DebugCompressionType::None),
      DecompressedSize(DecompressedSize), DecompressedAlign(DecompressedAlign) {
  OriginalData = CompressedData;
}

void CompressedSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void CompressedSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

void StringTableSection::addString(StringRef Name) {
  StrTabBuilder.add(Name);
  Size = StrTabBuilder.getSize();
}

uint32_t StringTableSection::findIndex(StringRef Name) const {
  return StrTabBuilder.getOffset(Name);
}

void StringTableSection::finalize() { StrTabBuilder.finalize(); }

void SectionWriter::visit(const StringTableSection &Sec) {
  Sec.StrTabBuilder.write(Out.getBufferStart() + Sec.Offset);
}

void StringTableSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void StringTableSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const SectionIndexSection &Sec) {
  uint8_t *Buf = Out.getBufferStart() + Sec.Offset;
  auto *IndexesBuffer = reinterpret_cast<Elf_Word *>(Buf);
  llvm::copy(Sec.Indexes, IndexesBuffer);
}

void SectionIndexSection::initialize(SectionTableRef SecTable) {
  Size = 0;
  setSymTab(SecTable.getSectionOfType<SymbolTableSection>(
      Link,
      "Link field value " + Twine(Link) + " in section " + Name + " is invalid",
      "Link field value " + Twine(Link) + " in section " + Name +
          " is not a symbol table"));
  Symbols->setShndxTable(this);
}

void SectionIndexSection::finalize() { Link = Symbols->Index; }

void SectionIndexSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void SectionIndexSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

static bool isValidReservedSectionIndex(uint16_t Index, uint16_t Machine) {
  switch (Index) {
  case SHN_ABS:
  case SHN_COMMON:
    return true;
  }
  if (Machine == EM_HEXAGON) {
    switch (Index) {
    case SHN_HEXAGON_SCOMMON:
    case SHN_HEXAGON_SCOMMON_2:
    case SHN_HEXAGON_SCOMMON_4:
    case SHN_HEXAGON_SCOMMON_8:
      return true;
    }
  }
  return false;
}

// Large indexes force us to clarify exactly what this function should do. This
// function should return the value that will appear in st_shndx when written
// out.
uint16_t Symbol::getShndx() const {
  if (DefinedIn != nullptr) {
    if (DefinedIn->Index >= SHN_LORESERVE)
      return SHN_XINDEX;
    return DefinedIn->Index;
  }
  switch (ShndxType) {
  // This means that we don't have a defined section but we do need to
  // output a legitimate section index.
  case SYMBOL_SIMPLE_INDEX:
    return SHN_UNDEF;
  case SYMBOL_ABS:
  case SYMBOL_COMMON:
  case SYMBOL_HEXAGON_SCOMMON:
  case SYMBOL_HEXAGON_SCOMMON_2:
  case SYMBOL_HEXAGON_SCOMMON_4:
  case SYMBOL_HEXAGON_SCOMMON_8:
  case SYMBOL_XINDEX:
    return static_cast<uint16_t>(ShndxType);
  }
  llvm_unreachable("Symbol with invalid ShndxType encountered");
}

bool Symbol::isCommon() const { return getShndx() == SHN_COMMON; }

void SymbolTableSection::assignIndices() {
  uint32_t Index = 0;
  for (auto &Sym : Symbols)
    Sym->Index = Index++;
}

void SymbolTableSection::addSymbol(Twine Name, uint8_t Bind, uint8_t Type,
                                   SectionBase *DefinedIn, uint64_t Value,
                                   uint8_t Visibility, uint16_t Shndx,
                                   uint64_t Size) {
  Symbol Sym;
  Sym.Name = Name.str();
  Sym.Binding = Bind;
  Sym.Type = Type;
  Sym.DefinedIn = DefinedIn;
  if (DefinedIn != nullptr)
    DefinedIn->HasSymbol = true;
  if (DefinedIn == nullptr) {
    if (Shndx >= SHN_LORESERVE)
      Sym.ShndxType = static_cast<SymbolShndxType>(Shndx);
    else
      Sym.ShndxType = SYMBOL_SIMPLE_INDEX;
  }
  Sym.Value = Value;
  Sym.Visibility = Visibility;
  Sym.Size = Size;
  Sym.Index = Symbols.size();
  Symbols.emplace_back(llvm::make_unique<Symbol>(Sym));
  Size += this->EntrySize;
}

void SymbolTableSection::removeSectionReferences(const SectionBase *Sec) {
  if (SectionIndexTable == Sec)
    SectionIndexTable = nullptr;
  if (SymbolNames == Sec) {
    error("String table " + SymbolNames->Name +
          " cannot be removed because it is referenced by the symbol table " +
          this->Name);
  }
  removeSymbols([Sec](const Symbol &Sym) { return Sym.DefinedIn == Sec; });
}

void SymbolTableSection::updateSymbols(function_ref<void(Symbol &)> Callable) {
  std::for_each(std::begin(Symbols) + 1, std::end(Symbols),
                [Callable](SymPtr &Sym) { Callable(*Sym); });
  std::stable_partition(
      std::begin(Symbols), std::end(Symbols),
      [](const SymPtr &Sym) { return Sym->Binding == STB_LOCAL; });
  assignIndices();
}

void SymbolTableSection::removeSymbols(
    function_ref<bool(const Symbol &)> ToRemove) {
  Symbols.erase(
      std::remove_if(std::begin(Symbols) + 1, std::end(Symbols),
                     [ToRemove](const SymPtr &Sym) { return ToRemove(*Sym); }),
      std::end(Symbols));
  Size = Symbols.size() * EntrySize;
  assignIndices();
}

void SymbolTableSection::initialize(SectionTableRef SecTable) {
  Size = 0;
  setStrTab(SecTable.getSectionOfType<StringTableSection>(
      Link,
      "Symbol table has link index of " + Twine(Link) +
          " which is not a valid index",
      "Symbol table has link index of " + Twine(Link) +
          " which is not a string table"));
}

void SymbolTableSection::finalize() {
  // Make sure SymbolNames is finalized before getting name indexes.
  SymbolNames->finalize();

  uint32_t MaxLocalIndex = 0;
  for (auto &Sym : Symbols) {
    Sym->NameIndex = SymbolNames->findIndex(Sym->Name);
    if (Sym->Binding == STB_LOCAL)
      MaxLocalIndex = std::max(MaxLocalIndex, Sym->Index);
  }
  // Now we need to set the Link and Info fields.
  Link = SymbolNames->Index;
  Info = MaxLocalIndex + 1;
}

void SymbolTableSection::prepareForLayout() {
  // Add all potential section indexes before file layout so that the section
  // index section has the approprite size.
  if (SectionIndexTable != nullptr) {
    for (const auto &Sym : Symbols) {
      if (Sym->DefinedIn != nullptr && Sym->DefinedIn->Index >= SHN_LORESERVE)
        SectionIndexTable->addIndex(Sym->DefinedIn->Index);
      else
        SectionIndexTable->addIndex(SHN_UNDEF);
    }
  }
  // Add all of our strings to SymbolNames so that SymbolNames has the right
  // size before layout is decided.
  for (auto &Sym : Symbols)
    SymbolNames->addString(Sym->Name);
}

const Symbol *SymbolTableSection::getSymbolByIndex(uint32_t Index) const {
  if (Symbols.size() <= Index)
    error("Invalid symbol index: " + Twine(Index));
  return Symbols[Index].get();
}

Symbol *SymbolTableSection::getSymbolByIndex(uint32_t Index) {
  return const_cast<Symbol *>(
      static_cast<const SymbolTableSection *>(this)->getSymbolByIndex(Index));
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const SymbolTableSection &Sec) {
  uint8_t *Buf = Out.getBufferStart();
  Buf += Sec.Offset;
  Elf_Sym *Sym = reinterpret_cast<Elf_Sym *>(Buf);
  // Loop though symbols setting each entry of the symbol table.
  for (auto &Symbol : Sec.Symbols) {
    Sym->st_name = Symbol->NameIndex;
    Sym->st_value = Symbol->Value;
    Sym->st_size = Symbol->Size;
    Sym->st_other = Symbol->Visibility;
    Sym->setBinding(Symbol->Binding);
    Sym->setType(Symbol->Type);
    Sym->st_shndx = Symbol->getShndx();
    ++Sym;
  }
}

void SymbolTableSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void SymbolTableSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

template <class SymTabType>
void RelocSectionWithSymtabBase<SymTabType>::removeSectionReferences(
    const SectionBase *Sec) {
  if (Symbols == Sec) {
    error("Symbol table " + Symbols->Name +
          " cannot be removed because it is "
          "referenced by the relocation "
          "section " +
          this->Name);
  }
}

template <class SymTabType>
void RelocSectionWithSymtabBase<SymTabType>::initialize(
    SectionTableRef SecTable) {
  if (Link != SHN_UNDEF)
    setSymTab(SecTable.getSectionOfType<SymTabType>(
        Link,
        "Link field value " + Twine(Link) + " in section " + Name +
            " is invalid",
        "Link field value " + Twine(Link) + " in section " + Name +
            " is not a symbol table"));

  if (Info != SHN_UNDEF)
    setSection(SecTable.getSection(Info, "Info field value " + Twine(Info) +
                                             " in section " + Name +
                                             " is invalid"));
  else
    setSection(nullptr);
}

template <class SymTabType>
void RelocSectionWithSymtabBase<SymTabType>::finalize() {
  this->Link = Symbols ? Symbols->Index : 0;

  if (SecToApplyRel != nullptr)
    this->Info = SecToApplyRel->Index;
}

template <class ELFT>
static void setAddend(Elf_Rel_Impl<ELFT, false> &Rel, uint64_t Addend) {}

template <class ELFT>
static void setAddend(Elf_Rel_Impl<ELFT, true> &Rela, uint64_t Addend) {
  Rela.r_addend = Addend;
}

template <class RelRange, class T>
static void writeRel(const RelRange &Relocations, T *Buf) {
  for (const auto &Reloc : Relocations) {
    Buf->r_offset = Reloc.Offset;
    setAddend(*Buf, Reloc.Addend);
    Buf->setSymbolAndType(Reloc.RelocSymbol->Index, Reloc.Type, false);
    ++Buf;
  }
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const RelocationSection &Sec) {
  uint8_t *Buf = Out.getBufferStart() + Sec.Offset;
  if (Sec.Type == SHT_REL)
    writeRel(Sec.Relocations, reinterpret_cast<Elf_Rel *>(Buf));
  else
    writeRel(Sec.Relocations, reinterpret_cast<Elf_Rela *>(Buf));
}

void RelocationSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void RelocationSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

void RelocationSection::removeSymbols(
    function_ref<bool(const Symbol &)> ToRemove) {
  for (const Relocation &Reloc : Relocations)
    if (ToRemove(*Reloc.RelocSymbol))
      error("not stripping symbol '" + Reloc.RelocSymbol->Name +
            "' because it is named in a relocation");
}

void RelocationSection::markSymbols() {
  for (const Relocation &Reloc : Relocations)
    Reloc.RelocSymbol->Referenced = true;
}

void SectionWriter::visit(const DynamicRelocationSection &Sec) {
  llvm::copy(Sec.Contents,
            Out.getBufferStart() + Sec.Offset);
}

void DynamicRelocationSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void DynamicRelocationSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

void Section::removeSectionReferences(const SectionBase *Sec) {
  if (LinkSection == Sec) {
    error("Section " + LinkSection->Name +
          " cannot be removed because it is "
          "referenced by the section " +
          this->Name);
  }
}

void GroupSection::finalize() {
  this->Info = Sym->Index;
  this->Link = SymTab->Index;
}

void GroupSection::removeSymbols(function_ref<bool(const Symbol &)> ToRemove) {
  if (ToRemove(*Sym)) {
    error("Symbol " + Sym->Name +
          " cannot be removed because it is "
          "referenced by the section " +
          this->Name + "[" + Twine(this->Index) + "]");
  }
}

void GroupSection::markSymbols() {
  if (Sym)
    Sym->Referenced = true;
}

void Section::initialize(SectionTableRef SecTable) {
  if (Link != ELF::SHN_UNDEF) {
    LinkSection =
        SecTable.getSection(Link, "Link field value " + Twine(Link) +
                                      " in section " + Name + " is invalid");
    if (LinkSection->Type == ELF::SHT_SYMTAB)
      LinkSection = nullptr;
  }
}

void Section::finalize() { this->Link = LinkSection ? LinkSection->Index : 0; }

void GnuDebugLinkSection::init(StringRef File, StringRef Data) {
  FileName = sys::path::filename(File);
  // The format for the .gnu_debuglink starts with the file name and is
  // followed by a null terminator and then the CRC32 of the file. The CRC32
  // should be 4 byte aligned. So we add the FileName size, a 1 for the null
  // byte, and then finally push the size to alignment and add 4.
  Size = alignTo(FileName.size() + 1, 4) + 4;
  // The CRC32 will only be aligned if we align the whole section.
  Align = 4;
  Type = ELF::SHT_PROGBITS;
  Name = ".gnu_debuglink";
  // For sections not found in segments, OriginalOffset is only used to
  // establish the order that sections should go in. By using the maximum
  // possible offset we cause this section to wind up at the end.
  OriginalOffset = std::numeric_limits<uint64_t>::max();
  JamCRC CRC;
  CRC.update(ArrayRef<char>(Data.data(), Data.size()));
  // The CRC32 value needs to be complemented because the JamCRC dosn't
  // finalize the CRC32 value. It also dosn't negate the initial CRC32 value
  // but it starts by default at 0xFFFFFFFF which is the complement of zero.
  CRC32 = ~CRC.getCRC();
}

GnuDebugLinkSection::GnuDebugLinkSection(StringRef File) : FileName(File) {
  // Read in the file to compute the CRC of it.
  auto DebugOrErr = MemoryBuffer::getFile(File);
  if (!DebugOrErr)
    error("'" + File + "': " + DebugOrErr.getError().message());
  auto Debug = std::move(*DebugOrErr);
  init(File, Debug->getBuffer());
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const GnuDebugLinkSection &Sec) {
  auto Buf = Out.getBufferStart() + Sec.Offset;
  char *File = reinterpret_cast<char *>(Buf);
  Elf_Word *CRC =
      reinterpret_cast<Elf_Word *>(Buf + Sec.Size - sizeof(Elf_Word));
  *CRC = Sec.CRC32;
  llvm::copy(Sec.FileName, File);
}

void GnuDebugLinkSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void GnuDebugLinkSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

template <class ELFT>
void ELFSectionWriter<ELFT>::visit(const GroupSection &Sec) {
  ELF::Elf32_Word *Buf =
      reinterpret_cast<ELF::Elf32_Word *>(Out.getBufferStart() + Sec.Offset);
  *Buf++ = Sec.FlagWord;
  for (const auto *S : Sec.GroupMembers)
    support::endian::write32<ELFT::TargetEndianness>(Buf++, S->Index);
}

void GroupSection::accept(SectionVisitor &Visitor) const {
  Visitor.visit(*this);
}

void GroupSection::accept(MutableSectionVisitor &Visitor) {
  Visitor.visit(*this);
}

// Returns true IFF a section is wholly inside the range of a segment
static bool sectionWithinSegment(const SectionBase &Section,
                                 const Segment &Segment) {
  // If a section is empty it should be treated like it has a size of 1. This is
  // to clarify the case when an empty section lies on a boundary between two
  // segments and ensures that the section "belongs" to the second segment and
  // not the first.
  uint64_t SecSize = Section.Size ? Section.Size : 1;
  return Segment.Offset <= Section.OriginalOffset &&
         Segment.Offset + Segment.FileSize >= Section.OriginalOffset + SecSize;
}

// Returns true IFF a segment's original offset is inside of another segment's
// range.
static bool segmentOverlapsSegment(const Segment &Child,
                                   const Segment &Parent) {

  return Parent.OriginalOffset <= Child.OriginalOffset &&
         Parent.OriginalOffset + Parent.FileSize > Child.OriginalOffset;
}

static bool compareSegmentsByOffset(const Segment *A, const Segment *B) {
  // Any segment without a parent segment should come before a segment
  // that has a parent segment.
  if (A->OriginalOffset < B->OriginalOffset)
    return true;
  if (A->OriginalOffset > B->OriginalOffset)
    return false;
  return A->Index < B->Index;
}

static bool compareSegmentsByPAddr(const Segment *A, const Segment *B) {
  if (A->PAddr < B->PAddr)
    return true;
  if (A->PAddr > B->PAddr)
    return false;
  return A->Index < B->Index;
}

void BinaryELFBuilder::initFileHeader() {
  Obj->Flags = 0x0;
  Obj->Type = ET_REL;
  Obj->OSABI = ELFOSABI_NONE;
  Obj->ABIVersion = 0;
  Obj->Entry = 0x0;
  Obj->Machine = EMachine;
  Obj->Version = 1;
}

void BinaryELFBuilder::initHeaderSegment() { Obj->ElfHdrSegment.Index = 0; }

StringTableSection *BinaryELFBuilder::addStrTab() {
  auto &StrTab = Obj->addSection<StringTableSection>();
  StrTab.Name = ".strtab";

  Obj->SectionNames = &StrTab;
  return &StrTab;
}

SymbolTableSection *BinaryELFBuilder::addSymTab(StringTableSection *StrTab) {
  auto &SymTab = Obj->addSection<SymbolTableSection>();

  SymTab.Name = ".symtab";
  SymTab.Link = StrTab->Index;

  // The symbol table always needs a null symbol
  SymTab.addSymbol("", 0, 0, nullptr, 0, 0, 0, 0);

  Obj->SymbolTable = &SymTab;
  return &SymTab;
}

void BinaryELFBuilder::addData(SymbolTableSection *SymTab) {
  auto Data = ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(MemBuf->getBufferStart()),
      MemBuf->getBufferSize());
  auto &DataSection = Obj->addSection<Section>(Data);
  DataSection.Name = ".data";
  DataSection.Type = ELF::SHT_PROGBITS;
  DataSection.Size = Data.size();
  DataSection.Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE;

  std::string SanitizedFilename = MemBuf->getBufferIdentifier().str();
  std::replace_if(std::begin(SanitizedFilename), std::end(SanitizedFilename),
                  [](char C) { return !isalnum(C); }, '_');
  Twine Prefix = Twine("_binary_") + SanitizedFilename;

  SymTab->addSymbol(Prefix + "_start", STB_GLOBAL, STT_NOTYPE, &DataSection,
                    /*Value=*/0, STV_DEFAULT, 0, 0);
  SymTab->addSymbol(Prefix + "_end", STB_GLOBAL, STT_NOTYPE, &DataSection,
                    /*Value=*/DataSection.Size, STV_DEFAULT, 0, 0);
  SymTab->addSymbol(Prefix + "_size", STB_GLOBAL, STT_NOTYPE, nullptr,
                    /*Value=*/DataSection.Size, STV_DEFAULT, SHN_ABS, 0);
}

void BinaryELFBuilder::initSections() {
  for (auto &Section : Obj->sections()) {
    Section.initialize(Obj->sections());
  }
}

std::unique_ptr<Object> BinaryELFBuilder::build() {
  initFileHeader();
  initHeaderSegment();
  StringTableSection *StrTab = addStrTab();
  SymbolTableSection *SymTab = addSymTab(StrTab);
  initSections();
  addData(SymTab);

  return std::move(Obj);
}

template <class ELFT> void ELFBuilder<ELFT>::setParentSegment(Segment &Child) {
  for (auto &Parent : Obj.segments()) {
    // Every segment will overlap with itself but we don't want a segment to
    // be it's own parent so we avoid that situation.
    if (&Child != &Parent && segmentOverlapsSegment(Child, Parent)) {
      // We want a canonical "most parental" segment but this requires
      // inspecting the ParentSegment.
      if (compareSegmentsByOffset(&Parent, &Child))
        if (Child.ParentSegment == nullptr ||
            compareSegmentsByOffset(&Parent, Child.ParentSegment)) {
          Child.ParentSegment = &Parent;
        }
    }
  }
}

template <class ELFT> void ELFBuilder<ELFT>::readProgramHeaders() {
  uint32_t Index = 0;
  for (const auto &Phdr : unwrapOrError(ElfFile.program_headers())) {
    ArrayRef<uint8_t> Data{ElfFile.base() + Phdr.p_offset,
                           (size_t)Phdr.p_filesz};
    Segment &Seg = Obj.addSegment(Data);
    Seg.Type = Phdr.p_type;
    Seg.Flags = Phdr.p_flags;
    Seg.OriginalOffset = Phdr.p_offset;
    Seg.Offset = Phdr.p_offset;
    Seg.VAddr = Phdr.p_vaddr;
    Seg.PAddr = Phdr.p_paddr;
    Seg.FileSize = Phdr.p_filesz;
    Seg.MemSize = Phdr.p_memsz;
    Seg.Align = Phdr.p_align;
    Seg.Index = Index++;
    for (auto &Section : Obj.sections()) {
      if (sectionWithinSegment(Section, Seg)) {
        Seg.addSection(&Section);
        if (!Section.ParentSegment ||
            Section.ParentSegment->Offset > Seg.Offset) {
          Section.ParentSegment = &Seg;
        }
      }
    }
  }

  auto &ElfHdr = Obj.ElfHdrSegment;
  ElfHdr.Index = Index++;

  const auto &Ehdr = *ElfFile.getHeader();
  auto &PrHdr = Obj.ProgramHdrSegment;
  PrHdr.Type = PT_PHDR;
  PrHdr.Flags = 0;
  // The spec requires us to have p_vaddr % p_align == p_offset % p_align.
  // Whereas this works automatically for ElfHdr, here OriginalOffset is
  // always non-zero and to ensure the equation we assign the same value to
  // VAddr as well.
  PrHdr.OriginalOffset = PrHdr.Offset = PrHdr.VAddr = Ehdr.e_phoff;
  PrHdr.PAddr = 0;
  PrHdr.FileSize = PrHdr.MemSize = Ehdr.e_phentsize * Ehdr.e_phnum;
  // The spec requires us to naturally align all the fields.
  PrHdr.Align = sizeof(Elf_Addr);
  PrHdr.Index = Index++;

  // Now we do an O(n^2) loop through the segments in order to match up
  // segments.
  for (auto &Child : Obj.segments())
    setParentSegment(Child);
  setParentSegment(ElfHdr);
  setParentSegment(PrHdr);
}

template <class ELFT>
void ELFBuilder<ELFT>::initGroupSection(GroupSection *GroupSec) {
  auto SecTable = Obj.sections();
  auto SymTab = SecTable.template getSectionOfType<SymbolTableSection>(
      GroupSec->Link,
      "Link field value " + Twine(GroupSec->Link) + " in section " +
          GroupSec->Name + " is invalid",
      "Link field value " + Twine(GroupSec->Link) + " in section " +
          GroupSec->Name + " is not a symbol table");
  auto Sym = SymTab->getSymbolByIndex(GroupSec->Info);
  if (!Sym)
    error("Info field value " + Twine(GroupSec->Info) + " in section " +
          GroupSec->Name + " is not a valid symbol index");
  GroupSec->setSymTab(SymTab);
  GroupSec->setSymbol(Sym);
  if (GroupSec->Contents.size() % sizeof(ELF::Elf32_Word) ||
      GroupSec->Contents.empty())
    error("The content of the section " + GroupSec->Name + " is malformed");
  const ELF::Elf32_Word *Word =
      reinterpret_cast<const ELF::Elf32_Word *>(GroupSec->Contents.data());
  const ELF::Elf32_Word *End =
      Word + GroupSec->Contents.size() / sizeof(ELF::Elf32_Word);
  GroupSec->setFlagWord(*Word++);
  for (; Word != End; ++Word) {
    uint32_t Index = support::endian::read32<ELFT::TargetEndianness>(Word);
    GroupSec->addMember(SecTable.getSection(
        Index, "Group member index " + Twine(Index) + " in section " +
                   GroupSec->Name + " is invalid"));
  }
}

template <class ELFT>
void ELFBuilder<ELFT>::initSymbolTable(SymbolTableSection *SymTab) {
  const Elf_Shdr &Shdr = *unwrapOrError(ElfFile.getSection(SymTab->Index));
  StringRef StrTabData = unwrapOrError(ElfFile.getStringTableForSymtab(Shdr));
  ArrayRef<Elf_Word> ShndxData;

  auto Symbols = unwrapOrError(ElfFile.symbols(&Shdr));
  for (const auto &Sym : Symbols) {
    SectionBase *DefSection = nullptr;
    StringRef Name = unwrapOrError(Sym.getName(StrTabData));

    if (Sym.st_shndx == SHN_XINDEX) {
      if (SymTab->getShndxTable() == nullptr)
        error("Symbol '" + Name +
              "' has index SHN_XINDEX but no SHT_SYMTAB_SHNDX section exists.");
      if (ShndxData.data() == nullptr) {
        const Elf_Shdr &ShndxSec =
            *unwrapOrError(ElfFile.getSection(SymTab->getShndxTable()->Index));
        ShndxData = unwrapOrError(
            ElfFile.template getSectionContentsAsArray<Elf_Word>(&ShndxSec));
        if (ShndxData.size() != Symbols.size())
          error("Symbol section index table does not have the same number of "
                "entries as the symbol table.");
      }
      Elf_Word Index = ShndxData[&Sym - Symbols.begin()];
      DefSection = Obj.sections().getSection(
          Index,
          "Symbol '" + Name + "' has invalid section index " + Twine(Index));
    } else if (Sym.st_shndx >= SHN_LORESERVE) {
      if (!isValidReservedSectionIndex(Sym.st_shndx, Obj.Machine)) {
        error(
            "Symbol '" + Name +
            "' has unsupported value greater than or equal to SHN_LORESERVE: " +
            Twine(Sym.st_shndx));
      }
    } else if (Sym.st_shndx != SHN_UNDEF) {
      DefSection = Obj.sections().getSection(
          Sym.st_shndx, "Symbol '" + Name +
                            "' is defined has invalid section index " +
                            Twine(Sym.st_shndx));
    }

    SymTab->addSymbol(Name, Sym.getBinding(), Sym.getType(), DefSection,
                      Sym.getValue(), Sym.st_other, Sym.st_shndx, Sym.st_size);
  }
}

template <class ELFT>
static void getAddend(uint64_t &ToSet, const Elf_Rel_Impl<ELFT, false> &Rel) {}

template <class ELFT>
static void getAddend(uint64_t &ToSet, const Elf_Rel_Impl<ELFT, true> &Rela) {
  ToSet = Rela.r_addend;
}

template <class T>
static void initRelocations(RelocationSection *Relocs,
                            SymbolTableSection *SymbolTable, T RelRange) {
  for (const auto &Rel : RelRange) {
    Relocation ToAdd;
    ToAdd.Offset = Rel.r_offset;
    getAddend(ToAdd.Addend, Rel);
    ToAdd.Type = Rel.getType(false);
    ToAdd.RelocSymbol = SymbolTable->getSymbolByIndex(Rel.getSymbol(false));
    Relocs->addRelocation(ToAdd);
  }
}

SectionBase *SectionTableRef::getSection(uint32_t Index, Twine ErrMsg) {
  if (Index == SHN_UNDEF || Index > Sections.size())
    error(ErrMsg);
  return Sections[Index - 1].get();
}

template <class T>
T *SectionTableRef::getSectionOfType(uint32_t Index, Twine IndexErrMsg,
                                     Twine TypeErrMsg) {
  if (T *Sec = dyn_cast<T>(getSection(Index, IndexErrMsg)))
    return Sec;
  error(TypeErrMsg);
}

template <class ELFT>
SectionBase &ELFBuilder<ELFT>::makeSection(const Elf_Shdr &Shdr) {
  ArrayRef<uint8_t> Data;
  switch (Shdr.sh_type) {
  case SHT_REL:
  case SHT_RELA:
    if (Shdr.sh_flags & SHF_ALLOC) {
      Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));
      return Obj.addSection<DynamicRelocationSection>(Data);
    }
    return Obj.addSection<RelocationSection>();
  case SHT_STRTAB:
    // If a string table is allocated we don't want to mess with it. That would
    // mean altering the memory image. There are no special link types or
    // anything so we can just use a Section.
    if (Shdr.sh_flags & SHF_ALLOC) {
      Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));
      return Obj.addSection<Section>(Data);
    }
    return Obj.addSection<StringTableSection>();
  case SHT_HASH:
  case SHT_GNU_HASH:
    // Hash tables should refer to SHT_DYNSYM which we're not going to change.
    // Because of this we don't need to mess with the hash tables either.
    Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));
    return Obj.addSection<Section>(Data);
  case SHT_GROUP:
    Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));
    return Obj.addSection<GroupSection>(Data);
  case SHT_DYNSYM:
    Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));
    return Obj.addSection<DynamicSymbolTableSection>(Data);
  case SHT_DYNAMIC:
    Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));
    return Obj.addSection<DynamicSection>(Data);
  case SHT_SYMTAB: {
    auto &SymTab = Obj.addSection<SymbolTableSection>();
    Obj.SymbolTable = &SymTab;
    return SymTab;
  }
  case SHT_SYMTAB_SHNDX: {
    auto &ShndxSection = Obj.addSection<SectionIndexSection>();
    Obj.SectionIndexTable = &ShndxSection;
    return ShndxSection;
  }
  case SHT_NOBITS:
    return Obj.addSection<Section>(Data);
  default: {
    Data = unwrapOrError(ElfFile.getSectionContents(&Shdr));

    if (isDataGnuCompressed(Data) || (Shdr.sh_flags & ELF::SHF_COMPRESSED)) {
      uint64_t DecompressedSize, DecompressedAlign;
      std::tie(DecompressedSize, DecompressedAlign) =
          getDecompressedSizeAndAlignment<ELFT>(Data);
      return Obj.addSection<CompressedSection>(Data, DecompressedSize,
                                               DecompressedAlign);
    }

    return Obj.addSection<Section>(Data);
  }
  }
}

template <class ELFT> void ELFBuilder<ELFT>::readSectionHeaders() {
  uint32_t Index = 0;
  for (const auto &Shdr : unwrapOrError(ElfFile.sections())) {
    if (Index == 0) {
      ++Index;
      continue;
    }
    auto &Sec = makeSection(Shdr);
    Sec.Name = unwrapOrError(ElfFile.getSectionName(&Shdr));
    Sec.Type = Shdr.sh_type;
    Sec.Flags = Shdr.sh_flags;
    Sec.Addr = Shdr.sh_addr;
    Sec.Offset = Shdr.sh_offset;
    Sec.OriginalOffset = Shdr.sh_offset;
    Sec.Size = Shdr.sh_size;
    Sec.Link = Shdr.sh_link;
    Sec.Info = Shdr.sh_info;
    Sec.Align = Shdr.sh_addralign;
    Sec.EntrySize = Shdr.sh_entsize;
    Sec.Index = Index++;
    Sec.OriginalData =
        ArrayRef<uint8_t>(ElfFile.base() + Shdr.sh_offset,
                          (Shdr.sh_type == SHT_NOBITS) ? 0 : Shdr.sh_size);
  }

  // If a section index table exists we'll need to initialize it before we
  // initialize the symbol table because the symbol table might need to
  // reference it.
  if (Obj.SectionIndexTable)
    Obj.SectionIndexTable->initialize(Obj.sections());

  // Now that all of the sections have been added we can fill out some extra
  // details about symbol tables. We need the symbol table filled out before
  // any relocations.
  if (Obj.SymbolTable) {
    Obj.SymbolTable->initialize(Obj.sections());
    initSymbolTable(Obj.SymbolTable);
  }

  // Now that all sections and symbols have been added we can add
  // relocations that reference symbols and set the link and info fields for
  // relocation sections.
  for (auto &Section : Obj.sections()) {
    if (&Section == Obj.SymbolTable)
      continue;
    Section.initialize(Obj.sections());
    if (auto RelSec = dyn_cast<RelocationSection>(&Section)) {
      auto Shdr = unwrapOrError(ElfFile.sections()).begin() + RelSec->Index;
      if (RelSec->Type == SHT_REL)
        initRelocations(RelSec, Obj.SymbolTable,
                        unwrapOrError(ElfFile.rels(Shdr)));
      else
        initRelocations(RelSec, Obj.SymbolTable,
                        unwrapOrError(ElfFile.relas(Shdr)));
    } else if (auto GroupSec = dyn_cast<GroupSection>(&Section)) {
      initGroupSection(GroupSec);
    }
  }
}

template <class ELFT> void ELFBuilder<ELFT>::build() {
  const auto &Ehdr = *ElfFile.getHeader();

  Obj.OSABI = Ehdr.e_ident[EI_OSABI];
  Obj.ABIVersion = Ehdr.e_ident[EI_ABIVERSION];
  Obj.Type = Ehdr.e_type;
  Obj.Machine = Ehdr.e_machine;
  Obj.Version = Ehdr.e_version;
  Obj.Entry = Ehdr.e_entry;
  Obj.Flags = Ehdr.e_flags;

  readSectionHeaders();
  readProgramHeaders();

  uint32_t ShstrIndex = Ehdr.e_shstrndx;
  if (ShstrIndex == SHN_XINDEX)
    ShstrIndex = unwrapOrError(ElfFile.getSection(0))->sh_link;

  Obj.SectionNames =
      Obj.sections().template getSectionOfType<StringTableSection>(
          ShstrIndex,
          "e_shstrndx field value " + Twine(Ehdr.e_shstrndx) +
              " in elf header " + " is invalid",
          "e_shstrndx field value " + Twine(Ehdr.e_shstrndx) +
              " in elf header " + " is not a string table");
}

// A generic size function which computes sizes of any random access range.
template <class R> size_t size(R &&Range) {
  return static_cast<size_t>(std::end(Range) - std::begin(Range));
}

Writer::~Writer() {}

Reader::~Reader() {}

std::unique_ptr<Object> BinaryReader::create() const {
  return BinaryELFBuilder(MInfo.EMachine, MemBuf).build();
}

std::unique_ptr<Object> ELFReader::create() const {
  auto Obj = llvm::make_unique<Object>();
  if (auto *O = dyn_cast<ELFObjectFile<ELF32LE>>(Bin)) {
    ELFBuilder<ELF32LE> Builder(*O, *Obj);
    Builder.build();
    return Obj;
  } else if (auto *O = dyn_cast<ELFObjectFile<ELF64LE>>(Bin)) {
    ELFBuilder<ELF64LE> Builder(*O, *Obj);
    Builder.build();
    return Obj;
  } else if (auto *O = dyn_cast<ELFObjectFile<ELF32BE>>(Bin)) {
    ELFBuilder<ELF32BE> Builder(*O, *Obj);
    Builder.build();
    return Obj;
  } else if (auto *O = dyn_cast<ELFObjectFile<ELF64BE>>(Bin)) {
    ELFBuilder<ELF64BE> Builder(*O, *Obj);
    Builder.build();
    return Obj;
  }
  error("Invalid file type");
}

template <class ELFT> void ELFWriter<ELFT>::writeEhdr() {
  uint8_t *B = Buf.getBufferStart();
  Elf_Ehdr &Ehdr = *reinterpret_cast<Elf_Ehdr *>(B);
  std::fill(Ehdr.e_ident, Ehdr.e_ident + 16, 0);
  Ehdr.e_ident[EI_MAG0] = 0x7f;
  Ehdr.e_ident[EI_MAG1] = 'E';
  Ehdr.e_ident[EI_MAG2] = 'L';
  Ehdr.e_ident[EI_MAG3] = 'F';
  Ehdr.e_ident[EI_CLASS] = ELFT::Is64Bits ? ELFCLASS64 : ELFCLASS32;
  Ehdr.e_ident[EI_DATA] =
      ELFT::TargetEndianness == support::big ? ELFDATA2MSB : ELFDATA2LSB;
  Ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  Ehdr.e_ident[EI_OSABI] = Obj.OSABI;
  Ehdr.e_ident[EI_ABIVERSION] = Obj.ABIVersion;

  Ehdr.e_type = Obj.Type;
  Ehdr.e_machine = Obj.Machine;
  Ehdr.e_version = Obj.Version;
  Ehdr.e_entry = Obj.Entry;
  // We have to use the fully-qualified name llvm::size
  // since some compilers complain on ambiguous resolution.
  Ehdr.e_phnum = llvm::size(Obj.segments());
  Ehdr.e_phoff = (Ehdr.e_phnum != 0) ? Obj.ProgramHdrSegment.Offset : 0;
  Ehdr.e_phentsize = (Ehdr.e_phnum != 0) ? sizeof(Elf_Phdr) : 0;
  Ehdr.e_flags = Obj.Flags;
  Ehdr.e_ehsize = sizeof(Elf_Ehdr);
  if (WriteSectionHeaders && size(Obj.sections()) != 0) {
    Ehdr.e_shentsize = sizeof(Elf_Shdr);
    Ehdr.e_shoff = Obj.SHOffset;
    // """
    // If the number of sections is greater than or equal to
    // SHN_LORESERVE (0xff00), this member has the value zero and the actual
    // number of section header table entries is contained in the sh_size field
    // of the section header at index 0.
    // """
    auto Shnum = size(Obj.sections()) + 1;
    if (Shnum >= SHN_LORESERVE)
      Ehdr.e_shnum = 0;
    else
      Ehdr.e_shnum = Shnum;
    // """
    // If the section name string table section index is greater than or equal
    // to SHN_LORESERVE (0xff00), this member has the value SHN_XINDEX (0xffff)
    // and the actual index of the section name string table section is
    // contained in the sh_link field of the section header at index 0.
    // """
    if (Obj.SectionNames->Index >= SHN_LORESERVE)
      Ehdr.e_shstrndx = SHN_XINDEX;
    else
      Ehdr.e_shstrndx = Obj.SectionNames->Index;
  } else {
    Ehdr.e_shentsize = 0;
    Ehdr.e_shoff = 0;
    Ehdr.e_shnum = 0;
    Ehdr.e_shstrndx = 0;
  }
}

template <class ELFT> void ELFWriter<ELFT>::writePhdrs() {
  for (auto &Seg : Obj.segments())
    writePhdr(Seg);
}

template <class ELFT> void ELFWriter<ELFT>::writeShdrs() {
  uint8_t *B = Buf.getBufferStart() + Obj.SHOffset;
  // This reference serves to write the dummy section header at the begining
  // of the file. It is not used for anything else
  Elf_Shdr &Shdr = *reinterpret_cast<Elf_Shdr *>(B);
  Shdr.sh_name = 0;
  Shdr.sh_type = SHT_NULL;
  Shdr.sh_flags = 0;
  Shdr.sh_addr = 0;
  Shdr.sh_offset = 0;
  // See writeEhdr for why we do this.
  uint64_t Shnum = size(Obj.sections()) + 1;
  if (Shnum >= SHN_LORESERVE)
    Shdr.sh_size = Shnum;
  else
    Shdr.sh_size = 0;
  // See writeEhdr for why we do this.
  if (Obj.SectionNames != nullptr && Obj.SectionNames->Index >= SHN_LORESERVE)
    Shdr.sh_link = Obj.SectionNames->Index;
  else
    Shdr.sh_link = 0;
  Shdr.sh_info = 0;
  Shdr.sh_addralign = 0;
  Shdr.sh_entsize = 0;

  for (auto &Sec : Obj.sections())
    writeShdr(Sec);
}

template <class ELFT> void ELFWriter<ELFT>::writeSectionData() {
  for (auto &Sec : Obj.sections())
    Sec.accept(*SecWriter);
}

void Object::removeSections(std::function<bool(const SectionBase &)> ToRemove) {

  auto Iter = std::stable_partition(
      std::begin(Sections), std::end(Sections), [=](const SecPtr &Sec) {
        if (ToRemove(*Sec))
          return false;
        if (auto RelSec = dyn_cast<RelocationSectionBase>(Sec.get())) {
          if (auto ToRelSec = RelSec->getSection())
            return !ToRemove(*ToRelSec);
        }
        return true;
      });
  if (SymbolTable != nullptr && ToRemove(*SymbolTable))
    SymbolTable = nullptr;
  if (SectionNames != nullptr && ToRemove(*SectionNames))
    SectionNames = nullptr;
  if (SectionIndexTable != nullptr && ToRemove(*SectionIndexTable))
    SectionIndexTable = nullptr;
  // Now make sure there are no remaining references to the sections that will
  // be removed. Sometimes it is impossible to remove a reference so we emit
  // an error here instead.
  for (auto &RemoveSec : make_range(Iter, std::end(Sections))) {
    for (auto &Segment : Segments)
      Segment->removeSection(RemoveSec.get());
    for (auto &KeepSec : make_range(std::begin(Sections), Iter))
      KeepSec->removeSectionReferences(RemoveSec.get());
  }
  // Now finally get rid of them all togethor.
  Sections.erase(Iter, std::end(Sections));
}

void Object::removeSymbols(function_ref<bool(const Symbol &)> ToRemove) {
  if (!SymbolTable)
    return;

  for (const SecPtr &Sec : Sections)
    Sec->removeSymbols(ToRemove);
}

void Object::sortSections() {
  // Put all sections in offset order. Maintain the ordering as closely as
  // possible while meeting that demand however.
  auto CompareSections = [](const SecPtr &A, const SecPtr &B) {
    return A->OriginalOffset < B->OriginalOffset;
  };
  std::stable_sort(std::begin(this->Sections), std::end(this->Sections),
                   CompareSections);
}

static uint64_t alignToAddr(uint64_t Offset, uint64_t Addr, uint64_t Align) {
  // Calculate Diff such that (Offset + Diff) & -Align == Addr & -Align.
  if (Align == 0)
    Align = 1;
  auto Diff =
      static_cast<int64_t>(Addr % Align) - static_cast<int64_t>(Offset % Align);
  // We only want to add to Offset, however, so if Diff < 0 we can add Align and
  // (Offset + Diff) & -Align == Addr & -Align will still hold.
  if (Diff < 0)
    Diff += Align;
  return Offset + Diff;
}

// Orders segments such that if x = y->ParentSegment then y comes before x.
static void orderSegments(std::vector<Segment *> &Segments) {
  std::stable_sort(std::begin(Segments), std::end(Segments),
                   compareSegmentsByOffset);
}

// This function finds a consistent layout for a list of segments starting from
// an Offset. It assumes that Segments have been sorted by OrderSegments and
// returns an Offset one past the end of the last segment.
static uint64_t LayoutSegments(std::vector<Segment *> &Segments,
                               uint64_t Offset) {
  assert(std::is_sorted(std::begin(Segments), std::end(Segments),
                        compareSegmentsByOffset));
  // The only way a segment should move is if a section was between two
  // segments and that section was removed. If that section isn't in a segment
  // then it's acceptable, but not ideal, to simply move it to after the
  // segments. So we can simply layout segments one after the other accounting
  // for alignment.
  for (auto &Segment : Segments) {
    // We assume that segments have been ordered by OriginalOffset and Index
    // such that a parent segment will always come before a child segment in
    // OrderedSegments. This means that the Offset of the ParentSegment should
    // already be set and we can set our offset relative to it.
    if (Segment->ParentSegment != nullptr) {
      auto Parent = Segment->ParentSegment;
      Segment->Offset =
          Parent->Offset + Segment->OriginalOffset - Parent->OriginalOffset;
    } else {
      Offset = alignToAddr(Offset, Segment->VAddr, Segment->Align);
      Segment->Offset = Offset;
    }
    Offset = std::max(Offset, Segment->Offset + Segment->FileSize);
  }
  return Offset;
}

// This function finds a consistent layout for a list of sections. It assumes
// that the ->ParentSegment of each section has already been laid out. The
// supplied starting Offset is used for the starting offset of any section that
// does not have a ParentSegment. It returns either the offset given if all
// sections had a ParentSegment or an offset one past the last section if there
// was a section that didn't have a ParentSegment.
template <class Range>
static uint64_t layoutSections(Range Sections, uint64_t Offset) {
  // Now the offset of every segment has been set we can assign the offsets
  // of each section. For sections that are covered by a segment we should use
  // the segment's original offset and the section's original offset to compute
  // the offset from the start of the segment. Using the offset from the start
  // of the segment we can assign a new offset to the section. For sections not
  // covered by segments we can just bump Offset to the next valid location.
  uint32_t Index = 1;
  for (auto &Section : Sections) {
    Section.Index = Index++;
    if (Section.ParentSegment != nullptr) {
      auto Segment = *Section.ParentSegment;
      Section.Offset =
          Segment.Offset + (Section.OriginalOffset - Segment.OriginalOffset);
    } else {
      Offset = alignTo(Offset, Section.Align == 0 ? 1 : Section.Align);
      Section.Offset = Offset;
      if (Section.Type != SHT_NOBITS)
        Offset += Section.Size;
    }
  }
  return Offset;
}

template <class ELFT> void ELFWriter<ELFT>::initEhdrSegment() {
  auto &ElfHdr = Obj.ElfHdrSegment;
  ElfHdr.Type = PT_PHDR;
  ElfHdr.Flags = 0;
  ElfHdr.OriginalOffset = ElfHdr.Offset = 0;
  ElfHdr.VAddr = 0;
  ElfHdr.PAddr = 0;
  ElfHdr.FileSize = ElfHdr.MemSize = sizeof(Elf_Ehdr);
  ElfHdr.Align = 0;
}

template <class ELFT> void ELFWriter<ELFT>::assignOffsets() {
  // We need a temporary list of segments that has a special order to it
  // so that we know that anytime ->ParentSegment is set that segment has
  // already had its offset properly set.
  std::vector<Segment *> OrderedSegments;
  for (auto &Segment : Obj.segments())
    OrderedSegments.push_back(&Segment);
  OrderedSegments.push_back(&Obj.ElfHdrSegment);
  OrderedSegments.push_back(&Obj.ProgramHdrSegment);
  orderSegments(OrderedSegments);
  // Offset is used as the start offset of the first segment to be laid out.
  // Since the ELF Header (ElfHdrSegment) must be at the start of the file,
  // we start at offset 0.
  uint64_t Offset = 0;
  Offset = LayoutSegments(OrderedSegments, Offset);
  Offset = layoutSections(Obj.sections(), Offset);
  // If we need to write the section header table out then we need to align the
  // Offset so that SHOffset is valid.
  if (WriteSectionHeaders)
    Offset = alignTo(Offset, sizeof(Elf_Addr));
  Obj.SHOffset = Offset;
}

template <class ELFT> size_t ELFWriter<ELFT>::totalSize() const {
  // We already have the section header offset so we can calculate the total
  // size by just adding up the size of each section header.
  auto NullSectionSize = WriteSectionHeaders ? sizeof(Elf_Shdr) : 0;
  return Obj.SHOffset + size(Obj.sections()) * sizeof(Elf_Shdr) +
         NullSectionSize;
}

template <class ELFT> void ELFWriter<ELFT>::write() {
  writeEhdr();
  writePhdrs();
  writeSectionData();
  if (WriteSectionHeaders)
    writeShdrs();
  if (auto E = Buf.commit())
    reportError(Buf.getName(), errorToErrorCode(std::move(E)));
}

template <class ELFT> void ELFWriter<ELFT>::finalize() {
  // It could happen that SectionNames has been removed and yet the user wants
  // a section header table output. We need to throw an error if a user tries
  // to do that.
  if (Obj.SectionNames == nullptr && WriteSectionHeaders)
    error("Cannot write section header table because section header string "
          "table was removed.");

  Obj.sortSections();

  // We need to assign indexes before we perform layout because we need to know
  // if we need large indexes or not. We can assign indexes first and check as
  // we go to see if we will actully need large indexes.
  bool NeedsLargeIndexes = false;
  if (size(Obj.sections()) >= SHN_LORESERVE) {
    auto Sections = Obj.sections();
    NeedsLargeIndexes =
        std::any_of(Sections.begin() + SHN_LORESERVE, Sections.end(),
                    [](const SectionBase &Sec) { return Sec.HasSymbol; });
    // TODO: handle case where only one section needs the large index table but
    // only needs it because the large index table hasn't been removed yet.
  }

  if (NeedsLargeIndexes) {
    // This means we definitely need to have a section index table but if we
    // already have one then we should use it instead of making a new one.
    if (Obj.SymbolTable != nullptr && Obj.SectionIndexTable == nullptr) {
      // Addition of a section to the end does not invalidate the indexes of
      // other sections and assigns the correct index to the new section.
      auto &Shndx = Obj.addSection<SectionIndexSection>();
      Obj.SymbolTable->setShndxTable(&Shndx);
      Shndx.setSymTab(Obj.SymbolTable);
    }
  } else {
    // Since we don't need SectionIndexTable we should remove it and all
    // references to it.
    if (Obj.SectionIndexTable != nullptr) {
      Obj.removeSections([this](const SectionBase &Sec) {
        return &Sec == Obj.SectionIndexTable;
      });
    }
  }

  // Make sure we add the names of all the sections. Importantly this must be
  // done after we decide to add or remove SectionIndexes.
  if (Obj.SectionNames != nullptr)
    for (const auto &Section : Obj.sections()) {
      Obj.SectionNames->addString(Section.Name);
    }

  initEhdrSegment();

  // Before we can prepare for layout the indexes need to be finalized.
  // Also, the output arch may not be the same as the input arch, so fix up
  // size-related fields before doing layout calculations.
  uint64_t Index = 0;
  auto SecSizer = llvm::make_unique<ELFSectionSizer<ELFT>>();
  for (auto &Sec : Obj.sections()) {
    Sec.Index = Index++;
    Sec.accept(*SecSizer);
  }

  // The symbol table does not update all other sections on update. For
  // instance, symbol names are not added as new symbols are added. This means
  // that some sections, like .strtab, don't yet have their final size.
  if (Obj.SymbolTable != nullptr)
    Obj.SymbolTable->prepareForLayout();

  assignOffsets();

  // Finalize SectionNames first so that we can assign name indexes.
  if (Obj.SectionNames != nullptr)
    Obj.SectionNames->finalize();
  // Finally now that all offsets and indexes have been set we can finalize any
  // remaining issues.
  uint64_t Offset = Obj.SHOffset + sizeof(Elf_Shdr);
  for (auto &Section : Obj.sections()) {
    Section.HeaderOffset = Offset;
    Offset += sizeof(Elf_Shdr);
    if (WriteSectionHeaders)
      Section.NameIndex = Obj.SectionNames->findIndex(Section.Name);
    Section.finalize();
  }

  Buf.allocate(totalSize());
  SecWriter = llvm::make_unique<ELFSectionWriter<ELFT>>(Buf);
}

void BinaryWriter::write() {
  for (auto &Section : Obj.sections()) {
    if ((Section.Flags & SHF_ALLOC) == 0)
      continue;
    Section.accept(*SecWriter);
  }
  if (auto E = Buf.commit())
    reportError(Buf.getName(), errorToErrorCode(std::move(E)));
}

void BinaryWriter::finalize() {
  // TODO: Create a filter range to construct OrderedSegments from so that this
  // code can be deduped with assignOffsets above. This should also solve the
  // todo below for LayoutSections.
  // We need a temporary list of segments that has a special order to it
  // so that we know that anytime ->ParentSegment is set that segment has
  // already had it's offset properly set. We only want to consider the segments
  // that will affect layout of allocated sections so we only add those.
  std::vector<Segment *> OrderedSegments;
  for (auto &Section : Obj.sections()) {
    if ((Section.Flags & SHF_ALLOC) != 0 && Section.ParentSegment != nullptr) {
      OrderedSegments.push_back(Section.ParentSegment);
    }
  }

  // For binary output, we're going to use physical addresses instead of
  // virtual addresses, since a binary output is used for cases like ROM
  // loading and physical addresses are intended for ROM loading.
  // However, if no segment has a physical address, we'll fallback to using
  // virtual addresses for all.
  if (all_of(OrderedSegments,
             [](const Segment *Seg) { return Seg->PAddr == 0; }))
    for (Segment *Seg : OrderedSegments)
      Seg->PAddr = Seg->VAddr;

  std::stable_sort(std::begin(OrderedSegments), std::end(OrderedSegments),
                   compareSegmentsByPAddr);

  // Because we add a ParentSegment for each section we might have duplicate
  // segments in OrderedSegments. If there were duplicates then LayoutSegments
  // would do very strange things.
  auto End =
      std::unique(std::begin(OrderedSegments), std::end(OrderedSegments));
  OrderedSegments.erase(End, std::end(OrderedSegments));

  uint64_t Offset = 0;

  // Modify the first segment so that there is no gap at the start. This allows
  // our layout algorithm to proceed as expected while not writing out the gap
  // at the start.
  if (!OrderedSegments.empty()) {
    auto Seg = OrderedSegments[0];
    auto Sec = Seg->firstSection();
    auto Diff = Sec->OriginalOffset - Seg->OriginalOffset;
    Seg->OriginalOffset += Diff;
    // The size needs to be shrunk as well.
    Seg->FileSize -= Diff;
    // The PAddr needs to be increased to remove the gap before the first
    // section.
    Seg->PAddr += Diff;
    uint64_t LowestPAddr = Seg->PAddr;
    for (auto &Segment : OrderedSegments) {
      Segment->Offset = Segment->PAddr - LowestPAddr;
      Offset = std::max(Offset, Segment->Offset + Segment->FileSize);
    }
  }

  // TODO: generalize LayoutSections to take a range. Pass a special range
  // constructed from an iterator that skips values for which a predicate does
  // not hold. Then pass such a range to LayoutSections instead of constructing
  // AllocatedSections here.
  std::vector<SectionBase *> AllocatedSections;
  for (auto &Section : Obj.sections()) {
    if ((Section.Flags & SHF_ALLOC) == 0)
      continue;
    AllocatedSections.push_back(&Section);
  }
  layoutSections(make_pointee_range(AllocatedSections), Offset);

  // Now that every section has been laid out we just need to compute the total
  // file size. This might not be the same as the offset returned by
  // LayoutSections, because we want to truncate the last segment to the end of
  // its last section, to match GNU objcopy's behaviour.
  TotalSize = 0;
  for (const auto &Section : AllocatedSections) {
    if (Section->Type != SHT_NOBITS)
      TotalSize = std::max(TotalSize, Section->Offset + Section->Size);
  }

  Buf.allocate(TotalSize);
  SecWriter = llvm::make_unique<BinarySectionWriter>(Buf);
}

template class ELFBuilder<ELF64LE>;
template class ELFBuilder<ELF64BE>;
template class ELFBuilder<ELF32LE>;
template class ELFBuilder<ELF32BE>;

template class ELFWriter<ELF64LE>;
template class ELFWriter<ELF64BE>;
template class ELFWriter<ELF32LE>;
template class ELFWriter<ELF32BE>;

} // end namespace elf
} // end namespace objcopy
} // end namespace llvm
