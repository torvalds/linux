//===- ScriptParser.cpp ---------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a recursive-descendent parser for linker scripts.
// Parsed results are stored to Config and Script global objects.
//
//===----------------------------------------------------------------------===//

#include "ScriptParser.h"
#include "Config.h"
#include "Driver.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "ScriptLexer.h"
#include "Symbols.h"
#include "Target.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <cassert>
#include <limits>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

static bool isUnderSysroot(StringRef Path);

namespace {
class ScriptParser final : ScriptLexer {
public:
  ScriptParser(MemoryBufferRef MB)
      : ScriptLexer(MB),
        IsUnderSysroot(isUnderSysroot(MB.getBufferIdentifier())) {}

  void readLinkerScript();
  void readVersionScript();
  void readDynamicList();
  void readDefsym(StringRef Name);

private:
  void addFile(StringRef Path);

  void readAsNeeded();
  void readEntry();
  void readExtern();
  void readGroup();
  void readInclude();
  void readInput();
  void readMemory();
  void readOutput();
  void readOutputArch();
  void readOutputFormat();
  void readPhdrs();
  void readRegionAlias();
  void readSearchDir();
  void readSections();
  void readTarget();
  void readVersion();
  void readVersionScriptCommand();

  SymbolAssignment *readSymbolAssignment(StringRef Name);
  ByteCommand *readByteCommand(StringRef Tok);
  std::array<uint8_t, 4> readFill();
  std::array<uint8_t, 4> parseFill(StringRef Tok);
  bool readSectionDirective(OutputSection *Cmd, StringRef Tok1, StringRef Tok2);
  void readSectionAddressType(OutputSection *Cmd);
  OutputSection *readOverlaySectionDescription();
  OutputSection *readOutputSectionDescription(StringRef OutSec);
  std::vector<BaseCommand *> readOverlay();
  std::vector<StringRef> readOutputSectionPhdrs();
  InputSectionDescription *readInputSectionDescription(StringRef Tok);
  StringMatcher readFilePatterns();
  std::vector<SectionPattern> readInputSectionsList();
  InputSectionDescription *readInputSectionRules(StringRef FilePattern);
  unsigned readPhdrType();
  SortSectionPolicy readSortKind();
  SymbolAssignment *readProvideHidden(bool Provide, bool Hidden);
  SymbolAssignment *readAssignment(StringRef Tok);
  void readSort();
  Expr readAssert();
  Expr readConstant();
  Expr getPageSize();

  uint64_t readMemoryAssignment(StringRef, StringRef, StringRef);
  std::pair<uint32_t, uint32_t> readMemoryAttributes();

  Expr combine(StringRef Op, Expr L, Expr R);
  Expr readExpr();
  Expr readExpr1(Expr Lhs, int MinPrec);
  StringRef readParenLiteral();
  Expr readPrimary();
  Expr readTernary(Expr Cond);
  Expr readParenExpr();

  // For parsing version script.
  std::vector<SymbolVersion> readVersionExtern();
  void readAnonymousDeclaration();
  void readVersionDeclaration(StringRef VerStr);

  std::pair<std::vector<SymbolVersion>, std::vector<SymbolVersion>>
  readSymbols();

  // True if a script being read is in a subdirectory specified by -sysroot.
  bool IsUnderSysroot;

  // A set to detect an INCLUDE() cycle.
  StringSet<> Seen;
};
} // namespace

static StringRef unquote(StringRef S) {
  if (S.startswith("\""))
    return S.substr(1, S.size() - 2);
  return S;
}

static bool isUnderSysroot(StringRef Path) {
  if (Config->Sysroot == "")
    return false;
  for (; !Path.empty(); Path = sys::path::parent_path(Path))
    if (sys::fs::equivalent(Config->Sysroot, Path))
      return true;
  return false;
}

// Some operations only support one non absolute value. Move the
// absolute one to the right hand side for convenience.
static void moveAbsRight(ExprValue &A, ExprValue &B) {
  if (A.Sec == nullptr || (A.ForceAbsolute && !B.isAbsolute()))
    std::swap(A, B);
  if (!B.isAbsolute())
    error(A.Loc + ": at least one side of the expression must be absolute");
}

static ExprValue add(ExprValue A, ExprValue B) {
  moveAbsRight(A, B);
  return {A.Sec, A.ForceAbsolute, A.getSectionOffset() + B.getValue(), A.Loc};
}

static ExprValue sub(ExprValue A, ExprValue B) {
  // The distance between two symbols in sections is absolute.
  if (!A.isAbsolute() && !B.isAbsolute())
    return A.getValue() - B.getValue();
  return {A.Sec, false, A.getSectionOffset() - B.getValue(), A.Loc};
}

static ExprValue bitAnd(ExprValue A, ExprValue B) {
  moveAbsRight(A, B);
  return {A.Sec, A.ForceAbsolute,
          (A.getValue() & B.getValue()) - A.getSecAddr(), A.Loc};
}

static ExprValue bitOr(ExprValue A, ExprValue B) {
  moveAbsRight(A, B);
  return {A.Sec, A.ForceAbsolute,
          (A.getValue() | B.getValue()) - A.getSecAddr(), A.Loc};
}

void ScriptParser::readDynamicList() {
  Config->HasDynamicList = true;
  expect("{");
  std::vector<SymbolVersion> Locals;
  std::vector<SymbolVersion> Globals;
  std::tie(Locals, Globals) = readSymbols();
  expect(";");

  if (!atEOF()) {
    setError("EOF expected, but got " + next());
    return;
  }
  if (!Locals.empty()) {
    setError("\"local:\" scope not supported in --dynamic-list");
    return;
  }

  for (SymbolVersion V : Globals)
    Config->DynamicList.push_back(V);
}

void ScriptParser::readVersionScript() {
  readVersionScriptCommand();
  if (!atEOF())
    setError("EOF expected, but got " + next());
}

void ScriptParser::readVersionScriptCommand() {
  if (consume("{")) {
    readAnonymousDeclaration();
    return;
  }

  while (!atEOF() && !errorCount() && peek() != "}") {
    StringRef VerStr = next();
    if (VerStr == "{") {
      setError("anonymous version definition is used in "
               "combination with other version definitions");
      return;
    }
    expect("{");
    readVersionDeclaration(VerStr);
  }
}

void ScriptParser::readVersion() {
  expect("{");
  readVersionScriptCommand();
  expect("}");
}

