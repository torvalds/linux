//===--- FormatToken.h - Format C++ code ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the FormatToken, a wrapper
/// around Token with additional information related to formatting.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_FORMATTOKEN_H
#define LLVM_CLANG_LIB_FORMAT_FORMATTOKEN_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/OperatorPrecedence.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include <memory>
#include <unordered_set>

namespace clang {
namespace format {

#define LIST_TOKEN_TYPES                                                       \
  TYPE(ArrayInitializerLSquare)                                                \
  TYPE(ArraySubscriptLSquare)                                                  \
  TYPE(AttributeColon)                                                         \
  TYPE(AttributeParen)                                                         \
  TYPE(AttributeSquare)                                                        \
  TYPE(BinaryOperator)                                                         \
  TYPE(BitFieldColon)                                                          \
  TYPE(BlockComment)                                                           \
  TYPE(CastRParen)                                                             \
  TYPE(ConditionalExpr)                                                        \
  TYPE(ConflictAlternative)                                                    \
  TYPE(ConflictEnd)                                                            \
  TYPE(ConflictStart)                                                          \
  TYPE(CtorInitializerColon)                                                   \
  TYPE(CtorInitializerComma)                                                   \
  TYPE(DesignatedInitializerLSquare)                                           \
  TYPE(DesignatedInitializerPeriod)                                            \
  TYPE(DictLiteral)                                                            \
  TYPE(ForEachMacro)                                                           \
  TYPE(FunctionAnnotationRParen)                                               \
  TYPE(FunctionDeclarationName)                                                \
  TYPE(FunctionLBrace)                                                         \
  TYPE(FunctionTypeLParen)                                                     \
  TYPE(ImplicitStringLiteral)                                                  \
  TYPE(InheritanceColon)                                                       \
  TYPE(InheritanceComma)                                                       \
  TYPE(InlineASMBrace)                                                         \
  TYPE(InlineASMColon)                                                         \
  TYPE(JavaAnnotation)                                                         \
  TYPE(JsComputedPropertyName)                                                 \
  TYPE(JsExponentiation)                                                       \
  TYPE(JsExponentiationEqual)                                                  \
  TYPE(JsFatArrow)                                                             \
  TYPE(JsNonNullAssertion)                                                     \
  TYPE(JsTypeColon)                                                            \
  TYPE(JsTypeOperator)                                                         \
  TYPE(JsTypeOptionalQuestion)                                                 \
  TYPE(LambdaArrow)                                                            \
  TYPE(LambdaLSquare)                                                          \
  TYPE(LeadingJavaAnnotation)                                                  \
  TYPE(LineComment)                                                            \
  TYPE(MacroBlockBegin)                                                        \
  TYPE(MacroBlockEnd)                                                          \
  TYPE(ObjCBlockLBrace)                                                        \
  TYPE(ObjCBlockLParen)                                                        \
  TYPE(ObjCDecl)                                                               \
  TYPE(ObjCForIn)                                                              \
  TYPE(ObjCMethodExpr)                                                         \
  TYPE(ObjCMethodSpecifier)                                                    \
  TYPE(ObjCProperty)                                                           \
  TYPE(ObjCStringLiteral)                                                      \
  TYPE(OverloadedOperator)                                                     \
  TYPE(OverloadedOperatorLParen)                                               \
  TYPE(PointerOrReference)                                                     \
  TYPE(PureVirtualSpecifier)                                                   \
  TYPE(RangeBasedForLoopColon)                                                 \
  TYPE(RegexLiteral)                                                           \
  TYPE(SelectorName)                                                           \
  TYPE(StartOfName)                                                            \
  TYPE(StatementMacro)                                                         \
  TYPE(StructuredBindingLSquare)                                               \
  TYPE(TemplateCloser)                                                         \
  TYPE(TemplateOpener)                                                         \
  TYPE(TemplateString)                                                         \
  TYPE(ProtoExtensionLSquare)                                                  \
  TYPE(TrailingAnnotation)                                                     \
  TYPE(TrailingReturnArrow)                                                    \
  TYPE(TrailingUnaryOperator)                                                  \
  TYPE(UnaryOperator)                                                          \
  TYPE(Unknown)

enum TokenType {
#define TYPE(X) TT_##X,
  LIST_TOKEN_TYPES
#undef TYPE
      NUM_TOKEN_TYPES
};

/// Determines the name of a token type.
const char *getTokenTypeName(TokenType Type);

// Represents what type of block a set of braces open.
enum BraceBlockKind { BK_Unknown, BK_Block, BK_BracedInit };

// The packing kind of a function's parameters.
enum ParameterPackingKind { PPK_BinPacked, PPK_OnePerLine, PPK_Inconclusive };

enum FormatDecision { FD_Unformatted, FD_Continue, FD_Break };

class TokenRole;
class AnnotatedLine;

/// A wrapper around a \c Token storing information about the
/// whitespace characters preceding it.
struct FormatToken {
  FormatToken() {}

