//===- ELF.cpp - ELF object file implementation ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/LEB128.h"

using namespace llvm;
using namespace object;

#define STRINGIFY_ENUM_CASE(ns, name)                                          \
  case ns::name:                                                               \
    return #name;

#define ELF_RELOC(name, value) STRINGIFY_ENUM_CASE(ELF, name)

StringRef llvm::object::getELFRelocationTypeName(uint32_t Machine,
                                                 uint32_t Type) {
  switch (Machine) {
  case ELF::EM_X86_64:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/x86_64.def"
    default:
      break;
    }
    break;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/i386.def"
    default:
      break;
    }
    break;
  case ELF::EM_MIPS:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Mips.def"
    default:
      break;
    }
    break;
  case ELF::EM_AARCH64:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/AArch64.def"
    default:
      break;
    }
    break;
  case ELF::EM_ARM:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/ARM.def"
    default:
      break;
    }
    break;
  case ELF::EM_ARC_COMPACT:
  case ELF::EM_ARC_COMPACT2:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/ARC.def"
    default:
      break;
    }
    break;
  case ELF::EM_AVR:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/AVR.def"
    default:
      break;
    }
    break;
  case ELF::EM_HEXAGON:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Hexagon.def"
    default:
      break;
    }
    break;
  case ELF::EM_LANAI:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Lanai.def"
    default:
      break;
    }
    break;
  case ELF::EM_PPC:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/PowerPC.def"
    default:
      break;
    }
    break;
  case ELF::EM_PPC64:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/PowerPC64.def"
    default:
      break;
    }
    break;
  case ELF::EM_RISCV:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/RISCV.def"
    default:
      break;
    }
    break;
  case ELF::EM_S390:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/SystemZ.def"
    default:
      break;
    }
    break;
  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
  case ELF::EM_SPARCV9:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/Sparc.def"
    default:
      break;
    }
    break;
  case ELF::EM_AMDGPU:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/AMDGPU.def"
    default:
      break;
    }
    break;
  case ELF::EM_BPF:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/BPF.def"
    default:
      break;
    }
    break;
  case ELF::EM_MSP430:
    switch (Type) {
#include "llvm/BinaryFormat/ELFRelocs/MSP430.def"
    default:
      break;
    }
    break;
  default:
    break;
  }
  return "Unknown";
}

#undef ELF_RELOC

uint32_t llvm::object::getELFRelativeRelocationType(uint32_t Machine) {
  switch (Machine) {
  case ELF::EM_X86_64:
    return ELF::R_X86_64_RELATIVE;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    return ELF::R_386_RELATIVE;
  case ELF::EM_MIPS:
    break;
  case ELF::EM_AARCH64:
    return ELF::R_AARCH64_RELATIVE;
  case ELF::EM_ARM:
    return ELF::R_ARM_RELATIVE;
  case ELF::EM_ARC_COMPACT:
  case ELF::EM_ARC_COMPACT2:
    return ELF::R_ARC_RELATIVE;
  case ELF::EM_AVR:
    break;
  case ELF::EM_HEXAGON:
    return ELF::R_HEX_RELATIVE;
  case ELF::EM_LANAI:
    break;
  case ELF::EM_PPC:
    break;
  case ELF::EM_PPC64:
    return ELF::R_PPC64_RELATIVE;
  case ELF::EM_RISCV:
    return ELF::R_RISCV_RELATIVE;
  case ELF::EM_S390:
    return ELF::R_390_RELATIVE;
  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
  case ELF::EM_SPARCV9:
    return ELF::R_SPARC_RELATIVE;
  case ELF::EM_AMDGPU:
    break;
  case ELF::EM_BPF:
    break;
  default:
    break;
  }
  return 0;
}

