//===- ASTStructuralEquivalence.cpp ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implement StructuralEquivalenceContext class and helper functions
//  for layout matching.
//
// The structural equivalence check could have been implemented as a parallel
// BFS on a pair of graphs.  That must have been the original approach at the
// beginning.
// Let's consider this simple BFS algorithm from the `s` source:
// ```
// void bfs(Graph G, int s)
// {
//   Queue<Integer> queue = new Queue<Integer>();
//   marked[s] = true; // Mark the source
//   queue.enqueue(s); // and put it on the queue.
//   while (!q.isEmpty()) {
//     int v = queue.dequeue(); // Remove next vertex from the queue.
//     for (int w : G.adj(v))
//       if (!marked[w]) // For every unmarked adjacent vertex,
//       {
//         marked[w] = true;
//         queue.enqueue(w);
//       }
//   }
// }
// ```
// Indeed, it has it's queue, which holds pairs of nodes, one from each graph,
// this is the `DeclsToCheck` and it's pair is in `TentativeEquivalences`.
// `TentativeEquivalences` also plays the role of the marking (`marked`)
// functionality above, we use it to check whether we've already seen a pair of
// nodes.
//
// We put in the elements into the queue only in the toplevel decl check
// function:
// ```
// static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
//                                      Decl *D1, Decl *D2);
// ```
// The `while` loop where we iterate over the children is implemented in
// `Finish()`.  And `Finish` is called only from the two **member** functions
// which check the equivalency of two Decls or two Types. ASTImporter (and
// other clients) call only these functions.
//
// The `static` implementation functions are called from `Finish`, these push
// the children nodes to the queue via `static bool
// IsStructurallyEquivalent(StructuralEquivalenceContext &Context, Decl *D1,
// Decl *D2)`.  So far so good, this is almost like the BFS.  However, if we
// let a static implementation function to call `Finish` via another **member**
// function that means we end up with two nested while loops each of them
// working on the same queue. This is wrong and nobody can reason about it's
// doing. Thus, static implementation functions must not call the **member**
// functions.
//
// So, now `TentativeEquivalences` plays two roles. It is used to store the
// second half of the decls which we want to compare, plus it plays a role in
// closing the recursion. On a long term, we could refactor structural
// equivalency to be more alike to the traditional BFS.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <utility>

using namespace clang;

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     QualType T1, QualType T2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     Decl *D1, Decl *D2);
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateArgument &Arg1,
                                     const TemplateArgument &Arg2);

/// Determine structural equivalence of two expressions.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const Expr *E1, const Expr *E2) {
  if (!E1 || !E2)
    return E1 == E2;

  // FIXME: Actually perform a structural comparison!
  return true;
}

/// Determine whether two identifiers are equivalent.
static bool IsStructurallyEquivalent(const IdentifierInfo *Name1,
                                     const IdentifierInfo *Name2) {
  if (!Name1 || !Name2)
    return Name1 == Name2;

  return Name1->getName() == Name2->getName();
}

/// Determine whether two nested-name-specifiers are equivalent.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     NestedNameSpecifier *NNS1,
                                     NestedNameSpecifier *NNS2) {
  if (NNS1->getKind() != NNS2->getKind())
    return false;

  NestedNameSpecifier *Prefix1 = NNS1->getPrefix(),
                      *Prefix2 = NNS2->getPrefix();
  if ((bool)Prefix1 != (bool)Prefix2)
    return false;

  if (Prefix1)
    if (!IsStructurallyEquivalent(Context, Prefix1, Prefix2))
      return false;

  switch (NNS1->getKind()) {
  case NestedNameSpecifier::Identifier:
    return IsStructurallyEquivalent(NNS1->getAsIdentifier(),
                                    NNS2->getAsIdentifier());
  case NestedNameSpecifier::Namespace:
    return IsStructurallyEquivalent(Context, NNS1->getAsNamespace(),
                                    NNS2->getAsNamespace());
  case NestedNameSpecifier::NamespaceAlias:
    return IsStructurallyEquivalent(Context, NNS1->getAsNamespaceAlias(),
                                    NNS2->getAsNamespaceAlias());
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    return IsStructurallyEquivalent(Context, QualType(NNS1->getAsType(), 0),
                                    QualType(NNS2->getAsType(), 0));
  case NestedNameSpecifier::Global:
    return true;
  case NestedNameSpecifier::Super:
    return IsStructurallyEquivalent(Context, NNS1->getAsRecordDecl(),
                                    NNS2->getAsRecordDecl());
  }
  return false;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateName &N1,
                                     const TemplateName &N2) {
  if (N1.getKind() != N2.getKind())
    return false;
  switch (N1.getKind()) {
  case TemplateName::Template:
    return IsStructurallyEquivalent(Context, N1.getAsTemplateDecl(),
                                    N2.getAsTemplateDecl());

  case TemplateName::OverloadedTemplate: {
    OverloadedTemplateStorage *OS1 = N1.getAsOverloadedTemplate(),
                              *OS2 = N2.getAsOverloadedTemplate();
    OverloadedTemplateStorage::iterator I1 = OS1->begin(), I2 = OS2->begin(),
                                        E1 = OS1->end(), E2 = OS2->end();
    for (; I1 != E1 && I2 != E2; ++I1, ++I2)
      if (!IsStructurallyEquivalent(Context, *I1, *I2))
        return false;
    return I1 == E1 && I2 == E2;
  }

  case TemplateName::QualifiedTemplate: {
    QualifiedTemplateName *QN1 = N1.getAsQualifiedTemplateName(),
                          *QN2 = N2.getAsQualifiedTemplateName();
    return IsStructurallyEquivalent(Context, QN1->getDecl(), QN2->getDecl()) &&
           IsStructurallyEquivalent(Context, QN1->getQualifier(),
                                    QN2->getQualifier());
  }

  case TemplateName::DependentTemplate: {
    DependentTemplateName *DN1 = N1.getAsDependentTemplateName(),
                          *DN2 = N2.getAsDependentTemplateName();
    if (!IsStructurallyEquivalent(Context, DN1->getQualifier(),
                                  DN2->getQualifier()))
      return false;
    if (DN1->isIdentifier() && DN2->isIdentifier())
      return IsStructurallyEquivalent(DN1->getIdentifier(),
                                      DN2->getIdentifier());
    else if (DN1->isOverloadedOperator() && DN2->isOverloadedOperator())
      return DN1->getOperator() == DN2->getOperator();
    return false;
  }

  case TemplateName::SubstTemplateTemplateParm: {
    SubstTemplateTemplateParmStorage *TS1 = N1.getAsSubstTemplateTemplateParm(),
                                     *TS2 = N2.getAsSubstTemplateTemplateParm();
    return IsStructurallyEquivalent(Context, TS1->getParameter(),
                                    TS2->getParameter()) &&
           IsStructurallyEquivalent(Context, TS1->getReplacement(),
                                    TS2->getReplacement());
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    SubstTemplateTemplateParmPackStorage
        *P1 = N1.getAsSubstTemplateTemplateParmPack(),
        *P2 = N2.getAsSubstTemplateTemplateParmPack();
    return IsStructurallyEquivalent(Context, P1->getArgumentPack(),
                                    P2->getArgumentPack()) &&
           IsStructurallyEquivalent(Context, P1->getParameterPack(),
                                    P2->getParameterPack());
  }
  }
  return false;
}

