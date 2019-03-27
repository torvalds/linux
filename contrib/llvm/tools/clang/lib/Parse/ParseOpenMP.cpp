//===--- ParseOpenMP.cpp - OpenMP directives parsing ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements parsing of all OpenMP directives and clauses.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/Scope.h"
#include "llvm/ADT/PointerIntPair.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// OpenMP declarative directives.
//===----------------------------------------------------------------------===//

namespace {
enum OpenMPDirectiveKindEx {
  OMPD_cancellation = OMPD_unknown + 1,
  OMPD_data,
  OMPD_declare,
  OMPD_end,
  OMPD_end_declare,
  OMPD_enter,
  OMPD_exit,
  OMPD_point,
  OMPD_reduction,
  OMPD_target_enter,
  OMPD_target_exit,
  OMPD_update,
  OMPD_distribute_parallel,
  OMPD_teams_distribute_parallel,
  OMPD_target_teams_distribute_parallel
};

class ThreadprivateListParserHelper final {
  SmallVector<Expr *, 4> Identifiers;
  Parser *P;

public:
  ThreadprivateListParserHelper(Parser *P) : P(P) {}
  void operator()(CXXScopeSpec &SS, DeclarationNameInfo NameInfo) {
    ExprResult Res =
        P->getActions().ActOnOpenMPIdExpression(P->getCurScope(), SS, NameInfo);
    if (Res.isUsable())
      Identifiers.push_back(Res.get());
  }
  llvm::ArrayRef<Expr *> getIdentifiers() const { return Identifiers; }
};
} // namespace

// Map token string to extended OMP token kind that are
// OpenMPDirectiveKind + OpenMPDirectiveKindEx.
static unsigned getOpenMPDirectiveKindEx(StringRef S) {
  auto DKind = getOpenMPDirectiveKind(S);
  if (DKind != OMPD_unknown)
    return DKind;

  return llvm::StringSwitch<unsigned>(S)
      .Case("cancellation", OMPD_cancellation)
      .Case("data", OMPD_data)
      .Case("declare", OMPD_declare)
      .Case("end", OMPD_end)
      .Case("enter", OMPD_enter)
      .Case("exit", OMPD_exit)
      .Case("point", OMPD_point)
      .Case("reduction", OMPD_reduction)
      .Case("update", OMPD_update)
      .Default(OMPD_unknown);
}

static OpenMPDirectiveKind parseOpenMPDirectiveKind(Parser &P) {
  // Array of foldings: F[i][0] F[i][1] ===> F[i][2].
  // E.g.: OMPD_for OMPD_simd ===> OMPD_for_simd
  // TODO: add other combined directives in topological order.
  static const unsigned F[][3] = {
      {OMPD_cancellation, OMPD_point, OMPD_cancellation_point},
      {OMPD_declare, OMPD_reduction, OMPD_declare_reduction},
      {OMPD_declare, OMPD_simd, OMPD_declare_simd},
      {OMPD_declare, OMPD_target, OMPD_declare_target},
      {OMPD_distribute, OMPD_parallel, OMPD_distribute_parallel},
      {OMPD_distribute_parallel, OMPD_for, OMPD_distribute_parallel_for},
      {OMPD_distribute_parallel_for, OMPD_simd,
       OMPD_distribute_parallel_for_simd},
      {OMPD_distribute, OMPD_simd, OMPD_distribute_simd},
      {OMPD_end, OMPD_declare, OMPD_end_declare},
      {OMPD_end_declare, OMPD_target, OMPD_end_declare_target},
      {OMPD_target, OMPD_data, OMPD_target_data},
      {OMPD_target, OMPD_enter, OMPD_target_enter},
      {OMPD_target, OMPD_exit, OMPD_target_exit},
      {OMPD_target, OMPD_update, OMPD_target_update},
      {OMPD_target_enter, OMPD_data, OMPD_target_enter_data},
      {OMPD_target_exit, OMPD_data, OMPD_target_exit_data},
      {OMPD_for, OMPD_simd, OMPD_for_simd},
      {OMPD_parallel, OMPD_for, OMPD_parallel_for},
      {OMPD_parallel_for, OMPD_simd, OMPD_parallel_for_simd},
      {OMPD_parallel, OMPD_sections, OMPD_parallel_sections},
      {OMPD_taskloop, OMPD_simd, OMPD_taskloop_simd},
      {OMPD_target, OMPD_parallel, OMPD_target_parallel},
      {OMPD_target, OMPD_simd, OMPD_target_simd},
      {OMPD_target_parallel, OMPD_for, OMPD_target_parallel_for},
      {OMPD_target_parallel_for, OMPD_simd, OMPD_target_parallel_for_simd},
      {OMPD_teams, OMPD_distribute, OMPD_teams_distribute},
      {OMPD_teams_distribute, OMPD_simd, OMPD_teams_distribute_simd},
      {OMPD_teams_distribute, OMPD_parallel, OMPD_teams_distribute_parallel},
      {OMPD_teams_distribute_parallel, OMPD_for,
       OMPD_teams_distribute_parallel_for},
      {OMPD_teams_distribute_parallel_for, OMPD_simd,
       OMPD_teams_distribute_parallel_for_simd},
      {OMPD_target, OMPD_teams, OMPD_target_teams},
      {OMPD_target_teams, OMPD_distribute, OMPD_target_teams_distribute},
      {OMPD_target_teams_distribute, OMPD_parallel,
       OMPD_target_teams_distribute_parallel},
      {OMPD_target_teams_distribute, OMPD_simd,
       OMPD_target_teams_distribute_simd},
      {OMPD_target_teams_distribute_parallel, OMPD_for,
       OMPD_target_teams_distribute_parallel_for},
      {OMPD_target_teams_distribute_parallel_for, OMPD_simd,
       OMPD_target_teams_distribute_parallel_for_simd}};
  enum { CancellationPoint = 0, DeclareReduction = 1, TargetData = 2 };
  Token Tok = P.getCurToken();
  unsigned DKind =
      Tok.isAnnotation()
          ? static_cast<unsigned>(OMPD_unknown)
          : getOpenMPDirectiveKindEx(P.getPreprocessor().getSpelling(Tok));
  if (DKind == OMPD_unknown)
    return OMPD_unknown;

  for (unsigned I = 0; I < llvm::array_lengthof(F); ++I) {
    if (DKind != F[I][0])
      continue;

    Tok = P.getPreprocessor().LookAhead(0);
    unsigned SDKind =
        Tok.isAnnotation()
            ? static_cast<unsigned>(OMPD_unknown)
            : getOpenMPDirectiveKindEx(P.getPreprocessor().getSpelling(Tok));
    if (SDKind == OMPD_unknown)
      continue;

    if (SDKind == F[I][1]) {
      P.ConsumeToken();
      DKind = F[I][2];
    }
  }
  return DKind < OMPD_unknown ? static_cast<OpenMPDirectiveKind>(DKind)
                              : OMPD_unknown;
}

static DeclarationName parseOpenMPReductionId(Parser &P) {
  Token Tok = P.getCurToken();
  Sema &Actions = P.getActions();
  OverloadedOperatorKind OOK = OO_None;
  // Allow to use 'operator' keyword for C++ operators
  bool WithOperator = false;
  if (Tok.is(tok::kw_operator)) {
    P.ConsumeToken();
    Tok = P.getCurToken();
    WithOperator = true;
  }
  switch (Tok.getKind()) {
  case tok::plus: // '+'
    OOK = OO_Plus;
    break;
  case tok::minus: // '-'
    OOK = OO_Minus;
    break;
  case tok::star: // '*'
    OOK = OO_Star;
    break;
  case tok::amp: // '&'
    OOK = OO_Amp;
    break;
  case tok::pipe: // '|'
    OOK = OO_Pipe;
    break;
  case tok::caret: // '^'
    OOK = OO_Caret;
    break;
  case tok::ampamp: // '&&'
    OOK = OO_AmpAmp;
    break;
  case tok::pipepipe: // '||'
    OOK = OO_PipePipe;
    break;
  case tok::identifier: // identifier
    if (!WithOperator)
      break;
    LLVM_FALLTHROUGH;
  default:
    P.Diag(Tok.getLocation(), diag::err_omp_expected_reduction_identifier);
    P.SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                Parser::StopBeforeMatch);
    return DeclarationName();
  }
  P.ConsumeToken();
  auto &DeclNames = Actions.getASTContext().DeclarationNames;
  return OOK == OO_None ? DeclNames.getIdentifier(Tok.getIdentifierInfo())
                        : DeclNames.getCXXOperatorName(OOK);
}