StringRef llvm::object::getELFSectionTypeName(uint32_t Machine, unsigned Type) {
  switch (Machine) {
  case ELF::EM_ARM:
    switch (Type) {
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_EXIDX);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_PREEMPTMAP);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_ATTRIBUTES);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_DEBUGOVERLAY);
      STRINGIFY_ENUM_CASE(ELF, SHT_ARM_OVERLAYSECTION);
    }
    break;
  case ELF::EM_HEXAGON:
    switch (Type) { STRINGIFY_ENUM_CASE(ELF, SHT_HEX_ORDERED); }
    break;
  case ELF::EM_X86_64:
    switch (Type) { STRINGIFY_ENUM_CASE(ELF, SHT_X86_64_UNWIND); }
    break;
  case ELF::EM_MIPS:
  case ELF::EM_MIPS_RS3_LE:
    switch (Type) {
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_REGINFO);
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_OPTIONS);
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_ABIFLAGS);
      STRINGIFY_ENUM_CASE(ELF, SHT_MIPS_DWARF);
    }
    break;
  default:
    break;
  }

  switch (Type) {
    STRINGIFY_ENUM_CASE(ELF, SHT_NULL);
    STRINGIFY_ENUM_CASE(ELF, SHT_PROGBITS);
    STRINGIFY_ENUM_CASE(ELF, SHT_SYMTAB);
    STRINGIFY_ENUM_CASE(ELF, SHT_STRTAB);
    STRINGIFY_ENUM_CASE(ELF, SHT_RELA);
    STRINGIFY_ENUM_CASE(ELF, SHT_HASH);
    STRINGIFY_ENUM_CASE(ELF, SHT_DYNAMIC);
    STRINGIFY_ENUM_CASE(ELF, SHT_NOTE);
    STRINGIFY_ENUM_CASE(ELF, SHT_NOBITS);
    STRINGIFY_ENUM_CASE(ELF, SHT_REL);
    STRINGIFY_ENUM_CASE(ELF, SHT_SHLIB);
    STRINGIFY_ENUM_CASE(ELF, SHT_DYNSYM);
    STRINGIFY_ENUM_CASE(ELF, SHT_INIT_ARRAY);
    STRINGIFY_ENUM_CASE(ELF, SHT_FINI_ARRAY);
    STRINGIFY_ENUM_CASE(ELF, SHT_PREINIT_ARRAY);
    STRINGIFY_ENUM_CASE(ELF, SHT_GROUP);
    STRINGIFY_ENUM_CASE(ELF, SHT_SYMTAB_SHNDX);
    STRINGIFY_ENUM_CASE(ELF, SHT_RELR);
    STRINGIFY_ENUM_CASE(ELF, SHT_ANDROID_REL);
    STRINGIFY_ENUM_CASE(ELF, SHT_ANDROID_RELA);
    STRINGIFY_ENUM_CASE(ELF, SHT_ANDROID_RELR);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_ODRTAB);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_LINKER_OPTIONS);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_CALL_GRAPH_PROFILE);
    STRINGIFY_ENUM_CASE(ELF, SHT_LLVM_ADDRSIG);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_ATTRIBUTES);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_HASH);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_verdef);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_verneed);
    STRINGIFY_ENUM_CASE(ELF, SHT_GNU_versym);
  default:
    return "Unknown";
  }
}

template <class ELFT>
Expected<std::vector<typename ELFT::Rela>>
ELFFile<ELFT>::decode_relrs(Elf_Relr_Range relrs) const {
  // This function decodes the contents of an SHT_RELR packed relocation
  // section.
  //
  // Proposal for adding SHT_RELR sections to generic-abi is here:
  //   https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
  //
  // The encoded sequence of Elf64_Relr entries in a SHT_RELR section looks
  // like [ AAAAAAAA BBBBBBB1 BBBBBBB1 ... AAAAAAAA BBBBBB1 ... ]
  //
  // i.e. start with an address, followed by any number of bitmaps. The address
  // entry encodes 1 relocation. The subsequent bitmap entries encode up to 63
  // relocations each, at subsequent offsets following the last address entry.
  //
  // The bitmap entries must have 1 in the least significant bit. The assumption
  // here is that an address cannot have 1 in lsb. Odd addresses are not
  // supported.
  //
  // Excluding the least significant bit in the bitmap, each non-zero bit in
  // the bitmap represents a relocation to be applied to a corresponding machine
  // word that follows the base address word. The second least significant bit
  // represents the machine word immediately following the initial address, and
  // each bit that follows represents the next word, in linear order. As such,
  // a single bitmap can encode up to 31 relocations in a 32-bit object, and
  // 63 relocations in a 64-bit object.
  //
  // This encoding has a couple of interesting properties:
  // 1. Looking at any entry, it is clear whether it's an address or a bitmap:
  //    even means address, odd means bitmap.
  // 2. Just a simple list of addresses is a valid encoding.

  Elf_Rela Rela;
  Rela.r_info = 0;
  Rela.r_addend = 0;
  Rela.setType(getRelativeRelocationType(), false);
  std::vector<Elf_Rela> Relocs;

  // Word type: uint32_t for Elf32, and uint64_t for Elf64.
  typedef typename ELFT::uint Word;

  // Word size in number of bytes.
  const size_t WordSize = sizeof(Word);

  // Number of bits used for the relocation offsets bitmap.
  // These many relative relocations can be encoded in a single entry.
  const size_t NBits = 8*WordSize - 1;

  Word Base = 0;
  for (const Elf_Relr &R : relrs) {
    Word Entry = R;
    if ((Entry&1) == 0) {
      // Even entry: encodes the offset for next relocation.
      Rela.r_offset = Entry;
      Relocs.push_back(Rela);
      // Set base offset for subsequent bitmap entries.
      Base = Entry + WordSize;
      continue;
    }

    // Odd entry: encodes bitmap for relocations starting at base.
    Word Offset = Base;
    while (Entry != 0) {
      Entry >>= 1;
      if ((Entry&1) != 0) {
        Rela.r_offset = Offset;
        Relocs.push_back(Rela);
      }
      Offset += WordSize;
    }

    // Advance base offset by NBits words.
    Base += NBits * WordSize;
  }

  return Relocs;
}

