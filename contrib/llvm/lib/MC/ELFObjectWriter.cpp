//===- lib/MC/ELFObjectWriter.cpp - ELF File Writer -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements ELF object file writer information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#undef  DEBUG_TYPE
#define DEBUG_TYPE "reloc-info"

namespace {

using SectionIndexMapTy = DenseMap<const MCSectionELF *, uint32_t>;

class ELFObjectWriter;
struct ELFWriter;

bool isDwoSection(const MCSectionELF &Sec) {
  return Sec.getSectionName().endswith(".dwo");
}

class SymbolTableWriter {
  ELFWriter &EWriter;
  bool Is64Bit;

  // indexes we are going to write to .symtab_shndx.
  std::vector<uint32_t> ShndxIndexes;

  // The numbel of symbols written so far.
  unsigned NumWritten;

  void createSymtabShndx();

  template <typename T> void write(T Value);

public:
  SymbolTableWriter(ELFWriter &EWriter, bool Is64Bit);

  void writeSymbol(uint32_t name, uint8_t info, uint64_t value, uint64_t size,
                   uint8_t other, uint32_t shndx, bool Reserved);

  ArrayRef<uint32_t> getShndxIndexes() const { return ShndxIndexes; }
};

struct ELFWriter {
  ELFObjectWriter &OWriter;
  support::endian::Writer W;

  enum DwoMode {
    AllSections,
    NonDwoOnly,
    DwoOnly,
  } Mode;

  static uint64_t SymbolValue(const MCSymbol &Sym, const MCAsmLayout &Layout);
  static bool isInSymtab(const MCAsmLayout &Layout, const MCSymbolELF &Symbol,
                         bool Used, bool Renamed);

  /// Helper struct for containing some precomputed information on symbols.
  struct ELFSymbolData {
    const MCSymbolELF *Symbol;
    uint32_t SectionIndex;
    StringRef Name;

    // Support lexicographic sorting.
    bool operator<(const ELFSymbolData &RHS) const {
      unsigned LHSType = Symbol->getType();
      unsigned RHSType = RHS.Symbol->getType();
      if (LHSType == ELF::STT_SECTION && RHSType != ELF::STT_SECTION)
        return false;
      if (LHSType != ELF::STT_SECTION && RHSType == ELF::STT_SECTION)
        return true;
      if (LHSType == ELF::STT_SECTION && RHSType == ELF::STT_SECTION)
        return SectionIndex < RHS.SectionIndex;
      return Name < RHS.Name;
    }
  };

  /// @}
  /// @name Symbol Table Data
  /// @{

  StringTableBuilder StrTabBuilder{StringTableBuilder::ELF};

  /// @}

  // This holds the symbol table index of the last local symbol.
  unsigned LastLocalSymbolIndex;
  // This holds the .strtab section index.
  unsigned StringTableIndex;
  // This holds the .symtab section index.
  unsigned SymbolTableIndex;

  // Sections in the order they are to be output in the section table.
  std::vector<const MCSectionELF *> SectionTable;
  unsigned addToSectionTable(const MCSectionELF *Sec);

  // TargetObjectWriter wrappers.
  bool is64Bit() const;
  bool hasRelocationAddend() const;

  void align(unsigned Alignment);

  bool maybeWriteCompression(uint64_t Size,
                             SmallVectorImpl<char> &CompressedContents,
                             bool ZLibStyle, unsigned Alignment);

public:
  ELFWriter(ELFObjectWriter &OWriter, raw_pwrite_stream &OS,
            bool IsLittleEndian, DwoMode Mode)
      : OWriter(OWriter),
        W(OS, IsLittleEndian ? support::little : support::big), Mode(Mode) {}

  void WriteWord(uint64_t Word) {
    if (is64Bit())
      W.write<uint64_t>(Word);
    else
      W.write<uint32_t>(Word);
  }

  template <typename T> void write(T Val) {
    W.write(Val);
  }

  void writeHeader(const MCAssembler &Asm);

  void writeSymbol(SymbolTableWriter &Writer, uint32_t StringIndex,
                   ELFSymbolData &MSD, const MCAsmLayout &Layout);

  // Start and end offset of each section
  using SectionOffsetsTy =
      std::map<const MCSectionELF *, std::pair<uint64_t, uint64_t>>;

  // Map from a signature symbol to the group section index
  using RevGroupMapTy = DenseMap<const MCSymbol *, unsigned>;

  /// Compute the symbol table data
  ///
  /// \param Asm - The assembler.
  /// \param SectionIndexMap - Maps a section to its index.
  /// \param RevGroupMap - Maps a signature symbol to the group section.
  void computeSymbolTable(MCAssembler &Asm, const MCAsmLayout &Layout,
                          const SectionIndexMapTy &SectionIndexMap,
                          const RevGroupMapTy &RevGroupMap,
                          SectionOffsetsTy &SectionOffsets);

  void writeAddrsigSection();

  MCSectionELF *createRelocationSection(MCContext &Ctx,
                                        const MCSectionELF &Sec);

  const MCSectionELF *createStringTable(MCContext &Ctx);

  void writeSectionHeader(const MCAsmLayout &Layout,
                          const SectionIndexMapTy &SectionIndexMap,
                          const SectionOffsetsTy &SectionOffsets);

  void writeSectionData(const MCAssembler &Asm, MCSection &Sec,
                        const MCAsmLayout &Layout);

  void WriteSecHdrEntry(uint32_t Name, uint32_t Type, uint64_t Flags,
                        uint64_t Address, uint64_t Offset, uint64_t Size,
                        uint32_t Link, uint32_t Info, uint64_t Alignment,
                        uint64_t EntrySize);

  void writeRelocations(const MCAssembler &Asm, const MCSectionELF &Sec);

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout);
  void writeSection(const SectionIndexMapTy &SectionIndexMap,
                    uint32_t GroupSymbolIndex, uint64_t Offset, uint64_t Size,
                    const MCSectionELF &Section);
};

class ELFObjectWriter : public MCObjectWriter {
  /// The target specific ELF writer instance.
  std::unique_ptr<MCELFObjectTargetWriter> TargetObjectWriter;

  DenseMap<const MCSectionELF *, std::vector<ELFRelocationEntry>> Relocations;

  DenseMap<const MCSymbolELF *, const MCSymbolELF *> Renames;

  bool EmitAddrsigSection = false;
  std::vector<const MCSymbol *> AddrsigSyms;

  bool hasRelocationAddend() const;

  bool shouldRelocateWithSymbol(const MCAssembler &Asm,
                                const MCSymbolRefExpr *RefA,
                                const MCSymbolELF *Sym, uint64_t C,
                                unsigned Type) const;

public:
  ELFObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW)
      : TargetObjectWriter(std::move(MOTW)) {}

  void reset() override {
    Relocations.clear();
    Renames.clear();
    MCObjectWriter::reset();
  }

  bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                              const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;

  virtual bool checkRelocation(MCContext &Ctx, SMLoc Loc,
                               const MCSectionELF *From,
                               const MCSectionELF *To) {
    return true;
  }

  void recordRelocation(MCAssembler &Asm, const MCAsmLayout &Layout,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;

  void executePostLayoutBinding(MCAssembler &Asm,
                                const MCAsmLayout &Layout) override;

  void emitAddrsigSection() override { EmitAddrsigSection = true; }
  void addAddrsigSymbol(const MCSymbol *Sym) override {
    AddrsigSyms.push_back(Sym);
  }

  friend struct ELFWriter;
};