  /// The \c Token.
  Token Tok;

  /// The number of newlines immediately before the \c Token.
  ///
  /// This can be used to determine what the user wrote in the original code
  /// and thereby e.g. leave an empty line between two function definitions.
  unsigned NewlinesBefore = 0;

  /// Whether there is at least one unescaped newline before the \c
  /// Token.
  bool HasUnescapedNewline = false;

  /// The range of the whitespace immediately preceding the \c Token.
  SourceRange WhitespaceRange;

  /// The offset just past the last '\n' in this token's leading
  /// whitespace (relative to \c WhiteSpaceStart). 0 if there is no '\n'.
  unsigned LastNewlineOffset = 0;

  /// The width of the non-whitespace parts of the token (or its first
  /// line for multi-line tokens) in columns.
  /// We need this to correctly measure number of columns a token spans.
  unsigned ColumnWidth = 0;

  /// Contains the width in columns of the last line of a multi-line
  /// token.
  unsigned LastLineColumnWidth = 0;

  /// Whether the token text contains newlines (escaped or not).
  bool IsMultiline = false;

  /// Indicates that this is the first token of the file.
  bool IsFirst = false;

  /// Whether there must be a line break before this token.
  ///
  /// This happens for example when a preprocessor directive ended directly
  /// before the token.
  bool MustBreakBefore = false;

  /// The raw text of the token.
  ///
  /// Contains the raw token text without leading whitespace and without leading
  /// escaped newlines.
  StringRef TokenText;

  /// Set to \c true if this token is an unterminated literal.
  bool IsUnterminatedLiteral = 0;

  /// Contains the kind of block if this token is a brace.
  BraceBlockKind BlockKind = BK_Unknown;

  TokenType Type = TT_Unknown;

  /// The number of spaces that should be inserted before this token.
  unsigned SpacesRequiredBefore = 0;

  /// \c true if it is allowed to break before this token.
  bool CanBreakBefore = false;

  /// \c true if this is the ">" of "template<..>".
  bool ClosesTemplateDeclaration = false;

  /// Number of parameters, if this is "(", "[" or "<".
  unsigned ParameterCount = 0;

  /// Number of parameters that are nested blocks,
  /// if this is "(", "[" or "<".
  unsigned BlockParameterCount = 0;

  /// If this is a bracket ("<", "(", "[" or "{"), contains the kind of
  /// the surrounding bracket.
  tok::TokenKind ParentBracket = tok::unknown;

  /// A token can have a special role that can carry extra information
  /// about the token's formatting.
  std::unique_ptr<TokenRole> Role;

  /// If this is an opening parenthesis, how are the parameters packed?
  ParameterPackingKind PackingKind = PPK_Inconclusive;

  /// The total length of the unwrapped line up to and including this
  /// token.
  unsigned TotalLength = 0;

  /// The original 0-based column of this token, including expanded tabs.
  /// The configured TabWidth is used as tab width.
  unsigned OriginalColumn = 0;

  /// The length of following tokens until the next natural split point,
  /// or the next token that can be broken.
  unsigned UnbreakableTailLength = 0;

  // FIXME: Come up with a 'cleaner' concept.
  /// The binding strength of a token. This is a combined value of
  /// operator precedence, parenthesis nesting, etc.
  unsigned BindingStrength = 0;

  /// The nesting level of this token, i.e. the number of surrounding (),
  /// [], {} or <>.
  unsigned NestingLevel = 0;

  /// The indent level of this token. Copied from the surrounding line.
  unsigned IndentLevel = 0;

  /// Penalty for inserting a line break before this token.
  unsigned SplitPenalty = 0;

