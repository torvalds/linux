//===- TokenLexer.h - Lex from a token buffer -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the TokenLexer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_TOKENLEXER_H
#define LLVM_CLANG_LEX_TOKENLEXER_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"

namespace clang {

class MacroArgs;
class MacroInfo;
class Preprocessor;
class Token;
class VAOptExpansionContext;

/// TokenLexer - This implements a lexer that returns tokens from a macro body
/// or token stream instead of lexing from a character buffer.  This is used for
/// macro expansion and _Pragma handling, for example.
class TokenLexer {
  friend class Preprocessor;

  /// The macro we are expanding from. This is null if expanding a token stream.
  MacroInfo *Macro = nullptr;

  /// The actual arguments specified for a function-like macro, or null. The
  /// TokenLexer owns the pointed-to object.
  MacroArgs *ActualArgs = nullptr;

  /// The current preprocessor object we are expanding for.
  Preprocessor &PP;

  /// This is the pointer to an array of tokens that the macro is
  /// defined to, with arguments expanded for function-like macros.  If this is
  /// a token stream, these are the tokens we are returning.  This points into
  /// the macro definition we are lexing from, a cache buffer that is owned by
  /// the preprocessor, or some other buffer that we may or may not own
  /// (depending on OwnsTokens).
  /// Note that if it points into Preprocessor's cache buffer, the Preprocessor
  /// may update the pointer as needed.
  const Token *Tokens;

  /// This is the length of the Tokens array.
  unsigned NumTokens;

  /// This is the index of the next token that Lex will return.
  unsigned CurTokenIdx;

  /// The source location range where this macro was expanded.
  SourceLocation ExpandLocStart, ExpandLocEnd;

  /// Source location pointing at the source location entry chunk that
  /// was reserved for the current macro expansion.
  SourceLocation MacroExpansionStart;

  /// The offset of the macro expansion in the
  /// "source location address space".
  unsigned MacroStartSLocOffset;

  /// Location of the macro definition.
  SourceLocation MacroDefStart;

  /// Length of the macro definition.
  unsigned MacroDefLength;

  /// Lexical information about the expansion point of the macro: the identifier
  /// that the macro expanded from had these properties.
  bool AtStartOfLine : 1;
  bool HasLeadingSpace : 1;

  // When this is true, the next token appended to the
  // output list during function argument expansion will get a leading space,
  // regardless of whether it had one to begin with or not. This is used for
  // placemarker support. If still true after function argument expansion, the
  // leading space will be applied to the first token following the macro
  // expansion.
  bool NextTokGetsSpace : 1;

  /// This is true if this TokenLexer allocated the Tokens
  /// array, and thus needs to free it when destroyed.  For simple object-like
  /// macros (for example) we just point into the token buffer of the macro
  /// definition, we don't make a copy of it.
  bool OwnsTokens : 1;

  /// This is true when tokens lexed from the TokenLexer
  /// should not be subject to further macro expansion.
  bool DisableMacroExpansion : 1;

public:
  /// Create a TokenLexer for the specified macro with the specified actual
  /// arguments.  Note that this ctor takes ownership of the ActualArgs pointer.
  /// ILEnd specifies the location of the ')' for a function-like macro or the
  /// identifier for an object-like macro.
  TokenLexer(Token &Tok, SourceLocation ILEnd, MacroInfo *MI,
             MacroArgs *ActualArgs, Preprocessor &pp)
      : PP(pp), OwnsTokens(false) {
    Init(Tok, ILEnd, MI, ActualArgs);
  }

  /// Create a TokenLexer for the specified token stream.  If 'OwnsTokens' is
  /// specified, this takes ownership of the tokens and delete[]'s them when
  /// the token lexer is empty.
  TokenLexer(const Token *TokArray, unsigned NumToks, bool DisableExpansion,
             bool ownsTokens, Preprocessor &pp)
      : PP(pp), OwnsTokens(false) {
    Init(TokArray, NumToks, DisableExpansion, ownsTokens);
  }

  TokenLexer(const TokenLexer &) = delete;
  TokenLexer &operator=(const TokenLexer &) = delete;
  ~TokenLexer() { destroy(); }

  /// Initialize this TokenLexer to expand from the specified macro
  /// with the specified argument information.  Note that this ctor takes
  /// ownership of the ActualArgs pointer.  ILEnd specifies the location of the
  /// ')' for a function-like macro or the identifier for an object-like macro.
  void Init(Token &Tok, SourceLocation ELEnd, MacroInfo *MI,
            MacroArgs *Actuals);

