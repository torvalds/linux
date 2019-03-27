//===- llvm/MC/WinCOFFObjectWriter.cpp ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains an implementation of a Win32 COFF object file writer.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/JamCRC.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using llvm::support::endian::write32le;

#define DEBUG_TYPE "WinCOFFObjectWriter"

namespace {

using name = SmallString<COFF::NameSize>;

enum AuxiliaryType {
  ATWeakExternal,
  ATFile,
  ATSectionDefinition
};

struct AuxSymbol {
  AuxiliaryType AuxType;
  COFF::Auxiliary Aux;
};

class COFFSection;

class COFFSymbol {
public:
  COFF::symbol Data = {};

  using AuxiliarySymbols = SmallVector<AuxSymbol, 1>;

  name Name;
  int Index;
  AuxiliarySymbols Aux;
  COFFSymbol *Other = nullptr;
  COFFSection *Section = nullptr;
  int Relocations = 0;
  const MCSymbol *MC = nullptr;

  COFFSymbol(StringRef Name) : Name(Name) {}

  void set_name_offset(uint32_t Offset);

  int64_t getIndex() const { return Index; }
  void setIndex(int Value) {
    Index = Value;
    if (MC)
      MC->setIndex(static_cast<uint32_t>(Value));
  }
};

// This class contains staging data for a COFF relocation entry.
struct COFFRelocation {
  COFF::relocation Data;
  COFFSymbol *Symb = nullptr;

  COFFRelocation() = default;

  static size_t size() { return COFF::RelocationSize; }
};

using relocations = std::vector<COFFRelocation>;

class COFFSection {
public:
  COFF::section Header = {};

  std::string Name;
  int Number;
  MCSectionCOFF const *MCSection = nullptr;
  COFFSymbol *Symbol = nullptr;
  relocations Relocations;

  COFFSection(StringRef Name) : Name(Name) {}
};

class WinCOFFObjectWriter : public MCObjectWriter {
public:
  support::endian::Writer W;

  using symbols = std::vector<std::unique_ptr<COFFSymbol>>;
  using sections = std::vector<std::unique_ptr<COFFSection>>;

  using symbol_map = DenseMap<MCSymbol const *, COFFSymbol *>;
  using section_map = DenseMap<MCSection const *, COFFSection *>;

  std::unique_ptr<MCWinCOFFObjectTargetWriter> TargetObjectWriter;

  // Root level file contents.
  COFF::header Header = {};
  sections Sections;
  symbols Symbols;
  StringTableBuilder Strings{StringTableBuilder::WinCOFF};

  // Maps used during object file creation.
  section_map SectionMap;
  symbol_map SymbolMap;

  bool UseBigObj;

  bool EmitAddrsigSection = false;
  MCSectionCOFF *AddrsigSection;
  std::vector<const MCSymbol *> AddrsigSyms;

  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS);

  void reset() override {
    memset(&Header, 0, sizeof(Header));
    Header.Machine = TargetObjectWriter->getMachine();
    Sections.clear();
    Symbols.clear();
    Strings.clear();
    SectionMap.clear();
    SymbolMap.clear();
    MCObjectWriter::reset();
  }

  COFFSymbol *createSymbol(StringRef Name);
  COFFSymbol *GetOrCreateCOFFSymbol(const MCSymbol *Symbol);
  COFFSection *createSection(StringRef Name);

  void defineSection(MCSectionCOFF const &Sec);

  COFFSymbol *getLinkedSymbol(const MCSymbol &Symbol);
  void DefineSymbol(const MCSymbol &Symbol, MCAssembler &Assembler,
                    const MCAsmLayout &Layout);

  void SetSymbolName(COFFSymbol &S);
  void SetSectionName(COFFSection &S);

  bool IsPhysicalSection(COFFSection *S);

  // Entity writing methods.

  void WriteFileHeader(const COFF::header &Header);
  void WriteSymbol(const COFFSymbol &S);
  void WriteAuxiliarySymbols(const COFFSymbol::AuxiliarySymbols &S);
  void writeSectionHeaders();
  void WriteRelocation(const COFF::relocation &R);
  uint32_t writeSectionContents(MCAssembler &Asm, const MCAsmLayout &Layout,
                                const MCSection &MCSec);
  void writeSection(MCAssembler &Asm, const MCAsmLayout &Layout,
                    const COFFSection &Sec, const MCSection &MCSec);

  // MCObjectWriter interface implementation.

  void executePostLayoutBinding(MCAssembler &Asm,
                                const MCAsmLayout &Layout) override;

  bool isSymbolRefDifferenceFullyResolvedImpl(const MCAssembler &Asm,
                                              const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;

  void recordRelocation(MCAssembler &Asm, const MCAsmLayout &Layout,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;

  void createFileSymbols(MCAssembler &Asm);
  void assignSectionNumbers();
  void assignFileOffsets(MCAssembler &Asm, const MCAsmLayout &Layout);

  void emitAddrsigSection() override { EmitAddrsigSection = true; }
  void addAddrsigSymbol(const MCSymbol *Sym) override {
    AddrsigSyms.push_back(Sym);
  }

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) override;
};

} // end anonymous namespace

//------------------------------------------------------------------------------
// Symbol class implementation

