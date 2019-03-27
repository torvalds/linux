//===- InputSection.h -------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_INPUT_SECTION_H
#define LLD_ELF_INPUT_SECTION_H

#include "Config.h"
#include "Relocations.h"
#include "Thunks.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Object/ELF.h"

namespace lld {
namespace elf {

class Symbol;
struct SectionPiece;

class Defined;
class SyntheticSection;
class MergeSyntheticSection;
template <class ELFT> class ObjFile;
class OutputSection;

// This is the base class of all sections that lld handles. Some are sections in
// input files, some are sections in the produced output file and some exist
// just as a convenience for implementing special ways of combining some
// sections.
class SectionBase {
public:
  enum Kind { Regular, EHFrame, Merge, Synthetic, Output };

  Kind kind() const { return (Kind)SectionKind; }

  StringRef Name;

  // This pointer points to the "real" instance of this instance.
  // Usually Repl == this. However, if ICF merges two sections,
  // Repl pointer of one section points to another section. So,
  // if you need to get a pointer to this instance, do not use
  // this but instead this->Repl.
  SectionBase *Repl;

  unsigned SectionKind : 3;

  // The next two bit fields are only used by InputSectionBase, but we
  // put them here so the struct packs better.

  // The garbage collector sets sections' Live bits.
  // If GC is disabled, all sections are considered live by default.
  unsigned Live : 1;

  unsigned Bss : 1;

  // Set for sections that should not be folded by ICF.
  unsigned KeepUnique : 1;

  // These corresponds to the fields in Elf_Shdr.
  uint32_t Alignment;
  uint64_t Flags;
  uint64_t Entsize;
  uint32_t Type;
  uint32_t Link;
  uint32_t Info;

  OutputSection *getOutputSection();
  const OutputSection *getOutputSection() const {
    return const_cast<SectionBase *>(this)->getOutputSection();
  }

  // Translate an offset in the input section to an offset in the output
  // section.
  uint64_t getOffset(uint64_t Offset) const;

  uint64_t getVA(uint64_t Offset = 0) const;

protected:
  SectionBase(Kind SectionKind, StringRef Name, uint64_t Flags,
              uint64_t Entsize, uint64_t Alignment, uint32_t Type,
              uint32_t Info, uint32_t Link)
      : Name(Name), Repl(this), SectionKind(SectionKind), Live(false),
        Bss(false), KeepUnique(false), Alignment(Alignment), Flags(Flags),
        Entsize(Entsize), Type(Type), Link(Link), Info(Info) {}
};

// This corresponds to a section of an input file.
class InputSectionBase : public SectionBase {
public:
  template <class ELFT>
  InputSectionBase(ObjFile<ELFT> &File, const typename ELFT::Shdr &Header,
                   StringRef Name, Kind SectionKind);

  InputSectionBase(InputFile *File, uint64_t Flags, uint32_t Type,
                   uint64_t Entsize, uint32_t Link, uint32_t Info,
                   uint32_t Alignment, ArrayRef<uint8_t> Data, StringRef Name,
                   Kind SectionKind);

  static bool classof(const SectionBase *S) { return S->kind() != Output; }

  // The file which contains this section. Its dynamic type is always
  // ObjFile<ELFT>, but in order to avoid ELFT, we use InputFile as
  // its static type.
  InputFile *File;

  template <class ELFT> ObjFile<ELFT> *getFile() const {
    return cast_or_null<ObjFile<ELFT>>(File);
  }

  ArrayRef<uint8_t> data() const {
    if (UncompressedSize >= 0 && !UncompressedBuf)
      uncompress();
    return RawData;
  }

  uint64_t getOffsetInFile() const;

  // True if this section has already been placed to a linker script
  // output section. This is needed because, in a linker script, you
  // can refer to the same section more than once. For example, in
  // the following linker script,
  //
  //   .foo : { *(.text) }
  //   .bar : { *(.text) }
  //
  // .foo takes all .text sections, and .bar becomes empty. To achieve
  // this, we need to memorize whether a section has been placed or
  // not for each input section.
  bool Assigned = false;

  // Input sections are part of an output section. Special sections
  // like .eh_frame and merge sections are first combined into a
  // synthetic section that is then added to an output section. In all
  // cases this points one level up.
  SectionBase *Parent = nullptr;

