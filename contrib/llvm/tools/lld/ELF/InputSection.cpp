//===- InputSection.cpp ---------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InputSection.h"
#include "Config.h"
#include "EhFrame.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "Relocations.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Thunks.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <mutex>
#include <set>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;
using namespace llvm::support::endian;
using namespace llvm::sys;

using namespace lld;
using namespace lld::elf;

std::vector<InputSectionBase *> elf::InputSections;

// Returns a string to construct an error message.
std::string lld::toString(const InputSectionBase *Sec) {
  return (toString(Sec->File) + ":(" + Sec->Name + ")").str();
}

template <class ELFT>
static ArrayRef<uint8_t> getSectionContents(ObjFile<ELFT> &File,
                                            const typename ELFT::Shdr &Hdr) {
  if (Hdr.sh_type == SHT_NOBITS)
    return makeArrayRef<uint8_t>(nullptr, Hdr.sh_size);
  return check(File.getObj().getSectionContents(&Hdr));
}

InputSectionBase::InputSectionBase(InputFile *File, uint64_t Flags,
                                   uint32_t Type, uint64_t Entsize,
                                   uint32_t Link, uint32_t Info,
                                   uint32_t Alignment, ArrayRef<uint8_t> Data,
                                   StringRef Name, Kind SectionKind)
    : SectionBase(SectionKind, Name, Flags, Entsize, Alignment, Type, Info,
                  Link),
      File(File), RawData(Data) {
  // In order to reduce memory allocation, we assume that mergeable
  // sections are smaller than 4 GiB, which is not an unreasonable
  // assumption as of 2017.
  if (SectionKind == SectionBase::Merge && RawData.size() > UINT32_MAX)
    error(toString(this) + ": section too large");

  NumRelocations = 0;
  AreRelocsRela = false;

  // The ELF spec states that a value of 0 means the section has
  // no alignment constraits.
  uint32_t V = std::max<uint64_t>(Alignment, 1);
  if (!isPowerOf2_64(V))
    fatal(toString(File) + ": section sh_addralign is not a power of 2");
  this->Alignment = V;

  // In ELF, each section can be compressed by zlib, and if compressed,
  // section name may be mangled by appending "z" (e.g. ".zdebug_info").
  // If that's the case, demangle section name so that we can handle a
  // section as if it weren't compressed.
  if ((Flags & SHF_COMPRESSED) || Name.startswith(".zdebug")) {
    if (!zlib::isAvailable())
      error(toString(File) + ": contains a compressed section, " +
            "but zlib is not available");
    parseCompressedHeader();
  }
}

// Drop SHF_GROUP bit unless we are producing a re-linkable object file.
// SHF_GROUP is a marker that a section belongs to some comdat group.
// That flag doesn't make sense in an executable.
static uint64_t getFlags(uint64_t Flags) {
  Flags &= ~(uint64_t)SHF_INFO_LINK;
  if (!Config->Relocatable)
    Flags &= ~(uint64_t)SHF_GROUP;
  return Flags;
}

// GNU assembler 2.24 and LLVM 4.0.0's MC (the newest release as of
// March 2017) fail to infer section types for sections starting with
// ".init_array." or ".fini_array.". They set SHT_PROGBITS instead of
// SHF_INIT_ARRAY. As a result, the following assembler directive
// creates ".init_array.100" with SHT_PROGBITS, for example.
//
//   .section .init_array.100, "aw"
//
// This function forces SHT_{INIT,FINI}_ARRAY so that we can handle
// incorrect inputs as if they were correct from the beginning.
static uint64_t getType(uint64_t Type, StringRef Name) {
  if (Type == SHT_PROGBITS && Name.startswith(".init_array."))
    return SHT_INIT_ARRAY;
  if (Type == SHT_PROGBITS && Name.startswith(".fini_array."))
    return SHT_FINI_ARRAY;
  return Type;
}

template <class ELFT>
InputSectionBase::InputSectionBase(ObjFile<ELFT> &File,
                                   const typename ELFT::Shdr &Hdr,
                                   StringRef Name, Kind SectionKind)
    : InputSectionBase(&File, getFlags(Hdr.sh_flags),
                       getType(Hdr.sh_type, Name), Hdr.sh_entsize, Hdr.sh_link,
                       Hdr.sh_info, Hdr.sh_addralign,
                       getSectionContents(File, Hdr), Name, SectionKind) {
  // We reject object files having insanely large alignments even though
  // they are allowed by the spec. I think 4GB is a reasonable limitation.
  // We might want to relax this in the future.
  if (Hdr.sh_addralign > UINT32_MAX)
    fatal(toString(&File) + ": section sh_addralign is too large");
}

size_t InputSectionBase::getSize() const {
  if (auto *S = dyn_cast<SyntheticSection>(this))
    return S->getSize();
  if (UncompressedSize >= 0)
    return UncompressedSize;
  return RawData.size();
}

void InputSectionBase::uncompress() const {
  size_t Size = UncompressedSize;
  UncompressedBuf.reset(new char[Size]);

  if (Error E =
          zlib::uncompress(toStringRef(RawData), UncompressedBuf.get(), Size))
    fatal(toString(this) +
          ": uncompress failed: " + llvm::toString(std::move(E)));
  RawData = makeArrayRef((uint8_t *)UncompressedBuf.get(), Size);
}

uint64_t InputSectionBase::getOffsetInFile() const {
  const uint8_t *FileStart = (const uint8_t *)File->MB.getBufferStart();
  const uint8_t *SecStart = data().begin();
  return SecStart - FileStart;
}

uint64_t SectionBase::getOffset(uint64_t Offset) const {
  switch (kind()) {
  case Output: {
    auto *OS = cast<OutputSection>(this);
    // For output sections we treat offset -1 as the end of the section.
    return Offset == uint64_t(-1) ? OS->Size : Offset;
  }
  case Regular:
  case Synthetic:
    return cast<InputSection>(this)->getOffset(Offset);
  case EHFrame:
    // The file crtbeginT.o has relocations pointing to the start of an empty
    // .eh_frame that is known to be the first in the link. It does that to
    // identify the start of the output .eh_frame.
    return Offset;
  case Merge:
    const MergeInputSection *MS = cast<MergeInputSection>(this);
    if (InputSection *IS = MS->getParent())
      return IS->getOffset(MS->getParentOffset(Offset));
    return MS->getParentOffset(Offset);
  }
  llvm_unreachable("invalid section kind");
}

uint64_t SectionBase::getVA(uint64_t Offset) const {
  const OutputSection *Out = getOutputSection();
  return (Out ? Out->Addr : 0) + getOffset(Offset);
}