// In the case that the name does not fit within 8 bytes, the offset
// into the string table is stored in the last 4 bytes instead, leaving
// the first 4 bytes as 0.
void COFFSymbol::set_name_offset(uint32_t Offset) {
  write32le(Data.Name + 0, 0);
  write32le(Data.Name + 4, Offset);
}

//------------------------------------------------------------------------------
// WinCOFFObjectWriter class implementation

WinCOFFObjectWriter::WinCOFFObjectWriter(
    std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS)
    : W(OS, support::little), TargetObjectWriter(std::move(MOTW)) {
  Header.Machine = TargetObjectWriter->getMachine();
}

COFFSymbol *WinCOFFObjectWriter::createSymbol(StringRef Name) {
  Symbols.push_back(make_unique<COFFSymbol>(Name));
  return Symbols.back().get();
}

COFFSymbol *WinCOFFObjectWriter::GetOrCreateCOFFSymbol(const MCSymbol *Symbol) {
  COFFSymbol *&Ret = SymbolMap[Symbol];
  if (!Ret)
    Ret = createSymbol(Symbol->getName());
  return Ret;
}

COFFSection *WinCOFFObjectWriter::createSection(StringRef Name) {
  Sections.emplace_back(make_unique<COFFSection>(Name));
  return Sections.back().get();
}

static uint32_t getAlignment(const MCSectionCOFF &Sec) {
  switch (Sec.getAlignment()) {
  case 1:
    return COFF::IMAGE_SCN_ALIGN_1BYTES;
  case 2:
    return COFF::IMAGE_SCN_ALIGN_2BYTES;
  case 4:
    return COFF::IMAGE_SCN_ALIGN_4BYTES;
  case 8:
    return COFF::IMAGE_SCN_ALIGN_8BYTES;
  case 16:
    return COFF::IMAGE_SCN_ALIGN_16BYTES;
  case 32:
    return COFF::IMAGE_SCN_ALIGN_32BYTES;
  case 64:
    return COFF::IMAGE_SCN_ALIGN_64BYTES;
  case 128:
    return COFF::IMAGE_SCN_ALIGN_128BYTES;
  case 256:
    return COFF::IMAGE_SCN_ALIGN_256BYTES;
  case 512:
    return COFF::IMAGE_SCN_ALIGN_512BYTES;
  case 1024:
    return COFF::IMAGE_SCN_ALIGN_1024BYTES;
  case 2048:
    return COFF::IMAGE_SCN_ALIGN_2048BYTES;
  case 4096:
    return COFF::IMAGE_SCN_ALIGN_4096BYTES;
  case 8192:
    return COFF::IMAGE_SCN_ALIGN_8192BYTES;
  }
  llvm_unreachable("unsupported section alignment");
}

/// This function takes a section data object from the assembler
/// and creates the associated COFF section staging object.
void WinCOFFObjectWriter::defineSection(const MCSectionCOFF &MCSec) {
  COFFSection *Section = createSection(MCSec.getSectionName());
  COFFSymbol *Symbol = createSymbol(MCSec.getSectionName());
  Section->Symbol = Symbol;
  Symbol->Section = Section;
  Symbol->Data.StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;

  // Create a COMDAT symbol if needed.
  if (MCSec.getSelection() != COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
    if (const MCSymbol *S = MCSec.getCOMDATSymbol()) {
      COFFSymbol *COMDATSymbol = GetOrCreateCOFFSymbol(S);
      if (COMDATSymbol->Section)
        report_fatal_error("two sections have the same comdat");
      COMDATSymbol->Section = Section;
    }
  }

  // In this case the auxiliary symbol is a Section Definition.
  Symbol->Aux.resize(1);
  Symbol->Aux[0] = {};
  Symbol->Aux[0].AuxType = ATSectionDefinition;
  Symbol->Aux[0].Aux.SectionDefinition.Selection = MCSec.getSelection();

  // Set section alignment.
  Section->Header.Characteristics = MCSec.getCharacteristics();
  Section->Header.Characteristics |= getAlignment(MCSec);

  // Bind internal COFF section to MC section.
  Section->MCSection = &MCSec;
  SectionMap[&MCSec] = Section;
}

static uint64_t getSymbolValue(const MCSymbol &Symbol,
                               const MCAsmLayout &Layout) {
  if (Symbol.isCommon() && Symbol.isExternal())
    return Symbol.getCommonSize();

  uint64_t Res;
  if (!Layout.getSymbolOffset(Symbol, Res))
    return 0;

  return Res;
}

COFFSymbol *WinCOFFObjectWriter::getLinkedSymbol(const MCSymbol &Symbol) {
  if (!Symbol.isVariable())
    return nullptr;

  const MCSymbolRefExpr *SymRef =
      dyn_cast<MCSymbolRefExpr>(Symbol.getVariableValue());
  if (!SymRef)
    return nullptr;

  const MCSymbol &Aliasee = SymRef->getSymbol();
  if (!Aliasee.isUndefined())
    return nullptr;
  return GetOrCreateCOFFSymbol(&Aliasee);
}

