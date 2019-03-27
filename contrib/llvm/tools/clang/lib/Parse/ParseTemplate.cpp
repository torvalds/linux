//===--- ParseTemplate.cpp - Template Parsing -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing of C++ templates.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
using namespace clang;

/// Parse a template declaration, explicit instantiation, or
/// explicit specialization.
Decl *Parser::ParseDeclarationStartingWithTemplate(
    DeclaratorContext Context, SourceLocation &DeclEnd,
    ParsedAttributes &AccessAttrs, AccessSpecifier AS) {
  ObjCDeclContextSwitch ObjCDC(*this);

  if (Tok.is(tok::kw_template) && NextToken().isNot(tok::less)) {
    return ParseExplicitInstantiation(Context, SourceLocation(), ConsumeToken(),
                                      DeclEnd, AccessAttrs, AS);
  }
  return ParseTemplateDeclarationOrSpecialization(Context, DeclEnd, AccessAttrs,
                                                  AS);
}

/// Parse a template declaration or an explicit specialization.
///
/// Template declarations include one or more template parameter lists
/// and either the function or class template declaration. Explicit
/// specializations contain one or more 'template < >' prefixes
/// followed by a (possibly templated) declaration. Since the
/// syntactic form of both features is nearly identical, we parse all
/// of the template headers together and let semantic analysis sort
/// the declarations from the explicit specializations.
///
///       template-declaration: [C++ temp]
///         'export'[opt] 'template' '<' template-parameter-list '>' declaration
///
///       explicit-specialization: [ C++ temp.expl.spec]
///         'template' '<' '>' declaration
Decl *Parser::ParseTemplateDeclarationOrSpecialization(
    DeclaratorContext Context, SourceLocation &DeclEnd,
    ParsedAttributes &AccessAttrs, AccessSpecifier AS) {
  assert(Tok.isOneOf(tok::kw_export, tok::kw_template) &&
         "Token does not start a template declaration.");

  // Enter template-parameter scope.
  ParseScope TemplateParmScope(this, Scope::TemplateParamScope);

  // Tell the action that names should be checked in the context of
  // the declaration to come.
  ParsingDeclRAIIObject
    ParsingTemplateParams(*this, ParsingDeclRAIIObject::NoParent);

  // Parse multiple levels of template headers within this template
  // parameter scope, e.g.,
  //
  //   template<typename T>
  //     template<typename U>
  //       class A<T>::B { ... };
  //
  // We parse multiple levels non-recursively so that we can build a
  // single data structure containing all of the template parameter
  // lists to easily differentiate between the case above and:
  //
  //   template<typename T>
  //   class A {
  //     template<typename U> class B;
  //   };
  //
  // In the first case, the action for declaring A<T>::B receives
  // both template parameter lists. In the second case, the action for
  // defining A<T>::B receives just the inner template parameter list
  // (and retrieves the outer template parameter list from its
  // context).
  bool isSpecialization = true;
  bool LastParamListWasEmpty = false;
  TemplateParameterLists ParamLists;
  TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);

  do {
    // Consume the 'export', if any.
    SourceLocation ExportLoc;
    TryConsumeToken(tok::kw_export, ExportLoc);

    // Consume the 'template', which should be here.
    SourceLocation TemplateLoc;
    if (!TryConsumeToken(tok::kw_template, TemplateLoc)) {
      Diag(Tok.getLocation(), diag::err_expected_template);
      return nullptr;
    }

    // Parse the '<' template-parameter-list '>'
    SourceLocation LAngleLoc, RAngleLoc;
    SmallVector<NamedDecl*, 4> TemplateParams;
    if (ParseTemplateParameters(CurTemplateDepthTracker.getDepth(),
                                TemplateParams, LAngleLoc, RAngleLoc)) {
      // Skip until the semi-colon or a '}'.
      SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
      TryConsumeToken(tok::semi);
      return nullptr;
    }

    ExprResult OptionalRequiresClauseConstraintER;
    if (!TemplateParams.empty()) {
      isSpecialization = false;
      ++CurTemplateDepthTracker;

      if (TryConsumeToken(tok::kw_requires)) {
        OptionalRequiresClauseConstraintER =
            Actions.CorrectDelayedTyposInExpr(ParseConstraintExpression());
        if (!OptionalRequiresClauseConstraintER.isUsable()) {
          // Skip until the semi-colon or a '}'.
          SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
          TryConsumeToken(tok::semi);
          return nullptr;
        }
      }
    } else {
      LastParamListWasEmpty = true;
    }

    ParamLists.push_back(Actions.ActOnTemplateParameterList(
        CurTemplateDepthTracker.getDepth(), ExportLoc, TemplateLoc, LAngleLoc,
        TemplateParams, RAngleLoc, OptionalRequiresClauseConstraintER.get()));
  } while (Tok.isOneOf(tok::kw_export, tok::kw_template));

  unsigned NewFlags = getCurScope()->getFlags() & ~Scope::TemplateParamScope;
  ParseScopeFlags TemplateScopeFlags(this, NewFlags, isSpecialization);

  // Parse the actual template declaration.
  return ParseSingleDeclarationAfterTemplate(
      Context,
      ParsedTemplateInfo(&ParamLists, isSpecialization, LastParamListWasEmpty),
      ParsingTemplateParams, DeclEnd, AccessAttrs, AS);
}

