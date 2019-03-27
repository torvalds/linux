//===--- Expr.cpp - Expression AST Node Implementation --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Expr class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/LiteralSupport.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstring>
using namespace clang;

const Expr *Expr::getBestDynamicClassTypeExpr() const {
  const Expr *E = this;
  while (true) {
    E = E->ignoreParenBaseCasts();

    // Follow the RHS of a comma operator.
    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Comma) {
        E = BO->getRHS();
        continue;
      }
    }

    // Step into initializer for materialized temporaries.
    if (auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = MTE->GetTemporaryExpr();
      continue;
    }

    break;
  }

  return E;
}

const CXXRecordDecl *Expr::getBestDynamicClassType() const {
  const Expr *E = getBestDynamicClassTypeExpr();
  QualType DerivedType = E->getType();
  if (const PointerType *PTy = DerivedType->getAs<PointerType>())
    DerivedType = PTy->getPointeeType();

  if (DerivedType->isDependentType())
    return nullptr;

  const RecordType *Ty = DerivedType->castAs<RecordType>();
  Decl *D = Ty->getDecl();
  return cast<CXXRecordDecl>(D);
}

const Expr *Expr::skipRValueSubobjectAdjustments(
    SmallVectorImpl<const Expr *> &CommaLHSs,
    SmallVectorImpl<SubobjectAdjustment> &Adjustments) const {
  const Expr *E = this;
  while (true) {
    E = E->IgnoreParens();

    if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
      if ((CE->getCastKind() == CK_DerivedToBase ||
           CE->getCastKind() == CK_UncheckedDerivedToBase) &&
          E->getType()->isRecordType()) {
        E = CE->getSubExpr();
        CXXRecordDecl *Derived
          = cast<CXXRecordDecl>(E->getType()->getAs<RecordType>()->getDecl());
        Adjustments.push_back(SubobjectAdjustment(CE, Derived));
        continue;
      }

      if (CE->getCastKind() == CK_NoOp) {
        E = CE->getSubExpr();
        continue;
      }
    } else if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
      if (!ME->isArrow()) {
        assert(ME->getBase()->getType()->isRecordType());
        if (FieldDecl *Field = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
          if (!Field->isBitField() && !Field->getType()->isReferenceType()) {
            E = ME->getBase();
            Adjustments.push_back(SubobjectAdjustment(Field));
            continue;
          }
        }
      }
    } else if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_PtrMemD) {
        assert(BO->getRHS()->isRValue());
        E = BO->getLHS();
        const MemberPointerType *MPT =
          BO->getRHS()->getType()->getAs<MemberPointerType>();
        Adjustments.push_back(SubobjectAdjustment(MPT, BO->getRHS()));
        continue;
      } else if (BO->getOpcode() == BO_Comma) {
        CommaLHSs.push_back(BO->getLHS());
        E = BO->getRHS();
        continue;
      }
    }

    // Nothing changed.
    break;
  }
  return E;
}

/// isKnownToHaveBooleanValue - Return true if this is an integer expression
/// that is known to return 0 or 1.  This happens for _Bool/bool expressions
/// but also int expressions which are produced by things like comparisons in
/// C.
bool Expr::isKnownToHaveBooleanValue() const {
  const Expr *E = IgnoreParens();

  // If this value has _Bool type, it is obvious 0/1.
  if (E->getType()->isBooleanType()) return true;
  // If this is a non-scalar-integer type, we don't care enough to try.
  if (!E->getType()->isIntegralOrEnumerationType()) return false;

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    switch (UO->getOpcode()) {
    case UO_Plus:
      return UO->getSubExpr()->isKnownToHaveBooleanValue();
    case UO_LNot:
      return true;
    default:
      return false;
    }
  }

  // Only look through implicit casts.  If the user writes
  // '(int) (a && b)' treat it as an arbitrary int.
  if (const ImplicitCastExpr *CE = dyn_cast<ImplicitCastExpr>(E))
    return CE->getSubExpr()->isKnownToHaveBooleanValue();

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    switch (BO->getOpcode()) {
    default: return false;
    case BO_LT:   // Relational operators.
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:   // Equality operators.
    case BO_NE:
    case BO_LAnd: // AND operator.
    case BO_LOr:  // Logical OR operator.
      return true;

    case BO_And:  // Bitwise AND operator.
    case BO_Xor:  // Bitwise XOR operator.
    case BO_Or:   // Bitwise OR operator.
      // Handle things like (x==2)|(y==12).
      return BO->getLHS()->isKnownToHaveBooleanValue() &&
             BO->getRHS()->isKnownToHaveBooleanValue();

    case BO_Comma:
    case BO_Assign:
      return BO->getRHS()->isKnownToHaveBooleanValue();
    }
  }

  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E))
    return CO->getTrueExpr()->isKnownToHaveBooleanValue() &&
           CO->getFalseExpr()->isKnownToHaveBooleanValue();

  return false;
}

// Amusing macro metaprogramming hack: check whether a class provides
// a more specific implementation of getExprLoc().
//
// See also Stmt.cpp:{getBeginLoc(),getEndLoc()}.
namespace {
  /// This implementation is used when a class provides a custom
  /// implementation of getExprLoc.
  template <class E, class T>
  SourceLocation getExprLocImpl(const Expr *expr,
                                SourceLocation (T::*v)() const) {
    return static_cast<const E*>(expr)->getExprLoc();
  }

  /// This implementation is used when a class doesn't provide
  /// a custom implementation of getExprLoc.  Overload resolution
  /// should pick it over the implementation above because it's
  /// more specialized according to function template partial ordering.
  template <class E>
  SourceLocation getExprLocImpl(const Expr *expr,
                                SourceLocation (Expr::*v)() const) {
    return static_cast<const E *>(expr)->getBeginLoc();
  }
}

SourceLocation Expr::getExprLoc() const {
  switch (getStmtClass()) {
  case Stmt::NoStmtClass: llvm_unreachable("statement without class");
#define ABSTRACT_STMT(type)
#define STMT(type, base) \
  case Stmt::type##Class: break;
#define EXPR(type, base) \
  case Stmt::type##Class: return getExprLocImpl<type>(this, &type::getExprLoc);
#include "clang/AST/StmtNodes.inc"
  }
  llvm_unreachable("unknown expression kind");
}

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

/// Compute the type-, value-, and instantiation-dependence of a
/// declaration reference
/// based on the declaration being referenced.
static void computeDeclRefDependence(const ASTContext &Ctx, NamedDecl *D,
                                     QualType T, bool &TypeDependent,
                                     bool &ValueDependent,
                                     bool &InstantiationDependent) {
  TypeDependent = false;
  ValueDependent = false;
  InstantiationDependent = false;

  // (TD) C++ [temp.dep.expr]p3:
  //   An id-expression is type-dependent if it contains:
  //
  // and
  //
  // (VD) C++ [temp.dep.constexpr]p2:
  //  An identifier is value-dependent if it is:

  //  (TD)  - an identifier that was declared with dependent type
  //  (VD)  - a name declared with a dependent type,
  if (T->isDependentType()) {
    TypeDependent = true;
    ValueDependent = true;
    InstantiationDependent = true;
    return;
  } else if (T->isInstantiationDependentType()) {
    InstantiationDependent = true;
  }

  //  (TD)  - a conversion-function-id that specifies a dependent type
  if (D->getDeclName().getNameKind()
                                == DeclarationName::CXXConversionFunctionName) {
    QualType T = D->getDeclName().getCXXNameType();
    if (T->isDependentType()) {
      TypeDependent = true;
      ValueDependent = true;
      InstantiationDependent = true;
      return;
    }

    if (T->isInstantiationDependentType())
      InstantiationDependent = true;
  }

  //  (VD)  - the name of a non-type template parameter,
  if (isa<NonTypeTemplateParmDecl>(D)) {
    ValueDependent = true;
    InstantiationDependent = true;
    return;
  }

  //  (VD) - a constant with integral or enumeration type and is
  //         initialized with an expression that is value-dependent.
  //  (VD) - a constant with literal type and is initialized with an
  //         expression that is value-dependent [C++11].
  //  (VD) - FIXME: Missing from the standard:
  //       -  an entity with reference type and is initialized with an
  //          expression that is value-dependent [C++11]
  if (VarDecl *Var = dyn_cast<VarDecl>(D)) {
    if ((Ctx.getLangOpts().CPlusPlus11 ?
           Var->getType()->isLiteralType(Ctx) :
           Var->getType()->isIntegralOrEnumerationType()) &&
        (Var->getType().isConstQualified() ||
         Var->getType()->isReferenceType())) {
      if (const Expr *Init = Var->getAnyInitializer())
        if (Init->isValueDependent()) {
          ValueDependent = true;
          InstantiationDependent = true;
        }
    }

    // (VD) - FIXME: Missing from the standard:
    //      -  a member function or a static data member of the current
    //         instantiation
    if (Var->isStaticDataMember() &&
        Var->getDeclContext()->isDependentContext()) {
      ValueDependent = true;
      InstantiationDependent = true;
      TypeSourceInfo *TInfo = Var->getFirstDecl()->getTypeSourceInfo();
      if (TInfo->getType()->isIncompleteArrayType())
        TypeDependent = true;
    }

    return;
  }

  // (VD) - FIXME: Missing from the standard:
  //      -  a member function or a static data member of the current
  //         instantiation
  if (isa<CXXMethodDecl>(D) && D->getDeclContext()->isDependentContext()) {
    ValueDependent = true;
    InstantiationDependent = true;
  }
}

void DeclRefExpr::computeDependence(const ASTContext &Ctx) {
  bool TypeDependent = false;
  bool ValueDependent = false;
  bool InstantiationDependent = false;
  computeDeclRefDependence(Ctx, getDecl(), getType(), TypeDependent,
                           ValueDependent, InstantiationDependent);

  ExprBits.TypeDependent |= TypeDependent;
  ExprBits.ValueDependent |= ValueDependent;
  ExprBits.InstantiationDependent |= InstantiationDependent;

  // Is the declaration a parameter pack?
  if (getDecl()->isParameterPack())
    ExprBits.ContainsUnexpandedParameterPack = true;
}

DeclRefExpr::DeclRefExpr(const ASTContext &Ctx, ValueDecl *D,
                         bool RefersToEnclosingVariableOrCapture, QualType T,
                         ExprValueKind VK, SourceLocation L,
                         const DeclarationNameLoc &LocInfo)
    : Expr(DeclRefExprClass, T, VK, OK_Ordinary, false, false, false, false),
      D(D), DNLoc(LocInfo) {
  DeclRefExprBits.HasQualifier = false;
  DeclRefExprBits.HasTemplateKWAndArgsInfo = false;
  DeclRefExprBits.HasFoundDecl = false;
  DeclRefExprBits.HadMultipleCandidates = false;
  DeclRefExprBits.RefersToEnclosingVariableOrCapture =
      RefersToEnclosingVariableOrCapture;
  DeclRefExprBits.Loc = L;
  computeDependence(Ctx);
}

DeclRefExpr::DeclRefExpr(const ASTContext &Ctx,
                         NestedNameSpecifierLoc QualifierLoc,
                         SourceLocation TemplateKWLoc, ValueDecl *D,
                         bool RefersToEnclosingVariableOrCapture,
                         const DeclarationNameInfo &NameInfo, NamedDecl *FoundD,
                         const TemplateArgumentListInfo *TemplateArgs,
                         QualType T, ExprValueKind VK)
    : Expr(DeclRefExprClass, T, VK, OK_Ordinary, false, false, false, false),
      D(D), DNLoc(NameInfo.getInfo()) {
  DeclRefExprBits.Loc = NameInfo.getLoc();
  DeclRefExprBits.HasQualifier = QualifierLoc ? 1 : 0;
  if (QualifierLoc) {
    new (getTrailingObjects<NestedNameSpecifierLoc>())
        NestedNameSpecifierLoc(QualifierLoc);
    auto *NNS = QualifierLoc.getNestedNameSpecifier();
    if (NNS->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (NNS->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;
  }
  DeclRefExprBits.HasFoundDecl = FoundD ? 1 : 0;
  if (FoundD)
    *getTrailingObjects<NamedDecl *>() = FoundD;
  DeclRefExprBits.HasTemplateKWAndArgsInfo
    = (TemplateArgs || TemplateKWLoc.isValid()) ? 1 : 0;
  DeclRefExprBits.RefersToEnclosingVariableOrCapture =
      RefersToEnclosingVariableOrCapture;
  if (TemplateArgs) {
    bool Dependent = false;
    bool InstantiationDependent = false;
    bool ContainsUnexpandedParameterPack = false;
    getTrailingObjects<ASTTemplateKWAndArgsInfo>()->initializeFrom(
        TemplateKWLoc, *TemplateArgs, getTrailingObjects<TemplateArgumentLoc>(),
        Dependent, InstantiationDependent, ContainsUnexpandedParameterPack);
    assert(!Dependent && "built a DeclRefExpr with dependent template args");
    ExprBits.InstantiationDependent |= InstantiationDependent;
    ExprBits.ContainsUnexpandedParameterPack |= ContainsUnexpandedParameterPack;
  } else if (TemplateKWLoc.isValid()) {
    getTrailingObjects<ASTTemplateKWAndArgsInfo>()->initializeFrom(
        TemplateKWLoc);
  }
  DeclRefExprBits.HadMultipleCandidates = 0;

  computeDependence(Ctx);
}

DeclRefExpr *DeclRefExpr::Create(const ASTContext &Context,
                                 NestedNameSpecifierLoc QualifierLoc,
                                 SourceLocation TemplateKWLoc,
                                 ValueDecl *D,
                                 bool RefersToEnclosingVariableOrCapture,
                                 SourceLocation NameLoc,
                                 QualType T,
                                 ExprValueKind VK,
                                 NamedDecl *FoundD,
                                 const TemplateArgumentListInfo *TemplateArgs) {
  return Create(Context, QualifierLoc, TemplateKWLoc, D,
                RefersToEnclosingVariableOrCapture,
                DeclarationNameInfo(D->getDeclName(), NameLoc),
                T, VK, FoundD, TemplateArgs);
}

DeclRefExpr *DeclRefExpr::Create(const ASTContext &Context,
                                 NestedNameSpecifierLoc QualifierLoc,
                                 SourceLocation TemplateKWLoc,
                                 ValueDecl *D,
                                 bool RefersToEnclosingVariableOrCapture,
                                 const DeclarationNameInfo &NameInfo,
                                 QualType T,
                                 ExprValueKind VK,
                                 NamedDecl *FoundD,
                                 const TemplateArgumentListInfo *TemplateArgs) {
  // Filter out cases where the found Decl is the same as the value refenenced.
  if (D == FoundD)
    FoundD = nullptr;

  bool HasTemplateKWAndArgsInfo = TemplateArgs || TemplateKWLoc.isValid();
  std::size_t Size =
      totalSizeToAlloc<NestedNameSpecifierLoc, NamedDecl *,
                       ASTTemplateKWAndArgsInfo, TemplateArgumentLoc>(
          QualifierLoc ? 1 : 0, FoundD ? 1 : 0,
          HasTemplateKWAndArgsInfo ? 1 : 0,
          TemplateArgs ? TemplateArgs->size() : 0);

  void *Mem = Context.Allocate(Size, alignof(DeclRefExpr));
  return new (Mem) DeclRefExpr(Context, QualifierLoc, TemplateKWLoc, D,
                               RefersToEnclosingVariableOrCapture,
                               NameInfo, FoundD, TemplateArgs, T, VK);
}

DeclRefExpr *DeclRefExpr::CreateEmpty(const ASTContext &Context,
                                      bool HasQualifier,
                                      bool HasFoundDecl,
                                      bool HasTemplateKWAndArgsInfo,
                                      unsigned NumTemplateArgs) {
  assert(NumTemplateArgs == 0 || HasTemplateKWAndArgsInfo);
  std::size_t Size =
      totalSizeToAlloc<NestedNameSpecifierLoc, NamedDecl *,
                       ASTTemplateKWAndArgsInfo, TemplateArgumentLoc>(
          HasQualifier ? 1 : 0, HasFoundDecl ? 1 : 0, HasTemplateKWAndArgsInfo,
          NumTemplateArgs);
  void *Mem = Context.Allocate(Size, alignof(DeclRefExpr));
  return new (Mem) DeclRefExpr(EmptyShell());
}

SourceLocation DeclRefExpr::getBeginLoc() const {
  if (hasQualifier())
    return getQualifierLoc().getBeginLoc();
  return getNameInfo().getBeginLoc();
}
SourceLocation DeclRefExpr::getEndLoc() const {
  if (hasExplicitTemplateArgs())
    return getRAngleLoc();
  return getNameInfo().getEndLoc();
}

PredefinedExpr::PredefinedExpr(SourceLocation L, QualType FNTy, IdentKind IK,
                               StringLiteral *SL)
    : Expr(PredefinedExprClass, FNTy, VK_LValue, OK_Ordinary,
           FNTy->isDependentType(), FNTy->isDependentType(),
           FNTy->isInstantiationDependentType(),
           /*ContainsUnexpandedParameterPack=*/false) {
  PredefinedExprBits.Kind = IK;
  assert((getIdentKind() == IK) &&
         "IdentKind do not fit in PredefinedExprBitfields!");
  bool HasFunctionName = SL != nullptr;
  PredefinedExprBits.HasFunctionName = HasFunctionName;
  PredefinedExprBits.Loc = L;
  if (HasFunctionName)
    setFunctionName(SL);
}

PredefinedExpr::PredefinedExpr(EmptyShell Empty, bool HasFunctionName)
    : Expr(PredefinedExprClass, Empty) {
  PredefinedExprBits.HasFunctionName = HasFunctionName;
}

PredefinedExpr *PredefinedExpr::Create(const ASTContext &Ctx, SourceLocation L,
                                       QualType FNTy, IdentKind IK,
                                       StringLiteral *SL) {
  bool HasFunctionName = SL != nullptr;
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Stmt *>(HasFunctionName),
                           alignof(PredefinedExpr));
  return new (Mem) PredefinedExpr(L, FNTy, IK, SL);
}

PredefinedExpr *PredefinedExpr::CreateEmpty(const ASTContext &Ctx,
                                            bool HasFunctionName) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Stmt *>(HasFunctionName),
                           alignof(PredefinedExpr));
  return new (Mem) PredefinedExpr(EmptyShell(), HasFunctionName);
}

StringRef PredefinedExpr::getIdentKindName(PredefinedExpr::IdentKind IK) {
  switch (IK) {
  case Func:
    return "__func__";
  case Function:
    return "__FUNCTION__";
  case FuncDName:
    return "__FUNCDNAME__";
  case LFunction:
    return "L__FUNCTION__";
  case PrettyFunction:
    return "__PRETTY_FUNCTION__";
  case FuncSig:
    return "__FUNCSIG__";
  case LFuncSig:
    return "L__FUNCSIG__";
  case PrettyFunctionNoVirtual:
    break;
  }
  llvm_unreachable("Unknown ident kind for PredefinedExpr");
}

