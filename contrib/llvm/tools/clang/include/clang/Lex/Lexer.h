//===- Lexer.h - C Language Family Lexer ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Lexer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_LEXER_H
#define LLVM_CLANG_LEX_LEXER_H

#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/PreprocessorLexer.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace clang {

class DiagnosticBuilder;
class Preprocessor;
class SourceManager;

/// ConflictMarkerKind - Kinds of conflict marker which the lexer might be
/// recovering from.
enum ConflictMarkerKind {
  /// Not within a conflict marker.
  CMK_None,

  /// A normal or diff3 conflict marker, initiated by at least 7 "<"s,
  /// separated by at least 7 "="s or "|"s, and terminated by at least 7 ">"s.
  CMK_Normal,

  /// A Perforce-style conflict marker, initiated by 4 ">"s,
  /// separated by 4 "="s, and terminated by 4 "<"s.
  CMK_Perforce
};

/// Describes the bounds (start, size) of the preamble and a flag required by
/// PreprocessorOptions::PrecompiledPreambleBytes.
/// The preamble includes the BOM, if any.
struct PreambleBounds {
  /// Size of the preamble in bytes.
  unsigned Size;

  /// Whether the preamble ends at the start of a new line.
  ///
  /// Used to inform the lexer as to whether it's starting at the beginning of
  /// a line after skipping the preamble.
  bool PreambleEndsAtStartOfLine;

  PreambleBounds(unsigned Size, bool PreambleEndsAtStartOfLine)
      : Size(Size), PreambleEndsAtStartOfLine(PreambleEndsAtStartOfLine) {}
};

/// Lexer - This provides a simple interface that turns a text buffer into a
/// stream of tokens.  This provides no support for file reading or buffering,
/// or buffering/seeking of tokens, only forward lexing is supported.  It relies
/// on the specified Preprocessor object to handle preprocessor directives, etc.
class Lexer : public PreprocessorLexer {
  friend class Preprocessor;

  void anchor() override;

  //===--------------------------------------------------------------------===//
  // Constant configuration values for this lexer.

  // Start of the buffer.
  const char *BufferStart;

  // End of the buffer.
  const char *BufferEnd;

  // Location for start of file.
  SourceLocation FileLoc;

  // LangOpts enabled by this language (cache).
  LangOptions LangOpts;

  // True if lexer for _Pragma handling.
  bool Is_PragmaLexer;

  //===--------------------------------------------------------------------===//
  // Context-specific lexing flags set by the preprocessor.
  //

  /// ExtendedTokenMode - The lexer can optionally keep comments and whitespace
  /// and return them as tokens.  This is used for -C and -CC modes, and
  /// whitespace preservation can be useful for some clients that want to lex
  /// the file in raw mode and get every character from the file.
  ///
  /// When this is set to 2 it returns comments and whitespace.  When set to 1
  /// it returns comments, when it is set to 0 it returns normal tokens only.
  unsigned char ExtendedTokenMode;

  //===--------------------------------------------------------------------===//
  // Context that changes as the file is lexed.
  // NOTE: any state that mutates when in raw mode must have save/restore code
  // in Lexer::isNextPPTokenLParen.

  // BufferPtr - Current pointer into the buffer.  This is the next character
  // to be lexed.
  const char *BufferPtr;

  // IsAtStartOfLine - True if the next lexed token should get the "start of
  // line" flag set on it.
  bool IsAtStartOfLine;

  bool IsAtPhysicalStartOfLine;

  bool HasLeadingSpace;

  bool HasLeadingEmptyMacro;

  // CurrentConflictMarkerState - The kind of conflict marker we are handling.
  ConflictMarkerKind CurrentConflictMarkerState;

  void InitLexer(const char *BufStart, const char *BufPtr, const char *BufEnd);

public:
  /// Lexer constructor - Create a new lexer object for the specified buffer
  /// with the specified preprocessor managing the lexing process.  This lexer
  /// assumes that the associated file buffer and Preprocessor objects will
  /// outlive it, so it doesn't take ownership of either of them.
  Lexer(FileID FID, const llvm::MemoryBuffer *InputFile, Preprocessor &PP);

  /// Lexer constructor - Create a new raw lexer object.  This object is only
  /// suitable for calls to 'LexFromRawLexer'.  This lexer assumes that the
  /// text range will outlive it, so it doesn't take ownership of it.
  Lexer(SourceLocation FileLoc, const LangOptions &LangOpts,
        const char *BufStart, const char *BufPtr, const char *BufEnd);