class ELFSingleObjectWriter : public ELFObjectWriter {
  raw_pwrite_stream &OS;
  bool IsLittleEndian;

public:
  ELFSingleObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                        raw_pwrite_stream &OS, bool IsLittleEndian)
      : ELFObjectWriter(std::move(MOTW)), OS(OS),
        IsLittleEndian(IsLittleEndian) {}

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) override {
    return ELFWriter(*this, OS, IsLittleEndian, ELFWriter::AllSections)
        .writeObject(Asm, Layout);
  }

  friend struct ELFWriter;
};

class ELFDwoObjectWriter : public ELFObjectWriter {
  raw_pwrite_stream &OS, &DwoOS;
  bool IsLittleEndian;

public:
  ELFDwoObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                     raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS,
                     bool IsLittleEndian)
      : ELFObjectWriter(std::move(MOTW)), OS(OS), DwoOS(DwoOS),
        IsLittleEndian(IsLittleEndian) {}

  virtual bool checkRelocation(MCContext &Ctx, SMLoc Loc,
                               const MCSectionELF *From,
                               const MCSectionELF *To) override {
    if (isDwoSection(*From)) {
      Ctx.reportError(Loc, "A dwo section may not contain relocations");
      return false;
    }
    if (To && isDwoSection(*To)) {
      Ctx.reportError(Loc, "A relocation may not refer to a dwo section");
      return false;
    }
    return true;
  }

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) override {
    uint64_t Size = ELFWriter(*this, OS, IsLittleEndian, ELFWriter::NonDwoOnly)
                        .writeObject(Asm, Layout);
    Size += ELFWriter(*this, DwoOS, IsLittleEndian, ELFWriter::DwoOnly)
                .writeObject(Asm, Layout);
    return Size;
  }
};

} // end anonymous namespace

void ELFWriter::align(unsigned Alignment) {
  uint64_t Padding = OffsetToAlignment(W.OS.tell(), Alignment);
  W.OS.write_zeros(Padding);
}

unsigned ELFWriter::addToSectionTable(const MCSectionELF *Sec) {
  SectionTable.push_back(Sec);
  StrTabBuilder.add(Sec->getSectionName());
  return SectionTable.size();
}

void SymbolTableWriter::createSymtabShndx() {
  if (!ShndxIndexes.empty())
    return;

  ShndxIndexes.resize(NumWritten);
}

template <typename T> void SymbolTableWriter::write(T Value) {
  EWriter.write(Value);
}

SymbolTableWriter::SymbolTableWriter(ELFWriter &EWriter, bool Is64Bit)
    : EWriter(EWriter), Is64Bit(Is64Bit), NumWritten(0) {}

void SymbolTableWriter::writeSymbol(uint32_t name, uint8_t info, uint64_t value,
                                    uint64_t size, uint8_t other,
                                    uint32_t shndx, bool Reserved) {
  bool LargeIndex = shndx >= ELF::SHN_LORESERVE && !Reserved;

  if (LargeIndex)
    createSymtabShndx();

  if (!ShndxIndexes.empty()) {
    if (LargeIndex)
      ShndxIndexes.push_back(shndx);
    else
      ShndxIndexes.push_back(0);
  }

  uint16_t Index = LargeIndex ? uint16_t(ELF::SHN_XINDEX) : shndx;

  if (Is64Bit) {
    write(name);  // st_name
    write(info);  // st_info
    write(other); // st_other
    write(Index); // st_shndx
    write(value); // st_value
    write(size);  // st_size
  } else {
    write(name);            // st_name
    write(uint32_t(value)); // st_value
    write(uint32_t(size));  // st_size
    write(info);            // st_info
    write(other);           // st_other
    write(Index);           // st_shndx
  }

  ++NumWritten;
}

bool ELFWriter::is64Bit() const {
  return OWriter.TargetObjectWriter->is64Bit();
}

bool ELFWriter::hasRelocationAddend() const {
  return OWriter.hasRelocationAddend();
}

// Emit the ELF header.
void ELFWriter::writeHeader(const MCAssembler &Asm) {
  // ELF Header
  // ----------
  //
  // Note
  // ----
  // emitWord method behaves differently for ELF32 and ELF64, writing
  // 4 bytes in the former and 8 in the latter.

  W.OS << ELF::ElfMagic; // e_ident[EI_MAG0] to e_ident[EI_MAG3]

  W.OS << char(is64Bit() ? ELF::ELFCLASS64 : ELF::ELFCLASS32); // e_ident[EI_CLASS]

  // e_ident[EI_DATA]
  W.OS << char(W.Endian == support::little ? ELF::ELFDATA2LSB
                                           : ELF::ELFDATA2MSB);

  W.OS << char(ELF::EV_CURRENT);        // e_ident[EI_VERSION]
  // e_ident[EI_OSABI]
  W.OS << char(OWriter.TargetObjectWriter->getOSABI());
  W.OS << char(0);                  // e_ident[EI_ABIVERSION]

  W.OS.write_zeros(ELF::EI_NIDENT - ELF::EI_PAD);

  W.write<uint16_t>(ELF::ET_REL);             // e_type

  W.write<uint16_t>(OWriter.TargetObjectWriter->getEMachine()); // e_machine = target

  W.write<uint32_t>(ELF::EV_CURRENT);         // e_version
  WriteWord(0);                    // e_entry, no entry point in .o file
  WriteWord(0);                    // e_phoff, no program header for .o
  WriteWord(0);                     // e_shoff = sec hdr table off in bytes

  // e_flags = whatever the target wants
  W.write<uint32_t>(Asm.getELFHeaderEFlags());

  // e_ehsize = ELF header size
  W.write<uint16_t>(is64Bit() ? sizeof(ELF::Elf64_Ehdr)
                              : sizeof(ELF::Elf32_Ehdr));

  W.write<uint16_t>(0);                  // e_phentsize = prog header entry size
  W.write<uint16_t>(0);                  // e_phnum = # prog header entries = 0

  // e_shentsize = Section header entry size
  W.write<uint16_t>(is64Bit() ? sizeof(ELF::Elf64_Shdr)
                              : sizeof(ELF::Elf32_Shdr));

  // e_shnum     = # of section header ents
  W.write<uint16_t>(0);

  // e_shstrndx  = Section # of '.shstrtab'
  assert(StringTableIndex < ELF::SHN_LORESERVE);
  W.write<uint16_t>(StringTableIndex);
}

uint64_t ELFWriter::SymbolValue(const MCSymbol &Sym,
                                const MCAsmLayout &Layout) {
  if (Sym.isCommon() && Sym.isExternal())
    return Sym.getCommonAlignment();

  uint64_t Res;
  if (!Layout.getSymbolOffset(Sym, Res))
    return 0;

  if (Layout.getAssembler().isThumbFunc(&Sym))
    Res |= 1;

  return Res;
}

