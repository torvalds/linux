//===--- Sema.cpp - AST Builder and Semantic Analysis Implementation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the actions class which performs semantic analysis and
// builds an AST out of a parse stream.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/CXXFieldCollector.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaConsumer.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/TemplateDeduction.h"
#include "clang/Sema/TemplateInstCallback.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
using namespace clang;
using namespace sema;

SourceLocation Sema::getLocForEndOfToken(SourceLocation Loc, unsigned Offset) {
  return Lexer::getLocForEndOfToken(Loc, Offset, SourceMgr, LangOpts);
}

ModuleLoader &Sema::getModuleLoader() const { return PP.getModuleLoader(); }

PrintingPolicy Sema::getPrintingPolicy(const ASTContext &Context,
                                       const Preprocessor &PP) {
  PrintingPolicy Policy = Context.getPrintingPolicy();
  // In diagnostics, we print _Bool as bool if the latter is defined as the
  // former.
  Policy.Bool = Context.getLangOpts().Bool;
  if (!Policy.Bool) {
    if (const MacroInfo *BoolMacro = PP.getMacroInfo(Context.getBoolName())) {
      Policy.Bool = BoolMacro->isObjectLike() &&
                    BoolMacro->getNumTokens() == 1 &&
                    BoolMacro->getReplacementToken(0).is(tok::kw__Bool);
    }
  }

  return Policy;
}

void Sema::ActOnTranslationUnitScope(Scope *S) {
  TUScope = S;
  PushDeclContext(S, Context.getTranslationUnitDecl());
}

namespace clang {
namespace sema {

class SemaPPCallbacks : public PPCallbacks {
  Sema *S = nullptr;
  llvm::SmallVector<SourceLocation, 8> IncludeStack;

public:
  void set(Sema &S) { this->S = &S; }

  void reset() { S = nullptr; }

  virtual void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                           SrcMgr::CharacteristicKind FileType,
                           FileID PrevFID) override {
    if (!S)
      return;
    switch (Reason) {
    case EnterFile: {
      SourceManager &SM = S->getSourceManager();
      SourceLocation IncludeLoc = SM.getIncludeLoc(SM.getFileID(Loc));
      if (IncludeLoc.isValid()) {
        IncludeStack.push_back(IncludeLoc);
        S->DiagnoseNonDefaultPragmaPack(
            Sema::PragmaPackDiagnoseKind::NonDefaultStateAtInclude, IncludeLoc);
      }
      break;
    }
    case ExitFile:
      if (!IncludeStack.empty())
        S->DiagnoseNonDefaultPragmaPack(
            Sema::PragmaPackDiagnoseKind::ChangedStateAtExit,
            IncludeStack.pop_back_val());
      break;
    default:
      break;
    }
  }
};

} // end namespace sema
} // end namespace clang

Sema::Sema(Preprocessor &pp, ASTContext &ctxt, ASTConsumer &consumer,
           TranslationUnitKind TUKind, CodeCompleteConsumer *CodeCompleter)
    : ExternalSource(nullptr), isMultiplexExternalSource(false),
      FPFeatures(pp.getLangOpts()), LangOpts(pp.getLangOpts()), PP(pp),
      Context(ctxt), Consumer(consumer), Diags(PP.getDiagnostics()),
      SourceMgr(PP.getSourceManager()), CollectStats(false),
      CodeCompleter(CodeCompleter), CurContext(nullptr),
      OriginalLexicalContext(nullptr), MSStructPragmaOn(false),
      MSPointerToMemberRepresentationMethod(
          LangOpts.getMSPointerToMemberRepresentationMethod()),
      VtorDispStack(MSVtorDispAttr::Mode(LangOpts.VtorDispMode)), PackStack(0),
      DataSegStack(nullptr), BSSSegStack(nullptr), ConstSegStack(nullptr),
      CodeSegStack(nullptr), CurInitSeg(nullptr), VisContext(nullptr),
      PragmaAttributeCurrentTargetDecl(nullptr),
      IsBuildingRecoveryCallExpr(false), Cleanup{}, LateTemplateParser(nullptr),
      LateTemplateParserCleanup(nullptr), OpaqueParser(nullptr), IdResolver(pp),
      StdExperimentalNamespaceCache(nullptr), StdInitializerList(nullptr),
      StdCoroutineTraitsCache(nullptr), CXXTypeInfoDecl(nullptr),
      MSVCGuidDecl(nullptr), NSNumberDecl(nullptr), NSValueDecl(nullptr),
      NSStringDecl(nullptr), StringWithUTF8StringMethod(nullptr),
      ValueWithBytesObjCTypeMethod(nullptr), NSArrayDecl(nullptr),
      ArrayWithObjectsMethod(nullptr), NSDictionaryDecl(nullptr),
      DictionaryWithObjectsMethod(nullptr), GlobalNewDeleteDeclared(false),
      TUKind(TUKind), NumSFINAEErrors(0),
      FullyCheckedComparisonCategories(
          static_cast<unsigned>(ComparisonCategoryType::Last) + 1),
      AccessCheckingSFINAE(false), InNonInstantiationSFINAEContext(false),
      NonInstantiationEntries(0), ArgumentPackSubstitutionIndex(-1),
      CurrentInstantiationScope(nullptr), DisableTypoCorrection(false),
      TyposCorrected(0), AnalysisWarnings(*this),
      ThreadSafetyDeclCache(nullptr), VarDataSharingAttributesStack(nullptr),
      CurScope(nullptr), Ident_super(nullptr), Ident___float128(nullptr) {
  TUScope = nullptr;

  LoadedExternalKnownNamespaces = false;
  for (unsigned I = 0; I != NSAPI::NumNSNumberLiteralMethods; ++I)
    NSNumberLiteralMethods[I] = nullptr;

  if (getLangOpts().ObjC)
    NSAPIObj.reset(new NSAPI(Context));

  if (getLangOpts().CPlusPlus)
    FieldCollector.reset(new CXXFieldCollector());

  // Tell diagnostics how to render things from the AST library.
  Diags.SetArgToStringFn(&FormatASTNodeDiagnosticArgument, &Context);

  ExprEvalContexts.emplace_back(
      ExpressionEvaluationContext::PotentiallyEvaluated, 0, CleanupInfo{},
      nullptr, ExpressionEvaluationContextRecord::EK_Other);

  PreallocatedFunctionScope.reset(new FunctionScopeInfo(Diags));

  // Initialization of data sharing attributes stack for OpenMP
  InitDataSharingAttributesStack();

  std::unique_ptr<sema::SemaPPCallbacks> Callbacks =
      llvm::make_unique<sema::SemaPPCallbacks>();
  SemaPPCallbackHandler = Callbacks.get();
  PP.addPPCallbacks(std::move(Callbacks));
  SemaPPCallbackHandler->set(*this);
}

void Sema::addImplicitTypedef(StringRef Name, QualType T) {
  DeclarationName DN = &Context.Idents.get(Name);
  if (IdResolver.begin(DN) == IdResolver.end())
    PushOnScopeChains(Context.buildImplicitTypedef(T, Name), TUScope);
}