/// This function takes a symbol data object from the assembler
/// and creates the associated COFF symbol staging object.
void WinCOFFObjectWriter::DefineSymbol(const MCSymbol &MCSym,
                                       MCAssembler &Assembler,
                                       const MCAsmLayout &Layout) {
  COFFSymbol *Sym = GetOrCreateCOFFSymbol(&MCSym);
  const MCSymbol *Base = Layout.getBaseSymbol(MCSym);
  COFFSection *Sec = nullptr;
  if (Base && Base->getFragment()) {
    Sec = SectionMap[Base->getFragment()->getParent()];
    if (Sym->Section && Sym->Section != Sec)
      report_fatal_error("conflicting sections for symbol");
  }

  COFFSymbol *Local = nullptr;
  if (cast<MCSymbolCOFF>(MCSym).isWeakExternal()) {
    Sym->Data.StorageClass = COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL;

    COFFSymbol *WeakDefault = getLinkedSymbol(MCSym);
    if (!WeakDefault) {
      std::string WeakName = (".weak." + MCSym.getName() + ".default").str();
      WeakDefault = createSymbol(WeakName);
      if (!Sec)
        WeakDefault->Data.SectionNumber = COFF::IMAGE_SYM_ABSOLUTE;
      else
        WeakDefault->Section = Sec;
      Local = WeakDefault;
    }

    Sym->Other = WeakDefault;

    // Setup the Weak External auxiliary symbol.
    Sym->Aux.resize(1);
    memset(&Sym->Aux[0], 0, sizeof(Sym->Aux[0]));
    Sym->Aux[0].AuxType = ATWeakExternal;
    Sym->Aux[0].Aux.WeakExternal.TagIndex = 0;
    Sym->Aux[0].Aux.WeakExternal.Characteristics =
        COFF::IMAGE_WEAK_EXTERN_SEARCH_LIBRARY;
  } else {
    if (!Base)
      Sym->Data.SectionNumber = COFF::IMAGE_SYM_ABSOLUTE;
    else
      Sym->Section = Sec;
    Local = Sym;
  }

  if (Local) {
    Local->Data.Value = getSymbolValue(MCSym, Layout);

    const MCSymbolCOFF &SymbolCOFF = cast<MCSymbolCOFF>(MCSym);
    Local->Data.Type = SymbolCOFF.getType();
    Local->Data.StorageClass = SymbolCOFF.getClass();

    // If no storage class was specified in the streamer, define it here.
    if (Local->Data.StorageClass == COFF::IMAGE_SYM_CLASS_NULL) {
      bool IsExternal = MCSym.isExternal() ||
                        (!MCSym.getFragment() && !MCSym.isVariable());

      Local->Data.StorageClass = IsExternal ? COFF::IMAGE_SYM_CLASS_EXTERNAL
                                            : COFF::IMAGE_SYM_CLASS_STATIC;
    }
  }

  Sym->MC = &MCSym;
}

// Maximum offsets for different string table entry encodings.
enum : unsigned { Max7DecimalOffset = 9999999U };
enum : uint64_t { MaxBase64Offset = 0xFFFFFFFFFULL }; // 64^6, including 0

// Encode a string table entry offset in base 64, padded to 6 chars, and
// prefixed with a double slash: '//AAAAAA', '//AAAAAB', ...
// Buffer must be at least 8 bytes large. No terminating null appended.
static void encodeBase64StringEntry(char *Buffer, uint64_t Value) {
  assert(Value > Max7DecimalOffset && Value <= MaxBase64Offset &&
         "Illegal section name encoding for value");

  static const char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz"
                                 "0123456789+/";

  Buffer[0] = '/';
  Buffer[1] = '/';

  char *Ptr = Buffer + 7;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned Rem = Value % 64;
    Value /= 64;
    *(Ptr--) = Alphabet[Rem];
  }
}

void WinCOFFObjectWriter::SetSectionName(COFFSection &S) {
  if (S.Name.size() <= COFF::NameSize) {
    std::memcpy(S.Header.Name, S.Name.c_str(), S.Name.size());
    return;
  }

  uint64_t StringTableEntry = Strings.getOffset(S.Name);
  if (StringTableEntry <= Max7DecimalOffset) {
    SmallVector<char, COFF::NameSize> Buffer;
    Twine('/').concat(Twine(StringTableEntry)).toVector(Buffer);
    assert(Buffer.size() <= COFF::NameSize && Buffer.size() >= 2);
    std::memcpy(S.Header.Name, Buffer.data(), Buffer.size());
    return;
  }
  if (StringTableEntry <= MaxBase64Offset) {
    // Starting with 10,000,000, offsets are encoded as base64.
    encodeBase64StringEntry(S.Header.Name, StringTableEntry);
    return;
  }
  report_fatal_error("COFF string table is greater than 64 GB.");
}

void WinCOFFObjectWriter::SetSymbolName(COFFSymbol &S) {
  if (S.Name.size() > COFF::NameSize)
    S.set_name_offset(Strings.getOffset(S.Name));
  else
    std::memcpy(S.Data.Name, S.Name.c_str(), S.Name.size());
}

bool WinCOFFObjectWriter::IsPhysicalSection(COFFSection *S) {
  return (S->Header.Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA) ==
         0;
}

//------------------------------------------------------------------------------
// entity writing methods

