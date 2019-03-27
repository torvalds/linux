//===--- DeclSpec.cpp - Declaration Specifier Semantic Analysis -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for declaration specifiers.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/DeclSpec.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/LocInfoType.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include <cstring>
using namespace clang;


void UnqualifiedId::setTemplateId(TemplateIdAnnotation *TemplateId) {
  assert(TemplateId && "NULL template-id annotation?");
  Kind = UnqualifiedIdKind::IK_TemplateId;
  this->TemplateId = TemplateId;
  StartLocation = TemplateId->TemplateNameLoc;
  EndLocation = TemplateId->RAngleLoc;
}

void UnqualifiedId::setConstructorTemplateId(TemplateIdAnnotation *TemplateId) {
  assert(TemplateId && "NULL template-id annotation?");
  Kind = UnqualifiedIdKind::IK_ConstructorTemplateId;
  this->TemplateId = TemplateId;
  StartLocation = TemplateId->TemplateNameLoc;
  EndLocation = TemplateId->RAngleLoc;
}

void CXXScopeSpec::Extend(ASTContext &Context, SourceLocation TemplateKWLoc,
                          TypeLoc TL, SourceLocation ColonColonLoc) {
  Builder.Extend(Context, TemplateKWLoc, TL, ColonColonLoc);
  if (Range.getBegin().isInvalid())
    Range.setBegin(TL.getBeginLoc());
  Range.setEnd(ColonColonLoc);

  assert(Range == Builder.getSourceRange() &&
         "NestedNameSpecifierLoc range computation incorrect");
}

void CXXScopeSpec::Extend(ASTContext &Context, IdentifierInfo *Identifier,
                          SourceLocation IdentifierLoc,
                          SourceLocation ColonColonLoc) {
  Builder.Extend(Context, Identifier, IdentifierLoc, ColonColonLoc);

  if (Range.getBegin().isInvalid())
    Range.setBegin(IdentifierLoc);
  Range.setEnd(ColonColonLoc);

  assert(Range == Builder.getSourceRange() &&
         "NestedNameSpecifierLoc range computation incorrect");
}

void CXXScopeSpec::Extend(ASTContext &Context, NamespaceDecl *Namespace,
                          SourceLocation NamespaceLoc,
                          SourceLocation ColonColonLoc) {
  Builder.Extend(Context, Namespace, NamespaceLoc, ColonColonLoc);

  if (Range.getBegin().isInvalid())
    Range.setBegin(NamespaceLoc);
  Range.setEnd(ColonColonLoc);

  assert(Range == Builder.getSourceRange() &&
         "NestedNameSpecifierLoc range computation incorrect");
}

void CXXScopeSpec::Extend(ASTContext &Context, NamespaceAliasDecl *Alias,
                          SourceLocation AliasLoc,
                          SourceLocation ColonColonLoc) {
  Builder.Extend(Context, Alias, AliasLoc, ColonColonLoc);

  if (Range.getBegin().isInvalid())
    Range.setBegin(AliasLoc);
  Range.setEnd(ColonColonLoc);

  assert(Range == Builder.getSourceRange() &&
         "NestedNameSpecifierLoc range computation incorrect");
}

void CXXScopeSpec::MakeGlobal(ASTContext &Context,
                              SourceLocation ColonColonLoc) {
  Builder.MakeGlobal(Context, ColonColonLoc);

  Range = SourceRange(ColonColonLoc);

  assert(Range == Builder.getSourceRange() &&
         "NestedNameSpecifierLoc range computation incorrect");
}

void CXXScopeSpec::MakeSuper(ASTContext &Context, CXXRecordDecl *RD,
                             SourceLocation SuperLoc,
                             SourceLocation ColonColonLoc) {
  Builder.MakeSuper(Context, RD, SuperLoc, ColonColonLoc);

  Range.setBegin(SuperLoc);
  Range.setEnd(ColonColonLoc);

  assert(Range == Builder.getSourceRange() &&
  "NestedNameSpecifierLoc range computation incorrect");
}

void CXXScopeSpec::MakeTrivial(ASTContext &Context,
                               NestedNameSpecifier *Qualifier, SourceRange R) {
  Builder.MakeTrivial(Context, Qualifier, R);
  Range = R;
}

void CXXScopeSpec::Adopt(NestedNameSpecifierLoc Other) {
  if (!Other) {
    Range = SourceRange();
    Builder.Clear();
    return;
  }

  Range = Other.getSourceRange();
  Builder.Adopt(Other);
}

SourceLocation CXXScopeSpec::getLastQualifierNameLoc() const {
  if (!Builder.getRepresentation())
    return SourceLocation();
  return Builder.getTemporary().getLocalBeginLoc();
}

NestedNameSpecifierLoc
CXXScopeSpec::getWithLocInContext(ASTContext &Context) const {
  if (!Builder.getRepresentation())
    return NestedNameSpecifierLoc();

  return Builder.getWithLocInContext(Context);
}