OutputSection *SectionBase::getOutputSection() {
  InputSection *Sec;
  if (auto *IS = dyn_cast<InputSection>(this))
    Sec = IS;
  else if (auto *MS = dyn_cast<MergeInputSection>(this))
    Sec = MS->getParent();
  else if (auto *EH = dyn_cast<EhInputSection>(this))
    Sec = EH->getParent();
  else
    return cast<OutputSection>(this);
  return Sec ? Sec->getParent() : nullptr;
}

// When a section is compressed, `RawData` consists with a header followed
// by zlib-compressed data. This function parses a header to initialize
// `UncompressedSize` member and remove the header from `RawData`.
void InputSectionBase::parseCompressedHeader() {
  typedef typename ELF64LE::Chdr Chdr64;
  typedef typename ELF32LE::Chdr Chdr32;

  // Old-style header
  if (Name.startswith(".zdebug")) {
    if (!toStringRef(RawData).startswith("ZLIB")) {
      error(toString(this) + ": corrupted compressed section header");
      return;
    }
    RawData = RawData.slice(4);

    if (RawData.size() < 8) {
      error(toString(this) + ": corrupted compressed section header");
      return;
    }

    UncompressedSize = read64be(RawData.data());
    RawData = RawData.slice(8);

    // Restore the original section name.
    // (e.g. ".zdebug_info" -> ".debug_info")
    Name = Saver.save("." + Name.substr(2));
    return;
  }

  assert(Flags & SHF_COMPRESSED);
  Flags &= ~(uint64_t)SHF_COMPRESSED;

  // New-style 64-bit header
  if (Config->Is64) {
    if (RawData.size() < sizeof(Chdr64)) {
      error(toString(this) + ": corrupted compressed section");
      return;
    }

    auto *Hdr = reinterpret_cast<const Chdr64 *>(RawData.data());
    if (Hdr->ch_type != ELFCOMPRESS_ZLIB) {
      error(toString(this) + ": unsupported compression type");
      return;
    }

    UncompressedSize = Hdr->ch_size;
    RawData = RawData.slice(sizeof(*Hdr));
    return;
  }

  // New-style 32-bit header
  if (RawData.size() < sizeof(Chdr32)) {
    error(toString(this) + ": corrupted compressed section");
    return;
  }

  auto *Hdr = reinterpret_cast<const Chdr32 *>(RawData.data());
  if (Hdr->ch_type != ELFCOMPRESS_ZLIB) {
    error(toString(this) + ": unsupported compression type");
    return;
  }

  UncompressedSize = Hdr->ch_size;
  RawData = RawData.slice(sizeof(*Hdr));
}

InputSection *InputSectionBase::getLinkOrderDep() const {
  assert(Link);
  assert(Flags & SHF_LINK_ORDER);
  return cast<InputSection>(File->getSections()[Link]);
}

// Find a function symbol that encloses a given location.
template <class ELFT>
Defined *InputSectionBase::getEnclosingFunction(uint64_t Offset) {
  for (Symbol *B : File->getSymbols())
    if (Defined *D = dyn_cast<Defined>(B))
      if (D->Section == this && D->Type == STT_FUNC && D->Value <= Offset &&
          Offset < D->Value + D->Size)
        return D;
  return nullptr;
}

// Returns a source location string. Used to construct an error message.
template <class ELFT>
std::string InputSectionBase::getLocation(uint64_t Offset) {
  std::string SecAndOffset = (Name + "+0x" + utohexstr(Offset)).str();

  // We don't have file for synthetic sections.
  if (getFile<ELFT>() == nullptr)
    return (Config->OutputFile + ":(" + SecAndOffset + ")")
        .str();

  // First check if we can get desired values from debugging information.
  if (Optional<DILineInfo> Info = getFile<ELFT>()->getDILineInfo(this, Offset))
    return Info->FileName + ":" + std::to_string(Info->Line) + ":(" +
           SecAndOffset + ")";

  // File->SourceFile contains STT_FILE symbol that contains a
  // source file name. If it's missing, we use an object file name.
  std::string SrcFile = getFile<ELFT>()->SourceFile;
  if (SrcFile.empty())
    SrcFile = toString(File);

  if (Defined *D = getEnclosingFunction<ELFT>(Offset))
    return SrcFile + ":(function " + toString(*D) + ": " + SecAndOffset + ")";

  // If there's no symbol, print out the offset in the section.
  return (SrcFile + ":(" + SecAndOffset + ")");
}

// This function is intended to be used for constructing an error message.
// The returned message looks like this:
//
//   foo.c:42 (/home/alice/possibly/very/long/path/foo.c:42)
//
//  Returns an empty string if there's no way to get line info.
std::string InputSectionBase::getSrcMsg(const Symbol &Sym, uint64_t Offset) {
  return File->getSrcMsg(Sym, *this, Offset);
}

// Returns a filename string along with an optional section name. This
// function is intended to be used for constructing an error
// message. The returned message looks like this:
//
//   path/to/foo.o:(function bar)
//
// or
//
//   path/to/foo.o:(function bar) in archive path/to/bar.a
std::string InputSectionBase::getObjMsg(uint64_t Off) {
  std::string Filename = File->getName();

  std::string Archive;
  if (!File->ArchiveName.empty())
    Archive = " in archive " + File->ArchiveName;

  // Find a symbol that encloses a given location.
  for (Symbol *B : File->getSymbols())
    if (auto *D = dyn_cast<Defined>(B))
      if (D->Section == this && D->Value <= Off && Off < D->Value + D->Size)
        return Filename + ":(" + toString(*D) + ")" + Archive;

  // If there's no symbol, print out the offset in the section.
  return (Filename + ":(" + Name + "+0x" + utohexstr(Off) + ")" + Archive)
      .str();
}

InputSection InputSection::Discarded(nullptr, 0, 0, 0, ArrayRef<uint8_t>(), "");

InputSection::InputSection(InputFile *F, uint64_t Flags, uint32_t Type,
                           uint32_t Alignment, ArrayRef<uint8_t> Data,
                           StringRef Name, Kind K)
    : InputSectionBase(F, Flags, Type,
                       /*Entsize*/ 0, /*Link*/ 0, /*Info*/ 0, Alignment, Data,
                       Name, K) {}

template <class ELFT>
InputSection::InputSection(ObjFile<ELFT> &F, const typename ELFT::Shdr &Header,
                           StringRef Name)
    : InputSectionBase(F, Header, Name, InputSectionBase::Regular) {}

bool InputSection::classof(const SectionBase *S) {
  return S->kind() == SectionBase::Regular ||
         S->kind() == SectionBase::Synthetic;
}