// FIXME: Maybe this should use DeclPrinter with a special "print predefined
// expr" policy instead.
std::string PredefinedExpr::ComputeName(IdentKind IK, const Decl *CurrentDecl) {
  ASTContext &Context = CurrentDecl->getASTContext();

  if (IK == PredefinedExpr::FuncDName) {
    if (const NamedDecl *ND = dyn_cast<NamedDecl>(CurrentDecl)) {
      std::unique_ptr<MangleContext> MC;
      MC.reset(Context.createMangleContext());

      if (MC->shouldMangleDeclName(ND)) {
        SmallString<256> Buffer;
        llvm::raw_svector_ostream Out(Buffer);
        if (const CXXConstructorDecl *CD = dyn_cast<CXXConstructorDecl>(ND))
          MC->mangleCXXCtor(CD, Ctor_Base, Out);
        else if (const CXXDestructorDecl *DD = dyn_cast<CXXDestructorDecl>(ND))
          MC->mangleCXXDtor(DD, Dtor_Base, Out);
        else
          MC->mangleName(ND, Out);

        if (!Buffer.empty() && Buffer.front() == '\01')
          return Buffer.substr(1);
        return Buffer.str();
      } else
        return ND->getIdentifier()->getName();
    }
    return "";
  }
  if (isa<BlockDecl>(CurrentDecl)) {
    // For blocks we only emit something if it is enclosed in a function
    // For top-level block we'd like to include the name of variable, but we
    // don't have it at this point.
    auto DC = CurrentDecl->getDeclContext();
    if (DC->isFileContext())
      return "";

    SmallString<256> Buffer;
    llvm::raw_svector_ostream Out(Buffer);
    if (auto *DCBlock = dyn_cast<BlockDecl>(DC))
      // For nested blocks, propagate up to the parent.
      Out << ComputeName(IK, DCBlock);
    else if (auto *DCDecl = dyn_cast<Decl>(DC))
      Out << ComputeName(IK, DCDecl) << "_block_invoke";
    return Out.str();
  }
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(CurrentDecl)) {
    if (IK != PrettyFunction && IK != PrettyFunctionNoVirtual &&
        IK != FuncSig && IK != LFuncSig)
      return FD->getNameAsString();

    SmallString<256> Name;
    llvm::raw_svector_ostream Out(Name);

    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (MD->isVirtual() && IK != PrettyFunctionNoVirtual)
        Out << "virtual ";
      if (MD->isStatic())
        Out << "static ";
    }

    PrintingPolicy Policy(Context.getLangOpts());
    std::string Proto;
    llvm::raw_string_ostream POut(Proto);

    const FunctionDecl *Decl = FD;
    if (const FunctionDecl* Pattern = FD->getTemplateInstantiationPattern())
      Decl = Pattern;
    const FunctionType *AFT = Decl->getType()->getAs<FunctionType>();
    const FunctionProtoType *FT = nullptr;
    if (FD->hasWrittenPrototype())
      FT = dyn_cast<FunctionProtoType>(AFT);

    if (IK == FuncSig || IK == LFuncSig) {
      switch (AFT->getCallConv()) {
      case CC_C: POut << "__cdecl "; break;
      case CC_X86StdCall: POut << "__stdcall "; break;
      case CC_X86FastCall: POut << "__fastcall "; break;
      case CC_X86ThisCall: POut << "__thiscall "; break;
      case CC_X86VectorCall: POut << "__vectorcall "; break;
      case CC_X86RegCall: POut << "__regcall "; break;
      // Only bother printing the conventions that MSVC knows about.
      default: break;
      }
    }

    FD->printQualifiedName(POut, Policy);

    POut << "(";
    if (FT) {
      for (unsigned i = 0, e = Decl->getNumParams(); i != e; ++i) {
        if (i) POut << ", ";
        POut << Decl->getParamDecl(i)->getType().stream(Policy);
      }

      if (FT->isVariadic()) {
        if (FD->getNumParams()) POut << ", ";
        POut << "...";
      } else if ((IK == FuncSig || IK == LFuncSig ||
                  !Context.getLangOpts().CPlusPlus) &&
                 !Decl->getNumParams()) {
        POut << "void";
      }
    }
    POut << ")";

    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
      assert(FT && "We must have a written prototype in this case.");
      if (FT->isConst())
        POut << " const";
      if (FT->isVolatile())
        POut << " volatile";
      RefQualifierKind Ref = MD->getRefQualifier();
      if (Ref == RQ_LValue)
        POut << " &";
      else if (Ref == RQ_RValue)
        POut << " &&";
    }

    typedef SmallVector<const ClassTemplateSpecializationDecl *, 8> SpecsTy;
    SpecsTy Specs;
    const DeclContext *Ctx = FD->getDeclContext();
    while (Ctx && isa<NamedDecl>(Ctx)) {
      const ClassTemplateSpecializationDecl *Spec
                               = dyn_cast<ClassTemplateSpecializationDecl>(Ctx);
      if (Spec && !Spec->isExplicitSpecialization())
        Specs.push_back(Spec);
      Ctx = Ctx->getParent();
    }

    std::string TemplateParams;
    llvm::raw_string_ostream TOut(TemplateParams);
    for (SpecsTy::reverse_iterator I = Specs.rbegin(), E = Specs.rend();
         I != E; ++I) {
      const TemplateParameterList *Params
                  = (*I)->getSpecializedTemplate()->getTemplateParameters();
      const TemplateArgumentList &Args = (*I)->getTemplateArgs();
      assert(Params->size() == Args.size());
      for (unsigned i = 0, numParams = Params->size(); i != numParams; ++i) {
        StringRef Param = Params->getParam(i)->getName();
        if (Param.empty()) continue;
        TOut << Param << " = ";
        Args.get(i).print(Policy, TOut);
        TOut << ", ";
      }
    }

    FunctionTemplateSpecializationInfo *FSI
                                          = FD->getTemplateSpecializationInfo();
    if (FSI && !FSI->isExplicitSpecialization()) {
      const TemplateParameterList* Params
                                  = FSI->getTemplate()->getTemplateParameters();
      const TemplateArgumentList* Args = FSI->TemplateArguments;
      assert(Params->size() == Args->size());
      for (unsigned i = 0, e = Params->size(); i != e; ++i) {
        StringRef Param = Params->getParam(i)->getName();
        if (Param.empty()) continue;
        TOut << Param << " = ";
        Args->get(i).print(Policy, TOut);
        TOut << ", ";
      }
    }

    TOut.flush();
    if (!TemplateParams.empty()) {
      // remove the trailing comma and space
      TemplateParams.resize(TemplateParams.size() - 2);
      POut << " [" << TemplateParams << "]";
    }

    POut.flush();

    // Print "auto" for all deduced return types. This includes C++1y return
    // type deduction and lambdas. For trailing return types resolve the
    // decltype expression. Otherwise print the real type when this is
    // not a constructor or destructor.
    if (isa<CXXMethodDecl>(FD) &&
         cast<CXXMethodDecl>(FD)->getParent()->isLambda())
      Proto = "auto " + Proto;
    else if (FT && FT->getReturnType()->getAs<DecltypeType>())
      FT->getReturnType()
          ->getAs<DecltypeType>()
          ->getUnderlyingType()
          .getAsStringInternal(Proto, Policy);
    else if (!isa<CXXConstructorDecl>(FD) && !isa<CXXDestructorDecl>(FD))
      AFT->getReturnType().getAsStringInternal(Proto, Policy);

    Out << Proto;

    return Name.str().str();
  }
  if (const CapturedDecl *CD = dyn_cast<CapturedDecl>(CurrentDecl)) {
    for (const DeclContext *DC = CD->getParent(); DC; DC = DC->getParent())
      // Skip to its enclosing function or method, but not its enclosing
      // CapturedDecl.
      if (DC->isFunctionOrMethod() && (DC->getDeclKind() != Decl::Captured)) {
        const Decl *D = Decl::castFromDeclContext(DC);
        return ComputeName(IK, D);
      }
    llvm_unreachable("CapturedDecl not inside a function or method");
  }
  if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(CurrentDecl)) {
    SmallString<256> Name;
    llvm::raw_svector_ostream Out(Name);
    Out << (MD->isInstanceMethod() ? '-' : '+');
    Out << '[';

    // For incorrect code, there might not be an ObjCInterfaceDecl.  Do
    // a null check to avoid a crash.
    if (const ObjCInterfaceDecl *ID = MD->getClassInterface())
      Out << *ID;

    if (const ObjCCategoryImplDecl *CID =
        dyn_cast<ObjCCategoryImplDecl>(MD->getDeclContext()))
      Out << '(' << *CID << ')';

    Out <<  ' ';
    MD->getSelector().print(Out);
    Out <<  ']';

    return Name.str().str();
  }
  if (isa<TranslationUnitDecl>(CurrentDecl) && IK == PrettyFunction) {
    // __PRETTY_FUNCTION__ -> "top level", the others produce an empty string.
    return "top level";
  }
  return "";
}

void APNumericStorage::setIntValue(const ASTContext &C,
                                   const llvm::APInt &Val) {
  if (hasAllocation())
    C.Deallocate(pVal);

  BitWidth = Val.getBitWidth();
  unsigned NumWords = Val.getNumWords();
  const uint64_t* Words = Val.getRawData();
  if (NumWords > 1) {
    pVal = new (C) uint64_t[NumWords];
    std::copy(Words, Words + NumWords, pVal);
  } else if (NumWords == 1)
    VAL = Words[0];
  else
    VAL = 0;
}

IntegerLiteral::IntegerLiteral(const ASTContext &C, const llvm::APInt &V,
                               QualType type, SourceLocation l)
  : Expr(IntegerLiteralClass, type, VK_RValue, OK_Ordinary, false, false,
         false, false),
    Loc(l) {
  assert(type->isIntegerType() && "Illegal type in IntegerLiteral");
  assert(V.getBitWidth() == C.getIntWidth(type) &&
         "Integer type is not the correct size for constant.");
  setValue(C, V);
}

IntegerLiteral *
IntegerLiteral::Create(const ASTContext &C, const llvm::APInt &V,
                       QualType type, SourceLocation l) {
  return new (C) IntegerLiteral(C, V, type, l);
}

IntegerLiteral *
IntegerLiteral::Create(const ASTContext &C, EmptyShell Empty) {
  return new (C) IntegerLiteral(Empty);
}

FixedPointLiteral::FixedPointLiteral(const ASTContext &C, const llvm::APInt &V,
                                     QualType type, SourceLocation l,
                                     unsigned Scale)
    : Expr(FixedPointLiteralClass, type, VK_RValue, OK_Ordinary, false, false,
           false, false),
      Loc(l), Scale(Scale) {
  assert(type->isFixedPointType() && "Illegal type in FixedPointLiteral");
  assert(V.getBitWidth() == C.getTypeInfo(type).Width &&
         "Fixed point type is not the correct size for constant.");
  setValue(C, V);
}

FixedPointLiteral *FixedPointLiteral::CreateFromRawInt(const ASTContext &C,
                                                       const llvm::APInt &V,
                                                       QualType type,
                                                       SourceLocation l,
                                                       unsigned Scale) {
  return new (C) FixedPointLiteral(C, V, type, l, Scale);
}

std::string FixedPointLiteral::getValueAsString(unsigned Radix) const {
  // Currently the longest decimal number that can be printed is the max for an
  // unsigned long _Accum: 4294967295.99999999976716935634613037109375
  // which is 43 characters.
  SmallString<64> S;
  FixedPointValueToString(
      S, llvm::APSInt::getUnsigned(getValue().getZExtValue()), Scale);
  return S.str();
}

FloatingLiteral::FloatingLiteral(const ASTContext &C, const llvm::APFloat &V,
                                 bool isexact, QualType Type, SourceLocation L)
  : Expr(FloatingLiteralClass, Type, VK_RValue, OK_Ordinary, false, false,
         false, false), Loc(L) {
  setSemantics(V.getSemantics());
  FloatingLiteralBits.IsExact = isexact;
  setValue(C, V);
}

FloatingLiteral::FloatingLiteral(const ASTContext &C, EmptyShell Empty)
  : Expr(FloatingLiteralClass, Empty) {
  setRawSemantics(IEEEhalf);
  FloatingLiteralBits.IsExact = false;
}

FloatingLiteral *
FloatingLiteral::Create(const ASTContext &C, const llvm::APFloat &V,
                        bool isexact, QualType Type, SourceLocation L) {
  return new (C) FloatingLiteral(C, V, isexact, Type, L);
}

FloatingLiteral *
FloatingLiteral::Create(const ASTContext &C, EmptyShell Empty) {
  return new (C) FloatingLiteral(C, Empty);
}

const llvm::fltSemantics &FloatingLiteral::getSemantics() const {
  switch(FloatingLiteralBits.Semantics) {
  case IEEEhalf:
    return llvm::APFloat::IEEEhalf();
  case IEEEsingle:
    return llvm::APFloat::IEEEsingle();
  case IEEEdouble:
    return llvm::APFloat::IEEEdouble();
  case x87DoubleExtended:
    return llvm::APFloat::x87DoubleExtended();
  case IEEEquad:
    return llvm::APFloat::IEEEquad();
  case PPCDoubleDouble:
    return llvm::APFloat::PPCDoubleDouble();
  }
  llvm_unreachable("Unrecognised floating semantics");
}

void FloatingLiteral::setSemantics(const llvm::fltSemantics &Sem) {
  if (&Sem == &llvm::APFloat::IEEEhalf())
    FloatingLiteralBits.Semantics = IEEEhalf;
  else if (&Sem == &llvm::APFloat::IEEEsingle())
    FloatingLiteralBits.Semantics = IEEEsingle;
  else if (&Sem == &llvm::APFloat::IEEEdouble())
    FloatingLiteralBits.Semantics = IEEEdouble;
  else if (&Sem == &llvm::APFloat::x87DoubleExtended())
    FloatingLiteralBits.Semantics = x87DoubleExtended;
  else if (&Sem == &llvm::APFloat::IEEEquad())
    FloatingLiteralBits.Semantics = IEEEquad;
  else if (&Sem == &llvm::APFloat::PPCDoubleDouble())
    FloatingLiteralBits.Semantics = PPCDoubleDouble;
  else
    llvm_unreachable("Unknown floating semantics");
}

/// getValueAsApproximateDouble - This returns the value as an inaccurate
/// double.  Note that this may cause loss of precision, but is useful for
/// debugging dumps, etc.
double FloatingLiteral::getValueAsApproximateDouble() const {
  llvm::APFloat V = getValue();
  bool ignored;
  V.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven,
            &ignored);
  return V.convertToDouble();
}

unsigned StringLiteral::mapCharByteWidth(TargetInfo const &Target,
                                         StringKind SK) {
  unsigned CharByteWidth = 0;
  switch (SK) {
  case Ascii:
  case UTF8:
    CharByteWidth = Target.getCharWidth();
    break;
  case Wide:
    CharByteWidth = Target.getWCharWidth();
    break;
  case UTF16:
    CharByteWidth = Target.getChar16Width();
    break;
  case UTF32:
    CharByteWidth = Target.getChar32Width();
    break;
  }
  assert((CharByteWidth & 7) == 0 && "Assumes character size is byte multiple");
  CharByteWidth /= 8;
  assert((CharByteWidth == 1 || CharByteWidth == 2 || CharByteWidth == 4) &&
         "The only supported character byte widths are 1,2 and 4!");
  return CharByteWidth;
}

StringLiteral::StringLiteral(const ASTContext &Ctx, StringRef Str,
                             StringKind Kind, bool Pascal, QualType Ty,
                             const SourceLocation *Loc,
                             unsigned NumConcatenated)
    : Expr(StringLiteralClass, Ty, VK_LValue, OK_Ordinary, false, false, false,
           false) {
  assert(Ctx.getAsConstantArrayType(Ty) &&
         "StringLiteral must be of constant array type!");
  unsigned CharByteWidth = mapCharByteWidth(Ctx.getTargetInfo(), Kind);
  unsigned ByteLength = Str.size();
  assert((ByteLength % CharByteWidth == 0) &&
         "The size of the data must be a multiple of CharByteWidth!");

  // Avoid the expensive division. The compiler should be able to figure it
  // out by itself. However as of clang 7, even with the appropriate
  // llvm_unreachable added just here, it is not able to do so.
  unsigned Length;
  switch (CharByteWidth) {
  case 1:
    Length = ByteLength;
    break;
  case 2:
    Length = ByteLength / 2;
    break;
  case 4:
    Length = ByteLength / 4;
    break;
  default:
    llvm_unreachable("Unsupported character width!");
  }

  StringLiteralBits.Kind = Kind;
  StringLiteralBits.CharByteWidth = CharByteWidth;
  StringLiteralBits.IsPascal = Pascal;
  StringLiteralBits.NumConcatenated = NumConcatenated;
  *getTrailingObjects<unsigned>() = Length;

  // Initialize the trailing array of SourceLocation.
  // This is safe since SourceLocation is POD-like.
  std::memcpy(getTrailingObjects<SourceLocation>(), Loc,
              NumConcatenated * sizeof(SourceLocation));

  // Initialize the trailing array of char holding the string data.
  std::memcpy(getTrailingObjects<char>(), Str.data(), ByteLength);
}

StringLiteral::StringLiteral(EmptyShell Empty, unsigned NumConcatenated,
                             unsigned Length, unsigned CharByteWidth)
    : Expr(StringLiteralClass, Empty) {
  StringLiteralBits.CharByteWidth = CharByteWidth;
  StringLiteralBits.NumConcatenated = NumConcatenated;
  *getTrailingObjects<unsigned>() = Length;
}

StringLiteral *StringLiteral::Create(const ASTContext &Ctx, StringRef Str,
                                     StringKind Kind, bool Pascal, QualType Ty,
                                     const SourceLocation *Loc,
                                     unsigned NumConcatenated) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<unsigned, SourceLocation, char>(
                               1, NumConcatenated, Str.size()),
                           alignof(StringLiteral));
  return new (Mem)
      StringLiteral(Ctx, Str, Kind, Pascal, Ty, Loc, NumConcatenated);
}

StringLiteral *StringLiteral::CreateEmpty(const ASTContext &Ctx,
                                          unsigned NumConcatenated,
                                          unsigned Length,
                                          unsigned CharByteWidth) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<unsigned, SourceLocation, char>(
                               1, NumConcatenated, Length * CharByteWidth),
                           alignof(StringLiteral));
  return new (Mem)
      StringLiteral(EmptyShell(), NumConcatenated, Length, CharByteWidth);
}

void StringLiteral::outputString(raw_ostream &OS) const {
  switch (getKind()) {
  case Ascii: break; // no prefix.
  case Wide:  OS << 'L'; break;
  case UTF8:  OS << "u8"; break;
  case UTF16: OS << 'u'; break;
  case UTF32: OS << 'U'; break;
  }
  OS << '"';
  static const char Hex[] = "0123456789ABCDEF";

  unsigned LastSlashX = getLength();
  for (unsigned I = 0, N = getLength(); I != N; ++I) {
    switch (uint32_t Char = getCodeUnit(I)) {
    default:
      // FIXME: Convert UTF-8 back to codepoints before rendering.

      // Convert UTF-16 surrogate pairs back to codepoints before rendering.
      // Leave invalid surrogates alone; we'll use \x for those.
      if (getKind() == UTF16 && I != N - 1 && Char >= 0xd800 &&
          Char <= 0xdbff) {
        uint32_t Trail = getCodeUnit(I + 1);
        if (Trail >= 0xdc00 && Trail <= 0xdfff) {
          Char = 0x10000 + ((Char - 0xd800) << 10) + (Trail - 0xdc00);
          ++I;
        }
      }

      if (Char > 0xff) {
        // If this is a wide string, output characters over 0xff using \x
        // escapes. Otherwise, this is a UTF-16 or UTF-32 string, and Char is a
        // codepoint: use \x escapes for invalid codepoints.
        if (getKind() == Wide ||
            (Char >= 0xd800 && Char <= 0xdfff) || Char >= 0x110000) {
          // FIXME: Is this the best way to print wchar_t?
          OS << "\\x";
          int Shift = 28;
          while ((Char >> Shift) == 0)
            Shift -= 4;
          for (/**/; Shift >= 0; Shift -= 4)
            OS << Hex[(Char >> Shift) & 15];
          LastSlashX = I;
          break;
        }

        if (Char > 0xffff)
          OS << "\\U00"
             << Hex[(Char >> 20) & 15]
             << Hex[(Char >> 16) & 15];
        else
          OS << "\\u";
        OS << Hex[(Char >> 12) & 15]
           << Hex[(Char >>  8) & 15]
           << Hex[(Char >>  4) & 15]
           << Hex[(Char >>  0) & 15];
        break;
      }

      // If we used \x... for the previous character, and this character is a
      // hexadecimal digit, prevent it being slurped as part of the \x.
      if (LastSlashX + 1 == I) {
        switch (Char) {
          case '0': case '1': case '2': case '3': case '4':
          case '5': case '6': case '7': case '8': case '9':
          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
          case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            OS << "\"\"";
        }
      }

      assert(Char <= 0xff &&
             "Characters above 0xff should already have been handled.");

      if (isPrintable(Char))
        OS << (char)Char;
      else  // Output anything hard as an octal escape.
        OS << '\\'
           << (char)('0' + ((Char >> 6) & 7))
           << (char)('0' + ((Char >> 3) & 7))
           << (char)('0' + ((Char >> 0) & 7));
      break;
    // Handle some common non-printable cases to make dumps prettier.
    case '\\': OS << "\\\\"; break;
    case '"': OS << "\\\""; break;
    case '\a': OS << "\\a"; break;
    case '\b': OS << "\\b"; break;
    case '\f': OS << "\\f"; break;
    case '\n': OS << "\\n"; break;
    case '\r': OS << "\\r"; break;
    case '\t': OS << "\\t"; break;
    case '\v': OS << "\\v"; break;
    }
  }
  OS << '"';
}