void WinCOFFObjectWriter::WriteFileHeader(const COFF::header &Header) {
  if (UseBigObj) {
    W.write<uint16_t>(COFF::IMAGE_FILE_MACHINE_UNKNOWN);
    W.write<uint16_t>(0xFFFF);
    W.write<uint16_t>(COFF::BigObjHeader::MinBigObjectVersion);
    W.write<uint16_t>(Header.Machine);
    W.write<uint32_t>(Header.TimeDateStamp);
    W.OS.write(COFF::BigObjMagic, sizeof(COFF::BigObjMagic));
    W.write<uint32_t>(0);
    W.write<uint32_t>(0);
    W.write<uint32_t>(0);
    W.write<uint32_t>(0);
    W.write<uint32_t>(Header.NumberOfSections);
    W.write<uint32_t>(Header.PointerToSymbolTable);
    W.write<uint32_t>(Header.NumberOfSymbols);
  } else {
    W.write<uint16_t>(Header.Machine);
    W.write<uint16_t>(static_cast<int16_t>(Header.NumberOfSections));
    W.write<uint32_t>(Header.TimeDateStamp);
    W.write<uint32_t>(Header.PointerToSymbolTable);
    W.write<uint32_t>(Header.NumberOfSymbols);
    W.write<uint16_t>(Header.SizeOfOptionalHeader);
    W.write<uint16_t>(Header.Characteristics);
  }
}

void WinCOFFObjectWriter::WriteSymbol(const COFFSymbol &S) {
  W.OS.write(S.Data.Name, COFF::NameSize);
  W.write<uint32_t>(S.Data.Value);
  if (UseBigObj)
    W.write<uint32_t>(S.Data.SectionNumber);
  else
    W.write<uint16_t>(static_cast<int16_t>(S.Data.SectionNumber));
  W.write<uint16_t>(S.Data.Type);
  W.OS << char(S.Data.StorageClass);
  W.OS << char(S.Data.NumberOfAuxSymbols);
  WriteAuxiliarySymbols(S.Aux);
}

void WinCOFFObjectWriter::WriteAuxiliarySymbols(
    const COFFSymbol::AuxiliarySymbols &S) {
  for (const AuxSymbol &i : S) {
    switch (i.AuxType) {
    case ATWeakExternal:
      W.write<uint32_t>(i.Aux.WeakExternal.TagIndex);
      W.write<uint32_t>(i.Aux.WeakExternal.Characteristics);
      W.OS.write_zeros(sizeof(i.Aux.WeakExternal.unused));
      if (UseBigObj)
        W.OS.write_zeros(COFF::Symbol32Size - COFF::Symbol16Size);
      break;
    case ATFile:
      W.OS.write(reinterpret_cast<const char *>(&i.Aux),
                        UseBigObj ? COFF::Symbol32Size : COFF::Symbol16Size);
      break;
    case ATSectionDefinition:
      W.write<uint32_t>(i.Aux.SectionDefinition.Length);
      W.write<uint16_t>(i.Aux.SectionDefinition.NumberOfRelocations);
      W.write<uint16_t>(i.Aux.SectionDefinition.NumberOfLinenumbers);
      W.write<uint32_t>(i.Aux.SectionDefinition.CheckSum);
      W.write<uint16_t>(static_cast<int16_t>(i.Aux.SectionDefinition.Number));
      W.OS << char(i.Aux.SectionDefinition.Selection);
      W.OS.write_zeros(sizeof(i.Aux.SectionDefinition.unused));
      W.write<uint16_t>(static_cast<int16_t>(i.Aux.SectionDefinition.Number >> 16));
      if (UseBigObj)
        W.OS.write_zeros(COFF::Symbol32Size - COFF::Symbol16Size);
      break;
    }
  }
}

// Write the section header.
void WinCOFFObjectWriter::writeSectionHeaders() {
  // Section numbers must be monotonically increasing in the section
  // header, but our Sections array is not sorted by section number,
  // so make a copy of Sections and sort it.
  std::vector<COFFSection *> Arr;
  for (auto &Section : Sections)
    Arr.push_back(Section.get());
  llvm::sort(Arr, [](const COFFSection *A, const COFFSection *B) {
    return A->Number < B->Number;
  });

  for (auto &Section : Arr) {
    if (Section->Number == -1)
      continue;

    COFF::section &S = Section->Header;
    if (Section->Relocations.size() >= 0xffff)
      S.Characteristics |= COFF::IMAGE_SCN_LNK_NRELOC_OVFL;
    W.OS.write(S.Name, COFF::NameSize);
    W.write<uint32_t>(S.VirtualSize);
    W.write<uint32_t>(S.VirtualAddress);
    W.write<uint32_t>(S.SizeOfRawData);
    W.write<uint32_t>(S.PointerToRawData);
    W.write<uint32_t>(S.PointerToRelocations);
    W.write<uint32_t>(S.PointerToLineNumbers);
    W.write<uint16_t>(S.NumberOfRelocations);
    W.write<uint16_t>(S.NumberOfLineNumbers);
    W.write<uint32_t>(S.Characteristics);
  }
}

void WinCOFFObjectWriter::WriteRelocation(const COFF::relocation &R) {
  W.write<uint32_t>(R.VirtualAddress);
  W.write<uint32_t>(R.SymbolTableIndex);
  W.write<uint16_t>(R.Type);
}

