//===--- ParseExprCXX.cpp - C++ Expression Parsing ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Expression parsing implementation for C++.
//
//===----------------------------------------------------------------------===//
#include "clang/Parse/Parser.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "llvm/Support/ErrorHandling.h"


using namespace clang;

static int SelectDigraphErrorMessage(tok::TokenKind Kind) {
  switch (Kind) {
    // template name
    case tok::unknown:             return 0;
    // casts
    case tok::kw_const_cast:       return 1;
    case tok::kw_dynamic_cast:     return 2;
    case tok::kw_reinterpret_cast: return 3;
    case tok::kw_static_cast:      return 4;
    default:
      llvm_unreachable("Unknown type for digraph error message.");
  }
}

// Are the two tokens adjacent in the same source file?
bool Parser::areTokensAdjacent(const Token &First, const Token &Second) {
  SourceManager &SM = PP.getSourceManager();
  SourceLocation FirstLoc = SM.getSpellingLoc(First.getLocation());
  SourceLocation FirstEnd = FirstLoc.getLocWithOffset(First.getLength());
  return FirstEnd == SM.getSpellingLoc(Second.getLocation());
}

// Suggest fixit for "<::" after a cast.
static void FixDigraph(Parser &P, Preprocessor &PP, Token &DigraphToken,
                       Token &ColonToken, tok::TokenKind Kind, bool AtDigraph) {
  // Pull '<:' and ':' off token stream.
  if (!AtDigraph)
    PP.Lex(DigraphToken);
  PP.Lex(ColonToken);

  SourceRange Range;
  Range.setBegin(DigraphToken.getLocation());
  Range.setEnd(ColonToken.getLocation());
  P.Diag(DigraphToken.getLocation(), diag::err_missing_whitespace_digraph)
      << SelectDigraphErrorMessage(Kind)
      << FixItHint::CreateReplacement(Range, "< ::");

  // Update token information to reflect their change in token type.
  ColonToken.setKind(tok::coloncolon);
  ColonToken.setLocation(ColonToken.getLocation().getLocWithOffset(-1));
  ColonToken.setLength(2);
  DigraphToken.setKind(tok::less);
  DigraphToken.setLength(1);

  // Push new tokens back to token stream.
  PP.EnterToken(ColonToken);
  if (!AtDigraph)
    PP.EnterToken(DigraphToken);
}

// Check for '<::' which should be '< ::' instead of '[:' when following
// a template name.
void Parser::CheckForTemplateAndDigraph(Token &Next, ParsedType ObjectType,
                                        bool EnteringContext,
                                        IdentifierInfo &II, CXXScopeSpec &SS) {
  if (!Next.is(tok::l_square) || Next.getLength() != 2)
    return;

  Token SecondToken = GetLookAheadToken(2);
  if (!SecondToken.is(tok::colon) || !areTokensAdjacent(Next, SecondToken))
    return;

  TemplateTy Template;
  UnqualifiedId TemplateName;
  TemplateName.setIdentifier(&II, Tok.getLocation());
  bool MemberOfUnknownSpecialization;
  if (!Actions.isTemplateName(getCurScope(), SS, /*hasTemplateKeyword=*/false,
                              TemplateName, ObjectType, EnteringContext,
                              Template, MemberOfUnknownSpecialization))
    return;

  FixDigraph(*this, PP, Next, SecondToken, tok::unknown,
             /*AtDigraph*/false);
}

/// Parse global scope or nested-name-specifier if present.
///
/// Parses a C++ global scope specifier ('::') or nested-name-specifier (which
/// may be preceded by '::'). Note that this routine will not parse ::new or
/// ::delete; it will just leave them in the token stream.
///
///       '::'[opt] nested-name-specifier
///       '::'
///
///       nested-name-specifier:
///         type-name '::'
///         namespace-name '::'
///         nested-name-specifier identifier '::'
///         nested-name-specifier 'template'[opt] simple-template-id '::'
///
///
/// \param SS the scope specifier that will be set to the parsed
/// nested-name-specifier (or empty)
///
/// \param ObjectType if this nested-name-specifier is being parsed following
/// the "." or "->" of a member access expression, this parameter provides the
/// type of the object whose members are being accessed.
///
/// \param EnteringContext whether we will be entering into the context of
/// the nested-name-specifier after parsing it.
///
/// \param MayBePseudoDestructor When non-NULL, points to a flag that
/// indicates whether this nested-name-specifier may be part of a
/// pseudo-destructor name. In this case, the flag will be set false
/// if we don't actually end up parsing a destructor name. Moreorover,
/// if we do end up determining that we are parsing a destructor name,
/// the last component of the nested-name-specifier is not parsed as
/// part of the scope specifier.
///
/// \param IsTypename If \c true, this nested-name-specifier is known to be
/// part of a type name. This is used to improve error recovery.
///
/// \param LastII When non-NULL, points to an IdentifierInfo* that will be
/// filled in with the leading identifier in the last component of the
/// nested-name-specifier, if any.
///
/// \param OnlyNamespace If true, only considers namespaces in lookup.
///
/// \returns true if there was an error parsing a scope specifier
bool Parser::ParseOptionalCXXScopeSpecifier(CXXScopeSpec &SS,
                                            ParsedType ObjectType,
                                            bool EnteringContext,
                                            bool *MayBePseudoDestructor,
                                            bool IsTypename,
                                            IdentifierInfo **LastII,
                                            bool OnlyNamespace) {
  assert(getLangOpts().CPlusPlus &&
         "Call sites of this function should be guarded by checking for C++");

  if (Tok.is(tok::annot_cxxscope)) {
    assert(!LastII && "want last identifier but have already annotated scope");
    assert(!MayBePseudoDestructor && "unexpected annot_cxxscope");
    Actions.RestoreNestedNameSpecifierAnnotation(Tok.getAnnotationValue(),
                                                 Tok.getAnnotationRange(),
                                                 SS);
    ConsumeAnnotationToken();
    return false;
  }

  if (Tok.is(tok::annot_template_id)) {
    // If the current token is an annotated template id, it may already have
    // a scope specifier. Restore it.
    TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
    SS = TemplateId->SS;
  }

  // Has to happen before any "return false"s in this function.
  bool CheckForDestructor = false;
  if (MayBePseudoDestructor && *MayBePseudoDestructor) {
    CheckForDestructor = true;
    *MayBePseudoDestructor = false;
  }

  if (LastII)
    *LastII = nullptr;

  bool HasScopeSpecifier = false;

  if (Tok.is(tok::coloncolon)) {
    // ::new and ::delete aren't nested-name-specifiers.
    tok::TokenKind NextKind = NextToken().getKind();
    if (NextKind == tok::kw_new || NextKind == tok::kw_delete)
      return false;

    if (NextKind == tok::l_brace) {
      // It is invalid to have :: {, consume the scope qualifier and pretend
      // like we never saw it.
      Diag(ConsumeToken(), diag::err_expected) << tok::identifier;
    } else {
      // '::' - Global scope qualifier.
      if (Actions.ActOnCXXGlobalScopeSpecifier(ConsumeToken(), SS))
        return true;

      HasScopeSpecifier = true;
    }
  }

  if (Tok.is(tok::kw___super)) {
    SourceLocation SuperLoc = ConsumeToken();
    if (!Tok.is(tok::coloncolon)) {
      Diag(Tok.getLocation(), diag::err_expected_coloncolon_after_super);
      return true;
    }

    return Actions.ActOnSuperScopeSpecifier(SuperLoc, ConsumeToken(), SS);
  }

  if (!HasScopeSpecifier &&
      Tok.isOneOf(tok::kw_decltype, tok::annot_decltype)) {
    DeclSpec DS(AttrFactory);
    SourceLocation DeclLoc = Tok.getLocation();
    SourceLocation EndLoc  = ParseDecltypeSpecifier(DS);

    SourceLocation CCLoc;
    // Work around a standard defect: 'decltype(auto)::' is not a
    // nested-name-specifier.
    if (DS.getTypeSpecType() == DeclSpec::TST_decltype_auto ||
        !TryConsumeToken(tok::coloncolon, CCLoc)) {
      AnnotateExistingDecltypeSpecifier(DS, DeclLoc, EndLoc);
      return false;
    }

    if (Actions.ActOnCXXNestedNameSpecifierDecltype(SS, DS, CCLoc))
      SS.SetInvalid(SourceRange(DeclLoc, CCLoc));

    HasScopeSpecifier = true;
  }

  while (true) {
    if (HasScopeSpecifier) {
      if (Tok.is(tok::code_completion)) {
        // Code completion for a nested-name-specifier, where the code
        // completion token follows the '::'.
        Actions.CodeCompleteQualifiedId(getCurScope(), SS, EnteringContext,
                                        ObjectType.get());
        // Include code completion token into the range of the scope otherwise
        // when we try to annotate the scope tokens the dangling code completion
        // token will cause assertion in
        // Preprocessor::AnnotatePreviousCachedTokens.
        SS.setEndLoc(Tok.getLocation());
        cutOffParsing();
        return true;
      }

      // C++ [basic.lookup.classref]p5:
      //   If the qualified-id has the form
      //
      //       ::class-name-or-namespace-name::...
      //
      //   the class-name-or-namespace-name is looked up in global scope as a
      //   class-name or namespace-name.
      //
      // To implement this, we clear out the object type as soon as we've
      // seen a leading '::' or part of a nested-name-specifier.
      ObjectType = nullptr;
    }

    // nested-name-specifier:
    //   nested-name-specifier 'template'[opt] simple-template-id '::'

    // Parse the optional 'template' keyword, then make sure we have
    // 'identifier <' after it.
    if (Tok.is(tok::kw_template)) {
      // If we don't have a scope specifier or an object type, this isn't a
      // nested-name-specifier, since they aren't allowed to start with
      // 'template'.
      if (!HasScopeSpecifier && !ObjectType)
        break;

      TentativeParsingAction TPA(*this);
      SourceLocation TemplateKWLoc = ConsumeToken();

      UnqualifiedId TemplateName;
      if (Tok.is(tok::identifier)) {
        // Consume the identifier.
        TemplateName.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
        ConsumeToken();
      } else if (Tok.is(tok::kw_operator)) {
        // We don't need to actually parse the unqualified-id in this case,
        // because a simple-template-id cannot start with 'operator', but
        // go ahead and parse it anyway for consistency with the case where
        // we already annotated the template-id.
        if (ParseUnqualifiedIdOperator(SS, EnteringContext, ObjectType,
                                       TemplateName)) {
          TPA.Commit();
          break;
        }

        if (TemplateName.getKind() != UnqualifiedIdKind::IK_OperatorFunctionId &&
            TemplateName.getKind() != UnqualifiedIdKind::IK_LiteralOperatorId) {
          Diag(TemplateName.getSourceRange().getBegin(),
               diag::err_id_after_template_in_nested_name_spec)
            << TemplateName.getSourceRange();
          TPA.Commit();
          break;
        }
      } else {
        TPA.Revert();
        break;
      }

      // If the next token is not '<', we have a qualified-id that refers
      // to a template name, such as T::template apply, but is not a
      // template-id.
      if (Tok.isNot(tok::less)) {
        TPA.Revert();
        break;
      }

      // Commit to parsing the template-id.
      TPA.Commit();
      TemplateTy Template;
      if (TemplateNameKind TNK = Actions.ActOnDependentTemplateName(
              getCurScope(), SS, TemplateKWLoc, TemplateName, ObjectType,
              EnteringContext, Template, /*AllowInjectedClassName*/ true)) {
        if (AnnotateTemplateIdToken(Template, TNK, SS, TemplateKWLoc,
                                    TemplateName, false))
          return true;
      } else
        return true;

      continue;
    }

    if (Tok.is(tok::annot_template_id) && NextToken().is(tok::coloncolon)) {
      // We have
      //
      //   template-id '::'
      //
      // So we need to check whether the template-id is a simple-template-id of
      // the right kind (it should name a type or be dependent), and then
      // convert it into a type within the nested-name-specifier.
      TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
      if (CheckForDestructor && GetLookAheadToken(2).is(tok::tilde)) {
        *MayBePseudoDestructor = true;
        return false;
      }

      if (LastII)
        *LastII = TemplateId->Name;

      // Consume the template-id token.
      ConsumeAnnotationToken();

      assert(Tok.is(tok::coloncolon) && "NextToken() not working properly!");
      SourceLocation CCLoc = ConsumeToken();

      HasScopeSpecifier = true;

      ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                         TemplateId->NumArgs);

      if (Actions.ActOnCXXNestedNameSpecifier(getCurScope(),
                                              SS,
                                              TemplateId->TemplateKWLoc,
                                              TemplateId->Template,
                                              TemplateId->TemplateNameLoc,
                                              TemplateId->LAngleLoc,
                                              TemplateArgsPtr,
                                              TemplateId->RAngleLoc,
                                              CCLoc,
                                              EnteringContext)) {
        SourceLocation StartLoc
          = SS.getBeginLoc().isValid()? SS.getBeginLoc()
                                      : TemplateId->TemplateNameLoc;
        SS.SetInvalid(SourceRange(StartLoc, CCLoc));
      }

      continue;
    }

    // The rest of the nested-name-specifier possibilities start with
    // tok::identifier.
    if (Tok.isNot(tok::identifier))
      break;

    IdentifierInfo &II = *Tok.getIdentifierInfo();

    // nested-name-specifier:
    //   type-name '::'
    //   namespace-name '::'
    //   nested-name-specifier identifier '::'
    Token Next = NextToken();
    Sema::NestedNameSpecInfo IdInfo(&II, Tok.getLocation(), Next.getLocation(),
                                    ObjectType);

    // If we get foo:bar, this is almost certainly a typo for foo::bar.  Recover
    // and emit a fixit hint for it.
    if (Next.is(tok::colon) && !ColonIsSacred) {
      if (Actions.IsInvalidUnlessNestedName(getCurScope(), SS, IdInfo,
                                            EnteringContext) &&
          // If the token after the colon isn't an identifier, it's still an
          // error, but they probably meant something else strange so don't
          // recover like this.
          PP.LookAhead(1).is(tok::identifier)) {
        Diag(Next, diag::err_unexpected_colon_in_nested_name_spec)
          << FixItHint::CreateReplacement(Next.getLocation(), "::");
        // Recover as if the user wrote '::'.
        Next.setKind(tok::coloncolon);
      }
    }

    if (Next.is(tok::coloncolon) && GetLookAheadToken(2).is(tok::l_brace)) {
      // It is invalid to have :: {, consume the scope qualifier and pretend
      // like we never saw it.
      Token Identifier = Tok; // Stash away the identifier.
      ConsumeToken();         // Eat the identifier, current token is now '::'.
      Diag(PP.getLocForEndOfToken(ConsumeToken()), diag::err_expected)
          << tok::identifier;
      UnconsumeToken(Identifier); // Stick the identifier back.
      Next = NextToken();         // Point Next at the '{' token.
    }

    if (Next.is(tok::coloncolon)) {
      if (CheckForDestructor && GetLookAheadToken(2).is(tok::tilde) &&
          !Actions.isNonTypeNestedNameSpecifier(getCurScope(), SS, IdInfo)) {
        *MayBePseudoDestructor = true;
        return false;
      }

      if (ColonIsSacred) {
        const Token &Next2 = GetLookAheadToken(2);
        if (Next2.is(tok::kw_private) || Next2.is(tok::kw_protected) ||
            Next2.is(tok::kw_public) || Next2.is(tok::kw_virtual)) {
          Diag(Next2, diag::err_unexpected_token_in_nested_name_spec)
              << Next2.getName()
              << FixItHint::CreateReplacement(Next.getLocation(), ":");
          Token ColonColon;
          PP.Lex(ColonColon);
          ColonColon.setKind(tok::colon);
          PP.EnterToken(ColonColon);
          break;
        }
      }

      if (LastII)
        *LastII = &II;

      // We have an identifier followed by a '::'. Lookup this name
      // as the name in a nested-name-specifier.
      Token Identifier = Tok;
      SourceLocation IdLoc = ConsumeToken();
      assert(Tok.isOneOf(tok::coloncolon, tok::colon) &&
             "NextToken() not working properly!");
      Token ColonColon = Tok;
      SourceLocation CCLoc = ConsumeToken();

      bool IsCorrectedToColon = false;
      bool *CorrectionFlagPtr = ColonIsSacred ? &IsCorrectedToColon : nullptr;
      if (Actions.ActOnCXXNestedNameSpecifier(
              getCurScope(), IdInfo, EnteringContext, SS, false,
              CorrectionFlagPtr, OnlyNamespace)) {
        // Identifier is not recognized as a nested name, but we can have
        // mistyped '::' instead of ':'.
        if (CorrectionFlagPtr && IsCorrectedToColon) {
          ColonColon.setKind(tok::colon);
          PP.EnterToken(Tok);
          PP.EnterToken(ColonColon);
          Tok = Identifier;
          break;
        }
        SS.SetInvalid(SourceRange(IdLoc, CCLoc));
      }
      HasScopeSpecifier = true;
      continue;
    }

    CheckForTemplateAndDigraph(Next, ObjectType, EnteringContext, II, SS);

    // nested-name-specifier:
    //   type-name '<'
    if (Next.is(tok::less)) {
      TemplateTy Template;
      UnqualifiedId TemplateName;
      TemplateName.setIdentifier(&II, Tok.getLocation());
      bool MemberOfUnknownSpecialization;
      if (TemplateNameKind TNK = Actions.isTemplateName(getCurScope(), SS,
                                              /*hasTemplateKeyword=*/false,
                                                        TemplateName,
                                                        ObjectType,
                                                        EnteringContext,
                                                        Template,
                                              MemberOfUnknownSpecialization)) {
        // We have found a template name, so annotate this token
        // with a template-id annotation. We do not permit the
        // template-id to be translated into a type annotation,
        // because some clients (e.g., the parsing of class template
        // specializations) still want to see the original template-id
        // token.
        ConsumeToken();
        if (AnnotateTemplateIdToken(Template, TNK, SS, SourceLocation(),
                                    TemplateName, false))
          return true;
        continue;
      }

      if (MemberOfUnknownSpecialization && (ObjectType || SS.isSet()) &&
          (IsTypename || IsTemplateArgumentList(1))) {
        // We have something like t::getAs<T>, where getAs is a
        // member of an unknown specialization. However, this will only
        // parse correctly as a template, so suggest the keyword 'template'
        // before 'getAs' and treat this as a dependent template name.
        unsigned DiagID = diag::err_missing_dependent_template_keyword;
        if (getLangOpts().MicrosoftExt)
          DiagID = diag::warn_missing_dependent_template_keyword;

        Diag(Tok.getLocation(), DiagID)
          << II.getName()
          << FixItHint::CreateInsertion(Tok.getLocation(), "template ");

        if (TemplateNameKind TNK = Actions.ActOnDependentTemplateName(
                getCurScope(), SS, Tok.getLocation(), TemplateName, ObjectType,
                EnteringContext, Template, /*AllowInjectedClassName*/ true)) {
          // Consume the identifier.
          ConsumeToken();
          if (AnnotateTemplateIdToken(Template, TNK, SS, SourceLocation(),
                                      TemplateName, false))
            return true;
        }
        else
          return true;

        continue;
      }
    }

    // We don't have any tokens that form the beginning of a
    // nested-name-specifier, so we're done.
    break;
  }

  // Even if we didn't see any pieces of a nested-name-specifier, we
  // still check whether there is a tilde in this position, which
  // indicates a potential pseudo-destructor.
  if (CheckForDestructor && Tok.is(tok::tilde))
    *MayBePseudoDestructor = true;

  return false;
}

