//===--- PPExpressions.cpp - Preprocessor Expression Evaluation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Preprocessor::EvaluateDirectiveExpression method,
// which parses and evaluates integer constant expressions for #if directives.
//
//===----------------------------------------------------------------------===//
//
// FIXME: implement testing for #assert's.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/CodeCompletionHandler.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"
#include <cassert>

using namespace clang;

namespace {

/// PPValue - Represents the value of a subexpression of a preprocessor
/// conditional and the source range covered by it.
class PPValue {
  SourceRange Range;
  IdentifierInfo *II;

public:
  llvm::APSInt Val;

  // Default ctor - Construct an 'invalid' PPValue.
  PPValue(unsigned BitWidth) : Val(BitWidth) {}

  // If this value was produced by directly evaluating an identifier, produce
  // that identifier.
  IdentifierInfo *getIdentifier() const { return II; }
  void setIdentifier(IdentifierInfo *II) { this->II = II; }

  unsigned getBitWidth() const { return Val.getBitWidth(); }
  bool isUnsigned() const { return Val.isUnsigned(); }

  SourceRange getRange() const { return Range; }

  void setRange(SourceLocation L) { Range.setBegin(L); Range.setEnd(L); }
  void setRange(SourceLocation B, SourceLocation E) {
    Range.setBegin(B); Range.setEnd(E);
  }
  void setBegin(SourceLocation L) { Range.setBegin(L); }
  void setEnd(SourceLocation L) { Range.setEnd(L); }
};

} // end anonymous namespace

static bool EvaluateDirectiveSubExpr(PPValue &LHS, unsigned MinPrec,
                                     Token &PeekTok, bool ValueLive,
                                     bool &IncludedUndefinedIds,
                                     Preprocessor &PP);

/// DefinedTracker - This struct is used while parsing expressions to keep track
/// of whether !defined(X) has been seen.
///
/// With this simple scheme, we handle the basic forms:
///    !defined(X)   and !defined X
/// but we also trivially handle (silly) stuff like:
///    !!!defined(X) and +!defined(X) and !+!+!defined(X) and !(defined(X)).
struct DefinedTracker {
  /// Each time a Value is evaluated, it returns information about whether the
  /// parsed value is of the form defined(X), !defined(X) or is something else.
  enum TrackerState {
    DefinedMacro,        // defined(X)
    NotDefinedMacro,     // !defined(X)
    Unknown              // Something else.
  } State;
  /// TheMacro - When the state is DefinedMacro or NotDefinedMacro, this
  /// indicates the macro that was checked.
  IdentifierInfo *TheMacro;
  bool IncludedUndefinedIds = false;
};

