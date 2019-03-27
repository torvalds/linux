//===--- Parser.h - C Language Parser ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Parser interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_PARSER_H
#define LLVM_CLANG_PARSE_PARSER_H

#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Availability.h"
#include "clang/Basic/BitmaskEnum.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/OperatorPrecedence.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Lex/CodeCompletionHandler.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SaveAndRestore.h"
#include <memory>
#include <stack>

namespace clang {
  class PragmaHandler;
  class Scope;
  class BalancedDelimiterTracker;
  class CorrectionCandidateCallback;
  class DeclGroupRef;
  class DiagnosticBuilder;
  struct LoopHint;
  class Parser;
  class ParsingDeclRAIIObject;
  class ParsingDeclSpec;
  class ParsingDeclarator;
  class ParsingFieldDeclarator;
  class ColonProtectionRAIIObject;
  class InMessageExpressionRAIIObject;
  class PoisonSEHIdentifiersRAIIObject;
  class OMPClause;
  class ObjCTypeParamList;
  class ObjCTypeParameter;

/// Parser - This implements a parser for the C family of languages.  After
/// parsing units of the grammar, productions are invoked to handle whatever has
/// been read.
///
class Parser : public CodeCompletionHandler {
  friend class ColonProtectionRAIIObject;
  friend class InMessageExpressionRAIIObject;
  friend class PoisonSEHIdentifiersRAIIObject;
  friend class ObjCDeclContextSwitch;
  friend class ParenBraceBracketBalancer;
  friend class BalancedDelimiterTracker;

  Preprocessor &PP;

  /// Tok - The current token we are peeking ahead.  All parsing methods assume
  /// that this is valid.
  Token Tok;

  // PrevTokLocation - The location of the token we previously
  // consumed. This token is used for diagnostics where we expected to
  // see a token following another token (e.g., the ';' at the end of
  // a statement).
  SourceLocation PrevTokLocation;

  unsigned short ParenCount = 0, BracketCount = 0, BraceCount = 0;
  unsigned short MisplacedModuleBeginCount = 0;

  /// Actions - These are the callbacks we invoke as we parse various constructs
  /// in the file.
  Sema &Actions;

  DiagnosticsEngine &Diags;

  /// ScopeCache - Cache scopes to reduce malloc traffic.
  enum { ScopeCacheSize = 16 };
  unsigned NumCachedScopes;
  Scope *ScopeCache[ScopeCacheSize];

  /// Identifiers used for SEH handling in Borland. These are only
  /// allowed in particular circumstances
  // __except block
  IdentifierInfo *Ident__exception_code,
                 *Ident___exception_code,
                 *Ident_GetExceptionCode;
  // __except filter expression
  IdentifierInfo *Ident__exception_info,
                 *Ident___exception_info,
                 *Ident_GetExceptionInfo;
  // __finally
  IdentifierInfo *Ident__abnormal_termination,
                 *Ident___abnormal_termination,
                 *Ident_AbnormalTermination;

  /// Contextual keywords for Microsoft extensions.
  IdentifierInfo *Ident__except;
  mutable IdentifierInfo *Ident_sealed;

  /// Ident_super - IdentifierInfo for "super", to support fast
  /// comparison.
  IdentifierInfo *Ident_super;
  /// Ident_vector, Ident_bool - cached IdentifierInfos for "vector" and
  /// "bool" fast comparison.  Only present if AltiVec or ZVector are enabled.
  IdentifierInfo *Ident_vector;
  IdentifierInfo *Ident_bool;
  /// Ident_pixel - cached IdentifierInfos for "pixel" fast comparison.
  /// Only present if AltiVec enabled.
  IdentifierInfo *Ident_pixel;

  /// Objective-C contextual keywords.
  IdentifierInfo *Ident_instancetype;

  /// Identifier for "introduced".
  IdentifierInfo *Ident_introduced;

  /// Identifier for "deprecated".
  IdentifierInfo *Ident_deprecated;

  /// Identifier for "obsoleted".
  IdentifierInfo *Ident_obsoleted;

  /// Identifier for "unavailable".
  IdentifierInfo *Ident_unavailable;

  /// Identifier for "message".
  IdentifierInfo *Ident_message;

  /// Identifier for "strict".
  IdentifierInfo *Ident_strict;

  /// Identifier for "replacement".
  IdentifierInfo *Ident_replacement;

  /// Identifiers used by the 'external_source_symbol' attribute.
  IdentifierInfo *Ident_language, *Ident_defined_in,
      *Ident_generated_declaration;

  /// C++0x contextual keywords.
  mutable IdentifierInfo *Ident_final;
  mutable IdentifierInfo *Ident_GNU_final;
  mutable IdentifierInfo *Ident_override;

  // C++ type trait keywords that can be reverted to identifiers and still be
  // used as type traits.
  llvm::SmallDenseMap<IdentifierInfo *, tok::TokenKind> RevertibleTypeTraits;

  std::unique_ptr<PragmaHandler> AlignHandler;
  std::unique_ptr<PragmaHandler> GCCVisibilityHandler;
  std::unique_ptr<PragmaHandler> OptionsHandler;
  std::unique_ptr<PragmaHandler> PackHandler;
  std::unique_ptr<PragmaHandler> MSStructHandler;
  std::unique_ptr<PragmaHandler> UnusedHandler;
  std::unique_ptr<PragmaHandler> WeakHandler;
  std::unique_ptr<PragmaHandler> RedefineExtnameHandler;
  std::unique_ptr<PragmaHandler> FPContractHandler;
  std::unique_ptr<PragmaHandler> OpenCLExtensionHandler;
  std::unique_ptr<PragmaHandler> OpenMPHandler;
  std::unique_ptr<PragmaHandler> PCSectionHandler;
  std::unique_ptr<PragmaHandler> MSCommentHandler;
  std::unique_ptr<PragmaHandler> MSDetectMismatchHandler;
  std::unique_ptr<PragmaHandler> MSPointersToMembers;
  std::unique_ptr<PragmaHandler> MSVtorDisp;
  std::unique_ptr<PragmaHandler> MSInitSeg;
  std::unique_ptr<PragmaHandler> MSDataSeg;
  std::unique_ptr<PragmaHandler> MSBSSSeg;
  std::unique_ptr<PragmaHandler> MSConstSeg;
  std::unique_ptr<PragmaHandler> MSCodeSeg;
  std::unique_ptr<PragmaHandler> MSSection;
  std::unique_ptr<PragmaHandler> MSRuntimeChecks;
  std::unique_ptr<PragmaHandler> MSIntrinsic;
  std::unique_ptr<PragmaHandler> MSOptimize;
  std::unique_ptr<PragmaHandler> CUDAForceHostDeviceHandler;
  std::unique_ptr<PragmaHandler> OptimizeHandler;
  std::unique_ptr<PragmaHandler> LoopHintHandler;
  std::unique_ptr<PragmaHandler> UnrollHintHandler;
  std::unique_ptr<PragmaHandler> NoUnrollHintHandler;
  std::unique_ptr<PragmaHandler> UnrollAndJamHintHandler;
  std::unique_ptr<PragmaHandler> NoUnrollAndJamHintHandler;
  std::unique_ptr<PragmaHandler> FPHandler;
  std::unique_ptr<PragmaHandler> STDCFENVHandler;
  std::unique_ptr<PragmaHandler> STDCCXLIMITHandler;
  std::unique_ptr<PragmaHandler> STDCUnknownHandler;
  std::unique_ptr<PragmaHandler> AttributePragmaHandler;

  std::unique_ptr<CommentHandler> CommentSemaHandler;

  /// Whether the '>' token acts as an operator or not. This will be
  /// true except when we are parsing an expression within a C++
  /// template argument list, where the '>' closes the template
  /// argument list.
  bool GreaterThanIsOperator;

  /// ColonIsSacred - When this is false, we aggressively try to recover from
  /// code like "foo : bar" as if it were a typo for "foo :: bar".  This is not
  /// safe in case statements and a few other things.  This is managed by the
  /// ColonProtectionRAIIObject RAII object.
  bool ColonIsSacred;

  /// When true, we are directly inside an Objective-C message
  /// send expression.
  ///
  /// This is managed by the \c InMessageExpressionRAIIObject class, and
  /// should not be set directly.
  bool InMessageExpression;

  /// Gets set to true after calling ProduceSignatureHelp, it is for a
  /// workaround to make sure ProduceSignatureHelp is only called at the deepest
  /// function call.
  bool CalledSignatureHelp = false;

  /// The "depth" of the template parameters currently being parsed.
  unsigned TemplateParameterDepth;

  /// RAII class that manages the template parameter depth.
  class TemplateParameterDepthRAII {
    unsigned &Depth;
    unsigned AddedLevels;
  public:
    explicit TemplateParameterDepthRAII(unsigned &Depth)
      : Depth(Depth), AddedLevels(0) {}

    ~TemplateParameterDepthRAII() {
      Depth -= AddedLevels;
    }

    void operator++() {
      ++Depth;
      ++AddedLevels;
    }
    void addDepth(unsigned D) {
      Depth += D;
      AddedLevels += D;
    }
    unsigned getDepth() const { return Depth; }
  };

  /// Factory object for creating ParsedAttr objects.
  AttributeFactory AttrFactory;

  /// Gathers and cleans up TemplateIdAnnotations when parsing of a
  /// top-level declaration is finished.
  SmallVector<TemplateIdAnnotation *, 16> TemplateIds;

  /// Identifiers which have been declared within a tentative parse.
  SmallVector<IdentifierInfo *, 8> TentativelyDeclaredIdentifiers;

  /// Tracker for '<' tokens that might have been intended to be treated as an
  /// angle bracket instead of a less-than comparison.
  ///
  /// This happens when the user intends to form a template-id, but typoes the
  /// template-name or forgets a 'template' keyword for a dependent template
  /// name.
  ///
  /// We track these locations from the point where we see a '<' with a
  /// name-like expression on its left until we see a '>' or '>>' that might
  /// match it.
  struct AngleBracketTracker {
    /// Flags used to rank candidate template names when there is more than one
    /// '<' in a scope.
    enum Priority : unsigned short {
      /// A non-dependent name that is a potential typo for a template name.
      PotentialTypo = 0x0,
      /// A dependent name that might instantiate to a template-name.
      DependentName = 0x2,

      /// A space appears before the '<' token.
      SpaceBeforeLess = 0x0,
      /// No space before the '<' token
      NoSpaceBeforeLess = 0x1,

      LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue*/ DependentName)
    };

    struct Loc {
      Expr *TemplateName;
      SourceLocation LessLoc;
      AngleBracketTracker::Priority Priority;
      unsigned short ParenCount, BracketCount, BraceCount;

      bool isActive(Parser &P) const {
        return P.ParenCount == ParenCount && P.BracketCount == BracketCount &&
               P.BraceCount == BraceCount;
      }

      bool isActiveOrNested(Parser &P) const {
        return isActive(P) || P.ParenCount > ParenCount ||
               P.BracketCount > BracketCount || P.BraceCount > BraceCount;
      }
    };

    SmallVector<Loc, 8> Locs;

    /// Add an expression that might have been intended to be a template name.
    /// In the case of ambiguity, we arbitrarily select the innermost such
    /// expression, for example in 'foo < bar < baz', 'bar' is the current
    /// candidate. No attempt is made to track that 'foo' is also a candidate
    /// for the case where we see a second suspicious '>' token.
    void add(Parser &P, Expr *TemplateName, SourceLocation LessLoc,
             Priority Prio) {
      if (!Locs.empty() && Locs.back().isActive(P)) {
        if (Locs.back().Priority <= Prio) {
          Locs.back().TemplateName = TemplateName;
          Locs.back().LessLoc = LessLoc;
          Locs.back().Priority = Prio;
        }
      } else {
        Locs.push_back({TemplateName, LessLoc, Prio,
                        P.ParenCount, P.BracketCount, P.BraceCount});
      }
    }

    /// Mark the current potential missing template location as having been
    /// handled (this happens if we pass a "corresponding" '>' or '>>' token
    /// or leave a bracket scope).
    void clear(Parser &P) {
      while (!Locs.empty() && Locs.back().isActiveOrNested(P))
        Locs.pop_back();
    }

    /// Get the current enclosing expression that might hve been intended to be
    /// a template name.
    Loc *getCurrent(Parser &P) {
      if (!Locs.empty() && Locs.back().isActive(P))
        return &Locs.back();
      return nullptr;
    }
  };

  AngleBracketTracker AngleBrackets;

  IdentifierInfo *getSEHExceptKeyword();

  /// True if we are within an Objective-C container while parsing C-like decls.
  ///
  /// This is necessary because Sema thinks we have left the container
  /// to parse the C-like decls, meaning Actions.getObjCDeclContext() will
  /// be NULL.
  bool ParsingInObjCContainer;

  /// Whether to skip parsing of function bodies.
  ///
  /// This option can be used, for example, to speed up searches for
  /// declarations/definitions when indexing.
  bool SkipFunctionBodies;

  /// The location of the expression statement that is being parsed right now.
  /// Used to determine if an expression that is being parsed is a statement or
  /// just a regular sub-expression.
  SourceLocation ExprStatementTokLoc;

public:
  Parser(Preprocessor &PP, Sema &Actions, bool SkipFunctionBodies);
  ~Parser() override;

  const LangOptions &getLangOpts() const { return PP.getLangOpts(); }
  const TargetInfo &getTargetInfo() const { return PP.getTargetInfo(); }
  Preprocessor &getPreprocessor() const { return PP; }
  Sema &getActions() const { return Actions; }
  AttributeFactory &getAttrFactory() { return AttrFactory; }

  const Token &getCurToken() const { return Tok; }
  Scope *getCurScope() const { return Actions.getCurScope(); }
  void incrementMSManglingNumber() const {
    return Actions.incrementMSManglingNumber();
  }

  Decl  *getObjCDeclContext() const { return Actions.getObjCDeclContext(); }

  // Type forwarding.  All of these are statically 'void*', but they may all be
  // different actual classes based on the actions in place.
  typedef OpaquePtr<DeclGroupRef> DeclGroupPtrTy;
  typedef OpaquePtr<TemplateName> TemplateTy;

  typedef SmallVector<TemplateParameterList *, 4> TemplateParameterLists;

  typedef Sema::FullExprArg FullExprArg;

  // Parsing methods.

  /// Initialize - Warm up the parser.
  ///
  void Initialize();

  /// Parse the first top-level declaration in a translation unit.
  bool ParseFirstTopLevelDecl(DeclGroupPtrTy &Result);

  /// ParseTopLevelDecl - Parse one top-level declaration. Returns true if
  /// the EOF was encountered.
  bool ParseTopLevelDecl(DeclGroupPtrTy &Result);
  bool ParseTopLevelDecl() {
    DeclGroupPtrTy Result;
    return ParseTopLevelDecl(Result);
  }