/// Parse a single declaration that declares a template,
/// template specialization, or explicit instantiation of a template.
///
/// \param DeclEnd will receive the source location of the last token
/// within this declaration.
///
/// \param AS the access specifier associated with this
/// declaration. Will be AS_none for namespace-scope declarations.
///
/// \returns the new declaration.
Decl *Parser::ParseSingleDeclarationAfterTemplate(
    DeclaratorContext Context, const ParsedTemplateInfo &TemplateInfo,
    ParsingDeclRAIIObject &DiagsFromTParams, SourceLocation &DeclEnd,
    ParsedAttributes &AccessAttrs, AccessSpecifier AS) {
  assert(TemplateInfo.Kind != ParsedTemplateInfo::NonTemplate &&
         "Template information required");

  if (Tok.is(tok::kw_static_assert)) {
    // A static_assert declaration may not be templated.
    Diag(Tok.getLocation(), diag::err_templated_invalid_declaration)
      << TemplateInfo.getSourceRange();
    // Parse the static_assert declaration to improve error recovery.
    return ParseStaticAssertDeclaration(DeclEnd);
  }

  if (Context == DeclaratorContext::MemberContext) {
    // We are parsing a member template.
    ParseCXXClassMemberDeclaration(AS, AccessAttrs, TemplateInfo,
                                   &DiagsFromTParams);
    return nullptr;
  }

  ParsedAttributesWithRange prefixAttrs(AttrFactory);
  MaybeParseCXX11Attributes(prefixAttrs);

  if (Tok.is(tok::kw_using)) {
    auto usingDeclPtr = ParseUsingDirectiveOrDeclaration(Context, TemplateInfo, DeclEnd,
                                                         prefixAttrs);
    if (!usingDeclPtr || !usingDeclPtr.get().isSingleDecl())
      return nullptr;
    return usingDeclPtr.get().getSingleDecl();
  }

  // Parse the declaration specifiers, stealing any diagnostics from
  // the template parameters.
  ParsingDeclSpec DS(*this, &DiagsFromTParams);

  ParseDeclarationSpecifiers(DS, TemplateInfo, AS,
                             getDeclSpecContextFromDeclaratorContext(Context));

  if (Tok.is(tok::semi)) {
    ProhibitAttributes(prefixAttrs);
    DeclEnd = ConsumeToken();
    RecordDecl *AnonRecord = nullptr;
    Decl *Decl = Actions.ParsedFreeStandingDeclSpec(
        getCurScope(), AS, DS,
        TemplateInfo.TemplateParams ? *TemplateInfo.TemplateParams
                                    : MultiTemplateParamsArg(),
        TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation,
        AnonRecord);
    assert(!AnonRecord &&
           "Anonymous unions/structs should not be valid with template");
    DS.complete(Decl);
    return Decl;
  }

  // Move the attributes from the prefix into the DS.
  if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation)
    ProhibitAttributes(prefixAttrs);
  else
    DS.takeAttributesFrom(prefixAttrs);

  // Parse the declarator.
  ParsingDeclarator DeclaratorInfo(*this, DS, (DeclaratorContext)Context);
  ParseDeclarator(DeclaratorInfo);
  // Error parsing the declarator?
  if (!DeclaratorInfo.hasName()) {
    // If so, skip until the semi-colon or a }.
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
    if (Tok.is(tok::semi))
      ConsumeToken();
    return nullptr;
  }

  LateParsedAttrList LateParsedAttrs(true);
  if (DeclaratorInfo.isFunctionDeclarator())
    MaybeParseGNUAttributes(DeclaratorInfo, &LateParsedAttrs);

  if (DeclaratorInfo.isFunctionDeclarator() &&
      isStartOfFunctionDefinition(DeclaratorInfo)) {

    // Function definitions are only allowed at file scope and in C++ classes.
    // The C++ inline method definition case is handled elsewhere, so we only
    // need to handle the file scope definition case.
    if (Context != DeclaratorContext::FileContext) {
      Diag(Tok, diag::err_function_definition_not_allowed);
      SkipMalformedDecl();
      return nullptr;
    }

    if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef) {
      // Recover by ignoring the 'typedef'. This was probably supposed to be
      // the 'typename' keyword, which we should have already suggested adding
      // if it's appropriate.
      Diag(DS.getStorageClassSpecLoc(), diag::err_function_declared_typedef)
        << FixItHint::CreateRemoval(DS.getStorageClassSpecLoc());
      DS.ClearStorageClassSpecs();
    }

    if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation) {
      if (DeclaratorInfo.getName().getKind() !=
          UnqualifiedIdKind::IK_TemplateId) {
        // If the declarator-id is not a template-id, issue a diagnostic and
        // recover by ignoring the 'template' keyword.
        Diag(Tok, diag::err_template_defn_explicit_instantiation) << 0;
        return ParseFunctionDefinition(DeclaratorInfo, ParsedTemplateInfo(),
                                       &LateParsedAttrs);
      } else {
        SourceLocation LAngleLoc
          = PP.getLocForEndOfToken(TemplateInfo.TemplateLoc);
        Diag(DeclaratorInfo.getIdentifierLoc(),
             diag::err_explicit_instantiation_with_definition)
            << SourceRange(TemplateInfo.TemplateLoc)
            << FixItHint::CreateInsertion(LAngleLoc, "<>");

        // Recover as if it were an explicit specialization.
        TemplateParameterLists FakedParamLists;
        FakedParamLists.push_back(Actions.ActOnTemplateParameterList(
            0, SourceLocation(), TemplateInfo.TemplateLoc, LAngleLoc, None,
            LAngleLoc, nullptr));

        return ParseFunctionDefinition(
            DeclaratorInfo, ParsedTemplateInfo(&FakedParamLists,
                                               /*isSpecialization=*/true,
                                               /*LastParamListWasEmpty=*/true),
            &LateParsedAttrs);
      }
    }
    return ParseFunctionDefinition(DeclaratorInfo, TemplateInfo,
                                   &LateParsedAttrs);
  }

  // Parse this declaration.
  Decl *ThisDecl = ParseDeclarationAfterDeclarator(DeclaratorInfo,
                                                   TemplateInfo);

  if (Tok.is(tok::comma)) {
    Diag(Tok, diag::err_multiple_template_declarators)
      << (int)TemplateInfo.Kind;
    SkipUntil(tok::semi);
    return ThisDecl;
  }

  // Eat the semi colon after the declaration.
  ExpectAndConsumeSemi(diag::err_expected_semi_declaration);
  if (LateParsedAttrs.size() > 0)
    ParseLexedAttributeList(LateParsedAttrs, ThisDecl, true, false);
  DeclaratorInfo.complete(ThisDecl);
  return ThisDecl;
}

/// ParseTemplateParameters - Parses a template-parameter-list enclosed in
/// angle brackets. Depth is the depth of this template-parameter-list, which
/// is the number of template headers directly enclosing this template header.
/// TemplateParams is the current list of template parameters we're building.
/// The template parameter we parse will be added to this list. LAngleLoc and
/// RAngleLoc will receive the positions of the '<' and '>', respectively,
/// that enclose this template parameter list.
///
/// \returns true if an error occurred, false otherwise.
bool Parser::ParseTemplateParameters(
    unsigned Depth, SmallVectorImpl<NamedDecl *> &TemplateParams,
    SourceLocation &LAngleLoc, SourceLocation &RAngleLoc) {
  // Get the template parameter list.
  if (!TryConsumeToken(tok::less, LAngleLoc)) {
    Diag(Tok.getLocation(), diag::err_expected_less_after) << "template";
    return true;
  }

  // Try to parse the template parameter list.
  bool Failed = false;
  if (!Tok.is(tok::greater) && !Tok.is(tok::greatergreater))
    Failed = ParseTemplateParameterList(Depth, TemplateParams);

  if (Tok.is(tok::greatergreater)) {
    // No diagnostic required here: a template-parameter-list can only be
    // followed by a declaration or, for a template template parameter, the
    // 'class' keyword. Therefore, the second '>' will be diagnosed later.
    // This matters for elegant diagnosis of:
    //   template<template<typename>> struct S;
    Tok.setKind(tok::greater);
    RAngleLoc = Tok.getLocation();
    Tok.setLocation(Tok.getLocation().getLocWithOffset(1));
  } else if (!TryConsumeToken(tok::greater, RAngleLoc) && Failed) {
    Diag(Tok.getLocation(), diag::err_expected) << tok::greater;
    return true;
  }
  return false;
}

/// ParseTemplateParameterList - Parse a template parameter list. If
/// the parsing fails badly (i.e., closing bracket was left out), this
/// will try to put the token stream in a reasonable position (closing
/// a statement, etc.) and return false.
///
///       template-parameter-list:    [C++ temp]
///         template-parameter
///         template-parameter-list ',' template-parameter
bool
Parser::ParseTemplateParameterList(const unsigned Depth,
                             SmallVectorImpl<NamedDecl*> &TemplateParams) {
  while (1) {

    if (NamedDecl *TmpParam
          = ParseTemplateParameter(Depth, TemplateParams.size())) {
      TemplateParams.push_back(TmpParam);
    } else {
      // If we failed to parse a template parameter, skip until we find
      // a comma or closing brace.
      SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                StopAtSemi | StopBeforeMatch);
    }

    // Did we find a comma or the end of the template parameter list?
    if (Tok.is(tok::comma)) {
      ConsumeToken();
    } else if (Tok.isOneOf(tok::greater, tok::greatergreater)) {
      // Don't consume this... that's done by template parser.
      break;
    } else {
      // Somebody probably forgot to close the template. Skip ahead and
      // try to get out of the expression. This error is currently
      // subsumed by whatever goes on in ParseTemplateParameter.
      Diag(Tok.getLocation(), diag::err_expected_comma_greater);
      SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                StopAtSemi | StopBeforeMatch);
      return false;
    }
  }
  return true;
}