/// Determine whether two template arguments are equivalent.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     const TemplateArgument &Arg1,
                                     const TemplateArgument &Arg2) {
  if (Arg1.getKind() != Arg2.getKind())
    return false;

  switch (Arg1.getKind()) {
  case TemplateArgument::Null:
    return true;

  case TemplateArgument::Type:
    return IsStructurallyEquivalent(Context, Arg1.getAsType(), Arg2.getAsType());

  case TemplateArgument::Integral:
    if (!IsStructurallyEquivalent(Context, Arg1.getIntegralType(),
                                          Arg2.getIntegralType()))
      return false;

    return llvm::APSInt::isSameValue(Arg1.getAsIntegral(),
                                     Arg2.getAsIntegral());

  case TemplateArgument::Declaration:
    return IsStructurallyEquivalent(Context, Arg1.getAsDecl(), Arg2.getAsDecl());

  case TemplateArgument::NullPtr:
    return true; // FIXME: Is this correct?

  case TemplateArgument::Template:
    return IsStructurallyEquivalent(Context, Arg1.getAsTemplate(),
                                    Arg2.getAsTemplate());

  case TemplateArgument::TemplateExpansion:
    return IsStructurallyEquivalent(Context,
                                    Arg1.getAsTemplateOrTemplatePattern(),
                                    Arg2.getAsTemplateOrTemplatePattern());

  case TemplateArgument::Expression:
    return IsStructurallyEquivalent(Context, Arg1.getAsExpr(),
                                    Arg2.getAsExpr());

  case TemplateArgument::Pack:
    if (Arg1.pack_size() != Arg2.pack_size())
      return false;

    for (unsigned I = 0, N = Arg1.pack_size(); I != N; ++I)
      if (!IsStructurallyEquivalent(Context, Arg1.pack_begin()[I],
                                    Arg2.pack_begin()[I]))
        return false;

    return true;
  }

  llvm_unreachable("Invalid template argument kind");
}

/// Determine structural equivalence for the common part of array
/// types.
static bool IsArrayStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                          const ArrayType *Array1,
                                          const ArrayType *Array2) {
  if (!IsStructurallyEquivalent(Context, Array1->getElementType(),
                                Array2->getElementType()))
    return false;
  if (Array1->getSizeModifier() != Array2->getSizeModifier())
    return false;
  if (Array1->getIndexTypeQualifiers() != Array2->getIndexTypeQualifiers())
    return false;

  return true;
}

