//===- Relocations.cpp ----------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains platform-independent functions to process relocations.
// I'll describe the overview of this file here.
//
// Simple relocations are easy to handle for the linker. For example,
// for R_X86_64_PC64 relocs, the linker just has to fix up locations
// with the relative offsets to the target symbols. It would just be
// reading records from relocation sections and applying them to output.
//
// But not all relocations are that easy to handle. For example, for
// R_386_GOTOFF relocs, the linker has to create new GOT entries for
// symbols if they don't exist, and fix up locations with GOT entry
// offsets from the beginning of GOT section. So there is more than
// fixing addresses in relocation processing.
//
// ELF defines a large number of complex relocations.
//
// The functions in this file analyze relocations and do whatever needs
// to be done. It includes, but not limited to, the following.
//
//  - create GOT/PLT entries
//  - create new relocations in .dynsym to let the dynamic linker resolve
//    them at runtime (since ELF supports dynamic linking, not all
//    relocations can be resolved at link-time)
//  - create COPY relocs and reserve space in .bss
//  - replace expensive relocs (in terms of runtime cost) with cheap ones
//  - error out infeasible combinations such as PIC and non-relative relocs
//
// Note that the functions in this file don't actually apply relocations
// because it doesn't know about the output file nor the output file buffer.
// It instead stores Relocation objects to InputSection's Relocations
// vector to let it apply later in InputSection::writeTo.
//
//===----------------------------------------------------------------------===//

#include "Relocations.h"
#include "Config.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Thunks.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support::endian;

using namespace lld;
using namespace lld::elf;

static Optional<std::string> getLinkerScriptLocation(const Symbol &Sym) {
  for (BaseCommand *Base : Script->SectionCommands)
    if (auto *Cmd = dyn_cast<SymbolAssignment>(Base))
      if (Cmd->Sym == &Sym)
        return Cmd->Location;
  return None;
}

// Construct a message in the following format.
//
// >>> defined in /home/alice/src/foo.o
// >>> referenced by bar.c:12 (/home/alice/src/bar.c:12)
// >>>               /home/alice/src/bar.o:(.text+0x1)
static std::string getLocation(InputSectionBase &S, const Symbol &Sym,
                               uint64_t Off) {
  std::string Msg = "\n>>> defined in ";
  if (Sym.File)
    Msg += toString(Sym.File);
  else if (Optional<std::string> Loc = getLinkerScriptLocation(Sym))
    Msg += *Loc;

  Msg += "\n>>> referenced by ";
  std::string Src = S.getSrcMsg(Sym, Off);
  if (!Src.empty())
    Msg += Src + "\n>>>               ";
  return Msg + S.getObjMsg(Off);
}

// This function is similar to the `handleTlsRelocation`. MIPS does not
// support any relaxations for TLS relocations so by factoring out MIPS
// handling in to the separate function we can simplify the code and do not
// pollute other `handleTlsRelocation` by MIPS `ifs` statements.
// Mips has a custom MipsGotSection that handles the writing of GOT entries
// without dynamic relocations.
static unsigned handleMipsTlsRelocation(RelType Type, Symbol &Sym,
                                        InputSectionBase &C, uint64_t Offset,
                                        int64_t Addend, RelExpr Expr) {
  if (Expr == R_MIPS_TLSLD) {
    In.MipsGot->addTlsIndex(*C.File);
    C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }
  if (Expr == R_MIPS_TLSGD) {
    In.MipsGot->addDynTlsEntry(*C.File, Sym);
    C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }
  return 0;
}

// This function is similar to the `handleMipsTlsRelocation`. ARM also does not
// support any relaxations for TLS relocations. ARM is logically similar to Mips
// in how it handles TLS, but Mips uses its own custom GOT which handles some
// of the cases that ARM uses GOT relocations for.
//
// We look for TLS global dynamic and local dynamic relocations, these may
// require the generation of a pair of GOT entries that have associated
// dynamic relocations. When the results of the dynamic relocations can be
// resolved at static link time we do so. This is necessary for static linking
// as there will be no dynamic loader to resolve them at load-time.
//
// The pair of GOT entries created are of the form
// GOT[e0] Module Index (Used to find pointer to TLS block at run-time)
// GOT[e1] Offset of symbol in TLS block
template <class ELFT>
static unsigned handleARMTlsRelocation(RelType Type, Symbol &Sym,
                                       InputSectionBase &C, uint64_t Offset,
                                       int64_t Addend, RelExpr Expr) {
  // The Dynamic TLS Module Index Relocation for a symbol defined in an
  // executable is always 1. If the target Symbol is not preemptible then
  // we know the offset into the TLS block at static link time.
  bool NeedDynId = Sym.IsPreemptible || Config->Shared;
  bool NeedDynOff = Sym.IsPreemptible;

  auto AddTlsReloc = [&](uint64_t Off, RelType Type, Symbol *Dest, bool Dyn) {
    if (Dyn)
      In.RelaDyn->addReloc(Type, In.Got, Off, Dest);
    else
      In.Got->Relocations.push_back({R_ABS, Type, Off, 0, Dest});
  };

  // Local Dynamic is for access to module local TLS variables, while still
  // being suitable for being dynamically loaded via dlopen.
  // GOT[e0] is the module index, with a special value of 0 for the current
  // module. GOT[e1] is unused. There only needs to be one module index entry.
  if (Expr == R_TLSLD_PC && In.Got->addTlsIndex()) {
    AddTlsReloc(In.Got->getTlsIndexOff(), Target->TlsModuleIndexRel,
                NeedDynId ? nullptr : &Sym, NeedDynId);
    C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }

  // Global Dynamic is the most general purpose access model. When we know
  // the module index and offset of symbol in TLS block we can fill these in
  // using static GOT relocations.
  if (Expr == R_TLSGD_PC) {
    if (In.Got->addDynTlsEntry(Sym)) {
      uint64_t Off = In.Got->getGlobalDynOffset(Sym);
      AddTlsReloc(Off, Target->TlsModuleIndexRel, &Sym, NeedDynId);
      AddTlsReloc(Off + Config->Wordsize, Target->TlsOffsetRel, &Sym,
                  NeedDynOff);
    }
    C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }
  return 0;
}

