//===- LinkerScript.cpp ---------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the parser/evaluator of the linker script.
//
//===----------------------------------------------------------------------===//

#include "LinkerScript.h"
#include "Config.h"
#include "InputSection.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "Writer.h"
#include "lld/Common/Memory.h"
#include "lld/Common/Strings.h"
#include "lld/Common/Threads.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

LinkerScript *elf::Script;

static uint64_t getOutputSectionVA(SectionBase *InputSec, StringRef Loc) {
  if (OutputSection *OS = InputSec->getOutputSection())
    return OS->Addr;
  error(Loc + ": unable to evaluate expression: input section " +
        InputSec->Name + " has no output section assigned");
  return 0;
}

uint64_t ExprValue::getValue() const {
  if (Sec)
    return alignTo(Sec->getOffset(Val) + getOutputSectionVA(Sec, Loc),
                   Alignment);
  return alignTo(Val, Alignment);
}

uint64_t ExprValue::getSecAddr() const {
  if (Sec)
    return Sec->getOffset(0) + getOutputSectionVA(Sec, Loc);
  return 0;
}

uint64_t ExprValue::getSectionOffset() const {
  // If the alignment is trivial, we don't have to compute the full
  // value to know the offset. This allows this function to succeed in
  // cases where the output section is not yet known.
  if (Alignment == 1 && (!Sec || !Sec->getOutputSection()))
    return Val;
  return getValue() - getSecAddr();
}

OutputSection *LinkerScript::createOutputSection(StringRef Name,
                                                 StringRef Location) {
  OutputSection *&SecRef = NameToOutputSection[Name];
  OutputSection *Sec;
  if (SecRef && SecRef->Location.empty()) {
    // There was a forward reference.
    Sec = SecRef;
  } else {
    Sec = make<OutputSection>(Name, SHT_NOBITS, 0);
    if (!SecRef)
      SecRef = Sec;
  }
  Sec->Location = Location;
  return Sec;
}

OutputSection *LinkerScript::getOrCreateOutputSection(StringRef Name) {
  OutputSection *&CmdRef = NameToOutputSection[Name];
  if (!CmdRef)
    CmdRef = make<OutputSection>(Name, SHT_PROGBITS, 0);
  return CmdRef;
}

// Expands the memory region by the specified size.
static void expandMemoryRegion(MemoryRegion *MemRegion, uint64_t Size,
                               StringRef RegionName, StringRef SecName) {
  MemRegion->CurPos += Size;
  uint64_t NewSize = MemRegion->CurPos - MemRegion->Origin;
  if (NewSize > MemRegion->Length)
    error("section '" + SecName + "' will not fit in region '" + RegionName +
          "': overflowed by " + Twine(NewSize - MemRegion->Length) + " bytes");
}

void LinkerScript::expandMemoryRegions(uint64_t Size) {
  if (Ctx->MemRegion)
    expandMemoryRegion(Ctx->MemRegion, Size, Ctx->MemRegion->Name,
                       Ctx->OutSec->Name);
  // Only expand the LMARegion if it is different from MemRegion.
  if (Ctx->LMARegion && Ctx->MemRegion != Ctx->LMARegion)
    expandMemoryRegion(Ctx->LMARegion, Size, Ctx->LMARegion->Name,
                       Ctx->OutSec->Name);
}

void LinkerScript::expandOutputSection(uint64_t Size) {
  Ctx->OutSec->Size += Size;
  expandMemoryRegions(Size);
}

void LinkerScript::setDot(Expr E, const Twine &Loc, bool InSec) {
  uint64_t Val = E().getValue();
  if (Val < Dot && InSec)
    error(Loc + ": unable to move location counter backward for: " +
          Ctx->OutSec->Name);

  // Update to location counter means update to section size.
  if (InSec)
    expandOutputSection(Val - Dot);
  else
    expandMemoryRegions(Val - Dot);

  Dot = Val;
}

// Used for handling linker symbol assignments, for both finalizing
// their values and doing early declarations. Returns true if symbol
// should be defined from linker script.
static bool shouldDefineSym(SymbolAssignment *Cmd) {
  if (Cmd->Name == ".")
    return false;

  if (!Cmd->Provide)
    return true;

  // If a symbol was in PROVIDE(), we need to define it only
  // when it is a referenced undefined symbol.
  Symbol *B = Symtab->find(Cmd->Name);
  if (B && !B->isDefined())
    return true;
  return false;
}

// This function is called from processSectionCommands,
// while we are fixing the output section layout.
void LinkerScript::addSymbol(SymbolAssignment *Cmd) {
  if (!shouldDefineSym(Cmd))
    return;

  // Define a symbol.
  Symbol *Sym;
  uint8_t Visibility = Cmd->Hidden ? STV_HIDDEN : STV_DEFAULT;
  std::tie(Sym, std::ignore) = Symtab->insert(Cmd->Name, Visibility,
                                              /*CanOmitFromDynSym*/ false,
                                              /*File*/ nullptr);
  ExprValue Value = Cmd->Expression();
  SectionBase *Sec = Value.isAbsolute() ? nullptr : Value.Sec;

  // When this function is called, section addresses have not been
  // fixed yet. So, we may or may not know the value of the RHS
  // expression.
  //
  // For example, if an expression is `x = 42`, we know x is always 42.
  // However, if an expression is `x = .`, there's no way to know its
  // value at the moment.
  //
  // We want to set symbol values early if we can. This allows us to
  // use symbols as variables in linker scripts. Doing so allows us to
  // write expressions like this: `alignment = 16; . = ALIGN(., alignment)`.
  uint64_t SymValue = Value.Sec ? 0 : Value.getValue();

  replaceSymbol<Defined>(Sym, nullptr, Cmd->Name, STB_GLOBAL, Visibility,
                         STT_NOTYPE, SymValue, 0, Sec);
  Cmd->Sym = cast<Defined>(Sym);
}

