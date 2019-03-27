//===- Object.h -------------------------------------------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_OBJCOPY_OBJECT_H
#define LLVM_TOOLS_OBJCOPY_OBJECT_H

#include "Buffer.h"
#include "CopyConfig.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/JamCRC.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace llvm {
enum class DebugCompressionType;
namespace objcopy {
namespace elf {

class SectionBase;
class Section;
class OwnedDataSection;
class StringTableSection;
class SymbolTableSection;
class RelocationSection;
class DynamicRelocationSection;
class GnuDebugLinkSection;
class GroupSection;
class SectionIndexSection;
class CompressedSection;
class DecompressedSection;
class Segment;
class Object;
struct Symbol;

class SectionTableRef {
  MutableArrayRef<std::unique_ptr<SectionBase>> Sections;

public:
  using iterator = pointee_iterator<std::unique_ptr<SectionBase> *>;

  explicit SectionTableRef(MutableArrayRef<std::unique_ptr<SectionBase>> Secs)
      : Sections(Secs) {}
  SectionTableRef(const SectionTableRef &) = default;

  iterator begin() { return iterator(Sections.data()); }
  iterator end() { return iterator(Sections.data() + Sections.size()); }

  SectionBase *getSection(uint32_t Index, Twine ErrMsg);

  template <class T>
  T *getSectionOfType(uint32_t Index, Twine IndexErrMsg, Twine TypeErrMsg);
};

enum ElfType { ELFT_ELF32LE, ELFT_ELF64LE, ELFT_ELF32BE, ELFT_ELF64BE };

class SectionVisitor {
public:
  virtual ~SectionVisitor() = default;

  virtual void visit(const Section &Sec) = 0;
  virtual void visit(const OwnedDataSection &Sec) = 0;
  virtual void visit(const StringTableSection &Sec) = 0;
  virtual void visit(const SymbolTableSection &Sec) = 0;
  virtual void visit(const RelocationSection &Sec) = 0;
  virtual void visit(const DynamicRelocationSection &Sec) = 0;
  virtual void visit(const GnuDebugLinkSection &Sec) = 0;
  virtual void visit(const GroupSection &Sec) = 0;
  virtual void visit(const SectionIndexSection &Sec) = 0;
  virtual void visit(const CompressedSection &Sec) = 0;
  virtual void visit(const DecompressedSection &Sec) = 0;
};

class MutableSectionVisitor {
public:
  virtual ~MutableSectionVisitor() = default;

  virtual void visit(Section &Sec) = 0;
  virtual void visit(OwnedDataSection &Sec) = 0;
  virtual void visit(StringTableSection &Sec) = 0;
  virtual void visit(SymbolTableSection &Sec) = 0;
  virtual void visit(RelocationSection &Sec) = 0;
  virtual void visit(DynamicRelocationSection &Sec) = 0;
  virtual void visit(GnuDebugLinkSection &Sec) = 0;
  virtual void visit(GroupSection &Sec) = 0;
  virtual void visit(SectionIndexSection &Sec) = 0;
  virtual void visit(CompressedSection &Sec) = 0;
  virtual void visit(DecompressedSection &Sec) = 0;
};

class SectionWriter : public SectionVisitor {
protected:
  Buffer &Out;

public:
  virtual ~SectionWriter(){};

  void visit(const Section &Sec) override;
  void visit(const OwnedDataSection &Sec) override;
  void visit(const StringTableSection &Sec) override;
  void visit(const DynamicRelocationSection &Sec) override;
  virtual void visit(const SymbolTableSection &Sec) override = 0;
  virtual void visit(const RelocationSection &Sec) override = 0;
  virtual void visit(const GnuDebugLinkSection &Sec) override = 0;
  virtual void visit(const GroupSection &Sec) override = 0;
  virtual void visit(const SectionIndexSection &Sec) override = 0;
  virtual void visit(const CompressedSection &Sec) override = 0;
  virtual void visit(const DecompressedSection &Sec) override = 0;

