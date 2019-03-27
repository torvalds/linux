//===--- ParseExpr.cpp - Expression Parsing -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provides the Expression parsing implementation.
///
/// Expressions in C99 basically consist of a bunch of binary operators with
/// unary operators and other random stuff at the leaves.
///
/// In the C99 grammar, these unary operators bind tightest and are represented
/// as the 'cast-expression' production.  Everything else is either a binary
/// operator (e.g. '/') or a ternary operator ("?:").  The unary leaves are
/// handled by ParseCastExpression, the higher level pieces are handled by
/// ParseBinaryExpression.
///
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/TypoCorrection.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;

/// Simple precedence-based parser for binary/ternary operators.
///
/// Note: we diverge from the C99 grammar when parsing the assignment-expression
/// production.  C99 specifies that the LHS of an assignment operator should be
/// parsed as a unary-expression, but consistency dictates that it be a
/// conditional-expession.  In practice, the important thing here is that the
/// LHS of an assignment has to be an l-value, which productions between
/// unary-expression and conditional-expression don't produce.  Because we want
/// consistency, we parse the LHS as a conditional-expression, then check for
/// l-value-ness in semantic analysis stages.
///
/// \verbatim
///       pm-expression: [C++ 5.5]
///         cast-expression
///         pm-expression '.*' cast-expression
///         pm-expression '->*' cast-expression
///
///       multiplicative-expression: [C99 6.5.5]
///     Note: in C++, apply pm-expression instead of cast-expression
///         cast-expression
///         multiplicative-expression '*' cast-expression
///         multiplicative-expression '/' cast-expression
///         multiplicative-expression '%' cast-expression
///
///       additive-expression: [C99 6.5.6]
///         multiplicative-expression
///         additive-expression '+' multiplicative-expression
///         additive-expression '-' multiplicative-expression
///
///       shift-expression: [C99 6.5.7]
///         additive-expression
///         shift-expression '<<' additive-expression
///         shift-expression '>>' additive-expression
///
///       compare-expression: [C++20 expr.spaceship]
///         shift-expression
///         compare-expression '<=>' shift-expression
///
///       relational-expression: [C99 6.5.8]
///         compare-expression
///         relational-expression '<' compare-expression
///         relational-expression '>' compare-expression
///         relational-expression '<=' compare-expression
///         relational-expression '>=' compare-expression
///
///       equality-expression: [C99 6.5.9]
///         relational-expression
///         equality-expression '==' relational-expression
///         equality-expression '!=' relational-expression
///
///       AND-expression: [C99 6.5.10]
///         equality-expression
///         AND-expression '&' equality-expression
///
///       exclusive-OR-expression: [C99 6.5.11]
///         AND-expression
///         exclusive-OR-expression '^' AND-expression
///
///       inclusive-OR-expression: [C99 6.5.12]
///         exclusive-OR-expression
///         inclusive-OR-expression '|' exclusive-OR-expression
///
///       logical-AND-expression: [C99 6.5.13]
///         inclusive-OR-expression
///         logical-AND-expression '&&' inclusive-OR-expression
///
///       logical-OR-expression: [C99 6.5.14]
///         logical-AND-expression
///         logical-OR-expression '||' logical-AND-expression
///
///       conditional-expression: [C99 6.5.15]
///         logical-OR-expression
///         logical-OR-expression '?' expression ':' conditional-expression
/// [GNU]   logical-OR-expression '?' ':' conditional-expression
/// [C++] the third operand is an assignment-expression
///
///       assignment-expression: [C99 6.5.16]
///         conditional-expression
///         unary-expression assignment-operator assignment-expression
/// [C++]   throw-expression [C++ 15]
///
///       assignment-operator: one of
///         = *= /= %= += -= <<= >>= &= ^= |=
///
///       expression: [C99 6.5.17]
///         assignment-expression ...[opt]
///         expression ',' assignment-expression ...[opt]
/// \endverbatim
ExprResult Parser::ParseExpression(TypeCastState isTypeCast) {
  ExprResult LHS(ParseAssignmentExpression(isTypeCast));
  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

/// This routine is called when the '@' is seen and consumed.
/// Current token is an Identifier and is not a 'try'. This
/// routine is necessary to disambiguate \@try-statement from,
/// for example, \@encode-expression.
///
ExprResult
Parser::ParseExpressionWithLeadingAt(SourceLocation AtLoc) {
  ExprResult LHS(ParseObjCAtExpression(AtLoc));
  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

/// This routine is called when a leading '__extension__' is seen and
/// consumed.  This is necessary because the token gets consumed in the
/// process of disambiguating between an expression and a declaration.
ExprResult
Parser::ParseExpressionWithLeadingExtension(SourceLocation ExtLoc) {
  ExprResult LHS(true);
  {
    // Silence extension warnings in the sub-expression
    ExtensionRAIIObject O(Diags);

    LHS = ParseCastExpression(false);
  }

  if (!LHS.isInvalid())
    LHS = Actions.ActOnUnaryOp(getCurScope(), ExtLoc, tok::kw___extension__,
                               LHS.get());

  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

/// Parse an expr that doesn't include (top-level) commas.
ExprResult Parser::ParseAssignmentExpression(TypeCastState isTypeCast) {
  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteOrdinaryName(getCurScope(), Sema::PCC_Expression);
    cutOffParsing();
    return ExprError();
  }

  if (Tok.is(tok::kw_throw))
    return ParseThrowExpression();
  if (Tok.is(tok::kw_co_yield))
    return ParseCoyieldExpression();

  ExprResult LHS = ParseCastExpression(/*isUnaryExpression=*/false,
                                       /*isAddressOfOperand=*/false,
                                       isTypeCast);
  return ParseRHSOfBinaryExpression(LHS, prec::Assignment);
}

/// Parse an assignment expression where part of an Objective-C message
/// send has already been parsed.
///
/// In this case \p LBracLoc indicates the location of the '[' of the message
/// send, and either \p ReceiverName or \p ReceiverExpr is non-null indicating
/// the receiver of the message.
///
/// Since this handles full assignment-expression's, it handles postfix
/// expressions and other binary operators for these expressions as well.
ExprResult
Parser::ParseAssignmentExprWithObjCMessageExprStart(SourceLocation LBracLoc,
                                                    SourceLocation SuperLoc,
                                                    ParsedType ReceiverType,
                                                    Expr *ReceiverExpr) {
  ExprResult R
    = ParseObjCMessageExpressionBody(LBracLoc, SuperLoc,
                                     ReceiverType, ReceiverExpr);
  R = ParsePostfixExpressionSuffix(R);
  return ParseRHSOfBinaryExpression(R, prec::Assignment);
}

ExprResult
Parser::ParseConstantExpressionInExprEvalContext(TypeCastState isTypeCast) {
  assert(Actions.ExprEvalContexts.back().Context ==
             Sema::ExpressionEvaluationContext::ConstantEvaluated &&
         "Call this function only if your ExpressionEvaluationContext is "
         "already ConstantEvaluated");
  ExprResult LHS(ParseCastExpression(false, false, isTypeCast));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  return Actions.ActOnConstantExpression(Res);
}

ExprResult Parser::ParseConstantExpression(TypeCastState isTypeCast) {
  // C++03 [basic.def.odr]p2:
  //   An expression is potentially evaluated unless it appears where an
  //   integral constant expression is required (see 5.19) [...].
  // C++98 and C++11 have no such rule, but this is only a defect in C++98.
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  return ParseConstantExpressionInExprEvalContext(isTypeCast);
}

ExprResult Parser::ParseCaseExpression(SourceLocation CaseLoc) {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  ExprResult LHS(ParseCastExpression(false, false, NotTypeCast));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  return Actions.ActOnCaseExpr(CaseLoc, Res);
}

/// Parse a constraint-expression.
///
/// \verbatim
///       constraint-expression: [Concepts TS temp.constr.decl p1]
///         logical-or-expression
/// \endverbatim
ExprResult Parser::ParseConstraintExpression() {
  // FIXME: this may erroneously consume a function-body as the braced
  // initializer list of a compound literal
  //
  // FIXME: this may erroneously consume a parenthesized rvalue reference
  // declarator as a parenthesized address-of-label expression
  ExprResult LHS(ParseCastExpression(/*isUnaryExpression=*/false));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::LogicalOr));

  return Res;
}

bool Parser::isNotExpressionStart() {
  tok::TokenKind K = Tok.getKind();
  if (K == tok::l_brace || K == tok::r_brace  ||
      K == tok::kw_for  || K == tok::kw_while ||
      K == tok::kw_if   || K == tok::kw_else  ||
      K == tok::kw_goto || K == tok::kw_try)
    return true;
  // If this is a decl-specifier, we can't be at the start of an expression.
  return isKnownToBeDeclarationSpecifier();
}

bool Parser::isFoldOperator(prec::Level Level) const {
  return Level > prec::Unknown && Level != prec::Conditional &&
         Level != prec::Spaceship;
}

bool Parser::isFoldOperator(tok::TokenKind Kind) const {
  return isFoldOperator(getBinOpPrecedence(Kind, GreaterThanIsOperator, true));
}

/// Parse a binary expression that starts with \p LHS and has a
/// precedence of at least \p MinPrec.
ExprResult
Parser::ParseRHSOfBinaryExpression(ExprResult LHS, prec::Level MinPrec) {
  prec::Level NextTokPrec = getBinOpPrecedence(Tok.getKind(),
                                               GreaterThanIsOperator,
                                               getLangOpts().CPlusPlus11);
  SourceLocation ColonLoc;

  while (1) {
    // If this token has a lower precedence than we are allowed to parse (e.g.
    // because we are called recursively, or because the token is not a binop),
    // then we are done!
    if (NextTokPrec < MinPrec)
      return LHS;

    // Consume the operator, saving the operator token for error reporting.
    Token OpToken = Tok;
    ConsumeToken();

    if (OpToken.is(tok::caretcaret)) {
      return ExprError(Diag(Tok, diag::err_opencl_logical_exclusive_or));
    }

    // If we're potentially in a template-id, we may now be able to determine
    // whether we're actually in one or not.
    if (OpToken.isOneOf(tok::comma, tok::greater, tok::greatergreater,
                        tok::greatergreatergreater) &&
        checkPotentialAngleBracketDelimiter(OpToken))
      return ExprError();

    // Bail out when encountering a comma followed by a token which can't
    // possibly be the start of an expression. For instance:
    //   int f() { return 1, }
    // We can't do this before consuming the comma, because
    // isNotExpressionStart() looks at the token stream.
    if (OpToken.is(tok::comma) && isNotExpressionStart()) {
      PP.EnterToken(Tok);
      Tok = OpToken;
      return LHS;
    }

    // If the next token is an ellipsis, then this is a fold-expression. Leave
    // it alone so we can handle it in the paren expression.
    if (isFoldOperator(NextTokPrec) && Tok.is(tok::ellipsis)) {
      // FIXME: We can't check this via lookahead before we consume the token
      // because that tickles a lexer bug.
      PP.EnterToken(Tok);
      Tok = OpToken;
      return LHS;
    }

    // In Objective-C++, alternative operator tokens can be used as keyword args
    // in message expressions. Unconsume the token so that it can reinterpreted
    // as an identifier in ParseObjCMessageExpressionBody. i.e., we support:
    //   [foo meth:0 and:0];
    //   [foo not_eq];
    if (getLangOpts().ObjC && getLangOpts().CPlusPlus &&
        Tok.isOneOf(tok::colon, tok::r_square) &&
        OpToken.getIdentifierInfo() != nullptr) {
      PP.EnterToken(Tok);
      Tok = OpToken;
      return LHS;
    }

    // Special case handling for the ternary operator.
    ExprResult TernaryMiddle(true);
    if (NextTokPrec == prec::Conditional) {
      if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
        // Parse a braced-init-list here for error recovery purposes.
        SourceLocation BraceLoc = Tok.getLocation();
        TernaryMiddle = ParseBraceInitializer();
        if (!TernaryMiddle.isInvalid()) {
          Diag(BraceLoc, diag::err_init_list_bin_op)
              << /*RHS*/ 1 << PP.getSpelling(OpToken)
              << Actions.getExprRange(TernaryMiddle.get());
          TernaryMiddle = ExprError();
        }
      } else if (Tok.isNot(tok::colon)) {
        // Don't parse FOO:BAR as if it were a typo for FOO::BAR.
        ColonProtectionRAIIObject X(*this);

        // Handle this production specially:
        //   logical-OR-expression '?' expression ':' conditional-expression
        // In particular, the RHS of the '?' is 'expression', not
        // 'logical-OR-expression' as we might expect.
        TernaryMiddle = ParseExpression();
      } else {
        // Special case handling of "X ? Y : Z" where Y is empty:
        //   logical-OR-expression '?' ':' conditional-expression   [GNU]
        TernaryMiddle = nullptr;
        Diag(Tok, diag::ext_gnu_conditional_expr);
      }

      if (TernaryMiddle.isInvalid()) {
        Actions.CorrectDelayedTyposInExpr(LHS);
        LHS = ExprError();
        TernaryMiddle = nullptr;
      }

      if (!TryConsumeToken(tok::colon, ColonLoc)) {
        // Otherwise, we're missing a ':'.  Assume that this was a typo that
        // the user forgot. If we're not in a macro expansion, we can suggest
        // a fixit hint. If there were two spaces before the current token,
        // suggest inserting the colon in between them, otherwise insert ": ".
        SourceLocation FILoc = Tok.getLocation();
        const char *FIText = ": ";
        const SourceManager &SM = PP.getSourceManager();
        if (FILoc.isFileID() || PP.isAtStartOfMacroExpansion(FILoc, &FILoc)) {
          assert(FILoc.isFileID());
          bool IsInvalid = false;
          const char *SourcePtr =
            SM.getCharacterData(FILoc.getLocWithOffset(-1), &IsInvalid);
          if (!IsInvalid && *SourcePtr == ' ') {
            SourcePtr =
              SM.getCharacterData(FILoc.getLocWithOffset(-2), &IsInvalid);
            if (!IsInvalid && *SourcePtr == ' ') {
              FILoc = FILoc.getLocWithOffset(-1);
              FIText = ":";
            }
          }
        }

        Diag(Tok, diag::err_expected)
            << tok::colon << FixItHint::CreateInsertion(FILoc, FIText);
        Diag(OpToken, diag::note_matching) << tok::question;
        ColonLoc = Tok.getLocation();
      }
    }

    // Code completion for the right-hand side of a binary expression goes
    // through a special hook that takes the left-hand side into account.
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteBinaryRHS(getCurScope(), LHS.get(),
                                    OpToken.getKind());
      cutOffParsing();
      return ExprError();
    }

    // Parse another leaf here for the RHS of the operator.
    // ParseCastExpression works here because all RHS expressions in C have it
    // as a prefix, at least. However, in C++, an assignment-expression could
    // be a throw-expression, which is not a valid cast-expression.
    // Therefore we need some special-casing here.
    // Also note that the third operand of the conditional operator is
    // an assignment-expression in C++, and in C++11, we can have a
    // braced-init-list on the RHS of an assignment. For better diagnostics,
    // parse as if we were allowed braced-init-lists everywhere, and check that
    // they only appear on the RHS of assignments later.
    ExprResult RHS;
    bool RHSIsInitList = false;
    if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
      RHS = ParseBraceInitializer();
      RHSIsInitList = true;
    } else if (getLangOpts().CPlusPlus && NextTokPrec <= prec::Conditional)
      RHS = ParseAssignmentExpression();
    else
      RHS = ParseCastExpression(false);

    if (RHS.isInvalid()) {
      // FIXME: Errors generated by the delayed typo correction should be
      // printed before errors from parsing the RHS, not after.
      Actions.CorrectDelayedTyposInExpr(LHS);
      if (TernaryMiddle.isUsable())
        TernaryMiddle = Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
      LHS = ExprError();
    }

    // Remember the precedence of this operator and get the precedence of the
    // operator immediately to the right of the RHS.
    prec::Level ThisPrec = NextTokPrec;
    NextTokPrec = getBinOpPrecedence(Tok.getKind(), GreaterThanIsOperator,
                                     getLangOpts().CPlusPlus11);

    // Assignment and conditional expressions are right-associative.
    bool isRightAssoc = ThisPrec == prec::Conditional ||
                        ThisPrec == prec::Assignment;

    // Get the precedence of the operator to the right of the RHS.  If it binds
    // more tightly with RHS than we do, evaluate it completely first.
    if (ThisPrec < NextTokPrec ||
        (ThisPrec == NextTokPrec && isRightAssoc)) {
      if (!RHS.isInvalid() && RHSIsInitList) {
        Diag(Tok, diag::err_init_list_bin_op)
          << /*LHS*/0 << PP.getSpelling(Tok) << Actions.getExprRange(RHS.get());
        RHS = ExprError();
      }
      // If this is left-associative, only parse things on the RHS that bind
      // more tightly than the current operator.  If it is left-associative, it
      // is okay, to bind exactly as tightly.  For example, compile A=B=C=D as
      // A=(B=(C=D)), where each paren is a level of recursion here.
      // The function takes ownership of the RHS.
      RHS = ParseRHSOfBinaryExpression(RHS,
                            static_cast<prec::Level>(ThisPrec + !isRightAssoc));
      RHSIsInitList = false;

      if (RHS.isInvalid()) {
        // FIXME: Errors generated by the delayed typo correction should be
        // printed before errors from ParseRHSOfBinaryExpression, not after.
        Actions.CorrectDelayedTyposInExpr(LHS);
        if (TernaryMiddle.isUsable())
          TernaryMiddle = Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
        LHS = ExprError();
      }

      NextTokPrec = getBinOpPrecedence(Tok.getKind(), GreaterThanIsOperator,
                                       getLangOpts().CPlusPlus11);
    }

    if (!RHS.isInvalid() && RHSIsInitList) {
      if (ThisPrec == prec::Assignment) {
        Diag(OpToken, diag::warn_cxx98_compat_generalized_initializer_lists)
          << Actions.getExprRange(RHS.get());
      } else if (ColonLoc.isValid()) {
        Diag(ColonLoc, diag::err_init_list_bin_op)
          << /*RHS*/1 << ":"
          << Actions.getExprRange(RHS.get());
        LHS = ExprError();
      } else {
        Diag(OpToken, diag::err_init_list_bin_op)
          << /*RHS*/1 << PP.getSpelling(OpToken)
          << Actions.getExprRange(RHS.get());
        LHS = ExprError();
      }
    }

    ExprResult OrigLHS = LHS;
    if (!LHS.isInvalid()) {
      // Combine the LHS and RHS into the LHS (e.g. build AST).
      if (TernaryMiddle.isInvalid()) {
        // If we're using '>>' as an operator within a template
        // argument list (in C++98), suggest the addition of
        // parentheses so that the code remains well-formed in C++0x.
        if (!GreaterThanIsOperator && OpToken.is(tok::greatergreater))
          SuggestParentheses(OpToken.getLocation(),
                             diag::warn_cxx11_right_shift_in_template_arg,
                         SourceRange(Actions.getExprRange(LHS.get()).getBegin(),
                                     Actions.getExprRange(RHS.get()).getEnd()));

        LHS = Actions.ActOnBinOp(getCurScope(), OpToken.getLocation(),
                                 OpToken.getKind(), LHS.get(), RHS.get());

      } else {
        LHS = Actions.ActOnConditionalOp(OpToken.getLocation(), ColonLoc,
                                         LHS.get(), TernaryMiddle.get(),
                                         RHS.get());
      }
      // In this case, ActOnBinOp or ActOnConditionalOp performed the
      // CorrectDelayedTyposInExpr check.
      if (!getLangOpts().CPlusPlus)
        continue;
    }

    // Ensure potential typos aren't left undiagnosed.
    if (LHS.isInvalid()) {
      Actions.CorrectDelayedTyposInExpr(OrigLHS);
      Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
      Actions.CorrectDelayedTyposInExpr(RHS);
    }
  }
}