// Write MCSec's contents. What this function does is essentially
// "Asm.writeSectionData(&MCSec, Layout)", but it's a bit complicated
// because it needs to compute a CRC.
uint32_t WinCOFFObjectWriter::writeSectionContents(MCAssembler &Asm,
                                                   const MCAsmLayout &Layout,
                                                   const MCSection &MCSec) {
  // Save the contents of the section to a temporary buffer, we need this
  // to CRC the data before we dump it into the object file.
  SmallVector<char, 128> Buf;
  raw_svector_ostream VecOS(Buf);
  Asm.writeSectionData(VecOS, &MCSec, Layout);

  // Write the section contents to the object file.
  W.OS << Buf;

  // Calculate our CRC with an initial value of '0', this is not how
  // JamCRC is specified but it aligns with the expected output.
  JamCRC JC(/*Init=*/0);
  JC.update(Buf);
  return JC.getCRC();
}

void WinCOFFObjectWriter::writeSection(MCAssembler &Asm,
                                       const MCAsmLayout &Layout,
                                       const COFFSection &Sec,
                                       const MCSection &MCSec) {
  if (Sec.Number == -1)
    return;

  // Write the section contents.
  if (Sec.Header.PointerToRawData != 0) {
    assert(W.OS.tell() == Sec.Header.PointerToRawData &&
           "Section::PointerToRawData is insane!");

    uint32_t CRC = writeSectionContents(Asm, Layout, MCSec);

    // Update the section definition auxiliary symbol to record the CRC.
    COFFSection *Sec = SectionMap[&MCSec];
    COFFSymbol::AuxiliarySymbols &AuxSyms = Sec->Symbol->Aux;
    assert(AuxSyms.size() == 1 && AuxSyms[0].AuxType == ATSectionDefinition);
    AuxSymbol &SecDef = AuxSyms[0];
    SecDef.Aux.SectionDefinition.CheckSum = CRC;
  }

  // Write relocations for this section.
  if (Sec.Relocations.empty()) {
    assert(Sec.Header.PointerToRelocations == 0 &&
           "Section::PointerToRelocations is insane!");
    return;
  }

  assert(W.OS.tell() == Sec.Header.PointerToRelocations &&
         "Section::PointerToRelocations is insane!");

  if (Sec.Relocations.size() >= 0xffff) {
    // In case of overflow, write actual relocation count as first
    // relocation. Including the synthetic reloc itself (+ 1).
    COFF::relocation R;
    R.VirtualAddress = Sec.Relocations.size() + 1;
    R.SymbolTableIndex = 0;
    R.Type = 0;
    WriteRelocation(R);
  }

  for (const auto &Relocation : Sec.Relocations)
    WriteRelocation(Relocation.Data);
}

////////////////////////////////////////////////////////////////////////////////
// MCObjectWriter interface implementations

void WinCOFFObjectWriter::executePostLayoutBinding(MCAssembler &Asm,
                                                   const MCAsmLayout &Layout) {
  if (EmitAddrsigSection) {
    AddrsigSection = Asm.getContext().getCOFFSection(
        ".llvm_addrsig", COFF::IMAGE_SCN_LNK_REMOVE,
        SectionKind::getMetadata());
    Asm.registerSection(*AddrsigSection);
  }

  // "Define" each section & symbol. This creates section & symbol
  // entries in the staging area.
  for (const auto &Section : Asm)
    defineSection(static_cast<const MCSectionCOFF &>(Section));

  for (const MCSymbol &Symbol : Asm.symbols())
    if (!Symbol.isTemporary())
      DefineSymbol(Symbol, Asm, Layout);
}

bool WinCOFFObjectWriter::isSymbolRefDifferenceFullyResolvedImpl(
    const MCAssembler &Asm, const MCSymbol &SymA, const MCFragment &FB,
    bool InSet, bool IsPCRel) const {
  // Don't drop relocations between functions, even if they are in the same text
  // section. Multiple Visual C++ linker features depend on having the
  // relocations present. The /INCREMENTAL flag will cause these relocations to
  // point to thunks, and the /GUARD:CF flag assumes that it can use relocations
  // to approximate the set of all address taken functions. LLD's implementation
  // of /GUARD:CF also relies on the existance of these relocations.
  uint16_t Type = cast<MCSymbolCOFF>(SymA).getType();
  if ((Type >> COFF::SCT_COMPLEX_TYPE_SHIFT) == COFF::IMAGE_SYM_DTYPE_FUNCTION)
    return false;
  return MCObjectWriter::isSymbolRefDifferenceFullyResolvedImpl(Asm, SymA, FB,
                                                                InSet, IsPCRel);
}