// This function is called from LinkerScript::declareSymbols.
// It creates a placeholder symbol if needed.
static void declareSymbol(SymbolAssignment *Cmd) {
  if (!shouldDefineSym(Cmd))
    return;

  // We can't calculate final value right now.
  Symbol *Sym;
  uint8_t Visibility = Cmd->Hidden ? STV_HIDDEN : STV_DEFAULT;
  std::tie(Sym, std::ignore) = Symtab->insert(Cmd->Name, Visibility,
                                              /*CanOmitFromDynSym*/ false,
                                              /*File*/ nullptr);
  replaceSymbol<Defined>(Sym, nullptr, Cmd->Name, STB_GLOBAL, Visibility,
                         STT_NOTYPE, 0, 0, nullptr);
  Cmd->Sym = cast<Defined>(Sym);
  Cmd->Provide = false;
  Sym->ScriptDefined = true;
}

// This method is used to handle INSERT AFTER statement. Here we rebuild
// the list of script commands to mix sections inserted into.
void LinkerScript::processInsertCommands() {
  std::vector<BaseCommand *> V;
  auto Insert = [&](std::vector<BaseCommand *> &From) {
    V.insert(V.end(), From.begin(), From.end());
    From.clear();
  };

  for (BaseCommand *Base : SectionCommands) {
    if (auto *OS = dyn_cast<OutputSection>(Base)) {
      Insert(InsertBeforeCommands[OS->Name]);
      V.push_back(Base);
      Insert(InsertAfterCommands[OS->Name]);
      continue;
    }
    V.push_back(Base);
  }

  for (auto &Cmds : {InsertBeforeCommands, InsertAfterCommands})
    for (const std::pair<StringRef, std::vector<BaseCommand *>> &P : Cmds)
      if (!P.second.empty())
        error("unable to INSERT AFTER/BEFORE " + P.first +
              ": section not defined");

  SectionCommands = std::move(V);
}

// Symbols defined in script should not be inlined by LTO. At the same time
// we don't know their final values until late stages of link. Here we scan
// over symbol assignment commands and create placeholder symbols if needed.
void LinkerScript::declareSymbols() {
  assert(!Ctx);
  for (BaseCommand *Base : SectionCommands) {
    if (auto *Cmd = dyn_cast<SymbolAssignment>(Base)) {
      declareSymbol(Cmd);
      continue;
    }

    // If the output section directive has constraints,
    // we can't say for sure if it is going to be included or not.
    // Skip such sections for now. Improve the checks if we ever
    // need symbols from that sections to be declared early.
    auto *Sec = cast<OutputSection>(Base);
    if (Sec->Constraint != ConstraintKind::NoConstraint)
      continue;
    for (BaseCommand *Base2 : Sec->SectionCommands)
      if (auto *Cmd = dyn_cast<SymbolAssignment>(Base2))
        declareSymbol(Cmd);
  }
}

// This function is called from assignAddresses, while we are
// fixing the output section addresses. This function is supposed
// to set the final value for a given symbol assignment.
void LinkerScript::assignSymbol(SymbolAssignment *Cmd, bool InSec) {
  if (Cmd->Name == ".") {
    setDot(Cmd->Expression, Cmd->Location, InSec);
    return;
  }

  if (!Cmd->Sym)
    return;

  ExprValue V = Cmd->Expression();
  if (V.isAbsolute()) {
    Cmd->Sym->Section = nullptr;
    Cmd->Sym->Value = V.getValue();
  } else {
    Cmd->Sym->Section = V.Sec;
    Cmd->Sym->Value = V.getSectionOffset();
  }
}

static std::string getFilename(InputFile *File) {
  if (!File)
    return "";
  if (File->ArchiveName.empty())
    return File->getName();
  return (File->ArchiveName + "(" + File->getName() + ")").str();
}

bool LinkerScript::shouldKeep(InputSectionBase *S) {
  if (KeptSections.empty())
    return false;
  std::string Filename = getFilename(S->File);
  for (InputSectionDescription *ID : KeptSections)
    if (ID->FilePat.match(Filename))
      for (SectionPattern &P : ID->SectionPatterns)
        if (P.SectionPat.match(S->Name))
          return true;
  return false;
}

// A helper function for the SORT() command.
static std::function<bool(InputSectionBase *, InputSectionBase *)>
getComparator(SortSectionPolicy K) {
  switch (K) {
  case SortSectionPolicy::Alignment:
    return [](InputSectionBase *A, InputSectionBase *B) {
      // ">" is not a mistake. Sections with larger alignments are placed
      // before sections with smaller alignments in order to reduce the
      // amount of padding necessary. This is compatible with GNU.
      return A->Alignment > B->Alignment;
    };
  case SortSectionPolicy::Name:
    return [](InputSectionBase *A, InputSectionBase *B) {
      return A->Name < B->Name;
    };
  case SortSectionPolicy::Priority:
    return [](InputSectionBase *A, InputSectionBase *B) {
      return getPriority(A->Name) < getPriority(B->Name);
    };
  default:
    llvm_unreachable("unknown sort policy");
  }
}