/// EvaluateDefined - Process a 'defined(sym)' expression.
static bool EvaluateDefined(PPValue &Result, Token &PeekTok, DefinedTracker &DT,
                            bool ValueLive, Preprocessor &PP) {
  SourceLocation beginLoc(PeekTok.getLocation());
  Result.setBegin(beginLoc);

  // Get the next token, don't expand it.
  PP.LexUnexpandedNonComment(PeekTok);

  // Two options, it can either be a pp-identifier or a (.
  SourceLocation LParenLoc;
  if (PeekTok.is(tok::l_paren)) {
    // Found a paren, remember we saw it and skip it.
    LParenLoc = PeekTok.getLocation();
    PP.LexUnexpandedNonComment(PeekTok);
  }

  if (PeekTok.is(tok::code_completion)) {
    if (PP.getCodeCompletionHandler())
      PP.getCodeCompletionHandler()->CodeCompleteMacroName(false);
    PP.setCodeCompletionReached();
    PP.LexUnexpandedNonComment(PeekTok);
  }

  // If we don't have a pp-identifier now, this is an error.
  if (PP.CheckMacroName(PeekTok, MU_Other))
    return true;

  // Otherwise, we got an identifier, is it defined to something?
  IdentifierInfo *II = PeekTok.getIdentifierInfo();
  MacroDefinition Macro = PP.getMacroDefinition(II);
  Result.Val = !!Macro;
  Result.Val.setIsUnsigned(false); // Result is signed intmax_t.
  DT.IncludedUndefinedIds = !Macro;

  // If there is a macro, mark it used.
  if (Result.Val != 0 && ValueLive)
    PP.markMacroAsUsed(Macro.getMacroInfo());

  // Save macro token for callback.
  Token macroToken(PeekTok);

  // If we are in parens, ensure we have a trailing ).
  if (LParenLoc.isValid()) {
    // Consume identifier.
    Result.setEnd(PeekTok.getLocation());
    PP.LexUnexpandedNonComment(PeekTok);

    if (PeekTok.isNot(tok::r_paren)) {
      PP.Diag(PeekTok.getLocation(), diag::err_pp_expected_after)
          << "'defined'" << tok::r_paren;
      PP.Diag(LParenLoc, diag::note_matching) << tok::l_paren;
      return true;
    }
    // Consume the ).
    Result.setEnd(PeekTok.getLocation());
    PP.LexNonComment(PeekTok);
  } else {
    // Consume identifier.
    Result.setEnd(PeekTok.getLocation());
    PP.LexNonComment(PeekTok);
  }

  // [cpp.cond]p4:
  //   Prior to evaluation, macro invocations in the list of preprocessing
  //   tokens that will become the controlling constant expression are replaced
  //   (except for those macro names modified by the 'defined' unary operator),
  //   just as in normal text. If the token 'defined' is generated as a result
  //   of this replacement process or use of the 'defined' unary operator does
  //   not match one of the two specified forms prior to macro replacement, the
  //   behavior is undefined.
  // This isn't an idle threat, consider this program:
  //   #define FOO
  //   #define BAR defined(FOO)
  //   #if BAR
  //   ...
  //   #else
  //   ...
  //   #endif
  // clang and gcc will pick the #if branch while Visual Studio will take the
  // #else branch.  Emit a warning about this undefined behavior.
  if (beginLoc.isMacroID()) {
    bool IsFunctionTypeMacro =
        PP.getSourceManager()
            .getSLocEntry(PP.getSourceManager().getFileID(beginLoc))
            .getExpansion()
            .isFunctionMacroExpansion();
    // For object-type macros, it's easy to replace
    //   #define FOO defined(BAR)
    // with
    //   #if defined(BAR)
    //   #define FOO 1
    //   #else
    //   #define FOO 0
    //   #endif
    // and doing so makes sense since compilers handle this differently in
    // practice (see example further up).  But for function-type macros,
    // there is no good way to write
    //   # define FOO(x) (defined(M_ ## x) && M_ ## x)
    // in a different way, and compilers seem to agree on how to behave here.
    // So warn by default on object-type macros, but only warn in -pedantic
    // mode on function-type macros.
    if (IsFunctionTypeMacro)
      PP.Diag(beginLoc, diag::warn_defined_in_function_type_macro);
    else
      PP.Diag(beginLoc, diag::warn_defined_in_object_type_macro);
  }

  // Invoke the 'defined' callback.
  if (PPCallbacks *Callbacks = PP.getPPCallbacks()) {
    Callbacks->Defined(macroToken, Macro,
                       SourceRange(beginLoc, PeekTok.getLocation()));
  }

  // Success, remember that we saw defined(X).
  DT.State = DefinedTracker::DefinedMacro;
  DT.TheMacro = II;
  return false;
}

