//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_SYMBOLS_H
#define LLD_COFF_SYMBOLS_H

#include "Chunks.h"
#include "Config.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include <atomic>
#include <memory>
#include <vector>

namespace lld {
namespace coff {

using llvm::object::Archive;
using llvm::object::COFFSymbolRef;
using llvm::object::coff_import_header;
using llvm::object::coff_symbol_generic;

class ArchiveFile;
class InputFile;
class ObjFile;
class SymbolTable;

// The base class for real symbol classes.
class Symbol {
public:
  enum Kind {
    // The order of these is significant. We start with the regular defined
    // symbols as those are the most prevalent and the zero tag is the cheapest
    // to set. Among the defined kinds, the lower the kind is preferred over
    // the higher kind when testing whether one symbol should take precedence
    // over another.
    DefinedRegularKind = 0,
    DefinedCommonKind,
    DefinedLocalImportKind,
    DefinedImportThunkKind,
    DefinedImportDataKind,
    DefinedAbsoluteKind,
    DefinedSyntheticKind,

    UndefinedKind,
    LazyKind,

    LastDefinedCOFFKind = DefinedCommonKind,
    LastDefinedKind = DefinedSyntheticKind,
  };

  Kind kind() const { return static_cast<Kind>(SymbolKind); }

  // Returns true if this is an external symbol.
  bool isExternal() { return IsExternal; }

  // Returns the symbol name.
  StringRef getName();

  void replaceKeepingName(Symbol *Other, size_t Size);

  // Returns the file from which this symbol was created.
  InputFile *getFile();

  // Indicates that this symbol will be included in the final image. Only valid
  // after calling markLive.
  bool isLive() const;

protected:
  friend SymbolTable;
  explicit Symbol(Kind K, StringRef N = "")
      : SymbolKind(K), IsExternal(true), IsCOMDAT(false),
        WrittenToSymtab(false), PendingArchiveLoad(false), IsGCRoot(false),
        IsRuntimePseudoReloc(false), Name(N) {}

  const unsigned SymbolKind : 8;
  unsigned IsExternal : 1;

  // This bit is used by the \c DefinedRegular subclass.
  unsigned IsCOMDAT : 1;

public:
  // This bit is used by Writer::createSymbolAndStringTable() to prevent
  // symbols from being written to the symbol table more than once.
  unsigned WrittenToSymtab : 1;

  // True if this symbol was referenced by a regular (non-bitcode) object.
  unsigned IsUsedInRegularObj : 1;

  // True if we've seen both a lazy and an undefined symbol with this symbol
  // name, which means that we have enqueued an archive member load and should
  // not load any more archive members to resolve the same symbol.
  unsigned PendingArchiveLoad : 1;

  /// True if we've already added this symbol to the list of GC roots.
  unsigned IsGCRoot : 1;

  unsigned IsRuntimePseudoReloc : 1;

protected:
  StringRef Name;
};

// The base class for any defined symbols, including absolute symbols,
// etc.
class Defined : public Symbol {
public:
  Defined(Kind K, StringRef N) : Symbol(K, N) {}

  static bool classof(const Symbol *S) { return S->kind() <= LastDefinedKind; }

  // Returns the RVA (relative virtual address) of this symbol. The
  // writer sets and uses RVAs.
  uint64_t getRVA();

  // Returns the chunk containing this symbol. Absolute symbols and __ImageBase
  // do not have chunks, so this may return null.
  Chunk *getChunk();
};

// Symbols defined via a COFF object file or bitcode file.  For COFF files, this
// stores a coff_symbol_generic*, and names of internal symbols are lazily
// loaded through that. For bitcode files, Sym is nullptr and the name is stored
// as a StringRef.
class DefinedCOFF : public Defined {
  friend Symbol;

public:
  DefinedCOFF(Kind K, InputFile *F, StringRef N, const coff_symbol_generic *S)
      : Defined(K, N), File(F), Sym(S) {}

  static bool classof(const Symbol *S) {
    return S->kind() <= LastDefinedCOFFKind;
  }

  InputFile *getFile() { return File; }

  COFFSymbolRef getCOFFSymbol();

  InputFile *File;

protected:
  const coff_symbol_generic *Sym;
};

// Regular defined symbols read from object file symbol tables.
class DefinedRegular : public DefinedCOFF {
public:
  DefinedRegular(InputFile *F, StringRef N, bool IsCOMDAT,
                 bool IsExternal = false,
                 const coff_symbol_generic *S = nullptr,
                 SectionChunk *C = nullptr)
      : DefinedCOFF(DefinedRegularKind, F, N, S), Data(C ? &C->Repl : nullptr) {
    this->IsExternal = IsExternal;
    this->IsCOMDAT = IsCOMDAT;
  }

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedRegularKind;
  }

