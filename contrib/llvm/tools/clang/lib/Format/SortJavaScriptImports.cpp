//===--- SortJavaScriptImports.cpp - Sort ES6 Imports -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a sort operation for JavaScript ES6 imports.
///
//===----------------------------------------------------------------------===//

#include "SortJavaScriptImports.h"
#include "TokenAnalyzer.h"
#include "TokenAnnotator.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
#include <string>

#define DEBUG_TYPE "format-formatter"

namespace clang {
namespace format {

class FormatTokenLexer;

using clang::format::FormatStyle;

// An imported symbol in a JavaScript ES6 import/export, possibly aliased.
struct JsImportedSymbol {
  StringRef Symbol;
  StringRef Alias;
  SourceRange Range;

  bool operator==(const JsImportedSymbol &RHS) const {
    // Ignore Range for comparison, it is only used to stitch code together,
    // but imports at different code locations are still conceptually the same.
    return Symbol == RHS.Symbol && Alias == RHS.Alias;
  }
};

// An ES6 module reference.
//
// ES6 implements a module system, where individual modules (~= source files)
// can reference other modules, either importing symbols from them, or exporting
// symbols from them:
//   import {foo} from 'foo';
//   export {foo};
//   export {bar} from 'bar';
//
// `export`s with URLs are syntactic sugar for an import of the symbol from the
// URL, followed by an export of the symbol, allowing this code to treat both
// statements more or less identically, with the exception being that `export`s
// are sorted last.
//
// imports and exports support individual symbols, but also a wildcard syntax:
//   import * as prefix from 'foo';
//   export * from 'bar';
//
// This struct represents both exports and imports to build up the information
// required for sorting module references.
struct JsModuleReference {
  bool IsExport = false;
  // Module references are sorted into these categories, in order.
  enum ReferenceCategory {
    SIDE_EFFECT,     // "import 'something';"
    ABSOLUTE,        // from 'something'
    RELATIVE_PARENT, // from '../*'
    RELATIVE,        // from './*'
  };
  ReferenceCategory Category = ReferenceCategory::SIDE_EFFECT;
  // The URL imported, e.g. `import .. from 'url';`. Empty for `export {a, b};`.
  StringRef URL;
  // Prefix from "import * as prefix". Empty for symbol imports and `export *`.
  // Implies an empty names list.
  StringRef Prefix;
  // Symbols from `import {SymbolA, SymbolB, ...} from ...;`.
  SmallVector<JsImportedSymbol, 1> Symbols;
  // Textual position of the import/export, including preceding and trailing
  // comments.
  SourceRange Range;
};

bool operator<(const JsModuleReference &LHS, const JsModuleReference &RHS) {
  if (LHS.IsExport != RHS.IsExport)
    return LHS.IsExport < RHS.IsExport;
  if (LHS.Category != RHS.Category)
    return LHS.Category < RHS.Category;
  if (LHS.Category == JsModuleReference::ReferenceCategory::SIDE_EFFECT)
    // Side effect imports might be ordering sensitive. Consider them equal so
    // that they maintain their relative order in the stable sort below.
    // This retains transitivity because LHS.Category == RHS.Category here.
    return false;
  // Empty URLs sort *last* (for export {...};).
  if (LHS.URL.empty() != RHS.URL.empty())
    return LHS.URL.empty() < RHS.URL.empty();
  if (int Res = LHS.URL.compare_lower(RHS.URL))
    return Res < 0;
  // '*' imports (with prefix) sort before {a, b, ...} imports.
  if (LHS.Prefix.empty() != RHS.Prefix.empty())
    return LHS.Prefix.empty() < RHS.Prefix.empty();
  if (LHS.Prefix != RHS.Prefix)
    return LHS.Prefix > RHS.Prefix;
  return false;
}

// JavaScriptImportSorter sorts JavaScript ES6 imports and exports. It is
// implemented as a TokenAnalyzer because ES6 imports have substantial syntactic
// structure, making it messy to sort them using regular expressions.
class JavaScriptImportSorter : public TokenAnalyzer {
public:
  JavaScriptImportSorter(const Environment &Env, const FormatStyle &Style)
      : TokenAnalyzer(Env, Style),
        FileContents(Env.getSourceManager().getBufferData(Env.getFileID())) {}

