//===--- TokenAnnotator.cpp - Format C++ code -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a token annotator, i.e. creates
/// \c AnnotatedTokens out of \c FormatTokens with required extra information.
///
//===----------------------------------------------------------------------===//

#include "TokenAnnotator.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "format-token-annotator"

namespace clang {
namespace format {

namespace {

/// Returns \c true if the token can be used as an identifier in
/// an Objective-C \c @selector, \c false otherwise.
///
/// Because getFormattingLangOpts() always lexes source code as
/// Objective-C++, C++ keywords like \c new and \c delete are
/// lexed as tok::kw_*, not tok::identifier, even for Objective-C.
///
/// For Objective-C and Objective-C++, both identifiers and keywords
/// are valid inside @selector(...) (or a macro which
/// invokes @selector(...)). So, we allow treat any identifier or
/// keyword as a potential Objective-C selector component.
static bool canBeObjCSelectorComponent(const FormatToken &Tok) {
  return Tok.Tok.getIdentifierInfo() != nullptr;
}

/// A parser that gathers additional information about tokens.
///
/// The \c TokenAnnotator tries to match parenthesis and square brakets and
/// store a parenthesis levels. It also tries to resolve matching "<" and ">"
/// into template parameter lists.
class AnnotatingParser {
public:
  AnnotatingParser(const FormatStyle &Style, AnnotatedLine &Line,
                   const AdditionalKeywords &Keywords)
      : Style(Style), Line(Line), CurrentToken(Line.First), AutoFound(false),
        Keywords(Keywords) {
    Contexts.push_back(Context(tok::unknown, 1, /*IsExpression=*/false));
    resetTokenMetadata(CurrentToken);
  }

private:
  bool parseAngle() {
    if (!CurrentToken || !CurrentToken->Previous)
      return false;
    if (NonTemplateLess.count(CurrentToken->Previous))
      return false;

    const FormatToken &Previous = *CurrentToken->Previous;  // The '<'.
    if (Previous.Previous) {
      if (Previous.Previous->Tok.isLiteral())
        return false;
      if (Previous.Previous->is(tok::r_paren) && Contexts.size() > 1 &&
          (!Previous.Previous->MatchingParen ||
           !Previous.Previous->MatchingParen->is(TT_OverloadedOperatorLParen)))
        return false;
    }

    FormatToken *Left = CurrentToken->Previous;
    Left->ParentBracket = Contexts.back().ContextKind;
    ScopedContextCreator ContextCreator(*this, tok::less, 12);

    // If this angle is in the context of an expression, we need to be more
    // hesitant to detect it as opening template parameters.
    bool InExprContext = Contexts.back().IsExpression;

    Contexts.back().IsExpression = false;
    // If there's a template keyword before the opening angle bracket, this is a
    // template parameter, not an argument.
    Contexts.back().InTemplateArgument =
        Left->Previous && Left->Previous->Tok.isNot(tok::kw_template);

    if (Style.Language == FormatStyle::LK_Java &&
        CurrentToken->is(tok::question))
      next();

    while (CurrentToken) {
      if (CurrentToken->is(tok::greater)) {
        Left->MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = Left;
        // In TT_Proto, we must distignuish between:
        //   map<key, value>
        //   msg < item: data >
        //   msg: < item: data >
        // In TT_TextProto, map<key, value> does not occur.
        if (Style.Language == FormatStyle::LK_TextProto ||
            (Style.Language == FormatStyle::LK_Proto && Left->Previous &&
             Left->Previous->isOneOf(TT_SelectorName, TT_DictLiteral)))
          CurrentToken->Type = TT_DictLiteral;
        else
          CurrentToken->Type = TT_TemplateCloser;
        next();
        return true;
      }
      if (CurrentToken->is(tok::question) &&
          Style.Language == FormatStyle::LK_Java) {
        next();
        continue;
      }
      if (CurrentToken->isOneOf(tok::r_paren, tok::r_square, tok::r_brace) ||
          (CurrentToken->isOneOf(tok::colon, tok::question) && InExprContext &&
           Style.Language != FormatStyle::LK_Proto &&
           Style.Language != FormatStyle::LK_TextProto))
        return false;
      // If a && or || is found and interpreted as a binary operator, this set
      // of angles is likely part of something like "a < b && c > d". If the
      // angles are inside an expression, the ||/&& might also be a binary
      // operator that was misinterpreted because we are parsing template
      // parameters.
      // FIXME: This is getting out of hand, write a decent parser.
      if (CurrentToken->Previous->isOneOf(tok::pipepipe, tok::ampamp) &&
          CurrentToken->Previous->is(TT_BinaryOperator) &&
          Contexts[Contexts.size() - 2].IsExpression &&
          !Line.startsWith(tok::kw_template))
        return false;
      updateParameterCount(Left, CurrentToken);
      if (Style.Language == FormatStyle::LK_Proto) {
        if (FormatToken *Previous = CurrentToken->getPreviousNonComment()) {
          if (CurrentToken->is(tok::colon) ||
              (CurrentToken->isOneOf(tok::l_brace, tok::less) &&
               Previous->isNot(tok::colon)))
            Previous->Type = TT_SelectorName;
        }
      }
      if (!consumeToken())
        return false;
    }
    return false;
  }

  bool parseParens(bool LookForDecls = false) {
    if (!CurrentToken)
      return false;
    FormatToken *Left = CurrentToken->Previous;
    Left->ParentBracket = Contexts.back().ContextKind;
    ScopedContextCreator ContextCreator(*this, tok::l_paren, 1);

    // FIXME: This is a bit of a hack. Do better.
    Contexts.back().ColonIsForRangeExpr =
        Contexts.size() == 2 && Contexts[0].ColonIsForRangeExpr;

    bool StartsObjCMethodExpr = false;
    if (FormatToken *MaybeSel = Left->Previous) {
      // @selector( starts a selector.
      if (MaybeSel->isObjCAtKeyword(tok::objc_selector) && MaybeSel->Previous &&
          MaybeSel->Previous->is(tok::at)) {
        StartsObjCMethodExpr = true;
      }
    }

    if (Left->is(TT_OverloadedOperatorLParen)) {
      Contexts.back().IsExpression = false;
    } else if (Style.Language == FormatStyle::LK_JavaScript &&
               (Line.startsWith(Keywords.kw_type, tok::identifier) ||
                Line.startsWith(tok::kw_export, Keywords.kw_type,
                                tok::identifier))) {
      // type X = (...);
      // export type X = (...);
      Contexts.back().IsExpression = false;
    } else if (Left->Previous &&
               (Left->Previous->isOneOf(tok::kw_static_assert, tok::kw_decltype,
                                        tok::kw_if, tok::kw_while, tok::l_paren,
                                        tok::comma) ||
                Left->Previous->endsSequence(tok::kw_constexpr, tok::kw_if) ||
                Left->Previous->is(TT_BinaryOperator))) {
      // static_assert, if and while usually contain expressions.
      Contexts.back().IsExpression = true;
    } else if (Style.Language == FormatStyle::LK_JavaScript && Left->Previous &&
               (Left->Previous->is(Keywords.kw_function) ||
                (Left->Previous->endsSequence(tok::identifier,
                                              Keywords.kw_function)))) {
      // function(...) or function f(...)
      Contexts.back().IsExpression = false;
    } else if (Style.Language == FormatStyle::LK_JavaScript && Left->Previous &&
               Left->Previous->is(TT_JsTypeColon)) {
      // let x: (SomeType);
      Contexts.back().IsExpression = false;
    } else if (Left->Previous && Left->Previous->is(tok::r_square) &&
               Left->Previous->MatchingParen &&
               Left->Previous->MatchingParen->is(TT_LambdaLSquare)) {
      // This is a parameter list of a lambda expression.
      Contexts.back().IsExpression = false;
    } else if (Line.InPPDirective &&
               (!Left->Previous || !Left->Previous->is(tok::identifier))) {
      Contexts.back().IsExpression = true;
    } else if (Contexts[Contexts.size() - 2].CaretFound) {
      // This is the parameter list of an ObjC block.
      Contexts.back().IsExpression = false;
    } else if (Left->Previous && Left->Previous->is(tok::kw___attribute)) {
      Left->Type = TT_AttributeParen;
    } else if (Left->Previous && Left->Previous->is(TT_ForEachMacro)) {
      // The first argument to a foreach macro is a declaration.
      Contexts.back().IsForEachMacro = true;
      Contexts.back().IsExpression = false;
    } else if (Left->Previous && Left->Previous->MatchingParen &&
               Left->Previous->MatchingParen->is(TT_ObjCBlockLParen)) {
      Contexts.back().IsExpression = false;
    } else if (!Line.MustBeDeclaration && !Line.InPPDirective) {
      bool IsForOrCatch =
          Left->Previous && Left->Previous->isOneOf(tok::kw_for, tok::kw_catch);
      Contexts.back().IsExpression = !IsForOrCatch;
    }

    if (StartsObjCMethodExpr) {
      Contexts.back().ColonIsObjCMethodExpr = true;
      Left->Type = TT_ObjCMethodExpr;
    }

    // MightBeFunctionType and ProbablyFunctionType are used for
    // function pointer and reference types as well as Objective-C
    // block types:
    //
    // void (*FunctionPointer)(void);
    // void (&FunctionReference)(void);
    // void (^ObjCBlock)(void);
    bool MightBeFunctionType = !Contexts[Contexts.size() - 2].IsExpression;
    bool ProbablyFunctionType =
        CurrentToken->isOneOf(tok::star, tok::amp, tok::caret);
    bool HasMultipleLines = false;
    bool HasMultipleParametersOnALine = false;
    bool MightBeObjCForRangeLoop =
        Left->Previous && Left->Previous->is(tok::kw_for);
    FormatToken *PossibleObjCForInToken = nullptr;
    while (CurrentToken) {
      // LookForDecls is set when "if (" has been seen. Check for
      // 'identifier' '*' 'identifier' followed by not '=' -- this
      // '*' has to be a binary operator but determineStarAmpUsage() will
      // categorize it as an unary operator, so set the right type here.
      if (LookForDecls && CurrentToken->Next) {
        FormatToken *Prev = CurrentToken->getPreviousNonComment();
        if (Prev) {
          FormatToken *PrevPrev = Prev->getPreviousNonComment();
          FormatToken *Next = CurrentToken->Next;
          if (PrevPrev && PrevPrev->is(tok::identifier) &&
              Prev->isOneOf(tok::star, tok::amp, tok::ampamp) &&
              CurrentToken->is(tok::identifier) && Next->isNot(tok::equal)) {
            Prev->Type = TT_BinaryOperator;
            LookForDecls = false;
          }
        }
      }

      if (CurrentToken->Previous->is(TT_PointerOrReference) &&
          CurrentToken->Previous->Previous->isOneOf(tok::l_paren,
                                                    tok::coloncolon))
        ProbablyFunctionType = true;
      if (CurrentToken->is(tok::comma))
        MightBeFunctionType = false;
      if (CurrentToken->Previous->is(TT_BinaryOperator))
        Contexts.back().IsExpression = true;
      if (CurrentToken->is(tok::r_paren)) {
        if (MightBeFunctionType && ProbablyFunctionType && CurrentToken->Next &&
            (CurrentToken->Next->is(tok::l_paren) ||
             (CurrentToken->Next->is(tok::l_square) && Line.MustBeDeclaration)))
          Left->Type = Left->Next->is(tok::caret) ? TT_ObjCBlockLParen
                                                  : TT_FunctionTypeLParen;
        Left->MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = Left;

        if (CurrentToken->Next && CurrentToken->Next->is(tok::l_brace) &&
            Left->Previous && Left->Previous->is(tok::l_paren)) {
          // Detect the case where macros are used to generate lambdas or
          // function bodies, e.g.:
          //   auto my_lambda = MARCO((Type *type, int i) { .. body .. });
          for (FormatToken *Tok = Left; Tok != CurrentToken; Tok = Tok->Next) {
            if (Tok->is(TT_BinaryOperator) &&
                Tok->isOneOf(tok::star, tok::amp, tok::ampamp))
              Tok->Type = TT_PointerOrReference;
          }
        }

        if (StartsObjCMethodExpr) {
          CurrentToken->Type = TT_ObjCMethodExpr;
          if (Contexts.back().FirstObjCSelectorName) {
            Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName =
                Contexts.back().LongestObjCSelectorName;
          }
        }

        if (Left->is(TT_AttributeParen))
          CurrentToken->Type = TT_AttributeParen;
        if (Left->Previous && Left->Previous->is(TT_JavaAnnotation))
          CurrentToken->Type = TT_JavaAnnotation;
        if (Left->Previous && Left->Previous->is(TT_LeadingJavaAnnotation))
          CurrentToken->Type = TT_LeadingJavaAnnotation;

        if (!HasMultipleLines)
          Left->PackingKind = PPK_Inconclusive;
        else if (HasMultipleParametersOnALine)
          Left->PackingKind = PPK_BinPacked;
        else
          Left->PackingKind = PPK_OnePerLine;

        next();
        return true;
      }
      if (CurrentToken->isOneOf(tok::r_square, tok::r_brace))
        return false;

      if (CurrentToken->is(tok::l_brace))
        Left->Type = TT_Unknown; // Not TT_ObjCBlockLParen
      if (CurrentToken->is(tok::comma) && CurrentToken->Next &&
          !CurrentToken->Next->HasUnescapedNewline &&
          !CurrentToken->Next->isTrailingComment())
        HasMultipleParametersOnALine = true;
      if ((CurrentToken->Previous->isOneOf(tok::kw_const, tok::kw_auto) ||
           CurrentToken->Previous->isSimpleTypeSpecifier()) &&
          !CurrentToken->is(tok::l_brace))
        Contexts.back().IsExpression = false;
      if (CurrentToken->isOneOf(tok::semi, tok::colon)) {
        MightBeObjCForRangeLoop = false;
        if (PossibleObjCForInToken) {
          PossibleObjCForInToken->Type = TT_Unknown;
          PossibleObjCForInToken = nullptr;
        }
      }
      if (MightBeObjCForRangeLoop && CurrentToken->is(Keywords.kw_in)) {
        PossibleObjCForInToken = CurrentToken;
        PossibleObjCForInToken->Type = TT_ObjCForIn;
      }
      // When we discover a 'new', we set CanBeExpression to 'false' in order to
      // parse the type correctly. Reset that after a comma.
      if (CurrentToken->is(tok::comma))
        Contexts.back().CanBeExpression = true;

      FormatToken *Tok = CurrentToken;
      if (!consumeToken())
        return false;
      updateParameterCount(Left, Tok);
      if (CurrentToken && CurrentToken->HasUnescapedNewline)
        HasMultipleLines = true;
    }
    return false;
  }

  bool isCpp11AttributeSpecifier(const FormatToken &Tok) {
    if (!Style.isCpp() || !Tok.startsSequence(tok::l_square, tok::l_square))
      return false;
    const FormatToken *AttrTok = Tok.Next->Next;
    if (!AttrTok)
      return false;
    // C++17 '[[using ns: foo, bar(baz, blech)]]'
    // We assume nobody will name an ObjC variable 'using'.
    if (AttrTok->startsSequence(tok::kw_using, tok::identifier, tok::colon))
      return true;
    if (AttrTok->isNot(tok::identifier))
      return false;
    while (AttrTok && !AttrTok->startsSequence(tok::r_square, tok::r_square)) {
      // ObjC message send. We assume nobody will use : in a C++11 attribute
      // specifier parameter, although this is technically valid:
      // [[foo(:)]]
      if (AttrTok->is(tok::colon) ||
          AttrTok->startsSequence(tok::identifier, tok::identifier) || 
          AttrTok->startsSequence(tok::r_paren, tok::identifier))
        return false;
      if (AttrTok->is(tok::ellipsis))
        return true;
      AttrTok = AttrTok->Next;
    }
    return AttrTok && AttrTok->startsSequence(tok::r_square, tok::r_square);
  }