// Returns the number of relocations processed.
template <class ELFT>
static unsigned
handleTlsRelocation(RelType Type, Symbol &Sym, InputSectionBase &C,
                    typename ELFT::uint Offset, int64_t Addend, RelExpr Expr) {
  if (!Sym.isTls())
    return 0;

  if (Config->EMachine == EM_ARM)
    return handleARMTlsRelocation<ELFT>(Type, Sym, C, Offset, Addend, Expr);
  if (Config->EMachine == EM_MIPS)
    return handleMipsTlsRelocation(Type, Sym, C, Offset, Addend, Expr);

  if (isRelExprOneOf<R_TLSDESC, R_AARCH64_TLSDESC_PAGE, R_TLSDESC_CALL>(Expr) &&
      Config->Shared) {
    if (In.Got->addDynTlsEntry(Sym)) {
      uint64_t Off = In.Got->getGlobalDynOffset(Sym);
      In.RelaDyn->addReloc(
          {Target->TlsDescRel, In.Got, Off, !Sym.IsPreemptible, &Sym, 0});
    }
    if (Expr != R_TLSDESC_CALL)
      C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }

  if (isRelExprOneOf<R_TLSLD_GOT, R_TLSLD_GOT_FROM_END, R_TLSLD_PC,
                     R_TLSLD_HINT>(Expr)) {
    // Local-Dynamic relocs can be relaxed to Local-Exec.
    if (!Config->Shared) {
      C.Relocations.push_back(
          {Target->adjustRelaxExpr(Type, nullptr, R_RELAX_TLS_LD_TO_LE), Type,
           Offset, Addend, &Sym});
      return Target->TlsGdRelaxSkip;
    }
    if (Expr == R_TLSLD_HINT)
      return 1;
    if (In.Got->addTlsIndex())
      In.RelaDyn->addReloc(Target->TlsModuleIndexRel, In.Got,
                           In.Got->getTlsIndexOff(), nullptr);
    C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }

  // Local-Dynamic relocs can be relaxed to Local-Exec.
  if (Expr == R_ABS && !Config->Shared) {
    C.Relocations.push_back(
        {Target->adjustRelaxExpr(Type, nullptr, R_RELAX_TLS_LD_TO_LE), Type,
         Offset, Addend, &Sym});
    return 1;
  }

  // Local-Dynamic sequence where offset of tls variable relative to dynamic
  // thread pointer is stored in the got.
  if (Expr == R_TLSLD_GOT_OFF) {
    // Local-Dynamic relocs can be relaxed to local-exec
    if (!Config->Shared) {
      C.Relocations.push_back({R_RELAX_TLS_LD_TO_LE, Type, Offset, Addend, &Sym});
      return 1;
    }
    if (!Sym.isInGot()) {
      In.Got->addEntry(Sym);
      uint64_t Off = Sym.getGotOffset();
      In.Got->Relocations.push_back(
          {R_ABS, Target->TlsOffsetRel, Off, 0, &Sym});
    }
    C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return 1;
  }

  if (isRelExprOneOf<R_TLSDESC, R_AARCH64_TLSDESC_PAGE, R_TLSDESC_CALL,
                     R_TLSGD_GOT, R_TLSGD_GOT_FROM_END, R_TLSGD_PC>(Expr)) {
    if (Config->Shared) {
      if (In.Got->addDynTlsEntry(Sym)) {
        uint64_t Off = In.Got->getGlobalDynOffset(Sym);
        In.RelaDyn->addReloc(Target->TlsModuleIndexRel, In.Got, Off, &Sym);

        // If the symbol is preemptible we need the dynamic linker to write
        // the offset too.
        uint64_t OffsetOff = Off + Config->Wordsize;
        if (Sym.IsPreemptible)
          In.RelaDyn->addReloc(Target->TlsOffsetRel, In.Got, OffsetOff, &Sym);
        else
          In.Got->Relocations.push_back(
              {R_ABS, Target->TlsOffsetRel, OffsetOff, 0, &Sym});
      }
      C.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
      return 1;
    }

    // Global-Dynamic relocs can be relaxed to Initial-Exec or Local-Exec
    // depending on the symbol being locally defined or not.
    if (Sym.IsPreemptible) {
      C.Relocations.push_back(
          {Target->adjustRelaxExpr(Type, nullptr, R_RELAX_TLS_GD_TO_IE), Type,
           Offset, Addend, &Sym});
      if (!Sym.isInGot()) {
        In.Got->addEntry(Sym);
        In.RelaDyn->addReloc(Target->TlsGotRel, In.Got, Sym.getGotOffset(),
                             &Sym);
      }
    } else {
      C.Relocations.push_back(
          {Target->adjustRelaxExpr(Type, nullptr, R_RELAX_TLS_GD_TO_LE), Type,
           Offset, Addend, &Sym});
    }
    return Target->TlsGdRelaxSkip;
  }

  // Initial-Exec relocs can be relaxed to Local-Exec if the symbol is locally
  // defined.
  if (isRelExprOneOf<R_GOT, R_GOT_FROM_END, R_GOT_PC, R_AARCH64_GOT_PAGE_PC,
                     R_GOT_OFF, R_TLSIE_HINT>(Expr) &&
      !Config->Shared && !Sym.IsPreemptible) {
    C.Relocations.push_back({R_RELAX_TLS_IE_TO_LE, Type, Offset, Addend, &Sym});
    return 1;
  }

  if (Expr == R_TLSIE_HINT)
    return 1;
  return 0;
}

static RelType getMipsPairType(RelType Type, bool IsLocal) {
  switch (Type) {
  case R_MIPS_HI16:
    return R_MIPS_LO16;
  case R_MIPS_GOT16:
    // In case of global symbol, the R_MIPS_GOT16 relocation does not
    // have a pair. Each global symbol has a unique entry in the GOT
    // and a corresponding instruction with help of the R_MIPS_GOT16
    // relocation loads an address of the symbol. In case of local
    // symbol, the R_MIPS_GOT16 relocation creates a GOT entry to hold
    // the high 16 bits of the symbol's value. A paired R_MIPS_LO16
    // relocations handle low 16 bits of the address. That allows
    // to allocate only one GOT entry for every 64 KBytes of local data.
    return IsLocal ? R_MIPS_LO16 : R_MIPS_NONE;
  case R_MICROMIPS_GOT16:
    return IsLocal ? R_MICROMIPS_LO16 : R_MIPS_NONE;
  case R_MIPS_PCHI16:
    return R_MIPS_PCLO16;
  case R_MICROMIPS_HI16:
    return R_MICROMIPS_LO16;
  default:
    return R_MIPS_NONE;
  }
}

// True if non-preemptable symbol always has the same value regardless of where
// the DSO is loaded.
static bool isAbsolute(const Symbol &Sym) {
  if (Sym.isUndefWeak())
    return true;
  if (const auto *DR = dyn_cast<Defined>(&Sym))
    return DR->Section == nullptr; // Absolute symbol.
  return false;
}

static bool isAbsoluteValue(const Symbol &Sym) {
  return isAbsolute(Sym) || Sym.isTls();
}

// Returns true if Expr refers a PLT entry.
static bool needsPlt(RelExpr Expr) {
  return isRelExprOneOf<R_PLT_PC, R_PPC_CALL_PLT, R_PLT, R_AARCH64_PLT_PAGE_PC,
                        R_GOT_PLT, R_AARCH64_GOT_PAGE_PC_PLT>(Expr);
}

// Returns true if Expr refers a GOT entry. Note that this function
// returns false for TLS variables even though they need GOT, because
// TLS variables uses GOT differently than the regular variables.
static bool needsGot(RelExpr Expr) {
  return isRelExprOneOf<R_GOT, R_GOT_OFF, R_HEXAGON_GOT, R_MIPS_GOT_LOCAL_PAGE,
                        R_MIPS_GOT_OFF, R_MIPS_GOT_OFF32, R_AARCH64_GOT_PAGE_PC,
                        R_AARCH64_GOT_PAGE_PC_PLT, R_GOT_PC, R_GOT_FROM_END,
                        R_GOT_PLT>(Expr);
}

// True if this expression is of the form Sym - X, where X is a position in the
// file (PC, or GOT for example).
static bool isRelExpr(RelExpr Expr) {
  return isRelExprOneOf<R_PC, R_GOTREL, R_GOTREL_FROM_END, R_MIPS_GOTREL,
                        R_PPC_CALL, R_PPC_CALL_PLT, R_AARCH64_PAGE_PC,
                        R_AARCH64_PLT_PAGE_PC, R_RELAX_GOT_PC>(Expr);
}