void ScriptParser::readLinkerScript() {
  while (!atEOF()) {
    StringRef Tok = next();
    if (Tok == ";")
      continue;

    if (Tok == "ENTRY") {
      readEntry();
    } else if (Tok == "EXTERN") {
      readExtern();
    } else if (Tok == "GROUP") {
      readGroup();
    } else if (Tok == "INCLUDE") {
      readInclude();
    } else if (Tok == "INPUT") {
      readInput();
    } else if (Tok == "MEMORY") {
      readMemory();
    } else if (Tok == "OUTPUT") {
      readOutput();
    } else if (Tok == "OUTPUT_ARCH") {
      readOutputArch();
    } else if (Tok == "OUTPUT_FORMAT") {
      readOutputFormat();
    } else if (Tok == "PHDRS") {
      readPhdrs();
    } else if (Tok == "REGION_ALIAS") {
      readRegionAlias();
    } else if (Tok == "SEARCH_DIR") {
      readSearchDir();
    } else if (Tok == "SECTIONS") {
      readSections();
    } else if (Tok == "TARGET") {
      readTarget();
    } else if (Tok == "VERSION") {
      readVersion();
    } else if (SymbolAssignment *Cmd = readAssignment(Tok)) {
      Script->SectionCommands.push_back(Cmd);
    } else {
      setError("unknown directive: " + Tok);
    }
  }
}

void ScriptParser::readDefsym(StringRef Name) {
  if (errorCount())
    return;
  Expr E = readExpr();
  if (!atEOF())
    setError("EOF expected, but got " + next());
  SymbolAssignment *Cmd = make<SymbolAssignment>(Name, E, getCurrentLocation());
  Script->SectionCommands.push_back(Cmd);
}

void ScriptParser::addFile(StringRef S) {
  if (IsUnderSysroot && S.startswith("/")) {
    SmallString<128> PathData;
    StringRef Path = (Config->Sysroot + S).toStringRef(PathData);
    if (sys::fs::exists(Path)) {
      Driver->addFile(Saver.save(Path), /*WithLOption=*/false);
      return;
    }
  }

  if (S.startswith("/")) {
    Driver->addFile(S, /*WithLOption=*/false);
  } else if (S.startswith("=")) {
    if (Config->Sysroot.empty())
      Driver->addFile(S.substr(1), /*WithLOption=*/false);
    else
      Driver->addFile(Saver.save(Config->Sysroot + "/" + S.substr(1)),
                      /*WithLOption=*/false);
  } else if (S.startswith("-l")) {
    Driver->addLibrary(S.substr(2));
  } else if (sys::fs::exists(S)) {
    Driver->addFile(S, /*WithLOption=*/false);
  } else {
    if (Optional<std::string> Path = findFromSearchPaths(S))
      Driver->addFile(Saver.save(*Path), /*WithLOption=*/true);
    else
      setError("unable to find " + S);
  }
}

void ScriptParser::readAsNeeded() {
  expect("(");
  bool Orig = Config->AsNeeded;
  Config->AsNeeded = true;
  while (!errorCount() && !consume(")"))
    addFile(unquote(next()));
  Config->AsNeeded = Orig;
}

void ScriptParser::readEntry() {
  // -e <symbol> takes predecence over ENTRY(<symbol>).
  expect("(");
  StringRef Tok = next();
  if (Config->Entry.empty())
    Config->Entry = Tok;
  expect(")");
}

void ScriptParser::readExtern() {
  expect("(");
  while (!errorCount() && !consume(")"))
    Config->Undefined.push_back(next());
}

void ScriptParser::readGroup() {
  bool Orig = InputFile::IsInGroup;
  InputFile::IsInGroup = true;
  readInput();
  InputFile::IsInGroup = Orig;
  if (!Orig)
    ++InputFile::NextGroupId;
}

void ScriptParser::readInclude() {
  StringRef Tok = unquote(next());

  if (!Seen.insert(Tok).second) {
    setError("there is a cycle in linker script INCLUDEs");
    return;
  }

  if (Optional<std::string> Path = searchScript(Tok)) {
    if (Optional<MemoryBufferRef> MB = readFile(*Path))
      tokenize(*MB);
    return;
  }
  setError("cannot find linker script " + Tok);
}

void ScriptParser::readInput() {
  expect("(");
  while (!errorCount() && !consume(")")) {
    if (consume("AS_NEEDED"))
      readAsNeeded();
    else
      addFile(unquote(next()));
  }
}

void ScriptParser::readOutput() {
  // -o <file> takes predecence over OUTPUT(<file>).
  expect("(");
  StringRef Tok = next();
  if (Config->OutputFile.empty())
    Config->OutputFile = unquote(Tok);
  expect(")");
}

void ScriptParser::readOutputArch() {
  // OUTPUT_ARCH is ignored for now.
  expect("(");
  while (!errorCount() && !consume(")"))
    skip();
}

static std::pair<ELFKind, uint16_t> parseBfdName(StringRef S) {
  return StringSwitch<std::pair<ELFKind, uint16_t>>(S)
      .Case("elf32-i386", {ELF32LEKind, EM_386})
      .Case("elf32-iamcu", {ELF32LEKind, EM_IAMCU})
      .Case("elf32-littlearm", {ELF32LEKind, EM_ARM})
      .Case("elf32-x86-64", {ELF32LEKind, EM_X86_64})
      .Case("elf64-aarch64", {ELF64LEKind, EM_AARCH64})
      .Case("elf64-littleaarch64", {ELF64LEKind, EM_AARCH64})
      .Case("elf32-powerpc", {ELF32BEKind, EM_PPC})
      .Case("elf64-powerpc", {ELF64BEKind, EM_PPC64})
      .Case("elf64-powerpcle", {ELF64LEKind, EM_PPC64})
      .Case("elf64-x86-64", {ELF64LEKind, EM_X86_64})
      .Cases("elf32-tradbigmips", "elf32-bigmips", {ELF32BEKind, EM_MIPS})
      .Case("elf32-ntradbigmips", {ELF32BEKind, EM_MIPS})
      .Case("elf32-tradlittlemips", {ELF32LEKind, EM_MIPS})
      .Case("elf32-ntradlittlemips", {ELF32LEKind, EM_MIPS})
      .Case("elf64-tradbigmips", {ELF64BEKind, EM_MIPS})
      .Case("elf64-tradlittlemips", {ELF64LEKind, EM_MIPS})
      .Default({ELFNoneKind, EM_NONE});
}