/// Determine whether the parser is at the start of a template
/// type parameter.
bool Parser::isStartOfTemplateTypeParameter() {
  if (Tok.is(tok::kw_class)) {
    // "class" may be the start of an elaborated-type-specifier or a
    // type-parameter. Per C++ [temp.param]p3, we prefer the type-parameter.
    switch (NextToken().getKind()) {
    case tok::equal:
    case tok::comma:
    case tok::greater:
    case tok::greatergreater:
    case tok::ellipsis:
      return true;

    case tok::identifier:
      // This may be either a type-parameter or an elaborated-type-specifier.
      // We have to look further.
      break;

    default:
      return false;
    }

    switch (GetLookAheadToken(2).getKind()) {
    case tok::equal:
    case tok::comma:
    case tok::greater:
    case tok::greatergreater:
      return true;

    default:
      return false;
    }
  }

  // 'typedef' is a reasonably-common typo/thinko for 'typename', and is
  // ill-formed otherwise.
  if (Tok.isNot(tok::kw_typename) && Tok.isNot(tok::kw_typedef))
    return false;

  // C++ [temp.param]p2:
  //   There is no semantic difference between class and typename in a
  //   template-parameter. typename followed by an unqualified-id
  //   names a template type parameter. typename followed by a
  //   qualified-id denotes the type in a non-type
  //   parameter-declaration.
  Token Next = NextToken();

  // If we have an identifier, skip over it.
  if (Next.getKind() == tok::identifier)
    Next = GetLookAheadToken(2);

  switch (Next.getKind()) {
  case tok::equal:
  case tok::comma:
  case tok::greater:
  case tok::greatergreater:
  case tok::ellipsis:
    return true;

  case tok::kw_typename:
  case tok::kw_typedef:
  case tok::kw_class:
    // These indicate that a comma was missed after a type parameter, not that
    // we have found a non-type parameter.
    return true;

  default:
    return false;
  }
}

/// ParseTemplateParameter - Parse a template-parameter (C++ [temp.param]).
///
///       template-parameter: [C++ temp.param]
///         type-parameter
///         parameter-declaration
///
///       type-parameter: (see below)
///         'class' ...[opt] identifier[opt]
///         'class' identifier[opt] '=' type-id
///         'typename' ...[opt] identifier[opt]
///         'typename' identifier[opt] '=' type-id
///         'template' '<' template-parameter-list '>'
///               'class' ...[opt] identifier[opt]
///         'template' '<' template-parameter-list '>' 'class' identifier[opt]
///               = id-expression
NamedDecl *Parser::ParseTemplateParameter(unsigned Depth, unsigned Position) {
  if (isStartOfTemplateTypeParameter()) {
    // Is there just a typo in the input code? ('typedef' instead of 'typename')
    if (Tok.is(tok::kw_typedef)) {
      Diag(Tok.getLocation(), diag::err_expected_template_parameter);

      Diag(Tok.getLocation(), diag::note_meant_to_use_typename)
          << FixItHint::CreateReplacement(CharSourceRange::getCharRange(
                                              Tok.getLocation(), Tok.getEndLoc()),
                                          "typename");

      Tok.setKind(tok::kw_typename);
    }

    return ParseTypeParameter(Depth, Position);
  }

  if (Tok.is(tok::kw_template))
    return ParseTemplateTemplateParameter(Depth, Position);

  // If it's none of the above, then it must be a parameter declaration.
  // NOTE: This will pick up errors in the closure of the template parameter
  // list (e.g., template < ; Check here to implement >> style closures.
  return ParseNonTypeTemplateParameter(Depth, Position);
}

/// ParseTypeParameter - Parse a template type parameter (C++ [temp.param]).
/// Other kinds of template parameters are parsed in
/// ParseTemplateTemplateParameter and ParseNonTypeTemplateParameter.
///
///       type-parameter:     [C++ temp.param]
///         'class' ...[opt][C++0x] identifier[opt]
///         'class' identifier[opt] '=' type-id
///         'typename' ...[opt][C++0x] identifier[opt]
///         'typename' identifier[opt] '=' type-id
NamedDecl *Parser::ParseTypeParameter(unsigned Depth, unsigned Position) {
  assert(Tok.isOneOf(tok::kw_class, tok::kw_typename) &&
         "A type-parameter starts with 'class' or 'typename'");

  // Consume the 'class' or 'typename' keyword.
  bool TypenameKeyword = Tok.is(tok::kw_typename);
  SourceLocation KeyLoc = ConsumeToken();

  // Grab the ellipsis (if given).
  SourceLocation EllipsisLoc;
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc)) {
    Diag(EllipsisLoc,
         getLangOpts().CPlusPlus11
           ? diag::warn_cxx98_compat_variadic_templates
           : diag::ext_variadic_templates);
  }

  // Grab the template parameter name (if given)
  SourceLocation NameLoc;
  IdentifierInfo *ParamName = nullptr;
  if (Tok.is(tok::identifier)) {
    ParamName = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  } else if (Tok.isOneOf(tok::equal, tok::comma, tok::greater,
                         tok::greatergreater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
    return nullptr;
  }

  // Recover from misplaced ellipsis.
  bool AlreadyHasEllipsis = EllipsisLoc.isValid();
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    DiagnoseMisplacedEllipsis(EllipsisLoc, NameLoc, AlreadyHasEllipsis, true);

  // Grab a default argument (if available).
  // Per C++0x [basic.scope.pdecl]p9, we parse the default argument before
  // we introduce the type parameter into the local scope.
  SourceLocation EqualLoc;
  ParsedType DefaultArg;
  if (TryConsumeToken(tok::equal, EqualLoc))
    DefaultArg = ParseTypeName(/*Range=*/nullptr,
                               DeclaratorContext::TemplateTypeArgContext).get();

  return Actions.ActOnTypeParameter(getCurScope(), TypenameKeyword, EllipsisLoc,
                                    KeyLoc, ParamName, NameLoc, Depth, Position,
                                    EqualLoc, DefaultArg);
}