  explicit SectionWriter(Buffer &Buf) : Out(Buf) {}
};

template <class ELFT> class ELFSectionWriter : public SectionWriter {
private:
  using Elf_Word = typename ELFT::Word;
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;
  using Elf_Sym = typename ELFT::Sym;

public:
  virtual ~ELFSectionWriter() {}
  void visit(const SymbolTableSection &Sec) override;
  void visit(const RelocationSection &Sec) override;
  void visit(const GnuDebugLinkSection &Sec) override;
  void visit(const GroupSection &Sec) override;
  void visit(const SectionIndexSection &Sec) override;
  void visit(const CompressedSection &Sec) override;
  void visit(const DecompressedSection &Sec) override;

  explicit ELFSectionWriter(Buffer &Buf) : SectionWriter(Buf) {}
};

template <class ELFT> class ELFSectionSizer : public MutableSectionVisitor {
private:
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;
  using Elf_Sym = typename ELFT::Sym;
  using Elf_Word = typename ELFT::Word;
  using Elf_Xword = typename ELFT::Xword;

public:
  void visit(Section &Sec) override;
  void visit(OwnedDataSection &Sec) override;
  void visit(StringTableSection &Sec) override;
  void visit(DynamicRelocationSection &Sec) override;
  void visit(SymbolTableSection &Sec) override;
  void visit(RelocationSection &Sec) override;
  void visit(GnuDebugLinkSection &Sec) override;
  void visit(GroupSection &Sec) override;
  void visit(SectionIndexSection &Sec) override;
  void visit(CompressedSection &Sec) override;
  void visit(DecompressedSection &Sec) override;
};

#define MAKE_SEC_WRITER_FRIEND                                                 \
  friend class SectionWriter;                                                  \
  template <class ELFT> friend class ELFSectionWriter;                         \
  template <class ELFT> friend class ELFSectionSizer;

class BinarySectionWriter : public SectionWriter {
public:
  virtual ~BinarySectionWriter() {}

  void visit(const SymbolTableSection &Sec) override;
  void visit(const RelocationSection &Sec) override;
  void visit(const GnuDebugLinkSection &Sec) override;
  void visit(const GroupSection &Sec) override;
  void visit(const SectionIndexSection &Sec) override;
  void visit(const CompressedSection &Sec) override;
  void visit(const DecompressedSection &Sec) override;

  explicit BinarySectionWriter(Buffer &Buf) : SectionWriter(Buf) {}
};

class Writer {
protected:
  Object &Obj;
  Buffer &Buf;

public:
  virtual ~Writer();
  virtual void finalize() = 0;
  virtual void write() = 0;

  Writer(Object &O, Buffer &B) : Obj(O), Buf(B) {}
};

template <class ELFT> class ELFWriter : public Writer {
private:
  using Elf_Addr = typename ELFT::Addr;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Phdr = typename ELFT::Phdr;
  using Elf_Ehdr = typename ELFT::Ehdr;

  void initEhdrSegment();

  void writeEhdr();
  void writePhdr(const Segment &Seg);
  void writeShdr(const SectionBase &Sec);

  void writePhdrs();
  void writeShdrs();
  void writeSectionData();

  void assignOffsets();

  std::unique_ptr<ELFSectionWriter<ELFT>> SecWriter;

  size_t totalSize() const;

public:
  virtual ~ELFWriter() {}
  bool WriteSectionHeaders = true;

  void finalize() override;
  void write() override;
  ELFWriter(Object &Obj, Buffer &Buf, bool WSH)
      : Writer(Obj, Buf), WriteSectionHeaders(WSH) {}
};

class BinaryWriter : public Writer {
private:
  std::unique_ptr<BinarySectionWriter> SecWriter;

  uint64_t TotalSize;

public:
  ~BinaryWriter() {}
  void finalize() override;
  void write() override;
  BinaryWriter(Object &Obj, Buffer &Buf) : Writer(Obj, Buf) {}
};

class SectionBase {
public:
  std::string Name;
  Segment *ParentSegment = nullptr;
  uint64_t HeaderOffset;
  uint64_t OriginalOffset = std::numeric_limits<uint64_t>::max();
  uint32_t Index;
  bool HasSymbol = false;

  uint64_t Addr = 0;
  uint64_t Align = 1;
  uint32_t EntrySize = 0;
  uint64_t Flags = 0;
  uint64_t Info = 0;
  uint64_t Link = ELF::SHN_UNDEF;
  uint64_t NameIndex = 0;
  uint64_t Offset = 0;
  uint64_t Size = 0;
  uint64_t Type = ELF::SHT_NULL;
  ArrayRef<uint8_t> OriginalData;

  SectionBase() = default;
  SectionBase(const SectionBase &) = default;

  virtual ~SectionBase() = default;