/// Parse a cast-expression, or, if \p isUnaryExpression is true,
/// parse a unary-expression.
///
/// \p isAddressOfOperand exists because an id-expression that is the
/// operand of address-of gets special treatment due to member pointers.
///
ExprResult Parser::ParseCastExpression(bool isUnaryExpression,
                                       bool isAddressOfOperand,
                                       TypeCastState isTypeCast,
                                       bool isVectorLiteral) {
  bool NotCastExpr;
  ExprResult Res = ParseCastExpression(isUnaryExpression,
                                       isAddressOfOperand,
                                       NotCastExpr,
                                       isTypeCast,
                                       isVectorLiteral);
  if (NotCastExpr)
    Diag(Tok, diag::err_expected_expression);
  return Res;
}

namespace {
class CastExpressionIdValidator : public CorrectionCandidateCallback {
 public:
  CastExpressionIdValidator(Token Next, bool AllowTypes, bool AllowNonTypes)
      : NextToken(Next), AllowNonTypes(AllowNonTypes) {
    WantTypeSpecifiers = WantFunctionLikeCasts = AllowTypes;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    NamedDecl *ND = candidate.getCorrectionDecl();
    if (!ND)
      return candidate.isKeyword();

    if (isa<TypeDecl>(ND))
      return WantTypeSpecifiers;

    if (!AllowNonTypes || !CorrectionCandidateCallback::ValidateCandidate(candidate))
      return false;

    if (!NextToken.isOneOf(tok::equal, tok::arrow, tok::period))
      return true;

    for (auto *C : candidate) {
      NamedDecl *ND = C->getUnderlyingDecl();
      if (isa<ValueDecl>(ND) && !isa<FunctionDecl>(ND))
        return true;
    }
    return false;
  }

 private:
  Token NextToken;
  bool AllowNonTypes;
};
}