  /// ConsumeToken - Consume the current 'peek token' and lex the next one.
  /// This does not work with special tokens: string literals, code completion,
  /// annotation tokens and balanced tokens must be handled using the specific
  /// consume methods.
  /// Returns the location of the consumed token.
  SourceLocation ConsumeToken() {
    assert(!isTokenSpecial() &&
           "Should consume special tokens with Consume*Token");
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  bool TryConsumeToken(tok::TokenKind Expected) {
    if (Tok.isNot(Expected))
      return false;
    assert(!isTokenSpecial() &&
           "Should consume special tokens with Consume*Token");
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return true;
  }

  bool TryConsumeToken(tok::TokenKind Expected, SourceLocation &Loc) {
    if (!TryConsumeToken(Expected))
      return false;
    Loc = PrevTokLocation;
    return true;
  }

  /// ConsumeAnyToken - Dispatch to the right Consume* method based on the
  /// current token type.  This should only be used in cases where the type of
  /// the token really isn't known, e.g. in error recovery.
  SourceLocation ConsumeAnyToken(bool ConsumeCodeCompletionTok = false) {
    if (isTokenParen())
      return ConsumeParen();
    if (isTokenBracket())
      return ConsumeBracket();
    if (isTokenBrace())
      return ConsumeBrace();
    if (isTokenStringLiteral())
      return ConsumeStringToken();
    if (Tok.is(tok::code_completion))
      return ConsumeCodeCompletionTok ? ConsumeCodeCompletionToken()
                                      : handleUnexpectedCodeCompletionToken();
    if (Tok.isAnnotation())
      return ConsumeAnnotationToken();
    return ConsumeToken();
  }


  SourceLocation getEndOfPreviousToken() {
    return PP.getLocForEndOfToken(PrevTokLocation);
  }

  /// Retrieve the underscored keyword (_Nonnull, _Nullable) that corresponds
  /// to the given nullability kind.
  IdentifierInfo *getNullabilityKeyword(NullabilityKind nullability) {
    return Actions.getNullabilityKeyword(nullability);
  }

private:
  //===--------------------------------------------------------------------===//
  // Low-Level token peeking and consumption methods.
  //

  /// isTokenParen - Return true if the cur token is '(' or ')'.
  bool isTokenParen() const {
    return Tok.isOneOf(tok::l_paren, tok::r_paren);
  }
  /// isTokenBracket - Return true if the cur token is '[' or ']'.
  bool isTokenBracket() const {
    return Tok.isOneOf(tok::l_square, tok::r_square);
  }
  /// isTokenBrace - Return true if the cur token is '{' or '}'.
  bool isTokenBrace() const {
    return Tok.isOneOf(tok::l_brace, tok::r_brace);
  }
  /// isTokenStringLiteral - True if this token is a string-literal.
  bool isTokenStringLiteral() const {
    return tok::isStringLiteral(Tok.getKind());
  }
  /// isTokenSpecial - True if this token requires special consumption methods.
  bool isTokenSpecial() const {
    return isTokenStringLiteral() || isTokenParen() || isTokenBracket() ||
           isTokenBrace() || Tok.is(tok::code_completion) || Tok.isAnnotation();
  }

  /// Returns true if the current token is '=' or is a type of '='.
  /// For typos, give a fixit to '='
  bool isTokenEqualOrEqualTypo();

  /// Return the current token to the token stream and make the given
  /// token the current token.
  void UnconsumeToken(Token &Consumed) {
      Token Next = Tok;
      PP.EnterToken(Consumed);
      PP.Lex(Tok);
      PP.EnterToken(Next);
  }

  SourceLocation ConsumeAnnotationToken() {
    assert(Tok.isAnnotation() && "wrong consume method");
    SourceLocation Loc = Tok.getLocation();
    PrevTokLocation = Tok.getAnnotationEndLoc();
    PP.Lex(Tok);
    return Loc;
  }

  /// ConsumeParen - This consume method keeps the paren count up-to-date.
  ///
  SourceLocation ConsumeParen() {
    assert(isTokenParen() && "wrong consume method");
    if (Tok.getKind() == tok::l_paren)
      ++ParenCount;
    else if (ParenCount) {
      AngleBrackets.clear(*this);
      --ParenCount;       // Don't let unbalanced )'s drive the count negative.
    }
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  /// ConsumeBracket - This consume method keeps the bracket count up-to-date.
  ///
  SourceLocation ConsumeBracket() {
    assert(isTokenBracket() && "wrong consume method");
    if (Tok.getKind() == tok::l_square)
      ++BracketCount;
    else if (BracketCount) {
      AngleBrackets.clear(*this);
      --BracketCount;     // Don't let unbalanced ]'s drive the count negative.
    }

    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  /// ConsumeBrace - This consume method keeps the brace count up-to-date.
  ///
  SourceLocation ConsumeBrace() {
    assert(isTokenBrace() && "wrong consume method");
    if (Tok.getKind() == tok::l_brace)
      ++BraceCount;
    else if (BraceCount) {
      AngleBrackets.clear(*this);
      --BraceCount;     // Don't let unbalanced }'s drive the count negative.
    }

    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  /// ConsumeStringToken - Consume the current 'peek token', lexing a new one
  /// and returning the token kind.  This method is specific to strings, as it
  /// handles string literal concatenation, as per C99 5.1.1.2, translation
  /// phase #6.
  SourceLocation ConsumeStringToken() {
    assert(isTokenStringLiteral() &&
           "Should only consume string literals with this method");
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  /// Consume the current code-completion token.
  ///
  /// This routine can be called to consume the code-completion token and
  /// continue processing in special cases where \c cutOffParsing() isn't
  /// desired, such as token caching or completion with lookahead.
  SourceLocation ConsumeCodeCompletionToken() {
    assert(Tok.is(tok::code_completion));
    PrevTokLocation = Tok.getLocation();
    PP.Lex(Tok);
    return PrevTokLocation;
  }

  ///\ brief When we are consuming a code-completion token without having
  /// matched specific position in the grammar, provide code-completion results
  /// based on context.
  ///
  /// \returns the source location of the code-completion token.
  SourceLocation handleUnexpectedCodeCompletionToken();

  /// Abruptly cut off parsing; mainly used when we have reached the
  /// code-completion point.
  void cutOffParsing() {
    if (PP.isCodeCompletionEnabled())
      PP.setCodeCompletionReached();
    // Cut off parsing by acting as if we reached the end-of-file.
    Tok.setKind(tok::eof);
  }

  /// Determine if we're at the end of the file or at a transition
  /// between modules.
  bool isEofOrEom() {
    tok::TokenKind Kind = Tok.getKind();
    return Kind == tok::eof || Kind == tok::annot_module_begin ||
           Kind == tok::annot_module_end || Kind == tok::annot_module_include;
  }

  /// Checks if the \p Level is valid for use in a fold expression.
  bool isFoldOperator(prec::Level Level) const;

  /// Checks if the \p Kind is a valid operator for fold expressions.
  bool isFoldOperator(tok::TokenKind Kind) const;

  /// Initialize all pragma handlers.
  void initializePragmaHandlers();

  /// Destroy and reset all pragma handlers.
  void resetPragmaHandlers();

  /// Handle the annotation token produced for #pragma unused(...)
  void HandlePragmaUnused();

  /// Handle the annotation token produced for
  /// #pragma GCC visibility...
  void HandlePragmaVisibility();

  /// Handle the annotation token produced for
  /// #pragma pack...
  void HandlePragmaPack();

  /// Handle the annotation token produced for
  /// #pragma ms_struct...
  void HandlePragmaMSStruct();

  /// Handle the annotation token produced for
  /// #pragma comment...
  void HandlePragmaMSComment();

  void HandlePragmaMSPointersToMembers();

  void HandlePragmaMSVtorDisp();

  void HandlePragmaMSPragma();
  bool HandlePragmaMSSection(StringRef PragmaName,
                             SourceLocation PragmaLocation);
  bool HandlePragmaMSSegment(StringRef PragmaName,
                             SourceLocation PragmaLocation);
  bool HandlePragmaMSInitSeg(StringRef PragmaName,
                             SourceLocation PragmaLocation);

  /// Handle the annotation token produced for
  /// #pragma align...
  void HandlePragmaAlign();

  /// Handle the annotation token produced for
  /// #pragma clang __debug dump...
  void HandlePragmaDump();

  /// Handle the annotation token produced for
  /// #pragma weak id...
  void HandlePragmaWeak();

  /// Handle the annotation token produced for
  /// #pragma weak id = id...
  void HandlePragmaWeakAlias();

  /// Handle the annotation token produced for
  /// #pragma redefine_extname...
  void HandlePragmaRedefineExtname();

  /// Handle the annotation token produced for
  /// #pragma STDC FP_CONTRACT...
  void HandlePragmaFPContract();

  /// Handle the annotation token produced for
  /// #pragma STDC FENV_ACCESS...
  void HandlePragmaFEnvAccess();

  /// \brief Handle the annotation token produced for
  /// #pragma clang fp ...
  void HandlePragmaFP();

  /// Handle the annotation token produced for
  /// #pragma OPENCL EXTENSION...
  void HandlePragmaOpenCLExtension();

  /// Handle the annotation token produced for
  /// #pragma clang __debug captured
  StmtResult HandlePragmaCaptured();

  /// Handle the annotation token produced for
  /// #pragma clang loop and #pragma unroll.
  bool HandlePragmaLoopHint(LoopHint &Hint);

  bool ParsePragmaAttributeSubjectMatchRuleSet(
      attr::ParsedSubjectMatchRuleSet &SubjectMatchRules,
      SourceLocation &AnyLoc, SourceLocation &LastMatchRuleEndLoc);

  void HandlePragmaAttribute();

  /// GetLookAheadToken - This peeks ahead N tokens and returns that token
  /// without consuming any tokens.  LookAhead(0) returns 'Tok', LookAhead(1)
  /// returns the token after Tok, etc.
  ///
  /// Note that this differs from the Preprocessor's LookAhead method, because
  /// the Parser always has one token lexed that the preprocessor doesn't.
  ///
  const Token &GetLookAheadToken(unsigned N) {
    if (N == 0 || Tok.is(tok::eof)) return Tok;
    return PP.LookAhead(N-1);
  }

public:
  /// NextToken - This peeks ahead one token and returns it without
  /// consuming it.
  const Token &NextToken() {
    return PP.LookAhead(0);
  }

  /// getTypeAnnotation - Read a parsed type out of an annotation token.
  static ParsedType getTypeAnnotation(const Token &Tok) {
    return ParsedType::getFromOpaquePtr(Tok.getAnnotationValue());
  }

private:
  static void setTypeAnnotation(Token &Tok, ParsedType T) {
    Tok.setAnnotationValue(T.getAsOpaquePtr());
  }

  /// Read an already-translated primary expression out of an annotation
  /// token.
  static ExprResult getExprAnnotation(const Token &Tok) {
    return ExprResult::getFromOpaquePointer(Tok.getAnnotationValue());
  }

  /// Set the primary expression corresponding to the given annotation
  /// token.
  static void setExprAnnotation(Token &Tok, ExprResult ER) {
    Tok.setAnnotationValue(ER.getAsOpaquePointer());
  }

public:
  // If NeedType is true, then TryAnnotateTypeOrScopeToken will try harder to
  // find a type name by attempting typo correction.
  bool TryAnnotateTypeOrScopeToken();
  bool TryAnnotateTypeOrScopeTokenAfterScopeSpec(CXXScopeSpec &SS,
                                                 bool IsNewScope);
  bool TryAnnotateCXXScopeToken(bool EnteringContext = false);

private:
  enum AnnotatedNameKind {
    /// Annotation has failed and emitted an error.
    ANK_Error,
    /// The identifier is a tentatively-declared name.
    ANK_TentativeDecl,
    /// The identifier is a template name. FIXME: Add an annotation for that.
    ANK_TemplateName,
    /// The identifier can't be resolved.
    ANK_Unresolved,
    /// Annotation was successful.
    ANK_Success
  };
  AnnotatedNameKind
  TryAnnotateName(bool IsAddressOfOperand,
                  std::unique_ptr<CorrectionCandidateCallback> CCC = nullptr);

  /// Push a tok::annot_cxxscope token onto the token stream.
  void AnnotateScopeToken(CXXScopeSpec &SS, bool IsNewAnnotation);

  /// TryAltiVecToken - Check for context-sensitive AltiVec identifier tokens,
  /// replacing them with the non-context-sensitive keywords.  This returns
  /// true if the token was replaced.
  bool TryAltiVecToken(DeclSpec &DS, SourceLocation Loc,
                       const char *&PrevSpec, unsigned &DiagID,
                       bool &isInvalid) {
    if (!getLangOpts().AltiVec && !getLangOpts().ZVector)
      return false;

    if (Tok.getIdentifierInfo() != Ident_vector &&
        Tok.getIdentifierInfo() != Ident_bool &&
        (!getLangOpts().AltiVec || Tok.getIdentifierInfo() != Ident_pixel))
      return false;

    return TryAltiVecTokenOutOfLine(DS, Loc, PrevSpec, DiagID, isInvalid);
  }

  /// TryAltiVecVectorToken - Check for context-sensitive AltiVec vector
  /// identifier token, replacing it with the non-context-sensitive __vector.
  /// This returns true if the token was replaced.
  bool TryAltiVecVectorToken() {
    if ((!getLangOpts().AltiVec && !getLangOpts().ZVector) ||
        Tok.getIdentifierInfo() != Ident_vector) return false;
    return TryAltiVecVectorTokenOutOfLine();
  }

  bool TryAltiVecVectorTokenOutOfLine();
  bool TryAltiVecTokenOutOfLine(DeclSpec &DS, SourceLocation Loc,
                                const char *&PrevSpec, unsigned &DiagID,
                                bool &isInvalid);

  /// Returns true if the current token is the identifier 'instancetype'.
  ///
  /// Should only be used in Objective-C language modes.
  bool isObjCInstancetype() {
    assert(getLangOpts().ObjC);
    if (Tok.isAnnotation())
      return false;
    if (!Ident_instancetype)
      Ident_instancetype = PP.getIdentifierInfo("instancetype");
    return Tok.getIdentifierInfo() == Ident_instancetype;
  }

  /// TryKeywordIdentFallback - For compatibility with system headers using
  /// keywords as identifiers, attempt to convert the current token to an
  /// identifier and optionally disable the keyword for the remainder of the
  /// translation unit. This returns false if the token was not replaced,
  /// otherwise emits a diagnostic and returns true.
  bool TryKeywordIdentFallback(bool DisableKeyword);

  /// Get the TemplateIdAnnotation from the token.
  TemplateIdAnnotation *takeTemplateIdAnnotation(const Token &tok);

  /// TentativeParsingAction - An object that is used as a kind of "tentative
  /// parsing transaction". It gets instantiated to mark the token position and
  /// after the token consumption is done, Commit() or Revert() is called to
  /// either "commit the consumed tokens" or revert to the previously marked
  /// token position. Example:
  ///
  ///   TentativeParsingAction TPA(*this);
  ///   ConsumeToken();
  ///   ....
  ///   TPA.Revert();
  ///
  class TentativeParsingAction {
    Parser &P;
    Token PrevTok;
    size_t PrevTentativelyDeclaredIdentifierCount;
    unsigned short PrevParenCount, PrevBracketCount, PrevBraceCount;
    bool isActive;

  public:
    explicit TentativeParsingAction(Parser& p) : P(p) {
      PrevTok = P.Tok;
      PrevTentativelyDeclaredIdentifierCount =
          P.TentativelyDeclaredIdentifiers.size();
      PrevParenCount = P.ParenCount;
      PrevBracketCount = P.BracketCount;
      PrevBraceCount = P.BraceCount;
      P.PP.EnableBacktrackAtThisPos();
      isActive = true;
    }
    void Commit() {
      assert(isActive && "Parsing action was finished!");
      P.TentativelyDeclaredIdentifiers.resize(
          PrevTentativelyDeclaredIdentifierCount);
      P.PP.CommitBacktrackedTokens();
      isActive = false;
    }
    void Revert() {
      assert(isActive && "Parsing action was finished!");
      P.PP.Backtrack();
      P.Tok = PrevTok;
      P.TentativelyDeclaredIdentifiers.resize(
          PrevTentativelyDeclaredIdentifierCount);
      P.ParenCount = PrevParenCount;
      P.BracketCount = PrevBracketCount;
      P.BraceCount = PrevBraceCount;
      isActive = false;
    }
    ~TentativeParsingAction() {
      assert(!isActive && "Forgot to call Commit or Revert!");
    }
  };
  /// A TentativeParsingAction that automatically reverts in its destructor.
  /// Useful for disambiguation parses that will always be reverted.
  class RevertingTentativeParsingAction
      : private Parser::TentativeParsingAction {
  public:
    RevertingTentativeParsingAction(Parser &P)
        : Parser::TentativeParsingAction(P) {}
    ~RevertingTentativeParsingAction() { Revert(); }
  };

  class UnannotatedTentativeParsingAction;

  /// ObjCDeclContextSwitch - An object used to switch context from
  /// an objective-c decl context to its enclosing decl context and
  /// back.
  class ObjCDeclContextSwitch {
    Parser &P;
    Decl *DC;
    SaveAndRestore<bool> WithinObjCContainer;
  public:
    explicit ObjCDeclContextSwitch(Parser &p)
      : P(p), DC(p.getObjCDeclContext()),
        WithinObjCContainer(P.ParsingInObjCContainer, DC != nullptr) {
      if (DC)
        P.Actions.ActOnObjCTemporaryExitContainerContext(cast<DeclContext>(DC));
    }
    ~ObjCDeclContextSwitch() {
      if (DC)
        P.Actions.ActOnObjCReenterContainerContext(cast<DeclContext>(DC));
    }
  };

  /// ExpectAndConsume - The parser expects that 'ExpectedTok' is next in the
  /// input.  If so, it is consumed and false is returned.
  ///
  /// If a trivial punctuator misspelling is encountered, a FixIt error
  /// diagnostic is issued and false is returned after recovery.
  ///
  /// If the input is malformed, this emits the specified diagnostic and true is
  /// returned.
  bool ExpectAndConsume(tok::TokenKind ExpectedTok,
                        unsigned Diag = diag::err_expected,
                        StringRef DiagMsg = "");

  /// The parser expects a semicolon and, if present, will consume it.
  ///
  /// If the next token is not a semicolon, this emits the specified diagnostic,
  /// or, if there's just some closing-delimiter noise (e.g., ')' or ']') prior
  /// to the semicolon, consumes that extra token.
  bool ExpectAndConsumeSemi(unsigned DiagID);

  /// The kind of extra semi diagnostic to emit.
  enum ExtraSemiKind {
    OutsideFunction = 0,
    InsideStruct = 1,
    InstanceVariableList = 2,
    AfterMemberFunctionDefinition = 3
  };

  /// Consume any extra semi-colons until the end of the line.
  void ConsumeExtraSemi(ExtraSemiKind Kind, unsigned TST = TST_unspecified);

  /// Return false if the next token is an identifier. An 'expected identifier'
  /// error is emitted otherwise.
  ///
  /// The parser tries to recover from the error by checking if the next token
  /// is a C++ keyword when parsing Objective-C++. Return false if the recovery
  /// was successful.
  bool expectIdentifier();

public:
  //===--------------------------------------------------------------------===//
  // Scope manipulation

  /// ParseScope - Introduces a new scope for parsing. The kind of
  /// scope is determined by ScopeFlags. Objects of this type should
  /// be created on the stack to coincide with the position where the
  /// parser enters the new scope, and this object's constructor will
  /// create that new scope. Similarly, once the object is destroyed
  /// the parser will exit the scope.
  class ParseScope {
    Parser *Self;
    ParseScope(const ParseScope &) = delete;
    void operator=(const ParseScope &) = delete;

  public:
    // ParseScope - Construct a new object to manage a scope in the
    // parser Self where the new Scope is created with the flags
    // ScopeFlags, but only when we aren't about to enter a compound statement.
    ParseScope(Parser *Self, unsigned ScopeFlags, bool EnteredScope = true,
               bool BeforeCompoundStmt = false)
      : Self(Self) {
      if (EnteredScope && !BeforeCompoundStmt)
        Self->EnterScope(ScopeFlags);
      else {
        if (BeforeCompoundStmt)
          Self->incrementMSManglingNumber();

        this->Self = nullptr;
      }
    }

    // Exit - Exit the scope associated with this object now, rather
    // than waiting until the object is destroyed.
    void Exit() {
      if (Self) {
        Self->ExitScope();
        Self = nullptr;
      }
    }

    ~ParseScope() {
      Exit();
    }
  };

  /// EnterScope - Start a new scope.
  void EnterScope(unsigned ScopeFlags);

  /// ExitScope - Pop a scope off the scope stack.
  void ExitScope();

private:
  /// RAII object used to modify the scope flags for the current scope.
  class ParseScopeFlags {
    Scope *CurScope;
    unsigned OldFlags;
    ParseScopeFlags(const ParseScopeFlags &) = delete;
    void operator=(const ParseScopeFlags &) = delete;

  public:
    ParseScopeFlags(Parser *Self, unsigned ScopeFlags, bool ManageFlags = true);
    ~ParseScopeFlags();
  };

  //===--------------------------------------------------------------------===//
  // Diagnostic Emission and Error recovery.

public:
  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID);
  DiagnosticBuilder Diag(const Token &Tok, unsigned DiagID);
  DiagnosticBuilder Diag(unsigned DiagID) {
    return Diag(Tok, DiagID);
  }

private:
  void SuggestParentheses(SourceLocation Loc, unsigned DK,
                          SourceRange ParenRange);
  void CheckNestedObjCContexts(SourceLocation AtLoc);

public:

  /// Control flags for SkipUntil functions.
  enum SkipUntilFlags {
    StopAtSemi = 1 << 0,  ///< Stop skipping at semicolon
    /// Stop skipping at specified token, but don't skip the token itself
    StopBeforeMatch = 1 << 1,
    StopAtCodeCompletion = 1 << 2 ///< Stop at code completion
  };

  friend constexpr SkipUntilFlags operator|(SkipUntilFlags L,
                                            SkipUntilFlags R) {
    return static_cast<SkipUntilFlags>(static_cast<unsigned>(L) |
                                       static_cast<unsigned>(R));
  }

  /// SkipUntil - Read tokens until we get to the specified token, then consume
  /// it (unless StopBeforeMatch is specified).  Because we cannot guarantee
  /// that the token will ever occur, this skips to the next token, or to some
  /// likely good stopping point.  If Flags has StopAtSemi flag, skipping will
  /// stop at a ';' character.
  ///
  /// If SkipUntil finds the specified token, it returns true, otherwise it
  /// returns false.
  bool SkipUntil(tok::TokenKind T,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0)) {
    return SkipUntil(llvm::makeArrayRef(T), Flags);
  }
  bool SkipUntil(tok::TokenKind T1, tok::TokenKind T2,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0)) {
    tok::TokenKind TokArray[] = {T1, T2};
    return SkipUntil(TokArray, Flags);
  }
  bool SkipUntil(tok::TokenKind T1, tok::TokenKind T2, tok::TokenKind T3,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0)) {
    tok::TokenKind TokArray[] = {T1, T2, T3};
    return SkipUntil(TokArray, Flags);
  }
  bool SkipUntil(ArrayRef<tok::TokenKind> Toks,
                 SkipUntilFlags Flags = static_cast<SkipUntilFlags>(0));

  /// SkipMalformedDecl - Read tokens until we get to some likely good stopping
  /// point for skipping past a simple-declaration.
  void SkipMalformedDecl();

private:
  //===--------------------------------------------------------------------===//
  // Lexing and parsing of C++ inline methods.

  struct ParsingClass;

  /// [class.mem]p1: "... the class is regarded as complete within
  /// - function bodies
  /// - default arguments
  /// - exception-specifications (TODO: C++0x)
  /// - and brace-or-equal-initializers for non-static data members
  /// (including such things in nested classes)."
  /// LateParsedDeclarations build the tree of those elements so they can
  /// be parsed after parsing the top-level class.
  class LateParsedDeclaration {
  public:
    virtual ~LateParsedDeclaration();

    virtual void ParseLexedMethodDeclarations();
    virtual void ParseLexedMemberInitializers();
    virtual void ParseLexedMethodDefs();
    virtual void ParseLexedAttributes();
  };

  /// Inner node of the LateParsedDeclaration tree that parses
  /// all its members recursively.
  class LateParsedClass : public LateParsedDeclaration {
  public:
    LateParsedClass(Parser *P, ParsingClass *C);
    ~LateParsedClass() override;

    void ParseLexedMethodDeclarations() override;
    void ParseLexedMemberInitializers() override;
    void ParseLexedMethodDefs() override;
    void ParseLexedAttributes() override;

  private:
    Parser *Self;
    ParsingClass *Class;
  };

  /// Contains the lexed tokens of an attribute with arguments that
  /// may reference member variables and so need to be parsed at the
  /// end of the class declaration after parsing all other member
  /// member declarations.
  /// FIXME: Perhaps we should change the name of LateParsedDeclaration to
  /// LateParsedTokens.
  struct LateParsedAttribute : public LateParsedDeclaration {
    Parser *Self;
    CachedTokens Toks;
    IdentifierInfo &AttrName;
    SourceLocation AttrNameLoc;
    SmallVector<Decl*, 2> Decls;

    explicit LateParsedAttribute(Parser *P, IdentifierInfo &Name,
                                 SourceLocation Loc)
      : Self(P), AttrName(Name), AttrNameLoc(Loc) {}

    void ParseLexedAttributes() override;

    void addDecl(Decl *D) { Decls.push_back(D); }
  };

  // A list of late-parsed attributes.  Used by ParseGNUAttributes.
  class LateParsedAttrList: public SmallVector<LateParsedAttribute *, 2> {
  public:
    LateParsedAttrList(bool PSoon = false) : ParseSoon(PSoon) { }

    bool parseSoon() { return ParseSoon; }

  private:
    bool ParseSoon;  // Are we planning to parse these shortly after creation?
  };

  /// Contains the lexed tokens of a member function definition
  /// which needs to be parsed at the end of the class declaration
  /// after parsing all other member declarations.
  struct LexedMethod : public LateParsedDeclaration {
    Parser *Self;
    Decl *D;
    CachedTokens Toks;

    /// Whether this member function had an associated template
    /// scope. When true, D is a template declaration.
    /// otherwise, it is a member function declaration.
    bool TemplateScope;

    explicit LexedMethod(Parser* P, Decl *MD)
      : Self(P), D(MD), TemplateScope(false) {}

    void ParseLexedMethodDefs() override;
  };

  /// LateParsedDefaultArgument - Keeps track of a parameter that may
  /// have a default argument that cannot be parsed yet because it
  /// occurs within a member function declaration inside the class
  /// (C++ [class.mem]p2).
  struct LateParsedDefaultArgument {
    explicit LateParsedDefaultArgument(Decl *P,
                                       std::unique_ptr<CachedTokens> Toks = nullptr)
      : Param(P), Toks(std::move(Toks)) { }

    /// Param - The parameter declaration for this parameter.
    Decl *Param;

    /// Toks - The sequence of tokens that comprises the default
    /// argument expression, not including the '=' or the terminating
    /// ')' or ','. This will be NULL for parameters that have no
    /// default argument.
    std::unique_ptr<CachedTokens> Toks;
  };

  /// LateParsedMethodDeclaration - A method declaration inside a class that
  /// contains at least one entity whose parsing needs to be delayed
  /// until the class itself is completely-defined, such as a default
  /// argument (C++ [class.mem]p2).
  struct LateParsedMethodDeclaration : public LateParsedDeclaration {
    explicit LateParsedMethodDeclaration(Parser *P, Decl *M)
      : Self(P), Method(M), TemplateScope(false),
        ExceptionSpecTokens(nullptr) {}

    void ParseLexedMethodDeclarations() override;

    Parser* Self;

    /// Method - The method declaration.
    Decl *Method;

    /// Whether this member function had an associated template
    /// scope. When true, D is a template declaration.
    /// otherwise, it is a member function declaration.
    bool TemplateScope;

    /// DefaultArgs - Contains the parameters of the function and
    /// their default arguments. At least one of the parameters will
    /// have a default argument, but all of the parameters of the
    /// method will be stored so that they can be reintroduced into
    /// scope at the appropriate times.
    SmallVector<LateParsedDefaultArgument, 8> DefaultArgs;

    /// The set of tokens that make up an exception-specification that
    /// has not yet been parsed.
    CachedTokens *ExceptionSpecTokens;
  };

  /// LateParsedMemberInitializer - An initializer for a non-static class data
  /// member whose parsing must to be delayed until the class is completely
  /// defined (C++11 [class.mem]p2).
  struct LateParsedMemberInitializer : public LateParsedDeclaration {
    LateParsedMemberInitializer(Parser *P, Decl *FD)
      : Self(P), Field(FD) { }

    void ParseLexedMemberInitializers() override;

    Parser *Self;

    /// Field - The field declaration.
    Decl *Field;

    /// CachedTokens - The sequence of tokens that comprises the initializer,
    /// including any leading '='.
    CachedTokens Toks;
  };

  /// LateParsedDeclarationsContainer - During parsing of a top (non-nested)
  /// C++ class, its method declarations that contain parts that won't be
  /// parsed until after the definition is completed (C++ [class.mem]p2),
  /// the method declarations and possibly attached inline definitions
  /// will be stored here with the tokens that will be parsed to create those
  /// entities.
  typedef SmallVector<LateParsedDeclaration*,2> LateParsedDeclarationsContainer;

  /// Representation of a class that has been parsed, including
  /// any member function declarations or definitions that need to be
  /// parsed after the corresponding top-level class is complete.
  struct ParsingClass {
    ParsingClass(Decl *TagOrTemplate, bool TopLevelClass, bool IsInterface)
      : TopLevelClass(TopLevelClass), TemplateScope(false),
        IsInterface(IsInterface), TagOrTemplate(TagOrTemplate) { }

    /// Whether this is a "top-level" class, meaning that it is
    /// not nested within another class.
    bool TopLevelClass : 1;

    /// Whether this class had an associated template
    /// scope. When true, TagOrTemplate is a template declaration;
    /// otherwise, it is a tag declaration.
    bool TemplateScope : 1;

    /// Whether this class is an __interface.
    bool IsInterface : 1;

    /// The class or class template whose definition we are parsing.
    Decl *TagOrTemplate;

    /// LateParsedDeclarations - Method declarations, inline definitions and
    /// nested classes that contain pieces whose parsing will be delayed until
    /// the top-level class is fully defined.
    LateParsedDeclarationsContainer LateParsedDeclarations;
  };

  /// The stack of classes that is currently being
  /// parsed. Nested and local classes will be pushed onto this stack
  /// when they are parsed, and removed afterward.
  std::stack<ParsingClass *> ClassStack;

  ParsingClass &getCurrentClass() {
    assert(!ClassStack.empty() && "No lexed method stacks!");
    return *ClassStack.top();
  }

  /// RAII object used to manage the parsing of a class definition.
  class ParsingClassDefinition {
    Parser &P;
    bool Popped;
    Sema::ParsingClassState State;

  public:
    ParsingClassDefinition(Parser &P, Decl *TagOrTemplate, bool TopLevelClass,
                           bool IsInterface)
      : P(P), Popped(false),
        State(P.PushParsingClass(TagOrTemplate, TopLevelClass, IsInterface)) {
    }

    /// Pop this class of the stack.
    void Pop() {
      assert(!Popped && "Nested class has already been popped");
      Popped = true;
      P.PopParsingClass(State);
    }

    ~ParsingClassDefinition() {
      if (!Popped)
        P.PopParsingClass(State);
    }
  };

  /// Contains information about any template-specific
  /// information that has been parsed prior to parsing declaration
  /// specifiers.
  struct ParsedTemplateInfo {
    ParsedTemplateInfo()
      : Kind(NonTemplate), TemplateParams(nullptr), TemplateLoc() { }

    ParsedTemplateInfo(TemplateParameterLists *TemplateParams,
                       bool isSpecialization,
                       bool lastParameterListWasEmpty = false)
      : Kind(isSpecialization? ExplicitSpecialization : Template),
        TemplateParams(TemplateParams),
        LastParameterListWasEmpty(lastParameterListWasEmpty) { }

    explicit ParsedTemplateInfo(SourceLocation ExternLoc,
                                SourceLocation TemplateLoc)
      : Kind(ExplicitInstantiation), TemplateParams(nullptr),
        ExternLoc(ExternLoc), TemplateLoc(TemplateLoc),
        LastParameterListWasEmpty(false){ }

    /// The kind of template we are parsing.
    enum {
      /// We are not parsing a template at all.
      NonTemplate = 0,
      /// We are parsing a template declaration.
      Template,
      /// We are parsing an explicit specialization.
      ExplicitSpecialization,
      /// We are parsing an explicit instantiation.
      ExplicitInstantiation
    } Kind;

    /// The template parameter lists, for template declarations
    /// and explicit specializations.
    TemplateParameterLists *TemplateParams;

    /// The location of the 'extern' keyword, if any, for an explicit
    /// instantiation
    SourceLocation ExternLoc;

    /// The location of the 'template' keyword, for an explicit
    /// instantiation.
    SourceLocation TemplateLoc;

    /// Whether the last template parameter list was empty.
    bool LastParameterListWasEmpty;

    SourceRange getSourceRange() const LLVM_READONLY;
  };

  void LexTemplateFunctionForLateParsing(CachedTokens &Toks);
  void ParseLateTemplatedFuncDef(LateParsedTemplate &LPT);

  static void LateTemplateParserCallback(void *P, LateParsedTemplate &LPT);
  static void LateTemplateParserCleanupCallback(void *P);

  Sema::ParsingClassState
  PushParsingClass(Decl *TagOrTemplate, bool TopLevelClass, bool IsInterface);
  void DeallocateParsedClasses(ParsingClass *Class);
  void PopParsingClass(Sema::ParsingClassState);

  enum CachedInitKind {
    CIK_DefaultArgument,
    CIK_DefaultInitializer
  };

  NamedDecl *ParseCXXInlineMethodDef(AccessSpecifier AS,
                                     ParsedAttributes &AccessAttrs,
                                     ParsingDeclarator &D,
                                     const ParsedTemplateInfo &TemplateInfo,
                                     const VirtSpecifiers &VS,
                                     SourceLocation PureSpecLoc);
  void ParseCXXNonStaticMemberInitializer(Decl *VarD);
  void ParseLexedAttributes(ParsingClass &Class);
  void ParseLexedAttributeList(LateParsedAttrList &LAs, Decl *D,
                               bool EnterScope, bool OnDefinition);
  void ParseLexedAttribute(LateParsedAttribute &LA,
                           bool EnterScope, bool OnDefinition);
  void ParseLexedMethodDeclarations(ParsingClass &Class);
  void ParseLexedMethodDeclaration(LateParsedMethodDeclaration &LM);
  void ParseLexedMethodDefs(ParsingClass &Class);
  void ParseLexedMethodDef(LexedMethod &LM);
  void ParseLexedMemberInitializers(ParsingClass &Class);
  void ParseLexedMemberInitializer(LateParsedMemberInitializer &MI);
  void ParseLexedObjCMethodDefs(LexedMethod &LM, bool parseMethod);
  bool ConsumeAndStoreFunctionPrologue(CachedTokens &Toks);
  bool ConsumeAndStoreInitializer(CachedTokens &Toks, CachedInitKind CIK);
  bool ConsumeAndStoreConditional(CachedTokens &Toks);
  bool ConsumeAndStoreUntil(tok::TokenKind T1,
                            CachedTokens &Toks,
                            bool StopAtSemi = true,
                            bool ConsumeFinalToken = true) {
    return ConsumeAndStoreUntil(T1, T1, Toks, StopAtSemi, ConsumeFinalToken);
  }
  bool ConsumeAndStoreUntil(tok::TokenKind T1, tok::TokenKind T2,
                            CachedTokens &Toks,
                            bool StopAtSemi = true,
                            bool ConsumeFinalToken = true);

  //===--------------------------------------------------------------------===//
  // C99 6.9: External Definitions.
  struct ParsedAttributesWithRange : ParsedAttributes {
    ParsedAttributesWithRange(AttributeFactory &factory)
      : ParsedAttributes(factory) {}

    void clear() {
      ParsedAttributes::clear();
      Range = SourceRange();
    }

    SourceRange Range;
  };
  struct ParsedAttributesViewWithRange : ParsedAttributesView {
    ParsedAttributesViewWithRange() : ParsedAttributesView() {}
    void clearListOnly() {
      ParsedAttributesView::clearListOnly();
      Range = SourceRange();
    }

    SourceRange Range;
  };

  DeclGroupPtrTy ParseExternalDeclaration(ParsedAttributesWithRange &attrs,
                                          ParsingDeclSpec *DS = nullptr);
  bool isDeclarationAfterDeclarator();
  bool isStartOfFunctionDefinition(const ParsingDeclarator &Declarator);
  DeclGroupPtrTy ParseDeclarationOrFunctionDefinition(
                                                  ParsedAttributesWithRange &attrs,
                                                  ParsingDeclSpec *DS = nullptr,
                                                  AccessSpecifier AS = AS_none);
  DeclGroupPtrTy ParseDeclOrFunctionDefInternal(ParsedAttributesWithRange &attrs,
                                                ParsingDeclSpec &DS,
                                                AccessSpecifier AS);

  void SkipFunctionBody();
  Decl *ParseFunctionDefinition(ParsingDeclarator &D,
                 const ParsedTemplateInfo &TemplateInfo = ParsedTemplateInfo(),
                 LateParsedAttrList *LateParsedAttrs = nullptr);
  void ParseKNRParamDeclarations(Declarator &D);
  // EndLoc, if non-NULL, is filled with the location of the last token of
  // the simple-asm.
  ExprResult ParseSimpleAsm(SourceLocation *EndLoc = nullptr);
  ExprResult ParseAsmStringLiteral();

  // Objective-C External Declarations
  void MaybeSkipAttributes(tok::ObjCKeywordKind Kind);
  DeclGroupPtrTy ParseObjCAtDirectives(ParsedAttributesWithRange &Attrs);
  DeclGroupPtrTy ParseObjCAtClassDeclaration(SourceLocation atLoc);
  Decl *ParseObjCAtInterfaceDeclaration(SourceLocation AtLoc,
                                        ParsedAttributes &prefixAttrs);
  class ObjCTypeParamListScope;
  ObjCTypeParamList *parseObjCTypeParamList();
  ObjCTypeParamList *parseObjCTypeParamListOrProtocolRefs(
      ObjCTypeParamListScope &Scope, SourceLocation &lAngleLoc,
      SmallVectorImpl<IdentifierLocPair> &protocolIdents,
      SourceLocation &rAngleLoc, bool mayBeProtocolList = true);

  void HelperActionsForIvarDeclarations(Decl *interfaceDecl, SourceLocation atLoc,
                                        BalancedDelimiterTracker &T,
                                        SmallVectorImpl<Decl *> &AllIvarDecls,
                                        bool RBraceMissing);
  void ParseObjCClassInstanceVariables(Decl *interfaceDecl,
                                       tok::ObjCKeywordKind visibility,
                                       SourceLocation atLoc);
  bool ParseObjCProtocolReferences(SmallVectorImpl<Decl *> &P,
                                   SmallVectorImpl<SourceLocation> &PLocs,
                                   bool WarnOnDeclarations,
                                   bool ForObjCContainer,
                                   SourceLocation &LAngleLoc,
                                   SourceLocation &EndProtoLoc,
                                   bool consumeLastToken);

  /// Parse the first angle-bracket-delimited clause for an
  /// Objective-C object or object pointer type, which may be either
  /// type arguments or protocol qualifiers.
  void parseObjCTypeArgsOrProtocolQualifiers(
         ParsedType baseType,
         SourceLocation &typeArgsLAngleLoc,
         SmallVectorImpl<ParsedType> &typeArgs,
         SourceLocation &typeArgsRAngleLoc,
         SourceLocation &protocolLAngleLoc,
         SmallVectorImpl<Decl *> &protocols,
         SmallVectorImpl<SourceLocation> &protocolLocs,
         SourceLocation &protocolRAngleLoc,
         bool consumeLastToken,
         bool warnOnIncompleteProtocols);

  /// Parse either Objective-C type arguments or protocol qualifiers; if the
  /// former, also parse protocol qualifiers afterward.
  void parseObjCTypeArgsAndProtocolQualifiers(
         ParsedType baseType,
         SourceLocation &typeArgsLAngleLoc,
         SmallVectorImpl<ParsedType> &typeArgs,
         SourceLocation &typeArgsRAngleLoc,
         SourceLocation &protocolLAngleLoc,
         SmallVectorImpl<Decl *> &protocols,
         SmallVectorImpl<SourceLocation> &protocolLocs,
         SourceLocation &protocolRAngleLoc,
         bool consumeLastToken);

  /// Parse a protocol qualifier type such as '<NSCopying>', which is
  /// an anachronistic way of writing 'id<NSCopying>'.
  TypeResult parseObjCProtocolQualifierType(SourceLocation &rAngleLoc);

  /// Parse Objective-C type arguments and protocol qualifiers, extending the
  /// current type with the parsed result.
  TypeResult parseObjCTypeArgsAndProtocolQualifiers(SourceLocation loc,
                                                    ParsedType type,
                                                    bool consumeLastToken,
                                                    SourceLocation &endLoc);

  void ParseObjCInterfaceDeclList(tok::ObjCKeywordKind contextKey,
                                  Decl *CDecl);
  DeclGroupPtrTy ParseObjCAtProtocolDeclaration(SourceLocation atLoc,
                                                ParsedAttributes &prefixAttrs);

  struct ObjCImplParsingDataRAII {
    Parser &P;
    Decl *Dcl;
    bool HasCFunction;
    typedef SmallVector<LexedMethod*, 8> LateParsedObjCMethodContainer;
    LateParsedObjCMethodContainer LateParsedObjCMethods;

    ObjCImplParsingDataRAII(Parser &parser, Decl *D)
      : P(parser), Dcl(D), HasCFunction(false) {
      P.CurParsedObjCImpl = this;
      Finished = false;
    }
    ~ObjCImplParsingDataRAII();

    void finish(SourceRange AtEnd);
    bool isFinished() const { return Finished; }

  private:
    bool Finished;
  };
  ObjCImplParsingDataRAII *CurParsedObjCImpl;
  void StashAwayMethodOrFunctionBodyTokens(Decl *MDecl);

  DeclGroupPtrTy ParseObjCAtImplementationDeclaration(SourceLocation AtLoc);
  DeclGroupPtrTy ParseObjCAtEndDeclaration(SourceRange atEnd);
  Decl *ParseObjCAtAliasDeclaration(SourceLocation atLoc);
  Decl *ParseObjCPropertySynthesize(SourceLocation atLoc);
  Decl *ParseObjCPropertyDynamic(SourceLocation atLoc);

  IdentifierInfo *ParseObjCSelectorPiece(SourceLocation &MethodLocation);
  // Definitions for Objective-c context sensitive keywords recognition.
  enum ObjCTypeQual {
    objc_in=0, objc_out, objc_inout, objc_oneway, objc_bycopy, objc_byref,
    objc_nonnull, objc_nullable, objc_null_unspecified,
    objc_NumQuals
  };
  IdentifierInfo *ObjCTypeQuals[objc_NumQuals];

  bool isTokIdentifier_in() const;

  ParsedType ParseObjCTypeName(ObjCDeclSpec &DS, DeclaratorContext Ctx,
                               ParsedAttributes *ParamAttrs);
  void ParseObjCMethodRequirement();
  Decl *ParseObjCMethodPrototype(
            tok::ObjCKeywordKind MethodImplKind = tok::objc_not_keyword,
            bool MethodDefinition = true);
  Decl *ParseObjCMethodDecl(SourceLocation mLoc, tok::TokenKind mType,
            tok::ObjCKeywordKind MethodImplKind = tok::objc_not_keyword,
            bool MethodDefinition=true);
  void ParseObjCPropertyAttribute(ObjCDeclSpec &DS);

  Decl *ParseObjCMethodDefinition();

public:
  //===--------------------------------------------------------------------===//
  // C99 6.5: Expressions.

  /// TypeCastState - State whether an expression is or may be a type cast.
  enum TypeCastState {
    NotTypeCast = 0,
    MaybeTypeCast,
    IsTypeCast
  };

  ExprResult ParseExpression(TypeCastState isTypeCast = NotTypeCast);
  ExprResult ParseConstantExpressionInExprEvalContext(
      TypeCastState isTypeCast = NotTypeCast);
  ExprResult ParseConstantExpression(TypeCastState isTypeCast = NotTypeCast);
  ExprResult ParseCaseExpression(SourceLocation CaseLoc);
  ExprResult ParseConstraintExpression();
  // Expr that doesn't include commas.
  ExprResult ParseAssignmentExpression(TypeCastState isTypeCast = NotTypeCast);

  ExprResult ParseMSAsmIdentifier(llvm::SmallVectorImpl<Token> &LineToks,
                                  unsigned &NumLineToksConsumed,
                                  bool IsUnevaluated);

private:
  ExprResult ParseExpressionWithLeadingAt(SourceLocation AtLoc);

  ExprResult ParseExpressionWithLeadingExtension(SourceLocation ExtLoc);

  ExprResult ParseRHSOfBinaryExpression(ExprResult LHS,
                                        prec::Level MinPrec);
  ExprResult ParseCastExpression(bool isUnaryExpression,
                                 bool isAddressOfOperand,
                                 bool &NotCastExpr,
                                 TypeCastState isTypeCast,
                                 bool isVectorLiteral = false);
  ExprResult ParseCastExpression(bool isUnaryExpression,
                                 bool isAddressOfOperand = false,
                                 TypeCastState isTypeCast = NotTypeCast,
                                 bool isVectorLiteral = false);

  /// Returns true if the next token cannot start an expression.
  bool isNotExpressionStart();

  /// Returns true if the next token would start a postfix-expression
  /// suffix.
  bool isPostfixExpressionSuffixStart() {
    tok::TokenKind K = Tok.getKind();
    return (K == tok::l_square || K == tok::l_paren ||
            K == tok::period || K == tok::arrow ||
            K == tok::plusplus || K == tok::minusminus);
  }

  bool diagnoseUnknownTemplateId(ExprResult TemplateName, SourceLocation Less);
  void checkPotentialAngleBracket(ExprResult &PotentialTemplateName);
  bool checkPotentialAngleBracketDelimiter(const AngleBracketTracker::Loc &,
                                           const Token &OpToken);
  bool checkPotentialAngleBracketDelimiter(const Token &OpToken) {
    if (auto *Info = AngleBrackets.getCurrent(*this))
      return checkPotentialAngleBracketDelimiter(*Info, OpToken);
    return false;
  }

  ExprResult ParsePostfixExpressionSuffix(ExprResult LHS);
  ExprResult ParseUnaryExprOrTypeTraitExpression();
  ExprResult ParseBuiltinPrimaryExpression();

  ExprResult ParseExprAfterUnaryExprOrTypeTrait(const Token &OpTok,
                                                     bool &isCastExpr,
                                                     ParsedType &CastTy,
                                                     SourceRange &CastRange);

  typedef SmallVector<Expr*, 20> ExprListTy;
  typedef SmallVector<SourceLocation, 20> CommaLocsTy;

  /// ParseExpressionList - Used for C/C++ (argument-)expression-list.
  bool ParseExpressionList(
      SmallVectorImpl<Expr *> &Exprs,
      SmallVectorImpl<SourceLocation> &CommaLocs,
      llvm::function_ref<void()> Completer = llvm::function_ref<void()>());

  /// ParseSimpleExpressionList - A simple comma-separated list of expressions,
  /// used for misc language extensions.
  bool ParseSimpleExpressionList(SmallVectorImpl<Expr*> &Exprs,
                                 SmallVectorImpl<SourceLocation> &CommaLocs);


  /// ParenParseOption - Control what ParseParenExpression will parse.
  enum ParenParseOption {
    SimpleExpr,      // Only parse '(' expression ')'
    FoldExpr,        // Also allow fold-expression <anything>
    CompoundStmt,    // Also allow '(' compound-statement ')'
    CompoundLiteral, // Also allow '(' type-name ')' '{' ... '}'
    CastExpr         // Also allow '(' type-name ')' <anything>
  };
  ExprResult ParseParenExpression(ParenParseOption &ExprType,
                                        bool stopIfCastExpr,
                                        bool isTypeCast,
                                        ParsedType &CastTy,
                                        SourceLocation &RParenLoc);

  ExprResult ParseCXXAmbiguousParenExpression(
      ParenParseOption &ExprType, ParsedType &CastTy,
      BalancedDelimiterTracker &Tracker, ColonProtectionRAIIObject &ColonProt);
  ExprResult ParseCompoundLiteralExpression(ParsedType Ty,
                                                  SourceLocation LParenLoc,
                                                  SourceLocation RParenLoc);

  ExprResult ParseStringLiteralExpression(bool AllowUserDefinedLiteral = false);

  ExprResult ParseGenericSelectionExpression();

  ExprResult ParseObjCBoolLiteral();

  ExprResult ParseFoldExpression(ExprResult LHS, BalancedDelimiterTracker &T);

  //===--------------------------------------------------------------------===//
  // C++ Expressions
  ExprResult tryParseCXXIdExpression(CXXScopeSpec &SS, bool isAddressOfOperand,
                                     Token &Replacement);
  ExprResult ParseCXXIdExpression(bool isAddressOfOperand = false);

  bool areTokensAdjacent(const Token &A, const Token &B);

  void CheckForTemplateAndDigraph(Token &Next, ParsedType ObjectTypePtr,
                                  bool EnteringContext, IdentifierInfo &II,
                                  CXXScopeSpec &SS);

  bool ParseOptionalCXXScopeSpecifier(CXXScopeSpec &SS,
                                      ParsedType ObjectType,
                                      bool EnteringContext,
                                      bool *MayBePseudoDestructor = nullptr,
                                      bool IsTypename = false,
                                      IdentifierInfo **LastII = nullptr,
                                      bool OnlyNamespace = false);

  //===--------------------------------------------------------------------===//
  // C++0x 5.1.2: Lambda expressions

  // [...] () -> type {...}
  ExprResult ParseLambdaExpression();
  ExprResult TryParseLambdaExpression();
  Optional<unsigned> ParseLambdaIntroducer(LambdaIntroducer &Intro,
                                           bool *SkippedInits = nullptr);
  bool TryParseLambdaIntroducer(LambdaIntroducer &Intro);
  ExprResult ParseLambdaExpressionAfterIntroducer(
               LambdaIntroducer &Intro);

  //===--------------------------------------------------------------------===//
  // C++ 5.2p1: C++ Casts
  ExprResult ParseCXXCasts();

  //===--------------------------------------------------------------------===//
  // C++ 5.2p1: C++ Type Identification
  ExprResult ParseCXXTypeid();

  //===--------------------------------------------------------------------===//
  //  C++ : Microsoft __uuidof Expression
  ExprResult ParseCXXUuidof();

  //===--------------------------------------------------------------------===//
  // C++ 5.2.4: C++ Pseudo-Destructor Expressions
  ExprResult ParseCXXPseudoDestructor(Expr *Base, SourceLocation OpLoc,
                                            tok::TokenKind OpKind,
                                            CXXScopeSpec &SS,
                                            ParsedType ObjectType);

  //===--------------------------------------------------------------------===//
  // C++ 9.3.2: C++ 'this' pointer
  ExprResult ParseCXXThis();

  //===--------------------------------------------------------------------===//
  // C++ 15: C++ Throw Expression
  ExprResult ParseThrowExpression();

  ExceptionSpecificationType tryParseExceptionSpecification(
                    bool Delayed,
                    SourceRange &SpecificationRange,
                    SmallVectorImpl<ParsedType> &DynamicExceptions,
                    SmallVectorImpl<SourceRange> &DynamicExceptionRanges,
                    ExprResult &NoexceptExpr,
                    CachedTokens *&ExceptionSpecTokens);

  // EndLoc is filled with the location of the last token of the specification.
  ExceptionSpecificationType ParseDynamicExceptionSpecification(
                                  SourceRange &SpecificationRange,
                                  SmallVectorImpl<ParsedType> &Exceptions,
                                  SmallVectorImpl<SourceRange> &Ranges);

  //===--------------------------------------------------------------------===//
  // C++0x 8: Function declaration trailing-return-type
  TypeResult ParseTrailingReturnType(SourceRange &Range,
                                     bool MayBeFollowedByDirectInit);

  //===--------------------------------------------------------------------===//
  // C++ 2.13.5: C++ Boolean Literals
  ExprResult ParseCXXBoolLiteral();

  //===--------------------------------------------------------------------===//
  // C++ 5.2.3: Explicit type conversion (functional notation)
  ExprResult ParseCXXTypeConstructExpression(const DeclSpec &DS);

  /// ParseCXXSimpleTypeSpecifier - [C++ 7.1.5.2] Simple type specifiers.
  /// This should only be called when the current token is known to be part of
  /// simple-type-specifier.
  void ParseCXXSimpleTypeSpecifier(DeclSpec &DS);

  bool ParseCXXTypeSpecifierSeq(DeclSpec &DS);

  //===--------------------------------------------------------------------===//
  // C++ 5.3.4 and 5.3.5: C++ new and delete
  bool ParseExpressionListOrTypeId(SmallVectorImpl<Expr*> &Exprs,
                                   Declarator &D);
  void ParseDirectNewDeclarator(Declarator &D);
  ExprResult ParseCXXNewExpression(bool UseGlobal, SourceLocation Start);
  ExprResult ParseCXXDeleteExpression(bool UseGlobal,
                                            SourceLocation Start);

  //===--------------------------------------------------------------------===//
  // C++ if/switch/while/for condition expression.
  struct ForRangeInfo;
  Sema::ConditionResult ParseCXXCondition(StmtResult *InitStmt,
                                          SourceLocation Loc,
                                          Sema::ConditionKind CK,
                                          ForRangeInfo *FRI = nullptr);

  //===--------------------------------------------------------------------===//
  // C++ Coroutines

  ExprResult ParseCoyieldExpression();

  //===--------------------------------------------------------------------===//
  // C99 6.7.8: Initialization.

  /// ParseInitializer
  ///       initializer: [C99 6.7.8]
  ///         assignment-expression
  ///         '{' ...
  ExprResult ParseInitializer() {
    if (Tok.isNot(tok::l_brace))
      return ParseAssignmentExpression();
    return ParseBraceInitializer();
  }
  bool MayBeDesignationStart();
  ExprResult ParseBraceInitializer();
  ExprResult ParseInitializerWithPotentialDesignator();

  //===--------------------------------------------------------------------===//
  // clang Expressions

  ExprResult ParseBlockLiteralExpression();  // ^{...}

  //===--------------------------------------------------------------------===//
  // Objective-C Expressions
  ExprResult ParseObjCAtExpression(SourceLocation AtLocation);
  ExprResult ParseObjCStringLiteral(SourceLocation AtLoc);
  ExprResult ParseObjCCharacterLiteral(SourceLocation AtLoc);
  ExprResult ParseObjCNumericLiteral(SourceLocation AtLoc);
  ExprResult ParseObjCBooleanLiteral(SourceLocation AtLoc, bool ArgValue);
  ExprResult ParseObjCArrayLiteral(SourceLocation AtLoc);
  ExprResult ParseObjCDictionaryLiteral(SourceLocation AtLoc);
  ExprResult ParseObjCBoxedExpr(SourceLocation AtLoc);
  ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc);
  ExprResult ParseObjCSelectorExpression(SourceLocation AtLoc);
  ExprResult ParseObjCProtocolExpression(SourceLocation AtLoc);
  bool isSimpleObjCMessageExpression();
  ExprResult ParseObjCMessageExpression();
  ExprResult ParseObjCMessageExpressionBody(SourceLocation LBracloc,
                                            SourceLocation SuperLoc,
                                            ParsedType ReceiverType,
                                            Expr *ReceiverExpr);
  ExprResult ParseAssignmentExprWithObjCMessageExprStart(
      SourceLocation LBracloc, SourceLocation SuperLoc,
      ParsedType ReceiverType, Expr *ReceiverExpr);
  bool ParseObjCXXMessageReceiver(bool &IsExpr, void *&TypeOrExpr);