  bool parseSquare() {
    if (!CurrentToken)
      return false;

    // A '[' could be an index subscript (after an identifier or after
    // ')' or ']'), it could be the start of an Objective-C method
    // expression, it could the start of an Objective-C array literal,
    // or it could be a C++ attribute specifier [[foo::bar]].
    FormatToken *Left = CurrentToken->Previous;
    Left->ParentBracket = Contexts.back().ContextKind;
    FormatToken *Parent = Left->getPreviousNonComment();

    // Cases where '>' is followed by '['.
    // In C++, this can happen either in array of templates (foo<int>[10])
    // or when array is a nested template type (unique_ptr<type1<type2>[]>).
    bool CppArrayTemplates =
        Style.isCpp() && Parent && Parent->is(TT_TemplateCloser) &&
        (Contexts.back().CanBeExpression || Contexts.back().IsExpression ||
         Contexts.back().InTemplateArgument);

    bool IsCpp11AttributeSpecifier = isCpp11AttributeSpecifier(*Left) ||
                                     Contexts.back().InCpp11AttributeSpecifier;

    bool InsideInlineASM = Line.startsWith(tok::kw_asm);
    bool StartsObjCMethodExpr =
        !InsideInlineASM && !CppArrayTemplates && Style.isCpp() &&
        !IsCpp11AttributeSpecifier && Contexts.back().CanBeExpression &&
        Left->isNot(TT_LambdaLSquare) &&
        !CurrentToken->isOneOf(tok::l_brace, tok::r_square) &&
        (!Parent ||
         Parent->isOneOf(tok::colon, tok::l_square, tok::l_paren,
                         tok::kw_return, tok::kw_throw) ||
         Parent->isUnaryOperator() ||
         // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
         Parent->isOneOf(TT_ObjCForIn, TT_CastRParen) ||
         getBinOpPrecedence(Parent->Tok.getKind(), true, true) > prec::Unknown);
    bool ColonFound = false;

    unsigned BindingIncrease = 1;
    if (Left->isCppStructuredBinding(Style)) {
      Left->Type = TT_StructuredBindingLSquare;
    } else if (Left->is(TT_Unknown)) {
      if (StartsObjCMethodExpr) {
        Left->Type = TT_ObjCMethodExpr;
      } else if (IsCpp11AttributeSpecifier) {
        Left->Type = TT_AttributeSquare;
      } else if (Style.Language == FormatStyle::LK_JavaScript && Parent &&
                 Contexts.back().ContextKind == tok::l_brace &&
                 Parent->isOneOf(tok::l_brace, tok::comma)) {
        Left->Type = TT_JsComputedPropertyName;
      } else if (Style.isCpp() && Contexts.back().ContextKind == tok::l_brace &&
                 Parent && Parent->isOneOf(tok::l_brace, tok::comma)) {
        Left->Type = TT_DesignatedInitializerLSquare;
      } else if (CurrentToken->is(tok::r_square) && Parent &&
                 Parent->is(TT_TemplateCloser)) {
        Left->Type = TT_ArraySubscriptLSquare;
      } else if (Style.Language == FormatStyle::LK_Proto ||
                 Style.Language == FormatStyle::LK_TextProto) {
        // Square braces in LK_Proto can either be message field attributes:
        //
        // optional Aaa aaa = 1 [
        //   (aaa) = aaa
        // ];
        //
        // extensions 123 [
        //   (aaa) = aaa
        // ];
        //
        // or text proto extensions (in options):
        //
        // option (Aaa.options) = {
        //   [type.type/type] {
        //     key: value
        //   }
        // }
        //
        // or repeated fields (in options):
        //
        // option (Aaa.options) = {
        //   keys: [ 1, 2, 3 ]
        // }
        //
        // In the first and the third case we want to spread the contents inside
        // the square braces; in the second we want to keep them inline.
        Left->Type = TT_ArrayInitializerLSquare;
        if (!Left->endsSequence(tok::l_square, tok::numeric_constant,
                                tok::equal) &&
            !Left->endsSequence(tok::l_square, tok::numeric_constant,
                                tok::identifier) &&
            !Left->endsSequence(tok::l_square, tok::colon, TT_SelectorName)) {
          Left->Type = TT_ProtoExtensionLSquare;
          BindingIncrease = 10;
        }
      } else if (!CppArrayTemplates && Parent &&
                 Parent->isOneOf(TT_BinaryOperator, TT_TemplateCloser, tok::at,
                                 tok::comma, tok::l_paren, tok::l_square,
                                 tok::question, tok::colon, tok::kw_return,
                                 // Should only be relevant to JavaScript:
                                 tok::kw_default)) {
        Left->Type = TT_ArrayInitializerLSquare;
      } else {
        BindingIncrease = 10;
        Left->Type = TT_ArraySubscriptLSquare;
      }
    }

    ScopedContextCreator ContextCreator(*this, tok::l_square, BindingIncrease);
    Contexts.back().IsExpression = true;
    if (Style.Language == FormatStyle::LK_JavaScript && Parent &&
        Parent->is(TT_JsTypeColon))
      Contexts.back().IsExpression = false;

    Contexts.back().ColonIsObjCMethodExpr = StartsObjCMethodExpr;
    Contexts.back().InCpp11AttributeSpecifier = IsCpp11AttributeSpecifier;

    while (CurrentToken) {
      if (CurrentToken->is(tok::r_square)) {
        if (IsCpp11AttributeSpecifier)
          CurrentToken->Type = TT_AttributeSquare;
        else if (CurrentToken->Next && CurrentToken->Next->is(tok::l_paren) &&
                 Left->is(TT_ObjCMethodExpr)) {
          // An ObjC method call is rarely followed by an open parenthesis.
          // FIXME: Do we incorrectly label ":" with this?
          StartsObjCMethodExpr = false;
          Left->Type = TT_Unknown;
        }
        if (StartsObjCMethodExpr && CurrentToken->Previous != Left) {
          CurrentToken->Type = TT_ObjCMethodExpr;
          // If we haven't seen a colon yet, make sure the last identifier
          // before the r_square is tagged as a selector name component.
          if (!ColonFound && CurrentToken->Previous &&
              CurrentToken->Previous->is(TT_Unknown) &&
              canBeObjCSelectorComponent(*CurrentToken->Previous))
            CurrentToken->Previous->Type = TT_SelectorName;
          // determineStarAmpUsage() thinks that '*' '[' is allocating an
          // array of pointers, but if '[' starts a selector then '*' is a
          // binary operator.
          if (Parent && Parent->is(TT_PointerOrReference))
            Parent->Type = TT_BinaryOperator;
        }
        Left->MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = Left;
        // FirstObjCSelectorName is set when a colon is found. This does
        // not work, however, when the method has no parameters.
        // Here, we set FirstObjCSelectorName when the end of the method call is
        // reached, in case it was not set already.
        if (!Contexts.back().FirstObjCSelectorName) {
            FormatToken* Previous = CurrentToken->getPreviousNonComment();
            if (Previous && Previous->is(TT_SelectorName)) {
              Previous->ObjCSelectorNameParts = 1;
              Contexts.back().FirstObjCSelectorName = Previous;
            }
        } else {
          Left->ParameterCount =
              Contexts.back().FirstObjCSelectorName->ObjCSelectorNameParts;
        }
        if (Contexts.back().FirstObjCSelectorName) {
          Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName =
              Contexts.back().LongestObjCSelectorName;
          if (Left->BlockParameterCount > 1)
            Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName = 0;
        }
        next();
        return true;
      }
      if (CurrentToken->isOneOf(tok::r_paren, tok::r_brace))
        return false;
      if (CurrentToken->is(tok::colon)) {
        if (IsCpp11AttributeSpecifier &&
            CurrentToken->endsSequence(tok::colon, tok::identifier,
                                       tok::kw_using)) {
          // Remember that this is a [[using ns: foo]] C++ attribute, so we
          // don't add a space before the colon (unlike other colons).
          CurrentToken->Type = TT_AttributeColon;
        } else if (Left->isOneOf(TT_ArraySubscriptLSquare,
                                 TT_DesignatedInitializerLSquare)) {
          Left->Type = TT_ObjCMethodExpr;
          StartsObjCMethodExpr = true;
          Contexts.back().ColonIsObjCMethodExpr = true;
          if (Parent && Parent->is(tok::r_paren))
            // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
            Parent->Type = TT_CastRParen;
        }
        ColonFound = true;
      }
      if (CurrentToken->is(tok::comma) && Left->is(TT_ObjCMethodExpr) &&
          !ColonFound)
        Left->Type = TT_ArrayInitializerLSquare;
      FormatToken *Tok = CurrentToken;
      if (!consumeToken())
        return false;
      updateParameterCount(Left, Tok);
    }
    return false;
  }

  bool parseBrace() {
    if (CurrentToken) {
      FormatToken *Left = CurrentToken->Previous;
      Left->ParentBracket = Contexts.back().ContextKind;

      if (Contexts.back().CaretFound)
        Left->Type = TT_ObjCBlockLBrace;
      Contexts.back().CaretFound = false;

      ScopedContextCreator ContextCreator(*this, tok::l_brace, 1);
      Contexts.back().ColonIsDictLiteral = true;
      if (Left->BlockKind == BK_BracedInit)
        Contexts.back().IsExpression = true;
      if (Style.Language == FormatStyle::LK_JavaScript && Left->Previous &&
          Left->Previous->is(TT_JsTypeColon))
        Contexts.back().IsExpression = false;

      while (CurrentToken) {
        if (CurrentToken->is(tok::r_brace)) {
          Left->MatchingParen = CurrentToken;
          CurrentToken->MatchingParen = Left;
          next();
          return true;
        }
        if (CurrentToken->isOneOf(tok::r_paren, tok::r_square))
          return false;
        updateParameterCount(Left, CurrentToken);
        if (CurrentToken->isOneOf(tok::colon, tok::l_brace, tok::less)) {
          FormatToken *Previous = CurrentToken->getPreviousNonComment();
          if (Previous->is(TT_JsTypeOptionalQuestion))
            Previous = Previous->getPreviousNonComment();
          if ((CurrentToken->is(tok::colon) &&
               (!Contexts.back().ColonIsDictLiteral || !Style.isCpp())) ||
              Style.Language == FormatStyle::LK_Proto ||
              Style.Language == FormatStyle::LK_TextProto) {
            Left->Type = TT_DictLiteral;
            if (Previous->Tok.getIdentifierInfo() ||
                Previous->is(tok::string_literal))
              Previous->Type = TT_SelectorName;
          }
          if (CurrentToken->is(tok::colon) ||
              Style.Language == FormatStyle::LK_JavaScript)
            Left->Type = TT_DictLiteral;
        }
        if (CurrentToken->is(tok::comma) &&
            Style.Language == FormatStyle::LK_JavaScript)
          Left->Type = TT_DictLiteral;
        if (!consumeToken())
          return false;
      }
    }
    return true;
  }

  void updateParameterCount(FormatToken *Left, FormatToken *Current) {
    // For ObjC methods, the number of parameters is calculated differently as
    // method declarations have a different structure (the parameters are not
    // inside a bracket scope).
    if (Current->is(tok::l_brace) && Current->BlockKind == BK_Block)
      ++Left->BlockParameterCount;
    if (Current->is(tok::comma)) {
      ++Left->ParameterCount;
      if (!Left->Role)
        Left->Role.reset(new CommaSeparatedList(Style));
      Left->Role->CommaFound(Current);
    } else if (Left->ParameterCount == 0 && Current->isNot(tok::comment)) {
      Left->ParameterCount = 1;
    }
  }

  bool parseConditional() {
    while (CurrentToken) {
      if (CurrentToken->is(tok::colon)) {
        CurrentToken->Type = TT_ConditionalExpr;
        next();
        return true;
      }
      if (!consumeToken())
        return false;
    }
    return false;
  }

  bool parseTemplateDeclaration() {
    if (CurrentToken && CurrentToken->is(tok::less)) {
      CurrentToken->Type = TT_TemplateOpener;
      next();
      if (!parseAngle())
        return false;
      if (CurrentToken)
        CurrentToken->Previous->ClosesTemplateDeclaration = true;
      return true;
    }
    return false;
  }

  bool consumeToken() {
    FormatToken *Tok = CurrentToken;
    next();
    switch (Tok->Tok.getKind()) {
    case tok::plus:
    case tok::minus:
      if (!Tok->Previous && Line.MustBeDeclaration)
        Tok->Type = TT_ObjCMethodSpecifier;
      break;
    case tok::colon:
      if (!Tok->Previous)
        return false;
      // Colons from ?: are handled in parseConditional().
      if (Style.Language == FormatStyle::LK_JavaScript) {
        if (Contexts.back().ColonIsForRangeExpr || // colon in for loop
            (Contexts.size() == 1 &&               // switch/case labels
             !Line.First->isOneOf(tok::kw_enum, tok::kw_case)) ||
            Contexts.back().ContextKind == tok::l_paren ||  // function params
            Contexts.back().ContextKind == tok::l_square || // array type
            (!Contexts.back().IsExpression &&
             Contexts.back().ContextKind == tok::l_brace) || // object type
            (Contexts.size() == 1 &&
             Line.MustBeDeclaration)) { // method/property declaration
          Contexts.back().IsExpression = false;
          Tok->Type = TT_JsTypeColon;
          break;
        }
      }
      if (Contexts.back().ColonIsDictLiteral ||
          Style.Language == FormatStyle::LK_Proto ||
          Style.Language == FormatStyle::LK_TextProto) {
        Tok->Type = TT_DictLiteral;
        if (Style.Language == FormatStyle::LK_TextProto) {
          if (FormatToken *Previous = Tok->getPreviousNonComment())
            Previous->Type = TT_SelectorName;
        }
      } else if (Contexts.back().ColonIsObjCMethodExpr ||
                 Line.startsWith(TT_ObjCMethodSpecifier)) {
        Tok->Type = TT_ObjCMethodExpr;
        const FormatToken *BeforePrevious = Tok->Previous->Previous;
        // Ensure we tag all identifiers in method declarations as
        // TT_SelectorName.
        bool UnknownIdentifierInMethodDeclaration =
            Line.startsWith(TT_ObjCMethodSpecifier) &&
            Tok->Previous->is(tok::identifier) && Tok->Previous->is(TT_Unknown);
        if (!BeforePrevious ||
            // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
            !(BeforePrevious->is(TT_CastRParen) ||
              (BeforePrevious->is(TT_ObjCMethodExpr) &&
               BeforePrevious->is(tok::colon))) ||
            BeforePrevious->is(tok::r_square) ||
            Contexts.back().LongestObjCSelectorName == 0 ||
            UnknownIdentifierInMethodDeclaration) {
          Tok->Previous->Type = TT_SelectorName;
          if (!Contexts.back().FirstObjCSelectorName)
            Contexts.back().FirstObjCSelectorName = Tok->Previous;
          else if (Tok->Previous->ColumnWidth >
                   Contexts.back().LongestObjCSelectorName)
            Contexts.back().LongestObjCSelectorName =
                Tok->Previous->ColumnWidth;
          Tok->Previous->ParameterIndex =
              Contexts.back().FirstObjCSelectorName->ObjCSelectorNameParts;
          ++Contexts.back().FirstObjCSelectorName->ObjCSelectorNameParts;
        }
      } else if (Contexts.back().ColonIsForRangeExpr) {
        Tok->Type = TT_RangeBasedForLoopColon;
      } else if (CurrentToken && CurrentToken->is(tok::numeric_constant)) {
        Tok->Type = TT_BitFieldColon;
      } else if (Contexts.size() == 1 &&
                 !Line.First->isOneOf(tok::kw_enum, tok::kw_case)) {
        if (Tok->getPreviousNonComment()->isOneOf(tok::r_paren,
                                                  tok::kw_noexcept))
          Tok->Type = TT_CtorInitializerColon;
        else
          Tok->Type = TT_InheritanceColon;
      } else if (canBeObjCSelectorComponent(*Tok->Previous) && Tok->Next &&
                 (Tok->Next->isOneOf(tok::r_paren, tok::comma) ||
                  (canBeObjCSelectorComponent(*Tok->Next) && Tok->Next->Next &&
                   Tok->Next->Next->is(tok::colon)))) {
        // This handles a special macro in ObjC code where selectors including
        // the colon are passed as macro arguments.
        Tok->Type = TT_ObjCMethodExpr;
      } else if (Contexts.back().ContextKind == tok::l_paren) {
        Tok->Type = TT_InlineASMColon;
      }
      break;
    case tok::pipe:
    case tok::amp:
      // | and & in declarations/type expressions represent union and
      // intersection types, respectively.
      if (Style.Language == FormatStyle::LK_JavaScript &&
          !Contexts.back().IsExpression)
        Tok->Type = TT_JsTypeOperator;
      break;
    case tok::kw_if:
    case tok::kw_while:
      if (Tok->is(tok::kw_if) && CurrentToken &&
          CurrentToken->is(tok::kw_constexpr))
        next();
      if (CurrentToken && CurrentToken->is(tok::l_paren)) {
        next();
        if (!parseParens(/*LookForDecls=*/true))
          return false;
      }
      break;
    case tok::kw_for:
      if (Style.Language == FormatStyle::LK_JavaScript) {
        // x.for and {for: ...}
        if ((Tok->Previous && Tok->Previous->is(tok::period)) ||
            (Tok->Next && Tok->Next->is(tok::colon)))
          break;
        // JS' for await ( ...
        if (CurrentToken && CurrentToken->is(Keywords.kw_await))
          next();
      }
      Contexts.back().ColonIsForRangeExpr = true;
      next();
      if (!parseParens())
        return false;
      break;
    case tok::l_paren:
      // When faced with 'operator()()', the kw_operator handler incorrectly
      // marks the first l_paren as a OverloadedOperatorLParen. Here, we make
      // the first two parens OverloadedOperators and the second l_paren an
      // OverloadedOperatorLParen.
      if (Tok->Previous && Tok->Previous->is(tok::r_paren) &&
          Tok->Previous->MatchingParen &&
          Tok->Previous->MatchingParen->is(TT_OverloadedOperatorLParen)) {
        Tok->Previous->Type = TT_OverloadedOperator;
        Tok->Previous->MatchingParen->Type = TT_OverloadedOperator;
        Tok->Type = TT_OverloadedOperatorLParen;
      }

      if (!parseParens())
        return false;
      if (Line.MustBeDeclaration && Contexts.size() == 1 &&
          !Contexts.back().IsExpression && !Line.startsWith(TT_ObjCProperty) &&
          (!Tok->Previous ||
           !Tok->Previous->isOneOf(tok::kw_decltype, tok::kw___attribute,
                                   TT_LeadingJavaAnnotation)))
        Line.MightBeFunctionDecl = true;
      break;
    case tok::l_square:
      if (!parseSquare())
        return false;
      break;
    case tok::l_brace:
      if (Style.Language == FormatStyle::LK_TextProto) {
        FormatToken *Previous = Tok->getPreviousNonComment();
        if (Previous && Previous->Type != TT_DictLiteral)
          Previous->Type = TT_SelectorName;
      }
      if (!parseBrace())
        return false;
      break;
    case tok::less:
      if (parseAngle()) {
        Tok->Type = TT_TemplateOpener;
        // In TT_Proto, we must distignuish between:
        //   map<key, value>
        //   msg < item: data >
        //   msg: < item: data >
        // In TT_TextProto, map<key, value> does not occur.
        if (Style.Language == FormatStyle::LK_TextProto ||
            (Style.Language == FormatStyle::LK_Proto && Tok->Previous &&
             Tok->Previous->isOneOf(TT_SelectorName, TT_DictLiteral))) {
          Tok->Type = TT_DictLiteral;
          FormatToken *Previous = Tok->getPreviousNonComment();
          if (Previous && Previous->Type != TT_DictLiteral)
            Previous->Type = TT_SelectorName;
        }
      } else {
        Tok->Type = TT_BinaryOperator;
        NonTemplateLess.insert(Tok);
        CurrentToken = Tok;
        next();
      }
      break;
    case tok::r_paren:
    case tok::r_square:
      return false;
    case tok::r_brace:
      // Lines can start with '}'.
      if (Tok->Previous)
        return false;
      break;
    case tok::greater:
      if (Style.Language != FormatStyle::LK_TextProto)
        Tok->Type = TT_BinaryOperator;
      break;
    case tok::kw_operator:
      if (Style.Language == FormatStyle::LK_TextProto ||
          Style.Language == FormatStyle::LK_Proto)
        break;
      while (CurrentToken &&
             !CurrentToken->isOneOf(tok::l_paren, tok::semi, tok::r_paren)) {
        if (CurrentToken->isOneOf(tok::star, tok::amp))
          CurrentToken->Type = TT_PointerOrReference;
        consumeToken();
        if (CurrentToken &&
            CurrentToken->Previous->isOneOf(TT_BinaryOperator, TT_UnaryOperator,
                                            tok::comma))
          CurrentToken->Previous->Type = TT_OverloadedOperator;
      }
      if (CurrentToken) {
        CurrentToken->Type = TT_OverloadedOperatorLParen;
        if (CurrentToken->Previous->is(TT_BinaryOperator))
          CurrentToken->Previous->Type = TT_OverloadedOperator;
      }
      break;
    case tok::question:
      if (Style.Language == FormatStyle::LK_JavaScript && Tok->Next &&
          Tok->Next->isOneOf(tok::semi, tok::comma, tok::colon, tok::r_paren,
                             tok::r_brace)) {
        // Question marks before semicolons, colons, etc. indicate optional
        // types (fields, parameters), e.g.
        //   function(x?: string, y?) {...}
        //   class X { y?; }
        Tok->Type = TT_JsTypeOptionalQuestion;
        break;
      }
      // Declarations cannot be conditional expressions, this can only be part
      // of a type declaration.
      if (Line.MustBeDeclaration && !Contexts.back().IsExpression &&
          Style.Language == FormatStyle::LK_JavaScript)
        break;
      parseConditional();
      break;
    case tok::kw_template:
      parseTemplateDeclaration();
      break;
    case tok::comma:
      if (Contexts.back().InCtorInitializer)
        Tok->Type = TT_CtorInitializerComma;
      else if (Contexts.back().InInheritanceList)
        Tok->Type = TT_InheritanceComma;
      else if (Contexts.back().FirstStartOfName &&
               (Contexts.size() == 1 || Line.startsWith(tok::kw_for))) {
        Contexts.back().FirstStartOfName->PartOfMultiVariableDeclStmt = true;
        Line.IsMultiVariableDeclStmt = true;
      }
      if (Contexts.back().IsForEachMacro)
        Contexts.back().IsExpression = true;
      break;
    case tok::identifier:
      if (Tok->isOneOf(Keywords.kw___has_include,
                       Keywords.kw___has_include_next)) {
        parseHasInclude();
      }
      break;
    default:
      break;
    }
    return true;
  }

