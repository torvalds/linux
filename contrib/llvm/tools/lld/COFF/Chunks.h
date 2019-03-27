//===- Chunks.h -------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_CHUNKS_H
#define LLD_COFF_CHUNKS_H

#include "Config.h"
#include "InputFiles.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/COFF.h"
#include <utility>
#include <vector>

namespace lld {
namespace coff {

using llvm::COFF::ImportDirectoryTableEntry;
using llvm::object::COFFSymbolRef;
using llvm::object::SectionRef;
using llvm::object::coff_relocation;
using llvm::object::coff_section;

class Baserel;
class Defined;
class DefinedImportData;
class DefinedRegular;
class ObjFile;
class OutputSection;
class RuntimePseudoReloc;
class Symbol;

// Mask for permissions (discardable, writable, readable, executable, etc).
const uint32_t PermMask = 0xFE000000;

// Mask for section types (code, data, bss).
const uint32_t TypeMask = 0x000000E0;

// A Chunk represents a chunk of data that will occupy space in the
// output (if the resolver chose that). It may or may not be backed by
// a section of an input file. It could be linker-created data, or
// doesn't even have actual data (if common or bss).
class Chunk {
public:
  enum Kind { SectionKind, OtherKind };
  Kind kind() const { return ChunkKind; }
  virtual ~Chunk() = default;

  // Returns the size of this chunk (even if this is a common or BSS.)
  virtual size_t getSize() const = 0;

  // Write this chunk to a mmap'ed file, assuming Buf is pointing to
  // beginning of the file. Because this function may use RVA values
  // of other chunks for relocations, you need to set them properly
  // before calling this function.
  virtual void writeTo(uint8_t *Buf) const {}

  // Called by the writer once before assigning addresses and writing
  // the output.
  virtual void readRelocTargets() {}

  // Called if restarting thunk addition.
  virtual void resetRelocTargets() {}

  // Called by the writer after an RVA is assigned, but before calling
  // getSize().
  virtual void finalizeContents() {}

  // The writer sets and uses the addresses.
  uint64_t getRVA() const { return RVA; }
  void setRVA(uint64_t V) { RVA = V; }

  // Returns true if this has non-zero data. BSS chunks return
  // false. If false is returned, the space occupied by this chunk
  // will be filled with zeros.
  virtual bool hasData() const { return true; }

  // Returns readable/writable/executable bits.
  virtual uint32_t getOutputCharacteristics() const { return 0; }

  // Returns the section name if this is a section chunk.
  // It is illegal to call this function on non-section chunks.
  virtual StringRef getSectionName() const {
    llvm_unreachable("unimplemented getSectionName");
  }

  // An output section has pointers to chunks in the section, and each
  // chunk has a back pointer to an output section.
  void setOutputSection(OutputSection *O) { Out = O; }
  OutputSection *getOutputSection() const { return Out; }

  // Windows-specific.
  // Collect all locations that contain absolute addresses for base relocations.
  virtual void getBaserels(std::vector<Baserel> *Res) {}

  // Returns a human-readable name of this chunk. Chunks are unnamed chunks of
  // bytes, so this is used only for logging or debugging.
  virtual StringRef getDebugName() { return ""; }

  // The alignment of this chunk. The writer uses the value.
  uint32_t Alignment = 1;

protected:
  Chunk(Kind K = OtherKind) : ChunkKind(K) {}
  const Kind ChunkKind;

  // The RVA of this chunk in the output. The writer sets a value.
  uint64_t RVA = 0;

  // The output section for this chunk.
  OutputSection *Out = nullptr;

public:
  // The offset from beginning of the output section. The writer sets a value.
  uint64_t OutputSectionOff = 0;

  // Whether this section needs to be kept distinct from other sections during
  // ICF. This is set by the driver using address-significance tables.
  bool KeepUnique = false;
};

// A chunk corresponding a section of an input file.
class SectionChunk final : public Chunk {
  // Identical COMDAT Folding feature accesses section internal data.
  friend class ICF;

public:
  class symbol_iterator : public llvm::iterator_adaptor_base<
                              symbol_iterator, const coff_relocation *,
                              std::random_access_iterator_tag, Symbol *> {
    friend SectionChunk;

    ObjFile *File;

    symbol_iterator(ObjFile *File, const coff_relocation *I)
        : symbol_iterator::iterator_adaptor_base(I), File(File) {}

  public:
    symbol_iterator() = default;

    Symbol *operator*() const { return File->getSymbol(I->SymbolTableIndex); }
  };

