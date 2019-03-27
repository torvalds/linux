//===- OutputSections.cpp -------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "OutputSections.h"
#include "Config.h"
#include "LinkerScript.h"
#include "SymbolTable.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Threads.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SHA1.h"

using namespace llvm;
using namespace llvm::dwarf;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;

using namespace lld;
using namespace lld::elf;

uint8_t Out::First;
PhdrEntry *Out::TlsPhdr;
OutputSection *Out::ElfHeader;
OutputSection *Out::ProgramHeaders;
OutputSection *Out::PreinitArray;
OutputSection *Out::InitArray;
OutputSection *Out::FiniArray;

std::vector<OutputSection *> elf::OutputSections;

uint32_t OutputSection::getPhdrFlags() const {
  uint32_t Ret = 0;
  if (Config->EMachine != EM_ARM || !(Flags & SHF_ARM_PURECODE))
    Ret |= PF_R;
  if (Flags & SHF_WRITE)
    Ret |= PF_W;
  if (Flags & SHF_EXECINSTR)
    Ret |= PF_X;
  return Ret;
}

template <class ELFT>
void OutputSection::writeHeaderTo(typename ELFT::Shdr *Shdr) {
  Shdr->sh_entsize = Entsize;
  Shdr->sh_addralign = Alignment;
  Shdr->sh_type = Type;
  Shdr->sh_offset = Offset;
  Shdr->sh_flags = Flags;
  Shdr->sh_info = Info;
  Shdr->sh_link = Link;
  Shdr->sh_addr = Addr;
  Shdr->sh_size = Size;
  Shdr->sh_name = ShName;
}

OutputSection::OutputSection(StringRef Name, uint32_t Type, uint64_t Flags)
    : BaseCommand(OutputSectionKind),
      SectionBase(Output, Name, Flags, /*Entsize*/ 0, /*Alignment*/ 1, Type,
                  /*Info*/ 0, /*Link*/ 0) {
  Live = false;
}

// We allow sections of types listed below to merged into a
// single progbits section. This is typically done by linker
// scripts. Merging nobits and progbits will force disk space
// to be allocated for nobits sections. Other ones don't require
// any special treatment on top of progbits, so there doesn't
// seem to be a harm in merging them.
static bool canMergeToProgbits(unsigned Type) {
  return Type == SHT_NOBITS || Type == SHT_PROGBITS || Type == SHT_INIT_ARRAY ||
         Type == SHT_PREINIT_ARRAY || Type == SHT_FINI_ARRAY ||
         Type == SHT_NOTE;
}

void OutputSection::addSection(InputSection *IS) {
  if (!Live) {
    // If IS is the first section to be added to this section,
    // initialize Type, Entsize and flags from IS.
    Live = true;
    Type = IS->Type;
    Entsize = IS->Entsize;
    Flags = IS->Flags;
  } else {
    // Otherwise, check if new type or flags are compatible with existing ones.
    unsigned Mask = SHF_TLS | SHF_LINK_ORDER;
    if ((Flags & Mask) != (IS->Flags & Mask))
      error("incompatible section flags for " + Name + "\n>>> " + toString(IS) +
            ": 0x" + utohexstr(IS->Flags) + "\n>>> output section " + Name +
            ": 0x" + utohexstr(Flags));

    if (Type != IS->Type) {
      if (!canMergeToProgbits(Type) || !canMergeToProgbits(IS->Type))
        error("section type mismatch for " + IS->Name + "\n>>> " +
              toString(IS) + ": " +
              getELFSectionTypeName(Config->EMachine, IS->Type) +
              "\n>>> output section " + Name + ": " +
              getELFSectionTypeName(Config->EMachine, Type));
      Type = SHT_PROGBITS;
    }
  }

  IS->Parent = this;
  uint64_t AndMask =
      Config->EMachine == EM_ARM ? (uint64_t)SHF_ARM_PURECODE : 0;
  uint64_t OrMask = ~AndMask;
  uint64_t AndFlags = (Flags & IS->Flags) & AndMask;
  uint64_t OrFlags = (Flags | IS->Flags) & OrMask;
  Flags = AndFlags | OrFlags;

  Alignment = std::max(Alignment, IS->Alignment);

  // If this section contains a table of fixed-size entries, sh_entsize
  // holds the element size. If it contains elements of different size we
  // set sh_entsize to 0.
  if (Entsize != IS->Entsize)
    Entsize = 0;

  if (!IS->Assigned) {
    IS->Assigned = true;
    if (SectionCommands.empty() ||
        !isa<InputSectionDescription>(SectionCommands.back()))
      SectionCommands.push_back(make<InputSectionDescription>(""));
    auto *ISD = cast<InputSectionDescription>(SectionCommands.back());
    ISD->Sections.push_back(IS);
  }
}