OutputSection *InputSection::getParent() const {
  return cast_or_null<OutputSection>(Parent);
}

// Copy SHT_GROUP section contents. Used only for the -r option.
template <class ELFT> void InputSection::copyShtGroup(uint8_t *Buf) {
  // ELFT::Word is the 32-bit integral type in the target endianness.
  typedef typename ELFT::Word u32;
  ArrayRef<u32> From = getDataAs<u32>();
  auto *To = reinterpret_cast<u32 *>(Buf);

  // The first entry is not a section number but a flag.
  *To++ = From[0];

  // Adjust section numbers because section numbers in an input object
  // files are different in the output.
  ArrayRef<InputSectionBase *> Sections = File->getSections();
  for (uint32_t Idx : From.slice(1))
    *To++ = Sections[Idx]->getOutputSection()->SectionIndex;
}

InputSectionBase *InputSection::getRelocatedSection() const {
  if (!File || (Type != SHT_RELA && Type != SHT_REL))
    return nullptr;
  ArrayRef<InputSectionBase *> Sections = File->getSections();
  return Sections[Info];
}

// This is used for -r and --emit-relocs. We can't use memcpy to copy
// relocations because we need to update symbol table offset and section index
// for each relocation. So we copy relocations one by one.
template <class ELFT, class RelTy>
void InputSection::copyRelocations(uint8_t *Buf, ArrayRef<RelTy> Rels) {
  InputSectionBase *Sec = getRelocatedSection();

  for (const RelTy &Rel : Rels) {
    RelType Type = Rel.getType(Config->IsMips64EL);
    Symbol &Sym = getFile<ELFT>()->getRelocTargetSym(Rel);

    auto *P = reinterpret_cast<typename ELFT::Rela *>(Buf);
    Buf += sizeof(RelTy);

    if (RelTy::IsRela)
      P->r_addend = getAddend<ELFT>(Rel);

    // Output section VA is zero for -r, so r_offset is an offset within the
    // section, but for --emit-relocs it is an virtual address.
    P->r_offset = Sec->getVA(Rel.r_offset);
    P->setSymbolAndType(In.SymTab->getSymbolIndex(&Sym), Type,
                        Config->IsMips64EL);

    if (Sym.Type == STT_SECTION) {
      // We combine multiple section symbols into only one per
      // section. This means we have to update the addend. That is
      // trivial for Elf_Rela, but for Elf_Rel we have to write to the
      // section data. We do that by adding to the Relocation vector.

      // .eh_frame is horribly special and can reference discarded sections. To
      // avoid having to parse and recreate .eh_frame, we just replace any
      // relocation in it pointing to discarded sections with R_*_NONE, which
      // hopefully creates a frame that is ignored at runtime.
      auto *D = dyn_cast<Defined>(&Sym);
      if (!D) {
        error("STT_SECTION symbol should be defined");
        continue;
      }
      SectionBase *Section = D->Section->Repl;
      if (!Section->Live) {
        P->setSymbolAndType(0, 0, false);
        continue;
      }

      int64_t Addend = getAddend<ELFT>(Rel);
      const uint8_t *BufLoc = Sec->data().begin() + Rel.r_offset;
      if (!RelTy::IsRela)
        Addend = Target->getImplicitAddend(BufLoc, Type);

      if (Config->EMachine == EM_MIPS && Config->Relocatable &&
          Target->getRelExpr(Type, Sym, BufLoc) == R_MIPS_GOTREL) {
        // Some MIPS relocations depend on "gp" value. By default,
        // this value has 0x7ff0 offset from a .got section. But
        // relocatable files produced by a complier or a linker
        // might redefine this default value and we must use it
        // for a calculation of the relocation result. When we
        // generate EXE or DSO it's trivial. Generating a relocatable
        // output is more difficult case because the linker does
        // not calculate relocations in this mode and loses
        // individual "gp" values used by each input object file.
        // As a workaround we add the "gp" value to the relocation
        // addend and save it back to the file.
        Addend += Sec->getFile<ELFT>()->MipsGp0;
      }

      if (RelTy::IsRela)
        P->r_addend = Sym.getVA(Addend) - Section->getOutputSection()->Addr;
      else if (Config->Relocatable)
        Sec->Relocations.push_back({R_ABS, Type, Rel.r_offset, Addend, &Sym});
    }
  }
}

// The ARM and AArch64 ABI handle pc-relative relocations to undefined weak
// references specially. The general rule is that the value of the symbol in
// this context is the address of the place P. A further special case is that
// branch relocations to an undefined weak reference resolve to the next
// instruction.
static uint32_t getARMUndefinedRelativeWeakVA(RelType Type, uint32_t A,
                                              uint32_t P) {
  switch (Type) {
  // Unresolved branch relocations to weak references resolve to next
  // instruction, this will be either 2 or 4 bytes on from P.
  case R_ARM_THM_JUMP11:
    return P + 2 + A;
  case R_ARM_CALL:
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_PREL31:
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
    return P + 4 + A;
  case R_ARM_THM_CALL:
    // We don't want an interworking BLX to ARM
    return P + 5 + A;
  // Unresolved non branch pc-relative relocations
  // R_ARM_TARGET2 which can be resolved relatively is not present as it never
  // targets a weak-reference.
  case R_ARM_MOVW_PREL_NC:
  case R_ARM_MOVT_PREL:
  case R_ARM_REL32:
  case R_ARM_THM_MOVW_PREL_NC:
  case R_ARM_THM_MOVT_PREL:
    return P + A;
  }
  llvm_unreachable("ARM pc-relative relocation expected\n");
}

// The comment above getARMUndefinedRelativeWeakVA applies to this function.
static uint64_t getAArch64UndefinedRelativeWeakVA(uint64_t Type, uint64_t A,
                                                  uint64_t P) {
  switch (Type) {
  // Unresolved branch relocations to weak references resolve to next
  // instruction, this is 4 bytes on from P.
  case R_AARCH64_CALL26:
  case R_AARCH64_CONDBR19:
  case R_AARCH64_JUMP26:
  case R_AARCH64_TSTBR14:
    return P + 4 + A;
  // Unresolved non branch pc-relative relocations
  case R_AARCH64_PREL16:
  case R_AARCH64_PREL32:
  case R_AARCH64_PREL64:
  case R_AARCH64_ADR_PREL_LO21:
  case R_AARCH64_LD_PREL_LO19:
    return P + A;
  }
  llvm_unreachable("AArch64 pc-relative relocation expected\n");
}