/// getLocationOfByte - Return a source location that points to the specified
/// byte of this string literal.
///
/// Strings are amazingly complex.  They can be formed from multiple tokens and
/// can have escape sequences in them in addition to the usual trigraph and
/// escaped newline business.  This routine handles this complexity.
///
/// The *StartToken sets the first token to be searched in this function and
/// the *StartTokenByteOffset is the byte offset of the first token. Before
/// returning, it updates the *StartToken to the TokNo of the token being found
/// and sets *StartTokenByteOffset to the byte offset of the token in the
/// string.
/// Using these two parameters can reduce the time complexity from O(n^2) to
/// O(n) if one wants to get the location of byte for all the tokens in a
/// string.
///
SourceLocation
StringLiteral::getLocationOfByte(unsigned ByteNo, const SourceManager &SM,
                                 const LangOptions &Features,
                                 const TargetInfo &Target, unsigned *StartToken,
                                 unsigned *StartTokenByteOffset) const {
  assert((getKind() == StringLiteral::Ascii ||
          getKind() == StringLiteral::UTF8) &&
         "Only narrow string literals are currently supported");

  // Loop over all of the tokens in this string until we find the one that
  // contains the byte we're looking for.
  unsigned TokNo = 0;
  unsigned StringOffset = 0;
  if (StartToken)
    TokNo = *StartToken;
  if (StartTokenByteOffset) {
    StringOffset = *StartTokenByteOffset;
    ByteNo -= StringOffset;
  }
  while (1) {
    assert(TokNo < getNumConcatenated() && "Invalid byte number!");
    SourceLocation StrTokLoc = getStrTokenLoc(TokNo);

    // Get the spelling of the string so that we can get the data that makes up
    // the string literal, not the identifier for the macro it is potentially
    // expanded through.
    SourceLocation StrTokSpellingLoc = SM.getSpellingLoc(StrTokLoc);

    // Re-lex the token to get its length and original spelling.
    std::pair<FileID, unsigned> LocInfo =
        SM.getDecomposedLoc(StrTokSpellingLoc);
    bool Invalid = false;
    StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
    if (Invalid) {
      if (StartTokenByteOffset != nullptr)
        *StartTokenByteOffset = StringOffset;
      if (StartToken != nullptr)
        *StartToken = TokNo;
      return StrTokSpellingLoc;
    }

    const char *StrData = Buffer.data()+LocInfo.second;

    // Create a lexer starting at the beginning of this token.
    Lexer TheLexer(SM.getLocForStartOfFile(LocInfo.first), Features,
                   Buffer.begin(), StrData, Buffer.end());
    Token TheTok;
    TheLexer.LexFromRawLexer(TheTok);

    // Use the StringLiteralParser to compute the length of the string in bytes.
    StringLiteralParser SLP(TheTok, SM, Features, Target);
    unsigned TokNumBytes = SLP.GetStringLength();

    // If the byte is in this token, return the location of the byte.
    if (ByteNo < TokNumBytes ||
        (ByteNo == TokNumBytes && TokNo == getNumConcatenated() - 1)) {
      unsigned Offset = SLP.getOffsetOfStringByte(TheTok, ByteNo);

      // Now that we know the offset of the token in the spelling, use the
      // preprocessor to get the offset in the original source.
      if (StartTokenByteOffset != nullptr)
        *StartTokenByteOffset = StringOffset;
      if (StartToken != nullptr)
        *StartToken = TokNo;
      return Lexer::AdvanceToTokenCharacter(StrTokLoc, Offset, SM, Features);
    }

    // Move to the next string token.
    StringOffset += TokNumBytes;
    ++TokNo;
    ByteNo -= TokNumBytes;
  }
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "sizeof" or "[pre]++".
StringRef UnaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
#define UNARY_OPERATION(Name, Spelling) case UO_##Name: return Spelling;
#include "clang/AST/OperationKinds.def"
  }
  llvm_unreachable("Unknown unary operator");
}

UnaryOperatorKind
UnaryOperator::getOverloadedOpcode(OverloadedOperatorKind OO, bool Postfix) {
  switch (OO) {
  default: llvm_unreachable("No unary operator for overloaded function");
  case OO_PlusPlus:   return Postfix ? UO_PostInc : UO_PreInc;
  case OO_MinusMinus: return Postfix ? UO_PostDec : UO_PreDec;
  case OO_Amp:        return UO_AddrOf;
  case OO_Star:       return UO_Deref;
  case OO_Plus:       return UO_Plus;
  case OO_Minus:      return UO_Minus;
  case OO_Tilde:      return UO_Not;
  case OO_Exclaim:    return UO_LNot;
  case OO_Coawait:    return UO_Coawait;
  }
}

OverloadedOperatorKind UnaryOperator::getOverloadedOperator(Opcode Opc) {
  switch (Opc) {
  case UO_PostInc: case UO_PreInc: return OO_PlusPlus;
  case UO_PostDec: case UO_PreDec: return OO_MinusMinus;
  case UO_AddrOf: return OO_Amp;
  case UO_Deref: return OO_Star;
  case UO_Plus: return OO_Plus;
  case UO_Minus: return OO_Minus;
  case UO_Not: return OO_Tilde;
  case UO_LNot: return OO_Exclaim;
  case UO_Coawait: return OO_Coawait;
  default: return OO_None;
  }
}


//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

CallExpr::CallExpr(StmtClass SC, Expr *Fn, ArrayRef<Expr *> PreArgs,
                   ArrayRef<Expr *> Args, QualType Ty, ExprValueKind VK,
                   SourceLocation RParenLoc, unsigned MinNumArgs,
                   ADLCallKind UsesADL)
    : Expr(SC, Ty, VK, OK_Ordinary, Fn->isTypeDependent(),
           Fn->isValueDependent(), Fn->isInstantiationDependent(),
           Fn->containsUnexpandedParameterPack()),
      RParenLoc(RParenLoc) {
  NumArgs = std::max<unsigned>(Args.size(), MinNumArgs);
  unsigned NumPreArgs = PreArgs.size();
  CallExprBits.NumPreArgs = NumPreArgs;
  assert((NumPreArgs == getNumPreArgs()) && "NumPreArgs overflow!");

  unsigned OffsetToTrailingObjects = offsetToTrailingObjects(SC);
  CallExprBits.OffsetToTrailingObjects = OffsetToTrailingObjects;
  assert((CallExprBits.OffsetToTrailingObjects == OffsetToTrailingObjects) &&
         "OffsetToTrailingObjects overflow!");

  CallExprBits.UsesADL = static_cast<bool>(UsesADL);

  setCallee(Fn);
  for (unsigned I = 0; I != NumPreArgs; ++I) {
    updateDependenciesFromArg(PreArgs[I]);
    setPreArg(I, PreArgs[I]);
  }
  for (unsigned I = 0; I != Args.size(); ++I) {
    updateDependenciesFromArg(Args[I]);
    setArg(I, Args[I]);
  }
  for (unsigned I = Args.size(); I != NumArgs; ++I) {
    setArg(I, nullptr);
  }
}

CallExpr::CallExpr(StmtClass SC, unsigned NumPreArgs, unsigned NumArgs,
                   EmptyShell Empty)
    : Expr(SC, Empty), NumArgs(NumArgs) {
  CallExprBits.NumPreArgs = NumPreArgs;
  assert((NumPreArgs == getNumPreArgs()) && "NumPreArgs overflow!");

  unsigned OffsetToTrailingObjects = offsetToTrailingObjects(SC);
  CallExprBits.OffsetToTrailingObjects = OffsetToTrailingObjects;
  assert((CallExprBits.OffsetToTrailingObjects == OffsetToTrailingObjects) &&
         "OffsetToTrailingObjects overflow!");
}

CallExpr *CallExpr::Create(const ASTContext &Ctx, Expr *Fn,
                           ArrayRef<Expr *> Args, QualType Ty, ExprValueKind VK,
                           SourceLocation RParenLoc, unsigned MinNumArgs,
                           ADLCallKind UsesADL) {
  unsigned NumArgs = std::max<unsigned>(Args.size(), MinNumArgs);
  unsigned SizeOfTrailingObjects =
      CallExpr::sizeOfTrailingObjects(/*NumPreArgs=*/0, NumArgs);
  void *Mem =
      Ctx.Allocate(sizeof(CallExpr) + SizeOfTrailingObjects, alignof(CallExpr));
  return new (Mem) CallExpr(CallExprClass, Fn, /*PreArgs=*/{}, Args, Ty, VK,
                            RParenLoc, MinNumArgs, UsesADL);
}

CallExpr *CallExpr::CreateTemporary(void *Mem, Expr *Fn, QualType Ty,
                                    ExprValueKind VK, SourceLocation RParenLoc,
                                    ADLCallKind UsesADL) {
  assert(!(reinterpret_cast<uintptr_t>(Mem) % alignof(CallExpr)) &&
         "Misaligned memory in CallExpr::CreateTemporary!");
  return new (Mem) CallExpr(CallExprClass, Fn, /*PreArgs=*/{}, /*Args=*/{}, Ty,
                            VK, RParenLoc, /*MinNumArgs=*/0, UsesADL);
}

CallExpr *CallExpr::CreateEmpty(const ASTContext &Ctx, unsigned NumArgs,
                                EmptyShell Empty) {
  unsigned SizeOfTrailingObjects =
      CallExpr::sizeOfTrailingObjects(/*NumPreArgs=*/0, NumArgs);
  void *Mem =
      Ctx.Allocate(sizeof(CallExpr) + SizeOfTrailingObjects, alignof(CallExpr));
  return new (Mem) CallExpr(CallExprClass, /*NumPreArgs=*/0, NumArgs, Empty);
}

unsigned CallExpr::offsetToTrailingObjects(StmtClass SC) {
  switch (SC) {
  case CallExprClass:
    return sizeof(CallExpr);
  case CXXOperatorCallExprClass:
    return sizeof(CXXOperatorCallExpr);
  case CXXMemberCallExprClass:
    return sizeof(CXXMemberCallExpr);
  case UserDefinedLiteralClass:
    return sizeof(UserDefinedLiteral);
  case CUDAKernelCallExprClass:
    return sizeof(CUDAKernelCallExpr);
  default:
    llvm_unreachable("unexpected class deriving from CallExpr!");
  }
}

void CallExpr::updateDependenciesFromArg(Expr *Arg) {
  if (Arg->isTypeDependent())
    ExprBits.TypeDependent = true;
  if (Arg->isValueDependent())
    ExprBits.ValueDependent = true;
  if (Arg->isInstantiationDependent())
    ExprBits.InstantiationDependent = true;
  if (Arg->containsUnexpandedParameterPack())
    ExprBits.ContainsUnexpandedParameterPack = true;
}

Decl *Expr::getReferencedDeclOfCallee() {
  Expr *CEE = IgnoreParenImpCasts();

  while (SubstNonTypeTemplateParmExpr *NTTP
                                = dyn_cast<SubstNonTypeTemplateParmExpr>(CEE)) {
    CEE = NTTP->getReplacement()->IgnoreParenCasts();
  }

  // If we're calling a dereference, look at the pointer instead.
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(CEE)) {
    if (BO->isPtrMemOp())
      CEE = BO->getRHS()->IgnoreParenCasts();
  } else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(CEE)) {
    if (UO->getOpcode() == UO_Deref)
      CEE = UO->getSubExpr()->IgnoreParenCasts();
  }
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(CEE))
    return DRE->getDecl();
  if (MemberExpr *ME = dyn_cast<MemberExpr>(CEE))
    return ME->getMemberDecl();

  return nullptr;
}

/// getBuiltinCallee - If this is a call to a builtin, return the builtin ID. If
/// not, return 0.
unsigned CallExpr::getBuiltinCallee() const {
  // All simple function calls (e.g. func()) are implicitly cast to pointer to
  // function. As a result, we try and obtain the DeclRefExpr from the
  // ImplicitCastExpr.
  const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(getCallee());
  if (!ICE) // FIXME: deal with more complex calls (e.g. (func)(), (*func)()).
    return 0;

  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(ICE->getSubExpr());
  if (!DRE)
    return 0;

  const FunctionDecl *FDecl = dyn_cast<FunctionDecl>(DRE->getDecl());
  if (!FDecl)
    return 0;

  if (!FDecl->getIdentifier())
    return 0;

  return FDecl->getBuiltinID();
}

bool CallExpr::isUnevaluatedBuiltinCall(const ASTContext &Ctx) const {
  if (unsigned BI = getBuiltinCallee())
    return Ctx.BuiltinInfo.isUnevaluated(BI);
  return false;
}

QualType CallExpr::getCallReturnType(const ASTContext &Ctx) const {
  const Expr *Callee = getCallee();
  QualType CalleeType = Callee->getType();
  if (const auto *FnTypePtr = CalleeType->getAs<PointerType>()) {
    CalleeType = FnTypePtr->getPointeeType();
  } else if (const auto *BPT = CalleeType->getAs<BlockPointerType>()) {
    CalleeType = BPT->getPointeeType();
  } else if (CalleeType->isSpecificPlaceholderType(BuiltinType::BoundMember)) {
    if (isa<CXXPseudoDestructorExpr>(Callee->IgnoreParens()))
      return Ctx.VoidTy;

    // This should never be overloaded and so should never return null.
    CalleeType = Expr::findBoundMemberType(Callee);
  }

  const FunctionType *FnType = CalleeType->castAs<FunctionType>();
  return FnType->getReturnType();
}

const Attr *CallExpr::getUnusedResultAttr(const ASTContext &Ctx) const {
  // If the return type is a struct, union, or enum that is marked nodiscard,
  // then return the return type attribute.
  if (const TagDecl *TD = getCallReturnType(Ctx)->getAsTagDecl())
    if (const auto *A = TD->getAttr<WarnUnusedResultAttr>())
      return A;

  // Otherwise, see if the callee is marked nodiscard and return that attribute
  // instead.
  const Decl *D = getCalleeDecl();
  return D ? D->getAttr<WarnUnusedResultAttr>() : nullptr;
}

SourceLocation CallExpr::getBeginLoc() const {
  if (isa<CXXOperatorCallExpr>(this))
    return cast<CXXOperatorCallExpr>(this)->getBeginLoc();

  SourceLocation begin = getCallee()->getBeginLoc();
  if (begin.isInvalid() && getNumArgs() > 0 && getArg(0))
    begin = getArg(0)->getBeginLoc();
  return begin;
}
SourceLocation CallExpr::getEndLoc() const {
  if (isa<CXXOperatorCallExpr>(this))
    return cast<CXXOperatorCallExpr>(this)->getEndLoc();

  SourceLocation end = getRParenLoc();
  if (end.isInvalid() && getNumArgs() > 0 && getArg(getNumArgs() - 1))
    end = getArg(getNumArgs() - 1)->getEndLoc();
  return end;
}

OffsetOfExpr *OffsetOfExpr::Create(const ASTContext &C, QualType type,
                                   SourceLocation OperatorLoc,
                                   TypeSourceInfo *tsi,
                                   ArrayRef<OffsetOfNode> comps,
                                   ArrayRef<Expr*> exprs,
                                   SourceLocation RParenLoc) {
  void *Mem = C.Allocate(
      totalSizeToAlloc<OffsetOfNode, Expr *>(comps.size(), exprs.size()));

  return new (Mem) OffsetOfExpr(C, type, OperatorLoc, tsi, comps, exprs,
                                RParenLoc);
}

OffsetOfExpr *OffsetOfExpr::CreateEmpty(const ASTContext &C,
                                        unsigned numComps, unsigned numExprs) {
  void *Mem =
      C.Allocate(totalSizeToAlloc<OffsetOfNode, Expr *>(numComps, numExprs));
  return new (Mem) OffsetOfExpr(numComps, numExprs);
}

OffsetOfExpr::OffsetOfExpr(const ASTContext &C, QualType type,
                           SourceLocation OperatorLoc, TypeSourceInfo *tsi,
                           ArrayRef<OffsetOfNode> comps, ArrayRef<Expr*> exprs,
                           SourceLocation RParenLoc)
  : Expr(OffsetOfExprClass, type, VK_RValue, OK_Ordinary,
         /*TypeDependent=*/false,
         /*ValueDependent=*/tsi->getType()->isDependentType(),
         tsi->getType()->isInstantiationDependentType(),
         tsi->getType()->containsUnexpandedParameterPack()),
    OperatorLoc(OperatorLoc), RParenLoc(RParenLoc), TSInfo(tsi),
    NumComps(comps.size()), NumExprs(exprs.size())
{
  for (unsigned i = 0; i != comps.size(); ++i) {
    setComponent(i, comps[i]);
  }

  for (unsigned i = 0; i != exprs.size(); ++i) {
    if (exprs[i]->isTypeDependent() || exprs[i]->isValueDependent())
      ExprBits.ValueDependent = true;
    if (exprs[i]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    setIndexExpr(i, exprs[i]);
  }
}

IdentifierInfo *OffsetOfNode::getFieldName() const {
  assert(getKind() == Field || getKind() == Identifier);
  if (getKind() == Field)
    return getField()->getIdentifier();

  return reinterpret_cast<IdentifierInfo *> (Data & ~(uintptr_t)Mask);
}

UnaryExprOrTypeTraitExpr::UnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTrait ExprKind, Expr *E, QualType resultType,
    SourceLocation op, SourceLocation rp)
    : Expr(UnaryExprOrTypeTraitExprClass, resultType, VK_RValue, OK_Ordinary,
           false, // Never type-dependent (C++ [temp.dep.expr]p3).
           // Value-dependent if the argument is type-dependent.
           E->isTypeDependent(), E->isInstantiationDependent(),
           E->containsUnexpandedParameterPack()),
      OpLoc(op), RParenLoc(rp) {
  UnaryExprOrTypeTraitExprBits.Kind = ExprKind;
  UnaryExprOrTypeTraitExprBits.IsType = false;
  Argument.Ex = E;

  // Check to see if we are in the situation where alignof(decl) should be
  // dependent because decl's alignment is dependent.
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    if (!isValueDependent() || !isInstantiationDependent()) {
      E = E->IgnoreParens();

      const ValueDecl *D = nullptr;
      if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
        D = DRE->getDecl();
      else if (const auto *ME = dyn_cast<MemberExpr>(E))
        D = ME->getMemberDecl();

      if (D) {
        for (const auto *I : D->specific_attrs<AlignedAttr>()) {
          if (I->isAlignmentDependent()) {
            setValueDependent(true);
            setInstantiationDependent(true);
            break;
          }
        }
      }
    }
  }
}