/// ParseTemplateTemplateParameter - Handle the parsing of template
/// template parameters.
///
///       type-parameter:    [C++ temp.param]
///         'template' '<' template-parameter-list '>' type-parameter-key
///                  ...[opt] identifier[opt]
///         'template' '<' template-parameter-list '>' type-parameter-key
///                  identifier[opt] = id-expression
///       type-parameter-key:
///         'class'
///         'typename'       [C++1z]
NamedDecl *
Parser::ParseTemplateTemplateParameter(unsigned Depth, unsigned Position) {
  assert(Tok.is(tok::kw_template) && "Expected 'template' keyword");

  // Handle the template <...> part.
  SourceLocation TemplateLoc = ConsumeToken();
  SmallVector<NamedDecl*,8> TemplateParams;
  SourceLocation LAngleLoc, RAngleLoc;
  {
    ParseScope TemplateParmScope(this, Scope::TemplateParamScope);
    if (ParseTemplateParameters(Depth + 1, TemplateParams, LAngleLoc,
                               RAngleLoc)) {
      return nullptr;
    }
  }

  // Provide an ExtWarn if the C++1z feature of using 'typename' here is used.
  // Generate a meaningful error if the user forgot to put class before the
  // identifier, comma, or greater. Provide a fixit if the identifier, comma,
  // or greater appear immediately or after 'struct'. In the latter case,
  // replace the keyword with 'class'.
  if (!TryConsumeToken(tok::kw_class)) {
    bool Replace = Tok.isOneOf(tok::kw_typename, tok::kw_struct);
    const Token &Next = Tok.is(tok::kw_struct) ? NextToken() : Tok;
    if (Tok.is(tok::kw_typename)) {
      Diag(Tok.getLocation(),
           getLangOpts().CPlusPlus17
               ? diag::warn_cxx14_compat_template_template_param_typename
               : diag::ext_template_template_param_typename)
        << (!getLangOpts().CPlusPlus17
                ? FixItHint::CreateReplacement(Tok.getLocation(), "class")
                : FixItHint());
    } else if (Next.isOneOf(tok::identifier, tok::comma, tok::greater,
                            tok::greatergreater, tok::ellipsis)) {
      Diag(Tok.getLocation(), diag::err_class_on_template_template_param)
        << (Replace ? FixItHint::CreateReplacement(Tok.getLocation(), "class")
                    : FixItHint::CreateInsertion(Tok.getLocation(), "class "));
    } else
      Diag(Tok.getLocation(), diag::err_class_on_template_template_param);

    if (Replace)
      ConsumeToken();
  }

  // Parse the ellipsis, if given.
  SourceLocation EllipsisLoc;
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    Diag(EllipsisLoc,
         getLangOpts().CPlusPlus11
           ? diag::warn_cxx98_compat_variadic_templates
           : diag::ext_variadic_templates);

  // Get the identifier, if given.
  SourceLocation NameLoc;
  IdentifierInfo *ParamName = nullptr;
  if (Tok.is(tok::identifier)) {
    ParamName = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  } else if (Tok.isOneOf(tok::equal, tok::comma, tok::greater,
                         tok::greatergreater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
    return nullptr;
  }

  // Recover from misplaced ellipsis.
  bool AlreadyHasEllipsis = EllipsisLoc.isValid();
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    DiagnoseMisplacedEllipsis(EllipsisLoc, NameLoc, AlreadyHasEllipsis, true);

  TemplateParameterList *ParamList =
    Actions.ActOnTemplateParameterList(Depth, SourceLocation(),
                                       TemplateLoc, LAngleLoc,
                                       TemplateParams,
                                       RAngleLoc, nullptr);

  // Grab a default argument (if available).
  // Per C++0x [basic.scope.pdecl]p9, we parse the default argument before
  // we introduce the template parameter into the local scope.
  SourceLocation EqualLoc;
  ParsedTemplateArgument DefaultArg;
  if (TryConsumeToken(tok::equal, EqualLoc)) {
    DefaultArg = ParseTemplateTemplateArgument();
    if (DefaultArg.isInvalid()) {
      Diag(Tok.getLocation(),
           diag::err_default_template_template_parameter_not_template);
      SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                StopAtSemi | StopBeforeMatch);
    }
  }

  return Actions.ActOnTemplateTemplateParameter(getCurScope(), TemplateLoc,
                                                ParamList, EllipsisLoc,
                                                ParamName, NameLoc, Depth,
                                                Position, EqualLoc, DefaultArg);
}

/// ParseNonTypeTemplateParameter - Handle the parsing of non-type
/// template parameters (e.g., in "template<int Size> class array;").
///
///       template-parameter:
///         ...
///         parameter-declaration
NamedDecl *
Parser::ParseNonTypeTemplateParameter(unsigned Depth, unsigned Position) {
  // Parse the declaration-specifiers (i.e., the type).
  // FIXME: The type should probably be restricted in some way... Not all
  // declarators (parts of declarators?) are accepted for parameters.
  DeclSpec DS(AttrFactory);
  ParseDeclarationSpecifiers(DS, ParsedTemplateInfo(), AS_none,
                             DeclSpecContext::DSC_template_param);

  // Parse this as a typename.
  Declarator ParamDecl(DS, DeclaratorContext::TemplateParamContext);
  ParseDeclarator(ParamDecl);
  if (DS.getTypeSpecType() == DeclSpec::TST_unspecified) {
    Diag(Tok.getLocation(), diag::err_expected_template_parameter);
    return nullptr;
  }

  // Recover from misplaced ellipsis.
  SourceLocation EllipsisLoc;
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    DiagnoseMisplacedEllipsisInDeclarator(EllipsisLoc, ParamDecl);

  // If there is a default value, parse it.
  // Per C++0x [basic.scope.pdecl]p9, we parse the default argument before
  // we introduce the template parameter into the local scope.
  SourceLocation EqualLoc;
  ExprResult DefaultArg;
  if (TryConsumeToken(tok::equal, EqualLoc)) {
    // C++ [temp.param]p15:
    //   When parsing a default template-argument for a non-type
    //   template-parameter, the first non-nested > is taken as the
    //   end of the template-parameter-list rather than a greater-than
    //   operator.
    GreaterThanIsOperatorScope G(GreaterThanIsOperator, false);
    EnterExpressionEvaluationContext ConstantEvaluated(
        Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);

    DefaultArg = Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    if (DefaultArg.isInvalid())
      SkipUntil(tok::comma, tok::greater, StopAtSemi | StopBeforeMatch);
  }

  // Create the parameter.
  return Actions.ActOnNonTypeTemplateParameter(getCurScope(), ParamDecl,
                                               Depth, Position, EqualLoc,
                                               DefaultArg.get());
}

void Parser::DiagnoseMisplacedEllipsis(SourceLocation EllipsisLoc,
                                       SourceLocation CorrectLoc,
                                       bool AlreadyHasEllipsis,
                                       bool IdentifierHasName) {
  FixItHint Insertion;
  if (!AlreadyHasEllipsis)
    Insertion = FixItHint::CreateInsertion(CorrectLoc, "...");
  Diag(EllipsisLoc, diag::err_misplaced_ellipsis_in_declaration)
      << FixItHint::CreateRemoval(EllipsisLoc) << Insertion
      << !IdentifierHasName;
}

void Parser::DiagnoseMisplacedEllipsisInDeclarator(SourceLocation EllipsisLoc,
                                                   Declarator &D) {
  assert(EllipsisLoc.isValid());
  bool AlreadyHasEllipsis = D.getEllipsisLoc().isValid();
  if (!AlreadyHasEllipsis)
    D.setEllipsisLoc(EllipsisLoc);
  DiagnoseMisplacedEllipsis(EllipsisLoc, D.getIdentifierLoc(),
                            AlreadyHasEllipsis, D.hasName());
}