/// Determine structural equivalence of two types.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     QualType T1, QualType T2) {
  if (T1.isNull() || T2.isNull())
    return T1.isNull() && T2.isNull();

  QualType OrigT1 = T1;
  QualType OrigT2 = T2;

  if (!Context.StrictTypeSpelling) {
    // We aren't being strict about token-to-token equivalence of types,
    // so map down to the canonical type.
    T1 = Context.FromCtx.getCanonicalType(T1);
    T2 = Context.ToCtx.getCanonicalType(T2);
  }

  if (T1.getQualifiers() != T2.getQualifiers())
    return false;

  Type::TypeClass TC = T1->getTypeClass();

  if (T1->getTypeClass() != T2->getTypeClass()) {
    // Compare function types with prototypes vs. without prototypes as if
    // both did not have prototypes.
    if (T1->getTypeClass() == Type::FunctionProto &&
        T2->getTypeClass() == Type::FunctionNoProto)
      TC = Type::FunctionNoProto;
    else if (T1->getTypeClass() == Type::FunctionNoProto &&
             T2->getTypeClass() == Type::FunctionProto)
      TC = Type::FunctionNoProto;
    else
      return false;
  }

  switch (TC) {
  case Type::Builtin:
    // FIXME: Deal with Char_S/Char_U.
    if (cast<BuiltinType>(T1)->getKind() != cast<BuiltinType>(T2)->getKind())
      return false;
    break;

  case Type::Complex:
    if (!IsStructurallyEquivalent(Context,
                                  cast<ComplexType>(T1)->getElementType(),
                                  cast<ComplexType>(T2)->getElementType()))
      return false;
    break;

  case Type::Adjusted:
  case Type::Decayed:
    if (!IsStructurallyEquivalent(Context,
                                  cast<AdjustedType>(T1)->getOriginalType(),
                                  cast<AdjustedType>(T2)->getOriginalType()))
      return false;
    break;

  case Type::Pointer:
    if (!IsStructurallyEquivalent(Context,
                                  cast<PointerType>(T1)->getPointeeType(),
                                  cast<PointerType>(T2)->getPointeeType()))
      return false;
    break;

  case Type::BlockPointer:
    if (!IsStructurallyEquivalent(Context,
                                  cast<BlockPointerType>(T1)->getPointeeType(),
                                  cast<BlockPointerType>(T2)->getPointeeType()))
      return false;
    break;

  case Type::LValueReference:
  case Type::RValueReference: {
    const auto *Ref1 = cast<ReferenceType>(T1);
    const auto *Ref2 = cast<ReferenceType>(T2);
    if (Ref1->isSpelledAsLValue() != Ref2->isSpelledAsLValue())
      return false;
    if (Ref1->isInnerRef() != Ref2->isInnerRef())
      return false;
    if (!IsStructurallyEquivalent(Context, Ref1->getPointeeTypeAsWritten(),
                                  Ref2->getPointeeTypeAsWritten()))
      return false;
    break;
  }

  case Type::MemberPointer: {
    const auto *MemPtr1 = cast<MemberPointerType>(T1);
    const auto *MemPtr2 = cast<MemberPointerType>(T2);
    if (!IsStructurallyEquivalent(Context, MemPtr1->getPointeeType(),
                                  MemPtr2->getPointeeType()))
      return false;
    if (!IsStructurallyEquivalent(Context, QualType(MemPtr1->getClass(), 0),
                                  QualType(MemPtr2->getClass(), 0)))
      return false;
    break;
  }

  case Type::ConstantArray: {
    const auto *Array1 = cast<ConstantArrayType>(T1);
    const auto *Array2 = cast<ConstantArrayType>(T2);
    if (!llvm::APInt::isSameValue(Array1->getSize(), Array2->getSize()))
      return false;

    if (!IsArrayStructurallyEquivalent(Context, Array1, Array2))
      return false;
    break;
  }

  case Type::IncompleteArray:
    if (!IsArrayStructurallyEquivalent(Context, cast<ArrayType>(T1),
                                       cast<ArrayType>(T2)))
      return false;
    break;

  case Type::VariableArray: {
    const auto *Array1 = cast<VariableArrayType>(T1);
    const auto *Array2 = cast<VariableArrayType>(T2);
    if (!IsStructurallyEquivalent(Context, Array1->getSizeExpr(),
                                  Array2->getSizeExpr()))
      return false;

    if (!IsArrayStructurallyEquivalent(Context, Array1, Array2))
      return false;

    break;
  }

  case Type::DependentSizedArray: {
    const auto *Array1 = cast<DependentSizedArrayType>(T1);
    const auto *Array2 = cast<DependentSizedArrayType>(T2);
    if (!IsStructurallyEquivalent(Context, Array1->getSizeExpr(),
                                  Array2->getSizeExpr()))
      return false;

    if (!IsArrayStructurallyEquivalent(Context, Array1, Array2))
      return false;

    break;
  }

  case Type::DependentAddressSpace: {
    const auto *DepAddressSpace1 = cast<DependentAddressSpaceType>(T1);
    const auto *DepAddressSpace2 = cast<DependentAddressSpaceType>(T2);
    if (!IsStructurallyEquivalent(Context, DepAddressSpace1->getAddrSpaceExpr(),
                                  DepAddressSpace2->getAddrSpaceExpr()))
      return false;
    if (!IsStructurallyEquivalent(Context, DepAddressSpace1->getPointeeType(),
                                  DepAddressSpace2->getPointeeType()))
      return false;

    break;
  }

  case Type::DependentSizedExtVector: {
    const auto *Vec1 = cast<DependentSizedExtVectorType>(T1);
    const auto *Vec2 = cast<DependentSizedExtVectorType>(T2);
    if (!IsStructurallyEquivalent(Context, Vec1->getSizeExpr(),
                                  Vec2->getSizeExpr()))
      return false;
    if (!IsStructurallyEquivalent(Context, Vec1->getElementType(),
                                  Vec2->getElementType()))
      return false;
    break;
  }

  case Type::DependentVector: {
    const auto *Vec1 = cast<DependentVectorType>(T1);
    const auto *Vec2 = cast<DependentVectorType>(T2);
    if (Vec1->getVectorKind() != Vec2->getVectorKind())
      return false;
    if (!IsStructurallyEquivalent(Context, Vec1->getSizeExpr(),
                                  Vec2->getSizeExpr()))
      return false;
    if (!IsStructurallyEquivalent(Context, Vec1->getElementType(),
                                  Vec2->getElementType()))
      return false;
    break;
  }

  case Type::Vector:
  case Type::ExtVector: {
    const auto *Vec1 = cast<VectorType>(T1);
    const auto *Vec2 = cast<VectorType>(T2);
    if (!IsStructurallyEquivalent(Context, Vec1->getElementType(),
                                  Vec2->getElementType()))
      return false;
    if (Vec1->getNumElements() != Vec2->getNumElements())
      return false;
    if (Vec1->getVectorKind() != Vec2->getVectorKind())
      return false;
    break;
  }

  case Type::FunctionProto: {
    const auto *Proto1 = cast<FunctionProtoType>(T1);
    const auto *Proto2 = cast<FunctionProtoType>(T2);

    if (Proto1->getNumParams() != Proto2->getNumParams())
      return false;
    for (unsigned I = 0, N = Proto1->getNumParams(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Proto1->getParamType(I),
                                    Proto2->getParamType(I)))
        return false;
    }
    if (Proto1->isVariadic() != Proto2->isVariadic())
      return false;

    if (Proto1->getTypeQuals() != Proto2->getTypeQuals())
      return false;

    // Check exceptions, this information is lost in canonical type.
    const auto *OrigProto1 =
        cast<FunctionProtoType>(OrigT1.getDesugaredType(Context.FromCtx));
    const auto *OrigProto2 =
        cast<FunctionProtoType>(OrigT2.getDesugaredType(Context.ToCtx));
    auto Spec1 = OrigProto1->getExceptionSpecType();
    auto Spec2 = OrigProto2->getExceptionSpecType();

    if (Spec1 != Spec2)
      return false;
    if (Spec1 == EST_Dynamic) {
      if (OrigProto1->getNumExceptions() != OrigProto2->getNumExceptions())
        return false;
      for (unsigned I = 0, N = OrigProto1->getNumExceptions(); I != N; ++I) {
        if (!IsStructurallyEquivalent(Context, OrigProto1->getExceptionType(I),
                                      OrigProto2->getExceptionType(I)))
          return false;
      }
    } else if (isComputedNoexcept(Spec1)) {
      if (!IsStructurallyEquivalent(Context, OrigProto1->getNoexceptExpr(),
                                    OrigProto2->getNoexceptExpr()))
        return false;
    }

    // Fall through to check the bits common with FunctionNoProtoType.
    LLVM_FALLTHROUGH;
  }

  case Type::FunctionNoProto: {
    const auto *Function1 = cast<FunctionType>(T1);
    const auto *Function2 = cast<FunctionType>(T2);
    if (!IsStructurallyEquivalent(Context, Function1->getReturnType(),
                                  Function2->getReturnType()))
      return false;
    if (Function1->getExtInfo() != Function2->getExtInfo())
      return false;
    break;
  }

  case Type::UnresolvedUsing:
    if (!IsStructurallyEquivalent(Context,
                                  cast<UnresolvedUsingType>(T1)->getDecl(),
                                  cast<UnresolvedUsingType>(T2)->getDecl()))
      return false;
    break;

  case Type::Attributed:
    if (!IsStructurallyEquivalent(Context,
                                  cast<AttributedType>(T1)->getModifiedType(),
                                  cast<AttributedType>(T2)->getModifiedType()))
      return false;
    if (!IsStructurallyEquivalent(
            Context, cast<AttributedType>(T1)->getEquivalentType(),
            cast<AttributedType>(T2)->getEquivalentType()))
      return false;
    break;

  case Type::Paren:
    if (!IsStructurallyEquivalent(Context, cast<ParenType>(T1)->getInnerType(),
                                  cast<ParenType>(T2)->getInnerType()))
      return false;
    break;

  case Type::Typedef:
    if (!IsStructurallyEquivalent(Context, cast<TypedefType>(T1)->getDecl(),
                                  cast<TypedefType>(T2)->getDecl()))
      return false;
    break;

  case Type::TypeOfExpr:
    if (!IsStructurallyEquivalent(
            Context, cast<TypeOfExprType>(T1)->getUnderlyingExpr(),
            cast<TypeOfExprType>(T2)->getUnderlyingExpr()))
      return false;
    break;

  case Type::TypeOf:
    if (!IsStructurallyEquivalent(Context,
                                  cast<TypeOfType>(T1)->getUnderlyingType(),
                                  cast<TypeOfType>(T2)->getUnderlyingType()))
      return false;
    break;

  case Type::UnaryTransform:
    if (!IsStructurallyEquivalent(
            Context, cast<UnaryTransformType>(T1)->getUnderlyingType(),
            cast<UnaryTransformType>(T2)->getUnderlyingType()))
      return false;
    break;

  case Type::Decltype:
    if (!IsStructurallyEquivalent(Context,
                                  cast<DecltypeType>(T1)->getUnderlyingExpr(),
                                  cast<DecltypeType>(T2)->getUnderlyingExpr()))
      return false;
    break;

  case Type::Auto:
    if (!IsStructurallyEquivalent(Context, cast<AutoType>(T1)->getDeducedType(),
                                  cast<AutoType>(T2)->getDeducedType()))
      return false;
    break;

  case Type::DeducedTemplateSpecialization: {
    const auto *DT1 = cast<DeducedTemplateSpecializationType>(T1);
    const auto *DT2 = cast<DeducedTemplateSpecializationType>(T2);
    if (!IsStructurallyEquivalent(Context, DT1->getTemplateName(),
                                  DT2->getTemplateName()))
      return false;
    if (!IsStructurallyEquivalent(Context, DT1->getDeducedType(),
                                  DT2->getDeducedType()))
      return false;
    break;
  }

  case Type::Record:
  case Type::Enum:
    if (!IsStructurallyEquivalent(Context, cast<TagType>(T1)->getDecl(),
                                  cast<TagType>(T2)->getDecl()))
      return false;
    break;

  case Type::TemplateTypeParm: {
    const auto *Parm1 = cast<TemplateTypeParmType>(T1);
    const auto *Parm2 = cast<TemplateTypeParmType>(T2);
    if (Parm1->getDepth() != Parm2->getDepth())
      return false;
    if (Parm1->getIndex() != Parm2->getIndex())
      return false;
    if (Parm1->isParameterPack() != Parm2->isParameterPack())
      return false;

    // Names of template type parameters are never significant.
    break;
  }

  case Type::SubstTemplateTypeParm: {
    const auto *Subst1 = cast<SubstTemplateTypeParmType>(T1);
    const auto *Subst2 = cast<SubstTemplateTypeParmType>(T2);
    if (!IsStructurallyEquivalent(Context,
                                  QualType(Subst1->getReplacedParameter(), 0),
                                  QualType(Subst2->getReplacedParameter(), 0)))
      return false;
    if (!IsStructurallyEquivalent(Context, Subst1->getReplacementType(),
                                  Subst2->getReplacementType()))
      return false;
    break;
  }

  case Type::SubstTemplateTypeParmPack: {
    const auto *Subst1 = cast<SubstTemplateTypeParmPackType>(T1);
    const auto *Subst2 = cast<SubstTemplateTypeParmPackType>(T2);
    if (!IsStructurallyEquivalent(Context,
                                  QualType(Subst1->getReplacedParameter(), 0),
                                  QualType(Subst2->getReplacedParameter(), 0)))
      return false;
    if (!IsStructurallyEquivalent(Context, Subst1->getArgumentPack(),
                                  Subst2->getArgumentPack()))
      return false;
    break;
  }

  case Type::TemplateSpecialization: {
    const auto *Spec1 = cast<TemplateSpecializationType>(T1);
    const auto *Spec2 = cast<TemplateSpecializationType>(T2);
    if (!IsStructurallyEquivalent(Context, Spec1->getTemplateName(),
                                  Spec2->getTemplateName()))
      return false;
    if (Spec1->getNumArgs() != Spec2->getNumArgs())
      return false;
    for (unsigned I = 0, N = Spec1->getNumArgs(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Spec1->getArg(I),
                                    Spec2->getArg(I)))
        return false;
    }
    break;
  }

  case Type::Elaborated: {
    const auto *Elab1 = cast<ElaboratedType>(T1);
    const auto *Elab2 = cast<ElaboratedType>(T2);
    // CHECKME: what if a keyword is ETK_None or ETK_typename ?
    if (Elab1->getKeyword() != Elab2->getKeyword())
      return false;
    if (!IsStructurallyEquivalent(Context, Elab1->getQualifier(),
                                  Elab2->getQualifier()))
      return false;
    if (!IsStructurallyEquivalent(Context, Elab1->getNamedType(),
                                  Elab2->getNamedType()))
      return false;
    break;
  }

  case Type::InjectedClassName: {
    const auto *Inj1 = cast<InjectedClassNameType>(T1);
    const auto *Inj2 = cast<InjectedClassNameType>(T2);
    if (!IsStructurallyEquivalent(Context,
                                  Inj1->getInjectedSpecializationType(),
                                  Inj2->getInjectedSpecializationType()))
      return false;
    break;
  }

  case Type::DependentName: {
    const auto *Typename1 = cast<DependentNameType>(T1);
    const auto *Typename2 = cast<DependentNameType>(T2);
    if (!IsStructurallyEquivalent(Context, Typename1->getQualifier(),
                                  Typename2->getQualifier()))
      return false;
    if (!IsStructurallyEquivalent(Typename1->getIdentifier(),
                                  Typename2->getIdentifier()))
      return false;

    break;
  }

  case Type::DependentTemplateSpecialization: {
    const auto *Spec1 = cast<DependentTemplateSpecializationType>(T1);
    const auto *Spec2 = cast<DependentTemplateSpecializationType>(T2);
    if (!IsStructurallyEquivalent(Context, Spec1->getQualifier(),
                                  Spec2->getQualifier()))
      return false;
    if (!IsStructurallyEquivalent(Spec1->getIdentifier(),
                                  Spec2->getIdentifier()))
      return false;
    if (Spec1->getNumArgs() != Spec2->getNumArgs())
      return false;
    for (unsigned I = 0, N = Spec1->getNumArgs(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Spec1->getArg(I),
                                    Spec2->getArg(I)))
        return false;
    }
    break;
  }

  case Type::PackExpansion:
    if (!IsStructurallyEquivalent(Context,
                                  cast<PackExpansionType>(T1)->getPattern(),
                                  cast<PackExpansionType>(T2)->getPattern()))
      return false;
    break;

  case Type::ObjCInterface: {
    const auto *Iface1 = cast<ObjCInterfaceType>(T1);
    const auto *Iface2 = cast<ObjCInterfaceType>(T2);
    if (!IsStructurallyEquivalent(Context, Iface1->getDecl(),
                                  Iface2->getDecl()))
      return false;
    break;
  }

  case Type::ObjCTypeParam: {
    const auto *Obj1 = cast<ObjCTypeParamType>(T1);
    const auto *Obj2 = cast<ObjCTypeParamType>(T2);
    if (!IsStructurallyEquivalent(Context, Obj1->getDecl(), Obj2->getDecl()))
      return false;

    if (Obj1->getNumProtocols() != Obj2->getNumProtocols())
      return false;
    for (unsigned I = 0, N = Obj1->getNumProtocols(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Obj1->getProtocol(I),
                                    Obj2->getProtocol(I)))
        return false;
    }
    break;
  }

  case Type::ObjCObject: {
    const auto *Obj1 = cast<ObjCObjectType>(T1);
    const auto *Obj2 = cast<ObjCObjectType>(T2);
    if (!IsStructurallyEquivalent(Context, Obj1->getBaseType(),
                                  Obj2->getBaseType()))
      return false;
    if (Obj1->getNumProtocols() != Obj2->getNumProtocols())
      return false;
    for (unsigned I = 0, N = Obj1->getNumProtocols(); I != N; ++I) {
      if (!IsStructurallyEquivalent(Context, Obj1->getProtocol(I),
                                    Obj2->getProtocol(I)))
        return false;
    }
    break;
  }

  case Type::ObjCObjectPointer: {
    const auto *Ptr1 = cast<ObjCObjectPointerType>(T1);
    const auto *Ptr2 = cast<ObjCObjectPointerType>(T2);
    if (!IsStructurallyEquivalent(Context, Ptr1->getPointeeType(),
                                  Ptr2->getPointeeType()))
      return false;
    break;
  }

  case Type::Atomic:
    if (!IsStructurallyEquivalent(Context, cast<AtomicType>(T1)->getValueType(),
                                  cast<AtomicType>(T2)->getValueType()))
      return false;
    break;

  case Type::Pipe:
    if (!IsStructurallyEquivalent(Context, cast<PipeType>(T1)->getElementType(),
                                  cast<PipeType>(T2)->getElementType()))
      return false;
    break;
  } // end switch

  return true;
}