// A helper function for the SORT() command.
static bool matchConstraints(ArrayRef<InputSection *> Sections,
                             ConstraintKind Kind) {
  if (Kind == ConstraintKind::NoConstraint)
    return true;

  bool IsRW = llvm::any_of(
      Sections, [](InputSection *Sec) { return Sec->Flags & SHF_WRITE; });

  return (IsRW && Kind == ConstraintKind::ReadWrite) ||
         (!IsRW && Kind == ConstraintKind::ReadOnly);
}

static void sortSections(MutableArrayRef<InputSection *> Vec,
                         SortSectionPolicy K) {
  if (K != SortSectionPolicy::Default && K != SortSectionPolicy::None)
    std::stable_sort(Vec.begin(), Vec.end(), getComparator(K));
}

// Sort sections as instructed by SORT-family commands and --sort-section
// option. Because SORT-family commands can be nested at most two depth
// (e.g. SORT_BY_NAME(SORT_BY_ALIGNMENT(.text.*))) and because the command
// line option is respected even if a SORT command is given, the exact
// behavior we have here is a bit complicated. Here are the rules.
//
// 1. If two SORT commands are given, --sort-section is ignored.
// 2. If one SORT command is given, and if it is not SORT_NONE,
//    --sort-section is handled as an inner SORT command.
// 3. If one SORT command is given, and if it is SORT_NONE, don't sort.
// 4. If no SORT command is given, sort according to --sort-section.
static void sortInputSections(MutableArrayRef<InputSection *> Vec,
                              const SectionPattern &Pat) {
  if (Pat.SortOuter == SortSectionPolicy::None)
    return;

  if (Pat.SortInner == SortSectionPolicy::Default)
    sortSections(Vec, Config->SortSection);
  else
    sortSections(Vec, Pat.SortInner);
  sortSections(Vec, Pat.SortOuter);
}

// Compute and remember which sections the InputSectionDescription matches.
std::vector<InputSection *>
LinkerScript::computeInputSections(const InputSectionDescription *Cmd) {
  std::vector<InputSection *> Ret;

  // Collects all sections that satisfy constraints of Cmd.
  for (const SectionPattern &Pat : Cmd->SectionPatterns) {
    size_t SizeBefore = Ret.size();

    for (InputSectionBase *Sec : InputSections) {
      if (!Sec->Live || Sec->Assigned)
        continue;

      // For -emit-relocs we have to ignore entries like
      //   .rela.dyn : { *(.rela.data) }
      // which are common because they are in the default bfd script.
      // We do not ignore SHT_REL[A] linker-synthesized sections here because
      // want to support scripts that do custom layout for them.
      if (auto *IS = dyn_cast<InputSection>(Sec))
        if (IS->getRelocatedSection())
          continue;

      std::string Filename = getFilename(Sec->File);
      if (!Cmd->FilePat.match(Filename) ||
          Pat.ExcludedFilePat.match(Filename) ||
          !Pat.SectionPat.match(Sec->Name))
        continue;

      // It is safe to assume that Sec is an InputSection
      // because mergeable or EH input sections have already been
      // handled and eliminated.
      Ret.push_back(cast<InputSection>(Sec));
      Sec->Assigned = true;
    }

    sortInputSections(MutableArrayRef<InputSection *>(Ret).slice(SizeBefore),
                      Pat);
  }
  return Ret;
}

void LinkerScript::discard(ArrayRef<InputSection *> V) {
  for (InputSection *S : V) {
    if (S == In.ShStrTab || S == In.RelaDyn || S == In.RelrDyn)
      error("discarding " + S->Name + " section is not allowed");

    // You can discard .hash and .gnu.hash sections by linker scripts. Since
    // they are synthesized sections, we need to handle them differently than
    // other regular sections.
    if (S == In.GnuHashTab)
      In.GnuHashTab = nullptr;
    if (S == In.HashTab)
      In.HashTab = nullptr;

    S->Assigned = false;
    S->Live = false;
    discard(S->DependentSections);
  }
}

std::vector<InputSection *>
LinkerScript::createInputSectionList(OutputSection &OutCmd) {
  std::vector<InputSection *> Ret;

  for (BaseCommand *Base : OutCmd.SectionCommands) {
    if (auto *Cmd = dyn_cast<InputSectionDescription>(Base)) {
      Cmd->Sections = computeInputSections(Cmd);
      Ret.insert(Ret.end(), Cmd->Sections.begin(), Cmd->Sections.end());
    }
  }
  return Ret;
}