  SectionChunk(ObjFile *File, const coff_section *Header);
  static bool classof(const Chunk *C) { return C->kind() == SectionKind; }
  void readRelocTargets() override;
  void resetRelocTargets() override;
  size_t getSize() const override { return Header->SizeOfRawData; }
  ArrayRef<uint8_t> getContents() const;
  void writeTo(uint8_t *Buf) const override;
  bool hasData() const override;
  uint32_t getOutputCharacteristics() const override;
  StringRef getSectionName() const override { return SectionName; }
  void getBaserels(std::vector<Baserel> *Res) override;
  bool isCOMDAT() const;
  void applyRelX64(uint8_t *Off, uint16_t Type, OutputSection *OS, uint64_t S,
                   uint64_t P) const;
  void applyRelX86(uint8_t *Off, uint16_t Type, OutputSection *OS, uint64_t S,
                   uint64_t P) const;
  void applyRelARM(uint8_t *Off, uint16_t Type, OutputSection *OS, uint64_t S,
                   uint64_t P) const;
  void applyRelARM64(uint8_t *Off, uint16_t Type, OutputSection *OS, uint64_t S,
                     uint64_t P) const;

  void getRuntimePseudoRelocs(std::vector<RuntimePseudoReloc> &Res);

  // Called if the garbage collector decides to not include this chunk
  // in a final output. It's supposed to print out a log message to stdout.
  void printDiscardedMessage() const;

  // Adds COMDAT associative sections to this COMDAT section. A chunk
  // and its children are treated as a group by the garbage collector.
  void addAssociative(SectionChunk *Child);

  StringRef getDebugName() override;

  // True if this is a codeview debug info chunk. These will not be laid out in
  // the image. Instead they will end up in the PDB, if one is requested.
  bool isCodeView() const {
    return SectionName == ".debug" || SectionName.startswith(".debug$");
  }

  // True if this is a DWARF debug info or exception handling chunk.
  bool isDWARF() const {
    return SectionName.startswith(".debug_") || SectionName == ".eh_frame";
  }

  // Allow iteration over the bodies of this chunk's relocated symbols.
  llvm::iterator_range<symbol_iterator> symbols() const {
    return llvm::make_range(symbol_iterator(File, Relocs.begin()),
                            symbol_iterator(File, Relocs.end()));
  }

  // Allow iteration over the associated child chunks for this section.
  ArrayRef<SectionChunk *> children() const { return AssocChildren; }

  // The section ID this chunk belongs to in its Obj.
  uint32_t getSectionNumber() const;

  // A pointer pointing to a replacement for this chunk.
  // Initially it points to "this" object. If this chunk is merged
  // with other chunk by ICF, it points to another chunk,
  // and this chunk is considered as dead.
  SectionChunk *Repl;

  // The CRC of the contents as described in the COFF spec 4.5.5.
  // Auxiliary Format 5: Section Definitions. Used for ICF.
  uint32_t Checksum = 0;

  const coff_section *Header;

  // The file that this chunk was created from.
  ObjFile *File;

  // The COMDAT leader symbol if this is a COMDAT chunk.
  DefinedRegular *Sym = nullptr;

  ArrayRef<coff_relocation> Relocs;

  // Used by the garbage collector.
  bool Live;

  // When inserting a thunk, we need to adjust a relocation to point to
  // the thunk instead of the actual original target Symbol.
  std::vector<Symbol *> RelocTargets;

private:
  StringRef SectionName;
  std::vector<SectionChunk *> AssocChildren;

  // Used for ICF (Identical COMDAT Folding)
  void replace(SectionChunk *Other);
  uint32_t Class[2] = {0, 0};
};

// This class is used to implement an lld-specific feature (not implemented in
// MSVC) that minimizes the output size by finding string literals sharing tail
// parts and merging them.
//
// If string tail merging is enabled and a section is identified as containing a
// string literal, it is added to a MergeChunk with an appropriate alignment.
// The MergeChunk then tail merges the strings using the StringTableBuilder
// class and assigns RVAs and section offsets to each of the member chunks based
// on the offsets assigned by the StringTableBuilder.
class MergeChunk : public Chunk {
public:
  MergeChunk(uint32_t Alignment);
  static void addSection(SectionChunk *C);
  void finalizeContents() override;

  uint32_t getOutputCharacteristics() const override;
  StringRef getSectionName() const override { return ".rdata"; }
  size_t getSize() const override;
  void writeTo(uint8_t *Buf) const override;