  /// If this is the first ObjC selector name in an ObjC method
  /// definition or call, this contains the length of the longest name.
  ///
  /// This being set to 0 means that the selectors should not be colon-aligned,
  /// e.g. because several of them are block-type.
  unsigned LongestObjCSelectorName = 0;

  /// If this is the first ObjC selector name in an ObjC method
  /// definition or call, this contains the number of parts that the whole
  /// selector consist of.
  unsigned ObjCSelectorNameParts = 0;

  /// The 0-based index of the parameter/argument. For ObjC it is set
  /// for the selector name token.
  /// For now calculated only for ObjC.
  unsigned ParameterIndex = 0;

  /// Stores the number of required fake parentheses and the
  /// corresponding operator precedence.
  ///
  /// If multiple fake parentheses start at a token, this vector stores them in
  /// reverse order, i.e. inner fake parenthesis first.
  SmallVector<prec::Level, 4> FakeLParens;
  /// Insert this many fake ) after this token for correct indentation.
  unsigned FakeRParens = 0;

  /// \c true if this token starts a binary expression, i.e. has at least
  /// one fake l_paren with a precedence greater than prec::Unknown.
  bool StartsBinaryExpression = false;
  /// \c true if this token ends a binary expression.
  bool EndsBinaryExpression = false;

  /// If this is an operator (or "."/"->") in a sequence of operators
  /// with the same precedence, contains the 0-based operator index.
  unsigned OperatorIndex = 0;

  /// If this is an operator (or "."/"->") in a sequence of operators
  /// with the same precedence, points to the next operator.
  FormatToken *NextOperator = nullptr;

  /// Is this token part of a \c DeclStmt defining multiple variables?
  ///
  /// Only set if \c Type == \c TT_StartOfName.
  bool PartOfMultiVariableDeclStmt = false;

  /// Does this line comment continue a line comment section?
  ///
  /// Only set to true if \c Type == \c TT_LineComment.
  bool ContinuesLineCommentSection = false;

  /// If this is a bracket, this points to the matching one.
  FormatToken *MatchingParen = nullptr;

  /// The previous token in the unwrapped line.
  FormatToken *Previous = nullptr;

  /// The next token in the unwrapped line.
  FormatToken *Next = nullptr;

  /// If this token starts a block, this contains all the unwrapped lines
  /// in it.
  SmallVector<AnnotatedLine *, 1> Children;

  /// Stores the formatting decision for the token once it was made.
  FormatDecision Decision = FD_Unformatted;

  /// If \c true, this token has been fully formatted (indented and
  /// potentially re-formatted inside), and we do not allow further formatting
  /// changes.
  bool Finalized = false;

  bool is(tok::TokenKind Kind) const { return Tok.is(Kind); }
  bool is(TokenType TT) const { return Type == TT; }
  bool is(const IdentifierInfo *II) const {
    return II && II == Tok.getIdentifierInfo();
  }
  bool is(tok::PPKeywordKind Kind) const {
    return Tok.getIdentifierInfo() &&
           Tok.getIdentifierInfo()->getPPKeywordID() == Kind;
  }
  template <typename A, typename B> bool isOneOf(A K1, B K2) const {
    return is(K1) || is(K2);
  }
  template <typename A, typename B, typename... Ts>
  bool isOneOf(A K1, B K2, Ts... Ks) const {
    return is(K1) || isOneOf(K2, Ks...);
  }
  template <typename T> bool isNot(T Kind) const { return !is(Kind); }

  bool closesScopeAfterBlock() const {
    if (BlockKind == BK_Block)
      return true;
    if (closesScope())
      return Previous->closesScopeAfterBlock();
    return false;
  }

  /// \c true if this token starts a sequence with the given tokens in order,
  /// following the ``Next`` pointers, ignoring comments.
  template <typename A, typename... Ts>
  bool startsSequence(A K1, Ts... Tokens) const {
    return startsSequenceInternal(K1, Tokens...);
  }

  /// \c true if this token ends a sequence with the given tokens in order,
  /// following the ``Previous`` pointers, ignoring comments.
  template <typename A, typename... Ts>
  bool endsSequence(A K1, Ts... Tokens) const {
    return endsSequenceInternal(K1, Tokens...);
  }

  bool isStringLiteral() const { return tok::isStringLiteral(Tok.getKind()); }

  bool isObjCAtKeyword(tok::ObjCKeywordKind Kind) const {
    return Tok.isObjCAtKeyword(Kind);
  }