void Sema::Initialize() {
  if (SemaConsumer *SC = dyn_cast<SemaConsumer>(&Consumer))
    SC->InitializeSema(*this);

  // Tell the external Sema source about this Sema object.
  if (ExternalSemaSource *ExternalSema
      = dyn_cast_or_null<ExternalSemaSource>(Context.getExternalSource()))
    ExternalSema->InitializeSema(*this);

  // This needs to happen after ExternalSemaSource::InitializeSema(this) or we
  // will not be able to merge any duplicate __va_list_tag decls correctly.
  VAListTagName = PP.getIdentifierInfo("__va_list_tag");

  if (!TUScope)
    return;

  // Initialize predefined 128-bit integer types, if needed.
  if (Context.getTargetInfo().hasInt128Type()) {
    // If either of the 128-bit integer types are unavailable to name lookup,
    // define them now.
    DeclarationName Int128 = &Context.Idents.get("__int128_t");
    if (IdResolver.begin(Int128) == IdResolver.end())
      PushOnScopeChains(Context.getInt128Decl(), TUScope);

    DeclarationName UInt128 = &Context.Idents.get("__uint128_t");
    if (IdResolver.begin(UInt128) == IdResolver.end())
      PushOnScopeChains(Context.getUInt128Decl(), TUScope);
  }


  // Initialize predefined Objective-C types:
  if (getLangOpts().ObjC) {
    // If 'SEL' does not yet refer to any declarations, make it refer to the
    // predefined 'SEL'.
    DeclarationName SEL = &Context.Idents.get("SEL");
    if (IdResolver.begin(SEL) == IdResolver.end())
      PushOnScopeChains(Context.getObjCSelDecl(), TUScope);

    // If 'id' does not yet refer to any declarations, make it refer to the
    // predefined 'id'.
    DeclarationName Id = &Context.Idents.get("id");
    if (IdResolver.begin(Id) == IdResolver.end())
      PushOnScopeChains(Context.getObjCIdDecl(), TUScope);

    // Create the built-in typedef for 'Class'.
    DeclarationName Class = &Context.Idents.get("Class");
    if (IdResolver.begin(Class) == IdResolver.end())
      PushOnScopeChains(Context.getObjCClassDecl(), TUScope);

    // Create the built-in forward declaratino for 'Protocol'.
    DeclarationName Protocol = &Context.Idents.get("Protocol");
    if (IdResolver.begin(Protocol) == IdResolver.end())
      PushOnScopeChains(Context.getObjCProtocolDecl(), TUScope);
  }

  // Create the internal type for the *StringMakeConstantString builtins.
  DeclarationName ConstantString = &Context.Idents.get("__NSConstantString");
  if (IdResolver.begin(ConstantString) == IdResolver.end())
    PushOnScopeChains(Context.getCFConstantStringDecl(), TUScope);

  // Initialize Microsoft "predefined C++ types".
  if (getLangOpts().MSVCCompat) {
    if (getLangOpts().CPlusPlus &&
        IdResolver.begin(&Context.Idents.get("type_info")) == IdResolver.end())
      PushOnScopeChains(Context.buildImplicitRecord("type_info", TTK_Class),
                        TUScope);

    addImplicitTypedef("size_t", Context.getSizeType());
  }

  // Initialize predefined OpenCL types and supported extensions and (optional)
  // core features.
  if (getLangOpts().OpenCL) {
    getOpenCLOptions().addSupport(
        Context.getTargetInfo().getSupportedOpenCLOpts());
    getOpenCLOptions().enableSupportedCore(getLangOpts());
    addImplicitTypedef("sampler_t", Context.OCLSamplerTy);
    addImplicitTypedef("event_t", Context.OCLEventTy);
    if (getLangOpts().OpenCLCPlusPlus || getLangOpts().OpenCLVersion >= 200) {
      addImplicitTypedef("clk_event_t", Context.OCLClkEventTy);
      addImplicitTypedef("queue_t", Context.OCLQueueTy);
      addImplicitTypedef("reserve_id_t", Context.OCLReserveIDTy);
      addImplicitTypedef("atomic_int", Context.getAtomicType(Context.IntTy));
      addImplicitTypedef("atomic_uint",
                         Context.getAtomicType(Context.UnsignedIntTy));
      auto AtomicLongT = Context.getAtomicType(Context.LongTy);
      addImplicitTypedef("atomic_long", AtomicLongT);
      auto AtomicULongT = Context.getAtomicType(Context.UnsignedLongTy);
      addImplicitTypedef("atomic_ulong", AtomicULongT);
      addImplicitTypedef("atomic_float",
                         Context.getAtomicType(Context.FloatTy));
      auto AtomicDoubleT = Context.getAtomicType(Context.DoubleTy);
      addImplicitTypedef("atomic_double", AtomicDoubleT);
      // OpenCLC v2.0, s6.13.11.6 requires that atomic_flag is implemented as
      // 32-bit integer and OpenCLC v2.0, s6.1.1 int is always 32-bit wide.
      addImplicitTypedef("atomic_flag", Context.getAtomicType(Context.IntTy));
      auto AtomicIntPtrT = Context.getAtomicType(Context.getIntPtrType());
      addImplicitTypedef("atomic_intptr_t", AtomicIntPtrT);
      auto AtomicUIntPtrT = Context.getAtomicType(Context.getUIntPtrType());
      addImplicitTypedef("atomic_uintptr_t", AtomicUIntPtrT);
      auto AtomicSizeT = Context.getAtomicType(Context.getSizeType());
      addImplicitTypedef("atomic_size_t", AtomicSizeT);
      auto AtomicPtrDiffT = Context.getAtomicType(Context.getPointerDiffType());
      addImplicitTypedef("atomic_ptrdiff_t", AtomicPtrDiffT);

      // OpenCL v2.0 s6.13.11.6:
      // - The atomic_long and atomic_ulong types are supported if the
      //   cl_khr_int64_base_atomics and cl_khr_int64_extended_atomics
      //   extensions are supported.
      // - The atomic_double type is only supported if double precision
      //   is supported and the cl_khr_int64_base_atomics and
      //   cl_khr_int64_extended_atomics extensions are supported.
      // - If the device address space is 64-bits, the data types
      //   atomic_intptr_t, atomic_uintptr_t, atomic_size_t and
      //   atomic_ptrdiff_t are supported if the cl_khr_int64_base_atomics and
      //   cl_khr_int64_extended_atomics extensions are supported.
      std::vector<QualType> Atomic64BitTypes;
      Atomic64BitTypes.push_back(AtomicLongT);
      Atomic64BitTypes.push_back(AtomicULongT);
      Atomic64BitTypes.push_back(AtomicDoubleT);
      if (Context.getTypeSize(AtomicSizeT) == 64) {
        Atomic64BitTypes.push_back(AtomicSizeT);
        Atomic64BitTypes.push_back(AtomicIntPtrT);
        Atomic64BitTypes.push_back(AtomicUIntPtrT);
        Atomic64BitTypes.push_back(AtomicPtrDiffT);
      }
      for (auto &I : Atomic64BitTypes)
        setOpenCLExtensionForType(I,
            "cl_khr_int64_base_atomics cl_khr_int64_extended_atomics");

      setOpenCLExtensionForType(AtomicDoubleT, "cl_khr_fp64");
    }

    setOpenCLExtensionForType(Context.DoubleTy, "cl_khr_fp64");

#define GENERIC_IMAGE_TYPE_EXT(Type, Id, Ext) \
    setOpenCLExtensionForType(Context.Id, Ext);
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    addImplicitTypedef(#ExtType, Context.Id##Ty); \
    setOpenCLExtensionForType(Context.Id##Ty, #Ext);
#include "clang/Basic/OpenCLExtensionTypes.def"
    };

  if (Context.getTargetInfo().hasBuiltinMSVaList()) {
    DeclarationName MSVaList = &Context.Idents.get("__builtin_ms_va_list");
    if (IdResolver.begin(MSVaList) == IdResolver.end())
      PushOnScopeChains(Context.getBuiltinMSVaListDecl(), TUScope);
  }

  DeclarationName BuiltinVaList = &Context.Idents.get("__builtin_va_list");
  if (IdResolver.begin(BuiltinVaList) == IdResolver.end())
    PushOnScopeChains(Context.getBuiltinVaListDecl(), TUScope);
}

Sema::~Sema() {
  if (VisContext) FreeVisContext();

  // Kill all the active scopes.
  for (sema::FunctionScopeInfo *FSI : FunctionScopes)
    if (FSI != PreallocatedFunctionScope.get())
      delete FSI;

  // Tell the SemaConsumer to forget about us; we're going out of scope.
  if (SemaConsumer *SC = dyn_cast<SemaConsumer>(&Consumer))
    SC->ForgetSema();

  // Detach from the external Sema source.
  if (ExternalSemaSource *ExternalSema
        = dyn_cast_or_null<ExternalSemaSource>(Context.getExternalSource()))
    ExternalSema->ForgetSema();

  // If Sema's ExternalSource is the multiplexer - we own it.
  if (isMultiplexExternalSource)
    delete ExternalSource;

  threadSafety::threadSafetyCleanup(ThreadSafetyDeclCache);

  // Destroys data sharing attributes stack for OpenMP
  DestroyDataSharingAttributesStack();

  // Detach from the PP callback handler which outlives Sema since it's owned
  // by the preprocessor.
  SemaPPCallbackHandler->reset();

  assert(DelayedTypos.empty() && "Uncorrected typos!");
}

/// makeUnavailableInSystemHeader - There is an error in the current
/// context.  If we're still in a system header, and we can plausibly
/// make the relevant declaration unavailable instead of erroring, do
/// so and return true.
bool Sema::makeUnavailableInSystemHeader(SourceLocation loc,
                                      UnavailableAttr::ImplicitReason reason) {
  // If we're not in a function, it's an error.
  FunctionDecl *fn = dyn_cast<FunctionDecl>(CurContext);
  if (!fn) return false;

  // If we're in template instantiation, it's an error.
  if (inTemplateInstantiation())
    return false;

  // If that function's not in a system header, it's an error.
  if (!Context.getSourceManager().isInSystemHeader(loc))
    return false;

  // If the function is already unavailable, it's not an error.
  if (fn->hasAttr<UnavailableAttr>()) return true;

  fn->addAttr(UnavailableAttr::CreateImplicit(Context, "", reason, loc));
  return true;
}

ASTMutationListener *Sema::getASTMutationListener() const {
  return getASTConsumer().GetASTMutationListener();
}

///Registers an external source. If an external source already exists,
/// creates a multiplex external source and appends to it.
///
///\param[in] E - A non-null external sema source.
///
void Sema::addExternalSource(ExternalSemaSource *E) {
  assert(E && "Cannot use with NULL ptr");

  if (!ExternalSource) {
    ExternalSource = E;
    return;
  }

  if (isMultiplexExternalSource)
    static_cast<MultiplexExternalSemaSource*>(ExternalSource)->addSource(*E);
  else {
    ExternalSource = new MultiplexExternalSemaSource(*ExternalSource, *E);
    isMultiplexExternalSource = true;
  }
}

/// Print out statistics about the semantic analysis.
void Sema::PrintStats() const {
  llvm::errs() << "\n*** Semantic Analysis Stats:\n";
  llvm::errs() << NumSFINAEErrors << " SFINAE diagnostics trapped.\n";

  BumpAlloc.PrintStats();
  AnalysisWarnings.PrintStats();
}

void Sema::diagnoseNullableToNonnullConversion(QualType DstType,
                                               QualType SrcType,
                                               SourceLocation Loc) {
  Optional<NullabilityKind> ExprNullability = SrcType->getNullability(Context);
  if (!ExprNullability || *ExprNullability != NullabilityKind::Nullable)
    return;

  Optional<NullabilityKind> TypeNullability = DstType->getNullability(Context);
  if (!TypeNullability || *TypeNullability != NullabilityKind::NonNull)
    return;

  Diag(Loc, diag::warn_nullability_lost) << SrcType << DstType;
}

void Sema::diagnoseZeroToNullptrConversion(CastKind Kind, const Expr* E) {
  if (Diags.isIgnored(diag::warn_zero_as_null_pointer_constant,
                      E->getBeginLoc()))
    return;
  // nullptr only exists from C++11 on, so don't warn on its absence earlier.
  if (!getLangOpts().CPlusPlus11)
    return;

  if (Kind != CK_NullToPointer && Kind != CK_NullToMemberPointer)
    return;
  if (E->IgnoreParenImpCasts()->getType()->isNullPtrType())
    return;

  // If it is a macro from system header, and if the macro name is not "NULL",
  // do not warn.
  SourceLocation MaybeMacroLoc = E->getBeginLoc();
  if (Diags.getSuppressSystemWarnings() &&
      SourceMgr.isInSystemMacro(MaybeMacroLoc) &&
      !findMacroSpelling(MaybeMacroLoc, "NULL"))
    return;

  Diag(E->getBeginLoc(), diag::warn_zero_as_null_pointer_constant)
      << FixItHint::CreateReplacement(E->getSourceRange(), "nullptr");
}

/// ImpCastExprToType - If Expr is not of type 'Type', insert an implicit cast.
/// If there is already an implicit cast, merge into the existing one.
/// The result is of the given category.
ExprResult Sema::ImpCastExprToType(Expr *E, QualType Ty,
                                   CastKind Kind, ExprValueKind VK,
                                   const CXXCastPath *BasePath,
                                   CheckedConversionKind CCK) {
#ifndef NDEBUG
  if (VK == VK_RValue && !E->isRValue()) {
    switch (Kind) {
    default:
      llvm_unreachable("can't implicitly cast lvalue to rvalue with this cast "
                       "kind");
    case CK_LValueToRValue:
    case CK_ArrayToPointerDecay:
    case CK_FunctionToPointerDecay:
    case CK_ToVoid:
    case CK_NonAtomicToAtomic:
      break;
    }
  }
  assert((VK == VK_RValue || !E->isRValue()) && "can't cast rvalue to lvalue");
#endif

  diagnoseNullableToNonnullConversion(Ty, E->getType(), E->getBeginLoc());
  diagnoseZeroToNullptrConversion(Kind, E);

  QualType ExprTy = Context.getCanonicalType(E->getType());
  QualType TypeTy = Context.getCanonicalType(Ty);

  if (ExprTy == TypeTy)
    return E;

  // C++1z [conv.array]: The temporary materialization conversion is applied.
  // We also use this to fuel C++ DR1213, which applies to C++11 onwards.
  if (Kind == CK_ArrayToPointerDecay && getLangOpts().CPlusPlus &&
      E->getValueKind() == VK_RValue) {
    // The temporary is an lvalue in C++98 and an xvalue otherwise.
    ExprResult Materialized = CreateMaterializeTemporaryExpr(
        E->getType(), E, !getLangOpts().CPlusPlus11);
    if (Materialized.isInvalid())
      return ExprError();
    E = Materialized.get();
  }

  if (ImplicitCastExpr *ImpCast = dyn_cast<ImplicitCastExpr>(E)) {
    if (ImpCast->getCastKind() == Kind && (!BasePath || BasePath->empty())) {
      ImpCast->setType(Ty);
      ImpCast->setValueKind(VK);
      return E;
    }
  }

  return ImplicitCastExpr::Create(Context, Ty, Kind, E, BasePath, VK);
}

/// ScalarTypeToBooleanCastKind - Returns the cast kind corresponding
/// to the conversion from scalar type ScalarTy to the Boolean type.
CastKind Sema::ScalarTypeToBooleanCastKind(QualType ScalarTy) {
  switch (ScalarTy->getScalarTypeKind()) {
  case Type::STK_Bool: return CK_NoOp;
  case Type::STK_CPointer: return CK_PointerToBoolean;
  case Type::STK_BlockPointer: return CK_PointerToBoolean;
  case Type::STK_ObjCObjectPointer: return CK_PointerToBoolean;
  case Type::STK_MemberPointer: return CK_MemberPointerToBoolean;
  case Type::STK_Integral: return CK_IntegralToBoolean;
  case Type::STK_Floating: return CK_FloatingToBoolean;
  case Type::STK_IntegralComplex: return CK_IntegralComplexToBoolean;
  case Type::STK_FloatingComplex: return CK_FloatingComplexToBoolean;
  case Type::STK_FixedPoint: return CK_FixedPointToBoolean;
  }
  llvm_unreachable("unknown scalar type kind");
}

/// Used to prune the decls of Sema's UnusedFileScopedDecls vector.
static bool ShouldRemoveFromUnused(Sema *SemaRef, const DeclaratorDecl *D) {
  if (D->getMostRecentDecl()->isUsed())
    return true;

  if (D->isExternallyVisible())
    return true;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    // If this is a function template and none of its specializations is used,
    // we should warn.
    if (FunctionTemplateDecl *Template = FD->getDescribedFunctionTemplate())
      for (const auto *Spec : Template->specializations())
        if (ShouldRemoveFromUnused(SemaRef, Spec))
          return true;

    // UnusedFileScopedDecls stores the first declaration.
    // The declaration may have become definition so check again.
    const FunctionDecl *DeclToCheck;
    if (FD->hasBody(DeclToCheck))
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);

    // Later redecls may add new information resulting in not having to warn,
    // so check again.
    DeclToCheck = FD->getMostRecentDecl();
    if (DeclToCheck != FD)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    // If a variable usable in constant expressions is referenced,
    // don't warn if it isn't used: if the value of a variable is required
    // for the computation of a constant expression, it doesn't make sense to
    // warn even if the variable isn't odr-used.  (isReferenced doesn't
    // precisely reflect that, but it's a decent approximation.)
    if (VD->isReferenced() &&
        VD->isUsableInConstantExpressions(SemaRef->Context))
      return true;

    if (VarTemplateDecl *Template = VD->getDescribedVarTemplate())
      // If this is a variable template and none of its specializations is used,
      // we should warn.
      for (const auto *Spec : Template->specializations())
        if (ShouldRemoveFromUnused(SemaRef, Spec))
          return true;

    // UnusedFileScopedDecls stores the first declaration.
    // The declaration may have become definition so check again.
    const VarDecl *DeclToCheck = VD->getDefinition();
    if (DeclToCheck)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);

    // Later redecls may add new information resulting in not having to warn,
    // so check again.
    DeclToCheck = VD->getMostRecentDecl();
    if (DeclToCheck != VD)
      return !SemaRef->ShouldWarnIfUnusedFileScopedDecl(DeclToCheck);
  }

  return false;
}