/// Parse 'omp declare reduction' construct.
///
///       declare-reduction-directive:
///        annot_pragma_openmp 'declare' 'reduction'
///        '(' <reduction_id> ':' <type> {',' <type>} ':' <expression> ')'
///        ['initializer' '(' ('omp_priv' '=' <expression>)|<function_call> ')']
///        annot_pragma_openmp_end
/// <reduction_id> is either a base language identifier or one of the following
/// operators: '+', '-', '*', '&', '|', '^', '&&' and '||'.
///
Parser::DeclGroupPtrTy
Parser::ParseOpenMPDeclareReductionDirective(AccessSpecifier AS) {
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPDirectiveName(OMPD_declare_reduction))) {
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  DeclarationName Name = parseOpenMPReductionId(*this);
  if (Name.isEmpty() && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  // Consume ':'.
  bool IsCorrect = !ExpectAndConsume(tok::colon);

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  IsCorrect = IsCorrect && !Name.isEmpty();

  if (Tok.is(tok::colon) || Tok.is(tok::annot_pragma_openmp_end)) {
    Diag(Tok.getLocation(), diag::err_expected_type);
    IsCorrect = false;
  }

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  SmallVector<std::pair<QualType, SourceLocation>, 8> ReductionTypes;
  // Parse list of types until ':' token.
  do {
    ColonProtectionRAIIObject ColonRAII(*this);
    SourceRange Range;
    TypeResult TR =
        ParseTypeName(&Range, DeclaratorContext::PrototypeContext, AS);
    if (TR.isUsable()) {
      QualType ReductionType =
          Actions.ActOnOpenMPDeclareReductionType(Range.getBegin(), TR);
      if (!ReductionType.isNull()) {
        ReductionTypes.push_back(
            std::make_pair(ReductionType, Range.getBegin()));
      }
    } else {
      SkipUntil(tok::comma, tok::colon, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }

    if (Tok.is(tok::colon) || Tok.is(tok::annot_pragma_openmp_end))
      break;

    // Consume ','.
    if (ExpectAndConsume(tok::comma)) {
      IsCorrect = false;
      if (Tok.is(tok::annot_pragma_openmp_end)) {
        Diag(Tok.getLocation(), diag::err_expected_type);
        return DeclGroupPtrTy();
      }
    }
  } while (Tok.isNot(tok::annot_pragma_openmp_end));

  if (ReductionTypes.empty()) {
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  // Consume ':'.
  if (ExpectAndConsume(tok::colon))
    IsCorrect = false;

  if (Tok.is(tok::annot_pragma_openmp_end)) {
    Diag(Tok.getLocation(), diag::err_expected_expression);
    return DeclGroupPtrTy();
  }

  DeclGroupPtrTy DRD = Actions.ActOnOpenMPDeclareReductionDirectiveStart(
      getCurScope(), Actions.getCurLexicalContext(), Name, ReductionTypes, AS);

  // Parse <combiner> expression and then parse initializer if any for each
  // correct type.
  unsigned I = 0, E = ReductionTypes.size();
  for (Decl *D : DRD.get()) {
    TentativeParsingAction TPA(*this);
    ParseScope OMPDRScope(this, Scope::FnScope | Scope::DeclScope |
                                    Scope::CompoundStmtScope |
                                    Scope::OpenMPDirectiveScope);
    // Parse <combiner> expression.
    Actions.ActOnOpenMPDeclareReductionCombinerStart(getCurScope(), D);
    ExprResult CombinerResult =
        Actions.ActOnFinishFullExpr(ParseAssignmentExpression().get(),
                                    D->getLocation(), /*DiscardedValue=*/true);
    Actions.ActOnOpenMPDeclareReductionCombinerEnd(D, CombinerResult.get());

    if (CombinerResult.isInvalid() && Tok.isNot(tok::r_paren) &&
        Tok.isNot(tok::annot_pragma_openmp_end)) {
      TPA.Commit();
      IsCorrect = false;
      break;
    }
    IsCorrect = !T.consumeClose() && IsCorrect && CombinerResult.isUsable();
    ExprResult InitializerResult;
    if (Tok.isNot(tok::annot_pragma_openmp_end)) {
      // Parse <initializer> expression.
      if (Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo()->isStr("initializer")) {
        ConsumeToken();
      } else {
        Diag(Tok.getLocation(), diag::err_expected) << "'initializer'";
        TPA.Commit();
        IsCorrect = false;
        break;
      }
      // Parse '('.
      BalancedDelimiterTracker T(*this, tok::l_paren,
                                 tok::annot_pragma_openmp_end);
      IsCorrect =
          !T.expectAndConsume(diag::err_expected_lparen_after, "initializer") &&
          IsCorrect;
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        ParseScope OMPDRScope(this, Scope::FnScope | Scope::DeclScope |
                                        Scope::CompoundStmtScope |
                                        Scope::OpenMPDirectiveScope);
        // Parse expression.
        VarDecl *OmpPrivParm =
            Actions.ActOnOpenMPDeclareReductionInitializerStart(getCurScope(),
                                                                D);
        // Check if initializer is omp_priv <init_expr> or something else.
        if (Tok.is(tok::identifier) &&
            Tok.getIdentifierInfo()->isStr("omp_priv")) {
          if (Actions.getLangOpts().CPlusPlus) {
            InitializerResult = Actions.ActOnFinishFullExpr(
                ParseAssignmentExpression().get(), D->getLocation(),
                /*DiscardedValue=*/true);
          } else {
            ConsumeToken();
            ParseOpenMPReductionInitializerForDecl(OmpPrivParm);
          }
        } else {
          InitializerResult = Actions.ActOnFinishFullExpr(
              ParseAssignmentExpression().get(), D->getLocation(),
              /*DiscardedValue=*/true);
        }
        Actions.ActOnOpenMPDeclareReductionInitializerEnd(
            D, InitializerResult.get(), OmpPrivParm);
        if (InitializerResult.isInvalid() && Tok.isNot(tok::r_paren) &&
            Tok.isNot(tok::annot_pragma_openmp_end)) {
          TPA.Commit();
          IsCorrect = false;
          break;
        }
        IsCorrect =
            !T.consumeClose() && IsCorrect && !InitializerResult.isInvalid();
      }
    }

    ++I;
    // Revert parsing if not the last type, otherwise accept it, we're done with
    // parsing.
    if (I != E)
      TPA.Revert();
    else
      TPA.Commit();
  }
  return Actions.ActOnOpenMPDeclareReductionDirectiveEnd(getCurScope(), DRD,
                                                         IsCorrect);
}

void Parser::ParseOpenMPReductionInitializerForDecl(VarDecl *OmpPrivParm) {
  // Parse declarator '=' initializer.
  // If a '==' or '+=' is found, suggest a fixit to '='.
  if (isTokenEqualOrEqualTypo()) {
    ConsumeToken();

    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteInitializer(getCurScope(), OmpPrivParm);
      Actions.FinalizeDeclaration(OmpPrivParm);
      cutOffParsing();
      return;
    }

    ExprResult Init(ParseInitializer());

    if (Init.isInvalid()) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openmp_end, StopBeforeMatch);
      Actions.ActOnInitializerError(OmpPrivParm);
    } else {
      Actions.AddInitializerToDecl(OmpPrivParm, Init.get(),
                                   /*DirectInit=*/false);
    }
  } else if (Tok.is(tok::l_paren)) {
    // Parse C++ direct initializer: '(' expression-list ')'
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();

    ExprVector Exprs;
    CommaLocsTy CommaLocs;

    SourceLocation LParLoc = T.getOpenLocation();
    if (ParseExpressionList(
            Exprs, CommaLocs, [this, OmpPrivParm, LParLoc, &Exprs] {
              QualType PreferredType = Actions.ProduceConstructorSignatureHelp(
                  getCurScope(),
                  OmpPrivParm->getType()->getCanonicalTypeInternal(),
                  OmpPrivParm->getLocation(), Exprs, LParLoc);
              CalledSignatureHelp = true;
              Actions.CodeCompleteExpression(getCurScope(), PreferredType);
            })) {
      if (PP.isCodeCompletionReached() && !CalledSignatureHelp) {
        Actions.ProduceConstructorSignatureHelp(
            getCurScope(), OmpPrivParm->getType()->getCanonicalTypeInternal(),
            OmpPrivParm->getLocation(), Exprs, LParLoc);
        CalledSignatureHelp = true;
      }
      Actions.ActOnInitializerError(OmpPrivParm);
      SkipUntil(tok::r_paren, tok::annot_pragma_openmp_end, StopBeforeMatch);
    } else {
      // Match the ')'.
      SourceLocation RLoc = Tok.getLocation();
      if (!T.consumeClose())
        RLoc = T.getCloseLocation();

      assert(!Exprs.empty() && Exprs.size() - 1 == CommaLocs.size() &&
             "Unexpected number of commas!");

      ExprResult Initializer =
          Actions.ActOnParenListExpr(T.getOpenLocation(), RLoc, Exprs);
      Actions.AddInitializerToDecl(OmpPrivParm, Initializer.get(),
                                   /*DirectInit=*/true);
    }
  } else if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
    // Parse C++0x braced-init-list.
    Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);

    ExprResult Init(ParseBraceInitializer());

    if (Init.isInvalid()) {
      Actions.ActOnInitializerError(OmpPrivParm);
    } else {
      Actions.AddInitializerToDecl(OmpPrivParm, Init.get(),
                                   /*DirectInit=*/true);
    }
  } else {
    Actions.ActOnUninitializedDecl(OmpPrivParm);
  }
}

namespace {
/// RAII that recreates function context for correct parsing of clauses of
/// 'declare simd' construct.
/// OpenMP, 2.8.2 declare simd Construct
/// The expressions appearing in the clauses of this directive are evaluated in
/// the scope of the arguments of the function declaration or definition.
class FNContextRAII final {
  Parser &P;
  Sema::CXXThisScopeRAII *ThisScope;
  Parser::ParseScope *TempScope;
  Parser::ParseScope *FnScope;
  bool HasTemplateScope = false;
  bool HasFunScope = false;
  FNContextRAII() = delete;
  FNContextRAII(const FNContextRAII &) = delete;
  FNContextRAII &operator=(const FNContextRAII &) = delete;

public:
  FNContextRAII(Parser &P, Parser::DeclGroupPtrTy Ptr) : P(P) {
    Decl *D = *Ptr.get().begin();
    NamedDecl *ND = dyn_cast<NamedDecl>(D);
    RecordDecl *RD = dyn_cast_or_null<RecordDecl>(D->getDeclContext());
    Sema &Actions = P.getActions();

    // Allow 'this' within late-parsed attributes.
    ThisScope = new Sema::CXXThisScopeRAII(Actions, RD, Qualifiers(),
                                           ND && ND->isCXXInstanceMember());

    // If the Decl is templatized, add template parameters to scope.
    HasTemplateScope = D->isTemplateDecl();
    TempScope =
        new Parser::ParseScope(&P, Scope::TemplateParamScope, HasTemplateScope);
    if (HasTemplateScope)
      Actions.ActOnReenterTemplateScope(Actions.getCurScope(), D);

    // If the Decl is on a function, add function parameters to the scope.
    HasFunScope = D->isFunctionOrFunctionTemplate();
    FnScope = new Parser::ParseScope(
        &P, Scope::FnScope | Scope::DeclScope | Scope::CompoundStmtScope,
        HasFunScope);
    if (HasFunScope)
      Actions.ActOnReenterFunctionContext(Actions.getCurScope(), D);
  }
  ~FNContextRAII() {
    if (HasFunScope) {
      P.getActions().ActOnExitFunctionContext();
      FnScope->Exit(); // Pop scope, and remove Decls from IdResolver
    }
    if (HasTemplateScope)
      TempScope->Exit();
    delete FnScope;
    delete TempScope;
    delete ThisScope;
  }
};
} // namespace

