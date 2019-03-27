//===- ELFObjectFile.h - ELF object file implementation ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ELFObjectFile template class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ELFOBJECTFILE_H
#define LLVM_OBJECT_ELFOBJECTFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/ARMAttributeParser.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <cstdint>
#include <system_error>

namespace llvm {
namespace object {

class elf_symbol_iterator;

class ELFObjectFileBase : public ObjectFile {
  friend class ELFRelocationRef;
  friend class ELFSectionRef;
  friend class ELFSymbolRef;

protected:
  ELFObjectFileBase(unsigned int Type, MemoryBufferRef Source);

  virtual uint16_t getEMachine() const = 0;
  virtual uint64_t getSymbolSize(DataRefImpl Symb) const = 0;
  virtual uint8_t getSymbolOther(DataRefImpl Symb) const = 0;
  virtual uint8_t getSymbolELFType(DataRefImpl Symb) const = 0;

  virtual uint32_t getSectionType(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionFlags(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionOffset(DataRefImpl Sec) const = 0;

  virtual Expected<int64_t> getRelocationAddend(DataRefImpl Rel) const = 0;

public:
  using elf_symbol_iterator_range = iterator_range<elf_symbol_iterator>;

  virtual elf_symbol_iterator_range getDynamicSymbolIterators() const = 0;

  /// Returns platform-specific object flags, if any.
  virtual unsigned getPlatformFlags() const = 0;

  elf_symbol_iterator_range symbols() const;

  static bool classof(const Binary *v) { return v->isELF(); }

  SubtargetFeatures getFeatures() const override;

  SubtargetFeatures getMIPSFeatures() const;

  SubtargetFeatures getARMFeatures() const;

  SubtargetFeatures getRISCVFeatures() const;

  void setARMSubArch(Triple &TheTriple) const override;

  virtual uint16_t getEType() const = 0;

  std::vector<std::pair<DataRefImpl, uint64_t>> getPltAddresses() const;
};

class ELFSectionRef : public SectionRef {
public:
  ELFSectionRef(const SectionRef &B) : SectionRef(B) {
    assert(isa<ELFObjectFileBase>(SectionRef::getObject()));
  }

  const ELFObjectFileBase *getObject() const {
    return cast<ELFObjectFileBase>(SectionRef::getObject());
  }

  uint32_t getType() const {
    return getObject()->getSectionType(getRawDataRefImpl());
  }

  uint64_t getFlags() const {
    return getObject()->getSectionFlags(getRawDataRefImpl());
  }

  uint64_t getOffset() const {
    return getObject()->getSectionOffset(getRawDataRefImpl());
  }
};

class elf_section_iterator : public section_iterator {
public:
  elf_section_iterator(const section_iterator &B) : section_iterator(B) {
    assert(isa<ELFObjectFileBase>(B->getObject()));
  }

  const ELFSectionRef *operator->() const {
    return static_cast<const ELFSectionRef *>(section_iterator::operator->());
  }

  const ELFSectionRef &operator*() const {
    return static_cast<const ELFSectionRef &>(section_iterator::operator*());
  }
};

class ELFSymbolRef : public SymbolRef {
public:
  ELFSymbolRef(const SymbolRef &B) : SymbolRef(B) {
    assert(isa<ELFObjectFileBase>(SymbolRef::getObject()));
  }

  const ELFObjectFileBase *getObject() const {
    return cast<ELFObjectFileBase>(BasicSymbolRef::getObject());
  }

  uint64_t getSize() const {
    return getObject()->getSymbolSize(getRawDataRefImpl());
  }

  uint8_t getOther() const {
    return getObject()->getSymbolOther(getRawDataRefImpl());
  }

  uint8_t getELFType() const {
    return getObject()->getSymbolELFType(getRawDataRefImpl());
  }
};

class elf_symbol_iterator : public symbol_iterator {
public:
  elf_symbol_iterator(const basic_symbol_iterator &B)
      : symbol_iterator(SymbolRef(B->getRawDataRefImpl(),
                                  cast<ELFObjectFileBase>(B->getObject()))) {}

  const ELFSymbolRef *operator->() const {
    return static_cast<const ELFSymbolRef *>(symbol_iterator::operator->());
  }

  const ELFSymbolRef &operator*() const {
    return static_cast<const ELFSymbolRef &>(symbol_iterator::operator*());
  }
};

class ELFRelocationRef : public RelocationRef {
public:
  ELFRelocationRef(const RelocationRef &B) : RelocationRef(B) {
    assert(isa<ELFObjectFileBase>(RelocationRef::getObject()));
  }

  const ELFObjectFileBase *getObject() const {
    return cast<ELFObjectFileBase>(RelocationRef::getObject());
  }

  Expected<int64_t> getAddend() const {
    return getObject()->getRelocationAddend(getRawDataRefImpl());
  }
};

class elf_relocation_iterator : public relocation_iterator {
public:
  elf_relocation_iterator(const relocation_iterator &B)
      : relocation_iterator(RelocationRef(
            B->getRawDataRefImpl(), cast<ELFObjectFileBase>(B->getObject()))) {}

  const ELFRelocationRef *operator->() const {
    return static_cast<const ELFRelocationRef *>(
        relocation_iterator::operator->());
  }

  const ELFRelocationRef &operator*() const {
    return static_cast<const ELFRelocationRef &>(
        relocation_iterator::operator*());
  }
};

inline ELFObjectFileBase::elf_symbol_iterator_range
ELFObjectFileBase::symbols() const {
  return elf_symbol_iterator_range(symbol_begin(), symbol_end());
}

template <class ELFT> class ELFObjectFile : public ELFObjectFileBase {
  uint16_t getEMachine() const override;
  uint16_t getEType() const override;
  uint64_t getSymbolSize(DataRefImpl Sym) const override;

public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  using uintX_t = typename ELFT::uint;

  using Elf_Sym = typename ELFT::Sym;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Ehdr = typename ELFT::Ehdr;
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;
  using Elf_Dyn = typename ELFT::Dyn;

private:
  ELFObjectFile(MemoryBufferRef Object, ELFFile<ELFT> EF,
                const Elf_Shdr *DotDynSymSec, const Elf_Shdr *DotSymtabSec,
                ArrayRef<Elf_Word> ShndxTable);

protected:
  ELFFile<ELFT> EF;

  const Elf_Shdr *DotDynSymSec = nullptr; // Dynamic symbol table section.
  const Elf_Shdr *DotSymtabSec = nullptr; // Symbol table section.
  ArrayRef<Elf_Word> ShndxTable;

  void moveSymbolNext(DataRefImpl &Symb) const override;
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  uint32_t getSymbolFlags(DataRefImpl Symb) const override;
  uint8_t getSymbolOther(DataRefImpl Symb) const override;
  uint8_t getSymbolELFType(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(const Elf_Sym *Symb,
                                              const Elf_Shdr *SymTab) const;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;

  void moveSectionNext(DataRefImpl &Sec) const override;
  std::error_code getSectionName(DataRefImpl Sec,
                                 StringRef &Res) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  std::error_code getSectionContents(DataRefImpl Sec,
                                     StringRef &Res) const override;
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  bool isSectionCompressed(DataRefImpl Sec) const override;
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override;
  bool isSectionVirtual(DataRefImpl Sec) const override;
  bool isBerkeleyText(DataRefImpl Sec) const override;
  bool isBerkeleyData(DataRefImpl Sec) const override;
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;
  std::vector<SectionRef> dynamic_relocation_sections() const override;
  section_iterator getRelocatedSection(DataRefImpl Sec) const override;

  void moveRelocationNext(DataRefImpl &Rel) const override;
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  uint32_t getSectionType(DataRefImpl Sec) const override;
  uint64_t getSectionFlags(DataRefImpl Sec) const override;
  uint64_t getSectionOffset(DataRefImpl Sec) const override;
  StringRef getRelocationTypeName(uint32_t Type) const;

  /// Get the relocation section that contains \a Rel.
  const Elf_Shdr *getRelSection(DataRefImpl Rel) const {
    auto RelSecOrErr = EF.getSection(Rel.d.a);
    if (!RelSecOrErr)
      report_fatal_error(errorToErrorCode(RelSecOrErr.takeError()).message());
    return *RelSecOrErr;
  }

  DataRefImpl toDRI(const Elf_Shdr *SymTable, unsigned SymbolNum) const {
    DataRefImpl DRI;
    if (!SymTable) {
      DRI.d.a = 0;
      DRI.d.b = 0;
      return DRI;
    }
    assert(SymTable->sh_type == ELF::SHT_SYMTAB ||
           SymTable->sh_type == ELF::SHT_DYNSYM);

    auto SectionsOrErr = EF.sections();
    if (!SectionsOrErr) {
      DRI.d.a = 0;
      DRI.d.b = 0;
      return DRI;
    }
    uintptr_t SHT = reinterpret_cast<uintptr_t>((*SectionsOrErr).begin());
    unsigned SymTableIndex =
        (reinterpret_cast<uintptr_t>(SymTable) - SHT) / sizeof(Elf_Shdr);

    DRI.d.a = SymTableIndex;
    DRI.d.b = SymbolNum;
    return DRI;
  }

  const Elf_Shdr *toELFShdrIter(DataRefImpl Sec) const {
    return reinterpret_cast<const Elf_Shdr *>(Sec.p);
  }

  DataRefImpl toDRI(const Elf_Shdr *Sec) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Sec);
    return DRI;
  }

  DataRefImpl toDRI(const Elf_Dyn *Dyn) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Dyn);
    return DRI;
  }