void LinkerScript::processSectionCommands() {
  // A symbol can be assigned before any section is mentioned in the linker
  // script. In an DSO, the symbol values are addresses, so the only important
  // section values are:
  // * SHN_UNDEF
  // * SHN_ABS
  // * Any value meaning a regular section.
  // To handle that, create a dummy aether section that fills the void before
  // the linker scripts switches to another section. It has an index of one
  // which will map to whatever the first actual section is.
  Aether = make<OutputSection>("", 0, SHF_ALLOC);
  Aether->SectionIndex = 1;

  // Ctx captures the local AddressState and makes it accessible deliberately.
  // This is needed as there are some cases where we cannot just
  // thread the current state through to a lambda function created by the
  // script parser.
  auto Deleter = make_unique<AddressState>();
  Ctx = Deleter.get();
  Ctx->OutSec = Aether;

  size_t I = 0;
  // Add input sections to output sections.
  for (BaseCommand *Base : SectionCommands) {
    // Handle symbol assignments outside of any output section.
    if (auto *Cmd = dyn_cast<SymbolAssignment>(Base)) {
      addSymbol(Cmd);
      continue;
    }

    if (auto *Sec = dyn_cast<OutputSection>(Base)) {
      std::vector<InputSection *> V = createInputSectionList(*Sec);

      // The output section name `/DISCARD/' is special.
      // Any input section assigned to it is discarded.
      if (Sec->Name == "/DISCARD/") {
        discard(V);
        Sec->SectionCommands.clear();
        continue;
      }

      // This is for ONLY_IF_RO and ONLY_IF_RW. An output section directive
      // ".foo : ONLY_IF_R[OW] { ... }" is handled only if all member input
      // sections satisfy a given constraint. If not, a directive is handled
      // as if it wasn't present from the beginning.
      //
      // Because we'll iterate over SectionCommands many more times, the easy
      // way to "make it as if it wasn't present" is to make it empty.
      if (!matchConstraints(V, Sec->Constraint)) {
        for (InputSectionBase *S : V)
          S->Assigned = false;
        Sec->SectionCommands.clear();
        continue;
      }

      // A directive may contain symbol definitions like this:
      // ".foo : { ...; bar = .; }". Handle them.
      for (BaseCommand *Base : Sec->SectionCommands)
        if (auto *OutCmd = dyn_cast<SymbolAssignment>(Base))
          addSymbol(OutCmd);

      // Handle subalign (e.g. ".foo : SUBALIGN(32) { ... }"). If subalign
      // is given, input sections are aligned to that value, whether the
      // given value is larger or smaller than the original section alignment.
      if (Sec->SubalignExpr) {
        uint32_t Subalign = Sec->SubalignExpr().getValue();
        for (InputSectionBase *S : V)
          S->Alignment = Subalign;
      }

      // Add input sections to an output section.
      for (InputSection *S : V)
        Sec->addSection(S);

      Sec->SectionIndex = I++;
      if (Sec->Noload)
        Sec->Type = SHT_NOBITS;
      if (Sec->NonAlloc)
        Sec->Flags &= ~(uint64_t)SHF_ALLOC;
    }
  }
  Ctx = nullptr;
}

static OutputSection *findByName(ArrayRef<BaseCommand *> Vec,
                                 StringRef Name) {
  for (BaseCommand *Base : Vec)
    if (auto *Sec = dyn_cast<OutputSection>(Base))
      if (Sec->Name == Name)
        return Sec;
  return nullptr;
}

static OutputSection *createSection(InputSectionBase *IS,
                                    StringRef OutsecName) {
  OutputSection *Sec = Script->createOutputSection(OutsecName, "<internal>");
  Sec->addSection(cast<InputSection>(IS));
  return Sec;
}

static OutputSection *addInputSec(StringMap<OutputSection *> &Map,
                                  InputSectionBase *IS, StringRef OutsecName) {
  // Sections with SHT_GROUP or SHF_GROUP attributes reach here only when the -r
  // option is given. A section with SHT_GROUP defines a "section group", and
  // its members have SHF_GROUP attribute. Usually these flags have already been
  // stripped by InputFiles.cpp as section groups are processed and uniquified.
  // However, for the -r option, we want to pass through all section groups
  // as-is because adding/removing members or merging them with other groups
  // change their semantics.
  if (IS->Type == SHT_GROUP || (IS->Flags & SHF_GROUP))
    return createSection(IS, OutsecName);

  // Imagine .zed : { *(.foo) *(.bar) } script. Both foo and bar may have
  // relocation sections .rela.foo and .rela.bar for example. Most tools do
  // not allow multiple REL[A] sections for output section. Hence we
  // should combine these relocation sections into single output.
  // We skip synthetic sections because it can be .rela.dyn/.rela.plt or any
  // other REL[A] sections created by linker itself.
  if (!isa<SyntheticSection>(IS) &&
      (IS->Type == SHT_REL || IS->Type == SHT_RELA)) {
    auto *Sec = cast<InputSection>(IS);
    OutputSection *Out = Sec->getRelocatedSection()->getOutputSection();

    if (Out->RelocationSection) {
      Out->RelocationSection->addSection(Sec);
      return nullptr;
    }

    Out->RelocationSection = createSection(IS, OutsecName);
    return Out->RelocationSection;
  }

  // When control reaches here, mergeable sections have already been merged into
  // synthetic sections. For relocatable case we want to create one output
  // section per syntetic section so that they have a valid sh_entsize.
  if (Config->Relocatable && (IS->Flags & SHF_MERGE))
    return createSection(IS, OutsecName);

  //  The ELF spec just says
  // ----------------------------------------------------------------
  // In the first phase, input sections that match in name, type and
  // attribute flags should be concatenated into single sections.
  // ----------------------------------------------------------------
  //
  // However, it is clear that at least some flags have to be ignored for
  // section merging. At the very least SHF_GROUP and SHF_COMPRESSED have to be
  // ignored. We should not have two output .text sections just because one was
  // in a group and another was not for example.
  //
  // It also seems that wording was a late addition and didn't get the
  // necessary scrutiny.
  //
  // Merging sections with different flags is expected by some users. One
  // reason is that if one file has
  //
  // int *const bar __attribute__((section(".foo"))) = (int *)0;
  //
  // gcc with -fPIC will produce a read only .foo section. But if another
  // file has
  //
  // int zed;
  // int *const bar __attribute__((section(".foo"))) = (int *)&zed;
  //
  // gcc with -fPIC will produce a read write section.
  //
  // Last but not least, when using linker script the merge rules are forced by
  // the script. Unfortunately, linker scripts are name based. This means that
  // expressions like *(.foo*) can refer to multiple input sections with
  // different flags. We cannot put them in different output sections or we
  // would produce wrong results for
  //
  // start = .; *(.foo.*) end = .; *(.bar)
  //
  // and a mapping of .foo1 and .bar1 to one section and .foo2 and .bar2 to
  // another. The problem is that there is no way to layout those output
  // sections such that the .foo sections are the only thing between the start
  // and end symbols.
  //
  // Given the above issues, we instead merge sections by name and error on
  // incompatible types and flags.
  OutputSection *&Sec = Map[OutsecName];
  if (Sec) {
    Sec->addSection(cast<InputSection>(IS));
    return nullptr;
  }

  Sec = createSection(IS, OutsecName);
  return Sec;
}

