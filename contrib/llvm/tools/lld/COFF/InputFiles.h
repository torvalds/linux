//===- InputFiles.h ---------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_INPUT_FILES_H
#define LLD_COFF_INPUT_FILES_H

#include "Config.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/StringSaver.h"
#include <memory>
#include <set>
#include <vector>

namespace llvm {
namespace pdb {
class DbiModuleDescriptorBuilder;
}
}

namespace lld {
namespace coff {

std::vector<MemoryBufferRef> getArchiveMembers(llvm::object::Archive *File);

using llvm::COFF::IMAGE_FILE_MACHINE_UNKNOWN;
using llvm::COFF::MachineTypes;
using llvm::object::Archive;
using llvm::object::COFFObjectFile;
using llvm::object::COFFSymbolRef;
using llvm::object::coff_import_header;
using llvm::object::coff_section;

class Chunk;
class Defined;
class DefinedImportData;
class DefinedImportThunk;
class Lazy;
class SectionChunk;
class Symbol;
class Undefined;

// The root class of input files.
class InputFile {
public:
  enum Kind { ArchiveKind, ObjectKind, ImportKind, BitcodeKind };
  Kind kind() const { return FileKind; }
  virtual ~InputFile() {}

  // Returns the filename.
  StringRef getName() const { return MB.getBufferIdentifier(); }

  // Reads a file (the constructor doesn't do that).
  virtual void parse() = 0;

  // Returns the CPU type this file was compiled to.
  virtual MachineTypes getMachineType() { return IMAGE_FILE_MACHINE_UNKNOWN; }

  MemoryBufferRef MB;

  // An archive file name if this file is created from an archive.
  StringRef ParentName;

  // Returns .drectve section contents if exist.
  StringRef getDirectives() { return StringRef(Directives).trim(); }

protected:
  InputFile(Kind K, MemoryBufferRef M) : MB(M), FileKind(K) {}

  std::string Directives;

private:
  const Kind FileKind;
};

// .lib or .a file.
class ArchiveFile : public InputFile {
public:
  explicit ArchiveFile(MemoryBufferRef M);
  static bool classof(const InputFile *F) { return F->kind() == ArchiveKind; }
  void parse() override;

  // Enqueues an archive member load for the given symbol. If we've already
  // enqueued a load for the same archive member, this function does nothing,
  // which ensures that we don't load the same member more than once.
  void addMember(const Archive::Symbol *Sym);

private:
  std::unique_ptr<Archive> File;
  std::string Filename;
  llvm::DenseSet<uint64_t> Seen;
};

// .obj or .o file. This may be a member of an archive file.
class ObjFile : public InputFile {
public:
  explicit ObjFile(MemoryBufferRef M) : InputFile(ObjectKind, M) {}
  static bool classof(const InputFile *F) { return F->kind() == ObjectKind; }
  void parse() override;
  MachineTypes getMachineType() override;
  ArrayRef<Chunk *> getChunks() { return Chunks; }
  ArrayRef<SectionChunk *> getDebugChunks() { return DebugChunks; }
  ArrayRef<SectionChunk *> getSXDataChunks() { return SXDataChunks; }
  ArrayRef<SectionChunk *> getGuardFidChunks() { return GuardFidChunks; }
  ArrayRef<SectionChunk *> getGuardLJmpChunks() { return GuardLJmpChunks; }
  ArrayRef<Symbol *> getSymbols() { return Symbols; }

  // Returns a Symbol object for the SymbolIndex'th symbol in the
  // underlying object file.
  Symbol *getSymbol(uint32_t SymbolIndex) {
    return Symbols[SymbolIndex];
  }

  // Returns the underlying COFF file.
  COFFObjectFile *getCOFFObj() { return COFFObj.get(); }

  // Whether the object was already merged into the final PDB or not
  bool wasProcessedForPDB() const { return !!ModuleDBI; }

  static std::vector<ObjFile *> Instances;

  // Flags in the absolute @feat.00 symbol if it is present. These usually
  // indicate if an object was compiled with certain security features enabled
  // like stack guard, safeseh, /guard:cf, or other things.
  uint32_t Feat00Flags = 0;

  // True if this object file is compatible with SEH.  COFF-specific and
  // x86-only. COFF spec 5.10.1. The .sxdata section.
  bool hasSafeSEH() { return Feat00Flags & 0x1; }

  // True if this file was compiled with /guard:cf.
  bool hasGuardCF() { return Feat00Flags & 0x800; }

  // Pointer to the PDB module descriptor builder. Various debug info records
  // will reference object files by "module index", which is here. Things like
  // source files and section contributions are also recorded here. Will be null
  // if we are not producing a PDB.
  llvm::pdb::DbiModuleDescriptorBuilder *ModuleDBI = nullptr;