  // Relocations that refer to this section.
  const void *FirstRelocation = nullptr;
  unsigned NumRelocations : 31;
  unsigned AreRelocsRela : 1;

  template <class ELFT> ArrayRef<typename ELFT::Rel> rels() const {
    assert(!AreRelocsRela);
    return llvm::makeArrayRef(
        static_cast<const typename ELFT::Rel *>(FirstRelocation),
        NumRelocations);
  }

  template <class ELFT> ArrayRef<typename ELFT::Rela> relas() const {
    assert(AreRelocsRela);
    return llvm::makeArrayRef(
        static_cast<const typename ELFT::Rela *>(FirstRelocation),
        NumRelocations);
  }

  // InputSections that are dependent on us (reverse dependency for GC)
  llvm::TinyPtrVector<InputSection *> DependentSections;

  // Returns the size of this section (even if this is a common or BSS.)
  size_t getSize() const;

  InputSection *getLinkOrderDep() const;

  // Get the function symbol that encloses this offset from within the
  // section.
  template <class ELFT>
  Defined *getEnclosingFunction(uint64_t Offset);

  // Returns a source location string. Used to construct an error message.
  template <class ELFT> std::string getLocation(uint64_t Offset);
  std::string getSrcMsg(const Symbol &Sym, uint64_t Offset);
  std::string getObjMsg(uint64_t Offset);

  // Each section knows how to relocate itself. These functions apply
  // relocations, assuming that Buf points to this section's copy in
  // the mmap'ed output buffer.
  template <class ELFT> void relocate(uint8_t *Buf, uint8_t *BufEnd);
  void relocateAlloc(uint8_t *Buf, uint8_t *BufEnd);

  // The native ELF reloc data type is not very convenient to handle.
  // So we convert ELF reloc records to our own records in Relocations.cpp.
  // This vector contains such "cooked" relocations.
  std::vector<Relocation> Relocations;

  // A function compiled with -fsplit-stack calling a function
  // compiled without -fsplit-stack needs its prologue adjusted. Find
  // such functions and adjust their prologues.  This is very similar
  // to relocation. See https://gcc.gnu.org/wiki/SplitStacks for more
  // information.
  template <typename ELFT>
  void adjustSplitStackFunctionPrologues(uint8_t *Buf, uint8_t *End);


  template <typename T> llvm::ArrayRef<T> getDataAs() const {
    size_t S = data().size();
    assert(S % sizeof(T) == 0);
    return llvm::makeArrayRef<T>((const T *)data().data(), S / sizeof(T));
  }

protected:
  void parseCompressedHeader();
  void uncompress() const;

  mutable ArrayRef<uint8_t> RawData;

  // A pointer that owns uncompressed data if a section is compressed by zlib.
  // Since the feature is not used often, this is usually a nullptr.
  mutable std::unique_ptr<char[]> UncompressedBuf;
  int64_t UncompressedSize = -1;
};

// SectionPiece represents a piece of splittable section contents.
// We allocate a lot of these and binary search on them. This means that they
// have to be as compact as possible, which is why we don't store the size (can
// be found by looking at the next one).
struct SectionPiece {
  SectionPiece(size_t Off, uint32_t Hash, bool Live)
      : InputOff(Off), Hash(Hash), OutputOff(0),
        Live(Live || !Config->GcSections) {}

  uint32_t InputOff;
  uint32_t Hash;
  int64_t OutputOff : 63;
  uint64_t Live : 1;
};

static_assert(sizeof(SectionPiece) == 16, "SectionPiece is too big");

// This corresponds to a SHF_MERGE section of an input file.
class MergeInputSection : public InputSectionBase {
public:
  template <class ELFT>
  MergeInputSection(ObjFile<ELFT> &F, const typename ELFT::Shdr &Header,
                    StringRef Name);
  MergeInputSection(uint64_t Flags, uint32_t Type, uint64_t Entsize,
                    ArrayRef<uint8_t> Data, StringRef Name);

  static bool classof(const SectionBase *S) { return S->kind() == Merge; }
  void splitIntoPieces();

  // Translate an offset in the input section to an offset in the parent
  // MergeSyntheticSection.
  uint64_t getParentOffset(uint64_t Offset) const;