static void sortByOrder(MutableArrayRef<InputSection *> In,
                        llvm::function_ref<int(InputSectionBase *S)> Order) {
  typedef std::pair<int, InputSection *> Pair;
  auto Comp = [](const Pair &A, const Pair &B) { return A.first < B.first; };

  std::vector<Pair> V;
  for (InputSection *S : In)
    V.push_back({Order(S), S});
  std::stable_sort(V.begin(), V.end(), Comp);

  for (size_t I = 0; I < V.size(); ++I)
    In[I] = V[I].second;
}

uint64_t elf::getHeaderSize() {
  if (Config->OFormatBinary)
    return 0;
  return Out::ElfHeader->Size + Out::ProgramHeaders->Size;
}

bool OutputSection::classof(const BaseCommand *C) {
  return C->Kind == OutputSectionKind;
}

void OutputSection::sort(llvm::function_ref<int(InputSectionBase *S)> Order) {
  assert(Live);
  for (BaseCommand *B : SectionCommands)
    if (auto *ISD = dyn_cast<InputSectionDescription>(B))
      sortByOrder(ISD->Sections, Order);
}

// Fill [Buf, Buf + Size) with Filler.
// This is used for linker script "=fillexp" command.
static void fill(uint8_t *Buf, size_t Size,
                 const std::array<uint8_t, 4> &Filler) {
  size_t I = 0;
  for (; I + 4 < Size; I += 4)
    memcpy(Buf + I, Filler.data(), 4);
  memcpy(Buf + I, Filler.data(), Size - I);
}

// Compress section contents if this section contains debug info.
template <class ELFT> void OutputSection::maybeCompress() {
  typedef typename ELFT::Chdr Elf_Chdr;

  // Compress only DWARF debug sections.
  if (!Config->CompressDebugSections || (Flags & SHF_ALLOC) ||
      !Name.startswith(".debug_"))
    return;

  // Create a section header.
  ZDebugHeader.resize(sizeof(Elf_Chdr));
  auto *Hdr = reinterpret_cast<Elf_Chdr *>(ZDebugHeader.data());
  Hdr->ch_type = ELFCOMPRESS_ZLIB;
  Hdr->ch_size = Size;
  Hdr->ch_addralign = Alignment;

  // Write section contents to a temporary buffer and compress it.
  std::vector<uint8_t> Buf(Size);
  writeTo<ELFT>(Buf.data());
  if (Error E = zlib::compress(toStringRef(Buf), CompressedData))
    fatal("compress failed: " + llvm::toString(std::move(E)));

  // Update section headers.
  Size = sizeof(Elf_Chdr) + CompressedData.size();
  Flags |= SHF_COMPRESSED;
}

static void writeInt(uint8_t *Buf, uint64_t Data, uint64_t Size) {
  if (Size == 1)
    *Buf = Data;
  else if (Size == 2)
    write16(Buf, Data);
  else if (Size == 4)
    write32(Buf, Data);
  else if (Size == 8)
    write64(Buf, Data);
  else
    llvm_unreachable("unsupported Size argument");
}