// Parse OUTPUT_FORMAT(bfdname) or OUTPUT_FORMAT(bfdname, big, little).
// Currently we ignore big and little parameters.
void ScriptParser::readOutputFormat() {
  expect("(");

  StringRef Name = unquote(next());
  StringRef S = Name;
  if (S.consume_back("-freebsd"))
    Config->OSABI = ELFOSABI_FREEBSD;

  std::tie(Config->EKind, Config->EMachine) = parseBfdName(S);
  if (Config->EMachine == EM_NONE)
    setError("unknown output format name: " + Name);
  if (S == "elf32-ntradlittlemips" || S == "elf32-ntradbigmips")
    Config->MipsN32Abi = true;

  if (consume(")"))
    return;
  expect(",");
  skip();
  expect(",");
  skip();
  expect(")");
}

void ScriptParser::readPhdrs() {
  expect("{");

  while (!errorCount() && !consume("}")) {
    PhdrsCommand Cmd;
    Cmd.Name = next();
    Cmd.Type = readPhdrType();

    while (!errorCount() && !consume(";")) {
      if (consume("FILEHDR"))
        Cmd.HasFilehdr = true;
      else if (consume("PHDRS"))
        Cmd.HasPhdrs = true;
      else if (consume("AT"))
        Cmd.LMAExpr = readParenExpr();
      else if (consume("FLAGS"))
        Cmd.Flags = readParenExpr()().getValue();
      else
        setError("unexpected header attribute: " + next());
    }

    Script->PhdrsCommands.push_back(Cmd);
  }
}

void ScriptParser::readRegionAlias() {
  expect("(");
  StringRef Alias = unquote(next());
  expect(",");
  StringRef Name = next();
  expect(")");

  if (Script->MemoryRegions.count(Alias))
    setError("redefinition of memory region '" + Alias + "'");
  if (!Script->MemoryRegions.count(Name))
    setError("memory region '" + Name + "' is not defined");
  Script->MemoryRegions.insert({Alias, Script->MemoryRegions[Name]});
}

void ScriptParser::readSearchDir() {
  expect("(");
  StringRef Tok = next();
  if (!Config->Nostdlib)
    Config->SearchPaths.push_back(unquote(Tok));
  expect(")");
}

// This reads an overlay description. Overlays are used to describe output
// sections that use the same virtual memory range and normally would trigger
// linker's sections sanity check failures.
// https://sourceware.org/binutils/docs/ld/Overlay-Description.html#Overlay-Description
std::vector<BaseCommand *> ScriptParser::readOverlay() {
  // VA and LMA expressions are optional, though for simplicity of
  // implementation we assume they are not. That is what OVERLAY was designed
  // for first of all: to allow sections with overlapping VAs at different LMAs.
  Expr AddrExpr = readExpr();
  expect(":");
  expect("AT");
  Expr LMAExpr = readParenExpr();
  expect("{");

  std::vector<BaseCommand *> V;
  OutputSection *Prev = nullptr;
  while (!errorCount() && !consume("}")) {
    // VA is the same for all sections. The LMAs are consecutive in memory
    // starting from the base load address specified.
    OutputSection *OS = readOverlaySectionDescription();
    OS->AddrExpr = AddrExpr;
    if (Prev)
      OS->LMAExpr = [=] { return Prev->getLMA() + Prev->Size; };
    else
      OS->LMAExpr = LMAExpr;
    V.push_back(OS);
    Prev = OS;
  }

  // According to the specification, at the end of the overlay, the location
  // counter should be equal to the overlay base address plus size of the
  // largest section seen in the overlay.
  // Here we want to create the Dot assignment command to achieve that.
  Expr MoveDot = [=] {
    uint64_t Max = 0;
    for (BaseCommand *Cmd : V)
      Max = std::max(Max, cast<OutputSection>(Cmd)->Size);
    return AddrExpr().getValue() + Max;
  };
  V.push_back(make<SymbolAssignment>(".", MoveDot, getCurrentLocation()));
  return V;
}

void ScriptParser::readSections() {
  Script->HasSectionsCommand = true;

  // -no-rosegment is used to avoid placing read only non-executable sections in
  // their own segment. We do the same if SECTIONS command is present in linker
  // script. See comment for computeFlags().
  Config->SingleRoRx = true;

  expect("{");
  std::vector<BaseCommand *> V;
  while (!errorCount() && !consume("}")) {
    StringRef Tok = next();
    if (Tok == "OVERLAY") {
      for (BaseCommand *Cmd : readOverlay())
        V.push_back(Cmd);
      continue;
    } else if (Tok == "INCLUDE") {
      readInclude();
      continue;
    }

    if (BaseCommand *Cmd = readAssignment(Tok))
      V.push_back(Cmd);
    else
      V.push_back(readOutputSectionDescription(Tok));
  }

  if (!atEOF() && consume("INSERT")) {
    std::vector<BaseCommand *> *Dest = nullptr;
    if (consume("AFTER"))
      Dest = &Script->InsertAfterCommands[next()];
    else if (consume("BEFORE"))
      Dest = &Script->InsertBeforeCommands[next()];
    else
      setError("expected AFTER/BEFORE, but got '" + next() + "'");
    if (Dest)
      Dest->insert(Dest->end(), V.begin(), V.end());
    return;
  }

  Script->SectionCommands.insert(Script->SectionCommands.end(), V.begin(),
                                 V.end());
}

void ScriptParser::readTarget() {
  // TARGET(foo) is an alias for "--format foo". Unlike GNU linkers,
  // we accept only a limited set of BFD names (i.e. "elf" or "binary")
  // for --format. We recognize only /^elf/ and "binary" in the linker
  // script as well.
  expect("(");
  StringRef Tok = next();
  expect(")");

  if (Tok.startswith("elf"))
    Config->FormatBinary = false;
  else if (Tok == "binary")
    Config->FormatBinary = true;
  else
    setError("unknown target: " + Tok);
}

static int precedence(StringRef Op) {
  return StringSwitch<int>(Op)
      .Cases("*", "/", "%", 8)
      .Cases("+", "-", 7)
      .Cases("<<", ">>", 6)
      .Cases("<", "<=", ">", ">=", "==", "!=", 5)
      .Case("&", 4)
      .Case("|", 3)
      .Case("&&", 2)
      .Case("||", 1)
      .Default(-1);
}

StringMatcher ScriptParser::readFilePatterns() {
  std::vector<StringRef> V;
  while (!errorCount() && !consume(")"))
    V.push_back(next());
  return StringMatcher(V);
}

