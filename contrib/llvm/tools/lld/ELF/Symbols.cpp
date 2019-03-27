//===- Symbols.cpp --------------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "OutputSections.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Writer.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Path.h"
#include <cstring>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;

using namespace lld;
using namespace lld::elf;

Defined *ElfSym::Bss;
Defined *ElfSym::Etext1;
Defined *ElfSym::Etext2;
Defined *ElfSym::Edata1;
Defined *ElfSym::Edata2;
Defined *ElfSym::End1;
Defined *ElfSym::End2;
Defined *ElfSym::GlobalOffsetTable;
Defined *ElfSym::MipsGp;
Defined *ElfSym::MipsGpDisp;
Defined *ElfSym::MipsLocalGp;
Defined *ElfSym::RelaIpltStart;
Defined *ElfSym::RelaIpltEnd;

static uint64_t getSymVA(const Symbol &Sym, int64_t &Addend) {
  switch (Sym.kind()) {
  case Symbol::DefinedKind: {
    auto &D = cast<Defined>(Sym);
    SectionBase *IS = D.Section;

    // According to the ELF spec reference to a local symbol from outside
    // the group are not allowed. Unfortunately .eh_frame breaks that rule
    // and must be treated specially. For now we just replace the symbol with
    // 0.
    if (IS == &InputSection::Discarded)
      return 0;

    // This is an absolute symbol.
    if (!IS)
      return D.Value;

    IS = IS->Repl;

    uint64_t Offset = D.Value;

    // An object in an SHF_MERGE section might be referenced via a
    // section symbol (as a hack for reducing the number of local
    // symbols).
    // Depending on the addend, the reference via a section symbol
    // refers to a different object in the merge section.
    // Since the objects in the merge section are not necessarily
    // contiguous in the output, the addend can thus affect the final
    // VA in a non-linear way.
    // To make this work, we incorporate the addend into the section
    // offset (and zero out the addend for later processing) so that
    // we find the right object in the section.
    if (D.isSection()) {
      Offset += Addend;
      Addend = 0;
    }

    // In the typical case, this is actually very simple and boils
    // down to adding together 3 numbers:
    // 1. The address of the output section.
    // 2. The offset of the input section within the output section.
    // 3. The offset within the input section (this addition happens
    //    inside InputSection::getOffset).
    //
    // If you understand the data structures involved with this next
    // line (and how they get built), then you have a pretty good
    // understanding of the linker.
    uint64_t VA = IS->getVA(Offset);

    if (D.isTls() && !Config->Relocatable) {
      // Use the address of the TLS segment's first section rather than the
      // segment's address, because segment addresses aren't initialized until
      // after sections are finalized. (e.g. Measuring the size of .rela.dyn
      // for Android relocation packing requires knowing TLS symbol addresses
      // during section finalization.)
      if (!Out::TlsPhdr || !Out::TlsPhdr->FirstSec)
        fatal(toString(D.File) +
              " has an STT_TLS symbol but doesn't have an SHF_TLS section");
      return VA - Out::TlsPhdr->FirstSec->Addr;
    }
    return VA;
  }
  case Symbol::SharedKind:
  case Symbol::UndefinedKind:
    return 0;
  case Symbol::LazyArchiveKind:
  case Symbol::LazyObjectKind:
    assert(Sym.IsUsedInRegularObj && "lazy symbol reached writer");
    return 0;
  case Symbol::PlaceholderKind:
    llvm_unreachable("placeholder symbol reached writer");
  }
  llvm_unreachable("invalid symbol kind");
}

uint64_t Symbol::getVA(int64_t Addend) const {
  uint64_t OutVA = getSymVA(*this, Addend);
  return OutVA + Addend;
}

uint64_t Symbol::getGotVA() const { return In.Got->getVA() + getGotOffset(); }

uint64_t Symbol::getGotOffset() const {
  return GotIndex * Target->GotEntrySize;
}

uint64_t Symbol::getGotPltVA() const {
  if (this->IsInIgot)
    return In.IgotPlt->getVA() + getGotPltOffset();
  return In.GotPlt->getVA() + getGotPltOffset();
}

uint64_t Symbol::getGotPltOffset() const {
  if (IsInIgot)
    return PltIndex * Target->GotPltEntrySize;
  return (PltIndex + Target->GotPltHeaderEntriesNum) * Target->GotPltEntrySize;
}

uint64_t Symbol::getPPC64LongBranchOffset() const {
  assert(PPC64BranchltIndex != 0xffff);
  return PPC64BranchltIndex * Target->GotPltEntrySize;
}

uint64_t Symbol::getPltVA() const {
  PltSection *Plt = IsInIplt ? In.Iplt : In.Plt;
  return Plt->getVA() + Plt->HeaderSize + PltIndex * Target->PltEntrySize;
}

uint64_t Symbol::getPPC64LongBranchTableVA() const {
  assert(PPC64BranchltIndex != 0xffff);
  return In.PPC64LongBranchTarget->getVA() +
         PPC64BranchltIndex * Target->GotPltEntrySize;
}