MemberExpr *MemberExpr::Create(
    const ASTContext &C, Expr *base, bool isarrow, SourceLocation OperatorLoc,
    NestedNameSpecifierLoc QualifierLoc, SourceLocation TemplateKWLoc,
    ValueDecl *memberdecl, DeclAccessPair founddecl,
    DeclarationNameInfo nameinfo, const TemplateArgumentListInfo *targs,
    QualType ty, ExprValueKind vk, ExprObjectKind ok) {

  bool hasQualOrFound = (QualifierLoc ||
                         founddecl.getDecl() != memberdecl ||
                         founddecl.getAccess() != memberdecl->getAccess());

  bool HasTemplateKWAndArgsInfo = targs || TemplateKWLoc.isValid();
  std::size_t Size =
      totalSizeToAlloc<MemberExprNameQualifier, ASTTemplateKWAndArgsInfo,
                       TemplateArgumentLoc>(hasQualOrFound ? 1 : 0,
                                            HasTemplateKWAndArgsInfo ? 1 : 0,
                                            targs ? targs->size() : 0);

  void *Mem = C.Allocate(Size, alignof(MemberExpr));
  MemberExpr *E = new (Mem)
      MemberExpr(base, isarrow, OperatorLoc, memberdecl, nameinfo, ty, vk, ok);

  if (hasQualOrFound) {
    // FIXME: Wrong. We should be looking at the member declaration we found.
    if (QualifierLoc && QualifierLoc.getNestedNameSpecifier()->isDependent()) {
      E->setValueDependent(true);
      E->setTypeDependent(true);
      E->setInstantiationDependent(true);
    }
    else if (QualifierLoc &&
             QualifierLoc.getNestedNameSpecifier()->isInstantiationDependent())
      E->setInstantiationDependent(true);

    E->MemberExprBits.HasQualifierOrFoundDecl = true;

    MemberExprNameQualifier *NQ =
        E->getTrailingObjects<MemberExprNameQualifier>();
    NQ->QualifierLoc = QualifierLoc;
    NQ->FoundDecl = founddecl;
  }

  E->MemberExprBits.HasTemplateKWAndArgsInfo =
      (targs || TemplateKWLoc.isValid());

  if (targs) {
    bool Dependent = false;
    bool InstantiationDependent = false;
    bool ContainsUnexpandedParameterPack = false;
    E->getTrailingObjects<ASTTemplateKWAndArgsInfo>()->initializeFrom(
        TemplateKWLoc, *targs, E->getTrailingObjects<TemplateArgumentLoc>(),
        Dependent, InstantiationDependent, ContainsUnexpandedParameterPack);
    if (InstantiationDependent)
      E->setInstantiationDependent(true);
  } else if (TemplateKWLoc.isValid()) {
    E->getTrailingObjects<ASTTemplateKWAndArgsInfo>()->initializeFrom(
        TemplateKWLoc);
  }

  return E;
}

SourceLocation MemberExpr::getBeginLoc() const {
  if (isImplicitAccess()) {
    if (hasQualifier())
      return getQualifierLoc().getBeginLoc();
    return MemberLoc;
  }

  // FIXME: We don't want this to happen. Rather, we should be able to
  // detect all kinds of implicit accesses more cleanly.
  SourceLocation BaseStartLoc = getBase()->getBeginLoc();
  if (BaseStartLoc.isValid())
    return BaseStartLoc;
  return MemberLoc;
}
SourceLocation MemberExpr::getEndLoc() const {
  SourceLocation EndLoc = getMemberNameInfo().getEndLoc();
  if (hasExplicitTemplateArgs())
    EndLoc = getRAngleLoc();
  else if (EndLoc.isInvalid())
    EndLoc = getBase()->getEndLoc();
  return EndLoc;
}

bool CastExpr::CastConsistency() const {
  switch (getCastKind()) {
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_DerivedToBaseMemberPointer:
  case CK_BaseToDerived:
  case CK_BaseToDerivedMemberPointer:
    assert(!path_empty() && "Cast kind should have a base path!");
    break;

  case CK_CPointerToObjCPointerCast:
    assert(getType()->isObjCObjectPointerType());
    assert(getSubExpr()->getType()->isPointerType());
    goto CheckNoBasePath;

  case CK_BlockPointerToObjCPointerCast:
    assert(getType()->isObjCObjectPointerType());
    assert(getSubExpr()->getType()->isBlockPointerType());
    goto CheckNoBasePath;

  case CK_ReinterpretMemberPointer:
    assert(getType()->isMemberPointerType());
    assert(getSubExpr()->getType()->isMemberPointerType());
    goto CheckNoBasePath;

  case CK_BitCast:
    // Arbitrary casts to C pointer types count as bitcasts.
    // Otherwise, we should only have block and ObjC pointer casts
    // here if they stay within the type kind.
    if (!getType()->isPointerType()) {
      assert(getType()->isObjCObjectPointerType() ==
             getSubExpr()->getType()->isObjCObjectPointerType());
      assert(getType()->isBlockPointerType() ==
             getSubExpr()->getType()->isBlockPointerType());
    }
    goto CheckNoBasePath;

  case CK_AnyPointerToBlockPointerCast:
    assert(getType()->isBlockPointerType());
    assert(getSubExpr()->getType()->isAnyPointerType() &&
           !getSubExpr()->getType()->isBlockPointerType());
    goto CheckNoBasePath;

  case CK_CopyAndAutoreleaseBlockObject:
    assert(getType()->isBlockPointerType());
    assert(getSubExpr()->getType()->isBlockPointerType());
    goto CheckNoBasePath;

  case CK_FunctionToPointerDecay:
    assert(getType()->isPointerType());
    assert(getSubExpr()->getType()->isFunctionType());
    goto CheckNoBasePath;

  case CK_AddressSpaceConversion: {
    auto Ty = getType();
    auto SETy = getSubExpr()->getType();
    assert(getValueKindForType(Ty) == Expr::getValueKindForType(SETy));
    if (isRValue()) {
      Ty = Ty->getPointeeType();
      SETy = SETy->getPointeeType();
    }
    assert(!Ty.isNull() && !SETy.isNull() &&
           Ty.getAddressSpace() != SETy.getAddressSpace());
    goto CheckNoBasePath;
  }
  // These should not have an inheritance path.
  case CK_Dynamic:
  case CK_ToUnion:
  case CK_ArrayToPointerDecay:
  case CK_NullToMemberPointer:
  case CK_NullToPointer:
  case CK_ConstructorConversion:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_ToVoid:
  case CK_VectorSplat:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingCast:
  case CK_ObjCObjectLValueCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_ZeroToOCLOpaqueType:
  case CK_IntToOCLSampler:
  case CK_FixedPointCast:
    assert(!getType()->isBooleanType() && "unheralded conversion to bool");
    goto CheckNoBasePath;

  case CK_Dependent:
  case CK_LValueToRValue:
  case CK_NoOp:
  case CK_AtomicToNonAtomic:
  case CK_NonAtomicToAtomic:
  case CK_PointerToBoolean:
  case CK_IntegralToBoolean:
  case CK_FloatingToBoolean:
  case CK_MemberPointerToBoolean:
  case CK_FloatingComplexToBoolean:
  case CK_IntegralComplexToBoolean:
  case CK_LValueBitCast:            // -> bool&
  case CK_UserDefinedConversion:    // operator bool()
  case CK_BuiltinFnToFnPtr:
  case CK_FixedPointToBoolean:
  CheckNoBasePath:
    assert(path_empty() && "Cast kind should not have a base path!");
    break;
  }
  return true;
}

const char *CastExpr::getCastKindName(CastKind CK) {
  switch (CK) {
#define CAST_OPERATION(Name) case CK_##Name: return #Name;
#include "clang/AST/OperationKinds.def"
  }
  llvm_unreachable("Unhandled cast kind!");
}

namespace {
  const Expr *skipImplicitTemporary(const Expr *E) {
    // Skip through reference binding to temporary.
    if (auto *Materialize = dyn_cast<MaterializeTemporaryExpr>(E))
      E = Materialize->GetTemporaryExpr();

    // Skip any temporary bindings; they're implicit.
    if (auto *Binder = dyn_cast<CXXBindTemporaryExpr>(E))
      E = Binder->getSubExpr();

    return E;
  }
}

Expr *CastExpr::getSubExprAsWritten() {
  const Expr *SubExpr = nullptr;
  const CastExpr *E = this;
  do {
    SubExpr = skipImplicitTemporary(E->getSubExpr());

    // Conversions by constructor and conversion functions have a
    // subexpression describing the call; strip it off.
    if (E->getCastKind() == CK_ConstructorConversion)
      SubExpr =
        skipImplicitTemporary(cast<CXXConstructExpr>(SubExpr)->getArg(0));
    else if (E->getCastKind() == CK_UserDefinedConversion) {
      assert((isa<CXXMemberCallExpr>(SubExpr) ||
              isa<BlockExpr>(SubExpr)) &&
             "Unexpected SubExpr for CK_UserDefinedConversion.");
      if (auto *MCE = dyn_cast<CXXMemberCallExpr>(SubExpr))
        SubExpr = MCE->getImplicitObjectArgument();
    }

    // If the subexpression we're left with is an implicit cast, look
    // through that, too.
  } while ((E = dyn_cast<ImplicitCastExpr>(SubExpr)));

  return const_cast<Expr*>(SubExpr);
}

NamedDecl *CastExpr::getConversionFunction() const {
  const Expr *SubExpr = nullptr;

  for (const CastExpr *E = this; E; E = dyn_cast<ImplicitCastExpr>(SubExpr)) {
    SubExpr = skipImplicitTemporary(E->getSubExpr());

    if (E->getCastKind() == CK_ConstructorConversion)
      return cast<CXXConstructExpr>(SubExpr)->getConstructor();

    if (E->getCastKind() == CK_UserDefinedConversion) {
      if (auto *MCE = dyn_cast<CXXMemberCallExpr>(SubExpr))
        return MCE->getMethodDecl();
    }
  }

  return nullptr;
}

CXXBaseSpecifier **CastExpr::path_buffer() {
  switch (getStmtClass()) {
#define ABSTRACT_STMT(x)
#define CASTEXPR(Type, Base)                                                   \
  case Stmt::Type##Class:                                                      \
    return static_cast<Type *>(this)->getTrailingObjects<CXXBaseSpecifier *>();
#define STMT(Type, Base)
#include "clang/AST/StmtNodes.inc"
  default:
    llvm_unreachable("non-cast expressions not possible here");
  }
}

const FieldDecl *CastExpr::getTargetFieldForToUnionCast(QualType unionType,
                                                        QualType opType) {
  auto RD = unionType->castAs<RecordType>()->getDecl();
  return getTargetFieldForToUnionCast(RD, opType);
}

const FieldDecl *CastExpr::getTargetFieldForToUnionCast(const RecordDecl *RD,
                                                        QualType OpType) {
  auto &Ctx = RD->getASTContext();
  RecordDecl::field_iterator Field, FieldEnd;
  for (Field = RD->field_begin(), FieldEnd = RD->field_end();
       Field != FieldEnd; ++Field) {
    if (Ctx.hasSameUnqualifiedType(Field->getType(), OpType) &&
        !Field->isUnnamedBitfield()) {
      return *Field;
    }
  }
  return nullptr;
}

ImplicitCastExpr *ImplicitCastExpr::Create(const ASTContext &C, QualType T,
                                           CastKind Kind, Expr *Operand,
                                           const CXXCastPath *BasePath,
                                           ExprValueKind VK) {
  unsigned PathSize = (BasePath ? BasePath->size() : 0);
  void *Buffer = C.Allocate(totalSizeToAlloc<CXXBaseSpecifier *>(PathSize));
  ImplicitCastExpr *E =
    new (Buffer) ImplicitCastExpr(T, Kind, Operand, PathSize, VK);
  if (PathSize)
    std::uninitialized_copy_n(BasePath->data(), BasePath->size(),
                              E->getTrailingObjects<CXXBaseSpecifier *>());
  return E;
}

ImplicitCastExpr *ImplicitCastExpr::CreateEmpty(const ASTContext &C,
                                                unsigned PathSize) {
  void *Buffer = C.Allocate(totalSizeToAlloc<CXXBaseSpecifier *>(PathSize));
  return new (Buffer) ImplicitCastExpr(EmptyShell(), PathSize);
}


CStyleCastExpr *CStyleCastExpr::Create(const ASTContext &C, QualType T,
                                       ExprValueKind VK, CastKind K, Expr *Op,
                                       const CXXCastPath *BasePath,
                                       TypeSourceInfo *WrittenTy,
                                       SourceLocation L, SourceLocation R) {
  unsigned PathSize = (BasePath ? BasePath->size() : 0);
  void *Buffer = C.Allocate(totalSizeToAlloc<CXXBaseSpecifier *>(PathSize));
  CStyleCastExpr *E =
    new (Buffer) CStyleCastExpr(T, VK, K, Op, PathSize, WrittenTy, L, R);
  if (PathSize)
    std::uninitialized_copy_n(BasePath->data(), BasePath->size(),
                              E->getTrailingObjects<CXXBaseSpecifier *>());
  return E;
}

CStyleCastExpr *CStyleCastExpr::CreateEmpty(const ASTContext &C,
                                            unsigned PathSize) {
  void *Buffer = C.Allocate(totalSizeToAlloc<CXXBaseSpecifier *>(PathSize));
  return new (Buffer) CStyleCastExpr(EmptyShell(), PathSize);
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "<<=".
StringRef BinaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
#define BINARY_OPERATION(Name, Spelling) case BO_##Name: return Spelling;
#include "clang/AST/OperationKinds.def"
  }
  llvm_unreachable("Invalid OpCode!");
}

BinaryOperatorKind
BinaryOperator::getOverloadedOpcode(OverloadedOperatorKind OO) {
  switch (OO) {
  default: llvm_unreachable("Not an overloadable binary operator");
  case OO_Plus: return BO_Add;
  case OO_Minus: return BO_Sub;
  case OO_Star: return BO_Mul;
  case OO_Slash: return BO_Div;
  case OO_Percent: return BO_Rem;
  case OO_Caret: return BO_Xor;
  case OO_Amp: return BO_And;
  case OO_Pipe: return BO_Or;
  case OO_Equal: return BO_Assign;
  case OO_Spaceship: return BO_Cmp;
  case OO_Less: return BO_LT;
  case OO_Greater: return BO_GT;
  case OO_PlusEqual: return BO_AddAssign;
  case OO_MinusEqual: return BO_SubAssign;
  case OO_StarEqual: return BO_MulAssign;
  case OO_SlashEqual: return BO_DivAssign;
  case OO_PercentEqual: return BO_RemAssign;
  case OO_CaretEqual: return BO_XorAssign;
  case OO_AmpEqual: return BO_AndAssign;
  case OO_PipeEqual: return BO_OrAssign;
  case OO_LessLess: return BO_Shl;
  case OO_GreaterGreater: return BO_Shr;
  case OO_LessLessEqual: return BO_ShlAssign;
  case OO_GreaterGreaterEqual: return BO_ShrAssign;
  case OO_EqualEqual: return BO_EQ;
  case OO_ExclaimEqual: return BO_NE;
  case OO_LessEqual: return BO_LE;
  case OO_GreaterEqual: return BO_GE;
  case OO_AmpAmp: return BO_LAnd;
  case OO_PipePipe: return BO_LOr;
  case OO_Comma: return BO_Comma;
  case OO_ArrowStar: return BO_PtrMemI;
  }
}

OverloadedOperatorKind BinaryOperator::getOverloadedOperator(Opcode Opc) {
  static const OverloadedOperatorKind OverOps[] = {
    /* .* Cannot be overloaded */OO_None, OO_ArrowStar,
    OO_Star, OO_Slash, OO_Percent,
    OO_Plus, OO_Minus,
    OO_LessLess, OO_GreaterGreater,
    OO_Spaceship,
    OO_Less, OO_Greater, OO_LessEqual, OO_GreaterEqual,
    OO_EqualEqual, OO_ExclaimEqual,
    OO_Amp,
    OO_Caret,
    OO_Pipe,
    OO_AmpAmp,
    OO_PipePipe,
    OO_Equal, OO_StarEqual,
    OO_SlashEqual, OO_PercentEqual,
    OO_PlusEqual, OO_MinusEqual,
    OO_LessLessEqual, OO_GreaterGreaterEqual,
    OO_AmpEqual, OO_CaretEqual,
    OO_PipeEqual,
    OO_Comma
  };
  return OverOps[Opc];
}