SortSectionPolicy ScriptParser::readSortKind() {
  if (consume("SORT") || consume("SORT_BY_NAME"))
    return SortSectionPolicy::Name;
  if (consume("SORT_BY_ALIGNMENT"))
    return SortSectionPolicy::Alignment;
  if (consume("SORT_BY_INIT_PRIORITY"))
    return SortSectionPolicy::Priority;
  if (consume("SORT_NONE"))
    return SortSectionPolicy::None;
  return SortSectionPolicy::Default;
}

// Reads SECTIONS command contents in the following form:
//
// <contents> ::= <elem>*
// <elem>     ::= <exclude>? <glob-pattern>
// <exclude>  ::= "EXCLUDE_FILE" "(" <glob-pattern>+ ")"
//
// For example,
//
// *(.foo EXCLUDE_FILE (a.o) .bar EXCLUDE_FILE (b.o) .baz)
//
// is parsed as ".foo", ".bar" with "a.o", and ".baz" with "b.o".
// The semantics of that is section .foo in any file, section .bar in
// any file but a.o, and section .baz in any file but b.o.
std::vector<SectionPattern> ScriptParser::readInputSectionsList() {
  std::vector<SectionPattern> Ret;
  while (!errorCount() && peek() != ")") {
    StringMatcher ExcludeFilePat;
    if (consume("EXCLUDE_FILE")) {
      expect("(");
      ExcludeFilePat = readFilePatterns();
    }

    std::vector<StringRef> V;
    while (!errorCount() && peek() != ")" && peek() != "EXCLUDE_FILE")
      V.push_back(next());

    if (!V.empty())
      Ret.push_back({std::move(ExcludeFilePat), StringMatcher(V)});
    else
      setError("section pattern is expected");
  }
  return Ret;
}

// Reads contents of "SECTIONS" directive. That directive contains a
// list of glob patterns for input sections. The grammar is as follows.
//
// <patterns> ::= <section-list>
//              | <sort> "(" <section-list> ")"
//              | <sort> "(" <sort> "(" <section-list> ")" ")"
//
// <sort>     ::= "SORT" | "SORT_BY_NAME" | "SORT_BY_ALIGNMENT"
//              | "SORT_BY_INIT_PRIORITY" | "SORT_NONE"
//
// <section-list> is parsed by readInputSectionsList().
InputSectionDescription *
ScriptParser::readInputSectionRules(StringRef FilePattern) {
  auto *Cmd = make<InputSectionDescription>(FilePattern);
  expect("(");

  while (!errorCount() && !consume(")")) {
    SortSectionPolicy Outer = readSortKind();
    SortSectionPolicy Inner = SortSectionPolicy::Default;
    std::vector<SectionPattern> V;
    if (Outer != SortSectionPolicy::Default) {
      expect("(");
      Inner = readSortKind();
      if (Inner != SortSectionPolicy::Default) {
        expect("(");
        V = readInputSectionsList();
        expect(")");
      } else {
        V = readInputSectionsList();
      }
      expect(")");
    } else {
      V = readInputSectionsList();
    }

    for (SectionPattern &Pat : V) {
      Pat.SortInner = Inner;
      Pat.SortOuter = Outer;
    }

    std::move(V.begin(), V.end(), std::back_inserter(Cmd->SectionPatterns));
  }
  return Cmd;
}

InputSectionDescription *
ScriptParser::readInputSectionDescription(StringRef Tok) {
  // Input section wildcard can be surrounded by KEEP.
  // https://sourceware.org/binutils/docs/ld/Input-Section-Keep.html#Input-Section-Keep
  if (Tok == "KEEP") {
    expect("(");
    StringRef FilePattern = next();
    InputSectionDescription *Cmd = readInputSectionRules(FilePattern);
    expect(")");
    Script->KeptSections.push_back(Cmd);
    return Cmd;
  }
  return readInputSectionRules(Tok);
}

void ScriptParser::readSort() {
  expect("(");
  expect("CONSTRUCTORS");
  expect(")");
}

Expr ScriptParser::readAssert() {
  expect("(");
  Expr E = readExpr();
  expect(",");
  StringRef Msg = unquote(next());
  expect(")");

  return [=] {
    if (!E().getValue())
      error(Msg);
    return Script->getDot();
  };
}

// Reads a FILL(expr) command. We handle the FILL command as an
// alias for =fillexp section attribute, which is different from
// what GNU linkers do.
// https://sourceware.org/binutils/docs/ld/Output-Section-Data.html
std::array<uint8_t, 4> ScriptParser::readFill() {
  expect("(");
  std::array<uint8_t, 4> V = parseFill(next());
  expect(")");
  return V;
}

// Tries to read the special directive for an output section definition which
// can be one of following: "(NOLOAD)", "(COPY)", "(INFO)" or "(OVERLAY)".
// Tok1 and Tok2 are next 2 tokens peeked. See comment for readSectionAddressType below.
bool ScriptParser::readSectionDirective(OutputSection *Cmd, StringRef Tok1, StringRef Tok2) {
  if (Tok1 != "(")
    return false;
  if (Tok2 != "NOLOAD" && Tok2 != "COPY" && Tok2 != "INFO" && Tok2 != "OVERLAY")
    return false;

  expect("(");
  if (consume("NOLOAD")) {
    Cmd->Noload = true;
  } else {
    skip(); // This is "COPY", "INFO" or "OVERLAY".
    Cmd->NonAlloc = true;
  }
  expect(")");
  return true;
}

// Reads an expression and/or the special directive for an output
// section definition. Directive is one of following: "(NOLOAD)",
// "(COPY)", "(INFO)" or "(OVERLAY)".
//
// An output section name can be followed by an address expression
// and/or directive. This grammar is not LL(1) because "(" can be
// interpreted as either the beginning of some expression or beginning
// of directive.
//
// https://sourceware.org/binutils/docs/ld/Output-Section-Address.html
// https://sourceware.org/binutils/docs/ld/Output-Section-Type.html
void ScriptParser::readSectionAddressType(OutputSection *Cmd) {
  if (readSectionDirective(Cmd, peek(), peek2()))
    return;

  Cmd->AddrExpr = readExpr();
  if (peek() == "(" && !readSectionDirective(Cmd, "(", peek2()))
    setError("unknown section directive: " + peek2());
}

static Expr checkAlignment(Expr E, std::string &Loc) {
  return [=] {
    uint64_t Alignment = std::max((uint64_t)1, E().getValue());
    if (!isPowerOf2_64(Alignment)) {
      error(Loc + ": alignment must be power of 2");
      return (uint64_t)1; // Return a dummy value.
    }
    return Alignment;
  };
}