  void parseIncludeDirective() {
    if (CurrentToken && CurrentToken->is(tok::less)) {
      next();
      while (CurrentToken) {
        // Mark tokens up to the trailing line comments as implicit string
        // literals.
        if (CurrentToken->isNot(tok::comment) &&
            !CurrentToken->TokenText.startswith("//"))
          CurrentToken->Type = TT_ImplicitStringLiteral;
        next();
      }
    }
  }

  void parseWarningOrError() {
    next();
    // We still want to format the whitespace left of the first token of the
    // warning or error.
    next();
    while (CurrentToken) {
      CurrentToken->Type = TT_ImplicitStringLiteral;
      next();
    }
  }

  void parsePragma() {
    next(); // Consume "pragma".
    if (CurrentToken &&
        CurrentToken->isOneOf(Keywords.kw_mark, Keywords.kw_option)) {
      bool IsMark = CurrentToken->is(Keywords.kw_mark);
      next(); // Consume "mark".
      next(); // Consume first token (so we fix leading whitespace).
      while (CurrentToken) {
        if (IsMark || CurrentToken->Previous->is(TT_BinaryOperator))
          CurrentToken->Type = TT_ImplicitStringLiteral;
        next();
      }
    }
  }

  void parseHasInclude() {
    if (!CurrentToken || !CurrentToken->is(tok::l_paren))
      return;
    next(); // '('
    parseIncludeDirective();
    next(); // ')'
  }

  LineType parsePreprocessorDirective() {
    bool IsFirstToken = CurrentToken->IsFirst;
    LineType Type = LT_PreprocessorDirective;
    next();
    if (!CurrentToken)
      return Type;

    if (Style.Language == FormatStyle::LK_JavaScript && IsFirstToken) {
      // JavaScript files can contain shebang lines of the form:
      // #!/usr/bin/env node
      // Treat these like C++ #include directives.
      while (CurrentToken) {
        // Tokens cannot be comments here.
        CurrentToken->Type = TT_ImplicitStringLiteral;
        next();
      }
      return LT_ImportStatement;
    }

    if (CurrentToken->Tok.is(tok::numeric_constant)) {
      CurrentToken->SpacesRequiredBefore = 1;
      return Type;
    }
    // Hashes in the middle of a line can lead to any strange token
    // sequence.
    if (!CurrentToken->Tok.getIdentifierInfo())
      return Type;
    switch (CurrentToken->Tok.getIdentifierInfo()->getPPKeywordID()) {
    case tok::pp_include:
    case tok::pp_include_next:
    case tok::pp_import:
      next();
      parseIncludeDirective();
      Type = LT_ImportStatement;
      break;
    case tok::pp_error:
    case tok::pp_warning:
      parseWarningOrError();
      break;
    case tok::pp_pragma:
      parsePragma();
      break;
    case tok::pp_if:
    case tok::pp_elif:
      Contexts.back().IsExpression = true;
      parseLine();
      break;
    default:
      break;
    }
    while (CurrentToken) {
      FormatToken *Tok = CurrentToken;
      next();
      if (Tok->is(tok::l_paren))
        parseParens();
      else if (Tok->isOneOf(Keywords.kw___has_include,
                            Keywords.kw___has_include_next))
        parseHasInclude();
    }
    return Type;
  }

public:
  LineType parseLine() {
    NonTemplateLess.clear();
    if (CurrentToken->is(tok::hash))
      return parsePreprocessorDirective();

    // Directly allow to 'import <string-literal>' to support protocol buffer
    // definitions (github.com/google/protobuf) or missing "#" (either way we
    // should not break the line).
    IdentifierInfo *Info = CurrentToken->Tok.getIdentifierInfo();
    if ((Style.Language == FormatStyle::LK_Java &&
         CurrentToken->is(Keywords.kw_package)) ||
        (Info && Info->getPPKeywordID() == tok::pp_import &&
         CurrentToken->Next &&
         CurrentToken->Next->isOneOf(tok::string_literal, tok::identifier,
                                     tok::kw_static))) {
      next();
      parseIncludeDirective();
      return LT_ImportStatement;
    }

    // If this line starts and ends in '<' and '>', respectively, it is likely
    // part of "#define <a/b.h>".
    if (CurrentToken->is(tok::less) && Line.Last->is(tok::greater)) {
      parseIncludeDirective();
      return LT_ImportStatement;
    }

    // In .proto files, top-level options are very similar to import statements
    // and should not be line-wrapped.
    if (Style.Language == FormatStyle::LK_Proto && Line.Level == 0 &&
        CurrentToken->is(Keywords.kw_option)) {
      next();
      if (CurrentToken && CurrentToken->is(tok::identifier))
        return LT_ImportStatement;
    }

    bool KeywordVirtualFound = false;
    bool ImportStatement = false;

    // import {...} from '...';
    if (Style.Language == FormatStyle::LK_JavaScript &&
        CurrentToken->is(Keywords.kw_import))
      ImportStatement = true;

    while (CurrentToken) {
      if (CurrentToken->is(tok::kw_virtual))
        KeywordVirtualFound = true;
      if (Style.Language == FormatStyle::LK_JavaScript) {
        // export {...} from '...';
        // An export followed by "from 'some string';" is a re-export from
        // another module identified by a URI and is treated as a
        // LT_ImportStatement (i.e. prevent wraps on it for long URIs).
        // Just "export {...};" or "export class ..." should not be treated as
        // an import in this sense.
        if (Line.First->is(tok::kw_export) &&
            CurrentToken->is(Keywords.kw_from) && CurrentToken->Next &&
            CurrentToken->Next->isStringLiteral())
          ImportStatement = true;
        if (isClosureImportStatement(*CurrentToken))
          ImportStatement = true;
      }
      if (!consumeToken())
        return LT_Invalid;
    }
    if (KeywordVirtualFound)
      return LT_VirtualFunctionDecl;
    if (ImportStatement)
      return LT_ImportStatement;

    if (Line.startsWith(TT_ObjCMethodSpecifier)) {
      if (Contexts.back().FirstObjCSelectorName)
        Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName =
            Contexts.back().LongestObjCSelectorName;
      return LT_ObjCMethodDecl;
    }

    return LT_Other;
  }

private:
  bool isClosureImportStatement(const FormatToken &Tok) {
    // FIXME: Closure-library specific stuff should not be hard-coded but be
    // configurable.
    return Tok.TokenText == "goog" && Tok.Next && Tok.Next->is(tok::period) &&
           Tok.Next->Next &&
           (Tok.Next->Next->TokenText == "module" ||
            Tok.Next->Next->TokenText == "provide" ||
            Tok.Next->Next->TokenText == "require" ||
            Tok.Next->Next->TokenText == "requireType" ||
            Tok.Next->Next->TokenText == "forwardDeclare") &&
           Tok.Next->Next->Next && Tok.Next->Next->Next->is(tok::l_paren);
  }

  void resetTokenMetadata(FormatToken *Token) {
    if (!Token)
      return;

    // Reset token type in case we have already looked at it and then
    // recovered from an error (e.g. failure to find the matching >).
    if (!CurrentToken->isOneOf(TT_LambdaLSquare, TT_ForEachMacro,
                               TT_FunctionLBrace, TT_ImplicitStringLiteral,
                               TT_InlineASMBrace, TT_JsFatArrow, TT_LambdaArrow,
                               TT_OverloadedOperator, TT_RegexLiteral,
                               TT_TemplateString, TT_ObjCStringLiteral))
      CurrentToken->Type = TT_Unknown;
    CurrentToken->Role.reset();
    CurrentToken->MatchingParen = nullptr;
    CurrentToken->FakeLParens.clear();
    CurrentToken->FakeRParens = 0;
  }

  void next() {
    if (CurrentToken) {
      CurrentToken->NestingLevel = Contexts.size() - 1;
      CurrentToken->BindingStrength = Contexts.back().BindingStrength;
      modifyContext(*CurrentToken);
      determineTokenType(*CurrentToken);
      CurrentToken = CurrentToken->Next;
    }

    resetTokenMetadata(CurrentToken);
  }

  /// A struct to hold information valid in a specific context, e.g.
  /// a pair of parenthesis.
  struct Context {
    Context(tok::TokenKind ContextKind, unsigned BindingStrength,
            bool IsExpression)
        : ContextKind(ContextKind), BindingStrength(BindingStrength),
          IsExpression(IsExpression) {}

    tok::TokenKind ContextKind;
    unsigned BindingStrength;
    bool IsExpression;
    unsigned LongestObjCSelectorName = 0;
    bool ColonIsForRangeExpr = false;
    bool ColonIsDictLiteral = false;
    bool ColonIsObjCMethodExpr = false;
    FormatToken *FirstObjCSelectorName = nullptr;
    FormatToken *FirstStartOfName = nullptr;
    bool CanBeExpression = true;
    bool InTemplateArgument = false;
    bool InCtorInitializer = false;
    bool InInheritanceList = false;
    bool CaretFound = false;
    bool IsForEachMacro = false;
    bool InCpp11AttributeSpecifier = false;
  };

  /// Puts a new \c Context onto the stack \c Contexts for the lifetime
  /// of each instance.
  struct ScopedContextCreator {
    AnnotatingParser &P;

    ScopedContextCreator(AnnotatingParser &P, tok::TokenKind ContextKind,
                         unsigned Increase)
        : P(P) {
      P.Contexts.push_back(Context(ContextKind,
                                   P.Contexts.back().BindingStrength + Increase,
                                   P.Contexts.back().IsExpression));
    }

    ~ScopedContextCreator() { P.Contexts.pop_back(); }
  };

  void modifyContext(const FormatToken &Current) {
    if (Current.getPrecedence() == prec::Assignment &&
        !Line.First->isOneOf(tok::kw_template, tok::kw_using, tok::kw_return) &&
        // Type aliases use `type X = ...;` in TypeScript and can be exported
        // using `export type ...`.
        !(Style.Language == FormatStyle::LK_JavaScript &&
          (Line.startsWith(Keywords.kw_type, tok::identifier) ||
           Line.startsWith(tok::kw_export, Keywords.kw_type,
                           tok::identifier))) &&
        (!Current.Previous || Current.Previous->isNot(tok::kw_operator))) {
      Contexts.back().IsExpression = true;
      if (!Line.startsWith(TT_UnaryOperator)) {
        for (FormatToken *Previous = Current.Previous;
             Previous && Previous->Previous &&
             !Previous->Previous->isOneOf(tok::comma, tok::semi);
             Previous = Previous->Previous) {
          if (Previous->isOneOf(tok::r_square, tok::r_paren)) {
            Previous = Previous->MatchingParen;
            if (!Previous)
              break;
          }
          if (Previous->opensScope())
            break;
          if (Previous->isOneOf(TT_BinaryOperator, TT_UnaryOperator) &&
              Previous->isOneOf(tok::star, tok::amp, tok::ampamp) &&
              Previous->Previous && Previous->Previous->isNot(tok::equal))
            Previous->Type = TT_PointerOrReference;
        }
      }
    } else if (Current.is(tok::lessless) &&
               (!Current.Previous || !Current.Previous->is(tok::kw_operator))) {
      Contexts.back().IsExpression = true;
    } else if (Current.isOneOf(tok::kw_return, tok::kw_throw)) {
      Contexts.back().IsExpression = true;
    } else if (Current.is(TT_TrailingReturnArrow)) {
      Contexts.back().IsExpression = false;
    } else if (Current.is(TT_LambdaArrow) || Current.is(Keywords.kw_assert)) {
      Contexts.back().IsExpression = Style.Language == FormatStyle::LK_Java;
    } else if (Current.Previous &&
               Current.Previous->is(TT_CtorInitializerColon)) {
      Contexts.back().IsExpression = true;
      Contexts.back().InCtorInitializer = true;
    } else if (Current.Previous && Current.Previous->is(TT_InheritanceColon)) {
      Contexts.back().InInheritanceList = true;
    } else if (Current.isOneOf(tok::r_paren, tok::greater, tok::comma)) {
      for (FormatToken *Previous = Current.Previous;
           Previous && Previous->isOneOf(tok::star, tok::amp);
           Previous = Previous->Previous)
        Previous->Type = TT_PointerOrReference;
      if (Line.MustBeDeclaration && !Contexts.front().InCtorInitializer)
        Contexts.back().IsExpression = false;
    } else if (Current.is(tok::kw_new)) {
      Contexts.back().CanBeExpression = false;
    } else if (Current.isOneOf(tok::semi, tok::exclaim)) {
      // This should be the condition or increment in a for-loop.
      Contexts.back().IsExpression = true;
    }
  }