/// Parses a '>' at the end of a template list.
///
/// If this function encounters '>>', '>>>', '>=', or '>>=', it tries
/// to determine if these tokens were supposed to be a '>' followed by
/// '>', '>>', '>=', or '>='. It emits an appropriate diagnostic if necessary.
///
/// \param RAngleLoc the location of the consumed '>'.
///
/// \param ConsumeLastToken if true, the '>' is consumed.
///
/// \param ObjCGenericList if true, this is the '>' closing an Objective-C
/// type parameter or type argument list, rather than a C++ template parameter
/// or argument list.
///
/// \returns true, if current token does not start with '>', false otherwise.
bool Parser::ParseGreaterThanInTemplateList(SourceLocation &RAngleLoc,
                                            bool ConsumeLastToken,
                                            bool ObjCGenericList) {
  // What will be left once we've consumed the '>'.
  tok::TokenKind RemainingToken;
  const char *ReplacementStr = "> >";
  bool MergeWithNextToken = false;

  switch (Tok.getKind()) {
  default:
    Diag(Tok.getLocation(), diag::err_expected) << tok::greater;
    return true;

  case tok::greater:
    // Determine the location of the '>' token. Only consume this token
    // if the caller asked us to.
    RAngleLoc = Tok.getLocation();
    if (ConsumeLastToken)
      ConsumeToken();
    return false;

  case tok::greatergreater:
    RemainingToken = tok::greater;
    break;

  case tok::greatergreatergreater:
    RemainingToken = tok::greatergreater;
    break;

  case tok::greaterequal:
    RemainingToken = tok::equal;
    ReplacementStr = "> =";

    // Join two adjacent '=' tokens into one, for cases like:
    //   void (*p)() = f<int>;
    //   return f<int>==p;
    if (NextToken().is(tok::equal) &&
        areTokensAdjacent(Tok, NextToken())) {
      RemainingToken = tok::equalequal;
      MergeWithNextToken = true;
    }
    break;

  case tok::greatergreaterequal:
    RemainingToken = tok::greaterequal;
    break;
  }

  // This template-id is terminated by a token that starts with a '>'.
  // Outside C++11 and Objective-C, this is now error recovery.
  //
  // C++11 allows this when the token is '>>', and in CUDA + C++11 mode, we
  // extend that treatment to also apply to the '>>>' token.
  //
  // Objective-C allows this in its type parameter / argument lists.

  SourceLocation TokBeforeGreaterLoc = PrevTokLocation;
  SourceLocation TokLoc = Tok.getLocation();
  Token Next = NextToken();

  // Whether splitting the current token after the '>' would undesirably result
  // in the remaining token pasting with the token after it. This excludes the
  // MergeWithNextToken cases, which we've already handled.
  bool PreventMergeWithNextToken =
      (RemainingToken == tok::greater ||
       RemainingToken == tok::greatergreater) &&
      (Next.isOneOf(tok::greater, tok::greatergreater,
                    tok::greatergreatergreater, tok::equal, tok::greaterequal,
                    tok::greatergreaterequal, tok::equalequal)) &&
      areTokensAdjacent(Tok, Next);

  // Diagnose this situation as appropriate.
  if (!ObjCGenericList) {
    // The source range of the replaced token(s).
    CharSourceRange ReplacementRange = CharSourceRange::getCharRange(
        TokLoc, Lexer::AdvanceToTokenCharacter(TokLoc, 2, PP.getSourceManager(),
                                               getLangOpts()));

    // A hint to put a space between the '>>'s. In order to make the hint as
    // clear as possible, we include the characters either side of the space in
    // the replacement, rather than just inserting a space at SecondCharLoc.
    FixItHint Hint1 = FixItHint::CreateReplacement(ReplacementRange,
                                                   ReplacementStr);

    // A hint to put another space after the token, if it would otherwise be
    // lexed differently.
    FixItHint Hint2;
    if (PreventMergeWithNextToken)
      Hint2 = FixItHint::CreateInsertion(Next.getLocation(), " ");

    unsigned DiagId = diag::err_two_right_angle_brackets_need_space;
    if (getLangOpts().CPlusPlus11 &&
        (Tok.is(tok::greatergreater) || Tok.is(tok::greatergreatergreater)))
      DiagId = diag::warn_cxx98_compat_two_right_angle_brackets;
    else if (Tok.is(tok::greaterequal))
      DiagId = diag::err_right_angle_bracket_equal_needs_space;
    Diag(TokLoc, DiagId) << Hint1 << Hint2;
  }

  // Find the "length" of the resulting '>' token. This is not always 1, as it
  // can contain escaped newlines.
  unsigned GreaterLength = Lexer::getTokenPrefixLength(
      TokLoc, 1, PP.getSourceManager(), getLangOpts());

  // Annotate the source buffer to indicate that we split the token after the
  // '>'. This allows us to properly find the end of, and extract the spelling
  // of, the '>' token later.
  RAngleLoc = PP.SplitToken(TokLoc, GreaterLength);

  // Strip the initial '>' from the token.
  bool CachingTokens = PP.IsPreviousCachedToken(Tok);

  Token Greater = Tok;
  Greater.setLocation(RAngleLoc);
  Greater.setKind(tok::greater);
  Greater.setLength(GreaterLength);

  unsigned OldLength = Tok.getLength();
  if (MergeWithNextToken) {
    ConsumeToken();
    OldLength += Tok.getLength();
  }

  Tok.setKind(RemainingToken);
  Tok.setLength(OldLength - GreaterLength);

  // Split the second token if lexing it normally would lex a different token
  // (eg, the fifth token in 'A<B>>>' should re-lex as '>', not '>>').
  SourceLocation AfterGreaterLoc = TokLoc.getLocWithOffset(GreaterLength);
  if (PreventMergeWithNextToken)
    AfterGreaterLoc = PP.SplitToken(AfterGreaterLoc, Tok.getLength());
  Tok.setLocation(AfterGreaterLoc);

  // Update the token cache to match what we just did if necessary.
  if (CachingTokens) {
    // If the previous cached token is being merged, delete it.
    if (MergeWithNextToken)
      PP.ReplacePreviousCachedToken({});

    if (ConsumeLastToken)
      PP.ReplacePreviousCachedToken({Greater, Tok});
    else
      PP.ReplacePreviousCachedToken({Greater});
  }

  if (ConsumeLastToken) {
    PrevTokLocation = RAngleLoc;
  } else {
    PrevTokLocation = TokBeforeGreaterLoc;
    PP.EnterToken(Tok);
    Tok = Greater;
  }

  return false;
}


/// Parses a template-id that after the template name has
/// already been parsed.
///
/// This routine takes care of parsing the enclosed template argument
/// list ('<' template-parameter-list [opt] '>') and placing the
/// results into a form that can be transferred to semantic analysis.
///
/// \param ConsumeLastToken if true, then we will consume the last
/// token that forms the template-id. Otherwise, we will leave the
/// last token in the stream (e.g., so that it can be replaced with an
/// annotation token).
bool
Parser::ParseTemplateIdAfterTemplateName(bool ConsumeLastToken,
                                         SourceLocation &LAngleLoc,
                                         TemplateArgList &TemplateArgs,
                                         SourceLocation &RAngleLoc) {
  assert(Tok.is(tok::less) && "Must have already parsed the template-name");

  // Consume the '<'.
  LAngleLoc = ConsumeToken();

  // Parse the optional template-argument-list.
  bool Invalid = false;
  {
    GreaterThanIsOperatorScope G(GreaterThanIsOperator, false);
    if (!Tok.isOneOf(tok::greater, tok::greatergreater,
                     tok::greatergreatergreater, tok::greaterequal,
                     tok::greatergreaterequal))
      Invalid = ParseTemplateArgumentList(TemplateArgs);

    if (Invalid) {
      // Try to find the closing '>'.
      if (ConsumeLastToken)
        SkipUntil(tok::greater, StopAtSemi);
      else
        SkipUntil(tok::greater, StopAtSemi | StopBeforeMatch);
      return true;
    }
  }

  return ParseGreaterThanInTemplateList(RAngleLoc, ConsumeLastToken,
                                        /*ObjCGenericList=*/false);
}