// Returns true if a given relocation can be computed at link-time.
//
// For instance, we know the offset from a relocation to its target at
// link-time if the relocation is PC-relative and refers a
// non-interposable function in the same executable. This function
// will return true for such relocation.
//
// If this function returns false, that means we need to emit a
// dynamic relocation so that the relocation will be fixed at load-time.
static bool isStaticLinkTimeConstant(RelExpr E, RelType Type, const Symbol &Sym,
                                     InputSectionBase &S, uint64_t RelOff) {
  // These expressions always compute a constant
  if (isRelExprOneOf<R_GOT_FROM_END, R_GOT_OFF, R_HEXAGON_GOT, R_TLSLD_GOT_OFF,
                     R_MIPS_GOT_LOCAL_PAGE, R_MIPS_GOTREL, R_MIPS_GOT_OFF,
                     R_MIPS_GOT_OFF32, R_MIPS_GOT_GP_PC, R_MIPS_TLSGD,
                     R_AARCH64_GOT_PAGE_PC, R_AARCH64_GOT_PAGE_PC_PLT, R_GOT_PC,
                     R_GOTONLY_PC, R_GOTONLY_PC_FROM_END, R_PLT_PC, R_TLSGD_GOT,
                     R_TLSGD_GOT_FROM_END, R_TLSGD_PC, R_PPC_CALL_PLT,
                     R_TLSDESC_CALL, R_AARCH64_TLSDESC_PAGE, R_HINT,
                     R_TLSLD_HINT, R_TLSIE_HINT>(E))
    return true;

  // The computation involves output from the ifunc resolver.
  if (Sym.isGnuIFunc() && Config->ZIfuncnoplt)
    return false;

  // These never do, except if the entire file is position dependent or if
  // only the low bits are used.
  if (E == R_GOT || E == R_GOT_PLT || E == R_PLT || E == R_TLSDESC)
    return Target->usesOnlyLowPageBits(Type) || !Config->Pic;

  if (Sym.IsPreemptible)
    return false;
  if (!Config->Pic)
    return true;

  // The size of a non preemptible symbol is a constant.
  if (E == R_SIZE)
    return true;

  // For the target and the relocation, we want to know if they are
  // absolute or relative.
  bool AbsVal = isAbsoluteValue(Sym);
  bool RelE = isRelExpr(E);
  if (AbsVal && !RelE)
    return true;
  if (!AbsVal && RelE)
    return true;
  if (!AbsVal && !RelE)
    return Target->usesOnlyLowPageBits(Type);

  // Relative relocation to an absolute value. This is normally unrepresentable,
  // but if the relocation refers to a weak undefined symbol, we allow it to
  // resolve to the image base. This is a little strange, but it allows us to
  // link function calls to such symbols. Normally such a call will be guarded
  // with a comparison, which will load a zero from the GOT.
  // Another special case is MIPS _gp_disp symbol which represents offset
  // between start of a function and '_gp' value and defined as absolute just
  // to simplify the code.
  assert(AbsVal && RelE);
  if (Sym.isUndefWeak())
    return true;

  error("relocation " + toString(Type) + " cannot refer to absolute symbol: " +
        toString(Sym) + getLocation(S, Sym, RelOff));
  return true;
}

static RelExpr toPlt(RelExpr Expr) {
  switch (Expr) {
  case R_PPC_CALL:
    return R_PPC_CALL_PLT;
  case R_PC:
    return R_PLT_PC;
  case R_AARCH64_PAGE_PC:
    return R_AARCH64_PLT_PAGE_PC;
  case R_AARCH64_GOT_PAGE_PC:
    return R_AARCH64_GOT_PAGE_PC_PLT;
  case R_ABS:
    return R_PLT;
  case R_GOT:
    return R_GOT_PLT;
  default:
    return Expr;
  }
}

static RelExpr fromPlt(RelExpr Expr) {
  // We decided not to use a plt. Optimize a reference to the plt to a
  // reference to the symbol itself.
  switch (Expr) {
  case R_PLT_PC:
    return R_PC;
  case R_PPC_CALL_PLT:
    return R_PPC_CALL;
  case R_PLT:
    return R_ABS;
  default:
    return Expr;
  }
}

// Returns true if a given shared symbol is in a read-only segment in a DSO.
template <class ELFT> static bool isReadOnly(SharedSymbol &SS) {
  typedef typename ELFT::Phdr Elf_Phdr;

  // Determine if the symbol is read-only by scanning the DSO's program headers.
  const SharedFile<ELFT> &File = SS.getFile<ELFT>();
  for (const Elf_Phdr &Phdr : check(File.getObj().program_headers()))
    if ((Phdr.p_type == ELF::PT_LOAD || Phdr.p_type == ELF::PT_GNU_RELRO) &&
        !(Phdr.p_flags & ELF::PF_W) && SS.Value >= Phdr.p_vaddr &&
        SS.Value < Phdr.p_vaddr + Phdr.p_memsz)
      return true;
  return false;
}

// Returns symbols at the same offset as a given symbol, including SS itself.
//
// If two or more symbols are at the same offset, and at least one of
// them are copied by a copy relocation, all of them need to be copied.
// Otherwise, they would refer to different places at runtime.
template <class ELFT>
static SmallSet<SharedSymbol *, 4> getSymbolsAt(SharedSymbol &SS) {
  typedef typename ELFT::Sym Elf_Sym;

  SharedFile<ELFT> &File = SS.getFile<ELFT>();

  SmallSet<SharedSymbol *, 4> Ret;
  for (const Elf_Sym &S : File.getGlobalELFSyms()) {
    if (S.st_shndx == SHN_UNDEF || S.st_shndx == SHN_ABS ||
        S.getType() == STT_TLS || S.st_value != SS.Value)
      continue;
    StringRef Name = check(S.getName(File.getStringTable()));
    Symbol *Sym = Symtab->find(Name);
    if (auto *Alias = dyn_cast_or_null<SharedSymbol>(Sym))
      Ret.insert(Alias);
  }
  return Ret;
}

// When a symbol is copy relocated or we create a canonical plt entry, it is
// effectively a defined symbol. In the case of copy relocation the symbol is
// in .bss and in the case of a canonical plt entry it is in .plt. This function
// replaces the existing symbol with a Defined pointing to the appropriate
// location.
static void replaceWithDefined(Symbol &Sym, SectionBase *Sec, uint64_t Value,
                               uint64_t Size) {
  Symbol Old = Sym;
  replaceSymbol<Defined>(&Sym, Sym.File, Sym.getName(), Sym.Binding,
                         Sym.StOther, Sym.Type, Value, Size, Sec);
  Sym.PltIndex = Old.PltIndex;
  Sym.GotIndex = Old.GotIndex;
  Sym.VerdefIndex = Old.VerdefIndex;
  Sym.PPC64BranchltIndex = Old.PPC64BranchltIndex;
  Sym.IsPreemptible = true;
  Sym.ExportDynamic = true;
  Sym.IsUsedInRegularObj = true;
  Sym.Used = true;
}

// Reserve space in .bss or .bss.rel.ro for copy relocation.
//
// The copy relocation is pretty much a hack. If you use a copy relocation
// in your program, not only the symbol name but the symbol's size, RW/RO
// bit and alignment become part of the ABI. In addition to that, if the
// symbol has aliases, the aliases become part of the ABI. That's subtle,
// but if you violate that implicit ABI, that can cause very counter-
// intuitive consequences.
//
// So, what is the copy relocation? It's for linking non-position
// independent code to DSOs. In an ideal world, all references to data
// exported by DSOs should go indirectly through GOT. But if object files
// are compiled as non-PIC, all data references are direct. There is no
// way for the linker to transform the code to use GOT, as machine
// instructions are already set in stone in object files. This is where
// the copy relocation takes a role.
//
// A copy relocation instructs the dynamic linker to copy data from a DSO
// to a specified address (which is usually in .bss) at load-time. If the
// static linker (that's us) finds a direct data reference to a DSO
// symbol, it creates a copy relocation, so that the symbol can be
// resolved as if it were in .bss rather than in a DSO.
//
// As you can see in this function, we create a copy relocation for the
// dynamic linker, and the relocation contains not only symbol name but
// various other informtion about the symbol. So, such attributes become a
// part of the ABI.
//
// Note for application developers: I can give you a piece of advice if
// you are writing a shared library. You probably should export only
// functions from your library. You shouldn't export variables.
//
// As an example what can happen when you export variables without knowing
// the semantics of copy relocations, assume that you have an exported
// variable of type T. It is an ABI-breaking change to add new members at
// end of T even though doing that doesn't change the layout of the
// existing members. That's because the space for the new members are not
// reserved in .bss unless you recompile the main program. That means they
// are likely to overlap with other data that happens to be laid out next
// to the variable in .bss. This kind of issue is sometimes very hard to
// debug. What's a solution? Instead of exporting a varaible V from a DSO,
// define an accessor getV().
template <class ELFT> static void addCopyRelSymbol(SharedSymbol &SS) {
  // Copy relocation against zero-sized symbol doesn't make sense.
  uint64_t SymSize = SS.getSize();
  if (SymSize == 0 || SS.Alignment == 0)
    fatal("cannot create a copy relocation for symbol " + toString(SS));

  // See if this symbol is in a read-only segment. If so, preserve the symbol's
  // memory protection by reserving space in the .bss.rel.ro section.
  bool IsReadOnly = isReadOnly<ELFT>(SS);
  BssSection *Sec = make<BssSection>(IsReadOnly ? ".bss.rel.ro" : ".bss",
                                     SymSize, SS.Alignment);
  if (IsReadOnly)
    In.BssRelRo->getParent()->addSection(Sec);
  else
    In.Bss->getParent()->addSection(Sec);

  // Look through the DSO's dynamic symbol table for aliases and create a
  // dynamic symbol for each one. This causes the copy relocation to correctly
  // interpose any aliases.
  for (SharedSymbol *Sym : getSymbolsAt<ELFT>(SS))
    replaceWithDefined(*Sym, Sec, 0, Sym->Size);

  In.RelaDyn->addReloc(Target->CopyRel, Sec, 0, &SS);
}

