//===-- ELFDump.cpp - ELF-specific dumper -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the ELF-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "llvm-objdump.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::object;

template <class ELFT>
Expected<StringRef> getDynamicStrTab(const ELFFile<ELFT> *Elf) {
  typedef ELFFile<ELFT> ELFO;

  auto DynamicEntriesOrError = Elf->dynamicEntries();
  if (!DynamicEntriesOrError)
    return DynamicEntriesOrError.takeError();

  for (const typename ELFO::Elf_Dyn &Dyn : *DynamicEntriesOrError) {
    if (Dyn.d_tag == ELF::DT_STRTAB) {
      auto MappedAddrOrError = Elf->toMappedAddr(Dyn.getPtr());
      if (!MappedAddrOrError)
        consumeError(MappedAddrOrError.takeError());
      return StringRef(reinterpret_cast<const char *>(*MappedAddrOrError));
    }
  }

  // If the dynamic segment is not present, we fall back on the sections.
  auto SectionsOrError = Elf->sections();
  if (!SectionsOrError)
    return SectionsOrError.takeError();

  for (const typename ELFO::Elf_Shdr &Sec : *SectionsOrError) {
    if (Sec.sh_type == ELF::SHT_DYNSYM)
      return Elf->getStringTableForSymtab(Sec);
  }

  return createError("dynamic string table not found");
}

template <class ELFT>
void printDynamicSection(const ELFFile<ELFT> *Elf, StringRef Filename) {
  auto ProgramHeaderOrError = Elf->program_headers();
  if (!ProgramHeaderOrError)
    report_error(Filename, ProgramHeaderOrError.takeError());

  auto DynamicEntriesOrError = Elf->dynamicEntries();
  if (!DynamicEntriesOrError)
    report_error(Filename, DynamicEntriesOrError.takeError());

  outs() << "Dynamic Section:\n";
  for (const auto &Dyn : *DynamicEntriesOrError) {
    if (Dyn.d_tag == ELF::DT_NULL)
      continue;

    StringRef Str = StringRef(Elf->getDynamicTagAsString(Dyn.d_tag));

    if (Str.empty()) {
      std::string HexStr = utohexstr(static_cast<uint64_t>(Dyn.d_tag), true);
      outs() << format("  0x%-19s", HexStr.c_str());
    } else {
      // We use "-21" in order to match GNU objdump's output.
      outs() << format("  %-21s", Str.data());
    }

    const char *Fmt =
        ELFT::Is64Bits ? "0x%016" PRIx64 "\n" : "0x%08" PRIx64 "\n";
    if (Dyn.d_tag == ELF::DT_NEEDED) {
      Expected<StringRef> StrTabOrErr = getDynamicStrTab(Elf);
      if (StrTabOrErr) {
        const char *Data = StrTabOrErr.get().data();
        outs() << (Data + Dyn.d_un.d_val) << "\n";
        continue;
      }
      warn(errorToErrorCode(StrTabOrErr.takeError()).message());
      consumeError(StrTabOrErr.takeError());
    }
    outs() << format(Fmt, (uint64_t)Dyn.d_un.d_val);
  }
}

template <class ELFT> void printProgramHeaders(const ELFFile<ELFT> *o) {
  typedef ELFFile<ELFT> ELFO;
  outs() << "Program Header:\n";
  auto ProgramHeaderOrError = o->program_headers();
  if (!ProgramHeaderOrError)
    report_fatal_error(
        errorToErrorCode(ProgramHeaderOrError.takeError()).message());
  for (const typename ELFO::Elf_Phdr &Phdr : *ProgramHeaderOrError) {
    switch (Phdr.p_type) {
    case ELF::PT_DYNAMIC:
      outs() << " DYNAMIC ";
      break;
    case ELF::PT_GNU_EH_FRAME:
      outs() << "EH_FRAME ";
      break;
    case ELF::PT_GNU_RELRO:
      outs() << "   RELRO ";
      break;
    case ELF::PT_GNU_STACK:
      outs() << "   STACK ";
      break;
    case ELF::PT_INTERP:
      outs() << "  INTERP ";
      break;
    case ELF::PT_LOAD:
      outs() << "    LOAD ";
      break;
    case ELF::PT_NOTE:
      outs() << "    NOTE ";
      break;
    case ELF::PT_OPENBSD_BOOTDATA:
      outs() << "    OPENBSD_BOOTDATA ";
      break;
    case ELF::PT_OPENBSD_RANDOMIZE:
      outs() << "    OPENBSD_RANDOMIZE ";
      break;
    case ELF::PT_OPENBSD_WXNEEDED:
      outs() << "    OPENBSD_WXNEEDED ";
      break;
    case ELF::PT_PHDR:
      outs() << "    PHDR ";
      break;
    case ELF::PT_TLS:
      outs() << "    TLS ";
      break;
    default:
      outs() << " UNKNOWN ";
    }

    const char *Fmt = ELFT::Is64Bits ? "0x%016" PRIx64 " " : "0x%08" PRIx64 " ";

    outs() << "off    " << format(Fmt, (uint64_t)Phdr.p_offset) << "vaddr "
           << format(Fmt, (uint64_t)Phdr.p_vaddr) << "paddr "
           << format(Fmt, (uint64_t)Phdr.p_paddr)
           << format("align 2**%u\n",
                     countTrailingZeros<uint64_t>(Phdr.p_align))
           << "         filesz " << format(Fmt, (uint64_t)Phdr.p_filesz)
           << "memsz " << format(Fmt, (uint64_t)Phdr.p_memsz) << "flags "
           << ((Phdr.p_flags & ELF::PF_R) ? "r" : "-")
           << ((Phdr.p_flags & ELF::PF_W) ? "w" : "-")
           << ((Phdr.p_flags & ELF::PF_X) ? "x" : "-") << "\n";
  }
  outs() << "\n";
}

void llvm::printELFFileHeader(const object::ObjectFile *Obj) {
  if (const auto *ELFObj = dyn_cast<ELF32LEObjectFile>(Obj))
    printProgramHeaders(ELFObj->getELFFile());
  else if (const auto *ELFObj = dyn_cast<ELF32BEObjectFile>(Obj))
    printProgramHeaders(ELFObj->getELFFile());
  else if (const auto *ELFObj = dyn_cast<ELF64LEObjectFile>(Obj))
    printProgramHeaders(ELFObj->getELFFile());
  else if (const auto *ELFObj = dyn_cast<ELF64BEObjectFile>(Obj))
    printProgramHeaders(ELFObj->getELFFile());
}

void llvm::printELFDynamicSection(const object::ObjectFile *Obj) {
  if (const auto *ELFObj = dyn_cast<ELF32LEObjectFile>(Obj))
    printDynamicSection(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *ELFObj = dyn_cast<ELF32BEObjectFile>(Obj))
    printDynamicSection(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *ELFObj = dyn_cast<ELF64LEObjectFile>(Obj))
    printDynamicSection(ELFObj->getELFFile(), Obj->getFileName());
  else if (const auto *ELFObj = dyn_cast<ELF64BEObjectFile>(Obj))
    printDynamicSection(ELFObj->getELFFile(), Obj->getFileName());
}
