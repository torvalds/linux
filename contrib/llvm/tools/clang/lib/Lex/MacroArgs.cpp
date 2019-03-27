//===--- MacroArgs.cpp - Formal argument info for Macros ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MacroArgs interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/MacroArgs.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SaveAndRestore.h"
#include <algorithm>

using namespace clang;

/// MacroArgs ctor function - This destroys the vector passed in.
MacroArgs *MacroArgs::create(const MacroInfo *MI,
                             ArrayRef<Token> UnexpArgTokens,
                             bool VarargsElided, Preprocessor &PP) {
  assert(MI->isFunctionLike() &&
         "Can't have args for an object-like macro!");
  MacroArgs **ResultEnt = nullptr;
  unsigned ClosestMatch = ~0U;

  // See if we have an entry with a big enough argument list to reuse on the
  // free list.  If so, reuse it.
  for (MacroArgs **Entry = &PP.MacroArgCache; *Entry;
       Entry = &(*Entry)->ArgCache) {
    if ((*Entry)->NumUnexpArgTokens >= UnexpArgTokens.size() &&
        (*Entry)->NumUnexpArgTokens < ClosestMatch) {
      ResultEnt = Entry;

      // If we have an exact match, use it.
      if ((*Entry)->NumUnexpArgTokens == UnexpArgTokens.size())
        break;
      // Otherwise, use the best fit.
      ClosestMatch = (*Entry)->NumUnexpArgTokens;
    }
  }
  MacroArgs *Result;
  if (!ResultEnt) {
    // Allocate memory for a MacroArgs object with the lexer tokens at the end,
    // and construct the MacroArgs object.
    Result = new (
        llvm::safe_malloc(totalSizeToAlloc<Token>(UnexpArgTokens.size())))
        MacroArgs(UnexpArgTokens.size(), VarargsElided, MI->getNumParams());
  } else {
    Result = *ResultEnt;
    // Unlink this node from the preprocessors singly linked list.
    *ResultEnt = Result->ArgCache;
    Result->NumUnexpArgTokens = UnexpArgTokens.size();
    Result->VarargsElided = VarargsElided;
    Result->NumMacroArgs = MI->getNumParams();
  }

  // Copy the actual unexpanded tokens to immediately after the result ptr.
  if (!UnexpArgTokens.empty()) {
    static_assert(std::is_trivial<Token>::value,
                  "assume trivial copyability if copying into the "
                  "uninitialized array (as opposed to reusing a cached "
                  "MacroArgs)");
    std::copy(UnexpArgTokens.begin(), UnexpArgTokens.end(),
              Result->getTrailingObjects<Token>());
  }

  return Result;
}

/// destroy - Destroy and deallocate the memory for this object.
///
void MacroArgs::destroy(Preprocessor &PP) {
  StringifiedArgs.clear();

  // Don't clear PreExpArgTokens, just clear the entries.  Clearing the entries
  // would deallocate the element vectors.
  for (unsigned i = 0, e = PreExpArgTokens.size(); i != e; ++i)
    PreExpArgTokens[i].clear();

  // Add this to the preprocessor's free list.
  ArgCache = PP.MacroArgCache;
  PP.MacroArgCache = this;
}

/// deallocate - This should only be called by the Preprocessor when managing
/// its freelist.
MacroArgs *MacroArgs::deallocate() {
  MacroArgs *Next = ArgCache;

  // Run the dtor to deallocate the vectors.
  this->~MacroArgs();
  // Release the memory for the object.
  static_assert(std::is_trivially_destructible<Token>::value,
                "assume trivially destructible and forego destructors");
  free(this);

  return Next;
}


/// getArgLength - Given a pointer to an expanded or unexpanded argument,
/// return the number of tokens, not counting the EOF, that make up the
/// argument.
unsigned MacroArgs::getArgLength(const Token *ArgPtr) {
  unsigned NumArgTokens = 0;
  for (; ArgPtr->isNot(tok::eof); ++ArgPtr)
    ++NumArgTokens;
  return NumArgTokens;
}