/// Determine structural equivalence of two fields.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FieldDecl *Field1, FieldDecl *Field2) {
  const auto *Owner2 = cast<RecordDecl>(Field2->getDeclContext());

  // For anonymous structs/unions, match up the anonymous struct/union type
  // declarations directly, so that we don't go off searching for anonymous
  // types
  if (Field1->isAnonymousStructOrUnion() &&
      Field2->isAnonymousStructOrUnion()) {
    RecordDecl *D1 = Field1->getType()->castAs<RecordType>()->getDecl();
    RecordDecl *D2 = Field2->getType()->castAs<RecordType>()->getDecl();
    return IsStructurallyEquivalent(Context, D1, D2);
  }

  // Check for equivalent field names.
  IdentifierInfo *Name1 = Field1->getIdentifier();
  IdentifierInfo *Name2 = Field2->getIdentifier();
  if (!::IsStructurallyEquivalent(Name1, Name2)) {
    if (Context.Complain) {
      Context.Diag2(Owner2->getLocation(),
                    Context.ErrorOnTagTypeMismatch
                        ? diag::err_odr_tag_type_inconsistent
                        : diag::warn_odr_tag_type_inconsistent)
          << Context.ToCtx.getTypeDeclType(Owner2);
      Context.Diag2(Field2->getLocation(), diag::note_odr_field_name)
          << Field2->getDeclName();
      Context.Diag1(Field1->getLocation(), diag::note_odr_field_name)
          << Field1->getDeclName();
    }
    return false;
  }

  if (!IsStructurallyEquivalent(Context, Field1->getType(),
                                Field2->getType())) {
    if (Context.Complain) {
      Context.Diag2(Owner2->getLocation(),
                    Context.ErrorOnTagTypeMismatch
                        ? diag::err_odr_tag_type_inconsistent
                        : diag::warn_odr_tag_type_inconsistent)
          << Context.ToCtx.getTypeDeclType(Owner2);
      Context.Diag2(Field2->getLocation(), diag::note_odr_field)
          << Field2->getDeclName() << Field2->getType();
      Context.Diag1(Field1->getLocation(), diag::note_odr_field)
          << Field1->getDeclName() << Field1->getType();
    }
    return false;
  }

  if (Field1->isBitField() != Field2->isBitField()) {
    if (Context.Complain) {
      Context.Diag2(Owner2->getLocation(),
                    Context.ErrorOnTagTypeMismatch
                        ? diag::err_odr_tag_type_inconsistent
                        : diag::warn_odr_tag_type_inconsistent)
          << Context.ToCtx.getTypeDeclType(Owner2);
      if (Field1->isBitField()) {
        Context.Diag1(Field1->getLocation(), diag::note_odr_bit_field)
            << Field1->getDeclName() << Field1->getType()
            << Field1->getBitWidthValue(Context.FromCtx);
        Context.Diag2(Field2->getLocation(), diag::note_odr_not_bit_field)
            << Field2->getDeclName();
      } else {
        Context.Diag2(Field2->getLocation(), diag::note_odr_bit_field)
            << Field2->getDeclName() << Field2->getType()
            << Field2->getBitWidthValue(Context.ToCtx);
        Context.Diag1(Field1->getLocation(), diag::note_odr_not_bit_field)
            << Field1->getDeclName();
      }
    }
    return false;
  }

  if (Field1->isBitField()) {
    // Make sure that the bit-fields are the same length.
    unsigned Bits1 = Field1->getBitWidthValue(Context.FromCtx);
    unsigned Bits2 = Field2->getBitWidthValue(Context.ToCtx);

    if (Bits1 != Bits2) {
      if (Context.Complain) {
        Context.Diag2(Owner2->getLocation(),
                      Context.ErrorOnTagTypeMismatch
                          ? diag::err_odr_tag_type_inconsistent
                          : diag::warn_odr_tag_type_inconsistent)
            << Context.ToCtx.getTypeDeclType(Owner2);
        Context.Diag2(Field2->getLocation(), diag::note_odr_bit_field)
            << Field2->getDeclName() << Field2->getType() << Bits2;
        Context.Diag1(Field1->getLocation(), diag::note_odr_bit_field)
            << Field1->getDeclName() << Field1->getType() << Bits1;
      }
      return false;
    }
  }

  return true;
}