ExprResult Parser::tryParseCXXIdExpression(CXXScopeSpec &SS, bool isAddressOfOperand,
                                           Token &Replacement) {
  SourceLocation TemplateKWLoc;
  UnqualifiedId Name;
  if (ParseUnqualifiedId(SS,
                         /*EnteringContext=*/false,
                         /*AllowDestructorName=*/false,
                         /*AllowConstructorName=*/false,
                         /*AllowDeductionGuide=*/false,
                         /*ObjectType=*/nullptr, &TemplateKWLoc, Name))
    return ExprError();

  // This is only the direct operand of an & operator if it is not
  // followed by a postfix-expression suffix.
  if (isAddressOfOperand && isPostfixExpressionSuffixStart())
    isAddressOfOperand = false;

  ExprResult E = Actions.ActOnIdExpression(
      getCurScope(), SS, TemplateKWLoc, Name, Tok.is(tok::l_paren),
      isAddressOfOperand, nullptr, /*IsInlineAsmIdentifier=*/false,
      &Replacement);
  if (!E.isInvalid() && !E.isUnset() && Tok.is(tok::less))
    checkPotentialAngleBracket(E);
  return E;
}

/// ParseCXXIdExpression - Handle id-expression.
///
///       id-expression:
///         unqualified-id
///         qualified-id
///
///       qualified-id:
///         '::'[opt] nested-name-specifier 'template'[opt] unqualified-id
///         '::' identifier
///         '::' operator-function-id
///         '::' template-id
///
/// NOTE: The standard specifies that, for qualified-id, the parser does not
/// expect:
///
///   '::' conversion-function-id
///   '::' '~' class-name
///
/// This may cause a slight inconsistency on diagnostics:
///
/// class C {};
/// namespace A {}
/// void f() {
///   :: A :: ~ C(); // Some Sema error about using destructor with a
///                  // namespace.
///   :: ~ C(); // Some Parser error like 'unexpected ~'.
/// }
///
/// We simplify the parser a bit and make it work like:
///
///       qualified-id:
///         '::'[opt] nested-name-specifier 'template'[opt] unqualified-id
///         '::' unqualified-id
///
/// That way Sema can handle and report similar errors for namespaces and the
/// global scope.
///
/// The isAddressOfOperand parameter indicates that this id-expression is a
/// direct operand of the address-of operator. This is, besides member contexts,
/// the only place where a qualified-id naming a non-static class member may
/// appear.
///
ExprResult Parser::ParseCXXIdExpression(bool isAddressOfOperand) {
  // qualified-id:
  //   '::'[opt] nested-name-specifier 'template'[opt] unqualified-id
  //   '::' unqualified-id
  //
  CXXScopeSpec SS;
  ParseOptionalCXXScopeSpecifier(SS, nullptr, /*EnteringContext=*/false);

  Token Replacement;
  ExprResult Result =
      tryParseCXXIdExpression(SS, isAddressOfOperand, Replacement);
  if (Result.isUnset()) {
    // If the ExprResult is valid but null, then typo correction suggested a
    // keyword replacement that needs to be reparsed.
    UnconsumeToken(Replacement);
    Result = tryParseCXXIdExpression(SS, isAddressOfOperand, Replacement);
  }
  assert(!Result.isUnset() && "Typo correction suggested a keyword replacement "
                              "for a previous keyword suggestion");
  return Result;
}

/// ParseLambdaExpression - Parse a C++11 lambda expression.
///
///       lambda-expression:
///         lambda-introducer lambda-declarator[opt] compound-statement
///
///       lambda-introducer:
///         '[' lambda-capture[opt] ']'
///
///       lambda-capture:
///         capture-default
///         capture-list
///         capture-default ',' capture-list
///
///       capture-default:
///         '&'
///         '='
///
///       capture-list:
///         capture
///         capture-list ',' capture
///
///       capture:
///         simple-capture
///         init-capture     [C++1y]
///
///       simple-capture:
///         identifier
///         '&' identifier
///         'this'
///
///       init-capture:      [C++1y]
///         identifier initializer
///         '&' identifier initializer
///
///       lambda-declarator:
///         '(' parameter-declaration-clause ')' attribute-specifier[opt]
///           'mutable'[opt] exception-specification[opt]
///           trailing-return-type[opt]
///
ExprResult Parser::ParseLambdaExpression() {
  // Parse lambda-introducer.
  LambdaIntroducer Intro;
  Optional<unsigned> DiagID = ParseLambdaIntroducer(Intro);
  if (DiagID) {
    Diag(Tok, DiagID.getValue());
    SkipUntil(tok::r_square, StopAtSemi);
    SkipUntil(tok::l_brace, StopAtSemi);
    SkipUntil(tok::r_brace, StopAtSemi);
    return ExprError();
  }

  return ParseLambdaExpressionAfterIntroducer(Intro);
}

/// TryParseLambdaExpression - Use lookahead and potentially tentative
/// parsing to determine if we are looking at a C++0x lambda expression, and parse
/// it if we are.
///
/// If we are not looking at a lambda expression, returns ExprError().
ExprResult Parser::TryParseLambdaExpression() {
  assert(getLangOpts().CPlusPlus11
         && Tok.is(tok::l_square)
         && "Not at the start of a possible lambda expression.");

  const Token Next = NextToken();
  if (Next.is(tok::eof)) // Nothing else to lookup here...
    return ExprEmpty();

  const Token After = GetLookAheadToken(2);
  // If lookahead indicates this is a lambda...
  if (Next.is(tok::r_square) ||     // []
      Next.is(tok::equal) ||        // [=
      (Next.is(tok::amp) &&         // [&] or [&,
       (After.is(tok::r_square) ||
        After.is(tok::comma))) ||
      (Next.is(tok::identifier) &&  // [identifier]
       After.is(tok::r_square))) {
    return ParseLambdaExpression();
  }

  // If lookahead indicates an ObjC message send...
  // [identifier identifier
  if (Next.is(tok::identifier) && After.is(tok::identifier)) {
    return ExprEmpty();
  }

  // Here, we're stuck: lambda introducers and Objective-C message sends are
  // unambiguous, but it requires arbitrary lookhead.  [a,b,c,d,e,f,g] is a
  // lambda, and [a,b,c,d,e,f,g h] is a Objective-C message send.  Instead of
  // writing two routines to parse a lambda introducer, just try to parse
  // a lambda introducer first, and fall back if that fails.
  // (TryParseLambdaIntroducer never produces any diagnostic output.)
  LambdaIntroducer Intro;
  if (TryParseLambdaIntroducer(Intro))
    return ExprEmpty();

  return ParseLambdaExpressionAfterIntroducer(Intro);
}