OutputSection *ScriptParser::readOverlaySectionDescription() {
  OutputSection *Cmd =
      Script->createOutputSection(next(), getCurrentLocation());
  Cmd->InOverlay = true;
  expect("{");
  while (!errorCount() && !consume("}"))
    Cmd->SectionCommands.push_back(readInputSectionRules(next()));
  Cmd->Phdrs = readOutputSectionPhdrs();
  return Cmd;
}

OutputSection *ScriptParser::readOutputSectionDescription(StringRef OutSec) {
  OutputSection *Cmd =
      Script->createOutputSection(OutSec, getCurrentLocation());

  size_t SymbolsReferenced = Script->ReferencedSymbols.size();

  if (peek() != ":")
    readSectionAddressType(Cmd);
  expect(":");

  std::string Location = getCurrentLocation();
  if (consume("AT"))
    Cmd->LMAExpr = readParenExpr();
  if (consume("ALIGN"))
    Cmd->AlignExpr = checkAlignment(readParenExpr(), Location);
  if (consume("SUBALIGN"))
    Cmd->SubalignExpr = checkAlignment(readParenExpr(), Location);

  // Parse constraints.
  if (consume("ONLY_IF_RO"))
    Cmd->Constraint = ConstraintKind::ReadOnly;
  if (consume("ONLY_IF_RW"))
    Cmd->Constraint = ConstraintKind::ReadWrite;
  expect("{");

  while (!errorCount() && !consume("}")) {
    StringRef Tok = next();
    if (Tok == ";") {
      // Empty commands are allowed. Do nothing here.
    } else if (SymbolAssignment *Assign = readAssignment(Tok)) {
      Cmd->SectionCommands.push_back(Assign);
    } else if (ByteCommand *Data = readByteCommand(Tok)) {
      Cmd->SectionCommands.push_back(Data);
    } else if (Tok == "CONSTRUCTORS") {
      // CONSTRUCTORS is a keyword to make the linker recognize C++ ctors/dtors
      // by name. This is for very old file formats such as ECOFF/XCOFF.
      // For ELF, we should ignore.
    } else if (Tok == "FILL") {
      Cmd->Filler = readFill();
    } else if (Tok == "SORT") {
      readSort();
    } else if (Tok == "INCLUDE") {
      readInclude();
    } else if (peek() == "(") {
      Cmd->SectionCommands.push_back(readInputSectionDescription(Tok));
    } else {
      // We have a file name and no input sections description. It is not a
      // commonly used syntax, but still acceptable. In that case, all sections
      // from the file will be included.
      auto *ISD = make<InputSectionDescription>(Tok);
      ISD->SectionPatterns.push_back({{}, StringMatcher({"*"})});
      Cmd->SectionCommands.push_back(ISD);
    }
  }

  if (consume(">"))
    Cmd->MemoryRegionName = next();

  if (consume("AT")) {
    expect(">");
    Cmd->LMARegionName = next();
  }

  if (Cmd->LMAExpr && !Cmd->LMARegionName.empty())
    error("section can't have both LMA and a load region");

  Cmd->Phdrs = readOutputSectionPhdrs();

  if (consume("="))
    Cmd->Filler = parseFill(next());
  else if (peek().startswith("="))
    Cmd->Filler = parseFill(next().drop_front());

  // Consume optional comma following output section command.
  consume(",");

  if (Script->ReferencedSymbols.size() > SymbolsReferenced)
    Cmd->ExpressionsUseSymbols = true;
  return Cmd;
}

// Parses a given string as a octal/decimal/hexadecimal number and
// returns it as a big-endian number. Used for `=<fillexp>`.
// https://sourceware.org/binutils/docs/ld/Output-Section-Fill.html
//
// When reading a hexstring, ld.bfd handles it as a blob of arbitrary
// size, while ld.gold always handles it as a 32-bit big-endian number.
// We are compatible with ld.gold because it's easier to implement.
std::array<uint8_t, 4> ScriptParser::parseFill(StringRef Tok) {
  uint32_t V = 0;
  if (!to_integer(Tok, V))
    setError("invalid filler expression: " + Tok);

  std::array<uint8_t, 4> Buf;
  write32be(Buf.data(), V);
  return Buf;
}

SymbolAssignment *ScriptParser::readProvideHidden(bool Provide, bool Hidden) {
  expect("(");
  SymbolAssignment *Cmd = readSymbolAssignment(next());
  Cmd->Provide = Provide;
  Cmd->Hidden = Hidden;
  expect(")");
  return Cmd;
}

SymbolAssignment *ScriptParser::readAssignment(StringRef Tok) {
  // Assert expression returns Dot, so this is equal to ".=."
  if (Tok == "ASSERT")
    return make<SymbolAssignment>(".", readAssert(), getCurrentLocation());

  size_t OldPos = Pos;
  SymbolAssignment *Cmd = nullptr;
  if (peek() == "=" || peek() == "+=")
    Cmd = readSymbolAssignment(Tok);
  else if (Tok == "PROVIDE")
    Cmd = readProvideHidden(true, false);
  else if (Tok == "HIDDEN")
    Cmd = readProvideHidden(false, true);
  else if (Tok == "PROVIDE_HIDDEN")
    Cmd = readProvideHidden(true, true);

  if (Cmd) {
    Cmd->CommandString =
        Tok.str() + " " +
        llvm::join(Tokens.begin() + OldPos, Tokens.begin() + Pos, " ");
    expect(";");
  }
  return Cmd;
}

SymbolAssignment *ScriptParser::readSymbolAssignment(StringRef Name) {
  StringRef Op = next();
  assert(Op == "=" || Op == "+=");
  Expr E = readExpr();
  if (Op == "+=") {
    std::string Loc = getCurrentLocation();
    E = [=] { return add(Script->getSymbolValue(Name, Loc), E()); };
  }
  return make<SymbolAssignment>(Name, E, getCurrentLocation());
}

// This is an operator-precedence parser to parse a linker
// script expression.
Expr ScriptParser::readExpr() {
  // Our lexer is context-aware. Set the in-expression bit so that
  // they apply different tokenization rules.
  bool Orig = InExpr;
  InExpr = true;
  Expr E = readExpr1(readPrimary(), 0);
  InExpr = Orig;
  return E;
}