/// Parses clauses for 'declare simd' directive.
///    clause:
///      'inbranch' | 'notinbranch'
///      'simdlen' '(' <expr> ')'
///      { 'uniform' '(' <argument_list> ')' }
///      { 'aligned '(' <argument_list> [ ':' <alignment> ] ')' }
///      { 'linear '(' <argument_list> [ ':' <step> ] ')' }
static bool parseDeclareSimdClauses(
    Parser &P, OMPDeclareSimdDeclAttr::BranchStateTy &BS, ExprResult &SimdLen,
    SmallVectorImpl<Expr *> &Uniforms, SmallVectorImpl<Expr *> &Aligneds,
    SmallVectorImpl<Expr *> &Alignments, SmallVectorImpl<Expr *> &Linears,
    SmallVectorImpl<unsigned> &LinModifiers, SmallVectorImpl<Expr *> &Steps) {
  SourceRange BSRange;
  const Token &Tok = P.getCurToken();
  bool IsError = false;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    if (Tok.isNot(tok::identifier))
      break;
    OMPDeclareSimdDeclAttr::BranchStateTy Out;
    IdentifierInfo *II = Tok.getIdentifierInfo();
    StringRef ClauseName = II->getName();
    // Parse 'inranch|notinbranch' clauses.
    if (OMPDeclareSimdDeclAttr::ConvertStrToBranchStateTy(ClauseName, Out)) {
      if (BS != OMPDeclareSimdDeclAttr::BS_Undefined && BS != Out) {
        P.Diag(Tok, diag::err_omp_declare_simd_inbranch_notinbranch)
            << ClauseName
            << OMPDeclareSimdDeclAttr::ConvertBranchStateTyToStr(BS) << BSRange;
        IsError = true;
      }
      BS = Out;
      BSRange = SourceRange(Tok.getLocation(), Tok.getEndLoc());
      P.ConsumeToken();
    } else if (ClauseName.equals("simdlen")) {
      if (SimdLen.isUsable()) {
        P.Diag(Tok, diag::err_omp_more_one_clause)
            << getOpenMPDirectiveName(OMPD_declare_simd) << ClauseName << 0;
        IsError = true;
      }
      P.ConsumeToken();
      SourceLocation RLoc;
      SimdLen = P.ParseOpenMPParensExpr(ClauseName, RLoc);
      if (SimdLen.isInvalid())
        IsError = true;
    } else {
      OpenMPClauseKind CKind = getOpenMPClauseKind(ClauseName);
      if (CKind == OMPC_uniform || CKind == OMPC_aligned ||
          CKind == OMPC_linear) {
        Parser::OpenMPVarListDataTy Data;
        SmallVectorImpl<Expr *> *Vars = &Uniforms;
        if (CKind == OMPC_aligned)
          Vars = &Aligneds;
        else if (CKind == OMPC_linear)
          Vars = &Linears;

        P.ConsumeToken();
        if (P.ParseOpenMPVarList(OMPD_declare_simd,
                                 getOpenMPClauseKind(ClauseName), *Vars, Data))
          IsError = true;
        if (CKind == OMPC_aligned) {
          Alignments.append(Aligneds.size() - Alignments.size(), Data.TailExpr);
        } else if (CKind == OMPC_linear) {
          if (P.getActions().CheckOpenMPLinearModifier(Data.LinKind,
                                                       Data.DepLinMapLoc))
            Data.LinKind = OMPC_LINEAR_val;
          LinModifiers.append(Linears.size() - LinModifiers.size(),
                              Data.LinKind);
          Steps.append(Linears.size() - Steps.size(), Data.TailExpr);
        }
      } else
        // TODO: add parsing of other clauses.
        break;
    }
    // Skip ',' if any.
    if (Tok.is(tok::comma))
      P.ConsumeToken();
  }
  return IsError;
}

/// Parse clauses for '#pragma omp declare simd'.
Parser::DeclGroupPtrTy
Parser::ParseOMPDeclareSimdClauses(Parser::DeclGroupPtrTy Ptr,
                                   CachedTokens &Toks, SourceLocation Loc) {
  PP.EnterToken(Tok);
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true);
  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);

  FNContextRAII FnContext(*this, Ptr);
  OMPDeclareSimdDeclAttr::BranchStateTy BS =
      OMPDeclareSimdDeclAttr::BS_Undefined;
  ExprResult Simdlen;
  SmallVector<Expr *, 4> Uniforms;
  SmallVector<Expr *, 4> Aligneds;
  SmallVector<Expr *, 4> Alignments;
  SmallVector<Expr *, 4> Linears;
  SmallVector<unsigned, 4> LinModifiers;
  SmallVector<Expr *, 4> Steps;
  bool IsError =
      parseDeclareSimdClauses(*this, BS, Simdlen, Uniforms, Aligneds,
                              Alignments, Linears, LinModifiers, Steps);
  // Need to check for extra tokens.
  if (Tok.isNot(tok::annot_pragma_openmp_end)) {
    Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
        << getOpenMPDirectiveName(OMPD_declare_simd);
    while (Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
  }
  // Skip the last annot_pragma_openmp_end.
  SourceLocation EndLoc = ConsumeAnnotationToken();
  if (IsError)
    return Ptr;
  return Actions.ActOnOpenMPDeclareSimdDirective(
      Ptr, BS, Simdlen.get(), Uniforms, Aligneds, Alignments, Linears,
      LinModifiers, Steps, SourceRange(Loc, EndLoc));
}

Parser::DeclGroupPtrTy Parser::ParseOMPDeclareTargetClauses() {
  // OpenMP 4.5 syntax with list of entities.
  Sema::NamedDeclSetType SameDirectiveDecls;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    OMPDeclareTargetDeclAttr::MapTypeTy MT = OMPDeclareTargetDeclAttr::MT_To;
    if (Tok.is(tok::identifier)) {
      IdentifierInfo *II = Tok.getIdentifierInfo();
      StringRef ClauseName = II->getName();
      // Parse 'to|link' clauses.
      if (!OMPDeclareTargetDeclAttr::ConvertStrToMapTypeTy(ClauseName, MT)) {
        Diag(Tok, diag::err_omp_declare_target_unexpected_clause) << ClauseName;
        break;
      }
      ConsumeToken();
    }
    auto &&Callback = [this, MT, &SameDirectiveDecls](
        CXXScopeSpec &SS, DeclarationNameInfo NameInfo) {
      Actions.ActOnOpenMPDeclareTargetName(getCurScope(), SS, NameInfo, MT,
                                           SameDirectiveDecls);
    };
    if (ParseOpenMPSimpleVarList(OMPD_declare_target, Callback,
                                 /*AllowScopeSpecifier=*/true))
      break;

    // Consume optional ','.
    if (Tok.is(tok::comma))
      ConsumeToken();
  }
  SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
  ConsumeAnyToken();
  SmallVector<Decl *, 4> Decls(SameDirectiveDecls.begin(),
                               SameDirectiveDecls.end());
  if (Decls.empty())
    return DeclGroupPtrTy();
  return Actions.BuildDeclaratorGroup(Decls);
}

void Parser::ParseOMPEndDeclareTargetDirective(OpenMPDirectiveKind DKind,
                                               SourceLocation DTLoc) {
  if (DKind != OMPD_end_declare_target) {
    Diag(Tok, diag::err_expected_end_declare_target);
    Diag(DTLoc, diag::note_matching) << "'#pragma omp declare target'";
    return;
  }
  ConsumeAnyToken();
  if (Tok.isNot(tok::annot_pragma_openmp_end)) {
    Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
        << getOpenMPDirectiveName(OMPD_end_declare_target);
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
  }
  // Skip the last annot_pragma_openmp_end.
  ConsumeAnyToken();
}