/// Determine structural equivalence of two methods.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     CXXMethodDecl *Method1,
                                     CXXMethodDecl *Method2) {
  bool PropertiesEqual =
      Method1->getDeclKind() == Method2->getDeclKind() &&
      Method1->getRefQualifier() == Method2->getRefQualifier() &&
      Method1->getAccess() == Method2->getAccess() &&
      Method1->getOverloadedOperator() == Method2->getOverloadedOperator() &&
      Method1->isStatic() == Method2->isStatic() &&
      Method1->isConst() == Method2->isConst() &&
      Method1->isVolatile() == Method2->isVolatile() &&
      Method1->isVirtual() == Method2->isVirtual() &&
      Method1->isPure() == Method2->isPure() &&
      Method1->isDefaulted() == Method2->isDefaulted() &&
      Method1->isDeleted() == Method2->isDeleted();
  if (!PropertiesEqual)
    return false;
  // FIXME: Check for 'final'.

  if (auto *Constructor1 = dyn_cast<CXXConstructorDecl>(Method1)) {
    auto *Constructor2 = cast<CXXConstructorDecl>(Method2);
    if (Constructor1->isExplicit() != Constructor2->isExplicit())
      return false;
  }

  if (auto *Conversion1 = dyn_cast<CXXConversionDecl>(Method1)) {
    auto *Conversion2 = cast<CXXConversionDecl>(Method2);
    if (Conversion1->isExplicit() != Conversion2->isExplicit())
      return false;
    if (!IsStructurallyEquivalent(Context, Conversion1->getConversionType(),
                                  Conversion2->getConversionType()))
      return false;
  }

  const IdentifierInfo *Name1 = Method1->getIdentifier();
  const IdentifierInfo *Name2 = Method2->getIdentifier();
  if (!::IsStructurallyEquivalent(Name1, Name2)) {
    return false;
    // TODO: Names do not match, add warning like at check for FieldDecl.
  }

  // Check the prototypes.
  if (!::IsStructurallyEquivalent(Context,
                                  Method1->getType(), Method2->getType()))
    return false;

  return true;
}