Expr ScriptParser::combine(StringRef Op, Expr L, Expr R) {
  if (Op == "+")
    return [=] { return add(L(), R()); };
  if (Op == "-")
    return [=] { return sub(L(), R()); };
  if (Op == "*")
    return [=] { return L().getValue() * R().getValue(); };
  if (Op == "/") {
    std::string Loc = getCurrentLocation();
    return [=]() -> uint64_t {
      if (uint64_t RV = R().getValue())
        return L().getValue() / RV;
      error(Loc + ": division by zero");
      return 0;
    };
  }
  if (Op == "%") {
    std::string Loc = getCurrentLocation();
    return [=]() -> uint64_t {
      if (uint64_t RV = R().getValue())
        return L().getValue() % RV;
      error(Loc + ": modulo by zero");
      return 0;
    };
  }
  if (Op == "<<")
    return [=] { return L().getValue() << R().getValue(); };
  if (Op == ">>")
    return [=] { return L().getValue() >> R().getValue(); };
  if (Op == "<")
    return [=] { return L().getValue() < R().getValue(); };
  if (Op == ">")
    return [=] { return L().getValue() > R().getValue(); };
  if (Op == ">=")
    return [=] { return L().getValue() >= R().getValue(); };
  if (Op == "<=")
    return [=] { return L().getValue() <= R().getValue(); };
  if (Op == "==")
    return [=] { return L().getValue() == R().getValue(); };
  if (Op == "!=")
    return [=] { return L().getValue() != R().getValue(); };
  if (Op == "||")
    return [=] { return L().getValue() || R().getValue(); };
  if (Op == "&&")
    return [=] { return L().getValue() && R().getValue(); };
  if (Op == "&")
    return [=] { return bitAnd(L(), R()); };
  if (Op == "|")
    return [=] { return bitOr(L(), R()); };
  llvm_unreachable("invalid operator");
}

// This is a part of the operator-precedence parser. This function
// assumes that the remaining token stream starts with an operator.
Expr ScriptParser::readExpr1(Expr Lhs, int MinPrec) {
  while (!atEOF() && !errorCount()) {
    // Read an operator and an expression.
    if (consume("?"))
      return readTernary(Lhs);
    StringRef Op1 = peek();
    if (precedence(Op1) < MinPrec)
      break;
    skip();
    Expr Rhs = readPrimary();

    // Evaluate the remaining part of the expression first if the
    // next operator has greater precedence than the previous one.
    // For example, if we have read "+" and "3", and if the next
    // operator is "*", then we'll evaluate 3 * ... part first.
    while (!atEOF()) {
      StringRef Op2 = peek();
      if (precedence(Op2) <= precedence(Op1))
        break;
      Rhs = readExpr1(Rhs, precedence(Op2));
    }

    Lhs = combine(Op1, Lhs, Rhs);
  }
  return Lhs;
}

Expr ScriptParser::getPageSize() {
  std::string Location = getCurrentLocation();
  return [=]() -> uint64_t {
    if (Target)
      return Target->PageSize;
    error(Location + ": unable to calculate page size");
    return 4096; // Return a dummy value.
  };
}

Expr ScriptParser::readConstant() {
  StringRef S = readParenLiteral();
  if (S == "COMMONPAGESIZE")
    return getPageSize();
  if (S == "MAXPAGESIZE")
    return [] { return Config->MaxPageSize; };
  setError("unknown constant: " + S);
  return [] { return 0; };
}

// Parses Tok as an integer. It recognizes hexadecimal (prefixed with
// "0x" or suffixed with "H") and decimal numbers. Decimal numbers may
// have "K" (Ki) or "M" (Mi) suffixes.
static Optional<uint64_t> parseInt(StringRef Tok) {
  // Hexadecimal
  uint64_t Val;
  if (Tok.startswith_lower("0x")) {
    if (!to_integer(Tok.substr(2), Val, 16))
      return None;
    return Val;
  }
  if (Tok.endswith_lower("H")) {
    if (!to_integer(Tok.drop_back(), Val, 16))
      return None;
    return Val;
  }

  // Decimal
  if (Tok.endswith_lower("K")) {
    if (!to_integer(Tok.drop_back(), Val, 10))
      return None;
    return Val * 1024;
  }
  if (Tok.endswith_lower("M")) {
    if (!to_integer(Tok.drop_back(), Val, 10))
      return None;
    return Val * 1024 * 1024;
  }
  if (!to_integer(Tok, Val, 10))
    return None;
  return Val;
}

ByteCommand *ScriptParser::readByteCommand(StringRef Tok) {
  int Size = StringSwitch<int>(Tok)
                 .Case("BYTE", 1)
                 .Case("SHORT", 2)
                 .Case("LONG", 4)
                 .Case("QUAD", 8)
                 .Default(-1);
  if (Size == -1)
    return nullptr;

  size_t OldPos = Pos;
  Expr E = readParenExpr();
  std::string CommandString =
      Tok.str() + " " +
      llvm::join(Tokens.begin() + OldPos, Tokens.begin() + Pos, " ");
  return make<ByteCommand>(E, Size, CommandString);
}

StringRef ScriptParser::readParenLiteral() {
  expect("(");
  bool Orig = InExpr;
  InExpr = false;
  StringRef Tok = next();
  InExpr = Orig;
  expect(")");
  return Tok;
}

static void checkIfExists(OutputSection *Cmd, StringRef Location) {
  if (Cmd->Location.empty() && Script->ErrorOnMissingSection)
    error(Location + ": undefined section " + Cmd->Name);
}