static uint8_t mergeTypeForSet(uint8_t origType, uint8_t newType) {
  uint8_t Type = newType;

  // Propagation rules:
  // IFUNC > FUNC > OBJECT > NOTYPE
  // TLS_OBJECT > OBJECT > NOTYPE
  //
  // dont let the new type degrade the old type
  switch (origType) {
  default:
    break;
  case ELF::STT_GNU_IFUNC:
    if (Type == ELF::STT_FUNC || Type == ELF::STT_OBJECT ||
        Type == ELF::STT_NOTYPE || Type == ELF::STT_TLS)
      Type = ELF::STT_GNU_IFUNC;
    break;
  case ELF::STT_FUNC:
    if (Type == ELF::STT_OBJECT || Type == ELF::STT_NOTYPE ||
        Type == ELF::STT_TLS)
      Type = ELF::STT_FUNC;
    break;
  case ELF::STT_OBJECT:
    if (Type == ELF::STT_NOTYPE)
      Type = ELF::STT_OBJECT;
    break;
  case ELF::STT_TLS:
    if (Type == ELF::STT_OBJECT || Type == ELF::STT_NOTYPE ||
        Type == ELF::STT_GNU_IFUNC || Type == ELF::STT_FUNC)
      Type = ELF::STT_TLS;
    break;
  }

  return Type;
}

void ELFWriter::writeSymbol(SymbolTableWriter &Writer, uint32_t StringIndex,
                            ELFSymbolData &MSD, const MCAsmLayout &Layout) {
  const auto &Symbol = cast<MCSymbolELF>(*MSD.Symbol);
  const MCSymbolELF *Base =
      cast_or_null<MCSymbolELF>(Layout.getBaseSymbol(Symbol));

  // This has to be in sync with when computeSymbolTable uses SHN_ABS or
  // SHN_COMMON.
  bool IsReserved = !Base || Symbol.isCommon();

  // Binding and Type share the same byte as upper and lower nibbles
  uint8_t Binding = Symbol.getBinding();
  uint8_t Type = Symbol.getType();
  if (Base) {
    Type = mergeTypeForSet(Type, Base->getType());
  }
  uint8_t Info = (Binding << 4) | Type;

  // Other and Visibility share the same byte with Visibility using the lower
  // 2 bits
  uint8_t Visibility = Symbol.getVisibility();
  uint8_t Other = Symbol.getOther() | Visibility;

  uint64_t Value = SymbolValue(*MSD.Symbol, Layout);
  uint64_t Size = 0;

  const MCExpr *ESize = MSD.Symbol->getSize();
  if (!ESize && Base)
    ESize = Base->getSize();

  if (ESize) {
    int64_t Res;
    if (!ESize->evaluateKnownAbsolute(Res, Layout))
      report_fatal_error("Size expression must be absolute.");
    Size = Res;
  }

  // Write out the symbol table entry
  Writer.writeSymbol(StringIndex, Info, Value, Size, Other, MSD.SectionIndex,
                     IsReserved);
}

// True if the assembler knows nothing about the final value of the symbol.
// This doesn't cover the comdat issues, since in those cases the assembler
// can at least know that all symbols in the section will move together.
static bool isWeak(const MCSymbolELF &Sym) {
  if (Sym.getType() == ELF::STT_GNU_IFUNC)
    return true;

  switch (Sym.getBinding()) {
  default:
    llvm_unreachable("Unknown binding");
  case ELF::STB_LOCAL:
    return false;
  case ELF::STB_GLOBAL:
    return false;
  case ELF::STB_WEAK:
  case ELF::STB_GNU_UNIQUE:
    return true;
  }
}

bool ELFWriter::isInSymtab(const MCAsmLayout &Layout, const MCSymbolELF &Symbol,
                           bool Used, bool Renamed) {
  if (Symbol.isVariable()) {
    const MCExpr *Expr = Symbol.getVariableValue();
    if (const MCSymbolRefExpr *Ref = dyn_cast<MCSymbolRefExpr>(Expr)) {
      if (Ref->getKind() == MCSymbolRefExpr::VK_WEAKREF)
        return false;
    }
  }

  if (Used)
    return true;

  if (Renamed)
    return false;

  if (Symbol.isVariable() && Symbol.isUndefined()) {
    // FIXME: this is here just to diagnose the case of a var = commmon_sym.
    Layout.getBaseSymbol(Symbol);
    return false;
  }

  if (Symbol.isUndefined() && !Symbol.isBindingSet())
    return false;

  if (Symbol.isTemporary())
    return false;

  if (Symbol.getType() == ELF::STT_SECTION)
    return false;

  return true;
}