// Add sections that didn't match any sections command.
void LinkerScript::addOrphanSections() {
  unsigned End = SectionCommands.size();
  StringMap<OutputSection *> Map;
  std::vector<OutputSection *> V;

  auto Add = [&](InputSectionBase *S) {
    if (!S->Live || S->Parent)
      return;

    StringRef Name = getOutputSectionName(S);

    if (Config->OrphanHandling == OrphanHandlingPolicy::Error)
      error(toString(S) + " is being placed in '" + Name + "'");
    else if (Config->OrphanHandling == OrphanHandlingPolicy::Warn)
      warn(toString(S) + " is being placed in '" + Name + "'");

    if (OutputSection *Sec =
            findByName(makeArrayRef(SectionCommands).slice(0, End), Name)) {
      Sec->addSection(cast<InputSection>(S));
      return;
    }

    if (OutputSection *OS = addInputSec(Map, S, Name))
      V.push_back(OS);
    assert(S->getOutputSection()->SectionIndex == UINT32_MAX);
  };

  // For futher --emit-reloc handling code we need target output section
  // to be created before we create relocation output section, so we want
  // to create target sections first. We do not want priority handling
  // for synthetic sections because them are special.
  for (InputSectionBase *IS : InputSections) {
    if (auto *Sec = dyn_cast<InputSection>(IS))
      if (InputSectionBase *Rel = Sec->getRelocatedSection())
        if (auto *RelIS = dyn_cast_or_null<InputSectionBase>(Rel->Parent))
          Add(RelIS);
    Add(IS);
  }

  // If no SECTIONS command was given, we should insert sections commands
  // before others, so that we can handle scripts which refers them,
  // for example: "foo = ABSOLUTE(ADDR(.text)));".
  // When SECTIONS command is present we just add all orphans to the end.
  if (HasSectionsCommand)
    SectionCommands.insert(SectionCommands.end(), V.begin(), V.end());
  else
    SectionCommands.insert(SectionCommands.begin(), V.begin(), V.end());
}

uint64_t LinkerScript::advance(uint64_t Size, unsigned Alignment) {
  bool IsTbss =
      (Ctx->OutSec->Flags & SHF_TLS) && Ctx->OutSec->Type == SHT_NOBITS;
  uint64_t Start = IsTbss ? Dot + Ctx->ThreadBssOffset : Dot;
  Start = alignTo(Start, Alignment);
  uint64_t End = Start + Size;

  if (IsTbss)
    Ctx->ThreadBssOffset = End - Dot;
  else
    Dot = End;
  return End;
}

void LinkerScript::output(InputSection *S) {
  assert(Ctx->OutSec == S->getParent());
  uint64_t Before = advance(0, 1);
  uint64_t Pos = advance(S->getSize(), S->Alignment);
  S->OutSecOff = Pos - S->getSize() - Ctx->OutSec->Addr;

  // Update output section size after adding each section. This is so that
  // SIZEOF works correctly in the case below:
  // .foo { *(.aaa) a = SIZEOF(.foo); *(.bbb) }
  expandOutputSection(Pos - Before);
}

void LinkerScript::switchTo(OutputSection *Sec) {
  Ctx->OutSec = Sec;

  uint64_t Before = advance(0, 1);
  Ctx->OutSec->Addr = advance(0, Ctx->OutSec->Alignment);
  expandMemoryRegions(Ctx->OutSec->Addr - Before);
}