/// Parsing of declarative OpenMP directives.
///
///       threadprivate-directive:
///         annot_pragma_openmp 'threadprivate' simple-variable-list
///         annot_pragma_openmp_end
///
///       declare-reduction-directive:
///        annot_pragma_openmp 'declare' 'reduction' [...]
///        annot_pragma_openmp_end
///
///       declare-simd-directive:
///         annot_pragma_openmp 'declare simd' {<clause> [,]}
///         annot_pragma_openmp_end
///         <function declaration/definition>
///
///       requires directive:
///         annot_pragma_openmp 'requires' <clause> [[[,] <clause>] ... ]
///         annot_pragma_openmp_end
///
Parser::DeclGroupPtrTy Parser::ParseOpenMPDeclarativeDirectiveWithExtDecl(
    AccessSpecifier &AS, ParsedAttributesWithRange &Attrs,
    DeclSpec::TST TagType, Decl *Tag) {
  assert(Tok.is(tok::annot_pragma_openmp) && "Not an OpenMP directive!");
  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  SourceLocation Loc = ConsumeAnnotationToken();
  OpenMPDirectiveKind DKind = parseOpenMPDirectiveKind(*this);

  switch (DKind) {
  case OMPD_threadprivate: {
    ConsumeToken();
    ThreadprivateListParserHelper Helper(this);
    if (!ParseOpenMPSimpleVarList(OMPD_threadprivate, Helper, true)) {
      // The last seen token is annot_pragma_openmp_end - need to check for
      // extra tokens.
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
            << getOpenMPDirectiveName(OMPD_threadprivate);
        SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
      }
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      return Actions.ActOnOpenMPThreadprivateDirective(Loc,
                                                       Helper.getIdentifiers());
    }
    break;
  }
  case OMPD_requires: {
    SourceLocation StartLoc = ConsumeToken();
    SmallVector<OMPClause *, 5> Clauses;
    SmallVector<llvm::PointerIntPair<OMPClause *, 1, bool>, OMPC_unknown + 1>
    FirstClauses(OMPC_unknown + 1);
    if (Tok.is(tok::annot_pragma_openmp_end)) {
      Diag(Tok, diag::err_omp_expected_clause) 
          << getOpenMPDirectiveName(OMPD_requires);
      break;
    }
    while (Tok.isNot(tok::annot_pragma_openmp_end)) {
      OpenMPClauseKind CKind = Tok.isAnnotation()
                                   ? OMPC_unknown
                                   : getOpenMPClauseKind(PP.getSpelling(Tok));
      Actions.StartOpenMPClause(CKind);
      OMPClause *Clause =
          ParseOpenMPClause(OMPD_requires, CKind, !FirstClauses[CKind].getInt());
      SkipUntil(tok::comma, tok::identifier, tok::annot_pragma_openmp_end, StopBeforeMatch);
      FirstClauses[CKind].setInt(true);
      if (Clause != nullptr)
        Clauses.push_back(Clause);
      if (Tok.is(tok::annot_pragma_openmp_end)) {
        Actions.EndOpenMPClause();
        break;
      }
      // Skip ',' if any.
      if (Tok.is(tok::comma))
        ConsumeToken();
      Actions.EndOpenMPClause();
    }
    // Consume final annot_pragma_openmp_end
    if (Clauses.size() == 0) {
      Diag(Tok, diag::err_omp_expected_clause)
          << getOpenMPDirectiveName(OMPD_requires);
      ConsumeAnnotationToken();
      return nullptr;
    }
    ConsumeAnnotationToken();
    return Actions.ActOnOpenMPRequiresDirective(StartLoc, Clauses);
  }
  case OMPD_declare_reduction:
    ConsumeToken();
    if (DeclGroupPtrTy Res = ParseOpenMPDeclareReductionDirective(AS)) {
      // The last seen token is annot_pragma_openmp_end - need to check for
      // extra tokens.
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
            << getOpenMPDirectiveName(OMPD_declare_reduction);
        while (Tok.isNot(tok::annot_pragma_openmp_end))
          ConsumeAnyToken();
      }
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      return Res;
    }
    break;
  case OMPD_declare_simd: {
    // The syntax is:
    // { #pragma omp declare simd }
    // <function-declaration-or-definition>
    //
    ConsumeToken();
    CachedTokens Toks;
    while(Tok.isNot(tok::annot_pragma_openmp_end)) {
      Toks.push_back(Tok);
      ConsumeAnyToken();
    }
    Toks.push_back(Tok);
    ConsumeAnyToken();

    DeclGroupPtrTy Ptr;
    if (Tok.is(tok::annot_pragma_openmp)) {
      Ptr = ParseOpenMPDeclarativeDirectiveWithExtDecl(AS, Attrs, TagType, Tag);
    } else if (Tok.isNot(tok::r_brace) && !isEofOrEom()) {
      // Here we expect to see some function declaration.
      if (AS == AS_none) {
        assert(TagType == DeclSpec::TST_unspecified);
        MaybeParseCXX11Attributes(Attrs);
        ParsingDeclSpec PDS(*this);
        Ptr = ParseExternalDeclaration(Attrs, &PDS);
      } else {
        Ptr =
            ParseCXXClassMemberDeclarationWithPragmas(AS, Attrs, TagType, Tag);
      }
    }
    if (!Ptr) {
      Diag(Loc, diag::err_omp_decl_in_declare_simd);
      return DeclGroupPtrTy();
    }
    return ParseOMPDeclareSimdClauses(Ptr, Toks, Loc);
  }
  case OMPD_declare_target: {
    SourceLocation DTLoc = ConsumeAnyToken();
    if (Tok.isNot(tok::annot_pragma_openmp_end)) {
      return ParseOMPDeclareTargetClauses();
    }

    // Skip the last annot_pragma_openmp_end.
    ConsumeAnyToken();

    if (!Actions.ActOnStartOpenMPDeclareTargetDirective(DTLoc))
      return DeclGroupPtrTy();

    llvm::SmallVector<Decl *, 4>  Decls;
    DKind = parseOpenMPDirectiveKind(*this);
    while (DKind != OMPD_end_declare_target && Tok.isNot(tok::eof) &&
           Tok.isNot(tok::r_brace)) {
      DeclGroupPtrTy Ptr;
      // Here we expect to see some function declaration.
      if (AS == AS_none) {
        assert(TagType == DeclSpec::TST_unspecified);
        MaybeParseCXX11Attributes(Attrs);
        ParsingDeclSpec PDS(*this);
        Ptr = ParseExternalDeclaration(Attrs, &PDS);
      } else {
        Ptr =
            ParseCXXClassMemberDeclarationWithPragmas(AS, Attrs, TagType, Tag);
      }
      if (Ptr) {
        DeclGroupRef Ref = Ptr.get();
        Decls.append(Ref.begin(), Ref.end());
      }
      if (Tok.isAnnotation() && Tok.is(tok::annot_pragma_openmp)) {
        TentativeParsingAction TPA(*this);
        ConsumeAnnotationToken();
        DKind = parseOpenMPDirectiveKind(*this);
        if (DKind != OMPD_end_declare_target)
          TPA.Revert();
        else
          TPA.Commit();
      }
    }

    ParseOMPEndDeclareTargetDirective(DKind, DTLoc);
    Actions.ActOnFinishOpenMPDeclareTargetDirective();
    return Actions.BuildDeclaratorGroup(Decls);
  }
  case OMPD_unknown:
    Diag(Tok, diag::err_omp_unknown_directive);
    break;
  case OMPD_parallel:
  case OMPD_simd:
  case OMPD_task:
  case OMPD_taskyield:
  case OMPD_barrier:
  case OMPD_taskwait:
  case OMPD_taskgroup:
  case OMPD_flush:
  case OMPD_for:
  case OMPD_for_simd:
  case OMPD_sections:
  case OMPD_section:
  case OMPD_single:
  case OMPD_master:
  case OMPD_ordered:
  case OMPD_critical:
  case OMPD_parallel_for:
  case OMPD_parallel_for_simd:
  case OMPD_parallel_sections:
  case OMPD_atomic:
  case OMPD_target:
  case OMPD_teams:
  case OMPD_cancellation_point:
  case OMPD_cancel:
  case OMPD_target_data:
  case OMPD_target_enter_data:
  case OMPD_target_exit_data:
  case OMPD_target_parallel:
  case OMPD_target_parallel_for:
  case OMPD_taskloop:
  case OMPD_taskloop_simd:
  case OMPD_distribute:
  case OMPD_end_declare_target:
  case OMPD_target_update:
  case OMPD_distribute_parallel_for:
  case OMPD_distribute_parallel_for_simd:
  case OMPD_distribute_simd:
  case OMPD_target_parallel_for_simd:
  case OMPD_target_simd:
  case OMPD_teams_distribute:
  case OMPD_teams_distribute_simd:
  case OMPD_teams_distribute_parallel_for_simd:
  case OMPD_teams_distribute_parallel_for:
  case OMPD_target_teams:
  case OMPD_target_teams_distribute:
  case OMPD_target_teams_distribute_parallel_for:
  case OMPD_target_teams_distribute_parallel_for_simd:
  case OMPD_target_teams_distribute_simd:
    Diag(Tok, diag::err_omp_unexpected_directive)
        << 1 << getOpenMPDirectiveName(DKind);
    break;
  }
  while (Tok.isNot(tok::annot_pragma_openmp_end))
    ConsumeAnyToken();
  ConsumeAnyToken();
  return nullptr;
}