// ARM SBREL relocations are of the form S + A - B where B is the static base
// The ARM ABI defines base to be "addressing origin of the output segment
// defining the symbol S". We defined the "addressing origin"/static base to be
// the base of the PT_LOAD segment containing the Sym.
// The procedure call standard only defines a Read Write Position Independent
// RWPI variant so in practice we should expect the static base to be the base
// of the RW segment.
static uint64_t getARMStaticBase(const Symbol &Sym) {
  OutputSection *OS = Sym.getOutputSection();
  if (!OS || !OS->PtLoad || !OS->PtLoad->FirstSec)
    fatal("SBREL relocation to " + Sym.getName() + " without static base");
  return OS->PtLoad->FirstSec->Addr;
}

// For R_RISCV_PC_INDIRECT (R_RISCV_PCREL_LO12_{I,S}), the symbol actually
// points the corresponding R_RISCV_PCREL_HI20 relocation, and the target VA
// is calculated using PCREL_HI20's symbol.
//
// This function returns the R_RISCV_PCREL_HI20 relocation from
// R_RISCV_PCREL_LO12's symbol and addend.
static Relocation *getRISCVPCRelHi20(const Symbol *Sym, uint64_t Addend) {
  const Defined *D = cast<Defined>(Sym);
  InputSection *IS = cast<InputSection>(D->Section);

  if (Addend != 0)
    warn("Non-zero addend in R_RISCV_PCREL_LO12 relocation to " +
         IS->getObjMsg(D->Value) + " is ignored");

  // Relocations are sorted by offset, so we can use std::equal_range to do
  // binary search.
  auto Range = std::equal_range(IS->Relocations.begin(), IS->Relocations.end(),
                                D->Value, RelocationOffsetComparator{});
  for (auto It = std::get<0>(Range); It != std::get<1>(Range); ++It)
    if (isRelExprOneOf<R_PC>(It->Expr))
      return &*It;

  error("R_RISCV_PCREL_LO12 relocation points to " + IS->getObjMsg(D->Value) +
        " without an associated R_RISCV_PCREL_HI20 relocation");
  return nullptr;
}

// A TLS symbol's virtual address is relative to the TLS segment. Add a
// target-specific adjustment to produce a thread-pointer-relative offset.
static int64_t getTlsTpOffset() {
  switch (Config->EMachine) {
  case EM_ARM:
  case EM_AARCH64:
    // Variant 1. The thread pointer points to a TCB with a fixed 2-word size,
    // followed by a variable amount of alignment padding, followed by the TLS
    // segment.
    //
    // NB: While the ARM/AArch64 ABI formally has a 2-word TCB size, lld
    // effectively increases the TCB size to 8 words for Android compatibility.
    // It accomplishes this by increasing the segment's alignment.
    return alignTo(Config->Wordsize * 2, Out::TlsPhdr->p_align);
  case EM_386:
  case EM_X86_64:
    // Variant 2. The TLS segment is located just before the thread pointer.
    return -Out::TlsPhdr->p_memsz;
  case EM_PPC64:
    // The thread pointer points to a fixed offset from the start of the
    // executable's TLS segment. An offset of 0x7000 allows a signed 16-bit
    // offset to reach 0x1000 of TCB/thread-library data and 0xf000 of the
    // program's TLS segment.
    return -0x7000;
  default:
    llvm_unreachable("unhandled Config->EMachine");
  }
}