bool BinaryOperator::isNullPointerArithmeticExtension(ASTContext &Ctx,
                                                      Opcode Opc,
                                                      Expr *LHS, Expr *RHS) {
  if (Opc != BO_Add)
    return false;

  // Check that we have one pointer and one integer operand.
  Expr *PExp;
  if (LHS->getType()->isPointerType()) {
    if (!RHS->getType()->isIntegerType())
      return false;
    PExp = LHS;
  } else if (RHS->getType()->isPointerType()) {
    if (!LHS->getType()->isIntegerType())
      return false;
    PExp = RHS;
  } else {
    return false;
  }

  // Check that the pointer is a nullptr.
  if (!PExp->IgnoreParenCasts()
          ->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
    return false;

  // Check that the pointee type is char-sized.
  const PointerType *PTy = PExp->getType()->getAs<PointerType>();
  if (!PTy || !PTy->getPointeeType()->isCharType())
    return false;

  return true;
}
InitListExpr::InitListExpr(const ASTContext &C, SourceLocation lbraceloc,
                           ArrayRef<Expr*> initExprs, SourceLocation rbraceloc)
  : Expr(InitListExprClass, QualType(), VK_RValue, OK_Ordinary, false, false,
         false, false),
    InitExprs(C, initExprs.size()),
    LBraceLoc(lbraceloc), RBraceLoc(rbraceloc), AltForm(nullptr, true)
{
  sawArrayRangeDesignator(false);
  for (unsigned I = 0; I != initExprs.size(); ++I) {
    if (initExprs[I]->isTypeDependent())
      ExprBits.TypeDependent = true;
    if (initExprs[I]->isValueDependent())
      ExprBits.ValueDependent = true;
    if (initExprs[I]->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (initExprs[I]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;
  }

  InitExprs.insert(C, InitExprs.end(), initExprs.begin(), initExprs.end());
}

void InitListExpr::reserveInits(const ASTContext &C, unsigned NumInits) {
  if (NumInits > InitExprs.size())
    InitExprs.reserve(C, NumInits);
}

void InitListExpr::resizeInits(const ASTContext &C, unsigned NumInits) {
  InitExprs.resize(C, NumInits, nullptr);
}

Expr *InitListExpr::updateInit(const ASTContext &C, unsigned Init, Expr *expr) {
  if (Init >= InitExprs.size()) {
    InitExprs.insert(C, InitExprs.end(), Init - InitExprs.size() + 1, nullptr);
    setInit(Init, expr);
    return nullptr;
  }

  Expr *Result = cast_or_null<Expr>(InitExprs[Init]);
  setInit(Init, expr);
  return Result;
}

void InitListExpr::setArrayFiller(Expr *filler) {
  assert(!hasArrayFiller() && "Filler already set!");
  ArrayFillerOrUnionFieldInit = filler;
  // Fill out any "holes" in the array due to designated initializers.
  Expr **inits = getInits();
  for (unsigned i = 0, e = getNumInits(); i != e; ++i)
    if (inits[i] == nullptr)
      inits[i] = filler;
}

bool InitListExpr::isStringLiteralInit() const {
  if (getNumInits() != 1)
    return false;
  const ArrayType *AT = getType()->getAsArrayTypeUnsafe();
  if (!AT || !AT->getElementType()->isIntegerType())
    return false;
  // It is possible for getInit() to return null.
  const Expr *Init = getInit(0);
  if (!Init)
    return false;
  Init = Init->IgnoreParens();
  return isa<StringLiteral>(Init) || isa<ObjCEncodeExpr>(Init);
}

bool InitListExpr::isTransparent() const {
  assert(isSemanticForm() && "syntactic form never semantically transparent");

  // A glvalue InitListExpr is always just sugar.
  if (isGLValue()) {
    assert(getNumInits() == 1 && "multiple inits in glvalue init list");
    return true;
  }

  // Otherwise, we're sugar if and only if we have exactly one initializer that
  // is of the same type.
  if (getNumInits() != 1 || !getInit(0))
    return false;

  // Don't confuse aggregate initialization of a struct X { X &x; }; with a
  // transparent struct copy.
  if (!getInit(0)->isRValue() && getType()->isRecordType())
    return false;

  return getType().getCanonicalType() ==
         getInit(0)->getType().getCanonicalType();
}

bool InitListExpr::isIdiomaticZeroInitializer(const LangOptions &LangOpts) const {
  assert(isSyntacticForm() && "only test syntactic form as zero initializer");

  if (LangOpts.CPlusPlus || getNumInits() != 1) {
    return false;
  }

  const IntegerLiteral *Lit = dyn_cast<IntegerLiteral>(getInit(0));
  return Lit && Lit->getValue() == 0;
}

SourceLocation InitListExpr::getBeginLoc() const {
  if (InitListExpr *SyntacticForm = getSyntacticForm())
    return SyntacticForm->getBeginLoc();
  SourceLocation Beg = LBraceLoc;
  if (Beg.isInvalid()) {
    // Find the first non-null initializer.
    for (InitExprsTy::const_iterator I = InitExprs.begin(),
                                     E = InitExprs.end();
      I != E; ++I) {
      if (Stmt *S = *I) {
        Beg = S->getBeginLoc();
        break;
      }
    }
  }
  return Beg;
}

SourceLocation InitListExpr::getEndLoc() const {
  if (InitListExpr *SyntacticForm = getSyntacticForm())
    return SyntacticForm->getEndLoc();
  SourceLocation End = RBraceLoc;
  if (End.isInvalid()) {
    // Find the first non-null initializer from the end.
    for (InitExprsTy::const_reverse_iterator I = InitExprs.rbegin(),
         E = InitExprs.rend();
         I != E; ++I) {
      if (Stmt *S = *I) {
        End = S->getEndLoc();
        break;
      }
    }
  }
  return End;
}

/// getFunctionType - Return the underlying function type for this block.
///
const FunctionProtoType *BlockExpr::getFunctionType() const {
  // The block pointer is never sugared, but the function type might be.
  return cast<BlockPointerType>(getType())
           ->getPointeeType()->castAs<FunctionProtoType>();
}

SourceLocation BlockExpr::getCaretLocation() const {
  return TheBlock->getCaretLocation();
}
const Stmt *BlockExpr::getBody() const {
  return TheBlock->getBody();
}
Stmt *BlockExpr::getBody() {
  return TheBlock->getBody();
}


//===----------------------------------------------------------------------===//
// Generic Expression Routines
//===----------------------------------------------------------------------===//

/// isUnusedResultAWarning - Return true if this immediate expression should
/// be warned about if the result is unused.  If so, fill in Loc and Ranges
/// with location to warn on and the source range[s] to report with the
/// warning.
bool Expr::isUnusedResultAWarning(const Expr *&WarnE, SourceLocation &Loc,
                                  SourceRange &R1, SourceRange &R2,
                                  ASTContext &Ctx) const {
  // Don't warn if the expr is type dependent. The type could end up
  // instantiating to void.
  if (isTypeDependent())
    return false;

  switch (getStmtClass()) {
  default:
    if (getType()->isVoidType())
      return false;
    WarnE = this;
    Loc = getExprLoc();
    R1 = getSourceRange();
    return true;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->
      isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case GenericSelectionExprClass:
    return cast<GenericSelectionExpr>(this)->getResultExpr()->
      isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case CoawaitExprClass:
  case CoyieldExprClass:
    return cast<CoroutineSuspendExpr>(this)->getResumeExpr()->
      isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case ChooseExprClass:
    return cast<ChooseExpr>(this)->getChosenSubExpr()->
      isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(this);

    switch (UO->getOpcode()) {
    case UO_Plus:
    case UO_Minus:
    case UO_AddrOf:
    case UO_Not:
    case UO_LNot:
    case UO_Deref:
      break;
    case UO_Coawait:
      // This is just the 'operator co_await' call inside the guts of a
      // dependent co_await call.
    case UO_PostInc:
    case UO_PostDec:
    case UO_PreInc:
    case UO_PreDec:                 // ++/--
      return false;  // Not a warning.
    case UO_Real:
    case UO_Imag:
      // accessing a piece of a volatile complex is a side-effect.
      if (Ctx.getCanonicalType(UO->getSubExpr()->getType())
          .isVolatileQualified())
        return false;
      break;
    case UO_Extension:
      return UO->getSubExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
    }
    WarnE = this;
    Loc = UO->getOperatorLoc();
    R1 = UO->getSubExpr()->getSourceRange();
    return true;
  }
  case BinaryOperatorClass: {
    const BinaryOperator *BO = cast<BinaryOperator>(this);
    switch (BO->getOpcode()) {
      default:
        break;
      // Consider the RHS of comma for side effects. LHS was checked by
      // Sema::CheckCommaOperands.
      case BO_Comma:
        // ((foo = <blah>), 0) is an idiom for hiding the result (and
        // lvalue-ness) of an assignment written in a macro.
        if (IntegerLiteral *IE =
              dyn_cast<IntegerLiteral>(BO->getRHS()->IgnoreParens()))
          if (IE->getValue() == 0)
            return false;
        return BO->getRHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
      // Consider '||', '&&' to have side effects if the LHS or RHS does.
      case BO_LAnd:
      case BO_LOr:
        if (!BO->getLHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx) ||
            !BO->getRHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx))
          return false;
        break;
    }
    if (BO->isAssignmentOp())
      return false;
    WarnE = this;
    Loc = BO->getOperatorLoc();
    R1 = BO->getLHS()->getSourceRange();
    R2 = BO->getRHS()->getSourceRange();
    return true;
  }
  case CompoundAssignOperatorClass:
  case VAArgExprClass:
  case AtomicExprClass:
    return false;

  case ConditionalOperatorClass: {
    // If only one of the LHS or RHS is a warning, the operator might
    // be being used for control flow. Only warn if both the LHS and
    // RHS are warnings.
    const ConditionalOperator *Exp = cast<ConditionalOperator>(this);
    if (!Exp->getRHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx))
      return false;
    if (!Exp->getLHS())
      return true;
    return Exp->getLHS()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }

  case MemberExprClass:
    WarnE = this;
    Loc = cast<MemberExpr>(this)->getMemberLoc();
    R1 = SourceRange(Loc, Loc);
    R2 = cast<MemberExpr>(this)->getBase()->getSourceRange();
    return true;

  case ArraySubscriptExprClass:
    WarnE = this;
    Loc = cast<ArraySubscriptExpr>(this)->getRBracketLoc();
    R1 = cast<ArraySubscriptExpr>(this)->getLHS()->getSourceRange();
    R2 = cast<ArraySubscriptExpr>(this)->getRHS()->getSourceRange();
    return true;

  case CXXOperatorCallExprClass: {
    // Warn about operator ==,!=,<,>,<=, and >= even when user-defined operator
    // overloads as there is no reasonable way to define these such that they
    // have non-trivial, desirable side-effects. See the -Wunused-comparison
    // warning: operators == and != are commonly typo'ed, and so warning on them
    // provides additional value as well. If this list is updated,
    // DiagnoseUnusedComparison should be as well.
    const CXXOperatorCallExpr *Op = cast<CXXOperatorCallExpr>(this);
    switch (Op->getOperator()) {
    default:
      break;
    case OO_EqualEqual:
    case OO_ExclaimEqual:
    case OO_Less:
    case OO_Greater:
    case OO_GreaterEqual:
    case OO_LessEqual:
      if (Op->getCallReturnType(Ctx)->isReferenceType() ||
          Op->getCallReturnType(Ctx)->isVoidType())
        break;
      WarnE = this;
      Loc = Op->getOperatorLoc();
      R1 = Op->getSourceRange();
      return true;
    }

    // Fallthrough for generic call handling.
    LLVM_FALLTHROUGH;
  }
  case CallExprClass:
  case CXXMemberCallExprClass:
  case UserDefinedLiteralClass: {
    // If this is a direct call, get the callee.
    const CallExpr *CE = cast<CallExpr>(this);
    if (const Decl *FD = CE->getCalleeDecl()) {
      // If the callee has attribute pure, const, or warn_unused_result, warn
      // about it. void foo() { strlen("bar"); } should warn.
      //
      // Note: If new cases are added here, DiagnoseUnusedExprResult should be
      // updated to match for QoI.
      if (CE->hasUnusedResultAttr(Ctx) ||
          FD->hasAttr<PureAttr>() || FD->hasAttr<ConstAttr>()) {
        WarnE = this;
        Loc = CE->getCallee()->getBeginLoc();
        R1 = CE->getCallee()->getSourceRange();

        if (unsigned NumArgs = CE->getNumArgs())
          R2 = SourceRange(CE->getArg(0)->getBeginLoc(),
                           CE->getArg(NumArgs - 1)->getEndLoc());
        return true;
      }
    }
    return false;
  }

  // If we don't know precisely what we're looking at, let's not warn.
  case UnresolvedLookupExprClass:
  case CXXUnresolvedConstructExprClass:
    return false;

  case CXXTemporaryObjectExprClass:
  case CXXConstructExprClass: {
    if (const CXXRecordDecl *Type = getType()->getAsCXXRecordDecl()) {
      if (Type->hasAttr<WarnUnusedAttr>()) {
        WarnE = this;
        Loc = getBeginLoc();
        R1 = getSourceRange();
        return true;
      }
    }
    return false;
  }

  case ObjCMessageExprClass: {
    const ObjCMessageExpr *ME = cast<ObjCMessageExpr>(this);
    if (Ctx.getLangOpts().ObjCAutoRefCount &&
        ME->isInstanceMessage() &&
        !ME->getType()->isVoidType() &&
        ME->getMethodFamily() == OMF_init) {
      WarnE = this;
      Loc = getExprLoc();
      R1 = ME->getSourceRange();
      return true;
    }

    if (const ObjCMethodDecl *MD = ME->getMethodDecl())
      if (MD->hasAttr<WarnUnusedResultAttr>()) {
        WarnE = this;
        Loc = getExprLoc();
        return true;
      }

    return false;
  }

  case ObjCPropertyRefExprClass:
    WarnE = this;
    Loc = getExprLoc();
    R1 = getSourceRange();
    return true;

  case PseudoObjectExprClass: {
    const PseudoObjectExpr *PO = cast<PseudoObjectExpr>(this);

    // Only complain about things that have the form of a getter.
    if (isa<UnaryOperator>(PO->getSyntacticForm()) ||
        isa<BinaryOperator>(PO->getSyntacticForm()))
      return false;

    WarnE = this;
    Loc = getExprLoc();
    R1 = getSourceRange();
    return true;
  }

  case StmtExprClass: {
    // Statement exprs don't logically have side effects themselves, but are
    // sometimes used in macros in ways that give them a type that is unused.
    // For example ({ blah; foo(); }) will end up with a type if foo has a type.
    // however, if the result of the stmt expr is dead, we don't want to emit a
    // warning.
    const CompoundStmt *CS = cast<StmtExpr>(this)->getSubStmt();
    if (!CS->body_empty()) {
      if (const Expr *E = dyn_cast<Expr>(CS->body_back()))
        return E->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
      if (const LabelStmt *Label = dyn_cast<LabelStmt>(CS->body_back()))
        if (const Expr *E = dyn_cast<Expr>(Label->getSubStmt()))
          return E->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
    }

    if (getType()->isVoidType())
      return false;
    WarnE = this;
    Loc = cast<StmtExpr>(this)->getLParenLoc();
    R1 = getSourceRange();
    return true;
  }
  case CXXFunctionalCastExprClass:
  case CStyleCastExprClass: {
    // Ignore an explicit cast to void unless the operand is a non-trivial
    // volatile lvalue.
    const CastExpr *CE = cast<CastExpr>(this);
    if (CE->getCastKind() == CK_ToVoid) {
      if (CE->getSubExpr()->isGLValue() &&
          CE->getSubExpr()->getType().isVolatileQualified()) {
        const DeclRefExpr *DRE =
            dyn_cast<DeclRefExpr>(CE->getSubExpr()->IgnoreParens());
        if (!(DRE && isa<VarDecl>(DRE->getDecl()) &&
              cast<VarDecl>(DRE->getDecl())->hasLocalStorage()) &&
            !isa<CallExpr>(CE->getSubExpr()->IgnoreParens())) {
          return CE->getSubExpr()->isUnusedResultAWarning(WarnE, Loc,
                                                          R1, R2, Ctx);
        }
      }
      return false;
    }

    // If this is a cast to a constructor conversion, check the operand.
    // Otherwise, the result of the cast is unused.
    if (CE->getCastKind() == CK_ConstructorConversion)
      return CE->getSubExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);

    WarnE = this;
    if (const CXXFunctionalCastExpr *CXXCE =
            dyn_cast<CXXFunctionalCastExpr>(this)) {
      Loc = CXXCE->getBeginLoc();
      R1 = CXXCE->getSubExpr()->getSourceRange();
    } else {
      const CStyleCastExpr *CStyleCE = cast<CStyleCastExpr>(this);
      Loc = CStyleCE->getLParenLoc();
      R1 = CStyleCE->getSubExpr()->getSourceRange();
    }
    return true;
  }
  case ImplicitCastExprClass: {
    const CastExpr *ICE = cast<ImplicitCastExpr>(this);

    // lvalue-to-rvalue conversion on a volatile lvalue is a side-effect.
    if (ICE->getCastKind() == CK_LValueToRValue &&
        ICE->getSubExpr()->getType().isVolatileQualified())
      return false;

    return ICE->getSubExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }
  case CXXDefaultArgExprClass:
    return (cast<CXXDefaultArgExpr>(this)
            ->getExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx));
  case CXXDefaultInitExprClass:
    return (cast<CXXDefaultInitExpr>(this)
            ->getExpr()->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx));

  case CXXNewExprClass:
    // FIXME: In theory, there might be new expressions that don't have side
    // effects (e.g. a placement new with an uninitialized POD).
  case CXXDeleteExprClass:
    return false;
  case MaterializeTemporaryExprClass:
    return cast<MaterializeTemporaryExpr>(this)->GetTemporaryExpr()
               ->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case CXXBindTemporaryExprClass:
    return cast<CXXBindTemporaryExpr>(this)->getSubExpr()
               ->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  case ExprWithCleanupsClass:
    return cast<ExprWithCleanups>(this)->getSubExpr()
               ->isUnusedResultAWarning(WarnE, Loc, R1, R2, Ctx);
  }
}

/// isOBJCGCCandidate - Check if an expression is objc gc'able.
/// returns true, if it is; false otherwise.
bool Expr::isOBJCGCCandidate(ASTContext &Ctx) const {
  const Expr *E = IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    return false;
  case ObjCIvarRefExprClass:
    return true;
  case Expr::UnaryOperatorClass:
    return cast<UnaryOperator>(E)->getSubExpr()->isOBJCGCCandidate(Ctx);
  case ImplicitCastExprClass:
    return cast<ImplicitCastExpr>(E)->getSubExpr()->isOBJCGCCandidate(Ctx);
  case MaterializeTemporaryExprClass:
    return cast<MaterializeTemporaryExpr>(E)->GetTemporaryExpr()
                                                      ->isOBJCGCCandidate(Ctx);
  case CStyleCastExprClass:
    return cast<CStyleCastExpr>(E)->getSubExpr()->isOBJCGCCandidate(Ctx);
  case DeclRefExprClass: {
    const Decl *D = cast<DeclRefExpr>(E)->getDecl();

    if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
      if (VD->hasGlobalStorage())
        return true;
      QualType T = VD->getType();
      // dereferencing to a  pointer is always a gc'able candidate,
      // unless it is __weak.
      return T->isPointerType() &&
             (Ctx.getObjCGCAttrKind(T) != Qualifiers::Weak);
    }
    return false;
  }
  case MemberExprClass: {
    const MemberExpr *M = cast<MemberExpr>(E);
    return M->getBase()->isOBJCGCCandidate(Ctx);
  }
  case ArraySubscriptExprClass:
    return cast<ArraySubscriptExpr>(E)->getBase()->isOBJCGCCandidate(Ctx);
  }
}

bool Expr::isBoundMemberFunction(ASTContext &Ctx) const {
  if (isTypeDependent())
    return false;
  return ClassifyLValue(Ctx) == Expr::LV_MemberFunction;
}

QualType Expr::findBoundMemberType(const Expr *expr) {
  assert(expr->hasPlaceholderType(BuiltinType::BoundMember));

  // Bound member expressions are always one of these possibilities:
  //   x->m      x.m      x->*y      x.*y
  // (possibly parenthesized)

  expr = expr->IgnoreParens();
  if (const MemberExpr *mem = dyn_cast<MemberExpr>(expr)) {
    assert(isa<CXXMethodDecl>(mem->getMemberDecl()));
    return mem->getMemberDecl()->getType();
  }

  if (const BinaryOperator *op = dyn_cast<BinaryOperator>(expr)) {
    QualType type = op->getRHS()->getType()->castAs<MemberPointerType>()
                      ->getPointeeType();
    assert(type->isFunctionType());
    return type;
  }

  assert(isa<UnresolvedMemberExpr>(expr) || isa<CXXPseudoDestructorExpr>(expr));
  return QualType();
}

Expr* Expr::IgnoreParens() {
  Expr* E = this;
  while (true) {
    if (ParenExpr* P = dyn_cast<ParenExpr>(E)) {
      E = P->getSubExpr();
      continue;
    }
    if (UnaryOperator* P = dyn_cast<UnaryOperator>(E)) {
      if (P->getOpcode() == UO_Extension) {
        E = P->getSubExpr();
        continue;
      }
    }
    if (GenericSelectionExpr* P = dyn_cast<GenericSelectionExpr>(E)) {
      if (!P->isResultDependent()) {
        E = P->getResultExpr();
        continue;
      }
    }
    if (ChooseExpr* P = dyn_cast<ChooseExpr>(E)) {
      if (!P->isConditionDependent()) {
        E = P->getChosenSubExpr();
        continue;
      }
    }
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(E)) {
      E = CE->getSubExpr();
      continue;
    }
    return E;
  }
}

/// IgnoreParenCasts - Ignore parentheses and casts.  Strip off any ParenExpr
/// or CastExprs or ImplicitCastExprs, returning their operand.
Expr *Expr::IgnoreParenCasts() {
  Expr *E = this;
  while (true) {
    E = E->IgnoreParens();
    if (CastExpr *P = dyn_cast<CastExpr>(E)) {
      E = P->getSubExpr();
      continue;
    }
    if (MaterializeTemporaryExpr *Materialize
                                      = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Materialize->GetTemporaryExpr();
      continue;
    }
    if (SubstNonTypeTemplateParmExpr *NTTP
                                  = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
      E = NTTP->getReplacement();
      continue;
    }
    if (FullExpr *FE = dyn_cast<FullExpr>(E)) {
      E = FE->getSubExpr();
      continue;
    }
    return E;
  }
}

Expr *Expr::IgnoreCasts() {
  Expr *E = this;
  while (true) {
    if (CastExpr *P = dyn_cast<CastExpr>(E)) {
      E = P->getSubExpr();
      continue;
    }
    if (MaterializeTemporaryExpr *Materialize
        = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Materialize->GetTemporaryExpr();
      continue;
    }
    if (SubstNonTypeTemplateParmExpr *NTTP
        = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
      E = NTTP->getReplacement();
      continue;
    }
    if (FullExpr *FE = dyn_cast<FullExpr>(E)) {
      E = FE->getSubExpr();
      continue;
    }
    return E;
  }
}

/// IgnoreParenLValueCasts - Ignore parentheses and lvalue-to-rvalue
/// casts.  This is intended purely as a temporary workaround for code
/// that hasn't yet been rewritten to do the right thing about those
/// casts, and may disappear along with the last internal use.
Expr *Expr::IgnoreParenLValueCasts() {
  Expr *E = this;
  while (true) {
    E = E->IgnoreParens();
    if (CastExpr *P = dyn_cast<CastExpr>(E)) {
      if (P->getCastKind() == CK_LValueToRValue) {
        E = P->getSubExpr();
        continue;
      }
    } else if (MaterializeTemporaryExpr *Materialize
                                      = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Materialize->GetTemporaryExpr();
      continue;
    } else if (SubstNonTypeTemplateParmExpr *NTTP
                                  = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
      E = NTTP->getReplacement();
      continue;
    } else if (FullExpr *FE = dyn_cast<FullExpr>(E)) {
      E = FE->getSubExpr();
      continue;
    }
    break;
  }
  return E;
}

Expr *Expr::ignoreParenBaseCasts() {
  Expr *E = this;
  while (true) {
    E = E->IgnoreParens();
    if (CastExpr *CE = dyn_cast<CastExpr>(E)) {
      if (CE->getCastKind() == CK_DerivedToBase ||
          CE->getCastKind() == CK_UncheckedDerivedToBase ||
          CE->getCastKind() == CK_NoOp) {
        E = CE->getSubExpr();
        continue;
      }
    }

    return E;
  }
}

