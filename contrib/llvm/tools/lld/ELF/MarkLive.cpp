//===- MarkLive.cpp -------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements --gc-sections, which is a feature to remove unused
// sections from output. Unused sections are sections that are not reachable
// from known GC-root symbols or sections. Naturally the feature is
// implemented as a mark-sweep garbage collector.
//
// Here's how it works. Each InputSectionBase has a "Live" bit. The bit is off
// by default. Starting with GC-root symbols or sections, markLive function
// defined in this file visits all reachable sections to set their Live
// bits. Writer will then ignore sections whose Live bits are off, so that
// such sections are not included into output.
//
//===----------------------------------------------------------------------===//

#include "MarkLive.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Object/ELF.h"
#include <functional>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support::endian;

using namespace lld;
using namespace lld::elf;

template <class ELFT>
static typename ELFT::uint getAddend(InputSectionBase &Sec,
                                     const typename ELFT::Rel &Rel) {
  return Target->getImplicitAddend(Sec.data().begin() + Rel.r_offset,
                                   Rel.getType(Config->IsMips64EL));
}

template <class ELFT>
static typename ELFT::uint getAddend(InputSectionBase &Sec,
                                     const typename ELFT::Rela &Rel) {
  return Rel.r_addend;
}

// There are normally few input sections whose names are valid C
// identifiers, so we just store a std::vector instead of a multimap.
static DenseMap<StringRef, std::vector<InputSectionBase *>> CNamedSections;

template <class ELFT, class RelT>
static void
resolveReloc(InputSectionBase &Sec, RelT &Rel,
             llvm::function_ref<void(InputSectionBase *, uint64_t)> Fn) {
  Symbol &B = Sec.getFile<ELFT>()->getRelocTargetSym(Rel);

  // If a symbol is referenced in a live section, it is used.
  B.Used = true;
  if (auto *SS = dyn_cast<SharedSymbol>(&B))
    if (!SS->isWeak())
      SS->getFile<ELFT>().IsNeeded = true;

  if (auto *D = dyn_cast<Defined>(&B)) {
    auto *RelSec = dyn_cast_or_null<InputSectionBase>(D->Section);
    if (!RelSec)
      return;
    uint64_t Offset = D->Value;
    if (D->isSection())
      Offset += getAddend<ELFT>(Sec, Rel);
    Fn(RelSec, Offset);
    return;
  }

  if (!B.isDefined())
    for (InputSectionBase *Sec : CNamedSections.lookup(B.getName()))
      Fn(Sec, 0);
}

// Calls Fn for each section that Sec refers to via relocations.
template <class ELFT>
static void
forEachSuccessor(InputSection &Sec,
                 llvm::function_ref<void(InputSectionBase *, uint64_t)> Fn) {
  if (Sec.AreRelocsRela) {
    for (const typename ELFT::Rela &Rel : Sec.template relas<ELFT>())
      resolveReloc<ELFT>(Sec, Rel, Fn);
  } else {
    for (const typename ELFT::Rel &Rel : Sec.template rels<ELFT>())
      resolveReloc<ELFT>(Sec, Rel, Fn);
  }

  for (InputSectionBase *IS : Sec.DependentSections)
    Fn(IS, 0);
}