  uint64_t getRVA() const { return (*Data)->getRVA() + Sym->Value; }
  bool isCOMDAT() const { return IsCOMDAT; }
  SectionChunk *getChunk() const { return *Data; }
  uint32_t getValue() const { return Sym->Value; }

  SectionChunk **Data;
};

class DefinedCommon : public DefinedCOFF {
public:
  DefinedCommon(InputFile *F, StringRef N, uint64_t Size,
                const coff_symbol_generic *S = nullptr,
                CommonChunk *C = nullptr)
      : DefinedCOFF(DefinedCommonKind, F, N, S), Data(C), Size(Size) {
    this->IsExternal = true;
  }

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedCommonKind;
  }

  uint64_t getRVA() { return Data->getRVA(); }
  CommonChunk *getChunk() { return Data; }

private:
  friend SymbolTable;
  uint64_t getSize() const { return Size; }
  CommonChunk *Data;
  uint64_t Size;
};

// Absolute symbols.
class DefinedAbsolute : public Defined {
public:
  DefinedAbsolute(StringRef N, COFFSymbolRef S)
      : Defined(DefinedAbsoluteKind, N), VA(S.getValue()) {
    IsExternal = S.isExternal();
  }

  DefinedAbsolute(StringRef N, uint64_t V)
      : Defined(DefinedAbsoluteKind, N), VA(V) {}

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedAbsoluteKind;
  }

  uint64_t getRVA() { return VA - Config->ImageBase; }
  void setVA(uint64_t V) { VA = V; }

  // Section index relocations against absolute symbols resolve to
  // this 16 bit number, and it is the largest valid section index
  // plus one. This variable keeps it.
  static uint16_t NumOutputSections;

private:
  uint64_t VA;
};

// This symbol is used for linker-synthesized symbols like __ImageBase and
// __safe_se_handler_table.
class DefinedSynthetic : public Defined {
public:
  explicit DefinedSynthetic(StringRef Name, Chunk *C)
      : Defined(DefinedSyntheticKind, Name), C(C) {}

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedSyntheticKind;
  }

  // A null chunk indicates that this is __ImageBase. Otherwise, this is some
  // other synthesized chunk, like SEHTableChunk.
  uint32_t getRVA() { return C ? C->getRVA() : 0; }
  Chunk *getChunk() { return C; }

private:
  Chunk *C;
};

// This class represents a symbol defined in an archive file. It is
// created from an archive file header, and it knows how to load an
// object file from an archive to replace itself with a defined
// symbol. If the resolver finds both Undefined and Lazy for
// the same name, it will ask the Lazy to load a file.
class Lazy : public Symbol {
public:
  Lazy(ArchiveFile *F, const Archive::Symbol S)
      : Symbol(LazyKind, S.getName()), File(F), Sym(S) {}

  static bool classof(const Symbol *S) { return S->kind() == LazyKind; }

  ArchiveFile *File;

private:
  friend SymbolTable;

private:
  const Archive::Symbol Sym;
};

// Undefined symbols.
class Undefined : public Symbol {
public:
  explicit Undefined(StringRef N) : Symbol(UndefinedKind, N) {}

  static bool classof(const Symbol *S) { return S->kind() == UndefinedKind; }

  // An undefined symbol can have a fallback symbol which gives an
  // undefined symbol a second chance if it would remain undefined.
  // If it remains undefined, it'll be replaced with whatever the
  // Alias pointer points to.
  Symbol *WeakAlias = nullptr;

  // If this symbol is external weak, try to resolve it to a defined
  // symbol by searching the chain of fallback symbols. Returns the symbol if
  // successful, otherwise returns null.
  Defined *getWeakAlias();
};

// Windows-specific classes.

// This class represents a symbol imported from a DLL. This has two
// names for internal use and external use. The former is used for
// name resolution, and the latter is used for the import descriptor
// table in an output. The former has "__imp_" prefix.
class DefinedImportData : public Defined {
public:
  DefinedImportData(StringRef N, ImportFile *F)
      : Defined(DefinedImportDataKind, N), File(F) {
  }

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedImportDataKind;
  }

  uint64_t getRVA() { return File->Location->getRVA(); }
  Chunk *getChunk() { return File->Location; }
  void setLocation(Chunk *AddressTable) { File->Location = AddressTable; }

  StringRef getDLLName() { return File->DLLName; }
  StringRef getExternalName() { return File->ExternalName; }
  uint16_t getOrdinal() { return File->Hdr->OrdinalHint; }

  ImportFile *File;
};