// This function searches for a memory region to place the given output
// section in. If found, a pointer to the appropriate memory region is
// returned. Otherwise, a nullptr is returned.
MemoryRegion *LinkerScript::findMemoryRegion(OutputSection *Sec) {
  // If a memory region name was specified in the output section command,
  // then try to find that region first.
  if (!Sec->MemoryRegionName.empty()) {
    if (MemoryRegion *M = MemoryRegions.lookup(Sec->MemoryRegionName))
      return M;
    error("memory region '" + Sec->MemoryRegionName + "' not declared");
    return nullptr;
  }

  // If at least one memory region is defined, all sections must
  // belong to some memory region. Otherwise, we don't need to do
  // anything for memory regions.
  if (MemoryRegions.empty())
    return nullptr;

  // See if a region can be found by matching section flags.
  for (auto &Pair : MemoryRegions) {
    MemoryRegion *M = Pair.second;
    if ((M->Flags & Sec->Flags) && (M->NegFlags & Sec->Flags) == 0)
      return M;
  }

  // Otherwise, no suitable region was found.
  if (Sec->Flags & SHF_ALLOC)
    error("no memory region specified for section '" + Sec->Name + "'");
  return nullptr;
}

static OutputSection *findFirstSection(PhdrEntry *Load) {
  for (OutputSection *Sec : OutputSections)
    if (Sec->PtLoad == Load)
      return Sec;
  return nullptr;
}

// This function assigns offsets to input sections and an output section
// for a single sections command (e.g. ".text { *(.text); }").
void LinkerScript::assignOffsets(OutputSection *Sec) {
  if (!(Sec->Flags & SHF_ALLOC))
    Dot = 0;
  else if (Sec->AddrExpr)
    setDot(Sec->AddrExpr, Sec->Location, false);

  Ctx->MemRegion = Sec->MemRegion;
  Ctx->LMARegion = Sec->LMARegion;
  if (Ctx->MemRegion)
    Dot = Ctx->MemRegion->CurPos;

  switchTo(Sec);

  if (Sec->LMAExpr)
    Ctx->LMAOffset = Sec->LMAExpr().getValue() - Dot;

  if (MemoryRegion *MR = Sec->LMARegion)
    Ctx->LMAOffset = MR->CurPos - Dot;

  // If neither AT nor AT> is specified for an allocatable section, the linker
  // will set the LMA such that the difference between VMA and LMA for the
  // section is the same as the preceding output section in the same region
  // https://sourceware.org/binutils/docs-2.20/ld/Output-Section-LMA.html
  // This, however, should only be done by the first "non-header" section
  // in the segment.
  if (PhdrEntry *L = Ctx->OutSec->PtLoad)
    if (Sec == findFirstSection(L))
      L->LMAOffset = Ctx->LMAOffset;

  // We can call this method multiple times during the creation of
  // thunks and want to start over calculation each time.
  Sec->Size = 0;

  // We visited SectionsCommands from processSectionCommands to
  // layout sections. Now, we visit SectionsCommands again to fix
  // section offsets.
  for (BaseCommand *Base : Sec->SectionCommands) {
    // This handles the assignments to symbol or to the dot.
    if (auto *Cmd = dyn_cast<SymbolAssignment>(Base)) {
      Cmd->Addr = Dot;
      assignSymbol(Cmd, true);
      Cmd->Size = Dot - Cmd->Addr;
      continue;
    }

    // Handle BYTE(), SHORT(), LONG(), or QUAD().
    if (auto *Cmd = dyn_cast<ByteCommand>(Base)) {
      Cmd->Offset = Dot - Ctx->OutSec->Addr;
      Dot += Cmd->Size;
      expandOutputSection(Cmd->Size);
      continue;
    }

    // Handle a single input section description command.
    // It calculates and assigns the offsets for each section and also
    // updates the output section size.
    for (InputSection *Sec : cast<InputSectionDescription>(Base)->Sections)
      output(Sec);
  }
}

static bool isDiscardable(OutputSection &Sec) {
  // We do not remove empty sections that are explicitly
  // assigned to any segment.
  if (!Sec.Phdrs.empty())
    return false;

  // We do not want to remove sections that reference symbols in address and
  // other expressions. We add script symbols as undefined, and want to ensure
  // all of them are defined in the output, hence have to keep them.
  if (Sec.ExpressionsUseSymbols)
    return false;

  for (BaseCommand *Base : Sec.SectionCommands) {
    if (auto Cmd = dyn_cast<SymbolAssignment>(Base))
      // Don't create empty output sections just for unreferenced PROVIDE
      // symbols.
      if (Cmd->Name != "." && !Cmd->Sym)
        continue;

    if (!isa<InputSectionDescription>(*Base))
      return false;
  }
  return true;
}

