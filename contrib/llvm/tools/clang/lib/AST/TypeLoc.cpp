//===- TypeLoc.cpp - Type Source Info Wrapper -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TypeLoc subclasses implementations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TypeLoc.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace clang;

static const unsigned TypeLocMaxDataAlign = alignof(void *);

//===----------------------------------------------------------------------===//
// TypeLoc Implementation
//===----------------------------------------------------------------------===//

namespace {

class TypeLocRanger : public TypeLocVisitor<TypeLocRanger, SourceRange> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  SourceRange Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { \
    return TyLoc.getLocalSourceRange(); \
  }
#include "clang/AST/TypeLocNodes.def"
};

} // namespace

SourceRange TypeLoc::getLocalSourceRangeImpl(TypeLoc TL) {
  if (TL.isNull()) return SourceRange();
  return TypeLocRanger().Visit(TL);
}

namespace {

class TypeAligner : public TypeLocVisitor<TypeAligner, unsigned> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  unsigned Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { \
    return TyLoc.getLocalDataAlignment(); \
  }
#include "clang/AST/TypeLocNodes.def"
};

} // namespace

/// Returns the alignment of the type source info data block.
unsigned TypeLoc::getLocalAlignmentForType(QualType Ty) {
  if (Ty.isNull()) return 1;
  return TypeAligner().Visit(TypeLoc(Ty, nullptr));
}

namespace {

class TypeSizer : public TypeLocVisitor<TypeSizer, unsigned> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  unsigned Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { \
    return TyLoc.getLocalDataSize(); \
  }
#include "clang/AST/TypeLocNodes.def"
};

} // namespace

/// Returns the size of the type source info data block.
unsigned TypeLoc::getFullDataSizeForType(QualType Ty) {
  unsigned Total = 0;
  TypeLoc TyLoc(Ty, nullptr);
  unsigned MaxAlign = 1;
  while (!TyLoc.isNull()) {
    unsigned Align = getLocalAlignmentForType(TyLoc.getType());
    MaxAlign = std::max(Align, MaxAlign);
    Total = llvm::alignTo(Total, Align);
    Total += TypeSizer().Visit(TyLoc);
    TyLoc = TyLoc.getNextTypeLoc();
  }
  Total = llvm::alignTo(Total, MaxAlign);
  return Total;
}

namespace {

class NextLoc : public TypeLocVisitor<NextLoc, TypeLoc> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  TypeLoc Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { \
    return TyLoc.getNextTypeLoc(); \
  }
#include "clang/AST/TypeLocNodes.def"
};

} // namespace

/// Get the next TypeLoc pointed by this TypeLoc, e.g for "int*" the
/// TypeLoc is a PointerLoc and next TypeLoc is for "int".
TypeLoc TypeLoc::getNextTypeLocImpl(TypeLoc TL) {
  return NextLoc().Visit(TL);
}

/// Initializes a type location, and all of its children
/// recursively, as if the entire tree had been written in the
/// given location.
void TypeLoc::initializeImpl(ASTContext &Context, TypeLoc TL,
                             SourceLocation Loc) {
  while (true) {
    switch (TL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)        \
    case CLASS: {                     \
      CLASS##TypeLoc TLCasted = TL.castAs<CLASS##TypeLoc>(); \
      TLCasted.initializeLocal(Context, Loc);  \
      TL = TLCasted.getNextTypeLoc(); \
      if (!TL) return;                \
      continue;                       \
    }
#include "clang/AST/TypeLocNodes.def"
    }
  }
}

namespace {

class TypeLocCopier : public TypeLocVisitor<TypeLocCopier> {
  TypeLoc Source;

public:
  TypeLocCopier(TypeLoc source) : Source(source) {}

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                          \
  void Visit##CLASS##TypeLoc(CLASS##TypeLoc dest) {   \
    dest.copyLocal(Source.castAs<CLASS##TypeLoc>());  \
  }
#include "clang/AST/TypeLocNodes.def"
};

} // namespace

void TypeLoc::copy(TypeLoc other) {
  assert(getFullDataSize() == other.getFullDataSize());

  // If both data pointers are aligned to the maximum alignment, we
  // can memcpy because getFullDataSize() accurately reflects the
  // layout of the data.
  if (reinterpret_cast<uintptr_t>(Data) ==
          llvm::alignTo(reinterpret_cast<uintptr_t>(Data),
                        TypeLocMaxDataAlign) &&
      reinterpret_cast<uintptr_t>(other.Data) ==
          llvm::alignTo(reinterpret_cast<uintptr_t>(other.Data),
                        TypeLocMaxDataAlign)) {
    memcpy(Data, other.Data, getFullDataSize());
    return;
  }

  // Copy each of the pieces.
  TypeLoc TL(getType(), Data);
  do {
    TypeLocCopier(other).Visit(TL);
    other = other.getNextTypeLoc();
  } while ((TL = TL.getNextTypeLoc()));
}