// MIPS has an odd notion of "paired" relocations to calculate addends.
// For example, if a relocation is of R_MIPS_HI16, there must be a
// R_MIPS_LO16 relocation after that, and an addend is calculated using
// the two relocations.
template <class ELFT, class RelTy>
static int64_t computeMipsAddend(const RelTy &Rel, const RelTy *End,
                                 InputSectionBase &Sec, RelExpr Expr,
                                 bool IsLocal) {
  if (Expr == R_MIPS_GOTREL && IsLocal)
    return Sec.getFile<ELFT>()->MipsGp0;

  // The ABI says that the paired relocation is used only for REL.
  // See p. 4-17 at ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
  if (RelTy::IsRela)
    return 0;

  RelType Type = Rel.getType(Config->IsMips64EL);
  uint32_t PairTy = getMipsPairType(Type, IsLocal);
  if (PairTy == R_MIPS_NONE)
    return 0;

  const uint8_t *Buf = Sec.data().data();
  uint32_t SymIndex = Rel.getSymbol(Config->IsMips64EL);

  // To make things worse, paired relocations might not be contiguous in
  // the relocation table, so we need to do linear search. *sigh*
  for (const RelTy *RI = &Rel; RI != End; ++RI)
    if (RI->getType(Config->IsMips64EL) == PairTy &&
        RI->getSymbol(Config->IsMips64EL) == SymIndex)
      return Target->getImplicitAddend(Buf + RI->r_offset, PairTy);

  warn("can't find matching " + toString(PairTy) + " relocation for " +
       toString(Type));
  return 0;
}

// Returns an addend of a given relocation. If it is RELA, an addend
// is in a relocation itself. If it is REL, we need to read it from an
// input section.
template <class ELFT, class RelTy>
static int64_t computeAddend(const RelTy &Rel, const RelTy *End,
                             InputSectionBase &Sec, RelExpr Expr,
                             bool IsLocal) {
  int64_t Addend;
  RelType Type = Rel.getType(Config->IsMips64EL);

  if (RelTy::IsRela) {
    Addend = getAddend<ELFT>(Rel);
  } else {
    const uint8_t *Buf = Sec.data().data();
    Addend = Target->getImplicitAddend(Buf + Rel.r_offset, Type);
  }

  if (Config->EMachine == EM_PPC64 && Config->Pic && Type == R_PPC64_TOC)
    Addend += getPPC64TocBase();
  if (Config->EMachine == EM_MIPS)
    Addend += computeMipsAddend<ELFT>(Rel, End, Sec, Expr, IsLocal);

  return Addend;
}

// Report an undefined symbol if necessary.
// Returns true if this function printed out an error message.
static bool maybeReportUndefined(Symbol &Sym, InputSectionBase &Sec,
                                 uint64_t Offset) {
  if (Sym.isLocal() || !Sym.isUndefined() || Sym.isWeak())
    return false;

  bool CanBeExternal =
      Sym.computeBinding() != STB_LOCAL && Sym.Visibility == STV_DEFAULT;
  if (Config->UnresolvedSymbols == UnresolvedPolicy::Ignore && CanBeExternal)
    return false;

  std::string Msg =
      "undefined symbol: " + toString(Sym) + "\n>>> referenced by ";

  std::string Src = Sec.getSrcMsg(Sym, Offset);
  if (!Src.empty())
    Msg += Src + "\n>>>               ";
  Msg += Sec.getObjMsg(Offset);

  if (Sym.getName().startswith("_ZTV"))
    Msg += "\nthe vtable symbol may be undefined because the class is missing "
           "its key function (see https://lld.llvm.org/missingkeyfunction)";

  if ((Config->UnresolvedSymbols == UnresolvedPolicy::Warn && CanBeExternal) ||
      Config->NoinhibitExec) {
    warn(Msg);
    return false;
  }

  error(Msg);
  return true;
}

// MIPS N32 ABI treats series of successive relocations with the same offset
// as a single relocation. The similar approach used by N64 ABI, but this ABI
// packs all relocations into the single relocation record. Here we emulate
// this for the N32 ABI. Iterate over relocation with the same offset and put
// theirs types into the single bit-set.
template <class RelTy> static RelType getMipsN32RelType(RelTy *&Rel, RelTy *End) {
  RelType Type = 0;
  uint64_t Offset = Rel->r_offset;

  int N = 0;
  while (Rel != End && Rel->r_offset == Offset)
    Type |= (Rel++)->getType(Config->IsMips64EL) << (8 * N++);
  return Type;
}

// .eh_frame sections are mergeable input sections, so their input
// offsets are not linearly mapped to output section. For each input
// offset, we need to find a section piece containing the offset and
// add the piece's base address to the input offset to compute the
// output offset. That isn't cheap.
//
// This class is to speed up the offset computation. When we process
// relocations, we access offsets in the monotonically increasing
// order. So we can optimize for that access pattern.
//
// For sections other than .eh_frame, this class doesn't do anything.
namespace {
class OffsetGetter {
public:
  explicit OffsetGetter(InputSectionBase &Sec) {
    if (auto *Eh = dyn_cast<EhInputSection>(&Sec))
      Pieces = Eh->Pieces;
  }

  // Translates offsets in input sections to offsets in output sections.
  // Given offset must increase monotonically. We assume that Piece is
  // sorted by InputOff.
  uint64_t get(uint64_t Off) {
    if (Pieces.empty())
      return Off;

    while (I != Pieces.size() && Pieces[I].InputOff + Pieces[I].Size <= Off)
      ++I;
    if (I == Pieces.size())
      fatal(".eh_frame: relocation is not in any piece");

    // Pieces must be contiguous, so there must be no holes in between.
    assert(Pieces[I].InputOff <= Off && "Relocation not in any piece");

    // Offset -1 means that the piece is dead (i.e. garbage collected).
    if (Pieces[I].OutputOff == -1)
      return -1;
    return Pieces[I].OutputOff + Off - Pieces[I].InputOff;
  }

private:
  ArrayRef<EhSectionPiece> Pieces;
  size_t I = 0;
};
} // namespace

static void addRelativeReloc(InputSectionBase *IS, uint64_t OffsetInSec,
                             Symbol *Sym, int64_t Addend, RelExpr Expr,
                             RelType Type) {
  // Add a relative relocation. If RelrDyn section is enabled, and the
  // relocation offset is guaranteed to be even, add the relocation to
  // the RelrDyn section, otherwise add it to the RelaDyn section.
  // RelrDyn sections don't support odd offsets. Also, RelrDyn sections
  // don't store the addend values, so we must write it to the relocated
  // address.
  if (In.RelrDyn && IS->Alignment >= 2 && OffsetInSec % 2 == 0) {
    IS->Relocations.push_back({Expr, Type, OffsetInSec, Addend, Sym});
    In.RelrDyn->Relocs.push_back({IS, OffsetInSec});
    return;
  }
  In.RelaDyn->addReloc(Target->RelativeRel, IS, OffsetInSec, Sym, Addend, Expr,
                       Type);
}