void ELFWriter::computeSymbolTable(
    MCAssembler &Asm, const MCAsmLayout &Layout,
    const SectionIndexMapTy &SectionIndexMap, const RevGroupMapTy &RevGroupMap,
    SectionOffsetsTy &SectionOffsets) {
  MCContext &Ctx = Asm.getContext();
  SymbolTableWriter Writer(*this, is64Bit());

  // Symbol table
  unsigned EntrySize = is64Bit() ? ELF::SYMENTRY_SIZE64 : ELF::SYMENTRY_SIZE32;
  MCSectionELF *SymtabSection =
      Ctx.getELFSection(".symtab", ELF::SHT_SYMTAB, 0, EntrySize, "");
  SymtabSection->setAlignment(is64Bit() ? 8 : 4);
  SymbolTableIndex = addToSectionTable(SymtabSection);

  align(SymtabSection->getAlignment());
  uint64_t SecStart = W.OS.tell();

  // The first entry is the undefined symbol entry.
  Writer.writeSymbol(0, 0, 0, 0, 0, 0, false);

  std::vector<ELFSymbolData> LocalSymbolData;
  std::vector<ELFSymbolData> ExternalSymbolData;

  // Add the data for the symbols.
  bool HasLargeSectionIndex = false;
  for (const MCSymbol &S : Asm.symbols()) {
    const auto &Symbol = cast<MCSymbolELF>(S);
    bool Used = Symbol.isUsedInReloc();
    bool WeakrefUsed = Symbol.isWeakrefUsedInReloc();
    bool isSignature = Symbol.isSignature();

    if (!isInSymtab(Layout, Symbol, Used || WeakrefUsed || isSignature,
                    OWriter.Renames.count(&Symbol)))
      continue;

    if (Symbol.isTemporary() && Symbol.isUndefined()) {
      Ctx.reportError(SMLoc(), "Undefined temporary symbol");
      continue;
    }

    ELFSymbolData MSD;
    MSD.Symbol = cast<MCSymbolELF>(&Symbol);

    bool Local = Symbol.getBinding() == ELF::STB_LOCAL;
    assert(Local || !Symbol.isTemporary());

    if (Symbol.isAbsolute()) {
      MSD.SectionIndex = ELF::SHN_ABS;
    } else if (Symbol.isCommon()) {
      assert(!Local);
      MSD.SectionIndex = ELF::SHN_COMMON;
    } else if (Symbol.isUndefined()) {
      if (isSignature && !Used) {
        MSD.SectionIndex = RevGroupMap.lookup(&Symbol);
        if (MSD.SectionIndex >= ELF::SHN_LORESERVE)
          HasLargeSectionIndex = true;
      } else {
        MSD.SectionIndex = ELF::SHN_UNDEF;
      }
    } else {
      const MCSectionELF &Section =
          static_cast<const MCSectionELF &>(Symbol.getSection());

      // We may end up with a situation when section symbol is technically
      // defined, but should not be. That happens because we explicitly
      // pre-create few .debug_* sections to have accessors.
      // And if these sections were not really defined in the code, but were
      // referenced, we simply error out.
      if (!Section.isRegistered()) {
        assert(static_cast<const MCSymbolELF &>(Symbol).getType() ==
               ELF::STT_SECTION);
        Ctx.reportError(SMLoc(),
                        "Undefined section reference: " + Symbol.getName());
        continue;
      }

      if (Mode == NonDwoOnly && isDwoSection(Section))
        continue;
      MSD.SectionIndex = SectionIndexMap.lookup(&Section);
      assert(MSD.SectionIndex && "Invalid section index!");
      if (MSD.SectionIndex >= ELF::SHN_LORESERVE)
        HasLargeSectionIndex = true;
    }

    StringRef Name = Symbol.getName();

    // Sections have their own string table
    if (Symbol.getType() != ELF::STT_SECTION) {
      MSD.Name = Name;
      StrTabBuilder.add(Name);
    }

    if (Local)
      LocalSymbolData.push_back(MSD);
    else
      ExternalSymbolData.push_back(MSD);
  }

  // This holds the .symtab_shndx section index.
  unsigned SymtabShndxSectionIndex = 0;

  if (HasLargeSectionIndex) {
    MCSectionELF *SymtabShndxSection =
        Ctx.getELFSection(".symtab_shndxr", ELF::SHT_SYMTAB_SHNDX, 0, 4, "");
    SymtabShndxSectionIndex = addToSectionTable(SymtabShndxSection);
    SymtabShndxSection->setAlignment(4);
  }

  ArrayRef<std::string> FileNames = Asm.getFileNames();
  for (const std::string &Name : FileNames)
    StrTabBuilder.add(Name);

  StrTabBuilder.finalize();

  // File symbols are emitted first and handled separately from normal symbols,
  // i.e. a non-STT_FILE symbol with the same name may appear.
  for (const std::string &Name : FileNames)
    Writer.writeSymbol(StrTabBuilder.getOffset(Name),
                       ELF::STT_FILE | ELF::STB_LOCAL, 0, 0, ELF::STV_DEFAULT,
                       ELF::SHN_ABS, true);

  // Symbols are required to be in lexicographic order.
  array_pod_sort(LocalSymbolData.begin(), LocalSymbolData.end());
  array_pod_sort(ExternalSymbolData.begin(), ExternalSymbolData.end());

  // Set the symbol indices. Local symbols must come before all other
  // symbols with non-local bindings.
  unsigned Index = FileNames.size() + 1;

  for (ELFSymbolData &MSD : LocalSymbolData) {
    unsigned StringIndex = MSD.Symbol->getType() == ELF::STT_SECTION
                               ? 0
                               : StrTabBuilder.getOffset(MSD.Name);
    MSD.Symbol->setIndex(Index++);
    writeSymbol(Writer, StringIndex, MSD, Layout);
  }

  // Write the symbol table entries.
  LastLocalSymbolIndex = Index;

  for (ELFSymbolData &MSD : ExternalSymbolData) {
    unsigned StringIndex = StrTabBuilder.getOffset(MSD.Name);
    MSD.Symbol->setIndex(Index++);
    writeSymbol(Writer, StringIndex, MSD, Layout);
    assert(MSD.Symbol->getBinding() != ELF::STB_LOCAL);
  }

  uint64_t SecEnd = W.OS.tell();
  SectionOffsets[SymtabSection] = std::make_pair(SecStart, SecEnd);

  ArrayRef<uint32_t> ShndxIndexes = Writer.getShndxIndexes();
  if (ShndxIndexes.empty()) {
    assert(SymtabShndxSectionIndex == 0);
    return;
  }
  assert(SymtabShndxSectionIndex != 0);

  SecStart = W.OS.tell();
  const MCSectionELF *SymtabShndxSection =
      SectionTable[SymtabShndxSectionIndex - 1];
  for (uint32_t Index : ShndxIndexes)
    write(Index);
  SecEnd = W.OS.tell();
  SectionOffsets[SymtabShndxSection] = std::make_pair(SecStart, SecEnd);
}

void ELFWriter::writeAddrsigSection() {
  for (const MCSymbol *Sym : OWriter.AddrsigSyms)
    encodeULEB128(Sym->getIndex(), W.OS);
}

MCSectionELF *ELFWriter::createRelocationSection(MCContext &Ctx,
                                                 const MCSectionELF &Sec) {
  if (OWriter.Relocations[&Sec].empty())
    return nullptr;

  const StringRef SectionName = Sec.getSectionName();
  std::string RelaSectionName = hasRelocationAddend() ? ".rela" : ".rel";
  RelaSectionName += SectionName;

  unsigned EntrySize;
  if (hasRelocationAddend())
    EntrySize = is64Bit() ? sizeof(ELF::Elf64_Rela) : sizeof(ELF::Elf32_Rela);
  else
    EntrySize = is64Bit() ? sizeof(ELF::Elf64_Rel) : sizeof(ELF::Elf32_Rel);

  unsigned Flags = 0;
  if (Sec.getFlags() & ELF::SHF_GROUP)
    Flags = ELF::SHF_GROUP;

  MCSectionELF *RelaSection = Ctx.createELFRelSection(
      RelaSectionName, hasRelocationAddend() ? ELF::SHT_RELA : ELF::SHT_REL,
      Flags, EntrySize, Sec.getGroup(), &Sec);
  RelaSection->setAlignment(is64Bit() ? 8 : 4);
  return RelaSection;
}

// Include the debug info compression header.
bool ELFWriter::maybeWriteCompression(
    uint64_t Size, SmallVectorImpl<char> &CompressedContents, bool ZLibStyle,
    unsigned Alignment) {
  if (ZLibStyle) {
    uint64_t HdrSize =
        is64Bit() ? sizeof(ELF::Elf32_Chdr) : sizeof(ELF::Elf64_Chdr);
    if (Size <= HdrSize + CompressedContents.size())
      return false;
    // Platform specific header is followed by compressed data.
    if (is64Bit()) {
      // Write Elf64_Chdr header.
      write(static_cast<ELF::Elf64_Word>(ELF::ELFCOMPRESS_ZLIB));
      write(static_cast<ELF::Elf64_Word>(0)); // ch_reserved field.
      write(static_cast<ELF::Elf64_Xword>(Size));
      write(static_cast<ELF::Elf64_Xword>(Alignment));
    } else {
      // Write Elf32_Chdr header otherwise.
      write(static_cast<ELF::Elf32_Word>(ELF::ELFCOMPRESS_ZLIB));
      write(static_cast<ELF::Elf32_Word>(Size));
      write(static_cast<ELF::Elf32_Word>(Alignment));
    }
    return true;
  }

  // "ZLIB" followed by 8 bytes representing the uncompressed size of the section,
  // useful for consumers to preallocate a buffer to decompress into.
  const StringRef Magic = "ZLIB";
  if (Size <= Magic.size() + sizeof(Size) + CompressedContents.size())
    return false;
  W.OS << Magic;
  support::endian::write(W.OS, Size, support::big);
  return true;
}