// The .eh_frame section is an unfortunate special case.
// The section is divided in CIEs and FDEs and the relocations it can have are
// * CIEs can refer to a personality function.
// * FDEs can refer to a LSDA
// * FDEs refer to the function they contain information about
// The last kind of relocation cannot keep the referred section alive, or they
// would keep everything alive in a common object file. In fact, each FDE is
// alive if the section it refers to is alive.
// To keep things simple, in here we just ignore the last relocation kind. The
// other two keep the referred section alive.
//
// A possible improvement would be to fully process .eh_frame in the middle of
// the gc pass. With that we would be able to also gc some sections holding
// LSDAs and personality functions if we found that they were unused.
template <class ELFT, class RelTy>
static void
scanEhFrameSection(EhInputSection &EH, ArrayRef<RelTy> Rels,
                   llvm::function_ref<void(InputSectionBase *, uint64_t)> Fn) {
  const endianness E = ELFT::TargetEndianness;

  for (unsigned I = 0, N = EH.Pieces.size(); I < N; ++I) {
    EhSectionPiece &Piece = EH.Pieces[I];
    unsigned FirstRelI = Piece.FirstRelocation;
    if (FirstRelI == (unsigned)-1)
      continue;
    if (read32<E>(Piece.data().data() + 4) == 0) {
      // This is a CIE, we only need to worry about the first relocation. It is
      // known to point to the personality function.
      resolveReloc<ELFT>(EH, Rels[FirstRelI], Fn);
      continue;
    }
    // This is a FDE. The relocations point to the described function or to
    // a LSDA. We only need to keep the LSDA alive, so ignore anything that
    // points to executable sections.
    typename ELFT::uint PieceEnd = Piece.InputOff + Piece.Size;
    for (unsigned I2 = FirstRelI, N2 = Rels.size(); I2 < N2; ++I2) {
      const RelTy &Rel = Rels[I2];
      if (Rel.r_offset >= PieceEnd)
        break;
      resolveReloc<ELFT>(EH, Rels[I2],
                         [&](InputSectionBase *Sec, uint64_t Offset) {
                           if (Sec && Sec != &InputSection::Discarded &&
                               !(Sec->Flags & SHF_EXECINSTR))
                             Fn(Sec, 0);
                         });
    }
  }
}

template <class ELFT>
static void
scanEhFrameSection(EhInputSection &EH,
                   llvm::function_ref<void(InputSectionBase *, uint64_t)> Fn) {
  if (!EH.NumRelocations)
    return;

  if (EH.AreRelocsRela)
    scanEhFrameSection<ELFT>(EH, EH.template relas<ELFT>(), Fn);
  else
    scanEhFrameSection<ELFT>(EH, EH.template rels<ELFT>(), Fn);
}

// Some sections are used directly by the loader, so they should never be
// garbage-collected. This function returns true if a given section is such
// section.
template <class ELFT> static bool isReserved(InputSectionBase *Sec) {
  switch (Sec->Type) {
  case SHT_FINI_ARRAY:
  case SHT_INIT_ARRAY:
  case SHT_NOTE:
  case SHT_PREINIT_ARRAY:
    return true;
  default:
    StringRef S = Sec->Name;
    return S.startswith(".ctors") || S.startswith(".dtors") ||
           S.startswith(".init") || S.startswith(".fini") ||
           S.startswith(".jcr");
  }
}