  const coff_section *AddrsigSec = nullptr;

  // When using Microsoft precompiled headers, this is the PCH's key.
  // The same key is used by both the precompiled object, and objects using the
  // precompiled object. Any difference indicates out-of-date objects.
  llvm::Optional<uint32_t> PCHSignature;

private:
  void initializeChunks();
  void initializeSymbols();

  SectionChunk *
  readSection(uint32_t SectionNumber,
              const llvm::object::coff_aux_section_definition *Def,
              StringRef LeaderName);

  void readAssociativeDefinition(
      COFFSymbolRef COFFSym,
      const llvm::object::coff_aux_section_definition *Def);

  void readAssociativeDefinition(
      COFFSymbolRef COFFSym,
      const llvm::object::coff_aux_section_definition *Def,
      uint32_t ParentSection);

  void recordPrevailingSymbolForMingw(
      COFFSymbolRef COFFSym,
      llvm::DenseMap<StringRef, uint32_t> &PrevailingSectionMap);

  void maybeAssociateSEHForMingw(
      COFFSymbolRef Sym, const llvm::object::coff_aux_section_definition *Def,
      const llvm::DenseMap<StringRef, uint32_t> &PrevailingSectionMap);

  llvm::Optional<Symbol *>
  createDefined(COFFSymbolRef Sym,
                std::vector<const llvm::object::coff_aux_section_definition *>
                    &ComdatDefs,
                bool &PrevailingComdat);
  Symbol *createRegular(COFFSymbolRef Sym);
  Symbol *createUndefined(COFFSymbolRef Sym);

  std::unique_ptr<COFFObjectFile> COFFObj;

  // List of all chunks defined by this file. This includes both section
  // chunks and non-section chunks for common symbols.
  std::vector<Chunk *> Chunks;

  // CodeView debug info sections.
  std::vector<SectionChunk *> DebugChunks;

  // Chunks containing symbol table indices of exception handlers. Only used for
  // 32-bit x86.
  std::vector<SectionChunk *> SXDataChunks;

  // Chunks containing symbol table indices of address taken symbols and longjmp
  // targets.  These are not linked into the final binary when /guard:cf is set.
  std::vector<SectionChunk *> GuardFidChunks;
  std::vector<SectionChunk *> GuardLJmpChunks;

  // This vector contains the same chunks as Chunks, but they are
  // indexed such that you can get a SectionChunk by section index.
  // Nonexistent section indices are filled with null pointers.
  // (Because section number is 1-based, the first slot is always a
  // null pointer.)
  std::vector<SectionChunk *> SparseChunks;

  // This vector contains a list of all symbols defined or referenced by this
  // file. They are indexed such that you can get a Symbol by symbol
  // index. Nonexistent indices (which are occupied by auxiliary
  // symbols in the real symbol table) are filled with null pointers.
  std::vector<Symbol *> Symbols;
};

// This type represents import library members that contain DLL names
// and symbols exported from the DLLs. See Microsoft PE/COFF spec. 7
// for details about the format.
class ImportFile : public InputFile {
public:
  explicit ImportFile(MemoryBufferRef M) : InputFile(ImportKind, M) {}

  static bool classof(const InputFile *F) { return F->kind() == ImportKind; }

  static std::vector<ImportFile *> Instances;

  Symbol *ImpSym = nullptr;
  Symbol *ThunkSym = nullptr;
  std::string DLLName;

private:
  void parse() override;

public:
  StringRef ExternalName;
  const coff_import_header *Hdr;
  Chunk *Location = nullptr;

  // We want to eliminate dllimported symbols if no one actually refers them.
  // These "Live" bits are used to keep track of which import library members
  // are actually in use.
  //
  // If the Live bit is turned off by MarkLive, Writer will ignore dllimported
  // symbols provided by this import library member. We also track whether the
  // imported symbol is used separately from whether the thunk is used in order
  // to avoid creating unnecessary thunks.
  bool Live = !Config->DoGC;
  bool ThunkLive = !Config->DoGC;
};

// Used for LTO.
class BitcodeFile : public InputFile {
public:
  explicit BitcodeFile(MemoryBufferRef M) : InputFile(BitcodeKind, M) {}
  static bool classof(const InputFile *F) { return F->kind() == BitcodeKind; }
  ArrayRef<Symbol *> getSymbols() { return Symbols; }
  MachineTypes getMachineType() override;
  static std::vector<BitcodeFile *> Instances;
  std::unique_ptr<llvm::lto::InputFile> Obj;

private:
  void parse() override;

  std::vector<Symbol *> Symbols;
};
} // namespace coff

std::string toString(const coff::InputFile *File);
} // namespace lld

#endif