Expr ScriptParser::readPrimary() {
  if (peek() == "(")
    return readParenExpr();

  if (consume("~")) {
    Expr E = readPrimary();
    return [=] { return ~E().getValue(); };
  }
  if (consume("!")) {
    Expr E = readPrimary();
    return [=] { return !E().getValue(); };
  }
  if (consume("-")) {
    Expr E = readPrimary();
    return [=] { return -E().getValue(); };
  }

  StringRef Tok = next();
  std::string Location = getCurrentLocation();

  // Built-in functions are parsed here.
  // https://sourceware.org/binutils/docs/ld/Builtin-Functions.html.
  if (Tok == "ABSOLUTE") {
    Expr Inner = readParenExpr();
    return [=] {
      ExprValue I = Inner();
      I.ForceAbsolute = true;
      return I;
    };
  }
  if (Tok == "ADDR") {
    StringRef Name = readParenLiteral();
    OutputSection *Sec = Script->getOrCreateOutputSection(Name);
    return [=]() -> ExprValue {
      checkIfExists(Sec, Location);
      return {Sec, false, 0, Location};
    };
  }
  if (Tok == "ALIGN") {
    expect("(");
    Expr E = readExpr();
    if (consume(")")) {
      E = checkAlignment(E, Location);
      return [=] { return alignTo(Script->getDot(), E().getValue()); };
    }
    expect(",");
    Expr E2 = checkAlignment(readExpr(), Location);
    expect(")");
    return [=] {
      ExprValue V = E();
      V.Alignment = E2().getValue();
      return V;
    };
  }
  if (Tok == "ALIGNOF") {
    StringRef Name = readParenLiteral();
    OutputSection *Cmd = Script->getOrCreateOutputSection(Name);
    return [=] {
      checkIfExists(Cmd, Location);
      return Cmd->Alignment;
    };
  }
  if (Tok == "ASSERT")
    return readAssert();
  if (Tok == "CONSTANT")
    return readConstant();
  if (Tok == "DATA_SEGMENT_ALIGN") {
    expect("(");
    Expr E = readExpr();
    expect(",");
    readExpr();
    expect(")");
    return [=] {
      return alignTo(Script->getDot(), std::max((uint64_t)1, E().getValue()));
    };
  }
  if (Tok == "DATA_SEGMENT_END") {
    expect("(");
    expect(".");
    expect(")");
    return [] { return Script->getDot(); };
  }
  if (Tok == "DATA_SEGMENT_RELRO_END") {
    // GNU linkers implements more complicated logic to handle
    // DATA_SEGMENT_RELRO_END. We instead ignore the arguments and
    // just align to the next page boundary for simplicity.
    expect("(");
    readExpr();
    expect(",");
    readExpr();
    expect(")");
    Expr E = getPageSize();
    return [=] { return alignTo(Script->getDot(), E().getValue()); };
  }
  if (Tok == "DEFINED") {
    StringRef Name = readParenLiteral();
    return [=] { return Symtab->find(Name) ? 1 : 0; };
  }
  if (Tok == "LENGTH") {
    StringRef Name = readParenLiteral();
    if (Script->MemoryRegions.count(Name) == 0) {
      setError("memory region not defined: " + Name);
      return [] { return 0; };
    }
    return [=] { return Script->MemoryRegions[Name]->Length; };
  }
  if (Tok == "LOADADDR") {
    StringRef Name = readParenLiteral();
    OutputSection *Cmd = Script->getOrCreateOutputSection(Name);
    return [=] {
      checkIfExists(Cmd, Location);
      return Cmd->getLMA();
    };
  }
  if (Tok == "MAX" || Tok == "MIN") {
    expect("(");
    Expr A = readExpr();
    expect(",");
    Expr B = readExpr();
    expect(")");
    if (Tok == "MIN")
      return [=] { return std::min(A().getValue(), B().getValue()); };
    return [=] { return std::max(A().getValue(), B().getValue()); };
  }
  if (Tok == "ORIGIN") {
    StringRef Name = readParenLiteral();
    if (Script->MemoryRegions.count(Name) == 0) {
      setError("memory region not defined: " + Name);
      return [] { return 0; };
    }
    return [=] { return Script->MemoryRegions[Name]->Origin; };
  }
  if (Tok == "SEGMENT_START") {
    expect("(");
    skip();
    expect(",");
    Expr E = readExpr();
    expect(")");
    return [=] { return E(); };
  }
  if (Tok == "SIZEOF") {
    StringRef Name = readParenLiteral();
    OutputSection *Cmd = Script->getOrCreateOutputSection(Name);
    // Linker script does not create an output section if its content is empty.
    // We want to allow SIZEOF(.foo) where .foo is a section which happened to
    // be empty.
    return [=] { return Cmd->Size; };
  }
  if (Tok == "SIZEOF_HEADERS")
    return [=] { return elf::getHeaderSize(); };

  // Tok is the dot.
  if (Tok == ".")
    return [=] { return Script->getSymbolValue(Tok, Location); };

  // Tok is a literal number.
  if (Optional<uint64_t> Val = parseInt(Tok))
    return [=] { return *Val; };

  // Tok is a symbol name.
  if (!isValidCIdentifier(Tok))
    setError("malformed number: " + Tok);
  Script->ReferencedSymbols.push_back(Tok);
  return [=] { return Script->getSymbolValue(Tok, Location); };
}

Expr ScriptParser::readTernary(Expr Cond) {
  Expr L = readExpr();
  expect(":");
  Expr R = readExpr();
  return [=] { return Cond().getValue() ? L() : R(); };
}

Expr ScriptParser::readParenExpr() {
  expect("(");
  Expr E = readExpr();
  expect(")");
  return E;
}

std::vector<StringRef> ScriptParser::readOutputSectionPhdrs() {
  std::vector<StringRef> Phdrs;
  while (!errorCount() && peek().startswith(":")) {
    StringRef Tok = next();
    Phdrs.push_back((Tok.size() == 1) ? next() : Tok.substr(1));
  }
  return Phdrs;
}

// Read a program header type name. The next token must be a
// name of a program header type or a constant (e.g. "0x3").
unsigned ScriptParser::readPhdrType() {
  StringRef Tok = next();
  if (Optional<uint64_t> Val = parseInt(Tok))
    return *Val;

  unsigned Ret = StringSwitch<unsigned>(Tok)
                     .Case("PT_NULL", PT_NULL)
                     .Case("PT_LOAD", PT_LOAD)
                     .Case("PT_DYNAMIC", PT_DYNAMIC)
                     .Case("PT_INTERP", PT_INTERP)
                     .Case("PT_NOTE", PT_NOTE)
                     .Case("PT_SHLIB", PT_SHLIB)
                     .Case("PT_PHDR", PT_PHDR)
                     .Case("PT_TLS", PT_TLS)
                     .Case("PT_GNU_EH_FRAME", PT_GNU_EH_FRAME)
                     .Case("PT_GNU_STACK", PT_GNU_STACK)
                     .Case("PT_GNU_RELRO", PT_GNU_RELRO)
                     .Case("PT_OPENBSD_RANDOMIZE", PT_OPENBSD_RANDOMIZE)
                     .Case("PT_OPENBSD_WXNEEDED", PT_OPENBSD_WXNEEDED)
                     .Case("PT_OPENBSD_BOOTDATA", PT_OPENBSD_BOOTDATA)
                     .Default(-1);

  if (Ret == (unsigned)-1) {
    setError("invalid program header type: " + Tok);
    return PT_NULL;
  }
  return Ret;
}