SourceLocation TypeLoc::getBeginLoc() const {
  TypeLoc Cur = *this;
  TypeLoc LeftMost = Cur;
  while (true) {
    switch (Cur.getTypeLocClass()) {
    case Elaborated:
      LeftMost = Cur;
      break;
    case FunctionProto:
      if (Cur.castAs<FunctionProtoTypeLoc>().getTypePtr()
              ->hasTrailingReturn()) {
        LeftMost = Cur;
        break;
      }
      LLVM_FALLTHROUGH;
    case FunctionNoProto:
    case ConstantArray:
    case DependentSizedArray:
    case IncompleteArray:
    case VariableArray:
      // FIXME: Currently QualifiedTypeLoc does not have a source range
    case Qualified:
      Cur = Cur.getNextTypeLoc();
      continue;
    default:
      if (Cur.getLocalSourceRange().getBegin().isValid())
        LeftMost = Cur;
      Cur = Cur.getNextTypeLoc();
      if (Cur.isNull())
        break;
      continue;
    } // switch
    break;
  } // while
  return LeftMost.getLocalSourceRange().getBegin();
}

SourceLocation TypeLoc::getEndLoc() const {
  TypeLoc Cur = *this;
  TypeLoc Last;
  while (true) {
    switch (Cur.getTypeLocClass()) {
    default:
      if (!Last)
        Last = Cur;
      return Last.getLocalSourceRange().getEnd();
    case Paren:
    case ConstantArray:
    case DependentSizedArray:
    case IncompleteArray:
    case VariableArray:
    case FunctionNoProto:
      Last = Cur;
      break;
    case FunctionProto:
      if (Cur.castAs<FunctionProtoTypeLoc>().getTypePtr()->hasTrailingReturn())
        Last = TypeLoc();
      else
        Last = Cur;
      break;
    case Pointer:
    case BlockPointer:
    case MemberPointer:
    case LValueReference:
    case RValueReference:
    case PackExpansion:
      if (!Last)
        Last = Cur;
      break;
    case Qualified:
    case Elaborated:
      break;
    }
    Cur = Cur.getNextTypeLoc();
  }
}

namespace {

struct TSTChecker : public TypeLocVisitor<TSTChecker, bool> {
  // Overload resolution does the real work for us.
  static bool isTypeSpec(TypeSpecTypeLoc _) { return true; }
  static bool isTypeSpec(TypeLoc _) { return false; }

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
  bool Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { \
    return isTypeSpec(TyLoc); \
  }
#include "clang/AST/TypeLocNodes.def"
};

} // namespace

/// Determines if the given type loc corresponds to a
/// TypeSpecTypeLoc.  Since there is not actually a TypeSpecType in
/// the type hierarchy, this is made somewhat complicated.
///
/// There are a lot of types that currently use TypeSpecTypeLoc
/// because it's a convenient base class.  Ideally we would not accept
/// those here, but ideally we would have better implementations for
/// them.
bool TypeSpecTypeLoc::isKind(const TypeLoc &TL) {
  if (TL.getType().hasLocalQualifiers()) return false;
  return TSTChecker().Visit(TL);
}

// Reimplemented to account for GNU/C++ extension
//     typeof unary-expression
// where there are no parentheses.
SourceRange TypeOfExprTypeLoc::getLocalSourceRange() const {
  if (getRParenLoc().isValid())
    return SourceRange(getTypeofLoc(), getRParenLoc());
  else
    return SourceRange(getTypeofLoc(),
                       getUnderlyingExpr()->getSourceRange().getEnd());
}