  std::pair<tooling::Replacements, unsigned>
  analyze(TokenAnnotator &Annotator,
          SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
          FormatTokenLexer &Tokens) override {
    tooling::Replacements Result;
    AffectedRangeMgr.computeAffectedLines(AnnotatedLines);

    const AdditionalKeywords &Keywords = Tokens.getKeywords();
    SmallVector<JsModuleReference, 16> References;
    AnnotatedLine *FirstNonImportLine;
    std::tie(References, FirstNonImportLine) =
        parseModuleReferences(Keywords, AnnotatedLines);

    if (References.empty())
      return {Result, 0};

    SmallVector<unsigned, 16> Indices;
    for (unsigned i = 0, e = References.size(); i != e; ++i)
      Indices.push_back(i);
    std::stable_sort(Indices.begin(), Indices.end(),
                     [&](unsigned LHSI, unsigned RHSI) {
                       return References[LHSI] < References[RHSI];
                     });
    bool ReferencesInOrder = std::is_sorted(Indices.begin(), Indices.end());

    std::string ReferencesText;
    bool SymbolsInOrder = true;
    for (unsigned i = 0, e = Indices.size(); i != e; ++i) {
      JsModuleReference Reference = References[Indices[i]];
      if (appendReference(ReferencesText, Reference))
        SymbolsInOrder = false;
      if (i + 1 < e) {
        // Insert breaks between imports and exports.
        ReferencesText += "\n";
        // Separate imports groups with two line breaks, but keep all exports
        // in a single group.
        if (!Reference.IsExport &&
            (Reference.IsExport != References[Indices[i + 1]].IsExport ||
             Reference.Category != References[Indices[i + 1]].Category))
          ReferencesText += "\n";
      }
    }

    if (ReferencesInOrder && SymbolsInOrder)
      return {Result, 0};

    SourceRange InsertionPoint = References[0].Range;
    InsertionPoint.setEnd(References[References.size() - 1].Range.getEnd());

    // The loop above might collapse previously existing line breaks between
    // import blocks, and thus shrink the file. SortIncludes must not shrink
    // overall source length as there is currently no re-calculation of ranges
    // after applying source sorting.
    // This loop just backfills trailing spaces after the imports, which are
    // harmless and will be stripped by the subsequent formatting pass.
    // FIXME: A better long term fix is to re-calculate Ranges after sorting.
    unsigned PreviousSize = getSourceText(InsertionPoint).size();
    while (ReferencesText.size() < PreviousSize) {
      ReferencesText += " ";
    }

    // Separate references from the main code body of the file.
    if (FirstNonImportLine && FirstNonImportLine->First->NewlinesBefore < 2)
      ReferencesText += "\n";

    LLVM_DEBUG(llvm::dbgs() << "Replacing imports:\n"
                            << getSourceText(InsertionPoint) << "\nwith:\n"
                            << ReferencesText << "\n");
    auto Err = Result.add(tooling::Replacement(
        Env.getSourceManager(), CharSourceRange::getCharRange(InsertionPoint),
        ReferencesText));
    // FIXME: better error handling. For now, just print error message and skip
    // the replacement for the release version.
    if (Err) {
      llvm::errs() << llvm::toString(std::move(Err)) << "\n";
      assert(false);
    }

    return {Result, 0};
  }

private:
  FormatToken *Current;
  FormatToken *LineEnd;

  FormatToken invalidToken;

  StringRef FileContents;

  void skipComments() { Current = skipComments(Current); }

  FormatToken *skipComments(FormatToken *Tok) {
    while (Tok && Tok->is(tok::comment))
      Tok = Tok->Next;
    return Tok;
  }

  void nextToken() {
    Current = Current->Next;
    skipComments();
    if (!Current || Current == LineEnd->Next) {
      // Set the current token to an invalid token, so that further parsing on
      // this line fails.
      invalidToken.Tok.setKind(tok::unknown);
      Current = &invalidToken;
    }
  }

  StringRef getSourceText(SourceRange Range) {
    return getSourceText(Range.getBegin(), Range.getEnd());
  }