  /// Lexer constructor - Create a new raw lexer object.  This object is only
  /// suitable for calls to 'LexFromRawLexer'.  This lexer assumes that the
  /// text range will outlive it, so it doesn't take ownership of it.
  Lexer(FileID FID, const llvm::MemoryBuffer *FromFile,
        const SourceManager &SM, const LangOptions &LangOpts);

  Lexer(const Lexer &) = delete;
  Lexer &operator=(const Lexer &) = delete;

  /// Create_PragmaLexer: Lexer constructor - Create a new lexer object for
  /// _Pragma expansion.  This has a variety of magic semantics that this method
  /// sets up.  It returns a new'd Lexer that must be delete'd when done.
  static Lexer *Create_PragmaLexer(SourceLocation SpellingLoc,
                                   SourceLocation ExpansionLocStart,
                                   SourceLocation ExpansionLocEnd,
                                   unsigned TokLen, Preprocessor &PP);

  /// getLangOpts - Return the language features currently enabled.
  /// NOTE: this lexer modifies features as a file is parsed!
  const LangOptions &getLangOpts() const { return LangOpts; }

  /// getFileLoc - Return the File Location for the file we are lexing out of.
  /// The physical location encodes the location where the characters come from,
  /// the virtual location encodes where we should *claim* the characters came
  /// from.  Currently this is only used by _Pragma handling.
  SourceLocation getFileLoc() const { return FileLoc; }

private:
  /// Lex - Return the next token in the file.  If this is the end of file, it
  /// return the tok::eof token.  This implicitly involves the preprocessor.
  bool Lex(Token &Result);

public:
  /// isPragmaLexer - Returns true if this Lexer is being used to lex a pragma.
  bool isPragmaLexer() const { return Is_PragmaLexer; }

private:
  /// IndirectLex - An indirect call to 'Lex' that can be invoked via
  ///  the PreprocessorLexer interface.
  void IndirectLex(Token &Result) override { Lex(Result); }

public:
  /// LexFromRawLexer - Lex a token from a designated raw lexer (one with no
  /// associated preprocessor object.  Return true if the 'next character to
  /// read' pointer points at the end of the lexer buffer, false otherwise.
  bool LexFromRawLexer(Token &Result) {
    assert(LexingRawMode && "Not already in raw mode!");
    Lex(Result);
    // Note that lexing to the end of the buffer doesn't implicitly delete the
    // lexer when in raw mode.
    return BufferPtr == BufferEnd;
  }

  /// isKeepWhitespaceMode - Return true if the lexer should return tokens for
  /// every character in the file, including whitespace and comments.  This
  /// should only be used in raw mode, as the preprocessor is not prepared to
  /// deal with the excess tokens.
  bool isKeepWhitespaceMode() const {
    return ExtendedTokenMode > 1;
  }

  /// SetKeepWhitespaceMode - This method lets clients enable or disable
  /// whitespace retention mode.
  void SetKeepWhitespaceMode(bool Val) {
    assert((!Val || LexingRawMode || LangOpts.TraditionalCPP) &&
           "Can only retain whitespace in raw mode or -traditional-cpp");
    ExtendedTokenMode = Val ? 2 : 0;
  }

  /// inKeepCommentMode - Return true if the lexer should return comments as
  /// tokens.
  bool inKeepCommentMode() const {
    return ExtendedTokenMode > 0;
  }

  /// SetCommentRetentionMode - Change the comment retention mode of the lexer
  /// to the specified mode.  This is really only useful when lexing in raw
  /// mode, because otherwise the lexer needs to manage this.
  void SetCommentRetentionState(bool Mode) {
    assert(!isKeepWhitespaceMode() &&
           "Can't play with comment retention state when retaining whitespace");
    ExtendedTokenMode = Mode ? 1 : 0;
  }

  /// Sets the extended token mode back to its initial value, according to the
  /// language options and preprocessor. This controls whether the lexer
  /// produces comment and whitespace tokens.
  ///
  /// This requires the lexer to have an associated preprocessor. A standalone
  /// lexer has nothing to reset to.
  void resetExtendedTokenMode();

  /// Gets source code buffer.
  StringRef getBuffer() const {
    return StringRef(BufferStart, BufferEnd - BufferStart);
  }