/// Parse a lambda introducer.
/// \param Intro A LambdaIntroducer filled in with information about the
///        contents of the lambda-introducer.
/// \param SkippedInits If non-null, we are disambiguating between an Obj-C
///        message send and a lambda expression. In this mode, we will
///        sometimes skip the initializers for init-captures and not fully
///        populate \p Intro. This flag will be set to \c true if we do so.
/// \return A DiagnosticID if it hit something unexpected. The location for
///         the diagnostic is that of the current token.
Optional<unsigned> Parser::ParseLambdaIntroducer(LambdaIntroducer &Intro,
                                                 bool *SkippedInits) {
  typedef Optional<unsigned> DiagResult;

  assert(Tok.is(tok::l_square) && "Lambda expressions begin with '['.");
  BalancedDelimiterTracker T(*this, tok::l_square);
  T.consumeOpen();

  Intro.Range.setBegin(T.getOpenLocation());

  bool first = true;

  // Parse capture-default.
  if (Tok.is(tok::amp) &&
      (NextToken().is(tok::comma) || NextToken().is(tok::r_square))) {
    Intro.Default = LCD_ByRef;
    Intro.DefaultLoc = ConsumeToken();
    first = false;
  } else if (Tok.is(tok::equal)) {
    Intro.Default = LCD_ByCopy;
    Intro.DefaultLoc = ConsumeToken();
    first = false;
  }

  while (Tok.isNot(tok::r_square)) {
    if (!first) {
      if (Tok.isNot(tok::comma)) {
        // Provide a completion for a lambda introducer here. Except
        // in Objective-C, where this is Almost Surely meant to be a message
        // send. In that case, fail here and let the ObjC message
        // expression parser perform the completion.
        if (Tok.is(tok::code_completion) &&
            !(getLangOpts().ObjC && Intro.Default == LCD_None &&
              !Intro.Captures.empty())) {
          Actions.CodeCompleteLambdaIntroducer(getCurScope(), Intro,
                                               /*AfterAmpersand=*/false);
          cutOffParsing();
          break;
        }

        return DiagResult(diag::err_expected_comma_or_rsquare);
      }
      ConsumeToken();
    }

    if (Tok.is(tok::code_completion)) {
      // If we're in Objective-C++ and we have a bare '[', then this is more
      // likely to be a message receiver.
      if (getLangOpts().ObjC && first)
        Actions.CodeCompleteObjCMessageReceiver(getCurScope());
      else
        Actions.CodeCompleteLambdaIntroducer(getCurScope(), Intro,
                                             /*AfterAmpersand=*/false);
      cutOffParsing();
      break;
    }

    first = false;

    // Parse capture.
    LambdaCaptureKind Kind = LCK_ByCopy;
    LambdaCaptureInitKind InitKind = LambdaCaptureInitKind::NoInit;
    SourceLocation Loc;
    IdentifierInfo *Id = nullptr;
    SourceLocation EllipsisLoc;
    ExprResult Init;
    SourceLocation LocStart = Tok.getLocation();

    if (Tok.is(tok::star)) {
      Loc = ConsumeToken();
      if (Tok.is(tok::kw_this)) {
        ConsumeToken();
        Kind = LCK_StarThis;
      } else {
        return DiagResult(diag::err_expected_star_this_capture);
      }
    } else if (Tok.is(tok::kw_this)) {
      Kind = LCK_This;
      Loc = ConsumeToken();
    } else {
      if (Tok.is(tok::amp)) {
        Kind = LCK_ByRef;
        ConsumeToken();

        if (Tok.is(tok::code_completion)) {
          Actions.CodeCompleteLambdaIntroducer(getCurScope(), Intro,
                                               /*AfterAmpersand=*/true);
          cutOffParsing();
          break;
        }
      }

      if (Tok.is(tok::identifier)) {
        Id = Tok.getIdentifierInfo();
        Loc = ConsumeToken();
      } else if (Tok.is(tok::kw_this)) {
        // FIXME: If we want to suggest a fixit here, will need to return more
        // than just DiagnosticID. Perhaps full DiagnosticBuilder that can be
        // Clear()ed to prevent emission in case of tentative parsing?
        return DiagResult(diag::err_this_captured_by_reference);
      } else {
        return DiagResult(diag::err_expected_capture);
      }

      if (Tok.is(tok::l_paren)) {
        BalancedDelimiterTracker Parens(*this, tok::l_paren);
        Parens.consumeOpen();

        InitKind = LambdaCaptureInitKind::DirectInit;

        ExprVector Exprs;
        CommaLocsTy Commas;
        if (SkippedInits) {
          Parens.skipToEnd();
          *SkippedInits = true;
        } else if (ParseExpressionList(Exprs, Commas)) {
          Parens.skipToEnd();
          Init = ExprError();
        } else {
          Parens.consumeClose();
          Init = Actions.ActOnParenListExpr(Parens.getOpenLocation(),
                                            Parens.getCloseLocation(),
                                            Exprs);
        }
      } else if (Tok.isOneOf(tok::l_brace, tok::equal)) {
        // Each lambda init-capture forms its own full expression, which clears
        // Actions.MaybeODRUseExprs. So create an expression evaluation context
        // to save the necessary state, and restore it later.
        EnterExpressionEvaluationContext EC(
            Actions, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

        if (TryConsumeToken(tok::equal))
          InitKind = LambdaCaptureInitKind::CopyInit;
        else
          InitKind = LambdaCaptureInitKind::ListInit;

        if (!SkippedInits) {
          Init = ParseInitializer();
        } else if (Tok.is(tok::l_brace)) {
          BalancedDelimiterTracker Braces(*this, tok::l_brace);
          Braces.consumeOpen();
          Braces.skipToEnd();
          *SkippedInits = true;
        } else {
          // We're disambiguating this:
          //
          //   [..., x = expr
          //
          // We need to find the end of the following expression in order to
          // determine whether this is an Obj-C message send's receiver, a
          // C99 designator, or a lambda init-capture.
          //
          // Parse the expression to find where it ends, and annotate it back
          // onto the tokens. We would have parsed this expression the same way
          // in either case: both the RHS of an init-capture and the RHS of an
          // assignment expression are parsed as an initializer-clause, and in
          // neither case can anything be added to the scope between the '[' and
          // here.
          //
          // FIXME: This is horrible. Adding a mechanism to skip an expression
          // would be much cleaner.
          // FIXME: If there is a ',' before the next ']' or ':', we can skip to
          // that instead. (And if we see a ':' with no matching '?', we can
          // classify this as an Obj-C message send.)
          SourceLocation StartLoc = Tok.getLocation();
          InMessageExpressionRAIIObject MaybeInMessageExpression(*this, true);
          Init = ParseInitializer();
          if (!Init.isInvalid())
            Init = Actions.CorrectDelayedTyposInExpr(Init.get());

          if (Tok.getLocation() != StartLoc) {
            // Back out the lexing of the token after the initializer.
            PP.RevertCachedTokens(1);

            // Replace the consumed tokens with an appropriate annotation.
            Tok.setLocation(StartLoc);
            Tok.setKind(tok::annot_primary_expr);
            setExprAnnotation(Tok, Init);
            Tok.setAnnotationEndLoc(PP.getLastCachedTokenLocation());
            PP.AnnotateCachedTokens(Tok);

            // Consume the annotated initializer.
            ConsumeAnnotationToken();
          }
        }
      } else
        TryConsumeToken(tok::ellipsis, EllipsisLoc);
    }
    // If this is an init capture, process the initialization expression
    // right away.  For lambda init-captures such as the following:
    // const int x = 10;
    //  auto L = [i = x+1](int a) {
    //    return [j = x+2,
    //           &k = x](char b) { };
    //  };
    // keep in mind that each lambda init-capture has to have:
    //  - its initialization expression executed in the context
    //    of the enclosing/parent decl-context.
    //  - but the variable itself has to be 'injected' into the
    //    decl-context of its lambda's call-operator (which has
    //    not yet been created).
    // Each init-expression is a full-expression that has to get
    // Sema-analyzed (for capturing etc.) before its lambda's
    // call-operator's decl-context, scope & scopeinfo are pushed on their
    // respective stacks.  Thus if any variable is odr-used in the init-capture
    // it will correctly get captured in the enclosing lambda, if one exists.
    // The init-variables above are created later once the lambdascope and
    // call-operators decl-context is pushed onto its respective stack.

    // Since the lambda init-capture's initializer expression occurs in the
    // context of the enclosing function or lambda, therefore we can not wait
    // till a lambda scope has been pushed on before deciding whether the
    // variable needs to be captured.  We also need to process all
    // lvalue-to-rvalue conversions and discarded-value conversions,
    // so that we can avoid capturing certain constant variables.
    // For e.g.,
    //  void test() {
    //   const int x = 10;
    //   auto L = [&z = x](char a) { <-- don't capture by the current lambda
    //     return [y = x](int i) { <-- don't capture by enclosing lambda
    //          return y;
    //     }
    //   };
    // }
    // If x was not const, the second use would require 'L' to capture, and
    // that would be an error.

    ParsedType InitCaptureType;
    if (!Init.isInvalid())
      Init = Actions.CorrectDelayedTyposInExpr(Init.get());
    if (Init.isUsable()) {
      // Get the pointer and store it in an lvalue, so we can use it as an
      // out argument.
      Expr *InitExpr = Init.get();
      // This performs any lvalue-to-rvalue conversions if necessary, which
      // can affect what gets captured in the containing decl-context.
      InitCaptureType = Actions.actOnLambdaInitCaptureInitialization(
          Loc, Kind == LCK_ByRef, Id, InitKind, InitExpr);
      Init = InitExpr;
    }

    SourceLocation LocEnd = PrevTokLocation;

    Intro.addCapture(Kind, Loc, Id, EllipsisLoc, InitKind, Init,
                     InitCaptureType, SourceRange(LocStart, LocEnd));
  }

  T.consumeClose();
  Intro.Range.setEnd(T.getCloseLocation());
  return DiagResult();
}

/// TryParseLambdaIntroducer - Tentatively parse a lambda introducer.
///
/// Returns true if it hit something unexpected.
bool Parser::TryParseLambdaIntroducer(LambdaIntroducer &Intro) {
  {
    bool SkippedInits = false;
    TentativeParsingAction PA1(*this);

    if (ParseLambdaIntroducer(Intro, &SkippedInits)) {
      PA1.Revert();
      return true;
    }

    if (!SkippedInits) {
      PA1.Commit();
      return false;
    }

    PA1.Revert();
  }

  // Try to parse it again, but this time parse the init-captures too.
  Intro = LambdaIntroducer();
  TentativeParsingAction PA2(*this);

  if (!ParseLambdaIntroducer(Intro)) {
    PA2.Commit();
    return false;
  }

  PA2.Revert();
  return true;
}

static void
tryConsumeMutableOrConstexprToken(Parser &P, SourceLocation &MutableLoc,
                                  SourceLocation &ConstexprLoc,
                                  SourceLocation &DeclEndLoc) {
  assert(MutableLoc.isInvalid());
  assert(ConstexprLoc.isInvalid());
  // Consume constexpr-opt mutable-opt in any sequence, and set the DeclEndLoc
  // to the final of those locations. Emit an error if we have multiple
  // copies of those keywords and recover.

  while (true) {
    switch (P.getCurToken().getKind()) {
    case tok::kw_mutable: {
      if (MutableLoc.isValid()) {
        P.Diag(P.getCurToken().getLocation(),
               diag::err_lambda_decl_specifier_repeated)
            << 0 << FixItHint::CreateRemoval(P.getCurToken().getLocation());
      }
      MutableLoc = P.ConsumeToken();
      DeclEndLoc = MutableLoc;
      break /*switch*/;
    }
    case tok::kw_constexpr:
      if (ConstexprLoc.isValid()) {
        P.Diag(P.getCurToken().getLocation(),
               diag::err_lambda_decl_specifier_repeated)
            << 1 << FixItHint::CreateRemoval(P.getCurToken().getLocation());
      }
      ConstexprLoc = P.ConsumeToken();
      DeclEndLoc = ConstexprLoc;
      break /*switch*/;
    default:
      return;
    }
  }
}

static void
addConstexprToLambdaDeclSpecifier(Parser &P, SourceLocation ConstexprLoc,
                                  DeclSpec &DS) {
  if (ConstexprLoc.isValid()) {
    P.Diag(ConstexprLoc, !P.getLangOpts().CPlusPlus17
                             ? diag::ext_constexpr_on_lambda_cxx17
                             : diag::warn_cxx14_compat_constexpr_on_lambda);
    const char *PrevSpec = nullptr;
    unsigned DiagID = 0;
    DS.SetConstexprSpec(ConstexprLoc, PrevSpec, DiagID);
    assert(PrevSpec == nullptr && DiagID == 0 &&
           "Constexpr cannot have been set previously!");
  }
}

/// ParseLambdaExpressionAfterIntroducer - Parse the rest of a lambda
/// expression.
ExprResult Parser::ParseLambdaExpressionAfterIntroducer(
                     LambdaIntroducer &Intro) {
  SourceLocation LambdaBeginLoc = Intro.Range.getBegin();
  Diag(LambdaBeginLoc, diag::warn_cxx98_compat_lambda);

  PrettyStackTraceLoc CrashInfo(PP.getSourceManager(), LambdaBeginLoc,
                                "lambda expression parsing");



  // FIXME: Call into Actions to add any init-capture declarations to the
  // scope while parsing the lambda-declarator and compound-statement.

  // Parse lambda-declarator[opt].
  DeclSpec DS(AttrFactory);
  Declarator D(DS, DeclaratorContext::LambdaExprContext);
  TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);
  Actions.PushLambdaScope();

  ParsedAttributes Attr(AttrFactory);
  SourceLocation DeclLoc = Tok.getLocation();
  if (getLangOpts().CUDA) {
    // In CUDA code, GNU attributes are allowed to appear immediately after the
    // "[...]", even if there is no "(...)" before the lambda body.
    MaybeParseGNUAttributes(D);
  }

  // Helper to emit a warning if we see a CUDA host/device/global attribute
  // after '(...)'. nvcc doesn't accept this.
  auto WarnIfHasCUDATargetAttr = [&] {
    if (getLangOpts().CUDA)
      for (const ParsedAttr &A : Attr)
        if (A.getKind() == ParsedAttr::AT_CUDADevice ||
            A.getKind() == ParsedAttr::AT_CUDAHost ||
            A.getKind() == ParsedAttr::AT_CUDAGlobal)
          Diag(A.getLoc(), diag::warn_cuda_attr_lambda_position)
              << A.getName()->getName();
  };

  TypeResult TrailingReturnType;
  if (Tok.is(tok::l_paren)) {
    ParseScope PrototypeScope(this,
                              Scope::FunctionPrototypeScope |
                              Scope::FunctionDeclarationScope |
                              Scope::DeclScope);

    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    SourceLocation LParenLoc = T.getOpenLocation();

    // Parse parameter-declaration-clause.
    SmallVector<DeclaratorChunk::ParamInfo, 16> ParamInfo;
    SourceLocation EllipsisLoc;

    if (Tok.isNot(tok::r_paren)) {
      Actions.RecordParsingTemplateParameterDepth(TemplateParameterDepth);
      ParseParameterDeclarationClause(D, Attr, ParamInfo, EllipsisLoc);
      // For a generic lambda, each 'auto' within the parameter declaration
      // clause creates a template type parameter, so increment the depth.
      if (Actions.getCurGenericLambda())
        ++CurTemplateDepthTracker;
    }
    T.consumeClose();
    SourceLocation RParenLoc = T.getCloseLocation();
    SourceLocation DeclEndLoc = RParenLoc;

    // GNU-style attributes must be parsed before the mutable specifier to be
    // compatible with GCC.
    MaybeParseGNUAttributes(Attr, &DeclEndLoc);

    // MSVC-style attributes must be parsed before the mutable specifier to be
    // compatible with MSVC.
    MaybeParseMicrosoftDeclSpecs(Attr, &DeclEndLoc);

    // Parse mutable-opt and/or constexpr-opt, and update the DeclEndLoc.
    SourceLocation MutableLoc;
    SourceLocation ConstexprLoc;
    tryConsumeMutableOrConstexprToken(*this, MutableLoc, ConstexprLoc,
                                      DeclEndLoc);

    addConstexprToLambdaDeclSpecifier(*this, ConstexprLoc, DS);

    // Parse exception-specification[opt].
    ExceptionSpecificationType ESpecType = EST_None;
    SourceRange ESpecRange;
    SmallVector<ParsedType, 2> DynamicExceptions;
    SmallVector<SourceRange, 2> DynamicExceptionRanges;
    ExprResult NoexceptExpr;
    CachedTokens *ExceptionSpecTokens;
    ESpecType = tryParseExceptionSpecification(/*Delayed=*/false,
                                               ESpecRange,
                                               DynamicExceptions,
                                               DynamicExceptionRanges,
                                               NoexceptExpr,
                                               ExceptionSpecTokens);

    if (ESpecType != EST_None)
      DeclEndLoc = ESpecRange.getEnd();

    // Parse attribute-specifier[opt].
    MaybeParseCXX11Attributes(Attr, &DeclEndLoc);

    SourceLocation FunLocalRangeEnd = DeclEndLoc;

    // Parse trailing-return-type[opt].
    if (Tok.is(tok::arrow)) {
      FunLocalRangeEnd = Tok.getLocation();
      SourceRange Range;
      TrailingReturnType =
          ParseTrailingReturnType(Range, /*MayBeFollowedByDirectInit*/ false);
      if (Range.getEnd().isValid())
        DeclEndLoc = Range.getEnd();
    }

    PrototypeScope.Exit();

    WarnIfHasCUDATargetAttr();

    SourceLocation NoLoc;
    D.AddTypeInfo(DeclaratorChunk::getFunction(
                      /*hasProto=*/true,
                      /*isAmbiguous=*/false, LParenLoc, ParamInfo.data(),
                      ParamInfo.size(), EllipsisLoc, RParenLoc,
                      /*RefQualifierIsLValueRef=*/true,
                      /*RefQualifierLoc=*/NoLoc, MutableLoc, ESpecType,
                      ESpecRange, DynamicExceptions.data(),
                      DynamicExceptionRanges.data(), DynamicExceptions.size(),
                      NoexceptExpr.isUsable() ? NoexceptExpr.get() : nullptr,
                      /*ExceptionSpecTokens*/ nullptr,
                      /*DeclsInPrototype=*/None, LParenLoc, FunLocalRangeEnd, D,
                      TrailingReturnType),
                  std::move(Attr), DeclEndLoc);
  } else if (Tok.isOneOf(tok::kw_mutable, tok::arrow, tok::kw___attribute,
                         tok::kw_constexpr) ||
             (Tok.is(tok::l_square) && NextToken().is(tok::l_square))) {
    // It's common to forget that one needs '()' before 'mutable', an attribute
    // specifier, or the result type. Deal with this.
    unsigned TokKind = 0;
    switch (Tok.getKind()) {
    case tok::kw_mutable: TokKind = 0; break;
    case tok::arrow: TokKind = 1; break;
    case tok::kw___attribute:
    case tok::l_square: TokKind = 2; break;
    case tok::kw_constexpr: TokKind = 3; break;
    default: llvm_unreachable("Unknown token kind");
    }

    Diag(Tok, diag::err_lambda_missing_parens)
      << TokKind
      << FixItHint::CreateInsertion(Tok.getLocation(), "() ");
    SourceLocation DeclEndLoc = DeclLoc;

    // GNU-style attributes must be parsed before the mutable specifier to be
    // compatible with GCC.
    MaybeParseGNUAttributes(Attr, &DeclEndLoc);

    // Parse 'mutable', if it's there.
    SourceLocation MutableLoc;
    if (Tok.is(tok::kw_mutable)) {
      MutableLoc = ConsumeToken();
      DeclEndLoc = MutableLoc;
    }

    // Parse attribute-specifier[opt].
    MaybeParseCXX11Attributes(Attr, &DeclEndLoc);

    // Parse the return type, if there is one.
    if (Tok.is(tok::arrow)) {
      SourceRange Range;
      TrailingReturnType =
          ParseTrailingReturnType(Range, /*MayBeFollowedByDirectInit*/ false);
      if (Range.getEnd().isValid())
        DeclEndLoc = Range.getEnd();
    }

    WarnIfHasCUDATargetAttr();

    SourceLocation NoLoc;
    D.AddTypeInfo(DeclaratorChunk::getFunction(
                      /*hasProto=*/true,
                      /*isAmbiguous=*/false,
                      /*LParenLoc=*/NoLoc,
                      /*Params=*/nullptr,
                      /*NumParams=*/0,
                      /*EllipsisLoc=*/NoLoc,
                      /*RParenLoc=*/NoLoc,
                      /*RefQualifierIsLValueRef=*/true,
                      /*RefQualifierLoc=*/NoLoc, MutableLoc, EST_None,
                      /*ESpecRange=*/SourceRange(),
                      /*Exceptions=*/nullptr,
                      /*ExceptionRanges=*/nullptr,
                      /*NumExceptions=*/0,
                      /*NoexceptExpr=*/nullptr,
                      /*ExceptionSpecTokens=*/nullptr,
                      /*DeclsInPrototype=*/None, DeclLoc, DeclEndLoc, D,
                      TrailingReturnType),
                  std::move(Attr), DeclEndLoc);
  }

  // FIXME: Rename BlockScope -> ClosureScope if we decide to continue using
  // it.
  unsigned ScopeFlags = Scope::BlockScope | Scope::FnScope | Scope::DeclScope |
                        Scope::CompoundStmtScope;
  ParseScope BodyScope(this, ScopeFlags);

  Actions.ActOnStartOfLambdaDefinition(Intro, D, getCurScope());

  // Parse compound-statement.
  if (!Tok.is(tok::l_brace)) {
    Diag(Tok, diag::err_expected_lambda_body);
    Actions.ActOnLambdaError(LambdaBeginLoc, getCurScope());
    return ExprError();
  }

  StmtResult Stmt(ParseCompoundStatementBody());
  BodyScope.Exit();

  if (!Stmt.isInvalid() && !TrailingReturnType.isInvalid())
    return Actions.ActOnLambdaExpr(LambdaBeginLoc, Stmt.get(), getCurScope());

  Actions.ActOnLambdaError(LambdaBeginLoc, getCurScope());
  return ExprError();
}