/// Determine structural equivalence of two records.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     RecordDecl *D1, RecordDecl *D2) {
  if (D1->isUnion() != D2->isUnion()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.ErrorOnTagTypeMismatch
                        ? diag::err_odr_tag_type_inconsistent
                        : diag::warn_odr_tag_type_inconsistent)
          << Context.ToCtx.getTypeDeclType(D2);
      Context.Diag1(D1->getLocation(), diag::note_odr_tag_kind_here)
          << D1->getDeclName() << (unsigned)D1->getTagKind();
    }
    return false;
  }

  if (!D1->getDeclName() && !D2->getDeclName()) {
    // If both anonymous structs/unions are in a record context, make sure
    // they occur in the same location in the context records.
    if (Optional<unsigned> Index1 =
            StructuralEquivalenceContext::findUntaggedStructOrUnionIndex(D1)) {
      if (Optional<unsigned> Index2 =
              StructuralEquivalenceContext::findUntaggedStructOrUnionIndex(
                  D2)) {
        if (*Index1 != *Index2)
          return false;
      }
    }
  }

  // If both declarations are class template specializations, we know
  // the ODR applies, so check the template and template arguments.
  const auto *Spec1 = dyn_cast<ClassTemplateSpecializationDecl>(D1);
  const auto *Spec2 = dyn_cast<ClassTemplateSpecializationDecl>(D2);
  if (Spec1 && Spec2) {
    // Check that the specialized templates are the same.
    if (!IsStructurallyEquivalent(Context, Spec1->getSpecializedTemplate(),
                                  Spec2->getSpecializedTemplate()))
      return false;

    // Check that the template arguments are the same.
    if (Spec1->getTemplateArgs().size() != Spec2->getTemplateArgs().size())
      return false;

    for (unsigned I = 0, N = Spec1->getTemplateArgs().size(); I != N; ++I)
      if (!IsStructurallyEquivalent(Context, Spec1->getTemplateArgs().get(I),
                                    Spec2->getTemplateArgs().get(I)))
        return false;
  }
  // If one is a class template specialization and the other is not, these
  // structures are different.
  else if (Spec1 || Spec2)
    return false;

  // Compare the definitions of these two records. If either or both are
  // incomplete (i.e. it is a forward decl), we assume that they are
  // equivalent.
  D1 = D1->getDefinition();
  D2 = D2->getDefinition();
  if (!D1 || !D2)
    return true;

  // If any of the records has external storage and we do a minimal check (or
  // AST import) we assume they are equivalent. (If we didn't have this
  // assumption then `RecordDecl::LoadFieldsFromExternalStorage` could trigger
  // another AST import which in turn would call the structural equivalency
  // check again and finally we'd have an improper result.)
  if (Context.EqKind == StructuralEquivalenceKind::Minimal)
    if (D1->hasExternalLexicalStorage() || D2->hasExternalLexicalStorage())
      return true;

  // If one definition is currently being defined, we do not compare for
  // equality and we assume that the decls are equal.
  if (D1->isBeingDefined() || D2->isBeingDefined())
    return true;

  if (auto *D1CXX = dyn_cast<CXXRecordDecl>(D1)) {
    if (auto *D2CXX = dyn_cast<CXXRecordDecl>(D2)) {
      if (D1CXX->hasExternalLexicalStorage() &&
          !D1CXX->isCompleteDefinition()) {
        D1CXX->getASTContext().getExternalSource()->CompleteType(D1CXX);
      }

      if (D1CXX->getNumBases() != D2CXX->getNumBases()) {
        if (Context.Complain) {
          Context.Diag2(D2->getLocation(), diag::warn_odr_tag_type_inconsistent)
              << Context.ToCtx.getTypeDeclType(D2);
          Context.Diag2(D2->getLocation(), diag::note_odr_number_of_bases)
              << D2CXX->getNumBases();
          Context.Diag1(D1->getLocation(), diag::note_odr_number_of_bases)
              << D1CXX->getNumBases();
        }
        return false;
      }

      // Check the base classes.
      for (CXXRecordDecl::base_class_iterator Base1 = D1CXX->bases_begin(),
                                              BaseEnd1 = D1CXX->bases_end(),
                                              Base2 = D2CXX->bases_begin();
           Base1 != BaseEnd1; ++Base1, ++Base2) {
        if (!IsStructurallyEquivalent(Context, Base1->getType(),
                                      Base2->getType())) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          diag::warn_odr_tag_type_inconsistent)
                << Context.ToCtx.getTypeDeclType(D2);
            Context.Diag2(Base2->getBeginLoc(), diag::note_odr_base)
                << Base2->getType() << Base2->getSourceRange();
            Context.Diag1(Base1->getBeginLoc(), diag::note_odr_base)
                << Base1->getType() << Base1->getSourceRange();
          }
          return false;
        }

        // Check virtual vs. non-virtual inheritance mismatch.
        if (Base1->isVirtual() != Base2->isVirtual()) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          diag::warn_odr_tag_type_inconsistent)
                << Context.ToCtx.getTypeDeclType(D2);
            Context.Diag2(Base2->getBeginLoc(), diag::note_odr_virtual_base)
                << Base2->isVirtual() << Base2->getSourceRange();
            Context.Diag1(Base1->getBeginLoc(), diag::note_odr_base)
                << Base1->isVirtual() << Base1->getSourceRange();
          }
          return false;
        }
      }

      // Check the friends for consistency.
      CXXRecordDecl::friend_iterator Friend2 = D2CXX->friend_begin(),
              Friend2End = D2CXX->friend_end();
      for (CXXRecordDecl::friend_iterator Friend1 = D1CXX->friend_begin(),
                   Friend1End = D1CXX->friend_end();
           Friend1 != Friend1End; ++Friend1, ++Friend2) {
        if (Friend2 == Friend2End) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(),
                          diag::warn_odr_tag_type_inconsistent)
                    << Context.ToCtx.getTypeDeclType(D2CXX);
            Context.Diag1((*Friend1)->getFriendLoc(), diag::note_odr_friend);
            Context.Diag2(D2->getLocation(), diag::note_odr_missing_friend);
          }
          return false;
        }

        if (!IsStructurallyEquivalent(Context, *Friend1, *Friend2)) {
          if (Context.Complain) {
            Context.Diag2(D2->getLocation(), diag::warn_odr_tag_type_inconsistent)
              << Context.ToCtx.getTypeDeclType(D2CXX);
            Context.Diag1((*Friend1)->getFriendLoc(), diag::note_odr_friend);
            Context.Diag2((*Friend2)->getFriendLoc(), diag::note_odr_friend);
          }
          return false;
        }
      }

      if (Friend2 != Friend2End) {
        if (Context.Complain) {
          Context.Diag2(D2->getLocation(), diag::warn_odr_tag_type_inconsistent)
                  << Context.ToCtx.getTypeDeclType(D2);
          Context.Diag2((*Friend2)->getFriendLoc(), diag::note_odr_friend);
          Context.Diag1(D1->getLocation(), diag::note_odr_missing_friend);
        }
        return false;
      }
    } else if (D1CXX->getNumBases() > 0) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(), diag::warn_odr_tag_type_inconsistent)
            << Context.ToCtx.getTypeDeclType(D2);
        const CXXBaseSpecifier *Base1 = D1CXX->bases_begin();
        Context.Diag1(Base1->getBeginLoc(), diag::note_odr_base)
            << Base1->getType() << Base1->getSourceRange();
        Context.Diag2(D2->getLocation(), diag::note_odr_missing_base);
      }
      return false;
    }
  }

  // Check the fields for consistency.
  RecordDecl::field_iterator Field2 = D2->field_begin(),
                             Field2End = D2->field_end();
  for (RecordDecl::field_iterator Field1 = D1->field_begin(),
                                  Field1End = D1->field_end();
       Field1 != Field1End; ++Field1, ++Field2) {
    if (Field2 == Field2End) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.ErrorOnTagTypeMismatch
                          ? diag::err_odr_tag_type_inconsistent
                          : diag::warn_odr_tag_type_inconsistent)
            << Context.ToCtx.getTypeDeclType(D2);
        Context.Diag1(Field1->getLocation(), diag::note_odr_field)
            << Field1->getDeclName() << Field1->getType();
        Context.Diag2(D2->getLocation(), diag::note_odr_missing_field);
      }
      return false;
    }

    if (!IsStructurallyEquivalent(Context, *Field1, *Field2))
      return false;
  }

  if (Field2 != Field2End) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.ErrorOnTagTypeMismatch
                        ? diag::err_odr_tag_type_inconsistent
                        : diag::warn_odr_tag_type_inconsistent)
          << Context.ToCtx.getTypeDeclType(D2);
      Context.Diag2(Field2->getLocation(), diag::note_odr_field)
          << Field2->getDeclName() << Field2->getType();
      Context.Diag1(D1->getLocation(), diag::note_odr_missing_field);
    }
    return false;
  }

  return true;
}

/// Determine structural equivalence of two enums.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     EnumDecl *D1, EnumDecl *D2) {

  // Compare the definitions of these two enums. If either or both are
  // incomplete (i.e. forward declared), we assume that they are equivalent.
  D1 = D1->getDefinition();
  D2 = D2->getDefinition();
  if (!D1 || !D2)
    return true;

  EnumDecl::enumerator_iterator EC2 = D2->enumerator_begin(),
                                EC2End = D2->enumerator_end();
  for (EnumDecl::enumerator_iterator EC1 = D1->enumerator_begin(),
                                     EC1End = D1->enumerator_end();
       EC1 != EC1End; ++EC1, ++EC2) {
    if (EC2 == EC2End) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.ErrorOnTagTypeMismatch
                          ? diag::err_odr_tag_type_inconsistent
                          : diag::warn_odr_tag_type_inconsistent)
            << Context.ToCtx.getTypeDeclType(D2);
        Context.Diag1(EC1->getLocation(), diag::note_odr_enumerator)
            << EC1->getDeclName() << EC1->getInitVal().toString(10);
        Context.Diag2(D2->getLocation(), diag::note_odr_missing_enumerator);
      }
      return false;
    }

    llvm::APSInt Val1 = EC1->getInitVal();
    llvm::APSInt Val2 = EC2->getInitVal();
    if (!llvm::APSInt::isSameValue(Val1, Val2) ||
        !IsStructurallyEquivalent(EC1->getIdentifier(), EC2->getIdentifier())) {
      if (Context.Complain) {
        Context.Diag2(D2->getLocation(),
                      Context.ErrorOnTagTypeMismatch
                          ? diag::err_odr_tag_type_inconsistent
                          : diag::warn_odr_tag_type_inconsistent)
            << Context.ToCtx.getTypeDeclType(D2);
        Context.Diag2(EC2->getLocation(), diag::note_odr_enumerator)
            << EC2->getDeclName() << EC2->getInitVal().toString(10);
        Context.Diag1(EC1->getLocation(), diag::note_odr_enumerator)
            << EC1->getDeclName() << EC1->getInitVal().toString(10);
      }
      return false;
    }
  }

  if (EC2 != EC2End) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    Context.ErrorOnTagTypeMismatch
                        ? diag::err_odr_tag_type_inconsistent
                        : diag::warn_odr_tag_type_inconsistent)
          << Context.ToCtx.getTypeDeclType(D2);
      Context.Diag2(EC2->getLocation(), diag::note_odr_enumerator)
          << EC2->getDeclName() << EC2->getInitVal().toString(10);
      Context.Diag1(D1->getLocation(), diag::note_odr_missing_enumerator);
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TemplateParameterList *Params1,
                                     TemplateParameterList *Params2) {
  if (Params1->size() != Params2->size()) {
    if (Context.Complain) {
      Context.Diag2(Params2->getTemplateLoc(),
                    diag::err_odr_different_num_template_parameters)
          << Params1->size() << Params2->size();
      Context.Diag1(Params1->getTemplateLoc(),
                    diag::note_odr_template_parameter_list);
    }
    return false;
  }

  for (unsigned I = 0, N = Params1->size(); I != N; ++I) {
    if (Params1->getParam(I)->getKind() != Params2->getParam(I)->getKind()) {
      if (Context.Complain) {
        Context.Diag2(Params2->getParam(I)->getLocation(),
                      diag::err_odr_different_template_parameter_kind);
        Context.Diag1(Params1->getParam(I)->getLocation(),
                      diag::note_odr_template_parameter_here);
      }
      return false;
    }

    if (!IsStructurallyEquivalent(Context, Params1->getParam(I),
                                  Params2->getParam(I)))
      return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TemplateTypeParmDecl *D1,
                                     TemplateTypeParmDecl *D2) {
  if (D1->isParameterPack() != D2->isParameterPack()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(), diag::err_odr_parameter_pack_non_pack)
          << D2->isParameterPack();
      Context.Diag1(D1->getLocation(), diag::note_odr_parameter_pack_non_pack)
          << D1->isParameterPack();
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     NonTypeTemplateParmDecl *D1,
                                     NonTypeTemplateParmDecl *D2) {
  if (D1->isParameterPack() != D2->isParameterPack()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(), diag::err_odr_parameter_pack_non_pack)
          << D2->isParameterPack();
      Context.Diag1(D1->getLocation(), diag::note_odr_parameter_pack_non_pack)
          << D1->isParameterPack();
    }
    return false;
  }

  // Check types.
  if (!IsStructurallyEquivalent(Context, D1->getType(), D2->getType())) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(),
                    diag::err_odr_non_type_parameter_type_inconsistent)
          << D2->getType() << D1->getType();
      Context.Diag1(D1->getLocation(), diag::note_odr_value_here)
          << D1->getType();
    }
    return false;
  }

  return true;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     TemplateTemplateParmDecl *D1,
                                     TemplateTemplateParmDecl *D2) {
  if (D1->isParameterPack() != D2->isParameterPack()) {
    if (Context.Complain) {
      Context.Diag2(D2->getLocation(), diag::err_odr_parameter_pack_non_pack)
          << D2->isParameterPack();
      Context.Diag1(D1->getLocation(), diag::note_odr_parameter_pack_non_pack)
          << D1->isParameterPack();
    }
    return false;
  }

  // Check template parameter lists.
  return IsStructurallyEquivalent(Context, D1->getTemplateParameters(),
                                  D2->getTemplateParameters());
}

