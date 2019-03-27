//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines various types of Symbols.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_SYMBOLS_H
#define LLD_ELF_SYMBOLS_H

#include "InputSection.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Strings.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELF.h"

namespace lld {
namespace elf {
class Symbol;
class InputFile;
} // namespace elf

std::string toString(const elf::Symbol &);
std::string toString(const elf::InputFile *);

namespace elf {

class ArchiveFile;
class BitcodeFile;
class BssSection;
class InputFile;
class LazyObjFile;
template <class ELFT> class ObjFile;
class OutputSection;
template <class ELFT> class SharedFile;

// This is a StringRef-like container that doesn't run strlen().
//
// ELF string tables contain a lot of null-terminated strings. Most of them
// are not necessary for the linker because they are names of local symbols,
// and the linker doesn't use local symbol names for name resolution. So, we
// use this class to represents strings read from string tables.
struct StringRefZ {
  StringRefZ(const char *S) : Data(S), Size(-1) {}
  StringRefZ(StringRef S) : Data(S.data()), Size(S.size()) {}

  const char *Data;
  const uint32_t Size;
};

// The base class for real symbol classes.
class Symbol {
public:
  enum Kind {
    PlaceholderKind,
    DefinedKind,
    SharedKind,
    UndefinedKind,
    LazyArchiveKind,
    LazyObjectKind,
  };

  Kind kind() const { return static_cast<Kind>(SymbolKind); }

  // The file from which this symbol was created.
  InputFile *File;

protected:
  const char *NameData;
  mutable uint32_t NameSize;

public:
  uint32_t DynsymIndex = 0;
  uint32_t GotIndex = -1;
  uint32_t PltIndex = -1;

  uint32_t GlobalDynIndex = -1;

  // This field is a index to the symbol's version definition.
  uint32_t VerdefIndex = -1;

  // Version definition index.
  uint16_t VersionId;

  // An index into the .branch_lt section on PPC64.
  uint16_t PPC64BranchltIndex = -1;

  // Symbol binding. This is not overwritten by replaceSymbol to track
  // changes during resolution. In particular:
  //  - An undefined weak is still weak when it resolves to a shared library.
  //  - An undefined weak will not fetch archive members, but we have to
  //    remember it is weak.
  uint8_t Binding;

  // The following fields have the same meaning as the ELF symbol attributes.
  uint8_t Type;    // symbol type
  uint8_t StOther; // st_other field value

  uint8_t SymbolKind;

  // Symbol visibility. This is the computed minimum visibility of all
  // observed non-DSO symbols.
  unsigned Visibility : 2;

  // True if the symbol was used for linking and thus need to be added to the
  // output file's symbol table. This is true for all symbols except for
  // unreferenced DSO symbols and bitcode symbols that are unreferenced except
  // by other bitcode objects.
  unsigned IsUsedInRegularObj : 1;

  // If this flag is true and the symbol has protected or default visibility, it
  // will appear in .dynsym. This flag is set by interposable DSO symbols in
  // executables, by most symbols in DSOs and executables built with
  // --export-dynamic, and by dynamic lists.
  unsigned ExportDynamic : 1;

  // False if LTO shouldn't inline whatever this symbol points to. If a symbol
  // is overwritten after LTO, LTO shouldn't inline the symbol because it
  // doesn't know the final contents of the symbol.
  unsigned CanInline : 1;

  // True if this symbol is specified by --trace-symbol option.
  unsigned Traced : 1;

  bool includeInDynsym() const;
  uint8_t computeBinding() const;
  bool isWeak() const { return Binding == llvm::ELF::STB_WEAK; }

  bool isUndefined() const { return SymbolKind == UndefinedKind; }
  bool isDefined() const { return SymbolKind == DefinedKind; }
  bool isShared() const { return SymbolKind == SharedKind; }
  bool isLocal() const { return Binding == llvm::ELF::STB_LOCAL; }

  bool isLazy() const {
    return SymbolKind == LazyArchiveKind || SymbolKind == LazyObjectKind;
  }

  // True if this is an undefined weak symbol. This only works once
  // all input files have been added.
  bool isUndefWeak() const {
    // See comment on lazy symbols for details.
    return isWeak() && (isUndefined() || isLazy());
  }

  StringRef getName() const {
    if (NameSize == (uint32_t)-1)
      NameSize = strlen(NameData);
    return {NameData, NameSize};
  }

  void setName(StringRef S) {
    NameData = S.data();
    NameSize = S.size();
  }

  void parseSymbolVersion();

  bool isInGot() const { return GotIndex != -1U; }
  bool isInPlt() const { return PltIndex != -1U; }
  bool isInPPC64Branchlt() const { return PPC64BranchltIndex != 0xffff; }

  uint64_t getVA(int64_t Addend = 0) const;