  bool isAccessSpecifier(bool ColonRequired = true) const {
    return isOneOf(tok::kw_public, tok::kw_protected, tok::kw_private) &&
           (!ColonRequired || (Next && Next->is(tok::colon)));
  }

  /// Determine whether the token is a simple-type-specifier.
  bool isSimpleTypeSpecifier() const;

  bool isObjCAccessSpecifier() const {
    return is(tok::at) && Next &&
           (Next->isObjCAtKeyword(tok::objc_public) ||
            Next->isObjCAtKeyword(tok::objc_protected) ||
            Next->isObjCAtKeyword(tok::objc_package) ||
            Next->isObjCAtKeyword(tok::objc_private));
  }

  /// Returns whether \p Tok is ([{ or an opening < of a template or in
  /// protos.
  bool opensScope() const {
    if (is(TT_TemplateString) && TokenText.endswith("${"))
      return true;
    if (is(TT_DictLiteral) && is(tok::less))
      return true;
    return isOneOf(tok::l_paren, tok::l_brace, tok::l_square,
                   TT_TemplateOpener);
  }
  /// Returns whether \p Tok is )]} or a closing > of a template or in
  /// protos.
  bool closesScope() const {
    if (is(TT_TemplateString) && TokenText.startswith("}"))
      return true;
    if (is(TT_DictLiteral) && is(tok::greater))
      return true;
    return isOneOf(tok::r_paren, tok::r_brace, tok::r_square,
                   TT_TemplateCloser);
  }

  /// Returns \c true if this is a "." or "->" accessing a member.
  bool isMemberAccess() const {
    return isOneOf(tok::arrow, tok::period, tok::arrowstar) &&
           !isOneOf(TT_DesignatedInitializerPeriod, TT_TrailingReturnArrow,
                    TT_LambdaArrow);
  }

  bool isUnaryOperator() const {
    switch (Tok.getKind()) {
    case tok::plus:
    case tok::plusplus:
    case tok::minus:
    case tok::minusminus:
    case tok::exclaim:
    case tok::tilde:
    case tok::kw_sizeof:
    case tok::kw_alignof:
      return true;
    default:
      return false;
    }
  }

  bool isBinaryOperator() const {
    // Comma is a binary operator, but does not behave as such wrt. formatting.
    return getPrecedence() > prec::Comma;
  }

  bool isTrailingComment() const {
    return is(tok::comment) &&
           (is(TT_LineComment) || !Next || Next->NewlinesBefore > 0);
  }

  /// Returns \c true if this is a keyword that can be used
  /// like a function call (e.g. sizeof, typeid, ...).
  bool isFunctionLikeKeyword() const {
    switch (Tok.getKind()) {
    case tok::kw_throw:
    case tok::kw_typeid:
    case tok::kw_return:
    case tok::kw_sizeof:
    case tok::kw_alignof:
    case tok::kw_alignas:
    case tok::kw_decltype:
    case tok::kw_noexcept:
    case tok::kw_static_assert:
    case tok::kw___attribute:
      return true;
    default:
      return false;
    }
  }

  /// Returns \c true if this is a string literal that's like a label,
  /// e.g. ends with "=" or ":".
  bool isLabelString() const {
    if (!is(tok::string_literal))
      return false;
    StringRef Content = TokenText;
    if (Content.startswith("\"") || Content.startswith("'"))
      Content = Content.drop_front(1);
    if (Content.endswith("\"") || Content.endswith("'"))
      Content = Content.drop_back(1);
    Content = Content.trim();
    return Content.size() > 1 &&
           (Content.back() == ':' || Content.back() == '=');
  }

  /// Returns actual token start location without leading escaped
  /// newlines and whitespace.
  ///
  /// This can be different to Tok.getLocation(), which includes leading escaped
  /// newlines.
  SourceLocation getStartOfNonWhitespace() const {
    return WhitespaceRange.getEnd();
  }

  prec::Level getPrecedence() const {
    return getBinOpPrecedence(Tok.getKind(), /*GreaterThanIsOperator=*/true,
                              /*CPlusPlus11=*/true);
  }

  /// Returns the previous token ignoring comments.
  FormatToken *getPreviousNonComment() const {
    FormatToken *Tok = Previous;
    while (Tok && Tok->is(tok::comment))
      Tok = Tok->Previous;
    return Tok;
  }