/// getUnexpArgument - Return the unexpanded tokens for the specified formal.
///
const Token *MacroArgs::getUnexpArgument(unsigned Arg) const {

  assert(Arg < getNumMacroArguments() && "Invalid arg #");
  // The unexpanded argument tokens start immediately after the MacroArgs object
  // in memory.
  const Token *Start = getTrailingObjects<Token>();
  const Token *Result = Start;

  // Scan to find Arg.
  for (; Arg; ++Result) {
    assert(Result < Start+NumUnexpArgTokens && "Invalid arg #");
    if (Result->is(tok::eof))
      --Arg;
  }
  assert(Result < Start+NumUnexpArgTokens && "Invalid arg #");
  return Result;
}

// This function assumes that the variadic arguments are the tokens
// corresponding to the last parameter (ellipsis) - and since tokens are
// separated by the 'eof' token, if that is the only token corresponding to that
// last parameter, we know no variadic arguments were supplied.
bool MacroArgs::invokedWithVariadicArgument(const MacroInfo *const MI) const {
  if (!MI->isVariadic())
    return false;
  const int VariadicArgIndex = getNumMacroArguments() - 1;
  return getUnexpArgument(VariadicArgIndex)->isNot(tok::eof);
}

/// ArgNeedsPreexpansion - If we can prove that the argument won't be affected
/// by pre-expansion, return false.  Otherwise, conservatively return true.
bool MacroArgs::ArgNeedsPreexpansion(const Token *ArgTok,
                                     Preprocessor &PP) const {
  // If there are no identifiers in the argument list, or if the identifiers are
  // known to not be macros, pre-expansion won't modify it.
  for (; ArgTok->isNot(tok::eof); ++ArgTok)
    if (IdentifierInfo *II = ArgTok->getIdentifierInfo())
      if (II->hasMacroDefinition())
        // Return true even though the macro could be a function-like macro
        // without a following '(' token, or could be disabled, or not visible.
        return true;
  return false;
}

/// getPreExpArgument - Return the pre-expanded form of the specified
/// argument.
const std::vector<Token> &MacroArgs::getPreExpArgument(unsigned Arg,
                                                       Preprocessor &PP) {
  assert(Arg < getNumMacroArguments() && "Invalid argument number!");

  // If we have already computed this, return it.
  if (PreExpArgTokens.size() < getNumMacroArguments())
    PreExpArgTokens.resize(getNumMacroArguments());

  std::vector<Token> &Result = PreExpArgTokens[Arg];
  if (!Result.empty()) return Result;

  SaveAndRestore<bool> PreExpandingMacroArgs(PP.InMacroArgPreExpansion, true);

  const Token *AT = getUnexpArgument(Arg);
  unsigned NumToks = getArgLength(AT)+1;  // Include the EOF.

  // Otherwise, we have to pre-expand this argument, populating Result.  To do
  // this, we set up a fake TokenLexer to lex from the unexpanded argument
  // list.  With this installed, we lex expanded tokens until we hit the EOF
  // token at the end of the unexp list.
  PP.EnterTokenStream(AT, NumToks, false /*disable expand*/,
                      false /*owns tokens*/);

  // Lex all of the macro-expanded tokens into Result.
  do {
    Result.push_back(Token());
    Token &Tok = Result.back();
    PP.Lex(Tok);
  } while (Result.back().isNot(tok::eof));

  // Pop the token stream off the top of the stack.  We know that the internal
  // pointer inside of it is to the "end" of the token stream, but the stack
  // will not otherwise be popped until the next token is lexed.  The problem is
  // that the token may be lexed sometime after the vector of tokens itself is
  // destroyed, which would be badness.
  if (PP.InCachingLexMode())
    PP.ExitCachingLexMode();
  PP.RemoveTopOfLexerStack();
  return Result;
}