void LinkerScript::adjustSectionsBeforeSorting() {
  // If the output section contains only symbol assignments, create a
  // corresponding output section. The issue is what to do with linker script
  // like ".foo : { symbol = 42; }". One option would be to convert it to
  // "symbol = 42;". That is, move the symbol out of the empty section
  // description. That seems to be what bfd does for this simple case. The
  // problem is that this is not completely general. bfd will give up and
  // create a dummy section too if there is a ". = . + 1" inside the section
  // for example.
  // Given that we want to create the section, we have to worry what impact
  // it will have on the link. For example, if we just create a section with
  // 0 for flags, it would change which PT_LOADs are created.
  // We could remember that particular section is dummy and ignore it in
  // other parts of the linker, but unfortunately there are quite a few places
  // that would need to change:
  //   * The program header creation.
  //   * The orphan section placement.
  //   * The address assignment.
  // The other option is to pick flags that minimize the impact the section
  // will have on the rest of the linker. That is why we copy the flags from
  // the previous sections. Only a few flags are needed to keep the impact low.
  uint64_t Flags = SHF_ALLOC;

  for (BaseCommand *&Cmd : SectionCommands) {
    auto *Sec = dyn_cast<OutputSection>(Cmd);
    if (!Sec)
      continue;

    // Handle align (e.g. ".foo : ALIGN(16) { ... }").
    if (Sec->AlignExpr)
      Sec->Alignment =
          std::max<uint32_t>(Sec->Alignment, Sec->AlignExpr().getValue());

    // A live output section means that some input section was added to it. It
    // might have been removed (if it was empty synthetic section), but we at
    // least know the flags.
    if (Sec->Live)
      Flags = Sec->Flags;

    // We do not want to keep any special flags for output section
    // in case it is empty.
    bool IsEmpty = getInputSections(Sec).empty();
    if (IsEmpty)
      Sec->Flags = Flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR);

    if (IsEmpty && isDiscardable(*Sec)) {
      Sec->Live = false;
      Cmd = nullptr;
    }
  }

  // It is common practice to use very generic linker scripts. So for any
  // given run some of the output sections in the script will be empty.
  // We could create corresponding empty output sections, but that would
  // clutter the output.
  // We instead remove trivially empty sections. The bfd linker seems even
  // more aggressive at removing them.
  llvm::erase_if(SectionCommands, [&](BaseCommand *Base) { return !Base; });
}

void LinkerScript::adjustSectionsAfterSorting() {
  // Try and find an appropriate memory region to assign offsets in.
  for (BaseCommand *Base : SectionCommands) {
    if (auto *Sec = dyn_cast<OutputSection>(Base)) {
      if (!Sec->LMARegionName.empty()) {
        if (MemoryRegion *M = MemoryRegions.lookup(Sec->LMARegionName))
          Sec->LMARegion = M;
        else
          error("memory region '" + Sec->LMARegionName + "' not declared");
      }
      Sec->MemRegion = findMemoryRegion(Sec);
    }
  }

  // If output section command doesn't specify any segments,
  // and we haven't previously assigned any section to segment,
  // then we simply assign section to the very first load segment.
  // Below is an example of such linker script:
  // PHDRS { seg PT_LOAD; }
  // SECTIONS { .aaa : { *(.aaa) } }
  std::vector<StringRef> DefPhdrs;
  auto FirstPtLoad = llvm::find_if(PhdrsCommands, [](const PhdrsCommand &Cmd) {
    return Cmd.Type == PT_LOAD;
  });
  if (FirstPtLoad != PhdrsCommands.end())
    DefPhdrs.push_back(FirstPtLoad->Name);

  // Walk the commands and propagate the program headers to commands that don't
  // explicitly specify them.
  for (BaseCommand *Base : SectionCommands) {
    auto *Sec = dyn_cast<OutputSection>(Base);
    if (!Sec)
      continue;

    if (Sec->Phdrs.empty()) {
      // To match the bfd linker script behaviour, only propagate program
      // headers to sections that are allocated.
      if (Sec->Flags & SHF_ALLOC)
        Sec->Phdrs = DefPhdrs;
    } else {
      DefPhdrs = Sec->Phdrs;
    }
  }
}

static uint64_t computeBase(uint64_t Min, bool AllocateHeaders) {
  // If there is no SECTIONS or if the linkerscript is explicit about program
  // headers, do our best to allocate them.
  if (!Script->HasSectionsCommand || AllocateHeaders)
    return 0;
  // Otherwise only allocate program headers if that would not add a page.
  return alignDown(Min, Config->MaxPageSize);
}

// Try to find an address for the file and program headers output sections,
// which were unconditionally added to the first PT_LOAD segment earlier.
//
// When using the default layout, we check if the headers fit below the first
// allocated section. When using a linker script, we also check if the headers
// are covered by the output section. This allows omitting the headers by not
// leaving enough space for them in the linker script; this pattern is common
// in embedded systems.
//
// If there isn't enough space for these sections, we'll remove them from the
// PT_LOAD segment, and we'll also remove the PT_PHDR segment.
void LinkerScript::allocateHeaders(std::vector<PhdrEntry *> &Phdrs) {
  uint64_t Min = std::numeric_limits<uint64_t>::max();
  for (OutputSection *Sec : OutputSections)
    if (Sec->Flags & SHF_ALLOC)
      Min = std::min<uint64_t>(Min, Sec->Addr);

  auto It = llvm::find_if(
      Phdrs, [](const PhdrEntry *E) { return E->p_type == PT_LOAD; });
  if (It == Phdrs.end())
    return;
  PhdrEntry *FirstPTLoad = *It;

  bool HasExplicitHeaders =
      llvm::any_of(PhdrsCommands, [](const PhdrsCommand &Cmd) {
        return Cmd.HasPhdrs || Cmd.HasFilehdr;
      });
  uint64_t HeaderSize = getHeaderSize();
  if (HeaderSize <= Min - computeBase(Min, HasExplicitHeaders)) {
    Min = alignDown(Min - HeaderSize, Config->MaxPageSize);
    Out::ElfHeader->Addr = Min;
    Out::ProgramHeaders->Addr = Min + Out::ElfHeader->Size;
    return;
  }

  // Error if we were explicitly asked to allocate headers.
  if (HasExplicitHeaders)
    error("could not allocate headers");

  Out::ElfHeader->PtLoad = nullptr;
  Out::ProgramHeaders->PtLoad = nullptr;
  FirstPTLoad->FirstSec = findFirstSection(FirstPTLoad);

  llvm::erase_if(Phdrs,
                 [](const PhdrEntry *E) { return E->p_type == PT_PHDR; });
}