void ELFWriter::writeSectionData(const MCAssembler &Asm, MCSection &Sec,
                                 const MCAsmLayout &Layout) {
  MCSectionELF &Section = static_cast<MCSectionELF &>(Sec);
  StringRef SectionName = Section.getSectionName();

  auto &MC = Asm.getContext();
  const auto &MAI = MC.getAsmInfo();

  // Compressing debug_frame requires handling alignment fragments which is
  // more work (possibly generalizing MCAssembler.cpp:writeFragment to allow
  // for writing to arbitrary buffers) for little benefit.
  bool CompressionEnabled =
      MAI->compressDebugSections() != DebugCompressionType::None;
  if (!CompressionEnabled || !SectionName.startswith(".debug_") ||
      SectionName == ".debug_frame") {
    Asm.writeSectionData(W.OS, &Section, Layout);
    return;
  }

  assert((MAI->compressDebugSections() == DebugCompressionType::Z ||
          MAI->compressDebugSections() == DebugCompressionType::GNU) &&
         "expected zlib or zlib-gnu style compression");

  SmallVector<char, 128> UncompressedData;
  raw_svector_ostream VecOS(UncompressedData);
  Asm.writeSectionData(VecOS, &Section, Layout);

  SmallVector<char, 128> CompressedContents;
  if (Error E = zlib::compress(
          StringRef(UncompressedData.data(), UncompressedData.size()),
          CompressedContents)) {
    consumeError(std::move(E));
    W.OS << UncompressedData;
    return;
  }

  bool ZlibStyle = MAI->compressDebugSections() == DebugCompressionType::Z;
  if (!maybeWriteCompression(UncompressedData.size(), CompressedContents,
                             ZlibStyle, Sec.getAlignment())) {
    W.OS << UncompressedData;
    return;
  }

  if (ZlibStyle)
    // Set the compressed flag. That is zlib style.
    Section.setFlags(Section.getFlags() | ELF::SHF_COMPRESSED);
  else
    // Add "z" prefix to section name. This is zlib-gnu style.
    MC.renameELFSection(&Section, (".z" + SectionName.drop_front(1)).str());
  W.OS << CompressedContents;
}

void ELFWriter::WriteSecHdrEntry(uint32_t Name, uint32_t Type, uint64_t Flags,
                                 uint64_t Address, uint64_t Offset,
                                 uint64_t Size, uint32_t Link, uint32_t Info,
                                 uint64_t Alignment, uint64_t EntrySize) {
  W.write<uint32_t>(Name);        // sh_name: index into string table
  W.write<uint32_t>(Type);        // sh_type
  WriteWord(Flags);     // sh_flags
  WriteWord(Address);   // sh_addr
  WriteWord(Offset);    // sh_offset
  WriteWord(Size);      // sh_size
  W.write<uint32_t>(Link);        // sh_link
  W.write<uint32_t>(Info);        // sh_info
  WriteWord(Alignment); // sh_addralign
  WriteWord(EntrySize); // sh_entsize
}

void ELFWriter::writeRelocations(const MCAssembler &Asm,
                                       const MCSectionELF &Sec) {
  std::vector<ELFRelocationEntry> &Relocs = OWriter.Relocations[&Sec];

  // We record relocations by pushing to the end of a vector. Reverse the vector
  // to get the relocations in the order they were created.
  // In most cases that is not important, but it can be for special sections
  // (.eh_frame) or specific relocations (TLS optimizations on SystemZ).
  std::reverse(Relocs.begin(), Relocs.end());

  // Sort the relocation entries. MIPS needs this.
  OWriter.TargetObjectWriter->sortRelocs(Asm, Relocs);

  for (unsigned i = 0, e = Relocs.size(); i != e; ++i) {
    const ELFRelocationEntry &Entry = Relocs[e - i - 1];
    unsigned Index = Entry.Symbol ? Entry.Symbol->getIndex() : 0;

    if (is64Bit()) {
      write(Entry.Offset);
      if (OWriter.TargetObjectWriter->getEMachine() == ELF::EM_MIPS) {
        write(uint32_t(Index));

        write(OWriter.TargetObjectWriter->getRSsym(Entry.Type));
        write(OWriter.TargetObjectWriter->getRType3(Entry.Type));
        write(OWriter.TargetObjectWriter->getRType2(Entry.Type));
        write(OWriter.TargetObjectWriter->getRType(Entry.Type));
      } else {
        struct ELF::Elf64_Rela ERE64;
        ERE64.setSymbolAndType(Index, Entry.Type);
        write(ERE64.r_info);
      }
      if (hasRelocationAddend())
        write(Entry.Addend);
    } else {
      write(uint32_t(Entry.Offset));

      struct ELF::Elf32_Rela ERE32;
      ERE32.setSymbolAndType(Index, Entry.Type);
      write(ERE32.r_info);

      if (hasRelocationAddend())
        write(uint32_t(Entry.Addend));

      if (OWriter.TargetObjectWriter->getEMachine() == ELF::EM_MIPS) {
        if (uint32_t RType =
                OWriter.TargetObjectWriter->getRType2(Entry.Type)) {
          write(uint32_t(Entry.Offset));

          ERE32.setSymbolAndType(0, RType);
          write(ERE32.r_info);
          write(uint32_t(0));
        }
        if (uint32_t RType =
                OWriter.TargetObjectWriter->getRType3(Entry.Type)) {
          write(uint32_t(Entry.Offset));

          ERE32.setSymbolAndType(0, RType);
          write(ERE32.r_info);
          write(uint32_t(0));
        }
      }
    }
  }
}

const MCSectionELF *ELFWriter::createStringTable(MCContext &Ctx) {
  const MCSectionELF *StrtabSection = SectionTable[StringTableIndex - 1];
  StrTabBuilder.write(W.OS);
  return StrtabSection;
}

void ELFWriter::writeSection(const SectionIndexMapTy &SectionIndexMap,
                             uint32_t GroupSymbolIndex, uint64_t Offset,
                             uint64_t Size, const MCSectionELF &Section) {
  uint64_t sh_link = 0;
  uint64_t sh_info = 0;

  switch(Section.getType()) {
  default:
    // Nothing to do.
    break;

  case ELF::SHT_DYNAMIC:
    llvm_unreachable("SHT_DYNAMIC in a relocatable object");

  case ELF::SHT_REL:
  case ELF::SHT_RELA: {
    sh_link = SymbolTableIndex;
    assert(sh_link && ".symtab not found");
    const MCSection *InfoSection = Section.getAssociatedSection();
    sh_info = SectionIndexMap.lookup(cast<MCSectionELF>(InfoSection));
    break;
  }

  case ELF::SHT_SYMTAB:
    sh_link = StringTableIndex;
    sh_info = LastLocalSymbolIndex;
    break;

  case ELF::SHT_SYMTAB_SHNDX:
  case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
  case ELF::SHT_LLVM_ADDRSIG:
    sh_link = SymbolTableIndex;
    break;

  case ELF::SHT_GROUP:
    sh_link = SymbolTableIndex;
    sh_info = GroupSymbolIndex;
    break;
  }

  if (Section.getFlags() & ELF::SHF_LINK_ORDER) {
    const MCSymbol *Sym = Section.getAssociatedSymbol();
    const MCSectionELF *Sec = cast<MCSectionELF>(&Sym->getSection());
    sh_link = SectionIndexMap.lookup(Sec);
  }

  WriteSecHdrEntry(StrTabBuilder.getOffset(Section.getSectionName()),
                   Section.getType(), Section.getFlags(), 0, Offset, Size,
                   sh_link, sh_info, Section.getAlignment(),
                   Section.getEntrySize());
}

