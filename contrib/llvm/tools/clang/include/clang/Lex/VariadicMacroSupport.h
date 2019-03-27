//===- VariadicMacroSupport.h - state machines and scope guards -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines support types to help with preprocessing variadic macro
// (i.e. macros that use: ellipses __VA_ARGS__ ) definitions and
// expansions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_VARIADICMACROSUPPORT_H
#define LLVM_CLANG_LEX_VARIADICMACROSUPPORT_H

#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {
  class Preprocessor;

  /// An RAII class that tracks when the Preprocessor starts and stops lexing
  /// the definition of a (ISO C/C++) variadic macro.  As an example, this is
  /// useful for unpoisoning and repoisoning certain identifiers (such as
  /// __VA_ARGS__) that are only allowed in this context.  Also, being a friend
  /// of the Preprocessor class allows it to access PP's cached identifiers
  /// directly (as opposed to performing a lookup each time).
  class VariadicMacroScopeGuard {
    const Preprocessor &PP;
    IdentifierInfo *const Ident__VA_ARGS__;
    IdentifierInfo *const Ident__VA_OPT__;

  public:
    VariadicMacroScopeGuard(const Preprocessor &P)
        : PP(P), Ident__VA_ARGS__(PP.Ident__VA_ARGS__),
          Ident__VA_OPT__(PP.Ident__VA_OPT__) {
      assert(Ident__VA_ARGS__->isPoisoned() && "__VA_ARGS__ should be poisoned "
                                              "outside an ISO C/C++ variadic "
                                              "macro definition!");
      assert(
          !Ident__VA_OPT__ ||
          (Ident__VA_OPT__->isPoisoned() && "__VA_OPT__ should be poisoned!"));
    }

    /// Client code should call this function just before the Preprocessor is
    /// about to Lex tokens from the definition of a variadic (ISO C/C++) macro.
    void enterScope() {
      Ident__VA_ARGS__->setIsPoisoned(false);
      if (Ident__VA_OPT__)
        Ident__VA_OPT__->setIsPoisoned(false);
    }

    /// Client code should call this function as soon as the Preprocessor has
    /// either completed lexing the macro's definition tokens, or an error
    /// occurred and the context is being exited.  This function is idempotent
    /// (might be explicitly called, and then reinvoked via the destructor).
    void exitScope() {
      Ident__VA_ARGS__->setIsPoisoned(true);
      if (Ident__VA_OPT__)
        Ident__VA_OPT__->setIsPoisoned(true);
    }

    ~VariadicMacroScopeGuard() { exitScope(); }
  };

  /// A class for tracking whether we're inside a VA_OPT during a
  /// traversal of the tokens of a variadic macro definition.
  class VAOptDefinitionContext {
    /// Contains all the locations of so far unmatched lparens.
    SmallVector<SourceLocation, 8> UnmatchedOpeningParens;

    const IdentifierInfo *const Ident__VA_OPT__;


  public:
    VAOptDefinitionContext(Preprocessor &PP)
        : Ident__VA_OPT__(PP.Ident__VA_OPT__) {}

    bool isVAOptToken(const Token &T) const {
      return Ident__VA_OPT__ && T.getIdentifierInfo() == Ident__VA_OPT__;
    }

    /// Returns true if we have seen the __VA_OPT__ and '(' but before having
    /// seen the matching ')'.
    bool isInVAOpt() const { return UnmatchedOpeningParens.size(); }

    /// Call this function as soon as you see __VA_OPT__ and '('.
    void sawVAOptFollowedByOpeningParens(const SourceLocation LParenLoc) {
      assert(!isInVAOpt() && "Must NOT be within VAOPT context to call this");
      UnmatchedOpeningParens.push_back(LParenLoc);

    }

    SourceLocation getUnmatchedOpeningParenLoc() const {
      assert(isInVAOpt() && "Must be within VAOPT context to call this");
      return UnmatchedOpeningParens.back();
    }

    /// Call this function each time an rparen is seen.  It returns true only if
    /// the rparen that was just seen was the eventual (non-nested) closing
    /// paren for VAOPT, and ejects us out of the VAOPT context.
    bool sawClosingParen() {
      assert(isInVAOpt() && "Must be within VAOPT context to call this");
      UnmatchedOpeningParens.pop_back();
      return !UnmatchedOpeningParens.size();
    }

    /// Call this function each time an lparen is seen.
    void sawOpeningParen(SourceLocation LParenLoc) {
      assert(isInVAOpt() && "Must be within VAOPT context to call this");
      UnmatchedOpeningParens.push_back(LParenLoc);
    }

  };

  /// A class for tracking whether we're inside a VA_OPT during a
  /// traversal of the tokens of a macro during macro expansion.
  class VAOptExpansionContext : VAOptDefinitionContext {

    Token SyntheticEOFToken;

    // The (spelling) location of the current __VA_OPT__ in the replacement list
    // of the function-like macro being expanded.
    SourceLocation VAOptLoc;

    // NumOfTokensPriorToVAOpt : when != -1, contains the index *of* the first
    // token of the current VAOPT contents (so we know where to start eager
    // token-pasting and stringification) *within*  the substituted tokens of
    // the function-like macro's new replacement list.
    int NumOfTokensPriorToVAOpt = -1;

    unsigned LeadingSpaceForStringifiedToken : 1;

    unsigned StringifyBefore : 1;
    unsigned CharifyBefore : 1;


    bool hasStringifyBefore() const {
      assert(!isReset() &&
             "Must only be called if the state has not been reset");
      return StringifyBefore;
    }

    bool isReset() const {
      return NumOfTokensPriorToVAOpt == -1 ||
             VAOptLoc.isInvalid();
    }

  public:
    VAOptExpansionContext(Preprocessor &PP)
        : VAOptDefinitionContext(PP), LeadingSpaceForStringifiedToken(false),
          StringifyBefore(false), CharifyBefore(false) {
      SyntheticEOFToken.startToken();
      SyntheticEOFToken.setKind(tok::eof);
    }

    void reset() {
      VAOptLoc = SourceLocation();
      NumOfTokensPriorToVAOpt = -1;
      LeadingSpaceForStringifiedToken = false;
      StringifyBefore = false;
      CharifyBefore = false;
    }

    const Token &getEOFTok() const { return SyntheticEOFToken; }

    void sawHashOrHashAtBefore(const bool HasLeadingSpace,
                               const bool IsHashAt) {

      StringifyBefore = !IsHashAt;
      CharifyBefore = IsHashAt;
      LeadingSpaceForStringifiedToken = HasLeadingSpace;
    }



    bool hasCharifyBefore() const {
      assert(!isReset() &&
             "Must only be called if the state has not been reset");
      return CharifyBefore;
    }
    bool hasStringifyOrCharifyBefore() const {
      return hasStringifyBefore() || hasCharifyBefore();
    }

    unsigned int getNumberOfTokensPriorToVAOpt() const {
      assert(!isReset() &&
             "Must only be called if the state has not been reset");
      return NumOfTokensPriorToVAOpt;
    }

    bool getLeadingSpaceForStringifiedToken() const {
      assert(hasStringifyBefore() &&
             "Must only be called if this has been marked for stringification");
      return LeadingSpaceForStringifiedToken;
    }

    void sawVAOptFollowedByOpeningParens(const SourceLocation VAOptLoc,
                                         const unsigned int NumPriorTokens) {
      assert(VAOptLoc.isFileID() && "Must not come from a macro expansion");
      assert(isReset() && "Must only be called if the state has been reset");
      VAOptDefinitionContext::sawVAOptFollowedByOpeningParens(SourceLocation());
      this->VAOptLoc = VAOptLoc;
      NumOfTokensPriorToVAOpt = NumPriorTokens;
      assert(NumOfTokensPriorToVAOpt > -1 &&
             "Too many prior tokens");
    }

    SourceLocation getVAOptLoc() const {
      assert(!isReset() &&
             "Must only be called if the state has not been reset");
      assert(VAOptLoc.isValid() && "__VA_OPT__ location must be valid");
      return VAOptLoc;
    }
    using VAOptDefinitionContext::isVAOptToken;
    using VAOptDefinitionContext::isInVAOpt;
    using VAOptDefinitionContext::sawClosingParen;
    using VAOptDefinitionContext::sawOpeningParen;

  };
}  // end namespace clang

#endif