Expr *Expr::IgnoreParenImpCasts() {
  Expr *E = this;
  while (true) {
    E = E->IgnoreParens();
    if (ImplicitCastExpr *P = dyn_cast<ImplicitCastExpr>(E)) {
      E = P->getSubExpr();
      continue;
    }
    if (MaterializeTemporaryExpr *Materialize
                                      = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = Materialize->GetTemporaryExpr();
      continue;
    }
    if (SubstNonTypeTemplateParmExpr *NTTP
                                  = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
      E = NTTP->getReplacement();
      continue;
    }
    return E;
  }
}

Expr *Expr::IgnoreConversionOperator() {
  if (CXXMemberCallExpr *MCE = dyn_cast<CXXMemberCallExpr>(this)) {
    if (MCE->getMethodDecl() && isa<CXXConversionDecl>(MCE->getMethodDecl()))
      return MCE->getImplicitObjectArgument();
  }
  return this;
}

/// IgnoreParenNoopCasts - Ignore parentheses and casts that do not change the
/// value (including ptr->int casts of the same size).  Strip off any
/// ParenExpr or CastExprs, returning their operand.
Expr *Expr::IgnoreParenNoopCasts(ASTContext &Ctx) {
  Expr *E = this;
  while (true) {
    E = E->IgnoreParens();

    if (CastExpr *P = dyn_cast<CastExpr>(E)) {
      // We ignore integer <-> casts that are of the same width, ptr<->ptr and
      // ptr<->int casts of the same width.  We also ignore all identity casts.
      Expr *SE = P->getSubExpr();

      if (Ctx.hasSameUnqualifiedType(E->getType(), SE->getType())) {
        E = SE;
        continue;
      }

      if ((E->getType()->isPointerType() ||
           E->getType()->isIntegralType(Ctx)) &&
          (SE->getType()->isPointerType() ||
           SE->getType()->isIntegralType(Ctx)) &&
          Ctx.getTypeSize(E->getType()) == Ctx.getTypeSize(SE->getType())) {
        E = SE;
        continue;
      }
    }

    if (SubstNonTypeTemplateParmExpr *NTTP
                                  = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
      E = NTTP->getReplacement();
      continue;
    }

    return E;
  }
}

bool Expr::isDefaultArgument() const {
  const Expr *E = this;
  if (const MaterializeTemporaryExpr *M = dyn_cast<MaterializeTemporaryExpr>(E))
    E = M->GetTemporaryExpr();

  while (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E))
    E = ICE->getSubExprAsWritten();

  return isa<CXXDefaultArgExpr>(E);
}

/// Skip over any no-op casts and any temporary-binding
/// expressions.
static const Expr *skipTemporaryBindingsNoOpCastsAndParens(const Expr *E) {
  if (const MaterializeTemporaryExpr *M = dyn_cast<MaterializeTemporaryExpr>(E))
    E = M->GetTemporaryExpr();

  while (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() == CK_NoOp)
      E = ICE->getSubExpr();
    else
      break;
  }

  while (const CXXBindTemporaryExpr *BE = dyn_cast<CXXBindTemporaryExpr>(E))
    E = BE->getSubExpr();

  while (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() == CK_NoOp)
      E = ICE->getSubExpr();
    else
      break;
  }

  return E->IgnoreParens();
}

/// isTemporaryObject - Determines if this expression produces a
/// temporary of the given class type.
bool Expr::isTemporaryObject(ASTContext &C, const CXXRecordDecl *TempTy) const {
  if (!C.hasSameUnqualifiedType(getType(), C.getTypeDeclType(TempTy)))
    return false;

  const Expr *E = skipTemporaryBindingsNoOpCastsAndParens(this);

  // Temporaries are by definition pr-values of class type.
  if (!E->Classify(C).isPRValue()) {
    // In this context, property reference is a message call and is pr-value.
    if (!isa<ObjCPropertyRefExpr>(E))
      return false;
  }

  // Black-list a few cases which yield pr-values of class type that don't
  // refer to temporaries of that type:

  // - implicit derived-to-base conversions
  if (isa<ImplicitCastExpr>(E)) {
    switch (cast<ImplicitCastExpr>(E)->getCastKind()) {
    case CK_DerivedToBase:
    case CK_UncheckedDerivedToBase:
      return false;
    default:
      break;
    }
  }

  // - member expressions (all)
  if (isa<MemberExpr>(E))
    return false;

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E))
    if (BO->isPtrMemOp())
      return false;

  // - opaque values (all)
  if (isa<OpaqueValueExpr>(E))
    return false;

  return true;
}

bool Expr::isImplicitCXXThis() const {
  const Expr *E = this;

  // Strip away parentheses and casts we don't care about.
  while (true) {
    if (const ParenExpr *Paren = dyn_cast<ParenExpr>(E)) {
      E = Paren->getSubExpr();
      continue;
    }

    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
      if (ICE->getCastKind() == CK_NoOp ||
          ICE->getCastKind() == CK_LValueToRValue ||
          ICE->getCastKind() == CK_DerivedToBase ||
          ICE->getCastKind() == CK_UncheckedDerivedToBase) {
        E = ICE->getSubExpr();
        continue;
      }
    }

    if (const UnaryOperator* UnOp = dyn_cast<UnaryOperator>(E)) {
      if (UnOp->getOpcode() == UO_Extension) {
        E = UnOp->getSubExpr();
        continue;
      }
    }

    if (const MaterializeTemporaryExpr *M
                                      = dyn_cast<MaterializeTemporaryExpr>(E)) {
      E = M->GetTemporaryExpr();
      continue;
    }

    break;
  }

  if (const CXXThisExpr *This = dyn_cast<CXXThisExpr>(E))
    return This->isImplicit();

  return false;
}

/// hasAnyTypeDependentArguments - Determines if any of the expressions
/// in Exprs is type-dependent.
bool Expr::hasAnyTypeDependentArguments(ArrayRef<Expr *> Exprs) {
  for (unsigned I = 0; I < Exprs.size(); ++I)
    if (Exprs[I]->isTypeDependent())
      return true;

  return false;
}

bool Expr::isConstantInitializer(ASTContext &Ctx, bool IsForRef,
                                 const Expr **Culprit) const {
  // This function is attempting whether an expression is an initializer
  // which can be evaluated at compile-time. It very closely parallels
  // ConstExprEmitter in CGExprConstant.cpp; if they don't match, it
  // will lead to unexpected results.  Like ConstExprEmitter, it falls back
  // to isEvaluatable most of the time.
  //
  // If we ever capture reference-binding directly in the AST, we can
  // kill the second parameter.

  if (IsForRef) {
    EvalResult Result;
    if (EvaluateAsLValue(Result, Ctx) && !Result.HasSideEffects)
      return true;
    if (Culprit)
      *Culprit = this;
    return false;
  }

  switch (getStmtClass()) {
  default: break;
  case StringLiteralClass:
  case ObjCEncodeExprClass:
    return true;
  case CXXTemporaryObjectExprClass:
  case CXXConstructExprClass: {
    const CXXConstructExpr *CE = cast<CXXConstructExpr>(this);

    if (CE->getConstructor()->isTrivial() &&
        CE->getConstructor()->getParent()->hasTrivialDestructor()) {
      // Trivial default constructor
      if (!CE->getNumArgs()) return true;

      // Trivial copy constructor
      assert(CE->getNumArgs() == 1 && "trivial ctor with > 1 argument");
      return CE->getArg(0)->isConstantInitializer(Ctx, false, Culprit);
    }

    break;
  }
  case ConstantExprClass: {
    // FIXME: We should be able to return "true" here, but it can lead to extra
    // error messages. E.g. in Sema/array-init.c.
    const Expr *Exp = cast<ConstantExpr>(this)->getSubExpr();
    return Exp->isConstantInitializer(Ctx, false, Culprit);
  }
  case CompoundLiteralExprClass: {
    // This handles gcc's extension that allows global initializers like
    // "struct x {int x;} x = (struct x) {};".
    // FIXME: This accepts other cases it shouldn't!
    const Expr *Exp = cast<CompoundLiteralExpr>(this)->getInitializer();
    return Exp->isConstantInitializer(Ctx, false, Culprit);
  }
  case DesignatedInitUpdateExprClass: {
    const DesignatedInitUpdateExpr *DIUE = cast<DesignatedInitUpdateExpr>(this);
    return DIUE->getBase()->isConstantInitializer(Ctx, false, Culprit) &&
           DIUE->getUpdater()->isConstantInitializer(Ctx, false, Culprit);
  }
  case InitListExprClass: {
    const InitListExpr *ILE = cast<InitListExpr>(this);
    if (ILE->getType()->isArrayType()) {
      unsigned numInits = ILE->getNumInits();
      for (unsigned i = 0; i < numInits; i++) {
        if (!ILE->getInit(i)->isConstantInitializer(Ctx, false, Culprit))
          return false;
      }
      return true;
    }

    if (ILE->getType()->isRecordType()) {
      unsigned ElementNo = 0;
      RecordDecl *RD = ILE->getType()->getAs<RecordType>()->getDecl();
      for (const auto *Field : RD->fields()) {
        // If this is a union, skip all the fields that aren't being initialized.
        if (RD->isUnion() && ILE->getInitializedFieldInUnion() != Field)
          continue;

        // Don't emit anonymous bitfields, they just affect layout.
        if (Field->isUnnamedBitfield())
          continue;

        if (ElementNo < ILE->getNumInits()) {
          const Expr *Elt = ILE->getInit(ElementNo++);
          if (Field->isBitField()) {
            // Bitfields have to evaluate to an integer.
            EvalResult Result;
            if (!Elt->EvaluateAsInt(Result, Ctx)) {
              if (Culprit)
                *Culprit = Elt;
              return false;
            }
          } else {
            bool RefType = Field->getType()->isReferenceType();
            if (!Elt->isConstantInitializer(Ctx, RefType, Culprit))
              return false;
          }
        }
      }
      return true;
    }

    break;
  }
  case ImplicitValueInitExprClass:
  case NoInitExprClass:
    return true;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()
      ->isConstantInitializer(Ctx, IsForRef, Culprit);
  case GenericSelectionExprClass:
    return cast<GenericSelectionExpr>(this)->getResultExpr()
      ->isConstantInitializer(Ctx, IsForRef, Culprit);
  case ChooseExprClass:
    if (cast<ChooseExpr>(this)->isConditionDependent()) {
      if (Culprit)
        *Culprit = this;
      return false;
    }
    return cast<ChooseExpr>(this)->getChosenSubExpr()
      ->isConstantInitializer(Ctx, IsForRef, Culprit);
  case UnaryOperatorClass: {
    const UnaryOperator* Exp = cast<UnaryOperator>(this);
    if (Exp->getOpcode() == UO_Extension)
      return Exp->getSubExpr()->isConstantInitializer(Ctx, false, Culprit);
    break;
  }
  case CXXFunctionalCastExprClass:
  case CXXStaticCastExprClass:
  case ImplicitCastExprClass:
  case CStyleCastExprClass:
  case ObjCBridgedCastExprClass:
  case CXXDynamicCastExprClass:
  case CXXReinterpretCastExprClass:
  case CXXConstCastExprClass: {
    const CastExpr *CE = cast<CastExpr>(this);

    // Handle misc casts we want to ignore.
    if (CE->getCastKind() == CK_NoOp ||
        CE->getCastKind() == CK_LValueToRValue ||
        CE->getCastKind() == CK_ToUnion ||
        CE->getCastKind() == CK_ConstructorConversion ||
        CE->getCastKind() == CK_NonAtomicToAtomic ||
        CE->getCastKind() == CK_AtomicToNonAtomic ||
        CE->getCastKind() == CK_IntToOCLSampler)
      return CE->getSubExpr()->isConstantInitializer(Ctx, false, Culprit);

    break;
  }
  case MaterializeTemporaryExprClass:
    return cast<MaterializeTemporaryExpr>(this)->GetTemporaryExpr()
      ->isConstantInitializer(Ctx, false, Culprit);

  case SubstNonTypeTemplateParmExprClass:
    return cast<SubstNonTypeTemplateParmExpr>(this)->getReplacement()
      ->isConstantInitializer(Ctx, false, Culprit);
  case CXXDefaultArgExprClass:
    return cast<CXXDefaultArgExpr>(this)->getExpr()
      ->isConstantInitializer(Ctx, false, Culprit);
  case CXXDefaultInitExprClass:
    return cast<CXXDefaultInitExpr>(this)->getExpr()
      ->isConstantInitializer(Ctx, false, Culprit);
  }
  // Allow certain forms of UB in constant initializers: signed integer
  // overflow and floating-point division by zero. We'll give a warning on
  // these, but they're common enough that we have to accept them.
  if (isEvaluatable(Ctx, SE_AllowUndefinedBehavior))
    return true;
  if (Culprit)
    *Culprit = this;
  return false;
}

bool CallExpr::isBuiltinAssumeFalse(const ASTContext &Ctx) const {
  const FunctionDecl* FD = getDirectCallee();
  if (!FD || (FD->getBuiltinID() != Builtin::BI__assume &&
              FD->getBuiltinID() != Builtin::BI__builtin_assume))
    return false;

  const Expr* Arg = getArg(0);
  bool ArgVal;
  return !Arg->isValueDependent() &&
         Arg->EvaluateAsBooleanCondition(ArgVal, Ctx) && !ArgVal;
}

namespace {
  /// Look for any side effects within a Stmt.
  class SideEffectFinder : public ConstEvaluatedExprVisitor<SideEffectFinder> {
    typedef ConstEvaluatedExprVisitor<SideEffectFinder> Inherited;
    const bool IncludePossibleEffects;
    bool HasSideEffects;

  public:
    explicit SideEffectFinder(const ASTContext &Context, bool IncludePossible)
      : Inherited(Context),
        IncludePossibleEffects(IncludePossible), HasSideEffects(false) { }

    bool hasSideEffects() const { return HasSideEffects; }

    void VisitExpr(const Expr *E) {
      if (!HasSideEffects &&
          E->HasSideEffects(Context, IncludePossibleEffects))
        HasSideEffects = true;
    }
  };
}

bool Expr::HasSideEffects(const ASTContext &Ctx,
                          bool IncludePossibleEffects) const {
  // In circumstances where we care about definite side effects instead of
  // potential side effects, we want to ignore expressions that are part of a
  // macro expansion as a potential side effect.
  if (!IncludePossibleEffects && getExprLoc().isMacroID())
    return false;

  if (isInstantiationDependent())
    return IncludePossibleEffects;

  switch (getStmtClass()) {
  case NoStmtClass:
  #define ABSTRACT_STMT(Type)
  #define STMT(Type, Base) case Type##Class:
  #define EXPR(Type, Base)
  #include "clang/AST/StmtNodes.inc"
    llvm_unreachable("unexpected Expr kind");

  case DependentScopeDeclRefExprClass:
  case CXXUnresolvedConstructExprClass:
  case CXXDependentScopeMemberExprClass:
  case UnresolvedLookupExprClass:
  case UnresolvedMemberExprClass:
  case PackExpansionExprClass:
  case SubstNonTypeTemplateParmPackExprClass:
  case FunctionParmPackExprClass:
  case TypoExprClass:
  case CXXFoldExprClass:
    llvm_unreachable("shouldn't see dependent / unresolved nodes here");

  case DeclRefExprClass:
  case ObjCIvarRefExprClass:
  case PredefinedExprClass:
  case IntegerLiteralClass:
  case FixedPointLiteralClass:
  case FloatingLiteralClass:
  case ImaginaryLiteralClass:
  case StringLiteralClass:
  case CharacterLiteralClass:
  case OffsetOfExprClass:
  case ImplicitValueInitExprClass:
  case UnaryExprOrTypeTraitExprClass:
  case AddrLabelExprClass:
  case GNUNullExprClass:
  case ArrayInitIndexExprClass:
  case NoInitExprClass:
  case CXXBoolLiteralExprClass:
  case CXXNullPtrLiteralExprClass:
  case CXXThisExprClass:
  case CXXScalarValueInitExprClass:
  case TypeTraitExprClass:
  case ArrayTypeTraitExprClass:
  case ExpressionTraitExprClass:
  case CXXNoexceptExprClass:
  case SizeOfPackExprClass:
  case ObjCStringLiteralClass:
  case ObjCEncodeExprClass:
  case ObjCBoolLiteralExprClass:
  case ObjCAvailabilityCheckExprClass:
  case CXXUuidofExprClass:
  case OpaqueValueExprClass:
    // These never have a side-effect.
    return false;

  case ConstantExprClass:
    // FIXME: Move this into the "return false;" block above.
    return cast<ConstantExpr>(this)->getSubExpr()->HasSideEffects(
        Ctx, IncludePossibleEffects);

  case CallExprClass:
  case CXXOperatorCallExprClass:
  case CXXMemberCallExprClass:
  case CUDAKernelCallExprClass:
  case UserDefinedLiteralClass: {
    // We don't know a call definitely has side effects, except for calls
    // to pure/const functions that definitely don't.
    // If the call itself is considered side-effect free, check the operands.
    const Decl *FD = cast<CallExpr>(this)->getCalleeDecl();
    bool IsPure = FD && (FD->hasAttr<ConstAttr>() || FD->hasAttr<PureAttr>());
    if (IsPure || !IncludePossibleEffects)
      break;
    return true;
  }

  case BlockExprClass:
  case CXXBindTemporaryExprClass:
    if (!IncludePossibleEffects)
      break;
    return true;

  case MSPropertyRefExprClass:
  case MSPropertySubscriptExprClass:
  case CompoundAssignOperatorClass:
  case VAArgExprClass:
  case AtomicExprClass:
  case CXXThrowExprClass:
  case CXXNewExprClass:
  case CXXDeleteExprClass:
  case CoawaitExprClass:
  case DependentCoawaitExprClass:
  case CoyieldExprClass:
    // These always have a side-effect.
    return true;

  case StmtExprClass: {
    // StmtExprs have a side-effect if any substatement does.
    SideEffectFinder Finder(Ctx, IncludePossibleEffects);
    Finder.Visit(cast<StmtExpr>(this)->getSubStmt());
    return Finder.hasSideEffects();
  }

  case ExprWithCleanupsClass:
    if (IncludePossibleEffects)
      if (cast<ExprWithCleanups>(this)->cleanupsHaveSideEffects())
        return true;
    break;

  case ParenExprClass:
  case ArraySubscriptExprClass:
  case OMPArraySectionExprClass:
  case MemberExprClass:
  case ConditionalOperatorClass:
  case BinaryConditionalOperatorClass:
  case CompoundLiteralExprClass:
  case ExtVectorElementExprClass:
  case DesignatedInitExprClass:
  case DesignatedInitUpdateExprClass:
  case ArrayInitLoopExprClass:
  case ParenListExprClass:
  case CXXPseudoDestructorExprClass:
  case CXXStdInitializerListExprClass:
  case SubstNonTypeTemplateParmExprClass:
  case MaterializeTemporaryExprClass:
  case ShuffleVectorExprClass:
  case ConvertVectorExprClass:
  case AsTypeExprClass:
    // These have a side-effect if any subexpression does.
    break;

  case UnaryOperatorClass:
    if (cast<UnaryOperator>(this)->isIncrementDecrementOp())
      return true;
    break;

  case BinaryOperatorClass:
    if (cast<BinaryOperator>(this)->isAssignmentOp())
      return true;
    break;

  case InitListExprClass:
    // FIXME: The children for an InitListExpr doesn't include the array filler.
    if (const Expr *E = cast<InitListExpr>(this)->getArrayFiller())
      if (E->HasSideEffects(Ctx, IncludePossibleEffects))
        return true;
    break;

  case GenericSelectionExprClass:
    return cast<GenericSelectionExpr>(this)->getResultExpr()->
        HasSideEffects(Ctx, IncludePossibleEffects);

  case ChooseExprClass:
    return cast<ChooseExpr>(this)->getChosenSubExpr()->HasSideEffects(
        Ctx, IncludePossibleEffects);

  case CXXDefaultArgExprClass:
    return cast<CXXDefaultArgExpr>(this)->getExpr()->HasSideEffects(
        Ctx, IncludePossibleEffects);

  case CXXDefaultInitExprClass: {
    const FieldDecl *FD = cast<CXXDefaultInitExpr>(this)->getField();
    if (const Expr *E = FD->getInClassInitializer())
      return E->HasSideEffects(Ctx, IncludePossibleEffects);
    // If we've not yet parsed the initializer, assume it has side-effects.
    return true;
  }

  case CXXDynamicCastExprClass: {
    // A dynamic_cast expression has side-effects if it can throw.
    const CXXDynamicCastExpr *DCE = cast<CXXDynamicCastExpr>(this);
    if (DCE->getTypeAsWritten()->isReferenceType() &&
        DCE->getCastKind() == CK_Dynamic)
      return true;
    }
    LLVM_FALLTHROUGH;
  case ImplicitCastExprClass:
  case CStyleCastExprClass:
  case CXXStaticCastExprClass:
  case CXXReinterpretCastExprClass:
  case CXXConstCastExprClass:
  case CXXFunctionalCastExprClass: {
    // While volatile reads are side-effecting in both C and C++, we treat them
    // as having possible (not definite) side-effects. This allows idiomatic
    // code to behave without warning, such as sizeof(*v) for a volatile-
    // qualified pointer.
    if (!IncludePossibleEffects)
      break;

    const CastExpr *CE = cast<CastExpr>(this);
    if (CE->getCastKind() == CK_LValueToRValue &&
        CE->getSubExpr()->getType().isVolatileQualified())
      return true;
    break;
  }

  case CXXTypeidExprClass:
    // typeid might throw if its subexpression is potentially-evaluated, so has
    // side-effects in that case whether or not its subexpression does.
    return cast<CXXTypeidExpr>(this)->isPotentiallyEvaluated();

  case CXXConstructExprClass:
  case CXXTemporaryObjectExprClass: {
    const CXXConstructExpr *CE = cast<CXXConstructExpr>(this);
    if (!CE->getConstructor()->isTrivial() && IncludePossibleEffects)
      return true;
    // A trivial constructor does not add any side-effects of its own. Just look
    // at its arguments.
    break;
  }

  case CXXInheritedCtorInitExprClass: {
    const auto *ICIE = cast<CXXInheritedCtorInitExpr>(this);
    if (!ICIE->getConstructor()->isTrivial() && IncludePossibleEffects)
      return true;
    break;
  }

  case LambdaExprClass: {
    const LambdaExpr *LE = cast<LambdaExpr>(this);
    for (Expr *E : LE->capture_inits())
      if (E->HasSideEffects(Ctx, IncludePossibleEffects))
        return true;
    return false;
  }

  case PseudoObjectExprClass: {
    // Only look for side-effects in the semantic form, and look past
    // OpaqueValueExpr bindings in that form.
    const PseudoObjectExpr *PO = cast<PseudoObjectExpr>(this);
    for (PseudoObjectExpr::const_semantics_iterator I = PO->semantics_begin(),
                                                    E = PO->semantics_end();
         I != E; ++I) {
      const Expr *Subexpr = *I;
      if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(Subexpr))
        Subexpr = OVE->getSourceExpr();
      if (Subexpr->HasSideEffects(Ctx, IncludePossibleEffects))
        return true;
    }
    return false;
  }

  case ObjCBoxedExprClass:
  case ObjCArrayLiteralClass:
  case ObjCDictionaryLiteralClass:
  case ObjCSelectorExprClass:
  case ObjCProtocolExprClass:
  case ObjCIsaExprClass:
  case ObjCIndirectCopyRestoreExprClass:
  case ObjCSubscriptRefExprClass:
  case ObjCBridgedCastExprClass:
  case ObjCMessageExprClass:
  case ObjCPropertyRefExprClass:
  // FIXME: Classify these cases better.
    if (IncludePossibleEffects)
      return true;
    break;
  }

  // Recurse to children.
  for (const Stmt *SubStmt : children())
    if (SubStmt &&
        cast<Expr>(SubStmt)->HasSideEffects(Ctx, IncludePossibleEffects))
      return true;

  return false;
}