void ELFWriter::writeSectionHeader(
    const MCAsmLayout &Layout, const SectionIndexMapTy &SectionIndexMap,
    const SectionOffsetsTy &SectionOffsets) {
  const unsigned NumSections = SectionTable.size();

  // Null section first.
  uint64_t FirstSectionSize =
      (NumSections + 1) >= ELF::SHN_LORESERVE ? NumSections + 1 : 0;
  WriteSecHdrEntry(0, 0, 0, 0, 0, FirstSectionSize, 0, 0, 0, 0);

  for (const MCSectionELF *Section : SectionTable) {
    uint32_t GroupSymbolIndex;
    unsigned Type = Section->getType();
    if (Type != ELF::SHT_GROUP)
      GroupSymbolIndex = 0;
    else
      GroupSymbolIndex = Section->getGroup()->getIndex();

    const std::pair<uint64_t, uint64_t> &Offsets =
        SectionOffsets.find(Section)->second;
    uint64_t Size;
    if (Type == ELF::SHT_NOBITS)
      Size = Layout.getSectionAddressSize(Section);
    else
      Size = Offsets.second - Offsets.first;

    writeSection(SectionIndexMap, GroupSymbolIndex, Offsets.first, Size,
                 *Section);
  }
}

uint64_t ELFWriter::writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) {
  uint64_t StartOffset = W.OS.tell();

  MCContext &Ctx = Asm.getContext();
  MCSectionELF *StrtabSection =
      Ctx.getELFSection(".strtab", ELF::SHT_STRTAB, 0);
  StringTableIndex = addToSectionTable(StrtabSection);

  RevGroupMapTy RevGroupMap;
  SectionIndexMapTy SectionIndexMap;

  std::map<const MCSymbol *, std::vector<const MCSectionELF *>> GroupMembers;

  // Write out the ELF header ...
  writeHeader(Asm);

  // ... then the sections ...
  SectionOffsetsTy SectionOffsets;
  std::vector<MCSectionELF *> Groups;
  std::vector<MCSectionELF *> Relocations;
  for (MCSection &Sec : Asm) {
    MCSectionELF &Section = static_cast<MCSectionELF &>(Sec);
    if (Mode == NonDwoOnly && isDwoSection(Section))
      continue;
    if (Mode == DwoOnly && !isDwoSection(Section))
      continue;

    align(Section.getAlignment());

    // Remember the offset into the file for this section.
    uint64_t SecStart = W.OS.tell();

    const MCSymbolELF *SignatureSymbol = Section.getGroup();
    writeSectionData(Asm, Section, Layout);

    uint64_t SecEnd = W.OS.tell();
    SectionOffsets[&Section] = std::make_pair(SecStart, SecEnd);

    MCSectionELF *RelSection = createRelocationSection(Ctx, Section);

    if (SignatureSymbol) {
      Asm.registerSymbol(*SignatureSymbol);
      unsigned &GroupIdx = RevGroupMap[SignatureSymbol];
      if (!GroupIdx) {
        MCSectionELF *Group = Ctx.createELFGroupSection(SignatureSymbol);
        GroupIdx = addToSectionTable(Group);
        Group->setAlignment(4);
        Groups.push_back(Group);
      }
      std::vector<const MCSectionELF *> &Members =
          GroupMembers[SignatureSymbol];
      Members.push_back(&Section);
      if (RelSection)
        Members.push_back(RelSection);
    }

    SectionIndexMap[&Section] = addToSectionTable(&Section);
    if (RelSection) {
      SectionIndexMap[RelSection] = addToSectionTable(RelSection);
      Relocations.push_back(RelSection);
    }

    OWriter.TargetObjectWriter->addTargetSectionFlags(Ctx, Section);
  }

  MCSectionELF *CGProfileSection = nullptr;
  if (!Asm.CGProfile.empty()) {
    CGProfileSection = Ctx.getELFSection(".llvm.call-graph-profile",
                                         ELF::SHT_LLVM_CALL_GRAPH_PROFILE,
                                         ELF::SHF_EXCLUDE, 16, "");
    SectionIndexMap[CGProfileSection] = addToSectionTable(CGProfileSection);
  }

  for (MCSectionELF *Group : Groups) {
    align(Group->getAlignment());

    // Remember the offset into the file for this section.
    uint64_t SecStart = W.OS.tell();

    const MCSymbol *SignatureSymbol = Group->getGroup();
    assert(SignatureSymbol);
    write(uint32_t(ELF::GRP_COMDAT));
    for (const MCSectionELF *Member : GroupMembers[SignatureSymbol]) {
      uint32_t SecIndex = SectionIndexMap.lookup(Member);
      write(SecIndex);
    }

    uint64_t SecEnd = W.OS.tell();
    SectionOffsets[Group] = std::make_pair(SecStart, SecEnd);
  }

  if (Mode == DwoOnly) {
    // dwo files don't have symbol tables or relocations, but they do have
    // string tables.
    StrTabBuilder.finalize();
  } else {
    MCSectionELF *AddrsigSection;
    if (OWriter.EmitAddrsigSection) {
      AddrsigSection = Ctx.getELFSection(".llvm_addrsig", ELF::SHT_LLVM_ADDRSIG,
                                         ELF::SHF_EXCLUDE);
      addToSectionTable(AddrsigSection);
    }

    // Compute symbol table information.
    computeSymbolTable(Asm, Layout, SectionIndexMap, RevGroupMap,
                       SectionOffsets);

    for (MCSectionELF *RelSection : Relocations) {
      align(RelSection->getAlignment());

      // Remember the offset into the file for this section.
      uint64_t SecStart = W.OS.tell();

      writeRelocations(Asm,
                       cast<MCSectionELF>(*RelSection->getAssociatedSection()));

      uint64_t SecEnd = W.OS.tell();
      SectionOffsets[RelSection] = std::make_pair(SecStart, SecEnd);
    }

    if (OWriter.EmitAddrsigSection) {
      uint64_t SecStart = W.OS.tell();
      writeAddrsigSection();
      uint64_t SecEnd = W.OS.tell();
      SectionOffsets[AddrsigSection] = std::make_pair(SecStart, SecEnd);
    }
  }

  if (CGProfileSection) {
    uint64_t SecStart = W.OS.tell();
    for (const MCAssembler::CGProfileEntry &CGPE : Asm.CGProfile) {
      W.write<uint32_t>(CGPE.From->getSymbol().getIndex());
      W.write<uint32_t>(CGPE.To->getSymbol().getIndex());
      W.write<uint64_t>(CGPE.Count);
    }
    uint64_t SecEnd = W.OS.tell();
    SectionOffsets[CGProfileSection] = std::make_pair(SecStart, SecEnd);
  }

  {
    uint64_t SecStart = W.OS.tell();
    const MCSectionELF *Sec = createStringTable(Ctx);
    uint64_t SecEnd = W.OS.tell();
    SectionOffsets[Sec] = std::make_pair(SecStart, SecEnd);
  }

  uint64_t NaturalAlignment = is64Bit() ? 8 : 4;
  align(NaturalAlignment);

  const uint64_t SectionHeaderOffset = W.OS.tell();

  // ... then the section header table ...
  writeSectionHeader(Layout, SectionIndexMap, SectionOffsets);

  uint16_t NumSections = support::endian::byte_swap<uint16_t>(
      (SectionTable.size() + 1 >= ELF::SHN_LORESERVE) ? (uint16_t)ELF::SHN_UNDEF
                                                      : SectionTable.size() + 1,
      W.Endian);
  unsigned NumSectionsOffset;

  auto &Stream = static_cast<raw_pwrite_stream &>(W.OS);
  if (is64Bit()) {
    uint64_t Val =
        support::endian::byte_swap<uint64_t>(SectionHeaderOffset, W.Endian);
    Stream.pwrite(reinterpret_cast<char *>(&Val), sizeof(Val),
                  offsetof(ELF::Elf64_Ehdr, e_shoff));
    NumSectionsOffset = offsetof(ELF::Elf64_Ehdr, e_shnum);
  } else {
    uint32_t Val =
        support::endian::byte_swap<uint32_t>(SectionHeaderOffset, W.Endian);
    Stream.pwrite(reinterpret_cast<char *>(&Val), sizeof(Val),
                  offsetof(ELF::Elf32_Ehdr, e_shoff));
    NumSectionsOffset = offsetof(ELF::Elf32_Ehdr, e_shnum);
  }
  Stream.pwrite(reinterpret_cast<char *>(&NumSections), sizeof(NumSections),
                NumSectionsOffset);

  return W.OS.tell() - StartOffset;
}

