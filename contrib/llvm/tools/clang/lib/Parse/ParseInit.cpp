//===--- ParseInit.cpp - Initializer Parsing ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements initializer parsing as specified by C99 6.7.8.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/Scope.h"
#include "llvm/ADT/SmallString.h"
using namespace clang;


/// MayBeDesignationStart - Return true if the current token might be the start
/// of a designator.  If we can tell it is impossible that it is a designator,
/// return false.
bool Parser::MayBeDesignationStart() {
  switch (Tok.getKind()) {
  default:
    return false;

  case tok::period:      // designator: '.' identifier
    return true;

  case tok::l_square: {  // designator: array-designator
    if (!PP.getLangOpts().CPlusPlus11)
      return true;

    // C++11 lambda expressions and C99 designators can be ambiguous all the
    // way through the closing ']' and to the next character. Handle the easy
    // cases here, and fall back to tentative parsing if those fail.
    switch (PP.LookAhead(0).getKind()) {
    case tok::equal:
    case tok::r_square:
      // Definitely starts a lambda expression.
      return false;

    case tok::amp:
    case tok::kw_this:
    case tok::identifier:
      // We have to do additional analysis, because these could be the
      // start of a constant expression or a lambda capture list.
      break;

    default:
      // Anything not mentioned above cannot occur following a '[' in a
      // lambda expression.
      return true;
    }

    // Handle the complicated case below.
    break;
  }
  case tok::identifier:  // designation: identifier ':'
    return PP.LookAhead(0).is(tok::colon);
  }

  // Parse up to (at most) the token after the closing ']' to determine
  // whether this is a C99 designator or a lambda.
  TentativeParsingAction Tentative(*this);

  LambdaIntroducer Intro;
  bool SkippedInits = false;
  Optional<unsigned> DiagID(ParseLambdaIntroducer(Intro, &SkippedInits));

  if (DiagID) {
    // If this can't be a lambda capture list, it's a designator.
    Tentative.Revert();
    return true;
  }

  // Once we hit the closing square bracket, we look at the next
  // token. If it's an '=', this is a designator. Otherwise, it's a
  // lambda expression. This decision favors lambdas over the older
  // GNU designator syntax, which allows one to omit the '=', but is
  // consistent with GCC.
  tok::TokenKind Kind = Tok.getKind();
  // FIXME: If we didn't skip any inits, parse the lambda from here
  // rather than throwing away then reparsing the LambdaIntroducer.
  Tentative.Revert();
  return Kind == tok::equal;
}

static void CheckArrayDesignatorSyntax(Parser &P, SourceLocation Loc,
                                       Designation &Desig) {
  // If we have exactly one array designator, this used the GNU
  // 'designation: array-designator' extension, otherwise there should be no
  // designators at all!
  if (Desig.getNumDesignators() == 1 &&
      (Desig.getDesignator(0).isArrayDesignator() ||
       Desig.getDesignator(0).isArrayRangeDesignator()))
    P.Diag(Loc, diag::ext_gnu_missing_equal_designator);
  else if (Desig.getNumDesignators() > 0)
    P.Diag(Loc, diag::err_expected_equal_designator);
}