template <class ELFT>
Expected<std::vector<typename ELFT::Rela>>
ELFFile<ELFT>::android_relas(const Elf_Shdr *Sec) const {
  // This function reads relocations in Android's packed relocation format,
  // which is based on SLEB128 and delta encoding.
  Expected<ArrayRef<uint8_t>> ContentsOrErr = getSectionContents(Sec);
  if (!ContentsOrErr)
    return ContentsOrErr.takeError();
  const uint8_t *Cur = ContentsOrErr->begin();
  const uint8_t *End = ContentsOrErr->end();
  if (ContentsOrErr->size() < 4 || Cur[0] != 'A' || Cur[1] != 'P' ||
      Cur[2] != 'S' || Cur[3] != '2')
    return createError("invalid packed relocation header");
  Cur += 4;

  const char *ErrStr = nullptr;
  auto ReadSLEB = [&]() -> int64_t {
    if (ErrStr)
      return 0;
    unsigned Len;
    int64_t Result = decodeSLEB128(Cur, &Len, End, &ErrStr);
    Cur += Len;
    return Result;
  };

  uint64_t NumRelocs = ReadSLEB();
  uint64_t Offset = ReadSLEB();
  uint64_t Addend = 0;

  if (ErrStr)
    return createError(ErrStr);

  std::vector<Elf_Rela> Relocs;
  Relocs.reserve(NumRelocs);
  while (NumRelocs) {
    uint64_t NumRelocsInGroup = ReadSLEB();
    if (NumRelocsInGroup > NumRelocs)
      return createError("relocation group unexpectedly large");
    NumRelocs -= NumRelocsInGroup;

    uint64_t GroupFlags = ReadSLEB();
    bool GroupedByInfo = GroupFlags & ELF::RELOCATION_GROUPED_BY_INFO_FLAG;
    bool GroupedByOffsetDelta = GroupFlags & ELF::RELOCATION_GROUPED_BY_OFFSET_DELTA_FLAG;
    bool GroupedByAddend = GroupFlags & ELF::RELOCATION_GROUPED_BY_ADDEND_FLAG;
    bool GroupHasAddend = GroupFlags & ELF::RELOCATION_GROUP_HAS_ADDEND_FLAG;

    uint64_t GroupOffsetDelta;
    if (GroupedByOffsetDelta)
      GroupOffsetDelta = ReadSLEB();

    uint64_t GroupRInfo;
    if (GroupedByInfo)
      GroupRInfo = ReadSLEB();

    if (GroupedByAddend && GroupHasAddend)
      Addend += ReadSLEB();

    if (!GroupHasAddend)
      Addend = 0;

    for (uint64_t I = 0; I != NumRelocsInGroup; ++I) {
      Elf_Rela R;
      Offset += GroupedByOffsetDelta ? GroupOffsetDelta : ReadSLEB();
      R.r_offset = Offset;
      R.r_info = GroupedByInfo ? GroupRInfo : ReadSLEB();
      if (GroupHasAddend && !GroupedByAddend)
        Addend += ReadSLEB();
      R.r_addend = Addend;
      Relocs.push_back(R);

      if (ErrStr)
        return createError(ErrStr);
    }

    if (ErrStr)
      return createError(ErrStr);
  }

  return Relocs;
}