  /// ReadToEndOfLine - Read the rest of the current preprocessor line as an
  /// uninterpreted string.  This switches the lexer out of directive mode.
  void ReadToEndOfLine(SmallVectorImpl<char> *Result = nullptr);


  /// Diag - Forwarding function for diagnostics.  This translate a source
  /// position in the current buffer into a SourceLocation object for rendering.
  DiagnosticBuilder Diag(const char *Loc, unsigned DiagID) const;

  /// getSourceLocation - Return a source location identifier for the specified
  /// offset in the current file.
  SourceLocation getSourceLocation(const char *Loc, unsigned TokLen = 1) const;

  /// getSourceLocation - Return a source location for the next character in
  /// the current file.
  SourceLocation getSourceLocation() override {
    return getSourceLocation(BufferPtr);
  }

  /// Return the current location in the buffer.
  const char *getBufferLocation() const { return BufferPtr; }

  /// Stringify - Convert the specified string into a C string by i) escaping
  /// '\\' and " characters and ii) replacing newline character(s) with "\\n".
  /// If Charify is true, this escapes the ' character instead of ".
  static std::string Stringify(StringRef Str, bool Charify = false);

  /// Stringify - Convert the specified string into a C string by i) escaping
  /// '\\' and " characters and ii) replacing newline character(s) with "\\n".
  static void Stringify(SmallVectorImpl<char> &Str);

  /// getSpelling - This method is used to get the spelling of a token into a
  /// preallocated buffer, instead of as an std::string.  The caller is required
  /// to allocate enough space for the token, which is guaranteed to be at least
  /// Tok.getLength() bytes long.  The length of the actual result is returned.
  ///
  /// Note that this method may do two possible things: it may either fill in
  /// the buffer specified with characters, or it may *change the input pointer*
  /// to point to a constant buffer with the data already in it (avoiding a
  /// copy).  The caller is not allowed to modify the returned buffer pointer
  /// if an internal buffer is returned.
  static unsigned getSpelling(const Token &Tok, const char *&Buffer,
                              const SourceManager &SourceMgr,
                              const LangOptions &LangOpts,
                              bool *Invalid = nullptr);

  /// getSpelling() - Return the 'spelling' of the Tok token.  The spelling of a
  /// token is the characters used to represent the token in the source file
  /// after trigraph expansion and escaped-newline folding.  In particular, this
  /// wants to get the true, uncanonicalized, spelling of things like digraphs
  /// UCNs, etc.
  static std::string getSpelling(const Token &Tok,
                                 const SourceManager &SourceMgr,
                                 const LangOptions &LangOpts,
                                 bool *Invalid = nullptr);

  /// getSpelling - This method is used to get the spelling of the
  /// token at the given source location.  If, as is usually true, it
  /// is not necessary to copy any data, then the returned string may
  /// not point into the provided buffer.
  ///
  /// This method lexes at the expansion depth of the given
  /// location and does not jump to the expansion or spelling
  /// location.
  static StringRef getSpelling(SourceLocation loc,
                               SmallVectorImpl<char> &buffer,
                               const SourceManager &SM,
                               const LangOptions &options,
                               bool *invalid = nullptr);

  /// MeasureTokenLength - Relex the token at the specified location and return
  /// its length in bytes in the input file.  If the token needs cleaning (e.g.
  /// includes a trigraph or an escaped newline) then this count includes bytes
  /// that are part of that.
  static unsigned MeasureTokenLength(SourceLocation Loc,
                                     const SourceManager &SM,
                                     const LangOptions &LangOpts);

  /// Relex the token at the specified location.
  /// \returns true if there was a failure, false on success.
  static bool getRawToken(SourceLocation Loc, Token &Result,
                          const SourceManager &SM,
                          const LangOptions &LangOpts,
                          bool IgnoreWhiteSpace = false);

  /// Given a location any where in a source buffer, find the location
  /// that corresponds to the beginning of the token in which the original
  /// source location lands.
  static SourceLocation GetBeginningOfToken(SourceLocation Loc,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts);

  /// Get the physical length (including trigraphs and escaped newlines) of the
  /// first \p Characters characters of the token starting at TokStart.
  static unsigned getTokenPrefixLength(SourceLocation TokStart,
                                       unsigned CharNo,
                                       const SourceManager &SM,
                                       const LangOptions &LangOpts);

