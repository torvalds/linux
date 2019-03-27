//===- LinkerScript.h -------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_LINKER_SCRIPT_H
#define LLD_ELF_LINKER_SCRIPT_H

#include "Config.h"
#include "Writer.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lld {
namespace elf {

class Defined;
class InputSection;
class InputSectionBase;
class InputSectionBase;
class OutputSection;
class SectionBase;
class Symbol;
class ThunkSection;

// This represents an r-value in the linker script.
struct ExprValue {
  ExprValue(SectionBase *Sec, bool ForceAbsolute, uint64_t Val,
            const Twine &Loc)
      : Sec(Sec), ForceAbsolute(ForceAbsolute), Val(Val), Loc(Loc.str()) {}

  ExprValue(uint64_t Val) : ExprValue(nullptr, false, Val, "") {}

  bool isAbsolute() const { return ForceAbsolute || Sec == nullptr; }
  uint64_t getValue() const;
  uint64_t getSecAddr() const;
  uint64_t getSectionOffset() const;

  // If a value is relative to a section, it has a non-null Sec.
  SectionBase *Sec;

  // True if this expression is enclosed in ABSOLUTE().
  // This flag affects the return value of getValue().
  bool ForceAbsolute;

  uint64_t Val;
  uint64_t Alignment = 1;

  // Original source location. Used for error messages.
  std::string Loc;
};

// This represents an expression in the linker script.
// ScriptParser::readExpr reads an expression and returns an Expr.
// Later, we evaluate the expression by calling the function.
typedef std::function<ExprValue()> Expr;

// This enum is used to implement linker script SECTIONS command.
// https://sourceware.org/binutils/docs/ld/SECTIONS.html#SECTIONS
enum SectionsCommandKind {
  AssignmentKind, // . = expr or <sym> = expr
  OutputSectionKind,
  InputSectionKind,
  ByteKind    // BYTE(expr), SHORT(expr), LONG(expr) or QUAD(expr)
};

struct BaseCommand {
  BaseCommand(int K) : Kind(K) {}
  int Kind;
};

// This represents ". = <expr>" or "<symbol> = <expr>".
struct SymbolAssignment : BaseCommand {
  SymbolAssignment(StringRef Name, Expr E, std::string Loc)
      : BaseCommand(AssignmentKind), Name(Name), Expression(E), Location(Loc) {}

  static bool classof(const BaseCommand *C) {
    return C->Kind == AssignmentKind;
  }

  // The LHS of an expression. Name is either a symbol name or ".".
  StringRef Name;
  Defined *Sym = nullptr;

  // The RHS of an expression.
  Expr Expression;

  // Command attributes for PROVIDE, HIDDEN and PROVIDE_HIDDEN.
  bool Provide = false;
  bool Hidden = false;

  // Holds file name and line number for error reporting.
  std::string Location;

  // A string representation of this command. We use this for -Map.
  std::string CommandString;

  // Address of this assignment command.
  unsigned Addr;

  // Size of this assignment command. This is usually 0, but if
  // you move '.' this may be greater than 0.
  unsigned Size;
};

// Linker scripts allow additional constraints to be put on ouput sections.
// If an output section is marked as ONLY_IF_RO, the section is created
// only if its input sections are read-only. Likewise, an output section
// with ONLY_IF_RW is created if all input sections are RW.
enum class ConstraintKind { NoConstraint, ReadOnly, ReadWrite };

// This struct is used to represent the location and size of regions of
// target memory. Instances of the struct are created by parsing the
// MEMORY command.
struct MemoryRegion {
  MemoryRegion(StringRef Name, uint64_t Origin, uint64_t Length, uint32_t Flags,
               uint32_t NegFlags)
      : Name(Name), Origin(Origin), Length(Length), Flags(Flags),
        NegFlags(NegFlags) {}

  std::string Name;
  uint64_t Origin;
  uint64_t Length;
  uint32_t Flags;
  uint32_t NegFlags;
  uint64_t CurPos = 0;
};

// This struct represents one section match pattern in SECTIONS() command.
// It can optionally have negative match pattern for EXCLUDED_FILE command.
// Also it may be surrounded with SORT() command, so contains sorting rules.
struct SectionPattern {
  SectionPattern(StringMatcher &&Pat1, StringMatcher &&Pat2)
      : ExcludedFilePat(Pat1), SectionPat(Pat2),
        SortOuter(SortSectionPolicy::Default),
        SortInner(SortSectionPolicy::Default) {}

  StringMatcher ExcludedFilePat;
  StringMatcher SectionPat;
  SortSectionPolicy SortOuter;
  SortSectionPolicy SortInner;
};

struct InputSectionDescription : BaseCommand {
  InputSectionDescription(StringRef FilePattern)
      : BaseCommand(InputSectionKind), FilePat(FilePattern) {}

  static bool classof(const BaseCommand *C) {
    return C->Kind == InputSectionKind;
  }

  StringMatcher FilePat;

  // Input sections that matches at least one of SectionPatterns
  // will be associated with this InputSectionDescription.
  std::vector<SectionPattern> SectionPatterns;