static bool IsTemplateDeclCommonStructurallyEquivalent(
    StructuralEquivalenceContext &Ctx, TemplateDecl *D1, TemplateDecl *D2) {
  if (!IsStructurallyEquivalent(D1->getIdentifier(), D2->getIdentifier()))
    return false;
  if (!D1->getIdentifier()) // Special name
    if (D1->getNameAsString() != D2->getNameAsString())
      return false;
  return IsStructurallyEquivalent(Ctx, D1->getTemplateParameters(),
                                  D2->getTemplateParameters());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     ClassTemplateDecl *D1,
                                     ClassTemplateDecl *D2) {
  // Check template parameters.
  if (!IsTemplateDeclCommonStructurallyEquivalent(Context, D1, D2))
    return false;

  // Check the templated declaration.
  return IsStructurallyEquivalent(Context, D1->getTemplatedDecl(),
                                  D2->getTemplatedDecl());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FunctionTemplateDecl *D1,
                                     FunctionTemplateDecl *D2) {
  // Check template parameters.
  if (!IsTemplateDeclCommonStructurallyEquivalent(Context, D1, D2))
    return false;

  // Check the templated declaration.
  return IsStructurallyEquivalent(Context, D1->getTemplatedDecl()->getType(),
                                  D2->getTemplatedDecl()->getType());
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FriendDecl *D1, FriendDecl *D2) {
  if ((D1->getFriendType() && D2->getFriendDecl()) ||
      (D1->getFriendDecl() && D2->getFriendType())) {
      return false;
  }
  if (D1->getFriendType() && D2->getFriendType())
    return IsStructurallyEquivalent(Context,
                                    D1->getFriendType()->getType(),
                                    D2->getFriendType()->getType());
  if (D1->getFriendDecl() && D2->getFriendDecl())
    return IsStructurallyEquivalent(Context, D1->getFriendDecl(),
                                    D2->getFriendDecl());
  return false;
}

static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     FunctionDecl *D1, FunctionDecl *D2) {
  // FIXME: Consider checking for function attributes as well.
  if (!IsStructurallyEquivalent(Context, D1->getType(), D2->getType()))
    return false;

  return true;
}

/// Determine structural equivalence of two declarations.
static bool IsStructurallyEquivalent(StructuralEquivalenceContext &Context,
                                     Decl *D1, Decl *D2) {
  // FIXME: Check for known structural equivalences via a callback of some sort.

  // Check whether we already know that these two declarations are not
  // structurally equivalent.
  if (Context.NonEquivalentDecls.count(
          std::make_pair(D1->getCanonicalDecl(), D2->getCanonicalDecl())))
    return false;

  // Determine whether we've already produced a tentative equivalence for D1.
  Decl *&EquivToD1 = Context.TentativeEquivalences[D1->getCanonicalDecl()];
  if (EquivToD1)
    return EquivToD1 == D2->getCanonicalDecl();

  // Produce a tentative equivalence D1 <-> D2, which will be checked later.
  EquivToD1 = D2->getCanonicalDecl();
  Context.DeclsToCheck.push_back(D1->getCanonicalDecl());
  return true;
}

DiagnosticBuilder StructuralEquivalenceContext::Diag1(SourceLocation Loc,
                                                      unsigned DiagID) {
  assert(Complain && "Not allowed to complain");
  if (LastDiagFromC2)
    FromCtx.getDiagnostics().notePriorDiagnosticFrom(ToCtx.getDiagnostics());
  LastDiagFromC2 = false;
  return FromCtx.getDiagnostics().Report(Loc, DiagID);
}

DiagnosticBuilder StructuralEquivalenceContext::Diag2(SourceLocation Loc,
                                                      unsigned DiagID) {
  assert(Complain && "Not allowed to complain");
  if (!LastDiagFromC2)
    ToCtx.getDiagnostics().notePriorDiagnosticFrom(FromCtx.getDiagnostics());
  LastDiagFromC2 = true;
  return ToCtx.getDiagnostics().Report(Loc, DiagID);
}

Optional<unsigned>
StructuralEquivalenceContext::findUntaggedStructOrUnionIndex(RecordDecl *Anon) {
  ASTContext &Context = Anon->getASTContext();
  QualType AnonTy = Context.getRecordType(Anon);

  const auto *Owner = dyn_cast<RecordDecl>(Anon->getDeclContext());
  if (!Owner)
    return None;

  unsigned Index = 0;
  for (const auto *D : Owner->noload_decls()) {
    const auto *F = dyn_cast<FieldDecl>(D);
    if (!F)
      continue;

    if (F->isAnonymousStructOrUnion()) {
      if (Context.hasSameType(F->getType(), AnonTy))
        break;
      ++Index;
      continue;
    }

    // If the field looks like this:
    // struct { ... } A;
    QualType FieldType = F->getType();
    // In case of nested structs.
    while (const auto *ElabType = dyn_cast<ElaboratedType>(FieldType))
      FieldType = ElabType->getNamedType();

    if (const auto *RecType = dyn_cast<RecordType>(FieldType)) {
      const RecordDecl *RecDecl = RecType->getDecl();
      if (RecDecl->getDeclContext() == Owner && !RecDecl->getIdentifier()) {
        if (Context.hasSameType(FieldType, AnonTy))
          break;
        ++Index;
        continue;
      }
    }
  }

  return Index;
}