  /// AdvanceToTokenCharacter - If the current SourceLocation specifies a
  /// location at the start of a token, return a new location that specifies a
  /// character within the token.  This handles trigraphs and escaped newlines.
  static SourceLocation AdvanceToTokenCharacter(SourceLocation TokStart,
                                                unsigned Characters,
                                                const SourceManager &SM,
                                                const LangOptions &LangOpts) {
    return TokStart.getLocWithOffset(
        getTokenPrefixLength(TokStart, Characters, SM, LangOpts));
  }

  /// Computes the source location just past the end of the
  /// token at this source location.
  ///
  /// This routine can be used to produce a source location that
  /// points just past the end of the token referenced by \p Loc, and
  /// is generally used when a diagnostic needs to point just after a
  /// token where it expected something different that it received. If
  /// the returned source location would not be meaningful (e.g., if
  /// it points into a macro), this routine returns an invalid
  /// source location.
  ///
  /// \param Offset an offset from the end of the token, where the source
  /// location should refer to. The default offset (0) produces a source
  /// location pointing just past the end of the token; an offset of 1 produces
  /// a source location pointing to the last character in the token, etc.
  static SourceLocation getLocForEndOfToken(SourceLocation Loc, unsigned Offset,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts);

  /// Given a token range, produce a corresponding CharSourceRange that
  /// is not a token range. This allows the source range to be used by
  /// components that don't have access to the lexer and thus can't find the
  /// end of the range for themselves.
  static CharSourceRange getAsCharRange(SourceRange Range,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts) {
    SourceLocation End = getLocForEndOfToken(Range.getEnd(), 0, SM, LangOpts);
    return End.isInvalid() ? CharSourceRange()
                           : CharSourceRange::getCharRange(
                                 Range.getBegin(), End.getLocWithOffset(-1));
  }
  static CharSourceRange getAsCharRange(CharSourceRange Range,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts) {
    return Range.isTokenRange()
               ? getAsCharRange(Range.getAsRange(), SM, LangOpts)
               : Range;
  }

  /// Returns true if the given MacroID location points at the first
  /// token of the macro expansion.
  ///
  /// \param MacroBegin If non-null and function returns true, it is set to
  /// begin location of the macro.
  static bool isAtStartOfMacroExpansion(SourceLocation loc,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts,
                                        SourceLocation *MacroBegin = nullptr);

  /// Returns true if the given MacroID location points at the last
  /// token of the macro expansion.
  ///
  /// \param MacroEnd If non-null and function returns true, it is set to
  /// end location of the macro.
  static bool isAtEndOfMacroExpansion(SourceLocation loc,
                                      const SourceManager &SM,
                                      const LangOptions &LangOpts,
                                      SourceLocation *MacroEnd = nullptr);

  /// Accepts a range and returns a character range with file locations.
  ///
  /// Returns a null range if a part of the range resides inside a macro
  /// expansion or the range does not reside on the same FileID.
  ///
  /// This function is trying to deal with macros and return a range based on
  /// file locations. The cases where it can successfully handle macros are:
  ///
  /// -begin or end range lies at the start or end of a macro expansion, in
  ///  which case the location will be set to the expansion point, e.g:
  ///    \#define M 1 2
  ///    a M
  /// If you have a range [a, 2] (where 2 came from the macro), the function
  /// will return a range for "a M"
  /// if you have range [a, 1], the function will fail because the range
  /// overlaps with only a part of the macro
  ///
  /// -The macro is a function macro and the range can be mapped to the macro
  ///  arguments, e.g:
  ///    \#define M 1 2
  ///    \#define FM(x) x
  ///    FM(a b M)
  /// if you have range [b, 2], the function will return the file range "b M"
  /// inside the macro arguments.
  /// if you have range [a, 2], the function will return the file range
  /// "FM(a b M)" since the range includes all of the macro expansion.
  static CharSourceRange makeFileCharRange(CharSourceRange Range,
                                           const SourceManager &SM,
                                           const LangOptions &LangOpts);

  /// Returns a string for the source that the range encompasses.
  static StringRef getSourceText(CharSourceRange Range,
                                 const SourceManager &SM,
                                 const LangOptions &LangOpts,
                                 bool *Invalid = nullptr);