  bool isExportedToOtherDSO(const Elf_Sym *ESym) const {
    unsigned char Binding = ESym->getBinding();
    unsigned char Visibility = ESym->getVisibility();

    // A symbol is exported if its binding is either GLOBAL or WEAK, and its
    // visibility is either DEFAULT or PROTECTED. All other symbols are not
    // exported.
    return (
        (Binding == ELF::STB_GLOBAL || Binding == ELF::STB_WEAK ||
         Binding == ELF::STB_GNU_UNIQUE) &&
        (Visibility == ELF::STV_DEFAULT || Visibility == ELF::STV_PROTECTED));
  }

  // This flag is used for classof, to distinguish ELFObjectFile from
  // its subclass. If more subclasses will be created, this flag will
  // have to become an enum.
  bool isDyldELFObject;

public:
  ELFObjectFile(ELFObjectFile<ELFT> &&Other);
  static Expected<ELFObjectFile<ELFT>> create(MemoryBufferRef Object);

  const Elf_Rel *getRel(DataRefImpl Rel) const;
  const Elf_Rela *getRela(DataRefImpl Rela) const;

  const Elf_Sym *getSymbol(DataRefImpl Sym) const {
    auto Ret = EF.template getEntry<Elf_Sym>(Sym.d.a, Sym.d.b);
    if (!Ret)
      report_fatal_error(errorToErrorCode(Ret.takeError()).message());
    return *Ret;
  }