static bool isFunctionOrVarDeclExternC(NamedDecl *ND) {
  if (auto *FD = dyn_cast<FunctionDecl>(ND))
    return FD->isExternC();
  return cast<VarDecl>(ND)->isExternC();
}

/// Determine whether ND is an external-linkage function or variable whose
/// type has no linkage.
bool Sema::isExternalWithNoLinkageType(ValueDecl *VD) {
  // Note: it's not quite enough to check whether VD has UniqueExternalLinkage,
  // because we also want to catch the case where its type has VisibleNoLinkage,
  // which does not affect the linkage of VD.
  return getLangOpts().CPlusPlus && VD->hasExternalFormalLinkage() &&
         !isExternalFormalLinkage(VD->getType()->getLinkage()) &&
         !isFunctionOrVarDeclExternC(VD);
}

/// Obtains a sorted list of functions and variables that are undefined but
/// ODR-used.
void Sema::getUndefinedButUsed(
    SmallVectorImpl<std::pair<NamedDecl *, SourceLocation> > &Undefined) {
  for (const auto &UndefinedUse : UndefinedButUsed) {
    NamedDecl *ND = UndefinedUse.first;

    // Ignore attributes that have become invalid.
    if (ND->isInvalidDecl()) continue;

    // __attribute__((weakref)) is basically a definition.
    if (ND->hasAttr<WeakRefAttr>()) continue;

    if (isa<CXXDeductionGuideDecl>(ND))
      continue;

    if (ND->hasAttr<DLLImportAttr>() || ND->hasAttr<DLLExportAttr>()) {
      // An exported function will always be emitted when defined, so even if
      // the function is inline, it doesn't have to be emitted in this TU. An
      // imported function implies that it has been exported somewhere else.
      continue;
    }

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(ND)) {
      if (FD->isDefined())
        continue;
      if (FD->isExternallyVisible() &&
          !isExternalWithNoLinkageType(FD) &&
          !FD->getMostRecentDecl()->isInlined() &&
          !FD->hasAttr<ExcludeFromExplicitInstantiationAttr>())
        continue;
      if (FD->getBuiltinID())
        continue;
    } else {
      auto *VD = cast<VarDecl>(ND);
      if (VD->hasDefinition() != VarDecl::DeclarationOnly)
        continue;
      if (VD->isExternallyVisible() &&
          !isExternalWithNoLinkageType(VD) &&
          !VD->getMostRecentDecl()->isInline() &&
          !VD->hasAttr<ExcludeFromExplicitInstantiationAttr>())
        continue;

      // Skip VarDecls that lack formal definitions but which we know are in
      // fact defined somewhere.
      if (VD->isKnownToBeDefined())
        continue;
    }

    Undefined.push_back(std::make_pair(ND, UndefinedUse.second));
  }
}

/// checkUndefinedButUsed - Check for undefined objects with internal linkage
/// or that are inline.
static void checkUndefinedButUsed(Sema &S) {
  if (S.UndefinedButUsed.empty()) return;

  // Collect all the still-undefined entities with internal linkage.
  SmallVector<std::pair<NamedDecl *, SourceLocation>, 16> Undefined;
  S.getUndefinedButUsed(Undefined);
  if (Undefined.empty()) return;

  for (auto Undef : Undefined) {
    ValueDecl *VD = cast<ValueDecl>(Undef.first);
    SourceLocation UseLoc = Undef.second;

    if (S.isExternalWithNoLinkageType(VD)) {
      // C++ [basic.link]p8:
      //   A type without linkage shall not be used as the type of a variable
      //   or function with external linkage unless
      //    -- the entity has C language linkage
      //    -- the entity is not odr-used or is defined in the same TU
      //
      // As an extension, accept this in cases where the type is externally
      // visible, since the function or variable actually can be defined in
      // another translation unit in that case.
      S.Diag(VD->getLocation(), isExternallyVisible(VD->getType()->getLinkage())
                                    ? diag::ext_undefined_internal_type
                                    : diag::err_undefined_internal_type)
        << isa<VarDecl>(VD) << VD;
    } else if (!VD->isExternallyVisible()) {
      // FIXME: We can promote this to an error. The function or variable can't
      // be defined anywhere else, so the program must necessarily violate the
      // one definition rule.
      S.Diag(VD->getLocation(), diag::warn_undefined_internal)
        << isa<VarDecl>(VD) << VD;
    } else if (auto *FD = dyn_cast<FunctionDecl>(VD)) {
      (void)FD;
      assert(FD->getMostRecentDecl()->isInlined() &&
             "used object requires definition but isn't inline or internal?");
      // FIXME: This is ill-formed; we should reject.
      S.Diag(VD->getLocation(), diag::warn_undefined_inline) << VD;
    } else {
      assert(cast<VarDecl>(VD)->getMostRecentDecl()->isInline() &&
             "used var requires definition but isn't inline or internal?");
      S.Diag(VD->getLocation(), diag::err_undefined_inline_var) << VD;
    }
    if (UseLoc.isValid())
      S.Diag(UseLoc, diag::note_used_here);
  }

  S.UndefinedButUsed.clear();
}