  virtual void initialize(SectionTableRef SecTable);
  virtual void finalize();
  virtual void removeSectionReferences(const SectionBase *Sec);
  virtual void removeSymbols(function_ref<bool(const Symbol &)> ToRemove);
  virtual void accept(SectionVisitor &Visitor) const = 0;
  virtual void accept(MutableSectionVisitor &Visitor) = 0;
  virtual void markSymbols();
};

class Segment {
private:
  struct SectionCompare {
    bool operator()(const SectionBase *Lhs, const SectionBase *Rhs) const {
      // Some sections might have the same address if one of them is empty. To
      // fix this we can use the lexicographic ordering on ->Addr and the
      // address of the actully stored section.
      if (Lhs->OriginalOffset == Rhs->OriginalOffset)
        return Lhs < Rhs;
      return Lhs->OriginalOffset < Rhs->OriginalOffset;
    }
  };

  std::set<const SectionBase *, SectionCompare> Sections;

public:
  uint32_t Type;
  uint32_t Flags;
  uint64_t Offset;
  uint64_t VAddr;
  uint64_t PAddr;
  uint64_t FileSize;
  uint64_t MemSize;
  uint64_t Align;

  uint32_t Index;
  uint64_t OriginalOffset;
  Segment *ParentSegment = nullptr;
  ArrayRef<uint8_t> Contents;

  explicit Segment(ArrayRef<uint8_t> Data) : Contents(Data) {}
  Segment() {}

  const SectionBase *firstSection() const {
    if (!Sections.empty())
      return *Sections.begin();
    return nullptr;
  }

  void removeSection(const SectionBase *Sec) { Sections.erase(Sec); }
  void addSection(const SectionBase *Sec) { Sections.insert(Sec); }
};

class Section : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  ArrayRef<uint8_t> Contents;
  SectionBase *LinkSection = nullptr;

public:
  explicit Section(ArrayRef<uint8_t> Data) : Contents(Data) {}

  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;
  void removeSectionReferences(const SectionBase *Sec) override;
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
};

class OwnedDataSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  std::vector<uint8_t> Data;

public:
  OwnedDataSection(StringRef SecName, ArrayRef<uint8_t> Data)
      : Data(std::begin(Data), std::end(Data)) {
    Name = SecName.str();
    Type = ELF::SHT_PROGBITS;
    Size = Data.size();
    OriginalOffset = std::numeric_limits<uint64_t>::max();
  }

  void accept(SectionVisitor &Sec) const override;
  void accept(MutableSectionVisitor &Visitor) override;
};

class CompressedSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  DebugCompressionType CompressionType;
  uint64_t DecompressedSize;
  uint64_t DecompressedAlign;
  SmallVector<char, 128> CompressedData;

public:
  CompressedSection(const SectionBase &Sec,
                    DebugCompressionType CompressionType);
  CompressedSection(ArrayRef<uint8_t> CompressedData, uint64_t DecompressedSize,
                    uint64_t DecompressedAlign);

  uint64_t getDecompressedSize() const { return DecompressedSize; }
  uint64_t getDecompressedAlign() const { return DecompressedAlign; }

  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;

  static bool classof(const SectionBase *S) {
    return (S->Flags & ELF::SHF_COMPRESSED) ||
           (StringRef(S->Name).startswith(".zdebug"));
  }
};

class DecompressedSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

public:
  explicit DecompressedSection(const CompressedSection &Sec)
      : SectionBase(Sec) {
    Size = Sec.getDecompressedSize();
    Align = Sec.getDecompressedAlign();
    Flags = (Flags & ~ELF::SHF_COMPRESSED);
    if (StringRef(Name).startswith(".zdebug"))
      Name = "." + Name.substr(2);
  }

  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;
};

// There are two types of string tables that can exist, dynamic and not dynamic.
// In the dynamic case the string table is allocated. Changing a dynamic string
// table would mean altering virtual addresses and thus the memory image. So
// dynamic string tables should not have an interface to modify them or
// reconstruct them. This type lets us reconstruct a string table. To avoid
// this class being used for dynamic string tables (which has happened) the
// classof method checks that the particular instance is not allocated. This
// then agrees with the makeSection method used to construct most sections.
class StringTableSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  StringTableBuilder StrTabBuilder;

public:
  StringTableSection() : StrTabBuilder(StringTableBuilder::ELF) {
    Type = ELF::SHT_STRTAB;
  }

  void addString(StringRef Name);
  uint32_t findIndex(StringRef Name) const;
  void finalize() override;
  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;

  static bool classof(const SectionBase *S) {
    if (S->Flags & ELF::SHF_ALLOC)
      return false;
    return S->Type == ELF::SHT_STRTAB;
  }
};