TypeSpecifierType BuiltinTypeLoc::getWrittenTypeSpec() const {
  if (needsExtraLocalData())
    return static_cast<TypeSpecifierType>(getWrittenBuiltinSpecs().Type);
  switch (getTypePtr()->getKind()) {
  case BuiltinType::Void:
    return TST_void;
  case BuiltinType::Bool:
    return TST_bool;
  case BuiltinType::Char_U:
  case BuiltinType::Char_S:
    return TST_char;
  case BuiltinType::Char8:
    return TST_char8;
  case BuiltinType::Char16:
    return TST_char16;
  case BuiltinType::Char32:
    return TST_char32;
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    return TST_wchar;
  case BuiltinType::UChar:
  case BuiltinType::UShort:
  case BuiltinType::UInt:
  case BuiltinType::ULong:
  case BuiltinType::ULongLong:
  case BuiltinType::UInt128:
  case BuiltinType::SChar:
  case BuiltinType::Short:
  case BuiltinType::Int:
  case BuiltinType::Long:
  case BuiltinType::LongLong:
  case BuiltinType::Int128:
  case BuiltinType::Half:
  case BuiltinType::Float:
  case BuiltinType::Double:
  case BuiltinType::LongDouble:
  case BuiltinType::Float16:
  case BuiltinType::Float128:
  case BuiltinType::ShortAccum:
  case BuiltinType::Accum:
  case BuiltinType::LongAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::UAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::ShortFract:
  case BuiltinType::Fract:
  case BuiltinType::LongFract:
  case BuiltinType::UShortFract:
  case BuiltinType::UFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatUShortAccum:
  case BuiltinType::SatUAccum:
  case BuiltinType::SatULongAccum:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatUShortFract:
  case BuiltinType::SatUFract:
  case BuiltinType::SatULongFract:
    llvm_unreachable("Builtin type needs extra local data!");
    // Fall through, if the impossible happens.

  case BuiltinType::NullPtr:
  case BuiltinType::Overload:
  case BuiltinType::Dependent:
  case BuiltinType::BoundMember:
  case BuiltinType::UnknownAny:
  case BuiltinType::ARCUnbridgedCast:
  case BuiltinType::PseudoObject:
  case BuiltinType::ObjCId:
  case BuiltinType::ObjCClass:
  case BuiltinType::ObjCSel:
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
  case BuiltinType::OCLSampler:
  case BuiltinType::OCLEvent:
  case BuiltinType::OCLClkEvent:
  case BuiltinType::OCLQueue:
  case BuiltinType::OCLReserveID:
  case BuiltinType::BuiltinFn:
  case BuiltinType::OMPArraySection:
    return TST_unspecified;
  }

  llvm_unreachable("Invalid BuiltinType Kind!");
}

TypeLoc TypeLoc::IgnoreParensImpl(TypeLoc TL) {
  while (ParenTypeLoc PTL = TL.getAs<ParenTypeLoc>())
    TL = PTL.getInnerLoc();
  return TL;
}

SourceLocation TypeLoc::findNullabilityLoc() const {
  if (auto ATL = getAs<AttributedTypeLoc>()) {
    const Attr *A = ATL.getAttr();
    if (A && (isa<TypeNullableAttr>(A) || isa<TypeNonNullAttr>(A) ||
              isa<TypeNullUnspecifiedAttr>(A)))
      return A->getLocation();
  }

  return {};
}

TypeLoc TypeLoc::findExplicitQualifierLoc() const {
  // Qualified types.
  if (auto qual = getAs<QualifiedTypeLoc>())
    return qual;

  TypeLoc loc = IgnoreParens();

  // Attributed types.
  if (auto attr = loc.getAs<AttributedTypeLoc>()) {
    if (attr.isQualifier()) return attr;
    return attr.getModifiedLoc().findExplicitQualifierLoc();
  }

  // C11 _Atomic types.
  if (auto atomic = loc.getAs<AtomicTypeLoc>()) {
    return atomic;
  }

  return {};
}

void ObjCTypeParamTypeLoc::initializeLocal(ASTContext &Context,
                                           SourceLocation Loc) {
  setNameLoc(Loc);
  if (!getNumProtocols()) return;

  setProtocolLAngleLoc(Loc);
  setProtocolRAngleLoc(Loc);
  for (unsigned i = 0, e = getNumProtocols(); i != e; ++i)
    setProtocolLoc(i, Loc);
}

void ObjCObjectTypeLoc::initializeLocal(ASTContext &Context,
                                        SourceLocation Loc) {
  setHasBaseTypeAsWritten(true);
  setTypeArgsLAngleLoc(Loc);
  setTypeArgsRAngleLoc(Loc);
  for (unsigned i = 0, e = getNumTypeArgs(); i != e; ++i) {
    setTypeArgTInfo(i,
                   Context.getTrivialTypeSourceInfo(
                     getTypePtr()->getTypeArgsAsWritten()[i], Loc));
  }
  setProtocolLAngleLoc(Loc);
  setProtocolRAngleLoc(Loc);
  for (unsigned i = 0, e = getNumProtocols(); i != e; ++i)
    setProtocolLoc(i, Loc);
}