  const Elf_Shdr *getSection(DataRefImpl Sec) const {
    return reinterpret_cast<const Elf_Shdr *>(Sec.p);
  }

  basic_symbol_iterator symbol_begin() const override;
  basic_symbol_iterator symbol_end() const override;

  elf_symbol_iterator dynamic_symbol_begin() const;
  elf_symbol_iterator dynamic_symbol_end() const;

  section_iterator section_begin() const override;
  section_iterator section_end() const override;

  Expected<int64_t> getRelocationAddend(DataRefImpl Rel) const override;

  uint8_t getBytesInAddress() const override;
  StringRef getFileFormatName() const override;
  Triple::ArchType getArch() const override;
  Expected<uint64_t> getStartAddress() const override;

  unsigned getPlatformFlags() const override { return EF.getHeader()->e_flags; }

  std::error_code getBuildAttributes(ARMAttributeParser &Attributes) const override {
    auto SectionsOrErr = EF.sections();
    if (!SectionsOrErr)
      return errorToErrorCode(SectionsOrErr.takeError());

    for (const Elf_Shdr &Sec : *SectionsOrErr) {
      if (Sec.sh_type == ELF::SHT_ARM_ATTRIBUTES) {
        auto ErrorOrContents = EF.getSectionContents(&Sec);
        if (!ErrorOrContents)
          return errorToErrorCode(ErrorOrContents.takeError());

        auto Contents = ErrorOrContents.get();
        if (Contents[0] != ARMBuildAttrs::Format_Version || Contents.size() == 1)
          return std::error_code();

        Attributes.Parse(Contents, ELFT::TargetEndianness == support::little);
        break;
      }
    }
    return std::error_code();
  }

  const ELFFile<ELFT> *getELFFile() const { return &EF; }

  bool isDyldType() const { return isDyldELFObject; }
  static bool classof(const Binary *v) {
    return v->getType() == getELFType(ELFT::TargetEndianness == support::little,
                                      ELFT::Is64Bits);
  }

  elf_symbol_iterator_range getDynamicSymbolIterators() const override;