  static std::map<uint32_t, MergeChunk *> Instances;
  std::vector<SectionChunk *> Sections;

private:
  llvm::StringTableBuilder Builder;
  bool Finalized = false;
};

// A chunk for common symbols. Common chunks don't have actual data.
class CommonChunk : public Chunk {
public:
  CommonChunk(const COFFSymbolRef Sym);
  size_t getSize() const override { return Sym.getValue(); }
  bool hasData() const override { return false; }
  uint32_t getOutputCharacteristics() const override;
  StringRef getSectionName() const override { return ".bss"; }

private:
  const COFFSymbolRef Sym;
};

// A chunk for linker-created strings.
class StringChunk : public Chunk {
public:
  explicit StringChunk(StringRef S) : Str(S) {}
  size_t getSize() const override { return Str.size() + 1; }
  void writeTo(uint8_t *Buf) const override;

private:
  StringRef Str;
};

static const uint8_t ImportThunkX86[] = {
    0xff, 0x25, 0x00, 0x00, 0x00, 0x00, // JMP *0x0
};

static const uint8_t ImportThunkARM[] = {
    0x40, 0xf2, 0x00, 0x0c, // mov.w ip, #0
    0xc0, 0xf2, 0x00, 0x0c, // mov.t ip, #0
    0xdc, 0xf8, 0x00, 0xf0, // ldr.w pc, [ip]
};

static const uint8_t ImportThunkARM64[] = {
    0x10, 0x00, 0x00, 0x90, // adrp x16, #0
    0x10, 0x02, 0x40, 0xf9, // ldr  x16, [x16]
    0x00, 0x02, 0x1f, 0xd6, // br   x16
};

// Windows-specific.
// A chunk for DLL import jump table entry. In a final output, its
// contents will be a JMP instruction to some __imp_ symbol.
class ImportThunkChunkX64 : public Chunk {
public:
  explicit ImportThunkChunkX64(Defined *S);
  size_t getSize() const override { return sizeof(ImportThunkX86); }
  void writeTo(uint8_t *Buf) const override;

private:
  Defined *ImpSymbol;
};

class ImportThunkChunkX86 : public Chunk {
public:
  explicit ImportThunkChunkX86(Defined *S) : ImpSymbol(S) {}
  size_t getSize() const override { return sizeof(ImportThunkX86); }
  void getBaserels(std::vector<Baserel> *Res) override;
  void writeTo(uint8_t *Buf) const override;

private:
  Defined *ImpSymbol;
};

class ImportThunkChunkARM : public Chunk {
public:
  explicit ImportThunkChunkARM(Defined *S) : ImpSymbol(S) {}
  size_t getSize() const override { return sizeof(ImportThunkARM); }
  void getBaserels(std::vector<Baserel> *Res) override;
  void writeTo(uint8_t *Buf) const override;

private:
  Defined *ImpSymbol;
};

class ImportThunkChunkARM64 : public Chunk {
public:
  explicit ImportThunkChunkARM64(Defined *S) : ImpSymbol(S) {}
  size_t getSize() const override { return sizeof(ImportThunkARM64); }
  void writeTo(uint8_t *Buf) const override;

private:
  Defined *ImpSymbol;
};

class RangeExtensionThunkARM : public Chunk {
public:
  explicit RangeExtensionThunkARM(Defined *T) : Target(T) {}
  size_t getSize() const override;
  void writeTo(uint8_t *Buf) const override;

  Defined *Target;
};

class RangeExtensionThunkARM64 : public Chunk {
public:
  explicit RangeExtensionThunkARM64(Defined *T) : Target(T) {}
  size_t getSize() const override;
  void writeTo(uint8_t *Buf) const override;

  Defined *Target;
};

// Windows-specific.
// See comments for DefinedLocalImport class.
class LocalImportChunk : public Chunk {
public:
  explicit LocalImportChunk(Defined *S) : Sym(S) {
    Alignment = Config->Wordsize;
  }
  size_t getSize() const override;
  void getBaserels(std::vector<Baserel> *Res) override;
  void writeTo(uint8_t *Buf) const override;

private:
  Defined *Sym;
};

// Duplicate RVAs are not allowed in RVA tables, so unique symbols by chunk and
// offset into the chunk. Order does not matter as the RVA table will be sorted
// later.
struct ChunkAndOffset {
  Chunk *InputChunk;
  uint32_t Offset;