  /// Returns the next token ignoring comments.
  const FormatToken *getNextNonComment() const {
    const FormatToken *Tok = Next;
    while (Tok && Tok->is(tok::comment))
      Tok = Tok->Next;
    return Tok;
  }

  /// Returns \c true if this tokens starts a block-type list, i.e. a
  /// list that should be indented with a block indent.
  bool opensBlockOrBlockTypeList(const FormatStyle &Style) const {
    if (is(TT_TemplateString) && opensScope())
      return true;
    return is(TT_ArrayInitializerLSquare) ||
           is(TT_ProtoExtensionLSquare) ||
           (is(tok::l_brace) &&
            (BlockKind == BK_Block || is(TT_DictLiteral) ||
             (!Style.Cpp11BracedListStyle && NestingLevel == 0))) ||
           (is(tok::less) && (Style.Language == FormatStyle::LK_Proto ||
                              Style.Language == FormatStyle::LK_TextProto));
  }

  /// Returns whether the token is the left square bracket of a C++
  /// structured binding declaration.
  bool isCppStructuredBinding(const FormatStyle &Style) const {
    if (!Style.isCpp() || isNot(tok::l_square))
      return false;
    const FormatToken *T = this;
    do {
      T = T->getPreviousNonComment();
    } while (T && T->isOneOf(tok::kw_const, tok::kw_volatile, tok::amp,
                             tok::ampamp));
    return T && T->is(tok::kw_auto);
  }

  /// Same as opensBlockOrBlockTypeList, but for the closing token.
  bool closesBlockOrBlockTypeList(const FormatStyle &Style) const {
    if (is(TT_TemplateString) && closesScope())
      return true;
    return MatchingParen && MatchingParen->opensBlockOrBlockTypeList(Style);
  }

  /// Return the actual namespace token, if this token starts a namespace
  /// block.
  const FormatToken *getNamespaceToken() const {
    const FormatToken *NamespaceTok = this;
    if (is(tok::comment))
      NamespaceTok = NamespaceTok->getNextNonComment();
    // Detect "(inline|export)? namespace" in the beginning of a line.
    if (NamespaceTok && NamespaceTok->isOneOf(tok::kw_inline, tok::kw_export))
      NamespaceTok = NamespaceTok->getNextNonComment();
    return NamespaceTok && NamespaceTok->is(tok::kw_namespace) ? NamespaceTok
                                                               : nullptr;
  }

private:
  // Disallow copying.
  FormatToken(const FormatToken &) = delete;
  void operator=(const FormatToken &) = delete;

  template <typename A, typename... Ts>
  bool startsSequenceInternal(A K1, Ts... Tokens) const {
    if (is(tok::comment) && Next)
      return Next->startsSequenceInternal(K1, Tokens...);
    return is(K1) && Next && Next->startsSequenceInternal(Tokens...);
  }

  template <typename A> bool startsSequenceInternal(A K1) const {
    if (is(tok::comment) && Next)
      return Next->startsSequenceInternal(K1);
    return is(K1);
  }

  template <typename A, typename... Ts> bool endsSequenceInternal(A K1) const {
    if (is(tok::comment) && Previous)
      return Previous->endsSequenceInternal(K1);
    return is(K1);
  }

  template <typename A, typename... Ts>
  bool endsSequenceInternal(A K1, Ts... Tokens) const {
    if (is(tok::comment) && Previous)
      return Previous->endsSequenceInternal(K1, Tokens...);
    return is(K1) && Previous && Previous->endsSequenceInternal(Tokens...);
  }
};

class ContinuationIndenter;
struct LineState;

class TokenRole {
public:
  TokenRole(const FormatStyle &Style) : Style(Style) {}
  virtual ~TokenRole();

  /// After the \c TokenAnnotator has finished annotating all the tokens,
  /// this function precomputes required information for formatting.
  virtual void precomputeFormattingInfos(const FormatToken *Token);

  /// Apply the special formatting that the given role demands.
  ///
  /// Assumes that the token having this role is already formatted.
  ///
  /// Continues formatting from \p State leaving indentation to \p Indenter and
  /// returns the total penalty that this formatting incurs.
  virtual unsigned formatFromToken(LineState &State,
                                   ContinuationIndenter *Indenter,
                                   bool DryRun) {
    return 0;
  }

