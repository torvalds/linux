//===- llvm/MC/MCAsmLexer.h - Abstract Asm Lexer Interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCASMLEXER_H
#define LLVM_MC_MCPARSER_MCASMLEXER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmMacro.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace llvm {

/// A callback class which is notified of each comment in an assembly file as
/// it is lexed.
class AsmCommentConsumer {
public:
  virtual ~AsmCommentConsumer() = default;

  /// Callback function for when a comment is lexed. Loc is the start of the
  /// comment text (excluding the comment-start marker). CommentText is the text
  /// of the comment, excluding the comment start and end markers, and the
  /// newline for single-line comments.
  virtual void HandleComment(SMLoc Loc, StringRef CommentText) = 0;
};


/// Generic assembler lexer interface, for use by target specific assembly
/// lexers.
class MCAsmLexer {
  /// The current token, stored in the base class for faster access.
  SmallVector<AsmToken, 1> CurTok;

  /// The location and description of the current error
  SMLoc ErrLoc;
  std::string Err;

protected: // Can only create subclasses.
  const char *TokStart = nullptr;
  bool SkipSpace = true;
  bool AllowAtInIdentifier;
  bool IsAtStartOfStatement = true;
  bool LexMasmIntegers = false;
  AsmCommentConsumer *CommentConsumer = nullptr;

  MCAsmLexer();

  virtual AsmToken LexToken() = 0;

  void SetError(SMLoc errLoc, const std::string &err) {
    ErrLoc = errLoc;
    Err = err;
  }

public:
  MCAsmLexer(const MCAsmLexer &) = delete;
  MCAsmLexer &operator=(const MCAsmLexer &) = delete;
  virtual ~MCAsmLexer();

  /// Consume the next token from the input stream and return it.
  ///
  /// The lexer will continuously return the end-of-file token once the end of
  /// the main input file has been reached.
  const AsmToken &Lex() {
    assert(!CurTok.empty());
    // Mark if we parsing out a EndOfStatement.
    IsAtStartOfStatement = CurTok.front().getKind() == AsmToken::EndOfStatement;
    CurTok.erase(CurTok.begin());
    // LexToken may generate multiple tokens via UnLex but will always return
    // the first one. Place returned value at head of CurTok vector.
    if (CurTok.empty()) {
      AsmToken T = LexToken();
      CurTok.insert(CurTok.begin(), T);
    }
    return CurTok.front();
  }

  void UnLex(AsmToken const &Token) {
    IsAtStartOfStatement = false;
    CurTok.insert(CurTok.begin(), Token);
  }

  bool isAtStartOfStatement() { return IsAtStartOfStatement; }

  virtual StringRef LexUntilEndOfStatement() = 0;

  /// Get the current source location.
  SMLoc getLoc() const;

  /// Get the current (last) lexed token.
  const AsmToken &getTok() const {
    return CurTok[0];
  }

  /// Look ahead at the next token to be lexed.
  const AsmToken peekTok(bool ShouldSkipSpace = true) {
    AsmToken Tok;

    MutableArrayRef<AsmToken> Buf(Tok);
    size_t ReadCount = peekTokens(Buf, ShouldSkipSpace);

    assert(ReadCount == 1);
    (void)ReadCount;

    return Tok;
  }

  /// Look ahead an arbitrary number of tokens.
  virtual size_t peekTokens(MutableArrayRef<AsmToken> Buf,
                            bool ShouldSkipSpace = true) = 0;

  /// Get the current error location
  SMLoc getErrLoc() {
    return ErrLoc;
  }

  /// Get the current error string
  const std::string &getErr() {
    return Err;
  }

  /// Get the kind of current token.
  AsmToken::TokenKind getKind() const { return getTok().getKind(); }

  /// Check if the current token has kind \p K.
  bool is(AsmToken::TokenKind K) const { return getTok().is(K); }

  /// Check if the current token has kind \p K.
  bool isNot(AsmToken::TokenKind K) const { return getTok().isNot(K); }

  /// Set whether spaces should be ignored by the lexer
  void setSkipSpace(bool val) { SkipSpace = val; }

  bool getAllowAtInIdentifier() { return AllowAtInIdentifier; }
  void setAllowAtInIdentifier(bool v) { AllowAtInIdentifier = v; }

  void setCommentConsumer(AsmCommentConsumer *CommentConsumer) {
    this->CommentConsumer = CommentConsumer;
  }

  /// Set whether to lex masm-style binary and hex literals. They look like
  /// 0b1101 and 0ABCh respectively.
  void setLexMasmIntegers(bool V) { LexMasmIntegers = V; }
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCASMLEXER_H