  //===--------------------------------------------------------------------===//
  // C99 6.8: Statements and Blocks.

  /// A SmallVector of statements, with stack size 32 (as that is the only one
  /// used.)
  typedef SmallVector<Stmt*, 32> StmtVector;
  /// A SmallVector of expressions, with stack size 12 (the maximum used.)
  typedef SmallVector<Expr*, 12> ExprVector;
  /// A SmallVector of types.
  typedef SmallVector<ParsedType, 12> TypeVector;

  StmtResult ParseStatement(SourceLocation *TrailingElseLoc = nullptr,
                            bool AllowOpenMPStandalone = false);
  enum AllowedConstructsKind {
    /// Allow any declarations, statements, OpenMP directives.
    ACK_Any,
    /// Allow only statements and non-standalone OpenMP directives.
    ACK_StatementsOpenMPNonStandalone,
    /// Allow statements and all executable OpenMP directives
    ACK_StatementsOpenMPAnyExecutable
  };
  StmtResult
  ParseStatementOrDeclaration(StmtVector &Stmts, AllowedConstructsKind Allowed,
                              SourceLocation *TrailingElseLoc = nullptr);
  StmtResult ParseStatementOrDeclarationAfterAttributes(
                                         StmtVector &Stmts,
                                         AllowedConstructsKind Allowed,
                                         SourceLocation *TrailingElseLoc,
                                         ParsedAttributesWithRange &Attrs);
  StmtResult ParseExprStatement();
  StmtResult ParseLabeledStatement(ParsedAttributesWithRange &attrs);
  StmtResult ParseCaseStatement(bool MissingCase = false,
                                ExprResult Expr = ExprResult());
  StmtResult ParseDefaultStatement();
  StmtResult ParseCompoundStatement(bool isStmtExpr = false);
  StmtResult ParseCompoundStatement(bool isStmtExpr,
                                    unsigned ScopeFlags);
  void ParseCompoundStatementLeadingPragmas();
  bool ConsumeNullStmt(StmtVector &Stmts);
  StmtResult ParseCompoundStatementBody(bool isStmtExpr = false);
  bool ParseParenExprOrCondition(StmtResult *InitStmt,
                                 Sema::ConditionResult &CondResult,
                                 SourceLocation Loc,
                                 Sema::ConditionKind CK);
  StmtResult ParseIfStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseSwitchStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseWhileStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseDoStatement();
  StmtResult ParseForStatement(SourceLocation *TrailingElseLoc);
  StmtResult ParseGotoStatement();
  StmtResult ParseContinueStatement();
  StmtResult ParseBreakStatement();
  StmtResult ParseReturnStatement();
  StmtResult ParseAsmStatement(bool &msAsm);
  StmtResult ParseMicrosoftAsmStatement(SourceLocation AsmLoc);
  StmtResult ParsePragmaLoopHint(StmtVector &Stmts,
                                 AllowedConstructsKind Allowed,
                                 SourceLocation *TrailingElseLoc,
                                 ParsedAttributesWithRange &Attrs);