LinkerScript::AddressState::AddressState() {
  for (auto &MRI : Script->MemoryRegions) {
    MemoryRegion *MR = MRI.second;
    MR->CurPos = MR->Origin;
  }
}

static uint64_t getInitialDot() {
  // By default linker scripts use an initial value of 0 for '.',
  // but prefer -image-base if set.
  if (Script->HasSectionsCommand)
    return Config->ImageBase ? *Config->ImageBase : 0;

  uint64_t StartAddr = UINT64_MAX;
  // The Sections with -T<section> have been sorted in order of ascending
  // address. We must lower StartAddr if the lowest -T<section address> as
  // calls to setDot() must be monotonically increasing.
  for (auto &KV : Config->SectionStartMap)
    StartAddr = std::min(StartAddr, KV.second);
  return std::min(StartAddr, Target->getImageBase() + elf::getHeaderSize());
}

// Here we assign addresses as instructed by linker script SECTIONS
// sub-commands. Doing that allows us to use final VA values, so here
// we also handle rest commands like symbol assignments and ASSERTs.
void LinkerScript::assignAddresses() {
  Dot = getInitialDot();

  auto Deleter = make_unique<AddressState>();
  Ctx = Deleter.get();
  ErrorOnMissingSection = true;
  switchTo(Aether);

  for (BaseCommand *Base : SectionCommands) {
    if (auto *Cmd = dyn_cast<SymbolAssignment>(Base)) {
      Cmd->Addr = Dot;
      assignSymbol(Cmd, false);
      Cmd->Size = Dot - Cmd->Addr;
      continue;
    }
    assignOffsets(cast<OutputSection>(Base));
  }
  Ctx = nullptr;
}

// Creates program headers as instructed by PHDRS linker script command.
std::vector<PhdrEntry *> LinkerScript::createPhdrs() {
  std::vector<PhdrEntry *> Ret;

  // Process PHDRS and FILEHDR keywords because they are not
  // real output sections and cannot be added in the following loop.
  for (const PhdrsCommand &Cmd : PhdrsCommands) {
    PhdrEntry *Phdr = make<PhdrEntry>(Cmd.Type, Cmd.Flags ? *Cmd.Flags : PF_R);

    if (Cmd.HasFilehdr)
      Phdr->add(Out::ElfHeader);
    if (Cmd.HasPhdrs)
      Phdr->add(Out::ProgramHeaders);

    if (Cmd.LMAExpr) {
      Phdr->p_paddr = Cmd.LMAExpr().getValue();
      Phdr->HasLMA = true;
    }
    Ret.push_back(Phdr);
  }

  // Add output sections to program headers.
  for (OutputSection *Sec : OutputSections) {
    // Assign headers specified by linker script
    for (size_t Id : getPhdrIndices(Sec)) {
      Ret[Id]->add(Sec);
      if (!PhdrsCommands[Id].Flags.hasValue())
        Ret[Id]->p_flags |= Sec->getPhdrFlags();
    }
  }
  return Ret;
}

// Returns true if we should emit an .interp section.
//
// We usually do. But if PHDRS commands are given, and
// no PT_INTERP is there, there's no place to emit an
// .interp, so we don't do that in that case.
bool LinkerScript::needsInterpSection() {
  if (PhdrsCommands.empty())
    return true;
  for (PhdrsCommand &Cmd : PhdrsCommands)
    if (Cmd.Type == PT_INTERP)
      return true;
  return false;
}

ExprValue LinkerScript::getSymbolValue(StringRef Name, const Twine &Loc) {
  if (Name == ".") {
    if (Ctx)
      return {Ctx->OutSec, false, Dot - Ctx->OutSec->Addr, Loc};
    error(Loc + ": unable to get location counter value");
    return 0;
  }

  if (Symbol *Sym = Symtab->find(Name)) {
    if (auto *DS = dyn_cast<Defined>(Sym))
      return {DS->Section, false, DS->Value, Loc};
    if (isa<SharedSymbol>(Sym))
      if (!ErrorOnMissingSection)
        return {nullptr, false, 0, Loc};
  }

  error(Loc + ": symbol not found: " + Name);
  return 0;
}

// Returns the index of the segment named Name.
static Optional<size_t> getPhdrIndex(ArrayRef<PhdrsCommand> Vec,
                                     StringRef Name) {
  for (size_t I = 0; I < Vec.size(); ++I)
    if (Vec[I].Name == Name)
      return I;
  return None;
}

// Returns indices of ELF headers containing specific section. Each index is a
// zero based number of ELF header listed within PHDRS {} script block.
std::vector<size_t> LinkerScript::getPhdrIndices(OutputSection *Cmd) {
  std::vector<size_t> Ret;

  for (StringRef S : Cmd->Phdrs) {
    if (Optional<size_t> Idx = getPhdrIndex(PhdrsCommands, S))
      Ret.push_back(*Idx);
    else if (S != "NONE")
      error(Cmd->Location + ": section header '" + S +
            "' is not listed in PHDRS");
  }
  return Ret;
}