namespace {
  /// Look for a call to a non-trivial function within an expression.
  class NonTrivialCallFinder : public ConstEvaluatedExprVisitor<NonTrivialCallFinder>
  {
    typedef ConstEvaluatedExprVisitor<NonTrivialCallFinder> Inherited;

    bool NonTrivial;

  public:
    explicit NonTrivialCallFinder(const ASTContext &Context)
      : Inherited(Context), NonTrivial(false) { }

    bool hasNonTrivialCall() const { return NonTrivial; }

    void VisitCallExpr(const CallExpr *E) {
      if (const CXXMethodDecl *Method
          = dyn_cast_or_null<const CXXMethodDecl>(E->getCalleeDecl())) {
        if (Method->isTrivial()) {
          // Recurse to children of the call.
          Inherited::VisitStmt(E);
          return;
        }
      }

      NonTrivial = true;
    }

    void VisitCXXConstructExpr(const CXXConstructExpr *E) {
      if (E->getConstructor()->isTrivial()) {
        // Recurse to children of the call.
        Inherited::VisitStmt(E);
        return;
      }

      NonTrivial = true;
    }

    void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *E) {
      if (E->getTemporary()->getDestructor()->isTrivial()) {
        Inherited::VisitStmt(E);
        return;
      }

      NonTrivial = true;
    }
  };
}

bool Expr::hasNonTrivialCall(const ASTContext &Ctx) const {
  NonTrivialCallFinder Finder(Ctx);
  Finder.Visit(this);
  return Finder.hasNonTrivialCall();
}

/// isNullPointerConstant - C99 6.3.2.3p3 - Return whether this is a null
/// pointer constant or not, as well as the specific kind of constant detected.
/// Null pointer constants can be integer constant expressions with the
/// value zero, casts of zero to void*, nullptr (C++0X), or __null
/// (a GNU extension).
Expr::NullPointerConstantKind
Expr::isNullPointerConstant(ASTContext &Ctx,
                            NullPointerConstantValueDependence NPC) const {
  if (isValueDependent() &&
      (!Ctx.getLangOpts().CPlusPlus11 || Ctx.getLangOpts().MSVCCompat)) {
    switch (NPC) {
    case NPC_NeverValueDependent:
      llvm_unreachable("Unexpected value dependent expression!");
    case NPC_ValueDependentIsNull:
      if (isTypeDependent() || getType()->isIntegralType(Ctx))
        return NPCK_ZeroExpression;
      else
        return NPCK_NotNull;

    case NPC_ValueDependentIsNotNull:
      return NPCK_NotNull;
    }
  }

  // Strip off a cast to void*, if it exists. Except in C++.
  if (const ExplicitCastExpr *CE = dyn_cast<ExplicitCastExpr>(this)) {
    if (!Ctx.getLangOpts().CPlusPlus) {
      // Check that it is a cast to void*.
      if (const PointerType *PT = CE->getType()->getAs<PointerType>()) {
        QualType Pointee = PT->getPointeeType();
        Qualifiers Qs = Pointee.getQualifiers();
        // Only (void*)0 or equivalent are treated as nullptr. If pointee type
        // has non-default address space it is not treated as nullptr.
        // (__generic void*)0 in OpenCL 2.0 should not be treated as nullptr
        // since it cannot be assigned to a pointer to constant address space.
        if ((Ctx.getLangOpts().OpenCLVersion >= 200 &&
             Pointee.getAddressSpace() == LangAS::opencl_generic) ||
            (Ctx.getLangOpts().OpenCL &&
             Ctx.getLangOpts().OpenCLVersion < 200 &&
             Pointee.getAddressSpace() == LangAS::opencl_private))
          Qs.removeAddressSpace();

        if (Pointee->isVoidType() && Qs.empty() && // to void*
            CE->getSubExpr()->getType()->isIntegerType()) // from int
          return CE->getSubExpr()->isNullPointerConstant(Ctx, NPC);
      }
    }
  } else if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(this)) {
    // Ignore the ImplicitCastExpr type entirely.
    return ICE->getSubExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const ParenExpr *PE = dyn_cast<ParenExpr>(this)) {
    // Accept ((void*)0) as a null pointer constant, as many other
    // implementations do.
    return PE->getSubExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const GenericSelectionExpr *GE =
               dyn_cast<GenericSelectionExpr>(this)) {
    if (GE->isResultDependent())
      return NPCK_NotNull;
    return GE->getResultExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const ChooseExpr *CE = dyn_cast<ChooseExpr>(this)) {
    if (CE->isConditionDependent())
      return NPCK_NotNull;
    return CE->getChosenSubExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const CXXDefaultArgExpr *DefaultArg
               = dyn_cast<CXXDefaultArgExpr>(this)) {
    // See through default argument expressions.
    return DefaultArg->getExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const CXXDefaultInitExpr *DefaultInit
               = dyn_cast<CXXDefaultInitExpr>(this)) {
    // See through default initializer expressions.
    return DefaultInit->getExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (isa<GNUNullExpr>(this)) {
    // The GNU __null extension is always a null pointer constant.
    return NPCK_GNUNull;
  } else if (const MaterializeTemporaryExpr *M
                                   = dyn_cast<MaterializeTemporaryExpr>(this)) {
    return M->GetTemporaryExpr()->isNullPointerConstant(Ctx, NPC);
  } else if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(this)) {
    if (const Expr *Source = OVE->getSourceExpr())
      return Source->isNullPointerConstant(Ctx, NPC);
  }

  // C++11 nullptr_t is always a null pointer constant.
  if (getType()->isNullPtrType())
    return NPCK_CXX11_nullptr;

  if (const RecordType *UT = getType()->getAsUnionType())
    if (!Ctx.getLangOpts().CPlusPlus11 &&
        UT && UT->getDecl()->hasAttr<TransparentUnionAttr>())
      if (const CompoundLiteralExpr *CLE = dyn_cast<CompoundLiteralExpr>(this)){
        const Expr *InitExpr = CLE->getInitializer();
        if (const InitListExpr *ILE = dyn_cast<InitListExpr>(InitExpr))
          return ILE->getInit(0)->isNullPointerConstant(Ctx, NPC);
      }
  // This expression must be an integer type.
  if (!getType()->isIntegerType() ||
      (Ctx.getLangOpts().CPlusPlus && getType()->isEnumeralType()))
    return NPCK_NotNull;

  if (Ctx.getLangOpts().CPlusPlus11) {
    // C++11 [conv.ptr]p1: A null pointer constant is an integer literal with
    // value zero or a prvalue of type std::nullptr_t.
    // Microsoft mode permits C++98 rules reflecting MSVC behavior.
    const IntegerLiteral *Lit = dyn_cast<IntegerLiteral>(this);
    if (Lit && !Lit->getValue())
      return NPCK_ZeroLiteral;
    else if (!Ctx.getLangOpts().MSVCCompat || !isCXX98IntegralConstantExpr(Ctx))
      return NPCK_NotNull;
  } else {
    // If we have an integer constant expression, we need to *evaluate* it and
    // test for the value 0.
    if (!isIntegerConstantExpr(Ctx))
      return NPCK_NotNull;
  }

  if (EvaluateKnownConstInt(Ctx) != 0)
    return NPCK_NotNull;

  if (isa<IntegerLiteral>(this))
    return NPCK_ZeroLiteral;
  return NPCK_ZeroExpression;
}

/// If this expression is an l-value for an Objective C
/// property, find the underlying property reference expression.
const ObjCPropertyRefExpr *Expr::getObjCProperty() const {
  const Expr *E = this;
  while (true) {
    assert((E->getValueKind() == VK_LValue &&
            E->getObjectKind() == OK_ObjCProperty) &&
           "expression is not a property reference");
    E = E->IgnoreParenCasts();
    if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Comma) {
        E = BO->getRHS();
        continue;
      }
    }

    break;
  }

  return cast<ObjCPropertyRefExpr>(E);
}

bool Expr::isObjCSelfExpr() const {
  const Expr *E = IgnoreParenImpCasts();

  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE)
    return false;

  const ImplicitParamDecl *Param = dyn_cast<ImplicitParamDecl>(DRE->getDecl());
  if (!Param)
    return false;

  const ObjCMethodDecl *M = dyn_cast<ObjCMethodDecl>(Param->getDeclContext());
  if (!M)
    return false;

  return M->getSelfDecl() == Param;
}

FieldDecl *Expr::getSourceBitField() {
  Expr *E = this->IgnoreParens();

  while (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() == CK_LValueToRValue ||
        (ICE->getValueKind() != VK_RValue && ICE->getCastKind() == CK_NoOp))
      E = ICE->getSubExpr()->IgnoreParens();
    else
      break;
  }

  if (MemberExpr *MemRef = dyn_cast<MemberExpr>(E))
    if (FieldDecl *Field = dyn_cast<FieldDecl>(MemRef->getMemberDecl()))
      if (Field->isBitField())
        return Field;

  if (ObjCIvarRefExpr *IvarRef = dyn_cast<ObjCIvarRefExpr>(E)) {
    FieldDecl *Ivar = IvarRef->getDecl();
    if (Ivar->isBitField())
      return Ivar;
  }

  if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(E)) {
    if (FieldDecl *Field = dyn_cast<FieldDecl>(DeclRef->getDecl()))
      if (Field->isBitField())
        return Field;

    if (BindingDecl *BD = dyn_cast<BindingDecl>(DeclRef->getDecl()))
      if (Expr *E = BD->getBinding())
        return E->getSourceBitField();
  }

  if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(E)) {
    if (BinOp->isAssignmentOp() && BinOp->getLHS())
      return BinOp->getLHS()->getSourceBitField();

    if (BinOp->getOpcode() == BO_Comma && BinOp->getRHS())
      return BinOp->getRHS()->getSourceBitField();
  }

  if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(E))
    if (UnOp->isPrefix() && UnOp->isIncrementDecrementOp())
      return UnOp->getSubExpr()->getSourceBitField();

  return nullptr;
}

bool Expr::refersToVectorElement() const {
  // FIXME: Why do we not just look at the ObjectKind here?
  const Expr *E = this->IgnoreParens();

  while (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getValueKind() != VK_RValue &&
        ICE->getCastKind() == CK_NoOp)
      E = ICE->getSubExpr()->IgnoreParens();
    else
      break;
  }

  if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E))
    return ASE->getBase()->getType()->isVectorType();

  if (isa<ExtVectorElementExpr>(E))
    return true;

  if (auto *DRE = dyn_cast<DeclRefExpr>(E))
    if (auto *BD = dyn_cast<BindingDecl>(DRE->getDecl()))
      if (auto *E = BD->getBinding())
        return E->refersToVectorElement();

  return false;
}

bool Expr::refersToGlobalRegisterVar() const {
  const Expr *E = this->IgnoreParenImpCasts();

  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      if (VD->getStorageClass() == SC_Register &&
          VD->hasAttr<AsmLabelAttr>() && !VD->isLocalVarDecl())
        return true;

  return false;
}

/// isArrow - Return true if the base expression is a pointer to vector,
/// return false if the base expression is a vector.
bool ExtVectorElementExpr::isArrow() const {
  return getBase()->getType()->isPointerType();
}

unsigned ExtVectorElementExpr::getNumElements() const {
  if (const VectorType *VT = getType()->getAs<VectorType>())
    return VT->getNumElements();
  return 1;
}

/// containsDuplicateElements - Return true if any element access is repeated.
bool ExtVectorElementExpr::containsDuplicateElements() const {
  // FIXME: Refactor this code to an accessor on the AST node which returns the
  // "type" of component access, and share with code below and in Sema.
  StringRef Comp = Accessor->getName();

  // Halving swizzles do not contain duplicate elements.
  if (Comp == "hi" || Comp == "lo" || Comp == "even" || Comp == "odd")
    return false;

  // Advance past s-char prefix on hex swizzles.
  if (Comp[0] == 's' || Comp[0] == 'S')
    Comp = Comp.substr(1);

  for (unsigned i = 0, e = Comp.size(); i != e; ++i)
    if (Comp.substr(i + 1).find(Comp[i]) != StringRef::npos)
        return true;

  return false;
}

/// getEncodedElementAccess - We encode the fields as a llvm ConstantArray.
void ExtVectorElementExpr::getEncodedElementAccess(
    SmallVectorImpl<uint32_t> &Elts) const {
  StringRef Comp = Accessor->getName();
  bool isNumericAccessor = false;
  if (Comp[0] == 's' || Comp[0] == 'S') {
    Comp = Comp.substr(1);
    isNumericAccessor = true;
  }

  bool isHi =   Comp == "hi";
  bool isLo =   Comp == "lo";
  bool isEven = Comp == "even";
  bool isOdd  = Comp == "odd";

  for (unsigned i = 0, e = getNumElements(); i != e; ++i) {
    uint64_t Index;

    if (isHi)
      Index = e + i;
    else if (isLo)
      Index = i;
    else if (isEven)
      Index = 2 * i;
    else if (isOdd)
      Index = 2 * i + 1;
    else
      Index = ExtVectorType::getAccessorIdx(Comp[i], isNumericAccessor);

    Elts.push_back(Index);
  }
}