  void determineTokenType(FormatToken &Current) {
    if (!Current.is(TT_Unknown))
      // The token type is already known.
      return;

    if (Style.Language == FormatStyle::LK_JavaScript) {
      if (Current.is(tok::exclaim)) {
        if (Current.Previous &&
            (Current.Previous->isOneOf(tok::identifier, tok::kw_namespace,
                                       tok::r_paren, tok::r_square,
                                       tok::r_brace) ||
             Current.Previous->Tok.isLiteral())) {
          Current.Type = TT_JsNonNullAssertion;
          return;
        }
        if (Current.Next &&
            Current.Next->isOneOf(TT_BinaryOperator, Keywords.kw_as)) {
          Current.Type = TT_JsNonNullAssertion;
          return;
        }
      }
    }

    // Line.MightBeFunctionDecl can only be true after the parentheses of a
    // function declaration have been found. In this case, 'Current' is a
    // trailing token of this declaration and thus cannot be a name.
    if (Current.is(Keywords.kw_instanceof)) {
      Current.Type = TT_BinaryOperator;
    } else if (isStartOfName(Current) &&
               (!Line.MightBeFunctionDecl || Current.NestingLevel != 0)) {
      Contexts.back().FirstStartOfName = &Current;
      Current.Type = TT_StartOfName;
    } else if (Current.is(tok::semi)) {
      // Reset FirstStartOfName after finding a semicolon so that a for loop
      // with multiple increment statements is not confused with a for loop
      // having multiple variable declarations.
      Contexts.back().FirstStartOfName = nullptr;
    } else if (Current.isOneOf(tok::kw_auto, tok::kw___auto_type)) {
      AutoFound = true;
    } else if (Current.is(tok::arrow) &&
               Style.Language == FormatStyle::LK_Java) {
      Current.Type = TT_LambdaArrow;
    } else if (Current.is(tok::arrow) && AutoFound && Line.MustBeDeclaration &&
               Current.NestingLevel == 0) {
      Current.Type = TT_TrailingReturnArrow;
    } else if (Current.isOneOf(tok::star, tok::amp, tok::ampamp)) {
      Current.Type = determineStarAmpUsage(Current,
                                           Contexts.back().CanBeExpression &&
                                               Contexts.back().IsExpression,
                                           Contexts.back().InTemplateArgument);
    } else if (Current.isOneOf(tok::minus, tok::plus, tok::caret)) {
      Current.Type = determinePlusMinusCaretUsage(Current);
      if (Current.is(TT_UnaryOperator) && Current.is(tok::caret))
        Contexts.back().CaretFound = true;
    } else if (Current.isOneOf(tok::minusminus, tok::plusplus)) {
      Current.Type = determineIncrementUsage(Current);
    } else if (Current.isOneOf(tok::exclaim, tok::tilde)) {
      Current.Type = TT_UnaryOperator;
    } else if (Current.is(tok::question)) {
      if (Style.Language == FormatStyle::LK_JavaScript &&
          Line.MustBeDeclaration && !Contexts.back().IsExpression) {
        // In JavaScript, `interface X { foo?(): bar; }` is an optional method
        // on the interface, not a ternary expression.
        Current.Type = TT_JsTypeOptionalQuestion;
      } else {
        Current.Type = TT_ConditionalExpr;
      }
    } else if (Current.isBinaryOperator() &&
               (!Current.Previous || Current.Previous->isNot(tok::l_square)) &&
               (!Current.is(tok::greater) &&
                Style.Language != FormatStyle::LK_TextProto)) {
      Current.Type = TT_BinaryOperator;
    } else if (Current.is(tok::comment)) {
      if (Current.TokenText.startswith("/*")) {
        if (Current.TokenText.endswith("*/"))
          Current.Type = TT_BlockComment;
        else
          // The lexer has for some reason determined a comment here. But we
          // cannot really handle it, if it isn't properly terminated.
          Current.Tok.setKind(tok::unknown);
      } else {
        Current.Type = TT_LineComment;
      }
    } else if (Current.is(tok::r_paren)) {
      if (rParenEndsCast(Current))
        Current.Type = TT_CastRParen;
      if (Current.MatchingParen && Current.Next &&
          !Current.Next->isBinaryOperator() &&
          !Current.Next->isOneOf(tok::semi, tok::colon, tok::l_brace,
                                 tok::comma, tok::period, tok::arrow,
                                 tok::coloncolon))
        if (FormatToken *AfterParen = Current.MatchingParen->Next) {
          // Make sure this isn't the return type of an Obj-C block declaration
          if (AfterParen->Tok.isNot(tok::caret)) {
            if (FormatToken *BeforeParen = Current.MatchingParen->Previous)
              if (BeforeParen->is(tok::identifier) &&
                  BeforeParen->TokenText == BeforeParen->TokenText.upper() &&
                  (!BeforeParen->Previous ||
                   BeforeParen->Previous->ClosesTemplateDeclaration))
                Current.Type = TT_FunctionAnnotationRParen;
          }
        }
    } else if (Current.is(tok::at) && Current.Next &&
               Style.Language != FormatStyle::LK_JavaScript &&
               Style.Language != FormatStyle::LK_Java) {
      // In Java & JavaScript, "@..." is a decorator or annotation. In ObjC, it
      // marks declarations and properties that need special formatting.
      switch (Current.Next->Tok.getObjCKeywordID()) {
      case tok::objc_interface:
      case tok::objc_implementation:
      case tok::objc_protocol:
        Current.Type = TT_ObjCDecl;
        break;
      case tok::objc_property:
        Current.Type = TT_ObjCProperty;
        break;
      default:
        break;
      }
    } else if (Current.is(tok::period)) {
      FormatToken *PreviousNoComment = Current.getPreviousNonComment();
      if (PreviousNoComment &&
          PreviousNoComment->isOneOf(tok::comma, tok::l_brace))
        Current.Type = TT_DesignatedInitializerPeriod;
      else if (Style.Language == FormatStyle::LK_Java && Current.Previous &&
               Current.Previous->isOneOf(TT_JavaAnnotation,
                                         TT_LeadingJavaAnnotation)) {
        Current.Type = Current.Previous->Type;
      }
    } else if (canBeObjCSelectorComponent(Current) &&
               // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
               Current.Previous && Current.Previous->is(TT_CastRParen) &&
               Current.Previous->MatchingParen &&
               Current.Previous->MatchingParen->Previous &&
               Current.Previous->MatchingParen->Previous->is(
                   TT_ObjCMethodSpecifier)) {
      // This is the first part of an Objective-C selector name. (If there's no
      // colon after this, this is the only place which annotates the identifier
      // as a selector.)
      Current.Type = TT_SelectorName;
    } else if (Current.isOneOf(tok::identifier, tok::kw_const) &&
               Current.Previous &&
               !Current.Previous->isOneOf(tok::equal, tok::at) &&
               Line.MightBeFunctionDecl && Contexts.size() == 1) {
      // Line.MightBeFunctionDecl can only be true after the parentheses of a
      // function declaration have been found.
      Current.Type = TT_TrailingAnnotation;
    } else if ((Style.Language == FormatStyle::LK_Java ||
                Style.Language == FormatStyle::LK_JavaScript) &&
               Current.Previous) {
      if (Current.Previous->is(tok::at) &&
          Current.isNot(Keywords.kw_interface)) {
        const FormatToken &AtToken = *Current.Previous;
        const FormatToken *Previous = AtToken.getPreviousNonComment();
        if (!Previous || Previous->is(TT_LeadingJavaAnnotation))
          Current.Type = TT_LeadingJavaAnnotation;
        else
          Current.Type = TT_JavaAnnotation;
      } else if (Current.Previous->is(tok::period) &&
                 Current.Previous->isOneOf(TT_JavaAnnotation,
                                           TT_LeadingJavaAnnotation)) {
        Current.Type = Current.Previous->Type;
      }
    }
  }

  /// Take a guess at whether \p Tok starts a name of a function or
  /// variable declaration.
  ///
  /// This is a heuristic based on whether \p Tok is an identifier following
  /// something that is likely a type.
  bool isStartOfName(const FormatToken &Tok) {
    if (Tok.isNot(tok::identifier) || !Tok.Previous)
      return false;

    if (Tok.Previous->isOneOf(TT_LeadingJavaAnnotation, Keywords.kw_instanceof,
                              Keywords.kw_as))
      return false;
    if (Style.Language == FormatStyle::LK_JavaScript &&
        Tok.Previous->is(Keywords.kw_in))
      return false;

    // Skip "const" as it does not have an influence on whether this is a name.
    FormatToken *PreviousNotConst = Tok.getPreviousNonComment();
    while (PreviousNotConst && PreviousNotConst->is(tok::kw_const))
      PreviousNotConst = PreviousNotConst->getPreviousNonComment();

    if (!PreviousNotConst)
      return false;

    bool IsPPKeyword = PreviousNotConst->is(tok::identifier) &&
                       PreviousNotConst->Previous &&
                       PreviousNotConst->Previous->is(tok::hash);

    if (PreviousNotConst->is(TT_TemplateCloser))
      return PreviousNotConst && PreviousNotConst->MatchingParen &&
             PreviousNotConst->MatchingParen->Previous &&
             PreviousNotConst->MatchingParen->Previous->isNot(tok::period) &&
             PreviousNotConst->MatchingParen->Previous->isNot(tok::kw_template);

    if (PreviousNotConst->is(tok::r_paren) && PreviousNotConst->MatchingParen &&
        PreviousNotConst->MatchingParen->Previous &&
        PreviousNotConst->MatchingParen->Previous->is(tok::kw_decltype))
      return true;

    return (!IsPPKeyword &&
            PreviousNotConst->isOneOf(tok::identifier, tok::kw_auto)) ||
           PreviousNotConst->is(TT_PointerOrReference) ||
           PreviousNotConst->isSimpleTypeSpecifier();
  }

  /// Determine whether ')' is ending a cast.
  bool rParenEndsCast(const FormatToken &Tok) {
    // C-style casts are only used in C++ and Java.
    if (!Style.isCpp() && Style.Language != FormatStyle::LK_Java)
      return false;

    // Empty parens aren't casts and there are no casts at the end of the line.
    if (Tok.Previous == Tok.MatchingParen || !Tok.Next || !Tok.MatchingParen)
      return false;

    FormatToken *LeftOfParens = Tok.MatchingParen->getPreviousNonComment();
    if (LeftOfParens) {
      // If there is a closing parenthesis left of the current parentheses,
      // look past it as these might be chained casts.
      if (LeftOfParens->is(tok::r_paren)) {
        if (!LeftOfParens->MatchingParen ||
            !LeftOfParens->MatchingParen->Previous)
          return false;
        LeftOfParens = LeftOfParens->MatchingParen->Previous;
      }

      // If there is an identifier (or with a few exceptions a keyword) right
      // before the parentheses, this is unlikely to be a cast.
      if (LeftOfParens->Tok.getIdentifierInfo() &&
          !LeftOfParens->isOneOf(Keywords.kw_in, tok::kw_return, tok::kw_case,
                                 tok::kw_delete))
        return false;

      // Certain other tokens right before the parentheses are also signals that
      // this cannot be a cast.
      if (LeftOfParens->isOneOf(tok::at, tok::r_square, TT_OverloadedOperator,
                                TT_TemplateCloser, tok::ellipsis))
        return false;
    }

    if (Tok.Next->is(tok::question))
      return false;

    // As Java has no function types, a "(" after the ")" likely means that this
    // is a cast.
    if (Style.Language == FormatStyle::LK_Java && Tok.Next->is(tok::l_paren))
      return true;

    // If a (non-string) literal follows, this is likely a cast.
    if (Tok.Next->isNot(tok::string_literal) &&
        (Tok.Next->Tok.isLiteral() ||
         Tok.Next->isOneOf(tok::kw_sizeof, tok::kw_alignof)))
      return true;

    // Heuristically try to determine whether the parentheses contain a type.
    bool ParensAreType =
        !Tok.Previous ||
        Tok.Previous->isOneOf(TT_PointerOrReference, TT_TemplateCloser) ||
        Tok.Previous->isSimpleTypeSpecifier();
    bool ParensCouldEndDecl =
        Tok.Next->isOneOf(tok::equal, tok::semi, tok::l_brace, tok::greater);
    if (ParensAreType && !ParensCouldEndDecl)
      return true;

    // At this point, we heuristically assume that there are no casts at the
    // start of the line. We assume that we have found most cases where there
    // are by the logic above, e.g. "(void)x;".
    if (!LeftOfParens)
      return false;

    // Certain token types inside the parentheses mean that this can't be a
    // cast.
    for (const FormatToken *Token = Tok.MatchingParen->Next; Token != &Tok;
         Token = Token->Next)
      if (Token->is(TT_BinaryOperator))
        return false;

    // If the following token is an identifier or 'this', this is a cast. All
    // cases where this can be something else are handled above.
    if (Tok.Next->isOneOf(tok::identifier, tok::kw_this))
      return true;

    if (!Tok.Next->Next)
      return false;

    // If the next token after the parenthesis is a unary operator, assume
    // that this is cast, unless there are unexpected tokens inside the
    // parenthesis.
    bool NextIsUnary =
        Tok.Next->isUnaryOperator() || Tok.Next->isOneOf(tok::amp, tok::star);
    if (!NextIsUnary || Tok.Next->is(tok::plus) ||
        !Tok.Next->Next->isOneOf(tok::identifier, tok::numeric_constant))
      return false;
    // Search for unexpected tokens.
    for (FormatToken *Prev = Tok.Previous; Prev != Tok.MatchingParen;
         Prev = Prev->Previous) {
      if (!Prev->isOneOf(tok::kw_const, tok::identifier, tok::coloncolon))
        return false;
    }
    return true;
  }

  /// Return the type of the given token assuming it is * or &.
  TokenType determineStarAmpUsage(const FormatToken &Tok, bool IsExpression,
                                  bool InTemplateArgument) {
    if (Style.Language == FormatStyle::LK_JavaScript)
      return TT_BinaryOperator;

    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken)
      return TT_UnaryOperator;

    const FormatToken *NextToken = Tok.getNextNonComment();
    if (!NextToken ||
        NextToken->isOneOf(tok::arrow, tok::equal, tok::kw_const) ||
        (NextToken->is(tok::l_brace) && !NextToken->getNextNonComment()))
      return TT_PointerOrReference;

    if (PrevToken->is(tok::coloncolon))
      return TT_PointerOrReference;

    if (PrevToken->isOneOf(tok::l_paren, tok::l_square, tok::l_brace,
                           tok::comma, tok::semi, tok::kw_return, tok::colon,
                           tok::equal, tok::kw_delete, tok::kw_sizeof,
                           tok::kw_throw) ||
        PrevToken->isOneOf(TT_BinaryOperator, TT_ConditionalExpr,
                           TT_UnaryOperator, TT_CastRParen))
      return TT_UnaryOperator;

    if (NextToken->is(tok::l_square) && NextToken->isNot(TT_LambdaLSquare))
      return TT_PointerOrReference;
    if (NextToken->is(tok::kw_operator) && !IsExpression)
      return TT_PointerOrReference;
    if (NextToken->isOneOf(tok::comma, tok::semi))
      return TT_PointerOrReference;

    if (PrevToken->is(tok::r_paren) && PrevToken->MatchingParen) {
      FormatToken *TokenBeforeMatchingParen =
          PrevToken->MatchingParen->getPreviousNonComment();
      if (TokenBeforeMatchingParen &&
          TokenBeforeMatchingParen->isOneOf(tok::kw_typeof, tok::kw_decltype))
        return TT_PointerOrReference;
    }

    if (PrevToken->Tok.isLiteral() ||
        PrevToken->isOneOf(tok::r_paren, tok::r_square, tok::kw_true,
                           tok::kw_false, tok::r_brace) ||
        NextToken->Tok.isLiteral() ||
        NextToken->isOneOf(tok::kw_true, tok::kw_false) ||
        NextToken->isUnaryOperator() ||
        // If we know we're in a template argument, there are no named
        // declarations. Thus, having an identifier on the right-hand side
        // indicates a binary operator.
        (InTemplateArgument && NextToken->Tok.isAnyIdentifier()))
      return TT_BinaryOperator;

    // "&&(" is quite unlikely to be two successive unary "&".
    if (Tok.is(tok::ampamp) && NextToken && NextToken->is(tok::l_paren))
      return TT_BinaryOperator;

    // This catches some cases where evaluation order is used as control flow:
    //   aaa && aaa->f();
    const FormatToken *NextNextToken = NextToken->getNextNonComment();
    if (NextNextToken && NextNextToken->is(tok::arrow))
      return TT_BinaryOperator;

    // It is very unlikely that we are going to find a pointer or reference type
    // definition on the RHS of an assignment.
    if (IsExpression && !Contexts.back().CaretFound)
      return TT_BinaryOperator;

    return TT_PointerOrReference;
  }

  TokenType determinePlusMinusCaretUsage(const FormatToken &Tok) {
    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken)
      return TT_UnaryOperator;

    if (PrevToken->isOneOf(TT_CastRParen, TT_UnaryOperator))
      // This must be a sequence of leading unary operators.
      return TT_UnaryOperator;

    // Use heuristics to recognize unary operators.
    if (PrevToken->isOneOf(tok::equal, tok::l_paren, tok::comma, tok::l_square,
                           tok::question, tok::colon, tok::kw_return,
                           tok::kw_case, tok::at, tok::l_brace))
      return TT_UnaryOperator;

    // There can't be two consecutive binary operators.
    if (PrevToken->is(TT_BinaryOperator))
      return TT_UnaryOperator;

    // Fall back to marking the token as binary operator.
    return TT_BinaryOperator;
  }

  /// Determine whether ++/-- are pre- or post-increments/-decrements.
  TokenType determineIncrementUsage(const FormatToken &Tok) {
    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken || PrevToken->is(TT_CastRParen))
      return TT_UnaryOperator;
    if (PrevToken->isOneOf(tok::r_paren, tok::r_square, tok::identifier))
      return TT_TrailingUnaryOperator;

    return TT_UnaryOperator;
  }

  SmallVector<Context, 8> Contexts;

  const FormatStyle &Style;
  AnnotatedLine &Line;
  FormatToken *CurrentToken;
  bool AutoFound;
  const AdditionalKeywords &Keywords;

  // Set of "<" tokens that do not open a template parameter list. If parseAngle
  // determines that a specific token can't be a template opener, it will make
  // same decision irrespective of the decisions for tokens leading up to it.
  // Store this information to prevent this from causing exponential runtime.
  llvm::SmallPtrSet<FormatToken *, 16> NonTemplateLess;
};

static const int PrecedenceUnaryOperator = prec::PointerToMember + 1;
static const int PrecedenceArrowAndPeriod = prec::PointerToMember + 2;

/// Parses binary expressions by inserting fake parenthesis based on
/// operator precedence.
class ExpressionParser {
public:
  ExpressionParser(const FormatStyle &Style, const AdditionalKeywords &Keywords,
                   AnnotatedLine &Line)
      : Style(Style), Keywords(Keywords), Current(Line.First) {}

  /// Parse expressions with the given operator precedence.
  void parse(int Precedence = 0) {
    // Skip 'return' and ObjC selector colons as they are not part of a binary
    // expression.
    while (Current && (Current->is(tok::kw_return) ||
                       (Current->is(tok::colon) &&
                        Current->isOneOf(TT_ObjCMethodExpr, TT_DictLiteral))))
      next();

    if (!Current || Precedence > PrecedenceArrowAndPeriod)
      return;

    // Conditional expressions need to be parsed separately for proper nesting.
    if (Precedence == prec::Conditional) {
      parseConditionalExpr();
      return;
    }

    // Parse unary operators, which all have a higher precedence than binary
    // operators.
    if (Precedence == PrecedenceUnaryOperator) {
      parseUnaryOperator();
      return;
    }

    FormatToken *Start = Current;
    FormatToken *LatestOperator = nullptr;
    unsigned OperatorIndex = 0;

    while (Current) {
      // Consume operators with higher precedence.
      parse(Precedence + 1);

      int CurrentPrecedence = getCurrentPrecedence();

      if (Current && Current->is(TT_SelectorName) &&
          Precedence == CurrentPrecedence) {
        if (LatestOperator)
          addFakeParenthesis(Start, prec::Level(Precedence));
        Start = Current;
      }

      // At the end of the line or when an operator with higher precedence is
      // found, insert fake parenthesis and return.
      if (!Current ||
          (Current->closesScope() &&
           (Current->MatchingParen || Current->is(TT_TemplateString))) ||
          (CurrentPrecedence != -1 && CurrentPrecedence < Precedence) ||
          (CurrentPrecedence == prec::Conditional &&
           Precedence == prec::Assignment && Current->is(tok::colon))) {
        break;
      }

      // Consume scopes: (), [], <> and {}
      if (Current->opensScope()) {
        // In fragment of a JavaScript template string can look like '}..${' and
        // thus close a scope and open a new one at the same time.
        while (Current && (!Current->closesScope() || Current->opensScope())) {
          next();
          parse();
        }
        next();
      } else {
        // Operator found.
        if (CurrentPrecedence == Precedence) {
          if (LatestOperator)
            LatestOperator->NextOperator = Current;
          LatestOperator = Current;
          Current->OperatorIndex = OperatorIndex;
          ++OperatorIndex;
        }
        next(/*SkipPastLeadingComments=*/Precedence > 0);
      }
    }

    if (LatestOperator && (Current || Precedence > 0)) {
      // LatestOperator->LastOperator = true;
      if (Precedence == PrecedenceArrowAndPeriod) {
        // Call expressions don't have a binary operator precedence.
        addFakeParenthesis(Start, prec::Unknown);
      } else {
        addFakeParenthesis(Start, prec::Level(Precedence));
      }
    }
  }

private:
  /// Gets the precedence (+1) of the given token for binary operators
  /// and other tokens that we treat like binary operators.
  int getCurrentPrecedence() {
    if (Current) {
      const FormatToken *NextNonComment = Current->getNextNonComment();
      if (Current->is(TT_ConditionalExpr))
        return prec::Conditional;
      if (NextNonComment && Current->is(TT_SelectorName) &&
          (NextNonComment->isOneOf(TT_DictLiteral, TT_JsTypeColon) ||
           ((Style.Language == FormatStyle::LK_Proto ||
             Style.Language == FormatStyle::LK_TextProto) &&
            NextNonComment->is(tok::less))))
        return prec::Assignment;
      if (Current->is(TT_JsComputedPropertyName))
        return prec::Assignment;
      if (Current->is(TT_LambdaArrow))
        return prec::Comma;
      if (Current->is(TT_JsFatArrow))
        return prec::Assignment;
      if (Current->isOneOf(tok::semi, TT_InlineASMColon, TT_SelectorName) ||
          (Current->is(tok::comment) && NextNonComment &&
           NextNonComment->is(TT_SelectorName)))
        return 0;
      if (Current->is(TT_RangeBasedForLoopColon))
        return prec::Comma;
      if ((Style.Language == FormatStyle::LK_Java ||
           Style.Language == FormatStyle::LK_JavaScript) &&
          Current->is(Keywords.kw_instanceof))
        return prec::Relational;
      if (Style.Language == FormatStyle::LK_JavaScript &&
          Current->isOneOf(Keywords.kw_in, Keywords.kw_as))
        return prec::Relational;
      if (Current->is(TT_BinaryOperator) || Current->is(tok::comma))
        return Current->getPrecedence();
      if (Current->isOneOf(tok::period, tok::arrow))
        return PrecedenceArrowAndPeriod;
      if ((Style.Language == FormatStyle::LK_Java ||
           Style.Language == FormatStyle::LK_JavaScript) &&
          Current->isOneOf(Keywords.kw_extends, Keywords.kw_implements,
                           Keywords.kw_throws))
        return 0;
    }
    return -1;
  }