static uint64_t getRelocTargetVA(const InputFile *File, RelType Type, int64_t A,
                                 uint64_t P, const Symbol &Sym, RelExpr Expr) {
  switch (Expr) {
  case R_INVALID:
    return 0;
  case R_ABS:
  case R_RELAX_TLS_LD_TO_LE_ABS:
  case R_RELAX_GOT_PC_NOPIC:
    return Sym.getVA(A);
  case R_ADDEND:
    return A;
  case R_ARM_SBREL:
    return Sym.getVA(A) - getARMStaticBase(Sym);
  case R_GOT:
  case R_GOT_PLT:
  case R_RELAX_TLS_GD_TO_IE_ABS:
    return Sym.getGotVA() + A;
  case R_GOTONLY_PC:
    return In.Got->getVA() + A - P;
  case R_GOTONLY_PC_FROM_END:
    return In.Got->getVA() + A - P + In.Got->getSize();
  case R_GOTREL:
    return Sym.getVA(A) - In.Got->getVA();
  case R_GOTREL_FROM_END:
    return Sym.getVA(A) - In.Got->getVA() - In.Got->getSize();
  case R_GOT_FROM_END:
  case R_RELAX_TLS_GD_TO_IE_END:
    return Sym.getGotOffset() + A - In.Got->getSize();
  case R_TLSLD_GOT_OFF:
  case R_GOT_OFF:
  case R_RELAX_TLS_GD_TO_IE_GOT_OFF:
    return Sym.getGotOffset() + A;
  case R_AARCH64_GOT_PAGE_PC:
  case R_AARCH64_GOT_PAGE_PC_PLT:
  case R_AARCH64_RELAX_TLS_GD_TO_IE_PAGE_PC:
    return getAArch64Page(Sym.getGotVA() + A) - getAArch64Page(P);
  case R_GOT_PC:
  case R_RELAX_TLS_GD_TO_IE:
    return Sym.getGotVA() + A - P;
  case R_HEXAGON_GOT:
    return Sym.getGotVA() - In.GotPlt->getVA();
  case R_MIPS_GOTREL:
    return Sym.getVA(A) - In.MipsGot->getGp(File);
  case R_MIPS_GOT_GP:
    return In.MipsGot->getGp(File) + A;
  case R_MIPS_GOT_GP_PC: {
    // R_MIPS_LO16 expression has R_MIPS_GOT_GP_PC type iif the target
    // is _gp_disp symbol. In that case we should use the following
    // formula for calculation "AHL + GP - P + 4". For details see p. 4-19 at
    // ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
    // microMIPS variants of these relocations use slightly different
    // expressions: AHL + GP - P + 3 for %lo() and AHL + GP - P - 1 for %hi()
    // to correctly handle less-sugnificant bit of the microMIPS symbol.
    uint64_t V = In.MipsGot->getGp(File) + A - P;
    if (Type == R_MIPS_LO16 || Type == R_MICROMIPS_LO16)
      V += 4;
    if (Type == R_MICROMIPS_LO16 || Type == R_MICROMIPS_HI16)
      V -= 1;
    return V;
  }
  case R_MIPS_GOT_LOCAL_PAGE:
    // If relocation against MIPS local symbol requires GOT entry, this entry
    // should be initialized by 'page address'. This address is high 16-bits
    // of sum the symbol's value and the addend.
    return In.MipsGot->getVA() + In.MipsGot->getPageEntryOffset(File, Sym, A) -
           In.MipsGot->getGp(File);
  case R_MIPS_GOT_OFF:
  case R_MIPS_GOT_OFF32:
    // In case of MIPS if a GOT relocation has non-zero addend this addend
    // should be applied to the GOT entry content not to the GOT entry offset.
    // That is why we use separate expression type.
    return In.MipsGot->getVA() + In.MipsGot->getSymEntryOffset(File, Sym, A) -
           In.MipsGot->getGp(File);
  case R_MIPS_TLSGD:
    return In.MipsGot->getVA() + In.MipsGot->getGlobalDynOffset(File, Sym) -
           In.MipsGot->getGp(File);
  case R_MIPS_TLSLD:
    return In.MipsGot->getVA() + In.MipsGot->getTlsIndexOffset(File) -
           In.MipsGot->getGp(File);
  case R_AARCH64_PAGE_PC: {
    uint64_t Val = Sym.isUndefWeak() ? P + A : Sym.getVA(A);
    return getAArch64Page(Val) - getAArch64Page(P);
  }
  case R_AARCH64_PLT_PAGE_PC: {
    uint64_t Val = Sym.isUndefWeak() ? P + A : Sym.getPltVA() + A;
    return getAArch64Page(Val) - getAArch64Page(P);
  }
  case R_RISCV_PC_INDIRECT: {
    if (const Relocation *HiRel = getRISCVPCRelHi20(&Sym, A))
      return getRelocTargetVA(File, HiRel->Type, HiRel->Addend, Sym.getVA(),
                              *HiRel->Sym, HiRel->Expr);
    return 0;
  }
  case R_PC: {
    uint64_t Dest;
    if (Sym.isUndefWeak()) {
      // On ARM and AArch64 a branch to an undefined weak resolves to the
      // next instruction, otherwise the place.
      if (Config->EMachine == EM_ARM)
        Dest = getARMUndefinedRelativeWeakVA(Type, A, P);
      else if (Config->EMachine == EM_AARCH64)
        Dest = getAArch64UndefinedRelativeWeakVA(Type, A, P);
      else
        Dest = Sym.getVA(A);
    } else {
      Dest = Sym.getVA(A);
    }
    return Dest - P;
  }
  case R_PLT:
    return Sym.getPltVA() + A;
  case R_PLT_PC:
  case R_PPC_CALL_PLT:
    return Sym.getPltVA() + A - P;
  case R_PPC_CALL: {
    uint64_t SymVA = Sym.getVA(A);
    // If we have an undefined weak symbol, we might get here with a symbol
    // address of zero. That could overflow, but the code must be unreachable,
    // so don't bother doing anything at all.
    if (!SymVA)
      return 0;

    // PPC64 V2 ABI describes two entry points to a function. The global entry
    // point is used for calls where the caller and callee (may) have different
    // TOC base pointers and r2 needs to be modified to hold the TOC base for
    // the callee. For local calls the caller and callee share the same
    // TOC base and so the TOC pointer initialization code should be skipped by
    // branching to the local entry point.
    return SymVA - P + getPPC64GlobalEntryToLocalEntryOffset(Sym.StOther);
  }
  case R_PPC_TOC:
    return getPPC64TocBase() + A;
  case R_RELAX_GOT_PC:
    return Sym.getVA(A) - P;
  case R_RELAX_TLS_GD_TO_LE:
  case R_RELAX_TLS_IE_TO_LE:
  case R_RELAX_TLS_LD_TO_LE:
  case R_TLS:
    // A weak undefined TLS symbol resolves to the base of the TLS
    // block, i.e. gets a value of zero. If we pass --gc-sections to
    // lld and .tbss is not referenced, it gets reclaimed and we don't
    // create a TLS program header. Therefore, we resolve this
    // statically to zero.
    if (Sym.isTls() && Sym.isUndefWeak())
      return 0;
    return Sym.getVA(A) + getTlsTpOffset();
  case R_RELAX_TLS_GD_TO_LE_NEG:
  case R_NEG_TLS:
    return Out::TlsPhdr->p_memsz - Sym.getVA(A);
  case R_SIZE:
    return Sym.getSize() + A;
  case R_TLSDESC:
    return In.Got->getGlobalDynAddr(Sym) + A;
  case R_AARCH64_TLSDESC_PAGE:
    return getAArch64Page(In.Got->getGlobalDynAddr(Sym) + A) -
           getAArch64Page(P);
  case R_TLSGD_GOT:
    return In.Got->getGlobalDynOffset(Sym) + A;
  case R_TLSGD_GOT_FROM_END:
    return In.Got->getGlobalDynOffset(Sym) + A - In.Got->getSize();
  case R_TLSGD_PC:
    return In.Got->getGlobalDynAddr(Sym) + A - P;
  case R_TLSLD_GOT_FROM_END:
    return In.Got->getTlsIndexOff() + A - In.Got->getSize();
  case R_TLSLD_GOT:
    return In.Got->getTlsIndexOff() + A;
  case R_TLSLD_PC:
    return In.Got->getTlsIndexVA() + A - P;
  default:
    llvm_unreachable("invalid expression");
  }
}

// This function applies relocations to sections without SHF_ALLOC bit.
// Such sections are never mapped to memory at runtime. Debug sections are
// an example. Relocations in non-alloc sections are much easier to
// handle than in allocated sections because it will never need complex
// treatement such as GOT or PLT (because at runtime no one refers them).
// So, we handle relocations for non-alloc sections directly in this
// function as a performance optimization.
template <class ELFT, class RelTy>
void InputSection::relocateNonAlloc(uint8_t *Buf, ArrayRef<RelTy> Rels) {
  const unsigned Bits = sizeof(typename ELFT::uint) * 8;

  for (const RelTy &Rel : Rels) {
    RelType Type = Rel.getType(Config->IsMips64EL);

    // GCC 8.0 or earlier have a bug that they emit R_386_GOTPC relocations
    // against _GLOBAL_OFFSET_TABLE_ for .debug_info. The bug has been fixed
    // in 2017 (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82630), but we
    // need to keep this bug-compatible code for a while.
    if (Config->EMachine == EM_386 && Type == R_386_GOTPC)
      continue;

    uint64_t Offset = getOffset(Rel.r_offset);
    uint8_t *BufLoc = Buf + Offset;
    int64_t Addend = getAddend<ELFT>(Rel);
    if (!RelTy::IsRela)
      Addend += Target->getImplicitAddend(BufLoc, Type);

    Symbol &Sym = getFile<ELFT>()->getRelocTargetSym(Rel);
    RelExpr Expr = Target->getRelExpr(Type, Sym, BufLoc);
    if (Expr == R_NONE)
      continue;

    if (Expr != R_ABS) {
      std::string Msg = getLocation<ELFT>(Offset) +
                        ": has non-ABS relocation " + toString(Type) +
                        " against symbol '" + toString(Sym) + "'";
      if (Expr != R_PC) {
        error(Msg);
        return;
      }

      // If the control reaches here, we found a PC-relative relocation in a
      // non-ALLOC section. Since non-ALLOC section is not loaded into memory
      // at runtime, the notion of PC-relative doesn't make sense here. So,
      // this is a usage error. However, GNU linkers historically accept such
      // relocations without any errors and relocate them as if they were at
      // address 0. For bug-compatibilty, we accept them with warnings. We
      // know Steel Bank Common Lisp as of 2018 have this bug.
      warn(Msg);
      Target->relocateOne(BufLoc, Type,
                          SignExtend64<Bits>(Sym.getVA(Addend - Offset)));
      continue;
    }

    if (Sym.isTls() && !Out::TlsPhdr)
      Target->relocateOne(BufLoc, Type, 0);
    else
      Target->relocateOne(BufLoc, Type, SignExtend64<Bits>(Sym.getVA(Addend)));
  }
}