// Reads an anonymous version declaration.
void ScriptParser::readAnonymousDeclaration() {
  std::vector<SymbolVersion> Locals;
  std::vector<SymbolVersion> Globals;
  std::tie(Locals, Globals) = readSymbols();

  for (SymbolVersion V : Locals) {
    if (V.Name == "*")
      Config->DefaultSymbolVersion = VER_NDX_LOCAL;
    else
      Config->VersionScriptLocals.push_back(V);
  }

  for (SymbolVersion V : Globals)
    Config->VersionScriptGlobals.push_back(V);

  expect(";");
}

// Reads a non-anonymous version definition,
// e.g. "VerStr { global: foo; bar; local: *; };".
void ScriptParser::readVersionDeclaration(StringRef VerStr) {
  // Read a symbol list.
  std::vector<SymbolVersion> Locals;
  std::vector<SymbolVersion> Globals;
  std::tie(Locals, Globals) = readSymbols();

  for (SymbolVersion V : Locals) {
    if (V.Name == "*")
      Config->DefaultSymbolVersion = VER_NDX_LOCAL;
    else
      Config->VersionScriptLocals.push_back(V);
  }

  // Create a new version definition and add that to the global symbols.
  VersionDefinition Ver;
  Ver.Name = VerStr;
  Ver.Globals = Globals;

  // User-defined version number starts from 2 because 0 and 1 are
  // reserved for VER_NDX_LOCAL and VER_NDX_GLOBAL, respectively.
  Ver.Id = Config->VersionDefinitions.size() + 2;
  Config->VersionDefinitions.push_back(Ver);

  // Each version may have a parent version. For example, "Ver2"
  // defined as "Ver2 { global: foo; local: *; } Ver1;" has "Ver1"
  // as a parent. This version hierarchy is, probably against your
  // instinct, purely for hint; the runtime doesn't care about it
  // at all. In LLD, we simply ignore it.
  if (peek() != ";")
    skip();
  expect(";");
}

static bool hasWildcard(StringRef S) {
  return S.find_first_of("?*[") != StringRef::npos;
}

// Reads a list of symbols, e.g. "{ global: foo; bar; local: *; };".
std::pair<std::vector<SymbolVersion>, std::vector<SymbolVersion>>
ScriptParser::readSymbols() {
  std::vector<SymbolVersion> Locals;
  std::vector<SymbolVersion> Globals;
  std::vector<SymbolVersion> *V = &Globals;

  while (!errorCount()) {
    if (consume("}"))
      break;
    if (consumeLabel("local")) {
      V = &Locals;
      continue;
    }
    if (consumeLabel("global")) {
      V = &Globals;
      continue;
    }

    if (consume("extern")) {
      std::vector<SymbolVersion> Ext = readVersionExtern();
      V->insert(V->end(), Ext.begin(), Ext.end());
    } else {
      StringRef Tok = next();
      V->push_back({unquote(Tok), false, hasWildcard(Tok)});
    }
    expect(";");
  }
  return {Locals, Globals};
}

// Reads an "extern C++" directive, e.g.,
// "extern "C++" { ns::*; "f(int, double)"; };"
//
// The last semicolon is optional. E.g. this is OK:
// "extern "C++" { ns::*; "f(int, double)" };"
std::vector<SymbolVersion> ScriptParser::readVersionExtern() {
  StringRef Tok = next();
  bool IsCXX = Tok == "\"C++\"";
  if (!IsCXX && Tok != "\"C\"")
    setError("Unknown language");
  expect("{");

  std::vector<SymbolVersion> Ret;
  while (!errorCount() && peek() != "}") {
    StringRef Tok = next();
    bool HasWildcard = !Tok.startswith("\"") && hasWildcard(Tok);
    Ret.push_back({unquote(Tok), IsCXX, HasWildcard});
    if (consume("}"))
      return Ret;
    expect(";");
  }

  expect("}");
  return Ret;
}

uint64_t ScriptParser::readMemoryAssignment(StringRef S1, StringRef S2,
                                            StringRef S3) {
  if (!consume(S1) && !consume(S2) && !consume(S3)) {
    setError("expected one of: " + S1 + ", " + S2 + ", or " + S3);
    return 0;
  }
  expect("=");
  return readExpr()().getValue();
}

// Parse the MEMORY command as specified in:
// https://sourceware.org/binutils/docs/ld/MEMORY.html
//
// MEMORY { name [(attr)] : ORIGIN = origin, LENGTH = len ... }
void ScriptParser::readMemory() {
  expect("{");
  while (!errorCount() && !consume("}")) {
    StringRef Tok = next();
    if (Tok == "INCLUDE") {
      readInclude();
      continue;
    }

    uint32_t Flags = 0;
    uint32_t NegFlags = 0;
    if (consume("(")) {
      std::tie(Flags, NegFlags) = readMemoryAttributes();
      expect(")");
    }
    expect(":");

    uint64_t Origin = readMemoryAssignment("ORIGIN", "org", "o");
    expect(",");
    uint64_t Length = readMemoryAssignment("LENGTH", "len", "l");

    // Add the memory region to the region map.
    MemoryRegion *MR = make<MemoryRegion>(Tok, Origin, Length, Flags, NegFlags);
    if (!Script->MemoryRegions.insert({Tok, MR}).second)
      setError("region '" + Tok + "' already defined");
  }
}

// This function parses the attributes used to match against section
// flags when placing output sections in a memory region. These flags
// are only used when an explicit memory region name is not used.
std::pair<uint32_t, uint32_t> ScriptParser::readMemoryAttributes() {
  uint32_t Flags = 0;
  uint32_t NegFlags = 0;
  bool Invert = false;

  for (char C : next().lower()) {
    uint32_t Flag = 0;
    if (C == '!')
      Invert = !Invert;
    else if (C == 'w')
      Flag = SHF_WRITE;
    else if (C == 'x')
      Flag = SHF_EXECINSTR;
    else if (C == 'a')
      Flag = SHF_ALLOC;
    else if (C != 'r')
      setError("invalid memory region attribute");

    if (Invert)
      NegFlags |= Flag;
    else
      Flags |= Flag;
  }
  return {Flags, NegFlags};
}

void elf::readLinkerScript(MemoryBufferRef MB) {
  ScriptParser(MB).readLinkerScript();
}

void elf::readVersionScript(MemoryBufferRef MB) {
  ScriptParser(MB).readVersionScript();
}

void elf::readDynamicList(MemoryBufferRef MB) {
  ScriptParser(MB).readDynamicList();
}

void elf::readDefsym(StringRef Name, MemoryBufferRef MB) {
  ScriptParser(MB).readDefsym(Name);
}