void Sema::LoadExternalWeakUndeclaredIdentifiers() {
  if (!ExternalSource)
    return;

  SmallVector<std::pair<IdentifierInfo *, WeakInfo>, 4> WeakIDs;
  ExternalSource->ReadWeakUndeclaredIdentifiers(WeakIDs);
  for (auto &WeakID : WeakIDs)
    WeakUndeclaredIdentifiers.insert(WeakID);
}


typedef llvm::DenseMap<const CXXRecordDecl*, bool> RecordCompleteMap;

/// Returns true, if all methods and nested classes of the given
/// CXXRecordDecl are defined in this translation unit.
///
/// Should only be called from ActOnEndOfTranslationUnit so that all
/// definitions are actually read.
static bool MethodsAndNestedClassesComplete(const CXXRecordDecl *RD,
                                            RecordCompleteMap &MNCComplete) {
  RecordCompleteMap::iterator Cache = MNCComplete.find(RD);
  if (Cache != MNCComplete.end())
    return Cache->second;
  if (!RD->isCompleteDefinition())
    return false;
  bool Complete = true;
  for (DeclContext::decl_iterator I = RD->decls_begin(),
                                  E = RD->decls_end();
       I != E && Complete; ++I) {
    if (const CXXMethodDecl *M = dyn_cast<CXXMethodDecl>(*I))
      Complete = M->isDefined() || M->isDefaulted() ||
                 (M->isPure() && !isa<CXXDestructorDecl>(M));
    else if (const FunctionTemplateDecl *F = dyn_cast<FunctionTemplateDecl>(*I))
      // If the template function is marked as late template parsed at this
      // point, it has not been instantiated and therefore we have not
      // performed semantic analysis on it yet, so we cannot know if the type
      // can be considered complete.
      Complete = !F->getTemplatedDecl()->isLateTemplateParsed() &&
                  F->getTemplatedDecl()->isDefined();
    else if (const CXXRecordDecl *R = dyn_cast<CXXRecordDecl>(*I)) {
      if (R->isInjectedClassName())
        continue;
      if (R->hasDefinition())
        Complete = MethodsAndNestedClassesComplete(R->getDefinition(),
                                                   MNCComplete);
      else
        Complete = false;
    }
  }
  MNCComplete[RD] = Complete;
  return Complete;
}

/// Returns true, if the given CXXRecordDecl is fully defined in this
/// translation unit, i.e. all methods are defined or pure virtual and all
/// friends, friend functions and nested classes are fully defined in this
/// translation unit.
///
/// Should only be called from ActOnEndOfTranslationUnit so that all
/// definitions are actually read.
static bool IsRecordFullyDefined(const CXXRecordDecl *RD,
                                 RecordCompleteMap &RecordsComplete,
                                 RecordCompleteMap &MNCComplete) {
  RecordCompleteMap::iterator Cache = RecordsComplete.find(RD);
  if (Cache != RecordsComplete.end())
    return Cache->second;
  bool Complete = MethodsAndNestedClassesComplete(RD, MNCComplete);
  for (CXXRecordDecl::friend_iterator I = RD->friend_begin(),
                                      E = RD->friend_end();
       I != E && Complete; ++I) {
    // Check if friend classes and methods are complete.
    if (TypeSourceInfo *TSI = (*I)->getFriendType()) {
      // Friend classes are available as the TypeSourceInfo of the FriendDecl.
      if (CXXRecordDecl *FriendD = TSI->getType()->getAsCXXRecordDecl())
        Complete = MethodsAndNestedClassesComplete(FriendD, MNCComplete);
      else
        Complete = false;
    } else {
      // Friend functions are available through the NamedDecl of FriendDecl.
      if (const FunctionDecl *FD =
          dyn_cast<FunctionDecl>((*I)->getFriendDecl()))
        Complete = FD->isDefined();
      else
        // This is a template friend, give up.
        Complete = false;
    }
  }
  RecordsComplete[RD] = Complete;
  return Complete;
}

void Sema::emitAndClearUnusedLocalTypedefWarnings() {
  if (ExternalSource)
    ExternalSource->ReadUnusedLocalTypedefNameCandidates(
        UnusedLocalTypedefNameCandidates);
  for (const TypedefNameDecl *TD : UnusedLocalTypedefNameCandidates) {
    if (TD->isReferenced())
      continue;
    Diag(TD->getLocation(), diag::warn_unused_local_typedef)
        << isa<TypeAliasDecl>(TD) << TD->getDeclName();
  }
  UnusedLocalTypedefNameCandidates.clear();
}

/// This is called before the very first declaration in the translation unit
/// is parsed. Note that the ASTContext may have already injected some
/// declarations.
void Sema::ActOnStartOfTranslationUnit() {
  if (getLangOpts().ModulesTS &&
      (getLangOpts().getCompilingModule() == LangOptions::CMK_ModuleInterface ||
       getLangOpts().getCompilingModule() == LangOptions::CMK_None)) {
    SourceLocation StartOfTU =
        SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID());

    // We start in the global module; all those declarations are implicitly
    // module-private (though they do not have module linkage).
    auto &Map = PP.getHeaderSearchInfo().getModuleMap();
    auto *GlobalModule = Map.createGlobalModuleForInterfaceUnit(StartOfTU);
    assert(GlobalModule && "module creation should not fail");

    // Enter the scope of the global module.
    ModuleScopes.push_back({});
    ModuleScopes.back().Module = GlobalModule;
    VisibleModules.setVisible(GlobalModule, StartOfTU);

    // All declarations created from now on are owned by the global module.
    auto *TU = Context.getTranslationUnitDecl();
    TU->setModuleOwnershipKind(Decl::ModuleOwnershipKind::Visible);
    TU->setLocalOwningModule(GlobalModule);
  }
}