/// Replace the tokens that form a simple-template-id with an
/// annotation token containing the complete template-id.
///
/// The first token in the stream must be the name of a template that
/// is followed by a '<'. This routine will parse the complete
/// simple-template-id and replace the tokens with a single annotation
/// token with one of two different kinds: if the template-id names a
/// type (and \p AllowTypeAnnotation is true), the annotation token is
/// a type annotation that includes the optional nested-name-specifier
/// (\p SS). Otherwise, the annotation token is a template-id
/// annotation that does not include the optional
/// nested-name-specifier.
///
/// \param Template  the declaration of the template named by the first
/// token (an identifier), as returned from \c Action::isTemplateName().
///
/// \param TNK the kind of template that \p Template
/// refers to, as returned from \c Action::isTemplateName().
///
/// \param SS if non-NULL, the nested-name-specifier that precedes
/// this template name.
///
/// \param TemplateKWLoc if valid, specifies that this template-id
/// annotation was preceded by the 'template' keyword and gives the
/// location of that keyword. If invalid (the default), then this
/// template-id was not preceded by a 'template' keyword.
///
/// \param AllowTypeAnnotation if true (the default), then a
/// simple-template-id that refers to a class template, template
/// template parameter, or other template that produces a type will be
/// replaced with a type annotation token. Otherwise, the
/// simple-template-id is always replaced with a template-id
/// annotation token.
///
/// If an unrecoverable parse error occurs and no annotation token can be
/// formed, this function returns true.
///
bool Parser::AnnotateTemplateIdToken(TemplateTy Template, TemplateNameKind TNK,
                                     CXXScopeSpec &SS,
                                     SourceLocation TemplateKWLoc,
                                     UnqualifiedId &TemplateName,
                                     bool AllowTypeAnnotation) {
  assert(getLangOpts().CPlusPlus && "Can only annotate template-ids in C++");
  assert(Template && Tok.is(tok::less) &&
         "Parser isn't at the beginning of a template-id");

  // Consume the template-name.
  SourceLocation TemplateNameLoc = TemplateName.getSourceRange().getBegin();

  // Parse the enclosed template argument list.
  SourceLocation LAngleLoc, RAngleLoc;
  TemplateArgList TemplateArgs;
  bool Invalid = ParseTemplateIdAfterTemplateName(false, LAngleLoc,
                                                  TemplateArgs,
                                                  RAngleLoc);

  if (Invalid) {
    // If we failed to parse the template ID but skipped ahead to a >, we're not
    // going to be able to form a token annotation.  Eat the '>' if present.
    TryConsumeToken(tok::greater);
    return true;
  }

  ASTTemplateArgsPtr TemplateArgsPtr(TemplateArgs);

  // Build the annotation token.
  if (TNK == TNK_Type_template && AllowTypeAnnotation) {
    TypeResult Type = Actions.ActOnTemplateIdType(
        SS, TemplateKWLoc, Template, TemplateName.Identifier,
        TemplateNameLoc, LAngleLoc, TemplateArgsPtr, RAngleLoc);
    if (Type.isInvalid()) {
      // If we failed to parse the template ID but skipped ahead to a >, we're
      // not going to be able to form a token annotation.  Eat the '>' if
      // present.
      TryConsumeToken(tok::greater);
      return true;
    }

    Tok.setKind(tok::annot_typename);
    setTypeAnnotation(Tok, Type.get());
    if (SS.isNotEmpty())
      Tok.setLocation(SS.getBeginLoc());
    else if (TemplateKWLoc.isValid())
      Tok.setLocation(TemplateKWLoc);
    else
      Tok.setLocation(TemplateNameLoc);
  } else {
    // Build a template-id annotation token that can be processed
    // later.
    Tok.setKind(tok::annot_template_id);

    IdentifierInfo *TemplateII =
        TemplateName.getKind() == UnqualifiedIdKind::IK_Identifier
            ? TemplateName.Identifier
            : nullptr;

    OverloadedOperatorKind OpKind =
        TemplateName.getKind() == UnqualifiedIdKind::IK_Identifier
            ? OO_None
            : TemplateName.OperatorFunctionId.Operator;

    TemplateIdAnnotation *TemplateId = TemplateIdAnnotation::Create(
      SS, TemplateKWLoc, TemplateNameLoc, TemplateII, OpKind, Template, TNK,
      LAngleLoc, RAngleLoc, TemplateArgs, TemplateIds);

    Tok.setAnnotationValue(TemplateId);
    if (TemplateKWLoc.isValid())
      Tok.setLocation(TemplateKWLoc);
    else
      Tok.setLocation(TemplateNameLoc);
  }

  // Common fields for the annotation token
  Tok.setAnnotationEndLoc(RAngleLoc);

  // In case the tokens were cached, have Preprocessor replace them with the
  // annotation token.
  PP.AnnotateCachedTokens(Tok);
  return false;
}

/// Replaces a template-id annotation token with a type
/// annotation token.
///
/// If there was a failure when forming the type from the template-id,
/// a type annotation token will still be created, but will have a
/// NULL type pointer to signify an error.
///
/// \param IsClassName Is this template-id appearing in a context where we
/// know it names a class, such as in an elaborated-type-specifier or
/// base-specifier? ('typename' and 'template' are unneeded and disallowed
/// in those contexts.)
void Parser::AnnotateTemplateIdTokenAsType(bool IsClassName) {
  assert(Tok.is(tok::annot_template_id) && "Requires template-id tokens");

  TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
  assert((TemplateId->Kind == TNK_Type_template ||
          TemplateId->Kind == TNK_Dependent_template_name) &&
         "Only works for type and dependent templates");

  ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                     TemplateId->NumArgs);

  TypeResult Type
    = Actions.ActOnTemplateIdType(TemplateId->SS,
                                  TemplateId->TemplateKWLoc,
                                  TemplateId->Template,
                                  TemplateId->Name,
                                  TemplateId->TemplateNameLoc,
                                  TemplateId->LAngleLoc,
                                  TemplateArgsPtr,
                                  TemplateId->RAngleLoc,
                                  /*IsCtorOrDtorName*/false,
                                  IsClassName);
  // Create the new "type" annotation token.
  Tok.setKind(tok::annot_typename);
  setTypeAnnotation(Tok, Type.isInvalid() ? nullptr : Type.get());
  if (TemplateId->SS.isNotEmpty()) // it was a C++ qualified type name.
    Tok.setLocation(TemplateId->SS.getBeginLoc());
  // End location stays the same

  // Replace the template-id annotation token, and possible the scope-specifier
  // that precedes it, with the typename annotation token.
  PP.AnnotateCachedTokens(Tok);
}

/// Determine whether the given token can end a template argument.
static bool isEndOfTemplateArgument(Token Tok) {
  return Tok.isOneOf(tok::comma, tok::greater, tok::greatergreater);
}

/// Parse a C++ template template argument.
ParsedTemplateArgument Parser::ParseTemplateTemplateArgument() {
  if (!Tok.is(tok::identifier) && !Tok.is(tok::coloncolon) &&
      !Tok.is(tok::annot_cxxscope))
    return ParsedTemplateArgument();

  // C++0x [temp.arg.template]p1:
  //   A template-argument for a template template-parameter shall be the name
  //   of a class template or an alias template, expressed as id-expression.
  //
  // We parse an id-expression that refers to a class template or alias
  // template. The grammar we parse is:
  //
  //   nested-name-specifier[opt] template[opt] identifier ...[opt]
  //
  // followed by a token that terminates a template argument, such as ',',
  // '>', or (in some cases) '>>'.
  CXXScopeSpec SS; // nested-name-specifier, if present
  ParseOptionalCXXScopeSpecifier(SS, nullptr,
                                 /*EnteringContext=*/false);

  ParsedTemplateArgument Result;
  SourceLocation EllipsisLoc;
  if (SS.isSet() && Tok.is(tok::kw_template)) {
    // Parse the optional 'template' keyword following the
    // nested-name-specifier.
    SourceLocation TemplateKWLoc = ConsumeToken();

    if (Tok.is(tok::identifier)) {
      // We appear to have a dependent template name.
      UnqualifiedId Name;
      Name.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
      ConsumeToken(); // the identifier

      TryConsumeToken(tok::ellipsis, EllipsisLoc);

      // If the next token signals the end of a template argument,
      // then we have a dependent template name that could be a template
      // template argument.
      TemplateTy Template;
      if (isEndOfTemplateArgument(Tok) &&
          Actions.ActOnDependentTemplateName(
              getCurScope(), SS, TemplateKWLoc, Name,
              /*ObjectType=*/nullptr,
              /*EnteringContext=*/false, Template))
        Result = ParsedTemplateArgument(SS, Template, Name.StartLocation);
    }
  } else if (Tok.is(tok::identifier)) {
    // We may have a (non-dependent) template name.
    TemplateTy Template;
    UnqualifiedId Name;
    Name.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
    ConsumeToken(); // the identifier

    TryConsumeToken(tok::ellipsis, EllipsisLoc);

    if (isEndOfTemplateArgument(Tok)) {
      bool MemberOfUnknownSpecialization;
      TemplateNameKind TNK = Actions.isTemplateName(
          getCurScope(), SS,
          /*hasTemplateKeyword=*/false, Name,
          /*ObjectType=*/nullptr,
          /*EnteringContext=*/false, Template, MemberOfUnknownSpecialization);
      if (TNK == TNK_Dependent_template_name || TNK == TNK_Type_template) {
        // We have an id-expression that refers to a class template or
        // (C++0x) alias template.
        Result = ParsedTemplateArgument(SS, Template, Name.StartLocation);
      }
    }
  }

  // If this is a pack expansion, build it as such.
  if (EllipsisLoc.isValid() && !Result.isInvalid())
    Result = Actions.ActOnPackExpansion(Result, EllipsisLoc);

  return Result;
}

