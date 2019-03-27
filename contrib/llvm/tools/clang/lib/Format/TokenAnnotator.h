//===--- TokenAnnotator.h - Format C++ code ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a token annotator, i.e. creates
/// \c AnnotatedTokens out of \c FormatTokens with required extra information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_TOKENANNOTATOR_H
#define LLVM_CLANG_LIB_FORMAT_TOKENANNOTATOR_H

#include "UnwrappedLineParser.h"
#include "clang/Format/Format.h"

namespace clang {
class SourceManager;

namespace format {

enum LineType {
  LT_Invalid,
  LT_ImportStatement,
  LT_ObjCDecl, // An @interface, @implementation, or @protocol line.
  LT_ObjCMethodDecl,
  LT_ObjCProperty, // An @property line.
  LT_Other,
  LT_PreprocessorDirective,
  LT_VirtualFunctionDecl
};

class AnnotatedLine {
public:
  AnnotatedLine(const UnwrappedLine &Line)
      : First(Line.Tokens.front().Tok), Level(Line.Level),
        MatchingOpeningBlockLineIndex(Line.MatchingOpeningBlockLineIndex),
        MatchingClosingBlockLineIndex(Line.MatchingClosingBlockLineIndex),
        InPPDirective(Line.InPPDirective),
        MustBeDeclaration(Line.MustBeDeclaration), MightBeFunctionDecl(false),
        IsMultiVariableDeclStmt(false), Affected(false),
        LeadingEmptyLinesAffected(false), ChildrenAffected(false),
        FirstStartColumn(Line.FirstStartColumn) {
    assert(!Line.Tokens.empty());

    // Calculate Next and Previous for all tokens. Note that we must overwrite
    // Next and Previous for every token, as previous formatting runs might have
    // left them in a different state.
    First->Previous = nullptr;
    FormatToken *Current = First;
    for (std::list<UnwrappedLineNode>::const_iterator I = ++Line.Tokens.begin(),
                                                      E = Line.Tokens.end();
         I != E; ++I) {
      const UnwrappedLineNode &Node = *I;
      Current->Next = I->Tok;
      I->Tok->Previous = Current;
      Current = Current->Next;
      Current->Children.clear();
      for (const auto &Child : Node.Children) {
        Children.push_back(new AnnotatedLine(Child));
        Current->Children.push_back(Children.back());
      }
    }
    Last = Current;
    Last->Next = nullptr;
  }

  ~AnnotatedLine() {
    for (unsigned i = 0, e = Children.size(); i != e; ++i) {
      delete Children[i];
    }
    FormatToken *Current = First;
    while (Current) {
      Current->Children.clear();
      Current->Role.reset();
      Current = Current->Next;
    }
  }

  /// \c true if this line starts with the given tokens in order, ignoring
  /// comments.
  template <typename... Ts> bool startsWith(Ts... Tokens) const {
    return First && First->startsSequence(Tokens...);
  }

  /// \c true if this line ends with the given tokens in reversed order,
  /// ignoring comments.
  /// For example, given tokens [T1, T2, T3, ...], the function returns true if
  /// this line is like "... T3 T2 T1".
  template <typename... Ts> bool endsWith(Ts... Tokens) const {
    return Last && Last->endsSequence(Tokens...);
  }

  /// \c true if this line looks like a function definition instead of a
  /// function declaration. Asserts MightBeFunctionDecl.
  bool mightBeFunctionDefinition() const {
    assert(MightBeFunctionDecl);
    // FIXME: Line.Last points to other characters than tok::semi
    // and tok::lbrace.
    return !Last->isOneOf(tok::semi, tok::comment);
  }

  /// \c true if this line starts a namespace definition.
  bool startsWithNamespace() const {
    return startsWith(tok::kw_namespace) ||
           startsWith(tok::kw_inline, tok::kw_namespace) ||
           startsWith(tok::kw_export, tok::kw_namespace);
  }

  FormatToken *First;
  FormatToken *Last;

  SmallVector<AnnotatedLine *, 0> Children;

  LineType Type;
  unsigned Level;
  size_t MatchingOpeningBlockLineIndex;
  size_t MatchingClosingBlockLineIndex;
  bool InPPDirective;
  bool MustBeDeclaration;
  bool MightBeFunctionDecl;
  bool IsMultiVariableDeclStmt;

  /// \c True if this line should be formatted, i.e. intersects directly or
  /// indirectly with one of the input ranges.
  bool Affected;

  /// \c True if the leading empty lines of this line intersect with one of the
  /// input ranges.
  bool LeadingEmptyLinesAffected;

  /// \c True if one of this line's children intersects with an input range.
  bool ChildrenAffected;

  unsigned FirstStartColumn;

private:
  // Disallow copying.
  AnnotatedLine(const AnnotatedLine &) = delete;
  void operator=(const AnnotatedLine &) = delete;
};

/// Determines extra information about the tokens comprising an
/// \c UnwrappedLine.
class TokenAnnotator {
public:
  TokenAnnotator(const FormatStyle &Style, const AdditionalKeywords &Keywords)
      : Style(Style), Keywords(Keywords) {}

  /// Adapts the indent levels of comment lines to the indent of the
  /// subsequent line.
  // FIXME: Can/should this be done in the UnwrappedLineParser?
  void setCommentLineLevels(SmallVectorImpl<AnnotatedLine *> &Lines);

  void annotate(AnnotatedLine &Line);
  void calculateFormattingInformation(AnnotatedLine &Line);

private:
  /// Calculate the penalty for splitting before \c Tok.
  unsigned splitPenalty(const AnnotatedLine &Line, const FormatToken &Tok,
                        bool InFunctionDecl);

  bool spaceRequiredBetween(const AnnotatedLine &Line, const FormatToken &Left,
                            const FormatToken &Right);

  bool spaceRequiredBefore(const AnnotatedLine &Line, const FormatToken &Right);

  bool mustBreakBefore(const AnnotatedLine &Line, const FormatToken &Right);

  bool canBreakBefore(const AnnotatedLine &Line, const FormatToken &Right);

  bool mustBreakForReturnType(const AnnotatedLine &Line) const;

  void printDebugInfo(const AnnotatedLine &Line);

  void calculateUnbreakableTailLengths(AnnotatedLine &Line);

  const FormatStyle &Style;

  const AdditionalKeywords &Keywords;
};

} // end namespace format
} // end namespace clang

#endif