/// ParseInitializerWithPotentialDesignator - Parse the 'initializer' production
/// checking to see if the token stream starts with a designator.
///
///       designation:
///         designator-list '='
/// [GNU]   array-designator
/// [GNU]   identifier ':'
///
///       designator-list:
///         designator
///         designator-list designator
///
///       designator:
///         array-designator
///         '.' identifier
///
///       array-designator:
///         '[' constant-expression ']'
/// [GNU]   '[' constant-expression '...' constant-expression ']'
///
/// NOTE: [OBC] allows '[ objc-receiver objc-message-args ]' as an
/// initializer (because it is an expression).  We need to consider this case
/// when parsing array designators.
///
ExprResult Parser::ParseInitializerWithPotentialDesignator() {

  // If this is the old-style GNU extension:
  //   designation ::= identifier ':'
  // Handle it as a field designator.  Otherwise, this must be the start of a
  // normal expression.
  if (Tok.is(tok::identifier)) {
    const IdentifierInfo *FieldName = Tok.getIdentifierInfo();

    SmallString<256> NewSyntax;
    llvm::raw_svector_ostream(NewSyntax) << '.' << FieldName->getName()
                                         << " = ";

    SourceLocation NameLoc = ConsumeToken(); // Eat the identifier.

    assert(Tok.is(tok::colon) && "MayBeDesignationStart not working properly!");
    SourceLocation ColonLoc = ConsumeToken();

    Diag(NameLoc, diag::ext_gnu_old_style_field_designator)
      << FixItHint::CreateReplacement(SourceRange(NameLoc, ColonLoc),
                                      NewSyntax);

    Designation D;
    D.AddDesignator(Designator::getField(FieldName, SourceLocation(), NameLoc));
    return Actions.ActOnDesignatedInitializer(D, ColonLoc, true,
                                              ParseInitializer());
  }

  // Desig - This is initialized when we see our first designator.  We may have
  // an objc message send with no designator, so we don't want to create this
  // eagerly.
  Designation Desig;

  // Parse each designator in the designator list until we find an initializer.
  while (Tok.is(tok::period) || Tok.is(tok::l_square)) {
    if (Tok.is(tok::period)) {
      // designator: '.' identifier
      SourceLocation DotLoc = ConsumeToken();

      if (Tok.isNot(tok::identifier)) {
        Diag(Tok.getLocation(), diag::err_expected_field_designator);
        return ExprError();
      }

      Desig.AddDesignator(Designator::getField(Tok.getIdentifierInfo(), DotLoc,
                                               Tok.getLocation()));
      ConsumeToken(); // Eat the identifier.
      continue;
    }

    // We must have either an array designator now or an objc message send.
    assert(Tok.is(tok::l_square) && "Unexpected token!");

    // Handle the two forms of array designator:
    //   array-designator: '[' constant-expression ']'
    //   array-designator: '[' constant-expression '...' constant-expression ']'
    //
    // Also, we have to handle the case where the expression after the
    // designator an an objc message send: '[' objc-message-expr ']'.
    // Interesting cases are:
    //   [foo bar]         -> objc message send
    //   [foo]             -> array designator
    //   [foo ... bar]     -> array designator
    //   [4][foo bar]      -> obsolete GNU designation with objc message send.
    //
    // We do not need to check for an expression starting with [[ here. If it
    // contains an Objective-C message send, then it is not an ill-formed
    // attribute. If it is a lambda-expression within an array-designator, then
    // it will be rejected because a constant-expression cannot begin with a
    // lambda-expression.
    InMessageExpressionRAIIObject InMessage(*this, true);

    BalancedDelimiterTracker T(*this, tok::l_square);
    T.consumeOpen();
    SourceLocation StartLoc = T.getOpenLocation();

    ExprResult Idx;

    // If Objective-C is enabled and this is a typename (class message
    // send) or send to 'super', parse this as a message send
    // expression.  We handle C++ and C separately, since C++ requires
    // much more complicated parsing.
    if  (getLangOpts().ObjC && getLangOpts().CPlusPlus) {
      // Send to 'super'.
      if (Tok.is(tok::identifier) && Tok.getIdentifierInfo() == Ident_super &&
          NextToken().isNot(tok::period) &&
          getCurScope()->isInObjcMethodScope()) {
        CheckArrayDesignatorSyntax(*this, StartLoc, Desig);
        return ParseAssignmentExprWithObjCMessageExprStart(
            StartLoc, ConsumeToken(), nullptr, nullptr);
      }

      // Parse the receiver, which is either a type or an expression.
      bool IsExpr;
      void *TypeOrExpr;
      if (ParseObjCXXMessageReceiver(IsExpr, TypeOrExpr)) {
        SkipUntil(tok::r_square, StopAtSemi);
        return ExprError();
      }

      // If the receiver was a type, we have a class message; parse
      // the rest of it.
      if (!IsExpr) {
        CheckArrayDesignatorSyntax(*this, StartLoc, Desig);
        return ParseAssignmentExprWithObjCMessageExprStart(StartLoc,
                                                           SourceLocation(),
                                   ParsedType::getFromOpaquePtr(TypeOrExpr),
                                                           nullptr);
      }

      // If the receiver was an expression, we still don't know
      // whether we have a message send or an array designator; just
      // adopt the expression for further analysis below.
      // FIXME: potentially-potentially evaluated expression above?
      Idx = ExprResult(static_cast<Expr*>(TypeOrExpr));
    } else if (getLangOpts().ObjC && Tok.is(tok::identifier)) {
      IdentifierInfo *II = Tok.getIdentifierInfo();
      SourceLocation IILoc = Tok.getLocation();
      ParsedType ReceiverType;
      // Three cases. This is a message send to a type: [type foo]
      // This is a message send to super:  [super foo]
      // This is a message sent to an expr:  [super.bar foo]
      switch (Actions.getObjCMessageKind(
          getCurScope(), II, IILoc, II == Ident_super,
          NextToken().is(tok::period), ReceiverType)) {
      case Sema::ObjCSuperMessage:
        CheckArrayDesignatorSyntax(*this, StartLoc, Desig);
        return ParseAssignmentExprWithObjCMessageExprStart(
            StartLoc, ConsumeToken(), nullptr, nullptr);

      case Sema::ObjCClassMessage:
        CheckArrayDesignatorSyntax(*this, StartLoc, Desig);
        ConsumeToken(); // the identifier
        if (!ReceiverType) {
          SkipUntil(tok::r_square, StopAtSemi);
          return ExprError();
        }

        // Parse type arguments and protocol qualifiers.
        if (Tok.is(tok::less)) {
          SourceLocation NewEndLoc;
          TypeResult NewReceiverType
            = parseObjCTypeArgsAndProtocolQualifiers(IILoc, ReceiverType,
                                                     /*consumeLastToken=*/true,
                                                     NewEndLoc);
          if (!NewReceiverType.isUsable()) {
            SkipUntil(tok::r_square, StopAtSemi);
            return ExprError();
          }

          ReceiverType = NewReceiverType.get();
        }

        return ParseAssignmentExprWithObjCMessageExprStart(StartLoc,
                                                           SourceLocation(),
                                                           ReceiverType,
                                                           nullptr);

      case Sema::ObjCInstanceMessage:
        // Fall through; we'll just parse the expression and
        // (possibly) treat this like an Objective-C message send
        // later.
        break;
      }
    }

    // Parse the index expression, if we haven't already gotten one
    // above (which can only happen in Objective-C++).
    // Note that we parse this as an assignment expression, not a constant
    // expression (allowing *=, =, etc) to handle the objc case.  Sema needs
    // to validate that the expression is a constant.
    // FIXME: We also need to tell Sema that we're in a
    // potentially-potentially evaluated context.
    if (!Idx.get()) {
      Idx = ParseAssignmentExpression();
      if (Idx.isInvalid()) {
        SkipUntil(tok::r_square, StopAtSemi);
        return Idx;
      }
    }

    // Given an expression, we could either have a designator (if the next
    // tokens are '...' or ']' or an objc message send.  If this is an objc
    // message send, handle it now.  An objc-message send is the start of
    // an assignment-expression production.
    if (getLangOpts().ObjC && Tok.isNot(tok::ellipsis) &&
        Tok.isNot(tok::r_square)) {
      CheckArrayDesignatorSyntax(*this, Tok.getLocation(), Desig);
      return ParseAssignmentExprWithObjCMessageExprStart(
          StartLoc, SourceLocation(), nullptr, Idx.get());
    }

    // If this is a normal array designator, remember it.
    if (Tok.isNot(tok::ellipsis)) {
      Desig.AddDesignator(Designator::getArray(Idx.get(), StartLoc));
    } else {
      // Handle the gnu array range extension.
      Diag(Tok, diag::ext_gnu_array_range);
      SourceLocation EllipsisLoc = ConsumeToken();

      ExprResult RHS(ParseConstantExpression());
      if (RHS.isInvalid()) {
        SkipUntil(tok::r_square, StopAtSemi);
        return RHS;
      }
      Desig.AddDesignator(Designator::getArrayRange(Idx.get(),
                                                    RHS.get(),
                                                    StartLoc, EllipsisLoc));
    }

    T.consumeClose();
    Desig.getDesignator(Desig.getNumDesignators() - 1).setRBracketLoc(
                                                        T.getCloseLocation());
  }

  // Okay, we're done with the designator sequence.  We know that there must be
  // at least one designator, because the only case we can get into this method
  // without a designator is when we have an objc message send.  That case is
  // handled and returned from above.
  assert(!Desig.empty() && "Designator is empty?");

  // Handle a normal designator sequence end, which is an equal.
  if (Tok.is(tok::equal)) {
    SourceLocation EqualLoc = ConsumeToken();
    return Actions.ActOnDesignatedInitializer(Desig, EqualLoc, false,
                                              ParseInitializer());
  }

  // We read some number of designators and found something that isn't an = or
  // an initializer.  If we have exactly one array designator, this
  // is the GNU 'designation: array-designator' extension.  Otherwise, it is a
  // parse error.
  if (Desig.getNumDesignators() == 1 &&
      (Desig.getDesignator(0).isArrayDesignator() ||
       Desig.getDesignator(0).isArrayRangeDesignator())) {
    Diag(Tok, diag::ext_gnu_missing_equal_designator)
      << FixItHint::CreateInsertion(Tok.getLocation(), "= ");
    return Actions.ActOnDesignatedInitializer(Desig, Tok.getLocation(),
                                              true, ParseInitializer());
  }

  Diag(Tok, diag::err_expected_equal_designator);
  return ExprError();
}