template <class ELFT, class GotPltSection>
static void addPltEntry(PltSection *Plt, GotPltSection *GotPlt,
                        RelocationBaseSection *Rel, RelType Type, Symbol &Sym) {
  Plt->addEntry<ELFT>(Sym);
  GotPlt->addEntry(Sym);
  Rel->addReloc(
      {Type, GotPlt, Sym.getGotPltOffset(), !Sym.IsPreemptible, &Sym, 0});
}

template <class ELFT> static void addGotEntry(Symbol &Sym) {
  In.Got->addEntry(Sym);

  RelExpr Expr;
  if (Sym.isTls())
    Expr = R_TLS;
  else if (Sym.isGnuIFunc())
    Expr = R_PLT;
  else
    Expr = R_ABS;

  uint64_t Off = Sym.getGotOffset();

  // If a GOT slot value can be calculated at link-time, which is now,
  // we can just fill that out.
  //
  // (We don't actually write a value to a GOT slot right now, but we
  // add a static relocation to a Relocations vector so that
  // InputSection::relocate will do the work for us. We may be able
  // to just write a value now, but it is a TODO.)
  bool IsLinkTimeConstant =
      !Sym.IsPreemptible && (!Config->Pic || isAbsolute(Sym));
  if (IsLinkTimeConstant) {
    In.Got->Relocations.push_back({Expr, Target->GotRel, Off, 0, &Sym});
    return;
  }

  // Otherwise, we emit a dynamic relocation to .rel[a].dyn so that
  // the GOT slot will be fixed at load-time.
  if (!Sym.isTls() && !Sym.IsPreemptible && Config->Pic && !isAbsolute(Sym)) {
    addRelativeReloc(In.Got, Off, &Sym, 0, R_ABS, Target->GotRel);
    return;
  }
  In.RelaDyn->addReloc(Sym.isTls() ? Target->TlsGotRel : Target->GotRel, In.Got,
                       Off, &Sym, 0, Sym.IsPreemptible ? R_ADDEND : R_ABS,
                       Target->GotRel);
}

// Return true if we can define a symbol in the executable that
// contains the value/function of a symbol defined in a shared
// library.
static bool canDefineSymbolInExecutable(Symbol &Sym) {
  // If the symbol has default visibility the symbol defined in the
  // executable will preempt it.
  // Note that we want the visibility of the shared symbol itself, not
  // the visibility of the symbol in the output file we are producing. That is
  // why we use Sym.StOther.
  if ((Sym.StOther & 0x3) == STV_DEFAULT)
    return true;

  // If we are allowed to break address equality of functions, defining
  // a plt entry will allow the program to call the function in the
  // .so, but the .so and the executable will no agree on the address
  // of the function. Similar logic for objects.
  return ((Sym.isFunc() && Config->IgnoreFunctionAddressEquality) ||
          (Sym.isObject() && Config->IgnoreDataAddressEquality));
}

// The reason we have to do this early scan is as follows
// * To mmap the output file, we need to know the size
// * For that, we need to know how many dynamic relocs we will have.
// It might be possible to avoid this by outputting the file with write:
// * Write the allocated output sections, computing addresses.
// * Apply relocations, recording which ones require a dynamic reloc.
// * Write the dynamic relocations.
// * Write the rest of the file.
// This would have some drawbacks. For example, we would only know if .rela.dyn
// is needed after applying relocations. If it is, it will go after rw and rx
// sections. Given that it is ro, we will need an extra PT_LOAD. This
// complicates things for the dynamic linker and means we would have to reserve
// space for the extra PT_LOAD even if we end up not using it.
template <class ELFT, class RelTy>
static void processRelocAux(InputSectionBase &Sec, RelExpr Expr, RelType Type,
                            uint64_t Offset, Symbol &Sym, const RelTy &Rel,
                            int64_t Addend) {
  if (isStaticLinkTimeConstant(Expr, Type, Sym, Sec, Offset)) {
    Sec.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return;
  }
  if (Sym.isGnuIFunc() && Config->ZIfuncnoplt) {
    In.RelaDyn->addReloc(Type, &Sec, Offset, &Sym, Addend, R_ADDEND, Type);
    return;
  }
  bool CanWrite = (Sec.Flags & SHF_WRITE) || !Config->ZText;
  if (CanWrite) {
    // R_GOT refers to a position in the got, even if the symbol is preemptible.
    bool IsPreemptibleValue = Sym.IsPreemptible && Expr != R_GOT;

    if (!IsPreemptibleValue) {
      addRelativeReloc(&Sec, Offset, &Sym, Addend, Expr, Type);
      return;
    } else if (RelType Rel = Target->getDynRel(Type)) {
      In.RelaDyn->addReloc(Rel, &Sec, Offset, &Sym, Addend, R_ADDEND, Type);

      // MIPS ABI turns using of GOT and dynamic relocations inside out.
      // While regular ABI uses dynamic relocations to fill up GOT entries
      // MIPS ABI requires dynamic linker to fills up GOT entries using
      // specially sorted dynamic symbol table. This affects even dynamic
      // relocations against symbols which do not require GOT entries
      // creation explicitly, i.e. do not have any GOT-relocations. So if
      // a preemptible symbol has a dynamic relocation we anyway have
      // to create a GOT entry for it.
      // If a non-preemptible symbol has a dynamic relocation against it,
      // dynamic linker takes it st_value, adds offset and writes down
      // result of the dynamic relocation. In case of preemptible symbol
      // dynamic linker performs symbol resolution, writes the symbol value
      // to the GOT entry and reads the GOT entry when it needs to perform
      // a dynamic relocation.
      // ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf p.4-19
      if (Config->EMachine == EM_MIPS)
        In.MipsGot->addEntry(*Sec.File, Sym, Addend, Expr);
      return;
    }
  }

  // If the relocation is to a weak undef, and we are producing
  // executable, give up on it and produce a non preemptible 0.
  if (!Config->Shared && Sym.isUndefWeak()) {
    Sec.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return;
  }

  if (!CanWrite && (Config->Pic && !isRelExpr(Expr))) {
    error(
        "can't create dynamic relocation " + toString(Type) + " against " +
        (Sym.getName().empty() ? "local symbol" : "symbol: " + toString(Sym)) +
        " in readonly segment; recompile object files with -fPIC "
        "or pass '-Wl,-z,notext' to allow text relocations in the output" +
        getLocation(Sec, Sym, Offset));
    return;
  }

  // Copy relocations are only possible if we are creating an executable.
  if (Config->Shared) {
    errorOrWarn("relocation " + toString(Type) +
                " cannot be used against symbol " + toString(Sym) +
                "; recompile with -fPIC" + getLocation(Sec, Sym, Offset));
    return;
  }

  // If the symbol is undefined we already reported any relevant errors.
  if (Sym.isUndefined())
    return;

  if (!canDefineSymbolInExecutable(Sym)) {
    error("cannot preempt symbol: " + toString(Sym) +
          getLocation(Sec, Sym, Offset));
    return;
  }

  if (Sym.isObject()) {
    // Produce a copy relocation.
    if (auto *SS = dyn_cast<SharedSymbol>(&Sym)) {
      if (!Config->ZCopyreloc)
        error("unresolvable relocation " + toString(Type) +
              " against symbol '" + toString(*SS) +
              "'; recompile with -fPIC or remove '-z nocopyreloc'" +
              getLocation(Sec, Sym, Offset));
      addCopyRelSymbol<ELFT>(*SS);
    }
    Sec.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return;
  }

  if (Sym.isFunc()) {
    // This handles a non PIC program call to function in a shared library. In
    // an ideal world, we could just report an error saying the relocation can
    // overflow at runtime. In the real world with glibc, crt1.o has a
    // R_X86_64_PC32 pointing to libc.so.
    //
    // The general idea on how to handle such cases is to create a PLT entry and
    // use that as the function value.
    //
    // For the static linking part, we just return a plt expr and everything
    // else will use the PLT entry as the address.
    //
    // The remaining problem is making sure pointer equality still works. We
    // need the help of the dynamic linker for that. We let it know that we have
    // a direct reference to a so symbol by creating an undefined symbol with a
    // non zero st_value. Seeing that, the dynamic linker resolves the symbol to
    // the value of the symbol we created. This is true even for got entries, so
    // pointer equality is maintained. To avoid an infinite loop, the only entry
    // that points to the real function is a dedicated got entry used by the
    // plt. That is identified by special relocation types (R_X86_64_JUMP_SLOT,
    // R_386_JMP_SLOT, etc).

    // For position independent executable on i386, the plt entry requires ebx
    // to be set. This causes two problems:
    // * If some code has a direct reference to a function, it was probably
    //   compiled without -fPIE/-fPIC and doesn't maintain ebx.
    // * If a library definition gets preempted to the executable, it will have
    //   the wrong ebx value.
    if (Config->Pie && Config->EMachine == EM_386)
      errorOrWarn("symbol '" + toString(Sym) +
                  "' cannot be preempted; recompile with -fPIE" +
                  getLocation(Sec, Sym, Offset));
    if (!Sym.isInPlt())
      addPltEntry<ELFT>(In.Plt, In.GotPlt, In.RelaPlt, Target->PltRel, Sym);
    if (!Sym.isDefined())
      replaceWithDefined(Sym, In.Plt, getPltEntryOffset(Sym.PltIndex), 0);
    Sym.NeedsPltAddr = true;
    Sec.Relocations.push_back({Expr, Type, Offset, Addend, &Sym});
    return;
  }

  errorOrWarn("symbol '" + toString(Sym) + "' has no type" +
              getLocation(Sec, Sym, Offset));
}