/// DeclaratorChunk::getFunction - Return a DeclaratorChunk for a function.
/// "TheDeclarator" is the declarator that this will be added to.
DeclaratorChunk DeclaratorChunk::getFunction(bool hasProto,
                                             bool isAmbiguous,
                                             SourceLocation LParenLoc,
                                             ParamInfo *Params,
                                             unsigned NumParams,
                                             SourceLocation EllipsisLoc,
                                             SourceLocation RParenLoc,
                                             bool RefQualifierIsLvalueRef,
                                             SourceLocation RefQualifierLoc,
                                             SourceLocation MutableLoc,
                                             ExceptionSpecificationType
                                                 ESpecType,
                                             SourceRange ESpecRange,
                                             ParsedType *Exceptions,
                                             SourceRange *ExceptionRanges,
                                             unsigned NumExceptions,
                                             Expr *NoexceptExpr,
                                             CachedTokens *ExceptionSpecTokens,
                                             ArrayRef<NamedDecl*>
                                                 DeclsInPrototype,
                                             SourceLocation LocalRangeBegin,
                                             SourceLocation LocalRangeEnd,
                                             Declarator &TheDeclarator,
                                             TypeResult TrailingReturnType,
                                             DeclSpec *MethodQualifiers) {
  assert(!(MethodQualifiers && MethodQualifiers->getTypeQualifiers() & DeclSpec::TQ_atomic) &&
         "function cannot have _Atomic qualifier");

  DeclaratorChunk I;
  I.Kind                        = Function;
  I.Loc                         = LocalRangeBegin;
  I.EndLoc                      = LocalRangeEnd;
  I.Fun.hasPrototype            = hasProto;
  I.Fun.isVariadic              = EllipsisLoc.isValid();
  I.Fun.isAmbiguous             = isAmbiguous;
  I.Fun.LParenLoc               = LParenLoc.getRawEncoding();
  I.Fun.EllipsisLoc             = EllipsisLoc.getRawEncoding();
  I.Fun.RParenLoc               = RParenLoc.getRawEncoding();
  I.Fun.DeleteParams            = false;
  I.Fun.NumParams               = NumParams;
  I.Fun.Params                  = nullptr;
  I.Fun.RefQualifierIsLValueRef = RefQualifierIsLvalueRef;
  I.Fun.RefQualifierLoc         = RefQualifierLoc.getRawEncoding();
  I.Fun.MutableLoc              = MutableLoc.getRawEncoding();
  I.Fun.ExceptionSpecType       = ESpecType;
  I.Fun.ExceptionSpecLocBeg     = ESpecRange.getBegin().getRawEncoding();
  I.Fun.ExceptionSpecLocEnd     = ESpecRange.getEnd().getRawEncoding();
  I.Fun.NumExceptionsOrDecls    = 0;
  I.Fun.Exceptions              = nullptr;
  I.Fun.NoexceptExpr            = nullptr;
  I.Fun.HasTrailingReturnType   = TrailingReturnType.isUsable() ||
                                  TrailingReturnType.isInvalid();
  I.Fun.TrailingReturnType      = TrailingReturnType.get();
  I.Fun.MethodQualifiers        = nullptr;
  I.Fun.QualAttrFactory         = nullptr;

  if (MethodQualifiers && (MethodQualifiers->getTypeQualifiers() ||
                           MethodQualifiers->getAttributes().size())) {
    auto &attrs = MethodQualifiers->getAttributes();
    I.Fun.MethodQualifiers = new DeclSpec(attrs.getPool().getFactory());
    MethodQualifiers->forEachCVRUQualifier(
        [&](DeclSpec::TQ TypeQual, StringRef PrintName, SourceLocation SL) {
          I.Fun.MethodQualifiers->SetTypeQual(TypeQual, SL);
        });
    I.Fun.MethodQualifiers->getAttributes().takeAllFrom(attrs);
    I.Fun.MethodQualifiers->getAttributePool().takeAllFrom(attrs.getPool());
  }

  assert(I.Fun.ExceptionSpecType == ESpecType && "bitfield overflow");

  // new[] a parameter array if needed.
  if (NumParams) {
    // If the 'InlineParams' in Declarator is unused and big enough, put our
    // parameter list there (in an effort to avoid new/delete traffic).  If it
    // is already used (consider a function returning a function pointer) or too
    // small (function with too many parameters), go to the heap.
    if (!TheDeclarator.InlineStorageUsed &&
        NumParams <= llvm::array_lengthof(TheDeclarator.InlineParams)) {
      I.Fun.Params = TheDeclarator.InlineParams;
      new (I.Fun.Params) ParamInfo[NumParams];
      I.Fun.DeleteParams = false;
      TheDeclarator.InlineStorageUsed = true;
    } else {
      I.Fun.Params = new DeclaratorChunk::ParamInfo[NumParams];
      I.Fun.DeleteParams = true;
    }
    for (unsigned i = 0; i < NumParams; i++)
      I.Fun.Params[i] = std::move(Params[i]);
  }

  // Check what exception specification information we should actually store.
  switch (ESpecType) {
  default: break; // By default, save nothing.
  case EST_Dynamic:
    // new[] an exception array if needed
    if (NumExceptions) {
      I.Fun.NumExceptionsOrDecls = NumExceptions;
      I.Fun.Exceptions = new DeclaratorChunk::TypeAndRange[NumExceptions];
      for (unsigned i = 0; i != NumExceptions; ++i) {
        I.Fun.Exceptions[i].Ty = Exceptions[i];
        I.Fun.Exceptions[i].Range = ExceptionRanges[i];
      }
    }
    break;

  case EST_DependentNoexcept:
  case EST_NoexceptFalse:
  case EST_NoexceptTrue:
    I.Fun.NoexceptExpr = NoexceptExpr;
    break;

  case EST_Unparsed:
    I.Fun.ExceptionSpecTokens = ExceptionSpecTokens;
    break;
  }

  if (!DeclsInPrototype.empty()) {
    assert(ESpecType == EST_None && NumExceptions == 0 &&
           "cannot have exception specifiers and decls in prototype");
    I.Fun.NumExceptionsOrDecls = DeclsInPrototype.size();
    // Copy the array of decls into stable heap storage.
    I.Fun.DeclsInPrototype = new NamedDecl *[DeclsInPrototype.size()];
    for (size_t J = 0; J < DeclsInPrototype.size(); ++J)
      I.Fun.DeclsInPrototype[J] = DeclsInPrototype[J];
  }

  return I;
}

void Declarator::setDecompositionBindings(
    SourceLocation LSquareLoc,
    ArrayRef<DecompositionDeclarator::Binding> Bindings,
    SourceLocation RSquareLoc) {
  assert(!hasName() && "declarator given multiple names!");

  BindingGroup.LSquareLoc = LSquareLoc;
  BindingGroup.RSquareLoc = RSquareLoc;
  BindingGroup.NumBindings = Bindings.size();
  Range.setEnd(RSquareLoc);

  // We're now past the identifier.
  SetIdentifier(nullptr, LSquareLoc);
  Name.EndLocation = RSquareLoc;

  // Allocate storage for bindings and stash them away.
  if (Bindings.size()) {
    if (!InlineStorageUsed &&
        Bindings.size() <= llvm::array_lengthof(InlineBindings)) {
      BindingGroup.Bindings = InlineBindings;
      BindingGroup.DeleteBindings = false;
      InlineStorageUsed = true;
    } else {
      BindingGroup.Bindings =
          new DecompositionDeclarator::Binding[Bindings.size()];
      BindingGroup.DeleteBindings = true;
    }
    std::uninitialized_copy(Bindings.begin(), Bindings.end(),
                            BindingGroup.Bindings);
  }
}