uint64_t Symbol::getSize() const {
  if (const auto *DR = dyn_cast<Defined>(this))
    return DR->Size;
  return cast<SharedSymbol>(this)->Size;
}

OutputSection *Symbol::getOutputSection() const {
  if (auto *S = dyn_cast<Defined>(this)) {
    if (auto *Sec = S->Section)
      return Sec->Repl->getOutputSection();
    return nullptr;
  }
  return nullptr;
}

// If a symbol name contains '@', the characters after that is
// a symbol version name. This function parses that.
void Symbol::parseSymbolVersion() {
  StringRef S = getName();
  size_t Pos = S.find('@');
  if (Pos == 0 || Pos == StringRef::npos)
    return;
  StringRef Verstr = S.substr(Pos + 1);
  if (Verstr.empty())
    return;

  // Truncate the symbol name so that it doesn't include the version string.
  NameSize = Pos;

  // If this is not in this DSO, it is not a definition.
  if (!isDefined())
    return;

  // '@@' in a symbol name means the default version.
  // It is usually the most recent one.
  bool IsDefault = (Verstr[0] == '@');
  if (IsDefault)
    Verstr = Verstr.substr(1);

  for (VersionDefinition &Ver : Config->VersionDefinitions) {
    if (Ver.Name != Verstr)
      continue;

    if (IsDefault)
      VersionId = Ver.Id;
    else
      VersionId = Ver.Id | VERSYM_HIDDEN;
    return;
  }

  // It is an error if the specified version is not defined.
  // Usually version script is not provided when linking executable,
  // but we may still want to override a versioned symbol from DSO,
  // so we do not report error in this case. We also do not error
  // if the symbol has a local version as it won't be in the dynamic
  // symbol table.
  if (Config->Shared && VersionId != VER_NDX_LOCAL)
    error(toString(File) + ": symbol " + S + " has undefined version " +
          Verstr);
}

InputFile *LazyArchive::fetch() { return cast<ArchiveFile>(File)->fetch(Sym); }

MemoryBufferRef LazyArchive::getMemberBuffer() {
  Archive::Child C = CHECK(
      Sym.getMember(), "could not get the member for symbol " + Sym.getName());

  return CHECK(C.getMemoryBufferRef(),
               "could not get the buffer for the member defining symbol " +
                   Sym.getName());
}

uint8_t Symbol::computeBinding() const {
  if (Config->Relocatable)
    return Binding;
  if (Visibility != STV_DEFAULT && Visibility != STV_PROTECTED)
    return STB_LOCAL;
  if (VersionId == VER_NDX_LOCAL && isDefined() && !IsPreemptible)
    return STB_LOCAL;
  if (!Config->GnuUnique && Binding == STB_GNU_UNIQUE)
    return STB_GLOBAL;
  return Binding;
}

bool Symbol::includeInDynsym() const {
  if (!Config->HasDynSymTab)
    return false;
  if (computeBinding() == STB_LOCAL)
    return false;
  if (!isDefined())
    return true;
  return ExportDynamic;
}

// Print out a log message for --trace-symbol.
void elf::printTraceSymbol(Symbol *Sym) {
  std::string S;
  if (Sym->isUndefined())
    S = ": reference to ";
  else if (Sym->isLazy())
    S = ": lazy definition of ";
  else if (Sym->isShared())
    S = ": shared definition of ";
  else if (dyn_cast_or_null<BssSection>(cast<Defined>(Sym)->Section))
    S = ": common definition of ";
  else
    S = ": definition of ";

  message(toString(Sym->File) + S + Sym->getName());
}

void elf::maybeWarnUnorderableSymbol(const Symbol *Sym) {
  if (!Config->WarnSymbolOrdering)
    return;

  // If UnresolvedPolicy::Ignore is used, no "undefined symbol" error/warning
  // is emitted. It makes sense to not warn on undefined symbols.
  //
  // Note, ld.bfd --symbol-ordering-file= does not warn on undefined symbols,
  // but we don't have to be compatible here.
  if (Sym->isUndefined() &&
      Config->UnresolvedSymbols == UnresolvedPolicy::Ignore)
    return;

  const InputFile *File = Sym->File;
  auto *D = dyn_cast<Defined>(Sym);

  auto Warn = [&](StringRef S) { warn(toString(File) + S + Sym->getName()); };

  if (Sym->isUndefined())
    Warn(": unable to order undefined symbol: ");
  else if (Sym->isShared())
    Warn(": unable to order shared symbol: ");
  else if (D && !D->Section)
    Warn(": unable to order absolute symbol: ");
  else if (D && isa<OutputSection>(D->Section))
    Warn(": unable to order synthetic symbol: ");
  else if (D && !D->Section->Repl->Live)
    Warn(": unable to order discarded symbol: ");
}

// Returns a symbol for an error message.
std::string lld::toString(const Symbol &B) {
  if (Config->Demangle)
    if (Optional<std::string> S = demangleItanium(B.getName()))
      return *S;
  return B.getName();
}