// This is the main function of the garbage collector.
// Starting from GC-root sections, this function visits all reachable
// sections to set their "Live" bits.
template <class ELFT> static void doGcSections() {
  SmallVector<InputSection *, 256> Q;
  CNamedSections.clear();

  auto Enqueue = [&](InputSectionBase *Sec, uint64_t Offset) {
    // Skip over discarded sections. This in theory shouldn't happen, because
    // the ELF spec doesn't allow a relocation to point to a deduplicated
    // COMDAT section directly. Unfortunately this happens in practice (e.g.
    // .eh_frame) so we need to add a check.
    if (Sec == &InputSection::Discarded)
      return;


    // Usually, a whole section is marked as live or dead, but in mergeable
    // (splittable) sections, each piece of data has independent liveness bit.
    // So we explicitly tell it which offset is in use.
    if (auto *MS = dyn_cast<MergeInputSection>(Sec))
      MS->getSectionPiece(Offset)->Live = true;

    if (Sec->Live)
      return;
    Sec->Live = true;

    // Add input section to the queue.
    if (InputSection *S = dyn_cast<InputSection>(Sec))
      Q.push_back(S);
  };

  auto MarkSymbol = [&](Symbol *Sym) {
    if (auto *D = dyn_cast_or_null<Defined>(Sym))
      if (auto *IS = dyn_cast_or_null<InputSectionBase>(D->Section))
        Enqueue(IS, D->Value);
  };

  // Add GC root symbols.
  MarkSymbol(Symtab->find(Config->Entry));
  MarkSymbol(Symtab->find(Config->Init));
  MarkSymbol(Symtab->find(Config->Fini));
  for (StringRef S : Config->Undefined)
    MarkSymbol(Symtab->find(S));
  for (StringRef S : Script->ReferencedSymbols)
    MarkSymbol(Symtab->find(S));

  // Preserve externally-visible symbols if the symbols defined by this
  // file can interrupt other ELF file's symbols at runtime.
  for (Symbol *S : Symtab->getSymbols())
    if (S->includeInDynsym())
      MarkSymbol(S);

  // Preserve special sections and those which are specified in linker
  // script KEEP command.
  for (InputSectionBase *Sec : InputSections) {
    // Mark .eh_frame sections as live because there are usually no relocations
    // that point to .eh_frames. Otherwise, the garbage collector would drop
    // all of them. We also want to preserve personality routines and LSDA
    // referenced by .eh_frame sections, so we scan them for that here.
    if (auto *EH = dyn_cast<EhInputSection>(Sec)) {
      EH->Live = true;
      scanEhFrameSection<ELFT>(*EH, Enqueue);
    }

    if (Sec->Flags & SHF_LINK_ORDER)
      continue;

    if (isReserved<ELFT>(Sec) || Script->shouldKeep(Sec)) {
      Enqueue(Sec, 0);
    } else if (isValidCIdentifier(Sec->Name)) {
      CNamedSections[Saver.save("__start_" + Sec->Name)].push_back(Sec);
      CNamedSections[Saver.save("__stop_" + Sec->Name)].push_back(Sec);
    }
  }

  // Mark all reachable sections.
  while (!Q.empty())
    forEachSuccessor<ELFT>(*Q.pop_back_val(), Enqueue);
}

// Before calling this function, Live bits are off for all
// input sections. This function make some or all of them on
// so that they are emitted to the output file.
template <class ELFT> void elf::markLive() {
  if (!Config->GcSections) {
    // If -gc-sections is missing, no sections are removed.
    for (InputSectionBase *Sec : InputSections)
      Sec->Live = true;

    // If a DSO defines a symbol referenced in a regular object, it is needed.
    for (Symbol *Sym : Symtab->getSymbols())
      if (auto *S = dyn_cast<SharedSymbol>(Sym))
        if (S->IsUsedInRegularObj && !S->isWeak())
          S->getFile<ELFT>().IsNeeded = true;
    return;
  }

  // The -gc-sections option works only for SHF_ALLOC sections
  // (sections that are memory-mapped at runtime). So we can
  // unconditionally make non-SHF_ALLOC sections alive except
  // SHF_LINK_ORDER and SHT_REL/SHT_RELA sections.
  //
  // Usually, SHF_ALLOC sections are not removed even if they are
  // unreachable through relocations because reachability is not
  // a good signal whether they are garbage or not (e.g. there is
  // usually no section referring to a .comment section, but we
  // want to keep it.).
  //
  // Note on SHF_LINK_ORDER: Such sections contain metadata and they
  // have a reverse dependency on the InputSection they are linked with.
  // We are able to garbage collect them.
  //
  // Note on SHF_REL{,A}: Such sections reach here only when -r
  // or -emit-reloc were given. And they are subject of garbage
  // collection because, if we remove a text section, we also
  // remove its relocation section.
  for (InputSectionBase *Sec : InputSections) {
    bool IsAlloc = (Sec->Flags & SHF_ALLOC);
    bool IsLinkOrder = (Sec->Flags & SHF_LINK_ORDER);
    bool IsRel = (Sec->Type == SHT_REL || Sec->Type == SHT_RELA);
    if (!IsAlloc && !IsLinkOrder && !IsRel)
      Sec->Live = true;
  }

  // Follow the graph to mark all live sections.
  doGcSections<ELFT>();

  // Report garbage-collected sections.
  if (Config->PrintGcSections)
    for (InputSectionBase *Sec : InputSections)
      if (!Sec->Live)
        message("removing unused section " + toString(Sec));
}

template void elf::markLive<ELF32LE>();
template void elf::markLive<ELF32BE>();
template void elf::markLive<ELF64LE>();
template void elf::markLive<ELF64BE>();
