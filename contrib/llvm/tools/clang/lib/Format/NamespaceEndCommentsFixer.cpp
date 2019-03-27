//===--- NamespaceEndCommentsFixer.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements NamespaceEndCommentsFixer, a TokenAnalyzer that
/// fixes namespace end comments.
///
//===----------------------------------------------------------------------===//

#include "NamespaceEndCommentsFixer.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"

#define DEBUG_TYPE "namespace-end-comments-fixer"

namespace clang {
namespace format {

namespace {
// The maximal number of unwrapped lines that a short namespace spans.
// Short namespaces don't need an end comment.
static const int kShortNamespaceMaxLines = 1;

// Computes the name of a namespace given the namespace token.
// Returns "" for anonymous namespace.
std::string computeName(const FormatToken *NamespaceTok) {
  assert(NamespaceTok && NamespaceTok->is(tok::kw_namespace) &&
         "expecting a namespace token");
  std::string name = "";
  // Collects all the non-comment tokens between 'namespace' and '{'.
  const FormatToken *Tok = NamespaceTok->getNextNonComment();
  while (Tok && !Tok->is(tok::l_brace)) {
    name += Tok->TokenText;
    Tok = Tok->getNextNonComment();
  }
  return name;
}

std::string computeEndCommentText(StringRef NamespaceName, bool AddNewline) {
  std::string text = "// namespace";
  if (!NamespaceName.empty()) {
    text += ' ';
    text += NamespaceName;
  }
  if (AddNewline)
    text += '\n';
  return text;
}

bool hasEndComment(const FormatToken *RBraceTok) {
  return RBraceTok->Next && RBraceTok->Next->is(tok::comment);
}

bool validEndComment(const FormatToken *RBraceTok, StringRef NamespaceName) {
  assert(hasEndComment(RBraceTok));
  const FormatToken *Comment = RBraceTok->Next;

  // Matches a valid namespace end comment.
  // Valid namespace end comments don't need to be edited.
  static llvm::Regex *const NamespaceCommentPattern =
      new llvm::Regex("^/[/*] *(end (of )?)? *(anonymous|unnamed)? *"
                      "namespace( +([a-zA-Z0-9:_]+))?\\.? *(\\*/)?$",
                      llvm::Regex::IgnoreCase);
  SmallVector<StringRef, 7> Groups;
  if (NamespaceCommentPattern->match(Comment->TokenText, &Groups)) {
    StringRef NamespaceNameInComment = Groups.size() > 5 ? Groups[5] : "";
    // Anonymous namespace comments must not mention a namespace name.
    if (NamespaceName.empty() && !NamespaceNameInComment.empty())
      return false;
    StringRef AnonymousInComment = Groups.size() > 3 ? Groups[3] : "";
    // Named namespace comments must not mention anonymous namespace.
    if (!NamespaceName.empty() && !AnonymousInComment.empty())
      return false;
    return NamespaceNameInComment == NamespaceName;
  }
  return false;
}

void addEndComment(const FormatToken *RBraceTok, StringRef EndCommentText,
                   const SourceManager &SourceMgr,
                   tooling::Replacements *Fixes) {
  auto EndLoc = RBraceTok->Tok.getEndLoc();
  auto Range = CharSourceRange::getCharRange(EndLoc, EndLoc);
  auto Err = Fixes->add(tooling::Replacement(SourceMgr, Range, EndCommentText));
  if (Err) {
    llvm::errs() << "Error while adding namespace end comment: "
                 << llvm::toString(std::move(Err)) << "\n";
  }
}

void updateEndComment(const FormatToken *RBraceTok, StringRef EndCommentText,
                      const SourceManager &SourceMgr,
                      tooling::Replacements *Fixes) {
  assert(hasEndComment(RBraceTok));
  const FormatToken *Comment = RBraceTok->Next;
  auto Range = CharSourceRange::getCharRange(Comment->getStartOfNonWhitespace(),
                                             Comment->Tok.getEndLoc());
  auto Err = Fixes->add(tooling::Replacement(SourceMgr, Range, EndCommentText));
  if (Err) {
    llvm::errs() << "Error while updating namespace end comment: "
                 << llvm::toString(std::move(Err)) << "\n";
  }
}
} // namespace

const FormatToken *
getNamespaceToken(const AnnotatedLine *Line,
                  const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
  if (!Line->Affected || Line->InPPDirective || !Line->startsWith(tok::r_brace))
    return nullptr;
  size_t StartLineIndex = Line->MatchingOpeningBlockLineIndex;
  if (StartLineIndex == UnwrappedLine::kInvalidIndex)
    return nullptr;
  assert(StartLineIndex < AnnotatedLines.size());
  const FormatToken *NamespaceTok = AnnotatedLines[StartLineIndex]->First;
  if (NamespaceTok->is(tok::l_brace)) {
    // "namespace" keyword can be on the line preceding '{', e.g. in styles
    // where BraceWrapping.AfterNamespace is true.
    if (StartLineIndex > 0)
      NamespaceTok = AnnotatedLines[StartLineIndex - 1]->First;
  }
  return NamespaceTok->getNamespaceToken();
}

NamespaceEndCommentsFixer::NamespaceEndCommentsFixer(const Environment &Env,
                                                     const FormatStyle &Style)
    : TokenAnalyzer(Env, Style) {}

std::pair<tooling::Replacements, unsigned> NamespaceEndCommentsFixer::analyze(
    TokenAnnotator &Annotator, SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
    FormatTokenLexer &Tokens) {
  const SourceManager &SourceMgr = Env.getSourceManager();
  AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
  tooling::Replacements Fixes;
  std::string AllNamespaceNames = "";
  size_t StartLineIndex = SIZE_MAX;
  unsigned int CompactedNamespacesCount = 0;
  for (size_t I = 0, E = AnnotatedLines.size(); I != E; ++I) {
    const AnnotatedLine *EndLine = AnnotatedLines[I];
    const FormatToken *NamespaceTok =
        getNamespaceToken(EndLine, AnnotatedLines);
    if (!NamespaceTok)
      continue;
    FormatToken *RBraceTok = EndLine->First;
    if (RBraceTok->Finalized)
      continue;
    RBraceTok->Finalized = true;
    const FormatToken *EndCommentPrevTok = RBraceTok;
    // Namespaces often end with '};'. In that case, attach namespace end
    // comments to the semicolon tokens.
    if (RBraceTok->Next && RBraceTok->Next->is(tok::semi)) {
      EndCommentPrevTok = RBraceTok->Next;
    }
    if (StartLineIndex == SIZE_MAX)
      StartLineIndex = EndLine->MatchingOpeningBlockLineIndex;
    std::string NamespaceName = computeName(NamespaceTok);
    if (Style.CompactNamespaces) {
      if ((I + 1 < E) &&
          getNamespaceToken(AnnotatedLines[I + 1], AnnotatedLines) &&
          StartLineIndex - CompactedNamespacesCount - 1 ==
              AnnotatedLines[I + 1]->MatchingOpeningBlockLineIndex &&
          !AnnotatedLines[I + 1]->First->Finalized) {
        if (hasEndComment(EndCommentPrevTok)) {
          // remove end comment, it will be merged in next one
          updateEndComment(EndCommentPrevTok, std::string(), SourceMgr, &Fixes);
        }
        CompactedNamespacesCount++;
        AllNamespaceNames = "::" + NamespaceName + AllNamespaceNames;
        continue;
      }
      NamespaceName += AllNamespaceNames;
      CompactedNamespacesCount = 0;
      AllNamespaceNames = std::string();
    }
    // The next token in the token stream after the place where the end comment
    // token must be. This is either the next token on the current line or the
    // first token on the next line.
    const FormatToken *EndCommentNextTok = EndCommentPrevTok->Next;
    if (EndCommentNextTok && EndCommentNextTok->is(tok::comment))
      EndCommentNextTok = EndCommentNextTok->Next;
    if (!EndCommentNextTok && I + 1 < E)
      EndCommentNextTok = AnnotatedLines[I + 1]->First;
    bool AddNewline = EndCommentNextTok &&
                      EndCommentNextTok->NewlinesBefore == 0 &&
                      EndCommentNextTok->isNot(tok::eof);
    const std::string EndCommentText =
        computeEndCommentText(NamespaceName, AddNewline);
    if (!hasEndComment(EndCommentPrevTok)) {
      bool isShort = I - StartLineIndex <= kShortNamespaceMaxLines + 1;
      if (!isShort)
        addEndComment(EndCommentPrevTok, EndCommentText, SourceMgr, &Fixes);
    } else if (!validEndComment(EndCommentPrevTok, NamespaceName)) {
      updateEndComment(EndCommentPrevTok, EndCommentText, SourceMgr, &Fixes);
    }
    StartLineIndex = SIZE_MAX;
  }
  return {Fixes, 0};
}

} // namespace format
} // namespace clang
