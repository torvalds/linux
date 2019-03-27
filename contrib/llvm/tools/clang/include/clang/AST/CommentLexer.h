//===--- CommentLexer.h - Lexer for structured comments ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines lexer for structured comments and supporting token class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_COMMENTLEXER_H
#define LLVM_CLANG_AST_COMMENTLEXER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {
namespace comments {

class Lexer;
class TextTokenRetokenizer;
struct CommandInfo;
class CommandTraits;

namespace tok {
enum TokenKind {
  eof,
  newline,
  text,
  unknown_command,   // Command that does not have an ID.
  backslash_command, // Command with an ID, that used backslash marker.
  at_command,        // Command with an ID, that used 'at' marker.
  verbatim_block_begin,
  verbatim_block_line,
  verbatim_block_end,
  verbatim_line_name,
  verbatim_line_text,
  html_start_tag,     // <tag
  html_ident,         // attr
  html_equals,        // =
  html_quoted_string, // "blah\"blah" or 'blah\'blah'
  html_greater,       // >
  html_slash_greater, // />
  html_end_tag        // </tag
};
} // end namespace tok

/// Comment token.
class Token {
  friend class Lexer;
  friend class TextTokenRetokenizer;

  /// The location of the token.
  SourceLocation Loc;

  /// The actual kind of the token.
  tok::TokenKind Kind;

  /// Length of the token spelling in comment.  Can be 0 for synthenized
  /// tokens.
  unsigned Length;

  /// Contains text value associated with a token.
  const char *TextPtr;

  /// Integer value associated with a token.
  ///
  /// If the token is a known command, contains command ID and TextPtr is
  /// unused (command spelling can be found with CommandTraits).  Otherwise,
  /// contains the length of the string that starts at TextPtr.
  unsigned IntVal;

public:
  SourceLocation getLocation() const LLVM_READONLY { return Loc; }
  void setLocation(SourceLocation SL) { Loc = SL; }

  SourceLocation getEndLocation() const LLVM_READONLY {
    if (Length == 0 || Length == 1)
      return Loc;
    return Loc.getLocWithOffset(Length - 1);
  }

  tok::TokenKind getKind() const LLVM_READONLY { return Kind; }
  void setKind(tok::TokenKind K) { Kind = K; }

  bool is(tok::TokenKind K) const LLVM_READONLY { return Kind == K; }
  bool isNot(tok::TokenKind K) const LLVM_READONLY { return Kind != K; }

  unsigned getLength() const LLVM_READONLY { return Length; }
  void setLength(unsigned L) { Length = L; }

  StringRef getText() const LLVM_READONLY {
    assert(is(tok::text));
    return StringRef(TextPtr, IntVal);
  }

  void setText(StringRef Text) {
    assert(is(tok::text));
    TextPtr = Text.data();
    IntVal = Text.size();
  }

  StringRef getUnknownCommandName() const LLVM_READONLY {
    assert(is(tok::unknown_command));
    return StringRef(TextPtr, IntVal);
  }

  void setUnknownCommandName(StringRef Name) {
    assert(is(tok::unknown_command));
    TextPtr = Name.data();
    IntVal = Name.size();
  }

  unsigned getCommandID() const LLVM_READONLY {
    assert(is(tok::backslash_command) || is(tok::at_command));
    return IntVal;
  }

  void setCommandID(unsigned ID) {
    assert(is(tok::backslash_command) || is(tok::at_command));
    IntVal = ID;
  }

  unsigned getVerbatimBlockID() const LLVM_READONLY {
    assert(is(tok::verbatim_block_begin) || is(tok::verbatim_block_end));
    return IntVal;
  }

  void setVerbatimBlockID(unsigned ID) {
    assert(is(tok::verbatim_block_begin) || is(tok::verbatim_block_end));
    IntVal = ID;
  }

  StringRef getVerbatimBlockText() const LLVM_READONLY {
    assert(is(tok::verbatim_block_line));
    return StringRef(TextPtr, IntVal);
  }

  void setVerbatimBlockText(StringRef Text) {
    assert(is(tok::verbatim_block_line));
    TextPtr = Text.data();
    IntVal = Text.size();
  }

  unsigned getVerbatimLineID() const LLVM_READONLY {
    assert(is(tok::verbatim_line_name));
    return IntVal;
  }

  void setVerbatimLineID(unsigned ID) {
    assert(is(tok::verbatim_line_name));
    IntVal = ID;
  }

  StringRef getVerbatimLineText() const LLVM_READONLY {
    assert(is(tok::verbatim_line_text));
    return StringRef(TextPtr, IntVal);
  }

  void setVerbatimLineText(StringRef Text) {
    assert(is(tok::verbatim_line_text));
    TextPtr = Text.data();
    IntVal = Text.size();
  }

  StringRef getHTMLTagStartName() const LLVM_READONLY {
    assert(is(tok::html_start_tag));
    return StringRef(TextPtr, IntVal);
  }

  void setHTMLTagStartName(StringRef Name) {
    assert(is(tok::html_start_tag));
    TextPtr = Name.data();
    IntVal = Name.size();
  }

  StringRef getHTMLIdent() const LLVM_READONLY {
    assert(is(tok::html_ident));
    return StringRef(TextPtr, IntVal);
  }

  void setHTMLIdent(StringRef Name) {
    assert(is(tok::html_ident));
    TextPtr = Name.data();
    IntVal = Name.size();
  }