/// ParseTemplateArgument - Parse a C++ template argument (C++ [temp.names]).
///
///       template-argument: [C++ 14.2]
///         constant-expression
///         type-id
///         id-expression
ParsedTemplateArgument Parser::ParseTemplateArgument() {
  // C++ [temp.arg]p2:
  //   In a template-argument, an ambiguity between a type-id and an
  //   expression is resolved to a type-id, regardless of the form of
  //   the corresponding template-parameter.
  //
  // Therefore, we initially try to parse a type-id - and isCXXTypeId might look
  // up and annotate an identifier as an id-expression during disambiguation,
  // so enter the appropriate context for a constant expression template
  // argument before trying to disambiguate.

  EnterExpressionEvaluationContext EnterConstantEvaluated(
    Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated,
    /*LambdaContextDecl=*/nullptr,
    /*ExprContext=*/Sema::ExpressionEvaluationContextRecord::EK_TemplateArgument);
  if (isCXXTypeId(TypeIdAsTemplateArgument)) {
    TypeResult TypeArg = ParseTypeName(
        /*Range=*/nullptr, DeclaratorContext::TemplateArgContext);
    return Actions.ActOnTemplateTypeArgument(TypeArg);
  }

  // Try to parse a template template argument.
  {
    TentativeParsingAction TPA(*this);

    ParsedTemplateArgument TemplateTemplateArgument
      = ParseTemplateTemplateArgument();
    if (!TemplateTemplateArgument.isInvalid()) {
      TPA.Commit();
      return TemplateTemplateArgument;
    }

    // Revert this tentative parse to parse a non-type template argument.
    TPA.Revert();
  }

  // Parse a non-type template argument.
  SourceLocation Loc = Tok.getLocation();
  ExprResult ExprArg = ParseConstantExpressionInExprEvalContext(MaybeTypeCast);
  if (ExprArg.isInvalid() || !ExprArg.get())
    return ParsedTemplateArgument();

  return ParsedTemplateArgument(ParsedTemplateArgument::NonType,
                                ExprArg.get(), Loc);
}

/// Determine whether the current tokens can only be parsed as a
/// template argument list (starting with the '<') and never as a '<'
/// expression.
bool Parser::IsTemplateArgumentList(unsigned Skip) {
  struct AlwaysRevertAction : TentativeParsingAction {
    AlwaysRevertAction(Parser &P) : TentativeParsingAction(P) { }
    ~AlwaysRevertAction() { Revert(); }
  } Tentative(*this);

  while (Skip) {
    ConsumeAnyToken();
    --Skip;
  }

  // '<'
  if (!TryConsumeToken(tok::less))
    return false;

  // An empty template argument list.
  if (Tok.is(tok::greater))
    return true;

  // See whether we have declaration specifiers, which indicate a type.
  while (isCXXDeclarationSpecifier() == TPResult::True)
    ConsumeAnyToken();

  // If we have a '>' or a ',' then this is a template argument list.
  return Tok.isOneOf(tok::greater, tok::comma);
}

/// ParseTemplateArgumentList - Parse a C++ template-argument-list
/// (C++ [temp.names]). Returns true if there was an error.
///
///       template-argument-list: [C++ 14.2]
///         template-argument
///         template-argument-list ',' template-argument
bool
Parser::ParseTemplateArgumentList(TemplateArgList &TemplateArgs) {

  ColonProtectionRAIIObject ColonProtection(*this, false);

  do {
    ParsedTemplateArgument Arg = ParseTemplateArgument();
    SourceLocation EllipsisLoc;
    if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
      Arg = Actions.ActOnPackExpansion(Arg, EllipsisLoc);

    if (Arg.isInvalid()) {
      SkipUntil(tok::comma, tok::greater, StopAtSemi | StopBeforeMatch);
      return true;
    }

    // Save this template argument.
    TemplateArgs.push_back(Arg);

    // If the next token is a comma, consume it and keep reading
    // arguments.
  } while (TryConsumeToken(tok::comma));

  return false;
}

/// Parse a C++ explicit template instantiation
/// (C++ [temp.explicit]).
///
///       explicit-instantiation:
///         'extern' [opt] 'template' declaration
///
/// Note that the 'extern' is a GNU extension and C++11 feature.
Decl *Parser::ParseExplicitInstantiation(DeclaratorContext Context,
                                         SourceLocation ExternLoc,
                                         SourceLocation TemplateLoc,
                                         SourceLocation &DeclEnd,
                                         ParsedAttributes &AccessAttrs,
                                         AccessSpecifier AS) {
  // This isn't really required here.
  ParsingDeclRAIIObject
    ParsingTemplateParams(*this, ParsingDeclRAIIObject::NoParent);

  return ParseSingleDeclarationAfterTemplate(
      Context, ParsedTemplateInfo(ExternLoc, TemplateLoc),
      ParsingTemplateParams, DeclEnd, AccessAttrs, AS);
}

SourceRange Parser::ParsedTemplateInfo::getSourceRange() const {
  if (TemplateParams)
    return getTemplateParamsRange(TemplateParams->data(),
                                  TemplateParams->size());

  SourceRange R(TemplateLoc);
  if (ExternLoc.isValid())
    R.setBegin(ExternLoc);
  return R;
}

void Parser::LateTemplateParserCallback(void *P, LateParsedTemplate &LPT) {
  ((Parser *)P)->ParseLateTemplatedFuncDef(LPT);
}