/// ParseBraceInitializer - Called when parsing an initializer that has a
/// leading open brace.
///
///       initializer: [C99 6.7.8]
///         '{' initializer-list '}'
///         '{' initializer-list ',' '}'
/// [GNU]   '{' '}'
///
///       initializer-list:
///         designation[opt] initializer ...[opt]
///         initializer-list ',' designation[opt] initializer ...[opt]
///
ExprResult Parser::ParseBraceInitializer() {
  InMessageExpressionRAIIObject InMessage(*this, false);

  BalancedDelimiterTracker T(*this, tok::l_brace);
  T.consumeOpen();
  SourceLocation LBraceLoc = T.getOpenLocation();

  /// InitExprs - This is the actual list of expressions contained in the
  /// initializer.
  ExprVector InitExprs;

  if (Tok.is(tok::r_brace)) {
    // Empty initializers are a C++ feature and a GNU extension to C.
    if (!getLangOpts().CPlusPlus)
      Diag(LBraceLoc, diag::ext_gnu_empty_initializer);
    // Match the '}'.
    return Actions.ActOnInitList(LBraceLoc, None, ConsumeBrace());
  }

  // Enter an appropriate expression evaluation context for an initializer list.
  EnterExpressionEvaluationContext EnterContext(
      Actions, EnterExpressionEvaluationContext::InitList);

  bool InitExprsOk = true;

  while (1) {
    // Handle Microsoft __if_exists/if_not_exists if necessary.
    if (getLangOpts().MicrosoftExt && (Tok.is(tok::kw___if_exists) ||
        Tok.is(tok::kw___if_not_exists))) {
      if (ParseMicrosoftIfExistsBraceInitializer(InitExprs, InitExprsOk)) {
        if (Tok.isNot(tok::comma)) break;
        ConsumeToken();
      }
      if (Tok.is(tok::r_brace)) break;
      continue;
    }

    // Parse: designation[opt] initializer

    // If we know that this cannot be a designation, just parse the nested
    // initializer directly.
    ExprResult SubElt;
    if (MayBeDesignationStart())
      SubElt = ParseInitializerWithPotentialDesignator();
    else
      SubElt = ParseInitializer();

    if (Tok.is(tok::ellipsis))
      SubElt = Actions.ActOnPackExpansion(SubElt.get(), ConsumeToken());

    SubElt = Actions.CorrectDelayedTyposInExpr(SubElt.get());

    // If we couldn't parse the subelement, bail out.
    if (SubElt.isUsable()) {
      InitExprs.push_back(SubElt.get());
    } else {
      InitExprsOk = false;

      // We have two ways to try to recover from this error: if the code looks
      // grammatically ok (i.e. we have a comma coming up) try to continue
      // parsing the rest of the initializer.  This allows us to emit
      // diagnostics for later elements that we find.  If we don't see a comma,
      // assume there is a parse error, and just skip to recover.
      // FIXME: This comment doesn't sound right. If there is a r_brace
      // immediately, it can't be an error, since there is no other way of
      // leaving this loop except through this if.
      if (Tok.isNot(tok::comma)) {
        SkipUntil(tok::r_brace, StopBeforeMatch);
        break;
      }
    }

    // If we don't have a comma continued list, we're done.
    if (Tok.isNot(tok::comma)) break;

    // TODO: save comma locations if some client cares.
    ConsumeToken();

    // Handle trailing comma.
    if (Tok.is(tok::r_brace)) break;
  }

  bool closed = !T.consumeClose();

  if (InitExprsOk && closed)
    return Actions.ActOnInitList(LBraceLoc, InitExprs,
                                 T.getCloseLocation());

  return ExprError(); // an error occurred.
}