/// Parse a cast-expression, or, if \pisUnaryExpression is true, parse
/// a unary-expression.
///
/// \p isAddressOfOperand exists because an id-expression that is the operand
/// of address-of gets special treatment due to member pointers. NotCastExpr
/// is set to true if the token is not the start of a cast-expression, and no
/// diagnostic is emitted in this case and no tokens are consumed.
///
/// \verbatim
///       cast-expression: [C99 6.5.4]
///         unary-expression
///         '(' type-name ')' cast-expression
///
///       unary-expression:  [C99 6.5.3]
///         postfix-expression
///         '++' unary-expression
///         '--' unary-expression
/// [Coro]  'co_await' cast-expression
///         unary-operator cast-expression
///         'sizeof' unary-expression
///         'sizeof' '(' type-name ')'
/// [C++11] 'sizeof' '...' '(' identifier ')'
/// [GNU]   '__alignof' unary-expression
/// [GNU]   '__alignof' '(' type-name ')'
/// [C11]   '_Alignof' '(' type-name ')'
/// [C++11] 'alignof' '(' type-id ')'
/// [GNU]   '&&' identifier
/// [C++11] 'noexcept' '(' expression ')' [C++11 5.3.7]
/// [C++]   new-expression
/// [C++]   delete-expression
///
///       unary-operator: one of
///         '&'  '*'  '+'  '-'  '~'  '!'
/// [GNU]   '__extension__'  '__real'  '__imag'
///
///       primary-expression: [C99 6.5.1]
/// [C99]   identifier
/// [C++]   id-expression
///         constant
///         string-literal
/// [C++]   boolean-literal  [C++ 2.13.5]
/// [C++11] 'nullptr'        [C++11 2.14.7]
/// [C++11] user-defined-literal
///         '(' expression ')'
/// [C11]   generic-selection
///         '__func__'        [C99 6.4.2.2]
/// [GNU]   '__FUNCTION__'
/// [MS]    '__FUNCDNAME__'
/// [MS]    'L__FUNCTION__'
/// [MS]    '__FUNCSIG__'
/// [MS]    'L__FUNCSIG__'
/// [GNU]   '__PRETTY_FUNCTION__'
/// [GNU]   '(' compound-statement ')'
/// [GNU]   '__builtin_va_arg' '(' assignment-expression ',' type-name ')'
/// [GNU]   '__builtin_offsetof' '(' type-name ',' offsetof-member-designator')'
/// [GNU]   '__builtin_choose_expr' '(' assign-expr ',' assign-expr ','
///                                     assign-expr ')'
/// [GNU]   '__builtin_types_compatible_p' '(' type-name ',' type-name ')'
/// [GNU]   '__null'
/// [OBJC]  '[' objc-message-expr ']'
/// [OBJC]  '\@selector' '(' objc-selector-arg ')'
/// [OBJC]  '\@protocol' '(' identifier ')'
/// [OBJC]  '\@encode' '(' type-name ')'
/// [OBJC]  objc-string-literal
/// [C++]   simple-type-specifier '(' expression-list[opt] ')'      [C++ 5.2.3]
/// [C++11] simple-type-specifier braced-init-list                  [C++11 5.2.3]
/// [C++]   typename-specifier '(' expression-list[opt] ')'         [C++ 5.2.3]
/// [C++11] typename-specifier braced-init-list                     [C++11 5.2.3]
/// [C++]   'const_cast' '<' type-name '>' '(' expression ')'       [C++ 5.2p1]
/// [C++]   'dynamic_cast' '<' type-name '>' '(' expression ')'     [C++ 5.2p1]
/// [C++]   'reinterpret_cast' '<' type-name '>' '(' expression ')' [C++ 5.2p1]
/// [C++]   'static_cast' '<' type-name '>' '(' expression ')'      [C++ 5.2p1]
/// [C++]   'typeid' '(' expression ')'                             [C++ 5.2p1]
/// [C++]   'typeid' '(' type-id ')'                                [C++ 5.2p1]
/// [C++]   'this'          [C++ 9.3.2]
/// [G++]   unary-type-trait '(' type-id ')'
/// [G++]   binary-type-trait '(' type-id ',' type-id ')'           [TODO]
/// [EMBT]  array-type-trait '(' type-id ',' integer ')'
/// [clang] '^' block-literal
///
///       constant: [C99 6.4.4]
///         integer-constant
///         floating-constant
///         enumeration-constant -> identifier
///         character-constant
///
///       id-expression: [C++ 5.1]
///                   unqualified-id
///                   qualified-id
///
///       unqualified-id: [C++ 5.1]
///                   identifier
///                   operator-function-id
///                   conversion-function-id
///                   '~' class-name
///                   template-id
///
///       new-expression: [C++ 5.3.4]
///                   '::'[opt] 'new' new-placement[opt] new-type-id
///                                     new-initializer[opt]
///                   '::'[opt] 'new' new-placement[opt] '(' type-id ')'
///                                     new-initializer[opt]
///
///       delete-expression: [C++ 5.3.5]
///                   '::'[opt] 'delete' cast-expression
///                   '::'[opt] 'delete' '[' ']' cast-expression
///
/// [GNU/Embarcadero] unary-type-trait:
///                   '__is_arithmetic'
///                   '__is_floating_point'
///                   '__is_integral'
///                   '__is_lvalue_expr'
///                   '__is_rvalue_expr'
///                   '__is_complete_type'
///                   '__is_void'
///                   '__is_array'
///                   '__is_function'
///                   '__is_reference'
///                   '__is_lvalue_reference'
///                   '__is_rvalue_reference'
///                   '__is_fundamental'
///                   '__is_object'
///                   '__is_scalar'
///                   '__is_compound'
///                   '__is_pointer'
///                   '__is_member_object_pointer'
///                   '__is_member_function_pointer'
///                   '__is_member_pointer'
///                   '__is_const'
///                   '__is_volatile'
///                   '__is_trivial'
///                   '__is_standard_layout'
///                   '__is_signed'
///                   '__is_unsigned'
///
/// [GNU] unary-type-trait:
///                   '__has_nothrow_assign'
///                   '__has_nothrow_copy'
///                   '__has_nothrow_constructor'
///                   '__has_trivial_assign'                  [TODO]
///                   '__has_trivial_copy'                    [TODO]
///                   '__has_trivial_constructor'
///                   '__has_trivial_destructor'
///                   '__has_virtual_destructor'
///                   '__is_abstract'                         [TODO]
///                   '__is_class'
///                   '__is_empty'                            [TODO]
///                   '__is_enum'
///                   '__is_final'
///                   '__is_pod'
///                   '__is_polymorphic'
///                   '__is_sealed'                           [MS]
///                   '__is_trivial'
///                   '__is_union'
///                   '__has_unique_object_representations'
///
/// [Clang] unary-type-trait:
///                   '__is_aggregate'
///                   '__trivially_copyable'
///
///       binary-type-trait:
/// [GNU]             '__is_base_of'
/// [MS]              '__is_convertible_to'
///                   '__is_convertible'
///                   '__is_same'
///
/// [Embarcadero] array-type-trait:
///                   '__array_rank'
///                   '__array_extent'
///
/// [Embarcadero] expression-trait:
///                   '__is_lvalue_expr'
///                   '__is_rvalue_expr'
/// \endverbatim
///
ExprResult Parser::ParseCastExpression(bool isUnaryExpression,
                                       bool isAddressOfOperand,
                                       bool &NotCastExpr,
                                       TypeCastState isTypeCast,
                                       bool isVectorLiteral) {
  ExprResult Res;
  tok::TokenKind SavedKind = Tok.getKind();
  NotCastExpr = false;

  // This handles all of cast-expression, unary-expression, postfix-expression,
  // and primary-expression.  We handle them together like this for efficiency
  // and to simplify handling of an expression starting with a '(' token: which
  // may be one of a parenthesized expression, cast-expression, compound literal
  // expression, or statement expression.
  //
  // If the parsed tokens consist of a primary-expression, the cases below
  // break out of the switch;  at the end we call ParsePostfixExpressionSuffix
  // to handle the postfix expression suffixes.  Cases that cannot be followed
  // by postfix exprs should return without invoking
  // ParsePostfixExpressionSuffix.
  switch (SavedKind) {
  case tok::l_paren: {
    // If this expression is limited to being a unary-expression, the parent can
    // not start a cast expression.
    ParenParseOption ParenExprType =
        (isUnaryExpression && !getLangOpts().CPlusPlus) ? CompoundLiteral
                                                        : CastExpr;
    ParsedType CastTy;
    SourceLocation RParenLoc;
    Res = ParseParenExpression(ParenExprType, false/*stopIfCastExr*/,
                               isTypeCast == IsTypeCast, CastTy, RParenLoc);

    if (isVectorLiteral)
        return Res;

    switch (ParenExprType) {
    case SimpleExpr:   break;    // Nothing else to do.
    case CompoundStmt: break;  // Nothing else to do.
    case CompoundLiteral:
      // We parsed '(' type-name ')' '{' ... '}'.  If any suffixes of
      // postfix-expression exist, parse them now.
      break;
    case CastExpr:
      // We have parsed the cast-expression and no postfix-expr pieces are
      // following.
      return Res;
    case FoldExpr:
      // We only parsed a fold-expression. There might be postfix-expr pieces
      // afterwards; parse them now.
      break;
    }

    break;
  }

    // primary-expression
  case tok::numeric_constant:
    // constant: integer-constant
    // constant: floating-constant

    Res = Actions.ActOnNumericConstant(Tok, /*UDLScope*/getCurScope());
    ConsumeToken();
    break;

  case tok::kw_true:
  case tok::kw_false:
    Res = ParseCXXBoolLiteral();
    break;

  case tok::kw___objc_yes:
  case tok::kw___objc_no:
      return ParseObjCBoolLiteral();

  case tok::kw_nullptr:
    Diag(Tok, diag::warn_cxx98_compat_nullptr);
    return Actions.ActOnCXXNullPtrLiteral(ConsumeToken());

  case tok::annot_primary_expr:
    assert(Res.get() == nullptr && "Stray primary-expression annotation?");
    Res = getExprAnnotation(Tok);
    ConsumeAnnotationToken();
    if (!Res.isInvalid() && Tok.is(tok::less))
      checkPotentialAngleBracket(Res);
    break;

  case tok::kw___super:
  case tok::kw_decltype:
    // Annotate the token and tail recurse.
    if (TryAnnotateTypeOrScopeToken())
      return ExprError();
    assert(Tok.isNot(tok::kw_decltype) && Tok.isNot(tok::kw___super));
    return ParseCastExpression(isUnaryExpression, isAddressOfOperand);

  case tok::identifier: {      // primary-expression: identifier
                               // unqualified-id: identifier
                               // constant: enumeration-constant
    // Turn a potentially qualified name into a annot_typename or
    // annot_cxxscope if it would be valid.  This handles things like x::y, etc.
    if (getLangOpts().CPlusPlus) {
      // Avoid the unnecessary parse-time lookup in the common case
      // where the syntax forbids a type.
      const Token &Next = NextToken();

      // If this identifier was reverted from a token ID, and the next token
      // is a parenthesis, this is likely to be a use of a type trait. Check
      // those tokens.
      if (Next.is(tok::l_paren) &&
          Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo()->hasRevertedTokenIDToIdentifier()) {
        IdentifierInfo *II = Tok.getIdentifierInfo();
        // Build up the mapping of revertible type traits, for future use.
        if (RevertibleTypeTraits.empty()) {
#define RTT_JOIN(X,Y) X##Y
#define REVERTIBLE_TYPE_TRAIT(Name)                         \
          RevertibleTypeTraits[PP.getIdentifierInfo(#Name)] \
            = RTT_JOIN(tok::kw_,Name)

          REVERTIBLE_TYPE_TRAIT(__is_abstract);
          REVERTIBLE_TYPE_TRAIT(__is_aggregate);
          REVERTIBLE_TYPE_TRAIT(__is_arithmetic);
          REVERTIBLE_TYPE_TRAIT(__is_array);
          REVERTIBLE_TYPE_TRAIT(__is_assignable);
          REVERTIBLE_TYPE_TRAIT(__is_base_of);
          REVERTIBLE_TYPE_TRAIT(__is_class);
          REVERTIBLE_TYPE_TRAIT(__is_complete_type);
          REVERTIBLE_TYPE_TRAIT(__is_compound);
          REVERTIBLE_TYPE_TRAIT(__is_const);
          REVERTIBLE_TYPE_TRAIT(__is_constructible);
          REVERTIBLE_TYPE_TRAIT(__is_convertible);
          REVERTIBLE_TYPE_TRAIT(__is_convertible_to);
          REVERTIBLE_TYPE_TRAIT(__is_destructible);
          REVERTIBLE_TYPE_TRAIT(__is_empty);
          REVERTIBLE_TYPE_TRAIT(__is_enum);
          REVERTIBLE_TYPE_TRAIT(__is_floating_point);
          REVERTIBLE_TYPE_TRAIT(__is_final);
          REVERTIBLE_TYPE_TRAIT(__is_function);
          REVERTIBLE_TYPE_TRAIT(__is_fundamental);
          REVERTIBLE_TYPE_TRAIT(__is_integral);
          REVERTIBLE_TYPE_TRAIT(__is_interface_class);
          REVERTIBLE_TYPE_TRAIT(__is_literal);
          REVERTIBLE_TYPE_TRAIT(__is_lvalue_expr);
          REVERTIBLE_TYPE_TRAIT(__is_lvalue_reference);
          REVERTIBLE_TYPE_TRAIT(__is_member_function_pointer);
          REVERTIBLE_TYPE_TRAIT(__is_member_object_pointer);
          REVERTIBLE_TYPE_TRAIT(__is_member_pointer);
          REVERTIBLE_TYPE_TRAIT(__is_nothrow_assignable);
          REVERTIBLE_TYPE_TRAIT(__is_nothrow_constructible);
          REVERTIBLE_TYPE_TRAIT(__is_nothrow_destructible);
          REVERTIBLE_TYPE_TRAIT(__is_object);
          REVERTIBLE_TYPE_TRAIT(__is_pod);
          REVERTIBLE_TYPE_TRAIT(__is_pointer);
          REVERTIBLE_TYPE_TRAIT(__is_polymorphic);
          REVERTIBLE_TYPE_TRAIT(__is_reference);
          REVERTIBLE_TYPE_TRAIT(__is_rvalue_expr);
          REVERTIBLE_TYPE_TRAIT(__is_rvalue_reference);
          REVERTIBLE_TYPE_TRAIT(__is_same);
          REVERTIBLE_TYPE_TRAIT(__is_scalar);
          REVERTIBLE_TYPE_TRAIT(__is_sealed);
          REVERTIBLE_TYPE_TRAIT(__is_signed);
          REVERTIBLE_TYPE_TRAIT(__is_standard_layout);
          REVERTIBLE_TYPE_TRAIT(__is_trivial);
          REVERTIBLE_TYPE_TRAIT(__is_trivially_assignable);
          REVERTIBLE_TYPE_TRAIT(__is_trivially_constructible);
          REVERTIBLE_TYPE_TRAIT(__is_trivially_copyable);
          REVERTIBLE_TYPE_TRAIT(__is_union);
          REVERTIBLE_TYPE_TRAIT(__is_unsigned);
          REVERTIBLE_TYPE_TRAIT(__is_void);
          REVERTIBLE_TYPE_TRAIT(__is_volatile);
#undef REVERTIBLE_TYPE_TRAIT
#undef RTT_JOIN
        }

        // If we find that this is in fact the name of a type trait,
        // update the token kind in place and parse again to treat it as
        // the appropriate kind of type trait.
        llvm::SmallDenseMap<IdentifierInfo *, tok::TokenKind>::iterator Known
          = RevertibleTypeTraits.find(II);
        if (Known != RevertibleTypeTraits.end()) {
          Tok.setKind(Known->second);
          return ParseCastExpression(isUnaryExpression, isAddressOfOperand,
                                     NotCastExpr, isTypeCast);
        }
      }

      if ((!ColonIsSacred && Next.is(tok::colon)) ||
          Next.isOneOf(tok::coloncolon, tok::less, tok::l_paren,
                       tok::l_brace)) {
        // If TryAnnotateTypeOrScopeToken annotates the token, tail recurse.
        if (TryAnnotateTypeOrScopeToken())
          return ExprError();
        if (!Tok.is(tok::identifier))
          return ParseCastExpression(isUnaryExpression, isAddressOfOperand);
      }
    }

    // Consume the identifier so that we can see if it is followed by a '(' or
    // '.'.
    IdentifierInfo &II = *Tok.getIdentifierInfo();
    SourceLocation ILoc = ConsumeToken();

    // Support 'Class.property' and 'super.property' notation.
    if (getLangOpts().ObjC && Tok.is(tok::period) &&
        (Actions.getTypeName(II, ILoc, getCurScope()) ||
         // Allow the base to be 'super' if in an objc-method.
         (&II == Ident_super && getCurScope()->isInObjcMethodScope()))) {
      ConsumeToken();

      if (Tok.is(tok::code_completion) && &II != Ident_super) {
        Actions.CodeCompleteObjCClassPropertyRefExpr(
            getCurScope(), II, ILoc, ExprStatementTokLoc == ILoc);
        cutOffParsing();
        return ExprError();
      }
      // Allow either an identifier or the keyword 'class' (in C++).
      if (Tok.isNot(tok::identifier) &&
          !(getLangOpts().CPlusPlus && Tok.is(tok::kw_class))) {
        Diag(Tok, diag::err_expected_property_name);
        return ExprError();
      }
      IdentifierInfo &PropertyName = *Tok.getIdentifierInfo();
      SourceLocation PropertyLoc = ConsumeToken();

      Res = Actions.ActOnClassPropertyRefExpr(II, PropertyName,
                                              ILoc, PropertyLoc);
      break;
    }

    // In an Objective-C method, if we have "super" followed by an identifier,
    // the token sequence is ill-formed. However, if there's a ':' or ']' after
    // that identifier, this is probably a message send with a missing open
    // bracket. Treat it as such.
    if (getLangOpts().ObjC && &II == Ident_super && !InMessageExpression &&
        getCurScope()->isInObjcMethodScope() &&
        ((Tok.is(tok::identifier) &&
         (NextToken().is(tok::colon) || NextToken().is(tok::r_square))) ||
         Tok.is(tok::code_completion))) {
      Res = ParseObjCMessageExpressionBody(SourceLocation(), ILoc, nullptr,
                                           nullptr);
      break;
    }

    // If we have an Objective-C class name followed by an identifier
    // and either ':' or ']', this is an Objective-C class message
    // send that's missing the opening '['. Recovery
    // appropriately. Also take this path if we're performing code
    // completion after an Objective-C class name.
    if (getLangOpts().ObjC &&
        ((Tok.is(tok::identifier) && !InMessageExpression) ||
         Tok.is(tok::code_completion))) {
      const Token& Next = NextToken();
      if (Tok.is(tok::code_completion) ||
          Next.is(tok::colon) || Next.is(tok::r_square))
        if (ParsedType Typ = Actions.getTypeName(II, ILoc, getCurScope()))
          if (Typ.get()->isObjCObjectOrInterfaceType()) {
            // Fake up a Declarator to use with ActOnTypeName.
            DeclSpec DS(AttrFactory);
            DS.SetRangeStart(ILoc);
            DS.SetRangeEnd(ILoc);
            const char *PrevSpec = nullptr;
            unsigned DiagID;
            DS.SetTypeSpecType(TST_typename, ILoc, PrevSpec, DiagID, Typ,
                               Actions.getASTContext().getPrintingPolicy());

            Declarator DeclaratorInfo(DS, DeclaratorContext::TypeNameContext);
            TypeResult Ty = Actions.ActOnTypeName(getCurScope(),
                                                  DeclaratorInfo);
            if (Ty.isInvalid())
              break;

            Res = ParseObjCMessageExpressionBody(SourceLocation(),
                                                 SourceLocation(),
                                                 Ty.get(), nullptr);
            break;
          }
    }

    // Make sure to pass down the right value for isAddressOfOperand.
    if (isAddressOfOperand && isPostfixExpressionSuffixStart())
      isAddressOfOperand = false;

    // Function designators are allowed to be undeclared (C99 6.5.1p2), so we
    // need to know whether or not this identifier is a function designator or
    // not.
    UnqualifiedId Name;
    CXXScopeSpec ScopeSpec;
    SourceLocation TemplateKWLoc;
    Token Replacement;
    auto Validator = llvm::make_unique<CastExpressionIdValidator>(
        Tok, isTypeCast != NotTypeCast, isTypeCast != IsTypeCast);
    Validator->IsAddressOfOperand = isAddressOfOperand;
    if (Tok.isOneOf(tok::periodstar, tok::arrowstar)) {
      Validator->WantExpressionKeywords = false;
      Validator->WantRemainingKeywords = false;
    } else {
      Validator->WantRemainingKeywords = Tok.isNot(tok::r_paren);
    }
    Name.setIdentifier(&II, ILoc);
    Res = Actions.ActOnIdExpression(
        getCurScope(), ScopeSpec, TemplateKWLoc, Name, Tok.is(tok::l_paren),
        isAddressOfOperand, std::move(Validator),
        /*IsInlineAsmIdentifier=*/false,
        Tok.is(tok::r_paren) ? nullptr : &Replacement);
    if (!Res.isInvalid() && Res.isUnset()) {
      UnconsumeToken(Replacement);
      return ParseCastExpression(isUnaryExpression, isAddressOfOperand,
                                 NotCastExpr, isTypeCast);
    }
    if (!Res.isInvalid() && Tok.is(tok::less))
      checkPotentialAngleBracket(Res);
    break;
  }
  case tok::char_constant:     // constant: character-constant
  case tok::wide_char_constant:
  case tok::utf8_char_constant:
  case tok::utf16_char_constant:
  case tok::utf32_char_constant:
    Res = Actions.ActOnCharacterConstant(Tok, /*UDLScope*/getCurScope());
    ConsumeToken();
    break;
  case tok::kw___func__:       // primary-expression: __func__ [C99 6.4.2.2]
  case tok::kw___FUNCTION__:   // primary-expression: __FUNCTION__ [GNU]
  case tok::kw___FUNCDNAME__:   // primary-expression: __FUNCDNAME__ [MS]
  case tok::kw___FUNCSIG__:     // primary-expression: __FUNCSIG__ [MS]
  case tok::kw_L__FUNCTION__:   // primary-expression: L__FUNCTION__ [MS]
  case tok::kw_L__FUNCSIG__:    // primary-expression: L__FUNCSIG__ [MS]
  case tok::kw___PRETTY_FUNCTION__:  // primary-expression: __P..Y_F..N__ [GNU]
    Res = Actions.ActOnPredefinedExpr(Tok.getLocation(), SavedKind);
    ConsumeToken();
    break;
  case tok::string_literal:    // primary-expression: string-literal
  case tok::wide_string_literal:
  case tok::utf8_string_literal:
  case tok::utf16_string_literal:
  case tok::utf32_string_literal:
    Res = ParseStringLiteralExpression(true);
    break;
  case tok::kw__Generic:   // primary-expression: generic-selection [C11 6.5.1]
    Res = ParseGenericSelectionExpression();
    break;
  case tok::kw___builtin_available:
    return ParseAvailabilityCheckExpr(Tok.getLocation());
  case tok::kw___builtin_va_arg:
  case tok::kw___builtin_offsetof:
  case tok::kw___builtin_choose_expr:
  case tok::kw___builtin_astype: // primary-expression: [OCL] as_type()
  case tok::kw___builtin_convertvector:
    return ParseBuiltinPrimaryExpression();
  case tok::kw___null:
    return Actions.ActOnGNUNullExpr(ConsumeToken());

  case tok::plusplus:      // unary-expression: '++' unary-expression [C99]
  case tok::minusminus: {  // unary-expression: '--' unary-expression [C99]
    // C++ [expr.unary] has:
    //   unary-expression:
    //     ++ cast-expression
    //     -- cast-expression
    Token SavedTok = Tok;
    ConsumeToken();
    // One special case is implicitly handled here: if the preceding tokens are
    // an ambiguous cast expression, such as "(T())++", then we recurse to
    // determine whether the '++' is prefix or postfix.
    Res = ParseCastExpression(!getLangOpts().CPlusPlus,
                              /*isAddressOfOperand*/false, NotCastExpr,
                              NotTypeCast);
    if (NotCastExpr) {
      // If we return with NotCastExpr = true, we must not consume any tokens,
      // so put the token back where we found it.
      assert(Res.isInvalid());
      UnconsumeToken(SavedTok);
      return ExprError();
    }
    if (!Res.isInvalid())
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedTok.getLocation(),
                                 SavedKind, Res.get());
    return Res;
  }
  case tok::amp: {         // unary-expression: '&' cast-expression
    // Special treatment because of member pointers
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(false, true);
    if (!Res.isInvalid())
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedLoc, SavedKind, Res.get());
    return Res;
  }

  case tok::star:          // unary-expression: '*' cast-expression
  case tok::plus:          // unary-expression: '+' cast-expression
  case tok::minus:         // unary-expression: '-' cast-expression
  case tok::tilde:         // unary-expression: '~' cast-expression
  case tok::exclaim:       // unary-expression: '!' cast-expression
  case tok::kw___real:     // unary-expression: '__real' cast-expression [GNU]
  case tok::kw___imag: {   // unary-expression: '__imag' cast-expression [GNU]
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(false);
    if (!Res.isInvalid())
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedLoc, SavedKind, Res.get());
    return Res;
  }

  case tok::kw_co_await: {  // unary-expression: 'co_await' cast-expression
    SourceLocation CoawaitLoc = ConsumeToken();
    Res = ParseCastExpression(false);
    if (!Res.isInvalid())
      Res = Actions.ActOnCoawaitExpr(getCurScope(), CoawaitLoc, Res.get());
    return Res;
  }

  case tok::kw___extension__:{//unary-expression:'__extension__' cast-expr [GNU]
    // __extension__ silences extension warnings in the subexpression.
    ExtensionRAIIObject O(Diags);  // Use RAII to do this.
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(false);
    if (!Res.isInvalid())
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedLoc, SavedKind, Res.get());
    return Res;
  }
  case tok::kw__Alignof:   // unary-expression: '_Alignof' '(' type-name ')'
    if (!getLangOpts().C11)
      Diag(Tok, diag::ext_c11_alignment) << Tok.getName();
    LLVM_FALLTHROUGH;
  case tok::kw_alignof:    // unary-expression: 'alignof' '(' type-id ')'
  case tok::kw___alignof:  // unary-expression: '__alignof' unary-expression
                           // unary-expression: '__alignof' '(' type-name ')'
  case tok::kw_sizeof:     // unary-expression: 'sizeof' unary-expression
                           // unary-expression: 'sizeof' '(' type-name ')'
  case tok::kw_vec_step:   // unary-expression: OpenCL 'vec_step' expression
  // unary-expression: '__builtin_omp_required_simd_align' '(' type-name ')'
  case tok::kw___builtin_omp_required_simd_align:
    return ParseUnaryExprOrTypeTraitExpression();
  case tok::ampamp: {      // unary-expression: '&&' identifier
    SourceLocation AmpAmpLoc = ConsumeToken();
    if (Tok.isNot(tok::identifier))
      return ExprError(Diag(Tok, diag::err_expected) << tok::identifier);

    if (getCurScope()->getFnParent() == nullptr)
      return ExprError(Diag(Tok, diag::err_address_of_label_outside_fn));

    Diag(AmpAmpLoc, diag::ext_gnu_address_of_label);
    LabelDecl *LD = Actions.LookupOrCreateLabel(Tok.getIdentifierInfo(),
                                                Tok.getLocation());
    Res = Actions.ActOnAddrLabel(AmpAmpLoc, Tok.getLocation(), LD);
    ConsumeToken();
    return Res;
  }
  case tok::kw_const_cast:
  case tok::kw_dynamic_cast:
  case tok::kw_reinterpret_cast:
  case tok::kw_static_cast:
    Res = ParseCXXCasts();
    break;
  case tok::kw_typeid:
    Res = ParseCXXTypeid();
    break;
  case tok::kw___uuidof:
    Res = ParseCXXUuidof();
    break;
  case tok::kw_this:
    Res = ParseCXXThis();
    break;

  case tok::annot_typename:
    if (isStartOfObjCClassMessageMissingOpenBracket()) {
      ParsedType Type = getTypeAnnotation(Tok);

      // Fake up a Declarator to use with ActOnTypeName.
      DeclSpec DS(AttrFactory);
      DS.SetRangeStart(Tok.getLocation());
      DS.SetRangeEnd(Tok.getLastLoc());

      const char *PrevSpec = nullptr;
      unsigned DiagID;
      DS.SetTypeSpecType(TST_typename, Tok.getAnnotationEndLoc(),
                         PrevSpec, DiagID, Type,
                         Actions.getASTContext().getPrintingPolicy());

      Declarator DeclaratorInfo(DS, DeclaratorContext::TypeNameContext);
      TypeResult Ty = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo);
      if (Ty.isInvalid())
        break;

      ConsumeAnnotationToken();
      Res = ParseObjCMessageExpressionBody(SourceLocation(), SourceLocation(),
                                           Ty.get(), nullptr);
      break;
    }
    LLVM_FALLTHROUGH;

  case tok::annot_decltype:
  case tok::kw_char:
  case tok::kw_wchar_t:
  case tok::kw_char8_t:
  case tok::kw_char16_t:
  case tok::kw_char32_t:
  case tok::kw_bool:
  case tok::kw_short:
  case tok::kw_int:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw_half:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Float16:
  case tok::kw___float128:
  case tok::kw_void:
  case tok::kw_typename:
  case tok::kw_typeof:
  case tok::kw___vector:
#define GENERIC_IMAGE_TYPE(ImgType, Id) case tok::kw_##ImgType##_t:
#include "clang/Basic/OpenCLImageTypes.def"
  {
    if (!getLangOpts().CPlusPlus) {
      Diag(Tok, diag::err_expected_expression);
      return ExprError();
    }

    if (SavedKind == tok::kw_typename) {
      // postfix-expression: typename-specifier '(' expression-list[opt] ')'
      //                     typename-specifier braced-init-list
      if (TryAnnotateTypeOrScopeToken())
        return ExprError();

      if (!Actions.isSimpleTypeSpecifier(Tok.getKind()))
        // We are trying to parse a simple-type-specifier but might not get such
        // a token after error recovery.
        return ExprError();
    }

    // postfix-expression: simple-type-specifier '(' expression-list[opt] ')'
    //                     simple-type-specifier braced-init-list
    //
    DeclSpec DS(AttrFactory);

    ParseCXXSimpleTypeSpecifier(DS);
    if (Tok.isNot(tok::l_paren) &&
        (!getLangOpts().CPlusPlus11 || Tok.isNot(tok::l_brace)))
      return ExprError(Diag(Tok, diag::err_expected_lparen_after_type)
                         << DS.getSourceRange());

    if (Tok.is(tok::l_brace))
      Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);

    Res = ParseCXXTypeConstructExpression(DS);
    break;
  }

  case tok::annot_cxxscope: { // [C++] id-expression: qualified-id
    // If TryAnnotateTypeOrScopeToken annotates the token, tail recurse.
    // (We can end up in this situation after tentative parsing.)
    if (TryAnnotateTypeOrScopeToken())
      return ExprError();
    if (!Tok.is(tok::annot_cxxscope))
      return ParseCastExpression(isUnaryExpression, isAddressOfOperand,
                                 NotCastExpr, isTypeCast);

    Token Next = NextToken();
    if (Next.is(tok::annot_template_id)) {
      TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Next);
      if (TemplateId->Kind == TNK_Type_template) {
        // We have a qualified template-id that we know refers to a
        // type, translate it into a type and continue parsing as a
        // cast expression.
        CXXScopeSpec SS;
        ParseOptionalCXXScopeSpecifier(SS, nullptr,
                                       /*EnteringContext=*/false);
        AnnotateTemplateIdTokenAsType();
        return ParseCastExpression(isUnaryExpression, isAddressOfOperand,
                                   NotCastExpr, isTypeCast);
      }
    }

    // Parse as an id-expression.
    Res = ParseCXXIdExpression(isAddressOfOperand);
    break;
  }

  case tok::annot_template_id: { // [C++]          template-id
    TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
    if (TemplateId->Kind == TNK_Type_template) {
      // We have a template-id that we know refers to a type,
      // translate it into a type and continue parsing as a cast
      // expression.
      AnnotateTemplateIdTokenAsType();
      return ParseCastExpression(isUnaryExpression, isAddressOfOperand,
                                 NotCastExpr, isTypeCast);
    }

    // Fall through to treat the template-id as an id-expression.
    LLVM_FALLTHROUGH;
  }

  case tok::kw_operator: // [C++] id-expression: operator/conversion-function-id
    Res = ParseCXXIdExpression(isAddressOfOperand);
    break;

  case tok::coloncolon: {
    // ::foo::bar -> global qualified name etc.   If TryAnnotateTypeOrScopeToken
    // annotates the token, tail recurse.
    if (TryAnnotateTypeOrScopeToken())
      return ExprError();
    if (!Tok.is(tok::coloncolon))
      return ParseCastExpression(isUnaryExpression, isAddressOfOperand);

    // ::new -> [C++] new-expression
    // ::delete -> [C++] delete-expression
    SourceLocation CCLoc = ConsumeToken();
    if (Tok.is(tok::kw_new))
      return ParseCXXNewExpression(true, CCLoc);
    if (Tok.is(tok::kw_delete))
      return ParseCXXDeleteExpression(true, CCLoc);

    // This is not a type name or scope specifier, it is an invalid expression.
    Diag(CCLoc, diag::err_expected_expression);
    return ExprError();
  }

  case tok::kw_new: // [C++] new-expression
    return ParseCXXNewExpression(false, Tok.getLocation());

  case tok::kw_delete: // [C++] delete-expression
    return ParseCXXDeleteExpression(false, Tok.getLocation());

  case tok::kw_noexcept: { // [C++0x] 'noexcept' '(' expression ')'
    Diag(Tok, diag::warn_cxx98_compat_noexcept_expr);
    SourceLocation KeyLoc = ConsumeToken();
    BalancedDelimiterTracker T(*this, tok::l_paren);

    if (T.expectAndConsume(diag::err_expected_lparen_after, "noexcept"))
      return ExprError();
    // C++11 [expr.unary.noexcept]p1:
    //   The noexcept operator determines whether the evaluation of its operand,
    //   which is an unevaluated operand, can throw an exception.
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    ExprResult Result = ParseExpression();

    T.consumeClose();

    if (!Result.isInvalid())
      Result = Actions.ActOnNoexceptExpr(KeyLoc, T.getOpenLocation(),
                                         Result.get(), T.getCloseLocation());
    return Result;
  }

#define TYPE_TRAIT(N,Spelling,K) \
  case tok::kw_##Spelling:
#include "clang/Basic/TokenKinds.def"
    return ParseTypeTrait();

  case tok::kw___array_rank:
  case tok::kw___array_extent:
    return ParseArrayTypeTrait();

  case tok::kw___is_lvalue_expr:
  case tok::kw___is_rvalue_expr:
    return ParseExpressionTrait();

  case tok::at: {
    SourceLocation AtLoc = ConsumeToken();
    return ParseObjCAtExpression(AtLoc);
  }
  case tok::caret:
    Res = ParseBlockLiteralExpression();
    break;
  case tok::code_completion: {
    Actions.CodeCompleteOrdinaryName(getCurScope(), Sema::PCC_Expression);
    cutOffParsing();
    return ExprError();
  }
  case tok::l_square:
    if (getLangOpts().CPlusPlus11) {
      if (getLangOpts().ObjC) {
        // C++11 lambda expressions and Objective-C message sends both start with a
        // square bracket.  There are three possibilities here:
        // we have a valid lambda expression, we have an invalid lambda
        // expression, or we have something that doesn't appear to be a lambda.
        // If we're in the last case, we fall back to ParseObjCMessageExpression.
        Res = TryParseLambdaExpression();
        if (!Res.isInvalid() && !Res.get())
          Res = ParseObjCMessageExpression();
        break;
      }
      Res = ParseLambdaExpression();
      break;
    }
    if (getLangOpts().ObjC) {
      Res = ParseObjCMessageExpression();
      break;
    }
    LLVM_FALLTHROUGH;
  default:
    NotCastExpr = true;
    return ExprError();
  }

  // Check to see whether Res is a function designator only. If it is and we
  // are compiling for OpenCL, we need to return an error as this implies
  // that the address of the function is being taken, which is illegal in CL.

  // These can be followed by postfix-expr pieces.
  Res = ParsePostfixExpressionSuffix(Res);
  if (getLangOpts().OpenCL)
    if (Expr *PostfixExpr = Res.get()) {
      QualType Ty = PostfixExpr->getType();
      if (!Ty.isNull() && Ty->isFunctionType()) {
        Diag(PostfixExpr->getExprLoc(),
             diag::err_opencl_taking_function_address_parser);
        return ExprError();
      }
    }

  return Res;
}