void WinCOFFObjectWriter::recordRelocation(MCAssembler &Asm,
                                           const MCAsmLayout &Layout,
                                           const MCFragment *Fragment,
                                           const MCFixup &Fixup, MCValue Target,
                                           uint64_t &FixedValue) {
  assert(Target.getSymA() && "Relocation must reference a symbol!");

  const MCSymbol &A = Target.getSymA()->getSymbol();
  if (!A.isRegistered()) {
    Asm.getContext().reportError(Fixup.getLoc(),
                                      Twine("symbol '") + A.getName() +
                                          "' can not be undefined");
    return;
  }
  if (A.isTemporary() && A.isUndefined()) {
    Asm.getContext().reportError(Fixup.getLoc(),
                                      Twine("assembler label '") + A.getName() +
                                          "' can not be undefined");
    return;
  }

  MCSection *MCSec = Fragment->getParent();

  // Mark this symbol as requiring an entry in the symbol table.
  assert(SectionMap.find(MCSec) != SectionMap.end() &&
         "Section must already have been defined in executePostLayoutBinding!");

  COFFSection *Sec = SectionMap[MCSec];
  const MCSymbolRefExpr *SymB = Target.getSymB();

  if (SymB) {
    const MCSymbol *B = &SymB->getSymbol();
    if (!B->getFragment()) {
      Asm.getContext().reportError(
          Fixup.getLoc(),
          Twine("symbol '") + B->getName() +
              "' can not be undefined in a subtraction expression");
      return;
    }

    // Offset of the symbol in the section
    int64_t OffsetOfB = Layout.getSymbolOffset(*B);

    // Offset of the relocation in the section
    int64_t OffsetOfRelocation =
        Layout.getFragmentOffset(Fragment) + Fixup.getOffset();

    FixedValue = (OffsetOfRelocation - OffsetOfB) + Target.getConstant();
  } else {
    FixedValue = Target.getConstant();
  }

  COFFRelocation Reloc;

  Reloc.Data.SymbolTableIndex = 0;
  Reloc.Data.VirtualAddress = Layout.getFragmentOffset(Fragment);

  // Turn relocations for temporary symbols into section relocations.
  if (A.isTemporary()) {
    MCSection *TargetSection = &A.getSection();
    assert(
        SectionMap.find(TargetSection) != SectionMap.end() &&
        "Section must already have been defined in executePostLayoutBinding!");
    Reloc.Symb = SectionMap[TargetSection]->Symbol;
    FixedValue += Layout.getSymbolOffset(A);
  } else {
    assert(
        SymbolMap.find(&A) != SymbolMap.end() &&
        "Symbol must already have been defined in executePostLayoutBinding!");
    Reloc.Symb = SymbolMap[&A];
  }

  ++Reloc.Symb->Relocations;

  Reloc.Data.VirtualAddress += Fixup.getOffset();
  Reloc.Data.Type = TargetObjectWriter->getRelocType(
      Asm.getContext(), Target, Fixup, SymB, Asm.getBackend());

  // FIXME: Can anyone explain what this does other than adjust for the size
  // of the offset?
  if ((Header.Machine == COFF::IMAGE_FILE_MACHINE_AMD64 &&
       Reloc.Data.Type == COFF::IMAGE_REL_AMD64_REL32) ||
      (Header.Machine == COFF::IMAGE_FILE_MACHINE_I386 &&
       Reloc.Data.Type == COFF::IMAGE_REL_I386_REL32))
    FixedValue += 4;

  if (Header.Machine == COFF::IMAGE_FILE_MACHINE_ARMNT) {
    switch (Reloc.Data.Type) {
    case COFF::IMAGE_REL_ARM_ABSOLUTE:
    case COFF::IMAGE_REL_ARM_ADDR32:
    case COFF::IMAGE_REL_ARM_ADDR32NB:
    case COFF::IMAGE_REL_ARM_TOKEN:
    case COFF::IMAGE_REL_ARM_SECTION:
    case COFF::IMAGE_REL_ARM_SECREL:
      break;
    case COFF::IMAGE_REL_ARM_BRANCH11:
    case COFF::IMAGE_REL_ARM_BLX11:
    // IMAGE_REL_ARM_BRANCH11 and IMAGE_REL_ARM_BLX11 are only used for
    // pre-ARMv7, which implicitly rules it out of ARMNT (it would be valid
    // for Windows CE).
    case COFF::IMAGE_REL_ARM_BRANCH24:
    case COFF::IMAGE_REL_ARM_BLX24:
    case COFF::IMAGE_REL_ARM_MOV32A:
      // IMAGE_REL_ARM_BRANCH24, IMAGE_REL_ARM_BLX24, IMAGE_REL_ARM_MOV32A are
      // only used for ARM mode code, which is documented as being unsupported
      // by Windows on ARM.  Empirical proof indicates that masm is able to
      // generate the relocations however the rest of the MSVC toolchain is
      // unable to handle it.
      llvm_unreachable("unsupported relocation");
      break;
    case COFF::IMAGE_REL_ARM_MOV32T:
      break;
    case COFF::IMAGE_REL_ARM_BRANCH20T:
    case COFF::IMAGE_REL_ARM_BRANCH24T:
    case COFF::IMAGE_REL_ARM_BLX23T:
      // IMAGE_REL_BRANCH20T, IMAGE_REL_ARM_BRANCH24T, IMAGE_REL_ARM_BLX23T all
      // perform a 4 byte adjustment to the relocation.  Relative branches are
      // offset by 4 on ARM, however, because there is no RELA relocations, all
      // branches are offset by 4.
      FixedValue = FixedValue + 4;
      break;
    }
  }

  // The fixed value never makes sense for section indices, ignore it.
  if (Fixup.getKind() == FK_SecRel_2)
    FixedValue = 0;

  if (TargetObjectWriter->recordRelocation(Fixup))
    Sec->Relocations.push_back(Reloc);
}