/// EvaluateValue - Evaluate the token PeekTok (and any others needed) and
/// return the computed value in Result.  Return true if there was an error
/// parsing.  This function also returns information about the form of the
/// expression in DT.  See above for information on what DT means.
///
/// If ValueLive is false, then this value is being evaluated in a context where
/// the result is not used.  As such, avoid diagnostics that relate to
/// evaluation.
static bool EvaluateValue(PPValue &Result, Token &PeekTok, DefinedTracker &DT,
                          bool ValueLive, Preprocessor &PP) {
  DT.State = DefinedTracker::Unknown;

  Result.setIdentifier(nullptr);

  if (PeekTok.is(tok::code_completion)) {
    if (PP.getCodeCompletionHandler())
      PP.getCodeCompletionHandler()->CodeCompletePreprocessorExpression();
    PP.setCodeCompletionReached();
    PP.LexNonComment(PeekTok);
  }

  switch (PeekTok.getKind()) {
  default:
    // If this token's spelling is a pp-identifier, check to see if it is
    // 'defined' or if it is a macro.  Note that we check here because many
    // keywords are pp-identifiers, so we can't check the kind.
    if (IdentifierInfo *II = PeekTok.getIdentifierInfo()) {
      // Handle "defined X" and "defined(X)".
      if (II->isStr("defined"))
        return EvaluateDefined(Result, PeekTok, DT, ValueLive, PP);

      if (!II->isCPlusPlusOperatorKeyword()) {
        // If this identifier isn't 'defined' or one of the special
        // preprocessor keywords and it wasn't macro expanded, it turns
        // into a simple 0
        if (ValueLive)
          PP.Diag(PeekTok, diag::warn_pp_undef_identifier) << II;
        Result.Val = 0;
        Result.Val.setIsUnsigned(false); // "0" is signed intmax_t 0.
        Result.setIdentifier(II);
        Result.setRange(PeekTok.getLocation());
        DT.IncludedUndefinedIds = true;
        PP.LexNonComment(PeekTok);
        return false;
      }
    }
    PP.Diag(PeekTok, diag::err_pp_expr_bad_token_start_expr);
    return true;
  case tok::eod:
  case tok::r_paren:
    // If there is no expression, report and exit.
    PP.Diag(PeekTok, diag::err_pp_expected_value_in_expr);
    return true;
  case tok::numeric_constant: {
    SmallString<64> IntegerBuffer;
    bool NumberInvalid = false;
    StringRef Spelling = PP.getSpelling(PeekTok, IntegerBuffer,
                                              &NumberInvalid);
    if (NumberInvalid)
      return true; // a diagnostic was already reported

    NumericLiteralParser Literal(Spelling, PeekTok.getLocation(), PP);
    if (Literal.hadError)
      return true; // a diagnostic was already reported.

    if (Literal.isFloatingLiteral() || Literal.isImaginary) {
      PP.Diag(PeekTok, diag::err_pp_illegal_floating_literal);
      return true;
    }
    assert(Literal.isIntegerLiteral() && "Unknown ppnumber");

    // Complain about, and drop, any ud-suffix.
    if (Literal.hasUDSuffix())
      PP.Diag(PeekTok, diag::err_pp_invalid_udl) << /*integer*/1;

    // 'long long' is a C99 or C++11 feature.
    if (!PP.getLangOpts().C99 && Literal.isLongLong) {
      if (PP.getLangOpts().CPlusPlus)
        PP.Diag(PeekTok,
             PP.getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_longlong : diag::ext_cxx11_longlong);
      else
        PP.Diag(PeekTok, diag::ext_c99_longlong);
    }

    // Parse the integer literal into Result.
    if (Literal.GetIntegerValue(Result.Val)) {
      // Overflow parsing integer literal.
      if (ValueLive)
        PP.Diag(PeekTok, diag::err_integer_literal_too_large)
            << /* Unsigned */ 1;
      Result.Val.setIsUnsigned(true);
    } else {
      // Set the signedness of the result to match whether there was a U suffix
      // or not.
      Result.Val.setIsUnsigned(Literal.isUnsigned);

      // Detect overflow based on whether the value is signed.  If signed
      // and if the value is too large, emit a warning "integer constant is so
      // large that it is unsigned" e.g. on 12345678901234567890 where intmax_t
      // is 64-bits.
      if (!Literal.isUnsigned && Result.Val.isNegative()) {
        // Octal, hexadecimal, and binary literals are implicitly unsigned if
        // the value does not fit into a signed integer type.
        if (ValueLive && Literal.getRadix() == 10)
          PP.Diag(PeekTok, diag::ext_integer_literal_too_large_for_signed);
        Result.Val.setIsUnsigned(true);
      }
    }

    // Consume the token.
    Result.setRange(PeekTok.getLocation());
    PP.LexNonComment(PeekTok);
    return false;
  }
  case tok::char_constant:          // 'x'
  case tok::wide_char_constant:     // L'x'
  case tok::utf8_char_constant:     // u8'x'
  case tok::utf16_char_constant:    // u'x'
  case tok::utf32_char_constant: {  // U'x'
    // Complain about, and drop, any ud-suffix.
    if (PeekTok.hasUDSuffix())
      PP.Diag(PeekTok, diag::err_pp_invalid_udl) << /*character*/0;

    SmallString<32> CharBuffer;
    bool CharInvalid = false;
    StringRef ThisTok = PP.getSpelling(PeekTok, CharBuffer, &CharInvalid);
    if (CharInvalid)
      return true;

    CharLiteralParser Literal(ThisTok.begin(), ThisTok.end(),
                              PeekTok.getLocation(), PP, PeekTok.getKind());
    if (Literal.hadError())
      return true;  // A diagnostic was already emitted.

    // Character literals are always int or wchar_t, expand to intmax_t.
    const TargetInfo &TI = PP.getTargetInfo();
    unsigned NumBits;
    if (Literal.isMultiChar())
      NumBits = TI.getIntWidth();
    else if (Literal.isWide())
      NumBits = TI.getWCharWidth();
    else if (Literal.isUTF16())
      NumBits = TI.getChar16Width();
    else if (Literal.isUTF32())
      NumBits = TI.getChar32Width();
    else // char or char8_t
      NumBits = TI.getCharWidth();

    // Set the width.
    llvm::APSInt Val(NumBits);
    // Set the value.
    Val = Literal.getValue();
    // Set the signedness. UTF-16 and UTF-32 are always unsigned
    if (Literal.isWide())
      Val.setIsUnsigned(!TargetInfo::isTypeSigned(TI.getWCharType()));
    else if (!Literal.isUTF16() && !Literal.isUTF32())
      Val.setIsUnsigned(!PP.getLangOpts().CharIsSigned);

    if (Result.Val.getBitWidth() > Val.getBitWidth()) {
      Result.Val = Val.extend(Result.Val.getBitWidth());
    } else {
      assert(Result.Val.getBitWidth() == Val.getBitWidth() &&
             "intmax_t smaller than char/wchar_t?");
      Result.Val = Val;
    }

    // Consume the token.
    Result.setRange(PeekTok.getLocation());
    PP.LexNonComment(PeekTok);
    return false;
  }
  case tok::l_paren: {
    SourceLocation Start = PeekTok.getLocation();
    PP.LexNonComment(PeekTok);  // Eat the (.
    // Parse the value and if there are any binary operators involved, parse
    // them.
    if (EvaluateValue(Result, PeekTok, DT, ValueLive, PP)) return true;

    // If this is a silly value like (X), which doesn't need parens, check for
    // !(defined X).
    if (PeekTok.is(tok::r_paren)) {
      // Just use DT unmodified as our result.
    } else {
      // Otherwise, we have something like (x+y), and we consumed '(x'.
      if (EvaluateDirectiveSubExpr(Result, 1, PeekTok, ValueLive,
                                   DT.IncludedUndefinedIds, PP))
        return true;

      if (PeekTok.isNot(tok::r_paren)) {
        PP.Diag(PeekTok.getLocation(), diag::err_pp_expected_rparen)
          << Result.getRange();
        PP.Diag(Start, diag::note_matching) << tok::l_paren;
        return true;
      }
      DT.State = DefinedTracker::Unknown;
    }
    Result.setRange(Start, PeekTok.getLocation());
    Result.setIdentifier(nullptr);
    PP.LexNonComment(PeekTok);  // Eat the ).
    return false;
  }
  case tok::plus: {
    SourceLocation Start = PeekTok.getLocation();
    // Unary plus doesn't modify the value.
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, ValueLive, PP)) return true;
    Result.setBegin(Start);
    Result.setIdentifier(nullptr);
    return false;
  }
  case tok::minus: {
    SourceLocation Loc = PeekTok.getLocation();
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, ValueLive, PP)) return true;
    Result.setBegin(Loc);
    Result.setIdentifier(nullptr);

    // C99 6.5.3.3p3: The sign of the result matches the sign of the operand.
    Result.Val = -Result.Val;

    // -MININT is the only thing that overflows.  Unsigned never overflows.
    bool Overflow = !Result.isUnsigned() && Result.Val.isMinSignedValue();

    // If this operator is live and overflowed, report the issue.
    if (Overflow && ValueLive)
      PP.Diag(Loc, diag::warn_pp_expr_overflow) << Result.getRange();

    DT.State = DefinedTracker::Unknown;
    return false;
  }

  case tok::tilde: {
    SourceLocation Start = PeekTok.getLocation();
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, ValueLive, PP)) return true;
    Result.setBegin(Start);
    Result.setIdentifier(nullptr);

    // C99 6.5.3.3p4: The sign of the result matches the sign of the operand.
    Result.Val = ~Result.Val;
    DT.State = DefinedTracker::Unknown;
    return false;
  }

  case tok::exclaim: {
    SourceLocation Start = PeekTok.getLocation();
    PP.LexNonComment(PeekTok);
    if (EvaluateValue(Result, PeekTok, DT, ValueLive, PP)) return true;
    Result.setBegin(Start);
    Result.Val = !Result.Val;
    // C99 6.5.3.3p5: The sign of the result is 'int', aka it is signed.
    Result.Val.setIsUnsigned(false);
    Result.setIdentifier(nullptr);

    if (DT.State == DefinedTracker::DefinedMacro)
      DT.State = DefinedTracker::NotDefinedMacro;
    else if (DT.State == DefinedTracker::NotDefinedMacro)
      DT.State = DefinedTracker::DefinedMacro;
    return false;
  }
  case tok::kw_true:
  case tok::kw_false:
    Result.Val = PeekTok.getKind() == tok::kw_true;
    Result.Val.setIsUnsigned(false); // "0" is signed intmax_t 0.
    Result.setIdentifier(PeekTok.getIdentifierInfo());
    Result.setRange(PeekTok.getLocation());
    PP.LexNonComment(PeekTok);
    return false;

  // FIXME: Handle #assert
  }
}