  void addFakeParenthesis(FormatToken *Start, prec::Level Precedence) {
    Start->FakeLParens.push_back(Precedence);
    if (Precedence > prec::Unknown)
      Start->StartsBinaryExpression = true;
    if (Current) {
      FormatToken *Previous = Current->Previous;
      while (Previous->is(tok::comment) && Previous->Previous)
        Previous = Previous->Previous;
      ++Previous->FakeRParens;
      if (Precedence > prec::Unknown)
        Previous->EndsBinaryExpression = true;
    }
  }

  /// Parse unary operator expressions and surround them with fake
  /// parentheses if appropriate.
  void parseUnaryOperator() {
    llvm::SmallVector<FormatToken *, 2> Tokens;
    while (Current && Current->is(TT_UnaryOperator)) {
      Tokens.push_back(Current);
      next();
    }
    parse(PrecedenceArrowAndPeriod);
    for (FormatToken *Token : llvm::reverse(Tokens))
      // The actual precedence doesn't matter.
      addFakeParenthesis(Token, prec::Unknown);
  }

  void parseConditionalExpr() {
    while (Current && Current->isTrailingComment()) {
      next();
    }
    FormatToken *Start = Current;
    parse(prec::LogicalOr);
    if (!Current || !Current->is(tok::question))
      return;
    next();
    parse(prec::Assignment);
    if (!Current || Current->isNot(TT_ConditionalExpr))
      return;
    next();
    parse(prec::Assignment);
    addFakeParenthesis(Start, prec::Conditional);
  }

  void next(bool SkipPastLeadingComments = true) {
    if (Current)
      Current = Current->Next;
    while (Current &&
           (Current->NewlinesBefore == 0 || SkipPastLeadingComments) &&
           Current->isTrailingComment())
      Current = Current->Next;
  }

  const FormatStyle &Style;
  const AdditionalKeywords &Keywords;
  FormatToken *Current;
};

} // end anonymous namespace

void TokenAnnotator::setCommentLineLevels(
    SmallVectorImpl<AnnotatedLine *> &Lines) {
  const AnnotatedLine *NextNonCommentLine = nullptr;
  for (SmallVectorImpl<AnnotatedLine *>::reverse_iterator I = Lines.rbegin(),
                                                          E = Lines.rend();
       I != E; ++I) {
    bool CommentLine = true;
    for (const FormatToken *Tok = (*I)->First; Tok; Tok = Tok->Next) {
      if (!Tok->is(tok::comment)) {
        CommentLine = false;
        break;
      }
    }

    // If the comment is currently aligned with the line immediately following
    // it, that's probably intentional and we should keep it.
    if (NextNonCommentLine && CommentLine &&
        NextNonCommentLine->First->NewlinesBefore <= 1 &&
        NextNonCommentLine->First->OriginalColumn ==
            (*I)->First->OriginalColumn) {
      // Align comments for preprocessor lines with the # in column 0.
      // Otherwise, align with the next line.
      (*I)->Level = (NextNonCommentLine->Type == LT_PreprocessorDirective ||
                     NextNonCommentLine->Type == LT_ImportStatement)
                        ? 0
                        : NextNonCommentLine->Level;
    } else {
      NextNonCommentLine = (*I)->First->isNot(tok::r_brace) ? (*I) : nullptr;
    }

    setCommentLineLevels((*I)->Children);
  }
}

static unsigned maxNestingDepth(const AnnotatedLine &Line) {
  unsigned Result = 0;
  for (const auto *Tok = Line.First; Tok != nullptr; Tok = Tok->Next)
    Result = std::max(Result, Tok->NestingLevel);
  return Result;
}

void TokenAnnotator::annotate(AnnotatedLine &Line) {
  for (SmallVectorImpl<AnnotatedLine *>::iterator I = Line.Children.begin(),
                                                  E = Line.Children.end();
       I != E; ++I) {
    annotate(**I);
  }
  AnnotatingParser Parser(Style, Line, Keywords);
  Line.Type = Parser.parseLine();

  // With very deep nesting, ExpressionParser uses lots of stack and the
  // formatting algorithm is very slow. We're not going to do a good job here
  // anyway - it's probably generated code being formatted by mistake.
  // Just skip the whole line.
  if (maxNestingDepth(Line) > 50)
    Line.Type = LT_Invalid;

  if (Line.Type == LT_Invalid)
    return;

  ExpressionParser ExprParser(Style, Keywords, Line);
  ExprParser.parse();

  if (Line.startsWith(TT_ObjCMethodSpecifier))
    Line.Type = LT_ObjCMethodDecl;
  else if (Line.startsWith(TT_ObjCDecl))
    Line.Type = LT_ObjCDecl;
  else if (Line.startsWith(TT_ObjCProperty))
    Line.Type = LT_ObjCProperty;

  Line.First->SpacesRequiredBefore = 1;
  Line.First->CanBreakBefore = Line.First->MustBreakBefore;
}

// This function heuristically determines whether 'Current' starts the name of a
// function declaration.
static bool isFunctionDeclarationName(const FormatToken &Current,
                                      const AnnotatedLine &Line) {
  auto skipOperatorName = [](const FormatToken *Next) -> const FormatToken * {
    for (; Next; Next = Next->Next) {
      if (Next->is(TT_OverloadedOperatorLParen))
        return Next;
      if (Next->is(TT_OverloadedOperator))
        continue;
      if (Next->isOneOf(tok::kw_new, tok::kw_delete)) {
        // For 'new[]' and 'delete[]'.
        if (Next->Next && Next->Next->is(tok::l_square) && Next->Next->Next &&
            Next->Next->Next->is(tok::r_square))
          Next = Next->Next->Next;
        continue;
      }

      break;
    }
    return nullptr;
  };

  // Find parentheses of parameter list.
  const FormatToken *Next = Current.Next;
  if (Current.is(tok::kw_operator)) {
    if (Current.Previous && Current.Previous->is(tok::coloncolon))
      return false;
    Next = skipOperatorName(Next);
  } else {
    if (!Current.is(TT_StartOfName) || Current.NestingLevel != 0)
      return false;
    for (; Next; Next = Next->Next) {
      if (Next->is(TT_TemplateOpener)) {
        Next = Next->MatchingParen;
      } else if (Next->is(tok::coloncolon)) {
        Next = Next->Next;
        if (!Next)
          return false;
        if (Next->is(tok::kw_operator)) {
          Next = skipOperatorName(Next->Next);
          break;
        }
        if (!Next->is(tok::identifier))
          return false;
      } else if (Next->is(tok::l_paren)) {
        break;
      } else {
        return false;
      }
    }
  }

  // Check whether parameter list can belong to a function declaration.
  if (!Next || !Next->is(tok::l_paren) || !Next->MatchingParen)
    return false;
  // If the lines ends with "{", this is likely an function definition.
  if (Line.Last->is(tok::l_brace))
    return true;
  if (Next->Next == Next->MatchingParen)
    return true; // Empty parentheses.
  // If there is an &/&& after the r_paren, this is likely a function.
  if (Next->MatchingParen->Next &&
      Next->MatchingParen->Next->is(TT_PointerOrReference))
    return true;
  for (const FormatToken *Tok = Next->Next; Tok && Tok != Next->MatchingParen;
       Tok = Tok->Next) {
    if (Tok->is(tok::l_paren) && Tok->MatchingParen) {
      Tok = Tok->MatchingParen;
      continue;
    }
    if (Tok->is(tok::kw_const) || Tok->isSimpleTypeSpecifier() ||
        Tok->isOneOf(TT_PointerOrReference, TT_StartOfName, tok::ellipsis))
      return true;
    if (Tok->isOneOf(tok::l_brace, tok::string_literal, TT_ObjCMethodExpr) ||
        Tok->Tok.isLiteral())
      return false;
  }
  return false;
}

bool TokenAnnotator::mustBreakForReturnType(const AnnotatedLine &Line) const {
  assert(Line.MightBeFunctionDecl);

  if ((Style.AlwaysBreakAfterReturnType == FormatStyle::RTBS_TopLevel ||
       Style.AlwaysBreakAfterReturnType ==
           FormatStyle::RTBS_TopLevelDefinitions) &&
      Line.Level > 0)
    return false;

  switch (Style.AlwaysBreakAfterReturnType) {
  case FormatStyle::RTBS_None:
    return false;
  case FormatStyle::RTBS_All:
  case FormatStyle::RTBS_TopLevel:
    return true;
  case FormatStyle::RTBS_AllDefinitions:
  case FormatStyle::RTBS_TopLevelDefinitions:
    return Line.mightBeFunctionDefinition();
  }

  return false;
}

void TokenAnnotator::calculateFormattingInformation(AnnotatedLine &Line) {
  for (SmallVectorImpl<AnnotatedLine *>::iterator I = Line.Children.begin(),
                                                  E = Line.Children.end();
       I != E; ++I) {
    calculateFormattingInformation(**I);
  }

  Line.First->TotalLength =
      Line.First->IsMultiline ? Style.ColumnLimit
                              : Line.FirstStartColumn + Line.First->ColumnWidth;
  FormatToken *Current = Line.First->Next;
  bool InFunctionDecl = Line.MightBeFunctionDecl;
  while (Current) {
    if (isFunctionDeclarationName(*Current, Line))
      Current->Type = TT_FunctionDeclarationName;
    if (Current->is(TT_LineComment)) {
      if (Current->Previous->BlockKind == BK_BracedInit &&
          Current->Previous->opensScope())
        Current->SpacesRequiredBefore = Style.Cpp11BracedListStyle ? 0 : 1;
      else
        Current->SpacesRequiredBefore = Style.SpacesBeforeTrailingComments;

      // If we find a trailing comment, iterate backwards to determine whether
      // it seems to relate to a specific parameter. If so, break before that
      // parameter to avoid changing the comment's meaning. E.g. don't move 'b'
      // to the previous line in:
      //   SomeFunction(a,
      //                b, // comment
      //                c);
      if (!Current->HasUnescapedNewline) {
        for (FormatToken *Parameter = Current->Previous; Parameter;
             Parameter = Parameter->Previous) {
          if (Parameter->isOneOf(tok::comment, tok::r_brace))
            break;
          if (Parameter->Previous && Parameter->Previous->is(tok::comma)) {
            if (!Parameter->Previous->is(TT_CtorInitializerComma) &&
                Parameter->HasUnescapedNewline)
              Parameter->MustBreakBefore = true;
            break;
          }
        }
      }
    } else if (Current->SpacesRequiredBefore == 0 &&
               spaceRequiredBefore(Line, *Current)) {
      Current->SpacesRequiredBefore = 1;
    }

    Current->MustBreakBefore =
        Current->MustBreakBefore || mustBreakBefore(Line, *Current);

    if (!Current->MustBreakBefore && InFunctionDecl &&
        Current->is(TT_FunctionDeclarationName))
      Current->MustBreakBefore = mustBreakForReturnType(Line);

    Current->CanBreakBefore =
        Current->MustBreakBefore || canBreakBefore(Line, *Current);
    unsigned ChildSize = 0;
    if (Current->Previous->Children.size() == 1) {
      FormatToken &LastOfChild = *Current->Previous->Children[0]->Last;
      ChildSize = LastOfChild.isTrailingComment() ? Style.ColumnLimit
                                                  : LastOfChild.TotalLength + 1;
    }
    const FormatToken *Prev = Current->Previous;
    if (Current->MustBreakBefore || Prev->Children.size() > 1 ||
        (Prev->Children.size() == 1 &&
         Prev->Children[0]->First->MustBreakBefore) ||
        Current->IsMultiline)
      Current->TotalLength = Prev->TotalLength + Style.ColumnLimit;
    else
      Current->TotalLength = Prev->TotalLength + Current->ColumnWidth +
                             ChildSize + Current->SpacesRequiredBefore;

    if (Current->is(TT_CtorInitializerColon))
      InFunctionDecl = false;

    // FIXME: Only calculate this if CanBreakBefore is true once static
    // initializers etc. are sorted out.
    // FIXME: Move magic numbers to a better place.

    // Reduce penalty for aligning ObjC method arguments using the colon
    // alignment as this is the canonical way (still prefer fitting everything
    // into one line if possible). Trying to fit a whole expression into one
    // line should not force other line breaks (e.g. when ObjC method
    // expression is a part of other expression).
    Current->SplitPenalty = splitPenalty(Line, *Current, InFunctionDecl);
    if (Style.Language == FormatStyle::LK_ObjC &&
        Current->is(TT_SelectorName) && Current->ParameterIndex > 0) {
      if (Current->ParameterIndex == 1)
        Current->SplitPenalty += 5 * Current->BindingStrength;
    } else {
      Current->SplitPenalty += 20 * Current->BindingStrength;
    }

    Current = Current->Next;
  }

  calculateUnbreakableTailLengths(Line);
  unsigned IndentLevel = Line.Level;
  for (Current = Line.First; Current != nullptr; Current = Current->Next) {
    if (Current->Role)
      Current->Role->precomputeFormattingInfos(Current);
    if (Current->MatchingParen &&
        Current->MatchingParen->opensBlockOrBlockTypeList(Style)) {
      assert(IndentLevel > 0);
      --IndentLevel;
    }
    Current->IndentLevel = IndentLevel;
    if (Current->opensBlockOrBlockTypeList(Style))
      ++IndentLevel;
  }

  LLVM_DEBUG({ printDebugInfo(Line); });
}

void TokenAnnotator::calculateUnbreakableTailLengths(AnnotatedLine &Line) {
  unsigned UnbreakableTailLength = 0;
  FormatToken *Current = Line.Last;
  while (Current) {
    Current->UnbreakableTailLength = UnbreakableTailLength;
    if (Current->CanBreakBefore ||
        Current->isOneOf(tok::comment, tok::string_literal)) {
      UnbreakableTailLength = 0;
    } else {
      UnbreakableTailLength +=
          Current->ColumnWidth + Current->SpacesRequiredBefore;
    }
    Current = Current->Previous;
  }
}