  bool isRelocatableObject() const override;
};

using ELF32LEObjectFile = ELFObjectFile<ELF32LE>;
using ELF64LEObjectFile = ELFObjectFile<ELF64LE>;
using ELF32BEObjectFile = ELFObjectFile<ELF32BE>;
using ELF64BEObjectFile = ELFObjectFile<ELF64BE>;

template <class ELFT>
void ELFObjectFile<ELFT>::moveSymbolNext(DataRefImpl &Sym) const {
  ++Sym.d.b;
}

template <class ELFT>
Expected<StringRef> ELFObjectFile<ELFT>::getSymbolName(DataRefImpl Sym) const {
  const Elf_Sym *ESym = getSymbol(Sym);
  auto SymTabOrErr = EF.getSection(Sym.d.a);
  if (!SymTabOrErr)
    return SymTabOrErr.takeError();
  const Elf_Shdr *SymTableSec = *SymTabOrErr;
  auto StrTabOrErr = EF.getSection(SymTableSec->sh_link);
  if (!StrTabOrErr)
    return StrTabOrErr.takeError();
  const Elf_Shdr *StringTableSec = *StrTabOrErr;
  auto SymStrTabOrErr = EF.getStringTable(StringTableSec);
  if (!SymStrTabOrErr)
    return SymStrTabOrErr.takeError();
  return ESym->getName(*SymStrTabOrErr);
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionFlags(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags;
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSectionType(DataRefImpl Sec) const {
  return getSection(Sec)->sh_type;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionOffset(DataRefImpl Sec) const {
  return getSection(Sec)->sh_offset;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSymbolValueImpl(DataRefImpl Symb) const {
  const Elf_Sym *ESym = getSymbol(Symb);
  uint64_t Ret = ESym->st_value;
  if (ESym->st_shndx == ELF::SHN_ABS)
    return Ret;

  const Elf_Ehdr *Header = EF.getHeader();
  // Clear the ARM/Thumb or microMIPS indicator flag.
  if ((Header->e_machine == ELF::EM_ARM || Header->e_machine == ELF::EM_MIPS) &&
      ESym->getType() == ELF::STT_FUNC)
    Ret &= ~1;

  return Ret;
}

template <class ELFT>
Expected<uint64_t>
ELFObjectFile<ELFT>::getSymbolAddress(DataRefImpl Symb) const {
  uint64_t Result = getSymbolValue(Symb);
  const Elf_Sym *ESym = getSymbol(Symb);
  switch (ESym->st_shndx) {
  case ELF::SHN_COMMON:
  case ELF::SHN_UNDEF:
  case ELF::SHN_ABS:
    return Result;
  }

  const Elf_Ehdr *Header = EF.getHeader();
  auto SymTabOrErr = EF.getSection(Symb.d.a);
  if (!SymTabOrErr)
    return SymTabOrErr.takeError();
  const Elf_Shdr *SymTab = *SymTabOrErr;

  if (Header->e_type == ELF::ET_REL) {
    auto SectionOrErr = EF.getSection(ESym, SymTab, ShndxTable);
    if (!SectionOrErr)
      return SectionOrErr.takeError();
    const Elf_Shdr *Section = *SectionOrErr;
    if (Section)
      Result += Section->sh_addr;
  }

  return Result;
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSymbolAlignment(DataRefImpl Symb) const {
  const Elf_Sym *Sym = getSymbol(Symb);
  if (Sym->st_shndx == ELF::SHN_COMMON)
    return Sym->st_value;
  return 0;
}

template <class ELFT>
uint16_t ELFObjectFile<ELFT>::getEMachine() const {
  return EF.getHeader()->e_machine;
}

template <class ELFT> uint16_t ELFObjectFile<ELFT>::getEType() const {
  return EF.getHeader()->e_type;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSymbolSize(DataRefImpl Sym) const {
  return getSymbol(Sym)->st_size;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getCommonSymbolSizeImpl(DataRefImpl Symb) const {
  return getSymbol(Symb)->st_size;
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getSymbolOther(DataRefImpl Symb) const {
  return getSymbol(Symb)->st_other;
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getSymbolELFType(DataRefImpl Symb) const {
  return getSymbol(Symb)->getType();
}

template <class ELFT>
Expected<SymbolRef::Type>
ELFObjectFile<ELFT>::getSymbolType(DataRefImpl Symb) const {
  const Elf_Sym *ESym = getSymbol(Symb);

  switch (ESym->getType()) {
  case ELF::STT_NOTYPE:
    return SymbolRef::ST_Unknown;
  case ELF::STT_SECTION:
    return SymbolRef::ST_Debug;
  case ELF::STT_FILE:
    return SymbolRef::ST_File;
  case ELF::STT_FUNC:
    return SymbolRef::ST_Function;
  case ELF::STT_OBJECT:
  case ELF::STT_COMMON:
  case ELF::STT_TLS:
    return SymbolRef::ST_Data;
  default:
    return SymbolRef::ST_Other;
  }
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSymbolFlags(DataRefImpl Sym) const {
  const Elf_Sym *ESym = getSymbol(Sym);

  uint32_t Result = SymbolRef::SF_None;

  if (ESym->getBinding() != ELF::STB_LOCAL)
    Result |= SymbolRef::SF_Global;

  if (ESym->getBinding() == ELF::STB_WEAK)
    Result |= SymbolRef::SF_Weak;

  if (ESym->st_shndx == ELF::SHN_ABS)
    Result |= SymbolRef::SF_Absolute;

  if (ESym->getType() == ELF::STT_FILE || ESym->getType() == ELF::STT_SECTION)
    Result |= SymbolRef::SF_FormatSpecific;

  auto DotSymtabSecSyms = EF.symbols(DotSymtabSec);
  if (DotSymtabSecSyms && ESym == (*DotSymtabSecSyms).begin())
    Result |= SymbolRef::SF_FormatSpecific;
  auto DotDynSymSecSyms = EF.symbols(DotDynSymSec);
  if (DotDynSymSecSyms && ESym == (*DotDynSymSecSyms).begin())
    Result |= SymbolRef::SF_FormatSpecific;

  if (EF.getHeader()->e_machine == ELF::EM_ARM) {
    if (Expected<StringRef> NameOrErr = getSymbolName(Sym)) {
      StringRef Name = *NameOrErr;
      if (Name.startswith("$d") || Name.startswith("$t") ||
          Name.startswith("$a"))
        Result |= SymbolRef::SF_FormatSpecific;
    } else {
      // TODO: Actually report errors helpfully.
      consumeError(NameOrErr.takeError());
    }
    if (ESym->getType() == ELF::STT_FUNC && (ESym->st_value & 1) == 1)
      Result |= SymbolRef::SF_Thumb;
  }

  if (ESym->st_shndx == ELF::SHN_UNDEF)
    Result |= SymbolRef::SF_Undefined;

  if (ESym->getType() == ELF::STT_COMMON || ESym->st_shndx == ELF::SHN_COMMON)
    Result |= SymbolRef::SF_Common;

  if (isExportedToOtherDSO(ESym))
    Result |= SymbolRef::SF_Exported;

  if (ESym->getVisibility() == ELF::STV_HIDDEN)
    Result |= SymbolRef::SF_Hidden;

  return Result;
}

template <class ELFT>
Expected<section_iterator>
ELFObjectFile<ELFT>::getSymbolSection(const Elf_Sym *ESym,
                                      const Elf_Shdr *SymTab) const {
  auto ESecOrErr = EF.getSection(ESym, SymTab, ShndxTable);
  if (!ESecOrErr)
    return ESecOrErr.takeError();

  const Elf_Shdr *ESec = *ESecOrErr;
  if (!ESec)
    return section_end();

  DataRefImpl Sec;
  Sec.p = reinterpret_cast<intptr_t>(ESec);
  return section_iterator(SectionRef(Sec, this));
}

template <class ELFT>
Expected<section_iterator>
ELFObjectFile<ELFT>::getSymbolSection(DataRefImpl Symb) const {
  const Elf_Sym *Sym = getSymbol(Symb);
  auto SymTabOrErr = EF.getSection(Symb.d.a);
  if (!SymTabOrErr)
    return SymTabOrErr.takeError();
  const Elf_Shdr *SymTab = *SymTabOrErr;
  return getSymbolSection(Sym, SymTab);
}

template <class ELFT>
void ELFObjectFile<ELFT>::moveSectionNext(DataRefImpl &Sec) const {
  const Elf_Shdr *ESec = getSection(Sec);
  Sec = toDRI(++ESec);
}

template <class ELFT>
std::error_code ELFObjectFile<ELFT>::getSectionName(DataRefImpl Sec,
                                                    StringRef &Result) const {
  auto Name = EF.getSectionName(&*getSection(Sec));
  if (!Name)
    return errorToErrorCode(Name.takeError());
  Result = *Name;
  return std::error_code();
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionAddress(DataRefImpl Sec) const {
  return getSection(Sec)->sh_addr;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionIndex(DataRefImpl Sec) const {
  auto SectionsOrErr = EF.sections();
  handleAllErrors(std::move(SectionsOrErr.takeError()),
                  [](const ErrorInfoBase &) {
                    llvm_unreachable("unable to get section index");
                  });
  const Elf_Shdr *First = SectionsOrErr->begin();
  return getSection(Sec) - First;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionSize(DataRefImpl Sec) const {
  return getSection(Sec)->sh_size;
}

template <class ELFT>
std::error_code
ELFObjectFile<ELFT>::getSectionContents(DataRefImpl Sec,
                                        StringRef &Result) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  if (std::error_code EC =
          checkOffset(getMemoryBufferRef(),
                      (uintptr_t)base() + EShdr->sh_offset, EShdr->sh_size))
    return EC;
  Result = StringRef((const char *)base() + EShdr->sh_offset, EShdr->sh_size);
  return std::error_code();
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionAlignment(DataRefImpl Sec) const {
  return getSection(Sec)->sh_addralign;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionCompressed(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags & ELF::SHF_COMPRESSED;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionText(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags & ELF::SHF_EXECINSTR;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionData(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  return EShdr->sh_type == ELF::SHT_PROGBITS &&
         EShdr->sh_flags & ELF::SHF_ALLOC &&
         !(EShdr->sh_flags & ELF::SHF_EXECINSTR);
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionBSS(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  return EShdr->sh_flags & (ELF::SHF_ALLOC | ELF::SHF_WRITE) &&
         EShdr->sh_type == ELF::SHT_NOBITS;
}

template <class ELFT>
std::vector<SectionRef>
ELFObjectFile<ELFT>::dynamic_relocation_sections() const {
  std::vector<SectionRef> Res;
  std::vector<uintptr_t> Offsets;

  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return Res;

  for (const Elf_Shdr &Sec : *SectionsOrErr) {
    if (Sec.sh_type != ELF::SHT_DYNAMIC)
      continue;
    Elf_Dyn *Dynamic =
        reinterpret_cast<Elf_Dyn *>((uintptr_t)base() + Sec.sh_offset);
    for (; Dynamic->d_tag != ELF::DT_NULL; Dynamic++) {
      if (Dynamic->d_tag == ELF::DT_REL || Dynamic->d_tag == ELF::DT_RELA ||
          Dynamic->d_tag == ELF::DT_JMPREL) {
        Offsets.push_back(Dynamic->d_un.d_val);
      }
    }
  }
  for (const Elf_Shdr &Sec : *SectionsOrErr) {
    if (is_contained(Offsets, Sec.sh_offset))
      Res.emplace_back(toDRI(&Sec), this);
  }
  return Res;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionVirtual(DataRefImpl Sec) const {
  return getSection(Sec)->sh_type == ELF::SHT_NOBITS;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isBerkeleyText(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags & ELF::SHF_ALLOC &&
         (getSection(Sec)->sh_flags & ELF::SHF_EXECINSTR ||
          !(getSection(Sec)->sh_flags & ELF::SHF_WRITE));
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isBerkeleyData(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  return !isBerkeleyText(Sec) && EShdr->sh_type != ELF::SHT_NOBITS &&
         EShdr->sh_flags & ELF::SHF_ALLOC;
}

template <class ELFT>
relocation_iterator
ELFObjectFile<ELFT>::section_rel_begin(DataRefImpl Sec) const {
  DataRefImpl RelData;
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return relocation_iterator(RelocationRef());
  uintptr_t SHT = reinterpret_cast<uintptr_t>((*SectionsOrErr).begin());
  RelData.d.a = (Sec.p - SHT) / EF.getHeader()->e_shentsize;
  RelData.d.b = 0;
  return relocation_iterator(RelocationRef(RelData, this));
}

template <class ELFT>
relocation_iterator
ELFObjectFile<ELFT>::section_rel_end(DataRefImpl Sec) const {
  const Elf_Shdr *S = reinterpret_cast<const Elf_Shdr *>(Sec.p);
  relocation_iterator Begin = section_rel_begin(Sec);
  if (S->sh_type != ELF::SHT_RELA && S->sh_type != ELF::SHT_REL)
    return Begin;
  DataRefImpl RelData = Begin->getRawDataRefImpl();
  const Elf_Shdr *RelSec = getRelSection(RelData);

  // Error check sh_link here so that getRelocationSymbol can just use it.
  auto SymSecOrErr = EF.getSection(RelSec->sh_link);
  if (!SymSecOrErr)
    report_fatal_error(errorToErrorCode(SymSecOrErr.takeError()).message());

  RelData.d.b += S->sh_size / S->sh_entsize;
  return relocation_iterator(RelocationRef(RelData, this));
}

template <class ELFT>
section_iterator
ELFObjectFile<ELFT>::getRelocatedSection(DataRefImpl Sec) const {
  if (EF.getHeader()->e_type != ELF::ET_REL)
    return section_end();

  const Elf_Shdr *EShdr = getSection(Sec);
  uintX_t Type = EShdr->sh_type;
  if (Type != ELF::SHT_REL && Type != ELF::SHT_RELA)
    return section_end();

  auto R = EF.getSection(EShdr->sh_info);
  if (!R)
    report_fatal_error(errorToErrorCode(R.takeError()).message());
  return section_iterator(SectionRef(toDRI(*R), this));
}

// Relocations
template <class ELFT>
void ELFObjectFile<ELFT>::moveRelocationNext(DataRefImpl &Rel) const {
  ++Rel.d.b;
}

template <class ELFT>
symbol_iterator
ELFObjectFile<ELFT>::getRelocationSymbol(DataRefImpl Rel) const {
  uint32_t symbolIdx;
  const Elf_Shdr *sec = getRelSection(Rel);
  if (sec->sh_type == ELF::SHT_REL)
    symbolIdx = getRel(Rel)->getSymbol(EF.isMips64EL());
  else
    symbolIdx = getRela(Rel)->getSymbol(EF.isMips64EL());
  if (!symbolIdx)
    return symbol_end();

  // FIXME: error check symbolIdx
  DataRefImpl SymbolData;
  SymbolData.d.a = sec->sh_link;
  SymbolData.d.b = symbolIdx;
  return symbol_iterator(SymbolRef(SymbolData, this));
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getRelocationOffset(DataRefImpl Rel) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  if (sec->sh_type == ELF::SHT_REL)
    return getRel(Rel)->r_offset;

  return getRela(Rel)->r_offset;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getRelocationType(DataRefImpl Rel) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  if (sec->sh_type == ELF::SHT_REL)
    return getRel(Rel)->getType(EF.isMips64EL());
  else
    return getRela(Rel)->getType(EF.isMips64EL());
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getRelocationTypeName(uint32_t Type) const {
  return getELFRelocationTypeName(EF.getHeader()->e_machine, Type);
}

template <class ELFT>
void ELFObjectFile<ELFT>::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  uint32_t type = getRelocationType(Rel);
  EF.getRelocationTypeName(type, Result);
}

template <class ELFT>
Expected<int64_t>
ELFObjectFile<ELFT>::getRelocationAddend(DataRefImpl Rel) const {
  if (getRelSection(Rel)->sh_type != ELF::SHT_RELA)
    return createError("Section is not SHT_RELA");
  return (int64_t)getRela(Rel)->r_addend;
}

template <class ELFT>
const typename ELFObjectFile<ELFT>::Elf_Rel *
ELFObjectFile<ELFT>::getRel(DataRefImpl Rel) const {
  assert(getRelSection(Rel)->sh_type == ELF::SHT_REL);
  auto Ret = EF.template getEntry<Elf_Rel>(Rel.d.a, Rel.d.b);
  if (!Ret)
    report_fatal_error(errorToErrorCode(Ret.takeError()).message());
  return *Ret;
}

template <class ELFT>
const typename ELFObjectFile<ELFT>::Elf_Rela *
ELFObjectFile<ELFT>::getRela(DataRefImpl Rela) const {
  assert(getRelSection(Rela)->sh_type == ELF::SHT_RELA);
  auto Ret = EF.template getEntry<Elf_Rela>(Rela.d.a, Rela.d.b);
  if (!Ret)
    report_fatal_error(errorToErrorCode(Ret.takeError()).message());
  return *Ret;
}

template <class ELFT>
Expected<ELFObjectFile<ELFT>>
ELFObjectFile<ELFT>::create(MemoryBufferRef Object) {
  auto EFOrErr = ELFFile<ELFT>::create(Object.getBuffer());
  if (Error E = EFOrErr.takeError())
    return std::move(E);
  auto EF = std::move(*EFOrErr);

  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return SectionsOrErr.takeError();

  const Elf_Shdr *DotDynSymSec = nullptr;
  const Elf_Shdr *DotSymtabSec = nullptr;
  ArrayRef<Elf_Word> ShndxTable;
  for (const Elf_Shdr &Sec : *SectionsOrErr) {
    switch (Sec.sh_type) {
    case ELF::SHT_DYNSYM: {
      if (DotDynSymSec)
        return createError("More than one dynamic symbol table!");
      DotDynSymSec = &Sec;
      break;
    }
    case ELF::SHT_SYMTAB: {
      if (DotSymtabSec)
        return createError("More than one static symbol table!");
      DotSymtabSec = &Sec;
      break;
    }
    case ELF::SHT_SYMTAB_SHNDX: {
      auto TableOrErr = EF.getSHNDXTable(Sec);
      if (!TableOrErr)
        return TableOrErr.takeError();
      ShndxTable = *TableOrErr;
      break;
    }
    }
  }
  return ELFObjectFile<ELFT>(Object, EF, DotDynSymSec, DotSymtabSec,
                             ShndxTable);
}

template <class ELFT>
ELFObjectFile<ELFT>::ELFObjectFile(MemoryBufferRef Object, ELFFile<ELFT> EF,
                                   const Elf_Shdr *DotDynSymSec,
                                   const Elf_Shdr *DotSymtabSec,
                                   ArrayRef<Elf_Word> ShndxTable)
    : ELFObjectFileBase(
          getELFType(ELFT::TargetEndianness == support::little, ELFT::Is64Bits),
          Object),
      EF(EF), DotDynSymSec(DotDynSymSec), DotSymtabSec(DotSymtabSec),
      ShndxTable(ShndxTable) {}

template <class ELFT>
ELFObjectFile<ELFT>::ELFObjectFile(ELFObjectFile<ELFT> &&Other)
    : ELFObjectFile(Other.Data, Other.EF, Other.DotDynSymSec,
                    Other.DotSymtabSec, Other.ShndxTable) {}

template <class ELFT>
basic_symbol_iterator ELFObjectFile<ELFT>::symbol_begin() const {
  DataRefImpl Sym = toDRI(DotSymtabSec, 0);
  return basic_symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
basic_symbol_iterator ELFObjectFile<ELFT>::symbol_end() const {
  const Elf_Shdr *SymTab = DotSymtabSec;
  if (!SymTab)
    return symbol_begin();
  DataRefImpl Sym = toDRI(SymTab, SymTab->sh_size / sizeof(Elf_Sym));
  return basic_symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
elf_symbol_iterator ELFObjectFile<ELFT>::dynamic_symbol_begin() const {
  DataRefImpl Sym = toDRI(DotDynSymSec, 0);
  return symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
elf_symbol_iterator ELFObjectFile<ELFT>::dynamic_symbol_end() const {
  const Elf_Shdr *SymTab = DotDynSymSec;
  if (!SymTab)
    return dynamic_symbol_begin();
  DataRefImpl Sym = toDRI(SymTab, SymTab->sh_size / sizeof(Elf_Sym));
  return basic_symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
section_iterator ELFObjectFile<ELFT>::section_begin() const {
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return section_iterator(SectionRef());
  return section_iterator(SectionRef(toDRI((*SectionsOrErr).begin()), this));
}

template <class ELFT>
section_iterator ELFObjectFile<ELFT>::section_end() const {
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return section_iterator(SectionRef());
  return section_iterator(SectionRef(toDRI((*SectionsOrErr).end()), this));
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getBytesInAddress() const {
  return ELFT::Is64Bits ? 8 : 4;
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getFileFormatName() const {
  bool IsLittleEndian = ELFT::TargetEndianness == support::little;
  switch (EF.getHeader()->e_ident[ELF::EI_CLASS]) {
  case ELF::ELFCLASS32:
    switch (EF.getHeader()->e_machine) {
    case ELF::EM_386:
      return "ELF32-i386";
    case ELF::EM_IAMCU:
      return "ELF32-iamcu";
    case ELF::EM_X86_64:
      return "ELF32-x86-64";
    case ELF::EM_ARM:
      return (IsLittleEndian ? "ELF32-arm-little" : "ELF32-arm-big");
    case ELF::EM_AVR:
      return "ELF32-avr";
    case ELF::EM_HEXAGON:
      return "ELF32-hexagon";
    case ELF::EM_LANAI:
      return "ELF32-lanai";
    case ELF::EM_MIPS:
      return "ELF32-mips";
    case ELF::EM_MSP430:
      return "ELF32-msp430";
    case ELF::EM_PPC:
      return "ELF32-ppc";
    case ELF::EM_RISCV:
      return "ELF32-riscv";
    case ELF::EM_SPARC:
    case ELF::EM_SPARC32PLUS:
      return "ELF32-sparc";
    case ELF::EM_AMDGPU:
      return "ELF32-amdgpu";
    default:
      return "ELF32-unknown";
    }
  case ELF::ELFCLASS64:
    switch (EF.getHeader()->e_machine) {
    case ELF::EM_386:
      return "ELF64-i386";
    case ELF::EM_X86_64:
      return "ELF64-x86-64";
    case ELF::EM_AARCH64:
      return (IsLittleEndian ? "ELF64-aarch64-little" : "ELF64-aarch64-big");
    case ELF::EM_PPC64:
      return "ELF64-ppc64";
    case ELF::EM_RISCV:
      return "ELF64-riscv";
    case ELF::EM_S390:
      return "ELF64-s390";
    case ELF::EM_SPARCV9:
      return "ELF64-sparc";
    case ELF::EM_MIPS:
      return "ELF64-mips";
    case ELF::EM_AMDGPU:
      return "ELF64-amdgpu";
    case ELF::EM_BPF:
      return "ELF64-BPF";
    default:
      return "ELF64-unknown";
    }
  default:
    // FIXME: Proper error handling.
    report_fatal_error("Invalid ELFCLASS!");
  }
}

template <class ELFT> Triple::ArchType ELFObjectFile<ELFT>::getArch() const {
  bool IsLittleEndian = ELFT::TargetEndianness == support::little;
  switch (EF.getHeader()->e_machine) {
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    return Triple::x86;
  case ELF::EM_X86_64:
    return Triple::x86_64;
  case ELF::EM_AARCH64:
    return IsLittleEndian ? Triple::aarch64 : Triple::aarch64_be;
  case ELF::EM_ARM:
    return Triple::arm;
  case ELF::EM_AVR:
    return Triple::avr;
  case ELF::EM_HEXAGON:
    return Triple::hexagon;
  case ELF::EM_LANAI:
    return Triple::lanai;
  case ELF::EM_MIPS:
    switch (EF.getHeader()->e_ident[ELF::EI_CLASS]) {
    case ELF::ELFCLASS32:
      return IsLittleEndian ? Triple::mipsel : Triple::mips;
    case ELF::ELFCLASS64:
      return IsLittleEndian ? Triple::mips64el : Triple::mips64;
    default:
      report_fatal_error("Invalid ELFCLASS!");
    }
  case ELF::EM_MSP430:
    return Triple::msp430;
  case ELF::EM_PPC:
    return Triple::ppc;
  case ELF::EM_PPC64:
    return IsLittleEndian ? Triple::ppc64le : Triple::ppc64;
  case ELF::EM_RISCV:
    switch (EF.getHeader()->e_ident[ELF::EI_CLASS]) {
    case ELF::ELFCLASS32:
      return Triple::riscv32;
    case ELF::ELFCLASS64:
      return Triple::riscv64;
    default:
      report_fatal_error("Invalid ELFCLASS!");
    }
  case ELF::EM_S390:
    return Triple::systemz;

  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
    return IsLittleEndian ? Triple::sparcel : Triple::sparc;
  case ELF::EM_SPARCV9:
    return Triple::sparcv9;

  case ELF::EM_AMDGPU: {
    if (!IsLittleEndian)
      return Triple::UnknownArch;

    unsigned MACH = EF.getHeader()->e_flags & ELF::EF_AMDGPU_MACH;
    if (MACH >= ELF::EF_AMDGPU_MACH_R600_FIRST &&
        MACH <= ELF::EF_AMDGPU_MACH_R600_LAST)
      return Triple::r600;
    if (MACH >= ELF::EF_AMDGPU_MACH_AMDGCN_FIRST &&
        MACH <= ELF::EF_AMDGPU_MACH_AMDGCN_LAST)
      return Triple::amdgcn;

    return Triple::UnknownArch;
  }

  case ELF::EM_BPF:
    return IsLittleEndian ? Triple::bpfel : Triple::bpfeb;

  default:
    return Triple::UnknownArch;
  }
}

template <class ELFT>
Expected<uint64_t> ELFObjectFile<ELFT>::getStartAddress() const {
  return EF.getHeader()->e_entry;
}

template <class ELFT>
ELFObjectFileBase::elf_symbol_iterator_range
ELFObjectFile<ELFT>::getDynamicSymbolIterators() const {
  return make_range(dynamic_symbol_begin(), dynamic_symbol_end());
}

template <class ELFT> bool ELFObjectFile<ELFT>::isRelocatableObject() const {
  return EF.getHeader()->e_type == ELF::ET_REL;
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_ELFOBJECTFILE_H