  /// Retrieve the name of the immediate macro expansion.
  ///
  /// This routine starts from a source location, and finds the name of the macro
  /// responsible for its immediate expansion. It looks through any intervening
  /// macro argument expansions to compute this. It returns a StringRef which
  /// refers to the SourceManager-owned buffer of the source where that macro
  /// name is spelled. Thus, the result shouldn't out-live that SourceManager.
  static StringRef getImmediateMacroName(SourceLocation Loc,
                                         const SourceManager &SM,
                                         const LangOptions &LangOpts);

  /// Retrieve the name of the immediate macro expansion.
  ///
  /// This routine starts from a source location, and finds the name of the
  /// macro responsible for its immediate expansion. It looks through any
  /// intervening macro argument expansions to compute this. It returns a
  /// StringRef which refers to the SourceManager-owned buffer of the source
  /// where that macro name is spelled. Thus, the result shouldn't out-live
  /// that SourceManager.
  ///
  /// This differs from Lexer::getImmediateMacroName in that any macro argument
  /// location will result in the topmost function macro that accepted it.
  /// e.g.
  /// \code
  ///   MAC1( MAC2(foo) )
  /// \endcode
  /// for location of 'foo' token, this function will return "MAC1" while
  /// Lexer::getImmediateMacroName will return "MAC2".
  static StringRef getImmediateMacroNameForDiagnostics(
      SourceLocation Loc, const SourceManager &SM, const LangOptions &LangOpts);

  /// Compute the preamble of the given file.
  ///
  /// The preamble of a file contains the initial comments, include directives,
  /// and other preprocessor directives that occur before the code in this
  /// particular file actually begins. The preamble of the main source file is
  /// a potential prefix header.
  ///
  /// \param Buffer The memory buffer containing the file's contents.
  ///
  /// \param MaxLines If non-zero, restrict the length of the preamble
  /// to fewer than this number of lines.
  ///
  /// \returns The offset into the file where the preamble ends and the rest
  /// of the file begins along with a boolean value indicating whether
  /// the preamble ends at the beginning of a new line.
  static PreambleBounds ComputePreamble(StringRef Buffer,
                                        const LangOptions &LangOpts,
                                        unsigned MaxLines = 0);

  /// Finds the token that comes right after the given location.
  ///
  /// Returns the next token, or none if the location is inside a macro.
  static Optional<Token> findNextToken(SourceLocation Loc,
                                       const SourceManager &SM,
                                       const LangOptions &LangOpts);

  /// Checks that the given token is the first token that occurs after
  /// the given location (this excludes comments and whitespace). Returns the
  /// location immediately after the specified token. If the token is not found
  /// or the location is inside a macro, the returned source location will be
  /// invalid.
  static SourceLocation findLocationAfterToken(SourceLocation loc,
                                         tok::TokenKind TKind,
                                         const SourceManager &SM,
                                         const LangOptions &LangOpts,
                                         bool SkipTrailingWhitespaceAndNewLine);

  /// Returns true if the given character could appear in an identifier.
  static bool isIdentifierBodyChar(char c, const LangOptions &LangOpts);

  /// Checks whether new line pointed by Str is preceded by escape
  /// sequence.
  static bool isNewLineEscaped(const char *BufferStart, const char *Str);

  /// getCharAndSizeNoWarn - Like the getCharAndSize method, but does not ever
  /// emit a warning.
  static inline char getCharAndSizeNoWarn(const char *Ptr, unsigned &Size,
                                          const LangOptions &LangOpts) {
    // If this is not a trigraph and not a UCN or escaped newline, return
    // quickly.
    if (isObviouslySimpleCharacter(Ptr[0])) {
      Size = 1;
      return *Ptr;
    }

    Size = 0;
    return getCharAndSizeSlowNoWarn(Ptr, Size, LangOpts);
  }

  /// Returns the leading whitespace for line that corresponds to the given
  /// location \p Loc.
  static StringRef getIndentationForLine(SourceLocation Loc,
                                         const SourceManager &SM);

private:
  //===--------------------------------------------------------------------===//
  // Internal implementation interfaces.

  /// LexTokenInternal - Internal interface to lex a preprocessing token. Called
  /// by Lex.
  ///
  bool LexTokenInternal(Token &Result, bool TokAtPhysicalStartOfLine);

  bool CheckUnicodeWhitespace(Token &Result, uint32_t C, const char *CurPtr);

  /// Given that a token begins with the Unicode character \p C, figure out
  /// what kind of token it is and dispatch to the appropriate lexing helper
  /// function.
  bool LexUnicode(Token &Result, uint32_t C, const char *CurPtr);