template <class ELFT> void OutputSection::writeTo(uint8_t *Buf) {
  if (Type == SHT_NOBITS)
    return;

  Loc = Buf;

  // If -compress-debug-section is specified and if this is a debug seciton,
  // we've already compressed section contents. If that's the case,
  // just write it down.
  if (!CompressedData.empty()) {
    memcpy(Buf, ZDebugHeader.data(), ZDebugHeader.size());
    memcpy(Buf + ZDebugHeader.size(), CompressedData.data(),
           CompressedData.size());
    return;
  }

  // Write leading padding.
  std::vector<InputSection *> Sections = getInputSections(this);
  std::array<uint8_t, 4> Filler = getFiller();
  bool NonZeroFiller = read32(Filler.data()) != 0;
  if (NonZeroFiller)
    fill(Buf, Sections.empty() ? Size : Sections[0]->OutSecOff, Filler);

  parallelForEachN(0, Sections.size(), [&](size_t I) {
    InputSection *IS = Sections[I];
    IS->writeTo<ELFT>(Buf);

    // Fill gaps between sections.
    if (NonZeroFiller) {
      uint8_t *Start = Buf + IS->OutSecOff + IS->getSize();
      uint8_t *End;
      if (I + 1 == Sections.size())
        End = Buf + Size;
      else
        End = Buf + Sections[I + 1]->OutSecOff;
      fill(Start, End - Start, Filler);
    }
  });

  // Linker scripts may have BYTE()-family commands with which you
  // can write arbitrary bytes to the output. Process them if any.
  for (BaseCommand *Base : SectionCommands)
    if (auto *Data = dyn_cast<ByteCommand>(Base))
      writeInt(Buf + Data->Offset, Data->Expression().getValue(), Data->Size);
}

template <class ELFT>
static void finalizeShtGroup(OutputSection *OS,
                             InputSection *Section) {
  assert(Config->Relocatable);

  // sh_link field for SHT_GROUP sections should contain the section index of
  // the symbol table.
  OS->Link = In.SymTab->getParent()->SectionIndex;

  // sh_info then contain index of an entry in symbol table section which
  // provides signature of the section group.
  ObjFile<ELFT> *Obj = Section->getFile<ELFT>();
  ArrayRef<Symbol *> Symbols = Obj->getSymbols();
  OS->Info = In.SymTab->getSymbolIndex(Symbols[Section->Info]);
}

template <class ELFT> void OutputSection::finalize() {
  if (Type == SHT_NOBITS)
    for (BaseCommand *Base : SectionCommands)
      if (isa<ByteCommand>(Base))
        Type = SHT_PROGBITS;

  std::vector<InputSection *> V = getInputSections(this);
  InputSection *First = V.empty() ? nullptr : V[0];

  if (Flags & SHF_LINK_ORDER) {
    // We must preserve the link order dependency of sections with the
    // SHF_LINK_ORDER flag. The dependency is indicated by the sh_link field. We
    // need to translate the InputSection sh_link to the OutputSection sh_link,
    // all InputSections in the OutputSection have the same dependency.
    if (auto *D = First->getLinkOrderDep())
      Link = D->getParent()->SectionIndex;
  }

  if (Type == SHT_GROUP) {
    finalizeShtGroup<ELFT>(this, First);
    return;
  }

  if (!Config->CopyRelocs || (Type != SHT_RELA && Type != SHT_REL))
    return;

  if (isa<SyntheticSection>(First))
    return;

  Link = In.SymTab->getParent()->SectionIndex;
  // sh_info for SHT_REL[A] sections should contain the section header index of
  // the section to which the relocation applies.
  InputSectionBase *S = First->getRelocatedSection();
  Info = S->getOutputSection()->SectionIndex;
  Flags |= SHF_INFO_LINK;
}

// Returns true if S matches /Filename.?\.o$/.
static bool isCrtBeginEnd(StringRef S, StringRef Filename) {
  if (!S.endswith(".o"))
    return false;
  S = S.drop_back(2);
  if (S.endswith(Filename))
    return true;
  return !S.empty() && S.drop_back().endswith(Filename);
}

static bool isCrtbegin(StringRef S) { return isCrtBeginEnd(S, "crtbegin"); }
static bool isCrtend(StringRef S) { return isCrtBeginEnd(S, "crtend"); }