/// Parsing of declarative or executable OpenMP directives.
///
///       threadprivate-directive:
///         annot_pragma_openmp 'threadprivate' simple-variable-list
///         annot_pragma_openmp_end
///
///       declare-reduction-directive:
///         annot_pragma_openmp 'declare' 'reduction' '(' <reduction_id> ':'
///         <type> {',' <type>} ':' <expression> ')' ['initializer' '('
///         ('omp_priv' '=' <expression>|<function_call>) ')']
///         annot_pragma_openmp_end
///
///       executable-directive:
///         annot_pragma_openmp 'parallel' | 'simd' | 'for' | 'sections' |
///         'section' | 'single' | 'master' | 'critical' [ '(' <name> ')' ] |
///         'parallel for' | 'parallel sections' | 'task' | 'taskyield' |
///         'barrier' | 'taskwait' | 'flush' | 'ordered' | 'atomic' |
///         'for simd' | 'parallel for simd' | 'target' | 'target data' |
///         'taskgroup' | 'teams' | 'taskloop' | 'taskloop simd' |
///         'distribute' | 'target enter data' | 'target exit data' |
///         'target parallel' | 'target parallel for' |
///         'target update' | 'distribute parallel for' |
///         'distribute paralle for simd' | 'distribute simd' |
///         'target parallel for simd' | 'target simd' |
///         'teams distribute' | 'teams distribute simd' |
///         'teams distribute parallel for simd' |
///         'teams distribute parallel for' | 'target teams' |
///         'target teams distribute' |
///         'target teams distribute parallel for' |
///         'target teams distribute parallel for simd' |
///         'target teams distribute simd' {clause}
///         annot_pragma_openmp_end
///
StmtResult Parser::ParseOpenMPDeclarativeOrExecutableDirective(
    AllowedConstructsKind Allowed) {
  assert(Tok.is(tok::annot_pragma_openmp) && "Not an OpenMP directive!");
  ParenBraceBracketBalancer BalancerRAIIObj(*this);
  SmallVector<OMPClause *, 5> Clauses;
  SmallVector<llvm::PointerIntPair<OMPClause *, 1, bool>, OMPC_unknown + 1>
  FirstClauses(OMPC_unknown + 1);
  unsigned ScopeFlags = Scope::FnScope | Scope::DeclScope |
                        Scope::CompoundStmtScope | Scope::OpenMPDirectiveScope;
  SourceLocation Loc = ConsumeAnnotationToken(), EndLoc;
  OpenMPDirectiveKind DKind = parseOpenMPDirectiveKind(*this);
  OpenMPDirectiveKind CancelRegion = OMPD_unknown;
  // Name of critical directive.
  DeclarationNameInfo DirName;
  StmtResult Directive = StmtError();
  bool HasAssociatedStatement = true;
  bool FlushHasClause = false;

  switch (DKind) {
  case OMPD_threadprivate: {
    if (Allowed != ACK_Any) {
      Diag(Tok, diag::err_omp_immediate_directive)
          << getOpenMPDirectiveName(DKind) << 0;
    }
    ConsumeToken();
    ThreadprivateListParserHelper Helper(this);
    if (!ParseOpenMPSimpleVarList(OMPD_threadprivate, Helper, false)) {
      // The last seen token is annot_pragma_openmp_end - need to check for
      // extra tokens.
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
            << getOpenMPDirectiveName(OMPD_threadprivate);
        SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
      }
      DeclGroupPtrTy Res = Actions.ActOnOpenMPThreadprivateDirective(
          Loc, Helper.getIdentifiers());
      Directive = Actions.ActOnDeclStmt(Res, Loc, Tok.getLocation());
    }
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  }
  case OMPD_declare_reduction:
    ConsumeToken();
    if (DeclGroupPtrTy Res =
            ParseOpenMPDeclareReductionDirective(/*AS=*/AS_none)) {
      // The last seen token is annot_pragma_openmp_end - need to check for
      // extra tokens.
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
            << getOpenMPDirectiveName(OMPD_declare_reduction);
        while (Tok.isNot(tok::annot_pragma_openmp_end))
          ConsumeAnyToken();
      }
      ConsumeAnyToken();
      Directive = Actions.ActOnDeclStmt(Res, Loc, Tok.getLocation());
    } else {
      SkipUntil(tok::annot_pragma_openmp_end);
    }
    break;
  case OMPD_flush:
    if (PP.LookAhead(0).is(tok::l_paren)) {
      FlushHasClause = true;
      // Push copy of the current token back to stream to properly parse
      // pseudo-clause OMPFlushClause.
      PP.EnterToken(Tok);
    }
    LLVM_FALLTHROUGH;
  case OMPD_taskyield:
  case OMPD_barrier:
  case OMPD_taskwait:
  case OMPD_cancellation_point:
  case OMPD_cancel:
  case OMPD_target_enter_data:
  case OMPD_target_exit_data:
  case OMPD_target_update:
    if (Allowed == ACK_StatementsOpenMPNonStandalone) {
      Diag(Tok, diag::err_omp_immediate_directive)
          << getOpenMPDirectiveName(DKind) << 0;
    }
    HasAssociatedStatement = false;
    // Fall through for further analysis.
    LLVM_FALLTHROUGH;
  case OMPD_parallel:
  case OMPD_simd:
  case OMPD_for:
  case OMPD_for_simd:
  case OMPD_sections:
  case OMPD_single:
  case OMPD_section:
  case OMPD_master:
  case OMPD_critical:
  case OMPD_parallel_for:
  case OMPD_parallel_for_simd:
  case OMPD_parallel_sections:
  case OMPD_task:
  case OMPD_ordered:
  case OMPD_atomic:
  case OMPD_target:
  case OMPD_teams:
  case OMPD_taskgroup:
  case OMPD_target_data:
  case OMPD_target_parallel:
  case OMPD_target_parallel_for:
  case OMPD_taskloop:
  case OMPD_taskloop_simd:
  case OMPD_distribute:
  case OMPD_distribute_parallel_for:
  case OMPD_distribute_parallel_for_simd:
  case OMPD_distribute_simd:
  case OMPD_target_parallel_for_simd:
  case OMPD_target_simd:
  case OMPD_teams_distribute:
  case OMPD_teams_distribute_simd:
  case OMPD_teams_distribute_parallel_for_simd:
  case OMPD_teams_distribute_parallel_for:
  case OMPD_target_teams:
  case OMPD_target_teams_distribute:
  case OMPD_target_teams_distribute_parallel_for:
  case OMPD_target_teams_distribute_parallel_for_simd:
  case OMPD_target_teams_distribute_simd: {
    ConsumeToken();
    // Parse directive name of the 'critical' directive if any.
    if (DKind == OMPD_critical) {
      BalancedDelimiterTracker T(*this, tok::l_paren,
                                 tok::annot_pragma_openmp_end);
      if (!T.consumeOpen()) {
        if (Tok.isAnyIdentifier()) {
          DirName =
              DeclarationNameInfo(Tok.getIdentifierInfo(), Tok.getLocation());
          ConsumeAnyToken();
        } else {
          Diag(Tok, diag::err_omp_expected_identifier_for_critical);
        }
        T.consumeClose();
      }
    } else if (DKind == OMPD_cancellation_point || DKind == OMPD_cancel) {
      CancelRegion = parseOpenMPDirectiveKind(*this);
      if (Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeToken();
    }

    if (isOpenMPLoopDirective(DKind))
      ScopeFlags |= Scope::OpenMPLoopDirectiveScope;
    if (isOpenMPSimdDirective(DKind))
      ScopeFlags |= Scope::OpenMPSimdDirectiveScope;
    ParseScope OMPDirectiveScope(this, ScopeFlags);
    Actions.StartOpenMPDSABlock(DKind, DirName, Actions.getCurScope(), Loc);

    while (Tok.isNot(tok::annot_pragma_openmp_end)) {
      OpenMPClauseKind CKind =
          Tok.isAnnotation()
              ? OMPC_unknown
              : FlushHasClause ? OMPC_flush
                               : getOpenMPClauseKind(PP.getSpelling(Tok));
      Actions.StartOpenMPClause(CKind);
      FlushHasClause = false;
      OMPClause *Clause =
          ParseOpenMPClause(DKind, CKind, !FirstClauses[CKind].getInt());
      FirstClauses[CKind].setInt(true);
      if (Clause) {
        FirstClauses[CKind].setPointer(Clause);
        Clauses.push_back(Clause);
      }

      // Skip ',' if any.
      if (Tok.is(tok::comma))
        ConsumeToken();
      Actions.EndOpenMPClause();
    }
    // End location of the directive.
    EndLoc = Tok.getLocation();
    // Consume final annot_pragma_openmp_end.
    ConsumeAnnotationToken();

    // OpenMP [2.13.8, ordered Construct, Syntax]
    // If the depend clause is specified, the ordered construct is a stand-alone
    // directive.
    if (DKind == OMPD_ordered && FirstClauses[OMPC_depend].getInt()) {
      if (Allowed == ACK_StatementsOpenMPNonStandalone) {
        Diag(Loc, diag::err_omp_immediate_directive)
            << getOpenMPDirectiveName(DKind) << 1
            << getOpenMPClauseName(OMPC_depend);
      }
      HasAssociatedStatement = false;
    }

    StmtResult AssociatedStmt;
    if (HasAssociatedStatement) {
      // The body is a block scope like in Lambdas and Blocks.
      Actions.ActOnOpenMPRegionStart(DKind, getCurScope());
      // FIXME: We create a bogus CompoundStmt scope to hold the contents of
      // the captured region. Code elsewhere assumes that any FunctionScopeInfo
      // should have at least one compound statement scope within it.
      AssociatedStmt = (Sema::CompoundScopeRAII(Actions), ParseStatement());
      AssociatedStmt = Actions.ActOnOpenMPRegionEnd(AssociatedStmt, Clauses);
    } else if (DKind == OMPD_target_update || DKind == OMPD_target_enter_data ||
               DKind == OMPD_target_exit_data) {
      Actions.ActOnOpenMPRegionStart(DKind, getCurScope());
      AssociatedStmt = (Sema::CompoundScopeRAII(Actions),
                        Actions.ActOnCompoundStmt(Loc, Loc, llvm::None,
                                                  /*isStmtExpr=*/false));
      AssociatedStmt = Actions.ActOnOpenMPRegionEnd(AssociatedStmt, Clauses);
    }
    Directive = Actions.ActOnOpenMPExecutableDirective(
        DKind, DirName, CancelRegion, Clauses, AssociatedStmt.get(), Loc,
        EndLoc);

    // Exit scope.
    Actions.EndOpenMPDSABlock(Directive.get());
    OMPDirectiveScope.Exit();
    break;
  }
  case OMPD_declare_simd:
  case OMPD_declare_target:
  case OMPD_end_declare_target:
  case OMPD_requires:
    Diag(Tok, diag::err_omp_unexpected_directive)
        << 1 << getOpenMPDirectiveName(DKind);
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  case OMPD_unknown:
    Diag(Tok, diag::err_omp_unknown_directive);
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  }
  return Directive;
}