  uint64_t getGotOffset() const;
  uint64_t getGotVA() const;
  uint64_t getGotPltOffset() const;
  uint64_t getGotPltVA() const;
  uint64_t getPltVA() const;
  uint64_t getPPC64LongBranchTableVA() const;
  uint64_t getPPC64LongBranchOffset() const;
  uint64_t getSize() const;
  OutputSection *getOutputSection() const;

protected:
  Symbol(Kind K, InputFile *File, StringRefZ Name, uint8_t Binding,
         uint8_t StOther, uint8_t Type)
      : File(File), NameData(Name.Data), NameSize(Name.Size), Binding(Binding),
        Type(Type), StOther(StOther), SymbolKind(K), NeedsPltAddr(false),
        IsInIplt(false), IsInIgot(false), IsPreemptible(false),
        Used(!Config->GcSections), NeedsTocRestore(false),
        ScriptDefined(false) {}

public:
  // True the symbol should point to its PLT entry.
  // For SharedSymbol only.
  unsigned NeedsPltAddr : 1;

  // True if this symbol is in the Iplt sub-section of the Plt.
  unsigned IsInIplt : 1;

  // True if this symbol is in the Igot sub-section of the .got.plt or .got.
  unsigned IsInIgot : 1;

  // True if this symbol is preemptible at load time.
  unsigned IsPreemptible : 1;

  // True if an undefined or shared symbol is used from a live section.
  unsigned Used : 1;

  // True if a call to this symbol needs to be followed by a restore of the
  // PPC64 toc pointer.
  unsigned NeedsTocRestore : 1;

  // True if this symbol is defined by a linker script.
  unsigned ScriptDefined : 1;

  bool isSection() const { return Type == llvm::ELF::STT_SECTION; }
  bool isTls() const { return Type == llvm::ELF::STT_TLS; }
  bool isFunc() const { return Type == llvm::ELF::STT_FUNC; }
  bool isGnuIFunc() const { return Type == llvm::ELF::STT_GNU_IFUNC; }
  bool isObject() const { return Type == llvm::ELF::STT_OBJECT; }
  bool isFile() const { return Type == llvm::ELF::STT_FILE; }
};

// Represents a symbol that is defined in the current output file.
class Defined : public Symbol {
public:
  Defined(InputFile *File, StringRefZ Name, uint8_t Binding, uint8_t StOther,
          uint8_t Type, uint64_t Value, uint64_t Size, SectionBase *Section)
      : Symbol(DefinedKind, File, Name, Binding, StOther, Type), Value(Value),
        Size(Size), Section(Section) {}

  static bool classof(const Symbol *S) { return S->isDefined(); }

  uint64_t Value;
  uint64_t Size;
  SectionBase *Section;
};

class Undefined : public Symbol {
public:
  Undefined(InputFile *File, StringRefZ Name, uint8_t Binding, uint8_t StOther,
            uint8_t Type)
      : Symbol(UndefinedKind, File, Name, Binding, StOther, Type) {}

  static bool classof(const Symbol *S) { return S->kind() == UndefinedKind; }
};

class SharedSymbol : public Symbol {
public:
  static bool classof(const Symbol *S) { return S->kind() == SharedKind; }

  SharedSymbol(InputFile &File, StringRef Name, uint8_t Binding,
               uint8_t StOther, uint8_t Type, uint64_t Value, uint64_t Size,
               uint32_t Alignment, uint32_t VerdefIndex)
      : Symbol(SharedKind, &File, Name, Binding, StOther, Type),
        Alignment(Alignment), Value(Value), Size(Size) {
    this->VerdefIndex = VerdefIndex;
    // GNU ifunc is a mechanism to allow user-supplied functions to
    // resolve PLT slot values at load-time. This is contrary to the
    // regular symbol resolution scheme in which symbols are resolved just
    // by name. Using this hook, you can program how symbols are solved
    // for you program. For example, you can make "memcpy" to be resolved
    // to a SSE-enabled version of memcpy only when a machine running the
    // program supports the SSE instruction set.
    //
    // Naturally, such symbols should always be called through their PLT
    // slots. What GNU ifunc symbols point to are resolver functions, and
    // calling them directly doesn't make sense (unless you are writing a
    // loader).
    //
    // For DSO symbols, we always call them through PLT slots anyway.
    // So there's no difference between GNU ifunc and regular function
    // symbols if they are in DSOs. So we can handle GNU_IFUNC as FUNC.
    if (this->Type == llvm::ELF::STT_GNU_IFUNC)
      this->Type = llvm::ELF::STT_FUNC;
  }

  template <class ELFT> SharedFile<ELFT> &getFile() const {
    return *cast<SharedFile<ELFT>>(File);
  }

  uint32_t Alignment;