// .ctors and .dtors are sorted by this priority from highest to lowest.
//
//  1. The section was contained in crtbegin (crtbegin contains
//     some sentinel value in its .ctors and .dtors so that the runtime
//     can find the beginning of the sections.)
//
//  2. The section has an optional priority value in the form of ".ctors.N"
//     or ".dtors.N" where N is a number. Unlike .{init,fini}_array,
//     they are compared as string rather than number.
//
//  3. The section is just ".ctors" or ".dtors".
//
//  4. The section was contained in crtend, which contains an end marker.
//
// In an ideal world, we don't need this function because .init_array and
// .ctors are duplicate features (and .init_array is newer.) However, there
// are too many real-world use cases of .ctors, so we had no choice to
// support that with this rather ad-hoc semantics.
static bool compCtors(const InputSection *A, const InputSection *B) {
  bool BeginA = isCrtbegin(A->File->getName());
  bool BeginB = isCrtbegin(B->File->getName());
  if (BeginA != BeginB)
    return BeginA;
  bool EndA = isCrtend(A->File->getName());
  bool EndB = isCrtend(B->File->getName());
  if (EndA != EndB)
    return EndB;
  StringRef X = A->Name;
  StringRef Y = B->Name;
  assert(X.startswith(".ctors") || X.startswith(".dtors"));
  assert(Y.startswith(".ctors") || Y.startswith(".dtors"));
  X = X.substr(6);
  Y = Y.substr(6);
  return X < Y;
}

// Sorts input sections by the special rules for .ctors and .dtors.
// Unfortunately, the rules are different from the one for .{init,fini}_array.
// Read the comment above.
void OutputSection::sortCtorsDtors() {
  assert(SectionCommands.size() == 1);
  auto *ISD = cast<InputSectionDescription>(SectionCommands[0]);
  std::stable_sort(ISD->Sections.begin(), ISD->Sections.end(), compCtors);
}

// If an input string is in the form of "foo.N" where N is a number,
// return N. Otherwise, returns 65536, which is one greater than the
// lowest priority.
int elf::getPriority(StringRef S) {
  size_t Pos = S.rfind('.');
  if (Pos == StringRef::npos)
    return 65536;
  int V;
  if (!to_integer(S.substr(Pos + 1), V, 10))
    return 65536;
  return V;
}

std::vector<InputSection *> elf::getInputSections(OutputSection *OS) {
  std::vector<InputSection *> Ret;
  for (BaseCommand *Base : OS->SectionCommands)
    if (auto *ISD = dyn_cast<InputSectionDescription>(Base))
      Ret.insert(Ret.end(), ISD->Sections.begin(), ISD->Sections.end());
  return Ret;
}

// Sorts input sections by section name suffixes, so that .foo.N comes
// before .foo.M if N < M. Used to sort .{init,fini}_array.N sections.
// We want to keep the original order if the priorities are the same
// because the compiler keeps the original initialization order in a
// translation unit and we need to respect that.
// For more detail, read the section of the GCC's manual about init_priority.
void OutputSection::sortInitFini() {
  // Sort sections by priority.
  sort([](InputSectionBase *S) { return getPriority(S->Name); });
}

std::array<uint8_t, 4> OutputSection::getFiller() {
  if (Filler)
    return *Filler;
  if (Flags & SHF_EXECINSTR)
    return Target->TrapInstr;
  return {0, 0, 0, 0};
}

template void OutputSection::writeHeaderTo<ELF32LE>(ELF32LE::Shdr *Shdr);
template void OutputSection::writeHeaderTo<ELF32BE>(ELF32BE::Shdr *Shdr);
template void OutputSection::writeHeaderTo<ELF64LE>(ELF64LE::Shdr *Shdr);
template void OutputSection::writeHeaderTo<ELF64BE>(ELF64BE::Shdr *Shdr);

template void OutputSection::writeTo<ELF32LE>(uint8_t *Buf);
template void OutputSection::writeTo<ELF32BE>(uint8_t *Buf);
template void OutputSection::writeTo<ELF64LE>(uint8_t *Buf);
template void OutputSection::writeTo<ELF64BE>(uint8_t *Buf);

template void OutputSection::maybeCompress<ELF32LE>();
template void OutputSection::maybeCompress<ELF32BE>();
template void OutputSection::maybeCompress<ELF64LE>();
template void OutputSection::maybeCompress<ELF64BE>();

template void OutputSection::finalize<ELF32LE>();
template void OutputSection::finalize<ELF32BE>();
template void OutputSection::finalize<ELF64LE>();
template void OutputSection::finalize<ELF64BE>();