/// ParseCXXCasts - This handles the various ways to cast expressions to another
/// type.
///
///       postfix-expression: [C++ 5.2p1]
///         'dynamic_cast' '<' type-name '>' '(' expression ')'
///         'static_cast' '<' type-name '>' '(' expression ')'
///         'reinterpret_cast' '<' type-name '>' '(' expression ')'
///         'const_cast' '<' type-name '>' '(' expression ')'
///
ExprResult Parser::ParseCXXCasts() {
  tok::TokenKind Kind = Tok.getKind();
  const char *CastName = nullptr; // For error messages

  switch (Kind) {
  default: llvm_unreachable("Unknown C++ cast!");
  case tok::kw_const_cast:       CastName = "const_cast";       break;
  case tok::kw_dynamic_cast:     CastName = "dynamic_cast";     break;
  case tok::kw_reinterpret_cast: CastName = "reinterpret_cast"; break;
  case tok::kw_static_cast:      CastName = "static_cast";      break;
  }

  SourceLocation OpLoc = ConsumeToken();
  SourceLocation LAngleBracketLoc = Tok.getLocation();

  // Check for "<::" which is parsed as "[:".  If found, fix token stream,
  // diagnose error, suggest fix, and recover parsing.
  if (Tok.is(tok::l_square) && Tok.getLength() == 2) {
    Token Next = NextToken();
    if (Next.is(tok::colon) && areTokensAdjacent(Tok, Next))
      FixDigraph(*this, PP, Tok, Next, Kind, /*AtDigraph*/true);
  }

  if (ExpectAndConsume(tok::less, diag::err_expected_less_after, CastName))
    return ExprError();

  // Parse the common declaration-specifiers piece.
  DeclSpec DS(AttrFactory);
  ParseSpecifierQualifierList(DS);

  // Parse the abstract-declarator, if present.
  Declarator DeclaratorInfo(DS, DeclaratorContext::TypeNameContext);
  ParseDeclarator(DeclaratorInfo);

  SourceLocation RAngleBracketLoc = Tok.getLocation();

  if (ExpectAndConsume(tok::greater))
    return ExprError(Diag(LAngleBracketLoc, diag::note_matching) << tok::less);

  BalancedDelimiterTracker T(*this, tok::l_paren);

  if (T.expectAndConsume(diag::err_expected_lparen_after, CastName))
    return ExprError();

  ExprResult Result = ParseExpression();

  // Match the ')'.
  T.consumeClose();

  if (!Result.isInvalid() && !DeclaratorInfo.isInvalidType())
    Result = Actions.ActOnCXXNamedCast(OpLoc, Kind,
                                       LAngleBracketLoc, DeclaratorInfo,
                                       RAngleBracketLoc,
                                       T.getOpenLocation(), Result.get(),
                                       T.getCloseLocation());

  return Result;
}

/// ParseCXXTypeid - This handles the C++ typeid expression.
///
///       postfix-expression: [C++ 5.2p1]
///         'typeid' '(' expression ')'
///         'typeid' '(' type-id ')'
///
ExprResult Parser::ParseCXXTypeid() {
  assert(Tok.is(tok::kw_typeid) && "Not 'typeid'!");

  SourceLocation OpLoc = ConsumeToken();
  SourceLocation LParenLoc, RParenLoc;
  BalancedDelimiterTracker T(*this, tok::l_paren);

  // typeid expressions are always parenthesized.
  if (T.expectAndConsume(diag::err_expected_lparen_after, "typeid"))
    return ExprError();
  LParenLoc = T.getOpenLocation();

  ExprResult Result;

  // C++0x [expr.typeid]p3:
  //   When typeid is applied to an expression other than an lvalue of a
  //   polymorphic class type [...] The expression is an unevaluated
  //   operand (Clause 5).
  //
  // Note that we can't tell whether the expression is an lvalue of a
  // polymorphic class type until after we've parsed the expression; we
  // speculatively assume the subexpression is unevaluated, and fix it up
  // later.
  //
  // We enter the unevaluated context before trying to determine whether we
  // have a type-id, because the tentative parse logic will try to resolve
  // names, and must treat them as unevaluated.
  EnterExpressionEvaluationContext Unevaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated,
      Sema::ReuseLambdaContextDecl);

  if (isTypeIdInParens()) {
    TypeResult Ty = ParseTypeName();

    // Match the ')'.
    T.consumeClose();
    RParenLoc = T.getCloseLocation();
    if (Ty.isInvalid() || RParenLoc.isInvalid())
      return ExprError();

    Result = Actions.ActOnCXXTypeid(OpLoc, LParenLoc, /*isType=*/true,
                                    Ty.get().getAsOpaquePtr(), RParenLoc);
  } else {
    Result = ParseExpression();

    // Match the ')'.
    if (Result.isInvalid())
      SkipUntil(tok::r_paren, StopAtSemi);
    else {
      T.consumeClose();
      RParenLoc = T.getCloseLocation();
      if (RParenLoc.isInvalid())
        return ExprError();

      Result = Actions.ActOnCXXTypeid(OpLoc, LParenLoc, /*isType=*/false,
                                      Result.get(), RParenLoc);
    }
  }

  return Result;
}

/// ParseCXXUuidof - This handles the Microsoft C++ __uuidof expression.
///
///         '__uuidof' '(' expression ')'
///         '__uuidof' '(' type-id ')'
///
ExprResult Parser::ParseCXXUuidof() {
  assert(Tok.is(tok::kw___uuidof) && "Not '__uuidof'!");

  SourceLocation OpLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);

  // __uuidof expressions are always parenthesized.
  if (T.expectAndConsume(diag::err_expected_lparen_after, "__uuidof"))
    return ExprError();

  ExprResult Result;

  if (isTypeIdInParens()) {
    TypeResult Ty = ParseTypeName();

    // Match the ')'.
    T.consumeClose();

    if (Ty.isInvalid())
      return ExprError();

    Result = Actions.ActOnCXXUuidof(OpLoc, T.getOpenLocation(), /*isType=*/true,
                                    Ty.get().getAsOpaquePtr(),
                                    T.getCloseLocation());
  } else {
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    Result = ParseExpression();

    // Match the ')'.
    if (Result.isInvalid())
      SkipUntil(tok::r_paren, StopAtSemi);
    else {
      T.consumeClose();

      Result = Actions.ActOnCXXUuidof(OpLoc, T.getOpenLocation(),
                                      /*isType=*/false,
                                      Result.get(), T.getCloseLocation());
    }
  }

  return Result;
}

/// Parse a C++ pseudo-destructor expression after the base,
/// . or -> operator, and nested-name-specifier have already been
/// parsed.
///
///       postfix-expression: [C++ 5.2]
///         postfix-expression . pseudo-destructor-name
///         postfix-expression -> pseudo-destructor-name
///
///       pseudo-destructor-name:
///         ::[opt] nested-name-specifier[opt] type-name :: ~type-name
///         ::[opt] nested-name-specifier template simple-template-id ::
///                 ~type-name
///         ::[opt] nested-name-specifier[opt] ~type-name
///
ExprResult
Parser::ParseCXXPseudoDestructor(Expr *Base, SourceLocation OpLoc,
                                 tok::TokenKind OpKind,
                                 CXXScopeSpec &SS,
                                 ParsedType ObjectType) {
  // We're parsing either a pseudo-destructor-name or a dependent
  // member access that has the same form as a
  // pseudo-destructor-name. We parse both in the same way and let
  // the action model sort them out.
  //
  // Note that the ::[opt] nested-name-specifier[opt] has already
  // been parsed, and if there was a simple-template-id, it has
  // been coalesced into a template-id annotation token.
  UnqualifiedId FirstTypeName;
  SourceLocation CCLoc;
  if (Tok.is(tok::identifier)) {
    FirstTypeName.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
    ConsumeToken();
    assert(Tok.is(tok::coloncolon) &&"ParseOptionalCXXScopeSpecifier fail");
    CCLoc = ConsumeToken();
  } else if (Tok.is(tok::annot_template_id)) {
    // FIXME: retrieve TemplateKWLoc from template-id annotation and
    // store it in the pseudo-dtor node (to be used when instantiating it).
    FirstTypeName.setTemplateId(
                              (TemplateIdAnnotation *)Tok.getAnnotationValue());
    ConsumeAnnotationToken();
    assert(Tok.is(tok::coloncolon) &&"ParseOptionalCXXScopeSpecifier fail");
    CCLoc = ConsumeToken();
  } else {
    FirstTypeName.setIdentifier(nullptr, SourceLocation());
  }

  // Parse the tilde.
  assert(Tok.is(tok::tilde) && "ParseOptionalCXXScopeSpecifier fail");
  SourceLocation TildeLoc = ConsumeToken();

  if (Tok.is(tok::kw_decltype) && !FirstTypeName.isValid() && SS.isEmpty()) {
    DeclSpec DS(AttrFactory);
    ParseDecltypeSpecifier(DS);
    if (DS.getTypeSpecType() == TST_error)
      return ExprError();
    return Actions.ActOnPseudoDestructorExpr(getCurScope(), Base, OpLoc, OpKind,
                                             TildeLoc, DS);
  }

  if (!Tok.is(tok::identifier)) {
    Diag(Tok, diag::err_destructor_tilde_identifier);
    return ExprError();
  }

  // Parse the second type.
  UnqualifiedId SecondTypeName;
  IdentifierInfo *Name = Tok.getIdentifierInfo();
  SourceLocation NameLoc = ConsumeToken();
  SecondTypeName.setIdentifier(Name, NameLoc);

  // If there is a '<', the second type name is a template-id. Parse
  // it as such.
  if (Tok.is(tok::less) &&
      ParseUnqualifiedIdTemplateId(SS, SourceLocation(),
                                   Name, NameLoc,
                                   false, ObjectType, SecondTypeName,
                                   /*AssumeTemplateName=*/true))
    return ExprError();

  return Actions.ActOnPseudoDestructorExpr(getCurScope(), Base, OpLoc, OpKind,
                                           SS, FirstTypeName, CCLoc, TildeLoc,
                                           SecondTypeName);
}

/// ParseCXXBoolLiteral - This handles the C++ Boolean literals.
///
///       boolean-literal: [C++ 2.13.5]
///         'true'
///         'false'
ExprResult Parser::ParseCXXBoolLiteral() {
  tok::TokenKind Kind = Tok.getKind();
  return Actions.ActOnCXXBoolLiteral(ConsumeToken(), Kind);
}

/// ParseThrowExpression - This handles the C++ throw expression.
///
///       throw-expression: [C++ 15]
///         'throw' assignment-expression[opt]
ExprResult Parser::ParseThrowExpression() {
  assert(Tok.is(tok::kw_throw) && "Not throw!");
  SourceLocation ThrowLoc = ConsumeToken();           // Eat the throw token.

  // If the current token isn't the start of an assignment-expression,
  // then the expression is not present.  This handles things like:
  //   "C ? throw : (void)42", which is crazy but legal.
  switch (Tok.getKind()) {  // FIXME: move this predicate somewhere common.
  case tok::semi:
  case tok::r_paren:
  case tok::r_square:
  case tok::r_brace:
  case tok::colon:
  case tok::comma:
    return Actions.ActOnCXXThrow(getCurScope(), ThrowLoc, nullptr);

  default:
    ExprResult Expr(ParseAssignmentExpression());
    if (Expr.isInvalid()) return Expr;
    return Actions.ActOnCXXThrow(getCurScope(), ThrowLoc, Expr.get());
  }
}

/// Parse the C++ Coroutines co_yield expression.
///
///       co_yield-expression:
///         'co_yield' assignment-expression[opt]
ExprResult Parser::ParseCoyieldExpression() {
  assert(Tok.is(tok::kw_co_yield) && "Not co_yield!");

  SourceLocation Loc = ConsumeToken();
  ExprResult Expr = Tok.is(tok::l_brace) ? ParseBraceInitializer()
                                         : ParseAssignmentExpression();
  if (!Expr.isInvalid())
    Expr = Actions.ActOnCoyieldExpr(getCurScope(), Loc, Expr.get());
  return Expr;
}

/// ParseCXXThis - This handles the C++ 'this' pointer.
///
/// C++ 9.3.2: In the body of a non-static member function, the keyword this is
/// a non-lvalue expression whose value is the address of the object for which
/// the function is called.
ExprResult Parser::ParseCXXThis() {
  assert(Tok.is(tok::kw_this) && "Not 'this'!");
  SourceLocation ThisLoc = ConsumeToken();
  return Actions.ActOnCXXThis(ThisLoc);
}