  /// Describes the behavior that should be taken for an __if_exists
  /// block.
  enum IfExistsBehavior {
    /// Parse the block; this code is always used.
    IEB_Parse,
    /// Skip the block entirely; this code is never used.
    IEB_Skip,
    /// Parse the block as a dependent block, which may be used in
    /// some template instantiations but not others.
    IEB_Dependent
  };

  /// Describes the condition of a Microsoft __if_exists or
  /// __if_not_exists block.
  struct IfExistsCondition {
    /// The location of the initial keyword.
    SourceLocation KeywordLoc;
    /// Whether this is an __if_exists block (rather than an
    /// __if_not_exists block).
    bool IsIfExists;

    /// Nested-name-specifier preceding the name.
    CXXScopeSpec SS;

    /// The name we're looking for.
    UnqualifiedId Name;

    /// The behavior of this __if_exists or __if_not_exists block
    /// should.
    IfExistsBehavior Behavior;
  };

  bool ParseMicrosoftIfExistsCondition(IfExistsCondition& Result);
  void ParseMicrosoftIfExistsStatement(StmtVector &Stmts);
  void ParseMicrosoftIfExistsExternalDeclaration();
  void ParseMicrosoftIfExistsClassDeclaration(DeclSpec::TST TagType,
                                              ParsedAttributes &AccessAttrs,
                                              AccessSpecifier &CurAS);
  bool ParseMicrosoftIfExistsBraceInitializer(ExprVector &InitExprs,
                                              bool &InitExprsOk);
  bool ParseAsmOperandsOpt(SmallVectorImpl<IdentifierInfo *> &Names,
                           SmallVectorImpl<Expr *> &Constraints,
                           SmallVectorImpl<Expr *> &Exprs);