/// Once the leading part of a postfix-expression is parsed, this
/// method parses any suffixes that apply.
///
/// \verbatim
///       postfix-expression: [C99 6.5.2]
///         primary-expression
///         postfix-expression '[' expression ']'
///         postfix-expression '[' braced-init-list ']'
///         postfix-expression '(' argument-expression-list[opt] ')'
///         postfix-expression '.' identifier
///         postfix-expression '->' identifier
///         postfix-expression '++'
///         postfix-expression '--'
///         '(' type-name ')' '{' initializer-list '}'
///         '(' type-name ')' '{' initializer-list ',' '}'
///
///       argument-expression-list: [C99 6.5.2]
///         argument-expression ...[opt]
///         argument-expression-list ',' assignment-expression ...[opt]
/// \endverbatim
ExprResult
Parser::ParsePostfixExpressionSuffix(ExprResult LHS) {
  // Now that the primary-expression piece of the postfix-expression has been
  // parsed, see if there are any postfix-expression pieces here.
  SourceLocation Loc;
  while (1) {
    switch (Tok.getKind()) {
    case tok::code_completion:
      if (InMessageExpression)
        return LHS;

      Actions.CodeCompletePostfixExpression(getCurScope(), LHS);
      cutOffParsing();
      return ExprError();

    case tok::identifier:
      // If we see identifier: after an expression, and we're not already in a
      // message send, then this is probably a message send with a missing
      // opening bracket '['.
      if (getLangOpts().ObjC && !InMessageExpression &&
          (NextToken().is(tok::colon) || NextToken().is(tok::r_square))) {
        LHS = ParseObjCMessageExpressionBody(SourceLocation(), SourceLocation(),
                                             nullptr, LHS.get());
        break;
      }
      // Fall through; this isn't a message send.
      LLVM_FALLTHROUGH;

    default:  // Not a postfix-expression suffix.
      return LHS;
    case tok::l_square: {  // postfix-expression: p-e '[' expression ']'
      // If we have a array postfix expression that starts on a new line and
      // Objective-C is enabled, it is highly likely that the user forgot a
      // semicolon after the base expression and that the array postfix-expr is
      // actually another message send.  In this case, do some look-ahead to see
      // if the contents of the square brackets are obviously not a valid
      // expression and recover by pretending there is no suffix.
      if (getLangOpts().ObjC && Tok.isAtStartOfLine() &&
          isSimpleObjCMessageExpression())
        return LHS;

      // Reject array indices starting with a lambda-expression. '[[' is
      // reserved for attributes.
      if (CheckProhibitedCXX11Attribute()) {
        (void)Actions.CorrectDelayedTyposInExpr(LHS);
        return ExprError();
      }

      BalancedDelimiterTracker T(*this, tok::l_square);
      T.consumeOpen();
      Loc = T.getOpenLocation();
      ExprResult Idx, Length;
      SourceLocation ColonLoc;
      if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
        Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);
        Idx = ParseBraceInitializer();
      } else if (getLangOpts().OpenMP) {
        ColonProtectionRAIIObject RAII(*this);
        // Parse [: or [ expr or [ expr :
        if (!Tok.is(tok::colon)) {
          // [ expr
          Idx = ParseExpression();
        }
        if (Tok.is(tok::colon)) {
          // Consume ':'
          ColonLoc = ConsumeToken();
          if (Tok.isNot(tok::r_square))
            Length = ParseExpression();
        }
      } else
        Idx = ParseExpression();

      SourceLocation RLoc = Tok.getLocation();

      ExprResult OrigLHS = LHS;
      if (!LHS.isInvalid() && !Idx.isInvalid() && !Length.isInvalid() &&
          Tok.is(tok::r_square)) {
        if (ColonLoc.isValid()) {
          LHS = Actions.ActOnOMPArraySectionExpr(LHS.get(), Loc, Idx.get(),
                                                 ColonLoc, Length.get(), RLoc);
        } else {
          LHS = Actions.ActOnArraySubscriptExpr(getCurScope(), LHS.get(), Loc,
                                                Idx.get(), RLoc);
        }
      } else {
        LHS = ExprError();
      }
      if (LHS.isInvalid()) {
        (void)Actions.CorrectDelayedTyposInExpr(OrigLHS);
        (void)Actions.CorrectDelayedTyposInExpr(Idx);
        (void)Actions.CorrectDelayedTyposInExpr(Length);
        LHS = ExprError();
        Idx = ExprError();
      }

      // Match the ']'.
      T.consumeClose();
      break;
    }

    case tok::l_paren:         // p-e: p-e '(' argument-expression-list[opt] ')'
    case tok::lesslessless: {  // p-e: p-e '<<<' argument-expression-list '>>>'
                               //   '(' argument-expression-list[opt] ')'
      tok::TokenKind OpKind = Tok.getKind();
      InMessageExpressionRAIIObject InMessage(*this, false);

      Expr *ExecConfig = nullptr;

      BalancedDelimiterTracker PT(*this, tok::l_paren);

      if (OpKind == tok::lesslessless) {
        ExprVector ExecConfigExprs;
        CommaLocsTy ExecConfigCommaLocs;
        SourceLocation OpenLoc = ConsumeToken();

        if (ParseSimpleExpressionList(ExecConfigExprs, ExecConfigCommaLocs)) {
          (void)Actions.CorrectDelayedTyposInExpr(LHS);
          LHS = ExprError();
        }

        SourceLocation CloseLoc;
        if (TryConsumeToken(tok::greatergreatergreater, CloseLoc)) {
        } else if (LHS.isInvalid()) {
          SkipUntil(tok::greatergreatergreater, StopAtSemi);
        } else {
          // There was an error closing the brackets
          Diag(Tok, diag::err_expected) << tok::greatergreatergreater;
          Diag(OpenLoc, diag::note_matching) << tok::lesslessless;
          SkipUntil(tok::greatergreatergreater, StopAtSemi);
          LHS = ExprError();
        }

        if (!LHS.isInvalid()) {
          if (ExpectAndConsume(tok::l_paren))
            LHS = ExprError();
          else
            Loc = PrevTokLocation;
        }

        if (!LHS.isInvalid()) {
          ExprResult ECResult = Actions.ActOnCUDAExecConfigExpr(getCurScope(),
                                    OpenLoc,
                                    ExecConfigExprs,
                                    CloseLoc);
          if (ECResult.isInvalid())
            LHS = ExprError();
          else
            ExecConfig = ECResult.get();
        }
      } else {
        PT.consumeOpen();
        Loc = PT.getOpenLocation();
      }

      ExprVector ArgExprs;
      CommaLocsTy CommaLocs;

      if (Tok.is(tok::code_completion)) {
        QualType PreferredType = Actions.ProduceCallSignatureHelp(
            getCurScope(), LHS.get(), None, PT.getOpenLocation());
        CalledSignatureHelp = true;
        Actions.CodeCompleteExpression(getCurScope(), PreferredType);
        cutOffParsing();
        return ExprError();
      }

      if (OpKind == tok::l_paren || !LHS.isInvalid()) {
        if (Tok.isNot(tok::r_paren)) {
          if (ParseExpressionList(ArgExprs, CommaLocs, [&] {
                QualType PreferredType = Actions.ProduceCallSignatureHelp(
                    getCurScope(), LHS.get(), ArgExprs, PT.getOpenLocation());
                CalledSignatureHelp = true;
                Actions.CodeCompleteExpression(getCurScope(), PreferredType);
              })) {
            (void)Actions.CorrectDelayedTyposInExpr(LHS);
            // If we got an error when parsing expression list, we don't call
            // the CodeCompleteCall handler inside the parser. So call it here
            // to make sure we get overload suggestions even when we are in the
            // middle of a parameter.
            if (PP.isCodeCompletionReached() && !CalledSignatureHelp) {
              Actions.ProduceCallSignatureHelp(getCurScope(), LHS.get(),
                                               ArgExprs, PT.getOpenLocation());
              CalledSignatureHelp = true;
            }
            LHS = ExprError();
          } else if (LHS.isInvalid()) {
            for (auto &E : ArgExprs)
              Actions.CorrectDelayedTyposInExpr(E);
          }
        }
      }

      // Match the ')'.
      if (LHS.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
      } else if (Tok.isNot(tok::r_paren)) {
        bool HadDelayedTypo = false;
        if (Actions.CorrectDelayedTyposInExpr(LHS).get() != LHS.get())
          HadDelayedTypo = true;
        for (auto &E : ArgExprs)
          if (Actions.CorrectDelayedTyposInExpr(E).get() != E)
            HadDelayedTypo = true;
        // If there were delayed typos in the LHS or ArgExprs, call SkipUntil
        // instead of PT.consumeClose() to avoid emitting extra diagnostics for
        // the unmatched l_paren.
        if (HadDelayedTypo)
          SkipUntil(tok::r_paren, StopAtSemi);
        else
          PT.consumeClose();
        LHS = ExprError();
      } else {
        assert((ArgExprs.size() == 0 ||
                ArgExprs.size()-1 == CommaLocs.size())&&
               "Unexpected number of commas!");
        LHS = Actions.ActOnCallExpr(getCurScope(), LHS.get(), Loc,
                                    ArgExprs, Tok.getLocation(),
                                    ExecConfig);
        PT.consumeClose();
      }

      break;
    }
    case tok::arrow:
    case tok::period: {
      // postfix-expression: p-e '->' template[opt] id-expression
      // postfix-expression: p-e '.' template[opt] id-expression
      tok::TokenKind OpKind = Tok.getKind();
      SourceLocation OpLoc = ConsumeToken();  // Eat the "." or "->" token.

      CXXScopeSpec SS;
      ParsedType ObjectType;
      bool MayBePseudoDestructor = false;
      Expr* OrigLHS = !LHS.isInvalid() ? LHS.get() : nullptr;

      if (getLangOpts().CPlusPlus && !LHS.isInvalid()) {
        Expr *Base = OrigLHS;
        const Type* BaseType = Base->getType().getTypePtrOrNull();
        if (BaseType && Tok.is(tok::l_paren) &&
            (BaseType->isFunctionType() ||
             BaseType->isSpecificPlaceholderType(BuiltinType::BoundMember))) {
          Diag(OpLoc, diag::err_function_is_not_record)
              << OpKind << Base->getSourceRange()
              << FixItHint::CreateRemoval(OpLoc);
          return ParsePostfixExpressionSuffix(Base);
        }

        LHS = Actions.ActOnStartCXXMemberReference(getCurScope(), Base,
                                                   OpLoc, OpKind, ObjectType,
                                                   MayBePseudoDestructor);
        if (LHS.isInvalid())
          break;

        ParseOptionalCXXScopeSpecifier(SS, ObjectType,
                                       /*EnteringContext=*/false,
                                       &MayBePseudoDestructor);
        if (SS.isNotEmpty())
          ObjectType = nullptr;
      }

      if (Tok.is(tok::code_completion)) {
        tok::TokenKind CorrectedOpKind =
            OpKind == tok::arrow ? tok::period : tok::arrow;
        ExprResult CorrectedLHS(/*IsInvalid=*/true);
        if (getLangOpts().CPlusPlus && OrigLHS) {
          const bool DiagsAreSuppressed = Diags.getSuppressAllDiagnostics();
          Diags.setSuppressAllDiagnostics(true);
          CorrectedLHS = Actions.ActOnStartCXXMemberReference(
              getCurScope(), OrigLHS, OpLoc, CorrectedOpKind, ObjectType,
              MayBePseudoDestructor);
          Diags.setSuppressAllDiagnostics(DiagsAreSuppressed);
        }

        Expr *Base = LHS.get();
        Expr *CorrectedBase = CorrectedLHS.get();
        if (!CorrectedBase && !getLangOpts().CPlusPlus)
          CorrectedBase = Base;

        // Code completion for a member access expression.
        Actions.CodeCompleteMemberReferenceExpr(
            getCurScope(), Base, CorrectedBase, OpLoc, OpKind == tok::arrow,
            Base && ExprStatementTokLoc == Base->getBeginLoc());

        cutOffParsing();
        return ExprError();
      }

      if (MayBePseudoDestructor && !LHS.isInvalid()) {
        LHS = ParseCXXPseudoDestructor(LHS.get(), OpLoc, OpKind, SS,
                                       ObjectType);
        break;
      }

      // Either the action has told us that this cannot be a
      // pseudo-destructor expression (based on the type of base
      // expression), or we didn't see a '~' in the right place. We
      // can still parse a destructor name here, but in that case it
      // names a real destructor.
      // Allow explicit constructor calls in Microsoft mode.
      // FIXME: Add support for explicit call of template constructor.
      SourceLocation TemplateKWLoc;
      UnqualifiedId Name;
      if (getLangOpts().ObjC && OpKind == tok::period &&
          Tok.is(tok::kw_class)) {
        // Objective-C++:
        //   After a '.' in a member access expression, treat the keyword
        //   'class' as if it were an identifier.
        //
        // This hack allows property access to the 'class' method because it is
        // such a common method name. For other C++ keywords that are
        // Objective-C method names, one must use the message send syntax.
        IdentifierInfo *Id = Tok.getIdentifierInfo();
        SourceLocation Loc = ConsumeToken();
        Name.setIdentifier(Id, Loc);
      } else if (ParseUnqualifiedId(SS,
                                    /*EnteringContext=*/false,
                                    /*AllowDestructorName=*/true,
                                    /*AllowConstructorName=*/
                                    getLangOpts().MicrosoftExt &&
                                        SS.isNotEmpty(),
                                    /*AllowDeductionGuide=*/false,
                                    ObjectType, &TemplateKWLoc, Name)) {
        (void)Actions.CorrectDelayedTyposInExpr(LHS);
        LHS = ExprError();
      }

      if (!LHS.isInvalid())
        LHS = Actions.ActOnMemberAccessExpr(getCurScope(), LHS.get(), OpLoc,
                                            OpKind, SS, TemplateKWLoc, Name,
                                 CurParsedObjCImpl ? CurParsedObjCImpl->Dcl
                                                   : nullptr);
      if (!LHS.isInvalid() && Tok.is(tok::less))
        checkPotentialAngleBracket(LHS);
      break;
    }
    case tok::plusplus:    // postfix-expression: postfix-expression '++'
    case tok::minusminus:  // postfix-expression: postfix-expression '--'
      if (!LHS.isInvalid()) {
        LHS = Actions.ActOnPostfixUnaryOp(getCurScope(), Tok.getLocation(),
                                          Tok.getKind(), LHS.get());
      }
      ConsumeToken();
      break;
    }
  }
}