  StringRef getHTMLQuotedString() const LLVM_READONLY {
    assert(is(tok::html_quoted_string));
    return StringRef(TextPtr, IntVal);
  }

  void setHTMLQuotedString(StringRef Str) {
    assert(is(tok::html_quoted_string));
    TextPtr = Str.data();
    IntVal = Str.size();
  }

  StringRef getHTMLTagEndName() const LLVM_READONLY {
    assert(is(tok::html_end_tag));
    return StringRef(TextPtr, IntVal);
  }

  void setHTMLTagEndName(StringRef Name) {
    assert(is(tok::html_end_tag));
    TextPtr = Name.data();
    IntVal = Name.size();
  }

  void dump(const Lexer &L, const SourceManager &SM) const;
};

/// Comment lexer.
class Lexer {
private:
  Lexer(const Lexer &) = delete;
  void operator=(const Lexer &) = delete;

  /// Allocator for strings that are semantic values of tokens and have to be
  /// computed (for example, resolved decimal character references).
  llvm::BumpPtrAllocator &Allocator;

  DiagnosticsEngine &Diags;

  const CommandTraits &Traits;

  const char *const BufferStart;
  const char *const BufferEnd;
  SourceLocation FileLoc;

  const char *BufferPtr;

  /// One past end pointer for the current comment.  For BCPL comments points
  /// to newline or BufferEnd, for C comments points to star in '*/'.
  const char *CommentEnd;

  enum LexerCommentState {
    LCS_BeforeComment,
    LCS_InsideBCPLComment,
    LCS_InsideCComment,
    LCS_BetweenComments
  };

  /// Low-level lexer state, track if we are inside or outside of comment.
  LexerCommentState CommentState;

  enum LexerState {
    /// Lexing normal comment text
    LS_Normal,

    /// Finished lexing verbatim block beginning command, will lex first body
    /// line.
    LS_VerbatimBlockFirstLine,

    /// Lexing verbatim block body line-by-line, skipping line-starting
    /// decorations.
    LS_VerbatimBlockBody,

    /// Finished lexing verbatim line beginning command, will lex text (one
    /// line).
    LS_VerbatimLineText,

    /// Finished lexing \verbatim <TAG \endverbatim part, lexing tag attributes.
    LS_HTMLStartTag,

    /// Finished lexing \verbatim </TAG \endverbatim part, lexing '>'.
    LS_HTMLEndTag
  };

  /// Current lexing mode.
  LexerState State;

  /// If State is LS_VerbatimBlock, contains the name of verbatim end
  /// command, including command marker.
  SmallString<16> VerbatimBlockEndCommandName;

  /// If true, the commands, html tags, etc will be parsed and reported as
  /// separate tokens inside the comment body. If false, the comment text will
  /// be parsed into text and newline tokens.
  bool ParseCommands;

  /// Given a character reference name (e.g., "lt"), return the character that
  /// it stands for (e.g., "<").
  StringRef resolveHTMLNamedCharacterReference(StringRef Name) const;

  /// Given a Unicode codepoint as base-10 integer, return the character.
  StringRef resolveHTMLDecimalCharacterReference(StringRef Name) const;

  /// Given a Unicode codepoint as base-16 integer, return the character.
  StringRef resolveHTMLHexCharacterReference(StringRef Name) const;

  void formTokenWithChars(Token &Result, const char *TokEnd,
                          tok::TokenKind Kind);

  void formTextToken(Token &Result, const char *TokEnd) {
    StringRef Text(BufferPtr, TokEnd - BufferPtr);
    formTokenWithChars(Result, TokEnd, tok::text);
    Result.setText(Text);
  }

  SourceLocation getSourceLocation(const char *Loc) const {
    assert(Loc >= BufferStart && Loc <= BufferEnd &&
           "Location out of range for this buffer!");

    const unsigned CharNo = Loc - BufferStart;
    return FileLoc.getLocWithOffset(CharNo);
  }

  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID) {
    return Diags.Report(Loc, DiagID);
  }

  /// Eat string matching regexp \code \s*\* \endcode.
  void skipLineStartingDecorations();

  /// Lex comment text, including commands if ParseCommands is set to true.
  void lexCommentText(Token &T);

  void setupAndLexVerbatimBlock(Token &T, const char *TextBegin, char Marker,
                                const CommandInfo *Info);

  void lexVerbatimBlockFirstLine(Token &T);

  void lexVerbatimBlockBody(Token &T);

  void setupAndLexVerbatimLine(Token &T, const char *TextBegin,
                               const CommandInfo *Info);

  void lexVerbatimLineText(Token &T);

  void lexHTMLCharacterReference(Token &T);

  void setupAndLexHTMLStartTag(Token &T);

  void lexHTMLStartTag(Token &T);

  void setupAndLexHTMLEndTag(Token &T);

  void lexHTMLEndTag(Token &T);

public:
  Lexer(llvm::BumpPtrAllocator &Allocator, DiagnosticsEngine &Diags,
        const CommandTraits &Traits, SourceLocation FileLoc,
        const char *BufferStart, const char *BufferEnd,
        bool ParseCommands = true);

  void lex(Token &T);

  StringRef getSpelling(const Token &Tok, const SourceManager &SourceMgr,
                        bool *Invalid = nullptr) const;
};

} // end namespace comments
} // end namespace clang

#endif