  //===--------------------------------------------------------------------===//
  // C++ 6: Statements and Blocks

  StmtResult ParseCXXTryBlock();
  StmtResult ParseCXXTryBlockCommon(SourceLocation TryLoc, bool FnTry = false);
  StmtResult ParseCXXCatchBlock(bool FnCatch = false);

  //===--------------------------------------------------------------------===//
  // MS: SEH Statements and Blocks

  StmtResult ParseSEHTryBlock();
  StmtResult ParseSEHExceptBlock(SourceLocation Loc);
  StmtResult ParseSEHFinallyBlock(SourceLocation Loc);
  StmtResult ParseSEHLeaveStatement();

  //===--------------------------------------------------------------------===//
  // Objective-C Statements

  StmtResult ParseObjCAtStatement(SourceLocation atLoc);
  StmtResult ParseObjCTryStmt(SourceLocation atLoc);
  StmtResult ParseObjCThrowStmt(SourceLocation atLoc);
  StmtResult ParseObjCSynchronizedStmt(SourceLocation atLoc);
  StmtResult ParseObjCAutoreleasePoolStmt(SourceLocation atLoc);


  //===--------------------------------------------------------------------===//
  // C99 6.7: Declarations.

  /// A context for parsing declaration specifiers.  TODO: flesh this
  /// out, there are other significant restrictions on specifiers than
  /// would be best implemented in the parser.
  enum class DeclSpecContext {
    DSC_normal, // normal context
    DSC_class,  // class context, enables 'friend'
    DSC_type_specifier, // C++ type-specifier-seq or C specifier-qualifier-list
    DSC_trailing, // C++11 trailing-type-specifier in a trailing return type
    DSC_alias_declaration, // C++11 type-specifier-seq in an alias-declaration
    DSC_top_level, // top-level/namespace declaration context
    DSC_template_param, // template parameter context
    DSC_template_type_arg, // template type argument context
    DSC_objc_method_result, // ObjC method result context, enables 'instancetype'
    DSC_condition // condition declaration context
  };