bool ELFObjectWriter::hasRelocationAddend() const {
  return TargetObjectWriter->hasRelocationAddend();
}

void ELFObjectWriter::executePostLayoutBinding(MCAssembler &Asm,
                                               const MCAsmLayout &Layout) {
  // The presence of symbol versions causes undefined symbols and
  // versions declared with @@@ to be renamed.
  for (const std::pair<StringRef, const MCSymbol *> &P : Asm.Symvers) {
    StringRef AliasName = P.first;
    const auto &Symbol = cast<MCSymbolELF>(*P.second);
    size_t Pos = AliasName.find('@');
    assert(Pos != StringRef::npos);

    StringRef Prefix = AliasName.substr(0, Pos);
    StringRef Rest = AliasName.substr(Pos);
    StringRef Tail = Rest;
    if (Rest.startswith("@@@"))
      Tail = Rest.substr(Symbol.isUndefined() ? 2 : 1);

    auto *Alias =
        cast<MCSymbolELF>(Asm.getContext().getOrCreateSymbol(Prefix + Tail));
    Asm.registerSymbol(*Alias);
    const MCExpr *Value = MCSymbolRefExpr::create(&Symbol, Asm.getContext());
    Alias->setVariableValue(Value);

    // Aliases defined with .symvar copy the binding from the symbol they alias.
    // This is the first place we are able to copy this information.
    Alias->setExternal(Symbol.isExternal());
    Alias->setBinding(Symbol.getBinding());

    if (!Symbol.isUndefined() && !Rest.startswith("@@@"))
      continue;

    // FIXME: Get source locations for these errors or diagnose them earlier.
    if (Symbol.isUndefined() && Rest.startswith("@@") &&
        !Rest.startswith("@@@")) {
      Asm.getContext().reportError(SMLoc(), "versioned symbol " + AliasName +
                                                " must be defined");
      continue;
    }

    if (Renames.count(&Symbol) && Renames[&Symbol] != Alias) {
      Asm.getContext().reportError(
          SMLoc(), llvm::Twine("multiple symbol versions defined for ") +
                       Symbol.getName());
      continue;
    }

    Renames.insert(std::make_pair(&Symbol, Alias));
  }

  for (const MCSymbol *&Sym : AddrsigSyms) {
    if (const MCSymbol *R = Renames.lookup(cast<MCSymbolELF>(Sym)))
      Sym = R;
    if (Sym->isInSection() && Sym->getName().startswith(".L"))
      Sym = Sym->getSection().getBeginSymbol();
    Sym->setUsedInReloc();
  }
}

// It is always valid to create a relocation with a symbol. It is preferable
// to use a relocation with a section if that is possible. Using the section
// allows us to omit some local symbols from the symbol table.
bool ELFObjectWriter::shouldRelocateWithSymbol(const MCAssembler &Asm,
                                               const MCSymbolRefExpr *RefA,
                                               const MCSymbolELF *Sym,
                                               uint64_t C,
                                               unsigned Type) const {
  // A PCRel relocation to an absolute value has no symbol (or section). We
  // represent that with a relocation to a null section.
  if (!RefA)
    return false;

  MCSymbolRefExpr::VariantKind Kind = RefA->getKind();
  switch (Kind) {
  default:
    break;
  // The .odp creation emits a relocation against the symbol ".TOC." which
  // create a R_PPC64_TOC relocation. However the relocation symbol name
  // in final object creation should be NULL, since the symbol does not
  // really exist, it is just the reference to TOC base for the current
  // object file. Since the symbol is undefined, returning false results
  // in a relocation with a null section which is the desired result.
  case MCSymbolRefExpr::VK_PPC_TOCBASE:
    return false;

  // These VariantKind cause the relocation to refer to something other than
  // the symbol itself, like a linker generated table. Since the address of
  // symbol is not relevant, we cannot replace the symbol with the
  // section and patch the difference in the addend.
  case MCSymbolRefExpr::VK_GOT:
  case MCSymbolRefExpr::VK_PLT:
  case MCSymbolRefExpr::VK_GOTPCREL:
  case MCSymbolRefExpr::VK_PPC_GOT_LO:
  case MCSymbolRefExpr::VK_PPC_GOT_HI:
  case MCSymbolRefExpr::VK_PPC_GOT_HA:
    return true;
  }

  // An undefined symbol is not in any section, so the relocation has to point
  // to the symbol itself.
  assert(Sym && "Expected a symbol");
  if (Sym->isUndefined())
    return true;

  unsigned Binding = Sym->getBinding();
  switch(Binding) {
  default:
    llvm_unreachable("Invalid Binding");
  case ELF::STB_LOCAL:
    break;
  case ELF::STB_WEAK:
    // If the symbol is weak, it might be overridden by a symbol in another
    // file. The relocation has to point to the symbol so that the linker
    // can update it.
    return true;
  case ELF::STB_GLOBAL:
    // Global ELF symbols can be preempted by the dynamic linker. The relocation
    // has to point to the symbol for a reason analogous to the STB_WEAK case.
    return true;
  }

  // If a relocation points to a mergeable section, we have to be careful.
  // If the offset is zero, a relocation with the section will encode the
  // same information. With a non-zero offset, the situation is different.
  // For example, a relocation can point 42 bytes past the end of a string.
  // If we change such a relocation to use the section, the linker would think
  // that it pointed to another string and subtracting 42 at runtime will
  // produce the wrong value.
  if (Sym->isInSection()) {
    auto &Sec = cast<MCSectionELF>(Sym->getSection());
    unsigned Flags = Sec.getFlags();
    if (Flags & ELF::SHF_MERGE) {
      if (C != 0)
        return true;

      // It looks like gold has a bug (http://sourceware.org/PR16794) and can
      // only handle section relocations to mergeable sections if using RELA.
      if (!hasRelocationAddend())
        return true;
    }

    // Most TLS relocations use a got, so they need the symbol. Even those that
    // are just an offset (@tpoff), require a symbol in gold versions before
    // 5efeedf61e4fe720fd3e9a08e6c91c10abb66d42 (2014-09-26) which fixed
    // http://sourceware.org/PR16773.
    if (Flags & ELF::SHF_TLS)
      return true;
  }

  // If the symbol is a thumb function the final relocation must set the lowest
  // bit. With a symbol that is done by just having the symbol have that bit
  // set, so we would lose the bit if we relocated with the section.
  // FIXME: We could use the section but add the bit to the relocation value.
  if (Asm.isThumbFunc(Sym))
    return true;

  if (TargetObjectWriter->needsRelocateWithSymbol(*Sym, Type))
    return true;
  return false;
}

