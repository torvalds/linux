//===--- UnwrappedLineParser.h - Format C++ code ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the UnwrappedLineParser,
/// which turns a stream of tokens into UnwrappedLines.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_UNWRAPPEDLINEPARSER_H
#define LLVM_CLANG_LIB_FORMAT_UNWRAPPEDLINEPARSER_H

#include "FormatToken.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Format/Format.h"
#include "llvm/Support/Regex.h"
#include <list>
#include <stack>

namespace clang {
namespace format {

struct UnwrappedLineNode;

/// An unwrapped line is a sequence of \c Token, that we would like to
/// put on a single line if there was no column limit.
///
/// This is used as a main interface between the \c UnwrappedLineParser and the
/// \c UnwrappedLineFormatter. The key property is that changing the formatting
/// within an unwrapped line does not affect any other unwrapped lines.
struct UnwrappedLine {
  UnwrappedLine();

  // FIXME: Don't use std::list here.
  /// The \c Tokens comprising this \c UnwrappedLine.
  std::list<UnwrappedLineNode> Tokens;

  /// The indent level of the \c UnwrappedLine.
  unsigned Level;

  /// Whether this \c UnwrappedLine is part of a preprocessor directive.
  bool InPPDirective;

  bool MustBeDeclaration;

  /// If this \c UnwrappedLine closes a block in a sequence of lines,
  /// \c MatchingOpeningBlockLineIndex stores the index of the corresponding
  /// opening line. Otherwise, \c MatchingOpeningBlockLineIndex must be
  /// \c kInvalidIndex.
  size_t MatchingOpeningBlockLineIndex = kInvalidIndex;

  /// If this \c UnwrappedLine opens a block, stores the index of the
  /// line with the corresponding closing brace.
  size_t MatchingClosingBlockLineIndex = kInvalidIndex;

  static const size_t kInvalidIndex = -1;

  unsigned FirstStartColumn = 0;
};

class UnwrappedLineConsumer {
public:
  virtual ~UnwrappedLineConsumer() {}
  virtual void consumeUnwrappedLine(const UnwrappedLine &Line) = 0;
  virtual void finishRun() = 0;
};

class FormatTokenSource;

class UnwrappedLineParser {
public:
  UnwrappedLineParser(const FormatStyle &Style,
                      const AdditionalKeywords &Keywords,
                      unsigned FirstStartColumn,
                      ArrayRef<FormatToken *> Tokens,
                      UnwrappedLineConsumer &Callback);

  void parse();

private:
  void reset();
  void parseFile();
  void parseLevel(bool HasOpeningBrace);
  void parseBlock(bool MustBeDeclaration, bool AddLevel = true,
                  bool MunchSemi = true);
  void parseChildBlock();
  void parsePPDirective();
  void parsePPDefine();
  void parsePPIf(bool IfDef);
  void parsePPElIf();
  void parsePPElse();
  void parsePPEndIf();
  void parsePPUnknown();
  void readTokenWithJavaScriptASI();
  void parseStructuralElement();
  bool tryToParseBracedList();
  bool parseBracedList(bool ContinueOnSemicolons = false,
                       tok::TokenKind ClosingBraceKind = tok::r_brace);
  void parseParens();
  void parseSquare(bool LambdaIntroducer = false);
  void parseIfThenElse();
  void parseTryCatch();
  void parseForOrWhileLoop();
  void parseDoWhile();
  void parseLabel();
  void parseCaseLabel();
  void parseSwitch();
  void parseNamespace();
  void parseNew();
  void parseAccessSpecifier();
  bool parseEnum();
  void parseJavaEnumBody();
  // Parses a record (aka class) as a top level element. If ParseAsExpr is true,
  // parses the record as a child block, i.e. if the class declaration is an
  // expression.
  void parseRecord(bool ParseAsExpr = false);
  void parseObjCMethod();
  void parseObjCProtocolList();
  void parseObjCUntilAtEnd();
  void parseObjCInterfaceOrImplementation();
  bool parseObjCProtocol();
  void parseJavaScriptEs6ImportExport();
  void parseStatementMacro();
  bool tryToParseLambda();
  bool tryToParseLambdaIntroducer();
  void tryToParseJSFunction();
  void addUnwrappedLine();
  bool eof() const;
  // LevelDifference is the difference of levels after and before the current
  // token. For example:
  // - if the token is '{' and opens a block, LevelDifference is 1.
  // - if the token is '}' and closes a block, LevelDifference is -1.
  void nextToken(int LevelDifference = 0);
  void readToken(int LevelDifference = 0);

  // Decides which comment tokens should be added to the current line and which
  // should be added as comments before the next token.
  //
  // Comments specifies the sequence of comment tokens to analyze. They get
  // either pushed to the current line or added to the comments before the next
  // token.
  //
  // NextTok specifies the next token. A null pointer NextTok is supported, and
  // signifies either the absence of a next token, or that the next token
  // shouldn't be taken into accunt for the analysis.
  void distributeComments(const SmallVectorImpl<FormatToken *> &Comments,
                          const FormatToken *NextTok);

  // Adds the comment preceding the next token to unwrapped lines.
  void flushComments(bool NewlineBeforeNext);
  void pushToken(FormatToken *Tok);
  void calculateBraceTypes(bool ExpectClassBody = false);