// Return true if a comma (or closing brace) is necessary after the
// __if_exists/if_not_exists statement.
bool Parser::ParseMicrosoftIfExistsBraceInitializer(ExprVector &InitExprs,
                                                    bool &InitExprsOk) {
  bool trailingComma = false;
  IfExistsCondition Result;
  if (ParseMicrosoftIfExistsCondition(Result))
    return false;

  BalancedDelimiterTracker Braces(*this, tok::l_brace);
  if (Braces.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return false;
  }

  switch (Result.Behavior) {
  case IEB_Parse:
    // Parse the declarations below.
    break;

  case IEB_Dependent:
    Diag(Result.KeywordLoc, diag::warn_microsoft_dependent_exists)
      << Result.IsIfExists;
    // Fall through to skip.
    LLVM_FALLTHROUGH;

  case IEB_Skip:
    Braces.skipToEnd();
    return false;
  }

  while (!isEofOrEom()) {
    trailingComma = false;
    // If we know that this cannot be a designation, just parse the nested
    // initializer directly.
    ExprResult SubElt;
    if (MayBeDesignationStart())
      SubElt = ParseInitializerWithPotentialDesignator();
    else
      SubElt = ParseInitializer();

    if (Tok.is(tok::ellipsis))
      SubElt = Actions.ActOnPackExpansion(SubElt.get(), ConsumeToken());

    // If we couldn't parse the subelement, bail out.
    if (!SubElt.isInvalid())
      InitExprs.push_back(SubElt.get());
    else
      InitExprsOk = false;

    if (Tok.is(tok::comma)) {
      ConsumeToken();
      trailingComma = true;
    }

    if (Tok.is(tok::r_brace))
      break;
  }

  Braces.consumeClose();

  return !trailingComma;
}