  /// Is this a context in which we are parsing just a type-specifier (or
  /// trailing-type-specifier)?
  static bool isTypeSpecifier(DeclSpecContext DSC) {
    switch (DSC) {
    case DeclSpecContext::DSC_normal:
    case DeclSpecContext::DSC_template_param:
    case DeclSpecContext::DSC_class:
    case DeclSpecContext::DSC_top_level:
    case DeclSpecContext::DSC_objc_method_result:
    case DeclSpecContext::DSC_condition:
      return false;

    case DeclSpecContext::DSC_template_type_arg:
    case DeclSpecContext::DSC_type_specifier:
    case DeclSpecContext::DSC_trailing:
    case DeclSpecContext::DSC_alias_declaration:
      return true;
    }
    llvm_unreachable("Missing DeclSpecContext case");
  }

  /// Is this a context in which we can perform class template argument
  /// deduction?
  static bool isClassTemplateDeductionContext(DeclSpecContext DSC) {
    switch (DSC) {
    case DeclSpecContext::DSC_normal:
    case DeclSpecContext::DSC_template_param:
    case DeclSpecContext::DSC_class:
    case DeclSpecContext::DSC_top_level:
    case DeclSpecContext::DSC_condition:
    case DeclSpecContext::DSC_type_specifier:
      return true;

    case DeclSpecContext::DSC_objc_method_result:
    case DeclSpecContext::DSC_template_type_arg:
    case DeclSpecContext::DSC_trailing:
    case DeclSpecContext::DSC_alias_declaration:
      return false;
    }
    llvm_unreachable("Missing DeclSpecContext case");
  }

  /// Information on a C++0x for-range-initializer found while parsing a
  /// declaration which turns out to be a for-range-declaration.
  struct ForRangeInit {
    SourceLocation ColonLoc;
    ExprResult RangeExpr;

    bool ParsedForRangeDecl() { return !ColonLoc.isInvalid(); }
  };
  struct ForRangeInfo : ForRangeInit {
    StmtResult LoopVar;
  };

  DeclGroupPtrTy ParseDeclaration(DeclaratorContext Context,
                                  SourceLocation &DeclEnd,
                                  ParsedAttributesWithRange &attrs);
  DeclGroupPtrTy ParseSimpleDeclaration(DeclaratorContext Context,
                                        SourceLocation &DeclEnd,
                                        ParsedAttributesWithRange &attrs,
                                        bool RequireSemi,
                                        ForRangeInit *FRI = nullptr);
  bool MightBeDeclarator(DeclaratorContext Context);
  DeclGroupPtrTy ParseDeclGroup(ParsingDeclSpec &DS, DeclaratorContext Context,
                                SourceLocation *DeclEnd = nullptr,
                                ForRangeInit *FRI = nullptr);
  Decl *ParseDeclarationAfterDeclarator(Declarator &D,
               const ParsedTemplateInfo &TemplateInfo = ParsedTemplateInfo());
  bool ParseAsmAttributesAfterDeclarator(Declarator &D);
  Decl *ParseDeclarationAfterDeclaratorAndAttributes(
      Declarator &D,
      const ParsedTemplateInfo &TemplateInfo = ParsedTemplateInfo(),
      ForRangeInit *FRI = nullptr);
  Decl *ParseFunctionStatementBody(Decl *Decl, ParseScope &BodyScope);
  Decl *ParseFunctionTryBlock(Decl *Decl, ParseScope &BodyScope);

  /// When in code-completion, skip parsing of the function/method body
  /// unless the body contains the code-completion point.
  ///
  /// \returns true if the function body was skipped.
  bool trySkippingFunctionBody();

  bool ParseImplicitInt(DeclSpec &DS, CXXScopeSpec *SS,
                        const ParsedTemplateInfo &TemplateInfo,
                        AccessSpecifier AS, DeclSpecContext DSC,
                        ParsedAttributesWithRange &Attrs);
  DeclSpecContext
  getDeclSpecContextFromDeclaratorContext(DeclaratorContext Context);
  void ParseDeclarationSpecifiers(
      DeclSpec &DS,
      const ParsedTemplateInfo &TemplateInfo = ParsedTemplateInfo(),
      AccessSpecifier AS = AS_none,
      DeclSpecContext DSC = DeclSpecContext::DSC_normal,
      LateParsedAttrList *LateAttrs = nullptr);
  bool DiagnoseMissingSemiAfterTagDefinition(
      DeclSpec &DS, AccessSpecifier AS, DeclSpecContext DSContext,
      LateParsedAttrList *LateAttrs = nullptr);

  void ParseSpecifierQualifierList(
      DeclSpec &DS, AccessSpecifier AS = AS_none,
      DeclSpecContext DSC = DeclSpecContext::DSC_normal);

  void ParseObjCTypeQualifierList(ObjCDeclSpec &DS,
                                  DeclaratorContext Context);

  void ParseEnumSpecifier(SourceLocation TagLoc, DeclSpec &DS,
                          const ParsedTemplateInfo &TemplateInfo,
                          AccessSpecifier AS, DeclSpecContext DSC);
  void ParseEnumBody(SourceLocation StartLoc, Decl *TagDecl);
  void ParseStructUnionBody(SourceLocation StartLoc, unsigned TagType,
                            Decl *TagDecl);

  void ParseStructDeclaration(
      ParsingDeclSpec &DS,
      llvm::function_ref<void(ParsingFieldDeclarator &)> FieldsCallback);

  bool isDeclarationSpecifier(bool DisambiguatingWithExpression = false);
  bool isTypeSpecifierQualifier();

  /// isKnownToBeTypeSpecifier - Return true if we know that the specified token
  /// is definitely a type-specifier.  Return false if it isn't part of a type
  /// specifier or if we're not sure.
  bool isKnownToBeTypeSpecifier(const Token &Tok) const;

  /// Return true if we know that we are definitely looking at a
  /// decl-specifier, and isn't part of an expression such as a function-style
  /// cast. Return false if it's no a decl-specifier, or we're not sure.
  bool isKnownToBeDeclarationSpecifier() {
    if (getLangOpts().CPlusPlus)
      return isCXXDeclarationSpecifier() == TPResult::True;
    return isDeclarationSpecifier(true);
  }

  /// isDeclarationStatement - Disambiguates between a declaration or an
  /// expression statement, when parsing function bodies.
  /// Returns true for declaration, false for expression.
  bool isDeclarationStatement() {
    if (getLangOpts().CPlusPlus)
      return isCXXDeclarationStatement();
    return isDeclarationSpecifier(true);
  }

  /// isForInitDeclaration - Disambiguates between a declaration or an
  /// expression in the context of the C 'clause-1' or the C++
  // 'for-init-statement' part of a 'for' statement.
  /// Returns true for declaration, false for expression.
  bool isForInitDeclaration() {
    if (getLangOpts().OpenMP)
      Actions.startOpenMPLoop();
    if (getLangOpts().CPlusPlus)
      return isCXXSimpleDeclaration(/*AllowForRangeDecl=*/true);
    return isDeclarationSpecifier(true);
  }

  /// Determine whether this is a C++1z for-range-identifier.
  bool isForRangeIdentifier();

  /// Determine whether we are currently at the start of an Objective-C
  /// class message that appears to be missing the open bracket '['.
  bool isStartOfObjCClassMessageMissingOpenBracket();

  /// Starting with a scope specifier, identifier, or
  /// template-id that refers to the current class, determine whether
  /// this is a constructor declarator.
  bool isConstructorDeclarator(bool Unqualified, bool DeductionGuide = false);

  /// Specifies the context in which type-id/expression
  /// disambiguation will occur.
  enum TentativeCXXTypeIdContext {
    TypeIdInParens,
    TypeIdUnambiguous,
    TypeIdAsTemplateArgument
  };


  /// isTypeIdInParens - Assumes that a '(' was parsed and now we want to know
  /// whether the parens contain an expression or a type-id.
  /// Returns true for a type-id and false for an expression.
  bool isTypeIdInParens(bool &isAmbiguous) {
    if (getLangOpts().CPlusPlus)
      return isCXXTypeId(TypeIdInParens, isAmbiguous);
    isAmbiguous = false;
    return isTypeSpecifierQualifier();
  }
  bool isTypeIdInParens() {
    bool isAmbiguous;
    return isTypeIdInParens(isAmbiguous);
  }

  /// Checks if the current tokens form type-id or expression.
  /// It is similar to isTypeIdInParens but does not suppose that type-id
  /// is in parenthesis.
  bool isTypeIdUnambiguously() {
    bool IsAmbiguous;
    if (getLangOpts().CPlusPlus)
      return isCXXTypeId(TypeIdUnambiguous, IsAmbiguous);
    return isTypeSpecifierQualifier();
  }

  /// isCXXDeclarationStatement - C++-specialized function that disambiguates
  /// between a declaration or an expression statement, when parsing function
  /// bodies. Returns true for declaration, false for expression.
  bool isCXXDeclarationStatement();

  /// isCXXSimpleDeclaration - C++-specialized function that disambiguates
  /// between a simple-declaration or an expression-statement.
  /// If during the disambiguation process a parsing error is encountered,
  /// the function returns true to let the declaration parsing code handle it.
  /// Returns false if the statement is disambiguated as expression.
  bool isCXXSimpleDeclaration(bool AllowForRangeDecl);

  /// isCXXFunctionDeclarator - Disambiguates between a function declarator or
  /// a constructor-style initializer, when parsing declaration statements.
  /// Returns true for function declarator and false for constructor-style
  /// initializer. Sets 'IsAmbiguous' to true to indicate that this declaration
  /// might be a constructor-style initializer.
  /// If during the disambiguation process a parsing error is encountered,
  /// the function returns true to let the declaration parsing code handle it.
  bool isCXXFunctionDeclarator(bool *IsAmbiguous = nullptr);

  struct ConditionDeclarationOrInitStatementState;
  enum class ConditionOrInitStatement {
    Expression,    ///< Disambiguated as an expression (either kind).
    ConditionDecl, ///< Disambiguated as the declaration form of condition.
    InitStmtDecl,  ///< Disambiguated as a simple-declaration init-statement.
    ForRangeDecl,  ///< Disambiguated as a for-range declaration.
    Error          ///< Can't be any of the above!
  };
  /// Disambiguates between the different kinds of things that can happen
  /// after 'if (' or 'switch ('. This could be one of two different kinds of
  /// declaration (depending on whether there is a ';' later) or an expression.
  ConditionOrInitStatement
  isCXXConditionDeclarationOrInitStatement(bool CanBeInitStmt,
                                           bool CanBeForRangeDecl);

  bool isCXXTypeId(TentativeCXXTypeIdContext Context, bool &isAmbiguous);
  bool isCXXTypeId(TentativeCXXTypeIdContext Context) {
    bool isAmbiguous;
    return isCXXTypeId(Context, isAmbiguous);
  }

  /// TPResult - Used as the result value for functions whose purpose is to
  /// disambiguate C++ constructs by "tentatively parsing" them.
  enum class TPResult {
    True, False, Ambiguous, Error
  };

  /// Based only on the given token kind, determine whether we know that
  /// we're at the start of an expression or a type-specifier-seq (which may
  /// be an expression, in C++).
  ///
  /// This routine does not attempt to resolve any of the trick cases, e.g.,
  /// those involving lookup of identifiers.
  ///
  /// \returns \c TPR_true if this token starts an expression, \c TPR_false if
  /// this token starts a type-specifier-seq, or \c TPR_ambiguous if it cannot
  /// tell.
  TPResult isExpressionOrTypeSpecifierSimple(tok::TokenKind Kind);

  /// isCXXDeclarationSpecifier - Returns TPResult::True if it is a
  /// declaration specifier, TPResult::False if it is not,
  /// TPResult::Ambiguous if it could be either a decl-specifier or a
  /// function-style cast, and TPResult::Error if a parsing error was
  /// encountered. If it could be a braced C++11 function-style cast, returns
  /// BracedCastResult.
  /// Doesn't consume tokens.
  TPResult
  isCXXDeclarationSpecifier(TPResult BracedCastResult = TPResult::False,
                            bool *HasMissingTypename = nullptr);

  /// Given that isCXXDeclarationSpecifier returns \c TPResult::True or
  /// \c TPResult::Ambiguous, determine whether the decl-specifier would be
  /// a type-specifier other than a cv-qualifier.
  bool isCXXDeclarationSpecifierAType();

  /// Determine whether an identifier has been tentatively declared as a
  /// non-type. Such tentative declarations should not be found to name a type
  /// during a tentative parse, but also should not be annotated as a non-type.
  bool isTentativelyDeclared(IdentifierInfo *II);

  // "Tentative parsing" functions, used for disambiguation. If a parsing error
  // is encountered they will return TPResult::Error.
  // Returning TPResult::True/False indicates that the ambiguity was
  // resolved and tentative parsing may stop. TPResult::Ambiguous indicates
  // that more tentative parsing is necessary for disambiguation.
  // They all consume tokens, so backtracking should be used after calling them.

  TPResult TryParseSimpleDeclaration(bool AllowForRangeDecl);
  TPResult TryParseTypeofSpecifier();
  TPResult TryParseProtocolQualifiers();
  TPResult TryParsePtrOperatorSeq();
  TPResult TryParseOperatorId();
  TPResult TryParseInitDeclaratorList();
  TPResult TryParseDeclarator(bool mayBeAbstract, bool mayHaveIdentifier = true,
                              bool mayHaveDirectInit = false);
  TPResult
  TryParseParameterDeclarationClause(bool *InvalidAsDeclaration = nullptr,
                                     bool VersusTemplateArg = false);
  TPResult TryParseFunctionDeclarator();
  TPResult TryParseBracketDeclarator();
  TPResult TryConsumeDeclarationSpecifier();

public:
  TypeResult ParseTypeName(SourceRange *Range = nullptr,
                           DeclaratorContext Context
                             = DeclaratorContext::TypeNameContext,
                           AccessSpecifier AS = AS_none,
                           Decl **OwnedType = nullptr,
                           ParsedAttributes *Attrs = nullptr);

private:
  void ParseBlockId(SourceLocation CaretLoc);

  /// Are [[]] attributes enabled?
  bool standardAttributesAllowed() const {
    const LangOptions &LO = getLangOpts();
    return LO.DoubleSquareBracketAttributes;
  }

  // Check for the start of an attribute-specifier-seq in a context where an
  // attribute is not allowed.
  bool CheckProhibitedCXX11Attribute() {
    assert(Tok.is(tok::l_square));
    if (!standardAttributesAllowed() || NextToken().isNot(tok::l_square))
      return false;
    return DiagnoseProhibitedCXX11Attribute();
  }

  bool DiagnoseProhibitedCXX11Attribute();
  void CheckMisplacedCXX11Attribute(ParsedAttributesWithRange &Attrs,
                                    SourceLocation CorrectLocation) {
    if (!standardAttributesAllowed())
      return;
    if ((Tok.isNot(tok::l_square) || NextToken().isNot(tok::l_square)) &&
        Tok.isNot(tok::kw_alignas))
      return;
    DiagnoseMisplacedCXX11Attribute(Attrs, CorrectLocation);
  }
  void DiagnoseMisplacedCXX11Attribute(ParsedAttributesWithRange &Attrs,
                                       SourceLocation CorrectLocation);

  void stripTypeAttributesOffDeclSpec(ParsedAttributesWithRange &Attrs,
                                      DeclSpec &DS, Sema::TagUseKind TUK);

  // FixItLoc = possible correct location for the attributes
  void ProhibitAttributes(ParsedAttributesWithRange &Attrs,
                          SourceLocation FixItLoc = SourceLocation()) {
    if (Attrs.Range.isInvalid())
      return;
    DiagnoseProhibitedAttributes(Attrs.Range, FixItLoc);
    Attrs.clear();
  }

  void ProhibitAttributes(ParsedAttributesViewWithRange &Attrs,
                          SourceLocation FixItLoc = SourceLocation()) {
    if (Attrs.Range.isInvalid())
      return;
    DiagnoseProhibitedAttributes(Attrs.Range, FixItLoc);
    Attrs.clearListOnly();
  }
  void DiagnoseProhibitedAttributes(const SourceRange &Range,
                                    SourceLocation FixItLoc);