bool StructuralEquivalenceContext::IsEquivalent(Decl *D1, Decl *D2) {

  // Ensure that the implementation functions (all static functions in this TU)
  // never call the public ASTStructuralEquivalence::IsEquivalent() functions,
  // because that will wreak havoc the internal state (DeclsToCheck and
  // TentativeEquivalences members) and can cause faulty behaviour. For
  // instance, some leaf declarations can be stated and cached as inequivalent
  // as a side effect of one inequivalent element in the DeclsToCheck list.
  assert(DeclsToCheck.empty());
  assert(TentativeEquivalences.empty());

  if (!::IsStructurallyEquivalent(*this, D1, D2))
    return false;

  return !Finish();
}

bool StructuralEquivalenceContext::IsEquivalent(QualType T1, QualType T2) {
  assert(DeclsToCheck.empty());
  assert(TentativeEquivalences.empty());
  if (!::IsStructurallyEquivalent(*this, T1, T2))
    return false;

  return !Finish();
}

bool StructuralEquivalenceContext::CheckCommonEquivalence(Decl *D1, Decl *D2) {
  // Check for equivalent described template.
  TemplateDecl *Template1 = D1->getDescribedTemplate();
  TemplateDecl *Template2 = D2->getDescribedTemplate();
  if ((Template1 != nullptr) != (Template2 != nullptr))
    return false;
  if (Template1 && !IsStructurallyEquivalent(*this, Template1, Template2))
    return false;

  // FIXME: Move check for identifier names into this function.

  return true;
}

bool StructuralEquivalenceContext::CheckKindSpecificEquivalence(
    Decl *D1, Decl *D2) {
  // FIXME: Switch on all declaration kinds. For now, we're just going to
  // check the obvious ones.
  if (auto *Record1 = dyn_cast<RecordDecl>(D1)) {
    if (auto *Record2 = dyn_cast<RecordDecl>(D2)) {
      // Check for equivalent structure names.
      IdentifierInfo *Name1 = Record1->getIdentifier();
      if (!Name1 && Record1->getTypedefNameForAnonDecl())
        Name1 = Record1->getTypedefNameForAnonDecl()->getIdentifier();
      IdentifierInfo *Name2 = Record2->getIdentifier();
      if (!Name2 && Record2->getTypedefNameForAnonDecl())
        Name2 = Record2->getTypedefNameForAnonDecl()->getIdentifier();
      if (!::IsStructurallyEquivalent(Name1, Name2) ||
          !::IsStructurallyEquivalent(*this, Record1, Record2))
        return false;
    } else {
      // Record/non-record mismatch.
      return false;
    }
  } else if (auto *Enum1 = dyn_cast<EnumDecl>(D1)) {
    if (auto *Enum2 = dyn_cast<EnumDecl>(D2)) {
      // Check for equivalent enum names.
      IdentifierInfo *Name1 = Enum1->getIdentifier();
      if (!Name1 && Enum1->getTypedefNameForAnonDecl())
        Name1 = Enum1->getTypedefNameForAnonDecl()->getIdentifier();
      IdentifierInfo *Name2 = Enum2->getIdentifier();
      if (!Name2 && Enum2->getTypedefNameForAnonDecl())
        Name2 = Enum2->getTypedefNameForAnonDecl()->getIdentifier();
      if (!::IsStructurallyEquivalent(Name1, Name2) ||
          !::IsStructurallyEquivalent(*this, Enum1, Enum2))
        return false;
    } else {
      // Enum/non-enum mismatch
      return false;
    }
  } else if (const auto *Typedef1 = dyn_cast<TypedefNameDecl>(D1)) {
    if (const auto *Typedef2 = dyn_cast<TypedefNameDecl>(D2)) {
      if (!::IsStructurallyEquivalent(Typedef1->getIdentifier(),
                                      Typedef2->getIdentifier()) ||
          !::IsStructurallyEquivalent(*this, Typedef1->getUnderlyingType(),
                                      Typedef2->getUnderlyingType()))
        return false;
    } else {
      // Typedef/non-typedef mismatch.
      return false;
    }
  } else if (auto *ClassTemplate1 = dyn_cast<ClassTemplateDecl>(D1)) {
    if (auto *ClassTemplate2 = dyn_cast<ClassTemplateDecl>(D2)) {
      if (!::IsStructurallyEquivalent(*this, ClassTemplate1,
                                      ClassTemplate2))
        return false;
    } else {
      // Class template/non-class-template mismatch.
      return false;
    }
  } else if (auto *FunctionTemplate1 = dyn_cast<FunctionTemplateDecl>(D1)) {
    if (auto *FunctionTemplate2 = dyn_cast<FunctionTemplateDecl>(D2)) {
      if (!::IsStructurallyEquivalent(*this, FunctionTemplate1,
                                      FunctionTemplate2))
        return false;
    } else {
      // Class template/non-class-template mismatch.
      return false;
    }
  } else if (auto *TTP1 = dyn_cast<TemplateTypeParmDecl>(D1)) {
    if (auto *TTP2 = dyn_cast<TemplateTypeParmDecl>(D2)) {
      if (!::IsStructurallyEquivalent(*this, TTP1, TTP2))
        return false;
    } else {
      // Kind mismatch.
      return false;
    }
  } else if (auto *NTTP1 = dyn_cast<NonTypeTemplateParmDecl>(D1)) {
    if (auto *NTTP2 = dyn_cast<NonTypeTemplateParmDecl>(D2)) {
      if (!::IsStructurallyEquivalent(*this, NTTP1, NTTP2))
        return false;
    } else {
      // Kind mismatch.
      return false;
    }
  } else if (auto *TTP1 = dyn_cast<TemplateTemplateParmDecl>(D1)) {
    if (auto *TTP2 = dyn_cast<TemplateTemplateParmDecl>(D2)) {
      if (!::IsStructurallyEquivalent(*this, TTP1, TTP2))
        return false;
    } else {
      // Kind mismatch.
      return false;
    }
  } else if (auto *MD1 = dyn_cast<CXXMethodDecl>(D1)) {
    if (auto *MD2 = dyn_cast<CXXMethodDecl>(D2)) {
      if (!::IsStructurallyEquivalent(*this, MD1, MD2))
        return false;
    } else {
      // Kind mismatch.
      return false;
    }
  } else if (FunctionDecl *FD1 = dyn_cast<FunctionDecl>(D1)) {
    if (FunctionDecl *FD2 = dyn_cast<FunctionDecl>(D2)) {
      if (!::IsStructurallyEquivalent(FD1->getIdentifier(),
                                      FD2->getIdentifier()))
        return false;
      if (!::IsStructurallyEquivalent(*this, FD1, FD2))
        return false;
    } else {
      // Kind mismatch.
      return false;
    }
  } else if (FriendDecl *FrD1 = dyn_cast<FriendDecl>(D1)) {
    if (FriendDecl *FrD2 = dyn_cast<FriendDecl>(D2)) {
        if (!::IsStructurallyEquivalent(*this, FrD1, FrD2))
          return false;
    } else {
      // Kind mismatch.
      return false;
    }
  }

  return true;
}

bool StructuralEquivalenceContext::Finish() {
  while (!DeclsToCheck.empty()) {
    // Check the next declaration.
    Decl *D1 = DeclsToCheck.front();
    DeclsToCheck.pop_front();

    Decl *D2 = TentativeEquivalences[D1];
    assert(D2 && "Unrecorded tentative equivalence?");

    bool Equivalent =
        CheckCommonEquivalence(D1, D2) && CheckKindSpecificEquivalence(D1, D2);

    if (!Equivalent) {
      // Note that these two declarations are not equivalent (and we already
      // know about it).
      NonEquivalentDecls.insert(
          std::make_pair(D1->getCanonicalDecl(), D2->getCanonicalDecl()));
      return true;
    }
  }

  return false;
}
