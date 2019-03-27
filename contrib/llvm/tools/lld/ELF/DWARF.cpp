//===- DWARF.cpp ----------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The -gdb-index option instructs the linker to emit a .gdb_index section.
// The section contains information to make gdb startup faster.
// The format of the section is described at
// https://sourceware.org/gdb/onlinedocs/gdb/Index-Section-Format.html.
//
//===----------------------------------------------------------------------===//

#include "DWARF.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/Memory.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/Object/ELFObjectFile.h"

using namespace llvm;
using namespace llvm::object;
using namespace lld;
using namespace lld::elf;

template <class ELFT> LLDDwarfObj<ELFT>::LLDDwarfObj(ObjFile<ELFT> *Obj) {
  for (InputSectionBase *Sec : Obj->getSections()) {
    if (!Sec)
      continue;

    if (LLDDWARFSection *M =
            StringSwitch<LLDDWARFSection *>(Sec->Name)
                .Case(".debug_addr", &AddrSection)
                .Case(".debug_gnu_pubnames", &GnuPubNamesSection)
                .Case(".debug_gnu_pubtypes", &GnuPubTypesSection)
                .Case(".debug_info", &InfoSection)
                .Case(".debug_ranges", &RangeSection)
                .Case(".debug_rnglists", &RngListsSection)
                .Case(".debug_line", &LineSection)
                .Default(nullptr)) {
      M->Data = toStringRef(Sec->data());
      M->Sec = Sec;
      continue;
    }

    if (Sec->Name == ".debug_abbrev")
      AbbrevSection = toStringRef(Sec->data());
    else if (Sec->Name == ".debug_str")
      StrSection = toStringRef(Sec->data());
    else if (Sec->Name == ".debug_line_str")
      LineStringSection = toStringRef(Sec->data());
  }
}

// Find if there is a relocation at Pos in Sec.  The code is a bit
// more complicated than usual because we need to pass a section index
// to llvm since it has no idea about InputSection.
template <class ELFT>
template <class RelTy>
Optional<RelocAddrEntry>
LLDDwarfObj<ELFT>::findAux(const InputSectionBase &Sec, uint64_t Pos,
                           ArrayRef<RelTy> Rels) const {
  auto It = std::lower_bound(
      Rels.begin(), Rels.end(), Pos,
      [](const RelTy &A, uint64_t B) { return A.r_offset < B; });
  if (It == Rels.end() || It->r_offset != Pos)
    return None;
  const RelTy &Rel = *It;

  const ObjFile<ELFT> *File = Sec.getFile<ELFT>();
  uint32_t SymIndex = Rel.getSymbol(Config->IsMips64EL);
  const typename ELFT::Sym &Sym = File->getELFSyms()[SymIndex];
  uint32_t SecIndex = File->getSectionIndex(Sym);

  // Broken debug info can point to a non-Defined symbol.
  auto *DR = dyn_cast<Defined>(&File->getRelocTargetSym(Rel));
  if (!DR) {
    RelType Type = Rel.getType(Config->IsMips64EL);
    if (Type != Target->NoneRel)
      error(toString(File) + ": relocation " + lld::toString(Type) + " at 0x" +
            llvm::utohexstr(Rel.r_offset) + " has unsupported target");
    return None;
  }
  uint64_t Val = DR->Value + getAddend<ELFT>(Rel);

  // FIXME: We should be consistent about always adding the file
  // offset or not.
  if (DR->Section->Flags & ELF::SHF_ALLOC)
    Val += cast<InputSection>(DR->Section)->getOffsetInFile();

  return RelocAddrEntry{SecIndex, Val};
}

template <class ELFT>
Optional<RelocAddrEntry> LLDDwarfObj<ELFT>::find(const llvm::DWARFSection &S,
                                                 uint64_t Pos) const {
  auto &Sec = static_cast<const LLDDWARFSection &>(S);
  if (Sec.Sec->AreRelocsRela)
    return findAux(*Sec.Sec, Pos, Sec.Sec->template relas<ELFT>());
  return findAux(*Sec.Sec, Pos, Sec.Sec->template rels<ELFT>());
}

template class elf::LLDDwarfObj<ELF32LE>;
template class elf::LLDDwarfObj<ELF32BE>;
template class elf::LLDDwarfObj<ELF64LE>;
template class elf::LLDDwarfObj<ELF64BE>;
