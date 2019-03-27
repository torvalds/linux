//===- PreprocessorLexer.h - C Language Family Lexer ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the PreprocessorLexer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PREPROCESSORLEXER_H
#define LLVM_CLANG_LEX_PREPROCESSORLEXER_H

#include "clang/Lex/MultipleIncludeOpt.h"
#include "clang/Lex/Token.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>

namespace clang {

class FileEntry;
class Preprocessor;

class PreprocessorLexer {
  virtual void anchor();

protected:
  friend class Preprocessor;

  // Preprocessor object controlling lexing.
  Preprocessor *PP = nullptr;

  /// The SourceManager FileID corresponding to the file being lexed.
  const FileID FID;

  /// Number of SLocEntries before lexing the file.
  unsigned InitialNumSLocEntries = 0;

  //===--------------------------------------------------------------------===//
  // Context-specific lexing flags set by the preprocessor.
  //===--------------------------------------------------------------------===//

  /// True when parsing \#XXX; turns '\\n' into a tok::eod token.
  bool ParsingPreprocessorDirective = false;

  /// True after \#include; turns \<xx> into a tok::angle_string_literal
  /// token.
  bool ParsingFilename = false;

  /// True if in raw mode.
  ///
  /// Raw mode disables interpretation of tokens and is a far faster mode to
  /// lex in than non-raw-mode.  This flag:
  ///  1. If EOF of the current lexer is found, the include stack isn't popped.
  ///  2. Identifier information is not looked up for identifier tokens.  As an
  ///     effect of this, implicit macro expansion is naturally disabled.
  ///  3. "#" tokens at the start of a line are treated as normal tokens, not
  ///     implicitly transformed by the lexer.
  ///  4. All diagnostic messages are disabled.
  ///  5. No callbacks are made into the preprocessor.
  ///
  /// Note that in raw mode that the PP pointer may be null.
  bool LexingRawMode = false;

  /// A state machine that detects the \#ifndef-wrapping a file
  /// idiom for the multiple-include optimization.
  MultipleIncludeOpt MIOpt;

  /// Information about the set of \#if/\#ifdef/\#ifndef blocks
  /// we are currently in.
  SmallVector<PPConditionalInfo, 4> ConditionalStack;

  PreprocessorLexer() : FID() {}
  PreprocessorLexer(Preprocessor *pp, FileID fid);
  virtual ~PreprocessorLexer() = default;

  virtual void IndirectLex(Token& Result) = 0;

  /// Return the source location for the next observable location.
  virtual SourceLocation getSourceLocation() = 0;

  //===--------------------------------------------------------------------===//
  // #if directive handling.

  /// pushConditionalLevel - When we enter a \#if directive, this keeps track of
  /// what we are currently in for diagnostic emission (e.g. \#if with missing
  /// \#endif).
  void pushConditionalLevel(SourceLocation DirectiveStart, bool WasSkipping,
                            bool FoundNonSkip, bool FoundElse) {
    PPConditionalInfo CI;
    CI.IfLoc = DirectiveStart;
    CI.WasSkipping = WasSkipping;
    CI.FoundNonSkip = FoundNonSkip;
    CI.FoundElse = FoundElse;
    ConditionalStack.push_back(CI);
  }
  void pushConditionalLevel(const PPConditionalInfo &CI) {
    ConditionalStack.push_back(CI);
  }

  /// popConditionalLevel - Remove an entry off the top of the conditional
  /// stack, returning information about it.  If the conditional stack is empty,
  /// this returns true and does not fill in the arguments.
  bool popConditionalLevel(PPConditionalInfo &CI) {
    if (ConditionalStack.empty())
      return true;
    CI = ConditionalStack.pop_back_val();
    return false;
  }

  /// Return the top of the conditional stack.
  /// \pre This requires that there be a conditional active.
  PPConditionalInfo &peekConditionalLevel() {
    assert(!ConditionalStack.empty() && "No conditionals active!");
    return ConditionalStack.back();
  }

  unsigned getConditionalStackDepth() const { return ConditionalStack.size(); }

public:
  PreprocessorLexer(const PreprocessorLexer &) = delete;
  PreprocessorLexer &operator=(const PreprocessorLexer &) = delete;

  //===--------------------------------------------------------------------===//
  // Misc. lexing methods.

  /// After the preprocessor has parsed a \#include, lex and
  /// (potentially) macro expand the filename.
  ///
  /// If the sequence parsed is not lexically legal, emit a diagnostic and
  /// return a result EOD token.
  void LexIncludeFilename(Token &FilenameTok);

  /// Inform the lexer whether or not we are currently lexing a
  /// preprocessor directive.
  void setParsingPreprocessorDirective(bool f) {
    ParsingPreprocessorDirective = f;
  }

  /// Return true if this lexer is in raw mode or not.
  bool isLexingRawMode() const { return LexingRawMode; }

  /// Return the preprocessor object for this lexer.
  Preprocessor *getPP() const { return PP; }

  FileID getFileID() const {
    assert(PP &&
      "PreprocessorLexer::getFileID() should only be used with a Preprocessor");
    return FID;
  }

  /// Number of SLocEntries before lexing the file.
  unsigned getInitialNumSLocEntries() const {
    return InitialNumSLocEntries;
  }

  /// getFileEntry - Return the FileEntry corresponding to this FileID.  Like
  /// getFileID(), this only works for lexers with attached preprocessors.
  const FileEntry *getFileEntry() const;

  /// Iterator that traverses the current stack of preprocessor
  /// conditional directives (\#if/\#ifdef/\#ifndef).
  using conditional_iterator =
      SmallVectorImpl<PPConditionalInfo>::const_iterator;

  conditional_iterator conditional_begin() const {
    return ConditionalStack.begin();
  }

  conditional_iterator conditional_end() const {
    return ConditionalStack.end();
  }

  void setConditionalLevels(ArrayRef<PPConditionalInfo> CL) {
    ConditionalStack.clear();
    ConditionalStack.append(CL.begin(), CL.end());
  }
};

} // namespace clang

#endif // LLVM_CLANG_LEX_PREPROCESSORLEXER_H