unsigned TokenAnnotator::splitPenalty(const AnnotatedLine &Line,
                                      const FormatToken &Tok,
                                      bool InFunctionDecl) {
  const FormatToken &Left = *Tok.Previous;
  const FormatToken &Right = Tok;

  if (Left.is(tok::semi))
    return 0;

  if (Style.Language == FormatStyle::LK_Java) {
    if (Right.isOneOf(Keywords.kw_extends, Keywords.kw_throws))
      return 1;
    if (Right.is(Keywords.kw_implements))
      return 2;
    if (Left.is(tok::comma) && Left.NestingLevel == 0)
      return 3;
  } else if (Style.Language == FormatStyle::LK_JavaScript) {
    if (Right.is(Keywords.kw_function) && Left.isNot(tok::comma))
      return 100;
    if (Left.is(TT_JsTypeColon))
      return 35;
    if ((Left.is(TT_TemplateString) && Left.TokenText.endswith("${")) ||
        (Right.is(TT_TemplateString) && Right.TokenText.startswith("}")))
      return 100;
    // Prefer breaking call chains (".foo") over empty "{}", "[]" or "()".
    if (Left.opensScope() && Right.closesScope())
      return 200;
  }

  if (Right.is(tok::identifier) && Right.Next && Right.Next->is(TT_DictLiteral))
    return 1;
  if (Right.is(tok::l_square)) {
    if (Style.Language == FormatStyle::LK_Proto)
      return 1;
    if (Left.is(tok::r_square))
      return 200;
    // Slightly prefer formatting local lambda definitions like functions.
    if (Right.is(TT_LambdaLSquare) && Left.is(tok::equal))
      return 35;
    if (!Right.isOneOf(TT_ObjCMethodExpr, TT_LambdaLSquare,
                       TT_ArrayInitializerLSquare,
                       TT_DesignatedInitializerLSquare, TT_AttributeSquare))
      return 500;
  }

  if (Right.isOneOf(TT_StartOfName, TT_FunctionDeclarationName) ||
      Right.is(tok::kw_operator)) {
    if (Line.startsWith(tok::kw_for) && Right.PartOfMultiVariableDeclStmt)
      return 3;
    if (Left.is(TT_StartOfName))
      return 110;
    if (InFunctionDecl && Right.NestingLevel == 0)
      return Style.PenaltyReturnTypeOnItsOwnLine;
    return 200;
  }
  if (Right.is(TT_PointerOrReference))
    return 190;
  if (Right.is(TT_LambdaArrow))
    return 110;
  if (Left.is(tok::equal) && Right.is(tok::l_brace))
    return 160;
  if (Left.is(TT_CastRParen))
    return 100;
  if (Left.is(tok::coloncolon) ||
      (Right.is(tok::period) && Style.Language == FormatStyle::LK_Proto))
    return 500;
  if (Left.isOneOf(tok::kw_class, tok::kw_struct))
    return 5000;
  if (Left.is(tok::comment))
    return 1000;

  if (Left.isOneOf(TT_RangeBasedForLoopColon, TT_InheritanceColon,
                   TT_CtorInitializerColon))
    return 2;

  if (Right.isMemberAccess()) {
    // Breaking before the "./->" of a chained call/member access is reasonably
    // cheap, as formatting those with one call per line is generally
    // desirable. In particular, it should be cheaper to break before the call
    // than it is to break inside a call's parameters, which could lead to weird
    // "hanging" indents. The exception is the very last "./->" to support this
    // frequent pattern:
    //
    //   aaaaaaaa.aaaaaaaa.bbbbbbb().ccccccccccccccccccccc(
    //       dddddddd);
    //
    // which might otherwise be blown up onto many lines. Here, clang-format
    // won't produce "hanging" indents anyway as there is no other trailing
    // call.
    //
    // Also apply higher penalty is not a call as that might lead to a wrapping
    // like:
    //
    //   aaaaaaa
    //       .aaaaaaaaa.bbbbbbbb(cccccccc);
    return !Right.NextOperator || !Right.NextOperator->Previous->closesScope()
               ? 150
               : 35;
  }

  if (Right.is(TT_TrailingAnnotation) &&
      (!Right.Next || Right.Next->isNot(tok::l_paren))) {
    // Moving trailing annotations to the next line is fine for ObjC method
    // declarations.
    if (Line.startsWith(TT_ObjCMethodSpecifier))
      return 10;
    // Generally, breaking before a trailing annotation is bad unless it is
    // function-like. It seems to be especially preferable to keep standard
    // annotations (i.e. "const", "final" and "override") on the same line.
    // Use a slightly higher penalty after ")" so that annotations like
    // "const override" are kept together.
    bool is_short_annotation = Right.TokenText.size() < 10;
    return (Left.is(tok::r_paren) ? 100 : 120) + (is_short_annotation ? 50 : 0);
  }

  // In for-loops, prefer breaking at ',' and ';'.
  if (Line.startsWith(tok::kw_for) && Left.is(tok::equal))
    return 4;

  // In Objective-C method expressions, prefer breaking before "param:" over
  // breaking after it.
  if (Right.is(TT_SelectorName))
    return 0;
  if (Left.is(tok::colon) && Left.is(TT_ObjCMethodExpr))
    return Line.MightBeFunctionDecl ? 50 : 500;

  // In Objective-C type declarations, avoid breaking after the category's
  // open paren (we'll prefer breaking after the protocol list's opening
  // angle bracket, if present).
  if (Line.Type == LT_ObjCDecl && Left.is(tok::l_paren) && Left.Previous &&
      Left.Previous->isOneOf(tok::identifier, tok::greater))
    return 500;

  if (Left.is(tok::l_paren) && InFunctionDecl &&
      Style.AlignAfterOpenBracket != FormatStyle::BAS_DontAlign)
    return 100;
  if (Left.is(tok::l_paren) && Left.Previous &&
      (Left.Previous->isOneOf(tok::kw_if, tok::kw_for) ||
       Left.Previous->endsSequence(tok::kw_constexpr, tok::kw_if)))
    return 1000;
  if (Left.is(tok::equal) && InFunctionDecl)
    return 110;
  if (Right.is(tok::r_brace))
    return 1;
  if (Left.is(TT_TemplateOpener))
    return 100;
  if (Left.opensScope()) {
    if (Style.AlignAfterOpenBracket == FormatStyle::BAS_DontAlign)
      return 0;
    if (Left.is(tok::l_brace) && !Style.Cpp11BracedListStyle)
      return 19;
    return Left.ParameterCount > 1 ? Style.PenaltyBreakBeforeFirstCallParameter
                                   : 19;
  }
  if (Left.is(TT_JavaAnnotation))
    return 50;

  if (Left.isOneOf(tok::plus, tok::comma) && Left.Previous &&
      Left.Previous->isLabelString() &&
      (Left.NextOperator || Left.OperatorIndex != 0))
    return 50;
  if (Right.is(tok::plus) && Left.isLabelString() &&
      (Right.NextOperator || Right.OperatorIndex != 0))
    return 25;
  if (Left.is(tok::comma))
    return 1;
  if (Right.is(tok::lessless) && Left.isLabelString() &&
      (Right.NextOperator || Right.OperatorIndex != 1))
    return 25;
  if (Right.is(tok::lessless)) {
    // Breaking at a << is really cheap.
    if (!Left.is(tok::r_paren) || Right.OperatorIndex > 0)
      // Slightly prefer to break before the first one in log-like statements.
      return 2;
    return 1;
  }
  if (Left.ClosesTemplateDeclaration)
    return Style.PenaltyBreakTemplateDeclaration;
  if (Left.is(TT_ConditionalExpr))
    return prec::Conditional;
  prec::Level Level = Left.getPrecedence();
  if (Level == prec::Unknown)
    Level = Right.getPrecedence();
  if (Level == prec::Assignment)
    return Style.PenaltyBreakAssignment;
  if (Level != prec::Unknown)
    return Level;

  return 3;
}

bool TokenAnnotator::spaceRequiredBetween(const AnnotatedLine &Line,
                                          const FormatToken &Left,
                                          const FormatToken &Right) {
  if (Left.is(tok::kw_return) && Right.isNot(tok::semi))
    return true;
  if (Left.is(Keywords.kw_assert) && Style.Language == FormatStyle::LK_Java)
    return true;
  if (Style.ObjCSpaceAfterProperty && Line.Type == LT_ObjCProperty &&
      Left.Tok.getObjCKeywordID() == tok::objc_property)
    return true;
  if (Right.is(tok::hashhash))
    return Left.is(tok::hash);
  if (Left.isOneOf(tok::hashhash, tok::hash))
    return Right.is(tok::hash);
  if (Left.is(tok::l_paren) && Right.is(tok::r_paren))
    return Style.SpaceInEmptyParentheses;
  if (Left.is(tok::l_paren) || Right.is(tok::r_paren))
    return (Right.is(TT_CastRParen) ||
            (Left.MatchingParen && Left.MatchingParen->is(TT_CastRParen)))
               ? Style.SpacesInCStyleCastParentheses
               : Style.SpacesInParentheses;
  if (Right.isOneOf(tok::semi, tok::comma))
    return false;
  if (Right.is(tok::less) && Line.Type == LT_ObjCDecl) {
    bool IsLightweightGeneric =
        Right.MatchingParen && Right.MatchingParen->Next &&
        Right.MatchingParen->Next->is(tok::colon);
    return !IsLightweightGeneric && Style.ObjCSpaceBeforeProtocolList;
  }
  if (Right.is(tok::less) && Left.is(tok::kw_template))
    return Style.SpaceAfterTemplateKeyword;
  if (Left.isOneOf(tok::exclaim, tok::tilde))
    return false;
  if (Left.is(tok::at) &&
      Right.isOneOf(tok::identifier, tok::string_literal, tok::char_constant,
                    tok::numeric_constant, tok::l_paren, tok::l_brace,
                    tok::kw_true, tok::kw_false))
    return false;
  if (Left.is(tok::colon))
    return !Left.is(TT_ObjCMethodExpr);
  if (Left.is(tok::coloncolon))
    return false;
  if (Left.is(tok::less) || Right.isOneOf(tok::greater, tok::less)) {
    if (Style.Language == FormatStyle::LK_TextProto ||
        (Style.Language == FormatStyle::LK_Proto &&
         (Left.is(TT_DictLiteral) || Right.is(TT_DictLiteral)))) {
      // Format empty list as `<>`.
      if (Left.is(tok::less) && Right.is(tok::greater))
        return false;
      return !Style.Cpp11BracedListStyle;
    }
    return false;
  }
  if (Right.is(tok::ellipsis))
    return Left.Tok.isLiteral() || (Left.is(tok::identifier) && Left.Previous &&
                                    Left.Previous->is(tok::kw_case));
  if (Left.is(tok::l_square) && Right.is(tok::amp))
    return false;
  if (Right.is(TT_PointerOrReference)) {
    if (Left.is(tok::r_paren) && Line.MightBeFunctionDecl) {
      if (!Left.MatchingParen)
        return true;
      FormatToken *TokenBeforeMatchingParen =
          Left.MatchingParen->getPreviousNonComment();
      if (!TokenBeforeMatchingParen ||
          !TokenBeforeMatchingParen->isOneOf(tok::kw_typeof, tok::kw_decltype))
        return true;
    }
    return (Left.Tok.isLiteral() ||
            (!Left.isOneOf(TT_PointerOrReference, tok::l_paren) &&
             (Style.PointerAlignment != FormatStyle::PAS_Left ||
              (Line.IsMultiVariableDeclStmt &&
               (Left.NestingLevel == 0 ||
                (Left.NestingLevel == 1 && Line.First->is(tok::kw_for)))))));
  }
  if (Right.is(TT_FunctionTypeLParen) && Left.isNot(tok::l_paren) &&
      (!Left.is(TT_PointerOrReference) ||
       (Style.PointerAlignment != FormatStyle::PAS_Right &&
        !Line.IsMultiVariableDeclStmt)))
    return true;
  if (Left.is(TT_PointerOrReference))
    return Right.Tok.isLiteral() || Right.is(TT_BlockComment) ||
           (Right.isOneOf(Keywords.kw_override, Keywords.kw_final) &&
            !Right.is(TT_StartOfName)) ||
           (Right.is(tok::l_brace) && Right.BlockKind == BK_Block) ||
           (!Right.isOneOf(TT_PointerOrReference, TT_ArraySubscriptLSquare,
                           tok::l_paren) &&
            (Style.PointerAlignment != FormatStyle::PAS_Right &&
             !Line.IsMultiVariableDeclStmt) &&
            Left.Previous &&
            !Left.Previous->isOneOf(tok::l_paren, tok::coloncolon));
  if (Right.is(tok::star) && Left.is(tok::l_paren))
    return false;
  const auto SpaceRequiredForArrayInitializerLSquare =
      [](const FormatToken &LSquareTok, const FormatStyle &Style) {
        return Style.SpacesInContainerLiterals ||
               ((Style.Language == FormatStyle::LK_Proto ||
                 Style.Language == FormatStyle::LK_TextProto) &&
                !Style.Cpp11BracedListStyle &&
                LSquareTok.endsSequence(tok::l_square, tok::colon,
                                        TT_SelectorName));
      };
  if (Left.is(tok::l_square))
    return (Left.is(TT_ArrayInitializerLSquare) && Right.isNot(tok::r_square) &&
            SpaceRequiredForArrayInitializerLSquare(Left, Style)) ||
           (Left.isOneOf(TT_ArraySubscriptLSquare,
                         TT_StructuredBindingLSquare) &&
            Style.SpacesInSquareBrackets && Right.isNot(tok::r_square));
  if (Right.is(tok::r_square))
    return Right.MatchingParen &&
           ((Right.MatchingParen->is(TT_ArrayInitializerLSquare) &&
             SpaceRequiredForArrayInitializerLSquare(*Right.MatchingParen,
                                                     Style)) ||
            (Style.SpacesInSquareBrackets &&
             Right.MatchingParen->isOneOf(TT_ArraySubscriptLSquare,
                                          TT_StructuredBindingLSquare)) ||
            Right.MatchingParen->is(TT_AttributeParen));
  if (Right.is(tok::l_square) &&
      !Right.isOneOf(TT_ObjCMethodExpr, TT_LambdaLSquare,
                     TT_DesignatedInitializerLSquare,
                     TT_StructuredBindingLSquare, TT_AttributeSquare) &&
      !Left.isOneOf(tok::numeric_constant, TT_DictLiteral))
    return false;
  if (Left.is(tok::l_brace) && Right.is(tok::r_brace))
    return !Left.Children.empty(); // No spaces in "{}".
  if ((Left.is(tok::l_brace) && Left.BlockKind != BK_Block) ||
      (Right.is(tok::r_brace) && Right.MatchingParen &&
       Right.MatchingParen->BlockKind != BK_Block))
    return !Style.Cpp11BracedListStyle;
  if (Left.is(TT_BlockComment))
    // No whitespace in x(/*foo=*/1), except for JavaScript.
    return Style.Language == FormatStyle::LK_JavaScript ||
           !Left.TokenText.endswith("=*/");
  if (Right.is(tok::l_paren)) {
    if ((Left.is(tok::r_paren) && Left.is(TT_AttributeParen)) ||
        (Left.is(tok::r_square) && Left.is(TT_AttributeSquare)))
      return true;
    return Line.Type == LT_ObjCDecl || Left.is(tok::semi) ||
           (Style.SpaceBeforeParens != FormatStyle::SBPO_Never &&
            (Left.isOneOf(tok::kw_if, tok::pp_elif, tok::kw_for, tok::kw_while,
                          tok::kw_switch, tok::kw_case, TT_ForEachMacro,
                          TT_ObjCForIn) ||
             Left.endsSequence(tok::kw_constexpr, tok::kw_if) ||
             (Left.isOneOf(tok::kw_try, Keywords.kw___except, tok::kw_catch,
                           tok::kw_new, tok::kw_delete) &&
              (!Left.Previous || Left.Previous->isNot(tok::period))))) ||
           (Style.SpaceBeforeParens == FormatStyle::SBPO_Always &&
            (Left.is(tok::identifier) || Left.isFunctionLikeKeyword() ||
             Left.is(tok::r_paren)) &&
            Line.Type != LT_PreprocessorDirective);
  }
  if (Left.is(tok::at) && Right.Tok.getObjCKeywordID() != tok::objc_not_keyword)
    return false;
  if (Right.is(TT_UnaryOperator))
    return !Left.isOneOf(tok::l_paren, tok::l_square, tok::at) &&
           (Left.isNot(tok::colon) || Left.isNot(TT_ObjCMethodExpr));
  if ((Left.isOneOf(tok::identifier, tok::greater, tok::r_square,
                    tok::r_paren) ||
       Left.isSimpleTypeSpecifier()) &&
      Right.is(tok::l_brace) && Right.getNextNonComment() &&
      Right.BlockKind != BK_Block)
    return false;
  if (Left.is(tok::period) || Right.is(tok::period))
    return false;
  if (Right.is(tok::hash) && Left.is(tok::identifier) && Left.TokenText == "L")
    return false;
  if (Left.is(TT_TemplateCloser) && Left.MatchingParen &&
      Left.MatchingParen->Previous &&
      (Left.MatchingParen->Previous->is(tok::period) ||
       Left.MatchingParen->Previous->is(tok::coloncolon)))
    // Java call to generic function with explicit type:
    // A.<B<C<...>>>DoSomething();
    // A::<B<C<...>>>DoSomething();  // With a Java 8 method reference.
    return false;
  if (Left.is(TT_TemplateCloser) && Right.is(tok::l_square))
    return false;
  if (Left.is(tok::l_brace) && Left.endsSequence(TT_DictLiteral, tok::at))
    // Objective-C dictionary literal -> no space after opening brace.
    return false;
  if (Right.is(tok::r_brace) && Right.MatchingParen &&
      Right.MatchingParen->endsSequence(TT_DictLiteral, tok::at))
    // Objective-C dictionary literal -> no space before closing brace.
    return false;
  return true;
}