/// ParseCXXTypeConstructExpression - Parse construction of a specified type.
/// Can be interpreted either as function-style casting ("int(x)")
/// or class type construction ("ClassType(x,y,z)")
/// or creation of a value-initialized type ("int()").
/// See [C++ 5.2.3].
///
///       postfix-expression: [C++ 5.2p1]
///         simple-type-specifier '(' expression-list[opt] ')'
/// [C++0x] simple-type-specifier braced-init-list
///         typename-specifier '(' expression-list[opt] ')'
/// [C++0x] typename-specifier braced-init-list
///
/// In C++1z onwards, the type specifier can also be a template-name.
ExprResult
Parser::ParseCXXTypeConstructExpression(const DeclSpec &DS) {
  Declarator DeclaratorInfo(DS, DeclaratorContext::FunctionalCastContext);
  ParsedType TypeRep = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo).get();

  assert((Tok.is(tok::l_paren) ||
          (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)))
         && "Expected '(' or '{'!");

  if (Tok.is(tok::l_brace)) {
    ExprResult Init = ParseBraceInitializer();
    if (Init.isInvalid())
      return Init;
    Expr *InitList = Init.get();
    return Actions.ActOnCXXTypeConstructExpr(
        TypeRep, InitList->getBeginLoc(), MultiExprArg(&InitList, 1),
        InitList->getEndLoc(), /*ListInitialization=*/true);
  } else {
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();

    ExprVector Exprs;
    CommaLocsTy CommaLocs;

    if (Tok.isNot(tok::r_paren)) {
      if (ParseExpressionList(Exprs, CommaLocs, [&] {
            QualType PreferredType = Actions.ProduceConstructorSignatureHelp(
                getCurScope(), TypeRep.get()->getCanonicalTypeInternal(),
                DS.getEndLoc(), Exprs, T.getOpenLocation());
            CalledSignatureHelp = true;
            Actions.CodeCompleteExpression(getCurScope(), PreferredType);
          })) {
        if (PP.isCodeCompletionReached() && !CalledSignatureHelp) {
          Actions.ProduceConstructorSignatureHelp(
              getCurScope(), TypeRep.get()->getCanonicalTypeInternal(),
              DS.getEndLoc(), Exprs, T.getOpenLocation());
          CalledSignatureHelp = true;
        }
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
    }

    // Match the ')'.
    T.consumeClose();

    // TypeRep could be null, if it references an invalid typedef.
    if (!TypeRep)
      return ExprError();

    assert((Exprs.size() == 0 || Exprs.size()-1 == CommaLocs.size())&&
           "Unexpected number of commas!");
    return Actions.ActOnCXXTypeConstructExpr(TypeRep, T.getOpenLocation(),
                                             Exprs, T.getCloseLocation(),
                                             /*ListInitialization=*/false);
  }
}

/// ParseCXXCondition - if/switch/while condition expression.
///
///       condition:
///         expression
///         type-specifier-seq declarator '=' assignment-expression
/// [C++11] type-specifier-seq declarator '=' initializer-clause
/// [C++11] type-specifier-seq declarator braced-init-list
/// [Clang] type-specifier-seq ref-qualifier[opt] '[' identifier-list ']'
///             brace-or-equal-initializer
/// [GNU]   type-specifier-seq declarator simple-asm-expr[opt] attributes[opt]
///             '=' assignment-expression
///
/// In C++1z, a condition may in some contexts be preceded by an
/// optional init-statement. This function will parse that too.
///
/// \param InitStmt If non-null, an init-statement is permitted, and if present
/// will be parsed and stored here.
///
/// \param Loc The location of the start of the statement that requires this
/// condition, e.g., the "for" in a for loop.
///
/// \param FRI If non-null, a for range declaration is permitted, and if
/// present will be parsed and stored here, and a null result will be returned.
///
/// \returns The parsed condition.
Sema::ConditionResult Parser::ParseCXXCondition(StmtResult *InitStmt,
                                                SourceLocation Loc,
                                                Sema::ConditionKind CK,
                                                ForRangeInfo *FRI) {
  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteOrdinaryName(getCurScope(), Sema::PCC_Condition);
    cutOffParsing();
    return Sema::ConditionError();
  }

  ParsedAttributesWithRange attrs(AttrFactory);
  MaybeParseCXX11Attributes(attrs);

  const auto WarnOnInit = [this, &CK] {
    Diag(Tok.getLocation(), getLangOpts().CPlusPlus17
                                ? diag::warn_cxx14_compat_init_statement
                                : diag::ext_init_statement)
        << (CK == Sema::ConditionKind::Switch);
  };

  // Determine what kind of thing we have.
  switch (isCXXConditionDeclarationOrInitStatement(InitStmt, FRI)) {
  case ConditionOrInitStatement::Expression: {
    ProhibitAttributes(attrs);

    // We can have an empty expression here.
    //   if (; true);
    if (InitStmt && Tok.is(tok::semi)) {
      WarnOnInit();
      SourceLocation SemiLoc = Tok.getLocation();
      if (!Tok.hasLeadingEmptyMacro() && !SemiLoc.isMacroID()) {
        Diag(SemiLoc, diag::warn_empty_init_statement)
            << (CK == Sema::ConditionKind::Switch)
            << FixItHint::CreateRemoval(SemiLoc);
      }
      ConsumeToken();
      *InitStmt = Actions.ActOnNullStmt(SemiLoc);
      return ParseCXXCondition(nullptr, Loc, CK);
    }

    // Parse the expression.
    ExprResult Expr = ParseExpression(); // expression
    if (Expr.isInvalid())
      return Sema::ConditionError();

    if (InitStmt && Tok.is(tok::semi)) {
      WarnOnInit();
      *InitStmt = Actions.ActOnExprStmt(Expr.get());
      ConsumeToken();
      return ParseCXXCondition(nullptr, Loc, CK);
    }

    return Actions.ActOnCondition(getCurScope(), Loc, Expr.get(), CK);
  }

  case ConditionOrInitStatement::InitStmtDecl: {
    WarnOnInit();
    SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
    DeclGroupPtrTy DG =
        ParseSimpleDeclaration(DeclaratorContext::InitStmtContext, DeclEnd,
                               attrs, /*RequireSemi=*/true);
    *InitStmt = Actions.ActOnDeclStmt(DG, DeclStart, DeclEnd);
    return ParseCXXCondition(nullptr, Loc, CK);
  }

  case ConditionOrInitStatement::ForRangeDecl: {
    assert(FRI && "should not parse a for range declaration here");
    SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
    DeclGroupPtrTy DG = ParseSimpleDeclaration(
        DeclaratorContext::ForContext, DeclEnd, attrs, false, FRI);
    FRI->LoopVar = Actions.ActOnDeclStmt(DG, DeclStart, Tok.getLocation());
    return Sema::ConditionResult();
  }

  case ConditionOrInitStatement::ConditionDecl:
  case ConditionOrInitStatement::Error:
    break;
  }

  // type-specifier-seq
  DeclSpec DS(AttrFactory);
  DS.takeAttributesFrom(attrs);
  ParseSpecifierQualifierList(DS, AS_none, DeclSpecContext::DSC_condition);

  // declarator
  Declarator DeclaratorInfo(DS, DeclaratorContext::ConditionContext);
  ParseDeclarator(DeclaratorInfo);

  // simple-asm-expr[opt]
  if (Tok.is(tok::kw_asm)) {
    SourceLocation Loc;
    ExprResult AsmLabel(ParseSimpleAsm(&Loc));
    if (AsmLabel.isInvalid()) {
      SkipUntil(tok::semi, StopAtSemi);
      return Sema::ConditionError();
    }
    DeclaratorInfo.setAsmLabel(AsmLabel.get());
    DeclaratorInfo.SetRangeEnd(Loc);
  }

  // If attributes are present, parse them.
  MaybeParseGNUAttributes(DeclaratorInfo);

  // Type-check the declaration itself.
  DeclResult Dcl = Actions.ActOnCXXConditionDeclaration(getCurScope(),
                                                        DeclaratorInfo);
  if (Dcl.isInvalid())
    return Sema::ConditionError();
  Decl *DeclOut = Dcl.get();

  // '=' assignment-expression
  // If a '==' or '+=' is found, suggest a fixit to '='.
  bool CopyInitialization = isTokenEqualOrEqualTypo();
  if (CopyInitialization)
    ConsumeToken();

  ExprResult InitExpr = ExprError();
  if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
    Diag(Tok.getLocation(),
         diag::warn_cxx98_compat_generalized_initializer_lists);
    InitExpr = ParseBraceInitializer();
  } else if (CopyInitialization) {
    InitExpr = ParseAssignmentExpression();
  } else if (Tok.is(tok::l_paren)) {
    // This was probably an attempt to initialize the variable.
    SourceLocation LParen = ConsumeParen(), RParen = LParen;
    if (SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch))
      RParen = ConsumeParen();
    Diag(DeclOut->getLocation(),
         diag::err_expected_init_in_condition_lparen)
      << SourceRange(LParen, RParen);
  } else {
    Diag(DeclOut->getLocation(), diag::err_expected_init_in_condition);
  }

  if (!InitExpr.isInvalid())
    Actions.AddInitializerToDecl(DeclOut, InitExpr.get(), !CopyInitialization);
  else
    Actions.ActOnInitializerError(DeclOut);

  Actions.FinalizeDeclaration(DeclOut);
  return Actions.ActOnConditionVariable(DeclOut, Loc, CK);
}