/// Late parse a C++ function template in Microsoft mode.
void Parser::ParseLateTemplatedFuncDef(LateParsedTemplate &LPT) {
  if (!LPT.D)
     return;

  // Get the FunctionDecl.
  FunctionDecl *FunD = LPT.D->getAsFunction();
  // Track template parameter depth.
  TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);

  // To restore the context after late parsing.
  Sema::ContextRAII GlobalSavedContext(
      Actions, Actions.Context.getTranslationUnitDecl());

  SmallVector<ParseScope*, 4> TemplateParamScopeStack;

  // Get the list of DeclContexts to reenter. For inline methods, we only want
  // to push the DeclContext of the outermost class. This matches the way the
  // parser normally parses bodies of inline methods when the outermost class is
  // complete.
  struct ContainingDC {
    ContainingDC(DeclContext *DC, bool ShouldPush) : Pair(DC, ShouldPush) {}
    llvm::PointerIntPair<DeclContext *, 1, bool> Pair;
    DeclContext *getDC() { return Pair.getPointer(); }
    bool shouldPushDC() { return Pair.getInt(); }
  };
  SmallVector<ContainingDC, 4> DeclContextsToReenter;
  DeclContext *DD = FunD;
  DeclContext *NextContaining = Actions.getContainingDC(DD);
  while (DD && !DD->isTranslationUnit()) {
    bool ShouldPush = DD == NextContaining;
    DeclContextsToReenter.push_back({DD, ShouldPush});
    if (ShouldPush)
      NextContaining = Actions.getContainingDC(DD);
    DD = DD->getLexicalParent();
  }

  // Reenter template scopes from outermost to innermost.
  for (ContainingDC CDC : reverse(DeclContextsToReenter)) {
    TemplateParamScopeStack.push_back(
        new ParseScope(this, Scope::TemplateParamScope));
    unsigned NumParamLists = Actions.ActOnReenterTemplateScope(
        getCurScope(), cast<Decl>(CDC.getDC()));
    CurTemplateDepthTracker.addDepth(NumParamLists);
    if (CDC.shouldPushDC()) {
      TemplateParamScopeStack.push_back(new ParseScope(this, Scope::DeclScope));
      Actions.PushDeclContext(Actions.getCurScope(), CDC.getDC());
    }
  }

  assert(!LPT.Toks.empty() && "Empty body!");

  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  LPT.Toks.push_back(Tok);
  PP.EnterTokenStream(LPT.Toks, true);

  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
  assert(Tok.isOneOf(tok::l_brace, tok::colon, tok::kw_try) &&
         "Inline method not starting with '{', ':' or 'try'");

  // Parse the method body. Function body parsing code is similar enough
  // to be re-used for method bodies as well.
  ParseScope FnScope(this, Scope::FnScope | Scope::DeclScope |
                               Scope::CompoundStmtScope);

  // Recreate the containing function DeclContext.
  Sema::ContextRAII FunctionSavedContext(Actions,
                                         Actions.getContainingDC(FunD));

  Actions.ActOnStartOfFunctionDef(getCurScope(), FunD);

  if (Tok.is(tok::kw_try)) {
    ParseFunctionTryBlock(LPT.D, FnScope);
  } else {
    if (Tok.is(tok::colon))
      ParseConstructorInitializer(LPT.D);
    else
      Actions.ActOnDefaultCtorInitializers(LPT.D);

    if (Tok.is(tok::l_brace)) {
      assert((!isa<FunctionTemplateDecl>(LPT.D) ||
              cast<FunctionTemplateDecl>(LPT.D)
                      ->getTemplateParameters()
                      ->getDepth() == TemplateParameterDepth - 1) &&
             "TemplateParameterDepth should be greater than the depth of "
             "current template being instantiated!");
      ParseFunctionStatementBody(LPT.D, FnScope);
      Actions.UnmarkAsLateParsedTemplate(FunD);
    } else
      Actions.ActOnFinishFunctionBody(LPT.D, nullptr);
  }

  // Exit scopes.
  FnScope.Exit();
  SmallVectorImpl<ParseScope *>::reverse_iterator I =
   TemplateParamScopeStack.rbegin();
  for (; I != TemplateParamScopeStack.rend(); ++I)
    delete *I;
}

/// Lex a delayed template function for late parsing.
void Parser::LexTemplateFunctionForLateParsing(CachedTokens &Toks) {
  tok::TokenKind kind = Tok.getKind();
  if (!ConsumeAndStoreFunctionPrologue(Toks)) {
    // Consume everything up to (and including) the matching right brace.
    ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
  }

  // If we're in a function-try-block, we need to store all the catch blocks.
  if (kind == tok::kw_try) {
    while (Tok.is(tok::kw_catch)) {
      ConsumeAndStoreUntil(tok::l_brace, Toks, /*StopAtSemi=*/false);
      ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
    }
  }
}

/// We've parsed something that could plausibly be intended to be a template
/// name (\p LHS) followed by a '<' token, and the following code can't possibly
/// be an expression. Determine if this is likely to be a template-id and if so,
/// diagnose it.
bool Parser::diagnoseUnknownTemplateId(ExprResult LHS, SourceLocation Less) {
  TentativeParsingAction TPA(*this);
  // FIXME: We could look at the token sequence in a lot more detail here.
  if (SkipUntil(tok::greater, tok::greatergreater, tok::greatergreatergreater,
                StopAtSemi | StopBeforeMatch)) {
    TPA.Commit();

    SourceLocation Greater;
    ParseGreaterThanInTemplateList(Greater, true, false);
    Actions.diagnoseExprIntendedAsTemplateName(getCurScope(), LHS,
                                               Less, Greater);
    return true;
  }

  // There's no matching '>' token, this probably isn't supposed to be
  // interpreted as a template-id. Parse it as an (ill-formed) comparison.
  TPA.Revert();
  return false;
}

void Parser::checkPotentialAngleBracket(ExprResult &PotentialTemplateName) {
  assert(Tok.is(tok::less) && "not at a potential angle bracket");

  bool DependentTemplateName = false;
  if (!Actions.mightBeIntendedToBeTemplateName(PotentialTemplateName,
                                               DependentTemplateName))
    return;

  // OK, this might be a name that the user intended to be parsed as a
  // template-name, followed by a '<' token. Check for some easy cases.

  // If we have potential_template<>, then it's supposed to be a template-name.
  if (NextToken().is(tok::greater) ||
      (getLangOpts().CPlusPlus11 &&
       NextToken().isOneOf(tok::greatergreater, tok::greatergreatergreater))) {
    SourceLocation Less = ConsumeToken();
    SourceLocation Greater;
    ParseGreaterThanInTemplateList(Greater, true, false);
    Actions.diagnoseExprIntendedAsTemplateName(
        getCurScope(), PotentialTemplateName, Less, Greater);
    // FIXME: Perform error recovery.
    PotentialTemplateName = ExprError();
    return;
  }

  // If we have 'potential_template<type-id', assume it's supposed to be a
  // template-name if there's a matching '>' later on.
  {
    // FIXME: Avoid the tentative parse when NextToken() can't begin a type.
    TentativeParsingAction TPA(*this);
    SourceLocation Less = ConsumeToken();
    if (isTypeIdUnambiguously() &&
        diagnoseUnknownTemplateId(PotentialTemplateName, Less)) {
      TPA.Commit();
      // FIXME: Perform error recovery.
      PotentialTemplateName = ExprError();
      return;
    }
    TPA.Revert();
  }

  // Otherwise, remember that we saw this in case we see a potentially-matching
  // '>' token later on.
  AngleBracketTracker::Priority Priority =
      (DependentTemplateName ? AngleBracketTracker::DependentName
                             : AngleBracketTracker::PotentialTypo) |
      (Tok.hasLeadingSpace() ? AngleBracketTracker::SpaceBeforeLess
                             : AngleBracketTracker::NoSpaceBeforeLess);
  AngleBrackets.add(*this, PotentialTemplateName.get(), Tok.getLocation(),
                    Priority);
}

bool Parser::checkPotentialAngleBracketDelimiter(
    const AngleBracketTracker::Loc &LAngle, const Token &OpToken) {
  // If a comma in an expression context is followed by a type that can be a
  // template argument and cannot be an expression, then this is ill-formed,
  // but might be intended to be part of a template-id.
  if (OpToken.is(tok::comma) && isTypeIdUnambiguously() &&
      diagnoseUnknownTemplateId(LAngle.TemplateName, LAngle.LessLoc)) {
    AngleBrackets.clear(*this);
    return true;
  }

  // If a context that looks like a template-id is followed by '()', then
  // this is ill-formed, but might be intended to be a template-id
  // followed by '()'.
  if (OpToken.is(tok::greater) && Tok.is(tok::l_paren) &&
      NextToken().is(tok::r_paren)) {
    Actions.diagnoseExprIntendedAsTemplateName(
        getCurScope(), LAngle.TemplateName, LAngle.LessLoc,
        OpToken.getLocation());
    AngleBrackets.clear(*this);
    return true;
  }

  // After a '>' (etc), we're no longer potentially in a construct that's
  // intended to be treated as a template-id.
  if (OpToken.is(tok::greater) ||
      (getLangOpts().CPlusPlus11 &&
       OpToken.isOneOf(tok::greatergreater, tok::greatergreatergreater)))
    AngleBrackets.clear(*this);
  return false;
}