  // Forbid C++11 and C2x attributes that appear on certain syntactic locations
  // which standard permits but we don't supported yet, for example, attributes
  // appertain to decl specifiers.
  void ProhibitCXX11Attributes(ParsedAttributesWithRange &Attrs,
                               unsigned DiagID);

  /// Skip C++11 and C2x attributes and return the end location of the
  /// last one.
  /// \returns SourceLocation() if there are no attributes.
  SourceLocation SkipCXX11Attributes();

  /// Diagnose and skip C++11 and C2x attributes that appear in syntactic
  /// locations where attributes are not allowed.
  void DiagnoseAndSkipCXX11Attributes();

  /// Parses syntax-generic attribute arguments for attributes which are
  /// known to the implementation, and adds them to the given ParsedAttributes
  /// list with the given attribute syntax. Returns the number of arguments
  /// parsed for the attribute.
  unsigned
  ParseAttributeArgsCommon(IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
                           ParsedAttributes &Attrs, SourceLocation *EndLoc,
                           IdentifierInfo *ScopeName, SourceLocation ScopeLoc,
                           ParsedAttr::Syntax Syntax);

  void MaybeParseGNUAttributes(Declarator &D,
                               LateParsedAttrList *LateAttrs = nullptr) {
    if (Tok.is(tok::kw___attribute)) {
      ParsedAttributes attrs(AttrFactory);
      SourceLocation endLoc;
      ParseGNUAttributes(attrs, &endLoc, LateAttrs, &D);
      D.takeAttributes(attrs, endLoc);
    }
  }
  void MaybeParseGNUAttributes(ParsedAttributes &attrs,
                               SourceLocation *endLoc = nullptr,
                               LateParsedAttrList *LateAttrs = nullptr) {
    if (Tok.is(tok::kw___attribute))
      ParseGNUAttributes(attrs, endLoc, LateAttrs);
  }
  void ParseGNUAttributes(ParsedAttributes &attrs,
                          SourceLocation *endLoc = nullptr,
                          LateParsedAttrList *LateAttrs = nullptr,
                          Declarator *D = nullptr);
  void ParseGNUAttributeArgs(IdentifierInfo *AttrName,
                             SourceLocation AttrNameLoc,
                             ParsedAttributes &Attrs, SourceLocation *EndLoc,
                             IdentifierInfo *ScopeName, SourceLocation ScopeLoc,
                             ParsedAttr::Syntax Syntax, Declarator *D);
  IdentifierLoc *ParseIdentifierLoc();

  unsigned
  ParseClangAttributeArgs(IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
                          ParsedAttributes &Attrs, SourceLocation *EndLoc,
                          IdentifierInfo *ScopeName, SourceLocation ScopeLoc,
                          ParsedAttr::Syntax Syntax);

  void MaybeParseCXX11Attributes(Declarator &D) {
    if (standardAttributesAllowed() && isCXX11AttributeSpecifier()) {
      ParsedAttributesWithRange attrs(AttrFactory);
      SourceLocation endLoc;
      ParseCXX11Attributes(attrs, &endLoc);
      D.takeAttributes(attrs, endLoc);
    }
  }
  void MaybeParseCXX11Attributes(ParsedAttributes &attrs,
                                 SourceLocation *endLoc = nullptr) {
    if (standardAttributesAllowed() && isCXX11AttributeSpecifier()) {
      ParsedAttributesWithRange attrsWithRange(AttrFactory);
      ParseCXX11Attributes(attrsWithRange, endLoc);
      attrs.takeAllFrom(attrsWithRange);
    }
  }
  void MaybeParseCXX11Attributes(ParsedAttributesWithRange &attrs,
                                 SourceLocation *endLoc = nullptr,
                                 bool OuterMightBeMessageSend = false) {
    if (standardAttributesAllowed() &&
      isCXX11AttributeSpecifier(false, OuterMightBeMessageSend))
      ParseCXX11Attributes(attrs, endLoc);
  }

  void ParseCXX11AttributeSpecifier(ParsedAttributes &attrs,
                                    SourceLocation *EndLoc = nullptr);
  void ParseCXX11Attributes(ParsedAttributesWithRange &attrs,
                            SourceLocation *EndLoc = nullptr);
  /// Parses a C++11 (or C2x)-style attribute argument list. Returns true
  /// if this results in adding an attribute to the ParsedAttributes list.
  bool ParseCXX11AttributeArgs(IdentifierInfo *AttrName,
                               SourceLocation AttrNameLoc,
                               ParsedAttributes &Attrs, SourceLocation *EndLoc,
                               IdentifierInfo *ScopeName,
                               SourceLocation ScopeLoc);

  IdentifierInfo *TryParseCXX11AttributeIdentifier(SourceLocation &Loc);

  void MaybeParseMicrosoftAttributes(ParsedAttributes &attrs,
                                     SourceLocation *endLoc = nullptr) {
    if (getLangOpts().MicrosoftExt && Tok.is(tok::l_square))
      ParseMicrosoftAttributes(attrs, endLoc);
  }
  void ParseMicrosoftUuidAttributeArgs(ParsedAttributes &Attrs);
  void ParseMicrosoftAttributes(ParsedAttributes &attrs,
                                SourceLocation *endLoc = nullptr);
  void MaybeParseMicrosoftDeclSpecs(ParsedAttributes &Attrs,
                                    SourceLocation *End = nullptr) {
    const auto &LO = getLangOpts();
    if (LO.DeclSpecKeyword && Tok.is(tok::kw___declspec))
      ParseMicrosoftDeclSpecs(Attrs, End);
  }
  void ParseMicrosoftDeclSpecs(ParsedAttributes &Attrs,
                               SourceLocation *End = nullptr);
  bool ParseMicrosoftDeclSpecArgs(IdentifierInfo *AttrName,
                                  SourceLocation AttrNameLoc,
                                  ParsedAttributes &Attrs);
  void ParseMicrosoftTypeAttributes(ParsedAttributes &attrs);
  void DiagnoseAndSkipExtendedMicrosoftTypeAttributes();
  SourceLocation SkipExtendedMicrosoftTypeAttributes();
  void ParseMicrosoftInheritanceClassAttributes(ParsedAttributes &attrs);
  void ParseBorlandTypeAttributes(ParsedAttributes &attrs);
  void ParseOpenCLKernelAttributes(ParsedAttributes &attrs);
  void ParseOpenCLQualifiers(ParsedAttributes &Attrs);
  /// Parses opencl_unroll_hint attribute if language is OpenCL v2.0
  /// or higher.
  /// \return false if error happens.
  bool MaybeParseOpenCLUnrollHintAttribute(ParsedAttributes &Attrs) {
    if (getLangOpts().OpenCL)
      return ParseOpenCLUnrollHintAttribute(Attrs);
    return true;
  }
  /// Parses opencl_unroll_hint attribute.
  /// \return false if error happens.
  bool ParseOpenCLUnrollHintAttribute(ParsedAttributes &Attrs);
  void ParseNullabilityTypeSpecifiers(ParsedAttributes &attrs);

  VersionTuple ParseVersionTuple(SourceRange &Range);
  void ParseAvailabilityAttribute(IdentifierInfo &Availability,
                                  SourceLocation AvailabilityLoc,
                                  ParsedAttributes &attrs,
                                  SourceLocation *endLoc,
                                  IdentifierInfo *ScopeName,
                                  SourceLocation ScopeLoc,
                                  ParsedAttr::Syntax Syntax);

  Optional<AvailabilitySpec> ParseAvailabilitySpec();
  ExprResult ParseAvailabilityCheckExpr(SourceLocation StartLoc);

  void ParseExternalSourceSymbolAttribute(IdentifierInfo &ExternalSourceSymbol,
                                          SourceLocation Loc,
                                          ParsedAttributes &Attrs,
                                          SourceLocation *EndLoc,
                                          IdentifierInfo *ScopeName,
                                          SourceLocation ScopeLoc,
                                          ParsedAttr::Syntax Syntax);

  void ParseObjCBridgeRelatedAttribute(IdentifierInfo &ObjCBridgeRelated,
                                       SourceLocation ObjCBridgeRelatedLoc,
                                       ParsedAttributes &attrs,
                                       SourceLocation *endLoc,
                                       IdentifierInfo *ScopeName,
                                       SourceLocation ScopeLoc,
                                       ParsedAttr::Syntax Syntax);

  void ParseTypeTagForDatatypeAttribute(IdentifierInfo &AttrName,
                                        SourceLocation AttrNameLoc,
                                        ParsedAttributes &Attrs,
                                        SourceLocation *EndLoc,
                                        IdentifierInfo *ScopeName,
                                        SourceLocation ScopeLoc,
                                        ParsedAttr::Syntax Syntax);

  void
  ParseAttributeWithTypeArg(IdentifierInfo &AttrName,
                            SourceLocation AttrNameLoc, ParsedAttributes &Attrs,
                            SourceLocation *EndLoc, IdentifierInfo *ScopeName,
                            SourceLocation ScopeLoc, ParsedAttr::Syntax Syntax);