// Parses simple list:
//   simple-variable-list:
//         '(' id-expression {, id-expression} ')'
//
bool Parser::ParseOpenMPSimpleVarList(
    OpenMPDirectiveKind Kind,
    const llvm::function_ref<void(CXXScopeSpec &, DeclarationNameInfo)> &
        Callback,
    bool AllowScopeSpecifier) {
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPDirectiveName(Kind)))
    return true;
  bool IsCorrect = true;
  bool NoIdentIsFound = true;

  // Read tokens while ')' or annot_pragma_openmp_end is not found.
  while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::annot_pragma_openmp_end)) {
    CXXScopeSpec SS;
    UnqualifiedId Name;
    // Read var name.
    Token PrevTok = Tok;
    NoIdentIsFound = false;

    if (AllowScopeSpecifier && getLangOpts().CPlusPlus &&
        ParseOptionalCXXScopeSpecifier(SS, nullptr, false)) {
      IsCorrect = false;
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    } else if (ParseUnqualifiedId(SS, false, false, false, false, nullptr,
                                  nullptr, Name)) {
      IsCorrect = false;
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    } else if (Tok.isNot(tok::comma) && Tok.isNot(tok::r_paren) &&
               Tok.isNot(tok::annot_pragma_openmp_end)) {
      IsCorrect = false;
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
      Diag(PrevTok.getLocation(), diag::err_expected)
          << tok::identifier
          << SourceRange(PrevTok.getLocation(), PrevTokLocation);
    } else {
      Callback(SS, Actions.GetNameFromUnqualifiedId(Name));
    }
    // Consume ','.
    if (Tok.is(tok::comma)) {
      ConsumeToken();
    }
  }

  if (NoIdentIsFound) {
    Diag(Tok, diag::err_expected) << tok::identifier;
    IsCorrect = false;
  }

  // Parse ')'.
  IsCorrect = !T.consumeClose() && IsCorrect;

  return !IsCorrect;
}

/// Parsing of OpenMP clauses.
///
///    clause:
///       if-clause | final-clause | num_threads-clause | safelen-clause |
///       default-clause | private-clause | firstprivate-clause | shared-clause
///       | linear-clause | aligned-clause | collapse-clause |
///       lastprivate-clause | reduction-clause | proc_bind-clause |
///       schedule-clause | copyin-clause | copyprivate-clause | untied-clause |
///       mergeable-clause | flush-clause | read-clause | write-clause |
///       update-clause | capture-clause | seq_cst-clause | device-clause |
///       simdlen-clause | threads-clause | simd-clause | num_teams-clause |
///       thread_limit-clause | priority-clause | grainsize-clause |
///       nogroup-clause | num_tasks-clause | hint-clause | to-clause |
///       from-clause | is_device_ptr-clause | task_reduction-clause |
///       in_reduction-clause
///
OMPClause *Parser::ParseOpenMPClause(OpenMPDirectiveKind DKind,
                                     OpenMPClauseKind CKind, bool FirstClause) {
  OMPClause *Clause = nullptr;
  bool ErrorFound = false;
  bool WrongDirective = false;
  // Check if clause is allowed for the given directive.
  if (CKind != OMPC_unknown && !isAllowedClauseForDirective(DKind, CKind)) {
    Diag(Tok, diag::err_omp_unexpected_clause) << getOpenMPClauseName(CKind)
                                               << getOpenMPDirectiveName(DKind);
    ErrorFound = true;
    WrongDirective = true;
  }

  switch (CKind) {
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_collapse:
  case OMPC_ordered:
  case OMPC_device:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_grainsize:
  case OMPC_num_tasks:
  case OMPC_hint:
    // OpenMP [2.5, Restrictions]
    //  At most one num_threads clause can appear on the directive.
    // OpenMP [2.8.1, simd construct, Restrictions]
    //  Only one safelen  clause can appear on a simd directive.
    //  Only one simdlen  clause can appear on a simd directive.
    //  Only one collapse clause can appear on a simd directive.
    // OpenMP [2.9.1, target data construct, Restrictions]
    //  At most one device clause can appear on the directive.
    // OpenMP [2.11.1, task Construct, Restrictions]
    //  At most one if clause can appear on the directive.
    //  At most one final clause can appear on the directive.
    // OpenMP [teams Construct, Restrictions]
    //  At most one num_teams clause can appear on the directive.
    //  At most one thread_limit clause can appear on the directive.
    // OpenMP [2.9.1, task Construct, Restrictions]
    // At most one priority clause can appear on the directive.
    // OpenMP [2.9.2, taskloop Construct, Restrictions]
    // At most one grainsize clause can appear on the directive.
    // OpenMP [2.9.2, taskloop Construct, Restrictions]
    // At most one num_tasks clause can appear on the directive.
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    if (CKind == OMPC_ordered && PP.LookAhead(/*N=*/0).isNot(tok::l_paren))
      Clause = ParseOpenMPClause(CKind, WrongDirective);
    else
      Clause = ParseOpenMPSingleExprClause(CKind, WrongDirective);
    break;
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_atomic_default_mem_order:
    // OpenMP [2.14.3.1, Restrictions]
    //  Only a single default clause may be specified on a parallel, task or
    //  teams directive.
    // OpenMP [2.5, parallel Construct, Restrictions]
    //  At most one proc_bind clause can appear on the directive.
    // OpenMP [5.0, Requires directive, Restrictions]
    //  At most one atomic_default_mem_order clause can appear
    //  on the directive
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    Clause = ParseOpenMPSimpleClause(CKind, WrongDirective);
    break;
  case OMPC_schedule:
  case OMPC_dist_schedule:
  case OMPC_defaultmap:
    // OpenMP [2.7.1, Restrictions, p. 3]
    //  Only one schedule clause can appear on a loop directive.
    // OpenMP [2.10.4, Restrictions, p. 106]
    //  At most one defaultmap clause can appear on the directive.
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }
    LLVM_FALLTHROUGH;

  case OMPC_if:
    Clause = ParseOpenMPSingleExprWithArgClause(CKind, WrongDirective);
    break;
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_read:
  case OMPC_write:
  case OMPC_update:
  case OMPC_capture:
  case OMPC_seq_cst:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_nogroup:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
    // OpenMP [2.7.1, Restrictions, p. 9]
    //  Only one ordered clause can appear on a loop directive.
    // OpenMP [2.7.1, Restrictions, C/C++, p. 4]
    //  Only one nowait clause can appear on a for directive.
    // OpenMP [5.0, Requires directive, Restrictions]
    //   Each of the requires clauses can appear at most once on the directive.
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    Clause = ParseOpenMPClause(CKind, WrongDirective);
    break;
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_lastprivate:
  case OMPC_shared:
  case OMPC_reduction:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_linear:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_flush:
  case OMPC_depend:
  case OMPC_map:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_is_device_ptr:
    Clause = ParseOpenMPVarListClause(DKind, CKind, WrongDirective);
    break;
  case OMPC_unknown:
    Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
        << getOpenMPDirectiveName(DKind);
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
    break;
  case OMPC_threadprivate:
  case OMPC_uniform:
    if (!WrongDirective)
      Diag(Tok, diag::err_omp_unexpected_clause)
          << getOpenMPClauseName(CKind) << getOpenMPDirectiveName(DKind);
    SkipUntil(tok::comma, tok::annot_pragma_openmp_end, StopBeforeMatch);
    break;
  }
  return ErrorFound ? nullptr : Clause;
}

/// Parses simple expression in parens for single-expression clauses of OpenMP
/// constructs.
/// \param RLoc Returned location of right paren.
ExprResult Parser::ParseOpenMPParensExpr(StringRef ClauseName,
                                         SourceLocation &RLoc) {
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after, ClauseName.data()))
    return ExprError();

  SourceLocation ELoc = Tok.getLocation();
  ExprResult LHS(ParseCastExpression(
      /*isUnaryExpression=*/false, /*isAddressOfOperand=*/false, NotTypeCast));
  ExprResult Val(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  Val = Actions.ActOnFinishFullExpr(Val.get(), ELoc);

  // Parse ')'.
  RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  return Val;
}

/// Parsing of OpenMP clauses with single expressions like 'final',
/// 'collapse', 'safelen', 'num_threads', 'simdlen', 'num_teams',
/// 'thread_limit', 'simdlen', 'priority', 'grainsize', 'num_tasks' or 'hint'.
///
///    final-clause:
///      'final' '(' expression ')'
///
///    num_threads-clause:
///      'num_threads' '(' expression ')'
///
///    safelen-clause:
///      'safelen' '(' expression ')'
///
///    simdlen-clause:
///      'simdlen' '(' expression ')'
///
///    collapse-clause:
///      'collapse' '(' expression ')'
///
///    priority-clause:
///      'priority' '(' expression ')'
///
///    grainsize-clause:
///      'grainsize' '(' expression ')'
///
///    num_tasks-clause:
///      'num_tasks' '(' expression ')'
///
///    hint-clause:
///      'hint' '(' expression ')'
///
OMPClause *Parser::ParseOpenMPSingleExprClause(OpenMPClauseKind Kind,
                                               bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  SourceLocation LLoc = Tok.getLocation();
  SourceLocation RLoc;

  ExprResult Val = ParseOpenMPParensExpr(getOpenMPClauseName(Kind), RLoc);

  if (Val.isInvalid())
    return nullptr;

  if (ParseOnly)
    return nullptr;
  return Actions.ActOnOpenMPSingleExprClause(Kind, Val.get(), Loc, LLoc, RLoc);
}

/// Parsing of simple OpenMP clauses like 'default' or 'proc_bind'.
///
///    default-clause:
///         'default' '(' 'none' | 'shared' ')
///
///    proc_bind-clause:
///         'proc_bind' '(' 'master' | 'close' | 'spread' ')
///
OMPClause *Parser::ParseOpenMPSimpleClause(OpenMPClauseKind Kind,
                                           bool ParseOnly) {
  SourceLocation Loc = Tok.getLocation();
  SourceLocation LOpen = ConsumeToken();
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind)))
    return nullptr;

  unsigned Type = getOpenMPSimpleClauseType(
      Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok));
  SourceLocation TypeLoc = Tok.getLocation();
  if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
      Tok.isNot(tok::annot_pragma_openmp_end))
    ConsumeAnyToken();

  // Parse ')'.
  SourceLocation RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  if (ParseOnly)
    return nullptr;
  return Actions.ActOnOpenMPSimpleClause(Kind, Type, TypeLoc, LOpen, Loc, RLoc);
}