/// ParseExprAfterUnaryExprOrTypeTrait - We parsed a typeof/sizeof/alignof/
/// vec_step and we are at the start of an expression or a parenthesized
/// type-id. OpTok is the operand token (typeof/sizeof/alignof). Returns the
/// expression (isCastExpr == false) or the type (isCastExpr == true).
///
/// \verbatim
///       unary-expression:  [C99 6.5.3]
///         'sizeof' unary-expression
///         'sizeof' '(' type-name ')'
/// [GNU]   '__alignof' unary-expression
/// [GNU]   '__alignof' '(' type-name ')'
/// [C11]   '_Alignof' '(' type-name ')'
/// [C++0x] 'alignof' '(' type-id ')'
///
/// [GNU]   typeof-specifier:
///           typeof ( expressions )
///           typeof ( type-name )
/// [GNU/C++] typeof unary-expression
///
/// [OpenCL 1.1 6.11.12] vec_step built-in function:
///           vec_step ( expressions )
///           vec_step ( type-name )
/// \endverbatim
ExprResult
Parser::ParseExprAfterUnaryExprOrTypeTrait(const Token &OpTok,
                                           bool &isCastExpr,
                                           ParsedType &CastTy,
                                           SourceRange &CastRange) {

  assert(OpTok.isOneOf(tok::kw_typeof, tok::kw_sizeof, tok::kw___alignof,
                       tok::kw_alignof, tok::kw__Alignof, tok::kw_vec_step,
                       tok::kw___builtin_omp_required_simd_align) &&
         "Not a typeof/sizeof/alignof/vec_step expression!");

  ExprResult Operand;

  // If the operand doesn't start with an '(', it must be an expression.
  if (Tok.isNot(tok::l_paren)) {
    // If construct allows a form without parenthesis, user may forget to put
    // pathenthesis around type name.
    if (OpTok.isOneOf(tok::kw_sizeof, tok::kw___alignof, tok::kw_alignof,
                      tok::kw__Alignof)) {
      if (isTypeIdUnambiguously()) {
        DeclSpec DS(AttrFactory);
        ParseSpecifierQualifierList(DS);
        Declarator DeclaratorInfo(DS, DeclaratorContext::TypeNameContext);
        ParseDeclarator(DeclaratorInfo);

        SourceLocation LParenLoc = PP.getLocForEndOfToken(OpTok.getLocation());
        SourceLocation RParenLoc = PP.getLocForEndOfToken(PrevTokLocation);
        Diag(LParenLoc, diag::err_expected_parentheses_around_typename)
          << OpTok.getName()
          << FixItHint::CreateInsertion(LParenLoc, "(")
          << FixItHint::CreateInsertion(RParenLoc, ")");
        isCastExpr = true;
        return ExprEmpty();
      }
    }

    isCastExpr = false;
    if (OpTok.is(tok::kw_typeof) && !getLangOpts().CPlusPlus) {
      Diag(Tok, diag::err_expected_after) << OpTok.getIdentifierInfo()
                                          << tok::l_paren;
      return ExprError();
    }

    Operand = ParseCastExpression(true/*isUnaryExpression*/);
  } else {
    // If it starts with a '(', we know that it is either a parenthesized
    // type-name, or it is a unary-expression that starts with a compound
    // literal, or starts with a primary-expression that is a parenthesized
    // expression.
    ParenParseOption ExprType = CastExpr;
    SourceLocation LParenLoc = Tok.getLocation(), RParenLoc;

    Operand = ParseParenExpression(ExprType, true/*stopIfCastExpr*/,
                                   false, CastTy, RParenLoc);
    CastRange = SourceRange(LParenLoc, RParenLoc);

    // If ParseParenExpression parsed a '(typename)' sequence only, then this is
    // a type.
    if (ExprType == CastExpr) {
      isCastExpr = true;
      return ExprEmpty();
    }

    if (getLangOpts().CPlusPlus || OpTok.isNot(tok::kw_typeof)) {
      // GNU typeof in C requires the expression to be parenthesized. Not so for
      // sizeof/alignof or in C++. Therefore, the parenthesized expression is
      // the start of a unary-expression, but doesn't include any postfix
      // pieces. Parse these now if present.
      if (!Operand.isInvalid())
        Operand = ParsePostfixExpressionSuffix(Operand.get());
    }
  }

  // If we get here, the operand to the typeof/sizeof/alignof was an expression.
  isCastExpr = false;
  return Operand;
}


/// Parse a sizeof or alignof expression.
///
/// \verbatim
///       unary-expression:  [C99 6.5.3]
///         'sizeof' unary-expression
///         'sizeof' '(' type-name ')'
/// [C++11] 'sizeof' '...' '(' identifier ')'
/// [GNU]   '__alignof' unary-expression
/// [GNU]   '__alignof' '(' type-name ')'
/// [C11]   '_Alignof' '(' type-name ')'
/// [C++11] 'alignof' '(' type-id ')'
/// \endverbatim
ExprResult Parser::ParseUnaryExprOrTypeTraitExpression() {
  assert(Tok.isOneOf(tok::kw_sizeof, tok::kw___alignof, tok::kw_alignof,
                     tok::kw__Alignof, tok::kw_vec_step,
                     tok::kw___builtin_omp_required_simd_align) &&
         "Not a sizeof/alignof/vec_step expression!");
  Token OpTok = Tok;
  ConsumeToken();

  // [C++11] 'sizeof' '...' '(' identifier ')'
  if (Tok.is(tok::ellipsis) && OpTok.is(tok::kw_sizeof)) {
    SourceLocation EllipsisLoc = ConsumeToken();
    SourceLocation LParenLoc, RParenLoc;
    IdentifierInfo *Name = nullptr;
    SourceLocation NameLoc;
    if (Tok.is(tok::l_paren)) {
      BalancedDelimiterTracker T(*this, tok::l_paren);
      T.consumeOpen();
      LParenLoc = T.getOpenLocation();
      if (Tok.is(tok::identifier)) {
        Name = Tok.getIdentifierInfo();
        NameLoc = ConsumeToken();
        T.consumeClose();
        RParenLoc = T.getCloseLocation();
        if (RParenLoc.isInvalid())
          RParenLoc = PP.getLocForEndOfToken(NameLoc);
      } else {
        Diag(Tok, diag::err_expected_parameter_pack);
        SkipUntil(tok::r_paren, StopAtSemi);
      }
    } else if (Tok.is(tok::identifier)) {
      Name = Tok.getIdentifierInfo();
      NameLoc = ConsumeToken();
      LParenLoc = PP.getLocForEndOfToken(EllipsisLoc);
      RParenLoc = PP.getLocForEndOfToken(NameLoc);
      Diag(LParenLoc, diag::err_paren_sizeof_parameter_pack)
        << Name
        << FixItHint::CreateInsertion(LParenLoc, "(")
        << FixItHint::CreateInsertion(RParenLoc, ")");
    } else {
      Diag(Tok, diag::err_sizeof_parameter_pack);
    }

    if (!Name)
      return ExprError();

    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated,
        Sema::ReuseLambdaContextDecl);

    return Actions.ActOnSizeofParameterPackExpr(getCurScope(),
                                                OpTok.getLocation(),
                                                *Name, NameLoc,
                                                RParenLoc);
  }

  if (OpTok.isOneOf(tok::kw_alignof, tok::kw__Alignof))
    Diag(OpTok, diag::warn_cxx98_compat_alignof);

  EnterExpressionEvaluationContext Unevaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated,
      Sema::ReuseLambdaContextDecl);

  bool isCastExpr;
  ParsedType CastTy;
  SourceRange CastRange;
  ExprResult Operand = ParseExprAfterUnaryExprOrTypeTrait(OpTok,
                                                          isCastExpr,
                                                          CastTy,
                                                          CastRange);

  UnaryExprOrTypeTrait ExprKind = UETT_SizeOf;
  if (OpTok.isOneOf(tok::kw_alignof, tok::kw__Alignof))
    ExprKind = UETT_AlignOf;
  else if (OpTok.is(tok::kw___alignof))
    ExprKind = UETT_PreferredAlignOf;
  else if (OpTok.is(tok::kw_vec_step))
    ExprKind = UETT_VecStep;
  else if (OpTok.is(tok::kw___builtin_omp_required_simd_align))
    ExprKind = UETT_OpenMPRequiredSimdAlign;

  if (isCastExpr)
    return Actions.ActOnUnaryExprOrTypeTraitExpr(OpTok.getLocation(),
                                                 ExprKind,
                                                 /*isType=*/true,
                                                 CastTy.getAsOpaquePtr(),
                                                 CastRange);

  if (OpTok.isOneOf(tok::kw_alignof, tok::kw__Alignof))
    Diag(OpTok, diag::ext_alignof_expr) << OpTok.getIdentifierInfo();

  // If we get here, the operand to the sizeof/alignof was an expression.
  if (!Operand.isInvalid())
    Operand = Actions.ActOnUnaryExprOrTypeTraitExpr(OpTok.getLocation(),
                                                    ExprKind,
                                                    /*isType=*/false,
                                                    Operand.get(),
                                                    CastRange);
  return Operand;
}

/// ParseBuiltinPrimaryExpression
///
/// \verbatim
///       primary-expression: [C99 6.5.1]
/// [GNU]   '__builtin_va_arg' '(' assignment-expression ',' type-name ')'
/// [GNU]   '__builtin_offsetof' '(' type-name ',' offsetof-member-designator')'
/// [GNU]   '__builtin_choose_expr' '(' assign-expr ',' assign-expr ','
///                                     assign-expr ')'
/// [GNU]   '__builtin_types_compatible_p' '(' type-name ',' type-name ')'
/// [OCL]   '__builtin_astype' '(' assignment-expression ',' type-name ')'
///
/// [GNU] offsetof-member-designator:
/// [GNU]   identifier
/// [GNU]   offsetof-member-designator '.' identifier
/// [GNU]   offsetof-member-designator '[' expression ']'
/// \endverbatim
ExprResult Parser::ParseBuiltinPrimaryExpression() {
  ExprResult Res;
  const IdentifierInfo *BuiltinII = Tok.getIdentifierInfo();

  tok::TokenKind T = Tok.getKind();
  SourceLocation StartLoc = ConsumeToken();   // Eat the builtin identifier.

  // All of these start with an open paren.
  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_after) << BuiltinII
                                                         << tok::l_paren);

  BalancedDelimiterTracker PT(*this, tok::l_paren);
  PT.consumeOpen();

  // TODO: Build AST.

  switch (T) {
  default: llvm_unreachable("Not a builtin primary expression!");
  case tok::kw___builtin_va_arg: {
    ExprResult Expr(ParseAssignmentExpression());

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      Expr = ExprError();
    }

    TypeResult Ty = ParseTypeName();

    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      Expr = ExprError();
    }

    if (Expr.isInvalid() || Ty.isInvalid())
      Res = ExprError();
    else
      Res = Actions.ActOnVAArg(StartLoc, Expr.get(), Ty.get(), ConsumeParen());
    break;
  }
  case tok::kw___builtin_offsetof: {
    SourceLocation TypeLoc = Tok.getLocation();
    TypeResult Ty = ParseTypeName();
    if (Ty.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // We must have at least one identifier here.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Keep track of the various subcomponents we see.
    SmallVector<Sema::OffsetOfComponent, 4> Comps;

    Comps.push_back(Sema::OffsetOfComponent());
    Comps.back().isBrackets = false;
    Comps.back().U.IdentInfo = Tok.getIdentifierInfo();
    Comps.back().LocStart = Comps.back().LocEnd = ConsumeToken();

    // FIXME: This loop leaks the index expressions on error.
    while (1) {
      if (Tok.is(tok::period)) {
        // offsetof-member-designator: offsetof-member-designator '.' identifier
        Comps.push_back(Sema::OffsetOfComponent());
        Comps.back().isBrackets = false;
        Comps.back().LocStart = ConsumeToken();

        if (Tok.isNot(tok::identifier)) {
          Diag(Tok, diag::err_expected) << tok::identifier;
          SkipUntil(tok::r_paren, StopAtSemi);
          return ExprError();
        }
        Comps.back().U.IdentInfo = Tok.getIdentifierInfo();
        Comps.back().LocEnd = ConsumeToken();

      } else if (Tok.is(tok::l_square)) {
        if (CheckProhibitedCXX11Attribute())
          return ExprError();

        // offsetof-member-designator: offsetof-member-design '[' expression ']'
        Comps.push_back(Sema::OffsetOfComponent());
        Comps.back().isBrackets = true;
        BalancedDelimiterTracker ST(*this, tok::l_square);
        ST.consumeOpen();
        Comps.back().LocStart = ST.getOpenLocation();
        Res = ParseExpression();
        if (Res.isInvalid()) {
          SkipUntil(tok::r_paren, StopAtSemi);
          return Res;
        }
        Comps.back().U.E = Res.get();

        ST.consumeClose();
        Comps.back().LocEnd = ST.getCloseLocation();
      } else {
        if (Tok.isNot(tok::r_paren)) {
          PT.consumeClose();
          Res = ExprError();
        } else if (Ty.isInvalid()) {
          Res = ExprError();
        } else {
          PT.consumeClose();
          Res = Actions.ActOnBuiltinOffsetOf(getCurScope(), StartLoc, TypeLoc,
                                             Ty.get(), Comps,
                                             PT.getCloseLocation());
        }
        break;
      }
    }
    break;
  }
  case tok::kw___builtin_choose_expr: {
    ExprResult Cond(ParseAssignmentExpression());
    if (Cond.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Cond;
    }
    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult Expr1(ParseAssignmentExpression());
    if (Expr1.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Expr1;
    }
    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult Expr2(ParseAssignmentExpression());
    if (Expr2.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Expr2;
    }
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      return ExprError();
    }
    Res = Actions.ActOnChooseExpr(StartLoc, Cond.get(), Expr1.get(),
                                  Expr2.get(), ConsumeParen());
    break;
  }
  case tok::kw___builtin_astype: {
    // The first argument is an expression to be converted, followed by a comma.
    ExprResult Expr(ParseAssignmentExpression());
    if (Expr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Second argument is the type to bitcast to.
    TypeResult DestTy = ParseTypeName();
    if (DestTy.isInvalid())
      return ExprError();

    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    Res = Actions.ActOnAsTypeExpr(Expr.get(), DestTy.get(), StartLoc,
                                  ConsumeParen());
    break;
  }
  case tok::kw___builtin_convertvector: {
    // The first argument is an expression to be converted, followed by a comma.
    ExprResult Expr(ParseAssignmentExpression());
    if (Expr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Second argument is the type to bitcast to.
    TypeResult DestTy = ParseTypeName();
    if (DestTy.isInvalid())
      return ExprError();

    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    Res = Actions.ActOnConvertVectorExpr(Expr.get(), DestTy.get(), StartLoc,
                                         ConsumeParen());
    break;
  }
  }

  if (Res.isInvalid())
    return ExprError();

  // These can be followed by postfix-expr pieces because they are
  // primary-expressions.
  return ParsePostfixExpressionSuffix(Res.get());
}

