//===--- UsingDeclarationsSorter.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements UsingDeclarationsSorter, a TokenAnalyzer that
/// sorts consecutive using declarations.
///
//===----------------------------------------------------------------------===//

#include "UsingDeclarationsSorter.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"

#include <algorithm>

#define DEBUG_TYPE "using-declarations-sorter"

namespace clang {
namespace format {

namespace {

// The order of using declaration is defined as follows:
// Split the strings by "::" and discard any initial empty strings. The last
// element of each list is a non-namespace name; all others are namespace
// names. Sort the lists of names lexicographically, where the sort order of
// individual names is that all non-namespace names come before all namespace
// names, and within those groups, names are in case-insensitive lexicographic
// order.
int compareLabels(StringRef A, StringRef B) {
  SmallVector<StringRef, 2> NamesA;
  A.split(NamesA, "::", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  SmallVector<StringRef, 2> NamesB;
  B.split(NamesB, "::", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  size_t SizeA = NamesA.size();
  size_t SizeB = NamesB.size();
  for (size_t I = 0, E = std::min(SizeA, SizeB); I < E; ++I) {
    if (I + 1 == SizeA) {
      // I is the last index of NamesA and NamesA[I] is a non-namespace name.

      // Non-namespace names come before all namespace names.
      if (SizeB > SizeA)
        return -1;

      // Two names within a group compare case-insensitively.
      return NamesA[I].compare_lower(NamesB[I]);
    }

    // I is the last index of NamesB and NamesB[I] is a non-namespace name.
    // Non-namespace names come before all namespace names.
    if (I + 1 == SizeB)
      return 1;

    // Two namespaces names within a group compare case-insensitively.
    int C = NamesA[I].compare_lower(NamesB[I]);
    if (C != 0)
      return C;
  }
  return 0;
}

struct UsingDeclaration {
  const AnnotatedLine *Line;
  std::string Label;

  UsingDeclaration(const AnnotatedLine *Line, const std::string &Label)
      : Line(Line), Label(Label) {}

  bool operator<(const UsingDeclaration &Other) const {
    return compareLabels(Label, Other.Label) < 0;
  }
};

/// Computes the label of a using declaration starting at tthe using token
/// \p UsingTok.
/// If \p UsingTok doesn't begin a using declaration, returns the empty string.
/// Note that this detects specifically using declarations, as in:
/// using A::B::C;
/// and not type aliases, as in:
/// using A = B::C;
/// Type aliases are in general not safe to permute.
std::string computeUsingDeclarationLabel(const FormatToken *UsingTok) {
  assert(UsingTok && UsingTok->is(tok::kw_using) && "Expecting a using token");
  std::string Label;
  const FormatToken *Tok = UsingTok->Next;
  if (Tok && Tok->is(tok::kw_typename)) {
    Label.append("typename ");
    Tok = Tok->Next;
  }
  if (Tok && Tok->is(tok::coloncolon)) {
    Label.append("::");
    Tok = Tok->Next;
  }
  bool HasIdentifier = false;
  while (Tok && Tok->is(tok::identifier)) {
    HasIdentifier = true;
    Label.append(Tok->TokenText.str());
    Tok = Tok->Next;
    if (!Tok || Tok->isNot(tok::coloncolon))
      break;
    Label.append("::");
    Tok = Tok->Next;
  }
  if (HasIdentifier && Tok && Tok->isOneOf(tok::semi, tok::comma))
    return Label;
  return "";
}

void endUsingDeclarationBlock(
    SmallVectorImpl<UsingDeclaration> *UsingDeclarations,
    const SourceManager &SourceMgr, tooling::Replacements *Fixes) {
  bool BlockAffected = false;
  for (const UsingDeclaration &Declaration : *UsingDeclarations) {
    if (Declaration.Line->Affected) {
      BlockAffected = true;
      break;
    }
  }
  if (!BlockAffected) {
    UsingDeclarations->clear();
    return;
  }
  SmallVector<UsingDeclaration, 4> SortedUsingDeclarations(
      UsingDeclarations->begin(), UsingDeclarations->end());
  std::stable_sort(SortedUsingDeclarations.begin(),
                   SortedUsingDeclarations.end());
  SortedUsingDeclarations.erase(
      std::unique(SortedUsingDeclarations.begin(),
                  SortedUsingDeclarations.end(),
                  [](const UsingDeclaration &a, const UsingDeclaration &b) {
                    return a.Label == b.Label;
                  }),
      SortedUsingDeclarations.end());
  for (size_t I = 0, E = UsingDeclarations->size(); I < E; ++I) {
    if (I >= SortedUsingDeclarations.size()) {
      // This using declaration has been deduplicated, delete it.
      auto Begin =
          (*UsingDeclarations)[I].Line->First->WhitespaceRange.getBegin();
      auto End = (*UsingDeclarations)[I].Line->Last->Tok.getEndLoc();
      auto Range = CharSourceRange::getCharRange(Begin, End);
      auto Err = Fixes->add(tooling::Replacement(SourceMgr, Range, ""));
      if (Err) {
        llvm::errs() << "Error while sorting using declarations: "
                     << llvm::toString(std::move(Err)) << "\n";
      }
      continue;
    }
    if ((*UsingDeclarations)[I].Line == SortedUsingDeclarations[I].Line)
      continue;
    auto Begin = (*UsingDeclarations)[I].Line->First->Tok.getLocation();
    auto End = (*UsingDeclarations)[I].Line->Last->Tok.getEndLoc();
    auto SortedBegin =
        SortedUsingDeclarations[I].Line->First->Tok.getLocation();
    auto SortedEnd = SortedUsingDeclarations[I].Line->Last->Tok.getEndLoc();
    StringRef Text(SourceMgr.getCharacterData(SortedBegin),
                   SourceMgr.getCharacterData(SortedEnd) -
                       SourceMgr.getCharacterData(SortedBegin));
    LLVM_DEBUG({
      StringRef OldText(SourceMgr.getCharacterData(Begin),
                        SourceMgr.getCharacterData(End) -
                            SourceMgr.getCharacterData(Begin));
      llvm::dbgs() << "Replacing '" << OldText << "' with '" << Text << "'\n";
    });
    auto Range = CharSourceRange::getCharRange(Begin, End);
    auto Err = Fixes->add(tooling::Replacement(SourceMgr, Range, Text));
    if (Err) {
      llvm::errs() << "Error while sorting using declarations: "
                   << llvm::toString(std::move(Err)) << "\n";
    }
  }
  UsingDeclarations->clear();
}

} // namespace

UsingDeclarationsSorter::UsingDeclarationsSorter(const Environment &Env,
                                                 const FormatStyle &Style)
    : TokenAnalyzer(Env, Style) {}

std::pair<tooling::Replacements, unsigned> UsingDeclarationsSorter::analyze(
    TokenAnnotator &Annotator, SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
    FormatTokenLexer &Tokens) {
  const SourceManager &SourceMgr = Env.getSourceManager();
  AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
  tooling::Replacements Fixes;
  SmallVector<UsingDeclaration, 4> UsingDeclarations;
  for (size_t I = 0, E = AnnotatedLines.size(); I != E; ++I) {
    const auto *FirstTok = AnnotatedLines[I]->First;
    if (AnnotatedLines[I]->InPPDirective ||
        !AnnotatedLines[I]->startsWith(tok::kw_using) || FirstTok->Finalized) {
      endUsingDeclarationBlock(&UsingDeclarations, SourceMgr, &Fixes);
      continue;
    }
    if (FirstTok->NewlinesBefore > 1)
      endUsingDeclarationBlock(&UsingDeclarations, SourceMgr, &Fixes);
    const auto *UsingTok =
        FirstTok->is(tok::comment) ? FirstTok->getNextNonComment() : FirstTok;
    std::string Label = computeUsingDeclarationLabel(UsingTok);
    if (Label.empty()) {
      endUsingDeclarationBlock(&UsingDeclarations, SourceMgr, &Fixes);
      continue;
    }
    UsingDeclarations.push_back(UsingDeclaration(AnnotatedLines[I], Label));
  }
  endUsingDeclarationBlock(&UsingDeclarations, SourceMgr, &Fixes);
  return {Fixes, 0};
}

} // namespace format
} // namespace clang