bool Declarator::isDeclarationOfFunction() const {
  for (unsigned i = 0, i_end = DeclTypeInfo.size(); i < i_end; ++i) {
    switch (DeclTypeInfo[i].Kind) {
    case DeclaratorChunk::Function:
      return true;
    case DeclaratorChunk::Paren:
      continue;
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Array:
    case DeclaratorChunk::BlockPointer:
    case DeclaratorChunk::MemberPointer:
    case DeclaratorChunk::Pipe:
      return false;
    }
    llvm_unreachable("Invalid type chunk");
  }

  switch (DS.getTypeSpecType()) {
    case TST_atomic:
    case TST_auto:
    case TST_auto_type:
    case TST_bool:
    case TST_char:
    case TST_char8:
    case TST_char16:
    case TST_char32:
    case TST_class:
    case TST_decimal128:
    case TST_decimal32:
    case TST_decimal64:
    case TST_double:
    case TST_Accum:
    case TST_Fract:
    case TST_Float16:
    case TST_float128:
    case TST_enum:
    case TST_error:
    case TST_float:
    case TST_half:
    case TST_int:
    case TST_int128:
    case TST_struct:
    case TST_interface:
    case TST_union:
    case TST_unknown_anytype:
    case TST_unspecified:
    case TST_void:
    case TST_wchar:
#define GENERIC_IMAGE_TYPE(ImgType, Id) case TST_##ImgType##_t:
#include "clang/Basic/OpenCLImageTypes.def"
      return false;

    case TST_decltype_auto:
      // This must have an initializer, so can't be a function declaration,
      // even if the initializer has function type.
      return false;

    case TST_decltype:
    case TST_typeofExpr:
      if (Expr *E = DS.getRepAsExpr())
        return E->getType()->isFunctionType();
      return false;

    case TST_underlyingType:
    case TST_typename:
    case TST_typeofType: {
      QualType QT = DS.getRepAsType().get();
      if (QT.isNull())
        return false;

      if (const LocInfoType *LIT = dyn_cast<LocInfoType>(QT))
        QT = LIT->getType();

      if (QT.isNull())
        return false;

      return QT->isFunctionType();
    }
  }

  llvm_unreachable("Invalid TypeSpecType!");
}

bool Declarator::isStaticMember() {
  assert(getContext() == DeclaratorContext::MemberContext);
  return getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_static ||
         (getName().Kind == UnqualifiedIdKind::IK_OperatorFunctionId &&
          CXXMethodDecl::isStaticOverloadedOperator(
              getName().OperatorFunctionId.Operator));
}

bool Declarator::isCtorOrDtor() {
  return (getName().getKind() == UnqualifiedIdKind::IK_ConstructorName) ||
         (getName().getKind() == UnqualifiedIdKind::IK_DestructorName);
}

void DeclSpec::forEachCVRUQualifier(
    llvm::function_ref<void(TQ, StringRef, SourceLocation)> Handle) {
  if (TypeQualifiers & TQ_const)
    Handle(TQ_const, "const", TQ_constLoc);
  if (TypeQualifiers & TQ_volatile)
    Handle(TQ_volatile, "volatile", TQ_volatileLoc);
  if (TypeQualifiers & TQ_restrict)
    Handle(TQ_restrict, "restrict", TQ_restrictLoc);
  if (TypeQualifiers & TQ_unaligned)
    Handle(TQ_unaligned, "unaligned", TQ_unalignedLoc);
}

void DeclSpec::forEachQualifier(
    llvm::function_ref<void(TQ, StringRef, SourceLocation)> Handle) {
  forEachCVRUQualifier(Handle);
  // FIXME: Add code below to iterate through the attributes and call Handle.
}

bool DeclSpec::hasTagDefinition() const {
  if (!TypeSpecOwned)
    return false;
  return cast<TagDecl>(getRepAsDecl())->isCompleteDefinition();
}

/// getParsedSpecifiers - Return a bitmask of which flavors of specifiers this
/// declaration specifier includes.
///
unsigned DeclSpec::getParsedSpecifiers() const {
  unsigned Res = 0;
  if (StorageClassSpec != SCS_unspecified ||
      ThreadStorageClassSpec != TSCS_unspecified)
    Res |= PQ_StorageClassSpecifier;

  if (TypeQualifiers != TQ_unspecified)
    Res |= PQ_TypeQualifier;

  if (hasTypeSpecifier())
    Res |= PQ_TypeSpecifier;

  if (FS_inline_specified || FS_virtual_specified || FS_explicit_specified ||
      FS_noreturn_specified || FS_forceinline_specified)
    Res |= PQ_FunctionSpecifier;
  return Res;
}

template <class T> static bool BadSpecifier(T TNew, T TPrev,
                                            const char *&PrevSpec,
                                            unsigned &DiagID,
                                            bool IsExtension = true) {
  PrevSpec = DeclSpec::getSpecifierName(TPrev);
  if (TNew != TPrev)
    DiagID = diag::err_invalid_decl_spec_combination;
  else
    DiagID = IsExtension ? diag::ext_warn_duplicate_declspec :
                           diag::warn_duplicate_declspec;
  return true;
}