/// Parsing of OpenMP clauses like 'ordered'.
///
///    ordered-clause:
///         'ordered'
///
///    nowait-clause:
///         'nowait'
///
///    untied-clause:
///         'untied'
///
///    mergeable-clause:
///         'mergeable'
///
///    read-clause:
///         'read'
///
///    threads-clause:
///         'threads'
///
///    simd-clause:
///         'simd'
///
///    nogroup-clause:
///         'nogroup'
///
OMPClause *Parser::ParseOpenMPClause(OpenMPClauseKind Kind, bool ParseOnly) {
  SourceLocation Loc = Tok.getLocation();
  ConsumeAnyToken();

  if (ParseOnly)
    return nullptr;
  return Actions.ActOnOpenMPClause(Kind, Loc, Tok.getLocation());
}


/// Parsing of OpenMP clauses with single expressions and some additional
/// argument like 'schedule' or 'dist_schedule'.
///
///    schedule-clause:
///      'schedule' '(' [ modifier [ ',' modifier ] ':' ] kind [',' expression ]
///      ')'
///
///    if-clause:
///      'if' '(' [ directive-name-modifier ':' ] expression ')'
///
///    defaultmap:
///      'defaultmap' '(' modifier ':' kind ')'
///
OMPClause *Parser::ParseOpenMPSingleExprWithArgClause(OpenMPClauseKind Kind,
                                                      bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  SourceLocation DelimLoc;
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind)))
    return nullptr;

  ExprResult Val;
  SmallVector<unsigned, 4> Arg;
  SmallVector<SourceLocation, 4> KLoc;
  if (Kind == OMPC_schedule) {
    enum { Modifier1, Modifier2, ScheduleKind, NumberOfElements };
    Arg.resize(NumberOfElements);
    KLoc.resize(NumberOfElements);
    Arg[Modifier1] = OMPC_SCHEDULE_MODIFIER_unknown;
    Arg[Modifier2] = OMPC_SCHEDULE_MODIFIER_unknown;
    Arg[ScheduleKind] = OMPC_SCHEDULE_unknown;
    unsigned KindModifier = getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok));
    if (KindModifier > OMPC_SCHEDULE_unknown) {
      // Parse 'modifier'
      Arg[Modifier1] = KindModifier;
      KLoc[Modifier1] = Tok.getLocation();
      if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
          Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeAnyToken();
      if (Tok.is(tok::comma)) {
        // Parse ',' 'modifier'
        ConsumeAnyToken();
        KindModifier = getOpenMPSimpleClauseType(
            Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok));
        Arg[Modifier2] = KindModifier > OMPC_SCHEDULE_unknown
                             ? KindModifier
                             : (unsigned)OMPC_SCHEDULE_unknown;
        KLoc[Modifier2] = Tok.getLocation();
        if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
            Tok.isNot(tok::annot_pragma_openmp_end))
          ConsumeAnyToken();
      }
      // Parse ':'
      if (Tok.is(tok::colon))
        ConsumeAnyToken();
      else
        Diag(Tok, diag::warn_pragma_expected_colon) << "schedule modifier";
      KindModifier = getOpenMPSimpleClauseType(
          Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok));
    }
    Arg[ScheduleKind] = KindModifier;
    KLoc[ScheduleKind] = Tok.getLocation();
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
    if ((Arg[ScheduleKind] == OMPC_SCHEDULE_static ||
         Arg[ScheduleKind] == OMPC_SCHEDULE_dynamic ||
         Arg[ScheduleKind] == OMPC_SCHEDULE_guided) &&
        Tok.is(tok::comma))
      DelimLoc = ConsumeAnyToken();
  } else if (Kind == OMPC_dist_schedule) {
    Arg.push_back(getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok)));
    KLoc.push_back(Tok.getLocation());
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
    if (Arg.back() == OMPC_DIST_SCHEDULE_static && Tok.is(tok::comma))
      DelimLoc = ConsumeAnyToken();
  } else if (Kind == OMPC_defaultmap) {
    // Get a defaultmap modifier
    Arg.push_back(getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok)));
    KLoc.push_back(Tok.getLocation());
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
    // Parse ':'
    if (Tok.is(tok::colon))
      ConsumeAnyToken();
    else if (Arg.back() != OMPC_DEFAULTMAP_MODIFIER_unknown)
      Diag(Tok, diag::warn_pragma_expected_colon) << "defaultmap modifier";
    // Get a defaultmap kind
    Arg.push_back(getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok)));
    KLoc.push_back(Tok.getLocation());
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
  } else {
    assert(Kind == OMPC_if);
    KLoc.push_back(Tok.getLocation());
    TentativeParsingAction TPA(*this);
    Arg.push_back(parseOpenMPDirectiveKind(*this));
    if (Arg.back() != OMPD_unknown) {
      ConsumeToken();
      if (Tok.is(tok::colon) && getLangOpts().OpenMP > 40) {
        TPA.Commit();
        DelimLoc = ConsumeToken();
      } else {
        TPA.Revert();
        Arg.back() = OMPD_unknown;
      }
    } else {
      TPA.Revert();
    }
  }

  bool NeedAnExpression = (Kind == OMPC_schedule && DelimLoc.isValid()) ||
                          (Kind == OMPC_dist_schedule && DelimLoc.isValid()) ||
                          Kind == OMPC_if;
  if (NeedAnExpression) {
    SourceLocation ELoc = Tok.getLocation();
    ExprResult LHS(ParseCastExpression(false, false, NotTypeCast));
    Val = ParseRHSOfBinaryExpression(LHS, prec::Conditional);
    Val = Actions.ActOnFinishFullExpr(Val.get(), ELoc);
  }

  // Parse ')'.
  SourceLocation RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  if (NeedAnExpression && Val.isInvalid())
    return nullptr;

  if (ParseOnly)
    return nullptr;
  return Actions.ActOnOpenMPSingleExprWithArgClause(
      Kind, Arg, Val.get(), Loc, T.getOpenLocation(), KLoc, DelimLoc, RLoc);
}

static bool ParseReductionId(Parser &P, CXXScopeSpec &ReductionIdScopeSpec,
                             UnqualifiedId &ReductionId) {
  if (ReductionIdScopeSpec.isEmpty()) {
    auto OOK = OO_None;
    switch (P.getCurToken().getKind()) {
    case tok::plus:
      OOK = OO_Plus;
      break;
    case tok::minus:
      OOK = OO_Minus;
      break;
    case tok::star:
      OOK = OO_Star;
      break;
    case tok::amp:
      OOK = OO_Amp;
      break;
    case tok::pipe:
      OOK = OO_Pipe;
      break;
    case tok::caret:
      OOK = OO_Caret;
      break;
    case tok::ampamp:
      OOK = OO_AmpAmp;
      break;
    case tok::pipepipe:
      OOK = OO_PipePipe;
      break;
    default:
      break;
    }
    if (OOK != OO_None) {
      SourceLocation OpLoc = P.ConsumeToken();
      SourceLocation SymbolLocations[] = {OpLoc, OpLoc, SourceLocation()};
      ReductionId.setOperatorFunctionId(OpLoc, OOK, SymbolLocations);
      return false;
    }
  }
  return P.ParseUnqualifiedId(ReductionIdScopeSpec, /*EnteringContext*/ false,
                              /*AllowDestructorName*/ false,
                              /*AllowConstructorName*/ false,
                              /*AllowDeductionGuide*/ false,
                              nullptr, nullptr, ReductionId);
}

/// Checks if the token is a valid map-type-modifier.
static OpenMPMapModifierKind isMapModifier(Parser &P) {
  Token Tok = P.getCurToken();
  if (!Tok.is(tok::identifier))
    return OMPC_MAP_MODIFIER_unknown;

  Preprocessor &PP = P.getPreprocessor();
  OpenMPMapModifierKind TypeModifier = static_cast<OpenMPMapModifierKind>(
      getOpenMPSimpleClauseType(OMPC_map, PP.getSpelling(Tok)));
  return TypeModifier;
}

/// Parse map-type-modifiers in map clause.
/// map([ [map-type-modifier[,] [map-type-modifier[,] ...] map-type : ] list)
/// where, map-type-modifier ::= always | close 
static void parseMapTypeModifiers(Parser &P,
                                  Parser::OpenMPVarListDataTy &Data) {
  Preprocessor &PP = P.getPreprocessor();
  while (P.getCurToken().isNot(tok::colon)) {
    Token Tok = P.getCurToken();
    OpenMPMapModifierKind TypeModifier = isMapModifier(P);
    if (TypeModifier == OMPC_MAP_MODIFIER_always ||
        TypeModifier == OMPC_MAP_MODIFIER_close) {
      Data.MapTypeModifiers.push_back(TypeModifier);
      Data.MapTypeModifiersLoc.push_back(Tok.getLocation());
      P.ConsumeToken();
    } else {
      // For the case of unknown map-type-modifier or a map-type.
      // Map-type is followed by a colon; the function returns when it
      // encounters a token followed by a colon.
      if (Tok.is(tok::comma)) {
        P.Diag(Tok, diag::err_omp_map_type_modifier_missing);
        P.ConsumeToken();
        continue;
      }
      // Potential map-type token as it is followed by a colon.
      if (PP.LookAhead(0).is(tok::colon))
        return;
      P.Diag(Tok, diag::err_omp_unknown_map_type_modifier);
      P.ConsumeToken();
    }
    if (P.getCurToken().is(tok::comma))
      P.ConsumeToken();
  }
}

/// Checks if the token is a valid map-type.
static OpenMPMapClauseKind isMapType(Parser &P) {
  Token Tok = P.getCurToken();
  // The map-type token can be either an identifier or the C++ delete keyword.
  if (!Tok.isOneOf(tok::identifier, tok::kw_delete))
    return OMPC_MAP_unknown;
  Preprocessor &PP = P.getPreprocessor();
  OpenMPMapClauseKind MapType = static_cast<OpenMPMapClauseKind>(
      getOpenMPSimpleClauseType(OMPC_map, PP.getSpelling(Tok)));
  return MapType;
}