  // Marks a conditional compilation edge (for example, an '#if', '#ifdef',
  // '#else' or merge conflict marker). If 'Unreachable' is true, assumes
  // this branch either cannot be taken (for example '#if false'), or should
  // not be taken in this round.
  void conditionalCompilationCondition(bool Unreachable);
  void conditionalCompilationStart(bool Unreachable);
  void conditionalCompilationAlternative();
  void conditionalCompilationEnd();

  bool isOnNewLine(const FormatToken &FormatTok);

  // Compute hash of the current preprocessor branch.
  // This is used to identify the different branches, and thus track if block
  // open and close in the same branch.
  size_t computePPHash() const;

  // FIXME: We are constantly running into bugs where Line.Level is incorrectly
  // subtracted from beyond 0. Introduce a method to subtract from Line.Level
  // and use that everywhere in the Parser.
  std::unique_ptr<UnwrappedLine> Line;

  // Comments are sorted into unwrapped lines by whether they are in the same
  // line as the previous token, or not. If not, they belong to the next token.
  // Since the next token might already be in a new unwrapped line, we need to
  // store the comments belonging to that token.
  SmallVector<FormatToken *, 1> CommentsBeforeNextToken;
  FormatToken *FormatTok;
  bool MustBreakBeforeNextToken;

  // The parsed lines. Only added to through \c CurrentLines.
  SmallVector<UnwrappedLine, 8> Lines;

  // Preprocessor directives are parsed out-of-order from other unwrapped lines.
  // Thus, we need to keep a list of preprocessor directives to be reported
  // after an unwrapped line that has been started was finished.
  SmallVector<UnwrappedLine, 4> PreprocessorDirectives;

  // New unwrapped lines are added via CurrentLines.
  // Usually points to \c &Lines. While parsing a preprocessor directive when
  // there is an unfinished previous unwrapped line, will point to
  // \c &PreprocessorDirectives.
  SmallVectorImpl<UnwrappedLine> *CurrentLines;

  // We store for each line whether it must be a declaration depending on
  // whether we are in a compound statement or not.
  std::vector<bool> DeclarationScopeStack;

  const FormatStyle &Style;
  const AdditionalKeywords &Keywords;

  llvm::Regex CommentPragmasRegex;

  FormatTokenSource *Tokens;
  UnwrappedLineConsumer &Callback;

  // FIXME: This is a temporary measure until we have reworked the ownership
  // of the format tokens. The goal is to have the actual tokens created and
  // owned outside of and handed into the UnwrappedLineParser.
  ArrayRef<FormatToken *> AllTokens;

  // Represents preprocessor branch type, so we can find matching
  // #if/#else/#endif directives.
  enum PPBranchKind {
    PP_Conditional, // Any #if, #ifdef, #ifndef, #elif, block outside #if 0
    PP_Unreachable  // #if 0 or a conditional preprocessor block inside #if 0
  };

  struct PPBranch {
    PPBranch(PPBranchKind Kind, size_t Line) : Kind(Kind), Line(Line) {}
    PPBranchKind Kind;
    size_t Line;
  };

  // Keeps a stack of currently active preprocessor branching directives.
  SmallVector<PPBranch, 16> PPStack;

  // The \c UnwrappedLineParser re-parses the code for each combination
  // of preprocessor branches that can be taken.
  // To that end, we take the same branch (#if, #else, or one of the #elif
  // branches) for each nesting level of preprocessor branches.
  // \c PPBranchLevel stores the current nesting level of preprocessor
  // branches during one pass over the code.
  int PPBranchLevel;

  // Contains the current branch (#if, #else or one of the #elif branches)
  // for each nesting level.
  SmallVector<int, 8> PPLevelBranchIndex;

  // Contains the maximum number of branches at each nesting level.
  SmallVector<int, 8> PPLevelBranchCount;

  // Contains the number of branches per nesting level we are currently
  // in while parsing a preprocessor branch sequence.
  // This is used to update PPLevelBranchCount at the end of a branch
  // sequence.
  std::stack<int> PPChainBranchIndex;

  // Include guard search state. Used to fixup preprocessor indent levels
  // so that include guards do not participate in indentation.
  enum IncludeGuardState {
    IG_Inited,   // Search started, looking for #ifndef.
    IG_IfNdefed, // #ifndef found, IncludeGuardToken points to condition.
    IG_Defined,  // Matching #define found, checking other requirements.
    IG_Found,    // All requirements met, need to fix indents.
    IG_Rejected, // Search failed or never started.
  };

  // Current state of include guard search.
  IncludeGuardState IncludeGuard;

  // Points to the #ifndef condition for a potential include guard. Null unless
  // IncludeGuardState == IG_IfNdefed.
  FormatToken *IncludeGuardToken;

  // Contains the first start column where the source begins. This is zero for
  // normal source code and may be nonzero when formatting a code fragment that
  // does not start at the beginning of the file.
  unsigned FirstStartColumn;

  friend class ScopedLineState;
  friend class CompoundStatementIndenter;
};

struct UnwrappedLineNode {
  UnwrappedLineNode() : Tok(nullptr) {}
  UnwrappedLineNode(FormatToken *Tok) : Tok(Tok) {}

  FormatToken *Tok;
  SmallVector<UnwrappedLine, 0> Children;
};

inline UnwrappedLine::UnwrappedLine()
    : Level(0), InPPDirective(false), MustBeDeclaration(false),
      MatchingOpeningBlockLineIndex(kInvalidIndex) {}

} // end namespace format
} // end namespace clang

#endif