/// getPrecedence - Return the precedence of the specified binary operator
/// token.  This returns:
///   ~0 - Invalid token.
///   14 -> 3 - various operators.
///    0 - 'eod' or ')'
static unsigned getPrecedence(tok::TokenKind Kind) {
  switch (Kind) {
  default: return ~0U;
  case tok::percent:
  case tok::slash:
  case tok::star:                 return 14;
  case tok::plus:
  case tok::minus:                return 13;
  case tok::lessless:
  case tok::greatergreater:       return 12;
  case tok::lessequal:
  case tok::less:
  case tok::greaterequal:
  case tok::greater:              return 11;
  case tok::exclaimequal:
  case tok::equalequal:           return 10;
  case tok::amp:                  return 9;
  case tok::caret:                return 8;
  case tok::pipe:                 return 7;
  case tok::ampamp:               return 6;
  case tok::pipepipe:             return 5;
  case tok::question:             return 4;
  case tok::comma:                return 3;
  case tok::colon:                return 2;
  case tok::r_paren:              return 0;// Lowest priority, end of expr.
  case tok::eod:                  return 0;// Lowest priority, end of directive.
  }
}

static void diagnoseUnexpectedOperator(Preprocessor &PP, PPValue &LHS,
                                       Token &Tok) {
  if (Tok.is(tok::l_paren) && LHS.getIdentifier())
    PP.Diag(LHS.getRange().getBegin(), diag::err_pp_expr_bad_token_lparen)
        << LHS.getIdentifier();
  else
    PP.Diag(Tok.getLocation(), diag::err_pp_expr_bad_token_binop)
        << LHS.getRange();
}