void ELFObjectWriter::recordRelocation(MCAssembler &Asm,
                                       const MCAsmLayout &Layout,
                                       const MCFragment *Fragment,
                                       const MCFixup &Fixup, MCValue Target,
                                       uint64_t &FixedValue) {
  MCAsmBackend &Backend = Asm.getBackend();
  bool IsPCRel = Backend.getFixupKindInfo(Fixup.getKind()).Flags &
                 MCFixupKindInfo::FKF_IsPCRel;
  const MCSectionELF &FixupSection = cast<MCSectionELF>(*Fragment->getParent());
  uint64_t C = Target.getConstant();
  uint64_t FixupOffset = Layout.getFragmentOffset(Fragment) + Fixup.getOffset();
  MCContext &Ctx = Asm.getContext();

  if (const MCSymbolRefExpr *RefB = Target.getSymB()) {
    // Let A, B and C being the components of Target and R be the location of
    // the fixup. If the fixup is not pcrel, we want to compute (A - B + C).
    // If it is pcrel, we want to compute (A - B + C - R).

    // In general, ELF has no relocations for -B. It can only represent (A + C)
    // or (A + C - R). If B = R + K and the relocation is not pcrel, we can
    // replace B to implement it: (A - R - K + C)
    if (IsPCRel) {
      Ctx.reportError(
          Fixup.getLoc(),
          "No relocation available to represent this relative expression");
      return;
    }

    const auto &SymB = cast<MCSymbolELF>(RefB->getSymbol());

    if (SymB.isUndefined()) {
      Ctx.reportError(Fixup.getLoc(),
                      Twine("symbol '") + SymB.getName() +
                          "' can not be undefined in a subtraction expression");
      return;
    }

    assert(!SymB.isAbsolute() && "Should have been folded");
    const MCSection &SecB = SymB.getSection();
    if (&SecB != &FixupSection) {
      Ctx.reportError(Fixup.getLoc(),
                      "Cannot represent a difference across sections");
      return;
    }

    uint64_t SymBOffset = Layout.getSymbolOffset(SymB);
    uint64_t K = SymBOffset - FixupOffset;
    IsPCRel = true;
    C -= K;
  }

  // We either rejected the fixup or folded B into C at this point.
  const MCSymbolRefExpr *RefA = Target.getSymA();
  const auto *SymA = RefA ? cast<MCSymbolELF>(&RefA->getSymbol()) : nullptr;

  bool ViaWeakRef = false;
  if (SymA && SymA->isVariable()) {
    const MCExpr *Expr = SymA->getVariableValue();
    if (const auto *Inner = dyn_cast<MCSymbolRefExpr>(Expr)) {
      if (Inner->getKind() == MCSymbolRefExpr::VK_WEAKREF) {
        SymA = cast<MCSymbolELF>(&Inner->getSymbol());
        ViaWeakRef = true;
      }
    }
  }

  unsigned Type = TargetObjectWriter->getRelocType(Ctx, Target, Fixup, IsPCRel);
  uint64_t OriginalC = C;
  bool RelocateWithSymbol = shouldRelocateWithSymbol(Asm, RefA, SymA, C, Type);
  if (!RelocateWithSymbol && SymA && !SymA->isUndefined())
    C += Layout.getSymbolOffset(*SymA);

  uint64_t Addend = 0;
  if (hasRelocationAddend()) {
    Addend = C;
    C = 0;
  }

  FixedValue = C;

  const MCSectionELF *SecA = (SymA && SymA->isInSection())
                                 ? cast<MCSectionELF>(&SymA->getSection())
                                 : nullptr;
  if (!checkRelocation(Ctx, Fixup.getLoc(), &FixupSection, SecA))
    return;

  if (!RelocateWithSymbol) {
    const auto *SectionSymbol =
        SecA ? cast<MCSymbolELF>(SecA->getBeginSymbol()) : nullptr;
    if (SectionSymbol)
      SectionSymbol->setUsedInReloc();
    ELFRelocationEntry Rec(FixupOffset, SectionSymbol, Type, Addend, SymA,
                           OriginalC);
    Relocations[&FixupSection].push_back(Rec);
    return;
  }

  const auto *RenamedSymA = SymA;
  if (SymA) {
    if (const MCSymbolELF *R = Renames.lookup(SymA))
      RenamedSymA = R;

    if (ViaWeakRef)
      RenamedSymA->setIsWeakrefUsedInReloc();
    else
      RenamedSymA->setUsedInReloc();
  }
  ELFRelocationEntry Rec(FixupOffset, RenamedSymA, Type, Addend, SymA,
                         OriginalC);
  Relocations[&FixupSection].push_back(Rec);
}

bool ELFObjectWriter::isSymbolRefDifferenceFullyResolvedImpl(
    const MCAssembler &Asm, const MCSymbol &SA, const MCFragment &FB,
    bool InSet, bool IsPCRel) const {
  const auto &SymA = cast<MCSymbolELF>(SA);
  if (IsPCRel) {
    assert(!InSet);
    if (isWeak(SymA))
      return false;
  }
  return MCObjectWriter::isSymbolRefDifferenceFullyResolvedImpl(Asm, SymA, FB,
                                                                InSet, IsPCRel);
}

std::unique_ptr<MCObjectWriter>
llvm::createELFObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                            raw_pwrite_stream &OS, bool IsLittleEndian) {
  return llvm::make_unique<ELFSingleObjectWriter>(std::move(MOTW), OS,
                                                  IsLittleEndian);
}

std::unique_ptr<MCObjectWriter>
llvm::createELFDwoObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                               raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS,
                               bool IsLittleEndian) {
  return llvm::make_unique<ELFDwoObjectWriter>(std::move(MOTW), OS, DwoOS,
                                               IsLittleEndian);
}