template <class ELFT, class RelTy>
static void scanReloc(InputSectionBase &Sec, OffsetGetter &GetOffset, RelTy *&I,
                      RelTy *End) {
  const RelTy &Rel = *I;
  Symbol &Sym = Sec.getFile<ELFT>()->getRelocTargetSym(Rel);
  RelType Type;

  // Deal with MIPS oddity.
  if (Config->MipsN32Abi) {
    Type = getMipsN32RelType(I, End);
  } else {
    Type = Rel.getType(Config->IsMips64EL);
    ++I;
  }

  // Get an offset in an output section this relocation is applied to.
  uint64_t Offset = GetOffset.get(Rel.r_offset);
  if (Offset == uint64_t(-1))
    return;

  // Skip if the target symbol is an erroneous undefined symbol.
  if (maybeReportUndefined(Sym, Sec, Rel.r_offset))
    return;

  const uint8_t *RelocatedAddr = Sec.data().begin() + Rel.r_offset;
  RelExpr Expr = Target->getRelExpr(Type, Sym, RelocatedAddr);

  // Ignore "hint" relocations because they are only markers for relaxation.
  if (isRelExprOneOf<R_HINT, R_NONE>(Expr))
    return;

  // Strenghten or relax relocations.
  //
  // GNU ifunc symbols must be accessed via PLT because their addresses
  // are determined by runtime.
  //
  // On the other hand, if we know that a PLT entry will be resolved within
  // the same ELF module, we can skip PLT access and directly jump to the
  // destination function. For example, if we are linking a main exectuable,
  // all dynamic symbols that can be resolved within the executable will
  // actually be resolved that way at runtime, because the main exectuable
  // is always at the beginning of a search list. We can leverage that fact.
  if (Sym.isGnuIFunc() && !Config->ZIfuncnoplt) {
    if (!Config->ZText && Config->WarnIfuncTextrel) {
      warn("using ifunc symbols when text relocations are allowed may produce "
           "a binary that will segfault, if the object file is linked with "
           "old version of glibc (glibc 2.28 and earlier). If this applies to "
           "you, consider recompiling the object files without -fPIC and "
           "without -Wl,-z,notext option. Use -no-warn-ifunc-textrel to "
           "turn off this warning." +
           getLocation(Sec, Sym, Offset));
    }
    Expr = toPlt(Expr);
  } else if (!Sym.IsPreemptible && Expr == R_GOT_PC && !isAbsoluteValue(Sym)) {
    Expr = Target->adjustRelaxExpr(Type, RelocatedAddr, Expr);
  } else if (!Sym.IsPreemptible) {
    Expr = fromPlt(Expr);
  }

  // This relocation does not require got entry, but it is relative to got and
  // needs it to be created. Here we request for that.
  if (isRelExprOneOf<R_GOTONLY_PC, R_GOTONLY_PC_FROM_END, R_GOTREL,
                     R_GOTREL_FROM_END, R_PPC_TOC>(Expr))
    In.Got->HasGotOffRel = true;

  // Read an addend.
  int64_t Addend = computeAddend<ELFT>(Rel, End, Sec, Expr, Sym.isLocal());

  // Process some TLS relocations, including relaxing TLS relocations.
  // Note that this function does not handle all TLS relocations.
  if (unsigned Processed =
          handleTlsRelocation<ELFT>(Type, Sym, Sec, Offset, Addend, Expr)) {
    I += (Processed - 1);
    return;
  }

  // If a relocation needs PLT, we create PLT and GOTPLT slots for the symbol.
  if (needsPlt(Expr) && !Sym.isInPlt()) {
    if (Sym.isGnuIFunc() && !Sym.IsPreemptible)
      addPltEntry<ELFT>(In.Iplt, In.IgotPlt, In.RelaIplt, Target->IRelativeRel,
                        Sym);
    else
      addPltEntry<ELFT>(In.Plt, In.GotPlt, In.RelaPlt, Target->PltRel, Sym);
  }

  // Create a GOT slot if a relocation needs GOT.
  if (needsGot(Expr)) {
    if (Config->EMachine == EM_MIPS) {
      // MIPS ABI has special rules to process GOT entries and doesn't
      // require relocation entries for them. A special case is TLS
      // relocations. In that case dynamic loader applies dynamic
      // relocations to initialize TLS GOT entries.
      // See "Global Offset Table" in Chapter 5 in the following document
      // for detailed description:
      // ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
      In.MipsGot->addEntry(*Sec.File, Sym, Addend, Expr);
    } else if (!Sym.isInGot()) {
      addGotEntry<ELFT>(Sym);
    }
  }

  processRelocAux<ELFT>(Sec, Expr, Type, Offset, Sym, Rel, Addend);
}

template <class ELFT, class RelTy>
static void scanRelocs(InputSectionBase &Sec, ArrayRef<RelTy> Rels) {
  OffsetGetter GetOffset(Sec);

  // Not all relocations end up in Sec.Relocations, but a lot do.
  Sec.Relocations.reserve(Rels.size());

  for (auto I = Rels.begin(), End = Rels.end(); I != End;)
    scanReloc<ELFT>(Sec, GetOffset, I, End);

  // Sort relocations by offset to binary search for R_RISCV_PCREL_HI20
  if (Config->EMachine == EM_RISCV)
    std::stable_sort(Sec.Relocations.begin(), Sec.Relocations.end(),
                     RelocationOffsetComparator{});
}

template <class ELFT> void elf::scanRelocations(InputSectionBase &S) {
  if (S.AreRelocsRela)
    scanRelocs<ELFT>(S, S.relas<ELFT>());
  else
    scanRelocs<ELFT>(S, S.rels<ELFT>());
}

static bool mergeCmp(const InputSection *A, const InputSection *B) {
  // std::merge requires a strict weak ordering.
  if (A->OutSecOff < B->OutSecOff)
    return true;

  if (A->OutSecOff == B->OutSecOff) {
    auto *TA = dyn_cast<ThunkSection>(A);
    auto *TB = dyn_cast<ThunkSection>(B);

    // Check if Thunk is immediately before any specific Target
    // InputSection for example Mips LA25 Thunks.
    if (TA && TA->getTargetInputSection() == B)
      return true;

    // Place Thunk Sections without specific targets before
    // non-Thunk Sections.
    if (TA && !TB && !TA->getTargetInputSection())
      return true;
  }

  return false;
}