  /// Same as \c formatFromToken, but assumes that the first token has
  /// already been set thereby deciding on the first line break.
  virtual unsigned formatAfterToken(LineState &State,
                                    ContinuationIndenter *Indenter,
                                    bool DryRun) {
    return 0;
  }

  /// Notifies the \c Role that a comma was found.
  virtual void CommaFound(const FormatToken *Token) {}

  virtual const FormatToken *lastComma() { return nullptr; }

protected:
  const FormatStyle &Style;
};

class CommaSeparatedList : public TokenRole {
public:
  CommaSeparatedList(const FormatStyle &Style)
      : TokenRole(Style), HasNestedBracedList(false) {}

  void precomputeFormattingInfos(const FormatToken *Token) override;

  unsigned formatAfterToken(LineState &State, ContinuationIndenter *Indenter,
                            bool DryRun) override;

  unsigned formatFromToken(LineState &State, ContinuationIndenter *Indenter,
                           bool DryRun) override;

  /// Adds \p Token as the next comma to the \c CommaSeparated list.
  void CommaFound(const FormatToken *Token) override {
    Commas.push_back(Token);
  }

  const FormatToken *lastComma() override {
    if (Commas.empty())
      return nullptr;
    return Commas.back();
  }

private:
  /// A struct that holds information on how to format a given list with
  /// a specific number of columns.
  struct ColumnFormat {
    /// The number of columns to use.
    unsigned Columns;

    /// The total width in characters.
    unsigned TotalWidth;

    /// The number of lines required for this format.
    unsigned LineCount;

    /// The size of each column in characters.
    SmallVector<unsigned, 8> ColumnSizes;
  };

  /// Calculate which \c ColumnFormat fits best into
  /// \p RemainingCharacters.
  const ColumnFormat *getColumnFormat(unsigned RemainingCharacters) const;

  /// The ordered \c FormatTokens making up the commas of this list.
  SmallVector<const FormatToken *, 8> Commas;

  /// The length of each of the list's items in characters including the
  /// trailing comma.
  SmallVector<unsigned, 8> ItemLengths;

  /// Precomputed formats that can be used for this list.
  SmallVector<ColumnFormat, 4> Formats;

  bool HasNestedBracedList;
};

/// Encapsulates keywords that are context sensitive or for languages not
/// properly supported by Clang's lexer.
struct AdditionalKeywords {
  AdditionalKeywords(IdentifierTable &IdentTable) {
    kw_final = &IdentTable.get("final");
    kw_override = &IdentTable.get("override");
    kw_in = &IdentTable.get("in");
    kw_of = &IdentTable.get("of");
    kw_CF_ENUM = &IdentTable.get("CF_ENUM");
    kw_CF_OPTIONS = &IdentTable.get("CF_OPTIONS");
    kw_NS_ENUM = &IdentTable.get("NS_ENUM");
    kw_NS_OPTIONS = &IdentTable.get("NS_OPTIONS");

    kw_as = &IdentTable.get("as");
    kw_async = &IdentTable.get("async");
    kw_await = &IdentTable.get("await");
    kw_declare = &IdentTable.get("declare");
    kw_finally = &IdentTable.get("finally");
    kw_from = &IdentTable.get("from");
    kw_function = &IdentTable.get("function");
    kw_get = &IdentTable.get("get");
    kw_import = &IdentTable.get("import");
    kw_infer = &IdentTable.get("infer");
    kw_is = &IdentTable.get("is");
    kw_let = &IdentTable.get("let");
    kw_module = &IdentTable.get("module");
    kw_readonly = &IdentTable.get("readonly");
    kw_set = &IdentTable.get("set");
    kw_type = &IdentTable.get("type");
    kw_typeof = &IdentTable.get("typeof");
    kw_var = &IdentTable.get("var");
    kw_yield = &IdentTable.get("yield");

    kw_abstract = &IdentTable.get("abstract");
    kw_assert = &IdentTable.get("assert");
    kw_extends = &IdentTable.get("extends");
    kw_implements = &IdentTable.get("implements");
    kw_instanceof = &IdentTable.get("instanceof");
    kw_interface = &IdentTable.get("interface");
    kw_native = &IdentTable.get("native");
    kw_package = &IdentTable.get("package");
    kw_synchronized = &IdentTable.get("synchronized");
    kw_throws = &IdentTable.get("throws");
    kw___except = &IdentTable.get("__except");
    kw___has_include = &IdentTable.get("__has_include");
    kw___has_include_next = &IdentTable.get("__has_include_next");

    kw_mark = &IdentTable.get("mark");

    kw_extend = &IdentTable.get("extend");
    kw_option = &IdentTable.get("option");
    kw_optional = &IdentTable.get("optional");
    kw_repeated = &IdentTable.get("repeated");
    kw_required = &IdentTable.get("required");
    kw_returns = &IdentTable.get("returns");

    kw_signals = &IdentTable.get("signals");
    kw_qsignals = &IdentTable.get("Q_SIGNALS");
    kw_slots = &IdentTable.get("slots");
    kw_qslots = &IdentTable.get("Q_SLOTS");

    // Keep this at the end of the constructor to make sure everything here is
    // already initialized.
    JsExtraKeywords = std::unordered_set<IdentifierInfo *>(
        {kw_as, kw_async, kw_await, kw_declare, kw_finally, kw_from,
         kw_function, kw_get, kw_import, kw_is, kw_let, kw_module, kw_readonly,
         kw_set, kw_type, kw_typeof, kw_var, kw_yield,
         // Keywords from the Java section.
         kw_abstract, kw_extends, kw_implements, kw_instanceof, kw_interface});
  }