  void ParseTypeofSpecifier(DeclSpec &DS);
  SourceLocation ParseDecltypeSpecifier(DeclSpec &DS);
  void AnnotateExistingDecltypeSpecifier(const DeclSpec &DS,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  void ParseUnderlyingTypeSpecifier(DeclSpec &DS);
  void ParseAtomicSpecifier(DeclSpec &DS);

  ExprResult ParseAlignArgument(SourceLocation Start,
                                SourceLocation &EllipsisLoc);
  void ParseAlignmentSpecifier(ParsedAttributes &Attrs,
                               SourceLocation *endLoc = nullptr);

  VirtSpecifiers::Specifier isCXX11VirtSpecifier(const Token &Tok) const;
  VirtSpecifiers::Specifier isCXX11VirtSpecifier() const {
    return isCXX11VirtSpecifier(Tok);
  }
  void ParseOptionalCXX11VirtSpecifierSeq(VirtSpecifiers &VS, bool IsInterface,
                                          SourceLocation FriendLoc);

  bool isCXX11FinalKeyword() const;

  /// DeclaratorScopeObj - RAII object used in Parser::ParseDirectDeclarator to
  /// enter a new C++ declarator scope and exit it when the function is
  /// finished.
  class DeclaratorScopeObj {
    Parser &P;
    CXXScopeSpec &SS;
    bool EnteredScope;
    bool CreatedScope;
  public:
    DeclaratorScopeObj(Parser &p, CXXScopeSpec &ss)
      : P(p), SS(ss), EnteredScope(false), CreatedScope(false) {}

    void EnterDeclaratorScope() {
      assert(!EnteredScope && "Already entered the scope!");
      assert(SS.isSet() && "C++ scope was not set!");

      CreatedScope = true;
      P.EnterScope(0); // Not a decl scope.

      if (!P.Actions.ActOnCXXEnterDeclaratorScope(P.getCurScope(), SS))
        EnteredScope = true;
    }

    ~DeclaratorScopeObj() {
      if (EnteredScope) {
        assert(SS.isSet() && "C++ scope was cleared ?");
        P.Actions.ActOnCXXExitDeclaratorScope(P.getCurScope(), SS);
      }
      if (CreatedScope)
        P.ExitScope();
    }
  };

  /// ParseDeclarator - Parse and verify a newly-initialized declarator.
  void ParseDeclarator(Declarator &D);
  /// A function that parses a variant of direct-declarator.
  typedef void (Parser::*DirectDeclParseFunction)(Declarator&);
  void ParseDeclaratorInternal(Declarator &D,
                               DirectDeclParseFunction DirectDeclParser);

  enum AttrRequirements {
    AR_NoAttributesParsed = 0, ///< No attributes are diagnosed.
    AR_GNUAttributesParsedAndRejected = 1 << 0, ///< Diagnose GNU attributes.
    AR_GNUAttributesParsed = 1 << 1,
    AR_CXX11AttributesParsed = 1 << 2,
    AR_DeclspecAttributesParsed = 1 << 3,
    AR_AllAttributesParsed = AR_GNUAttributesParsed |
                             AR_CXX11AttributesParsed |
                             AR_DeclspecAttributesParsed,
    AR_VendorAttributesParsed = AR_GNUAttributesParsed |
                                AR_DeclspecAttributesParsed
  };

  void ParseTypeQualifierListOpt(
      DeclSpec &DS, unsigned AttrReqs = AR_AllAttributesParsed,
      bool AtomicAllowed = true, bool IdentifierRequired = false,
      Optional<llvm::function_ref<void()>> CodeCompletionHandler = None);
  void ParseDirectDeclarator(Declarator &D);
  void ParseDecompositionDeclarator(Declarator &D);
  void ParseParenDeclarator(Declarator &D);
  void ParseFunctionDeclarator(Declarator &D,
                               ParsedAttributes &attrs,
                               BalancedDelimiterTracker &Tracker,
                               bool IsAmbiguous,
                               bool RequiresArg = false);
  bool ParseRefQualifier(bool &RefQualifierIsLValueRef,
                         SourceLocation &RefQualifierLoc);
  bool isFunctionDeclaratorIdentifierList();
  void ParseFunctionDeclaratorIdentifierList(
         Declarator &D,
         SmallVectorImpl<DeclaratorChunk::ParamInfo> &ParamInfo);
  void ParseParameterDeclarationClause(
         Declarator &D,
         ParsedAttributes &attrs,
         SmallVectorImpl<DeclaratorChunk::ParamInfo> &ParamInfo,
         SourceLocation &EllipsisLoc);
  void ParseBracketDeclarator(Declarator &D);
  void ParseMisplacedBracketDeclarator(Declarator &D);

  //===--------------------------------------------------------------------===//
  // C++ 7: Declarations [dcl.dcl]

  /// The kind of attribute specifier we have found.
  enum CXX11AttributeKind {
    /// This is not an attribute specifier.
    CAK_NotAttributeSpecifier,
    /// This should be treated as an attribute-specifier.
    CAK_AttributeSpecifier,
    /// The next tokens are '[[', but this is not an attribute-specifier. This
    /// is ill-formed by C++11 [dcl.attr.grammar]p6.
    CAK_InvalidAttributeSpecifier
  };
  CXX11AttributeKind
  isCXX11AttributeSpecifier(bool Disambiguate = false,
                            bool OuterMightBeMessageSend = false);

  void DiagnoseUnexpectedNamespace(NamedDecl *Context);

  DeclGroupPtrTy ParseNamespace(DeclaratorContext Context,
                                SourceLocation &DeclEnd,
                                SourceLocation InlineLoc = SourceLocation());

  struct InnerNamespaceInfo {
    SourceLocation NamespaceLoc;
    SourceLocation InlineLoc;
    SourceLocation IdentLoc;
    IdentifierInfo *Ident;
  };
  using InnerNamespaceInfoList = llvm::SmallVector<InnerNamespaceInfo, 4>;

  void ParseInnerNamespace(const InnerNamespaceInfoList &InnerNSs,
                           unsigned int index, SourceLocation &InlineLoc,
                           ParsedAttributes &attrs,
                           BalancedDelimiterTracker &Tracker);
  Decl *ParseLinkage(ParsingDeclSpec &DS, DeclaratorContext Context);
  Decl *ParseExportDeclaration();
  DeclGroupPtrTy ParseUsingDirectiveOrDeclaration(
      DeclaratorContext Context, const ParsedTemplateInfo &TemplateInfo,
      SourceLocation &DeclEnd, ParsedAttributesWithRange &attrs);
  Decl *ParseUsingDirective(DeclaratorContext Context,
                            SourceLocation UsingLoc,
                            SourceLocation &DeclEnd,
                            ParsedAttributes &attrs);

  struct UsingDeclarator {
    SourceLocation TypenameLoc;
    CXXScopeSpec SS;
    UnqualifiedId Name;
    SourceLocation EllipsisLoc;

    void clear() {
      TypenameLoc = EllipsisLoc = SourceLocation();
      SS.clear();
      Name.clear();
    }
  };

  bool ParseUsingDeclarator(DeclaratorContext Context, UsingDeclarator &D);
  DeclGroupPtrTy ParseUsingDeclaration(DeclaratorContext Context,
                                       const ParsedTemplateInfo &TemplateInfo,
                                       SourceLocation UsingLoc,
                                       SourceLocation &DeclEnd,
                                       AccessSpecifier AS = AS_none);
  Decl *ParseAliasDeclarationAfterDeclarator(
      const ParsedTemplateInfo &TemplateInfo, SourceLocation UsingLoc,
      UsingDeclarator &D, SourceLocation &DeclEnd, AccessSpecifier AS,
      ParsedAttributes &Attrs, Decl **OwnedType = nullptr);

  Decl *ParseStaticAssertDeclaration(SourceLocation &DeclEnd);
  Decl *ParseNamespaceAlias(SourceLocation NamespaceLoc,
                            SourceLocation AliasLoc, IdentifierInfo *Alias,
                            SourceLocation &DeclEnd);

  //===--------------------------------------------------------------------===//
  // C++ 9: classes [class] and C structs/unions.
  bool isValidAfterTypeSpecifier(bool CouldBeBitfield);
  void ParseClassSpecifier(tok::TokenKind TagTokKind, SourceLocation TagLoc,
                           DeclSpec &DS, const ParsedTemplateInfo &TemplateInfo,
                           AccessSpecifier AS, bool EnteringContext,
                           DeclSpecContext DSC,
                           ParsedAttributesWithRange &Attributes);
  void SkipCXXMemberSpecification(SourceLocation StartLoc,
                                  SourceLocation AttrFixitLoc,
                                  unsigned TagType,
                                  Decl *TagDecl);
  void ParseCXXMemberSpecification(SourceLocation StartLoc,
                                   SourceLocation AttrFixitLoc,
                                   ParsedAttributesWithRange &Attrs,
                                   unsigned TagType,
                                   Decl *TagDecl);
  ExprResult ParseCXXMemberInitializer(Decl *D, bool IsFunction,
                                       SourceLocation &EqualLoc);
  bool ParseCXXMemberDeclaratorBeforeInitializer(Declarator &DeclaratorInfo,
                                                 VirtSpecifiers &VS,
                                                 ExprResult &BitfieldSize,
                                                 LateParsedAttrList &LateAttrs);
  void MaybeParseAndDiagnoseDeclSpecAfterCXX11VirtSpecifierSeq(Declarator &D,
                                                               VirtSpecifiers &VS);
  DeclGroupPtrTy ParseCXXClassMemberDeclaration(
      AccessSpecifier AS, ParsedAttributes &Attr,
      const ParsedTemplateInfo &TemplateInfo = ParsedTemplateInfo(),
      ParsingDeclRAIIObject *DiagsFromTParams = nullptr);
  DeclGroupPtrTy ParseCXXClassMemberDeclarationWithPragmas(
      AccessSpecifier &AS, ParsedAttributesWithRange &AccessAttrs,
      DeclSpec::TST TagType, Decl *Tag);
  void ParseConstructorInitializer(Decl *ConstructorDecl);
  MemInitResult ParseMemInitializer(Decl *ConstructorDecl);
  void HandleMemberFunctionDeclDelays(Declarator& DeclaratorInfo,
                                      Decl *ThisDecl);

  //===--------------------------------------------------------------------===//
  // C++ 10: Derived classes [class.derived]
  TypeResult ParseBaseTypeSpecifier(SourceLocation &BaseLoc,
                                    SourceLocation &EndLocation);
  void ParseBaseClause(Decl *ClassDecl);
  BaseResult ParseBaseSpecifier(Decl *ClassDecl);
  AccessSpecifier getAccessSpecifierIfPresent() const;

  bool ParseUnqualifiedIdTemplateId(CXXScopeSpec &SS,
                                    SourceLocation TemplateKWLoc,
                                    IdentifierInfo *Name,
                                    SourceLocation NameLoc,
                                    bool EnteringContext,
                                    ParsedType ObjectType,
                                    UnqualifiedId &Id,
                                    bool AssumeTemplateId);
  bool ParseUnqualifiedIdOperator(CXXScopeSpec &SS, bool EnteringContext,
                                  ParsedType ObjectType,
                                  UnqualifiedId &Result);

  //===--------------------------------------------------------------------===//
  // OpenMP: Directives and clauses.
  /// Parse clauses for '#pragma omp declare simd'.
  DeclGroupPtrTy ParseOMPDeclareSimdClauses(DeclGroupPtrTy Ptr,
                                            CachedTokens &Toks,
                                            SourceLocation Loc);
  /// Parse clauses for '#pragma omp declare target'.
  DeclGroupPtrTy ParseOMPDeclareTargetClauses();
  /// Parse '#pragma omp end declare target'.
  void ParseOMPEndDeclareTargetDirective(OpenMPDirectiveKind DKind,
                                         SourceLocation Loc);
  /// Parses declarative OpenMP directives.
  DeclGroupPtrTy ParseOpenMPDeclarativeDirectiveWithExtDecl(
      AccessSpecifier &AS, ParsedAttributesWithRange &Attrs,
      DeclSpec::TST TagType = DeclSpec::TST_unspecified,
      Decl *TagDecl = nullptr);
  /// Parse 'omp declare reduction' construct.
  DeclGroupPtrTy ParseOpenMPDeclareReductionDirective(AccessSpecifier AS);
  /// Parses initializer for provided omp_priv declaration inside the reduction
  /// initializer.
  void ParseOpenMPReductionInitializerForDecl(VarDecl *OmpPrivParm);

  /// Parses simple list of variables.
  ///
  /// \param Kind Kind of the directive.
  /// \param Callback Callback function to be called for the list elements.
  /// \param AllowScopeSpecifier true, if the variables can have fully
  /// qualified names.
  ///
  bool ParseOpenMPSimpleVarList(
      OpenMPDirectiveKind Kind,
      const llvm::function_ref<void(CXXScopeSpec &, DeclarationNameInfo)> &
          Callback,
      bool AllowScopeSpecifier);
  /// Parses declarative or executable directive.
  ///
  /// \param Allowed ACK_Any, if any directives are allowed,
  /// ACK_StatementsOpenMPAnyExecutable - if any executable directives are
  /// allowed, ACK_StatementsOpenMPNonStandalone - if only non-standalone
  /// executable directives are allowed.
  ///
  StmtResult
  ParseOpenMPDeclarativeOrExecutableDirective(AllowedConstructsKind Allowed);
  /// Parses clause of kind \a CKind for directive of a kind \a Kind.
  ///
  /// \param DKind Kind of current directive.
  /// \param CKind Kind of current clause.
  /// \param FirstClause true, if this is the first clause of a kind \a CKind
  /// in current directive.
  ///
  OMPClause *ParseOpenMPClause(OpenMPDirectiveKind DKind,
                               OpenMPClauseKind CKind, bool FirstClause);
  /// Parses clause with a single expression of a kind \a Kind.
  ///
  /// \param Kind Kind of current clause.
  /// \param ParseOnly true to skip the clause's semantic actions and return
  /// nullptr.
  ///
  OMPClause *ParseOpenMPSingleExprClause(OpenMPClauseKind Kind,
                                         bool ParseOnly);
  /// Parses simple clause of a kind \a Kind.
  ///
  /// \param Kind Kind of current clause.
  /// \param ParseOnly true to skip the clause's semantic actions and return
  /// nullptr.
  ///
  OMPClause *ParseOpenMPSimpleClause(OpenMPClauseKind Kind, bool ParseOnly);
  /// Parses clause with a single expression and an additional argument
  /// of a kind \a Kind.
  ///
  /// \param Kind Kind of current clause.
  /// \param ParseOnly true to skip the clause's semantic actions and return
  /// nullptr.
  ///
  OMPClause *ParseOpenMPSingleExprWithArgClause(OpenMPClauseKind Kind,
                                                bool ParseOnly);
  /// Parses clause without any additional arguments.
  ///
  /// \param Kind Kind of current clause.
  /// \param ParseOnly true to skip the clause's semantic actions and return
  /// nullptr.
  ///
  OMPClause *ParseOpenMPClause(OpenMPClauseKind Kind, bool ParseOnly = false);
  /// Parses clause with the list of variables of a kind \a Kind.
  ///
  /// \param Kind Kind of current clause.
  /// \param ParseOnly true to skip the clause's semantic actions and return
  /// nullptr.
  ///
  OMPClause *ParseOpenMPVarListClause(OpenMPDirectiveKind DKind,
                                      OpenMPClauseKind Kind, bool ParseOnly);

public:
  /// Parses simple expression in parens for single-expression clauses of OpenMP
  /// constructs.
  /// \param RLoc Returned location of right paren.
  ExprResult ParseOpenMPParensExpr(StringRef ClauseName, SourceLocation &RLoc);

  /// Data used for parsing list of variables in OpenMP clauses.
  struct OpenMPVarListDataTy {
    Expr *TailExpr = nullptr;
    SourceLocation ColonLoc;
    SourceLocation RLoc;
    CXXScopeSpec ReductionIdScopeSpec;
    DeclarationNameInfo ReductionId;
    OpenMPDependClauseKind DepKind = OMPC_DEPEND_unknown;
    OpenMPLinearClauseKind LinKind = OMPC_LINEAR_val;
    SmallVector<OpenMPMapModifierKind, OMPMapClause::NumberOfModifiers>
    MapTypeModifiers;
    SmallVector<SourceLocation, OMPMapClause::NumberOfModifiers>
    MapTypeModifiersLoc;
    OpenMPMapClauseKind MapType = OMPC_MAP_unknown;
    bool IsMapTypeImplicit = false;
    SourceLocation DepLinMapLoc;
  };

  /// Parses clauses with list.
  bool ParseOpenMPVarList(OpenMPDirectiveKind DKind, OpenMPClauseKind Kind,
                          SmallVectorImpl<Expr *> &Vars,
                          OpenMPVarListDataTy &Data);
  bool ParseUnqualifiedId(CXXScopeSpec &SS, bool EnteringContext,
                          bool AllowDestructorName,
                          bool AllowConstructorName,
                          bool AllowDeductionGuide,
                          ParsedType ObjectType,
                          SourceLocation *TemplateKWLoc,
                          UnqualifiedId &Result);

private:
  //===--------------------------------------------------------------------===//
  // C++ 14: Templates [temp]

  // C++ 14.1: Template Parameters [temp.param]
  Decl *ParseDeclarationStartingWithTemplate(DeclaratorContext Context,
                                             SourceLocation &DeclEnd,
                                             ParsedAttributes &AccessAttrs,
                                             AccessSpecifier AS = AS_none);
  Decl *ParseTemplateDeclarationOrSpecialization(DeclaratorContext Context,
                                                 SourceLocation &DeclEnd,
                                                 ParsedAttributes &AccessAttrs,
                                                 AccessSpecifier AS);
  Decl *ParseSingleDeclarationAfterTemplate(
      DeclaratorContext Context, const ParsedTemplateInfo &TemplateInfo,
      ParsingDeclRAIIObject &DiagsFromParams, SourceLocation &DeclEnd,
      ParsedAttributes &AccessAttrs, AccessSpecifier AS = AS_none);
  bool ParseTemplateParameters(unsigned Depth,
                               SmallVectorImpl<NamedDecl *> &TemplateParams,
                               SourceLocation &LAngleLoc,
                               SourceLocation &RAngleLoc);
  bool ParseTemplateParameterList(unsigned Depth,
                                  SmallVectorImpl<NamedDecl*> &TemplateParams);
  bool isStartOfTemplateTypeParameter();
  NamedDecl *ParseTemplateParameter(unsigned Depth, unsigned Position);
  NamedDecl *ParseTypeParameter(unsigned Depth, unsigned Position);
  NamedDecl *ParseTemplateTemplateParameter(unsigned Depth, unsigned Position);
  NamedDecl *ParseNonTypeTemplateParameter(unsigned Depth, unsigned Position);
  void DiagnoseMisplacedEllipsis(SourceLocation EllipsisLoc,
                                 SourceLocation CorrectLoc,
                                 bool AlreadyHasEllipsis,
                                 bool IdentifierHasName);
  void DiagnoseMisplacedEllipsisInDeclarator(SourceLocation EllipsisLoc,
                                             Declarator &D);
  // C++ 14.3: Template arguments [temp.arg]
  typedef SmallVector<ParsedTemplateArgument, 16> TemplateArgList;

  bool ParseGreaterThanInTemplateList(SourceLocation &RAngleLoc,
                                      bool ConsumeLastToken,
                                      bool ObjCGenericList);
  bool ParseTemplateIdAfterTemplateName(bool ConsumeLastToken,
                                        SourceLocation &LAngleLoc,
                                        TemplateArgList &TemplateArgs,
                                        SourceLocation &RAngleLoc);

  bool AnnotateTemplateIdToken(TemplateTy Template, TemplateNameKind TNK,
                               CXXScopeSpec &SS,
                               SourceLocation TemplateKWLoc,
                               UnqualifiedId &TemplateName,
                               bool AllowTypeAnnotation = true);
  void AnnotateTemplateIdTokenAsType(bool IsClassName = false);
  bool IsTemplateArgumentList(unsigned Skip = 0);
  bool ParseTemplateArgumentList(TemplateArgList &TemplateArgs);
  ParsedTemplateArgument ParseTemplateTemplateArgument();
  ParsedTemplateArgument ParseTemplateArgument();
  Decl *ParseExplicitInstantiation(DeclaratorContext Context,
                                   SourceLocation ExternLoc,
                                   SourceLocation TemplateLoc,
                                   SourceLocation &DeclEnd,
                                   ParsedAttributes &AccessAttrs,
                                   AccessSpecifier AS = AS_none);

  //===--------------------------------------------------------------------===//
  // Modules
  DeclGroupPtrTy ParseModuleDecl();
  Decl *ParseModuleImport(SourceLocation AtLoc);
  bool parseMisplacedModuleImport();
  bool tryParseMisplacedModuleImport() {
    tok::TokenKind Kind = Tok.getKind();
    if (Kind == tok::annot_module_begin || Kind == tok::annot_module_end ||
        Kind == tok::annot_module_include)
      return parseMisplacedModuleImport();
    return false;
  }

  bool ParseModuleName(
      SourceLocation UseLoc,
      SmallVectorImpl<std::pair<IdentifierInfo *, SourceLocation>> &Path,
      bool IsImport);

  //===--------------------------------------------------------------------===//
  // C++11/G++: Type Traits [Type-Traits.html in the GCC manual]
  ExprResult ParseTypeTrait();

  //===--------------------------------------------------------------------===//
  // Embarcadero: Arary and Expression Traits
  ExprResult ParseArrayTypeTrait();
  ExprResult ParseExpressionTrait();

  //===--------------------------------------------------------------------===//
  // Preprocessor code-completion pass-through
  void CodeCompleteDirective(bool InConditional) override;
  void CodeCompleteInConditionalExclusion() override;
  void CodeCompleteMacroName(bool IsDefinition) override;
  void CodeCompletePreprocessorExpression() override;
  void CodeCompleteMacroArgument(IdentifierInfo *Macro, MacroInfo *MacroInfo,
                                 unsigned ArgumentIndex) override;
  void CodeCompleteIncludedFile(llvm::StringRef Dir, bool IsAngled) override;
  void CodeCompleteNaturalLanguage() override;
};

}  // end namespace clang

#endif