  std::vector<InputSection *> Sections;

  // Temporary record of synthetic ThunkSection instances and the pass that
  // they were created in. This is used to insert newly created ThunkSections
  // into Sections at the end of a createThunks() pass.
  std::vector<std::pair<ThunkSection *, uint32_t>> ThunkSections;
};

// Represents BYTE(), SHORT(), LONG(), or QUAD().
struct ByteCommand : BaseCommand {
  ByteCommand(Expr E, unsigned Size, std::string CommandString)
      : BaseCommand(ByteKind), CommandString(CommandString), Expression(E),
        Size(Size) {}

  static bool classof(const BaseCommand *C) { return C->Kind == ByteKind; }

  // Keeps string representing the command. Used for -Map" is perhaps better.
  std::string CommandString;

  Expr Expression;

  // This is just an offset of this assignment command in the output section.
  unsigned Offset;

  // Size of this data command.
  unsigned Size;
};

struct PhdrsCommand {
  StringRef Name;
  unsigned Type = llvm::ELF::PT_NULL;
  bool HasFilehdr = false;
  bool HasPhdrs = false;
  llvm::Optional<unsigned> Flags;
  Expr LMAExpr = nullptr;
};

class LinkerScript final {
  // Temporary state used in processSectionCommands() and assignAddresses()
  // that must be reinitialized for each call to the above functions, and must
  // not be used outside of the scope of a call to the above functions.
  struct AddressState {
    AddressState();
    uint64_t ThreadBssOffset = 0;
    OutputSection *OutSec = nullptr;
    MemoryRegion *MemRegion = nullptr;
    MemoryRegion *LMARegion = nullptr;
    uint64_t LMAOffset = 0;
  };

  llvm::DenseMap<StringRef, OutputSection *> NameToOutputSection;

  void addSymbol(SymbolAssignment *Cmd);
  void assignSymbol(SymbolAssignment *Cmd, bool InSec);
  void setDot(Expr E, const Twine &Loc, bool InSec);
  void expandOutputSection(uint64_t Size);
  void expandMemoryRegions(uint64_t Size);

  std::vector<InputSection *>
  computeInputSections(const InputSectionDescription *);

  std::vector<InputSection *> createInputSectionList(OutputSection &Cmd);

  std::vector<size_t> getPhdrIndices(OutputSection *Sec);

  MemoryRegion *findMemoryRegion(OutputSection *Sec);

  void switchTo(OutputSection *Sec);
  uint64_t advance(uint64_t Size, unsigned Align);
  void output(InputSection *Sec);

  void assignOffsets(OutputSection *Sec);

  // Ctx captures the local AddressState and makes it accessible
  // deliberately. This is needed as there are some cases where we cannot just
  // thread the current state through to a lambda function created by the
  // script parser.
  // This should remain a plain pointer as its lifetime is smaller than
  // LinkerScript.
  AddressState *Ctx = nullptr;

  OutputSection *Aether;

  uint64_t Dot;

public:
  OutputSection *createOutputSection(StringRef Name, StringRef Location);
  OutputSection *getOrCreateOutputSection(StringRef Name);

  bool hasPhdrsCommands() { return !PhdrsCommands.empty(); }
  uint64_t getDot() { return Dot; }
  void discard(ArrayRef<InputSection *> V);

  ExprValue getSymbolValue(StringRef Name, const Twine &Loc);

  void addOrphanSections();
  void adjustSectionsBeforeSorting();
  void adjustSectionsAfterSorting();

  std::vector<PhdrEntry *> createPhdrs();
  bool needsInterpSection();

  bool shouldKeep(InputSectionBase *S);
  void assignAddresses();
  void allocateHeaders(std::vector<PhdrEntry *> &Phdrs);
  void processSectionCommands();
  void declareSymbols();

  // Used to handle INSERT AFTER statements.
  void processInsertCommands();

  // SECTIONS command list.
  std::vector<BaseCommand *> SectionCommands;

  // PHDRS command list.
  std::vector<PhdrsCommand> PhdrsCommands;

  bool HasSectionsCommand = false;
  bool ErrorOnMissingSection = false;

  // List of section patterns specified with KEEP commands. They will
  // be kept even if they are unused and --gc-sections is specified.
  std::vector<InputSectionDescription *> KeptSections;

  // A map from memory region name to a memory region descriptor.
  llvm::MapVector<llvm::StringRef, MemoryRegion *> MemoryRegions;

  // A list of symbols referenced by the script.
  std::vector<llvm::StringRef> ReferencedSymbols;

  // Used to implement INSERT [AFTER|BEFORE]. Contains commands that need
  // to be inserted into SECTIONS commands list.
  llvm::DenseMap<StringRef, std::vector<BaseCommand *>> InsertAfterCommands;
  llvm::DenseMap<StringRef, std::vector<BaseCommand *>> InsertBeforeCommands;
};

extern LinkerScript *Script;

} // end namespace elf
} // end namespace lld

#endif // LLD_ELF_LINKER_SCRIPT_H