  /// FormTokenWithChars - When we lex a token, we have identified a span
  /// starting at BufferPtr, going to TokEnd that forms the token.  This method
  /// takes that range and assigns it to the token as its location and size.  In
  /// addition, since tokens cannot overlap, this also updates BufferPtr to be
  /// TokEnd.
  void FormTokenWithChars(Token &Result, const char *TokEnd,
                          tok::TokenKind Kind) {
    unsigned TokLen = TokEnd-BufferPtr;
    Result.setLength(TokLen);
    Result.setLocation(getSourceLocation(BufferPtr, TokLen));
    Result.setKind(Kind);
    BufferPtr = TokEnd;
  }

  /// isNextPPTokenLParen - Return 1 if the next unexpanded token will return a
  /// tok::l_paren token, 0 if it is something else and 2 if there are no more
  /// tokens in the buffer controlled by this lexer.
  unsigned isNextPPTokenLParen();

  //===--------------------------------------------------------------------===//
  // Lexer character reading interfaces.

  // This lexer is built on two interfaces for reading characters, both of which
  // automatically provide phase 1/2 translation.  getAndAdvanceChar is used
  // when we know that we will be reading a character from the input buffer and
  // that this character will be part of the result token. This occurs in (f.e.)
  // string processing, because we know we need to read until we find the
  // closing '"' character.
  //
  // The second interface is the combination of getCharAndSize with
  // ConsumeChar.  getCharAndSize reads a phase 1/2 translated character,
  // returning it and its size.  If the lexer decides that this character is
  // part of the current token, it calls ConsumeChar on it.  This two stage
  // approach allows us to emit diagnostics for characters (e.g. warnings about
  // trigraphs), knowing that they only are emitted if the character is
  // consumed.

  /// isObviouslySimpleCharacter - Return true if the specified character is
  /// obviously the same in translation phase 1 and translation phase 3.  This
  /// can return false for characters that end up being the same, but it will
  /// never return true for something that needs to be mapped.
  static bool isObviouslySimpleCharacter(char C) {
    return C != '?' && C != '\\';
  }

  /// getAndAdvanceChar - Read a single 'character' from the specified buffer,
  /// advance over it, and return it.  This is tricky in several cases.  Here we
  /// just handle the trivial case and fall-back to the non-inlined
  /// getCharAndSizeSlow method to handle the hard case.
  inline char getAndAdvanceChar(const char *&Ptr, Token &Tok) {
    // If this is not a trigraph and not a UCN or escaped newline, return
    // quickly.
    if (isObviouslySimpleCharacter(Ptr[0])) return *Ptr++;

    unsigned Size = 0;
    char C = getCharAndSizeSlow(Ptr, Size, &Tok);
    Ptr += Size;
    return C;
  }

  /// ConsumeChar - When a character (identified by getCharAndSize) is consumed
  /// and added to a given token, check to see if there are diagnostics that
  /// need to be emitted or flags that need to be set on the token.  If so, do
  /// it.
  const char *ConsumeChar(const char *Ptr, unsigned Size, Token &Tok) {
    // Normal case, we consumed exactly one token.  Just return it.
    if (Size == 1)
      return Ptr+Size;

    // Otherwise, re-lex the character with a current token, allowing
    // diagnostics to be emitted and flags to be set.
    Size = 0;
    getCharAndSizeSlow(Ptr, Size, &Tok);
    return Ptr+Size;
  }

  /// getCharAndSize - Peek a single 'character' from the specified buffer,
  /// get its size, and return it.  This is tricky in several cases.  Here we
  /// just handle the trivial case and fall-back to the non-inlined
  /// getCharAndSizeSlow method to handle the hard case.
  inline char getCharAndSize(const char *Ptr, unsigned &Size) {
    // If this is not a trigraph and not a UCN or escaped newline, return
    // quickly.
    if (isObviouslySimpleCharacter(Ptr[0])) {
      Size = 1;
      return *Ptr;
    }

    Size = 0;
    return getCharAndSizeSlow(Ptr, Size);
  }

  /// getCharAndSizeSlow - Handle the slow/uncommon case of the getCharAndSize
  /// method.
  char getCharAndSizeSlow(const char *Ptr, unsigned &Size,
                          Token *Tok = nullptr);

  /// getEscapedNewLineSize - Return the size of the specified escaped newline,
  /// or 0 if it is not an escaped newline. P[-1] is known to be a "\" on entry
  /// to this function.
  static unsigned getEscapedNewLineSize(const char *P);