/// EvaluateDirectiveSubExpr - Evaluate the subexpression whose first token is
/// PeekTok, and whose precedence is PeekPrec.  This returns the result in LHS.
///
/// If ValueLive is false, then this value is being evaluated in a context where
/// the result is not used.  As such, avoid diagnostics that relate to
/// evaluation, such as division by zero warnings.
static bool EvaluateDirectiveSubExpr(PPValue &LHS, unsigned MinPrec,
                                     Token &PeekTok, bool ValueLive,
                                     bool &IncludedUndefinedIds,
                                     Preprocessor &PP) {
  unsigned PeekPrec = getPrecedence(PeekTok.getKind());
  // If this token isn't valid, report the error.
  if (PeekPrec == ~0U) {
    diagnoseUnexpectedOperator(PP, LHS, PeekTok);
    return true;
  }

  while (true) {
    // If this token has a lower precedence than we are allowed to parse, return
    // it so that higher levels of the recursion can parse it.
    if (PeekPrec < MinPrec)
      return false;

    tok::TokenKind Operator = PeekTok.getKind();

    // If this is a short-circuiting operator, see if the RHS of the operator is
    // dead.  Note that this cannot just clobber ValueLive.  Consider
    // "0 && 1 ? 4 : 1 / 0", which is parsed as "(0 && 1) ? 4 : (1 / 0)".  In
    // this example, the RHS of the && being dead does not make the rest of the
    // expr dead.
    bool RHSIsLive;
    if (Operator == tok::ampamp && LHS.Val == 0)
      RHSIsLive = false;   // RHS of "0 && x" is dead.
    else if (Operator == tok::pipepipe && LHS.Val != 0)
      RHSIsLive = false;   // RHS of "1 || x" is dead.
    else if (Operator == tok::question && LHS.Val == 0)
      RHSIsLive = false;   // RHS (x) of "0 ? x : y" is dead.
    else
      RHSIsLive = ValueLive;

    // Consume the operator, remembering the operator's location for reporting.
    SourceLocation OpLoc = PeekTok.getLocation();
    PP.LexNonComment(PeekTok);

    PPValue RHS(LHS.getBitWidth());
    // Parse the RHS of the operator.
    DefinedTracker DT;
    if (EvaluateValue(RHS, PeekTok, DT, RHSIsLive, PP)) return true;
    IncludedUndefinedIds = DT.IncludedUndefinedIds;

    // Remember the precedence of this operator and get the precedence of the
    // operator immediately to the right of the RHS.
    unsigned ThisPrec = PeekPrec;
    PeekPrec = getPrecedence(PeekTok.getKind());

    // If this token isn't valid, report the error.
    if (PeekPrec == ~0U) {
      diagnoseUnexpectedOperator(PP, RHS, PeekTok);
      return true;
    }

    // Decide whether to include the next binop in this subexpression.  For
    // example, when parsing x+y*z and looking at '*', we want to recursively
    // handle y*z as a single subexpression.  We do this because the precedence
    // of * is higher than that of +.  The only strange case we have to handle
    // here is for the ?: operator, where the precedence is actually lower than
    // the LHS of the '?'.  The grammar rule is:
    //
    // conditional-expression ::=
    //    logical-OR-expression ? expression : conditional-expression
    // where 'expression' is actually comma-expression.
    unsigned RHSPrec;
    if (Operator == tok::question)
      // The RHS of "?" should be maximally consumed as an expression.
      RHSPrec = getPrecedence(tok::comma);
    else  // All others should munch while higher precedence.
      RHSPrec = ThisPrec+1;

    if (PeekPrec >= RHSPrec) {
      if (EvaluateDirectiveSubExpr(RHS, RHSPrec, PeekTok, RHSIsLive,
                                   IncludedUndefinedIds, PP))
        return true;
      PeekPrec = getPrecedence(PeekTok.getKind());
    }
    assert(PeekPrec <= ThisPrec && "Recursion didn't work!");

    // Usual arithmetic conversions (C99 6.3.1.8p1): result is unsigned if
    // either operand is unsigned.
    llvm::APSInt Res(LHS.getBitWidth());
    switch (Operator) {
    case tok::question:       // No UAC for x and y in "x ? y : z".
    case tok::lessless:       // Shift amount doesn't UAC with shift value.
    case tok::greatergreater: // Shift amount doesn't UAC with shift value.
    case tok::comma:          // Comma operands are not subject to UACs.
    case tok::pipepipe:       // Logical || does not do UACs.
    case tok::ampamp:         // Logical && does not do UACs.
      break;                  // No UAC
    default:
      Res.setIsUnsigned(LHS.isUnsigned()|RHS.isUnsigned());
      // If this just promoted something from signed to unsigned, and if the
      // value was negative, warn about it.
      if (ValueLive && Res.isUnsigned()) {
        if (!LHS.isUnsigned() && LHS.Val.isNegative())
          PP.Diag(OpLoc, diag::warn_pp_convert_to_positive) << 0
            << LHS.Val.toString(10, true) + " to " +
               LHS.Val.toString(10, false)
            << LHS.getRange() << RHS.getRange();
        if (!RHS.isUnsigned() && RHS.Val.isNegative())
          PP.Diag(OpLoc, diag::warn_pp_convert_to_positive) << 1
            << RHS.Val.toString(10, true) + " to " +
               RHS.Val.toString(10, false)
            << LHS.getRange() << RHS.getRange();
      }
      LHS.Val.setIsUnsigned(Res.isUnsigned());
      RHS.Val.setIsUnsigned(Res.isUnsigned());
    }

    bool Overflow = false;
    switch (Operator) {
    default: llvm_unreachable("Unknown operator token!");
    case tok::percent:
      if (RHS.Val != 0)
        Res = LHS.Val % RHS.Val;
      else if (ValueLive) {
        PP.Diag(OpLoc, diag::err_pp_remainder_by_zero)
          << LHS.getRange() << RHS.getRange();
        return true;
      }
      break;
    case tok::slash:
      if (RHS.Val != 0) {
        if (LHS.Val.isSigned())
          Res = llvm::APSInt(LHS.Val.sdiv_ov(RHS.Val, Overflow), false);
        else
          Res = LHS.Val / RHS.Val;
      } else if (ValueLive) {
        PP.Diag(OpLoc, diag::err_pp_division_by_zero)
          << LHS.getRange() << RHS.getRange();
        return true;
      }
      break;

    case tok::star:
      if (Res.isSigned())
        Res = llvm::APSInt(LHS.Val.smul_ov(RHS.Val, Overflow), false);
      else
        Res = LHS.Val * RHS.Val;
      break;
    case tok::lessless: {
      // Determine whether overflow is about to happen.
      if (LHS.isUnsigned())
        Res = LHS.Val.ushl_ov(RHS.Val, Overflow);
      else
        Res = llvm::APSInt(LHS.Val.sshl_ov(RHS.Val, Overflow), false);
      break;
    }
    case tok::greatergreater: {
      // Determine whether overflow is about to happen.
      unsigned ShAmt = static_cast<unsigned>(RHS.Val.getLimitedValue());
      if (ShAmt >= LHS.getBitWidth()) {
        Overflow = true;
        ShAmt = LHS.getBitWidth()-1;
      }
      Res = LHS.Val >> ShAmt;
      break;
    }
    case tok::plus:
      if (LHS.isUnsigned())
        Res = LHS.Val + RHS.Val;
      else
        Res = llvm::APSInt(LHS.Val.sadd_ov(RHS.Val, Overflow), false);
      break;
    case tok::minus:
      if (LHS.isUnsigned())
        Res = LHS.Val - RHS.Val;
      else
        Res = llvm::APSInt(LHS.Val.ssub_ov(RHS.Val, Overflow), false);
      break;
    case tok::lessequal:
      Res = LHS.Val <= RHS.Val;
      Res.setIsUnsigned(false);  // C99 6.5.8p6, result is always int (signed)
      break;
    case tok::less:
      Res = LHS.Val < RHS.Val;
      Res.setIsUnsigned(false);  // C99 6.5.8p6, result is always int (signed)
      break;
    case tok::greaterequal:
      Res = LHS.Val >= RHS.Val;
      Res.setIsUnsigned(false);  // C99 6.5.8p6, result is always int (signed)
      break;
    case tok::greater:
      Res = LHS.Val > RHS.Val;
      Res.setIsUnsigned(false);  // C99 6.5.8p6, result is always int (signed)
      break;
    case tok::exclaimequal:
      Res = LHS.Val != RHS.Val;
      Res.setIsUnsigned(false);  // C99 6.5.9p3, result is always int (signed)
      break;
    case tok::equalequal:
      Res = LHS.Val == RHS.Val;
      Res.setIsUnsigned(false);  // C99 6.5.9p3, result is always int (signed)
      break;
    case tok::amp:
      Res = LHS.Val & RHS.Val;
      break;
    case tok::caret:
      Res = LHS.Val ^ RHS.Val;
      break;
    case tok::pipe:
      Res = LHS.Val | RHS.Val;
      break;
    case tok::ampamp:
      Res = (LHS.Val != 0 && RHS.Val != 0);
      Res.setIsUnsigned(false);  // C99 6.5.13p3, result is always int (signed)
      break;
    case tok::pipepipe:
      Res = (LHS.Val != 0 || RHS.Val != 0);
      Res.setIsUnsigned(false);  // C99 6.5.14p3, result is always int (signed)
      break;
    case tok::comma:
      // Comma is invalid in pp expressions in c89/c++ mode, but is valid in C99
      // if not being evaluated.
      if (!PP.getLangOpts().C99 || ValueLive)
        PP.Diag(OpLoc, diag::ext_pp_comma_expr)
          << LHS.getRange() << RHS.getRange();
      Res = RHS.Val; // LHS = LHS,RHS -> RHS.
      break;
    case tok::question: {
      // Parse the : part of the expression.
      if (PeekTok.isNot(tok::colon)) {
        PP.Diag(PeekTok.getLocation(), diag::err_expected)
            << tok::colon << LHS.getRange() << RHS.getRange();
        PP.Diag(OpLoc, diag::note_matching) << tok::question;
        return true;
      }
      // Consume the :.
      PP.LexNonComment(PeekTok);

      // Evaluate the value after the :.
      bool AfterColonLive = ValueLive && LHS.Val == 0;
      PPValue AfterColonVal(LHS.getBitWidth());
      DefinedTracker DT;
      if (EvaluateValue(AfterColonVal, PeekTok, DT, AfterColonLive, PP))
        return true;

      // Parse anything after the : with the same precedence as ?.  We allow
      // things of equal precedence because ?: is right associative.
      if (EvaluateDirectiveSubExpr(AfterColonVal, ThisPrec,
                                   PeekTok, AfterColonLive,
                                   IncludedUndefinedIds, PP))
        return true;

      // Now that we have the condition, the LHS and the RHS of the :, evaluate.
      Res = LHS.Val != 0 ? RHS.Val : AfterColonVal.Val;
      RHS.setEnd(AfterColonVal.getRange().getEnd());

      // Usual arithmetic conversions (C99 6.3.1.8p1): result is unsigned if
      // either operand is unsigned.
      Res.setIsUnsigned(RHS.isUnsigned() | AfterColonVal.isUnsigned());

      // Figure out the precedence of the token after the : part.
      PeekPrec = getPrecedence(PeekTok.getKind());
      break;
    }
    case tok::colon:
      // Don't allow :'s to float around without being part of ?: exprs.
      PP.Diag(OpLoc, diag::err_pp_colon_without_question)
        << LHS.getRange() << RHS.getRange();
      return true;
    }

    // If this operator is live and overflowed, report the issue.
    if (Overflow && ValueLive)
      PP.Diag(OpLoc, diag::warn_pp_expr_overflow)
        << LHS.getRange() << RHS.getRange();

    // Put the result back into 'LHS' for our next iteration.
    LHS.Val = Res;
    LHS.setEnd(RHS.getRange().getEnd());
    RHS.setIdentifier(nullptr);
  }
}