/// ActOnEndOfTranslationUnit - This is called at the very end of the
/// translation unit when EOF is reached and all but the top-level scope is
/// popped.
void Sema::ActOnEndOfTranslationUnit() {
  assert(DelayedDiagnostics.getCurrentPool() == nullptr
         && "reached end of translation unit with a pool attached?");

  // If code completion is enabled, don't perform any end-of-translation-unit
  // work.
  if (PP.isCodeCompletionEnabled())
    return;

  // Transfer late parsed template instantiations over to the pending template
  // instantiation list. During normal compliation, the late template parser
  // will be installed and instantiating these templates will succeed.
  //
  // If we are building a TU prefix for serialization, it is also safe to
  // transfer these over, even though they are not parsed. The end of the TU
  // should be outside of any eager template instantiation scope, so when this
  // AST is deserialized, these templates will not be parsed until the end of
  // the combined TU.
  PendingInstantiations.insert(PendingInstantiations.end(),
                               LateParsedInstantiations.begin(),
                               LateParsedInstantiations.end());
  LateParsedInstantiations.clear();

  // Complete translation units and modules define vtables and perform implicit
  // instantiations. PCH files do not.
  if (TUKind != TU_Prefix) {
    DiagnoseUseOfUnimplementedSelectors();

    // If DefinedUsedVTables ends up marking any virtual member functions it
    // might lead to more pending template instantiations, which we then need
    // to instantiate.
    DefineUsedVTables();

    // C++: Perform implicit template instantiations.
    //
    // FIXME: When we perform these implicit instantiations, we do not
    // carefully keep track of the point of instantiation (C++ [temp.point]).
    // This means that name lookup that occurs within the template
    // instantiation will always happen at the end of the translation unit,
    // so it will find some names that are not required to be found. This is
    // valid, but we could do better by diagnosing if an instantiation uses a
    // name that was not visible at its first point of instantiation.
    if (ExternalSource) {
      // Load pending instantiations from the external source.
      SmallVector<PendingImplicitInstantiation, 4> Pending;
      ExternalSource->ReadPendingInstantiations(Pending);
      for (auto PII : Pending)
        if (auto Func = dyn_cast<FunctionDecl>(PII.first))
          Func->setInstantiationIsPending(true);
      PendingInstantiations.insert(PendingInstantiations.begin(),
                                   Pending.begin(), Pending.end());
    }

    PerformPendingInstantiations();

    assert(LateParsedInstantiations.empty() &&
           "end of TU template instantiation should not create more "
           "late-parsed templates");

    if (LateTemplateParserCleanup)
      LateTemplateParserCleanup(OpaqueParser);

    CheckDelayedMemberExceptionSpecs();
  }

  DiagnoseUnterminatedPragmaPack();
  DiagnoseUnterminatedPragmaAttribute();

  // All delayed member exception specs should be checked or we end up accepting
  // incompatible declarations.
  assert(DelayedOverridingExceptionSpecChecks.empty());
  assert(DelayedEquivalentExceptionSpecChecks.empty());
  assert(DelayedDefaultedMemberExceptionSpecs.empty());

  // All dllexport classes should have been processed already.
  assert(DelayedDllExportClasses.empty());

  // Remove file scoped decls that turned out to be used.
  UnusedFileScopedDecls.erase(
      std::remove_if(UnusedFileScopedDecls.begin(nullptr, true),
                     UnusedFileScopedDecls.end(),
                     [this](const DeclaratorDecl *DD) {
                       return ShouldRemoveFromUnused(this, DD);
                     }),
      UnusedFileScopedDecls.end());

  if (TUKind == TU_Prefix) {
    // Translation unit prefixes don't need any of the checking below.
    if (!PP.isIncrementalProcessingEnabled())
      TUScope = nullptr;
    return;
  }

  // Check for #pragma weak identifiers that were never declared
  LoadExternalWeakUndeclaredIdentifiers();
  for (auto WeakID : WeakUndeclaredIdentifiers) {
    if (WeakID.second.getUsed())
      continue;

    Decl *PrevDecl = LookupSingleName(TUScope, WeakID.first, SourceLocation(),
                                      LookupOrdinaryName);
    if (PrevDecl != nullptr &&
        !(isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl)))
      Diag(WeakID.second.getLocation(), diag::warn_attribute_wrong_decl_type)
          << "'weak'" << ExpectedVariableOrFunction;
    else
      Diag(WeakID.second.getLocation(), diag::warn_weak_identifier_undeclared)
          << WeakID.first;
  }

  if (LangOpts.CPlusPlus11 &&
      !Diags.isIgnored(diag::warn_delegating_ctor_cycle, SourceLocation()))
    CheckDelegatingCtorCycles();

  if (!Diags.hasErrorOccurred()) {
    if (ExternalSource)
      ExternalSource->ReadUndefinedButUsed(UndefinedButUsed);
    checkUndefinedButUsed(*this);
  }

  if (TUKind == TU_Module) {
    // If we are building a module interface unit, we need to have seen the
    // module declaration by now.
    if (getLangOpts().getCompilingModule() ==
            LangOptions::CMK_ModuleInterface &&
        (ModuleScopes.empty() ||
         ModuleScopes.back().Module->Kind != Module::ModuleInterfaceUnit)) {
      // FIXME: Make a better guess as to where to put the module declaration.
      Diag(getSourceManager().getLocForStartOfFile(
               getSourceManager().getMainFileID()),
           diag::err_module_declaration_missing);
    }

    // If we are building a module, resolve all of the exported declarations
    // now.
    if (Module *CurrentModule = PP.getCurrentModule()) {
      ModuleMap &ModMap = PP.getHeaderSearchInfo().getModuleMap();

      SmallVector<Module *, 2> Stack;
      Stack.push_back(CurrentModule);
      while (!Stack.empty()) {
        Module *Mod = Stack.pop_back_val();

        // Resolve the exported declarations and conflicts.
        // FIXME: Actually complain, once we figure out how to teach the
        // diagnostic client to deal with complaints in the module map at this
        // point.
        ModMap.resolveExports(Mod, /*Complain=*/false);
        ModMap.resolveUses(Mod, /*Complain=*/false);
        ModMap.resolveConflicts(Mod, /*Complain=*/false);

        // Queue the submodules, so their exports will also be resolved.
        Stack.append(Mod->submodule_begin(), Mod->submodule_end());
      }
    }

    // Warnings emitted in ActOnEndOfTranslationUnit() should be emitted for
    // modules when they are built, not every time they are used.
    emitAndClearUnusedLocalTypedefWarnings();
  }

  // C99 6.9.2p2:
  //   A declaration of an identifier for an object that has file
  //   scope without an initializer, and without a storage-class
  //   specifier or with the storage-class specifier static,
  //   constitutes a tentative definition. If a translation unit
  //   contains one or more tentative definitions for an identifier,
  //   and the translation unit contains no external definition for
  //   that identifier, then the behavior is exactly as if the
  //   translation unit contains a file scope declaration of that
  //   identifier, with the composite type as of the end of the
  //   translation unit, with an initializer equal to 0.
  llvm::SmallSet<VarDecl *, 32> Seen;
  for (TentativeDefinitionsType::iterator
            T = TentativeDefinitions.begin(ExternalSource),
         TEnd = TentativeDefinitions.end();
       T != TEnd; ++T) {
    VarDecl *VD = (*T)->getActingDefinition();

    // If the tentative definition was completed, getActingDefinition() returns
    // null. If we've already seen this variable before, insert()'s second
    // return value is false.
    if (!VD || VD->isInvalidDecl() || !Seen.insert(VD).second)
      continue;

    if (const IncompleteArrayType *ArrayT
        = Context.getAsIncompleteArrayType(VD->getType())) {
      // Set the length of the array to 1 (C99 6.9.2p5).
      Diag(VD->getLocation(), diag::warn_tentative_incomplete_array);
      llvm::APInt One(Context.getTypeSize(Context.getSizeType()), true);
      QualType T = Context.getConstantArrayType(ArrayT->getElementType(),
                                                One, ArrayType::Normal, 0);
      VD->setType(T);
    } else if (RequireCompleteType(VD->getLocation(), VD->getType(),
                                   diag::err_tentative_def_incomplete_type))
      VD->setInvalidDecl();

    // No initialization is performed for a tentative definition.
    CheckCompleteVariableDeclaration(VD);

    // Notify the consumer that we've completed a tentative definition.
    if (!VD->isInvalidDecl())
      Consumer.CompleteTentativeDefinition(VD);
  }

  // If there were errors, disable 'unused' warnings since they will mostly be
  // noise. Don't warn for a use from a module: either we should warn on all
  // file-scope declarations in modules or not at all, but whether the
  // declaration is used is immaterial.
  if (!Diags.hasErrorOccurred() && TUKind != TU_Module) {
    // Output warning for unused file scoped decls.
    for (UnusedFileScopedDeclsType::iterator
           I = UnusedFileScopedDecls.begin(ExternalSource),
           E = UnusedFileScopedDecls.end(); I != E; ++I) {
      if (ShouldRemoveFromUnused(this, *I))
        continue;

      if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(*I)) {
        const FunctionDecl *DiagD;
        if (!FD->hasBody(DiagD))
          DiagD = FD;
        if (DiagD->isDeleted())
          continue; // Deleted functions are supposed to be unused.
        if (DiagD->isReferenced()) {
          if (isa<CXXMethodDecl>(DiagD))
            Diag(DiagD->getLocation(), diag::warn_unneeded_member_function)
                  << DiagD->getDeclName();
          else {
            if (FD->getStorageClass() == SC_Static &&
                !FD->isInlineSpecified() &&
                !SourceMgr.isInMainFile(
                   SourceMgr.getExpansionLoc(FD->getLocation())))
              Diag(DiagD->getLocation(),
                   diag::warn_unneeded_static_internal_decl)
                  << DiagD->getDeclName();
            else
              Diag(DiagD->getLocation(), diag::warn_unneeded_internal_decl)
                   << /*function*/0 << DiagD->getDeclName();
          }
        } else {
          if (FD->getDescribedFunctionTemplate())
            Diag(DiagD->getLocation(), diag::warn_unused_template)
              << /*function*/0 << DiagD->getDeclName();
          else
            Diag(DiagD->getLocation(),
                 isa<CXXMethodDecl>(DiagD) ? diag::warn_unused_member_function
                                           : diag::warn_unused_function)
              << DiagD->getDeclName();
        }
      } else {
        const VarDecl *DiagD = cast<VarDecl>(*I)->getDefinition();
        if (!DiagD)
          DiagD = cast<VarDecl>(*I);
        if (DiagD->isReferenced()) {
          Diag(DiagD->getLocation(), diag::warn_unneeded_internal_decl)
                << /*variable*/1 << DiagD->getDeclName();
        } else if (DiagD->getType().isConstQualified()) {
          const SourceManager &SM = SourceMgr;
          if (SM.getMainFileID() != SM.getFileID(DiagD->getLocation()) ||
              !PP.getLangOpts().IsHeaderFile)
            Diag(DiagD->getLocation(), diag::warn_unused_const_variable)
                << DiagD->getDeclName();
        } else {
          if (DiagD->getDescribedVarTemplate())
            Diag(DiagD->getLocation(), diag::warn_unused_template)
              << /*variable*/1 << DiagD->getDeclName();
          else
            Diag(DiagD->getLocation(), diag::warn_unused_variable)
              << DiagD->getDeclName();
        }
      }
    }

    emitAndClearUnusedLocalTypedefWarnings();
  }

  if (!Diags.isIgnored(diag::warn_unused_private_field, SourceLocation())) {
    // FIXME: Load additional unused private field candidates from the external
    // source.
    RecordCompleteMap RecordsComplete;
    RecordCompleteMap MNCComplete;
    for (NamedDeclSetType::iterator I = UnusedPrivateFields.begin(),
         E = UnusedPrivateFields.end(); I != E; ++I) {
      const NamedDecl *D = *I;
      const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D->getDeclContext());
      if (RD && !RD->isUnion() &&
          IsRecordFullyDefined(RD, RecordsComplete, MNCComplete)) {
        Diag(D->getLocation(), diag::warn_unused_private_field)
              << D->getDeclName();
      }
    }
  }

  if (!Diags.isIgnored(diag::warn_mismatched_delete_new, SourceLocation())) {
    if (ExternalSource)
      ExternalSource->ReadMismatchingDeleteExpressions(DeleteExprs);
    for (const auto &DeletedFieldInfo : DeleteExprs) {
      for (const auto &DeleteExprLoc : DeletedFieldInfo.second) {
        AnalyzeDeleteExprMismatch(DeletedFieldInfo.first, DeleteExprLoc.first,
                                  DeleteExprLoc.second);
      }
    }
  }

  // Check we've noticed that we're no longer parsing the initializer for every
  // variable. If we miss cases, then at best we have a performance issue and
  // at worst a rejects-valid bug.
  assert(ParsingInitForAutoVars.empty() &&
         "Didn't unmark var as having its initializer parsed");

  if (!PP.isIncrementalProcessingEnabled())
    TUScope = nullptr;
}


//===----------------------------------------------------------------------===//
// Helper functions.
//===----------------------------------------------------------------------===//

DeclContext *Sema::getFunctionLevelDeclContext() {
  DeclContext *DC = CurContext;

  while (true) {
    if (isa<BlockDecl>(DC) || isa<EnumDecl>(DC) || isa<CapturedDecl>(DC)) {
      DC = DC->getParent();
    } else if (isa<CXXMethodDecl>(DC) &&
               cast<CXXMethodDecl>(DC)->getOverloadedOperator() == OO_Call &&
               cast<CXXRecordDecl>(DC->getParent())->isLambda()) {
      DC = DC->getParent()->getParent();
    }
    else break;
  }

  return DC;
}

/// getCurFunctionDecl - If inside of a function body, this returns a pointer
/// to the function decl for the function being parsed.  If we're currently
/// in a 'block', this returns the containing context.
FunctionDecl *Sema::getCurFunctionDecl() {
  DeclContext *DC = getFunctionLevelDeclContext();
  return dyn_cast<FunctionDecl>(DC);
}

ObjCMethodDecl *Sema::getCurMethodDecl() {
  DeclContext *DC = getFunctionLevelDeclContext();
  while (isa<RecordDecl>(DC))
    DC = DC->getParent();
  return dyn_cast<ObjCMethodDecl>(DC);
}

NamedDecl *Sema::getCurFunctionOrMethodDecl() {
  DeclContext *DC = getFunctionLevelDeclContext();
  if (isa<ObjCMethodDecl>(DC) || isa<FunctionDecl>(DC))
    return cast<NamedDecl>(DC);
  return nullptr;
}