template <class ELFT>
const char *ELFFile<ELFT>::getDynamicTagAsString(unsigned Arch,
                                                 uint64_t Type) const {
#define DYNAMIC_STRINGIFY_ENUM(tag, value)                                     \
  case value:                                                                  \
    return #tag;

#define DYNAMIC_TAG(n, v)
  switch (Arch) {
  case ELF::EM_HEXAGON:
    switch (Type) {
#define HEXAGON_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef HEXAGON_DYNAMIC_TAG
    }

  case ELF::EM_MIPS:
    switch (Type) {
#define MIPS_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef MIPS_DYNAMIC_TAG
    }

  case ELF::EM_PPC64:
    switch (Type) {
#define PPC64_DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef PPC64_DYNAMIC_TAG
    }
  }
#undef DYNAMIC_TAG
  switch (Type) {
// Now handle all dynamic tags except the architecture specific ones
#define MIPS_DYNAMIC_TAG(name, value)
#define HEXAGON_DYNAMIC_TAG(name, value)
#define PPC64_DYNAMIC_TAG(name, value)
// Also ignore marker tags such as DT_HIOS (maps to DT_VERNEEDNUM), etc.
#define DYNAMIC_TAG_MARKER(name, value)
#define DYNAMIC_TAG(name, value) DYNAMIC_STRINGIFY_ENUM(name, value)
#include "llvm/BinaryFormat/DynamicTags.def"
#undef DYNAMIC_TAG
#undef MIPS_DYNAMIC_TAG
#undef HEXAGON_DYNAMIC_TAG
#undef PPC64_DYNAMIC_TAG
#undef DYNAMIC_TAG_MARKER
#undef DYNAMIC_STRINGIFY_ENUM
  default:
    return "unknown";
  }
}

template <class ELFT>
const char *ELFFile<ELFT>::getDynamicTagAsString(uint64_t Type) const {
  return getDynamicTagAsString(getHeader()->e_machine, Type);
}

template <class ELFT>
Expected<typename ELFT::DynRange> ELFFile<ELFT>::dynamicEntries() const {
  ArrayRef<Elf_Dyn> Dyn;
  size_t DynSecSize = 0;

  auto ProgramHeadersOrError = program_headers();
  if (!ProgramHeadersOrError)
    return ProgramHeadersOrError.takeError();

  for (const Elf_Phdr &Phdr : *ProgramHeadersOrError) {
    if (Phdr.p_type == ELF::PT_DYNAMIC) {
      Dyn = makeArrayRef(
          reinterpret_cast<const Elf_Dyn *>(base() + Phdr.p_offset),
          Phdr.p_filesz / sizeof(Elf_Dyn));
      DynSecSize = Phdr.p_filesz;
      break;
    }
  }

  // If we can't find the dynamic section in the program headers, we just fall
  // back on the sections.
  if (Dyn.empty()) {
    auto SectionsOrError = sections();
    if (!SectionsOrError)
      return SectionsOrError.takeError();

    for (const Elf_Shdr &Sec : *SectionsOrError) {
      if (Sec.sh_type == ELF::SHT_DYNAMIC) {
        Expected<ArrayRef<Elf_Dyn>> DynOrError =
            getSectionContentsAsArray<Elf_Dyn>(&Sec);
        if (!DynOrError)
          return DynOrError.takeError();
        Dyn = *DynOrError;
        DynSecSize = Sec.sh_size;
        break;
      }
    }

    if (!Dyn.data())
      return ArrayRef<Elf_Dyn>();
  }

  if (Dyn.empty())
    return createError("invalid empty dynamic section");

  if (DynSecSize % sizeof(Elf_Dyn) != 0)
    return createError("malformed dynamic section");

  if (Dyn.back().d_tag != ELF::DT_NULL)
    return createError("dynamic sections must be DT_NULL terminated");

  return Dyn;
}

template <class ELFT>
Expected<const uint8_t *> ELFFile<ELFT>::toMappedAddr(uint64_t VAddr) const {
  auto ProgramHeadersOrError = program_headers();
  if (!ProgramHeadersOrError)
    return ProgramHeadersOrError.takeError();

  llvm::SmallVector<Elf_Phdr *, 4> LoadSegments;

  for (const Elf_Phdr &Phdr : *ProgramHeadersOrError)
    if (Phdr.p_type == ELF::PT_LOAD)
      LoadSegments.push_back(const_cast<Elf_Phdr *>(&Phdr));

  const Elf_Phdr *const *I =
      std::upper_bound(LoadSegments.begin(), LoadSegments.end(), VAddr,
                       [](uint64_t VAddr, const Elf_Phdr_Impl<ELFT> *Phdr) {
                         return VAddr < Phdr->p_vaddr;
                       });

  if (I == LoadSegments.begin())
    return createError("Virtual address is not in any segment");
  --I;
  const Elf_Phdr &Phdr = **I;
  uint64_t Delta = VAddr - Phdr.p_vaddr;
  if (Delta >= Phdr.p_filesz)
    return createError("Virtual address is not in any segment");
  return base() + Phdr.p_offset + Delta;
}

template class llvm::object::ELFFile<ELF32LE>;
template class llvm::object::ELFFile<ELF32BE>;
template class llvm::object::ELFFile<ELF64LE>;
template class llvm::object::ELFFile<ELF64BE>;