/// EvaluateDirectiveExpression - Evaluate an integer constant expression that
/// may occur after a #if or #elif directive.  If the expression is equivalent
/// to "!defined(X)" return X in IfNDefMacro.
Preprocessor::DirectiveEvalResult
Preprocessor::EvaluateDirectiveExpression(IdentifierInfo *&IfNDefMacro) {
  SaveAndRestore<bool> PPDir(ParsingIfOrElifDirective, true);
  // Save the current state of 'DisableMacroExpansion' and reset it to false. If
  // 'DisableMacroExpansion' is true, then we must be in a macro argument list
  // in which case a directive is undefined behavior.  We want macros to be able
  // to recursively expand in order to get more gcc-list behavior, so we force
  // DisableMacroExpansion to false and restore it when we're done parsing the
  // expression.
  bool DisableMacroExpansionAtStartOfDirective = DisableMacroExpansion;
  DisableMacroExpansion = false;

  // Peek ahead one token.
  Token Tok;
  LexNonComment(Tok);

  // C99 6.10.1p3 - All expressions are evaluated as intmax_t or uintmax_t.
  unsigned BitWidth = getTargetInfo().getIntMaxTWidth();

  PPValue ResVal(BitWidth);
  DefinedTracker DT;
  if (EvaluateValue(ResVal, Tok, DT, true, *this)) {
    // Parse error, skip the rest of the macro line.
    if (Tok.isNot(tok::eod))
      DiscardUntilEndOfDirective();

    // Restore 'DisableMacroExpansion'.
    DisableMacroExpansion = DisableMacroExpansionAtStartOfDirective;
    return {false, DT.IncludedUndefinedIds};
  }

  // If we are at the end of the expression after just parsing a value, there
  // must be no (unparenthesized) binary operators involved, so we can exit
  // directly.
  if (Tok.is(tok::eod)) {
    // If the expression we parsed was of the form !defined(macro), return the
    // macro in IfNDefMacro.
    if (DT.State == DefinedTracker::NotDefinedMacro)
      IfNDefMacro = DT.TheMacro;

    // Restore 'DisableMacroExpansion'.
    DisableMacroExpansion = DisableMacroExpansionAtStartOfDirective;
    return {ResVal.Val != 0, DT.IncludedUndefinedIds};
  }

  // Otherwise, we must have a binary operator (e.g. "#if 1 < 2"), so parse the
  // operator and the stuff after it.
  if (EvaluateDirectiveSubExpr(ResVal, getPrecedence(tok::question),
                               Tok, true, DT.IncludedUndefinedIds, *this)) {
    // Parse error, skip the rest of the macro line.
    if (Tok.isNot(tok::eod))
      DiscardUntilEndOfDirective();

    // Restore 'DisableMacroExpansion'.
    DisableMacroExpansion = DisableMacroExpansionAtStartOfDirective;
    return {false, DT.IncludedUndefinedIds};
  }

  // If we aren't at the tok::eod token, something bad happened, like an extra
  // ')' token.
  if (Tok.isNot(tok::eod)) {
    Diag(Tok, diag::err_pp_expected_eol);
    DiscardUntilEndOfDirective();
  }

  // Restore 'DisableMacroExpansion'.
  DisableMacroExpansion = DisableMacroExpansionAtStartOfDirective;
  return {ResVal.Val != 0, DT.IncludedUndefinedIds};
}