/// ParseParenExpression - This parses the unit that starts with a '(' token,
/// based on what is allowed by ExprType.  The actual thing parsed is returned
/// in ExprType. If stopIfCastExpr is true, it will only return the parsed type,
/// not the parsed cast-expression.
///
/// \verbatim
///       primary-expression: [C99 6.5.1]
///         '(' expression ')'
/// [GNU]   '(' compound-statement ')'      (if !ParenExprOnly)
///       postfix-expression: [C99 6.5.2]
///         '(' type-name ')' '{' initializer-list '}'
///         '(' type-name ')' '{' initializer-list ',' '}'
///       cast-expression: [C99 6.5.4]
///         '(' type-name ')' cast-expression
/// [ARC]   bridged-cast-expression
/// [ARC] bridged-cast-expression:
///         (__bridge type-name) cast-expression
///         (__bridge_transfer type-name) cast-expression
///         (__bridge_retained type-name) cast-expression
///       fold-expression: [C++1z]
///         '(' cast-expression fold-operator '...' ')'
///         '(' '...' fold-operator cast-expression ')'
///         '(' cast-expression fold-operator '...'
///                 fold-operator cast-expression ')'
/// \endverbatim
ExprResult
Parser::ParseParenExpression(ParenParseOption &ExprType, bool stopIfCastExpr,
                             bool isTypeCast, ParsedType &CastTy,
                             SourceLocation &RParenLoc) {
  assert(Tok.is(tok::l_paren) && "Not a paren expr!");
  ColonProtectionRAIIObject ColonProtection(*this, false);
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen())
    return ExprError();
  SourceLocation OpenLoc = T.getOpenLocation();

  ExprResult Result(true);
  bool isAmbiguousTypeId;
  CastTy = nullptr;

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteOrdinaryName(getCurScope(),
                 ExprType >= CompoundLiteral? Sema::PCC_ParenthesizedExpression
                                            : Sema::PCC_Expression);
    cutOffParsing();
    return ExprError();
  }

  // Diagnose use of bridge casts in non-arc mode.
  bool BridgeCast = (getLangOpts().ObjC &&
                     Tok.isOneOf(tok::kw___bridge,
                                 tok::kw___bridge_transfer,
                                 tok::kw___bridge_retained,
                                 tok::kw___bridge_retain));
  if (BridgeCast && !getLangOpts().ObjCAutoRefCount) {
    if (!TryConsumeToken(tok::kw___bridge)) {
      StringRef BridgeCastName = Tok.getName();
      SourceLocation BridgeKeywordLoc = ConsumeToken();
      if (!PP.getSourceManager().isInSystemHeader(BridgeKeywordLoc))
        Diag(BridgeKeywordLoc, diag::warn_arc_bridge_cast_nonarc)
          << BridgeCastName
          << FixItHint::CreateReplacement(BridgeKeywordLoc, "");
    }
    BridgeCast = false;
  }

  // None of these cases should fall through with an invalid Result
  // unless they've already reported an error.
  if (ExprType >= CompoundStmt && Tok.is(tok::l_brace)) {
    Diag(Tok, diag::ext_gnu_statement_expr);

    if (!getCurScope()->getFnParent() && !getCurScope()->getBlockParent()) {
      Result = ExprError(Diag(OpenLoc, diag::err_stmtexpr_file_scope));
    } else {
      // Find the nearest non-record decl context. Variables declared in a
      // statement expression behave as if they were declared in the enclosing
      // function, block, or other code construct.
      DeclContext *CodeDC = Actions.CurContext;
      while (CodeDC->isRecord() || isa<EnumDecl>(CodeDC)) {
        CodeDC = CodeDC->getParent();
        assert(CodeDC && !CodeDC->isFileContext() &&
               "statement expr not in code context");
      }
      Sema::ContextRAII SavedContext(Actions, CodeDC, /*NewThisContext=*/false);

      Actions.ActOnStartStmtExpr();

      StmtResult Stmt(ParseCompoundStatement(true));
      ExprType = CompoundStmt;

      // If the substmt parsed correctly, build the AST node.
      if (!Stmt.isInvalid()) {
        Result = Actions.ActOnStmtExpr(OpenLoc, Stmt.get(), Tok.getLocation());
      } else {
        Actions.ActOnStmtExprError();
      }
    }
  } else if (ExprType >= CompoundLiteral && BridgeCast) {
    tok::TokenKind tokenKind = Tok.getKind();
    SourceLocation BridgeKeywordLoc = ConsumeToken();

    // Parse an Objective-C ARC ownership cast expression.
    ObjCBridgeCastKind Kind;
    if (tokenKind == tok::kw___bridge)
      Kind = OBC_Bridge;
    else if (tokenKind == tok::kw___bridge_transfer)
      Kind = OBC_BridgeTransfer;
    else if (tokenKind == tok::kw___bridge_retained)
      Kind = OBC_BridgeRetained;
    else {
      // As a hopefully temporary workaround, allow __bridge_retain as
      // a synonym for __bridge_retained, but only in system headers.
      assert(tokenKind == tok::kw___bridge_retain);
      Kind = OBC_BridgeRetained;
      if (!PP.getSourceManager().isInSystemHeader(BridgeKeywordLoc))
        Diag(BridgeKeywordLoc, diag::err_arc_bridge_retain)
          << FixItHint::CreateReplacement(BridgeKeywordLoc,
                                          "__bridge_retained");
    }

    TypeResult Ty = ParseTypeName();
    T.consumeClose();
    ColonProtection.restore();
    RParenLoc = T.getCloseLocation();
    ExprResult SubExpr = ParseCastExpression(/*isUnaryExpression=*/false);

    if (Ty.isInvalid() || SubExpr.isInvalid())
      return ExprError();

    return Actions.ActOnObjCBridgedCast(getCurScope(), OpenLoc, Kind,
                                        BridgeKeywordLoc, Ty.get(),
                                        RParenLoc, SubExpr.get());
  } else if (ExprType >= CompoundLiteral &&
             isTypeIdInParens(isAmbiguousTypeId)) {

    // Otherwise, this is a compound literal expression or cast expression.

    // In C++, if the type-id is ambiguous we disambiguate based on context.
    // If stopIfCastExpr is true the context is a typeof/sizeof/alignof
    // in which case we should treat it as type-id.
    // if stopIfCastExpr is false, we need to determine the context past the
    // parens, so we defer to ParseCXXAmbiguousParenExpression for that.
    if (isAmbiguousTypeId && !stopIfCastExpr) {
      ExprResult res = ParseCXXAmbiguousParenExpression(ExprType, CastTy, T,
                                                        ColonProtection);
      RParenLoc = T.getCloseLocation();
      return res;
    }

    // Parse the type declarator.
    DeclSpec DS(AttrFactory);
    ParseSpecifierQualifierList(DS);
    Declarator DeclaratorInfo(DS, DeclaratorContext::TypeNameContext);
    ParseDeclarator(DeclaratorInfo);

    // If our type is followed by an identifier and either ':' or ']', then
    // this is probably an Objective-C message send where the leading '[' is
    // missing. Recover as if that were the case.
    if (!DeclaratorInfo.isInvalidType() && Tok.is(tok::identifier) &&
        !InMessageExpression && getLangOpts().ObjC &&
        (NextToken().is(tok::colon) || NextToken().is(tok::r_square))) {
      TypeResult Ty;
      {
        InMessageExpressionRAIIObject InMessage(*this, false);
        Ty = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo);
      }
      Result = ParseObjCMessageExpressionBody(SourceLocation(),
                                              SourceLocation(),
                                              Ty.get(), nullptr);
    } else {
      // Match the ')'.
      T.consumeClose();
      ColonProtection.restore();
      RParenLoc = T.getCloseLocation();
      if (Tok.is(tok::l_brace)) {
        ExprType = CompoundLiteral;
        TypeResult Ty;
        {
          InMessageExpressionRAIIObject InMessage(*this, false);
          Ty = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo);
        }
        return ParseCompoundLiteralExpression(Ty.get(), OpenLoc, RParenLoc);
      }

      if (Tok.is(tok::l_paren)) {
        // This could be OpenCL vector Literals
        if (getLangOpts().OpenCL)
        {
          TypeResult Ty;
          {
            InMessageExpressionRAIIObject InMessage(*this, false);
            Ty = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo);
          }
          if(Ty.isInvalid())
          {
             return ExprError();
          }
          QualType QT = Ty.get().get().getCanonicalType();
          if (QT->isVectorType())
          {
            // We parsed '(' vector-type-name ')' followed by '('

            // Parse the cast-expression that follows it next.
            // isVectorLiteral = true will make sure we don't parse any
            // Postfix expression yet
            Result = ParseCastExpression(/*isUnaryExpression=*/false,
                                         /*isAddressOfOperand=*/false,
                                         /*isTypeCast=*/IsTypeCast,
                                         /*isVectorLiteral=*/true);

            if (!Result.isInvalid()) {
              Result = Actions.ActOnCastExpr(getCurScope(), OpenLoc,
                                             DeclaratorInfo, CastTy,
                                             RParenLoc, Result.get());
            }

            // After we performed the cast we can check for postfix-expr pieces.
            if (!Result.isInvalid()) {
              Result = ParsePostfixExpressionSuffix(Result);
            }

            return Result;
          }
        }
      }

      if (ExprType == CastExpr) {
        // We parsed '(' type-name ')' and the thing after it wasn't a '{'.

        if (DeclaratorInfo.isInvalidType())
          return ExprError();

        // Note that this doesn't parse the subsequent cast-expression, it just
        // returns the parsed type to the callee.
        if (stopIfCastExpr) {
          TypeResult Ty;
          {
            InMessageExpressionRAIIObject InMessage(*this, false);
            Ty = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo);
          }
          CastTy = Ty.get();
          return ExprResult();
        }

        // Reject the cast of super idiom in ObjC.
        if (Tok.is(tok::identifier) && getLangOpts().ObjC &&
            Tok.getIdentifierInfo() == Ident_super &&
            getCurScope()->isInObjcMethodScope() &&
            GetLookAheadToken(1).isNot(tok::period)) {
          Diag(Tok.getLocation(), diag::err_illegal_super_cast)
            << SourceRange(OpenLoc, RParenLoc);
          return ExprError();
        }

        // Parse the cast-expression that follows it next.
        // TODO: For cast expression with CastTy.
        Result = ParseCastExpression(/*isUnaryExpression=*/false,
                                     /*isAddressOfOperand=*/false,
                                     /*isTypeCast=*/IsTypeCast);
        if (!Result.isInvalid()) {
          Result = Actions.ActOnCastExpr(getCurScope(), OpenLoc,
                                         DeclaratorInfo, CastTy,
                                         RParenLoc, Result.get());
        }
        return Result;
      }

      Diag(Tok, diag::err_expected_lbrace_in_compound_literal);
      return ExprError();
    }
  } else if (ExprType >= FoldExpr && Tok.is(tok::ellipsis) &&
             isFoldOperator(NextToken().getKind())) {
    ExprType = FoldExpr;
    return ParseFoldExpression(ExprResult(), T);
  } else if (isTypeCast) {
    // Parse the expression-list.
    InMessageExpressionRAIIObject InMessage(*this, false);

    ExprVector ArgExprs;
    CommaLocsTy CommaLocs;

    if (!ParseSimpleExpressionList(ArgExprs, CommaLocs)) {
      // FIXME: If we ever support comma expressions as operands to
      // fold-expressions, we'll need to allow multiple ArgExprs here.
      if (ExprType >= FoldExpr && ArgExprs.size() == 1 &&
          isFoldOperator(Tok.getKind()) && NextToken().is(tok::ellipsis)) {
        ExprType = FoldExpr;
        return ParseFoldExpression(ArgExprs[0], T);
      }

      ExprType = SimpleExpr;
      Result = Actions.ActOnParenListExpr(OpenLoc, Tok.getLocation(),
                                          ArgExprs);
    }
  } else {
    InMessageExpressionRAIIObject InMessage(*this, false);

    Result = ParseExpression(MaybeTypeCast);
    if (!getLangOpts().CPlusPlus && MaybeTypeCast && Result.isUsable()) {
      // Correct typos in non-C++ code earlier so that implicit-cast-like
      // expressions are parsed correctly.
      Result = Actions.CorrectDelayedTyposInExpr(Result);
    }

    if (ExprType >= FoldExpr && isFoldOperator(Tok.getKind()) &&
        NextToken().is(tok::ellipsis)) {
      ExprType = FoldExpr;
      return ParseFoldExpression(Result, T);
    }
    ExprType = SimpleExpr;

    // Don't build a paren expression unless we actually match a ')'.
    if (!Result.isInvalid() && Tok.is(tok::r_paren))
      Result =
          Actions.ActOnParenExpr(OpenLoc, Tok.getLocation(), Result.get());
  }

  // Match the ')'.
  if (Result.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  T.consumeClose();
  RParenLoc = T.getCloseLocation();
  return Result;
}

/// ParseCompoundLiteralExpression - We have parsed the parenthesized type-name
/// and we are at the left brace.
///
/// \verbatim
///       postfix-expression: [C99 6.5.2]
///         '(' type-name ')' '{' initializer-list '}'
///         '(' type-name ')' '{' initializer-list ',' '}'
/// \endverbatim
ExprResult
Parser::ParseCompoundLiteralExpression(ParsedType Ty,
                                       SourceLocation LParenLoc,
                                       SourceLocation RParenLoc) {
  assert(Tok.is(tok::l_brace) && "Not a compound literal!");
  if (!getLangOpts().C99)   // Compound literals don't exist in C90.
    Diag(LParenLoc, diag::ext_c99_compound_literal);
  ExprResult Result = ParseInitializer();
  if (!Result.isInvalid() && Ty)
    return Actions.ActOnCompoundLiteral(LParenLoc, Ty, RParenLoc, Result.get());
  return Result;
}

/// ParseStringLiteralExpression - This handles the various token types that
/// form string literals, and also handles string concatenation [C99 5.1.1.2,
/// translation phase #6].
///
/// \verbatim
///       primary-expression: [C99 6.5.1]
///         string-literal
/// \verbatim
ExprResult Parser::ParseStringLiteralExpression(bool AllowUserDefinedLiteral) {
  assert(isTokenStringLiteral() && "Not a string literal!");

  // String concat.  Note that keywords like __func__ and __FUNCTION__ are not
  // considered to be strings for concatenation purposes.
  SmallVector<Token, 4> StringToks;

  do {
    StringToks.push_back(Tok);
    ConsumeStringToken();
  } while (isTokenStringLiteral());

  // Pass the set of string tokens, ready for concatenation, to the actions.
  return Actions.ActOnStringLiteral(StringToks,
                                    AllowUserDefinedLiteral ? getCurScope()
                                                            : nullptr);
}