void Sema::EmitCurrentDiagnostic(unsigned DiagID) {
  // FIXME: It doesn't make sense to me that DiagID is an incoming argument here
  // and yet we also use the current diag ID on the DiagnosticsEngine. This has
  // been made more painfully obvious by the refactor that introduced this
  // function, but it is possible that the incoming argument can be
  // eliminated. If it truly cannot be (for example, there is some reentrancy
  // issue I am not seeing yet), then there should at least be a clarifying
  // comment somewhere.
  if (Optional<TemplateDeductionInfo*> Info = isSFINAEContext()) {
    switch (DiagnosticIDs::getDiagnosticSFINAEResponse(
              Diags.getCurrentDiagID())) {
    case DiagnosticIDs::SFINAE_Report:
      // We'll report the diagnostic below.
      break;

    case DiagnosticIDs::SFINAE_SubstitutionFailure:
      // Count this failure so that we know that template argument deduction
      // has failed.
      ++NumSFINAEErrors;

      // Make a copy of this suppressed diagnostic and store it with the
      // template-deduction information.
      if (*Info && !(*Info)->hasSFINAEDiagnostic()) {
        Diagnostic DiagInfo(&Diags);
        (*Info)->addSFINAEDiagnostic(DiagInfo.getLocation(),
                       PartialDiagnostic(DiagInfo, Context.getDiagAllocator()));
      }

      Diags.setLastDiagnosticIgnored();
      Diags.Clear();
      return;

    case DiagnosticIDs::SFINAE_AccessControl: {
      // Per C++ Core Issue 1170, access control is part of SFINAE.
      // Additionally, the AccessCheckingSFINAE flag can be used to temporarily
      // make access control a part of SFINAE for the purposes of checking
      // type traits.
      if (!AccessCheckingSFINAE && !getLangOpts().CPlusPlus11)
        break;

      SourceLocation Loc = Diags.getCurrentDiagLoc();

      // Suppress this diagnostic.
      ++NumSFINAEErrors;

      // Make a copy of this suppressed diagnostic and store it with the
      // template-deduction information.
      if (*Info && !(*Info)->hasSFINAEDiagnostic()) {
        Diagnostic DiagInfo(&Diags);
        (*Info)->addSFINAEDiagnostic(DiagInfo.getLocation(),
                       PartialDiagnostic(DiagInfo, Context.getDiagAllocator()));
      }

      Diags.setLastDiagnosticIgnored();
      Diags.Clear();

      // Now the diagnostic state is clear, produce a C++98 compatibility
      // warning.
      Diag(Loc, diag::warn_cxx98_compat_sfinae_access_control);

      // The last diagnostic which Sema produced was ignored. Suppress any
      // notes attached to it.
      Diags.setLastDiagnosticIgnored();
      return;
    }

    case DiagnosticIDs::SFINAE_Suppress:
      // Make a copy of this suppressed diagnostic and store it with the
      // template-deduction information;
      if (*Info) {
        Diagnostic DiagInfo(&Diags);
        (*Info)->addSuppressedDiagnostic(DiagInfo.getLocation(),
                       PartialDiagnostic(DiagInfo, Context.getDiagAllocator()));
      }

      // Suppress this diagnostic.
      Diags.setLastDiagnosticIgnored();
      Diags.Clear();
      return;
    }
  }

  // Copy the diagnostic printing policy over the ASTContext printing policy.
  // TODO: Stop doing that.  See: https://reviews.llvm.org/D45093#1090292
  Context.setPrintingPolicy(getPrintingPolicy());

  // Emit the diagnostic.
  if (!Diags.EmitCurrentDiagnostic())
    return;

  // If this is not a note, and we're in a template instantiation
  // that is different from the last template instantiation where
  // we emitted an error, print a template instantiation
  // backtrace.
  if (!DiagnosticIDs::isBuiltinNote(DiagID))
    PrintContextStack();
}

Sema::SemaDiagnosticBuilder
Sema::Diag(SourceLocation Loc, const PartialDiagnostic& PD) {
  SemaDiagnosticBuilder Builder(Diag(Loc, PD.getDiagID()));
  PD.Emit(Builder);

  return Builder;
}

/// Looks through the macro-expansion chain for the given
/// location, looking for a macro expansion with the given name.
/// If one is found, returns true and sets the location to that
/// expansion loc.
bool Sema::findMacroSpelling(SourceLocation &locref, StringRef name) {
  SourceLocation loc = locref;
  if (!loc.isMacroID()) return false;

  // There's no good way right now to look at the intermediate
  // expansions, so just jump to the expansion location.
  loc = getSourceManager().getExpansionLoc(loc);

  // If that's written with the name, stop here.
  SmallVector<char, 16> buffer;
  if (getPreprocessor().getSpelling(loc, buffer) == name) {
    locref = loc;
    return true;
  }
  return false;
}

/// Determines the active Scope associated with the given declaration
/// context.
///
/// This routine maps a declaration context to the active Scope object that
/// represents that declaration context in the parser. It is typically used
/// from "scope-less" code (e.g., template instantiation, lazy creation of
/// declarations) that injects a name for name-lookup purposes and, therefore,
/// must update the Scope.
///
/// \returns The scope corresponding to the given declaraion context, or NULL
/// if no such scope is open.
Scope *Sema::getScopeForContext(DeclContext *Ctx) {

  if (!Ctx)
    return nullptr;

  Ctx = Ctx->getPrimaryContext();
  for (Scope *S = getCurScope(); S; S = S->getParent()) {
    // Ignore scopes that cannot have declarations. This is important for
    // out-of-line definitions of static class members.
    if (S->getFlags() & (Scope::DeclScope | Scope::TemplateParamScope))
      if (DeclContext *Entity = S->getEntity())
        if (Ctx == Entity->getPrimaryContext())
          return S;
  }

  return nullptr;
}

/// Enter a new function scope
void Sema::PushFunctionScope() {
  if (FunctionScopes.empty()) {
    // Use PreallocatedFunctionScope to avoid allocating memory when possible.
    PreallocatedFunctionScope->Clear();
    FunctionScopes.push_back(PreallocatedFunctionScope.get());
  } else {
    FunctionScopes.push_back(new FunctionScopeInfo(getDiagnostics()));
  }
  if (LangOpts.OpenMP)
    pushOpenMPFunctionRegion();
}

void Sema::PushBlockScope(Scope *BlockScope, BlockDecl *Block) {
  FunctionScopes.push_back(new BlockScopeInfo(getDiagnostics(),
                                              BlockScope, Block));
}

LambdaScopeInfo *Sema::PushLambdaScope() {
  LambdaScopeInfo *const LSI = new LambdaScopeInfo(getDiagnostics());
  FunctionScopes.push_back(LSI);
  return LSI;
}

void Sema::RecordParsingTemplateParameterDepth(unsigned Depth) {
  if (LambdaScopeInfo *const LSI = getCurLambda()) {
    LSI->AutoTemplateParameterDepth = Depth;
    return;
  }
  llvm_unreachable(
      "Remove assertion if intentionally called in a non-lambda context.");
}

// Check that the type of the VarDecl has an accessible copy constructor and
// resolve its destructor's exception spefication.
static void checkEscapingByref(VarDecl *VD, Sema &S) {
  QualType T = VD->getType();
  EnterExpressionEvaluationContext scope(
      S, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
  SourceLocation Loc = VD->getLocation();
  Expr *VarRef =
      new (S.Context) DeclRefExpr(S.Context, VD, false, T, VK_LValue, Loc);
  ExprResult Result = S.PerformMoveOrCopyInitialization(
      InitializedEntity::InitializeBlock(Loc, T, false), VD, VD->getType(),
      VarRef, /*AllowNRVO=*/true);
  if (!Result.isInvalid()) {
    Result = S.MaybeCreateExprWithCleanups(Result);
    Expr *Init = Result.getAs<Expr>();
    S.Context.setBlockVarCopyInit(VD, Init, S.canThrow(Init));
  }

  // The destructor's exception spefication is needed when IRGen generates
  // block copy/destroy functions. Resolve it here.
  if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
    if (CXXDestructorDecl *DD = RD->getDestructor()) {
      auto *FPT = DD->getType()->getAs<FunctionProtoType>();
      S.ResolveExceptionSpec(Loc, FPT);
    }
}

static void markEscapingByrefs(const FunctionScopeInfo &FSI, Sema &S) {
  // Set the EscapingByref flag of __block variables captured by
  // escaping blocks.
  for (const BlockDecl *BD : FSI.Blocks) {
    if (BD->doesNotEscape())
      continue;
    for (const BlockDecl::Capture &BC : BD->captures()) {
      VarDecl *VD = BC.getVariable();
      if (VD->hasAttr<BlocksAttr>())
        VD->setEscapingByref();
    }
  }

  for (VarDecl *VD : FSI.ByrefBlockVars) {
    // __block variables might require us to capture a copy-initializer.
    if (!VD->isEscapingByref())
      continue;
    // It's currently invalid to ever have a __block variable with an
    // array type; should we diagnose that here?
    // Regardless, we don't want to ignore array nesting when
    // constructing this copy.
    if (VD->getType()->isStructureOrClassType())
      checkEscapingByref(VD, S);
  }
}

void Sema::PopFunctionScopeInfo(const AnalysisBasedWarnings::Policy *WP,
                                const Decl *D, const BlockExpr *blkExpr) {
  assert(!FunctionScopes.empty() && "mismatched push/pop!");

  // This function shouldn't be called after popping the current function scope.
  // markEscapingByrefs calls PerformMoveOrCopyInitialization, which can call
  // PushFunctionScope, which can cause clearing out PreallocatedFunctionScope
  // when FunctionScopes is empty.
  markEscapingByrefs(*FunctionScopes.back(), *this);

  FunctionScopeInfo *Scope = FunctionScopes.pop_back_val();

  if (LangOpts.OpenMP)
    popOpenMPFunctionRegion(Scope);

  // Issue any analysis-based warnings.
  if (WP && D)
    AnalysisWarnings.IssueWarnings(*WP, Scope, D, blkExpr);
  else
    for (const auto &PUD : Scope->PossiblyUnreachableDiags)
      Diag(PUD.Loc, PUD.PD);

  // Delete the scope unless its our preallocated scope.
  if (Scope != PreallocatedFunctionScope.get())
    delete Scope;
}

void Sema::PushCompoundScope(bool IsStmtExpr) {
  getCurFunction()->CompoundScopes.push_back(CompoundScopeInfo(IsStmtExpr));
}

void Sema::PopCompoundScope() {
  FunctionScopeInfo *CurFunction = getCurFunction();
  assert(!CurFunction->CompoundScopes.empty() && "mismatched push/pop");

  CurFunction->CompoundScopes.pop_back();
}

/// Determine whether any errors occurred within this function/method/
/// block.
bool Sema::hasAnyUnrecoverableErrorsInThisFunction() const {
  return getCurFunction()->ErrorTrap.hasUnrecoverableErrorOccurred();
}

void Sema::setFunctionHasBranchIntoScope() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasBranchIntoScope();
}