/// ParseCXXSimpleTypeSpecifier - [C++ 7.1.5.2] Simple type specifiers.
/// This should only be called when the current token is known to be part of
/// simple-type-specifier.
///
///       simple-type-specifier:
///         '::'[opt] nested-name-specifier[opt] type-name
///         '::'[opt] nested-name-specifier 'template' simple-template-id [TODO]
///         char
///         wchar_t
///         bool
///         short
///         int
///         long
///         signed
///         unsigned
///         float
///         double
///         void
/// [GNU]   typeof-specifier
/// [C++0x] auto               [TODO]
///
///       type-name:
///         class-name
///         enum-name
///         typedef-name
///
void Parser::ParseCXXSimpleTypeSpecifier(DeclSpec &DS) {
  DS.SetRangeStart(Tok.getLocation());
  const char *PrevSpec;
  unsigned DiagID;
  SourceLocation Loc = Tok.getLocation();
  const clang::PrintingPolicy &Policy =
      Actions.getASTContext().getPrintingPolicy();

  switch (Tok.getKind()) {
  case tok::identifier:   // foo::bar
  case tok::coloncolon:   // ::foo::bar
    llvm_unreachable("Annotation token should already be formed!");
  default:
    llvm_unreachable("Not a simple-type-specifier token!");

  // type-name
  case tok::annot_typename: {
    if (getTypeAnnotation(Tok))
      DS.SetTypeSpecType(DeclSpec::TST_typename, Loc, PrevSpec, DiagID,
                         getTypeAnnotation(Tok), Policy);
    else
      DS.SetTypeSpecError();

    DS.SetRangeEnd(Tok.getAnnotationEndLoc());
    ConsumeAnnotationToken();

    DS.Finish(Actions, Policy);
    return;
  }

  // builtin types
  case tok::kw_short:
    DS.SetTypeSpecWidth(DeclSpec::TSW_short, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_long:
    DS.SetTypeSpecWidth(DeclSpec::TSW_long, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw___int64:
    DS.SetTypeSpecWidth(DeclSpec::TSW_longlong, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_signed:
    DS.SetTypeSpecSign(DeclSpec::TSS_signed, Loc, PrevSpec, DiagID);
    break;
  case tok::kw_unsigned:
    DS.SetTypeSpecSign(DeclSpec::TSS_unsigned, Loc, PrevSpec, DiagID);
    break;
  case tok::kw_void:
    DS.SetTypeSpecType(DeclSpec::TST_void, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_char:
    DS.SetTypeSpecType(DeclSpec::TST_char, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_int:
    DS.SetTypeSpecType(DeclSpec::TST_int, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw___int128:
    DS.SetTypeSpecType(DeclSpec::TST_int128, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_half:
    DS.SetTypeSpecType(DeclSpec::TST_half, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_float:
    DS.SetTypeSpecType(DeclSpec::TST_float, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_double:
    DS.SetTypeSpecType(DeclSpec::TST_double, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw__Float16:
    DS.SetTypeSpecType(DeclSpec::TST_float16, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw___float128:
    DS.SetTypeSpecType(DeclSpec::TST_float128, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_wchar_t:
    DS.SetTypeSpecType(DeclSpec::TST_wchar, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_char8_t:
    DS.SetTypeSpecType(DeclSpec::TST_char8, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_char16_t:
    DS.SetTypeSpecType(DeclSpec::TST_char16, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_char32_t:
    DS.SetTypeSpecType(DeclSpec::TST_char32, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::kw_bool:
    DS.SetTypeSpecType(DeclSpec::TST_bool, Loc, PrevSpec, DiagID, Policy);
    break;
  case tok::annot_decltype:
  case tok::kw_decltype:
    DS.SetRangeEnd(ParseDecltypeSpecifier(DS));
    return DS.Finish(Actions, Policy);

  // GNU typeof support.
  case tok::kw_typeof:
    ParseTypeofSpecifier(DS);
    DS.Finish(Actions, Policy);
    return;
  }
  ConsumeAnyToken();
  DS.SetRangeEnd(PrevTokLocation);
  DS.Finish(Actions, Policy);
}

/// ParseCXXTypeSpecifierSeq - Parse a C++ type-specifier-seq (C++
/// [dcl.name]), which is a non-empty sequence of type-specifiers,
/// e.g., "const short int". Note that the DeclSpec is *not* finished
/// by parsing the type-specifier-seq, because these sequences are
/// typically followed by some form of declarator. Returns true and
/// emits diagnostics if this is not a type-specifier-seq, false
/// otherwise.
///
///   type-specifier-seq: [C++ 8.1]
///     type-specifier type-specifier-seq[opt]
///
bool Parser::ParseCXXTypeSpecifierSeq(DeclSpec &DS) {
  ParseSpecifierQualifierList(DS, AS_none, DeclSpecContext::DSC_type_specifier);
  DS.Finish(Actions, Actions.getASTContext().getPrintingPolicy());
  return false;
}

/// Finish parsing a C++ unqualified-id that is a template-id of
/// some form.
///
/// This routine is invoked when a '<' is encountered after an identifier or
/// operator-function-id is parsed by \c ParseUnqualifiedId() to determine
/// whether the unqualified-id is actually a template-id. This routine will
/// then parse the template arguments and form the appropriate template-id to
/// return to the caller.
///
/// \param SS the nested-name-specifier that precedes this template-id, if
/// we're actually parsing a qualified-id.
///
/// \param Name for constructor and destructor names, this is the actual
/// identifier that may be a template-name.
///
/// \param NameLoc the location of the class-name in a constructor or
/// destructor.
///
/// \param EnteringContext whether we're entering the scope of the
/// nested-name-specifier.
///
/// \param ObjectType if this unqualified-id occurs within a member access
/// expression, the type of the base object whose member is being accessed.
///
/// \param Id as input, describes the template-name or operator-function-id
/// that precedes the '<'. If template arguments were parsed successfully,
/// will be updated with the template-id.
///
/// \param AssumeTemplateId When true, this routine will assume that the name
/// refers to a template without performing name lookup to verify.
///
/// \returns true if a parse error occurred, false otherwise.
bool Parser::ParseUnqualifiedIdTemplateId(CXXScopeSpec &SS,
                                          SourceLocation TemplateKWLoc,
                                          IdentifierInfo *Name,
                                          SourceLocation NameLoc,
                                          bool EnteringContext,
                                          ParsedType ObjectType,
                                          UnqualifiedId &Id,
                                          bool AssumeTemplateId) {
  assert(Tok.is(tok::less) && "Expected '<' to finish parsing a template-id");

  TemplateTy Template;
  TemplateNameKind TNK = TNK_Non_template;
  switch (Id.getKind()) {
  case UnqualifiedIdKind::IK_Identifier:
  case UnqualifiedIdKind::IK_OperatorFunctionId:
  case UnqualifiedIdKind::IK_LiteralOperatorId:
    if (AssumeTemplateId) {
      // We defer the injected-class-name checks until we've found whether
      // this template-id is used to form a nested-name-specifier or not.
      TNK = Actions.ActOnDependentTemplateName(
          getCurScope(), SS, TemplateKWLoc, Id, ObjectType, EnteringContext,
          Template, /*AllowInjectedClassName*/ true);
      if (TNK == TNK_Non_template)
        return true;
    } else {
      bool MemberOfUnknownSpecialization;
      TNK = Actions.isTemplateName(getCurScope(), SS,
                                   TemplateKWLoc.isValid(), Id,
                                   ObjectType, EnteringContext, Template,
                                   MemberOfUnknownSpecialization);

      if (TNK == TNK_Non_template && MemberOfUnknownSpecialization &&
          ObjectType && IsTemplateArgumentList()) {
        // We have something like t->getAs<T>(), where getAs is a
        // member of an unknown specialization. However, this will only
        // parse correctly as a template, so suggest the keyword 'template'
        // before 'getAs' and treat this as a dependent template name.
        std::string Name;
        if (Id.getKind() == UnqualifiedIdKind::IK_Identifier)
          Name = Id.Identifier->getName();
        else {
          Name = "operator ";
          if (Id.getKind() == UnqualifiedIdKind::IK_OperatorFunctionId)
            Name += getOperatorSpelling(Id.OperatorFunctionId.Operator);
          else
            Name += Id.Identifier->getName();
        }
        Diag(Id.StartLocation, diag::err_missing_dependent_template_keyword)
          << Name
          << FixItHint::CreateInsertion(Id.StartLocation, "template ");
        TNK = Actions.ActOnDependentTemplateName(
            getCurScope(), SS, TemplateKWLoc, Id, ObjectType, EnteringContext,
            Template, /*AllowInjectedClassName*/ true);
        if (TNK == TNK_Non_template)
          return true;
      }
    }
    break;

  case UnqualifiedIdKind::IK_ConstructorName: {
    UnqualifiedId TemplateName;
    bool MemberOfUnknownSpecialization;
    TemplateName.setIdentifier(Name, NameLoc);
    TNK = Actions.isTemplateName(getCurScope(), SS, TemplateKWLoc.isValid(),
                                 TemplateName, ObjectType,
                                 EnteringContext, Template,
                                 MemberOfUnknownSpecialization);
    break;
  }

  case UnqualifiedIdKind::IK_DestructorName: {
    UnqualifiedId TemplateName;
    bool MemberOfUnknownSpecialization;
    TemplateName.setIdentifier(Name, NameLoc);
    if (ObjectType) {
      TNK = Actions.ActOnDependentTemplateName(
          getCurScope(), SS, TemplateKWLoc, TemplateName, ObjectType,
          EnteringContext, Template, /*AllowInjectedClassName*/ true);
      if (TNK == TNK_Non_template)
        return true;
    } else {
      TNK = Actions.isTemplateName(getCurScope(), SS, TemplateKWLoc.isValid(),
                                   TemplateName, ObjectType,
                                   EnteringContext, Template,
                                   MemberOfUnknownSpecialization);

      if (TNK == TNK_Non_template && !Id.DestructorName.get()) {
        Diag(NameLoc, diag::err_destructor_template_id)
          << Name << SS.getRange();
        return true;
      }
    }
    break;
  }

  default:
    return false;
  }

  if (TNK == TNK_Non_template)
    return false;

  // Parse the enclosed template argument list.
  SourceLocation LAngleLoc, RAngleLoc;
  TemplateArgList TemplateArgs;
  if (ParseTemplateIdAfterTemplateName(true, LAngleLoc, TemplateArgs,
                                       RAngleLoc))
    return true;

  if (Id.getKind() == UnqualifiedIdKind::IK_Identifier ||
      Id.getKind() == UnqualifiedIdKind::IK_OperatorFunctionId ||
      Id.getKind() == UnqualifiedIdKind::IK_LiteralOperatorId) {
    // Form a parsed representation of the template-id to be stored in the
    // UnqualifiedId.

    // FIXME: Store name for literal operator too.
    IdentifierInfo *TemplateII =
        Id.getKind() == UnqualifiedIdKind::IK_Identifier ? Id.Identifier
                                                         : nullptr;
    OverloadedOperatorKind OpKind =
        Id.getKind() == UnqualifiedIdKind::IK_Identifier
            ? OO_None
            : Id.OperatorFunctionId.Operator;

    TemplateIdAnnotation *TemplateId = TemplateIdAnnotation::Create(
        SS, TemplateKWLoc, Id.StartLocation, TemplateII, OpKind, Template, TNK,
        LAngleLoc, RAngleLoc, TemplateArgs, TemplateIds);

    Id.setTemplateId(TemplateId);
    return false;
  }

  // Bundle the template arguments together.
  ASTTemplateArgsPtr TemplateArgsPtr(TemplateArgs);

  // Constructor and destructor names.
  TypeResult Type
    = Actions.ActOnTemplateIdType(SS, TemplateKWLoc,
                                  Template, Name, NameLoc,
                                  LAngleLoc, TemplateArgsPtr, RAngleLoc,
                                  /*IsCtorOrDtorName=*/true);
  if (Type.isInvalid())
    return true;

  if (Id.getKind() == UnqualifiedIdKind::IK_ConstructorName)
    Id.setConstructorName(Type.get(), NameLoc, RAngleLoc);
  else
    Id.setDestructorName(Id.StartLocation, Type.get(), RAngleLoc);

  return false;
}

/// Parse an operator-function-id or conversion-function-id as part
/// of a C++ unqualified-id.
///
/// This routine is responsible only for parsing the operator-function-id or
/// conversion-function-id; it does not handle template arguments in any way.
///
/// \code
///       operator-function-id: [C++ 13.5]
///         'operator' operator
///
///       operator: one of
///            new   delete  new[]   delete[]
///            +     -    *  /    %  ^    &   |   ~
///            !     =    <  >    += -=   *=  /=  %=
///            ^=    &=   |= <<   >> >>= <<=  ==  !=
///            <=    >=   && ||   ++ --   ,   ->* ->
///            ()    []   <=>
///
///       conversion-function-id: [C++ 12.3.2]
///         operator conversion-type-id
///
///       conversion-type-id:
///         type-specifier-seq conversion-declarator[opt]
///
///       conversion-declarator:
///         ptr-operator conversion-declarator[opt]
/// \endcode
///
/// \param SS The nested-name-specifier that preceded this unqualified-id. If
/// non-empty, then we are parsing the unqualified-id of a qualified-id.
///
/// \param EnteringContext whether we are entering the scope of the
/// nested-name-specifier.
///
/// \param ObjectType if this unqualified-id occurs within a member access
/// expression, the type of the base object whose member is being accessed.
///
/// \param Result on a successful parse, contains the parsed unqualified-id.
///
/// \returns true if parsing fails, false otherwise.
bool Parser::ParseUnqualifiedIdOperator(CXXScopeSpec &SS, bool EnteringContext,
                                        ParsedType ObjectType,
                                        UnqualifiedId &Result) {
  assert(Tok.is(tok::kw_operator) && "Expected 'operator' keyword");

  // Consume the 'operator' keyword.
  SourceLocation KeywordLoc = ConsumeToken();

  // Determine what kind of operator name we have.
  unsigned SymbolIdx = 0;
  SourceLocation SymbolLocations[3];
  OverloadedOperatorKind Op = OO_None;
  switch (Tok.getKind()) {
    case tok::kw_new:
    case tok::kw_delete: {
      bool isNew = Tok.getKind() == tok::kw_new;
      // Consume the 'new' or 'delete'.
      SymbolLocations[SymbolIdx++] = ConsumeToken();
      // Check for array new/delete.
      if (Tok.is(tok::l_square) &&
          (!getLangOpts().CPlusPlus11 || NextToken().isNot(tok::l_square))) {
        // Consume the '[' and ']'.
        BalancedDelimiterTracker T(*this, tok::l_square);
        T.consumeOpen();
        T.consumeClose();
        if (T.getCloseLocation().isInvalid())
          return true;

        SymbolLocations[SymbolIdx++] = T.getOpenLocation();
        SymbolLocations[SymbolIdx++] = T.getCloseLocation();
        Op = isNew? OO_Array_New : OO_Array_Delete;
      } else {
        Op = isNew? OO_New : OO_Delete;
      }
      break;
    }

#define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
    case tok::Token:                                                     \
      SymbolLocations[SymbolIdx++] = ConsumeToken();                     \
      Op = OO_##Name;                                                    \
      break;
#define OVERLOADED_OPERATOR_MULTI(Name,Spelling,Unary,Binary,MemberOnly)
#include "clang/Basic/OperatorKinds.def"

    case tok::l_paren: {
      // Consume the '(' and ')'.
      BalancedDelimiterTracker T(*this, tok::l_paren);
      T.consumeOpen();
      T.consumeClose();
      if (T.getCloseLocation().isInvalid())
        return true;

      SymbolLocations[SymbolIdx++] = T.getOpenLocation();
      SymbolLocations[SymbolIdx++] = T.getCloseLocation();
      Op = OO_Call;
      break;
    }

    case tok::l_square: {
      // Consume the '[' and ']'.
      BalancedDelimiterTracker T(*this, tok::l_square);
      T.consumeOpen();
      T.consumeClose();
      if (T.getCloseLocation().isInvalid())
        return true;

      SymbolLocations[SymbolIdx++] = T.getOpenLocation();
      SymbolLocations[SymbolIdx++] = T.getCloseLocation();
      Op = OO_Subscript;
      break;
    }

    case tok::code_completion: {
      // Code completion for the operator name.
      Actions.CodeCompleteOperatorName(getCurScope());
      cutOffParsing();
      // Don't try to parse any further.
      return true;
    }

    default:
      break;
  }

  if (Op != OO_None) {
    // We have parsed an operator-function-id.
    Result.setOperatorFunctionId(KeywordLoc, Op, SymbolLocations);
    return false;
  }

  // Parse a literal-operator-id.
  //
  //   literal-operator-id: C++11 [over.literal]
  //     operator string-literal identifier
  //     operator user-defined-string-literal

  if (getLangOpts().CPlusPlus11 && isTokenStringLiteral()) {
    Diag(Tok.getLocation(), diag::warn_cxx98_compat_literal_operator);

    SourceLocation DiagLoc;
    unsigned DiagId = 0;

    // We're past translation phase 6, so perform string literal concatenation
    // before checking for "".
    SmallVector<Token, 4> Toks;
    SmallVector<SourceLocation, 4> TokLocs;
    while (isTokenStringLiteral()) {
      if (!Tok.is(tok::string_literal) && !DiagId) {
        // C++11 [over.literal]p1:
        //   The string-literal or user-defined-string-literal in a
        //   literal-operator-id shall have no encoding-prefix [...].
        DiagLoc = Tok.getLocation();
        DiagId = diag::err_literal_operator_string_prefix;
      }
      Toks.push_back(Tok);
      TokLocs.push_back(ConsumeStringToken());
    }

    StringLiteralParser Literal(Toks, PP);
    if (Literal.hadError)
      return true;

    // Grab the literal operator's suffix, which will be either the next token
    // or a ud-suffix from the string literal.
    IdentifierInfo *II = nullptr;
    SourceLocation SuffixLoc;
    if (!Literal.getUDSuffix().empty()) {
      II = &PP.getIdentifierTable().get(Literal.getUDSuffix());
      SuffixLoc =
        Lexer::AdvanceToTokenCharacter(TokLocs[Literal.getUDSuffixToken()],
                                       Literal.getUDSuffixOffset(),
                                       PP.getSourceManager(), getLangOpts());
    } else if (Tok.is(tok::identifier)) {
      II = Tok.getIdentifierInfo();
      SuffixLoc = ConsumeToken();
      TokLocs.push_back(SuffixLoc);
    } else {
      Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
      return true;
    }

    // The string literal must be empty.
    if (!Literal.GetString().empty() || Literal.Pascal) {
      // C++11 [over.literal]p1:
      //   The string-literal or user-defined-string-literal in a
      //   literal-operator-id shall [...] contain no characters
      //   other than the implicit terminating '\0'.
      DiagLoc = TokLocs.front();
      DiagId = diag::err_literal_operator_string_not_empty;
    }

    if (DiagId) {
      // This isn't a valid literal-operator-id, but we think we know
      // what the user meant. Tell them what they should have written.
      SmallString<32> Str;
      Str += "\"\"";
      Str += II->getName();
      Diag(DiagLoc, DiagId) << FixItHint::CreateReplacement(
          SourceRange(TokLocs.front(), TokLocs.back()), Str);
    }

    Result.setLiteralOperatorId(II, KeywordLoc, SuffixLoc);

    return Actions.checkLiteralOperatorId(SS, Result);
  }

  // Parse a conversion-function-id.
  //
  //   conversion-function-id: [C++ 12.3.2]
  //     operator conversion-type-id
  //
  //   conversion-type-id:
  //     type-specifier-seq conversion-declarator[opt]
  //
  //   conversion-declarator:
  //     ptr-operator conversion-declarator[opt]

  // Parse the type-specifier-seq.
  DeclSpec DS(AttrFactory);
  if (ParseCXXTypeSpecifierSeq(DS)) // FIXME: ObjectType?
    return true;

  // Parse the conversion-declarator, which is merely a sequence of
  // ptr-operators.
  Declarator D(DS, DeclaratorContext::ConversionIdContext);
  ParseDeclaratorInternal(D, /*DirectDeclParser=*/nullptr);

  // Finish up the type.
  TypeResult Ty = Actions.ActOnTypeName(getCurScope(), D);
  if (Ty.isInvalid())
    return true;

  // Note that this is a conversion-function-id.
  Result.setConversionFunctionId(KeywordLoc, Ty.get(),
                                 D.getSourceRange().getEnd());
  return false;
}

/// Parse a C++ unqualified-id (or a C identifier), which describes the
/// name of an entity.
///
/// \code
///       unqualified-id: [C++ expr.prim.general]
///         identifier
///         operator-function-id
///         conversion-function-id
/// [C++0x] literal-operator-id [TODO]
///         ~ class-name
///         template-id
///
/// \endcode
///
/// \param SS The nested-name-specifier that preceded this unqualified-id. If
/// non-empty, then we are parsing the unqualified-id of a qualified-id.
///
/// \param EnteringContext whether we are entering the scope of the
/// nested-name-specifier.
///
/// \param AllowDestructorName whether we allow parsing of a destructor name.
///
/// \param AllowConstructorName whether we allow parsing a constructor name.
///
/// \param AllowDeductionGuide whether we allow parsing a deduction guide name.
///
/// \param ObjectType if this unqualified-id occurs within a member access
/// expression, the type of the base object whose member is being accessed.
///
/// \param Result on a successful parse, contains the parsed unqualified-id.
///
/// \returns true if parsing fails, false otherwise.
bool Parser::ParseUnqualifiedId(CXXScopeSpec &SS, bool EnteringContext,
                                bool AllowDestructorName,
                                bool AllowConstructorName,
                                bool AllowDeductionGuide,
                                ParsedType ObjectType,
                                SourceLocation *TemplateKWLoc,
                                UnqualifiedId &Result) {
  if (TemplateKWLoc)
    *TemplateKWLoc = SourceLocation();

  // Handle 'A::template B'. This is for template-ids which have not
  // already been annotated by ParseOptionalCXXScopeSpecifier().
  bool TemplateSpecified = false;
  if (Tok.is(tok::kw_template)) {
    if (TemplateKWLoc && (ObjectType || SS.isSet())) {
      TemplateSpecified = true;
      *TemplateKWLoc = ConsumeToken();
    } else {
      SourceLocation TemplateLoc = ConsumeToken();
      Diag(TemplateLoc, diag::err_unexpected_template_in_unqualified_id)
        << FixItHint::CreateRemoval(TemplateLoc);
    }
  }

  // unqualified-id:
  //   identifier
  //   template-id (when it hasn't already been annotated)
  if (Tok.is(tok::identifier)) {
    // Consume the identifier.
    IdentifierInfo *Id = Tok.getIdentifierInfo();
    SourceLocation IdLoc = ConsumeToken();

    if (!getLangOpts().CPlusPlus) {
      // If we're not in C++, only identifiers matter. Record the
      // identifier and return.
      Result.setIdentifier(Id, IdLoc);
      return false;
    }

    ParsedTemplateTy TemplateName;
    if (AllowConstructorName &&
        Actions.isCurrentClassName(*Id, getCurScope(), &SS)) {
      // We have parsed a constructor name.
      ParsedType Ty = Actions.getConstructorName(*Id, IdLoc, getCurScope(), SS,
                                                 EnteringContext);
      if (!Ty)
        return true;
      Result.setConstructorName(Ty, IdLoc, IdLoc);
    } else if (getLangOpts().CPlusPlus17 &&
               AllowDeductionGuide && SS.isEmpty() &&
               Actions.isDeductionGuideName(getCurScope(), *Id, IdLoc,
                                            &TemplateName)) {
      // We have parsed a template-name naming a deduction guide.
      Result.setDeductionGuideName(TemplateName, IdLoc);
    } else {
      // We have parsed an identifier.
      Result.setIdentifier(Id, IdLoc);
    }

    // If the next token is a '<', we may have a template.
    TemplateTy Template;
    if (Tok.is(tok::less))
      return ParseUnqualifiedIdTemplateId(
          SS, TemplateKWLoc ? *TemplateKWLoc : SourceLocation(), Id, IdLoc,
          EnteringContext, ObjectType, Result, TemplateSpecified);
    else if (TemplateSpecified &&
             Actions.ActOnDependentTemplateName(
                 getCurScope(), SS, *TemplateKWLoc, Result, ObjectType,
                 EnteringContext, Template,
                 /*AllowInjectedClassName*/ true) == TNK_Non_template)
      return true;

    return false;
  }

  // unqualified-id:
  //   template-id (already parsed and annotated)
  if (Tok.is(tok::annot_template_id)) {
    TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);

    // If the template-name names the current class, then this is a constructor
    if (AllowConstructorName && TemplateId->Name &&
        Actions.isCurrentClassName(*TemplateId->Name, getCurScope(), &SS)) {
      if (SS.isSet()) {
        // C++ [class.qual]p2 specifies that a qualified template-name
        // is taken as the constructor name where a constructor can be
        // declared. Thus, the template arguments are extraneous, so
        // complain about them and remove them entirely.
        Diag(TemplateId->TemplateNameLoc,
             diag::err_out_of_line_constructor_template_id)
          << TemplateId->Name
          << FixItHint::CreateRemoval(
                    SourceRange(TemplateId->LAngleLoc, TemplateId->RAngleLoc));
        ParsedType Ty = Actions.getConstructorName(
            *TemplateId->Name, TemplateId->TemplateNameLoc, getCurScope(), SS,
            EnteringContext);
        if (!Ty)
          return true;
        Result.setConstructorName(Ty, TemplateId->TemplateNameLoc,
                                  TemplateId->RAngleLoc);
        ConsumeAnnotationToken();
        return false;
      }

      Result.setConstructorTemplateId(TemplateId);
      ConsumeAnnotationToken();
      return false;
    }

    // We have already parsed a template-id; consume the annotation token as
    // our unqualified-id.
    Result.setTemplateId(TemplateId);
    SourceLocation TemplateLoc = TemplateId->TemplateKWLoc;
    if (TemplateLoc.isValid()) {
      if (TemplateKWLoc && (ObjectType || SS.isSet()))
        *TemplateKWLoc = TemplateLoc;
      else
        Diag(TemplateLoc, diag::err_unexpected_template_in_unqualified_id)
            << FixItHint::CreateRemoval(TemplateLoc);
    }
    ConsumeAnnotationToken();
    return false;
  }

  // unqualified-id:
  //   operator-function-id
  //   conversion-function-id
  if (Tok.is(tok::kw_operator)) {
    if (ParseUnqualifiedIdOperator(SS, EnteringContext, ObjectType, Result))
      return true;

    // If we have an operator-function-id or a literal-operator-id and the next
    // token is a '<', we may have a
    //
    //   template-id:
    //     operator-function-id < template-argument-list[opt] >
    TemplateTy Template;
    if ((Result.getKind() == UnqualifiedIdKind::IK_OperatorFunctionId ||
         Result.getKind() == UnqualifiedIdKind::IK_LiteralOperatorId) &&
        Tok.is(tok::less))
      return ParseUnqualifiedIdTemplateId(
          SS, TemplateKWLoc ? *TemplateKWLoc : SourceLocation(), nullptr,
          SourceLocation(), EnteringContext, ObjectType, Result,
          TemplateSpecified);
    else if (TemplateSpecified &&
             Actions.ActOnDependentTemplateName(
                 getCurScope(), SS, *TemplateKWLoc, Result, ObjectType,
                 EnteringContext, Template,
                 /*AllowInjectedClassName*/ true) == TNK_Non_template)
      return true;

    return false;
  }

  if (getLangOpts().CPlusPlus &&
      (AllowDestructorName || SS.isSet()) && Tok.is(tok::tilde)) {
    // C++ [expr.unary.op]p10:
    //   There is an ambiguity in the unary-expression ~X(), where X is a
    //   class-name. The ambiguity is resolved in favor of treating ~ as a
    //    unary complement rather than treating ~X as referring to a destructor.

    // Parse the '~'.
    SourceLocation TildeLoc = ConsumeToken();

    if (SS.isEmpty() && Tok.is(tok::kw_decltype)) {
      DeclSpec DS(AttrFactory);
      SourceLocation EndLoc = ParseDecltypeSpecifier(DS);
      if (ParsedType Type =
              Actions.getDestructorTypeForDecltype(DS, ObjectType)) {
        Result.setDestructorName(TildeLoc, Type, EndLoc);
        return false;
      }
      return true;
    }

    // Parse the class-name.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_destructor_tilde_identifier);
      return true;
    }

    // If the user wrote ~T::T, correct it to T::~T.
    DeclaratorScopeObj DeclScopeObj(*this, SS);
    if (!TemplateSpecified && NextToken().is(tok::coloncolon)) {
      // Don't let ParseOptionalCXXScopeSpecifier() "correct"
      // `int A; struct { ~A::A(); };` to `int A; struct { ~A:A(); };`,
      // it will confuse this recovery logic.
      ColonProtectionRAIIObject ColonRAII(*this, false);

      if (SS.isSet()) {
        AnnotateScopeToken(SS, /*NewAnnotation*/true);
        SS.clear();
      }
      if (ParseOptionalCXXScopeSpecifier(SS, ObjectType, EnteringContext))
        return true;
      if (SS.isNotEmpty())
        ObjectType = nullptr;
      if (Tok.isNot(tok::identifier) || NextToken().is(tok::coloncolon) ||
          !SS.isSet()) {
        Diag(TildeLoc, diag::err_destructor_tilde_scope);
        return true;
      }

      // Recover as if the tilde had been written before the identifier.
      Diag(TildeLoc, diag::err_destructor_tilde_scope)
        << FixItHint::CreateRemoval(TildeLoc)
        << FixItHint::CreateInsertion(Tok.getLocation(), "~");

      // Temporarily enter the scope for the rest of this function.
      if (Actions.ShouldEnterDeclaratorScope(getCurScope(), SS))
        DeclScopeObj.EnterDeclaratorScope();
    }

    // Parse the class-name (or template-name in a simple-template-id).
    IdentifierInfo *ClassName = Tok.getIdentifierInfo();
    SourceLocation ClassNameLoc = ConsumeToken();

    if (Tok.is(tok::less)) {
      Result.setDestructorName(TildeLoc, nullptr, ClassNameLoc);
      return ParseUnqualifiedIdTemplateId(
          SS, TemplateKWLoc ? *TemplateKWLoc : SourceLocation(), ClassName,
          ClassNameLoc, EnteringContext, ObjectType, Result, TemplateSpecified);
    }

    // Note that this is a destructor name.
    ParsedType Ty = Actions.getDestructorName(TildeLoc, *ClassName,
                                              ClassNameLoc, getCurScope(),
                                              SS, ObjectType,
                                              EnteringContext);
    if (!Ty)
      return true;

    Result.setDestructorName(TildeLoc, Ty, ClassNameLoc);
    return false;
  }

  Diag(Tok, diag::err_expected_unqualified_id)
    << getLangOpts().CPlusPlus;
  return true;
}

/// ParseCXXNewExpression - Parse a C++ new-expression. New is used to allocate
/// memory in a typesafe manner and call constructors.
///
/// This method is called to parse the new expression after the optional :: has
/// been already parsed.  If the :: was present, "UseGlobal" is true and "Start"
/// is its location.  Otherwise, "Start" is the location of the 'new' token.
///
///        new-expression:
///                   '::'[opt] 'new' new-placement[opt] new-type-id
///                                     new-initializer[opt]
///                   '::'[opt] 'new' new-placement[opt] '(' type-id ')'
///                                     new-initializer[opt]
///
///        new-placement:
///                   '(' expression-list ')'
///
///        new-type-id:
///                   type-specifier-seq new-declarator[opt]
/// [GNU]             attributes type-specifier-seq new-declarator[opt]
///
///        new-declarator:
///                   ptr-operator new-declarator[opt]
///                   direct-new-declarator
///
///        new-initializer:
///                   '(' expression-list[opt] ')'
/// [C++0x]           braced-init-list
///
ExprResult
Parser::ParseCXXNewExpression(bool UseGlobal, SourceLocation Start) {
  assert(Tok.is(tok::kw_new) && "expected 'new' token");
  ConsumeToken();   // Consume 'new'

  // A '(' now can be a new-placement or the '(' wrapping the type-id in the
  // second form of new-expression. It can't be a new-type-id.

  ExprVector PlacementArgs;
  SourceLocation PlacementLParen, PlacementRParen;

  SourceRange TypeIdParens;
  DeclSpec DS(AttrFactory);
  Declarator DeclaratorInfo(DS, DeclaratorContext::CXXNewContext);
  if (Tok.is(tok::l_paren)) {
    // If it turns out to be a placement, we change the type location.
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    PlacementLParen = T.getOpenLocation();
    if (ParseExpressionListOrTypeId(PlacementArgs, DeclaratorInfo)) {
      SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
      return ExprError();
    }

    T.consumeClose();
    PlacementRParen = T.getCloseLocation();
    if (PlacementRParen.isInvalid()) {
      SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
      return ExprError();
    }

    if (PlacementArgs.empty()) {
      // Reset the placement locations. There was no placement.
      TypeIdParens = T.getRange();
      PlacementLParen = PlacementRParen = SourceLocation();
    } else {
      // We still need the type.
      if (Tok.is(tok::l_paren)) {
        BalancedDelimiterTracker T(*this, tok::l_paren);
        T.consumeOpen();
        MaybeParseGNUAttributes(DeclaratorInfo);
        ParseSpecifierQualifierList(DS);
        DeclaratorInfo.SetSourceRange(DS.getSourceRange());
        ParseDeclarator(DeclaratorInfo);
        T.consumeClose();
        TypeIdParens = T.getRange();
      } else {
        MaybeParseGNUAttributes(DeclaratorInfo);
        if (ParseCXXTypeSpecifierSeq(DS))
          DeclaratorInfo.setInvalidType(true);
        else {
          DeclaratorInfo.SetSourceRange(DS.getSourceRange());
          ParseDeclaratorInternal(DeclaratorInfo,
                                  &Parser::ParseDirectNewDeclarator);
        }
      }
    }
  } else {
    // A new-type-id is a simplified type-id, where essentially the
    // direct-declarator is replaced by a direct-new-declarator.
    MaybeParseGNUAttributes(DeclaratorInfo);
    if (ParseCXXTypeSpecifierSeq(DS))
      DeclaratorInfo.setInvalidType(true);
    else {
      DeclaratorInfo.SetSourceRange(DS.getSourceRange());
      ParseDeclaratorInternal(DeclaratorInfo,
                              &Parser::ParseDirectNewDeclarator);
    }
  }
  if (DeclaratorInfo.isInvalidType()) {
    SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
    return ExprError();
  }

  ExprResult Initializer;

  if (Tok.is(tok::l_paren)) {
    SourceLocation ConstructorLParen, ConstructorRParen;
    ExprVector ConstructorArgs;
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    ConstructorLParen = T.getOpenLocation();
    if (Tok.isNot(tok::r_paren)) {
      CommaLocsTy CommaLocs;
      if (ParseExpressionList(ConstructorArgs, CommaLocs, [&] {
            ParsedType TypeRep =
                Actions.ActOnTypeName(getCurScope(), DeclaratorInfo).get();
            QualType PreferredType = Actions.ProduceConstructorSignatureHelp(
                getCurScope(), TypeRep.get()->getCanonicalTypeInternal(),
                DeclaratorInfo.getEndLoc(), ConstructorArgs, ConstructorLParen);
            CalledSignatureHelp = true;
            Actions.CodeCompleteExpression(getCurScope(), PreferredType);
          })) {
        if (PP.isCodeCompletionReached() && !CalledSignatureHelp) {
          ParsedType TypeRep =
              Actions.ActOnTypeName(getCurScope(), DeclaratorInfo).get();
          Actions.ProduceConstructorSignatureHelp(
              getCurScope(), TypeRep.get()->getCanonicalTypeInternal(),
              DeclaratorInfo.getEndLoc(), ConstructorArgs, ConstructorLParen);
          CalledSignatureHelp = true;
        }
        SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
        return ExprError();
      }
    }
    T.consumeClose();
    ConstructorRParen = T.getCloseLocation();
    if (ConstructorRParen.isInvalid()) {
      SkipUntil(tok::semi, StopAtSemi | StopBeforeMatch);
      return ExprError();
    }
    Initializer = Actions.ActOnParenListExpr(ConstructorLParen,
                                             ConstructorRParen,
                                             ConstructorArgs);
  } else if (Tok.is(tok::l_brace) && getLangOpts().CPlusPlus11) {
    Diag(Tok.getLocation(),
         diag::warn_cxx98_compat_generalized_initializer_lists);
    Initializer = ParseBraceInitializer();
  }
  if (Initializer.isInvalid())
    return Initializer;

  return Actions.ActOnCXXNew(Start, UseGlobal, PlacementLParen,
                             PlacementArgs, PlacementRParen,
                             TypeIdParens, DeclaratorInfo, Initializer.get());
}

/// ParseDirectNewDeclarator - Parses a direct-new-declarator. Intended to be
/// passed to ParseDeclaratorInternal.
///
///        direct-new-declarator:
///                   '[' expression ']'
///                   direct-new-declarator '[' constant-expression ']'
///
void Parser::ParseDirectNewDeclarator(Declarator &D) {
  // Parse the array dimensions.
  bool first = true;
  while (Tok.is(tok::l_square)) {
    // An array-size expression can't start with a lambda.
    if (CheckProhibitedCXX11Attribute())
      continue;

    BalancedDelimiterTracker T(*this, tok::l_square);
    T.consumeOpen();

    ExprResult Size(first ? ParseExpression()
                                : ParseConstantExpression());
    if (Size.isInvalid()) {
      // Recover
      SkipUntil(tok::r_square, StopAtSemi);
      return;
    }
    first = false;

    T.consumeClose();

    // Attributes here appertain to the array type. C++11 [expr.new]p5.
    ParsedAttributes Attrs(AttrFactory);
    MaybeParseCXX11Attributes(Attrs);

    D.AddTypeInfo(DeclaratorChunk::getArray(0,
                                            /*static=*/false, /*star=*/false,
                                            Size.get(), T.getOpenLocation(),
                                            T.getCloseLocation()),
                  std::move(Attrs), T.getCloseLocation());

    if (T.getCloseLocation().isInvalid())
      return;
  }
}

/// ParseExpressionListOrTypeId - Parse either an expression-list or a type-id.
/// This ambiguity appears in the syntax of the C++ new operator.
///
///        new-expression:
///                   '::'[opt] 'new' new-placement[opt] '(' type-id ')'
///                                     new-initializer[opt]
///
///        new-placement:
///                   '(' expression-list ')'
///
bool Parser::ParseExpressionListOrTypeId(
                                   SmallVectorImpl<Expr*> &PlacementArgs,
                                         Declarator &D) {
  // The '(' was already consumed.
  if (isTypeIdInParens()) {
    ParseSpecifierQualifierList(D.getMutableDeclSpec());
    D.SetSourceRange(D.getDeclSpec().getSourceRange());
    ParseDeclarator(D);
    return D.isInvalidType();
  }

  // It's not a type, it has to be an expression list.
  // Discard the comma locations - ActOnCXXNew has enough parameters.
  CommaLocsTy CommaLocs;
  return ParseExpressionList(PlacementArgs, CommaLocs);
}

/// ParseCXXDeleteExpression - Parse a C++ delete-expression. Delete is used
/// to free memory allocated by new.
///
/// This method is called to parse the 'delete' expression after the optional
/// '::' has been already parsed.  If the '::' was present, "UseGlobal" is true
/// and "Start" is its location.  Otherwise, "Start" is the location of the
/// 'delete' token.
///
///        delete-expression:
///                   '::'[opt] 'delete' cast-expression
///                   '::'[opt] 'delete' '[' ']' cast-expression
ExprResult
Parser::ParseCXXDeleteExpression(bool UseGlobal, SourceLocation Start) {
  assert(Tok.is(tok::kw_delete) && "Expected 'delete' keyword");
  ConsumeToken(); // Consume 'delete'

  // Array delete?
  bool ArrayDelete = false;
  if (Tok.is(tok::l_square) && NextToken().is(tok::r_square)) {
    // C++11 [expr.delete]p1:
    //   Whenever the delete keyword is followed by empty square brackets, it
    //   shall be interpreted as [array delete].
    //   [Footnote: A lambda expression with a lambda-introducer that consists
    //              of empty square brackets can follow the delete keyword if
    //              the lambda expression is enclosed in parentheses.]
    // FIXME: Produce a better diagnostic if the '[]' is unambiguously a
    //        lambda-introducer.
    ArrayDelete = true;
    BalancedDelimiterTracker T(*this, tok::l_square);

    T.consumeOpen();
    T.consumeClose();
    if (T.getCloseLocation().isInvalid())
      return ExprError();
  }

  ExprResult Operand(ParseCastExpression(false));
  if (Operand.isInvalid())
    return Operand;

  return Actions.ActOnCXXDelete(Start, UseGlobal, ArrayDelete, Operand.get());
}

static TypeTrait TypeTraitFromTokKind(tok::TokenKind kind) {
  switch (kind) {
  default: llvm_unreachable("Not a known type trait");
#define TYPE_TRAIT_1(Spelling, Name, Key) \
case tok::kw_ ## Spelling: return UTT_ ## Name;
#define TYPE_TRAIT_2(Spelling, Name, Key) \
case tok::kw_ ## Spelling: return BTT_ ## Name;
#include "clang/Basic/TokenKinds.def"
#define TYPE_TRAIT_N(Spelling, Name, Key) \
  case tok::kw_ ## Spelling: return TT_ ## Name;
#include "clang/Basic/TokenKinds.def"
  }
}

static ArrayTypeTrait ArrayTypeTraitFromTokKind(tok::TokenKind kind) {
  switch(kind) {
  default: llvm_unreachable("Not a known binary type trait");
  case tok::kw___array_rank:                 return ATT_ArrayRank;
  case tok::kw___array_extent:               return ATT_ArrayExtent;
  }
}

static ExpressionTrait ExpressionTraitFromTokKind(tok::TokenKind kind) {
  switch(kind) {
  default: llvm_unreachable("Not a known unary expression trait.");
  case tok::kw___is_lvalue_expr:             return ET_IsLValueExpr;
  case tok::kw___is_rvalue_expr:             return ET_IsRValueExpr;
  }
}

static unsigned TypeTraitArity(tok::TokenKind kind) {
  switch (kind) {
    default: llvm_unreachable("Not a known type trait");
#define TYPE_TRAIT(N,Spelling,K) case tok::kw_##Spelling: return N;
#include "clang/Basic/TokenKinds.def"
  }
}

/// Parse the built-in type-trait pseudo-functions that allow
/// implementation of the TR1/C++11 type traits templates.
///
///       primary-expression:
///          unary-type-trait '(' type-id ')'
///          binary-type-trait '(' type-id ',' type-id ')'
///          type-trait '(' type-id-seq ')'
///
///       type-id-seq:
///          type-id ...[opt] type-id-seq[opt]
///
ExprResult Parser::ParseTypeTrait() {
  tok::TokenKind Kind = Tok.getKind();
  unsigned Arity = TypeTraitArity(Kind);

  SourceLocation Loc = ConsumeToken();

  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<ParsedType, 2> Args;
  do {
    // Parse the next type.
    TypeResult Ty = ParseTypeName();
    if (Ty.isInvalid()) {
      Parens.skipToEnd();
      return ExprError();
    }

    // Parse the ellipsis, if present.
    if (Tok.is(tok::ellipsis)) {
      Ty = Actions.ActOnPackExpansion(Ty.get(), ConsumeToken());
      if (Ty.isInvalid()) {
        Parens.skipToEnd();
        return ExprError();
      }
    }

    // Add this type to the list of arguments.
    Args.push_back(Ty.get());
  } while (TryConsumeToken(tok::comma));

  if (Parens.consumeClose())
    return ExprError();

  SourceLocation EndLoc = Parens.getCloseLocation();

  if (Arity && Args.size() != Arity) {
    Diag(EndLoc, diag::err_type_trait_arity)
      << Arity << 0 << (Arity > 1) << (int)Args.size() << SourceRange(Loc);
    return ExprError();
  }

  if (!Arity && Args.empty()) {
    Diag(EndLoc, diag::err_type_trait_arity)
      << 1 << 1 << 1 << (int)Args.size() << SourceRange(Loc);
    return ExprError();
  }

  return Actions.ActOnTypeTrait(TypeTraitFromTokKind(Kind), Loc, Args, EndLoc);
}

/// ParseArrayTypeTrait - Parse the built-in array type-trait
/// pseudo-functions.
///
///       primary-expression:
/// [Embarcadero]     '__array_rank' '(' type-id ')'
/// [Embarcadero]     '__array_extent' '(' type-id ',' expression ')'
///
ExprResult Parser::ParseArrayTypeTrait() {
  ArrayTypeTrait ATT = ArrayTypeTraitFromTokKind(Tok.getKind());
  SourceLocation Loc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  TypeResult Ty = ParseTypeName();
  if (Ty.isInvalid()) {
    SkipUntil(tok::comma, StopAtSemi);
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  switch (ATT) {
  case ATT_ArrayRank: {
    T.consumeClose();
    return Actions.ActOnArrayTypeTrait(ATT, Loc, Ty.get(), nullptr,
                                       T.getCloseLocation());
  }
  case ATT_ArrayExtent: {
    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult DimExpr = ParseExpression();
    T.consumeClose();

    return Actions.ActOnArrayTypeTrait(ATT, Loc, Ty.get(), DimExpr.get(),
                                       T.getCloseLocation());
  }
  }
  llvm_unreachable("Invalid ArrayTypeTrait!");
}

/// ParseExpressionTrait - Parse built-in expression-trait
/// pseudo-functions like __is_lvalue_expr( xxx ).
///
///       primary-expression:
/// [Embarcadero]     expression-trait '(' expression ')'
///
ExprResult Parser::ParseExpressionTrait() {
  ExpressionTrait ET = ExpressionTraitFromTokKind(Tok.getKind());
  SourceLocation Loc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  ExprResult Expr = ParseExpression();

  T.consumeClose();

  return Actions.ActOnExpressionTrait(ET, Loc, Expr.get(),
                                      T.getCloseLocation());
}


/// ParseCXXAmbiguousParenExpression - We have parsed the left paren of a
/// parenthesized ambiguous type-id. This uses tentative parsing to disambiguate
/// based on the context past the parens.
ExprResult
Parser::ParseCXXAmbiguousParenExpression(ParenParseOption &ExprType,
                                         ParsedType &CastTy,
                                         BalancedDelimiterTracker &Tracker,
                                         ColonProtectionRAIIObject &ColonProt) {
  assert(getLangOpts().CPlusPlus && "Should only be called for C++!");
  assert(ExprType == CastExpr && "Compound literals are not ambiguous!");
  assert(isTypeIdInParens() && "Not a type-id!");

  ExprResult Result(true);
  CastTy = nullptr;

  // We need to disambiguate a very ugly part of the C++ syntax:
  //
  // (T())x;  - type-id
  // (T())*x; - type-id
  // (T())/x; - expression
  // (T());   - expression
  //
  // The bad news is that we cannot use the specialized tentative parser, since
  // it can only verify that the thing inside the parens can be parsed as
  // type-id, it is not useful for determining the context past the parens.
  //
  // The good news is that the parser can disambiguate this part without
  // making any unnecessary Action calls.
  //
  // It uses a scheme similar to parsing inline methods. The parenthesized
  // tokens are cached, the context that follows is determined (possibly by
  // parsing a cast-expression), and then we re-introduce the cached tokens
  // into the token stream and parse them appropriately.

  ParenParseOption ParseAs;
  CachedTokens Toks;

  // Store the tokens of the parentheses. We will parse them after we determine
  // the context that follows them.
  if (!ConsumeAndStoreUntil(tok::r_paren, Toks)) {
    // We didn't find the ')' we expected.
    Tracker.consumeClose();
    return ExprError();
  }

  if (Tok.is(tok::l_brace)) {
    ParseAs = CompoundLiteral;
  } else {
    bool NotCastExpr;
    if (Tok.is(tok::l_paren) && NextToken().is(tok::r_paren)) {
      NotCastExpr = true;
    } else {
      // Try parsing the cast-expression that may follow.
      // If it is not a cast-expression, NotCastExpr will be true and no token
      // will be consumed.
      ColonProt.restore();
      Result = ParseCastExpression(false/*isUnaryExpression*/,
                                   false/*isAddressofOperand*/,
                                   NotCastExpr,
                                   // type-id has priority.
                                   IsTypeCast);
    }

    // If we parsed a cast-expression, it's really a type-id, otherwise it's
    // an expression.
    ParseAs = NotCastExpr ? SimpleExpr : CastExpr;
  }

  // Create a fake EOF to mark end of Toks buffer.
  Token AttrEnd;
  AttrEnd.startToken();
  AttrEnd.setKind(tok::eof);
  AttrEnd.setLocation(Tok.getLocation());
  AttrEnd.setEofData(Toks.data());
  Toks.push_back(AttrEnd);

  // The current token should go after the cached tokens.
  Toks.push_back(Tok);
  // Re-enter the stored parenthesized tokens into the token stream, so we may
  // parse them now.
  PP.EnterTokenStream(Toks, true /*DisableMacroExpansion*/);
  // Drop the current token and bring the first cached one. It's the same token
  // as when we entered this function.
  ConsumeAnyToken();

  if (ParseAs >= CompoundLiteral) {
    // Parse the type declarator.
    DeclSpec DS(AttrFactory);
    Declarator DeclaratorInfo(DS, DeclaratorContext::TypeNameContext);
    {
      ColonProtectionRAIIObject InnerColonProtection(*this);
      ParseSpecifierQualifierList(DS);
      ParseDeclarator(DeclaratorInfo);
    }

    // Match the ')'.
    Tracker.consumeClose();
    ColonProt.restore();

    // Consume EOF marker for Toks buffer.
    assert(Tok.is(tok::eof) && Tok.getEofData() == AttrEnd.getEofData());
    ConsumeAnyToken();

    if (ParseAs == CompoundLiteral) {
      ExprType = CompoundLiteral;
      if (DeclaratorInfo.isInvalidType())
        return ExprError();

      TypeResult Ty = Actions.ActOnTypeName(getCurScope(), DeclaratorInfo);
      return ParseCompoundLiteralExpression(Ty.get(),
                                            Tracker.getOpenLocation(),
                                            Tracker.getCloseLocation());
    }

    // We parsed '(' type-id ')' and the thing after it wasn't a '{'.
    assert(ParseAs == CastExpr);

    if (DeclaratorInfo.isInvalidType())
      return ExprError();

    // Result is what ParseCastExpression returned earlier.
    if (!Result.isInvalid())
      Result = Actions.ActOnCastExpr(getCurScope(), Tracker.getOpenLocation(),
                                    DeclaratorInfo, CastTy,
                                    Tracker.getCloseLocation(), Result.get());
    return Result;
  }

  // Not a compound literal, and not followed by a cast-expression.
  assert(ParseAs == SimpleExpr);

  ExprType = SimpleExpr;
  Result = ParseExpression();
  if (!Result.isInvalid() && Tok.is(tok::r_paren))
    Result = Actions.ActOnParenExpr(Tracker.getOpenLocation(),
                                    Tok.getLocation(), Result.get());

  // Match the ')'.
  if (Result.isInvalid()) {
    while (Tok.isNot(tok::eof))
      ConsumeAnyToken();
    assert(Tok.getEofData() == AttrEnd.getEofData());
    ConsumeAnyToken();
    return ExprError();
  }

  Tracker.consumeClose();
  // Consume EOF marker for Toks buffer.
  assert(Tok.is(tok::eof) && Tok.getEofData() == AttrEnd.getEofData());
  ConsumeAnyToken();
  return Result;
}