  // Splittable sections are handled as a sequence of data
  // rather than a single large blob of data.
  std::vector<SectionPiece> Pieces;

  // Returns I'th piece's data. This function is very hot when
  // string merging is enabled, so we want to inline.
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  llvm::CachedHashStringRef getData(size_t I) const {
    size_t Begin = Pieces[I].InputOff;
    size_t End =
        (Pieces.size() - 1 == I) ? data().size() : Pieces[I + 1].InputOff;
    return {toStringRef(data().slice(Begin, End - Begin)), Pieces[I].Hash};
  }

  // Returns the SectionPiece at a given input section offset.
  SectionPiece *getSectionPiece(uint64_t Offset);
  const SectionPiece *getSectionPiece(uint64_t Offset) const {
    return const_cast<MergeInputSection *>(this)->getSectionPiece(Offset);
  }

  SyntheticSection *getParent() const;

private:
  void splitStrings(ArrayRef<uint8_t> A, size_t Size);
  void splitNonStrings(ArrayRef<uint8_t> A, size_t Size);
};

struct EhSectionPiece {
  EhSectionPiece(size_t Off, InputSectionBase *Sec, uint32_t Size,
                 unsigned FirstRelocation)
      : InputOff(Off), Sec(Sec), Size(Size), FirstRelocation(FirstRelocation) {}

  ArrayRef<uint8_t> data() {
    return {Sec->data().data() + this->InputOff, Size};
  }

  size_t InputOff;
  ssize_t OutputOff = -1;
  InputSectionBase *Sec;
  uint32_t Size;
  unsigned FirstRelocation;
};

// This corresponds to a .eh_frame section of an input file.
class EhInputSection : public InputSectionBase {
public:
  template <class ELFT>
  EhInputSection(ObjFile<ELFT> &F, const typename ELFT::Shdr &Header,
                 StringRef Name);
  static bool classof(const SectionBase *S) { return S->kind() == EHFrame; }
  template <class ELFT> void split();
  template <class ELFT, class RelTy> void split(ArrayRef<RelTy> Rels);

  // Splittable sections are handled as a sequence of data
  // rather than a single large blob of data.
  std::vector<EhSectionPiece> Pieces;

  SyntheticSection *getParent() const;
};

// This is a section that is added directly to an output section
// instead of needing special combination via a synthetic section. This
// includes all input sections with the exceptions of SHF_MERGE and
// .eh_frame. It also includes the synthetic sections themselves.
class InputSection : public InputSectionBase {
public:
  InputSection(InputFile *F, uint64_t Flags, uint32_t Type, uint32_t Alignment,
               ArrayRef<uint8_t> Data, StringRef Name, Kind K = Regular);
  template <class ELFT>
  InputSection(ObjFile<ELFT> &F, const typename ELFT::Shdr &Header,
               StringRef Name);

  // Write this section to a mmap'ed file, assuming Buf is pointing to
  // beginning of the output section.
  template <class ELFT> void writeTo(uint8_t *Buf);

  uint64_t getOffset(uint64_t Offset) const { return OutSecOff + Offset; }

  OutputSection *getParent() const;

  // This variable has two usages. Initially, it represents an index in the
  // OutputSection's InputSection list, and is used when ordering SHF_LINK_ORDER
  // sections. After assignAddresses is called, it represents the offset from
  // the beginning of the output section this section was assigned to.
  uint64_t OutSecOff = 0;

  static bool classof(const SectionBase *S);

  InputSectionBase *getRelocatedSection() const;

  template <class ELFT, class RelTy>
  void relocateNonAlloc(uint8_t *Buf, llvm::ArrayRef<RelTy> Rels);

  // Used by ICF.
  uint32_t Class[2] = {0, 0};

  // Called by ICF to merge two input sections.
  void replace(InputSection *Other);

  static InputSection Discarded;

private:
  template <class ELFT, class RelTy>
  void copyRelocations(uint8_t *Buf, llvm::ArrayRef<RelTy> Rels);

  template <class ELFT> void copyShtGroup(uint8_t *Buf);
};

// The list of all input sections.
extern std::vector<InputSectionBase *> InputSections;

} // namespace elf

std::string toString(const elf::InputSectionBase *);
} // namespace lld

#endif