static std::time_t getTime() {
  std::time_t Now = time(nullptr);
  if (Now < 0 || !isUInt<32>(Now))
    return UINT32_MAX;
  return Now;
}

// Create .file symbols.
void WinCOFFObjectWriter::createFileSymbols(MCAssembler &Asm) {
  for (const std::string &Name : Asm.getFileNames()) {
    // round up to calculate the number of auxiliary symbols required
    unsigned SymbolSize = UseBigObj ? COFF::Symbol32Size : COFF::Symbol16Size;
    unsigned Count = (Name.size() + SymbolSize - 1) / SymbolSize;

    COFFSymbol *File = createSymbol(".file");
    File->Data.SectionNumber = COFF::IMAGE_SYM_DEBUG;
    File->Data.StorageClass = COFF::IMAGE_SYM_CLASS_FILE;
    File->Aux.resize(Count);

    unsigned Offset = 0;
    unsigned Length = Name.size();
    for (auto &Aux : File->Aux) {
      Aux.AuxType = ATFile;

      if (Length > SymbolSize) {
        memcpy(&Aux.Aux, Name.c_str() + Offset, SymbolSize);
        Length = Length - SymbolSize;
      } else {
        memcpy(&Aux.Aux, Name.c_str() + Offset, Length);
        memset((char *)&Aux.Aux + Length, 0, SymbolSize - Length);
        break;
      }

      Offset += SymbolSize;
    }
  }
}

static bool isAssociative(const COFFSection &Section) {
  return Section.Symbol->Aux[0].Aux.SectionDefinition.Selection ==
         COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE;
}

void WinCOFFObjectWriter::assignSectionNumbers() {
  size_t I = 1;
  auto Assign = [&](COFFSection &Section) {
    Section.Number = I;
    Section.Symbol->Data.SectionNumber = I;
    Section.Symbol->Aux[0].Aux.SectionDefinition.Number = I;
    ++I;
  };

  // Although it is not explicitly requested by the Microsoft COFF spec,
  // we should avoid emitting forward associative section references,
  // because MSVC link.exe as of 2017 cannot handle that.
  for (const std::unique_ptr<COFFSection> &Section : Sections)
    if (!isAssociative(*Section))
      Assign(*Section);
  for (const std::unique_ptr<COFFSection> &Section : Sections)
    if (isAssociative(*Section))
      Assign(*Section);
}

// Assign file offsets to COFF object file structures.
void WinCOFFObjectWriter::assignFileOffsets(MCAssembler &Asm,
                                            const MCAsmLayout &Layout) {
  unsigned Offset = W.OS.tell();

  Offset += UseBigObj ? COFF::Header32Size : COFF::Header16Size;
  Offset += COFF::SectionSize * Header.NumberOfSections;

  for (const auto &Section : Asm) {
    COFFSection *Sec = SectionMap[&Section];

    if (Sec->Number == -1)
      continue;

    Sec->Header.SizeOfRawData = Layout.getSectionAddressSize(&Section);

    if (IsPhysicalSection(Sec)) {
      Sec->Header.PointerToRawData = Offset;
      Offset += Sec->Header.SizeOfRawData;
    }

    if (!Sec->Relocations.empty()) {
      bool RelocationsOverflow = Sec->Relocations.size() >= 0xffff;

      if (RelocationsOverflow) {
        // Signal overflow by setting NumberOfRelocations to max value. Actual
        // size is found in reloc #0. Microsoft tools understand this.
        Sec->Header.NumberOfRelocations = 0xffff;
      } else {
        Sec->Header.NumberOfRelocations = Sec->Relocations.size();
      }
      Sec->Header.PointerToRelocations = Offset;

      if (RelocationsOverflow) {
        // Reloc #0 will contain actual count, so make room for it.
        Offset += COFF::RelocationSize;
      }

      Offset += COFF::RelocationSize * Sec->Relocations.size();

      for (auto &Relocation : Sec->Relocations) {
        assert(Relocation.Symb->getIndex() != -1);
        Relocation.Data.SymbolTableIndex = Relocation.Symb->getIndex();
      }
    }

    assert(Sec->Symbol->Aux.size() == 1 &&
           "Section's symbol must have one aux!");
    AuxSymbol &Aux = Sec->Symbol->Aux[0];
    assert(Aux.AuxType == ATSectionDefinition &&
           "Section's symbol's aux symbol must be a Section Definition!");
    Aux.Aux.SectionDefinition.Length = Sec->Header.SizeOfRawData;
    Aux.Aux.SectionDefinition.NumberOfRelocations =
        Sec->Header.NumberOfRelocations;
    Aux.Aux.SectionDefinition.NumberOfLinenumbers =
        Sec->Header.NumberOfLineNumbers;
  }

  Header.PointerToSymbolTable = Offset;
}