  struct DenseMapInfo {
    static ChunkAndOffset getEmptyKey() {
      return {llvm::DenseMapInfo<Chunk *>::getEmptyKey(), 0};
    }
    static ChunkAndOffset getTombstoneKey() {
      return {llvm::DenseMapInfo<Chunk *>::getTombstoneKey(), 0};
    }
    static unsigned getHashValue(const ChunkAndOffset &CO) {
      return llvm::DenseMapInfo<std::pair<Chunk *, uint32_t>>::getHashValue(
          {CO.InputChunk, CO.Offset});
    }
    static bool isEqual(const ChunkAndOffset &LHS, const ChunkAndOffset &RHS) {
      return LHS.InputChunk == RHS.InputChunk && LHS.Offset == RHS.Offset;
    }
  };
};

using SymbolRVASet = llvm::DenseSet<ChunkAndOffset>;

// Table which contains symbol RVAs. Used for /safeseh and /guard:cf.
class RVATableChunk : public Chunk {
public:
  explicit RVATableChunk(SymbolRVASet S) : Syms(std::move(S)) {}
  size_t getSize() const override { return Syms.size() * 4; }
  void writeTo(uint8_t *Buf) const override;

private:
  SymbolRVASet Syms;
};

// Windows-specific.
// This class represents a block in .reloc section.
// See the PE/COFF spec 5.6 for details.
class BaserelChunk : public Chunk {
public:
  BaserelChunk(uint32_t Page, Baserel *Begin, Baserel *End);
  size_t getSize() const override { return Data.size(); }
  void writeTo(uint8_t *Buf) const override;

private:
  std::vector<uint8_t> Data;
};

class Baserel {
public:
  Baserel(uint32_t V, uint8_t Ty) : RVA(V), Type(Ty) {}
  explicit Baserel(uint32_t V) : Baserel(V, getDefaultType()) {}
  uint8_t getDefaultType();

  uint32_t RVA;
  uint8_t Type;
};

// This is a placeholder Chunk, to allow attaching a DefinedSynthetic to a
// specific place in a section, without any data. This is used for the MinGW
// specific symbol __RUNTIME_PSEUDO_RELOC_LIST_END__, even though the concept
// of an empty chunk isn't MinGW specific.
class EmptyChunk : public Chunk {
public:
  EmptyChunk() {}
  size_t getSize() const override { return 0; }
  void writeTo(uint8_t *Buf) const override {}
};

// MinGW specific, for the "automatic import of variables from DLLs" feature.
// This provides the table of runtime pseudo relocations, for variable
// references that turned out to need to be imported from a DLL even though
// the reference didn't use the dllimport attribute. The MinGW runtime will
// process this table after loading, before handling control over to user
// code.
class PseudoRelocTableChunk : public Chunk {
public:
  PseudoRelocTableChunk(std::vector<RuntimePseudoReloc> &Relocs)
      : Relocs(std::move(Relocs)) {
    Alignment = 4;
  }
  size_t getSize() const override;
  void writeTo(uint8_t *Buf) const override;

private:
  std::vector<RuntimePseudoReloc> Relocs;
};

// MinGW specific; information about one individual location in the image
// that needs to be fixed up at runtime after loading. This represents
// one individual element in the PseudoRelocTableChunk table.
class RuntimePseudoReloc {
public:
  RuntimePseudoReloc(Defined *Sym, SectionChunk *Target, uint32_t TargetOffset,
                     int Flags)
      : Sym(Sym), Target(Target), TargetOffset(TargetOffset), Flags(Flags) {}

  Defined *Sym;
  SectionChunk *Target;
  uint32_t TargetOffset;
  // The Flags field contains the size of the relocation, in bits. No other
  // flags are currently defined.
  int Flags;
};

// MinGW specific. A Chunk that contains one pointer-sized absolute value.
class AbsolutePointerChunk : public Chunk {
public:
  AbsolutePointerChunk(uint64_t Value) : Value(Value) {
    Alignment = getSize();
  }
  size_t getSize() const override;
  void writeTo(uint8_t *Buf) const override;

private:
  uint64_t Value;
};

void applyMOV32T(uint8_t *Off, uint32_t V);
void applyBranch24T(uint8_t *Off, int32_t V);

void applyArm64Addr(uint8_t *Off, uint64_t S, uint64_t P, int Shift);
void applyArm64Imm(uint8_t *Off, uint64_t Imm, uint32_t RangeLimit);
void applyArm64Branch26(uint8_t *Off, int64_t V);

} // namespace coff
} // namespace lld

namespace llvm {
template <>
struct DenseMapInfo<lld::coff::ChunkAndOffset>
    : lld::coff::ChunkAndOffset::DenseMapInfo {};
}

#endif