void Sema::setFunctionHasBranchProtectedScope() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasBranchProtectedScope();
}

void Sema::setFunctionHasIndirectGoto() {
  if (!FunctionScopes.empty())
    FunctionScopes.back()->setHasIndirectGoto();
}

BlockScopeInfo *Sema::getCurBlock() {
  if (FunctionScopes.empty())
    return nullptr;

  auto CurBSI = dyn_cast<BlockScopeInfo>(FunctionScopes.back());
  if (CurBSI && CurBSI->TheDecl &&
      !CurBSI->TheDecl->Encloses(CurContext)) {
    // We have switched contexts due to template instantiation.
    assert(!CodeSynthesisContexts.empty());
    return nullptr;
  }

  return CurBSI;
}

FunctionScopeInfo *Sema::getEnclosingFunction() const {
  if (FunctionScopes.empty())
    return nullptr;

  for (int e = FunctionScopes.size() - 1; e >= 0; --e) {
    if (isa<sema::BlockScopeInfo>(FunctionScopes[e]))
      continue;
    return FunctionScopes[e];
  }
  return nullptr;
}

LambdaScopeInfo *Sema::getCurLambda(bool IgnoreNonLambdaCapturingScope) {
  if (FunctionScopes.empty())
    return nullptr;

  auto I = FunctionScopes.rbegin();
  if (IgnoreNonLambdaCapturingScope) {
    auto E = FunctionScopes.rend();
    while (I != E && isa<CapturingScopeInfo>(*I) && !isa<LambdaScopeInfo>(*I))
      ++I;
    if (I == E)
      return nullptr;
  }
  auto *CurLSI = dyn_cast<LambdaScopeInfo>(*I);
  if (CurLSI && CurLSI->Lambda &&
      !CurLSI->Lambda->Encloses(CurContext)) {
    // We have switched contexts due to template instantiation.
    assert(!CodeSynthesisContexts.empty());
    return nullptr;
  }

  return CurLSI;
}
// We have a generic lambda if we parsed auto parameters, or we have
// an associated template parameter list.
LambdaScopeInfo *Sema::getCurGenericLambda() {
  if (LambdaScopeInfo *LSI =  getCurLambda()) {
    return (LSI->AutoTemplateParams.size() ||
                    LSI->GLTemplateParameterList) ? LSI : nullptr;
  }
  return nullptr;
}


void Sema::ActOnComment(SourceRange Comment) {
  if (!LangOpts.RetainCommentsFromSystemHeaders &&
      SourceMgr.isInSystemHeader(Comment.getBegin()))
    return;
  RawComment RC(SourceMgr, Comment, LangOpts.CommentOpts, false);
  if (RC.isAlmostTrailingComment()) {
    SourceRange MagicMarkerRange(Comment.getBegin(),
                                 Comment.getBegin().getLocWithOffset(3));
    StringRef MagicMarkerText;
    switch (RC.getKind()) {
    case RawComment::RCK_OrdinaryBCPL:
      MagicMarkerText = "///<";
      break;
    case RawComment::RCK_OrdinaryC:
      MagicMarkerText = "/**<";
      break;
    default:
      llvm_unreachable("if this is an almost Doxygen comment, "
                       "it should be ordinary");
    }
    Diag(Comment.getBegin(), diag::warn_not_a_doxygen_trailing_member_comment) <<
      FixItHint::CreateReplacement(MagicMarkerRange, MagicMarkerText);
  }
  Context.addComment(RC);
}

// Pin this vtable to this file.
ExternalSemaSource::~ExternalSemaSource() {}

void ExternalSemaSource::ReadMethodPool(Selector Sel) { }
void ExternalSemaSource::updateOutOfDateSelector(Selector Sel) { }

void ExternalSemaSource::ReadKnownNamespaces(
                           SmallVectorImpl<NamespaceDecl *> &Namespaces) {
}

void ExternalSemaSource::ReadUndefinedButUsed(
    llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) {}

void ExternalSemaSource::ReadMismatchingDeleteExpressions(llvm::MapVector<
    FieldDecl *, llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &) {}

/// Figure out if an expression could be turned into a call.
///
/// Use this when trying to recover from an error where the programmer may have
/// written just the name of a function instead of actually calling it.
///
/// \param E - The expression to examine.
/// \param ZeroArgCallReturnTy - If the expression can be turned into a call
///  with no arguments, this parameter is set to the type returned by such a
///  call; otherwise, it is set to an empty QualType.
/// \param OverloadSet - If the expression is an overloaded function
///  name, this parameter is populated with the decls of the various overloads.
bool Sema::tryExprAsCall(Expr &E, QualType &ZeroArgCallReturnTy,
                         UnresolvedSetImpl &OverloadSet) {
  ZeroArgCallReturnTy = QualType();
  OverloadSet.clear();

  const OverloadExpr *Overloads = nullptr;
  bool IsMemExpr = false;
  if (E.getType() == Context.OverloadTy) {
    OverloadExpr::FindResult FR = OverloadExpr::find(const_cast<Expr*>(&E));

    // Ignore overloads that are pointer-to-member constants.
    if (FR.HasFormOfMemberPointer)
      return false;

    Overloads = FR.Expression;
  } else if (E.getType() == Context.BoundMemberTy) {
    Overloads = dyn_cast<UnresolvedMemberExpr>(E.IgnoreParens());
    IsMemExpr = true;
  }

  bool Ambiguous = false;
  bool IsMV = false;

  if (Overloads) {
    for (OverloadExpr::decls_iterator it = Overloads->decls_begin(),
         DeclsEnd = Overloads->decls_end(); it != DeclsEnd; ++it) {
      OverloadSet.addDecl(*it);

      // Check whether the function is a non-template, non-member which takes no
      // arguments.
      if (IsMemExpr)
        continue;
      if (const FunctionDecl *OverloadDecl
            = dyn_cast<FunctionDecl>((*it)->getUnderlyingDecl())) {
        if (OverloadDecl->getMinRequiredArguments() == 0) {
          if (!ZeroArgCallReturnTy.isNull() && !Ambiguous &&
              (!IsMV || !(OverloadDecl->isCPUDispatchMultiVersion() ||
                          OverloadDecl->isCPUSpecificMultiVersion()))) {
            ZeroArgCallReturnTy = QualType();
            Ambiguous = true;
          } else {
            ZeroArgCallReturnTy = OverloadDecl->getReturnType();
            IsMV = OverloadDecl->isCPUDispatchMultiVersion() ||
                   OverloadDecl->isCPUSpecificMultiVersion();
          }
        }
      }
    }

    // If it's not a member, use better machinery to try to resolve the call
    if (!IsMemExpr)
      return !ZeroArgCallReturnTy.isNull();
  }

  // Attempt to call the member with no arguments - this will correctly handle
  // member templates with defaults/deduction of template arguments, overloads
  // with default arguments, etc.
  if (IsMemExpr && !E.isTypeDependent()) {
    bool Suppress = getDiagnostics().getSuppressAllDiagnostics();
    getDiagnostics().setSuppressAllDiagnostics(true);
    ExprResult R = BuildCallToMemberFunction(nullptr, &E, SourceLocation(),
                                             None, SourceLocation());
    getDiagnostics().setSuppressAllDiagnostics(Suppress);
    if (R.isUsable()) {
      ZeroArgCallReturnTy = R.get()->getType();
      return true;
    }
    return false;
  }

  if (const DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E.IgnoreParens())) {
    if (const FunctionDecl *Fun = dyn_cast<FunctionDecl>(DeclRef->getDecl())) {
      if (Fun->getMinRequiredArguments() == 0)
        ZeroArgCallReturnTy = Fun->getReturnType();
      return true;
    }
  }

  // We don't have an expression that's convenient to get a FunctionDecl from,
  // but we can at least check if the type is "function of 0 arguments".
  QualType ExprTy = E.getType();
  const FunctionType *FunTy = nullptr;
  QualType PointeeTy = ExprTy->getPointeeType();
  if (!PointeeTy.isNull())
    FunTy = PointeeTy->getAs<FunctionType>();
  if (!FunTy)
    FunTy = ExprTy->getAs<FunctionType>();

  if (const FunctionProtoType *FPT =
      dyn_cast_or_null<FunctionProtoType>(FunTy)) {
    if (FPT->getNumParams() == 0)
      ZeroArgCallReturnTy = FunTy->getReturnType();
    return true;
  }
  return false;
}

/// Give notes for a set of overloads.
///
/// A companion to tryExprAsCall. In cases when the name that the programmer
/// wrote was an overloaded function, we may be able to make some guesses about
/// plausible overloads based on their return types; such guesses can be handed
/// off to this method to be emitted as notes.
///
/// \param Overloads - The overloads to note.
/// \param FinalNoteLoc - If we've suppressed printing some overloads due to
///  -fshow-overloads=best, this is the location to attach to the note about too
///  many candidates. Typically this will be the location of the original
///  ill-formed expression.
static void noteOverloads(Sema &S, const UnresolvedSetImpl &Overloads,
                          const SourceLocation FinalNoteLoc) {
  int ShownOverloads = 0;
  int SuppressedOverloads = 0;
  for (UnresolvedSetImpl::iterator It = Overloads.begin(),
       DeclsEnd = Overloads.end(); It != DeclsEnd; ++It) {
    // FIXME: Magic number for max shown overloads stolen from
    // OverloadCandidateSet::NoteCandidates.
    if (ShownOverloads >= 4 && S.Diags.getShowOverloads() == Ovl_Best) {
      ++SuppressedOverloads;
      continue;
    }

    NamedDecl *Fn = (*It)->getUnderlyingDecl();
    // Don't print overloads for non-default multiversioned functions.
    if (const auto *FD = Fn->getAsFunction()) {
      if (FD->isMultiVersion() && FD->hasAttr<TargetAttr>() &&
          !FD->getAttr<TargetAttr>()->isDefaultVersion())
        continue;
    }
    S.Diag(Fn->getLocation(), diag::note_possible_target_of_call);
    ++ShownOverloads;
  }

  if (SuppressedOverloads)
    S.Diag(FinalNoteLoc, diag::note_ovl_too_many_candidates)
      << SuppressedOverloads;
}

