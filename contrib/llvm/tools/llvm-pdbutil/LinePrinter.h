//===- LinePrinter.h ------------------------------------------ *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_LINEPRINTER_H
#define LLVM_TOOLS_LLVMPDBDUMP_LINEPRINTER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <list>

namespace llvm {
class BinaryStreamReader;
namespace msf {
class MSFStreamLayout;
} // namespace msf
namespace pdb {

class ClassLayout;
class PDBFile;

class LinePrinter {
  friend class WithColor;

public:
  LinePrinter(int Indent, bool UseColor, raw_ostream &Stream);

  void Indent(uint32_t Amount = 0);
  void Unindent(uint32_t Amount = 0);
  void NewLine();

  void printLine(const Twine &T);
  void print(const Twine &T);
  template <typename... Ts> void formatLine(const char *Fmt, Ts &&... Items) {
    printLine(formatv(Fmt, std::forward<Ts>(Items)...));
  }
  template <typename... Ts> void format(const char *Fmt, Ts &&... Items) {
    print(formatv(Fmt, std::forward<Ts>(Items)...));
  }

  void formatBinary(StringRef Label, ArrayRef<uint8_t> Data,
                    uint32_t StartOffset);
  void formatBinary(StringRef Label, ArrayRef<uint8_t> Data, uint64_t BaseAddr,
                    uint32_t StartOffset);

  void formatMsfStreamData(StringRef Label, PDBFile &File, uint32_t StreamIdx,
                           StringRef StreamPurpose, uint32_t Offset,
                           uint32_t Size);
  void formatMsfStreamData(StringRef Label, PDBFile &File,
                           const msf::MSFStreamLayout &Stream,
                           BinarySubstreamRef Substream);
  void formatMsfStreamBlocks(PDBFile &File, const msf::MSFStreamLayout &Stream);

  bool hasColor() const { return UseColor; }
  raw_ostream &getStream() { return OS; }
  int getIndentLevel() const { return CurrentIndent; }

  bool IsClassExcluded(const ClassLayout &Class);
  bool IsTypeExcluded(llvm::StringRef TypeName, uint32_t Size);
  bool IsSymbolExcluded(llvm::StringRef SymbolName);
  bool IsCompilandExcluded(llvm::StringRef CompilandName);

private:
  template <typename Iter>
  void SetFilters(std::list<Regex> &List, Iter Begin, Iter End) {
    List.clear();
    for (; Begin != End; ++Begin)
      List.emplace_back(StringRef(*Begin));
  }

  raw_ostream &OS;
  int IndentSpaces;
  int CurrentIndent;
  bool UseColor;

  std::list<Regex> ExcludeCompilandFilters;
  std::list<Regex> ExcludeTypeFilters;
  std::list<Regex> ExcludeSymbolFilters;

  std::list<Regex> IncludeCompilandFilters;
  std::list<Regex> IncludeTypeFilters;
  std::list<Regex> IncludeSymbolFilters;
};

struct PrintScope {
  explicit PrintScope(LinePrinter &P, uint32_t IndentLevel)
      : P(P), IndentLevel(IndentLevel) {}
  explicit PrintScope(const PrintScope &Other, uint32_t LabelWidth)
      : P(Other.P), IndentLevel(Other.IndentLevel), LabelWidth(LabelWidth) {}

  LinePrinter &P;
  uint32_t IndentLevel;
  uint32_t LabelWidth = 0;
};

inline Optional<PrintScope> withLabelWidth(const Optional<PrintScope> &Scope,
                                           uint32_t W) {
  if (!Scope)
    return None;
  return PrintScope{*Scope, W};
}

struct AutoIndent {
  explicit AutoIndent(LinePrinter &L, uint32_t Amount = 0)
      : L(&L), Amount(Amount) {
    L.Indent(Amount);
  }
  explicit AutoIndent(const Optional<PrintScope> &Scope) {
    if (Scope.hasValue()) {
      L = &Scope->P;
      Amount = Scope->IndentLevel;
    }
  }
  ~AutoIndent() {
    if (L)
      L->Unindent(Amount);
  }

  LinePrinter *L = nullptr;
  uint32_t Amount = 0;
};

template <class T>
inline raw_ostream &operator<<(LinePrinter &Printer, const T &Item) {
  Printer.getStream() << Item;
  return Printer.getStream();
}

enum class PDB_ColorItem {
  None,
  Address,
  Type,
  Comment,
  Padding,
  Keyword,
  Offset,
  Identifier,
  Path,
  SectionHeader,
  LiteralValue,
  Register,
};

class WithColor {
public:
  WithColor(LinePrinter &P, PDB_ColorItem C);
  ~WithColor();

  raw_ostream &get() { return OS; }

private:
  void applyColor(PDB_ColorItem C);
  raw_ostream &OS;
  bool UseColor;
};
}
}

#endif