// Symbols have a st_shndx field that normally stores an index but occasionally
// stores a different special value. This enum keeps track of what the st_shndx
// field means. Most of the values are just copies of the special SHN_* values.
// SYMBOL_SIMPLE_INDEX means that the st_shndx is just an index of a section.
enum SymbolShndxType {
  SYMBOL_SIMPLE_INDEX = 0,
  SYMBOL_ABS = ELF::SHN_ABS,
  SYMBOL_COMMON = ELF::SHN_COMMON,
  SYMBOL_HEXAGON_SCOMMON = ELF::SHN_HEXAGON_SCOMMON,
  SYMBOL_HEXAGON_SCOMMON_2 = ELF::SHN_HEXAGON_SCOMMON_2,
  SYMBOL_HEXAGON_SCOMMON_4 = ELF::SHN_HEXAGON_SCOMMON_4,
  SYMBOL_HEXAGON_SCOMMON_8 = ELF::SHN_HEXAGON_SCOMMON_8,
  SYMBOL_XINDEX = ELF::SHN_XINDEX,
};

struct Symbol {
  uint8_t Binding;
  SectionBase *DefinedIn = nullptr;
  SymbolShndxType ShndxType;
  uint32_t Index;
  std::string Name;
  uint32_t NameIndex;
  uint64_t Size;
  uint8_t Type;
  uint64_t Value;
  uint8_t Visibility;
  bool Referenced = false;

  uint16_t getShndx() const;
  bool isCommon() const;
};

class SectionIndexSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

private:
  std::vector<uint32_t> Indexes;
  SymbolTableSection *Symbols = nullptr;

public:
  virtual ~SectionIndexSection() {}
  void addIndex(uint32_t Index) {
    Indexes.push_back(Index);
    Size += 4;
  }
  void setSymTab(SymbolTableSection *SymTab) { Symbols = SymTab; }
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;

  SectionIndexSection() {
    Name = ".symtab_shndx";
    Align = 4;
    EntrySize = 4;
    Type = ELF::SHT_SYMTAB_SHNDX;
  }
};

class SymbolTableSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  void setStrTab(StringTableSection *StrTab) { SymbolNames = StrTab; }
  void assignIndices();

protected:
  std::vector<std::unique_ptr<Symbol>> Symbols;
  StringTableSection *SymbolNames = nullptr;
  SectionIndexSection *SectionIndexTable = nullptr;

  using SymPtr = std::unique_ptr<Symbol>;

public:
  SymbolTableSection() { Type = ELF::SHT_SYMTAB; }

  void addSymbol(Twine Name, uint8_t Bind, uint8_t Type, SectionBase *DefinedIn,
                 uint64_t Value, uint8_t Visibility, uint16_t Shndx,
                 uint64_t Size);
  void prepareForLayout();
  // An 'empty' symbol table still contains a null symbol.
  bool empty() const { return Symbols.size() == 1; }
  void setShndxTable(SectionIndexSection *ShndxTable) {
    SectionIndexTable = ShndxTable;
  }
  const SectionIndexSection *getShndxTable() const { return SectionIndexTable; }
  const SectionBase *getStrTab() const { return SymbolNames; }
  const Symbol *getSymbolByIndex(uint32_t Index) const;
  Symbol *getSymbolByIndex(uint32_t Index);
  void updateSymbols(function_ref<void(Symbol &)> Callable);

  void removeSectionReferences(const SectionBase *Sec) override;
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;
  void removeSymbols(function_ref<bool(const Symbol &)> ToRemove) override;

  static bool classof(const SectionBase *S) {
    return S->Type == ELF::SHT_SYMTAB;
  }
};

struct Relocation {
  Symbol *RelocSymbol = nullptr;
  uint64_t Offset;
  uint64_t Addend;
  uint32_t Type;
};

// All relocation sections denote relocations to apply to another section.
// However, some relocation sections use a dynamic symbol table and others use
// a regular symbol table. Because the types of the two symbol tables differ in
// our system (because they should behave differently) we can't uniformly
// represent all relocations with the same base class if we expose an interface
// that mentions the symbol table type. So we split the two base types into two
// different classes, one which handles the section the relocation is applied to
// and another which handles the symbol table type. The symbol table type is
// taken as a type parameter to the class (see RelocSectionWithSymtabBase).
class RelocationSectionBase : public SectionBase {
protected:
  SectionBase *SecToApplyRel = nullptr;

public:
  const SectionBase *getSection() const { return SecToApplyRel; }
  void setSection(SectionBase *Sec) { SecToApplyRel = Sec; }