/// StringifyArgument - Implement C99 6.10.3.2p2, converting a sequence of
/// tokens into the literal string token that should be produced by the C #
/// preprocessor operator.  If Charify is true, then it should be turned into
/// a character literal for the Microsoft charize (#@) extension.
///
Token MacroArgs::StringifyArgument(const Token *ArgToks,
                                   Preprocessor &PP, bool Charify,
                                   SourceLocation ExpansionLocStart,
                                   SourceLocation ExpansionLocEnd) {
  Token Tok;
  Tok.startToken();
  Tok.setKind(Charify ? tok::char_constant : tok::string_literal);

  const Token *ArgTokStart = ArgToks;

  // Stringify all the tokens.
  SmallString<128> Result;
  Result += "\"";

  bool isFirst = true;
  for (; ArgToks->isNot(tok::eof); ++ArgToks) {
    const Token &Tok = *ArgToks;
    if (!isFirst && (Tok.hasLeadingSpace() || Tok.isAtStartOfLine()))
      Result += ' ';
    isFirst = false;

    // If this is a string or character constant, escape the token as specified
    // by 6.10.3.2p2.
    if (tok::isStringLiteral(Tok.getKind()) || // "foo", u8R"x(foo)x"_bar, etc.
        Tok.is(tok::char_constant) ||          // 'x'
        Tok.is(tok::wide_char_constant) ||     // L'x'.
        Tok.is(tok::utf8_char_constant) ||     // u8'x'.
        Tok.is(tok::utf16_char_constant) ||    // u'x'.
        Tok.is(tok::utf32_char_constant)) {    // U'x'.
      bool Invalid = false;
      std::string TokStr = PP.getSpelling(Tok, &Invalid);
      if (!Invalid) {
        std::string Str = Lexer::Stringify(TokStr);
        Result.append(Str.begin(), Str.end());
      }
    } else if (Tok.is(tok::code_completion)) {
      PP.CodeCompleteNaturalLanguage();
    } else {
      // Otherwise, just append the token.  Do some gymnastics to get the token
      // in place and avoid copies where possible.
      unsigned CurStrLen = Result.size();
      Result.resize(CurStrLen+Tok.getLength());
      const char *BufPtr = Result.data() + CurStrLen;
      bool Invalid = false;
      unsigned ActualTokLen = PP.getSpelling(Tok, BufPtr, &Invalid);

      if (!Invalid) {
        // If getSpelling returned a pointer to an already uniqued version of
        // the string instead of filling in BufPtr, memcpy it onto our string.
        if (ActualTokLen && BufPtr != &Result[CurStrLen])
          memcpy(&Result[CurStrLen], BufPtr, ActualTokLen);

        // If the token was dirty, the spelling may be shorter than the token.
        if (ActualTokLen != Tok.getLength())
          Result.resize(CurStrLen+ActualTokLen);
      }
    }
  }

  // If the last character of the string is a \, and if it isn't escaped, this
  // is an invalid string literal, diagnose it as specified in C99.
  if (Result.back() == '\\') {
    // Count the number of consecutive \ characters.  If even, then they are
    // just escaped backslashes, otherwise it's an error.
    unsigned FirstNonSlash = Result.size()-2;
    // Guaranteed to find the starting " if nothing else.
    while (Result[FirstNonSlash] == '\\')
      --FirstNonSlash;
    if ((Result.size()-1-FirstNonSlash) & 1) {
      // Diagnose errors for things like: #define F(X) #X   /   F(\)
      PP.Diag(ArgToks[-1], diag::pp_invalid_string_literal);
      Result.pop_back();  // remove one of the \'s.
    }
  }
  Result += '"';

  // If this is the charify operation and the result is not a legal character
  // constant, diagnose it.
  if (Charify) {
    // First step, turn double quotes into single quotes:
    Result[0] = '\'';
    Result[Result.size()-1] = '\'';

    // Check for bogus character.
    bool isBad = false;
    if (Result.size() == 3)
      isBad = Result[1] == '\'';   // ''' is not legal. '\' already fixed above.
    else
      isBad = (Result.size() != 4 || Result[1] != '\\');  // Not '\x'

    if (isBad) {
      PP.Diag(ArgTokStart[0], diag::err_invalid_character_to_charify);
      Result = "' '";  // Use something arbitrary, but legal.
    }
  }

  PP.CreateString(Result, Tok,
                  ExpansionLocStart, ExpansionLocEnd);
  return Tok;
}

/// getStringifiedArgument - Compute, cache, and return the specified argument
/// that has been 'stringified' as required by the # operator.
const Token &MacroArgs::getStringifiedArgument(unsigned ArgNo,
                                               Preprocessor &PP,
                                               SourceLocation ExpansionLocStart,
                                               SourceLocation ExpansionLocEnd) {
  assert(ArgNo < getNumMacroArguments() && "Invalid argument number!");
  if (StringifiedArgs.empty())
    StringifiedArgs.resize(getNumMacroArguments(), {});

  if (StringifiedArgs[ArgNo].isNot(tok::string_literal))
    StringifiedArgs[ArgNo] = StringifyArgument(getUnexpArgument(ArgNo), PP,
                                               /*Charify=*/false,
                                               ExpansionLocStart,
                                               ExpansionLocEnd);
  return StringifiedArgs[ArgNo];
}