  // Context sensitive keywords.
  IdentifierInfo *kw_final;
  IdentifierInfo *kw_override;
  IdentifierInfo *kw_in;
  IdentifierInfo *kw_of;
  IdentifierInfo *kw_CF_ENUM;
  IdentifierInfo *kw_CF_OPTIONS;
  IdentifierInfo *kw_NS_ENUM;
  IdentifierInfo *kw_NS_OPTIONS;
  IdentifierInfo *kw___except;
  IdentifierInfo *kw___has_include;
  IdentifierInfo *kw___has_include_next;

  // JavaScript keywords.
  IdentifierInfo *kw_as;
  IdentifierInfo *kw_async;
  IdentifierInfo *kw_await;
  IdentifierInfo *kw_declare;
  IdentifierInfo *kw_finally;
  IdentifierInfo *kw_from;
  IdentifierInfo *kw_function;
  IdentifierInfo *kw_get;
  IdentifierInfo *kw_import;
  IdentifierInfo *kw_infer;
  IdentifierInfo *kw_is;
  IdentifierInfo *kw_let;
  IdentifierInfo *kw_module;
  IdentifierInfo *kw_readonly;
  IdentifierInfo *kw_set;
  IdentifierInfo *kw_type;
  IdentifierInfo *kw_typeof;
  IdentifierInfo *kw_var;
  IdentifierInfo *kw_yield;

  // Java keywords.
  IdentifierInfo *kw_abstract;
  IdentifierInfo *kw_assert;
  IdentifierInfo *kw_extends;
  IdentifierInfo *kw_implements;
  IdentifierInfo *kw_instanceof;
  IdentifierInfo *kw_interface;
  IdentifierInfo *kw_native;
  IdentifierInfo *kw_package;
  IdentifierInfo *kw_synchronized;
  IdentifierInfo *kw_throws;

  // Pragma keywords.
  IdentifierInfo *kw_mark;

  // Proto keywords.
  IdentifierInfo *kw_extend;
  IdentifierInfo *kw_option;
  IdentifierInfo *kw_optional;
  IdentifierInfo *kw_repeated;
  IdentifierInfo *kw_required;
  IdentifierInfo *kw_returns;

  // QT keywords.
  IdentifierInfo *kw_signals;
  IdentifierInfo *kw_qsignals;
  IdentifierInfo *kw_slots;
  IdentifierInfo *kw_qslots;

  /// Returns \c true if \p Tok is a true JavaScript identifier, returns
  /// \c false if it is a keyword or a pseudo keyword.
  bool IsJavaScriptIdentifier(const FormatToken &Tok) const {
    return Tok.is(tok::identifier) &&
           JsExtraKeywords.find(Tok.Tok.getIdentifierInfo()) ==
               JsExtraKeywords.end();
  }

private:
  /// The JavaScript keywords beyond the C++ keyword set.
  std::unordered_set<IdentifierInfo *> JsExtraKeywords;
};

} // namespace format
} // namespace clang

#endif