bool TokenAnnotator::spaceRequiredBefore(const AnnotatedLine &Line,
                                         const FormatToken &Right) {
  const FormatToken &Left = *Right.Previous;
  if (Right.Tok.getIdentifierInfo() && Left.Tok.getIdentifierInfo())
    return true; // Never ever merge two identifiers.
  if (Style.isCpp()) {
    if (Left.is(tok::kw_operator))
      return Right.is(tok::coloncolon);
    if (Right.is(tok::l_brace) && Right.BlockKind == BK_BracedInit &&
        !Left.opensScope() && Style.SpaceBeforeCpp11BracedList)
      return true;
  } else if (Style.Language == FormatStyle::LK_Proto ||
             Style.Language == FormatStyle::LK_TextProto) {
    if (Right.is(tok::period) &&
        Left.isOneOf(Keywords.kw_optional, Keywords.kw_required,
                     Keywords.kw_repeated, Keywords.kw_extend))
      return true;
    if (Right.is(tok::l_paren) &&
        Left.isOneOf(Keywords.kw_returns, Keywords.kw_option))
      return true;
    if (Right.isOneOf(tok::l_brace, tok::less) && Left.is(TT_SelectorName))
      return true;
    // Slashes occur in text protocol extension syntax: [type/type] { ... }.
    if (Left.is(tok::slash) || Right.is(tok::slash))
      return false;
    if (Left.MatchingParen && Left.MatchingParen->is(TT_ProtoExtensionLSquare) &&
        Right.isOneOf(tok::l_brace, tok::less))
      return !Style.Cpp11BracedListStyle;
    // A percent is probably part of a formatting specification, such as %lld.
    if (Left.is(tok::percent))
      return false;
    // Preserve the existence of a space before a percent for cases like 0x%04x
    // and "%d %d"
    if (Left.is(tok::numeric_constant) && Right.is(tok::percent))
      return Right.WhitespaceRange.getEnd() != Right.WhitespaceRange.getBegin();
  } else if (Style.Language == FormatStyle::LK_JavaScript) {
    if (Left.is(TT_JsFatArrow))
      return true;
    // for await ( ...
    if (Right.is(tok::l_paren) && Left.is(Keywords.kw_await) && Left.Previous &&
        Left.Previous->is(tok::kw_for))
      return true;
    if (Left.is(Keywords.kw_async) && Right.is(tok::l_paren) &&
        Right.MatchingParen) {
      const FormatToken *Next = Right.MatchingParen->getNextNonComment();
      // An async arrow function, for example: `x = async () => foo();`,
      // as opposed to calling a function called async: `x = async();`
      if (Next && Next->is(TT_JsFatArrow))
        return true;
    }
    if ((Left.is(TT_TemplateString) && Left.TokenText.endswith("${")) ||
        (Right.is(TT_TemplateString) && Right.TokenText.startswith("}")))
      return false;
    // In tagged template literals ("html`bar baz`"), there is no space between
    // the tag identifier and the template string. getIdentifierInfo makes sure
    // that the identifier is not a pseudo keyword like `yield`, either.
    if (Left.is(tok::identifier) && Keywords.IsJavaScriptIdentifier(Left) &&
        Right.is(TT_TemplateString))
      return false;
    if (Right.is(tok::star) &&
        Left.isOneOf(Keywords.kw_function, Keywords.kw_yield))
      return false;
    if (Right.isOneOf(tok::l_brace, tok::l_square) &&
        Left.isOneOf(Keywords.kw_function, Keywords.kw_yield,
                     Keywords.kw_extends, Keywords.kw_implements))
      return true;
    if (Right.is(tok::l_paren)) {
      // JS methods can use some keywords as names (e.g. `delete()`).
      if (Line.MustBeDeclaration && Left.Tok.getIdentifierInfo())
        return false;
      // Valid JS method names can include keywords, e.g. `foo.delete()` or
      // `bar.instanceof()`. Recognize call positions by preceding period.
      if (Left.Previous && Left.Previous->is(tok::period) &&
          Left.Tok.getIdentifierInfo())
        return false;
      // Additional unary JavaScript operators that need a space after.
      if (Left.isOneOf(tok::kw_throw, Keywords.kw_await, Keywords.kw_typeof,
                       tok::kw_void))
        return true;
    }
    if ((Left.isOneOf(Keywords.kw_let, Keywords.kw_var, Keywords.kw_in,
                      tok::kw_const) ||
         // "of" is only a keyword if it appears after another identifier
         // (e.g. as "const x of y" in a for loop), or after a destructuring
         // operation (const [x, y] of z, const {a, b} of c).
         (Left.is(Keywords.kw_of) && Left.Previous &&
          (Left.Previous->Tok.is(tok::identifier) ||
           Left.Previous->isOneOf(tok::r_square, tok::r_brace)))) &&
        (!Left.Previous || !Left.Previous->is(tok::period)))
      return true;
    if (Left.isOneOf(tok::kw_for, Keywords.kw_as) && Left.Previous &&
        Left.Previous->is(tok::period) && Right.is(tok::l_paren))
      return false;
    if (Left.is(Keywords.kw_as) &&
        Right.isOneOf(tok::l_square, tok::l_brace, tok::l_paren))
      return true;
    if (Left.is(tok::kw_default) && Left.Previous &&
        Left.Previous->is(tok::kw_export))
      return true;
    if (Left.is(Keywords.kw_is) && Right.is(tok::l_brace))
      return true;
    if (Right.isOneOf(TT_JsTypeColon, TT_JsTypeOptionalQuestion))
      return false;
    if (Left.is(TT_JsTypeOperator) || Right.is(TT_JsTypeOperator))
      return false;
    if ((Left.is(tok::l_brace) || Right.is(tok::r_brace)) &&
        Line.First->isOneOf(Keywords.kw_import, tok::kw_export))
      return false;
    if (Left.is(tok::ellipsis))
      return false;
    if (Left.is(TT_TemplateCloser) &&
        !Right.isOneOf(tok::equal, tok::l_brace, tok::comma, tok::l_square,
                       Keywords.kw_implements, Keywords.kw_extends))
      // Type assertions ('<type>expr') are not followed by whitespace. Other
      // locations that should have whitespace following are identified by the
      // above set of follower tokens.
      return false;
    if (Right.is(TT_JsNonNullAssertion))
      return false;
    if (Left.is(TT_JsNonNullAssertion) &&
        Right.isOneOf(Keywords.kw_as, Keywords.kw_in))
      return true; // "x! as string", "x! in y"
  } else if (Style.Language == FormatStyle::LK_Java) {
    if (Left.is(tok::r_square) && Right.is(tok::l_brace))
      return true;
    if (Left.is(Keywords.kw_synchronized) && Right.is(tok::l_paren))
      return Style.SpaceBeforeParens != FormatStyle::SBPO_Never;
    if ((Left.isOneOf(tok::kw_static, tok::kw_public, tok::kw_private,
                      tok::kw_protected) ||
         Left.isOneOf(Keywords.kw_final, Keywords.kw_abstract,
                      Keywords.kw_native)) &&
        Right.is(TT_TemplateOpener))
      return true;
  }
  if (Left.is(TT_ImplicitStringLiteral))
    return Right.WhitespaceRange.getBegin() != Right.WhitespaceRange.getEnd();
  if (Line.Type == LT_ObjCMethodDecl) {
    if (Left.is(TT_ObjCMethodSpecifier))
      return true;
    if (Left.is(tok::r_paren) && canBeObjCSelectorComponent(Right))
      // Don't space between ')' and <id> or ')' and 'new'. 'new' is not a
      // keyword in Objective-C, and '+ (instancetype)new;' is a standard class
      // method declaration.
      return false;
  }
  if (Line.Type == LT_ObjCProperty &&
      (Right.is(tok::equal) || Left.is(tok::equal)))
    return false;

  if (Right.isOneOf(TT_TrailingReturnArrow, TT_LambdaArrow) ||
      Left.isOneOf(TT_TrailingReturnArrow, TT_LambdaArrow))
    return true;
  if (Right.is(TT_OverloadedOperatorLParen))
    return Style.SpaceBeforeParens == FormatStyle::SBPO_Always;
  if (Left.is(tok::comma))
    return true;
  if (Right.is(tok::comma))
    return false;
  if (Right.is(TT_ObjCBlockLParen))
    return true;
  if (Right.is(TT_CtorInitializerColon))
    return Style.SpaceBeforeCtorInitializerColon;
  if (Right.is(TT_InheritanceColon) && !Style.SpaceBeforeInheritanceColon)
    return false;
  if (Right.is(TT_RangeBasedForLoopColon) &&
      !Style.SpaceBeforeRangeBasedForLoopColon)
    return false;
  if (Right.is(tok::colon)) {
    if (Line.First->isOneOf(tok::kw_case, tok::kw_default) ||
        !Right.getNextNonComment() || Right.getNextNonComment()->is(tok::semi))
      return false;
    if (Right.is(TT_ObjCMethodExpr))
      return false;
    if (Left.is(tok::question))
      return false;
    if (Right.is(TT_InlineASMColon) && Left.is(tok::coloncolon))
      return false;
    if (Right.is(TT_DictLiteral))
      return Style.SpacesInContainerLiterals;
    if (Right.is(TT_AttributeColon))
      return false;
    return true;
  }
  if (Left.is(TT_UnaryOperator))
    return Right.is(TT_BinaryOperator);

  // If the next token is a binary operator or a selector name, we have
  // incorrectly classified the parenthesis as a cast. FIXME: Detect correctly.
  if (Left.is(TT_CastRParen))
    return Style.SpaceAfterCStyleCast ||
           Right.isOneOf(TT_BinaryOperator, TT_SelectorName);

  if (Left.is(tok::greater) && Right.is(tok::greater)) {
    if (Style.Language == FormatStyle::LK_TextProto ||
        (Style.Language == FormatStyle::LK_Proto && Left.is(TT_DictLiteral)))
      return !Style.Cpp11BracedListStyle;
    return Right.is(TT_TemplateCloser) && Left.is(TT_TemplateCloser) &&
           (Style.Standard != FormatStyle::LS_Cpp11 || Style.SpacesInAngles);
  }
  if (Right.isOneOf(tok::arrow, tok::arrowstar, tok::periodstar) ||
      Left.isOneOf(tok::arrow, tok::period, tok::arrowstar, tok::periodstar) ||
      (Right.is(tok::period) && Right.isNot(TT_DesignatedInitializerPeriod)))
    return false;
  if (!Style.SpaceBeforeAssignmentOperators &&
      Right.getPrecedence() == prec::Assignment)
    return false;
  if (Style.Language == FormatStyle::LK_Java && Right.is(tok::coloncolon) &&
      (Left.is(tok::identifier) || Left.is(tok::kw_this)))
    return false;
  if (Right.is(tok::coloncolon) && Left.is(tok::identifier))
    // Generally don't remove existing spaces between an identifier and "::".
    // The identifier might actually be a macro name such as ALWAYS_INLINE. If
    // this turns out to be too lenient, add analysis of the identifier itself.
    return Right.WhitespaceRange.getBegin() != Right.WhitespaceRange.getEnd();
  if (Right.is(tok::coloncolon) && !Left.isOneOf(tok::l_brace, tok::comment))
    return (Left.is(TT_TemplateOpener) &&
            Style.Standard == FormatStyle::LS_Cpp03) ||
           !(Left.isOneOf(tok::l_paren, tok::r_paren, tok::l_square,
                          tok::kw___super, TT_TemplateCloser,
                          TT_TemplateOpener)) ||
           (Left.is(tok ::l_paren) && Style.SpacesInParentheses);
  if ((Left.is(TT_TemplateOpener)) != (Right.is(TT_TemplateCloser)))
    return Style.SpacesInAngles;
  // Space before TT_StructuredBindingLSquare.
  if (Right.is(TT_StructuredBindingLSquare))
    return !Left.isOneOf(tok::amp, tok::ampamp) ||
           Style.PointerAlignment != FormatStyle::PAS_Right;
  // Space before & or && following a TT_StructuredBindingLSquare.
  if (Right.Next && Right.Next->is(TT_StructuredBindingLSquare) &&
      Right.isOneOf(tok::amp, tok::ampamp))
    return Style.PointerAlignment != FormatStyle::PAS_Left;
  if ((Right.is(TT_BinaryOperator) && !Left.is(tok::l_paren)) ||
      (Left.isOneOf(TT_BinaryOperator, TT_ConditionalExpr) &&
       !Right.is(tok::r_paren)))
    return true;
  if (Left.is(TT_TemplateCloser) && Right.is(tok::l_paren) &&
      Right.isNot(TT_FunctionTypeLParen))
    return Style.SpaceBeforeParens == FormatStyle::SBPO_Always;
  if (Right.is(TT_TemplateOpener) && Left.is(tok::r_paren) &&
      Left.MatchingParen && Left.MatchingParen->is(TT_OverloadedOperatorLParen))
    return false;
  if (Right.is(tok::less) && Left.isNot(tok::l_paren) &&
      Line.startsWith(tok::hash))
    return true;
  if (Right.is(TT_TrailingUnaryOperator))
    return false;
  if (Left.is(TT_RegexLiteral))
    return false;
  return spaceRequiredBetween(Line, Left, Right);
}

// Returns 'true' if 'Tok' is a brace we'd want to break before in Allman style.
static bool isAllmanBrace(const FormatToken &Tok) {
  return Tok.is(tok::l_brace) && Tok.BlockKind == BK_Block &&
         !Tok.isOneOf(TT_ObjCBlockLBrace, TT_DictLiteral);
}

bool TokenAnnotator::mustBreakBefore(const AnnotatedLine &Line,
                                     const FormatToken &Right) {
  const FormatToken &Left = *Right.Previous;
  if (Right.NewlinesBefore > 1 && Style.MaxEmptyLinesToKeep > 0)
    return true;

  if (Style.Language == FormatStyle::LK_JavaScript) {
    // FIXME: This might apply to other languages and token kinds.
    if (Right.is(tok::string_literal) && Left.is(tok::plus) && Left.Previous &&
        Left.Previous->is(tok::string_literal))
      return true;
    if (Left.is(TT_DictLiteral) && Left.is(tok::l_brace) && Line.Level == 0 &&
        Left.Previous && Left.Previous->is(tok::equal) &&
        Line.First->isOneOf(tok::identifier, Keywords.kw_import, tok::kw_export,
                            tok::kw_const) &&
        // kw_var/kw_let are pseudo-tokens that are tok::identifier, so match
        // above.
        !Line.First->isOneOf(Keywords.kw_var, Keywords.kw_let))
      // Object literals on the top level of a file are treated as "enum-style".
      // Each key/value pair is put on a separate line, instead of bin-packing.
      return true;
    if (Left.is(tok::l_brace) && Line.Level == 0 &&
        (Line.startsWith(tok::kw_enum) ||
         Line.startsWith(tok::kw_const, tok::kw_enum) ||
         Line.startsWith(tok::kw_export, tok::kw_enum) ||
         Line.startsWith(tok::kw_export, tok::kw_const, tok::kw_enum)))
      // JavaScript top-level enum key/value pairs are put on separate lines
      // instead of bin-packing.
      return true;
    if (Right.is(tok::r_brace) && Left.is(tok::l_brace) &&
        !Left.Children.empty())
      // Support AllowShortFunctionsOnASingleLine for JavaScript.
      return Style.AllowShortFunctionsOnASingleLine == FormatStyle::SFS_None ||
             Style.AllowShortFunctionsOnASingleLine == FormatStyle::SFS_Empty ||
             (Left.NestingLevel == 0 && Line.Level == 0 &&
              Style.AllowShortFunctionsOnASingleLine &
                  FormatStyle::SFS_InlineOnly);
  } else if (Style.Language == FormatStyle::LK_Java) {
    if (Right.is(tok::plus) && Left.is(tok::string_literal) && Right.Next &&
        Right.Next->is(tok::string_literal))
      return true;
  } else if (Style.Language == FormatStyle::LK_Cpp ||
             Style.Language == FormatStyle::LK_ObjC ||
             Style.Language == FormatStyle::LK_Proto ||
             Style.Language == FormatStyle::LK_TableGen ||
             Style.Language == FormatStyle::LK_TextProto) {
    if (Left.isStringLiteral() && Right.isStringLiteral())
      return true;
  }

  // If the last token before a '}', ']', or ')' is a comma or a trailing
  // comment, the intention is to insert a line break after it in order to make
  // shuffling around entries easier. Import statements, especially in
  // JavaScript, can be an exception to this rule.
  if (Style.JavaScriptWrapImports || Line.Type != LT_ImportStatement) {
    const FormatToken *BeforeClosingBrace = nullptr;
    if ((Left.isOneOf(tok::l_brace, TT_ArrayInitializerLSquare) ||
         (Style.Language == FormatStyle::LK_JavaScript &&
          Left.is(tok::l_paren))) &&
        Left.BlockKind != BK_Block && Left.MatchingParen)
      BeforeClosingBrace = Left.MatchingParen->Previous;
    else if (Right.MatchingParen &&
             (Right.MatchingParen->isOneOf(tok::l_brace,
                                           TT_ArrayInitializerLSquare) ||
              (Style.Language == FormatStyle::LK_JavaScript &&
               Right.MatchingParen->is(tok::l_paren))))
      BeforeClosingBrace = &Left;
    if (BeforeClosingBrace && (BeforeClosingBrace->is(tok::comma) ||
                               BeforeClosingBrace->isTrailingComment()))
      return true;
  }

  if (Right.is(tok::comment))
    return Left.BlockKind != BK_BracedInit &&
           Left.isNot(TT_CtorInitializerColon) &&
           (Right.NewlinesBefore > 0 && Right.HasUnescapedNewline);
  if (Left.isTrailingComment())
    return true;
  if (Right.Previous->IsUnterminatedLiteral)
    return true;
  if (Right.is(tok::lessless) && Right.Next &&
      Right.Previous->is(tok::string_literal) &&
      Right.Next->is(tok::string_literal))
    return true;
  if (Right.Previous->ClosesTemplateDeclaration &&
      Right.Previous->MatchingParen &&
      Right.Previous->MatchingParen->NestingLevel == 0 &&
      Style.AlwaysBreakTemplateDeclarations == FormatStyle::BTDS_Yes)
    return true;
  if (Right.is(TT_CtorInitializerComma) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma &&
      !Style.ConstructorInitializerAllOnOneLineOrOnePerLine)
    return true;
  if (Right.is(TT_CtorInitializerColon) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma &&
      !Style.ConstructorInitializerAllOnOneLineOrOnePerLine)
    return true;
  // Break only if we have multiple inheritance.
  if (Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma &&
      Right.is(TT_InheritanceComma))
    return true;
  if (Right.is(tok::string_literal) && Right.TokenText.startswith("R\""))
    // Multiline raw string literals are special wrt. line breaks. The author
    // has made a deliberate choice and might have aligned the contents of the
    // string literal accordingly. Thus, we try keep existing line breaks.
    return Right.IsMultiline && Right.NewlinesBefore > 0;
  if ((Right.Previous->is(tok::l_brace) ||
       (Right.Previous->is(tok::less) && Right.Previous->Previous &&
        Right.Previous->Previous->is(tok::equal))) &&
      Right.NestingLevel == 1 && Style.Language == FormatStyle::LK_Proto) {
    // Don't put enums or option definitions onto single lines in protocol
    // buffers.
    return true;
  }
  if (Right.is(TT_InlineASMBrace))
    return Right.HasUnescapedNewline;
  if (isAllmanBrace(Left) || isAllmanBrace(Right))
    return (Line.startsWith(tok::kw_enum) && Style.BraceWrapping.AfterEnum) ||
           (Line.startsWith(tok::kw_typedef, tok::kw_enum) &&
            Style.BraceWrapping.AfterEnum) ||
           (Line.startsWith(tok::kw_class) && Style.BraceWrapping.AfterClass) ||
           (Line.startsWith(tok::kw_struct) && Style.BraceWrapping.AfterStruct);
  if (Left.is(TT_ObjCBlockLBrace) && !Style.AllowShortBlocksOnASingleLine)
    return true;

  if ((Style.Language == FormatStyle::LK_Java ||
       Style.Language == FormatStyle::LK_JavaScript) &&
      Left.is(TT_LeadingJavaAnnotation) &&
      Right.isNot(TT_LeadingJavaAnnotation) && Right.isNot(tok::l_paren) &&
      (Line.Last->is(tok::l_brace) || Style.BreakAfterJavaFieldAnnotations))
    return true;

  if (Right.is(TT_ProtoExtensionLSquare))
    return true;

  // In text proto instances if a submessage contains at least 2 entries and at
  // least one of them is a submessage, like A { ... B { ... } ... },
  // put all of the entries of A on separate lines by forcing the selector of
  // the submessage B to be put on a newline.
  //
  // Example: these can stay on one line:
  // a { scalar_1: 1 scalar_2: 2 }
  // a { b { key: value } }
  //
  // and these entries need to be on a new line even if putting them all in one
  // line is under the column limit:
  // a {
  //   scalar: 1
  //   b { key: value }
  // }
  //
  // We enforce this by breaking before a submessage field that has previous
  // siblings, *and* breaking before a field that follows a submessage field.
  //
  // Be careful to exclude the case  [proto.ext] { ... } since the `]` is
  // the TT_SelectorName there, but we don't want to break inside the brackets.
  //
  // Another edge case is @submessage { key: value }, which is a common
  // substitution placeholder. In this case we want to keep `@` and `submessage`
  // together.
  //
  // We ensure elsewhere that extensions are always on their own line.
  if ((Style.Language == FormatStyle::LK_Proto ||
       Style.Language == FormatStyle::LK_TextProto) &&
      Right.is(TT_SelectorName) && !Right.is(tok::r_square) && Right.Next) {
    // Keep `@submessage` together in:
    // @submessage { key: value }
    if (Right.Previous && Right.Previous->is(tok::at))
      return false;
    // Look for the scope opener after selector in cases like:
    // selector { ...
    // selector: { ...
    // selector: @base { ...
    FormatToken *LBrace = Right.Next;
    if (LBrace && LBrace->is(tok::colon)) {
      LBrace = LBrace->Next;
      if (LBrace && LBrace->is(tok::at)) {
        LBrace = LBrace->Next;
        if (LBrace)
          LBrace = LBrace->Next;
      }
    }
    if (LBrace &&
        // The scope opener is one of {, [, <:
        // selector { ... }
        // selector [ ... ]
        // selector < ... >
        //
        // In case of selector { ... }, the l_brace is TT_DictLiteral.
        // In case of an empty selector {}, the l_brace is not TT_DictLiteral,
        // so we check for immediately following r_brace.
        ((LBrace->is(tok::l_brace) &&
          (LBrace->is(TT_DictLiteral) ||
           (LBrace->Next && LBrace->Next->is(tok::r_brace)))) ||
         LBrace->is(TT_ArrayInitializerLSquare) || LBrace->is(tok::less))) {
      // If Left.ParameterCount is 0, then this submessage entry is not the
      // first in its parent submessage, and we want to break before this entry.
      // If Left.ParameterCount is greater than 0, then its parent submessage
      // might contain 1 or more entries and we want to break before this entry
      // if it contains at least 2 entries. We deal with this case later by
      // detecting and breaking before the next entry in the parent submessage.
      if (Left.ParameterCount == 0)
        return true;
      // However, if this submessage is the first entry in its parent
      // submessage, Left.ParameterCount might be 1 in some cases.
      // We deal with this case later by detecting an entry
      // following a closing paren of this submessage.
    }

    // If this is an entry immediately following a submessage, it will be
    // preceded by a closing paren of that submessage, like in:
    //     left---.  .---right
    //            v  v
    // sub: { ... } key: value
    // If there was a comment between `}` an `key` above, then `key` would be
    // put on a new line anyways.
    if (Left.isOneOf(tok::r_brace, tok::greater, tok::r_square))
      return true;
  }

  // Deal with lambda arguments in C++ - we want consistent line breaks whether
  // they happen to be at arg0, arg1 or argN. The selection is a bit nuanced
  // as aggressive line breaks are placed when the lambda is not the last arg.
  if ((Style.Language == FormatStyle::LK_Cpp ||
       Style.Language == FormatStyle::LK_ObjC) &&
      Left.is(tok::l_paren) && Left.BlockParameterCount > 0 &&
      !Right.isOneOf(tok::l_paren, TT_LambdaLSquare)) {
    // Multiple lambdas in the same function call force line breaks.
    if (Left.BlockParameterCount > 1)
      return true;

    // A lambda followed by another arg forces a line break.
    if (!Left.Role)
      return false;
    auto Comma = Left.Role->lastComma();
    if (!Comma)
      return false;
    auto Next = Comma->getNextNonComment();
    if (!Next)
      return false;
    if (!Next->isOneOf(TT_LambdaLSquare, tok::l_brace, tok::caret))
      return true;
  }

  return false;
}

