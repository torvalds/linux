//===--- ContinuationIndenter.cpp - Format C++ code -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the continuation indenter.
///
//===----------------------------------------------------------------------===//

#include "ContinuationIndenter.h"
#include "BreakableToken.h"
#include "FormatInternal.h"
#include "WhitespaceManager.h"
#include "clang/Basic/OperatorPrecedence.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "format-indenter"

namespace clang {
namespace format {

// Returns true if a TT_SelectorName should be indented when wrapped,
// false otherwise.
static bool shouldIndentWrappedSelectorName(const FormatStyle &Style,
                                            LineType LineType) {
  return Style.IndentWrappedFunctionNames || LineType == LT_ObjCMethodDecl;
}

// Returns the length of everything up to the first possible line break after
// the ), ], } or > matching \c Tok.
static unsigned getLengthToMatchingParen(const FormatToken &Tok,
                                         const std::vector<ParenState> &Stack) {
  // Normally whether or not a break before T is possible is calculated and
  // stored in T.CanBreakBefore. Braces, array initializers and text proto
  // messages like `key: < ... >` are an exception: a break is possible
  // before a closing brace R if a break was inserted after the corresponding
  // opening brace. The information about whether or not a break is needed
  // before a closing brace R is stored in the ParenState field
  // S.BreakBeforeClosingBrace where S is the state that R closes.
  //
  // In order to decide whether there can be a break before encountered right
  // braces, this implementation iterates over the sequence of tokens and over
  // the paren stack in lockstep, keeping track of the stack level which visited
  // right braces correspond to in MatchingStackIndex.
  //
  // For example, consider:
  // L. <- line number
  // 1. {
  // 2. {1},
  // 3. {2},
  // 4. {{3}}}
  //     ^ where we call this method with this token.
  // The paren stack at this point contains 3 brace levels:
  //  0. { at line 1, BreakBeforeClosingBrace: true
  //  1. first { at line 4, BreakBeforeClosingBrace: false
  //  2. second { at line 4, BreakBeforeClosingBrace: false,
  //  where there might be fake parens levels in-between these levels.
  // The algorithm will start at the first } on line 4, which is the matching
  // brace of the initial left brace and at level 2 of the stack. Then,
  // examining BreakBeforeClosingBrace: false at level 2, it will continue to
  // the second } on line 4, and will traverse the stack downwards until it
  // finds the matching { on level 1. Then, examining BreakBeforeClosingBrace:
  // false at level 1, it will continue to the third } on line 4 and will
  // traverse the stack downwards until it finds the matching { on level 0.
  // Then, examining BreakBeforeClosingBrace: true at level 0, the algorithm
  // will stop and will use the second } on line 4 to determine the length to
  // return, as in this example the range will include the tokens: {3}}
  //
  // The algorithm will only traverse the stack if it encounters braces, array
  // initializer squares or text proto angle brackets.
  if (!Tok.MatchingParen)
    return 0;
  FormatToken *End = Tok.MatchingParen;
  // Maintains a stack level corresponding to the current End token.
  int MatchingStackIndex = Stack.size() - 1;
  // Traverses the stack downwards, looking for the level to which LBrace
  // corresponds. Returns either a pointer to the matching level or nullptr if
  // LParen is not found in the initial portion of the stack up to
  // MatchingStackIndex.
  auto FindParenState = [&](const FormatToken *LBrace) -> const ParenState * {
    while (MatchingStackIndex >= 0 && Stack[MatchingStackIndex].Tok != LBrace)
      --MatchingStackIndex;
    return MatchingStackIndex >= 0 ? &Stack[MatchingStackIndex] : nullptr;
  };
  for (; End->Next; End = End->Next) {
    if (End->Next->CanBreakBefore)
      break;
    if (!End->Next->closesScope())
      continue;
    if (End->Next->MatchingParen &&
        End->Next->MatchingParen->isOneOf(
            tok::l_brace, TT_ArrayInitializerLSquare, tok::less)) {
      const ParenState *State = FindParenState(End->Next->MatchingParen);
      if (State && State->BreakBeforeClosingBrace)
        break;
    }
  }
  return End->TotalLength - Tok.TotalLength + 1;
}

static unsigned getLengthToNextOperator(const FormatToken &Tok) {
  if (!Tok.NextOperator)
    return 0;
  return Tok.NextOperator->TotalLength - Tok.TotalLength;
}

// Returns \c true if \c Tok is the "." or "->" of a call and starts the next
// segment of a builder type call.
static bool startsSegmentOfBuilderTypeCall(const FormatToken &Tok) {
  return Tok.isMemberAccess() && Tok.Previous && Tok.Previous->closesScope();
}

// Returns \c true if \c Current starts a new parameter.
static bool startsNextParameter(const FormatToken &Current,
                                const FormatStyle &Style) {
  const FormatToken &Previous = *Current.Previous;
  if (Current.is(TT_CtorInitializerComma) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma)
    return true;
  if (Style.Language == FormatStyle::LK_Proto && Current.is(TT_SelectorName))
    return true;
  return Previous.is(tok::comma) && !Current.isTrailingComment() &&
         ((Previous.isNot(TT_CtorInitializerComma) ||
           Style.BreakConstructorInitializers !=
               FormatStyle::BCIS_BeforeComma) &&
          (Previous.isNot(TT_InheritanceComma) ||
           Style.BreakInheritanceList != FormatStyle::BILS_BeforeComma));
}

static bool opensProtoMessageField(const FormatToken &LessTok,
                                   const FormatStyle &Style) {
  if (LessTok.isNot(tok::less))
    return false;
  return Style.Language == FormatStyle::LK_TextProto ||
         (Style.Language == FormatStyle::LK_Proto &&
          (LessTok.NestingLevel > 0 ||
           (LessTok.Previous && LessTok.Previous->is(tok::equal))));
}

// Returns the delimiter of a raw string literal, or None if TokenText is not
// the text of a raw string literal. The delimiter could be the empty string.
// For example, the delimiter of R"deli(cont)deli" is deli.
static llvm::Optional<StringRef> getRawStringDelimiter(StringRef TokenText) {
  if (TokenText.size() < 5 // The smallest raw string possible is 'R"()"'.
      || !TokenText.startswith("R\"") || !TokenText.endswith("\""))
    return None;

  // A raw string starts with 'R"<delimiter>(' and delimiter is ascii and has
  // size at most 16 by the standard, so the first '(' must be among the first
  // 19 bytes.
  size_t LParenPos = TokenText.substr(0, 19).find_first_of('(');
  if (LParenPos == StringRef::npos)
    return None;
  StringRef Delimiter = TokenText.substr(2, LParenPos - 2);

  // Check that the string ends in ')Delimiter"'.
  size_t RParenPos = TokenText.size() - Delimiter.size() - 2;
  if (TokenText[RParenPos] != ')')
    return None;
  if (!TokenText.substr(RParenPos + 1).startswith(Delimiter))
    return None;
  return Delimiter;
}

// Returns the canonical delimiter for \p Language, or the empty string if no
// canonical delimiter is specified.
static StringRef
getCanonicalRawStringDelimiter(const FormatStyle &Style,
                               FormatStyle::LanguageKind Language) {
  for (const auto &Format : Style.RawStringFormats) {
    if (Format.Language == Language)
      return StringRef(Format.CanonicalDelimiter);
  }
  return "";
}

RawStringFormatStyleManager::RawStringFormatStyleManager(
    const FormatStyle &CodeStyle) {
  for (const auto &RawStringFormat : CodeStyle.RawStringFormats) {
    llvm::Optional<FormatStyle> LanguageStyle =
        CodeStyle.GetLanguageStyle(RawStringFormat.Language);
    if (!LanguageStyle) {
      FormatStyle PredefinedStyle;
      if (!getPredefinedStyle(RawStringFormat.BasedOnStyle,
                              RawStringFormat.Language, &PredefinedStyle)) {
        PredefinedStyle = getLLVMStyle();
        PredefinedStyle.Language = RawStringFormat.Language;
      }
      LanguageStyle = PredefinedStyle;
    }
    LanguageStyle->ColumnLimit = CodeStyle.ColumnLimit;
    for (StringRef Delimiter : RawStringFormat.Delimiters) {
      DelimiterStyle.insert({Delimiter, *LanguageStyle});
    }
    for (StringRef EnclosingFunction : RawStringFormat.EnclosingFunctions) {
      EnclosingFunctionStyle.insert({EnclosingFunction, *LanguageStyle});
    }
  }
}

llvm::Optional<FormatStyle>
RawStringFormatStyleManager::getDelimiterStyle(StringRef Delimiter) const {
  auto It = DelimiterStyle.find(Delimiter);
  if (It == DelimiterStyle.end())
    return None;
  return It->second;
}

llvm::Optional<FormatStyle>
RawStringFormatStyleManager::getEnclosingFunctionStyle(
    StringRef EnclosingFunction) const {
  auto It = EnclosingFunctionStyle.find(EnclosingFunction);
  if (It == EnclosingFunctionStyle.end())
    return None;
  return It->second;
}

ContinuationIndenter::ContinuationIndenter(const FormatStyle &Style,
                                           const AdditionalKeywords &Keywords,
                                           const SourceManager &SourceMgr,
                                           WhitespaceManager &Whitespaces,
                                           encoding::Encoding Encoding,
                                           bool BinPackInconclusiveFunctions)
    : Style(Style), Keywords(Keywords), SourceMgr(SourceMgr),
      Whitespaces(Whitespaces), Encoding(Encoding),
      BinPackInconclusiveFunctions(BinPackInconclusiveFunctions),
      CommentPragmasRegex(Style.CommentPragmas), RawStringFormats(Style) {}

LineState ContinuationIndenter::getInitialState(unsigned FirstIndent,
                                                unsigned FirstStartColumn,
                                                const AnnotatedLine *Line,
                                                bool DryRun) {
  LineState State;
  State.FirstIndent = FirstIndent;
  if (FirstStartColumn && Line->First->NewlinesBefore == 0)
    State.Column = FirstStartColumn;
  else
    State.Column = FirstIndent;
  // With preprocessor directive indentation, the line starts on column 0
  // since it's indented after the hash, but FirstIndent is set to the
  // preprocessor indent.
  if (Style.IndentPPDirectives == FormatStyle::PPDIS_AfterHash &&
      (Line->Type == LT_PreprocessorDirective ||
       Line->Type == LT_ImportStatement))
    State.Column = 0;
  State.Line = Line;
  State.NextToken = Line->First;
  State.Stack.push_back(ParenState(/*Tok=*/nullptr, FirstIndent, FirstIndent,
                                   /*AvoidBinPacking=*/false,
                                   /*NoLineBreak=*/false));
  State.LineContainsContinuedForLoopSection = false;
  State.NoContinuation = false;
  State.StartOfStringLiteral = 0;
  State.StartOfLineLevel = 0;
  State.LowestLevelOnLine = 0;
  State.IgnoreStackForComparison = false;

  if (Style.Language == FormatStyle::LK_TextProto) {
    // We need this in order to deal with the bin packing of text fields at
    // global scope.
    State.Stack.back().AvoidBinPacking = true;
    State.Stack.back().BreakBeforeParameter = true;
    State.Stack.back().AlignColons = false;
  }

  // The first token has already been indented and thus consumed.
  moveStateToNextToken(State, DryRun, /*Newline=*/false);
  return State;
}

bool ContinuationIndenter::canBreak(const LineState &State) {
  const FormatToken &Current = *State.NextToken;
  const FormatToken &Previous = *Current.Previous;
  assert(&Previous == Current.Previous);
  if (!Current.CanBreakBefore && !(State.Stack.back().BreakBeforeClosingBrace &&
                                   Current.closesBlockOrBlockTypeList(Style)))
    return false;
  // The opening "{" of a braced list has to be on the same line as the first
  // element if it is nested in another braced init list or function call.
  if (!Current.MustBreakBefore && Previous.is(tok::l_brace) &&
      Previous.isNot(TT_DictLiteral) && Previous.BlockKind == BK_BracedInit &&
      Previous.Previous &&
      Previous.Previous->isOneOf(tok::l_brace, tok::l_paren, tok::comma))
    return false;
  // This prevents breaks like:
  //   ...
  //   SomeParameter, OtherParameter).DoSomething(
  //   ...
  // As they hide "DoSomething" and are generally bad for readability.
  if (Previous.opensScope() && Previous.isNot(tok::l_brace) &&
      State.LowestLevelOnLine < State.StartOfLineLevel &&
      State.LowestLevelOnLine < Current.NestingLevel)
    return false;
  if (Current.isMemberAccess() && State.Stack.back().ContainsUnwrappedBuilder)
    return false;

  // Don't create a 'hanging' indent if there are multiple blocks in a single
  // statement.
  if (Previous.is(tok::l_brace) && State.Stack.size() > 1 &&
      State.Stack[State.Stack.size() - 2].NestedBlockInlined &&
      State.Stack[State.Stack.size() - 2].HasMultipleNestedBlocks)
    return false;

  // Don't break after very short return types (e.g. "void") as that is often
  // unexpected.
  if (Current.is(TT_FunctionDeclarationName) && State.Column < 6) {
    if (Style.AlwaysBreakAfterReturnType == FormatStyle::RTBS_None)
      return false;
  }

  // If binary operators are moved to the next line (including commas for some
  // styles of constructor initializers), that's always ok.
  if (!Current.isOneOf(TT_BinaryOperator, tok::comma) &&
      State.Stack.back().NoLineBreakInOperand)
    return false;

  if (Previous.is(tok::l_square) && Previous.is(TT_ObjCMethodExpr))
    return false;

  return !State.Stack.back().NoLineBreak;
}

bool ContinuationIndenter::mustBreak(const LineState &State) {
  const FormatToken &Current = *State.NextToken;
  const FormatToken &Previous = *Current.Previous;
  if (Current.MustBreakBefore || Current.is(TT_InlineASMColon))
    return true;
  if (State.Stack.back().BreakBeforeClosingBrace &&
      Current.closesBlockOrBlockTypeList(Style))
    return true;
  if (Previous.is(tok::semi) && State.LineContainsContinuedForLoopSection)
    return true;
  if (Style.Language == FormatStyle::LK_ObjC &&
      Current.ObjCSelectorNameParts > 1 &&
      Current.startsSequence(TT_SelectorName, tok::colon, tok::caret)) {
    return true;
  }
  if ((startsNextParameter(Current, Style) || Previous.is(tok::semi) ||
       (Previous.is(TT_TemplateCloser) && Current.is(TT_StartOfName) &&
        Style.isCpp() &&
        // FIXME: This is a temporary workaround for the case where clang-format
        // sets BreakBeforeParameter to avoid bin packing and this creates a
        // completely unnecessary line break after a template type that isn't
        // line-wrapped.
        (Previous.NestingLevel == 1 || Style.BinPackParameters)) ||
       (Style.BreakBeforeTernaryOperators && Current.is(TT_ConditionalExpr) &&
        Previous.isNot(tok::question)) ||
       (!Style.BreakBeforeTernaryOperators &&
        Previous.is(TT_ConditionalExpr))) &&
      State.Stack.back().BreakBeforeParameter && !Current.isTrailingComment() &&
      !Current.isOneOf(tok::r_paren, tok::r_brace))
    return true;
  if (((Previous.is(TT_DictLiteral) && Previous.is(tok::l_brace)) ||
       (Previous.is(TT_ArrayInitializerLSquare) &&
        Previous.ParameterCount > 1) ||
       opensProtoMessageField(Previous, Style)) &&
      Style.ColumnLimit > 0 &&
      getLengthToMatchingParen(Previous, State.Stack) + State.Column - 1 >
          getColumnLimit(State))
    return true;

  const FormatToken &BreakConstructorInitializersToken =
      Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon
          ? Previous
          : Current;
  if (BreakConstructorInitializersToken.is(TT_CtorInitializerColon) &&
      (State.Column + State.Line->Last->TotalLength - Previous.TotalLength >
           getColumnLimit(State) ||
       State.Stack.back().BreakBeforeParameter) &&
      (Style.AllowShortFunctionsOnASingleLine != FormatStyle::SFS_All ||
       Style.BreakConstructorInitializers != FormatStyle::BCIS_BeforeColon ||
       Style.ColumnLimit != 0))
    return true;

  if (Current.is(TT_ObjCMethodExpr) && !Previous.is(TT_SelectorName) &&
      State.Line->startsWith(TT_ObjCMethodSpecifier))
    return true;
  if (Current.is(TT_SelectorName) && !Previous.is(tok::at) &&
      State.Stack.back().ObjCSelectorNameFound &&
      State.Stack.back().BreakBeforeParameter)
    return true;

  unsigned NewLineColumn = getNewLineColumn(State);
  if (Current.isMemberAccess() && Style.ColumnLimit != 0 &&
      State.Column + getLengthToNextOperator(Current) > Style.ColumnLimit &&
      (State.Column > NewLineColumn ||
       Current.NestingLevel < State.StartOfLineLevel))
    return true;

  if (startsSegmentOfBuilderTypeCall(Current) &&
      (State.Stack.back().CallContinuation != 0 ||
       State.Stack.back().BreakBeforeParameter) &&
      // JavaScript is treated different here as there is a frequent pattern:
      //   SomeFunction(function() {
      //     ...
      //   }.bind(...));
      // FIXME: We should find a more generic solution to this problem.
      !(State.Column <= NewLineColumn &&
        Style.Language == FormatStyle::LK_JavaScript) &&
      !(Previous.closesScopeAfterBlock() &&
        State.Column <= NewLineColumn))
    return true;

  // If the template declaration spans multiple lines, force wrap before the
  // function/class declaration
  if (Previous.ClosesTemplateDeclaration &&
      State.Stack.back().BreakBeforeParameter && Current.CanBreakBefore)
    return true;

  if (State.Column <= NewLineColumn)
    return false;

  if (Style.AlwaysBreakBeforeMultilineStrings &&
      (NewLineColumn == State.FirstIndent + Style.ContinuationIndentWidth ||
       Previous.is(tok::comma) || Current.NestingLevel < 2) &&
      !Previous.isOneOf(tok::kw_return, tok::lessless, tok::at) &&
      !Previous.isOneOf(TT_InlineASMColon, TT_ConditionalExpr) &&
      nextIsMultilineString(State))
    return true;

  // Using CanBreakBefore here and below takes care of the decision whether the
  // current style uses wrapping before or after operators for the given
  // operator.
  if (Previous.is(TT_BinaryOperator) && Current.CanBreakBefore) {
    // If we need to break somewhere inside the LHS of a binary expression, we
    // should also break after the operator. Otherwise, the formatting would
    // hide the operator precedence, e.g. in:
    //   if (aaaaaaaaaaaaaa ==
    //           bbbbbbbbbbbbbb && c) {..
    // For comparisons, we only apply this rule, if the LHS is a binary
    // expression itself as otherwise, the line breaks seem superfluous.
    // We need special cases for ">>" which we have split into two ">" while
    // lexing in order to make template parsing easier.
    bool IsComparison = (Previous.getPrecedence() == prec::Relational ||
                         Previous.getPrecedence() == prec::Equality ||
                         Previous.getPrecedence() == prec::Spaceship) &&
                        Previous.Previous &&
                        Previous.Previous->isNot(TT_BinaryOperator); // For >>.
    bool LHSIsBinaryExpr =
        Previous.Previous && Previous.Previous->EndsBinaryExpression;
    if ((!IsComparison || LHSIsBinaryExpr) && !Current.isTrailingComment() &&
        Previous.getPrecedence() != prec::Assignment &&
        State.Stack.back().BreakBeforeParameter)
      return true;
  } else if (Current.is(TT_BinaryOperator) && Current.CanBreakBefore &&
             State.Stack.back().BreakBeforeParameter) {
    return true;
  }

  // Same as above, but for the first "<<" operator.
  if (Current.is(tok::lessless) && Current.isNot(TT_OverloadedOperator) &&
      State.Stack.back().BreakBeforeParameter &&
      State.Stack.back().FirstLessLess == 0)
    return true;

  if (Current.NestingLevel == 0 && !Current.isTrailingComment()) {
    // Always break after "template <...>" and leading annotations. This is only
    // for cases where the entire line does not fit on a single line as a
    // different LineFormatter would be used otherwise.
    if (Previous.ClosesTemplateDeclaration)
      return Style.AlwaysBreakTemplateDeclarations != FormatStyle::BTDS_No;
    if (Previous.is(TT_FunctionAnnotationRParen))
      return true;
    if (Previous.is(TT_LeadingJavaAnnotation) && Current.isNot(tok::l_paren) &&
        Current.isNot(TT_LeadingJavaAnnotation))
      return true;
  }

  // If the return type spans multiple lines, wrap before the function name.
  if ((Current.is(TT_FunctionDeclarationName) ||
       (Current.is(tok::kw_operator) && !Previous.is(tok::coloncolon))) &&
      !Previous.is(tok::kw_template) && State.Stack.back().BreakBeforeParameter)
    return true;

  // The following could be precomputed as they do not depend on the state.
  // However, as they should take effect only if the UnwrappedLine does not fit
  // into the ColumnLimit, they are checked here in the ContinuationIndenter.
  if (Style.ColumnLimit != 0 && Previous.BlockKind == BK_Block &&
      Previous.is(tok::l_brace) && !Current.isOneOf(tok::r_brace, tok::comment))
    return true;

  if (Current.is(tok::lessless) &&
      ((Previous.is(tok::identifier) && Previous.TokenText == "endl") ||
       (Previous.Tok.isLiteral() && (Previous.TokenText.endswith("\\n\"") ||
                                     Previous.TokenText == "\'\\n\'"))))
    return true;

  if (Previous.is(TT_BlockComment) && Previous.IsMultiline)
    return true;

  if (State.NoContinuation)
    return true;

  return false;
}

unsigned ContinuationIndenter::addTokenToState(LineState &State, bool Newline,
                                               bool DryRun,
                                               unsigned ExtraSpaces) {
  const FormatToken &Current = *State.NextToken;

  assert(!State.Stack.empty());
  State.NoContinuation = false;

  if ((Current.is(TT_ImplicitStringLiteral) &&
       (Current.Previous->Tok.getIdentifierInfo() == nullptr ||
        Current.Previous->Tok.getIdentifierInfo()->getPPKeywordID() ==
            tok::pp_not_keyword))) {
    unsigned EndColumn =
        SourceMgr.getSpellingColumnNumber(Current.WhitespaceRange.getEnd());
    if (Current.LastNewlineOffset != 0) {
      // If there is a newline within this token, the final column will solely
      // determined by the current end column.
      State.Column = EndColumn;
    } else {
      unsigned StartColumn =
          SourceMgr.getSpellingColumnNumber(Current.WhitespaceRange.getBegin());
      assert(EndColumn >= StartColumn);
      State.Column += EndColumn - StartColumn;
    }
    moveStateToNextToken(State, DryRun, /*Newline=*/false);
    return 0;
  }

  unsigned Penalty = 0;
  if (Newline)
    Penalty = addTokenOnNewLine(State, DryRun);
  else
    addTokenOnCurrentLine(State, DryRun, ExtraSpaces);

  return moveStateToNextToken(State, DryRun, Newline) + Penalty;
}

void ContinuationIndenter::addTokenOnCurrentLine(LineState &State, bool DryRun,
                                                 unsigned ExtraSpaces) {
  FormatToken &Current = *State.NextToken;
  const FormatToken &Previous = *State.NextToken->Previous;
  if (Current.is(tok::equal) &&
      (State.Line->First->is(tok::kw_for) || Current.NestingLevel == 0) &&
      State.Stack.back().VariablePos == 0) {
    State.Stack.back().VariablePos = State.Column;
    // Move over * and & if they are bound to the variable name.
    const FormatToken *Tok = &Previous;
    while (Tok && State.Stack.back().VariablePos >= Tok->ColumnWidth) {
      State.Stack.back().VariablePos -= Tok->ColumnWidth;
      if (Tok->SpacesRequiredBefore != 0)
        break;
      Tok = Tok->Previous;
    }
    if (Previous.PartOfMultiVariableDeclStmt)
      State.Stack.back().LastSpace = State.Stack.back().VariablePos;
  }

  unsigned Spaces = Current.SpacesRequiredBefore + ExtraSpaces;

  // Indent preprocessor directives after the hash if required.
  int PPColumnCorrection = 0;
  if (Style.IndentPPDirectives == FormatStyle::PPDIS_AfterHash &&
      Previous.is(tok::hash) && State.FirstIndent > 0 &&
      (State.Line->Type == LT_PreprocessorDirective ||
       State.Line->Type == LT_ImportStatement)) {
    Spaces += State.FirstIndent;

    // For preprocessor indent with tabs, State.Column will be 1 because of the
    // hash. This causes second-level indents onward to have an extra space
    // after the tabs. We avoid this misalignment by subtracting 1 from the
    // column value passed to replaceWhitespace().
    if (Style.UseTab != FormatStyle::UT_Never)
      PPColumnCorrection = -1;
  }

  if (!DryRun)
    Whitespaces.replaceWhitespace(Current, /*Newlines=*/0, Spaces,
                                  State.Column + Spaces + PPColumnCorrection);

  // If "BreakBeforeInheritanceComma" mode, don't break within the inheritance
  // declaration unless there is multiple inheritance.
  if (Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma &&
      Current.is(TT_InheritanceColon))
    State.Stack.back().NoLineBreak = true;
  if (Style.BreakInheritanceList == FormatStyle::BILS_AfterColon &&
      Previous.is(TT_InheritanceColon))
    State.Stack.back().NoLineBreak = true;

  if (Current.is(TT_SelectorName) &&
      !State.Stack.back().ObjCSelectorNameFound) {
    unsigned MinIndent =
        std::max(State.FirstIndent + Style.ContinuationIndentWidth,
                 State.Stack.back().Indent);
    unsigned FirstColonPos = State.Column + Spaces + Current.ColumnWidth;
    if (Current.LongestObjCSelectorName == 0)
      State.Stack.back().AlignColons = false;
    else if (MinIndent + Current.LongestObjCSelectorName > FirstColonPos)
      State.Stack.back().ColonPos = MinIndent + Current.LongestObjCSelectorName;
    else
      State.Stack.back().ColonPos = FirstColonPos;
  }

  // In "AlwaysBreak" mode, enforce wrapping directly after the parenthesis by
  // disallowing any further line breaks if there is no line break after the
  // opening parenthesis. Don't break if it doesn't conserve columns.
  if (Style.AlignAfterOpenBracket == FormatStyle::BAS_AlwaysBreak &&
      Previous.isOneOf(tok::l_paren, TT_TemplateOpener, tok::l_square) &&
      State.Column > getNewLineColumn(State) &&
      (!Previous.Previous || !Previous.Previous->isOneOf(
                                 tok::kw_for, tok::kw_while, tok::kw_switch)) &&
      // Don't do this for simple (no expressions) one-argument function calls
      // as that feels like needlessly wasting whitespace, e.g.:
      //
      //   caaaaaaaaaaaall(
      //       caaaaaaaaaaaall(
      //           caaaaaaaaaaaall(
      //               caaaaaaaaaaaaaaaaaaaaaaall(aaaaaaaaaaaaaa, aaaaaaaaa))));
      Current.FakeLParens.size() > 0 &&
      Current.FakeLParens.back() > prec::Unknown)
    State.Stack.back().NoLineBreak = true;
  if (Previous.is(TT_TemplateString) && Previous.opensScope())
    State.Stack.back().NoLineBreak = true;

  if (Style.AlignAfterOpenBracket != FormatStyle::BAS_DontAlign &&
      Previous.opensScope() && Previous.isNot(TT_ObjCMethodExpr) &&
      (Current.isNot(TT_LineComment) || Previous.BlockKind == BK_BracedInit))
    State.Stack.back().Indent = State.Column + Spaces;
  if (State.Stack.back().AvoidBinPacking && startsNextParameter(Current, Style))
    State.Stack.back().NoLineBreak = true;
  if (startsSegmentOfBuilderTypeCall(Current) &&
      State.Column > getNewLineColumn(State))
    State.Stack.back().ContainsUnwrappedBuilder = true;

  if (Current.is(TT_LambdaArrow) && Style.Language == FormatStyle::LK_Java)
    State.Stack.back().NoLineBreak = true;
  if (Current.isMemberAccess() && Previous.is(tok::r_paren) &&
      (Previous.MatchingParen &&
       (Previous.TotalLength - Previous.MatchingParen->TotalLength > 10)))
    // If there is a function call with long parameters, break before trailing
    // calls. This prevents things like:
    //   EXPECT_CALL(SomeLongParameter).Times(
    //       2);
    // We don't want to do this for short parameters as they can just be
    // indexes.
    State.Stack.back().NoLineBreak = true;

  // Don't allow the RHS of an operator to be split over multiple lines unless
  // there is a line-break right after the operator.
  // Exclude relational operators, as there, it is always more desirable to
  // have the LHS 'left' of the RHS.
  const FormatToken *P = Current.getPreviousNonComment();
  if (!Current.is(tok::comment) && P &&
      (P->isOneOf(TT_BinaryOperator, tok::comma) ||
       (P->is(TT_ConditionalExpr) && P->is(tok::colon))) &&
      !P->isOneOf(TT_OverloadedOperator, TT_CtorInitializerComma) &&
      P->getPrecedence() != prec::Assignment &&
      P->getPrecedence() != prec::Relational &&
      P->getPrecedence() != prec::Spaceship) {
    bool BreakBeforeOperator =
        P->MustBreakBefore || P->is(tok::lessless) ||
        (P->is(TT_BinaryOperator) &&
         Style.BreakBeforeBinaryOperators != FormatStyle::BOS_None) ||
        (P->is(TT_ConditionalExpr) && Style.BreakBeforeTernaryOperators);
    // Don't do this if there are only two operands. In these cases, there is
    // always a nice vertical separation between them and the extra line break
    // does not help.
    bool HasTwoOperands =
        P->OperatorIndex == 0 && !P->NextOperator && !P->is(TT_ConditionalExpr);
    if ((!BreakBeforeOperator && !(HasTwoOperands && Style.AlignOperands)) ||
        (!State.Stack.back().LastOperatorWrapped && BreakBeforeOperator))
      State.Stack.back().NoLineBreakInOperand = true;
  }

  State.Column += Spaces;
  if (Current.isNot(tok::comment) && Previous.is(tok::l_paren) &&
      Previous.Previous &&
      (Previous.Previous->isOneOf(tok::kw_if, tok::kw_for) ||
       Previous.Previous->endsSequence(tok::kw_constexpr, tok::kw_if))) {
    // Treat the condition inside an if as if it was a second function
    // parameter, i.e. let nested calls have a continuation indent.
    State.Stack.back().LastSpace = State.Column;
    State.Stack.back().NestedBlockIndent = State.Column;
  } else if (!Current.isOneOf(tok::comment, tok::caret) &&
             ((Previous.is(tok::comma) &&
               !Previous.is(TT_OverloadedOperator)) ||
              (Previous.is(tok::colon) && Previous.is(TT_ObjCMethodExpr)))) {
    State.Stack.back().LastSpace = State.Column;
  } else if (Previous.is(TT_CtorInitializerColon) &&
             Style.BreakConstructorInitializers ==
                 FormatStyle::BCIS_AfterColon) {
    State.Stack.back().Indent = State.Column;
    State.Stack.back().LastSpace = State.Column;
  } else if ((Previous.isOneOf(TT_BinaryOperator, TT_ConditionalExpr,
                               TT_CtorInitializerColon)) &&
             ((Previous.getPrecedence() != prec::Assignment &&
               (Previous.isNot(tok::lessless) || Previous.OperatorIndex != 0 ||
                Previous.NextOperator)) ||
              Current.StartsBinaryExpression)) {
    // Indent relative to the RHS of the expression unless this is a simple
    // assignment without binary expression on the RHS. Also indent relative to
    // unary operators and the colons of constructor initializers.
    if (Style.BreakBeforeBinaryOperators == FormatStyle::BOS_None)
      State.Stack.back().LastSpace = State.Column;
  } else if (Previous.is(TT_InheritanceColon)) {
    State.Stack.back().Indent = State.Column;
    State.Stack.back().LastSpace = State.Column;
  } else if (Previous.opensScope()) {
    // If a function has a trailing call, indent all parameters from the
    // opening parenthesis. This avoids confusing indents like:
    //   OuterFunction(InnerFunctionCall( // break
    //       ParameterToInnerFunction))   // break
    //       .SecondInnerFunctionCall();
    bool HasTrailingCall = false;
    if (Previous.MatchingParen) {
      const FormatToken *Next = Previous.MatchingParen->getNextNonComment();
      HasTrailingCall = Next && Next->isMemberAccess();
    }
    if (HasTrailingCall && State.Stack.size() > 1 &&
        State.Stack[State.Stack.size() - 2].CallContinuation == 0)
      State.Stack.back().LastSpace = State.Column;
  }
}

unsigned ContinuationIndenter::addTokenOnNewLine(LineState &State,
                                                 bool DryRun) {
  FormatToken &Current = *State.NextToken;
  const FormatToken &Previous = *State.NextToken->Previous;

  // Extra penalty that needs to be added because of the way certain line
  // breaks are chosen.
  unsigned Penalty = 0;

  const FormatToken *PreviousNonComment = Current.getPreviousNonComment();
  const FormatToken *NextNonComment = Previous.getNextNonComment();
  if (!NextNonComment)
    NextNonComment = &Current;
  // The first line break on any NestingLevel causes an extra penalty in order
  // prefer similar line breaks.
  if (!State.Stack.back().ContainsLineBreak)
    Penalty += 15;
  State.Stack.back().ContainsLineBreak = true;

  Penalty += State.NextToken->SplitPenalty;

  // Breaking before the first "<<" is generally not desirable if the LHS is
  // short. Also always add the penalty if the LHS is split over multiple lines
  // to avoid unnecessary line breaks that just work around this penalty.
  if (NextNonComment->is(tok::lessless) &&
      State.Stack.back().FirstLessLess == 0 &&
      (State.Column <= Style.ColumnLimit / 3 ||
       State.Stack.back().BreakBeforeParameter))
    Penalty += Style.PenaltyBreakFirstLessLess;

  State.Column = getNewLineColumn(State);

  // Indent nested blocks relative to this column, unless in a very specific
  // JavaScript special case where:
  //
  //   var loooooong_name =
  //       function() {
  //     // code
  //   }
  //
  // is common and should be formatted like a free-standing function. The same
  // goes for wrapping before the lambda return type arrow.
  if (!Current.is(TT_LambdaArrow) &&
      (Style.Language != FormatStyle::LK_JavaScript ||
       Current.NestingLevel != 0 || !PreviousNonComment ||
       !PreviousNonComment->is(tok::equal) ||
       !Current.isOneOf(Keywords.kw_async, Keywords.kw_function)))
    State.Stack.back().NestedBlockIndent = State.Column;

  if (NextNonComment->isMemberAccess()) {
    if (State.Stack.back().CallContinuation == 0)
      State.Stack.back().CallContinuation = State.Column;
  } else if (NextNonComment->is(TT_SelectorName)) {
    if (!State.Stack.back().ObjCSelectorNameFound) {
      if (NextNonComment->LongestObjCSelectorName == 0) {
        State.Stack.back().AlignColons = false;
      } else {
        State.Stack.back().ColonPos =
            (shouldIndentWrappedSelectorName(Style, State.Line->Type)
                 ? std::max(State.Stack.back().Indent,
                            State.FirstIndent + Style.ContinuationIndentWidth)
                 : State.Stack.back().Indent) +
            std::max(NextNonComment->LongestObjCSelectorName,
                     NextNonComment->ColumnWidth);
      }
    } else if (State.Stack.back().AlignColons &&
               State.Stack.back().ColonPos <= NextNonComment->ColumnWidth) {
      State.Stack.back().ColonPos = State.Column + NextNonComment->ColumnWidth;
    }
  } else if (PreviousNonComment && PreviousNonComment->is(tok::colon) &&
             PreviousNonComment->isOneOf(TT_ObjCMethodExpr, TT_DictLiteral)) {
    // FIXME: This is hacky, find a better way. The problem is that in an ObjC
    // method expression, the block should be aligned to the line starting it,
    // e.g.:
    //   [aaaaaaaaaaaaaaa aaaaaaaaa: \\ break for some reason
    //                        ^(int *i) {
    //                            // ...
    //                        }];
    // Thus, we set LastSpace of the next higher NestingLevel, to which we move
    // when we consume all of the "}"'s FakeRParens at the "{".
    if (State.Stack.size() > 1)
      State.Stack[State.Stack.size() - 2].LastSpace =
          std::max(State.Stack.back().LastSpace, State.Stack.back().Indent) +
          Style.ContinuationIndentWidth;
  }

  if ((PreviousNonComment &&
       PreviousNonComment->isOneOf(tok::comma, tok::semi) &&
       !State.Stack.back().AvoidBinPacking) ||
      Previous.is(TT_BinaryOperator))
    State.Stack.back().BreakBeforeParameter = false;
  if (PreviousNonComment &&
      PreviousNonComment->isOneOf(TT_TemplateCloser, TT_JavaAnnotation) &&
      Current.NestingLevel == 0)
    State.Stack.back().BreakBeforeParameter = false;
  if (NextNonComment->is(tok::question) ||
      (PreviousNonComment && PreviousNonComment->is(tok::question)))
    State.Stack.back().BreakBeforeParameter = true;
  if (Current.is(TT_BinaryOperator) && Current.CanBreakBefore)
    State.Stack.back().BreakBeforeParameter = false;

  if (!DryRun) {
    unsigned MaxEmptyLinesToKeep = Style.MaxEmptyLinesToKeep + 1;
    if (Current.is(tok::r_brace) && Current.MatchingParen &&
        // Only strip trailing empty lines for l_braces that have children, i.e.
        // for function expressions (lambdas, arrows, etc).
        !Current.MatchingParen->Children.empty()) {
      // lambdas and arrow functions are expressions, thus their r_brace is not
      // on its own line, and thus not covered by UnwrappedLineFormatter's logic
      // about removing empty lines on closing blocks. Special case them here.
      MaxEmptyLinesToKeep = 1;
    }
    unsigned Newlines = std::max(
        1u, std::min(Current.NewlinesBefore, MaxEmptyLinesToKeep));
    bool ContinuePPDirective =
        State.Line->InPPDirective && State.Line->Type != LT_ImportStatement;
    Whitespaces.replaceWhitespace(Current, Newlines, State.Column, State.Column,
                                  ContinuePPDirective);
  }

  if (!Current.isTrailingComment())
    State.Stack.back().LastSpace = State.Column;
  if (Current.is(tok::lessless))
    // If we are breaking before a "<<", we always want to indent relative to
    // RHS. This is necessary only for "<<", as we special-case it and don't
    // always indent relative to the RHS.
    State.Stack.back().LastSpace += 3; // 3 -> width of "<< ".

  State.StartOfLineLevel = Current.NestingLevel;
  State.LowestLevelOnLine = Current.NestingLevel;

  // Any break on this level means that the parent level has been broken
  // and we need to avoid bin packing there.
  bool NestedBlockSpecialCase =
      !Style.isCpp() && Current.is(tok::r_brace) && State.Stack.size() > 1 &&
      State.Stack[State.Stack.size() - 2].NestedBlockInlined;
  if (!NestedBlockSpecialCase)
    for (unsigned i = 0, e = State.Stack.size() - 1; i != e; ++i)
      State.Stack[i].BreakBeforeParameter = true;

  if (PreviousNonComment &&
      !PreviousNonComment->isOneOf(tok::comma, tok::colon, tok::semi) &&
      (PreviousNonComment->isNot(TT_TemplateCloser) ||
       Current.NestingLevel != 0) &&
      !PreviousNonComment->isOneOf(
          TT_BinaryOperator, TT_FunctionAnnotationRParen, TT_JavaAnnotation,
          TT_LeadingJavaAnnotation) &&
      Current.isNot(TT_BinaryOperator) && !PreviousNonComment->opensScope())
    State.Stack.back().BreakBeforeParameter = true;

  // If we break after { or the [ of an array initializer, we should also break
  // before the corresponding } or ].
  if (PreviousNonComment &&
      (PreviousNonComment->isOneOf(tok::l_brace, TT_ArrayInitializerLSquare) ||
       opensProtoMessageField(*PreviousNonComment, Style)))
    State.Stack.back().BreakBeforeClosingBrace = true;

  if (State.Stack.back().AvoidBinPacking) {
    // If we are breaking after '(', '{', '<', this is not bin packing
    // unless AllowAllParametersOfDeclarationOnNextLine is false or this is a
    // dict/object literal.
    if (!Previous.isOneOf(tok::l_paren, tok::l_brace, TT_BinaryOperator) ||
        (!Style.AllowAllParametersOfDeclarationOnNextLine &&
         State.Line->MustBeDeclaration) ||
        Previous.is(TT_DictLiteral))
      State.Stack.back().BreakBeforeParameter = true;
  }

  return Penalty;
}

unsigned ContinuationIndenter::getNewLineColumn(const LineState &State) {
  if (!State.NextToken || !State.NextToken->Previous)
    return 0;
  FormatToken &Current = *State.NextToken;
  const FormatToken &Previous = *Current.Previous;
  // If we are continuing an expression, we want to use the continuation indent.
  unsigned ContinuationIndent =
      std::max(State.Stack.back().LastSpace, State.Stack.back().Indent) +
      Style.ContinuationIndentWidth;
  const FormatToken *PreviousNonComment = Current.getPreviousNonComment();
  const FormatToken *NextNonComment = Previous.getNextNonComment();
  if (!NextNonComment)
    NextNonComment = &Current;

  // Java specific bits.
  if (Style.Language == FormatStyle::LK_Java &&
      Current.isOneOf(Keywords.kw_implements, Keywords.kw_extends))
    return std::max(State.Stack.back().LastSpace,
                    State.Stack.back().Indent + Style.ContinuationIndentWidth);

  if (NextNonComment->is(tok::l_brace) && NextNonComment->BlockKind == BK_Block)
    return Current.NestingLevel == 0 ? State.FirstIndent
                                     : State.Stack.back().Indent;
  if ((Current.isOneOf(tok::r_brace, tok::r_square) ||
       (Current.is(tok::greater) &&
        (Style.Language == FormatStyle::LK_Proto ||
         Style.Language == FormatStyle::LK_TextProto))) &&
      State.Stack.size() > 1) {
    if (Current.closesBlockOrBlockTypeList(Style))
      return State.Stack[State.Stack.size() - 2].NestedBlockIndent;
    if (Current.MatchingParen &&
        Current.MatchingParen->BlockKind == BK_BracedInit)
      return State.Stack[State.Stack.size() - 2].LastSpace;
    return State.FirstIndent;
  }
  // Indent a closing parenthesis at the previous level if followed by a semi or
  // opening brace. This allows indentations such as:
  //     foo(
  //       a,
  //     );
  //     function foo(
  //       a,
  //     ) {
  //       code(); //
  //     }
  if (Current.is(tok::r_paren) && State.Stack.size() > 1 &&
      (!Current.Next || Current.Next->isOneOf(tok::semi, tok::l_brace)))
    return State.Stack[State.Stack.size() - 2].LastSpace;
  if (NextNonComment->is(TT_TemplateString) && NextNonComment->closesScope())
    return State.Stack[State.Stack.size() - 2].LastSpace;
  if (Current.is(tok::identifier) && Current.Next &&
      (Current.Next->is(TT_DictLiteral) ||
       ((Style.Language == FormatStyle::LK_Proto ||
         Style.Language == FormatStyle::LK_TextProto) &&
        Current.Next->isOneOf(tok::less, tok::l_brace))))
    return State.Stack.back().Indent;
  if (NextNonComment->is(TT_ObjCStringLiteral) &&
      State.StartOfStringLiteral != 0)
    return State.StartOfStringLiteral - 1;
  if (NextNonComment->isStringLiteral() && State.StartOfStringLiteral != 0)
    return State.StartOfStringLiteral;
  if (NextNonComment->is(tok::lessless) &&
      State.Stack.back().FirstLessLess != 0)
    return State.Stack.back().FirstLessLess;
  if (NextNonComment->isMemberAccess()) {
    if (State.Stack.back().CallContinuation == 0)
      return ContinuationIndent;
    return State.Stack.back().CallContinuation;
  }
  if (State.Stack.back().QuestionColumn != 0 &&
      ((NextNonComment->is(tok::colon) &&
        NextNonComment->is(TT_ConditionalExpr)) ||
       Previous.is(TT_ConditionalExpr)))
    return State.Stack.back().QuestionColumn;
  if (Previous.is(tok::comma) && State.Stack.back().VariablePos != 0)
    return State.Stack.back().VariablePos;
  if ((PreviousNonComment &&
       (PreviousNonComment->ClosesTemplateDeclaration ||
        PreviousNonComment->isOneOf(
            TT_AttributeParen, TT_AttributeSquare, TT_FunctionAnnotationRParen,
            TT_JavaAnnotation, TT_LeadingJavaAnnotation))) ||
      (!Style.IndentWrappedFunctionNames &&
       NextNonComment->isOneOf(tok::kw_operator, TT_FunctionDeclarationName)))
    return std::max(State.Stack.back().LastSpace, State.Stack.back().Indent);
  if (NextNonComment->is(TT_SelectorName)) {
    if (!State.Stack.back().ObjCSelectorNameFound) {
      unsigned MinIndent = State.Stack.back().Indent;
      if (shouldIndentWrappedSelectorName(Style, State.Line->Type))
        MinIndent = std::max(MinIndent,
                             State.FirstIndent + Style.ContinuationIndentWidth);
      // If LongestObjCSelectorName is 0, we are indenting the first
      // part of an ObjC selector (or a selector component which is
      // not colon-aligned due to block formatting).
      //
      // Otherwise, we are indenting a subsequent part of an ObjC
      // selector which should be colon-aligned to the longest
      // component of the ObjC selector.
      //
      // In either case, we want to respect Style.IndentWrappedFunctionNames.
      return MinIndent +
             std::max(NextNonComment->LongestObjCSelectorName,
                      NextNonComment->ColumnWidth) -
             NextNonComment->ColumnWidth;
    }
    if (!State.Stack.back().AlignColons)
      return State.Stack.back().Indent;
    if (State.Stack.back().ColonPos > NextNonComment->ColumnWidth)
      return State.Stack.back().ColonPos - NextNonComment->ColumnWidth;
    return State.Stack.back().Indent;
  }
  if (NextNonComment->is(tok::colon) && NextNonComment->is(TT_ObjCMethodExpr))
    return State.Stack.back().ColonPos;
  if (NextNonComment->is(TT_ArraySubscriptLSquare)) {
    if (State.Stack.back().StartOfArraySubscripts != 0)
      return State.Stack.back().StartOfArraySubscripts;
    return ContinuationIndent;
  }

  // This ensure that we correctly format ObjC methods calls without inputs,
  // i.e. where the last element isn't selector like: [callee method];
  if (NextNonComment->is(tok::identifier) && NextNonComment->FakeRParens == 0 &&
      NextNonComment->Next && NextNonComment->Next->is(TT_ObjCMethodExpr))
    return State.Stack.back().Indent;

  if (NextNonComment->isOneOf(TT_StartOfName, TT_PointerOrReference) ||
      Previous.isOneOf(tok::coloncolon, tok::equal, TT_JsTypeColon))
    return ContinuationIndent;
  if (PreviousNonComment && PreviousNonComment->is(tok::colon) &&
      PreviousNonComment->isOneOf(TT_ObjCMethodExpr, TT_DictLiteral))
    return ContinuationIndent;
  if (NextNonComment->is(TT_CtorInitializerComma))
    return State.Stack.back().Indent;
  if (PreviousNonComment && PreviousNonComment->is(TT_CtorInitializerColon) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon)
    return State.Stack.back().Indent;
  if (PreviousNonComment && PreviousNonComment->is(TT_InheritanceColon) &&
      Style.BreakInheritanceList == FormatStyle::BILS_AfterColon)
    return State.Stack.back().Indent;
  if (NextNonComment->isOneOf(TT_CtorInitializerColon, TT_InheritanceColon,
                              TT_InheritanceComma))
    return State.FirstIndent + Style.ConstructorInitializerIndentWidth;
  if (Previous.is(tok::r_paren) && !Current.isBinaryOperator() &&
      !Current.isOneOf(tok::colon, tok::comment))
    return ContinuationIndent;
  if (Current.is(TT_ProtoExtensionLSquare))
    return State.Stack.back().Indent;
  if (State.Stack.back().Indent == State.FirstIndent && PreviousNonComment &&
      PreviousNonComment->isNot(tok::r_brace))
    // Ensure that we fall back to the continuation indent width instead of
    // just flushing continuations left.
    return State.Stack.back().Indent + Style.ContinuationIndentWidth;
  return State.Stack.back().Indent;
}

unsigned ContinuationIndenter::moveStateToNextToken(LineState &State,
                                                    bool DryRun, bool Newline) {
  assert(State.Stack.size());
  const FormatToken &Current = *State.NextToken;

  if (Current.isOneOf(tok::comma, TT_BinaryOperator))
    State.Stack.back().NoLineBreakInOperand = false;
  if (Current.is(TT_InheritanceColon))
    State.Stack.back().AvoidBinPacking = true;
  if (Current.is(tok::lessless) && Current.isNot(TT_OverloadedOperator)) {
    if (State.Stack.back().FirstLessLess == 0)
      State.Stack.back().FirstLessLess = State.Column;
    else
      State.Stack.back().LastOperatorWrapped = Newline;
  }
  if (Current.is(TT_BinaryOperator) && Current.isNot(tok::lessless))
    State.Stack.back().LastOperatorWrapped = Newline;
  if (Current.is(TT_ConditionalExpr) && Current.Previous &&
      !Current.Previous->is(TT_ConditionalExpr))
    State.Stack.back().LastOperatorWrapped = Newline;
  if (Current.is(TT_ArraySubscriptLSquare) &&
      State.Stack.back().StartOfArraySubscripts == 0)
    State.Stack.back().StartOfArraySubscripts = State.Column;
  if (Style.BreakBeforeTernaryOperators && Current.is(tok::question))
    State.Stack.back().QuestionColumn = State.Column;
  if (!Style.BreakBeforeTernaryOperators && Current.isNot(tok::colon)) {
    const FormatToken *Previous = Current.Previous;
    while (Previous && Previous->isTrailingComment())
      Previous = Previous->Previous;
    if (Previous && Previous->is(tok::question))
      State.Stack.back().QuestionColumn = State.Column;
  }
  if (!Current.opensScope() && !Current.closesScope() &&
      !Current.is(TT_PointerOrReference))
    State.LowestLevelOnLine =
        std::min(State.LowestLevelOnLine, Current.NestingLevel);
  if (Current.isMemberAccess())
    State.Stack.back().StartOfFunctionCall =
        !Current.NextOperator ? 0 : State.Column;
  if (Current.is(TT_SelectorName))
    State.Stack.back().ObjCSelectorNameFound = true;
  if (Current.is(TT_CtorInitializerColon) &&
      Style.BreakConstructorInitializers != FormatStyle::BCIS_AfterColon) {
    // Indent 2 from the column, so:
    // SomeClass::SomeClass()
    //     : First(...), ...
    //       Next(...)
    //       ^ line up here.
    State.Stack.back().Indent =
        State.Column +
        (Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma
             ? 0
             : 2);
    State.Stack.back().NestedBlockIndent = State.Stack.back().Indent;
    if (Style.ConstructorInitializerAllOnOneLineOrOnePerLine)
      State.Stack.back().AvoidBinPacking = true;
    State.Stack.back().BreakBeforeParameter = false;
  }
  if (Current.is(TT_CtorInitializerColon) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon) {
    State.Stack.back().Indent =
        State.FirstIndent + Style.ConstructorInitializerIndentWidth;
    State.Stack.back().NestedBlockIndent = State.Stack.back().Indent;
    if (Style.ConstructorInitializerAllOnOneLineOrOnePerLine)
      State.Stack.back().AvoidBinPacking = true;
  }
  if (Current.is(TT_InheritanceColon))
    State.Stack.back().Indent =
        State.FirstIndent + Style.ConstructorInitializerIndentWidth;
  if (Current.isOneOf(TT_BinaryOperator, TT_ConditionalExpr) && Newline)
    State.Stack.back().NestedBlockIndent =
        State.Column + Current.ColumnWidth + 1;
  if (Current.isOneOf(TT_LambdaLSquare, TT_LambdaArrow))
    State.Stack.back().LastSpace = State.Column;

  // Insert scopes created by fake parenthesis.
  const FormatToken *Previous = Current.getPreviousNonComment();

  // Add special behavior to support a format commonly used for JavaScript
  // closures:
  //   SomeFunction(function() {
  //     foo();
  //     bar();
  //   }, a, b, c);
  if (Current.isNot(tok::comment) && Previous &&
      Previous->isOneOf(tok::l_brace, TT_ArrayInitializerLSquare) &&
      !Previous->is(TT_DictLiteral) && State.Stack.size() > 1 &&
      !State.Stack.back().HasMultipleNestedBlocks) {
    if (State.Stack[State.Stack.size() - 2].NestedBlockInlined && Newline)
      for (unsigned i = 0, e = State.Stack.size() - 1; i != e; ++i)
        State.Stack[i].NoLineBreak = true;
    State.Stack[State.Stack.size() - 2].NestedBlockInlined = false;
  }
  if (Previous &&
      (Previous->isOneOf(tok::l_paren, tok::comma, tok::colon) ||
       Previous->isOneOf(TT_BinaryOperator, TT_ConditionalExpr)) &&
      !Previous->isOneOf(TT_DictLiteral, TT_ObjCMethodExpr)) {
    State.Stack.back().NestedBlockInlined =
        !Newline &&
        (Previous->isNot(tok::l_paren) || Previous->ParameterCount > 1);
  }

  moveStatePastFakeLParens(State, Newline);
  moveStatePastScopeCloser(State);
  bool AllowBreak = !State.Stack.back().NoLineBreak &&
                    !State.Stack.back().NoLineBreakInOperand;
  moveStatePastScopeOpener(State, Newline);
  moveStatePastFakeRParens(State);

  if (Current.is(TT_ObjCStringLiteral) && State.StartOfStringLiteral == 0)
    State.StartOfStringLiteral = State.Column + 1;
  else if (Current.isStringLiteral() && State.StartOfStringLiteral == 0)
    State.StartOfStringLiteral = State.Column;
  else if (!Current.isOneOf(tok::comment, tok::identifier, tok::hash) &&
           !Current.isStringLiteral())
    State.StartOfStringLiteral = 0;

  State.Column += Current.ColumnWidth;
  State.NextToken = State.NextToken->Next;

  unsigned Penalty =
      handleEndOfLine(Current, State, DryRun, AllowBreak);

  if (Current.Role)
    Current.Role->formatFromToken(State, this, DryRun);
  // If the previous has a special role, let it consume tokens as appropriate.
  // It is necessary to start at the previous token for the only implemented
  // role (comma separated list). That way, the decision whether or not to break
  // after the "{" is already done and both options are tried and evaluated.
  // FIXME: This is ugly, find a better way.
  if (Previous && Previous->Role)
    Penalty += Previous->Role->formatAfterToken(State, this, DryRun);

  return Penalty;
}

void ContinuationIndenter::moveStatePastFakeLParens(LineState &State,
                                                    bool Newline) {
  const FormatToken &Current = *State.NextToken;
  const FormatToken *Previous = Current.getPreviousNonComment();

  // Don't add extra indentation for the first fake parenthesis after
  // 'return', assignments or opening <({[. The indentation for these cases
  // is special cased.
  bool SkipFirstExtraIndent =
      (Previous && (Previous->opensScope() ||
                    Previous->isOneOf(tok::semi, tok::kw_return) ||
                    (Previous->getPrecedence() == prec::Assignment &&
                     Style.AlignOperands) ||
                    Previous->is(TT_ObjCMethodExpr)));
  for (SmallVectorImpl<prec::Level>::const_reverse_iterator
           I = Current.FakeLParens.rbegin(),
           E = Current.FakeLParens.rend();
       I != E; ++I) {
    ParenState NewParenState = State.Stack.back();
    NewParenState.Tok = nullptr;
    NewParenState.ContainsLineBreak = false;
    NewParenState.LastOperatorWrapped = true;
    NewParenState.NoLineBreak =
        NewParenState.NoLineBreak || State.Stack.back().NoLineBreakInOperand;

    // Don't propagate AvoidBinPacking into subexpressions of arg/param lists.
    if (*I > prec::Comma)
      NewParenState.AvoidBinPacking = false;

    // Indent from 'LastSpace' unless these are fake parentheses encapsulating
    // a builder type call after 'return' or, if the alignment after opening
    // brackets is disabled.
    if (!Current.isTrailingComment() &&
        (Style.AlignOperands || *I < prec::Assignment) &&
        (!Previous || Previous->isNot(tok::kw_return) ||
         (Style.Language != FormatStyle::LK_Java && *I > 0)) &&
        (Style.AlignAfterOpenBracket != FormatStyle::BAS_DontAlign ||
         *I != prec::Comma || Current.NestingLevel == 0))
      NewParenState.Indent =
          std::max(std::max(State.Column, NewParenState.Indent),
                   State.Stack.back().LastSpace);

    // Do not indent relative to the fake parentheses inserted for "." or "->".
    // This is a special case to make the following to statements consistent:
    //   OuterFunction(InnerFunctionCall( // break
    //       ParameterToInnerFunction));
    //   OuterFunction(SomeObject.InnerFunctionCall( // break
    //       ParameterToInnerFunction));
    if (*I > prec::Unknown)
      NewParenState.LastSpace = std::max(NewParenState.LastSpace, State.Column);
    if (*I != prec::Conditional && !Current.is(TT_UnaryOperator) &&
        Style.AlignAfterOpenBracket != FormatStyle::BAS_DontAlign)
      NewParenState.StartOfFunctionCall = State.Column;

    // Always indent conditional expressions. Never indent expression where
    // the 'operator' is ',', ';' or an assignment (i.e. *I <=
    // prec::Assignment) as those have different indentation rules. Indent
    // other expression, unless the indentation needs to be skipped.
    if (*I == prec::Conditional ||
        (!SkipFirstExtraIndent && *I > prec::Assignment &&
         !Current.isTrailingComment()))
      NewParenState.Indent += Style.ContinuationIndentWidth;
    if ((Previous && !Previous->opensScope()) || *I != prec::Comma)
      NewParenState.BreakBeforeParameter = false;
    State.Stack.push_back(NewParenState);
    SkipFirstExtraIndent = false;
  }
}

void ContinuationIndenter::moveStatePastFakeRParens(LineState &State) {
  for (unsigned i = 0, e = State.NextToken->FakeRParens; i != e; ++i) {
    unsigned VariablePos = State.Stack.back().VariablePos;
    if (State.Stack.size() == 1) {
      // Do not pop the last element.
      break;
    }
    State.Stack.pop_back();
    State.Stack.back().VariablePos = VariablePos;
  }
}

void ContinuationIndenter::moveStatePastScopeOpener(LineState &State,
                                                    bool Newline) {
  const FormatToken &Current = *State.NextToken;
  if (!Current.opensScope())
    return;

  if (Current.MatchingParen && Current.BlockKind == BK_Block) {
    moveStateToNewBlock(State);
    return;
  }

  unsigned NewIndent;
  unsigned LastSpace = State.Stack.back().LastSpace;
  bool AvoidBinPacking;
  bool BreakBeforeParameter = false;
  unsigned NestedBlockIndent = std::max(State.Stack.back().StartOfFunctionCall,
                                        State.Stack.back().NestedBlockIndent);
  if (Current.isOneOf(tok::l_brace, TT_ArrayInitializerLSquare) ||
      opensProtoMessageField(Current, Style)) {
    if (Current.opensBlockOrBlockTypeList(Style)) {
      NewIndent = Style.IndentWidth +
                  std::min(State.Column, State.Stack.back().NestedBlockIndent);
    } else {
      NewIndent = State.Stack.back().LastSpace + Style.ContinuationIndentWidth;
    }
    const FormatToken *NextNoComment = Current.getNextNonComment();
    bool EndsInComma = Current.MatchingParen &&
                       Current.MatchingParen->Previous &&
                       Current.MatchingParen->Previous->is(tok::comma);
    AvoidBinPacking = EndsInComma || Current.is(TT_DictLiteral) ||
                      Style.Language == FormatStyle::LK_Proto ||
                      Style.Language == FormatStyle::LK_TextProto ||
                      !Style.BinPackArguments ||
                      (NextNoComment &&
                       NextNoComment->isOneOf(TT_DesignatedInitializerPeriod,
                                              TT_DesignatedInitializerLSquare));
    BreakBeforeParameter = EndsInComma;
    if (Current.ParameterCount > 1)
      NestedBlockIndent = std::max(NestedBlockIndent, State.Column + 1);
  } else {
    NewIndent = Style.ContinuationIndentWidth +
                std::max(State.Stack.back().LastSpace,
                         State.Stack.back().StartOfFunctionCall);

    // Ensure that different different brackets force relative alignment, e.g.:
    // void SomeFunction(vector<  // break
    //                       int> v);
    // FIXME: We likely want to do this for more combinations of brackets.
    if (Current.is(tok::less) && Current.ParentBracket == tok::l_paren) {
      NewIndent = std::max(NewIndent, State.Stack.back().Indent);
      LastSpace = std::max(LastSpace, State.Stack.back().Indent);
    }

    bool EndsInComma =
        Current.MatchingParen &&
        Current.MatchingParen->getPreviousNonComment() &&
        Current.MatchingParen->getPreviousNonComment()->is(tok::comma);

    // If ObjCBinPackProtocolList is unspecified, fall back to BinPackParameters
    // for backwards compatibility.
    bool ObjCBinPackProtocolList =
        (Style.ObjCBinPackProtocolList == FormatStyle::BPS_Auto &&
         Style.BinPackParameters) ||
        Style.ObjCBinPackProtocolList == FormatStyle::BPS_Always;

    bool BinPackDeclaration =
        (State.Line->Type != LT_ObjCDecl && Style.BinPackParameters) ||
        (State.Line->Type == LT_ObjCDecl && ObjCBinPackProtocolList);

    AvoidBinPacking =
        (Style.Language == FormatStyle::LK_JavaScript && EndsInComma) ||
        (State.Line->MustBeDeclaration && !BinPackDeclaration) ||
        (!State.Line->MustBeDeclaration && !Style.BinPackArguments) ||
        (Style.ExperimentalAutoDetectBinPacking &&
         (Current.PackingKind == PPK_OnePerLine ||
          (!BinPackInconclusiveFunctions &&
           Current.PackingKind == PPK_Inconclusive)));

    if (Current.is(TT_ObjCMethodExpr) && Current.MatchingParen) {
      if (Style.ColumnLimit) {
        // If this '[' opens an ObjC call, determine whether all parameters fit
        // into one line and put one per line if they don't.
        if (getLengthToMatchingParen(Current, State.Stack) + State.Column >
            getColumnLimit(State))
          BreakBeforeParameter = true;
      } else {
        // For ColumnLimit = 0, we have to figure out whether there is or has to
        // be a line break within this call.
        for (const FormatToken *Tok = &Current;
             Tok && Tok != Current.MatchingParen; Tok = Tok->Next) {
          if (Tok->MustBreakBefore ||
              (Tok->CanBreakBefore && Tok->NewlinesBefore > 0)) {
            BreakBeforeParameter = true;
            break;
          }
        }
      }
    }

    if (Style.Language == FormatStyle::LK_JavaScript && EndsInComma)
      BreakBeforeParameter = true;
  }
  // Generally inherit NoLineBreak from the current scope to nested scope.
  // However, don't do this for non-empty nested blocks, dict literals and
  // array literals as these follow different indentation rules.
  bool NoLineBreak =
      Current.Children.empty() &&
      !Current.isOneOf(TT_DictLiteral, TT_ArrayInitializerLSquare) &&
      (State.Stack.back().NoLineBreak ||
       State.Stack.back().NoLineBreakInOperand ||
       (Current.is(TT_TemplateOpener) &&
        State.Stack.back().ContainsUnwrappedBuilder));
  State.Stack.push_back(
      ParenState(&Current, NewIndent, LastSpace, AvoidBinPacking, NoLineBreak));
  State.Stack.back().NestedBlockIndent = NestedBlockIndent;
  State.Stack.back().BreakBeforeParameter = BreakBeforeParameter;
  State.Stack.back().HasMultipleNestedBlocks = Current.BlockParameterCount > 1;
  State.Stack.back().IsInsideObjCArrayLiteral =
      Current.is(TT_ArrayInitializerLSquare) && Current.Previous &&
      Current.Previous->is(tok::at);
}

void ContinuationIndenter::moveStatePastScopeCloser(LineState &State) {
  const FormatToken &Current = *State.NextToken;
  if (!Current.closesScope())
    return;

  // If we encounter a closing ), ], } or >, we can remove a level from our
  // stacks.
  if (State.Stack.size() > 1 &&
      (Current.isOneOf(tok::r_paren, tok::r_square, TT_TemplateString) ||
       (Current.is(tok::r_brace) && State.NextToken != State.Line->First) ||
       State.NextToken->is(TT_TemplateCloser) ||
       (Current.is(tok::greater) && Current.is(TT_DictLiteral))))
    State.Stack.pop_back();

  // Reevaluate whether ObjC message arguments fit into one line.
  // If a receiver spans multiple lines, e.g.:
  //   [[object block:^{
  //     return 42;
  //   }] a:42 b:42];
  // BreakBeforeParameter is calculated based on an incorrect assumption
  // (it is checked whether the whole expression fits into one line without
  // considering a line break inside a message receiver).
  // We check whether arguements fit after receiver scope closer (into the same
  // line).
  if (State.Stack.back().BreakBeforeParameter && Current.MatchingParen &&
      Current.MatchingParen->Previous) {
    const FormatToken &CurrentScopeOpener = *Current.MatchingParen->Previous;
    if (CurrentScopeOpener.is(TT_ObjCMethodExpr) &&
        CurrentScopeOpener.MatchingParen) {
      int NecessarySpaceInLine =
          getLengthToMatchingParen(CurrentScopeOpener, State.Stack) +
          CurrentScopeOpener.TotalLength - Current.TotalLength - 1;
      if (State.Column + Current.ColumnWidth + NecessarySpaceInLine <=
          Style.ColumnLimit)
        State.Stack.back().BreakBeforeParameter = false;
    }
  }

  if (Current.is(tok::r_square)) {
    // If this ends the array subscript expr, reset the corresponding value.
    const FormatToken *NextNonComment = Current.getNextNonComment();
    if (NextNonComment && NextNonComment->isNot(tok::l_square))
      State.Stack.back().StartOfArraySubscripts = 0;
  }
}

void ContinuationIndenter::moveStateToNewBlock(LineState &State) {
  unsigned NestedBlockIndent = State.Stack.back().NestedBlockIndent;
  // ObjC block sometimes follow special indentation rules.
  unsigned NewIndent =
      NestedBlockIndent + (State.NextToken->is(TT_ObjCBlockLBrace)
                               ? Style.ObjCBlockIndentWidth
                               : Style.IndentWidth);
  State.Stack.push_back(ParenState(State.NextToken, NewIndent,
                                   State.Stack.back().LastSpace,
                                   /*AvoidBinPacking=*/true,
                                   /*NoLineBreak=*/false));
  State.Stack.back().NestedBlockIndent = NestedBlockIndent;
  State.Stack.back().BreakBeforeParameter = true;
}

static unsigned getLastLineEndColumn(StringRef Text, unsigned StartColumn,
                                     unsigned TabWidth,
                                     encoding::Encoding Encoding) {
  size_t LastNewlinePos = Text.find_last_of("\n");
  if (LastNewlinePos == StringRef::npos) {
    return StartColumn +
           encoding::columnWidthWithTabs(Text, StartColumn, TabWidth, Encoding);
  } else {
    return encoding::columnWidthWithTabs(Text.substr(LastNewlinePos),
                                         /*StartColumn=*/0, TabWidth, Encoding);
  }
}

unsigned ContinuationIndenter::reformatRawStringLiteral(
    const FormatToken &Current, LineState &State,
    const FormatStyle &RawStringStyle, bool DryRun) {
  unsigned StartColumn = State.Column - Current.ColumnWidth;
  StringRef OldDelimiter = *getRawStringDelimiter(Current.TokenText);
  StringRef NewDelimiter =
      getCanonicalRawStringDelimiter(Style, RawStringStyle.Language);
  if (NewDelimiter.empty() || OldDelimiter.empty())
    NewDelimiter = OldDelimiter;
  // The text of a raw string is between the leading 'R"delimiter(' and the
  // trailing 'delimiter)"'.
  unsigned OldPrefixSize = 3 + OldDelimiter.size();
  unsigned OldSuffixSize = 2 + OldDelimiter.size();
  // We create a virtual text environment which expects a null-terminated
  // string, so we cannot use StringRef.
  std::string RawText =
      Current.TokenText.substr(OldPrefixSize).drop_back(OldSuffixSize);
  if (NewDelimiter != OldDelimiter) {
    // Don't update to the canonical delimiter 'deli' if ')deli"' occurs in the
    // raw string.
    std::string CanonicalDelimiterSuffix = (")" + NewDelimiter + "\"").str();
    if (StringRef(RawText).contains(CanonicalDelimiterSuffix))
      NewDelimiter = OldDelimiter;
  }

  unsigned NewPrefixSize = 3 + NewDelimiter.size();
  unsigned NewSuffixSize = 2 + NewDelimiter.size();

  // The first start column is the column the raw text starts after formatting.
  unsigned FirstStartColumn = StartColumn + NewPrefixSize;

  // The next start column is the intended indentation a line break inside
  // the raw string at level 0. It is determined by the following rules:
  //   - if the content starts on newline, it is one level more than the current
  //     indent, and
  //   - if the content does not start on a newline, it is the first start
  //     column.
  // These rules have the advantage that the formatted content both does not
  // violate the rectangle rule and visually flows within the surrounding
  // source.
  bool ContentStartsOnNewline = Current.TokenText[OldPrefixSize] == '\n';
  // If this token is the last parameter (checked by looking if it's followed by
  // `)`, the base the indent off the line's nested block indent. Otherwise,
  // base the indent off the arguments indent, so we can achieve:
  // fffffffffff(1, 2, 3, R"pb(
  //     key1: 1  #
  //     key2: 2)pb");
  //
  // fffffffffff(1, 2, 3,
  //             R"pb(
  //               key1: 1  #
  //               key2: 2
  //             )pb",
  //             5);
  unsigned CurrentIndent = (Current.Next && Current.Next->is(tok::r_paren))
                               ? State.Stack.back().NestedBlockIndent
                               : State.Stack.back().Indent;
  unsigned NextStartColumn = ContentStartsOnNewline
                                 ? CurrentIndent + Style.IndentWidth
                                 : FirstStartColumn;

  // The last start column is the column the raw string suffix starts if it is
  // put on a newline.
  // The last start column is the intended indentation of the raw string postfix
  // if it is put on a newline. It is determined by the following rules:
  //   - if the raw string prefix starts on a newline, it is the column where
  //     that raw string prefix starts, and
  //   - if the raw string prefix does not start on a newline, it is the current
  //     indent.
  unsigned LastStartColumn = Current.NewlinesBefore
                                 ? FirstStartColumn - NewPrefixSize
                                 : CurrentIndent;

  std::pair<tooling::Replacements, unsigned> Fixes = internal::reformat(
      RawStringStyle, RawText, {tooling::Range(0, RawText.size())},
      FirstStartColumn, NextStartColumn, LastStartColumn, "<stdin>",
      /*Status=*/nullptr);

  auto NewCode = applyAllReplacements(RawText, Fixes.first);
  tooling::Replacements NoFixes;
  if (!NewCode) {
    return addMultilineToken(Current, State);
  }
  if (!DryRun) {
    if (NewDelimiter != OldDelimiter) {
      // In 'R"delimiter(...', the delimiter starts 2 characters after the start
      // of the token.
      SourceLocation PrefixDelimiterStart =
          Current.Tok.getLocation().getLocWithOffset(2);
      auto PrefixErr = Whitespaces.addReplacement(tooling::Replacement(
          SourceMgr, PrefixDelimiterStart, OldDelimiter.size(), NewDelimiter));
      if (PrefixErr) {
        llvm::errs()
            << "Failed to update the prefix delimiter of a raw string: "
            << llvm::toString(std::move(PrefixErr)) << "\n";
      }
      // In 'R"delimiter(...)delimiter"', the suffix delimiter starts at
      // position length - 1 - |delimiter|.
      SourceLocation SuffixDelimiterStart =
          Current.Tok.getLocation().getLocWithOffset(Current.TokenText.size() -
                                                     1 - OldDelimiter.size());
      auto SuffixErr = Whitespaces.addReplacement(tooling::Replacement(
          SourceMgr, SuffixDelimiterStart, OldDelimiter.size(), NewDelimiter));
      if (SuffixErr) {
        llvm::errs()
            << "Failed to update the suffix delimiter of a raw string: "
            << llvm::toString(std::move(SuffixErr)) << "\n";
      }
    }
    SourceLocation OriginLoc =
        Current.Tok.getLocation().getLocWithOffset(OldPrefixSize);
    for (const tooling::Replacement &Fix : Fixes.first) {
      auto Err = Whitespaces.addReplacement(tooling::Replacement(
          SourceMgr, OriginLoc.getLocWithOffset(Fix.getOffset()),
          Fix.getLength(), Fix.getReplacementText()));
      if (Err) {
        llvm::errs() << "Failed to reformat raw string: "
                     << llvm::toString(std::move(Err)) << "\n";
      }
    }
  }
  unsigned RawLastLineEndColumn = getLastLineEndColumn(
      *NewCode, FirstStartColumn, Style.TabWidth, Encoding);
  State.Column = RawLastLineEndColumn + NewSuffixSize;
  // Since we're updating the column to after the raw string literal here, we
  // have to manually add the penalty for the prefix R"delim( over the column
  // limit.
  unsigned PrefixExcessCharacters =
      StartColumn + NewPrefixSize > Style.ColumnLimit ?
      StartColumn + NewPrefixSize - Style.ColumnLimit : 0;
  bool IsMultiline =
      ContentStartsOnNewline || (NewCode->find('\n') != std::string::npos);
  if (IsMultiline) {
    // Break before further function parameters on all levels.
    for (unsigned i = 0, e = State.Stack.size(); i != e; ++i)
      State.Stack[i].BreakBeforeParameter = true;
  }
  return Fixes.second + PrefixExcessCharacters * Style.PenaltyExcessCharacter;
}

unsigned ContinuationIndenter::addMultilineToken(const FormatToken &Current,
                                                 LineState &State) {
  // Break before further function parameters on all levels.
  for (unsigned i = 0, e = State.Stack.size(); i != e; ++i)
    State.Stack[i].BreakBeforeParameter = true;

  unsigned ColumnsUsed = State.Column;
  // We can only affect layout of the first and the last line, so the penalty
  // for all other lines is constant, and we ignore it.
  State.Column = Current.LastLineColumnWidth;

  if (ColumnsUsed > getColumnLimit(State))
    return Style.PenaltyExcessCharacter * (ColumnsUsed - getColumnLimit(State));
  return 0;
}

unsigned ContinuationIndenter::handleEndOfLine(const FormatToken &Current,
                                               LineState &State, bool DryRun,
                                               bool AllowBreak) {
  unsigned Penalty = 0;
  // Compute the raw string style to use in case this is a raw string literal
  // that can be reformatted.
  auto RawStringStyle = getRawStringStyle(Current, State);
  if (RawStringStyle && !Current.Finalized) {
    Penalty = reformatRawStringLiteral(Current, State, *RawStringStyle, DryRun);
  } else if (Current.IsMultiline && Current.isNot(TT_BlockComment)) {
    // Don't break multi-line tokens other than block comments and raw string
    // literals. Instead, just update the state.
    Penalty = addMultilineToken(Current, State);
  } else if (State.Line->Type != LT_ImportStatement) {
    // We generally don't break import statements.
    LineState OriginalState = State;

    // Whether we force the reflowing algorithm to stay strictly within the
    // column limit.
    bool Strict = false;
    // Whether the first non-strict attempt at reflowing did intentionally
    // exceed the column limit.
    bool Exceeded = false;
    std::tie(Penalty, Exceeded) = breakProtrudingToken(
        Current, State, AllowBreak, /*DryRun=*/true, Strict);
    if (Exceeded) {
      // If non-strict reflowing exceeds the column limit, try whether strict
      // reflowing leads to an overall lower penalty.
      LineState StrictState = OriginalState;
      unsigned StrictPenalty =
          breakProtrudingToken(Current, StrictState, AllowBreak,
                               /*DryRun=*/true, /*Strict=*/true)
              .first;
      Strict = StrictPenalty <= Penalty;
      if (Strict) {
        Penalty = StrictPenalty;
        State = StrictState;
      }
    }
    if (!DryRun) {
      // If we're not in dry-run mode, apply the changes with the decision on
      // strictness made above.
      breakProtrudingToken(Current, OriginalState, AllowBreak, /*DryRun=*/false,
                           Strict);
    }
  }
  if (State.Column > getColumnLimit(State)) {
    unsigned ExcessCharacters = State.Column - getColumnLimit(State);
    Penalty += Style.PenaltyExcessCharacter * ExcessCharacters;
  }
  return Penalty;
}

// Returns the enclosing function name of a token, or the empty string if not
// found.
static StringRef getEnclosingFunctionName(const FormatToken &Current) {
  // Look for: 'function(' or 'function<templates>(' before Current.
  auto Tok = Current.getPreviousNonComment();
  if (!Tok || !Tok->is(tok::l_paren))
    return "";
  Tok = Tok->getPreviousNonComment();
  if (!Tok)
    return "";
  if (Tok->is(TT_TemplateCloser)) {
    Tok = Tok->MatchingParen;
    if (Tok)
      Tok = Tok->getPreviousNonComment();
  }
  if (!Tok || !Tok->is(tok::identifier))
    return "";
  return Tok->TokenText;
}

llvm::Optional<FormatStyle>
ContinuationIndenter::getRawStringStyle(const FormatToken &Current,
                                        const LineState &State) {
  if (!Current.isStringLiteral())
    return None;
  auto Delimiter = getRawStringDelimiter(Current.TokenText);
  if (!Delimiter)
    return None;
  auto RawStringStyle = RawStringFormats.getDelimiterStyle(*Delimiter);
  if (!RawStringStyle && Delimiter->empty())
    RawStringStyle = RawStringFormats.getEnclosingFunctionStyle(
        getEnclosingFunctionName(Current));
  if (!RawStringStyle)
    return None;
  RawStringStyle->ColumnLimit = getColumnLimit(State);
  return RawStringStyle;
}

std::unique_ptr<BreakableToken> ContinuationIndenter::createBreakableToken(
    const FormatToken &Current, LineState &State, bool AllowBreak) {
  unsigned StartColumn = State.Column - Current.ColumnWidth;
  if (Current.isStringLiteral()) {
    // FIXME: String literal breaking is currently disabled for Java and JS, as
    // it requires strings to be merged using "+" which we don't support.
    if (Style.Language == FormatStyle::LK_Java ||
        Style.Language == FormatStyle::LK_JavaScript ||
        !Style.BreakStringLiterals ||
        !AllowBreak)
      return nullptr;

    // Don't break string literals inside preprocessor directives (except for
    // #define directives, as their contents are stored in separate lines and
    // are not affected by this check).
    // This way we avoid breaking code with line directives and unknown
    // preprocessor directives that contain long string literals.
    if (State.Line->Type == LT_PreprocessorDirective)
      return nullptr;
    // Exempts unterminated string literals from line breaking. The user will
    // likely want to terminate the string before any line breaking is done.
    if (Current.IsUnterminatedLiteral)
      return nullptr;
    // Don't break string literals inside Objective-C array literals (doing so
    // raises the warning -Wobjc-string-concatenation).
    if (State.Stack.back().IsInsideObjCArrayLiteral) {
      return nullptr;
    }

    StringRef Text = Current.TokenText;
    StringRef Prefix;
    StringRef Postfix;
    // FIXME: Handle whitespace between '_T', '(', '"..."', and ')'.
    // FIXME: Store Prefix and Suffix (or PrefixLength and SuffixLength to
    // reduce the overhead) for each FormatToken, which is a string, so that we
    // don't run multiple checks here on the hot path.
    if ((Text.endswith(Postfix = "\"") &&
         (Text.startswith(Prefix = "@\"") || Text.startswith(Prefix = "\"") ||
          Text.startswith(Prefix = "u\"") || Text.startswith(Prefix = "U\"") ||
          Text.startswith(Prefix = "u8\"") ||
          Text.startswith(Prefix = "L\""))) ||
        (Text.startswith(Prefix = "_T(\"") && Text.endswith(Postfix = "\")"))) {
      // We need this to address the case where there is an unbreakable tail
      // only if certain other formatting decisions have been taken. The
      // UnbreakableTailLength of Current is an overapproximation is that case
      // and we need to be correct here.
      unsigned UnbreakableTailLength = (State.NextToken && canBreak(State))
                                           ? 0
                                           : Current.UnbreakableTailLength;
      return llvm::make_unique<BreakableStringLiteral>(
          Current, StartColumn, Prefix, Postfix, UnbreakableTailLength,
          State.Line->InPPDirective, Encoding, Style);
    }
  } else if (Current.is(TT_BlockComment)) {
    if (!Style.ReflowComments ||
        // If a comment token switches formatting, like
        // /* clang-format on */, we don't want to break it further,
        // but we may still want to adjust its indentation.
        switchesFormatting(Current)) {
      return nullptr;
    }
    return llvm::make_unique<BreakableBlockComment>(
        Current, StartColumn, Current.OriginalColumn, !Current.Previous,
        State.Line->InPPDirective, Encoding, Style);
  } else if (Current.is(TT_LineComment) &&
             (Current.Previous == nullptr ||
              Current.Previous->isNot(TT_ImplicitStringLiteral))) {
    if (!Style.ReflowComments ||
        CommentPragmasRegex.match(Current.TokenText.substr(2)) ||
        switchesFormatting(Current))
      return nullptr;
    return llvm::make_unique<BreakableLineCommentSection>(
        Current, StartColumn, Current.OriginalColumn, !Current.Previous,
        /*InPPDirective=*/false, Encoding, Style);
  }
  return nullptr;
}

std::pair<unsigned, bool>
ContinuationIndenter::breakProtrudingToken(const FormatToken &Current,
                                           LineState &State, bool AllowBreak,
                                           bool DryRun, bool Strict) {
  std::unique_ptr<const BreakableToken> Token =
      createBreakableToken(Current, State, AllowBreak);
  if (!Token)
    return {0, false};
  assert(Token->getLineCount() > 0);
  unsigned ColumnLimit = getColumnLimit(State);
  if (Current.is(TT_LineComment)) {
    // We don't insert backslashes when breaking line comments.
    ColumnLimit = Style.ColumnLimit;
  }
  if (Current.UnbreakableTailLength >= ColumnLimit)
    return {0, false};
  // ColumnWidth was already accounted into State.Column before calling
  // breakProtrudingToken.
  unsigned StartColumn = State.Column - Current.ColumnWidth;
  unsigned NewBreakPenalty = Current.isStringLiteral()
                                 ? Style.PenaltyBreakString
                                 : Style.PenaltyBreakComment;
  // Stores whether we intentionally decide to let a line exceed the column
  // limit.
  bool Exceeded = false;
  // Stores whether we introduce a break anywhere in the token.
  bool BreakInserted = Token->introducesBreakBeforeToken();
  // Store whether we inserted a new line break at the end of the previous
  // logical line.
  bool NewBreakBefore = false;
  // We use a conservative reflowing strategy. Reflow starts after a line is
  // broken or the corresponding whitespace compressed. Reflow ends as soon as a
  // line that doesn't get reflown with the previous line is reached.
  bool Reflow = false;
  // Keep track of where we are in the token:
  // Where we are in the content of the current logical line.
  unsigned TailOffset = 0;
  // The column number we're currently at.
  unsigned ContentStartColumn =
      Token->getContentStartColumn(0, /*Break=*/false);
  // The number of columns left in the current logical line after TailOffset.
  unsigned RemainingTokenColumns =
      Token->getRemainingLength(0, TailOffset, ContentStartColumn);
  // Adapt the start of the token, for example indent.
  if (!DryRun)
    Token->adaptStartOfLine(0, Whitespaces);

  unsigned ContentIndent = 0;
  unsigned Penalty = 0;
  LLVM_DEBUG(llvm::dbgs() << "Breaking protruding token at column "
                          << StartColumn << ".\n");
  for (unsigned LineIndex = 0, EndIndex = Token->getLineCount();
       LineIndex != EndIndex; ++LineIndex) {
    LLVM_DEBUG(llvm::dbgs()
               << "  Line: " << LineIndex << " (Reflow: " << Reflow << ")\n");
    NewBreakBefore = false;
    // If we did reflow the previous line, we'll try reflowing again. Otherwise
    // we'll start reflowing if the current line is broken or whitespace is
    // compressed.
    bool TryReflow = Reflow;
    // Break the current token until we can fit the rest of the line.
    while (ContentStartColumn + RemainingTokenColumns > ColumnLimit) {
      LLVM_DEBUG(llvm::dbgs() << "    Over limit, need: "
                              << (ContentStartColumn + RemainingTokenColumns)
                              << ", space: " << ColumnLimit
                              << ", reflown prefix: " << ContentStartColumn
                              << ", offset in line: " << TailOffset << "\n");
      // If the current token doesn't fit, find the latest possible split in the
      // current line so that breaking at it will be under the column limit.
      // FIXME: Use the earliest possible split while reflowing to correctly
      // compress whitespace within a line.
      BreakableToken::Split Split =
          Token->getSplit(LineIndex, TailOffset, ColumnLimit,
                          ContentStartColumn, CommentPragmasRegex);
      if (Split.first == StringRef::npos) {
        // No break opportunity - update the penalty and continue with the next
        // logical line.
        if (LineIndex < EndIndex - 1)
          // The last line's penalty is handled in addNextStateToQueue() or when
          // calling replaceWhitespaceAfterLastLine below.
          Penalty += Style.PenaltyExcessCharacter *
                     (ContentStartColumn + RemainingTokenColumns - ColumnLimit);
        LLVM_DEBUG(llvm::dbgs() << "    No break opportunity.\n");
        break;
      }
      assert(Split.first != 0);

      if (Token->supportsReflow()) {
        // Check whether the next natural split point after the current one can
        // still fit the line, either because we can compress away whitespace,
        // or because the penalty the excess characters introduce is lower than
        // the break penalty.
        // We only do this for tokens that support reflowing, and thus allow us
        // to change the whitespace arbitrarily (e.g. comments).
        // Other tokens, like string literals, can be broken on arbitrary
        // positions.

        // First, compute the columns from TailOffset to the next possible split
        // position.
        // For example:
        // ColumnLimit:     |
        // // Some text   that    breaks
        //    ^ tail offset
        //             ^-- split
        //    ^-------- to split columns
        //                    ^--- next split
        //    ^--------------- to next split columns
        unsigned ToSplitColumns = Token->getRangeLength(
            LineIndex, TailOffset, Split.first, ContentStartColumn);
        LLVM_DEBUG(llvm::dbgs() << "    ToSplit: " << ToSplitColumns << "\n");

        BreakableToken::Split NextSplit = Token->getSplit(
            LineIndex, TailOffset + Split.first + Split.second, ColumnLimit,
            ContentStartColumn + ToSplitColumns + 1, CommentPragmasRegex);
        // Compute the columns necessary to fit the next non-breakable sequence
        // into the current line.
        unsigned ToNextSplitColumns = 0;
        if (NextSplit.first == StringRef::npos) {
          ToNextSplitColumns = Token->getRemainingLength(LineIndex, TailOffset,
                                                         ContentStartColumn);
        } else {
          ToNextSplitColumns = Token->getRangeLength(
              LineIndex, TailOffset,
              Split.first + Split.second + NextSplit.first, ContentStartColumn);
        }
        // Compress the whitespace between the break and the start of the next
        // unbreakable sequence.
        ToNextSplitColumns =
            Token->getLengthAfterCompression(ToNextSplitColumns, Split);
        LLVM_DEBUG(llvm::dbgs()
                   << "    ContentStartColumn: " << ContentStartColumn << "\n");
        LLVM_DEBUG(llvm::dbgs()
                   << "    ToNextSplit: " << ToNextSplitColumns << "\n");
        // If the whitespace compression makes us fit, continue on the current
        // line.
        bool ContinueOnLine =
            ContentStartColumn + ToNextSplitColumns <= ColumnLimit;
        unsigned ExcessCharactersPenalty = 0;
        if (!ContinueOnLine && !Strict) {
          // Similarly, if the excess characters' penalty is lower than the
          // penalty of introducing a new break, continue on the current line.
          ExcessCharactersPenalty =
              (ContentStartColumn + ToNextSplitColumns - ColumnLimit) *
              Style.PenaltyExcessCharacter;
          LLVM_DEBUG(llvm::dbgs()
                     << "    Penalty excess: " << ExcessCharactersPenalty
                     << "\n            break : " << NewBreakPenalty << "\n");
          if (ExcessCharactersPenalty < NewBreakPenalty) {
            Exceeded = true;
            ContinueOnLine = true;
          }
        }
        if (ContinueOnLine) {
          LLVM_DEBUG(llvm::dbgs() << "    Continuing on line...\n");
          // The current line fits after compressing the whitespace - reflow
          // the next line into it if possible.
          TryReflow = true;
          if (!DryRun)
            Token->compressWhitespace(LineIndex, TailOffset, Split,
                                      Whitespaces);
          // When we continue on the same line, leave one space between content.
          ContentStartColumn += ToSplitColumns + 1;
          Penalty += ExcessCharactersPenalty;
          TailOffset += Split.first + Split.second;
          RemainingTokenColumns = Token->getRemainingLength(
              LineIndex, TailOffset, ContentStartColumn);
          continue;
        }
      }
      LLVM_DEBUG(llvm::dbgs() << "    Breaking...\n");
      // Update the ContentIndent only if the current line was not reflown with
      // the previous line, since in that case the previous line should still
      // determine the ContentIndent. Also never intent the last line.
      if (!Reflow)
        ContentIndent = Token->getContentIndent(LineIndex);
      LLVM_DEBUG(llvm::dbgs()
                 << "    ContentIndent: " << ContentIndent << "\n");
      ContentStartColumn = ContentIndent + Token->getContentStartColumn(
                                               LineIndex, /*Break=*/true);

      unsigned NewRemainingTokenColumns = Token->getRemainingLength(
          LineIndex, TailOffset + Split.first + Split.second,
          ContentStartColumn);
      if (NewRemainingTokenColumns == 0) {
        // No content to indent.
        ContentIndent = 0;
        ContentStartColumn =
            Token->getContentStartColumn(LineIndex, /*Break=*/true);
        NewRemainingTokenColumns = Token->getRemainingLength(
            LineIndex, TailOffset + Split.first + Split.second,
            ContentStartColumn);
      }

      // When breaking before a tab character, it may be moved by a few columns,
      // but will still be expanded to the next tab stop, so we don't save any
      // columns.
      if (NewRemainingTokenColumns == RemainingTokenColumns) {
        // FIXME: Do we need to adjust the penalty?
        break;
      }
      assert(NewRemainingTokenColumns < RemainingTokenColumns);

      LLVM_DEBUG(llvm::dbgs() << "    Breaking at: " << TailOffset + Split.first
                              << ", " << Split.second << "\n");
      if (!DryRun)
        Token->insertBreak(LineIndex, TailOffset, Split, ContentIndent,
                           Whitespaces);

      Penalty += NewBreakPenalty;
      TailOffset += Split.first + Split.second;
      RemainingTokenColumns = NewRemainingTokenColumns;
      BreakInserted = true;
      NewBreakBefore = true;
    }
    // In case there's another line, prepare the state for the start of the next
    // line.
    if (LineIndex + 1 != EndIndex) {
      unsigned NextLineIndex = LineIndex + 1;
      if (NewBreakBefore)
        // After breaking a line, try to reflow the next line into the current
        // one once RemainingTokenColumns fits.
        TryReflow = true;
      if (TryReflow) {
        // We decided that we want to try reflowing the next line into the
        // current one.
        // We will now adjust the state as if the reflow is successful (in
        // preparation for the next line), and see whether that works. If we
        // decide that we cannot reflow, we will later reset the state to the
        // start of the next line.
        Reflow = false;
        // As we did not continue breaking the line, RemainingTokenColumns is
        // known to fit after ContentStartColumn. Adapt ContentStartColumn to
        // the position at which we want to format the next line if we do
        // actually reflow.
        // When we reflow, we need to add a space between the end of the current
        // line and the next line's start column.
        ContentStartColumn += RemainingTokenColumns + 1;
        // Get the split that we need to reflow next logical line into the end
        // of the current one; the split will include any leading whitespace of
        // the next logical line.
        BreakableToken::Split SplitBeforeNext =
            Token->getReflowSplit(NextLineIndex, CommentPragmasRegex);
        LLVM_DEBUG(llvm::dbgs()
                   << "    Size of reflown text: " << ContentStartColumn
                   << "\n    Potential reflow split: ");
        if (SplitBeforeNext.first != StringRef::npos) {
          LLVM_DEBUG(llvm::dbgs() << SplitBeforeNext.first << ", "
                                  << SplitBeforeNext.second << "\n");
          TailOffset = SplitBeforeNext.first + SplitBeforeNext.second;
          // If the rest of the next line fits into the current line below the
          // column limit, we can safely reflow.
          RemainingTokenColumns = Token->getRemainingLength(
              NextLineIndex, TailOffset, ContentStartColumn);
          Reflow = true;
          if (ContentStartColumn + RemainingTokenColumns > ColumnLimit) {
            LLVM_DEBUG(llvm::dbgs()
                       << "    Over limit after reflow, need: "
                       << (ContentStartColumn + RemainingTokenColumns)
                       << ", space: " << ColumnLimit
                       << ", reflown prefix: " << ContentStartColumn
                       << ", offset in line: " << TailOffset << "\n");
            // If the whole next line does not fit, try to find a point in
            // the next line at which we can break so that attaching the part
            // of the next line to that break point onto the current line is
            // below the column limit.
            BreakableToken::Split Split =
                Token->getSplit(NextLineIndex, TailOffset, ColumnLimit,
                                ContentStartColumn, CommentPragmasRegex);
            if (Split.first == StringRef::npos) {
              LLVM_DEBUG(llvm::dbgs() << "    Did not find later break\n");
              Reflow = false;
            } else {
              // Check whether the first split point gets us below the column
              // limit. Note that we will execute this split below as part of
              // the normal token breaking and reflow logic within the line.
              unsigned ToSplitColumns = Token->getRangeLength(
                  NextLineIndex, TailOffset, Split.first, ContentStartColumn);
              if (ContentStartColumn + ToSplitColumns > ColumnLimit) {
                LLVM_DEBUG(llvm::dbgs() << "    Next split protrudes, need: "
                                        << (ContentStartColumn + ToSplitColumns)
                                        << ", space: " << ColumnLimit);
                unsigned ExcessCharactersPenalty =
                    (ContentStartColumn + ToSplitColumns - ColumnLimit) *
                    Style.PenaltyExcessCharacter;
                if (NewBreakPenalty < ExcessCharactersPenalty) {
                  Reflow = false;
                }
              }
            }
          }
        } else {
          LLVM_DEBUG(llvm::dbgs() << "not found.\n");
        }
      }
      if (!Reflow) {
        // If we didn't reflow into the next line, the only space to consider is
        // the next logical line. Reset our state to match the start of the next
        // line.
        TailOffset = 0;
        ContentStartColumn =
            Token->getContentStartColumn(NextLineIndex, /*Break=*/false);
        RemainingTokenColumns = Token->getRemainingLength(
            NextLineIndex, TailOffset, ContentStartColumn);
        // Adapt the start of the token, for example indent.
        if (!DryRun)
          Token->adaptStartOfLine(NextLineIndex, Whitespaces);
      } else {
        // If we found a reflow split and have added a new break before the next
        // line, we are going to remove the line break at the start of the next
        // logical line. For example, here we'll add a new line break after
        // 'text', and subsequently delete the line break between 'that' and
        // 'reflows'.
        //   // some text that
        //   // reflows
        // ->
        //   // some text
        //   // that reflows
        // When adding the line break, we also added the penalty for it, so we
        // need to subtract that penalty again when we remove the line break due
        // to reflowing.
        if (NewBreakBefore) {
          assert(Penalty >= NewBreakPenalty);
          Penalty -= NewBreakPenalty;
        }
        if (!DryRun)
          Token->reflow(NextLineIndex, Whitespaces);
      }
    }
  }

  BreakableToken::Split SplitAfterLastLine =
      Token->getSplitAfterLastLine(TailOffset);
  if (SplitAfterLastLine.first != StringRef::npos) {
    LLVM_DEBUG(llvm::dbgs() << "Replacing whitespace after last line.\n");

    // We add the last line's penalty here, since that line is going to be split
    // now.
    Penalty += Style.PenaltyExcessCharacter *
               (ContentStartColumn + RemainingTokenColumns - ColumnLimit);

    if (!DryRun)
      Token->replaceWhitespaceAfterLastLine(TailOffset, SplitAfterLastLine,
                                            Whitespaces);
    ContentStartColumn =
        Token->getContentStartColumn(Token->getLineCount() - 1, /*Break=*/true);
    RemainingTokenColumns = Token->getRemainingLength(
        Token->getLineCount() - 1,
        TailOffset + SplitAfterLastLine.first + SplitAfterLastLine.second,
        ContentStartColumn);
  }

  State.Column = ContentStartColumn + RemainingTokenColumns -
                 Current.UnbreakableTailLength;

  if (BreakInserted) {
    // If we break the token inside a parameter list, we need to break before
    // the next parameter on all levels, so that the next parameter is clearly
    // visible. Line comments already introduce a break.
    if (Current.isNot(TT_LineComment)) {
      for (unsigned i = 0, e = State.Stack.size(); i != e; ++i)
        State.Stack[i].BreakBeforeParameter = true;
    }

    if (Current.is(TT_BlockComment))
      State.NoContinuation = true;

    State.Stack.back().LastSpace = StartColumn;
  }

  Token->updateNextToken(State);

  return {Penalty, Exceeded};
}

unsigned ContinuationIndenter::getColumnLimit(const LineState &State) const {
  // In preprocessor directives reserve two chars for trailing " \"
  return Style.ColumnLimit - (State.Line->InPPDirective ? 2 : 0);
}

bool ContinuationIndenter::nextIsMultilineString(const LineState &State) {
  const FormatToken &Current = *State.NextToken;
  if (!Current.isStringLiteral() || Current.is(TT_ImplicitStringLiteral))
    return false;
  // We never consider raw string literals "multiline" for the purpose of
  // AlwaysBreakBeforeMultilineStrings implementation as they are special-cased
  // (see TokenAnnotator::mustBreakBefore().
  if (Current.TokenText.startswith("R\""))
    return false;
  if (Current.IsMultiline)
    return true;
  if (Current.getNextNonComment() &&
      Current.getNextNonComment()->isStringLiteral())
    return true; // Implicit concatenation.
  if (Style.ColumnLimit != 0 && Style.BreakStringLiterals &&
      State.Column + Current.ColumnWidth + Current.UnbreakableTailLength >
          Style.ColumnLimit)
    return true; // String will be split.
  return false;
}

} // namespace format
} // namespace clang