  uint64_t Value; // st_value
  uint64_t Size;  // st_size
};

// LazyArchive and LazyObject represent a symbols that is not yet in the link,
// but we know where to find it if needed. If the resolver finds both Undefined
// and Lazy for the same name, it will ask the Lazy to load a file.
//
// A special complication is the handling of weak undefined symbols. They should
// not load a file, but we have to remember we have seen both the weak undefined
// and the lazy. We represent that with a lazy symbol with a weak binding. This
// means that code looking for undefined symbols normally also has to take lazy
// symbols into consideration.

// This class represents a symbol defined in an archive file. It is
// created from an archive file header, and it knows how to load an
// object file from an archive to replace itself with a defined
// symbol.
class LazyArchive : public Symbol {
public:
  LazyArchive(InputFile &File, uint8_t Type,
              const llvm::object::Archive::Symbol S)
      : Symbol(LazyArchiveKind, &File, S.getName(), llvm::ELF::STB_GLOBAL,
               llvm::ELF::STV_DEFAULT, Type),
        Sym(S) {}

  static bool classof(const Symbol *S) { return S->kind() == LazyArchiveKind; }

  InputFile *fetch();
  MemoryBufferRef getMemberBuffer();

private:
  const llvm::object::Archive::Symbol Sym;
};

// LazyObject symbols represents symbols in object files between
// --start-lib and --end-lib options.
class LazyObject : public Symbol {
public:
  LazyObject(InputFile &File, uint8_t Type, StringRef Name)
      : Symbol(LazyObjectKind, &File, Name, llvm::ELF::STB_GLOBAL,
               llvm::ELF::STV_DEFAULT, Type) {}

  static bool classof(const Symbol *S) { return S->kind() == LazyObjectKind; }
};

// Some linker-generated symbols need to be created as
// Defined symbols.
struct ElfSym {
  // __bss_start
  static Defined *Bss;

  // etext and _etext
  static Defined *Etext1;
  static Defined *Etext2;

  // edata and _edata
  static Defined *Edata1;
  static Defined *Edata2;

  // end and _end
  static Defined *End1;
  static Defined *End2;

  // The _GLOBAL_OFFSET_TABLE_ symbol is defined by target convention to
  // be at some offset from the base of the .got section, usually 0 or
  // the end of the .got.
  static Defined *GlobalOffsetTable;

  // _gp, _gp_disp and __gnu_local_gp symbols. Only for MIPS.
  static Defined *MipsGp;
  static Defined *MipsGpDisp;
  static Defined *MipsLocalGp;

  // __rel{,a}_iplt_{start,end} symbols.
  static Defined *RelaIpltStart;
  static Defined *RelaIpltEnd;
};

// A buffer class that is large enough to hold any Symbol-derived
// object. We allocate memory using this class and instantiate a symbol
// using the placement new.
union SymbolUnion {
  alignas(Defined) char A[sizeof(Defined)];
  alignas(Undefined) char C[sizeof(Undefined)];
  alignas(SharedSymbol) char D[sizeof(SharedSymbol)];
  alignas(LazyArchive) char E[sizeof(LazyArchive)];
  alignas(LazyObject) char F[sizeof(LazyObject)];
};

void printTraceSymbol(Symbol *Sym);

template <typename T, typename... ArgT>
void replaceSymbol(Symbol *S, ArgT &&... Arg) {
  using llvm::ELF::STT_TLS;

  static_assert(std::is_trivially_destructible<T>(),
                "Symbol types must be trivially destructible");
  static_assert(sizeof(T) <= sizeof(SymbolUnion), "SymbolUnion too small");
  static_assert(alignof(T) <= alignof(SymbolUnion),
                "SymbolUnion not aligned enough");
  assert(static_cast<Symbol *>(static_cast<T *>(nullptr)) == nullptr &&
         "Not a Symbol");

  Symbol Sym = *S;

  new (S) T(std::forward<ArgT>(Arg)...);

  S->VersionId = Sym.VersionId;
  S->Visibility = Sym.Visibility;
  S->IsUsedInRegularObj = Sym.IsUsedInRegularObj;
  S->ExportDynamic = Sym.ExportDynamic;
  S->CanInline = Sym.CanInline;
  S->Traced = Sym.Traced;
  S->ScriptDefined = Sym.ScriptDefined;

  // Symbols representing thread-local variables must be referenced by
  // TLS-aware relocations, and non-TLS symbols must be reference by
  // non-TLS relocations, so there's a clear distinction between TLS
  // and non-TLS symbols. It is an error if the same symbol is defined
  // as a TLS symbol in one file and as a non-TLS symbol in other file.
  bool TlsMismatch = (Sym.Type == STT_TLS && S->Type != STT_TLS) ||
                     (Sym.Type != STT_TLS && S->Type == STT_TLS);

  if (Sym.SymbolKind != Symbol::PlaceholderKind && TlsMismatch && !Sym.isLazy())
    error("TLS attribute mismatch: " + toString(Sym) + "\n>>> defined in " +
          toString(Sym.File) + "\n>>> defined in " + toString(S->File));

  // Print out a log message if --trace-symbol was specified.
  // This is for debugging.
  if (S->Traced)
    printTraceSymbol(S);
}

void maybeWarnUnorderableSymbol(const Symbol *Sym);
} // namespace elf
} // namespace lld

#endif
