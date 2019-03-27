//===--- UnwrappedLineParser.cpp - Format C++ code ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of the UnwrappedLineParser,
/// which turns a stream of tokens into UnwrappedLines.
///
//===----------------------------------------------------------------------===//

#include "UnwrappedLineParser.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

#define DEBUG_TYPE "format-parser"

namespace clang {
namespace format {

class FormatTokenSource {
public:
  virtual ~FormatTokenSource() {}
  virtual FormatToken *getNextToken() = 0;

  virtual unsigned getPosition() = 0;
  virtual FormatToken *setPosition(unsigned Position) = 0;
};

namespace {

class ScopedDeclarationState {
public:
  ScopedDeclarationState(UnwrappedLine &Line, std::vector<bool> &Stack,
                         bool MustBeDeclaration)
      : Line(Line), Stack(Stack) {
    Line.MustBeDeclaration = MustBeDeclaration;
    Stack.push_back(MustBeDeclaration);
  }
  ~ScopedDeclarationState() {
    Stack.pop_back();
    if (!Stack.empty())
      Line.MustBeDeclaration = Stack.back();
    else
      Line.MustBeDeclaration = true;
  }

private:
  UnwrappedLine &Line;
  std::vector<bool> &Stack;
};

static bool isLineComment(const FormatToken &FormatTok) {
  return FormatTok.is(tok::comment) && !FormatTok.TokenText.startswith("/*");
}

// Checks if \p FormatTok is a line comment that continues the line comment
// \p Previous. The original column of \p MinColumnToken is used to determine
// whether \p FormatTok is indented enough to the right to continue \p Previous.
static bool continuesLineComment(const FormatToken &FormatTok,
                                 const FormatToken *Previous,
                                 const FormatToken *MinColumnToken) {
  if (!Previous || !MinColumnToken)
    return false;
  unsigned MinContinueColumn =
      MinColumnToken->OriginalColumn + (isLineComment(*MinColumnToken) ? 0 : 1);
  return isLineComment(FormatTok) && FormatTok.NewlinesBefore == 1 &&
         isLineComment(*Previous) &&
         FormatTok.OriginalColumn >= MinContinueColumn;
}

class ScopedMacroState : public FormatTokenSource {
public:
  ScopedMacroState(UnwrappedLine &Line, FormatTokenSource *&TokenSource,
                   FormatToken *&ResetToken)
      : Line(Line), TokenSource(TokenSource), ResetToken(ResetToken),
        PreviousLineLevel(Line.Level), PreviousTokenSource(TokenSource),
        Token(nullptr), PreviousToken(nullptr) {
    FakeEOF.Tok.startToken();
    FakeEOF.Tok.setKind(tok::eof);
    TokenSource = this;
    Line.Level = 0;
    Line.InPPDirective = true;
  }

  ~ScopedMacroState() override {
    TokenSource = PreviousTokenSource;
    ResetToken = Token;
    Line.InPPDirective = false;
    Line.Level = PreviousLineLevel;
  }

  FormatToken *getNextToken() override {
    // The \c UnwrappedLineParser guards against this by never calling
    // \c getNextToken() after it has encountered the first eof token.
    assert(!eof());
    PreviousToken = Token;
    Token = PreviousTokenSource->getNextToken();
    if (eof())
      return &FakeEOF;
    return Token;
  }

  unsigned getPosition() override { return PreviousTokenSource->getPosition(); }

  FormatToken *setPosition(unsigned Position) override {
    PreviousToken = nullptr;
    Token = PreviousTokenSource->setPosition(Position);
    return Token;
  }

private:
  bool eof() {
    return Token && Token->HasUnescapedNewline &&
           !continuesLineComment(*Token, PreviousToken,
                                 /*MinColumnToken=*/PreviousToken);
  }

  FormatToken FakeEOF;
  UnwrappedLine &Line;
  FormatTokenSource *&TokenSource;
  FormatToken *&ResetToken;
  unsigned PreviousLineLevel;
  FormatTokenSource *PreviousTokenSource;

  FormatToken *Token;
  FormatToken *PreviousToken;
};

} // end anonymous namespace

class ScopedLineState {
public:
  ScopedLineState(UnwrappedLineParser &Parser,
                  bool SwitchToPreprocessorLines = false)
      : Parser(Parser), OriginalLines(Parser.CurrentLines) {
    if (SwitchToPreprocessorLines)
      Parser.CurrentLines = &Parser.PreprocessorDirectives;
    else if (!Parser.Line->Tokens.empty())
      Parser.CurrentLines = &Parser.Line->Tokens.back().Children;
    PreBlockLine = std::move(Parser.Line);
    Parser.Line = llvm::make_unique<UnwrappedLine>();
    Parser.Line->Level = PreBlockLine->Level;
    Parser.Line->InPPDirective = PreBlockLine->InPPDirective;
  }

  ~ScopedLineState() {
    if (!Parser.Line->Tokens.empty()) {
      Parser.addUnwrappedLine();
    }
    assert(Parser.Line->Tokens.empty());
    Parser.Line = std::move(PreBlockLine);
    if (Parser.CurrentLines == &Parser.PreprocessorDirectives)
      Parser.MustBreakBeforeNextToken = true;
    Parser.CurrentLines = OriginalLines;
  }

private:
  UnwrappedLineParser &Parser;

  std::unique_ptr<UnwrappedLine> PreBlockLine;
  SmallVectorImpl<UnwrappedLine> *OriginalLines;
};

class CompoundStatementIndenter {
public:
  CompoundStatementIndenter(UnwrappedLineParser *Parser,
                            const FormatStyle &Style, unsigned &LineLevel)
      : LineLevel(LineLevel), OldLineLevel(LineLevel) {
    if (Style.BraceWrapping.AfterControlStatement)
      Parser->addUnwrappedLine();
    if (Style.BraceWrapping.IndentBraces)
      ++LineLevel;
  }
  ~CompoundStatementIndenter() { LineLevel = OldLineLevel; }

private:
  unsigned &LineLevel;
  unsigned OldLineLevel;
};

namespace {

class IndexedTokenSource : public FormatTokenSource {
public:
  IndexedTokenSource(ArrayRef<FormatToken *> Tokens)
      : Tokens(Tokens), Position(-1) {}

  FormatToken *getNextToken() override {
    ++Position;
    return Tokens[Position];
  }

  unsigned getPosition() override {
    assert(Position >= 0);
    return Position;
  }

  FormatToken *setPosition(unsigned P) override {
    Position = P;
    return Tokens[Position];
  }