uint64_t WinCOFFObjectWriter::writeObject(MCAssembler &Asm,
                                          const MCAsmLayout &Layout) {
  uint64_t StartOffset = W.OS.tell();

  if (Sections.size() > INT32_MAX)
    report_fatal_error(
        "PE COFF object files can't have more than 2147483647 sections");

  UseBigObj = Sections.size() > COFF::MaxNumberOfSections16;
  Header.NumberOfSections = Sections.size();
  Header.NumberOfSymbols = 0;

  assignSectionNumbers();
  createFileSymbols(Asm);

  for (auto &Symbol : Symbols) {
    // Update section number & offset for symbols that have them.
    if (Symbol->Section)
      Symbol->Data.SectionNumber = Symbol->Section->Number;
    Symbol->setIndex(Header.NumberOfSymbols++);
    // Update auxiliary symbol info.
    Symbol->Data.NumberOfAuxSymbols = Symbol->Aux.size();
    Header.NumberOfSymbols += Symbol->Data.NumberOfAuxSymbols;
  }

  // Build string table.
  for (const auto &S : Sections)
    if (S->Name.size() > COFF::NameSize)
      Strings.add(S->Name);
  for (const auto &S : Symbols)
    if (S->Name.size() > COFF::NameSize)
      Strings.add(S->Name);
  Strings.finalize();

  // Set names.
  for (const auto &S : Sections)
    SetSectionName(*S);
  for (auto &S : Symbols)
    SetSymbolName(*S);

  // Fixup weak external references.
  for (auto &Symbol : Symbols) {
    if (Symbol->Other) {
      assert(Symbol->getIndex() != -1);
      assert(Symbol->Aux.size() == 1 && "Symbol must contain one aux symbol!");
      assert(Symbol->Aux[0].AuxType == ATWeakExternal &&
             "Symbol's aux symbol must be a Weak External!");
      Symbol->Aux[0].Aux.WeakExternal.TagIndex = Symbol->Other->getIndex();
    }
  }

  // Fixup associative COMDAT sections.
  for (auto &Section : Sections) {
    if (Section->Symbol->Aux[0].Aux.SectionDefinition.Selection !=
        COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE)
      continue;

    const MCSectionCOFF &MCSec = *Section->MCSection;
    const MCSymbol *AssocMCSym = MCSec.getCOMDATSymbol();
    assert(AssocMCSym);

    // It's an error to try to associate with an undefined symbol or a symbol
    // without a section.
    if (!AssocMCSym->isInSection()) {
      Asm.getContext().reportError(
          SMLoc(), Twine("cannot make section ") + MCSec.getSectionName() +
                       Twine(" associative with sectionless symbol ") +
                       AssocMCSym->getName());
      continue;
    }

    const auto *AssocMCSec = cast<MCSectionCOFF>(&AssocMCSym->getSection());
    assert(SectionMap.count(AssocMCSec));
    COFFSection *AssocSec = SectionMap[AssocMCSec];

    // Skip this section if the associated section is unused.
    if (AssocSec->Number == -1)
      continue;

    Section->Symbol->Aux[0].Aux.SectionDefinition.Number = AssocSec->Number;
  }

  // Create the contents of the .llvm_addrsig section.
  if (EmitAddrsigSection) {
    auto Frag = new MCDataFragment(AddrsigSection);
    Frag->setLayoutOrder(0);
    raw_svector_ostream OS(Frag->getContents());
    for (const MCSymbol *S : AddrsigSyms) {
      if (!S->isTemporary()) {
        encodeULEB128(S->getIndex(), OS);
        continue;
      }

      MCSection *TargetSection = &S->getSection();
      assert(SectionMap.find(TargetSection) != SectionMap.end() &&
             "Section must already have been defined in "
             "executePostLayoutBinding!");
      encodeULEB128(SectionMap[TargetSection]->Symbol->getIndex(), OS);
    }
  }

  assignFileOffsets(Asm, Layout);

  // MS LINK expects to be able to use this timestamp to implement their
  // /INCREMENTAL feature.
  if (Asm.isIncrementalLinkerCompatible()) {
    Header.TimeDateStamp = getTime();
  } else {
    // Have deterministic output if /INCREMENTAL isn't needed. Also matches GNU.
    Header.TimeDateStamp = 0;
  }

  // Write it all to disk...
  WriteFileHeader(Header);
  writeSectionHeaders();

  // Write section contents.
  sections::iterator I = Sections.begin();
  sections::iterator IE = Sections.end();
  MCAssembler::iterator J = Asm.begin();
  MCAssembler::iterator JE = Asm.end();
  for (; I != IE && J != JE; ++I, ++J)
    writeSection(Asm, Layout, **I, *J);

  assert(W.OS.tell() == Header.PointerToSymbolTable &&
         "Header::PointerToSymbolTable is insane!");

  // Write a symbol table.
  for (auto &Symbol : Symbols)
    if (Symbol->getIndex() != -1)
      WriteSymbol(*Symbol);

  // Write a string table, which completes the entire COFF file.
  Strings.write(W.OS);

  return W.OS.tell() - StartOffset;
}

MCWinCOFFObjectTargetWriter::MCWinCOFFObjectTargetWriter(unsigned Machine_)
    : Machine(Machine_) {}

// Pin the vtable to this file.
void MCWinCOFFObjectTargetWriter::anchor() {}

//------------------------------------------------------------------------------
// WinCOFFObjectWriter factory function

std::unique_ptr<MCObjectWriter> llvm::createWinCOFFObjectWriter(
    std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS) {
  return llvm::make_unique<WinCOFFObjectWriter>(std::move(MOTW), OS);
}