// This is used when '-r' is given.
// For REL targets, InputSection::copyRelocations() may store artificial
// relocations aimed to update addends. They are handled in relocateAlloc()
// for allocatable sections, and this function does the same for
// non-allocatable sections, such as sections with debug information.
static void relocateNonAllocForRelocatable(InputSection *Sec, uint8_t *Buf) {
  const unsigned Bits = Config->Is64 ? 64 : 32;

  for (const Relocation &Rel : Sec->Relocations) {
    // InputSection::copyRelocations() adds only R_ABS relocations.
    assert(Rel.Expr == R_ABS);
    uint8_t *BufLoc = Buf + Rel.Offset + Sec->OutSecOff;
    uint64_t TargetVA = SignExtend64(Rel.Sym->getVA(Rel.Addend), Bits);
    Target->relocateOne(BufLoc, Rel.Type, TargetVA);
  }
}

template <class ELFT>
void InputSectionBase::relocate(uint8_t *Buf, uint8_t *BufEnd) {
  if (Flags & SHF_EXECINSTR)
    adjustSplitStackFunctionPrologues<ELFT>(Buf, BufEnd);

  if (Flags & SHF_ALLOC) {
    relocateAlloc(Buf, BufEnd);
    return;
  }

  auto *Sec = cast<InputSection>(this);
  if (Config->Relocatable)
    relocateNonAllocForRelocatable(Sec, Buf);
  else if (Sec->AreRelocsRela)
    Sec->relocateNonAlloc<ELFT>(Buf, Sec->template relas<ELFT>());
  else
    Sec->relocateNonAlloc<ELFT>(Buf, Sec->template rels<ELFT>());
}

void InputSectionBase::relocateAlloc(uint8_t *Buf, uint8_t *BufEnd) {
  assert(Flags & SHF_ALLOC);
  const unsigned Bits = Config->Wordsize * 8;

  for (const Relocation &Rel : Relocations) {
    uint64_t Offset = Rel.Offset;
    if (auto *Sec = dyn_cast<InputSection>(this))
      Offset += Sec->OutSecOff;
    uint8_t *BufLoc = Buf + Offset;
    RelType Type = Rel.Type;

    uint64_t AddrLoc = getOutputSection()->Addr + Offset;
    RelExpr Expr = Rel.Expr;
    uint64_t TargetVA = SignExtend64(
        getRelocTargetVA(File, Type, Rel.Addend, AddrLoc, *Rel.Sym, Expr),
        Bits);

    switch (Expr) {
    case R_RELAX_GOT_PC:
    case R_RELAX_GOT_PC_NOPIC:
      Target->relaxGot(BufLoc, TargetVA);
      break;
    case R_RELAX_TLS_IE_TO_LE:
      Target->relaxTlsIeToLe(BufLoc, Type, TargetVA);
      break;
    case R_RELAX_TLS_LD_TO_LE:
    case R_RELAX_TLS_LD_TO_LE_ABS:
      Target->relaxTlsLdToLe(BufLoc, Type, TargetVA);
      break;
    case R_RELAX_TLS_GD_TO_LE:
    case R_RELAX_TLS_GD_TO_LE_NEG:
      Target->relaxTlsGdToLe(BufLoc, Type, TargetVA);
      break;
    case R_AARCH64_RELAX_TLS_GD_TO_IE_PAGE_PC:
    case R_RELAX_TLS_GD_TO_IE:
    case R_RELAX_TLS_GD_TO_IE_ABS:
    case R_RELAX_TLS_GD_TO_IE_GOT_OFF:
    case R_RELAX_TLS_GD_TO_IE_END:
      Target->relaxTlsGdToIe(BufLoc, Type, TargetVA);
      break;
    case R_PPC_CALL:
      // If this is a call to __tls_get_addr, it may be part of a TLS
      // sequence that has been relaxed and turned into a nop. In this
      // case, we don't want to handle it as a call.
      if (read32(BufLoc) == 0x60000000) // nop
        break;

      // Patch a nop (0x60000000) to a ld.
      if (Rel.Sym->NeedsTocRestore) {
        if (BufLoc + 8 > BufEnd || read32(BufLoc + 4) != 0x60000000) {
          error(getErrorLocation(BufLoc) + "call lacks nop, can't restore toc");
          break;
        }
        write32(BufLoc + 4, 0xe8410018); // ld %r2, 24(%r1)
      }
      Target->relocateOne(BufLoc, Type, TargetVA);
      break;
    default:
      Target->relocateOne(BufLoc, Type, TargetVA);
      break;
    }
  }
}

// For each function-defining prologue, find any calls to __morestack,
// and replace them with calls to __morestack_non_split.
static void switchMorestackCallsToMorestackNonSplit(
    DenseSet<Defined *> &Prologues, std::vector<Relocation *> &MorestackCalls) {

  // If the target adjusted a function's prologue, all calls to
  // __morestack inside that function should be switched to
  // __morestack_non_split.
  Symbol *MoreStackNonSplit = Symtab->find("__morestack_non_split");
  if (!MoreStackNonSplit) {
    error("Mixing split-stack objects requires a definition of "
          "__morestack_non_split");
    return;
  }

  // Sort both collections to compare addresses efficiently.
  llvm::sort(MorestackCalls, [](const Relocation *L, const Relocation *R) {
    return L->Offset < R->Offset;
  });
  std::vector<Defined *> Functions(Prologues.begin(), Prologues.end());
  llvm::sort(Functions, [](const Defined *L, const Defined *R) {
    return L->Value < R->Value;
  });

  auto It = MorestackCalls.begin();
  for (Defined *F : Functions) {
    // Find the first call to __morestack within the function.
    while (It != MorestackCalls.end() && (*It)->Offset < F->Value)
      ++It;
    // Adjust all calls inside the function.
    while (It != MorestackCalls.end() && (*It)->Offset < F->Value + F->Size) {
      (*It)->Sym = MoreStackNonSplit;
      ++It;
    }
  }
}