  StringRef getSourceText(SourceLocation Begin, SourceLocation End) {
    const SourceManager &SM = Env.getSourceManager();
    return FileContents.substr(SM.getFileOffset(Begin),
                               SM.getFileOffset(End) - SM.getFileOffset(Begin));
  }

  // Appends ``Reference`` to ``Buffer``, returning true if text within the
  // ``Reference`` changed (e.g. symbol order).
  bool appendReference(std::string &Buffer, JsModuleReference &Reference) {
    // Sort the individual symbols within the import.
    // E.g. `import {b, a} from 'x';` -> `import {a, b} from 'x';`
    SmallVector<JsImportedSymbol, 1> Symbols = Reference.Symbols;
    std::stable_sort(
        Symbols.begin(), Symbols.end(),
        [&](const JsImportedSymbol &LHS, const JsImportedSymbol &RHS) {
          return LHS.Symbol.compare_lower(RHS.Symbol) < 0;
        });
    if (Symbols == Reference.Symbols) {
      // No change in symbol order.
      StringRef ReferenceStmt = getSourceText(Reference.Range);
      Buffer += ReferenceStmt;
      return false;
    }
    // Stitch together the module reference start...
    SourceLocation SymbolsStart = Reference.Symbols.front().Range.getBegin();
    SourceLocation SymbolsEnd = Reference.Symbols.back().Range.getEnd();
    Buffer += getSourceText(Reference.Range.getBegin(), SymbolsStart);
    // ... then the references in order ...
    for (auto I = Symbols.begin(), E = Symbols.end(); I != E; ++I) {
      if (I != Symbols.begin())
        Buffer += ",";
      Buffer += getSourceText(I->Range);
    }
    // ... followed by the module reference end.
    Buffer += getSourceText(SymbolsEnd, Reference.Range.getEnd());
    return true;
  }

  // Parses module references in the given lines. Returns the module references,
  // and a pointer to the first "main code" line if that is adjacent to the
  // affected lines of module references, nullptr otherwise.
  std::pair<SmallVector<JsModuleReference, 16>, AnnotatedLine *>
  parseModuleReferences(const AdditionalKeywords &Keywords,
                        SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
    SmallVector<JsModuleReference, 16> References;
    SourceLocation Start;
    AnnotatedLine *FirstNonImportLine = nullptr;
    bool AnyImportAffected = false;
    for (auto Line : AnnotatedLines) {
      Current = Line->First;
      LineEnd = Line->Last;
      skipComments();
      if (Start.isInvalid() || References.empty())
        // After the first file level comment, consider line comments to be part
        // of the import that immediately follows them by using the previously
        // set Start.
        Start = Line->First->Tok.getLocation();
      if (!Current) {
        // Only comments on this line. Could be the first non-import line.
        FirstNonImportLine = Line;
        continue;
      }
      JsModuleReference Reference;
      Reference.Range.setBegin(Start);
      if (!parseModuleReference(Keywords, Reference)) {
        if (!FirstNonImportLine)
          FirstNonImportLine = Line; // if no comment before.
        break;
      }
      FirstNonImportLine = nullptr;
      AnyImportAffected = AnyImportAffected || Line->Affected;
      Reference.Range.setEnd(LineEnd->Tok.getEndLoc());
      LLVM_DEBUG({
        llvm::dbgs() << "JsModuleReference: {"
                     << "is_export: " << Reference.IsExport
                     << ", cat: " << Reference.Category
                     << ", url: " << Reference.URL
                     << ", prefix: " << Reference.Prefix;
        for (size_t i = 0; i < Reference.Symbols.size(); ++i)
          llvm::dbgs() << ", " << Reference.Symbols[i].Symbol << " as "
                       << Reference.Symbols[i].Alias;
        llvm::dbgs() << ", text: " << getSourceText(Reference.Range);
        llvm::dbgs() << "}\n";
      });
      References.push_back(Reference);
      Start = SourceLocation();
    }
    // Sort imports if any import line was affected.
    if (!AnyImportAffected)
      References.clear();
    return std::make_pair(References, FirstNonImportLine);
  }