void TypeOfTypeLoc::initializeLocal(ASTContext &Context,
                                       SourceLocation Loc) {
  TypeofLikeTypeLoc<TypeOfTypeLoc, TypeOfType, TypeOfTypeLocInfo>
      ::initializeLocal(Context, Loc);
  this->getLocalData()->UnderlyingTInfo = Context.getTrivialTypeSourceInfo(
      getUnderlyingType(), Loc);
}

void UnaryTransformTypeLoc::initializeLocal(ASTContext &Context,
                                       SourceLocation Loc) {
    setKWLoc(Loc);
    setRParenLoc(Loc);
    setLParenLoc(Loc);
    this->setUnderlyingTInfo(
        Context.getTrivialTypeSourceInfo(getTypePtr()->getBaseType(), Loc));
}

void ElaboratedTypeLoc::initializeLocal(ASTContext &Context,
                                        SourceLocation Loc) {
  setElaboratedKeywordLoc(Loc);
  NestedNameSpecifierLocBuilder Builder;
  Builder.MakeTrivial(Context, getTypePtr()->getQualifier(), Loc);
  setQualifierLoc(Builder.getWithLocInContext(Context));
}

void DependentNameTypeLoc::initializeLocal(ASTContext &Context,
                                           SourceLocation Loc) {
  setElaboratedKeywordLoc(Loc);
  NestedNameSpecifierLocBuilder Builder;
  Builder.MakeTrivial(Context, getTypePtr()->getQualifier(), Loc);
  setQualifierLoc(Builder.getWithLocInContext(Context));
  setNameLoc(Loc);
}

void
DependentTemplateSpecializationTypeLoc::initializeLocal(ASTContext &Context,
                                                        SourceLocation Loc) {
  setElaboratedKeywordLoc(Loc);
  if (getTypePtr()->getQualifier()) {
    NestedNameSpecifierLocBuilder Builder;
    Builder.MakeTrivial(Context, getTypePtr()->getQualifier(), Loc);
    setQualifierLoc(Builder.getWithLocInContext(Context));
  } else {
    setQualifierLoc(NestedNameSpecifierLoc());
  }
  setTemplateKeywordLoc(Loc);
  setTemplateNameLoc(Loc);
  setLAngleLoc(Loc);
  setRAngleLoc(Loc);
  TemplateSpecializationTypeLoc::initializeArgLocs(Context, getNumArgs(),
                                                   getTypePtr()->getArgs(),
                                                   getArgInfos(), Loc);
}

void TemplateSpecializationTypeLoc::initializeArgLocs(ASTContext &Context,
                                                      unsigned NumArgs,
                                                  const TemplateArgument *Args,
                                              TemplateArgumentLocInfo *ArgInfos,
                                                      SourceLocation Loc) {
  for (unsigned i = 0, e = NumArgs; i != e; ++i) {
    switch (Args[i].getKind()) {
    case TemplateArgument::Null:
      llvm_unreachable("Impossible TemplateArgument");

    case TemplateArgument::Integral:
    case TemplateArgument::Declaration:
    case TemplateArgument::NullPtr:
      ArgInfos[i] = TemplateArgumentLocInfo();
      break;

    case TemplateArgument::Expression:
      ArgInfos[i] = TemplateArgumentLocInfo(Args[i].getAsExpr());
      break;

    case TemplateArgument::Type:
      ArgInfos[i] = TemplateArgumentLocInfo(
                          Context.getTrivialTypeSourceInfo(Args[i].getAsType(),
                                                           Loc));
      break;

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion: {
      NestedNameSpecifierLocBuilder Builder;
      TemplateName Template = Args[i].getAsTemplateOrTemplatePattern();
      if (DependentTemplateName *DTN = Template.getAsDependentTemplateName())
        Builder.MakeTrivial(Context, DTN->getQualifier(), Loc);
      else if (QualifiedTemplateName *QTN = Template.getAsQualifiedTemplateName())
        Builder.MakeTrivial(Context, QTN->getQualifier(), Loc);

      ArgInfos[i] = TemplateArgumentLocInfo(
          Builder.getWithLocInContext(Context), Loc,
          Args[i].getKind() == TemplateArgument::Template ? SourceLocation()
                                                          : Loc);
      break;
    }

    case TemplateArgument::Pack:
      ArgInfos[i] = TemplateArgumentLocInfo();
      break;
    }
  }
}