  /// SkipEscapedNewLines - If P points to an escaped newline (or a series of
  /// them), skip over them and return the first non-escaped-newline found,
  /// otherwise return P.
  static const char *SkipEscapedNewLines(const char *P);

  /// getCharAndSizeSlowNoWarn - Same as getCharAndSizeSlow, but never emits a
  /// diagnostic.
  static char getCharAndSizeSlowNoWarn(const char *Ptr, unsigned &Size,
                                       const LangOptions &LangOpts);

  //===--------------------------------------------------------------------===//
  // Other lexer functions.

  void SetByteOffset(unsigned Offset, bool StartOfLine);

  void PropagateLineStartLeadingSpaceInfo(Token &Result);

  const char *LexUDSuffix(Token &Result, const char *CurPtr,
                          bool IsStringLiteral);

  // Helper functions to lex the remainder of a token of the specific type.
  bool LexIdentifier         (Token &Result, const char *CurPtr);
  bool LexNumericConstant    (Token &Result, const char *CurPtr);
  bool LexStringLiteral      (Token &Result, const char *CurPtr,
                              tok::TokenKind Kind);
  bool LexRawStringLiteral   (Token &Result, const char *CurPtr,
                              tok::TokenKind Kind);
  bool LexAngledStringLiteral(Token &Result, const char *CurPtr);
  bool LexCharConstant       (Token &Result, const char *CurPtr,
                              tok::TokenKind Kind);
  bool LexEndOfFile          (Token &Result, const char *CurPtr);
  bool SkipWhitespace        (Token &Result, const char *CurPtr,
                              bool &TokAtPhysicalStartOfLine);
  bool SkipLineComment       (Token &Result, const char *CurPtr,
                              bool &TokAtPhysicalStartOfLine);
  bool SkipBlockComment      (Token &Result, const char *CurPtr,
                              bool &TokAtPhysicalStartOfLine);
  bool SaveLineComment       (Token &Result, const char *CurPtr);

  bool IsStartOfConflictMarker(const char *CurPtr);
  bool HandleEndOfConflictMarker(const char *CurPtr);

  bool lexEditorPlaceholder(Token &Result, const char *CurPtr);

  bool isCodeCompletionPoint(const char *CurPtr) const;
  void cutOffLexing() { BufferPtr = BufferEnd; }

  bool isHexaLiteral(const char *Start, const LangOptions &LangOpts);

  void codeCompleteIncludedFile(const char *PathStart,
                                const char *CompletionPoint, bool IsAngled);

  /// Read a universal character name.
  ///
  /// \param StartPtr The position in the source buffer after the initial '\'.
  ///                 If the UCN is syntactically well-formed (but not 
  ///                 necessarily valid), this parameter will be updated to
  ///                 point to the character after the UCN.
  /// \param SlashLoc The position in the source buffer of the '\'.
  /// \param Result   The token being formed. Pass \c nullptr to suppress
  ///                 diagnostics and handle token formation in the caller.
  ///
  /// \return The Unicode codepoint specified by the UCN, or 0 if the UCN is
  ///         invalid.
  uint32_t tryReadUCN(const char *&StartPtr, const char *SlashLoc, Token *Result);

  /// Try to consume a UCN as part of an identifier at the current
  /// location.
  /// \param CurPtr Initially points to the range of characters in the source
  ///               buffer containing the '\'. Updated to point past the end of
  ///               the UCN on success.
  /// \param Size The number of characters occupied by the '\' (including
  ///             trigraphs and escaped newlines).
  /// \param Result The token being produced. Marked as containing a UCN on
  ///               success.
  /// \return \c true if a UCN was lexed and it produced an acceptable
  ///         identifier character, \c false otherwise.
  bool tryConsumeIdentifierUCN(const char *&CurPtr, unsigned Size,
                               Token &Result);

  /// Try to consume an identifier character encoded in UTF-8.
  /// \param CurPtr Points to the start of the (potential) UTF-8 code unit
  ///        sequence. On success, updated to point past the end of it.
  /// \return \c true if a UTF-8 sequence mapping to an acceptable identifier
  ///         character was lexed, \c false otherwise.
  bool tryConsumeIdentifierUTF8Char(const char *&CurPtr);
};

} // namespace clang

#endif // LLVM_CLANG_LEX_LEXER_H