  static bool classof(const SectionBase *S) {
    return S->Type == ELF::SHT_REL || S->Type == ELF::SHT_RELA;
  }
};

// Takes the symbol table type to use as a parameter so that we can deduplicate
// that code between the two symbol table types.
template <class SymTabType>
class RelocSectionWithSymtabBase : public RelocationSectionBase {
  SymTabType *Symbols = nullptr;
  void setSymTab(SymTabType *SymTab) { Symbols = SymTab; }

protected:
  RelocSectionWithSymtabBase() = default;

public:
  void removeSectionReferences(const SectionBase *Sec) override;
  void initialize(SectionTableRef SecTable) override;
  void finalize() override;
};

class RelocationSection
    : public RelocSectionWithSymtabBase<SymbolTableSection> {
  MAKE_SEC_WRITER_FRIEND

  std::vector<Relocation> Relocations;

public:
  void addRelocation(Relocation Rel) { Relocations.push_back(Rel); }
  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;
  void removeSymbols(function_ref<bool(const Symbol &)> ToRemove) override;
  void markSymbols() override;

  static bool classof(const SectionBase *S) {
    if (S->Flags & ELF::SHF_ALLOC)
      return false;
    return S->Type == ELF::SHT_REL || S->Type == ELF::SHT_RELA;
  }
};

// TODO: The way stripping and groups interact is complicated
// and still needs to be worked on.

class GroupSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND
  const SymbolTableSection *SymTab = nullptr;
  Symbol *Sym = nullptr;
  ELF::Elf32_Word FlagWord;
  SmallVector<SectionBase *, 3> GroupMembers;

public:
  // TODO: Contents is present in several classes of the hierarchy.
  // This needs to be refactored to avoid duplication.
  ArrayRef<uint8_t> Contents;

  explicit GroupSection(ArrayRef<uint8_t> Data) : Contents(Data) {}

  void setSymTab(const SymbolTableSection *SymTabSec) { SymTab = SymTabSec; }
  void setSymbol(Symbol *S) { Sym = S; }
  void setFlagWord(ELF::Elf32_Word W) { FlagWord = W; }
  void addMember(SectionBase *Sec) { GroupMembers.push_back(Sec); }

  void accept(SectionVisitor &) const override;
  void accept(MutableSectionVisitor &Visitor) override;
  void finalize() override;
  void removeSymbols(function_ref<bool(const Symbol &)> ToRemove) override;
  void markSymbols() override;

  static bool classof(const SectionBase *S) {
    return S->Type == ELF::SHT_GROUP;
  }
};

class DynamicSymbolTableSection : public Section {
public:
  explicit DynamicSymbolTableSection(ArrayRef<uint8_t> Data) : Section(Data) {}

  static bool classof(const SectionBase *S) {
    return S->Type == ELF::SHT_DYNSYM;
  }
};

class DynamicSection : public Section {
public:
  explicit DynamicSection(ArrayRef<uint8_t> Data) : Section(Data) {}

  static bool classof(const SectionBase *S) {
    return S->Type == ELF::SHT_DYNAMIC;
  }
};

class DynamicRelocationSection
    : public RelocSectionWithSymtabBase<DynamicSymbolTableSection> {
  MAKE_SEC_WRITER_FRIEND

private:
  ArrayRef<uint8_t> Contents;

public:
  explicit DynamicRelocationSection(ArrayRef<uint8_t> Data) : Contents(Data) {}

  void accept(SectionVisitor &) const override;
  void accept(MutableSectionVisitor &Visitor) override;

  static bool classof(const SectionBase *S) {
    if (!(S->Flags & ELF::SHF_ALLOC))
      return false;
    return S->Type == ELF::SHT_REL || S->Type == ELF::SHT_RELA;
  }
};

class GnuDebugLinkSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

private:
  StringRef FileName;
  uint32_t CRC32;

  void init(StringRef File, StringRef Data);

public:
  // If we add this section from an external source we can use this ctor.
  explicit GnuDebugLinkSection(StringRef File);
  void accept(SectionVisitor &Visitor) const override;
  void accept(MutableSectionVisitor &Visitor) override;
};