/// ParseGenericSelectionExpression - Parse a C11 generic-selection
/// [C11 6.5.1.1].
///
/// \verbatim
///    generic-selection:
///           _Generic ( assignment-expression , generic-assoc-list )
///    generic-assoc-list:
///           generic-association
///           generic-assoc-list , generic-association
///    generic-association:
///           type-name : assignment-expression
///           default : assignment-expression
/// \endverbatim
ExprResult Parser::ParseGenericSelectionExpression() {
  assert(Tok.is(tok::kw__Generic) && "_Generic keyword expected");
  SourceLocation KeyLoc = ConsumeToken();

  if (!getLangOpts().C11)
    Diag(KeyLoc, diag::ext_c11_generic_selection);

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  ExprResult ControllingExpr;
  {
    // C11 6.5.1.1p3 "The controlling expression of a generic selection is
    // not evaluated."
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    ControllingExpr =
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    if (ControllingExpr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
  }

  if (ExpectAndConsume(tok::comma)) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  SourceLocation DefaultLoc;
  TypeVector Types;
  ExprVector Exprs;
  do {
    ParsedType Ty;
    if (Tok.is(tok::kw_default)) {
      // C11 6.5.1.1p2 "A generic selection shall have no more than one default
      // generic association."
      if (!DefaultLoc.isInvalid()) {
        Diag(Tok, diag::err_duplicate_default_assoc);
        Diag(DefaultLoc, diag::note_previous_default_assoc);
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
      DefaultLoc = ConsumeToken();
      Ty = nullptr;
    } else {
      ColonProtectionRAIIObject X(*this);
      TypeResult TR = ParseTypeName();
      if (TR.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
      Ty = TR.get();
    }
    Types.push_back(Ty);

    if (ExpectAndConsume(tok::colon)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // FIXME: These expressions should be parsed in a potentially potentially
    // evaluated context.
    ExprResult ER(
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression()));
    if (ER.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    Exprs.push_back(ER.get());
  } while (TryConsumeToken(tok::comma));

  T.consumeClose();
  if (T.getCloseLocation().isInvalid())
    return ExprError();

  return Actions.ActOnGenericSelectionExpr(KeyLoc, DefaultLoc,
                                           T.getCloseLocation(),
                                           ControllingExpr.get(),
                                           Types, Exprs);
}

/// Parse A C++1z fold-expression after the opening paren and optional
/// left-hand-side expression.
///
/// \verbatim
///   fold-expression:
///       ( cast-expression fold-operator ... )
///       ( ... fold-operator cast-expression )
///       ( cast-expression fold-operator ... fold-operator cast-expression )
ExprResult Parser::ParseFoldExpression(ExprResult LHS,
                                       BalancedDelimiterTracker &T) {
  if (LHS.isInvalid()) {
    T.skipToEnd();
    return true;
  }

  tok::TokenKind Kind = tok::unknown;
  SourceLocation FirstOpLoc;
  if (LHS.isUsable()) {
    Kind = Tok.getKind();
    assert(isFoldOperator(Kind) && "missing fold-operator");
    FirstOpLoc = ConsumeToken();
  }

  assert(Tok.is(tok::ellipsis) && "not a fold-expression");
  SourceLocation EllipsisLoc = ConsumeToken();

  ExprResult RHS;
  if (Tok.isNot(tok::r_paren)) {
    if (!isFoldOperator(Tok.getKind()))
      return Diag(Tok.getLocation(), diag::err_expected_fold_operator);

    if (Kind != tok::unknown && Tok.getKind() != Kind)
      Diag(Tok.getLocation(), diag::err_fold_operator_mismatch)
        << SourceRange(FirstOpLoc);
    Kind = Tok.getKind();
    ConsumeToken();

    RHS = ParseExpression();
    if (RHS.isInvalid()) {
      T.skipToEnd();
      return true;
    }
  }

  Diag(EllipsisLoc, getLangOpts().CPlusPlus17
                        ? diag::warn_cxx14_compat_fold_expression
                        : diag::ext_fold_expression);

  T.consumeClose();
  return Actions.ActOnCXXFoldExpr(T.getOpenLocation(), LHS.get(), Kind,
                                  EllipsisLoc, RHS.get(), T.getCloseLocation());
}

/// ParseExpressionList - Used for C/C++ (argument-)expression-list.
///
/// \verbatim
///       argument-expression-list:
///         assignment-expression
///         argument-expression-list , assignment-expression
///
/// [C++] expression-list:
/// [C++]   assignment-expression
/// [C++]   expression-list , assignment-expression
///
/// [C++0x] expression-list:
/// [C++0x]   initializer-list
///
/// [C++0x] initializer-list
/// [C++0x]   initializer-clause ...[opt]
/// [C++0x]   initializer-list , initializer-clause ...[opt]
///
/// [C++0x] initializer-clause:
/// [C++0x]   assignment-expression
/// [C++0x]   braced-init-list
/// \endverbatim
bool Parser::ParseExpressionList(SmallVectorImpl<Expr *> &Exprs,
                                 SmallVectorImpl<SourceLocation> &CommaLocs,
                                 llvm::function_ref<void()> Completer) {
  bool SawError = false;
  while (1) {
    if (Tok.is(tok::code_completion)) {
      if (Completer)
        Completer();
      else
        Actions.CodeCompleteOrdinaryName(getCurScope(), Sema::PCC_Expression);
      cutOffParsing();
      return true;
    }

    ExprResult Expr;
    if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
      Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);
      Expr = ParseBraceInitializer();
    } else
      Expr = ParseAssignmentExpression();

    if (Tok.is(tok::ellipsis))
      Expr = Actions.ActOnPackExpansion(Expr.get(), ConsumeToken());
    if (Expr.isInvalid()) {
      SkipUntil(tok::comma, tok::r_paren, StopBeforeMatch);
      SawError = true;
    } else {
      Exprs.push_back(Expr.get());
    }

    if (Tok.isNot(tok::comma))
      break;
    // Move to the next argument, remember where the comma was.
    Token Comma = Tok;
    CommaLocs.push_back(ConsumeToken());

    checkPotentialAngleBracketDelimiter(Comma);
  }
  if (SawError) {
    // Ensure typos get diagnosed when errors were encountered while parsing the
    // expression list.
    for (auto &E : Exprs) {
      ExprResult Expr = Actions.CorrectDelayedTyposInExpr(E);
      if (Expr.isUsable()) E = Expr.get();
    }
  }
  return SawError;
}

/// ParseSimpleExpressionList - A simple comma-separated list of expressions,
/// used for misc language extensions.
///
/// \verbatim
///       simple-expression-list:
///         assignment-expression
///         simple-expression-list , assignment-expression
/// \endverbatim
bool
Parser::ParseSimpleExpressionList(SmallVectorImpl<Expr*> &Exprs,
                                  SmallVectorImpl<SourceLocation> &CommaLocs) {
  while (1) {
    ExprResult Expr = ParseAssignmentExpression();
    if (Expr.isInvalid())
      return true;

    Exprs.push_back(Expr.get());

    if (Tok.isNot(tok::comma))
      return false;

    // Move to the next argument, remember where the comma was.
    Token Comma = Tok;
    CommaLocs.push_back(ConsumeToken());

    checkPotentialAngleBracketDelimiter(Comma);
  }
}

/// ParseBlockId - Parse a block-id, which roughly looks like int (int x).
///
/// \verbatim
/// [clang] block-id:
/// [clang]   specifier-qualifier-list block-declarator
/// \endverbatim
void Parser::ParseBlockId(SourceLocation CaretLoc) {
  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteOrdinaryName(getCurScope(), Sema::PCC_Type);
    return cutOffParsing();
  }

  // Parse the specifier-qualifier-list piece.
  DeclSpec DS(AttrFactory);
  ParseSpecifierQualifierList(DS);

  // Parse the block-declarator.
  Declarator DeclaratorInfo(DS, DeclaratorContext::BlockLiteralContext);
  DeclaratorInfo.setFunctionDefinitionKind(FDK_Definition);
  ParseDeclarator(DeclaratorInfo);

  MaybeParseGNUAttributes(DeclaratorInfo);

  // Inform sema that we are starting a block.
  Actions.ActOnBlockArguments(CaretLoc, DeclaratorInfo, getCurScope());
}

/// ParseBlockLiteralExpression - Parse a block literal, which roughly looks
/// like ^(int x){ return x+1; }
///
/// \verbatim
///         block-literal:
/// [clang]   '^' block-args[opt] compound-statement
/// [clang]   '^' block-id compound-statement
/// [clang] block-args:
/// [clang]   '(' parameter-list ')'
/// \endverbatim
ExprResult Parser::ParseBlockLiteralExpression() {
  assert(Tok.is(tok::caret) && "block literal starts with ^");
  SourceLocation CaretLoc = ConsumeToken();

  PrettyStackTraceLoc CrashInfo(PP.getSourceManager(), CaretLoc,
                                "block literal parsing");

  // Enter a scope to hold everything within the block.  This includes the
  // argument decls, decls within the compound expression, etc.  This also
  // allows determining whether a variable reference inside the block is
  // within or outside of the block.
  ParseScope BlockScope(this, Scope::BlockScope | Scope::FnScope |
                                  Scope::CompoundStmtScope | Scope::DeclScope);

  // Inform sema that we are starting a block.
  Actions.ActOnBlockStart(CaretLoc, getCurScope());

  // Parse the return type if present.
  DeclSpec DS(AttrFactory);
  Declarator ParamInfo(DS, DeclaratorContext::BlockLiteralContext);
  ParamInfo.setFunctionDefinitionKind(FDK_Definition);
  // FIXME: Since the return type isn't actually parsed, it can't be used to
  // fill ParamInfo with an initial valid range, so do it manually.
  ParamInfo.SetSourceRange(SourceRange(Tok.getLocation(), Tok.getLocation()));

  // If this block has arguments, parse them.  There is no ambiguity here with
  // the expression case, because the expression case requires a parameter list.
  if (Tok.is(tok::l_paren)) {
    ParseParenDeclarator(ParamInfo);
    // Parse the pieces after the identifier as if we had "int(...)".
    // SetIdentifier sets the source range end, but in this case we're past
    // that location.
    SourceLocation Tmp = ParamInfo.getSourceRange().getEnd();
    ParamInfo.SetIdentifier(nullptr, CaretLoc);
    ParamInfo.SetRangeEnd(Tmp);
    if (ParamInfo.isInvalidType()) {
      // If there was an error parsing the arguments, they may have
      // tried to use ^(x+y) which requires an argument list.  Just
      // skip the whole block literal.
      Actions.ActOnBlockError(CaretLoc, getCurScope());
      return ExprError();
    }

    MaybeParseGNUAttributes(ParamInfo);

    // Inform sema that we are starting a block.
    Actions.ActOnBlockArguments(CaretLoc, ParamInfo, getCurScope());
  } else if (!Tok.is(tok::l_brace)) {
    ParseBlockId(CaretLoc);
  } else {
    // Otherwise, pretend we saw (void).
    SourceLocation NoLoc;
    ParamInfo.AddTypeInfo(
        DeclaratorChunk::getFunction(/*HasProto=*/true,
                                     /*IsAmbiguous=*/false,
                                     /*RParenLoc=*/NoLoc,
                                     /*ArgInfo=*/nullptr,
                                     /*NumArgs=*/0,
                                     /*EllipsisLoc=*/NoLoc,
                                     /*RParenLoc=*/NoLoc,
                                     /*RefQualifierIsLvalueRef=*/true,
                                     /*RefQualifierLoc=*/NoLoc,
                                     /*MutableLoc=*/NoLoc, EST_None,
                                     /*ESpecRange=*/SourceRange(),
                                     /*Exceptions=*/nullptr,
                                     /*ExceptionRanges=*/nullptr,
                                     /*NumExceptions=*/0,
                                     /*NoexceptExpr=*/nullptr,
                                     /*ExceptionSpecTokens=*/nullptr,
                                     /*DeclsInPrototype=*/None, CaretLoc,
                                     CaretLoc, ParamInfo),
        CaretLoc);

    MaybeParseGNUAttributes(ParamInfo);

    // Inform sema that we are starting a block.
    Actions.ActOnBlockArguments(CaretLoc, ParamInfo, getCurScope());
  }


  ExprResult Result(true);
  if (!Tok.is(tok::l_brace)) {
    // Saw something like: ^expr
    Diag(Tok, diag::err_expected_expression);
    Actions.ActOnBlockError(CaretLoc, getCurScope());
    return ExprError();
  }

  StmtResult Stmt(ParseCompoundStatementBody());
  BlockScope.Exit();
  if (!Stmt.isInvalid())
    Result = Actions.ActOnBlockStmtExpr(CaretLoc, Stmt.get(), getCurScope());
  else
    Actions.ActOnBlockError(CaretLoc, getCurScope());
  return Result;
}

/// ParseObjCBoolLiteral - This handles the objective-c Boolean literals.
///
///         '__objc_yes'
///         '__objc_no'
ExprResult Parser::ParseObjCBoolLiteral() {
  tok::TokenKind Kind = Tok.getKind();
  return Actions.ActOnObjCBoolLiteral(ConsumeToken(), Kind);
}

/// Validate availability spec list, emitting diagnostics if necessary. Returns
/// true if invalid.
static bool CheckAvailabilitySpecList(Parser &P,
                                      ArrayRef<AvailabilitySpec> AvailSpecs) {
  llvm::SmallSet<StringRef, 4> Platforms;
  bool HasOtherPlatformSpec = false;
  bool Valid = true;
  for (const auto &Spec : AvailSpecs) {
    if (Spec.isOtherPlatformSpec()) {
      if (HasOtherPlatformSpec) {
        P.Diag(Spec.getBeginLoc(), diag::err_availability_query_repeated_star);
        Valid = false;
      }

      HasOtherPlatformSpec = true;
      continue;
    }

    bool Inserted = Platforms.insert(Spec.getPlatform()).second;
    if (!Inserted) {
      // Rule out multiple version specs referring to the same platform.
      // For example, we emit an error for:
      // @available(macos 10.10, macos 10.11, *)
      StringRef Platform = Spec.getPlatform();
      P.Diag(Spec.getBeginLoc(), diag::err_availability_query_repeated_platform)
          << Spec.getEndLoc() << Platform;
      Valid = false;
    }
  }

  if (!HasOtherPlatformSpec) {
    SourceLocation InsertWildcardLoc = AvailSpecs.back().getEndLoc();
    P.Diag(InsertWildcardLoc, diag::err_availability_query_wildcard_required)
        << FixItHint::CreateInsertion(InsertWildcardLoc, ", *");
    return true;
  }

  return !Valid;
}

/// Parse availability query specification.
///
///  availability-spec:
///     '*'
///     identifier version-tuple
Optional<AvailabilitySpec> Parser::ParseAvailabilitySpec() {
  if (Tok.is(tok::star)) {
    return AvailabilitySpec(ConsumeToken());
  } else {
    // Parse the platform name.
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteAvailabilityPlatformName();
      cutOffParsing();
      return None;
    }
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_avail_query_expected_platform_name);
      return None;
    }

    IdentifierLoc *PlatformIdentifier = ParseIdentifierLoc();
    SourceRange VersionRange;
    VersionTuple Version = ParseVersionTuple(VersionRange);

    if (Version.empty())
      return None;

    StringRef GivenPlatform = PlatformIdentifier->Ident->getName();
    StringRef Platform =
        AvailabilityAttr::canonicalizePlatformName(GivenPlatform);

    if (AvailabilityAttr::getPrettyPlatformName(Platform).empty()) {
      Diag(PlatformIdentifier->Loc,
           diag::err_avail_query_unrecognized_platform_name)
          << GivenPlatform;
      return None;
    }

    return AvailabilitySpec(Version, Platform, PlatformIdentifier->Loc,
                            VersionRange.getEnd());
  }
}

ExprResult Parser::ParseAvailabilityCheckExpr(SourceLocation BeginLoc) {
  assert(Tok.is(tok::kw___builtin_available) ||
         Tok.isObjCAtKeyword(tok::objc_available));

  // Eat the available or __builtin_available.
  ConsumeToken();

  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<AvailabilitySpec, 4> AvailSpecs;
  bool HasError = false;
  while (true) {
    Optional<AvailabilitySpec> Spec = ParseAvailabilitySpec();
    if (!Spec)
      HasError = true;
    else
      AvailSpecs.push_back(*Spec);

    if (!TryConsumeToken(tok::comma))
      break;
  }

  if (HasError) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  CheckAvailabilitySpecList(*this, AvailSpecs);

  if (Parens.consumeClose())
    return ExprError();

  return Actions.ActOnObjCAvailabilityCheckExpr(AvailSpecs, BeginLoc,
                                                Parens.getCloseLocation());
}