static bool enclosingPrologueAttempted(uint64_t Offset,
                                       const DenseSet<Defined *> &Prologues) {
  for (Defined *F : Prologues)
    if (F->Value <= Offset && Offset < F->Value + F->Size)
      return true;
  return false;
}

// If a function compiled for split stack calls a function not
// compiled for split stack, then the caller needs its prologue
// adjusted to ensure that the called function will have enough stack
// available. Find those functions, and adjust their prologues.
template <class ELFT>
void InputSectionBase::adjustSplitStackFunctionPrologues(uint8_t *Buf,
                                                         uint8_t *End) {
  if (!getFile<ELFT>()->SplitStack)
    return;
  DenseSet<Defined *> Prologues;
  std::vector<Relocation *> MorestackCalls;

  for (Relocation &Rel : Relocations) {
    // Local symbols can't possibly be cross-calls, and should have been
    // resolved long before this line.
    if (Rel.Sym->isLocal())
      continue;

    // Ignore calls into the split-stack api.
    if (Rel.Sym->getName().startswith("__morestack")) {
      if (Rel.Sym->getName().equals("__morestack"))
        MorestackCalls.push_back(&Rel);
      continue;
    }

    // A relocation to non-function isn't relevant. Sometimes
    // __morestack is not marked as a function, so this check comes
    // after the name check.
    if (Rel.Sym->Type != STT_FUNC)
      continue;

    // If the callee's-file was compiled with split stack, nothing to do.  In
    // this context, a "Defined" symbol is one "defined by the binary currently
    // being produced". So an "undefined" symbol might be provided by a shared
    // library. It is not possible to tell how such symbols were compiled, so be
    // conservative.
    if (Defined *D = dyn_cast<Defined>(Rel.Sym))
      if (InputSection *IS = cast_or_null<InputSection>(D->Section))
        if (!IS || !IS->getFile<ELFT>() || IS->getFile<ELFT>()->SplitStack)
          continue;

    if (enclosingPrologueAttempted(Rel.Offset, Prologues))
      continue;

    if (Defined *F = getEnclosingFunction<ELFT>(Rel.Offset)) {
      Prologues.insert(F);
      if (Target->adjustPrologueForCrossSplitStack(Buf + getOffset(F->Value),
                                                   End, F->StOther))
        continue;
      if (!getFile<ELFT>()->SomeNoSplitStack)
        error(lld::toString(this) + ": " + F->getName() +
              " (with -fsplit-stack) calls " + Rel.Sym->getName() +
              " (without -fsplit-stack), but couldn't adjust its prologue");
    }
  }

  if (Target->NeedsMoreStackNonSplit)
    switchMorestackCallsToMorestackNonSplit(Prologues, MorestackCalls);
}

template <class ELFT> void InputSection::writeTo(uint8_t *Buf) {
  if (Type == SHT_NOBITS)
    return;

  if (auto *S = dyn_cast<SyntheticSection>(this)) {
    S->writeTo(Buf + OutSecOff);
    return;
  }

  // If -r or --emit-relocs is given, then an InputSection
  // may be a relocation section.
  if (Type == SHT_RELA) {
    copyRelocations<ELFT>(Buf + OutSecOff, getDataAs<typename ELFT::Rela>());
    return;
  }
  if (Type == SHT_REL) {
    copyRelocations<ELFT>(Buf + OutSecOff, getDataAs<typename ELFT::Rel>());
    return;
  }

  // If -r is given, we may have a SHT_GROUP section.
  if (Type == SHT_GROUP) {
    copyShtGroup<ELFT>(Buf + OutSecOff);
    return;
  }

  // If this is a compressed section, uncompress section contents directly
  // to the buffer.
  if (UncompressedSize >= 0 && !UncompressedBuf) {
    size_t Size = UncompressedSize;
    if (Error E = zlib::uncompress(toStringRef(RawData),
                                   (char *)(Buf + OutSecOff), Size))
      fatal(toString(this) +
            ": uncompress failed: " + llvm::toString(std::move(E)));
    uint8_t *BufEnd = Buf + OutSecOff + Size;
    relocate<ELFT>(Buf, BufEnd);
    return;
  }

  // Copy section contents from source object file to output file
  // and then apply relocations.
  memcpy(Buf + OutSecOff, data().data(), data().size());
  uint8_t *BufEnd = Buf + OutSecOff + data().size();
  relocate<ELFT>(Buf, BufEnd);
}

void InputSection::replace(InputSection *Other) {
  Alignment = std::max(Alignment, Other->Alignment);
  Other->Repl = Repl;
  Other->Live = false;
}

template <class ELFT>
EhInputSection::EhInputSection(ObjFile<ELFT> &F,
                               const typename ELFT::Shdr &Header,
                               StringRef Name)
    : InputSectionBase(F, Header, Name, InputSectionBase::EHFrame) {}

SyntheticSection *EhInputSection::getParent() const {
  return cast_or_null<SyntheticSection>(Parent);
}

// Returns the index of the first relocation that points to a region between
// Begin and Begin+Size.
template <class IntTy, class RelTy>
static unsigned getReloc(IntTy Begin, IntTy Size, const ArrayRef<RelTy> &Rels,
                         unsigned &RelocI) {
  // Start search from RelocI for fast access. That works because the
  // relocations are sorted in .eh_frame.
  for (unsigned N = Rels.size(); RelocI < N; ++RelocI) {
    const RelTy &Rel = Rels[RelocI];
    if (Rel.r_offset < Begin)
      continue;

    if (Rel.r_offset < Begin + Size)
      return RelocI;
    return -1;
  }
  return -1;
}

// .eh_frame is a sequence of CIE or FDE records.
// This function splits an input section into records and returns them.
template <class ELFT> void EhInputSection::split() {
  if (AreRelocsRela)
    split<ELFT>(relas<ELFT>());
  else
    split<ELFT>(rels<ELFT>());
}