ShuffleVectorExpr::ShuffleVectorExpr(const ASTContext &C, ArrayRef<Expr*> args,
                                     QualType Type, SourceLocation BLoc,
                                     SourceLocation RP)
   : Expr(ShuffleVectorExprClass, Type, VK_RValue, OK_Ordinary,
          Type->isDependentType(), Type->isDependentType(),
          Type->isInstantiationDependentType(),
          Type->containsUnexpandedParameterPack()),
     BuiltinLoc(BLoc), RParenLoc(RP), NumExprs(args.size())
{
  SubExprs = new (C) Stmt*[args.size()];
  for (unsigned i = 0; i != args.size(); i++) {
    if (args[i]->isTypeDependent())
      ExprBits.TypeDependent = true;
    if (args[i]->isValueDependent())
      ExprBits.ValueDependent = true;
    if (args[i]->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (args[i]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    SubExprs[i] = args[i];
  }
}

void ShuffleVectorExpr::setExprs(const ASTContext &C, ArrayRef<Expr *> Exprs) {
  if (SubExprs) C.Deallocate(SubExprs);

  this->NumExprs = Exprs.size();
  SubExprs = new (C) Stmt*[NumExprs];
  memcpy(SubExprs, Exprs.data(), sizeof(Expr *) * Exprs.size());
}

GenericSelectionExpr::GenericSelectionExpr(const ASTContext &Context,
                               SourceLocation GenericLoc, Expr *ControllingExpr,
                               ArrayRef<TypeSourceInfo*> AssocTypes,
                               ArrayRef<Expr*> AssocExprs,
                               SourceLocation DefaultLoc,
                               SourceLocation RParenLoc,
                               bool ContainsUnexpandedParameterPack,
                               unsigned ResultIndex)
  : Expr(GenericSelectionExprClass,
         AssocExprs[ResultIndex]->getType(),
         AssocExprs[ResultIndex]->getValueKind(),
         AssocExprs[ResultIndex]->getObjectKind(),
         AssocExprs[ResultIndex]->isTypeDependent(),
         AssocExprs[ResultIndex]->isValueDependent(),
         AssocExprs[ResultIndex]->isInstantiationDependent(),
         ContainsUnexpandedParameterPack),
    AssocTypes(new (Context) TypeSourceInfo*[AssocTypes.size()]),
    SubExprs(new (Context) Stmt*[END_EXPR+AssocExprs.size()]),
    NumAssocs(AssocExprs.size()), ResultIndex(ResultIndex),
    GenericLoc(GenericLoc), DefaultLoc(DefaultLoc), RParenLoc(RParenLoc) {
  SubExprs[CONTROLLING] = ControllingExpr;
  assert(AssocTypes.size() == AssocExprs.size());
  std::copy(AssocTypes.begin(), AssocTypes.end(), this->AssocTypes);
  std::copy(AssocExprs.begin(), AssocExprs.end(), SubExprs+END_EXPR);
}

GenericSelectionExpr::GenericSelectionExpr(const ASTContext &Context,
                               SourceLocation GenericLoc, Expr *ControllingExpr,
                               ArrayRef<TypeSourceInfo*> AssocTypes,
                               ArrayRef<Expr*> AssocExprs,
                               SourceLocation DefaultLoc,
                               SourceLocation RParenLoc,
                               bool ContainsUnexpandedParameterPack)
  : Expr(GenericSelectionExprClass,
         Context.DependentTy,
         VK_RValue,
         OK_Ordinary,
         /*isTypeDependent=*/true,
         /*isValueDependent=*/true,
         /*isInstantiationDependent=*/true,
         ContainsUnexpandedParameterPack),
    AssocTypes(new (Context) TypeSourceInfo*[AssocTypes.size()]),
    SubExprs(new (Context) Stmt*[END_EXPR+AssocExprs.size()]),
    NumAssocs(AssocExprs.size()), ResultIndex(-1U), GenericLoc(GenericLoc),
    DefaultLoc(DefaultLoc), RParenLoc(RParenLoc) {
  SubExprs[CONTROLLING] = ControllingExpr;
  assert(AssocTypes.size() == AssocExprs.size());
  std::copy(AssocTypes.begin(), AssocTypes.end(), this->AssocTypes);
  std::copy(AssocExprs.begin(), AssocExprs.end(), SubExprs+END_EXPR);
}

//===----------------------------------------------------------------------===//
//  DesignatedInitExpr
//===----------------------------------------------------------------------===//

IdentifierInfo *DesignatedInitExpr::Designator::getFieldName() const {
  assert(Kind == FieldDesignator && "Only valid on a field designator");
  if (Field.NameOrField & 0x01)
    return reinterpret_cast<IdentifierInfo *>(Field.NameOrField&~0x01);
  else
    return getField()->getIdentifier();
}

DesignatedInitExpr::DesignatedInitExpr(const ASTContext &C, QualType Ty,
                                       llvm::ArrayRef<Designator> Designators,
                                       SourceLocation EqualOrColonLoc,
                                       bool GNUSyntax,
                                       ArrayRef<Expr*> IndexExprs,
                                       Expr *Init)
  : Expr(DesignatedInitExprClass, Ty,
         Init->getValueKind(), Init->getObjectKind(),
         Init->isTypeDependent(), Init->isValueDependent(),
         Init->isInstantiationDependent(),
         Init->containsUnexpandedParameterPack()),
    EqualOrColonLoc(EqualOrColonLoc), GNUSyntax(GNUSyntax),
    NumDesignators(Designators.size()), NumSubExprs(IndexExprs.size() + 1) {
  this->Designators = new (C) Designator[NumDesignators];

  // Record the initializer itself.
  child_iterator Child = child_begin();
  *Child++ = Init;

  // Copy the designators and their subexpressions, computing
  // value-dependence along the way.
  unsigned IndexIdx = 0;
  for (unsigned I = 0; I != NumDesignators; ++I) {
    this->Designators[I] = Designators[I];

    if (this->Designators[I].isArrayDesignator()) {
      // Compute type- and value-dependence.
      Expr *Index = IndexExprs[IndexIdx];
      if (Index->isTypeDependent() || Index->isValueDependent())
        ExprBits.TypeDependent = ExprBits.ValueDependent = true;
      if (Index->isInstantiationDependent())
        ExprBits.InstantiationDependent = true;
      // Propagate unexpanded parameter packs.
      if (Index->containsUnexpandedParameterPack())
        ExprBits.ContainsUnexpandedParameterPack = true;

      // Copy the index expressions into permanent storage.
      *Child++ = IndexExprs[IndexIdx++];
    } else if (this->Designators[I].isArrayRangeDesignator()) {
      // Compute type- and value-dependence.
      Expr *Start = IndexExprs[IndexIdx];
      Expr *End = IndexExprs[IndexIdx + 1];
      if (Start->isTypeDependent() || Start->isValueDependent() ||
          End->isTypeDependent() || End->isValueDependent()) {
        ExprBits.TypeDependent = ExprBits.ValueDependent = true;
        ExprBits.InstantiationDependent = true;
      } else if (Start->isInstantiationDependent() ||
                 End->isInstantiationDependent()) {
        ExprBits.InstantiationDependent = true;
      }

      // Propagate unexpanded parameter packs.
      if (Start->containsUnexpandedParameterPack() ||
          End->containsUnexpandedParameterPack())
        ExprBits.ContainsUnexpandedParameterPack = true;

      // Copy the start/end expressions into permanent storage.
      *Child++ = IndexExprs[IndexIdx++];
      *Child++ = IndexExprs[IndexIdx++];
    }
  }

  assert(IndexIdx == IndexExprs.size() && "Wrong number of index expressions");
}

DesignatedInitExpr *
DesignatedInitExpr::Create(const ASTContext &C,
                           llvm::ArrayRef<Designator> Designators,
                           ArrayRef<Expr*> IndexExprs,
                           SourceLocation ColonOrEqualLoc,
                           bool UsesColonSyntax, Expr *Init) {
  void *Mem = C.Allocate(totalSizeToAlloc<Stmt *>(IndexExprs.size() + 1),
                         alignof(DesignatedInitExpr));
  return new (Mem) DesignatedInitExpr(C, C.VoidTy, Designators,
                                      ColonOrEqualLoc, UsesColonSyntax,
                                      IndexExprs, Init);
}

DesignatedInitExpr *DesignatedInitExpr::CreateEmpty(const ASTContext &C,
                                                    unsigned NumIndexExprs) {
  void *Mem = C.Allocate(totalSizeToAlloc<Stmt *>(NumIndexExprs + 1),
                         alignof(DesignatedInitExpr));
  return new (Mem) DesignatedInitExpr(NumIndexExprs + 1);
}

void DesignatedInitExpr::setDesignators(const ASTContext &C,
                                        const Designator *Desigs,
                                        unsigned NumDesigs) {
  Designators = new (C) Designator[NumDesigs];
  NumDesignators = NumDesigs;
  for (unsigned I = 0; I != NumDesigs; ++I)
    Designators[I] = Desigs[I];
}

SourceRange DesignatedInitExpr::getDesignatorsSourceRange() const {
  DesignatedInitExpr *DIE = const_cast<DesignatedInitExpr*>(this);
  if (size() == 1)
    return DIE->getDesignator(0)->getSourceRange();
  return SourceRange(DIE->getDesignator(0)->getBeginLoc(),
                     DIE->getDesignator(size() - 1)->getEndLoc());
}

SourceLocation DesignatedInitExpr::getBeginLoc() const {
  SourceLocation StartLoc;
  auto *DIE = const_cast<DesignatedInitExpr *>(this);
  Designator &First = *DIE->getDesignator(0);
  if (First.isFieldDesignator()) {
    if (GNUSyntax)
      StartLoc = SourceLocation::getFromRawEncoding(First.Field.FieldLoc);
    else
      StartLoc = SourceLocation::getFromRawEncoding(First.Field.DotLoc);
  } else
    StartLoc =
      SourceLocation::getFromRawEncoding(First.ArrayOrRange.LBracketLoc);
  return StartLoc;
}

SourceLocation DesignatedInitExpr::getEndLoc() const {
  return getInit()->getEndLoc();
}

Expr *DesignatedInitExpr::getArrayIndex(const Designator& D) const {
  assert(D.Kind == Designator::ArrayDesignator && "Requires array designator");
  return getSubExpr(D.ArrayOrRange.Index + 1);
}

Expr *DesignatedInitExpr::getArrayRangeStart(const Designator &D) const {
  assert(D.Kind == Designator::ArrayRangeDesignator &&
         "Requires array range designator");
  return getSubExpr(D.ArrayOrRange.Index + 1);
}

Expr *DesignatedInitExpr::getArrayRangeEnd(const Designator &D) const {
  assert(D.Kind == Designator::ArrayRangeDesignator &&
         "Requires array range designator");
  return getSubExpr(D.ArrayOrRange.Index + 2);
}

/// Replaces the designator at index @p Idx with the series
/// of designators in [First, Last).
void DesignatedInitExpr::ExpandDesignator(const ASTContext &C, unsigned Idx,
                                          const Designator *First,
                                          const Designator *Last) {
  unsigned NumNewDesignators = Last - First;
  if (NumNewDesignators == 0) {
    std::copy_backward(Designators + Idx + 1,
                       Designators + NumDesignators,
                       Designators + Idx);
    --NumNewDesignators;
    return;
  } else if (NumNewDesignators == 1) {
    Designators[Idx] = *First;
    return;
  }

  Designator *NewDesignators
    = new (C) Designator[NumDesignators - 1 + NumNewDesignators];
  std::copy(Designators, Designators + Idx, NewDesignators);
  std::copy(First, Last, NewDesignators + Idx);
  std::copy(Designators + Idx + 1, Designators + NumDesignators,
            NewDesignators + Idx + NumNewDesignators);
  Designators = NewDesignators;
  NumDesignators = NumDesignators - 1 + NumNewDesignators;
}

DesignatedInitUpdateExpr::DesignatedInitUpdateExpr(const ASTContext &C,
    SourceLocation lBraceLoc, Expr *baseExpr, SourceLocation rBraceLoc)
  : Expr(DesignatedInitUpdateExprClass, baseExpr->getType(), VK_RValue,
         OK_Ordinary, false, false, false, false) {
  BaseAndUpdaterExprs[0] = baseExpr;

  InitListExpr *ILE = new (C) InitListExpr(C, lBraceLoc, None, rBraceLoc);
  ILE->setType(baseExpr->getType());
  BaseAndUpdaterExprs[1] = ILE;
}

SourceLocation DesignatedInitUpdateExpr::getBeginLoc() const {
  return getBase()->getBeginLoc();
}

SourceLocation DesignatedInitUpdateExpr::getEndLoc() const {
  return getBase()->getEndLoc();
}

ParenListExpr::ParenListExpr(SourceLocation LParenLoc, ArrayRef<Expr *> Exprs,
                             SourceLocation RParenLoc)
    : Expr(ParenListExprClass, QualType(), VK_RValue, OK_Ordinary, false, false,
           false, false),
      LParenLoc(LParenLoc), RParenLoc(RParenLoc) {
  ParenListExprBits.NumExprs = Exprs.size();

  for (unsigned I = 0, N = Exprs.size(); I != N; ++I) {
    if (Exprs[I]->isTypeDependent())
      ExprBits.TypeDependent = true;
    if (Exprs[I]->isValueDependent())
      ExprBits.ValueDependent = true;
    if (Exprs[I]->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (Exprs[I]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    getTrailingObjects<Stmt *>()[I] = Exprs[I];
  }
}

ParenListExpr::ParenListExpr(EmptyShell Empty, unsigned NumExprs)
    : Expr(ParenListExprClass, Empty) {
  ParenListExprBits.NumExprs = NumExprs;
}

ParenListExpr *ParenListExpr::Create(const ASTContext &Ctx,
                                     SourceLocation LParenLoc,
                                     ArrayRef<Expr *> Exprs,
                                     SourceLocation RParenLoc) {
  void *Mem = Ctx.Allocate(totalSizeToAlloc<Stmt *>(Exprs.size()),
                           alignof(ParenListExpr));
  return new (Mem) ParenListExpr(LParenLoc, Exprs, RParenLoc);
}

ParenListExpr *ParenListExpr::CreateEmpty(const ASTContext &Ctx,
                                          unsigned NumExprs) {
  void *Mem =
      Ctx.Allocate(totalSizeToAlloc<Stmt *>(NumExprs), alignof(ParenListExpr));
  return new (Mem) ParenListExpr(EmptyShell(), NumExprs);
}

const OpaqueValueExpr *OpaqueValueExpr::findInCopyConstruct(const Expr *e) {
  if (const ExprWithCleanups *ewc = dyn_cast<ExprWithCleanups>(e))
    e = ewc->getSubExpr();
  if (const MaterializeTemporaryExpr *m = dyn_cast<MaterializeTemporaryExpr>(e))
    e = m->GetTemporaryExpr();
  e = cast<CXXConstructExpr>(e)->getArg(0);
  while (const ImplicitCastExpr *ice = dyn_cast<ImplicitCastExpr>(e))
    e = ice->getSubExpr();
  return cast<OpaqueValueExpr>(e);
}

PseudoObjectExpr *PseudoObjectExpr::Create(const ASTContext &Context,
                                           EmptyShell sh,
                                           unsigned numSemanticExprs) {
  void *buffer =
      Context.Allocate(totalSizeToAlloc<Expr *>(1 + numSemanticExprs),
                       alignof(PseudoObjectExpr));
  return new(buffer) PseudoObjectExpr(sh, numSemanticExprs);
}

PseudoObjectExpr::PseudoObjectExpr(EmptyShell shell, unsigned numSemanticExprs)
  : Expr(PseudoObjectExprClass, shell) {
  PseudoObjectExprBits.NumSubExprs = numSemanticExprs + 1;
}

PseudoObjectExpr *PseudoObjectExpr::Create(const ASTContext &C, Expr *syntax,
                                           ArrayRef<Expr*> semantics,
                                           unsigned resultIndex) {
  assert(syntax && "no syntactic expression!");
  assert(semantics.size() && "no semantic expressions!");

  QualType type;
  ExprValueKind VK;
  if (resultIndex == NoResult) {
    type = C.VoidTy;
    VK = VK_RValue;
  } else {
    assert(resultIndex < semantics.size());
    type = semantics[resultIndex]->getType();
    VK = semantics[resultIndex]->getValueKind();
    assert(semantics[resultIndex]->getObjectKind() == OK_Ordinary);
  }

  void *buffer = C.Allocate(totalSizeToAlloc<Expr *>(semantics.size() + 1),
                            alignof(PseudoObjectExpr));
  return new(buffer) PseudoObjectExpr(type, VK, syntax, semantics,
                                      resultIndex);
}

PseudoObjectExpr::PseudoObjectExpr(QualType type, ExprValueKind VK,
                                   Expr *syntax, ArrayRef<Expr*> semantics,
                                   unsigned resultIndex)
  : Expr(PseudoObjectExprClass, type, VK, OK_Ordinary,
         /*filled in at end of ctor*/ false, false, false, false) {
  PseudoObjectExprBits.NumSubExprs = semantics.size() + 1;
  PseudoObjectExprBits.ResultIndex = resultIndex + 1;

  for (unsigned i = 0, e = semantics.size() + 1; i != e; ++i) {
    Expr *E = (i == 0 ? syntax : semantics[i-1]);
    getSubExprsBuffer()[i] = E;

    if (E->isTypeDependent())
      ExprBits.TypeDependent = true;
    if (E->isValueDependent())
      ExprBits.ValueDependent = true;
    if (E->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (E->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    if (isa<OpaqueValueExpr>(E))
      assert(cast<OpaqueValueExpr>(E)->getSourceExpr() != nullptr &&
             "opaque-value semantic expressions for pseudo-object "
             "operations must have sources");
  }
}

//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

// UnaryExprOrTypeTraitExpr
Stmt::child_range UnaryExprOrTypeTraitExpr::children() {
  const_child_range CCR =
      const_cast<const UnaryExprOrTypeTraitExpr *>(this)->children();
  return child_range(cast_away_const(CCR.begin()), cast_away_const(CCR.end()));
}

Stmt::const_child_range UnaryExprOrTypeTraitExpr::children() const {
  // If this is of a type and the type is a VLA type (and not a typedef), the
  // size expression of the VLA needs to be treated as an executable expression.
  // Why isn't this weirdness documented better in StmtIterator?
  if (isArgumentType()) {
    if (const VariableArrayType *T =
            dyn_cast<VariableArrayType>(getArgumentType().getTypePtr()))
      return const_child_range(const_child_iterator(T), const_child_iterator());
    return const_child_range(const_child_iterator(), const_child_iterator());
  }
  return const_child_range(&Argument.Ex, &Argument.Ex + 1);
}

AtomicExpr::AtomicExpr(SourceLocation BLoc, ArrayRef<Expr*> args,
                       QualType t, AtomicOp op, SourceLocation RP)
  : Expr(AtomicExprClass, t, VK_RValue, OK_Ordinary,
         false, false, false, false),
    NumSubExprs(args.size()), BuiltinLoc(BLoc), RParenLoc(RP), Op(op)
{
  assert(args.size() == getNumSubExprs(op) && "wrong number of subexpressions");
  for (unsigned i = 0; i != args.size(); i++) {
    if (args[i]->isTypeDependent())
      ExprBits.TypeDependent = true;
    if (args[i]->isValueDependent())
      ExprBits.ValueDependent = true;
    if (args[i]->isInstantiationDependent())
      ExprBits.InstantiationDependent = true;
    if (args[i]->containsUnexpandedParameterPack())
      ExprBits.ContainsUnexpandedParameterPack = true;

    SubExprs[i] = args[i];
  }
}

unsigned AtomicExpr::getNumSubExprs(AtomicOp Op) {
  switch (Op) {
  case AO__c11_atomic_init:
  case AO__opencl_atomic_init:
  case AO__c11_atomic_load:
  case AO__atomic_load_n:
    return 2;

  case AO__opencl_atomic_load:
  case AO__c11_atomic_store:
  case AO__c11_atomic_exchange:
  case AO__atomic_load:
  case AO__atomic_store:
  case AO__atomic_store_n:
  case AO__atomic_exchange_n:
  case AO__c11_atomic_fetch_add:
  case AO__c11_atomic_fetch_sub:
  case AO__c11_atomic_fetch_and:
  case AO__c11_atomic_fetch_or:
  case AO__c11_atomic_fetch_xor:
  case AO__atomic_fetch_add:
  case AO__atomic_fetch_sub:
  case AO__atomic_fetch_and:
  case AO__atomic_fetch_or:
  case AO__atomic_fetch_xor:
  case AO__atomic_fetch_nand:
  case AO__atomic_add_fetch:
  case AO__atomic_sub_fetch:
  case AO__atomic_and_fetch:
  case AO__atomic_or_fetch:
  case AO__atomic_xor_fetch:
  case AO__atomic_nand_fetch:
  case AO__atomic_fetch_min:
  case AO__atomic_fetch_max:
    return 3;

  case AO__opencl_atomic_store:
  case AO__opencl_atomic_exchange:
  case AO__opencl_atomic_fetch_add:
  case AO__opencl_atomic_fetch_sub:
  case AO__opencl_atomic_fetch_and:
  case AO__opencl_atomic_fetch_or:
  case AO__opencl_atomic_fetch_xor:
  case AO__opencl_atomic_fetch_min:
  case AO__opencl_atomic_fetch_max:
  case AO__atomic_exchange:
    return 4;

  case AO__c11_atomic_compare_exchange_strong:
  case AO__c11_atomic_compare_exchange_weak:
    return 5;

  case AO__opencl_atomic_compare_exchange_strong:
  case AO__opencl_atomic_compare_exchange_weak:
  case AO__atomic_compare_exchange:
  case AO__atomic_compare_exchange_n:
    return 6;
  }
  llvm_unreachable("unknown atomic op");
}

QualType AtomicExpr::getValueType() const {
  auto T = getPtr()->getType()->castAs<PointerType>()->getPointeeType();
  if (auto AT = T->getAs<AtomicType>())
    return AT->getValueType();
  return T;
}

QualType OMPArraySectionExpr::getBaseOriginalType(const Expr *Base) {
  unsigned ArraySectionCount = 0;
  while (auto *OASE = dyn_cast<OMPArraySectionExpr>(Base->IgnoreParens())) {
    Base = OASE->getBase();
    ++ArraySectionCount;
  }
  while (auto *ASE =
             dyn_cast<ArraySubscriptExpr>(Base->IgnoreParenImpCasts())) {
    Base = ASE->getBase();
    ++ArraySectionCount;
  }
  Base = Base->IgnoreParenImpCasts();
  auto OriginalTy = Base->getType();
  if (auto *DRE = dyn_cast<DeclRefExpr>(Base))
    if (auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl()))
      OriginalTy = PVD->getOriginalType().getNonReferenceType();

  for (unsigned Cnt = 0; Cnt < ArraySectionCount; ++Cnt) {
    if (OriginalTy->isAnyPointerType())
      OriginalTy = OriginalTy->getPointeeType();
    else {
      assert (OriginalTy->isArrayType());
      OriginalTy = OriginalTy->castAsArrayTypeUnsafe()->getElementType();
    }
  }
  return OriginalTy;
}