  // Parses a JavaScript/ECMAScript 6 module reference.
  // See http://www.ecma-international.org/ecma-262/6.0/#sec-scripts-and-modules
  // for grammar EBNF (production ModuleItem).
  bool parseModuleReference(const AdditionalKeywords &Keywords,
                            JsModuleReference &Reference) {
    if (!Current || !Current->isOneOf(Keywords.kw_import, tok::kw_export))
      return false;
    Reference.IsExport = Current->is(tok::kw_export);

    nextToken();
    if (Current->isStringLiteral() && !Reference.IsExport) {
      // "import 'side-effect';"
      Reference.Category = JsModuleReference::ReferenceCategory::SIDE_EFFECT;
      Reference.URL =
          Current->TokenText.substr(1, Current->TokenText.size() - 2);
      return true;
    }

    if (!parseModuleBindings(Keywords, Reference))
      return false;

    if (Current->is(Keywords.kw_from)) {
      // imports have a 'from' clause, exports might not.
      nextToken();
      if (!Current->isStringLiteral())
        return false;
      // URL = TokenText without the quotes.
      Reference.URL =
          Current->TokenText.substr(1, Current->TokenText.size() - 2);
      if (Reference.URL.startswith(".."))
        Reference.Category =
            JsModuleReference::ReferenceCategory::RELATIVE_PARENT;
      else if (Reference.URL.startswith("."))
        Reference.Category = JsModuleReference::ReferenceCategory::RELATIVE;
      else
        Reference.Category = JsModuleReference::ReferenceCategory::ABSOLUTE;
    } else {
      // w/o URL groups with "empty".
      Reference.Category = JsModuleReference::ReferenceCategory::RELATIVE;
    }
    return true;
  }

  bool parseModuleBindings(const AdditionalKeywords &Keywords,
                           JsModuleReference &Reference) {
    if (parseStarBinding(Keywords, Reference))
      return true;
    return parseNamedBindings(Keywords, Reference);
  }

  bool parseStarBinding(const AdditionalKeywords &Keywords,
                        JsModuleReference &Reference) {
    // * as prefix from '...';
    if (Current->isNot(tok::star))
      return false;
    nextToken();
    if (Current->isNot(Keywords.kw_as))
      return false;
    nextToken();
    if (Current->isNot(tok::identifier))
      return false;
    Reference.Prefix = Current->TokenText;
    nextToken();
    return true;
  }

  bool parseNamedBindings(const AdditionalKeywords &Keywords,
                          JsModuleReference &Reference) {
    if (Current->is(tok::identifier)) {
      nextToken();
      if (Current->is(Keywords.kw_from))
        return true;
      if (Current->isNot(tok::comma))
        return false;
      nextToken(); // eat comma.
    }
    if (Current->isNot(tok::l_brace))
      return false;

    // {sym as alias, sym2 as ...} from '...';
    while (Current->isNot(tok::r_brace)) {
      nextToken();
      if (Current->is(tok::r_brace))
        break;
      if (!Current->isOneOf(tok::identifier, tok::kw_default))
        return false;

      JsImportedSymbol Symbol;
      Symbol.Symbol = Current->TokenText;
      // Make sure to include any preceding comments.
      Symbol.Range.setBegin(
          Current->getPreviousNonComment()->Next->WhitespaceRange.getBegin());
      nextToken();

      if (Current->is(Keywords.kw_as)) {
        nextToken();
        if (!Current->isOneOf(tok::identifier, tok::kw_default))
          return false;
        Symbol.Alias = Current->TokenText;
        nextToken();
      }
      Symbol.Range.setEnd(Current->Tok.getLocation());
      Reference.Symbols.push_back(Symbol);

      if (!Current->isOneOf(tok::r_brace, tok::comma))
        return false;
    }
    nextToken(); // consume r_brace
    return true;
  }
};

tooling::Replacements sortJavaScriptImports(const FormatStyle &Style,
                                            StringRef Code,
                                            ArrayRef<tooling::Range> Ranges,
                                            StringRef FileName) {
  // FIXME: Cursor support.
  return JavaScriptImportSorter(Environment(Code, FileName, Ranges), Style)
      .process()
      .first;
}

} // end namespace format
} // end namespace clang