const char *DeclSpec::getSpecifierName(DeclSpec::SCS S) {
  switch (S) {
  case DeclSpec::SCS_unspecified: return "unspecified";
  case DeclSpec::SCS_typedef:     return "typedef";
  case DeclSpec::SCS_extern:      return "extern";
  case DeclSpec::SCS_static:      return "static";
  case DeclSpec::SCS_auto:        return "auto";
  case DeclSpec::SCS_register:    return "register";
  case DeclSpec::SCS_private_extern: return "__private_extern__";
  case DeclSpec::SCS_mutable:     return "mutable";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(DeclSpec::TSCS S) {
  switch (S) {
  case DeclSpec::TSCS_unspecified:   return "unspecified";
  case DeclSpec::TSCS___thread:      return "__thread";
  case DeclSpec::TSCS_thread_local:  return "thread_local";
  case DeclSpec::TSCS__Thread_local: return "_Thread_local";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(TSW W) {
  switch (W) {
  case TSW_unspecified: return "unspecified";
  case TSW_short:       return "short";
  case TSW_long:        return "long";
  case TSW_longlong:    return "long long";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(TSC C) {
  switch (C) {
  case TSC_unspecified: return "unspecified";
  case TSC_imaginary:   return "imaginary";
  case TSC_complex:     return "complex";
  }
  llvm_unreachable("Unknown typespec!");
}


const char *DeclSpec::getSpecifierName(TSS S) {
  switch (S) {
  case TSS_unspecified: return "unspecified";
  case TSS_signed:      return "signed";
  case TSS_unsigned:    return "unsigned";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(DeclSpec::TST T,
                                       const PrintingPolicy &Policy) {
  switch (T) {
  case DeclSpec::TST_unspecified: return "unspecified";
  case DeclSpec::TST_void:        return "void";
  case DeclSpec::TST_char:        return "char";
  case DeclSpec::TST_wchar:       return Policy.MSWChar ? "__wchar_t" : "wchar_t";
  case DeclSpec::TST_char8:       return "char8_t";
  case DeclSpec::TST_char16:      return "char16_t";
  case DeclSpec::TST_char32:      return "char32_t";
  case DeclSpec::TST_int:         return "int";
  case DeclSpec::TST_int128:      return "__int128";
  case DeclSpec::TST_half:        return "half";
  case DeclSpec::TST_float:       return "float";
  case DeclSpec::TST_double:      return "double";
  case DeclSpec::TST_accum:       return "_Accum";
  case DeclSpec::TST_fract:       return "_Fract";
  case DeclSpec::TST_float16:     return "_Float16";
  case DeclSpec::TST_float128:    return "__float128";
  case DeclSpec::TST_bool:        return Policy.Bool ? "bool" : "_Bool";
  case DeclSpec::TST_decimal32:   return "_Decimal32";
  case DeclSpec::TST_decimal64:   return "_Decimal64";
  case DeclSpec::TST_decimal128:  return "_Decimal128";
  case DeclSpec::TST_enum:        return "enum";
  case DeclSpec::TST_class:       return "class";
  case DeclSpec::TST_union:       return "union";
  case DeclSpec::TST_struct:      return "struct";
  case DeclSpec::TST_interface:   return "__interface";
  case DeclSpec::TST_typename:    return "type-name";
  case DeclSpec::TST_typeofType:
  case DeclSpec::TST_typeofExpr:  return "typeof";
  case DeclSpec::TST_auto:        return "auto";
  case DeclSpec::TST_auto_type:   return "__auto_type";
  case DeclSpec::TST_decltype:    return "(decltype)";
  case DeclSpec::TST_decltype_auto: return "decltype(auto)";
  case DeclSpec::TST_underlyingType: return "__underlying_type";
  case DeclSpec::TST_unknown_anytype: return "__unknown_anytype";
  case DeclSpec::TST_atomic: return "_Atomic";
#define GENERIC_IMAGE_TYPE(ImgType, Id) \
  case DeclSpec::TST_##ImgType##_t: \
    return #ImgType "_t";
#include "clang/Basic/OpenCLImageTypes.def"
  case DeclSpec::TST_error:       return "(error)";
  }
  llvm_unreachable("Unknown typespec!");
}

const char *DeclSpec::getSpecifierName(TQ T) {
  switch (T) {
  case DeclSpec::TQ_unspecified: return "unspecified";
  case DeclSpec::TQ_const:       return "const";
  case DeclSpec::TQ_restrict:    return "restrict";
  case DeclSpec::TQ_volatile:    return "volatile";
  case DeclSpec::TQ_atomic:      return "_Atomic";
  case DeclSpec::TQ_unaligned:   return "__unaligned";
  }
  llvm_unreachable("Unknown typespec!");
}

bool DeclSpec::SetStorageClassSpec(Sema &S, SCS SC, SourceLocation Loc,
                                   const char *&PrevSpec,
                                   unsigned &DiagID,
                                   const PrintingPolicy &Policy) {
  // OpenCL v1.1 s6.8g: "The extern, static, auto and register storage-class
  // specifiers are not supported.
  // It seems sensible to prohibit private_extern too
  // The cl_clang_storage_class_specifiers extension enables support for
  // these storage-class specifiers.
  // OpenCL v1.2 s6.8 changes this to "The auto and register storage-class
  // specifiers are not supported."
  // OpenCL C++ v1.0 s2.9 restricts register.
  if (S.getLangOpts().OpenCL &&
      !S.getOpenCLOptions().isEnabled("cl_clang_storage_class_specifiers")) {
    switch (SC) {
    case SCS_extern:
    case SCS_private_extern:
    case SCS_static:
      if (S.getLangOpts().OpenCLVersion < 120 &&
          !S.getLangOpts().OpenCLCPlusPlus) {
        DiagID = diag::err_opencl_unknown_type_specifier;
        PrevSpec = getSpecifierName(SC);
        return true;
      }
      break;
    case SCS_auto:
    case SCS_register:
      DiagID   = diag::err_opencl_unknown_type_specifier;
      PrevSpec = getSpecifierName(SC);
      return true;
    default:
      break;
    }
  }

  if (StorageClassSpec != SCS_unspecified) {
    // Maybe this is an attempt to use C++11 'auto' outside of C++11 mode.
    bool isInvalid = true;
    if (TypeSpecType == TST_unspecified && S.getLangOpts().CPlusPlus) {
      if (SC == SCS_auto)
        return SetTypeSpecType(TST_auto, Loc, PrevSpec, DiagID, Policy);
      if (StorageClassSpec == SCS_auto) {
        isInvalid = SetTypeSpecType(TST_auto, StorageClassSpecLoc,
                                    PrevSpec, DiagID, Policy);
        assert(!isInvalid && "auto SCS -> TST recovery failed");
      }
    }

    // Changing storage class is allowed only if the previous one
    // was the 'extern' that is part of a linkage specification and
    // the new storage class is 'typedef'.
    if (isInvalid &&
        !(SCS_extern_in_linkage_spec &&
          StorageClassSpec == SCS_extern &&
          SC == SCS_typedef))
      return BadSpecifier(SC, (SCS)StorageClassSpec, PrevSpec, DiagID);
  }
  StorageClassSpec = SC;
  StorageClassSpecLoc = Loc;
  assert((unsigned)SC == StorageClassSpec && "SCS constants overflow bitfield");
  return false;
}

bool DeclSpec::SetStorageClassSpecThread(TSCS TSC, SourceLocation Loc,
                                         const char *&PrevSpec,
                                         unsigned &DiagID) {
  if (ThreadStorageClassSpec != TSCS_unspecified)
    return BadSpecifier(TSC, (TSCS)ThreadStorageClassSpec, PrevSpec, DiagID);

  ThreadStorageClassSpec = TSC;
  ThreadStorageClassSpecLoc = Loc;
  return false;
}

/// These methods set the specified attribute of the DeclSpec, but return true
/// and ignore the request if invalid (e.g. "extern" then "auto" is
/// specified).
bool DeclSpec::SetTypeSpecWidth(TSW W, SourceLocation Loc,
                                const char *&PrevSpec,
                                unsigned &DiagID,
                                const PrintingPolicy &Policy) {
  // Overwrite TSWRange.Begin only if TypeSpecWidth was unspecified, so that
  // for 'long long' we will keep the source location of the first 'long'.
  if (TypeSpecWidth == TSW_unspecified)
    TSWRange.setBegin(Loc);
  // Allow turning long -> long long.
  else if (W != TSW_longlong || TypeSpecWidth != TSW_long)
    return BadSpecifier(W, (TSW)TypeSpecWidth, PrevSpec, DiagID);
  TypeSpecWidth = W;
  // Remember location of the last 'long'
  TSWRange.setEnd(Loc);
  return false;
}

bool DeclSpec::SetTypeSpecComplex(TSC C, SourceLocation Loc,
                                  const char *&PrevSpec,
                                  unsigned &DiagID) {
  if (TypeSpecComplex != TSC_unspecified)
    return BadSpecifier(C, (TSC)TypeSpecComplex, PrevSpec, DiagID);
  TypeSpecComplex = C;
  TSCLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecSign(TSS S, SourceLocation Loc,
                               const char *&PrevSpec,
                               unsigned &DiagID) {
  if (TypeSpecSign != TSS_unspecified)
    return BadSpecifier(S, (TSS)TypeSpecSign, PrevSpec, DiagID);
  TypeSpecSign = S;
  TSSLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc,
                               const char *&PrevSpec,
                               unsigned &DiagID,
                               ParsedType Rep,
                               const PrintingPolicy &Policy) {
  return SetTypeSpecType(T, Loc, Loc, PrevSpec, DiagID, Rep, Policy);
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                               SourceLocation TagNameLoc,
                               const char *&PrevSpec,
                               unsigned &DiagID,
                               ParsedType Rep,
                               const PrintingPolicy &Policy) {
  assert(isTypeRep(T) && "T does not store a type");
  assert(Rep && "no type provided!");
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TypeSpecType = T;
  TypeRep = Rep;
  TSTLoc = TagKwLoc;
  TSTNameLoc = TagNameLoc;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc,
                               const char *&PrevSpec,
                               unsigned &DiagID,
                               Expr *Rep,
                               const PrintingPolicy &Policy) {
  assert(isExprRep(T) && "T does not store an expr");
  assert(Rep && "no expression provided!");
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TypeSpecType = T;
  ExprRep = Rep;
  TSTLoc = Loc;
  TSTNameLoc = Loc;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc,
                               const char *&PrevSpec,
                               unsigned &DiagID,
                               Decl *Rep, bool Owned,
                               const PrintingPolicy &Policy) {
  return SetTypeSpecType(T, Loc, Loc, PrevSpec, DiagID, Rep, Owned, Policy);
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation TagKwLoc,
                               SourceLocation TagNameLoc,
                               const char *&PrevSpec,
                               unsigned &DiagID,
                               Decl *Rep, bool Owned,
                               const PrintingPolicy &Policy) {
  assert(isDeclRep(T) && "T does not store a decl");
  // Unlike the other cases, we don't assert that we actually get a decl.

  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TypeSpecType = T;
  DeclRep = Rep;
  TSTLoc = TagKwLoc;
  TSTNameLoc = TagNameLoc;
  TypeSpecOwned = Owned && Rep != nullptr;
  return false;
}

bool DeclSpec::SetTypeSpecType(TST T, SourceLocation Loc,
                               const char *&PrevSpec,
                               unsigned &DiagID,
                               const PrintingPolicy &Policy) {
  assert(!isDeclRep(T) && !isTypeRep(T) && !isExprRep(T) &&
         "rep required for these type-spec kinds!");
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }
  TSTLoc = Loc;
  TSTNameLoc = Loc;
  if (TypeAltiVecVector && (T == TST_bool) && !TypeAltiVecBool) {
    TypeAltiVecBool = true;
    return false;
  }
  TypeSpecType = T;
  TypeSpecOwned = false;
  return false;
}

bool DeclSpec::SetTypeSpecSat(SourceLocation Loc, const char *&PrevSpec,
                              unsigned &DiagID) {
  // Cannot set twice
  if (TypeSpecSat) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "_Sat";
    return true;
  }
  TypeSpecSat = true;
  TSSatLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeAltiVecVector(bool isAltiVecVector, SourceLocation Loc,
                          const char *&PrevSpec, unsigned &DiagID,
                          const PrintingPolicy &Policy) {
  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_vector_decl_spec_combination;
    return true;
  }
  TypeAltiVecVector = isAltiVecVector;
  AltiVecLoc = Loc;
  return false;
}

bool DeclSpec::SetTypePipe(bool isPipe, SourceLocation Loc,
                           const char *&PrevSpec, unsigned &DiagID,
                           const PrintingPolicy &Policy) {

  if (TypeSpecType != TST_unspecified) {
    PrevSpec = DeclSpec::getSpecifierName((TST)TypeSpecType, Policy);
    DiagID = diag::err_invalid_decl_spec_combination;
    return true;
  }

  if (isPipe) {
    TypeSpecPipe = TSP_pipe;
  }
  return false;
}

bool DeclSpec::SetTypeAltiVecPixel(bool isAltiVecPixel, SourceLocation Loc,
                          const char *&PrevSpec, unsigned &DiagID,
                          const PrintingPolicy &Policy) {
  if (!TypeAltiVecVector || TypeAltiVecPixel ||
      (TypeSpecType != TST_unspecified)) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_pixel_decl_spec_combination;
    return true;
  }
  TypeAltiVecPixel = isAltiVecPixel;
  TSTLoc = Loc;
  TSTNameLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeAltiVecBool(bool isAltiVecBool, SourceLocation Loc,
                                  const char *&PrevSpec, unsigned &DiagID,
                                  const PrintingPolicy &Policy) {
  if (!TypeAltiVecVector || TypeAltiVecBool ||
      (TypeSpecType != TST_unspecified)) {
    PrevSpec = DeclSpec::getSpecifierName((TST) TypeSpecType, Policy);
    DiagID = diag::err_invalid_vector_bool_decl_spec;
    return true;
  }
  TypeAltiVecBool = isAltiVecBool;
  TSTLoc = Loc;
  TSTNameLoc = Loc;
  return false;
}

bool DeclSpec::SetTypeSpecError() {
  TypeSpecType = TST_error;
  TypeSpecOwned = false;
  TSTLoc = SourceLocation();
  TSTNameLoc = SourceLocation();
  return false;
}

bool DeclSpec::SetTypeQual(TQ T, SourceLocation Loc, const char *&PrevSpec,
                           unsigned &DiagID, const LangOptions &Lang) {
  // Duplicates are permitted in C99 onwards, but are not permitted in C89 or
  // C++.  However, since this is likely not what the user intended, we will
  // always warn.  We do not need to set the qualifier's location since we
  // already have it.
  if (TypeQualifiers & T) {
    bool IsExtension = true;
    if (Lang.C99)
      IsExtension = false;
    return BadSpecifier(T, T, PrevSpec, DiagID, IsExtension);
  }

  return SetTypeQual(T, Loc);
}

bool DeclSpec::SetTypeQual(TQ T, SourceLocation Loc) {
  TypeQualifiers |= T;

  switch (T) {
  case TQ_unspecified: break;
  case TQ_const:    TQ_constLoc = Loc; return false;
  case TQ_restrict: TQ_restrictLoc = Loc; return false;
  case TQ_volatile: TQ_volatileLoc = Loc; return false;
  case TQ_unaligned: TQ_unalignedLoc = Loc; return false;
  case TQ_atomic:   TQ_atomicLoc = Loc; return false;
  }

  llvm_unreachable("Unknown type qualifier!");
}

bool DeclSpec::setFunctionSpecInline(SourceLocation Loc, const char *&PrevSpec,
                                     unsigned &DiagID) {
  // 'inline inline' is ok.  However, since this is likely not what the user
  // intended, we will always warn, similar to duplicates of type qualifiers.
  if (FS_inline_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "inline";
    return true;
  }
  FS_inline_specified = true;
  FS_inlineLoc = Loc;
  return false;
}

bool DeclSpec::setFunctionSpecForceInline(SourceLocation Loc, const char *&PrevSpec,
                                          unsigned &DiagID) {
  if (FS_forceinline_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "__forceinline";
    return true;
  }
  FS_forceinline_specified = true;
  FS_forceinlineLoc = Loc;
  return false;
}

bool DeclSpec::setFunctionSpecVirtual(SourceLocation Loc,
                                      const char *&PrevSpec,
                                      unsigned &DiagID) {
  // 'virtual virtual' is ok, but warn as this is likely not what the user
  // intended.
  if (FS_virtual_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "virtual";
    return true;
  }
  FS_virtual_specified = true;
  FS_virtualLoc = Loc;
  return false;
}

bool DeclSpec::setFunctionSpecExplicit(SourceLocation Loc,
                                       const char *&PrevSpec,
                                       unsigned &DiagID) {
  // 'explicit explicit' is ok, but warn as this is likely not what the user
  // intended.
  if (FS_explicit_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "explicit";
    return true;
  }
  FS_explicit_specified = true;
  FS_explicitLoc = Loc;
  return false;
}

bool DeclSpec::setFunctionSpecNoreturn(SourceLocation Loc,
                                       const char *&PrevSpec,
                                       unsigned &DiagID) {
  // '_Noreturn _Noreturn' is ok, but warn as this is likely not what the user
  // intended.
  if (FS_noreturn_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "_Noreturn";
    return true;
  }
  FS_noreturn_specified = true;
  FS_noreturnLoc = Loc;
  return false;
}

bool DeclSpec::SetFriendSpec(SourceLocation Loc, const char *&PrevSpec,
                             unsigned &DiagID) {
  if (Friend_specified) {
    PrevSpec = "friend";
    // Keep the later location, so that we can later diagnose ill-formed
    // declarations like 'friend class X friend;'. Per [class.friend]p3,
    // 'friend' must be the first token in a friend declaration that is
    // not a function declaration.
    FriendLoc = Loc;
    DiagID = diag::warn_duplicate_declspec;
    return true;
  }

  Friend_specified = true;
  FriendLoc = Loc;
  return false;
}

bool DeclSpec::setModulePrivateSpec(SourceLocation Loc, const char *&PrevSpec,
                                    unsigned &DiagID) {
  if (isModulePrivateSpecified()) {
    PrevSpec = "__module_private__";
    DiagID = diag::ext_warn_duplicate_declspec;
    return true;
  }

  ModulePrivateLoc = Loc;
  return false;
}

bool DeclSpec::SetConstexprSpec(SourceLocation Loc, const char *&PrevSpec,
                                unsigned &DiagID) {
  // 'constexpr constexpr' is ok, but warn as this is likely not what the user
  // intended.
  if (Constexpr_specified) {
    DiagID = diag::warn_duplicate_declspec;
    PrevSpec = "constexpr";
    return true;
  }
  Constexpr_specified = true;
  ConstexprLoc = Loc;
  return false;
}

void DeclSpec::SaveWrittenBuiltinSpecs() {
  writtenBS.Sign = getTypeSpecSign();
  writtenBS.Width = getTypeSpecWidth();
  writtenBS.Type = getTypeSpecType();
  // Search the list of attributes for the presence of a mode attribute.
  writtenBS.ModeAttr = getAttributes().hasAttribute(ParsedAttr::AT_Mode);
}

/// Finish - This does final analysis of the declspec, rejecting things like
/// "_Imaginary" (lacking an FP type).  This returns a diagnostic to issue or
/// diag::NUM_DIAGNOSTICS if there is no error.  After calling this method,
/// DeclSpec is guaranteed self-consistent, even if an error occurred.
void DeclSpec::Finish(Sema &S, const PrintingPolicy &Policy) {
  // Before possibly changing their values, save specs as written.
  SaveWrittenBuiltinSpecs();

  // Check the type specifier components first.

  // If decltype(auto) is used, no other type specifiers are permitted.
  if (TypeSpecType == TST_decltype_auto &&
      (TypeSpecWidth != TSW_unspecified ||
       TypeSpecComplex != TSC_unspecified ||
       TypeSpecSign != TSS_unspecified ||
       TypeAltiVecVector || TypeAltiVecPixel || TypeAltiVecBool ||
       TypeQualifiers)) {
    const unsigned NumLocs = 9;
    SourceLocation ExtraLocs[NumLocs] = {
        TSWRange.getBegin(), TSCLoc,       TSSLoc,
        AltiVecLoc,          TQ_constLoc,  TQ_restrictLoc,
        TQ_volatileLoc,      TQ_atomicLoc, TQ_unalignedLoc};
    FixItHint Hints[NumLocs];
    SourceLocation FirstLoc;
    for (unsigned I = 0; I != NumLocs; ++I) {
      if (ExtraLocs[I].isValid()) {
        if (FirstLoc.isInvalid() ||
            S.getSourceManager().isBeforeInTranslationUnit(ExtraLocs[I],
                                                           FirstLoc))
          FirstLoc = ExtraLocs[I];
        Hints[I] = FixItHint::CreateRemoval(ExtraLocs[I]);
      }
    }
    TypeSpecWidth = TSW_unspecified;
    TypeSpecComplex = TSC_unspecified;
    TypeSpecSign = TSS_unspecified;
    TypeAltiVecVector = TypeAltiVecPixel = TypeAltiVecBool = false;
    TypeQualifiers = 0;
    S.Diag(TSTLoc, diag::err_decltype_auto_cannot_be_combined)
      << Hints[0] << Hints[1] << Hints[2] << Hints[3]
      << Hints[4] << Hints[5] << Hints[6] << Hints[7];
  }

  // Validate and finalize AltiVec vector declspec.
  if (TypeAltiVecVector) {
    if (TypeAltiVecBool) {
      // Sign specifiers are not allowed with vector bool. (PIM 2.1)
      if (TypeSpecSign != TSS_unspecified) {
        S.Diag(TSSLoc, diag::err_invalid_vector_bool_decl_spec)
          << getSpecifierName((TSS)TypeSpecSign);
      }

      // Only char/int are valid with vector bool. (PIM 2.1)
      if (((TypeSpecType != TST_unspecified) && (TypeSpecType != TST_char) &&
           (TypeSpecType != TST_int)) || TypeAltiVecPixel) {
        S.Diag(TSTLoc, diag::err_invalid_vector_bool_decl_spec)
          << (TypeAltiVecPixel ? "__pixel" :
                                 getSpecifierName((TST)TypeSpecType, Policy));
      }

      // Only 'short' and 'long long' are valid with vector bool. (PIM 2.1)
      if ((TypeSpecWidth != TSW_unspecified) && (TypeSpecWidth != TSW_short) &&
          (TypeSpecWidth != TSW_longlong))
        S.Diag(TSWRange.getBegin(), diag::err_invalid_vector_bool_decl_spec)
            << getSpecifierName((TSW)TypeSpecWidth);

      // vector bool long long requires VSX support or ZVector.
      if ((TypeSpecWidth == TSW_longlong) &&
          (!S.Context.getTargetInfo().hasFeature("vsx")) &&
          (!S.Context.getTargetInfo().hasFeature("power8-vector")) &&
          !S.getLangOpts().ZVector)
        S.Diag(TSTLoc, diag::err_invalid_vector_long_long_decl_spec);

      // Elements of vector bool are interpreted as unsigned. (PIM 2.1)
      if ((TypeSpecType == TST_char) || (TypeSpecType == TST_int) ||
          (TypeSpecWidth != TSW_unspecified))
        TypeSpecSign = TSS_unsigned;
    } else if (TypeSpecType == TST_double) {
      // vector long double and vector long long double are never allowed.
      // vector double is OK for Power7 and later, and ZVector.
      if (TypeSpecWidth == TSW_long || TypeSpecWidth == TSW_longlong)
        S.Diag(TSWRange.getBegin(),
               diag::err_invalid_vector_long_double_decl_spec);
      else if (!S.Context.getTargetInfo().hasFeature("vsx") &&
               !S.getLangOpts().ZVector)
        S.Diag(TSTLoc, diag::err_invalid_vector_double_decl_spec);
    } else if (TypeSpecType == TST_float) {
      // vector float is unsupported for ZVector unless we have the
      // vector-enhancements facility 1 (ISA revision 12).
      if (S.getLangOpts().ZVector &&
          !S.Context.getTargetInfo().hasFeature("arch12"))
        S.Diag(TSTLoc, diag::err_invalid_vector_float_decl_spec);
    } else if (TypeSpecWidth == TSW_long) {
      // vector long is unsupported for ZVector and deprecated for AltiVec.
      if (S.getLangOpts().ZVector)
        S.Diag(TSWRange.getBegin(), diag::err_invalid_vector_long_decl_spec);
      else
        S.Diag(TSWRange.getBegin(),
               diag::warn_vector_long_decl_spec_combination)
            << getSpecifierName((TST)TypeSpecType, Policy);
    }

    if (TypeAltiVecPixel) {
      //TODO: perform validation
      TypeSpecType = TST_int;
      TypeSpecSign = TSS_unsigned;
      TypeSpecWidth = TSW_short;
      TypeSpecOwned = false;
    }
  }

  bool IsFixedPointType =
      TypeSpecType == TST_accum || TypeSpecType == TST_fract;

  // signed/unsigned are only valid with int/char/wchar_t/_Accum.
  if (TypeSpecSign != TSS_unspecified) {
    if (TypeSpecType == TST_unspecified)
      TypeSpecType = TST_int; // unsigned -> unsigned int, signed -> signed int.
    else if (TypeSpecType != TST_int && TypeSpecType != TST_int128 &&
             TypeSpecType != TST_char && TypeSpecType != TST_wchar &&
             !IsFixedPointType) {
      S.Diag(TSSLoc, diag::err_invalid_sign_spec)
        << getSpecifierName((TST)TypeSpecType, Policy);
      // signed double -> double.
      TypeSpecSign = TSS_unspecified;
    }
  }

  // Validate the width of the type.
  switch (TypeSpecWidth) {
  case TSW_unspecified: break;
  case TSW_short:    // short int
  case TSW_longlong: // long long int
    if (TypeSpecType == TST_unspecified)
      TypeSpecType = TST_int; // short -> short int, long long -> long long int.
    else if (!(TypeSpecType == TST_int ||
               (IsFixedPointType && TypeSpecWidth != TSW_longlong))) {
      S.Diag(TSWRange.getBegin(), diag::err_invalid_width_spec)
          << (int)TypeSpecWidth << getSpecifierName((TST)TypeSpecType, Policy);
      TypeSpecType = TST_int;
      TypeSpecSat = false;
      TypeSpecOwned = false;
    }
    break;
  case TSW_long:  // long double, long int
    if (TypeSpecType == TST_unspecified)
      TypeSpecType = TST_int;  // long -> long int.
    else if (TypeSpecType != TST_int && TypeSpecType != TST_double &&
             !IsFixedPointType) {
      S.Diag(TSWRange.getBegin(), diag::err_invalid_width_spec)
          << (int)TypeSpecWidth << getSpecifierName((TST)TypeSpecType, Policy);
      TypeSpecType = TST_int;
      TypeSpecSat = false;
      TypeSpecOwned = false;
    }
    break;
  }

  // TODO: if the implementation does not implement _Complex or _Imaginary,
  // disallow their use.  Need information about the backend.
  if (TypeSpecComplex != TSC_unspecified) {
    if (TypeSpecType == TST_unspecified) {
      S.Diag(TSCLoc, diag::ext_plain_complex)
        << FixItHint::CreateInsertion(
                              S.getLocForEndOfToken(getTypeSpecComplexLoc()),
                                                 " double");
      TypeSpecType = TST_double;   // _Complex -> _Complex double.
    } else if (TypeSpecType == TST_int || TypeSpecType == TST_char) {
      // Note that this intentionally doesn't include _Complex _Bool.
      if (!S.getLangOpts().CPlusPlus)
        S.Diag(TSTLoc, diag::ext_integer_complex);
    } else if (TypeSpecType != TST_float && TypeSpecType != TST_double) {
      S.Diag(TSCLoc, diag::err_invalid_complex_spec)
        << getSpecifierName((TST)TypeSpecType, Policy);
      TypeSpecComplex = TSC_unspecified;
    }
  }

  // C11 6.7.1/3, C++11 [dcl.stc]p1, GNU TLS: __thread, thread_local and
  // _Thread_local can only appear with the 'static' and 'extern' storage class
  // specifiers. We also allow __private_extern__ as an extension.
  if (ThreadStorageClassSpec != TSCS_unspecified) {
    switch (StorageClassSpec) {
    case SCS_unspecified:
    case SCS_extern:
    case SCS_private_extern:
    case SCS_static:
      break;
    default:
      if (S.getSourceManager().isBeforeInTranslationUnit(
            getThreadStorageClassSpecLoc(), getStorageClassSpecLoc()))
        S.Diag(getStorageClassSpecLoc(),
             diag::err_invalid_decl_spec_combination)
          << DeclSpec::getSpecifierName(getThreadStorageClassSpec())
          << SourceRange(getThreadStorageClassSpecLoc());
      else
        S.Diag(getThreadStorageClassSpecLoc(),
             diag::err_invalid_decl_spec_combination)
          << DeclSpec::getSpecifierName(getStorageClassSpec())
          << SourceRange(getStorageClassSpecLoc());
      // Discard the thread storage class specifier to recover.
      ThreadStorageClassSpec = TSCS_unspecified;
      ThreadStorageClassSpecLoc = SourceLocation();
    }
  }

  // If no type specifier was provided and we're parsing a language where
  // the type specifier is not optional, but we got 'auto' as a storage
  // class specifier, then assume this is an attempt to use C++0x's 'auto'
  // type specifier.
  if (S.getLangOpts().CPlusPlus &&
      TypeSpecType == TST_unspecified && StorageClassSpec == SCS_auto) {
    TypeSpecType = TST_auto;
    StorageClassSpec = SCS_unspecified;
    TSTLoc = TSTNameLoc = StorageClassSpecLoc;
    StorageClassSpecLoc = SourceLocation();
  }
  // Diagnose if we've recovered from an ill-formed 'auto' storage class
  // specifier in a pre-C++11 dialect of C++.
  if (!S.getLangOpts().CPlusPlus11 && TypeSpecType == TST_auto)
    S.Diag(TSTLoc, diag::ext_auto_type_specifier);
  if (S.getLangOpts().CPlusPlus && !S.getLangOpts().CPlusPlus11 &&
      StorageClassSpec == SCS_auto)
    S.Diag(StorageClassSpecLoc, diag::warn_auto_storage_class)
      << FixItHint::CreateRemoval(StorageClassSpecLoc);
  if (TypeSpecType == TST_char8)
    S.Diag(TSTLoc, diag::warn_cxx17_compat_unicode_type);
  else if (TypeSpecType == TST_char16 || TypeSpecType == TST_char32)
    S.Diag(TSTLoc, diag::warn_cxx98_compat_unicode_type)
      << (TypeSpecType == TST_char16 ? "char16_t" : "char32_t");
  if (Constexpr_specified)
    S.Diag(ConstexprLoc, diag::warn_cxx98_compat_constexpr);

  // C++ [class.friend]p6:
  //   No storage-class-specifier shall appear in the decl-specifier-seq
  //   of a friend declaration.
  if (isFriendSpecified() &&
      (getStorageClassSpec() || getThreadStorageClassSpec())) {
    SmallString<32> SpecName;
    SourceLocation SCLoc;
    FixItHint StorageHint, ThreadHint;

    if (DeclSpec::SCS SC = getStorageClassSpec()) {
      SpecName = getSpecifierName(SC);
      SCLoc = getStorageClassSpecLoc();
      StorageHint = FixItHint::CreateRemoval(SCLoc);
    }

    if (DeclSpec::TSCS TSC = getThreadStorageClassSpec()) {
      if (!SpecName.empty()) SpecName += " ";
      SpecName += getSpecifierName(TSC);
      SCLoc = getThreadStorageClassSpecLoc();
      ThreadHint = FixItHint::CreateRemoval(SCLoc);
    }

    S.Diag(SCLoc, diag::err_friend_decl_spec)
      << SpecName << StorageHint << ThreadHint;

    ClearStorageClassSpecs();
  }

  // C++11 [dcl.fct.spec]p5:
  //   The virtual specifier shall be used only in the initial
  //   declaration of a non-static class member function;
  // C++11 [dcl.fct.spec]p6:
  //   The explicit specifier shall be used only in the declaration of
  //   a constructor or conversion function within its class
  //   definition;
  if (isFriendSpecified() && (isVirtualSpecified() || isExplicitSpecified())) {
    StringRef Keyword;
    SourceLocation SCLoc;

    if (isVirtualSpecified()) {
      Keyword = "virtual";
      SCLoc = getVirtualSpecLoc();
    } else {
      Keyword = "explicit";
      SCLoc = getExplicitSpecLoc();
    }

    FixItHint Hint = FixItHint::CreateRemoval(SCLoc);
    S.Diag(SCLoc, diag::err_friend_decl_spec)
      << Keyword << Hint;

    FS_virtual_specified = FS_explicit_specified = false;
    FS_virtualLoc = FS_explicitLoc = SourceLocation();
  }

  assert(!TypeSpecOwned || isDeclRep((TST) TypeSpecType));

  // Okay, now we can infer the real type.

  // TODO: return "auto function" and other bad things based on the real type.

  // 'data definition has no type or storage class'?
}

bool DeclSpec::isMissingDeclaratorOk() {
  TST tst = getTypeSpecType();
  return isDeclRep(tst) && getRepAsDecl() != nullptr &&
    StorageClassSpec != DeclSpec::SCS_typedef;
}

void UnqualifiedId::setOperatorFunctionId(SourceLocation OperatorLoc,
                                          OverloadedOperatorKind Op,
                                          SourceLocation SymbolLocations[3]) {
  Kind = UnqualifiedIdKind::IK_OperatorFunctionId;
  StartLocation = OperatorLoc;
  EndLocation = OperatorLoc;
  OperatorFunctionId.Operator = Op;
  for (unsigned I = 0; I != 3; ++I) {
    OperatorFunctionId.SymbolLocations[I] = SymbolLocations[I].getRawEncoding();

    if (SymbolLocations[I].isValid())
      EndLocation = SymbolLocations[I];
  }
}

bool VirtSpecifiers::SetSpecifier(Specifier VS, SourceLocation Loc,
                                  const char *&PrevSpec) {
  if (!FirstLocation.isValid())
    FirstLocation = Loc;
  LastLocation = Loc;
  LastSpecifier = VS;

  if (Specifiers & VS) {
    PrevSpec = getSpecifierName(VS);
    return true;
  }

  Specifiers |= VS;

  switch (VS) {
  default: llvm_unreachable("Unknown specifier!");
  case VS_Override: VS_overrideLoc = Loc; break;
  case VS_GNU_Final:
  case VS_Sealed:
  case VS_Final:    VS_finalLoc = Loc; break;
  }

  return false;
}

const char *VirtSpecifiers::getSpecifierName(Specifier VS) {
  switch (VS) {
  default: llvm_unreachable("Unknown specifier");
  case VS_Override: return "override";
  case VS_Final: return "final";
  case VS_GNU_Final: return "__final";
  case VS_Sealed: return "sealed";
  }
}