/// Parse map-type in map clause.
/// map([ [map-type-modifier[,] [map-type-modifier[,] ...] map-type : ] list)
/// where, map-type ::= to | from | tofrom | alloc | release | delete 
static void parseMapType(Parser &P, Parser::OpenMPVarListDataTy &Data) {
  Token Tok = P.getCurToken();
  if (Tok.is(tok::colon)) {
    P.Diag(Tok, diag::err_omp_map_type_missing);
    return;
  }
  Data.MapType = isMapType(P);
  if (Data.MapType == OMPC_MAP_unknown)
    P.Diag(Tok, diag::err_omp_unknown_map_type);
  P.ConsumeToken();
}

/// Parses clauses with list.
bool Parser::ParseOpenMPVarList(OpenMPDirectiveKind DKind,
                                OpenMPClauseKind Kind,
                                SmallVectorImpl<Expr *> &Vars,
                                OpenMPVarListDataTy &Data) {
  UnqualifiedId UnqualifiedReductionId;
  bool InvalidReductionId = false;

  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind)))
    return true;

  bool NeedRParenForLinear = false;
  BalancedDelimiterTracker LinearT(*this, tok::l_paren,
                                  tok::annot_pragma_openmp_end);
  // Handle reduction-identifier for reduction clause.
  if (Kind == OMPC_reduction || Kind == OMPC_task_reduction ||
      Kind == OMPC_in_reduction) {
    ColonProtectionRAIIObject ColonRAII(*this);
    if (getLangOpts().CPlusPlus)
      ParseOptionalCXXScopeSpecifier(Data.ReductionIdScopeSpec,
                                     /*ObjectType=*/nullptr,
                                     /*EnteringContext=*/false);
    InvalidReductionId = ParseReductionId(*this, Data.ReductionIdScopeSpec,
                                          UnqualifiedReductionId);
    if (InvalidReductionId) {
      SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }
    if (Tok.is(tok::colon))
      Data.ColonLoc = ConsumeToken();
    else
      Diag(Tok, diag::warn_pragma_expected_colon) << "reduction identifier";
    if (!InvalidReductionId)
      Data.ReductionId =
          Actions.GetNameFromUnqualifiedId(UnqualifiedReductionId);
  } else if (Kind == OMPC_depend) {
  // Handle dependency type for depend clause.
    ColonProtectionRAIIObject ColonRAII(*this);
    Data.DepKind =
        static_cast<OpenMPDependClauseKind>(getOpenMPSimpleClauseType(
            Kind, Tok.is(tok::identifier) ? PP.getSpelling(Tok) : ""));
    Data.DepLinMapLoc = Tok.getLocation();

    if (Data.DepKind == OMPC_DEPEND_unknown) {
      SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    } else {
      ConsumeToken();
      // Special processing for depend(source) clause.
      if (DKind == OMPD_ordered && Data.DepKind == OMPC_DEPEND_source) {
        // Parse ')'.
        T.consumeClose();
        return false;
      }
    }
    if (Tok.is(tok::colon)) {
      Data.ColonLoc = ConsumeToken();
    } else {
      Diag(Tok, DKind == OMPD_ordered ? diag::warn_pragma_expected_colon_r_paren
                                      : diag::warn_pragma_expected_colon)
          << "dependency type";
    }
  } else if (Kind == OMPC_linear) {
    // Try to parse modifier if any.
    if (Tok.is(tok::identifier) && PP.LookAhead(0).is(tok::l_paren)) {
      Data.LinKind = static_cast<OpenMPLinearClauseKind>(
          getOpenMPSimpleClauseType(Kind, PP.getSpelling(Tok)));
      Data.DepLinMapLoc = ConsumeToken();
      LinearT.consumeOpen();
      NeedRParenForLinear = true;
    }
  } else if (Kind == OMPC_map) {
    // Handle map type for map clause.
    ColonProtectionRAIIObject ColonRAII(*this);

    // The first identifier may be a list item, a map-type or a
    // map-type-modifier. The map-type can also be delete which has the same
    // spelling of the C++ delete keyword.
    Data.DepLinMapLoc = Tok.getLocation();

    // Check for presence of a colon in the map clause.
    TentativeParsingAction TPA(*this);
    bool ColonPresent = false;
    if (SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
        StopBeforeMatch)) {
      if (Tok.is(tok::colon))
        ColonPresent = true;
    }
    TPA.Revert();
    // Only parse map-type-modifier[s] and map-type if a colon is present in
    // the map clause.
    if (ColonPresent) {
      parseMapTypeModifiers(*this, Data);
      parseMapType(*this, Data);
    }
    if (Data.MapType == OMPC_MAP_unknown) {
      Data.MapType = OMPC_MAP_tofrom;
      Data.IsMapTypeImplicit = true;
    }

    if (Tok.is(tok::colon))
      Data.ColonLoc = ConsumeToken();
  }

  bool IsComma =
      (Kind != OMPC_reduction && Kind != OMPC_task_reduction &&
       Kind != OMPC_in_reduction && Kind != OMPC_depend && Kind != OMPC_map) ||
      (Kind == OMPC_reduction && !InvalidReductionId) ||
      (Kind == OMPC_map && Data.MapType != OMPC_MAP_unknown) ||
      (Kind == OMPC_depend && Data.DepKind != OMPC_DEPEND_unknown);
  const bool MayHaveTail = (Kind == OMPC_linear || Kind == OMPC_aligned);
  while (IsComma || (Tok.isNot(tok::r_paren) && Tok.isNot(tok::colon) &&
                     Tok.isNot(tok::annot_pragma_openmp_end))) {
    ColonProtectionRAIIObject ColonRAII(*this, MayHaveTail);
    // Parse variable
    ExprResult VarExpr =
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    if (VarExpr.isUsable()) {
      Vars.push_back(VarExpr.get());
    } else {
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }
    // Skip ',' if any
    IsComma = Tok.is(tok::comma);
    if (IsComma)
      ConsumeToken();
    else if (Tok.isNot(tok::r_paren) &&
             Tok.isNot(tok::annot_pragma_openmp_end) &&
             (!MayHaveTail || Tok.isNot(tok::colon)))
      Diag(Tok, diag::err_omp_expected_punc)
          << ((Kind == OMPC_flush) ? getOpenMPDirectiveName(OMPD_flush)
                                   : getOpenMPClauseName(Kind))
          << (Kind == OMPC_flush);
  }

  // Parse ')' for linear clause with modifier.
  if (NeedRParenForLinear)
    LinearT.consumeClose();

  // Parse ':' linear-step (or ':' alignment).
  const bool MustHaveTail = MayHaveTail && Tok.is(tok::colon);
  if (MustHaveTail) {
    Data.ColonLoc = Tok.getLocation();
    SourceLocation ELoc = ConsumeToken();
    ExprResult Tail = ParseAssignmentExpression();
    Tail = Actions.ActOnFinishFullExpr(Tail.get(), ELoc);
    if (Tail.isUsable())
      Data.TailExpr = Tail.get();
    else
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
  }

  // Parse ')'.
  Data.RLoc = Tok.getLocation();
  if (!T.consumeClose())
    Data.RLoc = T.getCloseLocation();
  return (Kind == OMPC_depend && Data.DepKind != OMPC_DEPEND_unknown &&
          Vars.empty()) ||
         (Kind != OMPC_depend && Kind != OMPC_map && Vars.empty()) ||
         (MustHaveTail && !Data.TailExpr) || InvalidReductionId;
}

/// Parsing of OpenMP clause 'private', 'firstprivate', 'lastprivate',
/// 'shared', 'copyin', 'copyprivate', 'flush', 'reduction', 'task_reduction' or
/// 'in_reduction'.
///
///    private-clause:
///       'private' '(' list ')'
///    firstprivate-clause:
///       'firstprivate' '(' list ')'
///    lastprivate-clause:
///       'lastprivate' '(' list ')'
///    shared-clause:
///       'shared' '(' list ')'
///    linear-clause:
///       'linear' '(' linear-list [ ':' linear-step ] ')'
///    aligned-clause:
///       'aligned' '(' list [ ':' alignment ] ')'
///    reduction-clause:
///       'reduction' '(' reduction-identifier ':' list ')'
///    task_reduction-clause:
///       'task_reduction' '(' reduction-identifier ':' list ')'
///    in_reduction-clause:
///       'in_reduction' '(' reduction-identifier ':' list ')'
///    copyprivate-clause:
///       'copyprivate' '(' list ')'
///    flush-clause:
///       'flush' '(' list ')'
///    depend-clause:
///       'depend' '(' in | out | inout : list | source ')'
///    map-clause:
///       'map' '(' [ [ always [,] ] [ close [,] ]
///          to | from | tofrom | alloc | release | delete ':' ] list ')';
///    to-clause:
///       'to' '(' list ')'
///    from-clause:
///       'from' '(' list ')'
///    use_device_ptr-clause:
///       'use_device_ptr' '(' list ')'
///    is_device_ptr-clause:
///       'is_device_ptr' '(' list ')'
///
/// For 'linear' clause linear-list may have the following forms:
///  list
///  modifier(list)
/// where modifier is 'val' (C) or 'ref', 'val' or 'uval'(C++).
OMPClause *Parser::ParseOpenMPVarListClause(OpenMPDirectiveKind DKind,
                                            OpenMPClauseKind Kind,
                                            bool ParseOnly) {
  SourceLocation Loc = Tok.getLocation();
  SourceLocation LOpen = ConsumeToken();
  SmallVector<Expr *, 4> Vars;
  OpenMPVarListDataTy Data;

  if (ParseOpenMPVarList(DKind, Kind, Vars, Data))
    return nullptr;

  if (ParseOnly)
    return nullptr;
  return Actions.ActOnOpenMPVarListClause(
      Kind, Vars, Data.TailExpr, Loc, LOpen, Data.ColonLoc, Data.RLoc,
      Data.ReductionIdScopeSpec, Data.ReductionId, Data.DepKind, Data.LinKind,
      Data.MapTypeModifiers, Data.MapTypeModifiersLoc, Data.MapType,
      Data.IsMapTypeImplicit, Data.DepLinMapLoc);
}