  /// Initialize this TokenLexer with the specified token stream.
  /// This does not take ownership of the specified token vector.
  ///
  /// DisableExpansion is true when macro expansion of tokens lexed from this
  /// stream should be disabled.
  void Init(const Token *TokArray, unsigned NumToks,
            bool DisableMacroExpansion, bool OwnsTokens);

  /// If the next token lexed will pop this macro off the
  /// expansion stack, return 2.  If the next unexpanded token is a '(', return
  /// 1, otherwise return 0.
  unsigned isNextTokenLParen() const;

  /// Lex and return a token from this macro stream.
  bool Lex(Token &Tok);

  /// isParsingPreprocessorDirective - Return true if we are in the middle of a
  /// preprocessor directive.
  bool isParsingPreprocessorDirective() const;

private:
  void destroy();

  /// Return true if the next lex call will pop this macro off the include
  /// stack.
  bool isAtEnd() const {
    return CurTokenIdx == NumTokens;
  }

  /// Concatenates the next (sub-)sequence of \p Tokens separated by '##'
  /// starting with LHSTok - stopping when we encounter a token that is neither
  /// '##' nor preceded by '##'.  Places the result back into \p LHSTok and sets
  /// \p CurIdx to point to the token following the last one that was pasted.
  ///
  /// Also performs the MSVC extension wide-literal token pasting involved with:
  ///       \code L #macro-arg. \endcode
  ///
  /// \param[in,out] LHSTok - Contains the token to the left of '##' in \p
  /// Tokens upon entry and will contain the resulting concatenated Token upon
  /// exit.
  ///
  /// \param[in] TokenStream - The stream of Tokens we are lexing from.
  ///
  /// \param[in,out] CurIdx - Upon entry, \pTokens[\pCurIdx] must equal '##'
  /// (with the exception of the MSVC extension mentioned above).  Upon exit, it
  /// is set to the index of the token following the last token that was
  /// concatenated together.
  ///
  /// \returns If this returns true, the caller should immediately return the
  /// token.
  bool pasteTokens(Token &LHSTok, ArrayRef<Token> TokenStream,
                   unsigned int &CurIdx);

  /// Calls pasteTokens above, passing in the '*this' object's Tokens and
  /// CurTokenIdx data members.
  bool pasteTokens(Token &Tok);


  /// Takes the tail sequence of tokens within ReplacementToks that represent
  /// the just expanded __VA_OPT__ tokens (possibly zero tokens) and transforms
  /// them into a string.  \p VCtx is used to determine which token represents
  /// the first __VA_OPT__ replacement token.
  ///
  /// \param[in,out] ResultToks - Contains the current Replacement Tokens
  /// (prior to rescanning and token pasting), the tail end of which represents
  /// the tokens just expanded through __VA_OPT__ processing.  These (sub)
  /// sequence of tokens are folded into one stringified token.
  ///
  /// \param[in] VCtx - contains relevant contextual information about the
  /// state of the tokens around and including the __VA_OPT__ token, necessary
  /// for stringification.
  void stringifyVAOPTContents(SmallVectorImpl<Token> &ResultToks,
                              const VAOptExpansionContext &VCtx,
                              SourceLocation VAOPTClosingParenLoc);

  /// Expand the arguments of a function-like macro so that we can quickly
  /// return preexpanded tokens from Tokens.
  void ExpandFunctionArguments();

  /// In microsoft compatibility mode, /##/ pastes
  /// together to form a comment that comments out everything in the current
  /// macro, other active macros, and anything left on the current physical
  /// source line of the expanded buffer.  Handle this by returning the
  /// first token on the next line.
  void HandleMicrosoftCommentPaste(Token &Tok, SourceLocation OpLoc);

  /// If \p loc is a FileID and points inside the current macro
  /// definition, returns the appropriate source location pointing at the
  /// macro expansion source location entry.
  SourceLocation getExpansionLocForMacroDefLoc(SourceLocation loc) const;

  /// Creates SLocEntries and updates the locations of macro argument
  /// tokens to their new expanded locations.
  ///
  /// \param ArgIdSpellLoc the location of the macro argument id inside the
  /// macro definition.
  void updateLocForMacroArgTokens(SourceLocation ArgIdSpellLoc,
                                  Token *begin_tokens, Token *end_tokens);

  /// Remove comma ahead of __VA_ARGS__, if present, according to compiler
  /// dialect settings.  Returns true if the comma is removed.
  bool MaybeRemoveCommaBeforeVaArgs(SmallVectorImpl<Token> &ResultToks,
                                    bool HasPasteOperator,
                                    MacroInfo *Macro, unsigned MacroArgNo,
                                    Preprocessor &PP);

  void PropagateLineStartLeadingSpaceInfo(Token &Result);
};

} // namespace clang

#endif // LLVM_CLANG_LEX_TOKENLEXER_H