class Reader {
public:
  virtual ~Reader();
  virtual std::unique_ptr<Object> create() const = 0;
};

using object::Binary;
using object::ELFFile;
using object::ELFObjectFile;
using object::OwningBinary;

class BinaryELFBuilder {
  uint16_t EMachine;
  MemoryBuffer *MemBuf;
  std::unique_ptr<Object> Obj;

  void initFileHeader();
  void initHeaderSegment();
  StringTableSection *addStrTab();
  SymbolTableSection *addSymTab(StringTableSection *StrTab);
  void addData(SymbolTableSection *SymTab);
  void initSections();

public:
  BinaryELFBuilder(uint16_t EM, MemoryBuffer *MB)
      : EMachine(EM), MemBuf(MB), Obj(llvm::make_unique<Object>()) {}

  std::unique_ptr<Object> build();
};

template <class ELFT> class ELFBuilder {
private:
  using Elf_Addr = typename ELFT::Addr;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Word = typename ELFT::Word;

  const ELFFile<ELFT> &ElfFile;
  Object &Obj;

  void setParentSegment(Segment &Child);
  void readProgramHeaders();
  void initGroupSection(GroupSection *GroupSec);
  void initSymbolTable(SymbolTableSection *SymTab);
  void readSectionHeaders();
  SectionBase &makeSection(const Elf_Shdr &Shdr);

public:
  ELFBuilder(const ELFObjectFile<ELFT> &ElfObj, Object &Obj)
      : ElfFile(*ElfObj.getELFFile()), Obj(Obj) {}

  void build();
};

class BinaryReader : public Reader {
  const MachineInfo &MInfo;
  MemoryBuffer *MemBuf;

public:
  BinaryReader(const MachineInfo &MI, MemoryBuffer *MB)
      : MInfo(MI), MemBuf(MB) {}
  std::unique_ptr<Object> create() const override;
};

class ELFReader : public Reader {
  Binary *Bin;

public:
  std::unique_ptr<Object> create() const override;
  explicit ELFReader(Binary *B) : Bin(B) {}
};

class Object {
private:
  using SecPtr = std::unique_ptr<SectionBase>;
  using SegPtr = std::unique_ptr<Segment>;

  std::vector<SecPtr> Sections;
  std::vector<SegPtr> Segments;

public:
  template <class T>
  using Range = iterator_range<
      pointee_iterator<typename std::vector<std::unique_ptr<T>>::iterator>>;

  template <class T>
  using ConstRange = iterator_range<pointee_iterator<
      typename std::vector<std::unique_ptr<T>>::const_iterator>>;

  // It is often the case that the ELF header and the program header table are
  // not present in any segment. This could be a problem during file layout,
  // because other segments may get assigned an offset where either of the
  // two should reside, which will effectively corrupt the resulting binary.
  // Other than that we use these segments to track program header offsets
  // when they may not follow the ELF header.
  Segment ElfHdrSegment;
  Segment ProgramHdrSegment;

  uint8_t OSABI;
  uint8_t ABIVersion;
  uint64_t Entry;
  uint64_t SHOffset;
  uint32_t Type;
  uint32_t Machine;
  uint32_t Version;
  uint32_t Flags;

  StringTableSection *SectionNames = nullptr;
  SymbolTableSection *SymbolTable = nullptr;
  SectionIndexSection *SectionIndexTable = nullptr;

  void sortSections();
  SectionTableRef sections() { return SectionTableRef(Sections); }
  ConstRange<SectionBase> sections() const {
    return make_pointee_range(Sections);
  }
  Range<Segment> segments() { return make_pointee_range(Segments); }
  ConstRange<Segment> segments() const { return make_pointee_range(Segments); }

  void removeSections(std::function<bool(const SectionBase &)> ToRemove);
  void removeSymbols(function_ref<bool(const Symbol &)> ToRemove);
  template <class T, class... Ts> T &addSection(Ts &&... Args) {
    auto Sec = llvm::make_unique<T>(std::forward<Ts>(Args)...);
    auto Ptr = Sec.get();
    Sections.emplace_back(std::move(Sec));
    Ptr->Index = Sections.size();
    return *Ptr;
  }
  Segment &addSegment(ArrayRef<uint8_t> Data) {
    Segments.emplace_back(llvm::make_unique<Segment>(Data));
    return *Segments.back();
  }
};

} // end namespace elf
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_TOOLS_OBJCOPY_OBJECT_H