// Call Fn on every executable InputSection accessed via the linker script
// InputSectionDescription::Sections.
static void forEachInputSectionDescription(
    ArrayRef<OutputSection *> OutputSections,
    llvm::function_ref<void(OutputSection *, InputSectionDescription *)> Fn) {
  for (OutputSection *OS : OutputSections) {
    if (!(OS->Flags & SHF_ALLOC) || !(OS->Flags & SHF_EXECINSTR))
      continue;
    for (BaseCommand *BC : OS->SectionCommands)
      if (auto *ISD = dyn_cast<InputSectionDescription>(BC))
        Fn(OS, ISD);
  }
}

// Thunk Implementation
//
// Thunks (sometimes called stubs, veneers or branch islands) are small pieces
// of code that the linker inserts inbetween a caller and a callee. The thunks
// are added at link time rather than compile time as the decision on whether
// a thunk is needed, such as the caller and callee being out of range, can only
// be made at link time.
//
// It is straightforward to tell given the current state of the program when a
// thunk is needed for a particular call. The more difficult part is that
// the thunk needs to be placed in the program such that the caller can reach
// the thunk and the thunk can reach the callee; furthermore, adding thunks to
// the program alters addresses, which can mean more thunks etc.
//
// In lld we have a synthetic ThunkSection that can hold many Thunks.
// The decision to have a ThunkSection act as a container means that we can
// more easily handle the most common case of a single block of contiguous
// Thunks by inserting just a single ThunkSection.
//
// The implementation of Thunks in lld is split across these areas
// Relocations.cpp : Framework for creating and placing thunks
// Thunks.cpp : The code generated for each supported thunk
// Target.cpp : Target specific hooks that the framework uses to decide when
//              a thunk is used
// Synthetic.cpp : Implementation of ThunkSection
// Writer.cpp : Iteratively call framework until no more Thunks added
//
// Thunk placement requirements:
// Mips LA25 thunks. These must be placed immediately before the callee section
// We can assume that the caller is in range of the Thunk. These are modelled
// by Thunks that return the section they must precede with
// getTargetInputSection().
//
// ARM interworking and range extension thunks. These thunks must be placed
// within range of the caller. All implemented ARM thunks can always reach the
// callee as they use an indirect jump via a register that has no range
// restrictions.
//
// Thunk placement algorithm:
// For Mips LA25 ThunkSections; the placement is explicit, it has to be before
// getTargetInputSection().
//
// For thunks that must be placed within range of the caller there are many
// possible choices given that the maximum range from the caller is usually
// much larger than the average InputSection size. Desirable properties include:
// - Maximize reuse of thunks by multiple callers
// - Minimize number of ThunkSections to simplify insertion
// - Handle impact of already added Thunks on addresses
// - Simple to understand and implement
//
// In lld for the first pass, we pre-create one or more ThunkSections per
// InputSectionDescription at Target specific intervals. A ThunkSection is
// placed so that the estimated end of the ThunkSection is within range of the
// start of the InputSectionDescription or the previous ThunkSection. For
// example:
// InputSectionDescription
// Section 0
// ...
// Section N
// ThunkSection 0
// Section N + 1
// ...
// Section N + K
// Thunk Section 1
//
// The intention is that we can add a Thunk to a ThunkSection that is well
// spaced enough to service a number of callers without having to do a lot
// of work. An important principle is that it is not an error if a Thunk cannot
// be placed in a pre-created ThunkSection; when this happens we create a new
// ThunkSection placed next to the caller. This allows us to handle the vast
// majority of thunks simply, but also handle rare cases where the branch range
// is smaller than the target specific spacing.
//
// The algorithm is expected to create all the thunks that are needed in a
// single pass, with a small number of programs needing a second pass due to
// the insertion of thunks in the first pass increasing the offset between
// callers and callees that were only just in range.
//
// A consequence of allowing new ThunkSections to be created outside of the
// pre-created ThunkSections is that in rare cases calls to Thunks that were in
// range in pass K, are out of range in some pass > K due to the insertion of
// more Thunks in between the caller and callee. When this happens we retarget
// the relocation back to the original target and create another Thunk.

// Remove ThunkSections that are empty, this should only be the initial set
// precreated on pass 0.

// Insert the Thunks for OutputSection OS into their designated place
// in the Sections vector, and recalculate the InputSection output section
// offsets.
// This may invalidate any output section offsets stored outside of InputSection
void ThunkCreator::mergeThunks(ArrayRef<OutputSection *> OutputSections) {
  forEachInputSectionDescription(
      OutputSections, [&](OutputSection *OS, InputSectionDescription *ISD) {
        if (ISD->ThunkSections.empty())
          return;

        // Remove any zero sized precreated Thunks.
        llvm::erase_if(ISD->ThunkSections,
                       [](const std::pair<ThunkSection *, uint32_t> &TS) {
                         return TS.first->getSize() == 0;
                       });

        // ISD->ThunkSections contains all created ThunkSections, including
        // those inserted in previous passes. Extract the Thunks created this
        // pass and order them in ascending OutSecOff.
        std::vector<ThunkSection *> NewThunks;
        for (const std::pair<ThunkSection *, uint32_t> TS : ISD->ThunkSections)
          if (TS.second == Pass)
            NewThunks.push_back(TS.first);
        std::stable_sort(NewThunks.begin(), NewThunks.end(),
                         [](const ThunkSection *A, const ThunkSection *B) {
                           return A->OutSecOff < B->OutSecOff;
                         });

        // Merge sorted vectors of Thunks and InputSections by OutSecOff
        std::vector<InputSection *> Tmp;
        Tmp.reserve(ISD->Sections.size() + NewThunks.size());

        std::merge(ISD->Sections.begin(), ISD->Sections.end(),
                   NewThunks.begin(), NewThunks.end(), std::back_inserter(Tmp),
                   mergeCmp);

        ISD->Sections = std::move(Tmp);
      });
}

// Find or create a ThunkSection within the InputSectionDescription (ISD) that
// is in range of Src. An ISD maps to a range of InputSections described by a
// linker script section pattern such as { .text .text.* }.
ThunkSection *ThunkCreator::getISDThunkSec(OutputSection *OS, InputSection *IS,
                                           InputSectionDescription *ISD,
                                           uint32_t Type, uint64_t Src) {
  for (std::pair<ThunkSection *, uint32_t> TP : ISD->ThunkSections) {
    ThunkSection *TS = TP.first;
    uint64_t TSBase = OS->Addr + TS->OutSecOff;
    uint64_t TSLimit = TSBase + TS->getSize();
    if (Target->inBranchRange(Type, Src, (Src > TSLimit) ? TSBase : TSLimit))
      return TS;
  }

  // No suitable ThunkSection exists. This can happen when there is a branch
  // with lower range than the ThunkSection spacing or when there are too
  // many Thunks. Create a new ThunkSection as close to the InputSection as
  // possible. Error if InputSection is so large we cannot place ThunkSection
  // anywhere in Range.
  uint64_t ThunkSecOff = IS->OutSecOff;
  if (!Target->inBranchRange(Type, Src, OS->Addr + ThunkSecOff)) {
    ThunkSecOff = IS->OutSecOff + IS->getSize();
    if (!Target->inBranchRange(Type, Src, OS->Addr + ThunkSecOff))
      fatal("InputSection too large for range extension thunk " +
            IS->getObjMsg(Src - (OS->Addr + IS->OutSecOff)));
  }
  return addThunkSection(OS, ISD, ThunkSecOff);
}

// Add a Thunk that needs to be placed in a ThunkSection that immediately
// precedes its Target.
ThunkSection *ThunkCreator::getISThunkSec(InputSection *IS) {
  ThunkSection *TS = ThunkedSections.lookup(IS);
  if (TS)
    return TS;

  // Find InputSectionRange within Target Output Section (TOS) that the
  // InputSection (IS) that we need to precede is in.
  OutputSection *TOS = IS->getParent();
  for (BaseCommand *BC : TOS->SectionCommands) {
    auto *ISD = dyn_cast<InputSectionDescription>(BC);
    if (!ISD || ISD->Sections.empty())
      continue;

    InputSection *First = ISD->Sections.front();
    InputSection *Last = ISD->Sections.back();

    if (IS->OutSecOff < First->OutSecOff || Last->OutSecOff < IS->OutSecOff)
      continue;

    TS = addThunkSection(TOS, ISD, IS->OutSecOff);
    ThunkedSections[IS] = TS;
    return TS;
  }

  return nullptr;
}