bool TokenAnnotator::canBreakBefore(const AnnotatedLine &Line,
                                    const FormatToken &Right) {
  const FormatToken &Left = *Right.Previous;

  // Language-specific stuff.
  if (Style.Language == FormatStyle::LK_Java) {
    if (Left.isOneOf(Keywords.kw_throws, Keywords.kw_extends,
                     Keywords.kw_implements))
      return false;
    if (Right.isOneOf(Keywords.kw_throws, Keywords.kw_extends,
                      Keywords.kw_implements))
      return true;
  } else if (Style.Language == FormatStyle::LK_JavaScript) {
    const FormatToken *NonComment = Right.getPreviousNonComment();
    if (NonComment &&
        NonComment->isOneOf(
            tok::kw_return, Keywords.kw_yield, tok::kw_continue, tok::kw_break,
            tok::kw_throw, Keywords.kw_interface, Keywords.kw_type,
            tok::kw_static, tok::kw_public, tok::kw_private, tok::kw_protected,
            Keywords.kw_readonly, Keywords.kw_abstract, Keywords.kw_get,
            Keywords.kw_set, Keywords.kw_async, Keywords.kw_await))
      return false; // Otherwise automatic semicolon insertion would trigger.
    if (Right.NestingLevel == 0 &&
        (Left.Tok.getIdentifierInfo() ||
         Left.isOneOf(tok::r_square, tok::r_paren)) &&
        Right.isOneOf(tok::l_square, tok::l_paren))
      return false; // Otherwise automatic semicolon insertion would trigger.
    if (Left.is(TT_JsFatArrow) && Right.is(tok::l_brace))
      return false;
    if (Left.is(TT_JsTypeColon))
      return true;
    // Don't wrap between ":" and "!" of a strict prop init ("field!: type;").
    if (Left.is(tok::exclaim) && Right.is(tok::colon))
      return false;
    // Look for is type annotations like:
    // function f(): a is B { ... }
    // Do not break before is in these cases.
    if (Right.is(Keywords.kw_is)) {
      const FormatToken* Next = Right.getNextNonComment();
      // If `is` is followed by a colon, it's likely that it's a dict key, so
      // ignore it for this check.
      // For example this is common in Polymer:
      // Polymer({
      //   is: 'name',
      //   ...
      // });
      if (!Next || !Next->is(tok::colon))
        return false;
    }
    if (Left.is(Keywords.kw_in))
      return Style.BreakBeforeBinaryOperators == FormatStyle::BOS_None;
    if (Right.is(Keywords.kw_in))
      return Style.BreakBeforeBinaryOperators != FormatStyle::BOS_None;
    if (Right.is(Keywords.kw_as))
      return false; // must not break before as in 'x as type' casts
    if (Right.isOneOf(Keywords.kw_extends, Keywords.kw_infer)) {
      // extends and infer can appear as keywords in conditional types:
      //   https://www.typescriptlang.org/docs/handbook/release-notes/typescript-2-8.html#conditional-types
      // do not break before them, as the expressions are subject to ASI.
      return false;
    }
    if (Left.is(Keywords.kw_as))
      return true;
    if (Left.is(TT_JsNonNullAssertion))
      return true;
    if (Left.is(Keywords.kw_declare) &&
        Right.isOneOf(Keywords.kw_module, tok::kw_namespace,
                      Keywords.kw_function, tok::kw_class, tok::kw_enum,
                      Keywords.kw_interface, Keywords.kw_type, Keywords.kw_var,
                      Keywords.kw_let, tok::kw_const))
      // See grammar for 'declare' statements at:
      // https://github.com/Microsoft/TypeScript/blob/master/doc/spec.md#A.10
      return false;
    if (Left.isOneOf(Keywords.kw_module, tok::kw_namespace) &&
        Right.isOneOf(tok::identifier, tok::string_literal))
      return false; // must not break in "module foo { ...}"
    if (Right.is(TT_TemplateString) && Right.closesScope())
      return false;
    if (Left.is(TT_TemplateString) && Left.opensScope())
      return true;
  }

  if (Left.is(tok::at))
    return false;
  if (Left.Tok.getObjCKeywordID() == tok::objc_interface)
    return false;
  if (Left.isOneOf(TT_JavaAnnotation, TT_LeadingJavaAnnotation))
    return !Right.is(tok::l_paren);
  if (Right.is(TT_PointerOrReference))
    return Line.IsMultiVariableDeclStmt ||
           (Style.PointerAlignment == FormatStyle::PAS_Right &&
            (!Right.Next || Right.Next->isNot(TT_FunctionDeclarationName)));
  if (Right.isOneOf(TT_StartOfName, TT_FunctionDeclarationName) ||
      Right.is(tok::kw_operator))
    return true;
  if (Left.is(TT_PointerOrReference))
    return false;
  if (Right.isTrailingComment())
    // We rely on MustBreakBefore being set correctly here as we should not
    // change the "binding" behavior of a comment.
    // The first comment in a braced lists is always interpreted as belonging to
    // the first list element. Otherwise, it should be placed outside of the
    // list.
    return Left.BlockKind == BK_BracedInit ||
           (Left.is(TT_CtorInitializerColon) &&
            Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon);
  if (Left.is(tok::question) && Right.is(tok::colon))
    return false;
  if (Right.is(TT_ConditionalExpr) || Right.is(tok::question))
    return Style.BreakBeforeTernaryOperators;
  if (Left.is(TT_ConditionalExpr) || Left.is(tok::question))
    return !Style.BreakBeforeTernaryOperators;
  if (Left.is(TT_InheritanceColon))
    return Style.BreakInheritanceList == FormatStyle::BILS_AfterColon;
  if (Right.is(TT_InheritanceColon))
    return Style.BreakInheritanceList != FormatStyle::BILS_AfterColon;
  if (Right.is(TT_ObjCMethodExpr) && !Right.is(tok::r_square) &&
      Left.isNot(TT_SelectorName))
    return true;

  if (Right.is(tok::colon) &&
      !Right.isOneOf(TT_CtorInitializerColon, TT_InlineASMColon))
    return false;
  if (Left.is(tok::colon) && Left.isOneOf(TT_DictLiteral, TT_ObjCMethodExpr)) {
    if (Style.Language == FormatStyle::LK_Proto ||
        Style.Language == FormatStyle::LK_TextProto) {
      if (!Style.AlwaysBreakBeforeMultilineStrings && Right.isStringLiteral())
        return false;
      // Prevent cases like:
      //
      // submessage:
      //     { key: valueeeeeeeeeeee }
      //
      // when the snippet does not fit into one line.
      // Prefer:
      //
      // submessage: {
      //   key: valueeeeeeeeeeee
      // }
      //
      // instead, even if it is longer by one line.
      //
      // Note that this allows allows the "{" to go over the column limit
      // when the column limit is just between ":" and "{", but that does
      // not happen too often and alternative formattings in this case are
      // not much better.
      //
      // The code covers the cases:
      //
      // submessage: { ... }
      // submessage: < ... >
      // repeated: [ ... ]
      if (((Right.is(tok::l_brace) || Right.is(tok::less)) &&
           Right.is(TT_DictLiteral)) ||
          Right.is(TT_ArrayInitializerLSquare))
        return false;
    }
    return true;
  }
  if (Right.is(tok::r_square) && Right.MatchingParen &&
      Right.MatchingParen->is(TT_ProtoExtensionLSquare))
    return false;
  if (Right.is(TT_SelectorName) || (Right.is(tok::identifier) && Right.Next &&
                                    Right.Next->is(TT_ObjCMethodExpr)))
    return Left.isNot(tok::period); // FIXME: Properly parse ObjC calls.
  if (Left.is(tok::r_paren) && Line.Type == LT_ObjCProperty)
    return true;
  if (Left.ClosesTemplateDeclaration || Left.is(TT_FunctionAnnotationRParen))
    return true;
  if (Right.isOneOf(TT_RangeBasedForLoopColon, TT_OverloadedOperatorLParen,
                    TT_OverloadedOperator))
    return false;
  if (Left.is(TT_RangeBasedForLoopColon))
    return true;
  if (Right.is(TT_RangeBasedForLoopColon))
    return false;
  if (Left.is(TT_TemplateCloser) && Right.is(TT_TemplateOpener))
    return true;
  if (Left.isOneOf(TT_TemplateCloser, TT_UnaryOperator) ||
      Left.is(tok::kw_operator))
    return false;
  if (Left.is(tok::equal) && !Right.isOneOf(tok::kw_default, tok::kw_delete) &&
      Line.Type == LT_VirtualFunctionDecl && Left.NestingLevel == 0)
    return false;
  if (Left.is(tok::equal) && Right.is(tok::l_brace) &&
      !Style.Cpp11BracedListStyle)
    return false;
  if (Left.is(tok::l_paren) && Left.is(TT_AttributeParen))
    return false;
  if (Left.is(tok::l_paren) && Left.Previous &&
      (Left.Previous->isOneOf(TT_BinaryOperator, TT_CastRParen)))
    return false;
  if (Right.is(TT_ImplicitStringLiteral))
    return false;

  if (Right.is(tok::r_paren) || Right.is(TT_TemplateCloser))
    return false;
  if (Right.is(tok::r_square) && Right.MatchingParen &&
      Right.MatchingParen->is(TT_LambdaLSquare))
    return false;

  // We only break before r_brace if there was a corresponding break before
  // the l_brace, which is tracked by BreakBeforeClosingBrace.
  if (Right.is(tok::r_brace))
    return Right.MatchingParen && Right.MatchingParen->BlockKind == BK_Block;

  // Allow breaking after a trailing annotation, e.g. after a method
  // declaration.
  if (Left.is(TT_TrailingAnnotation))
    return !Right.isOneOf(tok::l_brace, tok::semi, tok::equal, tok::l_paren,
                          tok::less, tok::coloncolon);

  if (Right.is(tok::kw___attribute) ||
      (Right.is(tok::l_square) && Right.is(TT_AttributeSquare)))
    return true;

  if (Left.is(tok::identifier) && Right.is(tok::string_literal))
    return true;

  if (Right.is(tok::identifier) && Right.Next && Right.Next->is(TT_DictLiteral))
    return true;

  if (Left.is(TT_CtorInitializerColon))
    return Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon;
  if (Right.is(TT_CtorInitializerColon))
    return Style.BreakConstructorInitializers != FormatStyle::BCIS_AfterColon;
  if (Left.is(TT_CtorInitializerComma) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma)
    return false;
  if (Right.is(TT_CtorInitializerComma) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma)
    return true;
  if (Left.is(TT_InheritanceComma) &&
      Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma)
    return false;
  if (Right.is(TT_InheritanceComma) &&
      Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma)
    return true;
  if ((Left.is(tok::greater) && Right.is(tok::greater)) ||
      (Left.is(tok::less) && Right.is(tok::less)))
    return false;
  if (Right.is(TT_BinaryOperator) &&
      Style.BreakBeforeBinaryOperators != FormatStyle::BOS_None &&
      (Style.BreakBeforeBinaryOperators == FormatStyle::BOS_All ||
       Right.getPrecedence() != prec::Assignment))
    return true;
  if (Left.is(TT_ArrayInitializerLSquare))
    return true;
  if (Right.is(tok::kw_typename) && Left.isNot(tok::kw_const))
    return true;
  if ((Left.isBinaryOperator() || Left.is(TT_BinaryOperator)) &&
      !Left.isOneOf(tok::arrowstar, tok::lessless) &&
      Style.BreakBeforeBinaryOperators != FormatStyle::BOS_All &&
      (Style.BreakBeforeBinaryOperators == FormatStyle::BOS_None ||
       Left.getPrecedence() == prec::Assignment))
    return true;
  if ((Left.is(TT_AttributeSquare) && Right.is(tok::l_square)) ||
      (Left.is(tok::r_square) && Right.is(TT_AttributeSquare)))
    return false;
  return Left.isOneOf(tok::comma, tok::coloncolon, tok::semi, tok::l_brace,
                      tok::kw_class, tok::kw_struct, tok::comment) ||
         Right.isMemberAccess() ||
         Right.isOneOf(TT_TrailingReturnArrow, TT_LambdaArrow, tok::lessless,
                       tok::colon, tok::l_square, tok::at) ||
         (Left.is(tok::r_paren) &&
          Right.isOneOf(tok::identifier, tok::kw_const)) ||
         (Left.is(tok::l_paren) && !Right.is(tok::r_paren)) ||
         (Left.is(TT_TemplateOpener) && !Right.is(TT_TemplateCloser));
}

void TokenAnnotator::printDebugInfo(const AnnotatedLine &Line) {
  llvm::errs() << "AnnotatedTokens(L=" << Line.Level << "):\n";
  const FormatToken *Tok = Line.First;
  while (Tok) {
    llvm::errs() << " M=" << Tok->MustBreakBefore
                 << " C=" << Tok->CanBreakBefore
                 << " T=" << getTokenTypeName(Tok->Type)
                 << " S=" << Tok->SpacesRequiredBefore
                 << " B=" << Tok->BlockParameterCount
                 << " BK=" << Tok->BlockKind << " P=" << Tok->SplitPenalty
                 << " Name=" << Tok->Tok.getName() << " L=" << Tok->TotalLength
                 << " PPK=" << Tok->PackingKind << " FakeLParens=";
    for (unsigned i = 0, e = Tok->FakeLParens.size(); i != e; ++i)
      llvm::errs() << Tok->FakeLParens[i] << "/";
    llvm::errs() << " FakeRParens=" << Tok->FakeRParens;
    llvm::errs() << " II=" << Tok->Tok.getIdentifierInfo();
    llvm::errs() << " Text='" << Tok->TokenText << "'\n";
    if (!Tok->Next)
      assert(Tok == Line.Last);
    Tok = Tok->Next;
  }
  llvm::errs() << "----\n";
}

} // namespace format
} // namespace clang