static void notePlausibleOverloads(Sema &S, SourceLocation Loc,
                                   const UnresolvedSetImpl &Overloads,
                                   bool (*IsPlausibleResult)(QualType)) {
  if (!IsPlausibleResult)
    return noteOverloads(S, Overloads, Loc);

  UnresolvedSet<2> PlausibleOverloads;
  for (OverloadExpr::decls_iterator It = Overloads.begin(),
         DeclsEnd = Overloads.end(); It != DeclsEnd; ++It) {
    const FunctionDecl *OverloadDecl = cast<FunctionDecl>(*It);
    QualType OverloadResultTy = OverloadDecl->getReturnType();
    if (IsPlausibleResult(OverloadResultTy))
      PlausibleOverloads.addDecl(It.getDecl());
  }
  noteOverloads(S, PlausibleOverloads, Loc);
}

/// Determine whether the given expression can be called by just
/// putting parentheses after it.  Notably, expressions with unary
/// operators can't be because the unary operator will start parsing
/// outside the call.
static bool IsCallableWithAppend(Expr *E) {
  E = E->IgnoreImplicit();
  return (!isa<CStyleCastExpr>(E) &&
          !isa<UnaryOperator>(E) &&
          !isa<BinaryOperator>(E) &&
          !isa<CXXOperatorCallExpr>(E));
}

static bool IsCPUDispatchCPUSpecificMultiVersion(const Expr *E) {
  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    E = UO->getSubExpr();

  if (const auto *ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
    if (ULE->getNumDecls() == 0)
      return false;

    const NamedDecl *ND = *ULE->decls_begin();
    if (const auto *FD = dyn_cast<FunctionDecl>(ND))
      return FD->isCPUDispatchMultiVersion() || FD->isCPUSpecificMultiVersion();
  }
  return false;
}

bool Sema::tryToRecoverWithCall(ExprResult &E, const PartialDiagnostic &PD,
                                bool ForceComplain,
                                bool (*IsPlausibleResult)(QualType)) {
  SourceLocation Loc = E.get()->getExprLoc();
  SourceRange Range = E.get()->getSourceRange();

  QualType ZeroArgCallTy;
  UnresolvedSet<4> Overloads;
  if (tryExprAsCall(*E.get(), ZeroArgCallTy, Overloads) &&
      !ZeroArgCallTy.isNull() &&
      (!IsPlausibleResult || IsPlausibleResult(ZeroArgCallTy))) {
    // At this point, we know E is potentially callable with 0
    // arguments and that it returns something of a reasonable type,
    // so we can emit a fixit and carry on pretending that E was
    // actually a CallExpr.
    SourceLocation ParenInsertionLoc = getLocForEndOfToken(Range.getEnd());
    bool IsMV = IsCPUDispatchCPUSpecificMultiVersion(E.get());
    Diag(Loc, PD) << /*zero-arg*/ 1 << IsMV << Range
                  << (IsCallableWithAppend(E.get())
                          ? FixItHint::CreateInsertion(ParenInsertionLoc, "()")
                          : FixItHint());
    if (!IsMV)
      notePlausibleOverloads(*this, Loc, Overloads, IsPlausibleResult);

    // FIXME: Try this before emitting the fixit, and suppress diagnostics
    // while doing so.
    E = ActOnCallExpr(nullptr, E.get(), Range.getEnd(), None,
                      Range.getEnd().getLocWithOffset(1));
    return true;
  }

  if (!ForceComplain) return false;

  bool IsMV = IsCPUDispatchCPUSpecificMultiVersion(E.get());
  Diag(Loc, PD) << /*not zero-arg*/ 0 << IsMV << Range;
  if (!IsMV)
    notePlausibleOverloads(*this, Loc, Overloads, IsPlausibleResult);
  E = ExprError();
  return true;
}

IdentifierInfo *Sema::getSuperIdentifier() const {
  if (!Ident_super)
    Ident_super = &Context.Idents.get("super");
  return Ident_super;
}

IdentifierInfo *Sema::getFloat128Identifier() const {
  if (!Ident___float128)
    Ident___float128 = &Context.Idents.get("__float128");
  return Ident___float128;
}

void Sema::PushCapturedRegionScope(Scope *S, CapturedDecl *CD, RecordDecl *RD,
                                   CapturedRegionKind K) {
  CapturingScopeInfo *CSI = new CapturedRegionScopeInfo(
      getDiagnostics(), S, CD, RD, CD->getContextParam(), K,
      (getLangOpts().OpenMP && K == CR_OpenMP) ? getOpenMPNestingLevel() : 0);
  CSI->ReturnType = Context.VoidTy;
  FunctionScopes.push_back(CSI);
}

CapturedRegionScopeInfo *Sema::getCurCapturedRegion() {
  if (FunctionScopes.empty())
    return nullptr;

  return dyn_cast<CapturedRegionScopeInfo>(FunctionScopes.back());
}

const llvm::MapVector<FieldDecl *, Sema::DeleteLocs> &
Sema::getMismatchingDeleteExpressions() const {
  return DeleteExprs;
}

void Sema::setOpenCLExtensionForType(QualType T, llvm::StringRef ExtStr) {
  if (ExtStr.empty())
    return;
  llvm::SmallVector<StringRef, 1> Exts;
  ExtStr.split(Exts, " ", /* limit */ -1, /* keep empty */ false);
  auto CanT = T.getCanonicalType().getTypePtr();
  for (auto &I : Exts)
    OpenCLTypeExtMap[CanT].insert(I.str());
}

void Sema::setOpenCLExtensionForDecl(Decl *FD, StringRef ExtStr) {
  llvm::SmallVector<StringRef, 1> Exts;
  ExtStr.split(Exts, " ", /* limit */ -1, /* keep empty */ false);
  if (Exts.empty())
    return;
  for (auto &I : Exts)
    OpenCLDeclExtMap[FD].insert(I.str());
}

void Sema::setCurrentOpenCLExtensionForType(QualType T) {
  if (CurrOpenCLExtension.empty())
    return;
  setOpenCLExtensionForType(T, CurrOpenCLExtension);
}

void Sema::setCurrentOpenCLExtensionForDecl(Decl *D) {
  if (CurrOpenCLExtension.empty())
    return;
  setOpenCLExtensionForDecl(D, CurrOpenCLExtension);
}

std::string Sema::getOpenCLExtensionsFromDeclExtMap(FunctionDecl *FD) {
  if (!OpenCLDeclExtMap.empty())
    return getOpenCLExtensionsFromExtMap(FD, OpenCLDeclExtMap);

  return "";
}

std::string Sema::getOpenCLExtensionsFromTypeExtMap(FunctionType *FT) {
  if (!OpenCLTypeExtMap.empty())
    return getOpenCLExtensionsFromExtMap(FT, OpenCLTypeExtMap);

  return "";
}

template <typename T, typename MapT>
std::string Sema::getOpenCLExtensionsFromExtMap(T *FDT, MapT &Map) {
  std::string ExtensionNames = "";
  auto Loc = Map.find(FDT);

  for (auto const& I : Loc->second) {
    ExtensionNames += I;
    ExtensionNames += " ";
  }
  ExtensionNames.pop_back();

  return ExtensionNames;
}

bool Sema::isOpenCLDisabledDecl(Decl *FD) {
  auto Loc = OpenCLDeclExtMap.find(FD);
  if (Loc == OpenCLDeclExtMap.end())
    return false;
  for (auto &I : Loc->second) {
    if (!getOpenCLOptions().isEnabled(I))
      return true;
  }
  return false;
}

template <typename T, typename DiagLocT, typename DiagInfoT, typename MapT>
bool Sema::checkOpenCLDisabledTypeOrDecl(T D, DiagLocT DiagLoc,
                                         DiagInfoT DiagInfo, MapT &Map,
                                         unsigned Selector,
                                         SourceRange SrcRange) {
  auto Loc = Map.find(D);
  if (Loc == Map.end())
    return false;
  bool Disabled = false;
  for (auto &I : Loc->second) {
    if (I != CurrOpenCLExtension && !getOpenCLOptions().isEnabled(I)) {
      Diag(DiagLoc, diag::err_opencl_requires_extension) << Selector << DiagInfo
                                                         << I << SrcRange;
      Disabled = true;
    }
  }
  return Disabled;
}

bool Sema::checkOpenCLDisabledTypeDeclSpec(const DeclSpec &DS, QualType QT) {
  // Check extensions for declared types.
  Decl *Decl = nullptr;
  if (auto TypedefT = dyn_cast<TypedefType>(QT.getTypePtr()))
    Decl = TypedefT->getDecl();
  if (auto TagT = dyn_cast<TagType>(QT.getCanonicalType().getTypePtr()))
    Decl = TagT->getDecl();
  auto Loc = DS.getTypeSpecTypeLoc();

  // Check extensions for vector types.
  // e.g. double4 is not allowed when cl_khr_fp64 is absent.
  if (QT->isExtVectorType()) {
    auto TypePtr = QT->castAs<ExtVectorType>()->getElementType().getTypePtr();
    return checkOpenCLDisabledTypeOrDecl(TypePtr, Loc, QT, OpenCLTypeExtMap);
  }

  if (checkOpenCLDisabledTypeOrDecl(Decl, Loc, QT, OpenCLDeclExtMap))
    return true;

  // Check extensions for builtin types.
  return checkOpenCLDisabledTypeOrDecl(QT.getCanonicalType().getTypePtr(), Loc,
                                       QT, OpenCLTypeExtMap);
}

bool Sema::checkOpenCLDisabledDecl(const NamedDecl &D, const Expr &E) {
  IdentifierInfo *FnName = D.getIdentifier();
  return checkOpenCLDisabledTypeOrDecl(&D, E.getBeginLoc(), FnName,
                                       OpenCLDeclExtMap, 1, D.getSourceRange());
}