template <class ELFT, class RelTy>
void EhInputSection::split(ArrayRef<RelTy> Rels) {
  unsigned RelI = 0;
  for (size_t Off = 0, End = data().size(); Off != End;) {
    size_t Size = readEhRecordSize(this, Off);
    Pieces.emplace_back(Off, this, Size, getReloc(Off, Size, Rels, RelI));
    // The empty record is the end marker.
    if (Size == 4)
      break;
    Off += Size;
  }
}

static size_t findNull(StringRef S, size_t EntSize) {
  // Optimize the common case.
  if (EntSize == 1)
    return S.find(0);

  for (unsigned I = 0, N = S.size(); I != N; I += EntSize) {
    const char *B = S.begin() + I;
    if (std::all_of(B, B + EntSize, [](char C) { return C == 0; }))
      return I;
  }
  return StringRef::npos;
}

SyntheticSection *MergeInputSection::getParent() const {
  return cast_or_null<SyntheticSection>(Parent);
}

// Split SHF_STRINGS section. Such section is a sequence of
// null-terminated strings.
void MergeInputSection::splitStrings(ArrayRef<uint8_t> Data, size_t EntSize) {
  size_t Off = 0;
  bool IsAlloc = Flags & SHF_ALLOC;
  StringRef S = toStringRef(Data);

  while (!S.empty()) {
    size_t End = findNull(S, EntSize);
    if (End == StringRef::npos)
      fatal(toString(this) + ": string is not null terminated");
    size_t Size = End + EntSize;

    Pieces.emplace_back(Off, xxHash64(S.substr(0, Size)), !IsAlloc);
    S = S.substr(Size);
    Off += Size;
  }
}

// Split non-SHF_STRINGS section. Such section is a sequence of
// fixed size records.
void MergeInputSection::splitNonStrings(ArrayRef<uint8_t> Data,
                                        size_t EntSize) {
  size_t Size = Data.size();
  assert((Size % EntSize) == 0);
  bool IsAlloc = Flags & SHF_ALLOC;

  for (size_t I = 0; I != Size; I += EntSize)
    Pieces.emplace_back(I, xxHash64(Data.slice(I, EntSize)), !IsAlloc);
}

template <class ELFT>
MergeInputSection::MergeInputSection(ObjFile<ELFT> &F,
                                     const typename ELFT::Shdr &Header,
                                     StringRef Name)
    : InputSectionBase(F, Header, Name, InputSectionBase::Merge) {}

MergeInputSection::MergeInputSection(uint64_t Flags, uint32_t Type,
                                     uint64_t Entsize, ArrayRef<uint8_t> Data,
                                     StringRef Name)
    : InputSectionBase(nullptr, Flags, Type, Entsize, /*Link*/ 0, /*Info*/ 0,
                       /*Alignment*/ Entsize, Data, Name, SectionBase::Merge) {}

// This function is called after we obtain a complete list of input sections
// that need to be linked. This is responsible to split section contents
// into small chunks for further processing.
//
// Note that this function is called from parallelForEach. This must be
// thread-safe (i.e. no memory allocation from the pools).
void MergeInputSection::splitIntoPieces() {
  assert(Pieces.empty());

  if (Flags & SHF_STRINGS)
    splitStrings(data(), Entsize);
  else
    splitNonStrings(data(), Entsize);
}

SectionPiece *MergeInputSection::getSectionPiece(uint64_t Offset) {
  if (this->data().size() <= Offset)
    fatal(toString(this) + ": offset is outside the section");

  // If Offset is not at beginning of a section piece, it is not in the map.
  // In that case we need to  do a binary search of the original section piece vector.
  auto It2 =
      llvm::upper_bound(Pieces, Offset, [](uint64_t Offset, SectionPiece P) {
        return Offset < P.InputOff;
      });
  return &It2[-1];
}

// Returns the offset in an output section for a given input offset.
// Because contents of a mergeable section is not contiguous in output,
// it is not just an addition to a base output offset.
uint64_t MergeInputSection::getParentOffset(uint64_t Offset) const {
  // If Offset is not at beginning of a section piece, it is not in the map.
  // In that case we need to search from the original section piece vector.
  const SectionPiece &Piece =
      *(const_cast<MergeInputSection *>(this)->getSectionPiece (Offset));
  uint64_t Addend = Offset - Piece.InputOff;
  return Piece.OutputOff + Addend;
}

template InputSection::InputSection(ObjFile<ELF32LE> &, const ELF32LE::Shdr &,
                                    StringRef);
template InputSection::InputSection(ObjFile<ELF32BE> &, const ELF32BE::Shdr &,
                                    StringRef);
template InputSection::InputSection(ObjFile<ELF64LE> &, const ELF64LE::Shdr &,
                                    StringRef);
template InputSection::InputSection(ObjFile<ELF64BE> &, const ELF64BE::Shdr &,
                                    StringRef);

template std::string InputSectionBase::getLocation<ELF32LE>(uint64_t);
template std::string InputSectionBase::getLocation<ELF32BE>(uint64_t);
template std::string InputSectionBase::getLocation<ELF64LE>(uint64_t);
template std::string InputSectionBase::getLocation<ELF64BE>(uint64_t);

template void InputSection::writeTo<ELF32LE>(uint8_t *);
template void InputSection::writeTo<ELF32BE>(uint8_t *);
template void InputSection::writeTo<ELF64LE>(uint8_t *);
template void InputSection::writeTo<ELF64BE>(uint8_t *);

template MergeInputSection::MergeInputSection(ObjFile<ELF32LE> &,
                                              const ELF32LE::Shdr &, StringRef);
template MergeInputSection::MergeInputSection(ObjFile<ELF32BE> &,
                                              const ELF32BE::Shdr &, StringRef);
template MergeInputSection::MergeInputSection(ObjFile<ELF64LE> &,
                                              const ELF64LE::Shdr &, StringRef);
template MergeInputSection::MergeInputSection(ObjFile<ELF64BE> &,
                                              const ELF64BE::Shdr &, StringRef);

template EhInputSection::EhInputSection(ObjFile<ELF32LE> &,
                                        const ELF32LE::Shdr &, StringRef);
template EhInputSection::EhInputSection(ObjFile<ELF32BE> &,
                                        const ELF32BE::Shdr &, StringRef);
template EhInputSection::EhInputSection(ObjFile<ELF64LE> &,
                                        const ELF64LE::Shdr &, StringRef);
template EhInputSection::EhInputSection(ObjFile<ELF64BE> &,
                                        const ELF64BE::Shdr &, StringRef);

template void EhInputSection::split<ELF32LE>();
template void EhInputSection::split<ELF32BE>();
template void EhInputSection::split<ELF64LE>();
template void EhInputSection::split<ELF64BE>();