// This class represents a symbol for a jump table entry which jumps
// to a function in a DLL. Linker are supposed to create such symbols
// without "__imp_" prefix for all function symbols exported from
// DLLs, so that you can call DLL functions as regular functions with
// a regular name. A function pointer is given as a DefinedImportData.
class DefinedImportThunk : public Defined {
public:
  DefinedImportThunk(StringRef Name, DefinedImportData *S, uint16_t Machine);

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedImportThunkKind;
  }

  uint64_t getRVA() { return Data->getRVA(); }
  Chunk *getChunk() { return Data; }

  DefinedImportData *WrappedSym;

private:
  Chunk *Data;
};

// If you have a symbol "foo" in your object file, a symbol name
// "__imp_foo" becomes automatically available as a pointer to "foo".
// This class is for such automatically-created symbols.
// Yes, this is an odd feature. We didn't intend to implement that.
// This is here just for compatibility with MSVC.
class DefinedLocalImport : public Defined {
public:
  DefinedLocalImport(StringRef N, Defined *S)
      : Defined(DefinedLocalImportKind, N), Data(make<LocalImportChunk>(S)) {}

  static bool classof(const Symbol *S) {
    return S->kind() == DefinedLocalImportKind;
  }

  uint64_t getRVA() { return Data->getRVA(); }
  Chunk *getChunk() { return Data; }

private:
  LocalImportChunk *Data;
};

inline uint64_t Defined::getRVA() {
  switch (kind()) {
  case DefinedAbsoluteKind:
    return cast<DefinedAbsolute>(this)->getRVA();
  case DefinedSyntheticKind:
    return cast<DefinedSynthetic>(this)->getRVA();
  case DefinedImportDataKind:
    return cast<DefinedImportData>(this)->getRVA();
  case DefinedImportThunkKind:
    return cast<DefinedImportThunk>(this)->getRVA();
  case DefinedLocalImportKind:
    return cast<DefinedLocalImport>(this)->getRVA();
  case DefinedCommonKind:
    return cast<DefinedCommon>(this)->getRVA();
  case DefinedRegularKind:
    return cast<DefinedRegular>(this)->getRVA();
  case LazyKind:
  case UndefinedKind:
    llvm_unreachable("Cannot get the address for an undefined symbol.");
  }
  llvm_unreachable("unknown symbol kind");
}

inline Chunk *Defined::getChunk() {
  switch (kind()) {
  case DefinedRegularKind:
    return cast<DefinedRegular>(this)->getChunk();
  case DefinedAbsoluteKind:
    return nullptr;
  case DefinedSyntheticKind:
    return cast<DefinedSynthetic>(this)->getChunk();
  case DefinedImportDataKind:
    return cast<DefinedImportData>(this)->getChunk();
  case DefinedImportThunkKind:
    return cast<DefinedImportThunk>(this)->getChunk();
  case DefinedLocalImportKind:
    return cast<DefinedLocalImport>(this)->getChunk();
  case DefinedCommonKind:
    return cast<DefinedCommon>(this)->getChunk();
  case LazyKind:
  case UndefinedKind:
    llvm_unreachable("Cannot get the chunk of an undefined symbol.");
  }
  llvm_unreachable("unknown symbol kind");
}

// A buffer class that is large enough to hold any Symbol-derived
// object. We allocate memory using this class and instantiate a symbol
// using the placement new.
union SymbolUnion {
  alignas(DefinedRegular) char A[sizeof(DefinedRegular)];
  alignas(DefinedCommon) char B[sizeof(DefinedCommon)];
  alignas(DefinedAbsolute) char C[sizeof(DefinedAbsolute)];
  alignas(DefinedSynthetic) char D[sizeof(DefinedSynthetic)];
  alignas(Lazy) char E[sizeof(Lazy)];
  alignas(Undefined) char F[sizeof(Undefined)];
  alignas(DefinedImportData) char G[sizeof(DefinedImportData)];
  alignas(DefinedImportThunk) char H[sizeof(DefinedImportThunk)];
  alignas(DefinedLocalImport) char I[sizeof(DefinedLocalImport)];
};

template <typename T, typename... ArgT>
void replaceSymbol(Symbol *S, ArgT &&... Arg) {
  static_assert(std::is_trivially_destructible<T>(),
                "Symbol types must be trivially destructible");
  static_assert(sizeof(T) <= sizeof(SymbolUnion), "Symbol too small");
  static_assert(alignof(T) <= alignof(SymbolUnion),
                "SymbolUnion not aligned enough");
  assert(static_cast<Symbol *>(static_cast<T *>(nullptr)) == nullptr &&
         "Not a Symbol");
  new (S) T(std::forward<ArgT>(Arg)...);
}
} // namespace coff

std::string toString(coff::Symbol &B);
} // namespace lld

#endif