  void reset() { Position = -1; }

private:
  ArrayRef<FormatToken *> Tokens;
  int Position;
};

} // end anonymous namespace

UnwrappedLineParser::UnwrappedLineParser(const FormatStyle &Style,
                                         const AdditionalKeywords &Keywords,
                                         unsigned FirstStartColumn,
                                         ArrayRef<FormatToken *> Tokens,
                                         UnwrappedLineConsumer &Callback)
    : Line(new UnwrappedLine), MustBreakBeforeNextToken(false),
      CurrentLines(&Lines), Style(Style), Keywords(Keywords),
      CommentPragmasRegex(Style.CommentPragmas), Tokens(nullptr),
      Callback(Callback), AllTokens(Tokens), PPBranchLevel(-1),
      IncludeGuard(Style.IndentPPDirectives == FormatStyle::PPDIS_None
                       ? IG_Rejected
                       : IG_Inited),
      IncludeGuardToken(nullptr), FirstStartColumn(FirstStartColumn) {}

void UnwrappedLineParser::reset() {
  PPBranchLevel = -1;
  IncludeGuard = Style.IndentPPDirectives == FormatStyle::PPDIS_None
                     ? IG_Rejected
                     : IG_Inited;
  IncludeGuardToken = nullptr;
  Line.reset(new UnwrappedLine);
  CommentsBeforeNextToken.clear();
  FormatTok = nullptr;
  MustBreakBeforeNextToken = false;
  PreprocessorDirectives.clear();
  CurrentLines = &Lines;
  DeclarationScopeStack.clear();
  PPStack.clear();
  Line->FirstStartColumn = FirstStartColumn;
}

void UnwrappedLineParser::parse() {
  IndexedTokenSource TokenSource(AllTokens);
  Line->FirstStartColumn = FirstStartColumn;
  do {
    LLVM_DEBUG(llvm::dbgs() << "----\n");
    reset();
    Tokens = &TokenSource;
    TokenSource.reset();

    readToken();
    parseFile();

    // If we found an include guard then all preprocessor directives (other than
    // the guard) are over-indented by one.
    if (IncludeGuard == IG_Found)
      for (auto &Line : Lines)
        if (Line.InPPDirective && Line.Level > 0)
          --Line.Level;

    // Create line with eof token.
    pushToken(FormatTok);
    addUnwrappedLine();

    for (SmallVectorImpl<UnwrappedLine>::iterator I = Lines.begin(),
                                                  E = Lines.end();
         I != E; ++I) {
      Callback.consumeUnwrappedLine(*I);
    }
    Callback.finishRun();
    Lines.clear();
    while (!PPLevelBranchIndex.empty() &&
           PPLevelBranchIndex.back() + 1 >= PPLevelBranchCount.back()) {
      PPLevelBranchIndex.resize(PPLevelBranchIndex.size() - 1);
      PPLevelBranchCount.resize(PPLevelBranchCount.size() - 1);
    }
    if (!PPLevelBranchIndex.empty()) {
      ++PPLevelBranchIndex.back();
      assert(PPLevelBranchIndex.size() == PPLevelBranchCount.size());
      assert(PPLevelBranchIndex.back() <= PPLevelBranchCount.back());
    }
  } while (!PPLevelBranchIndex.empty());
}

void UnwrappedLineParser::parseFile() {
  // The top-level context in a file always has declarations, except for pre-
  // processor directives and JavaScript files.
  bool MustBeDeclaration =
      !Line->InPPDirective && Style.Language != FormatStyle::LK_JavaScript;
  ScopedDeclarationState DeclarationState(*Line, DeclarationScopeStack,
                                          MustBeDeclaration);
  if (Style.Language == FormatStyle::LK_TextProto)
    parseBracedList();
  else
    parseLevel(/*HasOpeningBrace=*/false);
  // Make sure to format the remaining tokens.
  //
  // LK_TextProto is special since its top-level is parsed as the body of a
  // braced list, which does not necessarily have natural line separators such
  // as a semicolon. Comments after the last entry that have been determined to
  // not belong to that line, as in:
  //   key: value
  //   // endfile comment
  // do not have a chance to be put on a line of their own until this point.
  // Here we add this newline before end-of-file comments.
  if (Style.Language == FormatStyle::LK_TextProto &&
      !CommentsBeforeNextToken.empty())
    addUnwrappedLine();
  flushComments(true);
  addUnwrappedLine();
}

void UnwrappedLineParser::parseLevel(bool HasOpeningBrace) {
  bool SwitchLabelEncountered = false;
  do {
    tok::TokenKind kind = FormatTok->Tok.getKind();
    if (FormatTok->Type == TT_MacroBlockBegin) {
      kind = tok::l_brace;
    } else if (FormatTok->Type == TT_MacroBlockEnd) {
      kind = tok::r_brace;
    }

    switch (kind) {
    case tok::comment:
      nextToken();
      addUnwrappedLine();
      break;
    case tok::l_brace:
      // FIXME: Add parameter whether this can happen - if this happens, we must
      // be in a non-declaration context.
      if (!FormatTok->is(TT_MacroBlockBegin) && tryToParseBracedList())
        continue;
      parseBlock(/*MustBeDeclaration=*/false);
      addUnwrappedLine();
      break;
    case tok::r_brace:
      if (HasOpeningBrace)
        return;
      nextToken();
      addUnwrappedLine();
      break;
    case tok::kw_default: {
      unsigned StoredPosition = Tokens->getPosition();
      FormatToken *Next;
      do {
        Next = Tokens->getNextToken();
      } while (Next && Next->is(tok::comment));
      FormatTok = Tokens->setPosition(StoredPosition);
      if (Next && Next->isNot(tok::colon)) {
        // default not followed by ':' is not a case label; treat it like
        // an identifier.
        parseStructuralElement();
        break;
      }
      // Else, if it is 'default:', fall through to the case handling.
      LLVM_FALLTHROUGH;
    }
    case tok::kw_case:
      if (Style.Language == FormatStyle::LK_JavaScript &&
          Line->MustBeDeclaration) {
        // A 'case: string' style field declaration.
        parseStructuralElement();
        break;
      }
      if (!SwitchLabelEncountered &&
          (Style.IndentCaseLabels || (Line->InPPDirective && Line->Level == 1)))
        ++Line->Level;
      SwitchLabelEncountered = true;
      parseStructuralElement();
      break;
    default:
      parseStructuralElement();
      break;
    }
  } while (!eof());
}

void UnwrappedLineParser::calculateBraceTypes(bool ExpectClassBody) {
  // We'll parse forward through the tokens until we hit
  // a closing brace or eof - note that getNextToken() will
  // parse macros, so this will magically work inside macro
  // definitions, too.
  unsigned StoredPosition = Tokens->getPosition();
  FormatToken *Tok = FormatTok;
  const FormatToken *PrevTok = Tok->Previous;
  // Keep a stack of positions of lbrace tokens. We will
  // update information about whether an lbrace starts a
  // braced init list or a different block during the loop.
  SmallVector<FormatToken *, 8> LBraceStack;
  assert(Tok->Tok.is(tok::l_brace));
  do {
    // Get next non-comment token.
    FormatToken *NextTok;
    unsigned ReadTokens = 0;
    do {
      NextTok = Tokens->getNextToken();
      ++ReadTokens;
    } while (NextTok->is(tok::comment));

    switch (Tok->Tok.getKind()) {
    case tok::l_brace:
      if (Style.Language == FormatStyle::LK_JavaScript && PrevTok) {
        if (PrevTok->isOneOf(tok::colon, tok::less))
          // A ':' indicates this code is in a type, or a braced list
          // following a label in an object literal ({a: {b: 1}}).
          // A '<' could be an object used in a comparison, but that is nonsense
          // code (can never return true), so more likely it is a generic type
          // argument (`X<{a: string; b: number}>`).
          // The code below could be confused by semicolons between the
          // individual members in a type member list, which would normally
          // trigger BK_Block. In both cases, this must be parsed as an inline
          // braced init.
          Tok->BlockKind = BK_BracedInit;
        else if (PrevTok->is(tok::r_paren))
          // `) { }` can only occur in function or method declarations in JS.
          Tok->BlockKind = BK_Block;
      } else {
        Tok->BlockKind = BK_Unknown;
      }
      LBraceStack.push_back(Tok);
      break;
    case tok::r_brace:
      if (LBraceStack.empty())
        break;
      if (LBraceStack.back()->BlockKind == BK_Unknown) {
        bool ProbablyBracedList = false;
        if (Style.Language == FormatStyle::LK_Proto) {
          ProbablyBracedList = NextTok->isOneOf(tok::comma, tok::r_square);
        } else {
          // Using OriginalColumn to distinguish between ObjC methods and
          // binary operators is a bit hacky.
          bool NextIsObjCMethod = NextTok->isOneOf(tok::plus, tok::minus) &&
                                  NextTok->OriginalColumn == 0;

          // If there is a comma, semicolon or right paren after the closing
          // brace, we assume this is a braced initializer list.  Note that
          // regardless how we mark inner braces here, we will overwrite the
          // BlockKind later if we parse a braced list (where all blocks
          // inside are by default braced lists), or when we explicitly detect
          // blocks (for example while parsing lambdas).
          // FIXME: Some of these do not apply to JS, e.g. "} {" can never be a
          // braced list in JS.
          ProbablyBracedList =
              (Style.Language == FormatStyle::LK_JavaScript &&
               NextTok->isOneOf(Keywords.kw_of, Keywords.kw_in,
                                Keywords.kw_as)) ||
              (Style.isCpp() && NextTok->is(tok::l_paren)) ||
              NextTok->isOneOf(tok::comma, tok::period, tok::colon,
                               tok::r_paren, tok::r_square, tok::l_brace,
                               tok::ellipsis) ||
              (NextTok->is(tok::identifier) &&
               !PrevTok->isOneOf(tok::semi, tok::r_brace, tok::l_brace)) ||
              (NextTok->is(tok::semi) &&
               (!ExpectClassBody || LBraceStack.size() != 1)) ||
              (NextTok->isBinaryOperator() && !NextIsObjCMethod);
          if (NextTok->is(tok::l_square)) {
            // We can have an array subscript after a braced init
            // list, but C++11 attributes are expected after blocks.
            NextTok = Tokens->getNextToken();
            ++ReadTokens;
            ProbablyBracedList = NextTok->isNot(tok::l_square);
          }
        }
        if (ProbablyBracedList) {
          Tok->BlockKind = BK_BracedInit;
          LBraceStack.back()->BlockKind = BK_BracedInit;
        } else {
          Tok->BlockKind = BK_Block;
          LBraceStack.back()->BlockKind = BK_Block;
        }
      }
      LBraceStack.pop_back();
      break;
    case tok::identifier:
      if (!Tok->is(TT_StatementMacro))
          break;
      LLVM_FALLTHROUGH;
    case tok::at:
    case tok::semi:
    case tok::kw_if:
    case tok::kw_while:
    case tok::kw_for:
    case tok::kw_switch:
    case tok::kw_try:
    case tok::kw___try:
      if (!LBraceStack.empty() && LBraceStack.back()->BlockKind == BK_Unknown)
        LBraceStack.back()->BlockKind = BK_Block;
      break;
    default:
      break;
    }
    PrevTok = Tok;
    Tok = NextTok;
  } while (Tok->Tok.isNot(tok::eof) && !LBraceStack.empty());

  // Assume other blocks for all unclosed opening braces.
  for (unsigned i = 0, e = LBraceStack.size(); i != e; ++i) {
    if (LBraceStack[i]->BlockKind == BK_Unknown)
      LBraceStack[i]->BlockKind = BK_Block;
  }

  FormatTok = Tokens->setPosition(StoredPosition);
}

template <class T>
static inline void hash_combine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t UnwrappedLineParser::computePPHash() const {
  size_t h = 0;
  for (const auto &i : PPStack) {
    hash_combine(h, size_t(i.Kind));
    hash_combine(h, i.Line);
  }
  return h;
}

void UnwrappedLineParser::parseBlock(bool MustBeDeclaration, bool AddLevel,
                                     bool MunchSemi) {
  assert(FormatTok->isOneOf(tok::l_brace, TT_MacroBlockBegin) &&
         "'{' or macro block token expected");
  const bool MacroBlock = FormatTok->is(TT_MacroBlockBegin);
  FormatTok->BlockKind = BK_Block;

  size_t PPStartHash = computePPHash();

  unsigned InitialLevel = Line->Level;
  nextToken(/*LevelDifference=*/AddLevel ? 1 : 0);

  if (MacroBlock && FormatTok->is(tok::l_paren))
    parseParens();

  size_t NbPreprocessorDirectives =
      CurrentLines == &Lines ? PreprocessorDirectives.size() : 0;
  addUnwrappedLine();
  size_t OpeningLineIndex =
      CurrentLines->empty()
          ? (UnwrappedLine::kInvalidIndex)
          : (CurrentLines->size() - 1 - NbPreprocessorDirectives);

  ScopedDeclarationState DeclarationState(*Line, DeclarationScopeStack,
                                          MustBeDeclaration);
  if (AddLevel)
    ++Line->Level;
  parseLevel(/*HasOpeningBrace=*/true);

  if (eof())
    return;

  if (MacroBlock ? !FormatTok->is(TT_MacroBlockEnd)
                 : !FormatTok->is(tok::r_brace)) {
    Line->Level = InitialLevel;
    FormatTok->BlockKind = BK_Block;
    return;
  }

  size_t PPEndHash = computePPHash();

  // Munch the closing brace.
  nextToken(/*LevelDifference=*/AddLevel ? -1 : 0);

  if (MacroBlock && FormatTok->is(tok::l_paren))
    parseParens();

  if (MunchSemi && FormatTok->Tok.is(tok::semi))
    nextToken();
  Line->Level = InitialLevel;

  if (PPStartHash == PPEndHash) {
    Line->MatchingOpeningBlockLineIndex = OpeningLineIndex;
    if (OpeningLineIndex != UnwrappedLine::kInvalidIndex) {
      // Update the opening line to add the forward reference as well
      (*CurrentLines)[OpeningLineIndex].MatchingClosingBlockLineIndex =
          CurrentLines->size() - 1;
    }
  }
}

static bool isGoogScope(const UnwrappedLine &Line) {
  // FIXME: Closure-library specific stuff should not be hard-coded but be
  // configurable.
  if (Line.Tokens.size() < 4)
    return false;
  auto I = Line.Tokens.begin();
  if (I->Tok->TokenText != "goog")
    return false;
  ++I;
  if (I->Tok->isNot(tok::period))
    return false;
  ++I;
  if (I->Tok->TokenText != "scope")
    return false;
  ++I;
  return I->Tok->is(tok::l_paren);
}

static bool isIIFE(const UnwrappedLine &Line,
                   const AdditionalKeywords &Keywords) {
  // Look for the start of an immediately invoked anonymous function.
  // https://en.wikipedia.org/wiki/Immediately-invoked_function_expression
  // This is commonly done in JavaScript to create a new, anonymous scope.
  // Example: (function() { ... })()
  if (Line.Tokens.size() < 3)
    return false;
  auto I = Line.Tokens.begin();
  if (I->Tok->isNot(tok::l_paren))
    return false;
  ++I;
  if (I->Tok->isNot(Keywords.kw_function))
    return false;
  ++I;
  return I->Tok->is(tok::l_paren);
}

static bool ShouldBreakBeforeBrace(const FormatStyle &Style,
                                   const FormatToken &InitialToken) {
  if (InitialToken.is(tok::kw_namespace))
    return Style.BraceWrapping.AfterNamespace;
  if (InitialToken.is(tok::kw_class))
    return Style.BraceWrapping.AfterClass;
  if (InitialToken.is(tok::kw_union))
    return Style.BraceWrapping.AfterUnion;
  if (InitialToken.is(tok::kw_struct))
    return Style.BraceWrapping.AfterStruct;
  return false;
}

void UnwrappedLineParser::parseChildBlock() {
  FormatTok->BlockKind = BK_Block;
  nextToken();
  {
    bool SkipIndent = (Style.Language == FormatStyle::LK_JavaScript &&
                       (isGoogScope(*Line) || isIIFE(*Line, Keywords)));
    ScopedLineState LineState(*this);
    ScopedDeclarationState DeclarationState(*Line, DeclarationScopeStack,
                                            /*MustBeDeclaration=*/false);
    Line->Level += SkipIndent ? 0 : 1;
    parseLevel(/*HasOpeningBrace=*/true);
    flushComments(isOnNewLine(*FormatTok));
    Line->Level -= SkipIndent ? 0 : 1;
  }
  nextToken();
}

void UnwrappedLineParser::parsePPDirective() {
  assert(FormatTok->Tok.is(tok::hash) && "'#' expected");
  ScopedMacroState MacroState(*Line, Tokens, FormatTok);
  nextToken();

  if (!FormatTok->Tok.getIdentifierInfo()) {
    parsePPUnknown();
    return;
  }

  switch (FormatTok->Tok.getIdentifierInfo()->getPPKeywordID()) {
  case tok::pp_define:
    parsePPDefine();
    return;
  case tok::pp_if:
    parsePPIf(/*IfDef=*/false);
    break;
  case tok::pp_ifdef:
  case tok::pp_ifndef:
    parsePPIf(/*IfDef=*/true);
    break;
  case tok::pp_else:
    parsePPElse();
    break;
  case tok::pp_elif:
    parsePPElIf();
    break;
  case tok::pp_endif:
    parsePPEndIf();
    break;
  default:
    parsePPUnknown();
    break;
  }
}

void UnwrappedLineParser::conditionalCompilationCondition(bool Unreachable) {
  size_t Line = CurrentLines->size();
  if (CurrentLines == &PreprocessorDirectives)
    Line += Lines.size();

  if (Unreachable ||
      (!PPStack.empty() && PPStack.back().Kind == PP_Unreachable))
    PPStack.push_back({PP_Unreachable, Line});
  else
    PPStack.push_back({PP_Conditional, Line});
}

void UnwrappedLineParser::conditionalCompilationStart(bool Unreachable) {
  ++PPBranchLevel;
  assert(PPBranchLevel >= 0 && PPBranchLevel <= (int)PPLevelBranchIndex.size());
  if (PPBranchLevel == (int)PPLevelBranchIndex.size()) {
    PPLevelBranchIndex.push_back(0);
    PPLevelBranchCount.push_back(0);
  }
  PPChainBranchIndex.push(0);
  bool Skip = PPLevelBranchIndex[PPBranchLevel] > 0;
  conditionalCompilationCondition(Unreachable || Skip);
}

void UnwrappedLineParser::conditionalCompilationAlternative() {
  if (!PPStack.empty())
    PPStack.pop_back();
  assert(PPBranchLevel < (int)PPLevelBranchIndex.size());
  if (!PPChainBranchIndex.empty())
    ++PPChainBranchIndex.top();
  conditionalCompilationCondition(
      PPBranchLevel >= 0 && !PPChainBranchIndex.empty() &&
      PPLevelBranchIndex[PPBranchLevel] != PPChainBranchIndex.top());
}

void UnwrappedLineParser::conditionalCompilationEnd() {
  assert(PPBranchLevel < (int)PPLevelBranchIndex.size());
  if (PPBranchLevel >= 0 && !PPChainBranchIndex.empty()) {
    if (PPChainBranchIndex.top() + 1 > PPLevelBranchCount[PPBranchLevel]) {
      PPLevelBranchCount[PPBranchLevel] = PPChainBranchIndex.top() + 1;
    }
  }
  // Guard against #endif's without #if.
  if (PPBranchLevel > -1)
    --PPBranchLevel;
  if (!PPChainBranchIndex.empty())
    PPChainBranchIndex.pop();
  if (!PPStack.empty())
    PPStack.pop_back();
}

void UnwrappedLineParser::parsePPIf(bool IfDef) {
  bool IfNDef = FormatTok->is(tok::pp_ifndef);
  nextToken();
  bool Unreachable = false;
  if (!IfDef && (FormatTok->is(tok::kw_false) || FormatTok->TokenText == "0"))
    Unreachable = true;
  if (IfDef && !IfNDef && FormatTok->TokenText == "SWIG")
    Unreachable = true;
  conditionalCompilationStart(Unreachable);
  FormatToken *IfCondition = FormatTok;
  // If there's a #ifndef on the first line, and the only lines before it are
  // comments, it could be an include guard.
  bool MaybeIncludeGuard = IfNDef;
  if (IncludeGuard == IG_Inited && MaybeIncludeGuard)
    for (auto &Line : Lines) {
      if (!Line.Tokens.front().Tok->is(tok::comment)) {
        MaybeIncludeGuard = false;
        IncludeGuard = IG_Rejected;
        break;
      }
    }
  --PPBranchLevel;
  parsePPUnknown();
  ++PPBranchLevel;
  if (IncludeGuard == IG_Inited && MaybeIncludeGuard) {
    IncludeGuard = IG_IfNdefed;
    IncludeGuardToken = IfCondition;
  }
}

void UnwrappedLineParser::parsePPElse() {
  // If a potential include guard has an #else, it's not an include guard.
  if (IncludeGuard == IG_Defined && PPBranchLevel == 0)
    IncludeGuard = IG_Rejected;
  conditionalCompilationAlternative();
  if (PPBranchLevel > -1)
    --PPBranchLevel;
  parsePPUnknown();
  ++PPBranchLevel;
}

void UnwrappedLineParser::parsePPElIf() { parsePPElse(); }

void UnwrappedLineParser::parsePPEndIf() {
  conditionalCompilationEnd();
  parsePPUnknown();
  // If the #endif of a potential include guard is the last thing in the file,
  // then we found an include guard.
  unsigned TokenPosition = Tokens->getPosition();
  FormatToken *PeekNext = AllTokens[TokenPosition];
  if (IncludeGuard == IG_Defined && PPBranchLevel == -1 &&
      PeekNext->is(tok::eof) &&
      Style.IndentPPDirectives != FormatStyle::PPDIS_None)
    IncludeGuard = IG_Found;
}

void UnwrappedLineParser::parsePPDefine() {
  nextToken();

  if (FormatTok->Tok.getKind() != tok::identifier) {
    IncludeGuard = IG_Rejected;
    IncludeGuardToken = nullptr;
    parsePPUnknown();
    return;
  }

  if (IncludeGuard == IG_IfNdefed &&
      IncludeGuardToken->TokenText == FormatTok->TokenText) {
    IncludeGuard = IG_Defined;
    IncludeGuardToken = nullptr;
    for (auto &Line : Lines) {
      if (!Line.Tokens.front().Tok->isOneOf(tok::comment, tok::hash)) {
        IncludeGuard = IG_Rejected;
        break;
      }
    }
  }

  nextToken();
  if (FormatTok->Tok.getKind() == tok::l_paren &&
      FormatTok->WhitespaceRange.getBegin() ==
          FormatTok->WhitespaceRange.getEnd()) {
    parseParens();
  }
  if (Style.IndentPPDirectives == FormatStyle::PPDIS_AfterHash)
    Line->Level += PPBranchLevel + 1;
  addUnwrappedLine();
  ++Line->Level;

  // Errors during a preprocessor directive can only affect the layout of the
  // preprocessor directive, and thus we ignore them. An alternative approach
  // would be to use the same approach we use on the file level (no
  // re-indentation if there was a structural error) within the macro
  // definition.
  parseFile();
}

void UnwrappedLineParser::parsePPUnknown() {
  do {
    nextToken();
  } while (!eof());
  if (Style.IndentPPDirectives == FormatStyle::PPDIS_AfterHash)
    Line->Level += PPBranchLevel + 1;
  addUnwrappedLine();
}

// Here we blacklist certain tokens that are not usually the first token in an
// unwrapped line. This is used in attempt to distinguish macro calls without
// trailing semicolons from other constructs split to several lines.
static bool tokenCanStartNewLine(const clang::Token &Tok) {
  // Semicolon can be a null-statement, l_square can be a start of a macro or
  // a C++11 attribute, but this doesn't seem to be common.
  return Tok.isNot(tok::semi) && Tok.isNot(tok::l_brace) &&
         Tok.isNot(tok::l_square) &&
         // Tokens that can only be used as binary operators and a part of
         // overloaded operator names.
         Tok.isNot(tok::period) && Tok.isNot(tok::periodstar) &&
         Tok.isNot(tok::arrow) && Tok.isNot(tok::arrowstar) &&
         Tok.isNot(tok::less) && Tok.isNot(tok::greater) &&
         Tok.isNot(tok::slash) && Tok.isNot(tok::percent) &&
         Tok.isNot(tok::lessless) && Tok.isNot(tok::greatergreater) &&
         Tok.isNot(tok::equal) && Tok.isNot(tok::plusequal) &&
         Tok.isNot(tok::minusequal) && Tok.isNot(tok::starequal) &&
         Tok.isNot(tok::slashequal) && Tok.isNot(tok::percentequal) &&
         Tok.isNot(tok::ampequal) && Tok.isNot(tok::pipeequal) &&
         Tok.isNot(tok::caretequal) && Tok.isNot(tok::greatergreaterequal) &&
         Tok.isNot(tok::lesslessequal) &&
         // Colon is used in labels, base class lists, initializer lists,
         // range-based for loops, ternary operator, but should never be the
         // first token in an unwrapped line.
         Tok.isNot(tok::colon) &&
         // 'noexcept' is a trailing annotation.
         Tok.isNot(tok::kw_noexcept);
}

static bool mustBeJSIdent(const AdditionalKeywords &Keywords,
                          const FormatToken *FormatTok) {
  // FIXME: This returns true for C/C++ keywords like 'struct'.
  return FormatTok->is(tok::identifier) &&
         (FormatTok->Tok.getIdentifierInfo() == nullptr ||
          !FormatTok->isOneOf(
              Keywords.kw_in, Keywords.kw_of, Keywords.kw_as, Keywords.kw_async,
              Keywords.kw_await, Keywords.kw_yield, Keywords.kw_finally,
              Keywords.kw_function, Keywords.kw_import, Keywords.kw_is,
              Keywords.kw_let, Keywords.kw_var, tok::kw_const,
              Keywords.kw_abstract, Keywords.kw_extends, Keywords.kw_implements,
              Keywords.kw_instanceof, Keywords.kw_interface, Keywords.kw_throws,
              Keywords.kw_from));
}

static bool mustBeJSIdentOrValue(const AdditionalKeywords &Keywords,
                                 const FormatToken *FormatTok) {
  return FormatTok->Tok.isLiteral() ||
         FormatTok->isOneOf(tok::kw_true, tok::kw_false) ||
         mustBeJSIdent(Keywords, FormatTok);
}

// isJSDeclOrStmt returns true if |FormatTok| starts a declaration or statement
// when encountered after a value (see mustBeJSIdentOrValue).
static bool isJSDeclOrStmt(const AdditionalKeywords &Keywords,
                           const FormatToken *FormatTok) {
  return FormatTok->isOneOf(
      tok::kw_return, Keywords.kw_yield,
      // conditionals
      tok::kw_if, tok::kw_else,
      // loops
      tok::kw_for, tok::kw_while, tok::kw_do, tok::kw_continue, tok::kw_break,
      // switch/case
      tok::kw_switch, tok::kw_case,
      // exceptions
      tok::kw_throw, tok::kw_try, tok::kw_catch, Keywords.kw_finally,
      // declaration
      tok::kw_const, tok::kw_class, Keywords.kw_var, Keywords.kw_let,
      Keywords.kw_async, Keywords.kw_function,
      // import/export
      Keywords.kw_import, tok::kw_export);
}

// readTokenWithJavaScriptASI reads the next token and terminates the current
// line if JavaScript Automatic Semicolon Insertion must
// happen between the current token and the next token.
//
// This method is conservative - it cannot cover all edge cases of JavaScript,
// but only aims to correctly handle certain well known cases. It *must not*
// return true in speculative cases.
void UnwrappedLineParser::readTokenWithJavaScriptASI() {
  FormatToken *Previous = FormatTok;
  readToken();
  FormatToken *Next = FormatTok;

  bool IsOnSameLine =
      CommentsBeforeNextToken.empty()
          ? Next->NewlinesBefore == 0
          : CommentsBeforeNextToken.front()->NewlinesBefore == 0;
  if (IsOnSameLine)
    return;

  bool PreviousMustBeValue = mustBeJSIdentOrValue(Keywords, Previous);
  bool PreviousStartsTemplateExpr =
      Previous->is(TT_TemplateString) && Previous->TokenText.endswith("${");
  if (PreviousMustBeValue || Previous->is(tok::r_paren)) {
    // If the line contains an '@' sign, the previous token might be an
    // annotation, which can precede another identifier/value.
    bool HasAt = std::find_if(Line->Tokens.begin(), Line->Tokens.end(),
                              [](UnwrappedLineNode &LineNode) {
                                return LineNode.Tok->is(tok::at);
                              }) != Line->Tokens.end();
    if (HasAt)
      return;
  }
  if (Next->is(tok::exclaim) && PreviousMustBeValue)
    return addUnwrappedLine();
  bool NextMustBeValue = mustBeJSIdentOrValue(Keywords, Next);
  bool NextEndsTemplateExpr =
      Next->is(TT_TemplateString) && Next->TokenText.startswith("}");
  if (NextMustBeValue && !NextEndsTemplateExpr && !PreviousStartsTemplateExpr &&
      (PreviousMustBeValue ||
       Previous->isOneOf(tok::r_square, tok::r_paren, tok::plusplus,
                         tok::minusminus)))
    return addUnwrappedLine();
  if ((PreviousMustBeValue || Previous->is(tok::r_paren)) &&
      isJSDeclOrStmt(Keywords, Next))
    return addUnwrappedLine();
}

void UnwrappedLineParser::parseStructuralElement() {
  assert(!FormatTok->is(tok::l_brace));
  if (Style.Language == FormatStyle::LK_TableGen &&
      FormatTok->is(tok::pp_include)) {
    nextToken();
    if (FormatTok->is(tok::string_literal))
      nextToken();
    addUnwrappedLine();
    return;
  }
  switch (FormatTok->Tok.getKind()) {
  case tok::kw_asm:
    nextToken();
    if (FormatTok->is(tok::l_brace)) {
      FormatTok->Type = TT_InlineASMBrace;
      nextToken();
      while (FormatTok && FormatTok->isNot(tok::eof)) {
        if (FormatTok->is(tok::r_brace)) {
          FormatTok->Type = TT_InlineASMBrace;
          nextToken();
          addUnwrappedLine();
          break;
        }
        FormatTok->Finalized = true;
        nextToken();
      }
    }
    break;
  case tok::kw_namespace:
    parseNamespace();
    return;
  case tok::kw_public:
  case tok::kw_protected:
  case tok::kw_private:
    if (Style.Language == FormatStyle::LK_Java ||
        Style.Language == FormatStyle::LK_JavaScript)
      nextToken();
    else
      parseAccessSpecifier();
    return;
  case tok::kw_if:
    parseIfThenElse();
    return;
  case tok::kw_for:
  case tok::kw_while:
    parseForOrWhileLoop();
    return;
  case tok::kw_do:
    parseDoWhile();
    return;
  case tok::kw_switch:
    if (Style.Language == FormatStyle::LK_JavaScript && Line->MustBeDeclaration)
      // 'switch: string' field declaration.
      break;
    parseSwitch();
    return;
  case tok::kw_default:
    if (Style.Language == FormatStyle::LK_JavaScript && Line->MustBeDeclaration)
      // 'default: string' field declaration.
      break;
    nextToken();
    if (FormatTok->is(tok::colon)) {
      parseLabel();
      return;
    }
    // e.g. "default void f() {}" in a Java interface.
    break;
  case tok::kw_case:
    if (Style.Language == FormatStyle::LK_JavaScript && Line->MustBeDeclaration)
      // 'case: string' field declaration.
      break;
    parseCaseLabel();
    return;
  case tok::kw_try:
  case tok::kw___try:
    parseTryCatch();
    return;
  case tok::kw_extern:
    nextToken();
    if (FormatTok->Tok.is(tok::string_literal)) {
      nextToken();
      if (FormatTok->Tok.is(tok::l_brace)) {
        if (Style.BraceWrapping.AfterExternBlock) {
          addUnwrappedLine();
          parseBlock(/*MustBeDeclaration=*/true);
        } else {
          parseBlock(/*MustBeDeclaration=*/true, /*AddLevel=*/false);
        }
        addUnwrappedLine();
        return;
      }
    }
    break;
  case tok::kw_export:
    if (Style.Language == FormatStyle::LK_JavaScript) {
      parseJavaScriptEs6ImportExport();
      return;
    }
    if (!Style.isCpp())
      break;
    // Handle C++ "(inline|export) namespace".
    LLVM_FALLTHROUGH;
  case tok::kw_inline:
    nextToken();
    if (FormatTok->Tok.is(tok::kw_namespace)) {
      parseNamespace();
      return;
    }
    break;
  case tok::identifier:
    if (FormatTok->is(TT_ForEachMacro)) {
      parseForOrWhileLoop();
      return;
    }
    if (FormatTok->is(TT_MacroBlockBegin)) {
      parseBlock(/*MustBeDeclaration=*/false, /*AddLevel=*/true,
                 /*MunchSemi=*/false);
      return;
    }
    if (FormatTok->is(Keywords.kw_import)) {
      if (Style.Language == FormatStyle::LK_JavaScript) {
        parseJavaScriptEs6ImportExport();
        return;
      }
      if (Style.Language == FormatStyle::LK_Proto) {
        nextToken();
        if (FormatTok->is(tok::kw_public))
          nextToken();
        if (!FormatTok->is(tok::string_literal))
          return;
        nextToken();
        if (FormatTok->is(tok::semi))
          nextToken();
        addUnwrappedLine();
        return;
      }
    }
    if (Style.isCpp() &&
        FormatTok->isOneOf(Keywords.kw_signals, Keywords.kw_qsignals,
                           Keywords.kw_slots, Keywords.kw_qslots)) {
      nextToken();
      if (FormatTok->is(tok::colon)) {
        nextToken();
        addUnwrappedLine();
        return;
      }
    }
    if (Style.isCpp() && FormatTok->is(TT_StatementMacro)) {
      parseStatementMacro();
      return;
    }
    // In all other cases, parse the declaration.
    break;
  default:
    break;
  }
  do {
    const FormatToken *Previous = FormatTok->Previous;
    switch (FormatTok->Tok.getKind()) {
    case tok::at:
      nextToken();
      if (FormatTok->Tok.is(tok::l_brace)) {
        nextToken();
        parseBracedList();
        break;
      } else if (Style.Language == FormatStyle::LK_Java &&
                 FormatTok->is(Keywords.kw_interface)) {
        nextToken();
        break;
      }
      switch (FormatTok->Tok.getObjCKeywordID()) {
      case tok::objc_public:
      case tok::objc_protected:
      case tok::objc_package:
      case tok::objc_private:
        return parseAccessSpecifier();
      case tok::objc_interface:
      case tok::objc_implementation:
        return parseObjCInterfaceOrImplementation();
      case tok::objc_protocol:
        if (parseObjCProtocol())
          return;
        break;
      case tok::objc_end:
        return; // Handled by the caller.
      case tok::objc_optional:
      case tok::objc_required:
        nextToken();
        addUnwrappedLine();
        return;
      case tok::objc_autoreleasepool:
        nextToken();
        if (FormatTok->Tok.is(tok::l_brace)) {
          if (Style.BraceWrapping.AfterControlStatement)
            addUnwrappedLine();
          parseBlock(/*MustBeDeclaration=*/false);
        }
        addUnwrappedLine();
        return;
      case tok::objc_synchronized:
        nextToken();
        if (FormatTok->Tok.is(tok::l_paren))
           // Skip synchronization object
           parseParens();
        if (FormatTok->Tok.is(tok::l_brace)) {
          if (Style.BraceWrapping.AfterControlStatement)
            addUnwrappedLine();
          parseBlock(/*MustBeDeclaration=*/false);
        }
        addUnwrappedLine();
        return;
      case tok::objc_try:
        // This branch isn't strictly necessary (the kw_try case below would
        // do this too after the tok::at is parsed above).  But be explicit.
        parseTryCatch();
        return;
      default:
        break;
      }
      break;
    case tok::kw_enum:
      // Ignore if this is part of "template <enum ...".
      if (Previous && Previous->is(tok::less)) {
        nextToken();
        break;
      }

      // parseEnum falls through and does not yet add an unwrapped line as an
      // enum definition can start a structural element.
      if (!parseEnum())
        break;
      // This only applies for C++.
      if (!Style.isCpp()) {
        addUnwrappedLine();
        return;
      }
      break;
    case tok::kw_typedef:
      nextToken();
      if (FormatTok->isOneOf(Keywords.kw_NS_ENUM, Keywords.kw_NS_OPTIONS,
                             Keywords.kw_CF_ENUM, Keywords.kw_CF_OPTIONS))
        parseEnum();
      break;
    case tok::kw_struct:
    case tok::kw_union:
    case tok::kw_class:
      // parseRecord falls through and does not yet add an unwrapped line as a
      // record declaration or definition can start a structural element.
      parseRecord();
      // This does not apply for Java and JavaScript.
      if (Style.Language == FormatStyle::LK_Java ||
          Style.Language == FormatStyle::LK_JavaScript) {
        if (FormatTok->is(tok::semi))
          nextToken();
        addUnwrappedLine();
        return;
      }
      break;
    case tok::period:
      nextToken();
      // In Java, classes have an implicit static member "class".
      if (Style.Language == FormatStyle::LK_Java && FormatTok &&
          FormatTok->is(tok::kw_class))
        nextToken();
      if (Style.Language == FormatStyle::LK_JavaScript && FormatTok &&
          FormatTok->Tok.getIdentifierInfo())
        // JavaScript only has pseudo keywords, all keywords are allowed to
        // appear in "IdentifierName" positions. See http://es5.github.io/#x7.6
        nextToken();
      break;
    case tok::semi:
      nextToken();
      addUnwrappedLine();
      return;
    case tok::r_brace:
      addUnwrappedLine();
      return;
    case tok::l_paren:
      parseParens();
      break;
    case tok::kw_operator:
      nextToken();
      if (FormatTok->isBinaryOperator())
        nextToken();
      break;
    case tok::caret:
      nextToken();
      if (FormatTok->Tok.isAnyIdentifier() ||
          FormatTok->isSimpleTypeSpecifier())
        nextToken();
      if (FormatTok->is(tok::l_paren))
        parseParens();
      if (FormatTok->is(tok::l_brace))
        parseChildBlock();
      break;
    case tok::l_brace:
      if (!tryToParseBracedList()) {
        // A block outside of parentheses must be the last part of a
        // structural element.
        // FIXME: Figure out cases where this is not true, and add projections
        // for them (the one we know is missing are lambdas).
        if (Style.BraceWrapping.AfterFunction)
          addUnwrappedLine();
        FormatTok->Type = TT_FunctionLBrace;
        parseBlock(/*MustBeDeclaration=*/false);
        addUnwrappedLine();
        return;
      }
      // Otherwise this was a braced init list, and the structural
      // element continues.
      break;
    case tok::kw_try:
      // We arrive here when parsing function-try blocks.
      if (Style.BraceWrapping.AfterFunction)
        addUnwrappedLine();
      parseTryCatch();
      return;
    case tok::identifier: {
      if (FormatTok->is(TT_MacroBlockEnd)) {
        addUnwrappedLine();
        return;
      }

      // Function declarations (as opposed to function expressions) are parsed
      // on their own unwrapped line by continuing this loop. Function
      // expressions (functions that are not on their own line) must not create
      // a new unwrapped line, so they are special cased below.
      size_t TokenCount = Line->Tokens.size();
      if (Style.Language == FormatStyle::LK_JavaScript &&
          FormatTok->is(Keywords.kw_function) &&
          (TokenCount > 1 || (TokenCount == 1 && !Line->Tokens.front().Tok->is(
                                                     Keywords.kw_async)))) {
        tryToParseJSFunction();
        break;
      }
      if ((Style.Language == FormatStyle::LK_JavaScript ||
           Style.Language == FormatStyle::LK_Java) &&
          FormatTok->is(Keywords.kw_interface)) {
        if (Style.Language == FormatStyle::LK_JavaScript) {
          // In JavaScript/TypeScript, "interface" can be used as a standalone
          // identifier, e.g. in `var interface = 1;`. If "interface" is
          // followed by another identifier, it is very like to be an actual
          // interface declaration.
          unsigned StoredPosition = Tokens->getPosition();
          FormatToken *Next = Tokens->getNextToken();
          FormatTok = Tokens->setPosition(StoredPosition);
          if (Next && !mustBeJSIdent(Keywords, Next)) {
            nextToken();
            break;
          }
        }
        parseRecord();
        addUnwrappedLine();
        return;
      }

      if (Style.isCpp() && FormatTok->is(TT_StatementMacro)) {
        parseStatementMacro();
        return;
      }

      // See if the following token should start a new unwrapped line.
      StringRef Text = FormatTok->TokenText;
      nextToken();
      if (Line->Tokens.size() == 1 &&
          // JS doesn't have macros, and within classes colons indicate fields,
          // not labels.
          Style.Language != FormatStyle::LK_JavaScript) {
        if (FormatTok->Tok.is(tok::colon) && !Line->MustBeDeclaration) {
          Line->Tokens.begin()->Tok->MustBreakBefore = true;
          parseLabel();
          return;
        }
        // Recognize function-like macro usages without trailing semicolon as
        // well as free-standing macros like Q_OBJECT.
        bool FunctionLike = FormatTok->is(tok::l_paren);
        if (FunctionLike)
          parseParens();

        bool FollowedByNewline =
            CommentsBeforeNextToken.empty()
                ? FormatTok->NewlinesBefore > 0
                : CommentsBeforeNextToken.front()->NewlinesBefore > 0;

        if (FollowedByNewline && (Text.size() >= 5 || FunctionLike) &&
            tokenCanStartNewLine(FormatTok->Tok) && Text == Text.upper()) {
          addUnwrappedLine();
          return;
        }
      }
      break;
    }
    case tok::equal:
      // Fat arrows (=>) have tok::TokenKind tok::equal but TokenType
      // TT_JsFatArrow. The always start an expression or a child block if
      // followed by a curly.
      if (FormatTok->is(TT_JsFatArrow)) {
        nextToken();
        if (FormatTok->is(tok::l_brace))
          parseChildBlock();
        break;
      }

      nextToken();
      if (FormatTok->Tok.is(tok::l_brace)) {
        nextToken();
        parseBracedList();
      } else if (Style.Language == FormatStyle::LK_Proto &&
                 FormatTok->Tok.is(tok::less)) {
        nextToken();
        parseBracedList(/*ContinueOnSemicolons=*/false,
                        /*ClosingBraceKind=*/tok::greater);
      }
      break;
    case tok::l_square:
      parseSquare();
      break;
    case tok::kw_new:
      parseNew();
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
}

bool UnwrappedLineParser::tryToParseLambda() {
  if (!Style.isCpp()) {
    nextToken();
    return false;
  }
  assert(FormatTok->is(tok::l_square));
  FormatToken &LSquare = *FormatTok;
  if (!tryToParseLambdaIntroducer())
    return false;

  while (FormatTok->isNot(tok::l_brace)) {
    if (FormatTok->isSimpleTypeSpecifier()) {
      nextToken();
      continue;
    }
    switch (FormatTok->Tok.getKind()) {
    case tok::l_brace:
      break;
    case tok::l_paren:
      parseParens();
      break;
    case tok::amp:
    case tok::star:
    case tok::kw_const:
    case tok::comma:
    case tok::less:
    case tok::greater:
    case tok::identifier:
    case tok::numeric_constant:
    case tok::coloncolon:
    case tok::kw_mutable:
      nextToken();
      break;
    case tok::arrow:
      FormatTok->Type = TT_LambdaArrow;
      nextToken();
      break;
    default:
      return true;
    }
  }
  LSquare.Type = TT_LambdaLSquare;
  parseChildBlock();
  return true;
}

bool UnwrappedLineParser::tryToParseLambdaIntroducer() {
  const FormatToken *Previous = FormatTok->Previous;
  if (Previous &&
      (Previous->isOneOf(tok::identifier, tok::kw_operator, tok::kw_new,
                         tok::kw_delete, tok::l_square) ||
       FormatTok->isCppStructuredBinding(Style) || Previous->closesScope() ||
       Previous->isSimpleTypeSpecifier())) {
    nextToken();
    return false;
  }
  nextToken();
  if (FormatTok->is(tok::l_square)) {
    return false;
  }
  parseSquare(/*LambdaIntroducer=*/true);
  return true;
}

void UnwrappedLineParser::tryToParseJSFunction() {
  assert(FormatTok->is(Keywords.kw_function) ||
         FormatTok->startsSequence(Keywords.kw_async, Keywords.kw_function));
  if (FormatTok->is(Keywords.kw_async))
    nextToken();
  // Consume "function".
  nextToken();

  // Consume * (generator function). Treat it like C++'s overloaded operators.
  if (FormatTok->is(tok::star)) {
    FormatTok->Type = TT_OverloadedOperator;
    nextToken();
  }

  // Consume function name.
  if (FormatTok->is(tok::identifier))
    nextToken();

  if (FormatTok->isNot(tok::l_paren))
    return;

  // Parse formal parameter list.
  parseParens();

  if (FormatTok->is(tok::colon)) {
    // Parse a type definition.
    nextToken();

    // Eat the type declaration. For braced inline object types, balance braces,
    // otherwise just parse until finding an l_brace for the function body.
    if (FormatTok->is(tok::l_brace))
      tryToParseBracedList();
    else
      while (!FormatTok->isOneOf(tok::l_brace, tok::semi) && !eof())
        nextToken();
  }

  if (FormatTok->is(tok::semi))
    return;

  parseChildBlock();
}

bool UnwrappedLineParser::tryToParseBracedList() {
  if (FormatTok->BlockKind == BK_Unknown)
    calculateBraceTypes();
  assert(FormatTok->BlockKind != BK_Unknown);
  if (FormatTok->BlockKind == BK_Block)
    return false;
  nextToken();
  parseBracedList();
  return true;
}

bool UnwrappedLineParser::parseBracedList(bool ContinueOnSemicolons,
                                          tok::TokenKind ClosingBraceKind) {
  bool HasError = false;

  // FIXME: Once we have an expression parser in the UnwrappedLineParser,
  // replace this by using parseAssigmentExpression() inside.
  do {
    if (Style.Language == FormatStyle::LK_JavaScript) {
      if (FormatTok->is(Keywords.kw_function) ||
          FormatTok->startsSequence(Keywords.kw_async, Keywords.kw_function)) {
        tryToParseJSFunction();
        continue;
      }
      if (FormatTok->is(TT_JsFatArrow)) {
        nextToken();
        // Fat arrows can be followed by simple expressions or by child blocks
        // in curly braces.
        if (FormatTok->is(tok::l_brace)) {
          parseChildBlock();
          continue;
        }
      }
      if (FormatTok->is(tok::l_brace)) {
        // Could be a method inside of a braced list `{a() { return 1; }}`.
        if (tryToParseBracedList())
          continue;
        parseChildBlock();
      }
    }
    if (FormatTok->Tok.getKind() == ClosingBraceKind) {
      nextToken();
      return !HasError;
    }
    switch (FormatTok->Tok.getKind()) {
    case tok::caret:
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        parseChildBlock();
      }
      break;
    case tok::l_square:
      tryToParseLambda();
      break;
    case tok::l_paren:
      parseParens();
      // JavaScript can just have free standing methods and getters/setters in
      // object literals. Detect them by a "{" following ")".
      if (Style.Language == FormatStyle::LK_JavaScript) {
        if (FormatTok->is(tok::l_brace))
          parseChildBlock();
        break;
      }
      break;
    case tok::l_brace:
      // Assume there are no blocks inside a braced init list apart
      // from the ones we explicitly parse out (like lambdas).
      FormatTok->BlockKind = BK_BracedInit;
      nextToken();
      parseBracedList();
      break;
    case tok::less:
      if (Style.Language == FormatStyle::LK_Proto) {
        nextToken();
        parseBracedList(/*ContinueOnSemicolons=*/false,
                        /*ClosingBraceKind=*/tok::greater);
      } else {
        nextToken();
      }
      break;
    case tok::semi:
      // JavaScript (or more precisely TypeScript) can have semicolons in braced
      // lists (in so-called TypeMemberLists). Thus, the semicolon cannot be
      // used for error recovery if we have otherwise determined that this is
      // a braced list.
      if (Style.Language == FormatStyle::LK_JavaScript) {
        nextToken();
        break;
      }
      HasError = true;
      if (!ContinueOnSemicolons)
        return !HasError;
      nextToken();
      break;
    case tok::comma:
      nextToken();
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
  return false;
}

void UnwrappedLineParser::parseParens() {
  assert(FormatTok->Tok.is(tok::l_paren) && "'(' expected.");
  nextToken();
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::l_paren:
      parseParens();
      if (Style.Language == FormatStyle::LK_Java && FormatTok->is(tok::l_brace))
        parseChildBlock();
      break;
    case tok::r_paren:
      nextToken();
      return;
    case tok::r_brace:
      // A "}" inside parenthesis is an error if there wasn't a matching "{".
      return;
    case tok::l_square:
      tryToParseLambda();
      break;
    case tok::l_brace:
      if (!tryToParseBracedList())
        parseChildBlock();
      break;
    case tok::at:
      nextToken();
      if (FormatTok->Tok.is(tok::l_brace)) {
        nextToken();
        parseBracedList();
      }
      break;
    case tok::kw_class:
      if (Style.Language == FormatStyle::LK_JavaScript)
        parseRecord(/*ParseAsExpr=*/true);
      else
        nextToken();
      break;
    case tok::identifier:
      if (Style.Language == FormatStyle::LK_JavaScript &&
          (FormatTok->is(Keywords.kw_function) ||
           FormatTok->startsSequence(Keywords.kw_async, Keywords.kw_function)))
        tryToParseJSFunction();
      else
        nextToken();
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
}

void UnwrappedLineParser::parseSquare(bool LambdaIntroducer) {
  if (!LambdaIntroducer) {
    assert(FormatTok->Tok.is(tok::l_square) && "'[' expected.");
    if (tryToParseLambda())
      return;
  }
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::l_paren:
      parseParens();
      break;
    case tok::r_square:
      nextToken();
      return;
    case tok::r_brace:
      // A "}" inside parenthesis is an error if there wasn't a matching "{".
      return;
    case tok::l_square:
      parseSquare();
      break;
    case tok::l_brace: {
      if (!tryToParseBracedList())
        parseChildBlock();
      break;
    }
    case tok::at:
      nextToken();
      if (FormatTok->Tok.is(tok::l_brace)) {
        nextToken();
        parseBracedList();
      }
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
}

void UnwrappedLineParser::parseIfThenElse() {
  assert(FormatTok->Tok.is(tok::kw_if) && "'if' expected");
  nextToken();
  if (FormatTok->Tok.is(tok::kw_constexpr))
    nextToken();
  if (FormatTok->Tok.is(tok::l_paren))
    parseParens();
  bool NeedsUnwrappedLine = false;
  if (FormatTok->Tok.is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    if (Style.BraceWrapping.BeforeElse)
      addUnwrappedLine();
    else
      NeedsUnwrappedLine = true;
  } else {
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }
  if (FormatTok->Tok.is(tok::kw_else)) {
    nextToken();
    if (FormatTok->Tok.is(tok::l_brace)) {
      CompoundStatementIndenter Indenter(this, Style, Line->Level);
      parseBlock(/*MustBeDeclaration=*/false);
      addUnwrappedLine();
    } else if (FormatTok->Tok.is(tok::kw_if)) {
      parseIfThenElse();
    } else {
      addUnwrappedLine();
      ++Line->Level;
      parseStructuralElement();
      if (FormatTok->is(tok::eof))
        addUnwrappedLine();
      --Line->Level;
    }
  } else if (NeedsUnwrappedLine) {
    addUnwrappedLine();
  }
}

void UnwrappedLineParser::parseTryCatch() {
  assert(FormatTok->isOneOf(tok::kw_try, tok::kw___try) && "'try' expected");
  nextToken();
  bool NeedsUnwrappedLine = false;
  if (FormatTok->is(tok::colon)) {
    // We are in a function try block, what comes is an initializer list.
    nextToken();
    while (FormatTok->is(tok::identifier)) {
      nextToken();
      if (FormatTok->is(tok::l_paren))
        parseParens();
      if (FormatTok->is(tok::comma))
        nextToken();
    }
  }
  // Parse try with resource.
  if (Style.Language == FormatStyle::LK_Java && FormatTok->is(tok::l_paren)) {
    parseParens();
  }
  if (FormatTok->is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    if (Style.BraceWrapping.BeforeCatch) {
      addUnwrappedLine();
    } else {
      NeedsUnwrappedLine = true;
    }
  } else if (!FormatTok->is(tok::kw_catch)) {
    // The C++ standard requires a compound-statement after a try.
    // If there's none, we try to assume there's a structuralElement
    // and try to continue.
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }
  while (1) {
    if (FormatTok->is(tok::at))
      nextToken();
    if (!(FormatTok->isOneOf(tok::kw_catch, Keywords.kw___except,
                             tok::kw___finally) ||
          ((Style.Language == FormatStyle::LK_Java ||
            Style.Language == FormatStyle::LK_JavaScript) &&
           FormatTok->is(Keywords.kw_finally)) ||
          (FormatTok->Tok.isObjCAtKeyword(tok::objc_catch) ||
           FormatTok->Tok.isObjCAtKeyword(tok::objc_finally))))
      break;
    nextToken();
    while (FormatTok->isNot(tok::l_brace)) {
      if (FormatTok->is(tok::l_paren)) {
        parseParens();
        continue;
      }
      if (FormatTok->isOneOf(tok::semi, tok::r_brace, tok::eof))
        return;
      nextToken();
    }
    NeedsUnwrappedLine = false;
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    if (Style.BraceWrapping.BeforeCatch)
      addUnwrappedLine();
    else
      NeedsUnwrappedLine = true;
  }
  if (NeedsUnwrappedLine)
    addUnwrappedLine();
}

void UnwrappedLineParser::parseNamespace() {
  assert(FormatTok->Tok.is(tok::kw_namespace) && "'namespace' expected");

  const FormatToken &InitialToken = *FormatTok;
  nextToken();
  while (FormatTok->isOneOf(tok::identifier, tok::coloncolon))
    nextToken();
  if (FormatTok->Tok.is(tok::l_brace)) {
    if (ShouldBreakBeforeBrace(Style, InitialToken))
      addUnwrappedLine();

    bool AddLevel = Style.NamespaceIndentation == FormatStyle::NI_All ||
                    (Style.NamespaceIndentation == FormatStyle::NI_Inner &&
                     DeclarationScopeStack.size() > 1);
    parseBlock(/*MustBeDeclaration=*/true, AddLevel);
    // Munch the semicolon after a namespace. This is more common than one would
    // think. Puttin the semicolon into its own line is very ugly.
    if (FormatTok->Tok.is(tok::semi))
      nextToken();
    addUnwrappedLine();
  }
  // FIXME: Add error handling.
}

void UnwrappedLineParser::parseNew() {
  assert(FormatTok->is(tok::kw_new) && "'new' expected");
  nextToken();
  if (Style.Language != FormatStyle::LK_Java)
    return;

  // In Java, we can parse everything up to the parens, which aren't optional.
  do {
    // There should not be a ;, { or } before the new's open paren.
    if (FormatTok->isOneOf(tok::semi, tok::l_brace, tok::r_brace))
      return;

    // Consume the parens.
    if (FormatTok->is(tok::l_paren)) {
      parseParens();

      // If there is a class body of an anonymous class, consume that as child.
      if (FormatTok->is(tok::l_brace))
        parseChildBlock();
      return;
    }
    nextToken();
  } while (!eof());
}

void UnwrappedLineParser::parseForOrWhileLoop() {
  assert(FormatTok->isOneOf(tok::kw_for, tok::kw_while, TT_ForEachMacro) &&
         "'for', 'while' or foreach macro expected");
  nextToken();
  // JS' for await ( ...
  if (Style.Language == FormatStyle::LK_JavaScript &&
      FormatTok->is(Keywords.kw_await))
    nextToken();
  if (FormatTok->Tok.is(tok::l_paren))
    parseParens();
  if (FormatTok->Tok.is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    addUnwrappedLine();
  } else {
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }
}

void UnwrappedLineParser::parseDoWhile() {
  assert(FormatTok->Tok.is(tok::kw_do) && "'do' expected");
  nextToken();
  if (FormatTok->Tok.is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    if (Style.BraceWrapping.IndentBraces)
      addUnwrappedLine();
  } else {
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }

  // FIXME: Add error handling.
  if (!FormatTok->Tok.is(tok::kw_while)) {
    addUnwrappedLine();
    return;
  }

  nextToken();
  parseStructuralElement();
}

void UnwrappedLineParser::parseLabel() {
  nextToken();
  unsigned OldLineLevel = Line->Level;
  if (Line->Level > 1 || (!Line->InPPDirective && Line->Level > 0))
    --Line->Level;
  if (CommentsBeforeNextToken.empty() && FormatTok->Tok.is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    if (FormatTok->Tok.is(tok::kw_break)) {
      if (Style.BraceWrapping.AfterControlStatement)
        addUnwrappedLine();
      parseStructuralElement();
    }
    addUnwrappedLine();
  } else {
    if (FormatTok->is(tok::semi))
      nextToken();
    addUnwrappedLine();
  }
  Line->Level = OldLineLevel;
  if (FormatTok->isNot(tok::l_brace)) {
    parseStructuralElement();
    addUnwrappedLine();
  }
}

void UnwrappedLineParser::parseCaseLabel() {
  assert(FormatTok->Tok.is(tok::kw_case) && "'case' expected");
  // FIXME: fix handling of complex expressions here.
  do {
    nextToken();
  } while (!eof() && !FormatTok->Tok.is(tok::colon));
  parseLabel();
}

void UnwrappedLineParser::parseSwitch() {
  assert(FormatTok->Tok.is(tok::kw_switch) && "'switch' expected");
  nextToken();
  if (FormatTok->Tok.is(tok::l_paren))
    parseParens();
  if (FormatTok->Tok.is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false);
    addUnwrappedLine();
  } else {
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }
}

void UnwrappedLineParser::parseAccessSpecifier() {
  nextToken();
  // Understand Qt's slots.
  if (FormatTok->isOneOf(Keywords.kw_slots, Keywords.kw_qslots))
    nextToken();
  // Otherwise, we don't know what it is, and we'd better keep the next token.
  if (FormatTok->Tok.is(tok::colon))
    nextToken();
  addUnwrappedLine();
}

bool UnwrappedLineParser::parseEnum() {
  // Won't be 'enum' for NS_ENUMs.
  if (FormatTok->Tok.is(tok::kw_enum))
    nextToken();

  // In TypeScript, "enum" can also be used as property name, e.g. in interface
  // declarations. An "enum" keyword followed by a colon would be a syntax
  // error and thus assume it is just an identifier.
  if (Style.Language == FormatStyle::LK_JavaScript &&
      FormatTok->isOneOf(tok::colon, tok::question))
    return false;

  // Eat up enum class ...
  if (FormatTok->Tok.is(tok::kw_class) || FormatTok->Tok.is(tok::kw_struct))
    nextToken();

  while (FormatTok->Tok.getIdentifierInfo() ||
         FormatTok->isOneOf(tok::colon, tok::coloncolon, tok::less,
                            tok::greater, tok::comma, tok::question)) {
    nextToken();
    // We can have macros or attributes in between 'enum' and the enum name.
    if (FormatTok->is(tok::l_paren))
      parseParens();
    if (FormatTok->is(tok::identifier)) {
      nextToken();
      // If there are two identifiers in a row, this is likely an elaborate
      // return type. In Java, this can be "implements", etc.
      if (Style.isCpp() && FormatTok->is(tok::identifier))
        return false;
    }
  }

  // Just a declaration or something is wrong.
  if (FormatTok->isNot(tok::l_brace))
    return true;
  FormatTok->BlockKind = BK_Block;

  if (Style.Language == FormatStyle::LK_Java) {
    // Java enums are different.
    parseJavaEnumBody();
    return true;
  }
  if (Style.Language == FormatStyle::LK_Proto) {
    parseBlock(/*MustBeDeclaration=*/true);
    return true;
  }

  // Parse enum body.
  nextToken();
  bool HasError = !parseBracedList(/*ContinueOnSemicolons=*/true);
  if (HasError) {
    if (FormatTok->is(tok::semi))
      nextToken();
    addUnwrappedLine();
  }
  return true;

  // There is no addUnwrappedLine() here so that we fall through to parsing a
  // structural element afterwards. Thus, in "enum A {} n, m;",
  // "} n, m;" will end up in one unwrapped line.
}

void UnwrappedLineParser::parseJavaEnumBody() {
  // Determine whether the enum is simple, i.e. does not have a semicolon or
  // constants with class bodies. Simple enums can be formatted like braced
  // lists, contracted to a single line, etc.
  unsigned StoredPosition = Tokens->getPosition();
  bool IsSimple = true;
  FormatToken *Tok = Tokens->getNextToken();
  while (Tok) {
    if (Tok->is(tok::r_brace))
      break;
    if (Tok->isOneOf(tok::l_brace, tok::semi)) {
      IsSimple = false;
      break;
    }
    // FIXME: This will also mark enums with braces in the arguments to enum
    // constants as "not simple". This is probably fine in practice, though.
    Tok = Tokens->getNextToken();
  }
  FormatTok = Tokens->setPosition(StoredPosition);

  if (IsSimple) {
    nextToken();
    parseBracedList();
    addUnwrappedLine();
    return;
  }

  // Parse the body of a more complex enum.
  // First add a line for everything up to the "{".
  nextToken();
  addUnwrappedLine();
  ++Line->Level;

  // Parse the enum constants.
  while (FormatTok) {
    if (FormatTok->is(tok::l_brace)) {
      // Parse the constant's class body.
      parseBlock(/*MustBeDeclaration=*/true, /*AddLevel=*/true,
                 /*MunchSemi=*/false);
    } else if (FormatTok->is(tok::l_paren)) {
      parseParens();
    } else if (FormatTok->is(tok::comma)) {
      nextToken();
      addUnwrappedLine();
    } else if (FormatTok->is(tok::semi)) {
      nextToken();
      addUnwrappedLine();
      break;
    } else if (FormatTok->is(tok::r_brace)) {
      addUnwrappedLine();
      break;
    } else {
      nextToken();
    }
  }

  // Parse the class body after the enum's ";" if any.
  parseLevel(/*HasOpeningBrace=*/true);
  nextToken();
  --Line->Level;
  addUnwrappedLine();
}

void UnwrappedLineParser::parseRecord(bool ParseAsExpr) {
  const FormatToken &InitialToken = *FormatTok;
  nextToken();

  // The actual identifier can be a nested name specifier, and in macros
  // it is often token-pasted.
  while (FormatTok->isOneOf(tok::identifier, tok::coloncolon, tok::hashhash,
                            tok::kw___attribute, tok::kw___declspec,
                            tok::kw_alignas) ||
         ((Style.Language == FormatStyle::LK_Java ||
           Style.Language == FormatStyle::LK_JavaScript) &&
          FormatTok->isOneOf(tok::period, tok::comma))) {
    if (Style.Language == FormatStyle::LK_JavaScript &&
        FormatTok->isOneOf(Keywords.kw_extends, Keywords.kw_implements)) {
      // JavaScript/TypeScript supports inline object types in
      // extends/implements positions:
      //     class Foo implements {bar: number} { }
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        tryToParseBracedList();
        continue;
      }
    }
    bool IsNonMacroIdentifier =
        FormatTok->is(tok::identifier) &&
        FormatTok->TokenText != FormatTok->TokenText.upper();
    nextToken();
    // We can have macros or attributes in between 'class' and the class name.
    if (!IsNonMacroIdentifier && FormatTok->Tok.is(tok::l_paren))
      parseParens();
  }

  // Note that parsing away template declarations here leads to incorrectly
  // accepting function declarations as record declarations.
  // In general, we cannot solve this problem. Consider:
  // class A<int> B() {}
  // which can be a function definition or a class definition when B() is a
  // macro. If we find enough real-world cases where this is a problem, we
  // can parse for the 'template' keyword in the beginning of the statement,
  // and thus rule out the record production in case there is no template
  // (this would still leave us with an ambiguity between template function
  // and class declarations).
  if (FormatTok->isOneOf(tok::colon, tok::less)) {
    while (!eof()) {
      if (FormatTok->is(tok::l_brace)) {
        calculateBraceTypes(/*ExpectClassBody=*/true);
        if (!tryToParseBracedList())
          break;
      }
      if (FormatTok->Tok.is(tok::semi))
        return;
      nextToken();
    }
  }
  if (FormatTok->Tok.is(tok::l_brace)) {
    if (ParseAsExpr) {
      parseChildBlock();
    } else {
      if (ShouldBreakBeforeBrace(Style, InitialToken))
        addUnwrappedLine();

      parseBlock(/*MustBeDeclaration=*/true, /*AddLevel=*/true,
                 /*MunchSemi=*/false);
    }
  }
  // There is no addUnwrappedLine() here so that we fall through to parsing a
  // structural element afterwards. Thus, in "class A {} n, m;",
  // "} n, m;" will end up in one unwrapped line.
}

void UnwrappedLineParser::parseObjCMethod() {
  assert(FormatTok->Tok.isOneOf(tok::l_paren, tok::identifier) &&
         "'(' or identifier expected.");
  do {
    if (FormatTok->Tok.is(tok::semi)) {
      nextToken();
      addUnwrappedLine();
      return;
    } else if (FormatTok->Tok.is(tok::l_brace)) {
      if (Style.BraceWrapping.AfterFunction)
        addUnwrappedLine();
      parseBlock(/*MustBeDeclaration=*/false);
      addUnwrappedLine();
      return;
    } else {
      nextToken();
    }
  } while (!eof());
}

void UnwrappedLineParser::parseObjCProtocolList() {
  assert(FormatTok->Tok.is(tok::less) && "'<' expected.");
  do {
    nextToken();
    // Early exit in case someone forgot a close angle.
    if (FormatTok->isOneOf(tok::semi, tok::l_brace) ||
        FormatTok->Tok.isObjCAtKeyword(tok::objc_end))
      return;
  } while (!eof() && FormatTok->Tok.isNot(tok::greater));
  nextToken(); // Skip '>'.
}

void UnwrappedLineParser::parseObjCUntilAtEnd() {
  do {
    if (FormatTok->Tok.isObjCAtKeyword(tok::objc_end)) {
      nextToken();
      addUnwrappedLine();
      break;
    }
    if (FormatTok->is(tok::l_brace)) {
      parseBlock(/*MustBeDeclaration=*/false);
      // In ObjC interfaces, nothing should be following the "}".
      addUnwrappedLine();
    } else if (FormatTok->is(tok::r_brace)) {
      // Ignore stray "}". parseStructuralElement doesn't consume them.
      nextToken();
      addUnwrappedLine();
    } else if (FormatTok->isOneOf(tok::minus, tok::plus)) {
      nextToken();
      parseObjCMethod();
    } else {
      parseStructuralElement();
    }
  } while (!eof());
}

void UnwrappedLineParser::parseObjCInterfaceOrImplementation() {
  assert(FormatTok->Tok.getObjCKeywordID() == tok::objc_interface ||
         FormatTok->Tok.getObjCKeywordID() == tok::objc_implementation);
  nextToken();
  nextToken(); // interface name

  // @interface can be followed by a lightweight generic
  // specialization list, then either a base class or a category.
  if (FormatTok->Tok.is(tok::less)) {
    // Unlike protocol lists, generic parameterizations support
    // nested angles:
    //
    // @interface Foo<ValueType : id <NSCopying, NSSecureCoding>> :
    //     NSObject <NSCopying, NSSecureCoding>
    //
    // so we need to count how many open angles we have left.
    unsigned NumOpenAngles = 1;
    do {
      nextToken();
      // Early exit in case someone forgot a close angle.
      if (FormatTok->isOneOf(tok::semi, tok::l_brace) ||
          FormatTok->Tok.isObjCAtKeyword(tok::objc_end))
        break;
      if (FormatTok->Tok.is(tok::less))
        ++NumOpenAngles;
      else if (FormatTok->Tok.is(tok::greater)) {
        assert(NumOpenAngles > 0 && "'>' makes NumOpenAngles negative");
        --NumOpenAngles;
      }
    } while (!eof() && NumOpenAngles != 0);
    nextToken(); // Skip '>'.
  }
  if (FormatTok->Tok.is(tok::colon)) {
    nextToken();
    nextToken(); // base class name
  } else if (FormatTok->Tok.is(tok::l_paren))
    // Skip category, if present.
    parseParens();

  if (FormatTok->Tok.is(tok::less))
    parseObjCProtocolList();

  if (FormatTok->Tok.is(tok::l_brace)) {
    if (Style.BraceWrapping.AfterObjCDeclaration)
      addUnwrappedLine();
    parseBlock(/*MustBeDeclaration=*/true);
  }

  // With instance variables, this puts '}' on its own line.  Without instance
  // variables, this ends the @interface line.
  addUnwrappedLine();

  parseObjCUntilAtEnd();
}

// Returns true for the declaration/definition form of @protocol,
// false for the expression form.
bool UnwrappedLineParser::parseObjCProtocol() {
  assert(FormatTok->Tok.getObjCKeywordID() == tok::objc_protocol);
  nextToken();

  if (FormatTok->is(tok::l_paren))
    // The expression form of @protocol, e.g. "Protocol* p = @protocol(foo);".
    return false;

  // The definition/declaration form,
  // @protocol Foo
  // - (int)someMethod;
  // @end

  nextToken(); // protocol name

  if (FormatTok->Tok.is(tok::less))
    parseObjCProtocolList();

  // Check for protocol declaration.
  if (FormatTok->Tok.is(tok::semi)) {
    nextToken();
    addUnwrappedLine();
    return true;
  }

  addUnwrappedLine();
  parseObjCUntilAtEnd();
  return true;
}

void UnwrappedLineParser::parseJavaScriptEs6ImportExport() {
  bool IsImport = FormatTok->is(Keywords.kw_import);
  assert(IsImport || FormatTok->is(tok::kw_export));
  nextToken();

  // Consume the "default" in "export default class/function".
  if (FormatTok->is(tok::kw_default))
    nextToken();

  // Consume "async function", "function" and "default function", so that these
  // get parsed as free-standing JS functions, i.e. do not require a trailing
  // semicolon.
  if (FormatTok->is(Keywords.kw_async))
    nextToken();
  if (FormatTok->is(Keywords.kw_function)) {
    nextToken();
    return;
  }

  // For imports, `export *`, `export {...}`, consume the rest of the line up
  // to the terminating `;`. For everything else, just return and continue
  // parsing the structural element, i.e. the declaration or expression for
  // `export default`.
  if (!IsImport && !FormatTok->isOneOf(tok::l_brace, tok::star) &&
      !FormatTok->isStringLiteral())
    return;

  while (!eof()) {
    if (FormatTok->is(tok::semi))
      return;
    if (Line->Tokens.empty()) {
      // Common issue: Automatic Semicolon Insertion wrapped the line, so the
      // import statement should terminate.
      return;
    }
    if (FormatTok->is(tok::l_brace)) {
      FormatTok->BlockKind = BK_Block;
      nextToken();
      parseBracedList();
    } else {
      nextToken();
    }
  }
}

void UnwrappedLineParser::parseStatementMacro()
{
  nextToken();
  if (FormatTok->is(tok::l_paren))
    parseParens();
  if (FormatTok->is(tok::semi))
    nextToken();
  addUnwrappedLine();
}

LLVM_ATTRIBUTE_UNUSED static void printDebugInfo(const UnwrappedLine &Line,
                                                 StringRef Prefix = "") {
  llvm::dbgs() << Prefix << "Line(" << Line.Level
               << ", FSC=" << Line.FirstStartColumn << ")"
               << (Line.InPPDirective ? " MACRO" : "") << ": ";
  for (std::list<UnwrappedLineNode>::const_iterator I = Line.Tokens.begin(),
                                                    E = Line.Tokens.end();
       I != E; ++I) {
    llvm::dbgs() << I->Tok->Tok.getName() << "["
                 << "T=" << I->Tok->Type << ", OC=" << I->Tok->OriginalColumn
                 << "] ";
  }
  for (std::list<UnwrappedLineNode>::const_iterator I = Line.Tokens.begin(),
                                                    E = Line.Tokens.end();
       I != E; ++I) {
    const UnwrappedLineNode &Node = *I;
    for (SmallVectorImpl<UnwrappedLine>::const_iterator
             I = Node.Children.begin(),
             E = Node.Children.end();
         I != E; ++I) {
      printDebugInfo(*I, "\nChild: ");
    }
  }
  llvm::dbgs() << "\n";
}

void UnwrappedLineParser::addUnwrappedLine() {
  if (Line->Tokens.empty())
    return;
  LLVM_DEBUG({
    if (CurrentLines == &Lines)
      printDebugInfo(*Line);
  });
  CurrentLines->push_back(std::move(*Line));
  Line->Tokens.clear();
  Line->MatchingOpeningBlockLineIndex = UnwrappedLine::kInvalidIndex;
  Line->FirstStartColumn = 0;
  if (CurrentLines == &Lines && !PreprocessorDirectives.empty()) {
    CurrentLines->append(
        std::make_move_iterator(PreprocessorDirectives.begin()),
        std::make_move_iterator(PreprocessorDirectives.end()));
    PreprocessorDirectives.clear();
  }
  // Disconnect the current token from the last token on the previous line.
  FormatTok->Previous = nullptr;
}

bool UnwrappedLineParser::eof() const { return FormatTok->Tok.is(tok::eof); }

bool UnwrappedLineParser::isOnNewLine(const FormatToken &FormatTok) {
  return (Line->InPPDirective || FormatTok.HasUnescapedNewline) &&
         FormatTok.NewlinesBefore > 0;
}

// Checks if \p FormatTok is a line comment that continues the line comment
// section on \p Line.
static bool continuesLineCommentSection(const FormatToken &FormatTok,
                                        const UnwrappedLine &Line,
                                        llvm::Regex &CommentPragmasRegex) {
  if (Line.Tokens.empty())
    return false;

  StringRef IndentContent = FormatTok.TokenText;
  if (FormatTok.TokenText.startswith("//") ||
      FormatTok.TokenText.startswith("/*"))
    IndentContent = FormatTok.TokenText.substr(2);
  if (CommentPragmasRegex.match(IndentContent))
    return false;

  // If Line starts with a line comment, then FormatTok continues the comment
  // section if its original column is greater or equal to the original start
  // column of the line.
  //
  // Define the min column token of a line as follows: if a line ends in '{' or
  // contains a '{' followed by a line comment, then the min column token is
  // that '{'. Otherwise, the min column token of the line is the first token of
  // the line.
  //
  // If Line starts with a token other than a line comment, then FormatTok
  // continues the comment section if its original column is greater than the
  // original start column of the min column token of the line.
  //
  // For example, the second line comment continues the first in these cases:
  //
  // // first line
  // // second line
  //
  // and:
  //
  // // first line
  //  // second line
  //
  // and:
  //
  // int i; // first line
  //  // second line
  //
  // and:
  //
  // do { // first line
  //      // second line
  //   int i;
  // } while (true);
  //
  // and:
  //
  // enum {
  //   a, // first line
  //    // second line
  //   b
  // };
  //
  // The second line comment doesn't continue the first in these cases:
  //
  //   // first line
  //  // second line
  //
  // and:
  //
  // int i; // first line
  // // second line
  //
  // and:
  //
  // do { // first line
  //   // second line
  //   int i;
  // } while (true);
  //
  // and:
  //
  // enum {
  //   a, // first line
  //   // second line
  // };
  const FormatToken *MinColumnToken = Line.Tokens.front().Tok;

  // Scan for '{//'. If found, use the column of '{' as a min column for line
  // comment section continuation.
  const FormatToken *PreviousToken = nullptr;
  for (const UnwrappedLineNode &Node : Line.Tokens) {
    if (PreviousToken && PreviousToken->is(tok::l_brace) &&
        isLineComment(*Node.Tok)) {
      MinColumnToken = PreviousToken;
      break;
    }
    PreviousToken = Node.Tok;

    // Grab the last newline preceding a token in this unwrapped line.
    if (Node.Tok->NewlinesBefore > 0) {
      MinColumnToken = Node.Tok;
    }
  }
  if (PreviousToken && PreviousToken->is(tok::l_brace)) {
    MinColumnToken = PreviousToken;
  }

  return continuesLineComment(FormatTok, /*Previous=*/Line.Tokens.back().Tok,
                              MinColumnToken);
}

void UnwrappedLineParser::flushComments(bool NewlineBeforeNext) {
  bool JustComments = Line->Tokens.empty();
  for (SmallVectorImpl<FormatToken *>::const_iterator
           I = CommentsBeforeNextToken.begin(),
           E = CommentsBeforeNextToken.end();
       I != E; ++I) {
    // Line comments that belong to the same line comment section are put on the
    // same line since later we might want to reflow content between them.
    // Additional fine-grained breaking of line comment sections is controlled
    // by the class BreakableLineCommentSection in case it is desirable to keep
    // several line comment sections in the same unwrapped line.
    //
    // FIXME: Consider putting separate line comment sections as children to the
    // unwrapped line instead.
    (*I)->ContinuesLineCommentSection =
        continuesLineCommentSection(**I, *Line, CommentPragmasRegex);
    if (isOnNewLine(**I) && JustComments && !(*I)->ContinuesLineCommentSection)
      addUnwrappedLine();
    pushToken(*I);
  }
  if (NewlineBeforeNext && JustComments)
    addUnwrappedLine();
  CommentsBeforeNextToken.clear();
}

void UnwrappedLineParser::nextToken(int LevelDifference) {
  if (eof())
    return;
  flushComments(isOnNewLine(*FormatTok));
  pushToken(FormatTok);
  FormatToken *Previous = FormatTok;
  if (Style.Language != FormatStyle::LK_JavaScript)
    readToken(LevelDifference);
  else
    readTokenWithJavaScriptASI();
  FormatTok->Previous = Previous;
}

void UnwrappedLineParser::distributeComments(
    const SmallVectorImpl<FormatToken *> &Comments,
    const FormatToken *NextTok) {
  // Whether or not a line comment token continues a line is controlled by
  // the method continuesLineCommentSection, with the following caveat:
  //
  // Define a trail of Comments to be a nonempty proper postfix of Comments such
  // that each comment line from the trail is aligned with the next token, if
  // the next token exists. If a trail exists, the beginning of the maximal
  // trail is marked as a start of a new comment section.
  //
  // For example in this code:
  //
  // int a; // line about a
  //   // line 1 about b
  //   // line 2 about b
  //   int b;
  //
  // the two lines about b form a maximal trail, so there are two sections, the
  // first one consisting of the single comment "// line about a" and the
  // second one consisting of the next two comments.
  if (Comments.empty())
    return;
  bool ShouldPushCommentsInCurrentLine = true;
  bool HasTrailAlignedWithNextToken = false;
  unsigned StartOfTrailAlignedWithNextToken = 0;
  if (NextTok) {
    // We are skipping the first element intentionally.
    for (unsigned i = Comments.size() - 1; i > 0; --i) {
      if (Comments[i]->OriginalColumn == NextTok->OriginalColumn) {
        HasTrailAlignedWithNextToken = true;
        StartOfTrailAlignedWithNextToken = i;
      }
    }
  }
  for (unsigned i = 0, e = Comments.size(); i < e; ++i) {
    FormatToken *FormatTok = Comments[i];
    if (HasTrailAlignedWithNextToken && i == StartOfTrailAlignedWithNextToken) {
      FormatTok->ContinuesLineCommentSection = false;
    } else {
      FormatTok->ContinuesLineCommentSection =
          continuesLineCommentSection(*FormatTok, *Line, CommentPragmasRegex);
    }
    if (!FormatTok->ContinuesLineCommentSection &&
        (isOnNewLine(*FormatTok) || FormatTok->IsFirst)) {
      ShouldPushCommentsInCurrentLine = false;
    }
    if (ShouldPushCommentsInCurrentLine) {
      pushToken(FormatTok);
    } else {
      CommentsBeforeNextToken.push_back(FormatTok);
    }
  }
}

void UnwrappedLineParser::readToken(int LevelDifference) {
  SmallVector<FormatToken *, 1> Comments;
  do {
    FormatTok = Tokens->getNextToken();
    assert(FormatTok);
    while (!Line->InPPDirective && FormatTok->Tok.is(tok::hash) &&
           (FormatTok->HasUnescapedNewline || FormatTok->IsFirst)) {
      distributeComments(Comments, FormatTok);
      Comments.clear();
      // If there is an unfinished unwrapped line, we flush the preprocessor
      // directives only after that unwrapped line was finished later.
      bool SwitchToPreprocessorLines = !Line->Tokens.empty();
      ScopedLineState BlockState(*this, SwitchToPreprocessorLines);
      assert((LevelDifference >= 0 ||
              static_cast<unsigned>(-LevelDifference) <= Line->Level) &&
             "LevelDifference makes Line->Level negative");
      Line->Level += LevelDifference;
      // Comments stored before the preprocessor directive need to be output
      // before the preprocessor directive, at the same level as the
      // preprocessor directive, as we consider them to apply to the directive.
      flushComments(isOnNewLine(*FormatTok));
      parsePPDirective();
    }
    while (FormatTok->Type == TT_ConflictStart ||
           FormatTok->Type == TT_ConflictEnd ||
           FormatTok->Type == TT_ConflictAlternative) {
      if (FormatTok->Type == TT_ConflictStart) {
        conditionalCompilationStart(/*Unreachable=*/false);
      } else if (FormatTok->Type == TT_ConflictAlternative) {
        conditionalCompilationAlternative();
      } else if (FormatTok->Type == TT_ConflictEnd) {
        conditionalCompilationEnd();
      }
      FormatTok = Tokens->getNextToken();
      FormatTok->MustBreakBefore = true;
    }

    if (!PPStack.empty() && (PPStack.back().Kind == PP_Unreachable) &&
        !Line->InPPDirective) {
      continue;
    }

    if (!FormatTok->Tok.is(tok::comment)) {
      distributeComments(Comments, FormatTok);
      Comments.clear();
      return;
    }

    Comments.push_back(FormatTok);
  } while (!eof());

  distributeComments(Comments, nullptr);
  Comments.clear();
}

void UnwrappedLineParser::pushToken(FormatToken *Tok) {
  Line->Tokens.push_back(UnwrappedLineNode(Tok));
  if (MustBreakBeforeNextToken) {
    Line->Tokens.back().Tok->MustBreakBefore = true;
    MustBreakBeforeNextToken = false;
  }
}

} // end namespace format
} // end namespace clang