// Create one or more ThunkSections per OS that can be used to place Thunks.
// We attempt to place the ThunkSections using the following desirable
// properties:
// - Within range of the maximum number of callers
// - Minimise the number of ThunkSections
//
// We follow a simple but conservative heuristic to place ThunkSections at
// offsets that are multiples of a Target specific branch range.
// For an InputSectionDescription that is smaller than the range, a single
// ThunkSection at the end of the range will do.
//
// For an InputSectionDescription that is more than twice the size of the range,
// we place the last ThunkSection at range bytes from the end of the
// InputSectionDescription in order to increase the likelihood that the
// distance from a thunk to its target will be sufficiently small to
// allow for the creation of a short thunk.
void ThunkCreator::createInitialThunkSections(
    ArrayRef<OutputSection *> OutputSections) {
  uint32_t ThunkSectionSpacing = Target->getThunkSectionSpacing();

  forEachInputSectionDescription(
      OutputSections, [&](OutputSection *OS, InputSectionDescription *ISD) {
        if (ISD->Sections.empty())
          return;

        uint32_t ISDBegin = ISD->Sections.front()->OutSecOff;
        uint32_t ISDEnd =
            ISD->Sections.back()->OutSecOff + ISD->Sections.back()->getSize();
        uint32_t LastThunkLowerBound = -1;
        if (ISDEnd - ISDBegin > ThunkSectionSpacing * 2)
          LastThunkLowerBound = ISDEnd - ThunkSectionSpacing;

        uint32_t ISLimit;
        uint32_t PrevISLimit = ISDBegin;
        uint32_t ThunkUpperBound = ISDBegin + ThunkSectionSpacing;

        for (const InputSection *IS : ISD->Sections) {
          ISLimit = IS->OutSecOff + IS->getSize();
          if (ISLimit > ThunkUpperBound) {
            addThunkSection(OS, ISD, PrevISLimit);
            ThunkUpperBound = PrevISLimit + ThunkSectionSpacing;
          }
          if (ISLimit > LastThunkLowerBound)
            break;
          PrevISLimit = ISLimit;
        }
        addThunkSection(OS, ISD, ISLimit);
      });
}

ThunkSection *ThunkCreator::addThunkSection(OutputSection *OS,
                                            InputSectionDescription *ISD,
                                            uint64_t Off) {
  auto *TS = make<ThunkSection>(OS, Off);
  ISD->ThunkSections.push_back({TS, Pass});
  return TS;
}

std::pair<Thunk *, bool> ThunkCreator::getThunk(Symbol &Sym, RelType Type,
                                                uint64_t Src) {
  std::vector<Thunk *> *ThunkVec = nullptr;

  // We use (section, offset) pair to find the thunk position if possible so
  // that we create only one thunk for aliased symbols or ICFed sections.
  if (auto *D = dyn_cast<Defined>(&Sym))
    if (!D->isInPlt() && D->Section)
      ThunkVec = &ThunkedSymbolsBySection[{D->Section->Repl, D->Value}];
  if (!ThunkVec)
    ThunkVec = &ThunkedSymbols[&Sym];

  // Check existing Thunks for Sym to see if they can be reused
  for (Thunk *T : *ThunkVec)
    if (T->isCompatibleWith(Type) &&
        Target->inBranchRange(Type, Src, T->getThunkTargetSym()->getVA()))
      return std::make_pair(T, false);

  // No existing compatible Thunk in range, create a new one
  Thunk *T = addThunk(Type, Sym);
  ThunkVec->push_back(T);
  return std::make_pair(T, true);
}

// Return true if the relocation target is an in range Thunk.
// Return false if the relocation is not to a Thunk. If the relocation target
// was originally to a Thunk, but is no longer in range we revert the
// relocation back to its original non-Thunk target.
bool ThunkCreator::normalizeExistingThunk(Relocation &Rel, uint64_t Src) {
  if (Thunk *T = Thunks.lookup(Rel.Sym)) {
    if (Target->inBranchRange(Rel.Type, Src, Rel.Sym->getVA()))
      return true;
    Rel.Sym = &T->Destination;
    if (Rel.Sym->isInPlt())
      Rel.Expr = toPlt(Rel.Expr);
  }
  return false;
}

// Process all relocations from the InputSections that have been assigned
// to InputSectionDescriptions and redirect through Thunks if needed. The
// function should be called iteratively until it returns false.
//
// PreConditions:
// All InputSections that may need a Thunk are reachable from
// OutputSectionCommands.
//
// All OutputSections have an address and all InputSections have an offset
// within the OutputSection.
//
// The offsets between caller (relocation place) and callee
// (relocation target) will not be modified outside of createThunks().
//
// PostConditions:
// If return value is true then ThunkSections have been inserted into
// OutputSections. All relocations that needed a Thunk based on the information
// available to createThunks() on entry have been redirected to a Thunk. Note
// that adding Thunks changes offsets between caller and callee so more Thunks
// may be required.
//
// If return value is false then no more Thunks are needed, and createThunks has
// made no changes. If the target requires range extension thunks, currently
// ARM, then any future change in offset between caller and callee risks a
// relocation out of range error.
bool ThunkCreator::createThunks(ArrayRef<OutputSection *> OutputSections) {
  bool AddressesChanged = false;

  if (Pass == 0 && Target->getThunkSectionSpacing())
    createInitialThunkSections(OutputSections);

  // With Thunk Size much smaller than branch range we expect to
  // converge quickly; if we get to 10 something has gone wrong.
  if (Pass == 10)
    fatal("thunk creation not converged");

  // Create all the Thunks and insert them into synthetic ThunkSections. The
  // ThunkSections are later inserted back into InputSectionDescriptions.
  // We separate the creation of ThunkSections from the insertion of the
  // ThunkSections as ThunkSections are not always inserted into the same
  // InputSectionDescription as the caller.
  forEachInputSectionDescription(
      OutputSections, [&](OutputSection *OS, InputSectionDescription *ISD) {
        for (InputSection *IS : ISD->Sections)
          for (Relocation &Rel : IS->Relocations) {
            uint64_t Src = IS->getVA(Rel.Offset);

            // If we are a relocation to an existing Thunk, check if it is
            // still in range. If not then Rel will be altered to point to its
            // original target so another Thunk can be generated.
            if (Pass > 0 && normalizeExistingThunk(Rel, Src))
              continue;

            if (!Target->needsThunk(Rel.Expr, Rel.Type, IS->File, Src,
                                    *Rel.Sym))
              continue;

            Thunk *T;
            bool IsNew;
            std::tie(T, IsNew) = getThunk(*Rel.Sym, Rel.Type, Src);

            if (IsNew) {
              // Find or create a ThunkSection for the new Thunk
              ThunkSection *TS;
              if (auto *TIS = T->getTargetInputSection())
                TS = getISThunkSec(TIS);
              else
                TS = getISDThunkSec(OS, IS, ISD, Rel.Type, Src);
              TS->addThunk(T);
              Thunks[T->getThunkTargetSym()] = T;
            }

            // Redirect relocation to Thunk, we never go via the PLT to a Thunk
            Rel.Sym = T->getThunkTargetSym();
            Rel.Expr = fromPlt(Rel.Expr);
          }

        for (auto &P : ISD->ThunkSections)
          AddressesChanged |= P.first->assignOffsets();
      });

  for (auto &P : ThunkedSections)
    AddressesChanged |= P.second->assignOffsets();

  // Merge all created synthetic ThunkSections back into OutputSection
  mergeThunks(OutputSections);
  ++Pass;
  return AddressesChanged;
}

template void elf::scanRelocations<ELF32LE>(InputSectionBase &);
template void elf::scanRelocations<ELF32BE>(InputSectionBase &);
template void elf::scanRelocations<ELF64LE>(InputSectionBase &);
template void elf::scanRelocations<ELF64BE>(InputSectionBase &);
