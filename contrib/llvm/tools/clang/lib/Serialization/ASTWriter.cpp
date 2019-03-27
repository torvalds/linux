//===- ASTWriter.cpp - AST File Writer ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTWriter class, which writes AST files.
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/ASTWriter.h"
#include "ASTCommon.h"
#include "ASTReaderInternals.h"
#include "MultiOnDiskHashTable.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/MemoryBufferCache.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/OpenCLOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Sema/IdentifierResolver.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Weak.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/Module.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Serialization/SerializationDiagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitCodes.h"
#include "llvm/Bitcode/BitstreamWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <limits>
#include <memory>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::serialization;

template <typename T, typename Allocator>
static StringRef bytes(const std::vector<T, Allocator> &v) {
  if (v.empty()) return StringRef();
  return StringRef(reinterpret_cast<const char*>(&v[0]),
                         sizeof(T) * v.size());
}

template <typename T>
static StringRef bytes(const SmallVectorImpl<T> &v) {
  return StringRef(reinterpret_cast<const char*>(v.data()),
                         sizeof(T) * v.size());
}

//===----------------------------------------------------------------------===//
// Type serialization
//===----------------------------------------------------------------------===//

namespace clang {

  class ASTTypeWriter {
    ASTWriter &Writer;
    ASTRecordWriter Record;

    /// Type code that corresponds to the record generated.
    TypeCode Code = static_cast<TypeCode>(0);

    /// Abbreviation to use for the record, if any.
    unsigned AbbrevToUse = 0;

  public:
    ASTTypeWriter(ASTWriter &Writer, ASTWriter::RecordDataImpl &Record)
      : Writer(Writer), Record(Writer, Record) {}

    uint64_t Emit() {
      return Record.Emit(Code, AbbrevToUse);
    }

    void Visit(QualType T) {
      if (T.hasLocalNonFastQualifiers()) {
        Qualifiers Qs = T.getLocalQualifiers();
        Record.AddTypeRef(T.getLocalUnqualifiedType());
        Record.push_back(Qs.getAsOpaqueValue());
        Code = TYPE_EXT_QUAL;
        AbbrevToUse = Writer.TypeExtQualAbbrev;
      } else {
        switch (T->getTypeClass()) {
          // For all of the concrete, non-dependent types, call the
          // appropriate visitor function.
#define TYPE(Class, Base) \
        case Type::Class: Visit##Class##Type(cast<Class##Type>(T)); break;
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"
        }
      }
    }

    void VisitArrayType(const ArrayType *T);
    void VisitFunctionType(const FunctionType *T);
    void VisitTagType(const TagType *T);

#define TYPE(Class, Base) void Visit##Class##Type(const Class##Type *T);
#define ABSTRACT_TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"
  };

} // namespace clang

void ASTTypeWriter::VisitBuiltinType(const BuiltinType *T) {
  llvm_unreachable("Built-in types are never serialized");
}

void ASTTypeWriter::VisitComplexType(const ComplexType *T) {
  Record.AddTypeRef(T->getElementType());
  Code = TYPE_COMPLEX;
}

void ASTTypeWriter::VisitPointerType(const PointerType *T) {
  Record.AddTypeRef(T->getPointeeType());
  Code = TYPE_POINTER;
}

void ASTTypeWriter::VisitDecayedType(const DecayedType *T) {
  Record.AddTypeRef(T->getOriginalType());
  Code = TYPE_DECAYED;
}

void ASTTypeWriter::VisitAdjustedType(const AdjustedType *T) {
  Record.AddTypeRef(T->getOriginalType());
  Record.AddTypeRef(T->getAdjustedType());
  Code = TYPE_ADJUSTED;
}

void ASTTypeWriter::VisitBlockPointerType(const BlockPointerType *T) {
  Record.AddTypeRef(T->getPointeeType());
  Code = TYPE_BLOCK_POINTER;
}

void ASTTypeWriter::VisitLValueReferenceType(const LValueReferenceType *T) {
  Record.AddTypeRef(T->getPointeeTypeAsWritten());
  Record.push_back(T->isSpelledAsLValue());
  Code = TYPE_LVALUE_REFERENCE;
}

void ASTTypeWriter::VisitRValueReferenceType(const RValueReferenceType *T) {
  Record.AddTypeRef(T->getPointeeTypeAsWritten());
  Code = TYPE_RVALUE_REFERENCE;
}

void ASTTypeWriter::VisitMemberPointerType(const MemberPointerType *T) {
  Record.AddTypeRef(T->getPointeeType());
  Record.AddTypeRef(QualType(T->getClass(), 0));
  Code = TYPE_MEMBER_POINTER;
}

void ASTTypeWriter::VisitArrayType(const ArrayType *T) {
  Record.AddTypeRef(T->getElementType());
  Record.push_back(T->getSizeModifier()); // FIXME: stable values
  Record.push_back(T->getIndexTypeCVRQualifiers()); // FIXME: stable values
}

void ASTTypeWriter::VisitConstantArrayType(const ConstantArrayType *T) {
  VisitArrayType(T);
  Record.AddAPInt(T->getSize());
  Code = TYPE_CONSTANT_ARRAY;
}

void ASTTypeWriter::VisitIncompleteArrayType(const IncompleteArrayType *T) {
  VisitArrayType(T);
  Code = TYPE_INCOMPLETE_ARRAY;
}

void ASTTypeWriter::VisitVariableArrayType(const VariableArrayType *T) {
  VisitArrayType(T);
  Record.AddSourceLocation(T->getLBracketLoc());
  Record.AddSourceLocation(T->getRBracketLoc());
  Record.AddStmt(T->getSizeExpr());
  Code = TYPE_VARIABLE_ARRAY;
}

void ASTTypeWriter::VisitVectorType(const VectorType *T) {
  Record.AddTypeRef(T->getElementType());
  Record.push_back(T->getNumElements());
  Record.push_back(T->getVectorKind());
  Code = TYPE_VECTOR;
}

void ASTTypeWriter::VisitExtVectorType(const ExtVectorType *T) {
  VisitVectorType(T);
  Code = TYPE_EXT_VECTOR;
}

void ASTTypeWriter::VisitFunctionType(const FunctionType *T) {
  Record.AddTypeRef(T->getReturnType());
  FunctionType::ExtInfo C = T->getExtInfo();
  Record.push_back(C.getNoReturn());
  Record.push_back(C.getHasRegParm());
  Record.push_back(C.getRegParm());
  // FIXME: need to stabilize encoding of calling convention...
  Record.push_back(C.getCC());
  Record.push_back(C.getProducesResult());
  Record.push_back(C.getNoCallerSavedRegs());
  Record.push_back(C.getNoCfCheck());

  if (C.getHasRegParm() || C.getRegParm() || C.getProducesResult())
    AbbrevToUse = 0;
}

void ASTTypeWriter::VisitFunctionNoProtoType(const FunctionNoProtoType *T) {
  VisitFunctionType(T);
  Code = TYPE_FUNCTION_NO_PROTO;
}

static void addExceptionSpec(const FunctionProtoType *T,
                             ASTRecordWriter &Record) {
  Record.push_back(T->getExceptionSpecType());
  if (T->getExceptionSpecType() == EST_Dynamic) {
    Record.push_back(T->getNumExceptions());
    for (unsigned I = 0, N = T->getNumExceptions(); I != N; ++I)
      Record.AddTypeRef(T->getExceptionType(I));
  } else if (isComputedNoexcept(T->getExceptionSpecType())) {
    Record.AddStmt(T->getNoexceptExpr());
  } else if (T->getExceptionSpecType() == EST_Uninstantiated) {
    Record.AddDeclRef(T->getExceptionSpecDecl());
    Record.AddDeclRef(T->getExceptionSpecTemplate());
  } else if (T->getExceptionSpecType() == EST_Unevaluated) {
    Record.AddDeclRef(T->getExceptionSpecDecl());
  }
}

void ASTTypeWriter::VisitFunctionProtoType(const FunctionProtoType *T) {
  VisitFunctionType(T);

  Record.push_back(T->isVariadic());
  Record.push_back(T->hasTrailingReturn());
  Record.push_back(T->getTypeQuals().getAsOpaqueValue());
  Record.push_back(static_cast<unsigned>(T->getRefQualifier()));
  addExceptionSpec(T, Record);

  Record.push_back(T->getNumParams());
  for (unsigned I = 0, N = T->getNumParams(); I != N; ++I)
    Record.AddTypeRef(T->getParamType(I));

  if (T->hasExtParameterInfos()) {
    for (unsigned I = 0, N = T->getNumParams(); I != N; ++I)
      Record.push_back(T->getExtParameterInfo(I).getOpaqueValue());
  }

  if (T->isVariadic() || T->hasTrailingReturn() || T->getTypeQuals() ||
      T->getRefQualifier() || T->getExceptionSpecType() != EST_None ||
      T->hasExtParameterInfos())
    AbbrevToUse = 0;

  Code = TYPE_FUNCTION_PROTO;
}

void ASTTypeWriter::VisitUnresolvedUsingType(const UnresolvedUsingType *T) {
  Record.AddDeclRef(T->getDecl());
  Code = TYPE_UNRESOLVED_USING;
}

void ASTTypeWriter::VisitTypedefType(const TypedefType *T) {
  Record.AddDeclRef(T->getDecl());
  assert(!T->isCanonicalUnqualified() && "Invalid typedef ?");
  Record.AddTypeRef(T->getCanonicalTypeInternal());
  Code = TYPE_TYPEDEF;
}

void ASTTypeWriter::VisitTypeOfExprType(const TypeOfExprType *T) {
  Record.AddStmt(T->getUnderlyingExpr());
  Code = TYPE_TYPEOF_EXPR;
}

void ASTTypeWriter::VisitTypeOfType(const TypeOfType *T) {
  Record.AddTypeRef(T->getUnderlyingType());
  Code = TYPE_TYPEOF;
}

void ASTTypeWriter::VisitDecltypeType(const DecltypeType *T) {
  Record.AddTypeRef(T->getUnderlyingType());
  Record.AddStmt(T->getUnderlyingExpr());
  Code = TYPE_DECLTYPE;
}

void ASTTypeWriter::VisitUnaryTransformType(const UnaryTransformType *T) {
  Record.AddTypeRef(T->getBaseType());
  Record.AddTypeRef(T->getUnderlyingType());
  Record.push_back(T->getUTTKind());
  Code = TYPE_UNARY_TRANSFORM;
}

void ASTTypeWriter::VisitAutoType(const AutoType *T) {
  Record.AddTypeRef(T->getDeducedType());
  Record.push_back((unsigned)T->getKeyword());
  if (T->getDeducedType().isNull())
    Record.push_back(T->isDependentType());
  Code = TYPE_AUTO;
}

void ASTTypeWriter::VisitDeducedTemplateSpecializationType(
    const DeducedTemplateSpecializationType *T) {
  Record.AddTemplateName(T->getTemplateName());
  Record.AddTypeRef(T->getDeducedType());
  if (T->getDeducedType().isNull())
    Record.push_back(T->isDependentType());
  Code = TYPE_DEDUCED_TEMPLATE_SPECIALIZATION;
}

void ASTTypeWriter::VisitTagType(const TagType *T) {
  Record.push_back(T->isDependentType());
  Record.AddDeclRef(T->getDecl()->getCanonicalDecl());
  assert(!T->isBeingDefined() &&
         "Cannot serialize in the middle of a type definition");
}

void ASTTypeWriter::VisitRecordType(const RecordType *T) {
  VisitTagType(T);
  Code = TYPE_RECORD;
}

void ASTTypeWriter::VisitEnumType(const EnumType *T) {
  VisitTagType(T);
  Code = TYPE_ENUM;
}

void ASTTypeWriter::VisitAttributedType(const AttributedType *T) {
  Record.AddTypeRef(T->getModifiedType());
  Record.AddTypeRef(T->getEquivalentType());
  Record.push_back(T->getAttrKind());
  Code = TYPE_ATTRIBUTED;
}

void
ASTTypeWriter::VisitSubstTemplateTypeParmType(
                                        const SubstTemplateTypeParmType *T) {
  Record.AddTypeRef(QualType(T->getReplacedParameter(), 0));
  Record.AddTypeRef(T->getReplacementType());
  Code = TYPE_SUBST_TEMPLATE_TYPE_PARM;
}

void
ASTTypeWriter::VisitSubstTemplateTypeParmPackType(
                                      const SubstTemplateTypeParmPackType *T) {
  Record.AddTypeRef(QualType(T->getReplacedParameter(), 0));
  Record.AddTemplateArgument(T->getArgumentPack());
  Code = TYPE_SUBST_TEMPLATE_TYPE_PARM_PACK;
}

void
ASTTypeWriter::VisitTemplateSpecializationType(
                                       const TemplateSpecializationType *T) {
  Record.push_back(T->isDependentType());
  Record.AddTemplateName(T->getTemplateName());
  Record.push_back(T->getNumArgs());
  for (const auto &ArgI : *T)
    Record.AddTemplateArgument(ArgI);
  Record.AddTypeRef(T->isTypeAlias() ? T->getAliasedType()
                                     : T->isCanonicalUnqualified()
                                           ? QualType()
                                           : T->getCanonicalTypeInternal());
  Code = TYPE_TEMPLATE_SPECIALIZATION;
}

void
ASTTypeWriter::VisitDependentSizedArrayType(const DependentSizedArrayType *T) {
  VisitArrayType(T);
  Record.AddStmt(T->getSizeExpr());
  Record.AddSourceRange(T->getBracketsRange());
  Code = TYPE_DEPENDENT_SIZED_ARRAY;
}

void
ASTTypeWriter::VisitDependentSizedExtVectorType(
                                        const DependentSizedExtVectorType *T) {
  Record.AddTypeRef(T->getElementType());
  Record.AddStmt(T->getSizeExpr());
  Record.AddSourceLocation(T->getAttributeLoc());
  Code = TYPE_DEPENDENT_SIZED_EXT_VECTOR;
}

void ASTTypeWriter::VisitDependentVectorType(const DependentVectorType *T) {
  Record.AddTypeRef(T->getElementType());
  Record.AddStmt(const_cast<Expr*>(T->getSizeExpr()));
  Record.AddSourceLocation(T->getAttributeLoc());
  Record.push_back(T->getVectorKind());
  Code = TYPE_DEPENDENT_SIZED_VECTOR;
}

void
ASTTypeWriter::VisitDependentAddressSpaceType(
    const DependentAddressSpaceType *T) {
  Record.AddTypeRef(T->getPointeeType());
  Record.AddStmt(T->getAddrSpaceExpr());
  Record.AddSourceLocation(T->getAttributeLoc());
  Code = TYPE_DEPENDENT_ADDRESS_SPACE;
}

void
ASTTypeWriter::VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
  Record.push_back(T->getDepth());
  Record.push_back(T->getIndex());
  Record.push_back(T->isParameterPack());
  Record.AddDeclRef(T->getDecl());
  Code = TYPE_TEMPLATE_TYPE_PARM;
}

void
ASTTypeWriter::VisitDependentNameType(const DependentNameType *T) {
  Record.push_back(T->getKeyword());
  Record.AddNestedNameSpecifier(T->getQualifier());
  Record.AddIdentifierRef(T->getIdentifier());
  Record.AddTypeRef(
      T->isCanonicalUnqualified() ? QualType() : T->getCanonicalTypeInternal());
  Code = TYPE_DEPENDENT_NAME;
}

void
ASTTypeWriter::VisitDependentTemplateSpecializationType(
                                const DependentTemplateSpecializationType *T) {
  Record.push_back(T->getKeyword());
  Record.AddNestedNameSpecifier(T->getQualifier());
  Record.AddIdentifierRef(T->getIdentifier());
  Record.push_back(T->getNumArgs());
  for (const auto &I : *T)
    Record.AddTemplateArgument(I);
  Code = TYPE_DEPENDENT_TEMPLATE_SPECIALIZATION;
}

void ASTTypeWriter::VisitPackExpansionType(const PackExpansionType *T) {
  Record.AddTypeRef(T->getPattern());
  if (Optional<unsigned> NumExpansions = T->getNumExpansions())
    Record.push_back(*NumExpansions + 1);
  else
    Record.push_back(0);
  Code = TYPE_PACK_EXPANSION;
}

void ASTTypeWriter::VisitParenType(const ParenType *T) {
  Record.AddTypeRef(T->getInnerType());
  Code = TYPE_PAREN;
}

void ASTTypeWriter::VisitElaboratedType(const ElaboratedType *T) {
  Record.push_back(T->getKeyword());
  Record.AddNestedNameSpecifier(T->getQualifier());
  Record.AddTypeRef(T->getNamedType());
  Record.AddDeclRef(T->getOwnedTagDecl());
  Code = TYPE_ELABORATED;
}

void ASTTypeWriter::VisitInjectedClassNameType(const InjectedClassNameType *T) {
  Record.AddDeclRef(T->getDecl()->getCanonicalDecl());
  Record.AddTypeRef(T->getInjectedSpecializationType());
  Code = TYPE_INJECTED_CLASS_NAME;
}

void ASTTypeWriter::VisitObjCInterfaceType(const ObjCInterfaceType *T) {
  Record.AddDeclRef(T->getDecl()->getCanonicalDecl());
  Code = TYPE_OBJC_INTERFACE;
}

void ASTTypeWriter::VisitObjCTypeParamType(const ObjCTypeParamType *T) {
  Record.AddDeclRef(T->getDecl());
  Record.push_back(T->getNumProtocols());
  for (const auto *I : T->quals())
    Record.AddDeclRef(I);
  Code = TYPE_OBJC_TYPE_PARAM;
}

void ASTTypeWriter::VisitObjCObjectType(const ObjCObjectType *T) {
  Record.AddTypeRef(T->getBaseType());
  Record.push_back(T->getTypeArgsAsWritten().size());
  for (auto TypeArg : T->getTypeArgsAsWritten())
    Record.AddTypeRef(TypeArg);
  Record.push_back(T->getNumProtocols());
  for (const auto *I : T->quals())
    Record.AddDeclRef(I);
  Record.push_back(T->isKindOfTypeAsWritten());
  Code = TYPE_OBJC_OBJECT;
}

void
ASTTypeWriter::VisitObjCObjectPointerType(const ObjCObjectPointerType *T) {
  Record.AddTypeRef(T->getPointeeType());
  Code = TYPE_OBJC_OBJECT_POINTER;
}

void
ASTTypeWriter::VisitAtomicType(const AtomicType *T) {
  Record.AddTypeRef(T->getValueType());
  Code = TYPE_ATOMIC;
}

void
ASTTypeWriter::VisitPipeType(const PipeType *T) {
  Record.AddTypeRef(T->getElementType());
  Record.push_back(T->isReadOnly());
  Code = TYPE_PIPE;
}

namespace {

class TypeLocWriter : public TypeLocVisitor<TypeLocWriter> {
  ASTRecordWriter &Record;

public:
  TypeLocWriter(ASTRecordWriter &Record) : Record(Record) {}

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
    void Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc);
#include "clang/AST/TypeLocNodes.def"

  void VisitArrayTypeLoc(ArrayTypeLoc TyLoc);
  void VisitFunctionTypeLoc(FunctionTypeLoc TyLoc);
};

} // namespace

void TypeLocWriter::VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
  Record.AddSourceLocation(TL.getBuiltinLoc());
  if (TL.needsExtraLocalData()) {
    Record.push_back(TL.getWrittenTypeSpec());
    Record.push_back(TL.getWrittenSignSpec());
    Record.push_back(TL.getWrittenWidthSpec());
    Record.push_back(TL.hasModeAttr());
  }
}

void TypeLocWriter::VisitComplexTypeLoc(ComplexTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitPointerTypeLoc(PointerTypeLoc TL) {
  Record.AddSourceLocation(TL.getStarLoc());
}

void TypeLocWriter::VisitDecayedTypeLoc(DecayedTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitBlockPointerTypeLoc(BlockPointerTypeLoc TL) {
  Record.AddSourceLocation(TL.getCaretLoc());
}

void TypeLocWriter::VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc TL) {
  Record.AddSourceLocation(TL.getAmpLoc());
}

void TypeLocWriter::VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc TL) {
  Record.AddSourceLocation(TL.getAmpAmpLoc());
}

void TypeLocWriter::VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
  Record.AddSourceLocation(TL.getStarLoc());
  Record.AddTypeSourceInfo(TL.getClassTInfo());
}

void TypeLocWriter::VisitArrayTypeLoc(ArrayTypeLoc TL) {
  Record.AddSourceLocation(TL.getLBracketLoc());
  Record.AddSourceLocation(TL.getRBracketLoc());
  Record.push_back(TL.getSizeExpr() ? 1 : 0);
  if (TL.getSizeExpr())
    Record.AddStmt(TL.getSizeExpr());
}

void TypeLocWriter::VisitConstantArrayTypeLoc(ConstantArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitIncompleteArrayTypeLoc(IncompleteArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitVariableArrayTypeLoc(VariableArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitDependentSizedArrayTypeLoc(
                                            DependentSizedArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitDependentAddressSpaceTypeLoc(
    DependentAddressSpaceTypeLoc TL) {
  Record.AddSourceLocation(TL.getAttrNameLoc());
  SourceRange range = TL.getAttrOperandParensRange();
  Record.AddSourceLocation(range.getBegin());
  Record.AddSourceLocation(range.getEnd());
  Record.AddStmt(TL.getAttrExprOperand());
}

void TypeLocWriter::VisitDependentSizedExtVectorTypeLoc(
                                        DependentSizedExtVectorTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitVectorTypeLoc(VectorTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDependentVectorTypeLoc(
    DependentVectorTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitExtVectorTypeLoc(ExtVectorTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitFunctionTypeLoc(FunctionTypeLoc TL) {
  Record.AddSourceLocation(TL.getLocalRangeBegin());
  Record.AddSourceLocation(TL.getLParenLoc());
  Record.AddSourceLocation(TL.getRParenLoc());
  Record.AddSourceRange(TL.getExceptionSpecRange());
  Record.AddSourceLocation(TL.getLocalRangeEnd());
  for (unsigned i = 0, e = TL.getNumParams(); i != e; ++i)
    Record.AddDeclRef(TL.getParam(i));
}

void TypeLocWriter::VisitFunctionProtoTypeLoc(FunctionProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocWriter::VisitFunctionNoProtoTypeLoc(FunctionNoProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocWriter::VisitUnresolvedUsingTypeLoc(UnresolvedUsingTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitTypedefTypeLoc(TypedefTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitObjCTypeParamTypeLoc(ObjCTypeParamTypeLoc TL) {
  if (TL.getNumProtocols()) {
    Record.AddSourceLocation(TL.getProtocolLAngleLoc());
    Record.AddSourceLocation(TL.getProtocolRAngleLoc());
  }
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    Record.AddSourceLocation(TL.getProtocolLoc(i));
}

void TypeLocWriter::VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
  Record.AddSourceLocation(TL.getTypeofLoc());
  Record.AddSourceLocation(TL.getLParenLoc());
  Record.AddSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
  Record.AddSourceLocation(TL.getTypeofLoc());
  Record.AddSourceLocation(TL.getLParenLoc());
  Record.AddSourceLocation(TL.getRParenLoc());
  Record.AddTypeSourceInfo(TL.getUnderlyingTInfo());
}

void TypeLocWriter::VisitDecltypeTypeLoc(DecltypeTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitUnaryTransformTypeLoc(UnaryTransformTypeLoc TL) {
  Record.AddSourceLocation(TL.getKWLoc());
  Record.AddSourceLocation(TL.getLParenLoc());
  Record.AddSourceLocation(TL.getRParenLoc());
  Record.AddTypeSourceInfo(TL.getUnderlyingTInfo());
}

void TypeLocWriter::VisitAutoTypeLoc(AutoTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDeducedTemplateSpecializationTypeLoc(
    DeducedTemplateSpecializationTypeLoc TL) {
  Record.AddSourceLocation(TL.getTemplateNameLoc());
}

void TypeLocWriter::VisitRecordTypeLoc(RecordTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitEnumTypeLoc(EnumTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitAttributedTypeLoc(AttributedTypeLoc TL) {
  Record.AddAttr(TL.getAttr());
}

void TypeLocWriter::VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitSubstTemplateTypeParmTypeLoc(
                                            SubstTemplateTypeParmTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitSubstTemplateTypeParmPackTypeLoc(
                                          SubstTemplateTypeParmPackTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitTemplateSpecializationTypeLoc(
                                           TemplateSpecializationTypeLoc TL) {
  Record.AddSourceLocation(TL.getTemplateKeywordLoc());
  Record.AddSourceLocation(TL.getTemplateNameLoc());
  Record.AddSourceLocation(TL.getLAngleLoc());
  Record.AddSourceLocation(TL.getRAngleLoc());
  for (unsigned i = 0, e = TL.getNumArgs(); i != e; ++i)
    Record.AddTemplateArgumentLocInfo(TL.getArgLoc(i).getArgument().getKind(),
                                      TL.getArgLoc(i).getLocInfo());
}

void TypeLocWriter::VisitParenTypeLoc(ParenTypeLoc TL) {
  Record.AddSourceLocation(TL.getLParenLoc());
  Record.AddSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
  Record.AddSourceLocation(TL.getElaboratedKeywordLoc());
  Record.AddNestedNameSpecifierLoc(TL.getQualifierLoc());
}

void TypeLocWriter::VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
  Record.AddSourceLocation(TL.getElaboratedKeywordLoc());
  Record.AddNestedNameSpecifierLoc(TL.getQualifierLoc());
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDependentTemplateSpecializationTypeLoc(
       DependentTemplateSpecializationTypeLoc TL) {
  Record.AddSourceLocation(TL.getElaboratedKeywordLoc());
  Record.AddNestedNameSpecifierLoc(TL.getQualifierLoc());
  Record.AddSourceLocation(TL.getTemplateKeywordLoc());
  Record.AddSourceLocation(TL.getTemplateNameLoc());
  Record.AddSourceLocation(TL.getLAngleLoc());
  Record.AddSourceLocation(TL.getRAngleLoc());
  for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I)
    Record.AddTemplateArgumentLocInfo(TL.getArgLoc(I).getArgument().getKind(),
                                      TL.getArgLoc(I).getLocInfo());
}

void TypeLocWriter::VisitPackExpansionTypeLoc(PackExpansionTypeLoc TL) {
  Record.AddSourceLocation(TL.getEllipsisLoc());
}

void TypeLocWriter::VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
  Record.AddSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
  Record.push_back(TL.hasBaseTypeAsWritten());
  Record.AddSourceLocation(TL.getTypeArgsLAngleLoc());
  Record.AddSourceLocation(TL.getTypeArgsRAngleLoc());
  for (unsigned i = 0, e = TL.getNumTypeArgs(); i != e; ++i)
    Record.AddTypeSourceInfo(TL.getTypeArgTInfo(i));
  Record.AddSourceLocation(TL.getProtocolLAngleLoc());
  Record.AddSourceLocation(TL.getProtocolRAngleLoc());
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    Record.AddSourceLocation(TL.getProtocolLoc(i));
}

void TypeLocWriter::VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
  Record.AddSourceLocation(TL.getStarLoc());
}

void TypeLocWriter::VisitAtomicTypeLoc(AtomicTypeLoc TL) {
  Record.AddSourceLocation(TL.getKWLoc());
  Record.AddSourceLocation(TL.getLParenLoc());
  Record.AddSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitPipeTypeLoc(PipeTypeLoc TL) {
  Record.AddSourceLocation(TL.getKWLoc());
}

void ASTWriter::WriteTypeAbbrevs() {
  using namespace llvm;

  std::shared_ptr<BitCodeAbbrev> Abv;

  // Abbreviation for TYPE_EXT_QUAL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::TYPE_EXT_QUAL));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Type
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 3));   // Quals
  TypeExtQualAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for TYPE_FUNCTION_PROTO
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::TYPE_FUNCTION_PROTO));
  // FunctionType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // ReturnType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // NoReturn
  Abv->Add(BitCodeAbbrevOp(0));                         // HasRegParm
  Abv->Add(BitCodeAbbrevOp(0));                         // RegParm
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // CC
  Abv->Add(BitCodeAbbrevOp(0));                         // ProducesResult
  Abv->Add(BitCodeAbbrevOp(0));                         // NoCallerSavedRegs
  Abv->Add(BitCodeAbbrevOp(0));                         // NoCfCheck
  // FunctionProtoType
  Abv->Add(BitCodeAbbrevOp(0));                         // IsVariadic
  Abv->Add(BitCodeAbbrevOp(0));                         // HasTrailingReturn
  Abv->Add(BitCodeAbbrevOp(0));                         // TypeQuals
  Abv->Add(BitCodeAbbrevOp(0));                         // RefQualifier
  Abv->Add(BitCodeAbbrevOp(EST_None));                  // ExceptionSpec
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // NumParams
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Params
  TypeFunctionProtoAbbrev = Stream.EmitAbbrev(std::move(Abv));
}

//===----------------------------------------------------------------------===//
// ASTWriter Implementation
//===----------------------------------------------------------------------===//

static void EmitBlockID(unsigned ID, const char *Name,
                        llvm::BitstreamWriter &Stream,
                        ASTWriter::RecordDataImpl &Record) {
  Record.clear();
  Record.push_back(ID);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETBID, Record);

  // Emit the block name if present.
  if (!Name || Name[0] == 0)
    return;
  Record.clear();
  while (*Name)
    Record.push_back(*Name++);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_BLOCKNAME, Record);
}

static void EmitRecordID(unsigned ID, const char *Name,
                         llvm::BitstreamWriter &Stream,
                         ASTWriter::RecordDataImpl &Record) {
  Record.clear();
  Record.push_back(ID);
  while (*Name)
    Record.push_back(*Name++);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETRECORDNAME, Record);
}

static void AddStmtsExprs(llvm::BitstreamWriter &Stream,
                          ASTWriter::RecordDataImpl &Record) {
#define RECORD(X) EmitRecordID(X, #X, Stream, Record)
  RECORD(STMT_STOP);
  RECORD(STMT_NULL_PTR);
  RECORD(STMT_REF_PTR);
  RECORD(STMT_NULL);
  RECORD(STMT_COMPOUND);
  RECORD(STMT_CASE);
  RECORD(STMT_DEFAULT);
  RECORD(STMT_LABEL);
  RECORD(STMT_ATTRIBUTED);
  RECORD(STMT_IF);
  RECORD(STMT_SWITCH);
  RECORD(STMT_WHILE);
  RECORD(STMT_DO);
  RECORD(STMT_FOR);
  RECORD(STMT_GOTO);
  RECORD(STMT_INDIRECT_GOTO);
  RECORD(STMT_CONTINUE);
  RECORD(STMT_BREAK);
  RECORD(STMT_RETURN);
  RECORD(STMT_DECL);
  RECORD(STMT_GCCASM);
  RECORD(STMT_MSASM);
  RECORD(EXPR_PREDEFINED);
  RECORD(EXPR_DECL_REF);
  RECORD(EXPR_INTEGER_LITERAL);
  RECORD(EXPR_FLOATING_LITERAL);
  RECORD(EXPR_IMAGINARY_LITERAL);
  RECORD(EXPR_STRING_LITERAL);
  RECORD(EXPR_CHARACTER_LITERAL);
  RECORD(EXPR_PAREN);
  RECORD(EXPR_PAREN_LIST);
  RECORD(EXPR_UNARY_OPERATOR);
  RECORD(EXPR_SIZEOF_ALIGN_OF);
  RECORD(EXPR_ARRAY_SUBSCRIPT);
  RECORD(EXPR_CALL);
  RECORD(EXPR_MEMBER);
  RECORD(EXPR_BINARY_OPERATOR);
  RECORD(EXPR_COMPOUND_ASSIGN_OPERATOR);
  RECORD(EXPR_CONDITIONAL_OPERATOR);
  RECORD(EXPR_IMPLICIT_CAST);
  RECORD(EXPR_CSTYLE_CAST);
  RECORD(EXPR_COMPOUND_LITERAL);
  RECORD(EXPR_EXT_VECTOR_ELEMENT);
  RECORD(EXPR_INIT_LIST);
  RECORD(EXPR_DESIGNATED_INIT);
  RECORD(EXPR_DESIGNATED_INIT_UPDATE);
  RECORD(EXPR_IMPLICIT_VALUE_INIT);
  RECORD(EXPR_NO_INIT);
  RECORD(EXPR_VA_ARG);
  RECORD(EXPR_ADDR_LABEL);
  RECORD(EXPR_STMT);
  RECORD(EXPR_CHOOSE);
  RECORD(EXPR_GNU_NULL);
  RECORD(EXPR_SHUFFLE_VECTOR);
  RECORD(EXPR_BLOCK);
  RECORD(EXPR_GENERIC_SELECTION);
  RECORD(EXPR_OBJC_STRING_LITERAL);
  RECORD(EXPR_OBJC_BOXED_EXPRESSION);
  RECORD(EXPR_OBJC_ARRAY_LITERAL);
  RECORD(EXPR_OBJC_DICTIONARY_LITERAL);
  RECORD(EXPR_OBJC_ENCODE);
  RECORD(EXPR_OBJC_SELECTOR_EXPR);
  RECORD(EXPR_OBJC_PROTOCOL_EXPR);
  RECORD(EXPR_OBJC_IVAR_REF_EXPR);
  RECORD(EXPR_OBJC_PROPERTY_REF_EXPR);
  RECORD(EXPR_OBJC_KVC_REF_EXPR);
  RECORD(EXPR_OBJC_MESSAGE_EXPR);
  RECORD(STMT_OBJC_FOR_COLLECTION);
  RECORD(STMT_OBJC_CATCH);
  RECORD(STMT_OBJC_FINALLY);
  RECORD(STMT_OBJC_AT_TRY);
  RECORD(STMT_OBJC_AT_SYNCHRONIZED);
  RECORD(STMT_OBJC_AT_THROW);
  RECORD(EXPR_OBJC_BOOL_LITERAL);
  RECORD(STMT_CXX_CATCH);
  RECORD(STMT_CXX_TRY);
  RECORD(STMT_CXX_FOR_RANGE);
  RECORD(EXPR_CXX_OPERATOR_CALL);
  RECORD(EXPR_CXX_MEMBER_CALL);
  RECORD(EXPR_CXX_CONSTRUCT);
  RECORD(EXPR_CXX_TEMPORARY_OBJECT);
  RECORD(EXPR_CXX_STATIC_CAST);
  RECORD(EXPR_CXX_DYNAMIC_CAST);
  RECORD(EXPR_CXX_REINTERPRET_CAST);
  RECORD(EXPR_CXX_CONST_CAST);
  RECORD(EXPR_CXX_FUNCTIONAL_CAST);
  RECORD(EXPR_USER_DEFINED_LITERAL);
  RECORD(EXPR_CXX_STD_INITIALIZER_LIST);
  RECORD(EXPR_CXX_BOOL_LITERAL);
  RECORD(EXPR_CXX_NULL_PTR_LITERAL);
  RECORD(EXPR_CXX_TYPEID_EXPR);
  RECORD(EXPR_CXX_TYPEID_TYPE);
  RECORD(EXPR_CXX_THIS);
  RECORD(EXPR_CXX_THROW);
  RECORD(EXPR_CXX_DEFAULT_ARG);
  RECORD(EXPR_CXX_DEFAULT_INIT);
  RECORD(EXPR_CXX_BIND_TEMPORARY);
  RECORD(EXPR_CXX_SCALAR_VALUE_INIT);
  RECORD(EXPR_CXX_NEW);
  RECORD(EXPR_CXX_DELETE);
  RECORD(EXPR_CXX_PSEUDO_DESTRUCTOR);
  RECORD(EXPR_EXPR_WITH_CLEANUPS);
  RECORD(EXPR_CXX_DEPENDENT_SCOPE_MEMBER);
  RECORD(EXPR_CXX_DEPENDENT_SCOPE_DECL_REF);
  RECORD(EXPR_CXX_UNRESOLVED_CONSTRUCT);
  RECORD(EXPR_CXX_UNRESOLVED_MEMBER);
  RECORD(EXPR_CXX_UNRESOLVED_LOOKUP);
  RECORD(EXPR_CXX_EXPRESSION_TRAIT);
  RECORD(EXPR_CXX_NOEXCEPT);
  RECORD(EXPR_OPAQUE_VALUE);
  RECORD(EXPR_BINARY_CONDITIONAL_OPERATOR);
  RECORD(EXPR_TYPE_TRAIT);
  RECORD(EXPR_ARRAY_TYPE_TRAIT);
  RECORD(EXPR_PACK_EXPANSION);
  RECORD(EXPR_SIZEOF_PACK);
  RECORD(EXPR_SUBST_NON_TYPE_TEMPLATE_PARM);
  RECORD(EXPR_SUBST_NON_TYPE_TEMPLATE_PARM_PACK);
  RECORD(EXPR_FUNCTION_PARM_PACK);
  RECORD(EXPR_MATERIALIZE_TEMPORARY);
  RECORD(EXPR_CUDA_KERNEL_CALL);
  RECORD(EXPR_CXX_UUIDOF_EXPR);
  RECORD(EXPR_CXX_UUIDOF_TYPE);
  RECORD(EXPR_LAMBDA);
#undef RECORD
}

void ASTWriter::WriteBlockInfoBlock() {
  RecordData Record;
  Stream.EnterBlockInfoBlock();

#define BLOCK(X) EmitBlockID(X ## _ID, #X, Stream, Record)
#define RECORD(X) EmitRecordID(X, #X, Stream, Record)

  // Control Block.
  BLOCK(CONTROL_BLOCK);
  RECORD(METADATA);
  RECORD(MODULE_NAME);
  RECORD(MODULE_DIRECTORY);
  RECORD(MODULE_MAP_FILE);
  RECORD(IMPORTS);
  RECORD(ORIGINAL_FILE);
  RECORD(ORIGINAL_PCH_DIR);
  RECORD(ORIGINAL_FILE_ID);
  RECORD(INPUT_FILE_OFFSETS);

  BLOCK(OPTIONS_BLOCK);
  RECORD(LANGUAGE_OPTIONS);
  RECORD(TARGET_OPTIONS);
  RECORD(FILE_SYSTEM_OPTIONS);
  RECORD(HEADER_SEARCH_OPTIONS);
  RECORD(PREPROCESSOR_OPTIONS);

  BLOCK(INPUT_FILES_BLOCK);
  RECORD(INPUT_FILE);

  // AST Top-Level Block.
  BLOCK(AST_BLOCK);
  RECORD(TYPE_OFFSET);
  RECORD(DECL_OFFSET);
  RECORD(IDENTIFIER_OFFSET);
  RECORD(IDENTIFIER_TABLE);
  RECORD(EAGERLY_DESERIALIZED_DECLS);
  RECORD(MODULAR_CODEGEN_DECLS);
  RECORD(SPECIAL_TYPES);
  RECORD(STATISTICS);
  RECORD(TENTATIVE_DEFINITIONS);
  RECORD(SELECTOR_OFFSETS);
  RECORD(METHOD_POOL);
  RECORD(PP_COUNTER_VALUE);
  RECORD(SOURCE_LOCATION_OFFSETS);
  RECORD(SOURCE_LOCATION_PRELOADS);
  RECORD(EXT_VECTOR_DECLS);
  RECORD(UNUSED_FILESCOPED_DECLS);
  RECORD(PPD_ENTITIES_OFFSETS);
  RECORD(VTABLE_USES);
  RECORD(PPD_SKIPPED_RANGES);
  RECORD(REFERENCED_SELECTOR_POOL);
  RECORD(TU_UPDATE_LEXICAL);
  RECORD(SEMA_DECL_REFS);
  RECORD(WEAK_UNDECLARED_IDENTIFIERS);
  RECORD(PENDING_IMPLICIT_INSTANTIATIONS);
  RECORD(UPDATE_VISIBLE);
  RECORD(DECL_UPDATE_OFFSETS);
  RECORD(DECL_UPDATES);
  RECORD(CUDA_SPECIAL_DECL_REFS);
  RECORD(HEADER_SEARCH_TABLE);
  RECORD(FP_PRAGMA_OPTIONS);
  RECORD(OPENCL_EXTENSIONS);
  RECORD(OPENCL_EXTENSION_TYPES);
  RECORD(OPENCL_EXTENSION_DECLS);
  RECORD(DELEGATING_CTORS);
  RECORD(KNOWN_NAMESPACES);
  RECORD(MODULE_OFFSET_MAP);
  RECORD(SOURCE_MANAGER_LINE_TABLE);
  RECORD(OBJC_CATEGORIES_MAP);
  RECORD(FILE_SORTED_DECLS);
  RECORD(IMPORTED_MODULES);
  RECORD(OBJC_CATEGORIES);
  RECORD(MACRO_OFFSET);
  RECORD(INTERESTING_IDENTIFIERS);
  RECORD(UNDEFINED_BUT_USED);
  RECORD(LATE_PARSED_TEMPLATE);
  RECORD(OPTIMIZE_PRAGMA_OPTIONS);
  RECORD(MSSTRUCT_PRAGMA_OPTIONS);
  RECORD(POINTERS_TO_MEMBERS_PRAGMA_OPTIONS);
  RECORD(UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES);
  RECORD(DELETE_EXPRS_TO_ANALYZE);
  RECORD(CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH);
  RECORD(PP_CONDITIONAL_STACK);

  // SourceManager Block.
  BLOCK(SOURCE_MANAGER_BLOCK);
  RECORD(SM_SLOC_FILE_ENTRY);
  RECORD(SM_SLOC_BUFFER_ENTRY);
  RECORD(SM_SLOC_BUFFER_BLOB);
  RECORD(SM_SLOC_BUFFER_BLOB_COMPRESSED);
  RECORD(SM_SLOC_EXPANSION_ENTRY);

  // Preprocessor Block.
  BLOCK(PREPROCESSOR_BLOCK);
  RECORD(PP_MACRO_DIRECTIVE_HISTORY);
  RECORD(PP_MACRO_FUNCTION_LIKE);
  RECORD(PP_MACRO_OBJECT_LIKE);
  RECORD(PP_MODULE_MACRO);
  RECORD(PP_TOKEN);

  // Submodule Block.
  BLOCK(SUBMODULE_BLOCK);
  RECORD(SUBMODULE_METADATA);
  RECORD(SUBMODULE_DEFINITION);
  RECORD(SUBMODULE_UMBRELLA_HEADER);
  RECORD(SUBMODULE_HEADER);
  RECORD(SUBMODULE_TOPHEADER);
  RECORD(SUBMODULE_UMBRELLA_DIR);
  RECORD(SUBMODULE_IMPORTS);
  RECORD(SUBMODULE_EXPORTS);
  RECORD(SUBMODULE_REQUIRES);
  RECORD(SUBMODULE_EXCLUDED_HEADER);
  RECORD(SUBMODULE_LINK_LIBRARY);
  RECORD(SUBMODULE_CONFIG_MACRO);
  RECORD(SUBMODULE_CONFLICT);
  RECORD(SUBMODULE_PRIVATE_HEADER);
  RECORD(SUBMODULE_TEXTUAL_HEADER);
  RECORD(SUBMODULE_PRIVATE_TEXTUAL_HEADER);
  RECORD(SUBMODULE_INITIALIZERS);
  RECORD(SUBMODULE_EXPORT_AS);

  // Comments Block.
  BLOCK(COMMENTS_BLOCK);
  RECORD(COMMENTS_RAW_COMMENT);

  // Decls and Types block.
  BLOCK(DECLTYPES_BLOCK);
  RECORD(TYPE_EXT_QUAL);
  RECORD(TYPE_COMPLEX);
  RECORD(TYPE_POINTER);
  RECORD(TYPE_BLOCK_POINTER);
  RECORD(TYPE_LVALUE_REFERENCE);
  RECORD(TYPE_RVALUE_REFERENCE);
  RECORD(TYPE_MEMBER_POINTER);
  RECORD(TYPE_CONSTANT_ARRAY);
  RECORD(TYPE_INCOMPLETE_ARRAY);
  RECORD(TYPE_VARIABLE_ARRAY);
  RECORD(TYPE_VECTOR);
  RECORD(TYPE_EXT_VECTOR);
  RECORD(TYPE_FUNCTION_NO_PROTO);
  RECORD(TYPE_FUNCTION_PROTO);
  RECORD(TYPE_TYPEDEF);
  RECORD(TYPE_TYPEOF_EXPR);
  RECORD(TYPE_TYPEOF);
  RECORD(TYPE_RECORD);
  RECORD(TYPE_ENUM);
  RECORD(TYPE_OBJC_INTERFACE);
  RECORD(TYPE_OBJC_OBJECT_POINTER);
  RECORD(TYPE_DECLTYPE);
  RECORD(TYPE_ELABORATED);
  RECORD(TYPE_SUBST_TEMPLATE_TYPE_PARM);
  RECORD(TYPE_UNRESOLVED_USING);
  RECORD(TYPE_INJECTED_CLASS_NAME);
  RECORD(TYPE_OBJC_OBJECT);
  RECORD(TYPE_TEMPLATE_TYPE_PARM);
  RECORD(TYPE_TEMPLATE_SPECIALIZATION);
  RECORD(TYPE_DEPENDENT_NAME);
  RECORD(TYPE_DEPENDENT_TEMPLATE_SPECIALIZATION);
  RECORD(TYPE_DEPENDENT_SIZED_ARRAY);
  RECORD(TYPE_PAREN);
  RECORD(TYPE_PACK_EXPANSION);
  RECORD(TYPE_ATTRIBUTED);
  RECORD(TYPE_SUBST_TEMPLATE_TYPE_PARM_PACK);
  RECORD(TYPE_AUTO);
  RECORD(TYPE_UNARY_TRANSFORM);
  RECORD(TYPE_ATOMIC);
  RECORD(TYPE_DECAYED);
  RECORD(TYPE_ADJUSTED);
  RECORD(TYPE_OBJC_TYPE_PARAM);
  RECORD(LOCAL_REDECLARATIONS);
  RECORD(DECL_TYPEDEF);
  RECORD(DECL_TYPEALIAS);
  RECORD(DECL_ENUM);
  RECORD(DECL_RECORD);
  RECORD(DECL_ENUM_CONSTANT);
  RECORD(DECL_FUNCTION);
  RECORD(DECL_OBJC_METHOD);
  RECORD(DECL_OBJC_INTERFACE);
  RECORD(DECL_OBJC_PROTOCOL);
  RECORD(DECL_OBJC_IVAR);
  RECORD(DECL_OBJC_AT_DEFS_FIELD);
  RECORD(DECL_OBJC_CATEGORY);
  RECORD(DECL_OBJC_CATEGORY_IMPL);
  RECORD(DECL_OBJC_IMPLEMENTATION);
  RECORD(DECL_OBJC_COMPATIBLE_ALIAS);
  RECORD(DECL_OBJC_PROPERTY);
  RECORD(DECL_OBJC_PROPERTY_IMPL);
  RECORD(DECL_FIELD);
  RECORD(DECL_MS_PROPERTY);
  RECORD(DECL_VAR);
  RECORD(DECL_IMPLICIT_PARAM);
  RECORD(DECL_PARM_VAR);
  RECORD(DECL_FILE_SCOPE_ASM);
  RECORD(DECL_BLOCK);
  RECORD(DECL_CONTEXT_LEXICAL);
  RECORD(DECL_CONTEXT_VISIBLE);
  RECORD(DECL_NAMESPACE);
  RECORD(DECL_NAMESPACE_ALIAS);
  RECORD(DECL_USING);
  RECORD(DECL_USING_SHADOW);
  RECORD(DECL_USING_DIRECTIVE);
  RECORD(DECL_UNRESOLVED_USING_VALUE);
  RECORD(DECL_UNRESOLVED_USING_TYPENAME);
  RECORD(DECL_LINKAGE_SPEC);
  RECORD(DECL_CXX_RECORD);
  RECORD(DECL_CXX_METHOD);
  RECORD(DECL_CXX_CONSTRUCTOR);
  RECORD(DECL_CXX_INHERITED_CONSTRUCTOR);
  RECORD(DECL_CXX_DESTRUCTOR);
  RECORD(DECL_CXX_CONVERSION);
  RECORD(DECL_ACCESS_SPEC);
  RECORD(DECL_FRIEND);
  RECORD(DECL_FRIEND_TEMPLATE);
  RECORD(DECL_CLASS_TEMPLATE);
  RECORD(DECL_CLASS_TEMPLATE_SPECIALIZATION);
  RECORD(DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION);
  RECORD(DECL_VAR_TEMPLATE);
  RECORD(DECL_VAR_TEMPLATE_SPECIALIZATION);
  RECORD(DECL_VAR_TEMPLATE_PARTIAL_SPECIALIZATION);
  RECORD(DECL_FUNCTION_TEMPLATE);
  RECORD(DECL_TEMPLATE_TYPE_PARM);
  RECORD(DECL_NON_TYPE_TEMPLATE_PARM);
  RECORD(DECL_TEMPLATE_TEMPLATE_PARM);
  RECORD(DECL_TYPE_ALIAS_TEMPLATE);
  RECORD(DECL_STATIC_ASSERT);
  RECORD(DECL_CXX_BASE_SPECIFIERS);
  RECORD(DECL_CXX_CTOR_INITIALIZERS);
  RECORD(DECL_INDIRECTFIELD);
  RECORD(DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK);
  RECORD(DECL_EXPANDED_TEMPLATE_TEMPLATE_PARM_PACK);
  RECORD(DECL_CLASS_SCOPE_FUNCTION_SPECIALIZATION);
  RECORD(DECL_IMPORT);
  RECORD(DECL_OMP_THREADPRIVATE);
  RECORD(DECL_EMPTY);
  RECORD(DECL_OBJC_TYPE_PARAM);
  RECORD(DECL_OMP_CAPTUREDEXPR);
  RECORD(DECL_PRAGMA_COMMENT);
  RECORD(DECL_PRAGMA_DETECT_MISMATCH);
  RECORD(DECL_OMP_DECLARE_REDUCTION);

  // Statements and Exprs can occur in the Decls and Types block.
  AddStmtsExprs(Stream, Record);

  BLOCK(PREPROCESSOR_DETAIL_BLOCK);
  RECORD(PPD_MACRO_EXPANSION);
  RECORD(PPD_MACRO_DEFINITION);
  RECORD(PPD_INCLUSION_DIRECTIVE);

  // Decls and Types block.
  BLOCK(EXTENSION_BLOCK);
  RECORD(EXTENSION_METADATA);

  BLOCK(UNHASHED_CONTROL_BLOCK);
  RECORD(SIGNATURE);
  RECORD(DIAGNOSTIC_OPTIONS);
  RECORD(DIAG_PRAGMA_MAPPINGS);

#undef RECORD
#undef BLOCK
  Stream.ExitBlock();
}

/// Prepares a path for being written to an AST file by converting it
/// to an absolute path and removing nested './'s.
///
/// \return \c true if the path was changed.
static bool cleanPathForOutput(FileManager &FileMgr,
                               SmallVectorImpl<char> &Path) {
  bool Changed = FileMgr.makeAbsolutePath(Path);
  return Changed | llvm::sys::path::remove_dots(Path);
}

/// Adjusts the given filename to only write out the portion of the
/// filename that is not part of the system root directory.
///
/// \param Filename the file name to adjust.
///
/// \param BaseDir When non-NULL, the PCH file is a relocatable AST file and
/// the returned filename will be adjusted by this root directory.
///
/// \returns either the original filename (if it needs no adjustment) or the
/// adjusted filename (which points into the @p Filename parameter).
static const char *
adjustFilenameForRelocatableAST(const char *Filename, StringRef BaseDir) {
  assert(Filename && "No file name to adjust?");

  if (BaseDir.empty())
    return Filename;

  // Verify that the filename and the system root have the same prefix.
  unsigned Pos = 0;
  for (; Filename[Pos] && Pos < BaseDir.size(); ++Pos)
    if (Filename[Pos] != BaseDir[Pos])
      return Filename; // Prefixes don't match.

  // We hit the end of the filename before we hit the end of the system root.
  if (!Filename[Pos])
    return Filename;

  // If there's not a path separator at the end of the base directory nor
  // immediately after it, then this isn't within the base directory.
  if (!llvm::sys::path::is_separator(Filename[Pos])) {
    if (!llvm::sys::path::is_separator(BaseDir.back()))
      return Filename;
  } else {
    // If the file name has a '/' at the current position, skip over the '/'.
    // We distinguish relative paths from absolute paths by the
    // absence of '/' at the beginning of relative paths.
    //
    // FIXME: This is wrong. We distinguish them by asking if the path is
    // absolute, which isn't the same thing. And there might be multiple '/'s
    // in a row. Use a better mechanism to indicate whether we have emitted an
    // absolute or relative path.
    ++Pos;
  }

  return Filename + Pos;
}

ASTFileSignature ASTWriter::createSignature(StringRef Bytes) {
  // Calculate the hash till start of UNHASHED_CONTROL_BLOCK.
  llvm::SHA1 Hasher;
  Hasher.update(ArrayRef<uint8_t>(Bytes.bytes_begin(), Bytes.size()));
  auto Hash = Hasher.result();

  // Convert to an array [5*i32].
  ASTFileSignature Signature;
  auto LShift = [&](unsigned char Val, unsigned Shift) {
    return (uint32_t)Val << Shift;
  };
  for (int I = 0; I != 5; ++I)
    Signature[I] = LShift(Hash[I * 4 + 0], 24) | LShift(Hash[I * 4 + 1], 16) |
                   LShift(Hash[I * 4 + 2], 8) | LShift(Hash[I * 4 + 3], 0);

  return Signature;
}

ASTFileSignature ASTWriter::writeUnhashedControlBlock(Preprocessor &PP,
                                                      ASTContext &Context) {
  // Flush first to prepare the PCM hash (signature).
  Stream.FlushToWord();
  auto StartOfUnhashedControl = Stream.GetCurrentBitNo() >> 3;

  // Enter the block and prepare to write records.
  RecordData Record;
  Stream.EnterSubblock(UNHASHED_CONTROL_BLOCK_ID, 5);

  // For implicit modules, write the hash of the PCM as its signature.
  ASTFileSignature Signature;
  if (WritingModule &&
      PP.getHeaderSearchInfo().getHeaderSearchOpts().ModulesHashContent) {
    Signature = createSignature(StringRef(Buffer.begin(), StartOfUnhashedControl));
    Record.append(Signature.begin(), Signature.end());
    Stream.EmitRecord(SIGNATURE, Record);
    Record.clear();
  }

  // Diagnostic options.
  const auto &Diags = Context.getDiagnostics();
  const DiagnosticOptions &DiagOpts = Diags.getDiagnosticOptions();
#define DIAGOPT(Name, Bits, Default) Record.push_back(DiagOpts.Name);
#define ENUM_DIAGOPT(Name, Type, Bits, Default)                                \
  Record.push_back(static_cast<unsigned>(DiagOpts.get##Name()));
#include "clang/Basic/DiagnosticOptions.def"
  Record.push_back(DiagOpts.Warnings.size());
  for (unsigned I = 0, N = DiagOpts.Warnings.size(); I != N; ++I)
    AddString(DiagOpts.Warnings[I], Record);
  Record.push_back(DiagOpts.Remarks.size());
  for (unsigned I = 0, N = DiagOpts.Remarks.size(); I != N; ++I)
    AddString(DiagOpts.Remarks[I], Record);
  // Note: we don't serialize the log or serialization file names, because they
  // are generally transient files and will almost always be overridden.
  Stream.EmitRecord(DIAGNOSTIC_OPTIONS, Record);

  // Write out the diagnostic/pragma mappings.
  WritePragmaDiagnosticMappings(Diags, /* IsModule = */ WritingModule);

  // Leave the options block.
  Stream.ExitBlock();
  return Signature;
}

/// Write the control block.
void ASTWriter::WriteControlBlock(Preprocessor &PP, ASTContext &Context,
                                  StringRef isysroot,
                                  const std::string &OutputFile) {
  using namespace llvm;

  Stream.EnterSubblock(CONTROL_BLOCK_ID, 5);
  RecordData Record;

  // Metadata
  auto MetadataAbbrev = std::make_shared<BitCodeAbbrev>();
  MetadataAbbrev->Add(BitCodeAbbrevOp(METADATA));
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Major
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Minor
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Clang maj.
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Clang min.
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Relocatable
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Timestamps
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // PCHHasObjectFile
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Errors
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // SVN branch/tag
  unsigned MetadataAbbrevCode = Stream.EmitAbbrev(std::move(MetadataAbbrev));
  assert((!WritingModule || isysroot.empty()) &&
         "writing module as a relocatable PCH?");
  {
    RecordData::value_type Record[] = {
        METADATA,
        VERSION_MAJOR,
        VERSION_MINOR,
        CLANG_VERSION_MAJOR,
        CLANG_VERSION_MINOR,
        !isysroot.empty(),
        IncludeTimestamps,
        Context.getLangOpts().BuildingPCHWithObjectFile,
        ASTHasCompilerErrors};
    Stream.EmitRecordWithBlob(MetadataAbbrevCode, Record,
                              getClangFullRepositoryVersion());
  }

  if (WritingModule) {
    // Module name
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(MODULE_NAME));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
    unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));
    RecordData::value_type Record[] = {MODULE_NAME};
    Stream.EmitRecordWithBlob(AbbrevCode, Record, WritingModule->Name);
  }

  if (WritingModule && WritingModule->Directory) {
    SmallString<128> BaseDir(WritingModule->Directory->getName());
    cleanPathForOutput(Context.getSourceManager().getFileManager(), BaseDir);

    // If the home of the module is the current working directory, then we
    // want to pick up the cwd of the build process loading the module, not
    // our cwd, when we load this module.
    if (!PP.getHeaderSearchInfo()
             .getHeaderSearchOpts()
             .ModuleMapFileHomeIsCwd ||
        WritingModule->Directory->getName() != StringRef(".")) {
      // Module directory.
      auto Abbrev = std::make_shared<BitCodeAbbrev>();
      Abbrev->Add(BitCodeAbbrevOp(MODULE_DIRECTORY));
      Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Directory
      unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));

      RecordData::value_type Record[] = {MODULE_DIRECTORY};
      Stream.EmitRecordWithBlob(AbbrevCode, Record, BaseDir);
    }

    // Write out all other paths relative to the base directory if possible.
    BaseDirectory.assign(BaseDir.begin(), BaseDir.end());
  } else if (!isysroot.empty()) {
    // Write out paths relative to the sysroot if possible.
    BaseDirectory = isysroot;
  }

  // Module map file
  if (WritingModule && WritingModule->Kind == Module::ModuleMapModule) {
    Record.clear();

    auto &Map = PP.getHeaderSearchInfo().getModuleMap();
    AddPath(WritingModule->PresumedModuleMapFile.empty()
                ? Map.getModuleMapFileForUniquing(WritingModule)->getName()
                : StringRef(WritingModule->PresumedModuleMapFile),
            Record);

    // Additional module map files.
    if (auto *AdditionalModMaps =
            Map.getAdditionalModuleMapFiles(WritingModule)) {
      Record.push_back(AdditionalModMaps->size());
      for (const FileEntry *F : *AdditionalModMaps)
        AddPath(F->getName(), Record);
    } else {
      Record.push_back(0);
    }

    Stream.EmitRecord(MODULE_MAP_FILE, Record);
  }

  // Imports
  if (Chain) {
    serialization::ModuleManager &Mgr = Chain->getModuleManager();
    Record.clear();

    for (ModuleFile &M : Mgr) {
      // Skip modules that weren't directly imported.
      if (!M.isDirectlyImported())
        continue;

      Record.push_back((unsigned)M.Kind); // FIXME: Stable encoding
      AddSourceLocation(M.ImportLoc, Record);

      // If we have calculated signature, there is no need to store
      // the size or timestamp.
      Record.push_back(M.Signature ? 0 : M.File->getSize());
      Record.push_back(M.Signature ? 0 : getTimestampForOutput(M.File));

      for (auto I : M.Signature)
        Record.push_back(I);

      AddString(M.ModuleName, Record);
      AddPath(M.FileName, Record);
    }
    Stream.EmitRecord(IMPORTS, Record);
  }

  // Write the options block.
  Stream.EnterSubblock(OPTIONS_BLOCK_ID, 4);

  // Language options.
  Record.clear();
  const LangOptions &LangOpts = Context.getLangOpts();
#define LANGOPT(Name, Bits, Default, Description) \
  Record.push_back(LangOpts.Name);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
  Record.push_back(static_cast<unsigned>(LangOpts.get##Name()));
#include "clang/Basic/LangOptions.def"
#define SANITIZER(NAME, ID)                                                    \
  Record.push_back(LangOpts.Sanitize.has(SanitizerKind::ID));
#include "clang/Basic/Sanitizers.def"

  Record.push_back(LangOpts.ModuleFeatures.size());
  for (StringRef Feature : LangOpts.ModuleFeatures)
    AddString(Feature, Record);

  Record.push_back((unsigned) LangOpts.ObjCRuntime.getKind());
  AddVersionTuple(LangOpts.ObjCRuntime.getVersion(), Record);

  AddString(LangOpts.CurrentModule, Record);

  // Comment options.
  Record.push_back(LangOpts.CommentOpts.BlockCommandNames.size());
  for (const auto &I : LangOpts.CommentOpts.BlockCommandNames) {
    AddString(I, Record);
  }
  Record.push_back(LangOpts.CommentOpts.ParseAllComments);

  // OpenMP offloading options.
  Record.push_back(LangOpts.OMPTargetTriples.size());
  for (auto &T : LangOpts.OMPTargetTriples)
    AddString(T.getTriple(), Record);

  AddString(LangOpts.OMPHostIRFile, Record);

  Stream.EmitRecord(LANGUAGE_OPTIONS, Record);

  // Target options.
  Record.clear();
  const TargetInfo &Target = Context.getTargetInfo();
  const TargetOptions &TargetOpts = Target.getTargetOpts();
  AddString(TargetOpts.Triple, Record);
  AddString(TargetOpts.CPU, Record);
  AddString(TargetOpts.ABI, Record);
  Record.push_back(TargetOpts.FeaturesAsWritten.size());
  for (unsigned I = 0, N = TargetOpts.FeaturesAsWritten.size(); I != N; ++I) {
    AddString(TargetOpts.FeaturesAsWritten[I], Record);
  }
  Record.push_back(TargetOpts.Features.size());
  for (unsigned I = 0, N = TargetOpts.Features.size(); I != N; ++I) {
    AddString(TargetOpts.Features[I], Record);
  }
  Stream.EmitRecord(TARGET_OPTIONS, Record);

  // File system options.
  Record.clear();
  const FileSystemOptions &FSOpts =
      Context.getSourceManager().getFileManager().getFileSystemOpts();
  AddString(FSOpts.WorkingDir, Record);
  Stream.EmitRecord(FILE_SYSTEM_OPTIONS, Record);

  // Header search options.
  Record.clear();
  const HeaderSearchOptions &HSOpts
    = PP.getHeaderSearchInfo().getHeaderSearchOpts();
  AddString(HSOpts.Sysroot, Record);

  // Include entries.
  Record.push_back(HSOpts.UserEntries.size());
  for (unsigned I = 0, N = HSOpts.UserEntries.size(); I != N; ++I) {
    const HeaderSearchOptions::Entry &Entry = HSOpts.UserEntries[I];
    AddString(Entry.Path, Record);
    Record.push_back(static_cast<unsigned>(Entry.Group));
    Record.push_back(Entry.IsFramework);
    Record.push_back(Entry.IgnoreSysRoot);
  }

  // System header prefixes.
  Record.push_back(HSOpts.SystemHeaderPrefixes.size());
  for (unsigned I = 0, N = HSOpts.SystemHeaderPrefixes.size(); I != N; ++I) {
    AddString(HSOpts.SystemHeaderPrefixes[I].Prefix, Record);
    Record.push_back(HSOpts.SystemHeaderPrefixes[I].IsSystemHeader);
  }

  AddString(HSOpts.ResourceDir, Record);
  AddString(HSOpts.ModuleCachePath, Record);
  AddString(HSOpts.ModuleUserBuildPath, Record);
  Record.push_back(HSOpts.DisableModuleHash);
  Record.push_back(HSOpts.ImplicitModuleMaps);
  Record.push_back(HSOpts.ModuleMapFileHomeIsCwd);
  Record.push_back(HSOpts.UseBuiltinIncludes);
  Record.push_back(HSOpts.UseStandardSystemIncludes);
  Record.push_back(HSOpts.UseStandardCXXIncludes);
  Record.push_back(HSOpts.UseLibcxx);
  // Write out the specific module cache path that contains the module files.
  AddString(PP.getHeaderSearchInfo().getModuleCachePath(), Record);
  Stream.EmitRecord(HEADER_SEARCH_OPTIONS, Record);

  // Preprocessor options.
  Record.clear();
  const PreprocessorOptions &PPOpts = PP.getPreprocessorOpts();

  // Macro definitions.
  Record.push_back(PPOpts.Macros.size());
  for (unsigned I = 0, N = PPOpts.Macros.size(); I != N; ++I) {
    AddString(PPOpts.Macros[I].first, Record);
    Record.push_back(PPOpts.Macros[I].second);
  }

  // Includes
  Record.push_back(PPOpts.Includes.size());
  for (unsigned I = 0, N = PPOpts.Includes.size(); I != N; ++I)
    AddString(PPOpts.Includes[I], Record);

  // Macro includes
  Record.push_back(PPOpts.MacroIncludes.size());
  for (unsigned I = 0, N = PPOpts.MacroIncludes.size(); I != N; ++I)
    AddString(PPOpts.MacroIncludes[I], Record);

  Record.push_back(PPOpts.UsePredefines);
  // Detailed record is important since it is used for the module cache hash.
  Record.push_back(PPOpts.DetailedRecord);
  AddString(PPOpts.ImplicitPCHInclude, Record);
  Record.push_back(static_cast<unsigned>(PPOpts.ObjCXXARCStandardLibrary));
  Stream.EmitRecord(PREPROCESSOR_OPTIONS, Record);

  // Leave the options block.
  Stream.ExitBlock();

  // Original file name and file ID
  SourceManager &SM = Context.getSourceManager();
  if (const FileEntry *MainFile = SM.getFileEntryForID(SM.getMainFileID())) {
    auto FileAbbrev = std::make_shared<BitCodeAbbrev>();
    FileAbbrev->Add(BitCodeAbbrevOp(ORIGINAL_FILE));
    FileAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // File ID
    FileAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // File name
    unsigned FileAbbrevCode = Stream.EmitAbbrev(std::move(FileAbbrev));

    Record.clear();
    Record.push_back(ORIGINAL_FILE);
    Record.push_back(SM.getMainFileID().getOpaqueValue());
    EmitRecordWithPath(FileAbbrevCode, Record, MainFile->getName());
  }

  Record.clear();
  Record.push_back(SM.getMainFileID().getOpaqueValue());
  Stream.EmitRecord(ORIGINAL_FILE_ID, Record);

  // Original PCH directory
  if (!OutputFile.empty() && OutputFile != "-") {
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(ORIGINAL_PCH_DIR));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // File name
    unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));

    SmallString<128> OutputPath(OutputFile);

    SM.getFileManager().makeAbsolutePath(OutputPath);
    StringRef origDir = llvm::sys::path::parent_path(OutputPath);

    RecordData::value_type Record[] = {ORIGINAL_PCH_DIR};
    Stream.EmitRecordWithBlob(AbbrevCode, Record, origDir);
  }

  WriteInputFiles(Context.SourceMgr,
                  PP.getHeaderSearchInfo().getHeaderSearchOpts(),
                  PP.getLangOpts().Modules);
  Stream.ExitBlock();
}

namespace  {

/// An input file.
struct InputFileEntry {
  const FileEntry *File;
  bool IsSystemFile;
  bool IsTransient;
  bool BufferOverridden;
  bool IsTopLevelModuleMap;
};

} // namespace

void ASTWriter::WriteInputFiles(SourceManager &SourceMgr,
                                HeaderSearchOptions &HSOpts,
                                bool Modules) {
  using namespace llvm;

  Stream.EnterSubblock(INPUT_FILES_BLOCK_ID, 4);

  // Create input-file abbreviation.
  auto IFAbbrev = std::make_shared<BitCodeAbbrev>();
  IFAbbrev->Add(BitCodeAbbrevOp(INPUT_FILE));
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ID
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 12)); // Size
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 32)); // Modification time
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Overridden
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Transient
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Module map
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // File name
  unsigned IFAbbrevCode = Stream.EmitAbbrev(std::move(IFAbbrev));

  // Get all ContentCache objects for files, sorted by whether the file is a
  // system one or not. System files go at the back, users files at the front.
  std::deque<InputFileEntry> SortedFiles;
  for (unsigned I = 1, N = SourceMgr.local_sloc_entry_size(); I != N; ++I) {
    // Get this source location entry.
    const SrcMgr::SLocEntry *SLoc = &SourceMgr.getLocalSLocEntry(I);
    assert(&SourceMgr.getSLocEntry(FileID::get(I)) == SLoc);

    // We only care about file entries that were not overridden.
    if (!SLoc->isFile())
      continue;
    const SrcMgr::FileInfo &File = SLoc->getFile();
    const SrcMgr::ContentCache *Cache = File.getContentCache();
    if (!Cache->OrigEntry)
      continue;

    InputFileEntry Entry;
    Entry.File = Cache->OrigEntry;
    Entry.IsSystemFile = Cache->IsSystemFile;
    Entry.IsTransient = Cache->IsTransient;
    Entry.BufferOverridden = Cache->BufferOverridden;
    Entry.IsTopLevelModuleMap = isModuleMap(File.getFileCharacteristic()) &&
                                File.getIncludeLoc().isInvalid();
    if (Cache->IsSystemFile)
      SortedFiles.push_back(Entry);
    else
      SortedFiles.push_front(Entry);
  }

  unsigned UserFilesNum = 0;
  // Write out all of the input files.
  std::vector<uint64_t> InputFileOffsets;
  for (const auto &Entry : SortedFiles) {
    uint32_t &InputFileID = InputFileIDs[Entry.File];
    if (InputFileID != 0)
      continue; // already recorded this file.

    // Record this entry's offset.
    InputFileOffsets.push_back(Stream.GetCurrentBitNo());

    InputFileID = InputFileOffsets.size();

    if (!Entry.IsSystemFile)
      ++UserFilesNum;

    // Emit size/modification time for this file.
    // And whether this file was overridden.
    RecordData::value_type Record[] = {
        INPUT_FILE,
        InputFileOffsets.size(),
        (uint64_t)Entry.File->getSize(),
        (uint64_t)getTimestampForOutput(Entry.File),
        Entry.BufferOverridden,
        Entry.IsTransient,
        Entry.IsTopLevelModuleMap};

    EmitRecordWithPath(IFAbbrevCode, Record, Entry.File->getName());
  }

  Stream.ExitBlock();

  // Create input file offsets abbreviation.
  auto OffsetsAbbrev = std::make_shared<BitCodeAbbrev>();
  OffsetsAbbrev->Add(BitCodeAbbrevOp(INPUT_FILE_OFFSETS));
  OffsetsAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # input files
  OffsetsAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # non-system
                                                                //   input files
  OffsetsAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));   // Array
  unsigned OffsetsAbbrevCode = Stream.EmitAbbrev(std::move(OffsetsAbbrev));

  // Write input file offsets.
  RecordData::value_type Record[] = {INPUT_FILE_OFFSETS,
                                     InputFileOffsets.size(), UserFilesNum};
  Stream.EmitRecordWithBlob(OffsetsAbbrevCode, Record, bytes(InputFileOffsets));
}

//===----------------------------------------------------------------------===//
// Source Manager Serialization
//===----------------------------------------------------------------------===//

/// Create an abbreviation for the SLocEntry that refers to a
/// file.
static unsigned CreateSLocFileAbbrev(llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SM_SLOC_FILE_ENTRY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Include location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Characteristic
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Line directives
  // FileEntry fields.
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Input File ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // NumCreatedFIDs
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 24)); // FirstDeclIndex
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // NumDecls
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Create an abbreviation for the SLocEntry that refers to a
/// buffer.
static unsigned CreateSLocBufferAbbrev(llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SM_SLOC_BUFFER_ENTRY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Include location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Characteristic
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Line directives
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Buffer name blob
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Create an abbreviation for the SLocEntry that refers to a
/// buffer's blob.
static unsigned CreateSLocBufferBlobAbbrev(llvm::BitstreamWriter &Stream,
                                           bool Compressed) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(Compressed ? SM_SLOC_BUFFER_BLOB_COMPRESSED
                                         : SM_SLOC_BUFFER_BLOB));
  if (Compressed)
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Uncompressed size
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Blob
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Create an abbreviation for the SLocEntry that refers to a macro
/// expansion.
static unsigned CreateSLocExpansionAbbrev(llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SM_SLOC_EXPANSION_ENTRY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Spelling location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Start location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // End location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Is token range
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Token length
  return Stream.EmitAbbrev(std::move(Abbrev));
}

namespace {

  // Trait used for the on-disk hash table of header search information.
  class HeaderFileInfoTrait {
    ASTWriter &Writer;

    // Keep track of the framework names we've used during serialization.
    SmallVector<char, 128> FrameworkStringData;
    llvm::StringMap<unsigned> FrameworkNameOffset;

  public:
    HeaderFileInfoTrait(ASTWriter &Writer) : Writer(Writer) {}

    struct key_type {
      StringRef Filename;
      off_t Size;
      time_t ModTime;
    };
    using key_type_ref = const key_type &;

    using UnresolvedModule =
        llvm::PointerIntPair<Module *, 2, ModuleMap::ModuleHeaderRole>;

    struct data_type {
      const HeaderFileInfo &HFI;
      ArrayRef<ModuleMap::KnownHeader> KnownHeaders;
      UnresolvedModule Unresolved;
    };
    using data_type_ref = const data_type &;

    using hash_value_type = unsigned;
    using offset_type = unsigned;

    hash_value_type ComputeHash(key_type_ref key) {
      // The hash is based only on size/time of the file, so that the reader can
      // match even when symlinking or excess path elements ("foo/../", "../")
      // change the form of the name. However, complete path is still the key.
      return llvm::hash_combine(key.Size, key.ModTime);
    }

    std::pair<unsigned, unsigned>
    EmitKeyDataLength(raw_ostream& Out, key_type_ref key, data_type_ref Data) {
      using namespace llvm::support;

      endian::Writer LE(Out, little);
      unsigned KeyLen = key.Filename.size() + 1 + 8 + 8;
      LE.write<uint16_t>(KeyLen);
      unsigned DataLen = 1 + 2 + 4 + 4;
      for (auto ModInfo : Data.KnownHeaders)
        if (Writer.getLocalOrImportedSubmoduleID(ModInfo.getModule()))
          DataLen += 4;
      if (Data.Unresolved.getPointer())
        DataLen += 4;
      LE.write<uint8_t>(DataLen);
      return std::make_pair(KeyLen, DataLen);
    }

    void EmitKey(raw_ostream& Out, key_type_ref key, unsigned KeyLen) {
      using namespace llvm::support;

      endian::Writer LE(Out, little);
      LE.write<uint64_t>(key.Size);
      KeyLen -= 8;
      LE.write<uint64_t>(key.ModTime);
      KeyLen -= 8;
      Out.write(key.Filename.data(), KeyLen);
    }

    void EmitData(raw_ostream &Out, key_type_ref key,
                  data_type_ref Data, unsigned DataLen) {
      using namespace llvm::support;

      endian::Writer LE(Out, little);
      uint64_t Start = Out.tell(); (void)Start;

      unsigned char Flags = (Data.HFI.isImport << 5)
                          | (Data.HFI.isPragmaOnce << 4)
                          | (Data.HFI.DirInfo << 1)
                          | Data.HFI.IndexHeaderMapHeader;
      LE.write<uint8_t>(Flags);
      LE.write<uint16_t>(Data.HFI.NumIncludes);

      if (!Data.HFI.ControllingMacro)
        LE.write<uint32_t>(Data.HFI.ControllingMacroID);
      else
        LE.write<uint32_t>(Writer.getIdentifierRef(Data.HFI.ControllingMacro));

      unsigned Offset = 0;
      if (!Data.HFI.Framework.empty()) {
        // If this header refers into a framework, save the framework name.
        llvm::StringMap<unsigned>::iterator Pos
          = FrameworkNameOffset.find(Data.HFI.Framework);
        if (Pos == FrameworkNameOffset.end()) {
          Offset = FrameworkStringData.size() + 1;
          FrameworkStringData.append(Data.HFI.Framework.begin(),
                                     Data.HFI.Framework.end());
          FrameworkStringData.push_back(0);

          FrameworkNameOffset[Data.HFI.Framework] = Offset;
        } else
          Offset = Pos->second;
      }
      LE.write<uint32_t>(Offset);

      auto EmitModule = [&](Module *M, ModuleMap::ModuleHeaderRole Role) {
        if (uint32_t ModID = Writer.getLocalOrImportedSubmoduleID(M)) {
          uint32_t Value = (ModID << 2) | (unsigned)Role;
          assert((Value >> 2) == ModID && "overflow in header module info");
          LE.write<uint32_t>(Value);
        }
      };

      // FIXME: If the header is excluded, we should write out some
      // record of that fact.
      for (auto ModInfo : Data.KnownHeaders)
        EmitModule(ModInfo.getModule(), ModInfo.getRole());
      if (Data.Unresolved.getPointer())
        EmitModule(Data.Unresolved.getPointer(), Data.Unresolved.getInt());

      assert(Out.tell() - Start == DataLen && "Wrong data length");
    }

    const char *strings_begin() const { return FrameworkStringData.begin(); }
    const char *strings_end() const { return FrameworkStringData.end(); }
  };

} // namespace

/// Write the header search block for the list of files that
///
/// \param HS The header search structure to save.
void ASTWriter::WriteHeaderSearch(const HeaderSearch &HS) {
  HeaderFileInfoTrait GeneratorTrait(*this);
  llvm::OnDiskChainedHashTableGenerator<HeaderFileInfoTrait> Generator;
  SmallVector<const char *, 4> SavedStrings;
  unsigned NumHeaderSearchEntries = 0;

  // Find all unresolved headers for the current module. We generally will
  // have resolved them before we get here, but not necessarily: we might be
  // compiling a preprocessed module, where there is no requirement for the
  // original files to exist any more.
  const HeaderFileInfo Empty; // So we can take a reference.
  if (WritingModule) {
    llvm::SmallVector<Module *, 16> Worklist(1, WritingModule);
    while (!Worklist.empty()) {
      Module *M = Worklist.pop_back_val();
      if (!M->isAvailable())
        continue;

      // Map to disk files where possible, to pick up any missing stat
      // information. This also means we don't need to check the unresolved
      // headers list when emitting resolved headers in the first loop below.
      // FIXME: It'd be preferable to avoid doing this if we were given
      // sufficient stat information in the module map.
      HS.getModuleMap().resolveHeaderDirectives(M);

      // If the file didn't exist, we can still create a module if we were given
      // enough information in the module map.
      for (auto U : M->MissingHeaders) {
        // Check that we were given enough information to build a module
        // without this file existing on disk.
        if (!U.Size || (!U.ModTime && IncludeTimestamps)) {
          PP->Diag(U.FileNameLoc, diag::err_module_no_size_mtime_for_header)
            << WritingModule->getFullModuleName() << U.Size.hasValue()
            << U.FileName;
          continue;
        }

        // Form the effective relative pathname for the file.
        SmallString<128> Filename(M->Directory->getName());
        llvm::sys::path::append(Filename, U.FileName);
        PreparePathForOutput(Filename);

        StringRef FilenameDup = strdup(Filename.c_str());
        SavedStrings.push_back(FilenameDup.data());

        HeaderFileInfoTrait::key_type Key = {
          FilenameDup, *U.Size, IncludeTimestamps ? *U.ModTime : 0
        };
        HeaderFileInfoTrait::data_type Data = {
          Empty, {}, {M, ModuleMap::headerKindToRole(U.Kind)}
        };
        // FIXME: Deal with cases where there are multiple unresolved header
        // directives in different submodules for the same header.
        Generator.insert(Key, Data, GeneratorTrait);
        ++NumHeaderSearchEntries;
      }

      Worklist.append(M->submodule_begin(), M->submodule_end());
    }
  }

  SmallVector<const FileEntry *, 16> FilesByUID;
  HS.getFileMgr().GetUniqueIDMapping(FilesByUID);

  if (FilesByUID.size() > HS.header_file_size())
    FilesByUID.resize(HS.header_file_size());

  for (unsigned UID = 0, LastUID = FilesByUID.size(); UID != LastUID; ++UID) {
    const FileEntry *File = FilesByUID[UID];
    if (!File)
      continue;

    // Get the file info. This will load info from the external source if
    // necessary. Skip emitting this file if we have no information on it
    // as a header file (in which case HFI will be null) or if it hasn't
    // changed since it was loaded. Also skip it if it's for a modular header
    // from a different module; in that case, we rely on the module(s)
    // containing the header to provide this information.
    const HeaderFileInfo *HFI =
        HS.getExistingFileInfo(File, /*WantExternal*/!Chain);
    if (!HFI || (HFI->isModuleHeader && !HFI->isCompilingModuleHeader))
      continue;

    // Massage the file path into an appropriate form.
    StringRef Filename = File->getName();
    SmallString<128> FilenameTmp(Filename);
    if (PreparePathForOutput(FilenameTmp)) {
      // If we performed any translation on the file name at all, we need to
      // save this string, since the generator will refer to it later.
      Filename = StringRef(strdup(FilenameTmp.c_str()));
      SavedStrings.push_back(Filename.data());
    }

    HeaderFileInfoTrait::key_type Key = {
      Filename, File->getSize(), getTimestampForOutput(File)
    };
    HeaderFileInfoTrait::data_type Data = {
      *HFI, HS.getModuleMap().findAllModulesForHeader(File), {}
    };
    Generator.insert(Key, Data, GeneratorTrait);
    ++NumHeaderSearchEntries;
  }

  // Create the on-disk hash table in a buffer.
  SmallString<4096> TableData;
  uint32_t BucketOffset;
  {
    using namespace llvm::support;

    llvm::raw_svector_ostream Out(TableData);
    // Make sure that no bucket is at offset 0
    endian::write<uint32_t>(Out, 0, little);
    BucketOffset = Generator.Emit(Out, GeneratorTrait);
  }

  // Create a blob abbreviation
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(HEADER_SEARCH_TABLE));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned TableAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  // Write the header search table
  RecordData::value_type Record[] = {HEADER_SEARCH_TABLE, BucketOffset,
                                     NumHeaderSearchEntries, TableData.size()};
  TableData.append(GeneratorTrait.strings_begin(),GeneratorTrait.strings_end());
  Stream.EmitRecordWithBlob(TableAbbrev, Record, TableData);

  // Free all of the strings we had to duplicate.
  for (unsigned I = 0, N = SavedStrings.size(); I != N; ++I)
    free(const_cast<char *>(SavedStrings[I]));
}

static void emitBlob(llvm::BitstreamWriter &Stream, StringRef Blob,
                     unsigned SLocBufferBlobCompressedAbbrv,
                     unsigned SLocBufferBlobAbbrv) {
  using RecordDataType = ASTWriter::RecordData::value_type;

  // Compress the buffer if possible. We expect that almost all PCM
  // consumers will not want its contents.
  SmallString<0> CompressedBuffer;
  if (llvm::zlib::isAvailable()) {
    llvm::Error E = llvm::zlib::compress(Blob.drop_back(1), CompressedBuffer);
    if (!E) {
      RecordDataType Record[] = {SM_SLOC_BUFFER_BLOB_COMPRESSED,
                                 Blob.size() - 1};
      Stream.EmitRecordWithBlob(SLocBufferBlobCompressedAbbrv, Record,
                                CompressedBuffer);
      return;
    }
    llvm::consumeError(std::move(E));
  }

  RecordDataType Record[] = {SM_SLOC_BUFFER_BLOB};
  Stream.EmitRecordWithBlob(SLocBufferBlobAbbrv, Record, Blob);
}

/// Writes the block containing the serialized form of the
/// source manager.
///
/// TODO: We should probably use an on-disk hash table (stored in a
/// blob), indexed based on the file name, so that we only create
/// entries for files that we actually need. In the common case (no
/// errors), we probably won't have to create file entries for any of
/// the files in the AST.
void ASTWriter::WriteSourceManagerBlock(SourceManager &SourceMgr,
                                        const Preprocessor &PP) {
  RecordData Record;

  // Enter the source manager block.
  Stream.EnterSubblock(SOURCE_MANAGER_BLOCK_ID, 4);

  // Abbreviations for the various kinds of source-location entries.
  unsigned SLocFileAbbrv = CreateSLocFileAbbrev(Stream);
  unsigned SLocBufferAbbrv = CreateSLocBufferAbbrev(Stream);
  unsigned SLocBufferBlobAbbrv = CreateSLocBufferBlobAbbrev(Stream, false);
  unsigned SLocBufferBlobCompressedAbbrv =
      CreateSLocBufferBlobAbbrev(Stream, true);
  unsigned SLocExpansionAbbrv = CreateSLocExpansionAbbrev(Stream);

  // Write out the source location entry table. We skip the first
  // entry, which is always the same dummy entry.
  std::vector<uint32_t> SLocEntryOffsets;
  RecordData PreloadSLocs;
  SLocEntryOffsets.reserve(SourceMgr.local_sloc_entry_size() - 1);
  for (unsigned I = 1, N = SourceMgr.local_sloc_entry_size();
       I != N; ++I) {
    // Get this source location entry.
    const SrcMgr::SLocEntry *SLoc = &SourceMgr.getLocalSLocEntry(I);
    FileID FID = FileID::get(I);
    assert(&SourceMgr.getSLocEntry(FID) == SLoc);

    // Record the offset of this source-location entry.
    SLocEntryOffsets.push_back(Stream.GetCurrentBitNo());

    // Figure out which record code to use.
    unsigned Code;
    if (SLoc->isFile()) {
      const SrcMgr::ContentCache *Cache = SLoc->getFile().getContentCache();
      if (Cache->OrigEntry) {
        Code = SM_SLOC_FILE_ENTRY;
      } else
        Code = SM_SLOC_BUFFER_ENTRY;
    } else
      Code = SM_SLOC_EXPANSION_ENTRY;
    Record.clear();
    Record.push_back(Code);

    // Starting offset of this entry within this module, so skip the dummy.
    Record.push_back(SLoc->getOffset() - 2);
    if (SLoc->isFile()) {
      const SrcMgr::FileInfo &File = SLoc->getFile();
      AddSourceLocation(File.getIncludeLoc(), Record);
      Record.push_back(File.getFileCharacteristic()); // FIXME: stable encoding
      Record.push_back(File.hasLineDirectives());

      const SrcMgr::ContentCache *Content = File.getContentCache();
      bool EmitBlob = false;
      if (Content->OrigEntry) {
        assert(Content->OrigEntry == Content->ContentsEntry &&
               "Writing to AST an overridden file is not supported");

        // The source location entry is a file. Emit input file ID.
        assert(InputFileIDs[Content->OrigEntry] != 0 && "Missed file entry");
        Record.push_back(InputFileIDs[Content->OrigEntry]);

        Record.push_back(File.NumCreatedFIDs);

        FileDeclIDsTy::iterator FDI = FileDeclIDs.find(FID);
        if (FDI != FileDeclIDs.end()) {
          Record.push_back(FDI->second->FirstDeclIndex);
          Record.push_back(FDI->second->DeclIDs.size());
        } else {
          Record.push_back(0);
          Record.push_back(0);
        }

        Stream.EmitRecordWithAbbrev(SLocFileAbbrv, Record);

        if (Content->BufferOverridden || Content->IsTransient)
          EmitBlob = true;
      } else {
        // The source location entry is a buffer. The blob associated
        // with this entry contains the contents of the buffer.

        // We add one to the size so that we capture the trailing NULL
        // that is required by llvm::MemoryBuffer::getMemBuffer (on
        // the reader side).
        const llvm::MemoryBuffer *Buffer
          = Content->getBuffer(PP.getDiagnostics(), PP.getSourceManager());
        StringRef Name = Buffer->getBufferIdentifier();
        Stream.EmitRecordWithBlob(SLocBufferAbbrv, Record,
                                  StringRef(Name.data(), Name.size() + 1));
        EmitBlob = true;

        if (Name == "<built-in>")
          PreloadSLocs.push_back(SLocEntryOffsets.size());
      }

      if (EmitBlob) {
        // Include the implicit terminating null character in the on-disk buffer
        // if we're writing it uncompressed.
        const llvm::MemoryBuffer *Buffer =
            Content->getBuffer(PP.getDiagnostics(), PP.getSourceManager());
        StringRef Blob(Buffer->getBufferStart(), Buffer->getBufferSize() + 1);
        emitBlob(Stream, Blob, SLocBufferBlobCompressedAbbrv,
                 SLocBufferBlobAbbrv);
      }
    } else {
      // The source location entry is a macro expansion.
      const SrcMgr::ExpansionInfo &Expansion = SLoc->getExpansion();
      AddSourceLocation(Expansion.getSpellingLoc(), Record);
      AddSourceLocation(Expansion.getExpansionLocStart(), Record);
      AddSourceLocation(Expansion.isMacroArgExpansion()
                            ? SourceLocation()
                            : Expansion.getExpansionLocEnd(),
                        Record);
      Record.push_back(Expansion.isExpansionTokenRange());

      // Compute the token length for this macro expansion.
      unsigned NextOffset = SourceMgr.getNextLocalOffset();
      if (I + 1 != N)
        NextOffset = SourceMgr.getLocalSLocEntry(I + 1).getOffset();
      Record.push_back(NextOffset - SLoc->getOffset() - 1);
      Stream.EmitRecordWithAbbrev(SLocExpansionAbbrv, Record);
    }
  }

  Stream.ExitBlock();

  if (SLocEntryOffsets.empty())
    return;

  // Write the source-location offsets table into the AST block. This
  // table is used for lazily loading source-location information.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SOURCE_LOCATION_OFFSETS));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 16)); // # of slocs
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 16)); // total size
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // offsets
  unsigned SLocOffsetsAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {
        SOURCE_LOCATION_OFFSETS, SLocEntryOffsets.size(),
        SourceMgr.getNextLocalOffset() - 1 /* skip dummy */};
    Stream.EmitRecordWithBlob(SLocOffsetsAbbrev, Record,
                              bytes(SLocEntryOffsets));
  }
  // Write the source location entry preloads array, telling the AST
  // reader which source locations entries it should load eagerly.
  Stream.EmitRecord(SOURCE_LOCATION_PRELOADS, PreloadSLocs);

  // Write the line table. It depends on remapping working, so it must come
  // after the source location offsets.
  if (SourceMgr.hasLineTable()) {
    LineTableInfo &LineTable = SourceMgr.getLineTable();

    Record.clear();

    // Emit the needed file names.
    llvm::DenseMap<int, int> FilenameMap;
    FilenameMap[-1] = -1; // For unspecified filenames.
    for (const auto &L : LineTable) {
      if (L.first.ID < 0)
        continue;
      for (auto &LE : L.second) {
        if (FilenameMap.insert(std::make_pair(LE.FilenameID,
                                              FilenameMap.size() - 1)).second)
          AddPath(LineTable.getFilename(LE.FilenameID), Record);
      }
    }
    Record.push_back(0);

    // Emit the line entries
    for (const auto &L : LineTable) {
      // Only emit entries for local files.
      if (L.first.ID < 0)
        continue;

      // Emit the file ID
      Record.push_back(L.first.ID);

      // Emit the line entries
      Record.push_back(L.second.size());
      for (const auto &LE : L.second) {
        Record.push_back(LE.FileOffset);
        Record.push_back(LE.LineNo);
        Record.push_back(FilenameMap[LE.FilenameID]);
        Record.push_back((unsigned)LE.FileKind);
        Record.push_back(LE.IncludeOffset);
      }
    }

    Stream.EmitRecord(SOURCE_MANAGER_LINE_TABLE, Record);
  }
}

//===----------------------------------------------------------------------===//
// Preprocessor Serialization
//===----------------------------------------------------------------------===//

static bool shouldIgnoreMacro(MacroDirective *MD, bool IsModule,
                              const Preprocessor &PP) {
  if (MacroInfo *MI = MD->getMacroInfo())
    if (MI->isBuiltinMacro())
      return true;

  if (IsModule) {
    SourceLocation Loc = MD->getLocation();
    if (Loc.isInvalid())
      return true;
    if (PP.getSourceManager().getFileID(Loc) == PP.getPredefinesFileID())
      return true;
  }

  return false;
}

/// Writes the block containing the serialized form of the
/// preprocessor.
void ASTWriter::WritePreprocessor(const Preprocessor &PP, bool IsModule) {
  PreprocessingRecord *PPRec = PP.getPreprocessingRecord();
  if (PPRec)
    WritePreprocessorDetail(*PPRec);

  RecordData Record;
  RecordData ModuleMacroRecord;

  // If the preprocessor __COUNTER__ value has been bumped, remember it.
  if (PP.getCounterValue() != 0) {
    RecordData::value_type Record[] = {PP.getCounterValue()};
    Stream.EmitRecord(PP_COUNTER_VALUE, Record);
  }

  if (PP.isRecordingPreamble() && PP.hasRecordedPreamble()) {
    assert(!IsModule);
    auto SkipInfo = PP.getPreambleSkipInfo();
    if (SkipInfo.hasValue()) {
      Record.push_back(true);
      AddSourceLocation(SkipInfo->HashTokenLoc, Record);
      AddSourceLocation(SkipInfo->IfTokenLoc, Record);
      Record.push_back(SkipInfo->FoundNonSkipPortion);
      Record.push_back(SkipInfo->FoundElse);
      AddSourceLocation(SkipInfo->ElseLoc, Record);
    } else {
      Record.push_back(false);
    }
    for (const auto &Cond : PP.getPreambleConditionalStack()) {
      AddSourceLocation(Cond.IfLoc, Record);
      Record.push_back(Cond.WasSkipping);
      Record.push_back(Cond.FoundNonSkip);
      Record.push_back(Cond.FoundElse);
    }
    Stream.EmitRecord(PP_CONDITIONAL_STACK, Record);
    Record.clear();
  }

  // Enter the preprocessor block.
  Stream.EnterSubblock(PREPROCESSOR_BLOCK_ID, 3);

  // If the AST file contains __DATE__ or __TIME__ emit a warning about this.
  // FIXME: Include a location for the use, and say which one was used.
  if (PP.SawDateOrTime())
    PP.Diag(SourceLocation(), diag::warn_module_uses_date_time) << IsModule;

  // Loop over all the macro directives that are live at the end of the file,
  // emitting each to the PP section.

  // Construct the list of identifiers with macro directives that need to be
  // serialized.
  SmallVector<const IdentifierInfo *, 128> MacroIdentifiers;
  for (auto &Id : PP.getIdentifierTable())
    if (Id.second->hadMacroDefinition() &&
        (!Id.second->isFromAST() ||
         Id.second->hasChangedSinceDeserialization()))
      MacroIdentifiers.push_back(Id.second);
  // Sort the set of macro definitions that need to be serialized by the
  // name of the macro, to provide a stable ordering.
  llvm::sort(MacroIdentifiers, llvm::less_ptr<IdentifierInfo>());

  // Emit the macro directives as a list and associate the offset with the
  // identifier they belong to.
  for (const IdentifierInfo *Name : MacroIdentifiers) {
    MacroDirective *MD = PP.getLocalMacroDirectiveHistory(Name);
    auto StartOffset = Stream.GetCurrentBitNo();

    // Emit the macro directives in reverse source order.
    for (; MD; MD = MD->getPrevious()) {
      // Once we hit an ignored macro, we're done: the rest of the chain
      // will all be ignored macros.
      if (shouldIgnoreMacro(MD, IsModule, PP))
        break;

      AddSourceLocation(MD->getLocation(), Record);
      Record.push_back(MD->getKind());
      if (auto *DefMD = dyn_cast<DefMacroDirective>(MD)) {
        Record.push_back(getMacroRef(DefMD->getInfo(), Name));
      } else if (auto *VisMD = dyn_cast<VisibilityMacroDirective>(MD)) {
        Record.push_back(VisMD->isPublic());
      }
    }

    // Write out any exported module macros.
    bool EmittedModuleMacros = false;
    // We write out exported module macros for PCH as well.
    auto Leafs = PP.getLeafModuleMacros(Name);
    SmallVector<ModuleMacro*, 8> Worklist(Leafs.begin(), Leafs.end());
    llvm::DenseMap<ModuleMacro*, unsigned> Visits;
    while (!Worklist.empty()) {
      auto *Macro = Worklist.pop_back_val();

      // Emit a record indicating this submodule exports this macro.
      ModuleMacroRecord.push_back(
          getSubmoduleID(Macro->getOwningModule()));
      ModuleMacroRecord.push_back(getMacroRef(Macro->getMacroInfo(), Name));
      for (auto *M : Macro->overrides())
        ModuleMacroRecord.push_back(getSubmoduleID(M->getOwningModule()));

      Stream.EmitRecord(PP_MODULE_MACRO, ModuleMacroRecord);
      ModuleMacroRecord.clear();

      // Enqueue overridden macros once we've visited all their ancestors.
      for (auto *M : Macro->overrides())
        if (++Visits[M] == M->getNumOverridingMacros())
          Worklist.push_back(M);

      EmittedModuleMacros = true;
    }

    if (Record.empty() && !EmittedModuleMacros)
      continue;

    IdentMacroDirectivesOffsetMap[Name] = StartOffset;
    Stream.EmitRecord(PP_MACRO_DIRECTIVE_HISTORY, Record);
    Record.clear();
  }

  /// Offsets of each of the macros into the bitstream, indexed by
  /// the local macro ID
  ///
  /// For each identifier that is associated with a macro, this map
  /// provides the offset into the bitstream where that macro is
  /// defined.
  std::vector<uint32_t> MacroOffsets;

  for (unsigned I = 0, N = MacroInfosToEmit.size(); I != N; ++I) {
    const IdentifierInfo *Name = MacroInfosToEmit[I].Name;
    MacroInfo *MI = MacroInfosToEmit[I].MI;
    MacroID ID = MacroInfosToEmit[I].ID;

    if (ID < FirstMacroID) {
      assert(0 && "Loaded MacroInfo entered MacroInfosToEmit ?");
      continue;
    }

    // Record the local offset of this macro.
    unsigned Index = ID - FirstMacroID;
    if (Index == MacroOffsets.size())
      MacroOffsets.push_back(Stream.GetCurrentBitNo());
    else {
      if (Index > MacroOffsets.size())
        MacroOffsets.resize(Index + 1);

      MacroOffsets[Index] = Stream.GetCurrentBitNo();
    }

    AddIdentifierRef(Name, Record);
    AddSourceLocation(MI->getDefinitionLoc(), Record);
    AddSourceLocation(MI->getDefinitionEndLoc(), Record);
    Record.push_back(MI->isUsed());
    Record.push_back(MI->isUsedForHeaderGuard());
    unsigned Code;
    if (MI->isObjectLike()) {
      Code = PP_MACRO_OBJECT_LIKE;
    } else {
      Code = PP_MACRO_FUNCTION_LIKE;

      Record.push_back(MI->isC99Varargs());
      Record.push_back(MI->isGNUVarargs());
      Record.push_back(MI->hasCommaPasting());
      Record.push_back(MI->getNumParams());
      for (const IdentifierInfo *Param : MI->params())
        AddIdentifierRef(Param, Record);
    }

    // If we have a detailed preprocessing record, record the macro definition
    // ID that corresponds to this macro.
    if (PPRec)
      Record.push_back(MacroDefinitions[PPRec->findMacroDefinition(MI)]);

    Stream.EmitRecord(Code, Record);
    Record.clear();

    // Emit the tokens array.
    for (unsigned TokNo = 0, e = MI->getNumTokens(); TokNo != e; ++TokNo) {
      // Note that we know that the preprocessor does not have any annotation
      // tokens in it because they are created by the parser, and thus can't
      // be in a macro definition.
      const Token &Tok = MI->getReplacementToken(TokNo);
      AddToken(Tok, Record);
      Stream.EmitRecord(PP_TOKEN, Record);
      Record.clear();
    }
    ++NumMacros;
  }

  Stream.ExitBlock();

  // Write the offsets table for macro IDs.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(MACRO_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of macros
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));

  unsigned MacroOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {MACRO_OFFSET, MacroOffsets.size(),
                                       FirstMacroID - NUM_PREDEF_MACRO_IDS};
    Stream.EmitRecordWithBlob(MacroOffsetAbbrev, Record, bytes(MacroOffsets));
  }
}

void ASTWriter::WritePreprocessorDetail(PreprocessingRecord &PPRec) {
  if (PPRec.local_begin() == PPRec.local_end())
    return;

  SmallVector<PPEntityOffset, 64> PreprocessedEntityOffsets;

  // Enter the preprocessor block.
  Stream.EnterSubblock(PREPROCESSOR_DETAIL_BLOCK_ID, 3);

  // If the preprocessor has a preprocessing record, emit it.
  unsigned NumPreprocessingRecords = 0;
  using namespace llvm;

  // Set up the abbreviation for
  unsigned InclusionAbbrev = 0;
  {
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(PPD_INCLUSION_DIRECTIVE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // filename length
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // in quotes
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // kind
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // imported module
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    InclusionAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  }

  unsigned FirstPreprocessorEntityID
    = (Chain ? PPRec.getNumLoadedPreprocessedEntities() : 0)
    + NUM_PREDEF_PP_ENTITY_IDS;
  unsigned NextPreprocessorEntityID = FirstPreprocessorEntityID;
  RecordData Record;
  for (PreprocessingRecord::iterator E = PPRec.local_begin(),
                                  EEnd = PPRec.local_end();
       E != EEnd;
       (void)++E, ++NumPreprocessingRecords, ++NextPreprocessorEntityID) {
    Record.clear();

    PreprocessedEntityOffsets.push_back(
        PPEntityOffset((*E)->getSourceRange(), Stream.GetCurrentBitNo()));

    if (auto *MD = dyn_cast<MacroDefinitionRecord>(*E)) {
      // Record this macro definition's ID.
      MacroDefinitions[MD] = NextPreprocessorEntityID;

      AddIdentifierRef(MD->getName(), Record);
      Stream.EmitRecord(PPD_MACRO_DEFINITION, Record);
      continue;
    }

    if (auto *ME = dyn_cast<MacroExpansion>(*E)) {
      Record.push_back(ME->isBuiltinMacro());
      if (ME->isBuiltinMacro())
        AddIdentifierRef(ME->getName(), Record);
      else
        Record.push_back(MacroDefinitions[ME->getDefinition()]);
      Stream.EmitRecord(PPD_MACRO_EXPANSION, Record);
      continue;
    }

    if (auto *ID = dyn_cast<InclusionDirective>(*E)) {
      Record.push_back(PPD_INCLUSION_DIRECTIVE);
      Record.push_back(ID->getFileName().size());
      Record.push_back(ID->wasInQuotes());
      Record.push_back(static_cast<unsigned>(ID->getKind()));
      Record.push_back(ID->importedModule());
      SmallString<64> Buffer;
      Buffer += ID->getFileName();
      // Check that the FileEntry is not null because it was not resolved and
      // we create a PCH even with compiler errors.
      if (ID->getFile())
        Buffer += ID->getFile()->getName();
      Stream.EmitRecordWithBlob(InclusionAbbrev, Record, Buffer);
      continue;
    }

    llvm_unreachable("Unhandled PreprocessedEntity in ASTWriter");
  }
  Stream.ExitBlock();

  // Write the offsets table for the preprocessing record.
  if (NumPreprocessingRecords > 0) {
    assert(PreprocessedEntityOffsets.size() == NumPreprocessingRecords);

    // Write the offsets table for identifier IDs.
    using namespace llvm;

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(PPD_ENTITIES_OFFSETS));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first pp entity
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned PPEOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    RecordData::value_type Record[] = {PPD_ENTITIES_OFFSETS,
                                       FirstPreprocessorEntityID -
                                           NUM_PREDEF_PP_ENTITY_IDS};
    Stream.EmitRecordWithBlob(PPEOffsetAbbrev, Record,
                              bytes(PreprocessedEntityOffsets));
  }

  // Write the skipped region table for the preprocessing record.
  ArrayRef<SourceRange> SkippedRanges = PPRec.getSkippedRanges();
  if (SkippedRanges.size() > 0) {
    std::vector<PPSkippedRange> SerializedSkippedRanges;
    SerializedSkippedRanges.reserve(SkippedRanges.size());
    for (auto const& Range : SkippedRanges)
      SerializedSkippedRanges.emplace_back(Range);

    using namespace llvm;
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(PPD_SKIPPED_RANGES));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned PPESkippedRangeAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    Record.clear();
    Record.push_back(PPD_SKIPPED_RANGES);
    Stream.EmitRecordWithBlob(PPESkippedRangeAbbrev, Record,
                              bytes(SerializedSkippedRanges));
  }
}

unsigned ASTWriter::getLocalOrImportedSubmoduleID(Module *Mod) {
  if (!Mod)
    return 0;

  llvm::DenseMap<Module *, unsigned>::iterator Known = SubmoduleIDs.find(Mod);
  if (Known != SubmoduleIDs.end())
    return Known->second;

  auto *Top = Mod->getTopLevelModule();
  if (Top != WritingModule &&
      (getLangOpts().CompilingPCH ||
       !Top->fullModuleNameIs(StringRef(getLangOpts().CurrentModule))))
    return 0;

  return SubmoduleIDs[Mod] = NextSubmoduleID++;
}

unsigned ASTWriter::getSubmoduleID(Module *Mod) {
  // FIXME: This can easily happen, if we have a reference to a submodule that
  // did not result in us loading a module file for that submodule. For
  // instance, a cross-top-level-module 'conflict' declaration will hit this.
  unsigned ID = getLocalOrImportedSubmoduleID(Mod);
  assert((ID || !Mod) &&
         "asked for module ID for non-local, non-imported module");
  return ID;
}

/// Compute the number of modules within the given tree (including the
/// given module).
static unsigned getNumberOfModules(Module *Mod) {
  unsigned ChildModules = 0;
  for (auto Sub = Mod->submodule_begin(), SubEnd = Mod->submodule_end();
       Sub != SubEnd; ++Sub)
    ChildModules += getNumberOfModules(*Sub);

  return ChildModules + 1;
}

void ASTWriter::WriteSubmodules(Module *WritingModule) {
  // Enter the submodule description block.
  Stream.EnterSubblock(SUBMODULE_BLOCK_ID, /*bits for abbreviations*/5);

  // Write the abbreviations needed for the submodules block.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_DEFINITION));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Parent
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // Kind
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsFramework
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsExplicit
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsSystem
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsExternC
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InferSubmodules...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InferExplicit...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InferExportWild...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ConfigMacrosExh...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ModuleMapIsPriv...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned DefinitionAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_UMBRELLA_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned UmbrellaAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned HeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_TOPHEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned TopHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_UMBRELLA_DIR));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned UmbrellaDirAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_REQUIRES));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // State
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));     // Feature
  unsigned RequiresAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_EXCLUDED_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned ExcludedHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_TEXTUAL_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned TextualHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_PRIVATE_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned PrivateHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_PRIVATE_TEXTUAL_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned PrivateTextualHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_LINK_LIBRARY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsFramework
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));     // Name
  unsigned LinkLibraryAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_CONFIG_MACRO));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));    // Macro name
  unsigned ConfigMacroAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_CONFLICT));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));  // Other module
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));    // Message
  unsigned ConflictAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_EXPORT_AS));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));    // Macro name
  unsigned ExportAsAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  // Write the submodule metadata block.
  RecordData::value_type Record[] = {
      getNumberOfModules(WritingModule),
      FirstSubmoduleID - NUM_PREDEF_SUBMODULE_IDS};
  Stream.EmitRecord(SUBMODULE_METADATA, Record);

  // Write all of the submodules.
  std::queue<Module *> Q;
  Q.push(WritingModule);
  while (!Q.empty()) {
    Module *Mod = Q.front();
    Q.pop();
    unsigned ID = getSubmoduleID(Mod);

    uint64_t ParentID = 0;
    if (Mod->Parent) {
      assert(SubmoduleIDs[Mod->Parent] && "Submodule parent not written?");
      ParentID = SubmoduleIDs[Mod->Parent];
    }

    // Emit the definition of the block.
    {
      RecordData::value_type Record[] = {SUBMODULE_DEFINITION,
                                         ID,
                                         ParentID,
                                         (RecordData::value_type)Mod->Kind,
                                         Mod->IsFramework,
                                         Mod->IsExplicit,
                                         Mod->IsSystem,
                                         Mod->IsExternC,
                                         Mod->InferSubmodules,
                                         Mod->InferExplicitSubmodules,
                                         Mod->InferExportWildcard,
                                         Mod->ConfigMacrosExhaustive,
                                         Mod->ModuleMapIsPrivate};
      Stream.EmitRecordWithBlob(DefinitionAbbrev, Record, Mod->Name);
    }

    // Emit the requirements.
    for (const auto &R : Mod->Requirements) {
      RecordData::value_type Record[] = {SUBMODULE_REQUIRES, R.second};
      Stream.EmitRecordWithBlob(RequiresAbbrev, Record, R.first);
    }

    // Emit the umbrella header, if there is one.
    if (auto UmbrellaHeader = Mod->getUmbrellaHeader()) {
      RecordData::value_type Record[] = {SUBMODULE_UMBRELLA_HEADER};
      Stream.EmitRecordWithBlob(UmbrellaAbbrev, Record,
                                UmbrellaHeader.NameAsWritten);
    } else if (auto UmbrellaDir = Mod->getUmbrellaDir()) {
      RecordData::value_type Record[] = {SUBMODULE_UMBRELLA_DIR};
      Stream.EmitRecordWithBlob(UmbrellaDirAbbrev, Record,
                                UmbrellaDir.NameAsWritten);
    }

    // Emit the headers.
    struct {
      unsigned RecordKind;
      unsigned Abbrev;
      Module::HeaderKind HeaderKind;
    } HeaderLists[] = {
      {SUBMODULE_HEADER, HeaderAbbrev, Module::HK_Normal},
      {SUBMODULE_TEXTUAL_HEADER, TextualHeaderAbbrev, Module::HK_Textual},
      {SUBMODULE_PRIVATE_HEADER, PrivateHeaderAbbrev, Module::HK_Private},
      {SUBMODULE_PRIVATE_TEXTUAL_HEADER, PrivateTextualHeaderAbbrev,
        Module::HK_PrivateTextual},
      {SUBMODULE_EXCLUDED_HEADER, ExcludedHeaderAbbrev, Module::HK_Excluded}
    };
    for (auto &HL : HeaderLists) {
      RecordData::value_type Record[] = {HL.RecordKind};
      for (auto &H : Mod->Headers[HL.HeaderKind])
        Stream.EmitRecordWithBlob(HL.Abbrev, Record, H.NameAsWritten);
    }

    // Emit the top headers.
    {
      auto TopHeaders = Mod->getTopHeaders(PP->getFileManager());
      RecordData::value_type Record[] = {SUBMODULE_TOPHEADER};
      for (auto *H : TopHeaders)
        Stream.EmitRecordWithBlob(TopHeaderAbbrev, Record, H->getName());
    }

    // Emit the imports.
    if (!Mod->Imports.empty()) {
      RecordData Record;
      for (auto *I : Mod->Imports)
        Record.push_back(getSubmoduleID(I));
      Stream.EmitRecord(SUBMODULE_IMPORTS, Record);
    }

    // Emit the exports.
    if (!Mod->Exports.empty()) {
      RecordData Record;
      for (const auto &E : Mod->Exports) {
        // FIXME: This may fail; we don't require that all exported modules
        // are local or imported.
        Record.push_back(getSubmoduleID(E.getPointer()));
        Record.push_back(E.getInt());
      }
      Stream.EmitRecord(SUBMODULE_EXPORTS, Record);
    }

    //FIXME: How do we emit the 'use'd modules?  They may not be submodules.
    // Might be unnecessary as use declarations are only used to build the
    // module itself.

    // Emit the link libraries.
    for (const auto &LL : Mod->LinkLibraries) {
      RecordData::value_type Record[] = {SUBMODULE_LINK_LIBRARY,
                                         LL.IsFramework};
      Stream.EmitRecordWithBlob(LinkLibraryAbbrev, Record, LL.Library);
    }

    // Emit the conflicts.
    for (const auto &C : Mod->Conflicts) {
      // FIXME: This may fail; we don't require that all conflicting modules
      // are local or imported.
      RecordData::value_type Record[] = {SUBMODULE_CONFLICT,
                                         getSubmoduleID(C.Other)};
      Stream.EmitRecordWithBlob(ConflictAbbrev, Record, C.Message);
    }

    // Emit the configuration macros.
    for (const auto &CM : Mod->ConfigMacros) {
      RecordData::value_type Record[] = {SUBMODULE_CONFIG_MACRO};
      Stream.EmitRecordWithBlob(ConfigMacroAbbrev, Record, CM);
    }

    // Emit the initializers, if any.
    RecordData Inits;
    for (Decl *D : Context->getModuleInitializers(Mod))
      Inits.push_back(GetDeclRef(D));
    if (!Inits.empty())
      Stream.EmitRecord(SUBMODULE_INITIALIZERS, Inits);

    // Emit the name of the re-exported module, if any.
    if (!Mod->ExportAsModule.empty()) {
      RecordData::value_type Record[] = {SUBMODULE_EXPORT_AS};
      Stream.EmitRecordWithBlob(ExportAsAbbrev, Record, Mod->ExportAsModule);
    }

    // Queue up the submodules of this module.
    for (auto *M : Mod->submodules())
      Q.push(M);
  }

  Stream.ExitBlock();

  assert((NextSubmoduleID - FirstSubmoduleID ==
          getNumberOfModules(WritingModule)) &&
         "Wrong # of submodules; found a reference to a non-local, "
         "non-imported submodule?");
}

void ASTWriter::WritePragmaDiagnosticMappings(const DiagnosticsEngine &Diag,
                                              bool isModule) {
  llvm::SmallDenseMap<const DiagnosticsEngine::DiagState *, unsigned, 64>
      DiagStateIDMap;
  unsigned CurrID = 0;
  RecordData Record;

  auto EncodeDiagStateFlags =
      [](const DiagnosticsEngine::DiagState *DS) -> unsigned {
    unsigned Result = (unsigned)DS->ExtBehavior;
    for (unsigned Val :
         {(unsigned)DS->IgnoreAllWarnings, (unsigned)DS->EnableAllWarnings,
          (unsigned)DS->WarningsAsErrors, (unsigned)DS->ErrorsAsFatal,
          (unsigned)DS->SuppressSystemWarnings})
      Result = (Result << 1) | Val;
    return Result;
  };

  unsigned Flags = EncodeDiagStateFlags(Diag.DiagStatesByLoc.FirstDiagState);
  Record.push_back(Flags);

  auto AddDiagState = [&](const DiagnosticsEngine::DiagState *State,
                          bool IncludeNonPragmaStates) {
    // Ensure that the diagnostic state wasn't modified since it was created.
    // We will not correctly round-trip this information otherwise.
    assert(Flags == EncodeDiagStateFlags(State) &&
           "diag state flags vary in single AST file");

    unsigned &DiagStateID = DiagStateIDMap[State];
    Record.push_back(DiagStateID);

    if (DiagStateID == 0) {
      DiagStateID = ++CurrID;

      // Add a placeholder for the number of mappings.
      auto SizeIdx = Record.size();
      Record.emplace_back();
      for (const auto &I : *State) {
        if (I.second.isPragma() || IncludeNonPragmaStates) {
          Record.push_back(I.first);
          Record.push_back(I.second.serialize());
        }
      }
      // Update the placeholder.
      Record[SizeIdx] = (Record.size() - SizeIdx) / 2;
    }
  };

  AddDiagState(Diag.DiagStatesByLoc.FirstDiagState, isModule);

  // Reserve a spot for the number of locations with state transitions.
  auto NumLocationsIdx = Record.size();
  Record.emplace_back();

  // Emit the state transitions.
  unsigned NumLocations = 0;
  for (auto &FileIDAndFile : Diag.DiagStatesByLoc.Files) {
    if (!FileIDAndFile.first.isValid() ||
        !FileIDAndFile.second.HasLocalTransitions)
      continue;
    ++NumLocations;

    SourceLocation Loc = Diag.SourceMgr->getComposedLoc(FileIDAndFile.first, 0);
    assert(!Loc.isInvalid() && "start loc for valid FileID is invalid");
    AddSourceLocation(Loc, Record);

    Record.push_back(FileIDAndFile.second.StateTransitions.size());
    for (auto &StatePoint : FileIDAndFile.second.StateTransitions) {
      Record.push_back(StatePoint.Offset);
      AddDiagState(StatePoint.State, false);
    }
  }

  // Backpatch the number of locations.
  Record[NumLocationsIdx] = NumLocations;

  // Emit CurDiagStateLoc.  Do it last in order to match source order.
  //
  // This also protects against a hypothetical corner case with simulating
  // -Werror settings for implicit modules in the ASTReader, where reading
  // CurDiagState out of context could change whether warning pragmas are
  // treated as errors.
  AddSourceLocation(Diag.DiagStatesByLoc.CurDiagStateLoc, Record);
  AddDiagState(Diag.DiagStatesByLoc.CurDiagState, false);

  Stream.EmitRecord(DIAG_PRAGMA_MAPPINGS, Record);
}

//===----------------------------------------------------------------------===//
// Type Serialization
//===----------------------------------------------------------------------===//

/// Write the representation of a type to the AST stream.
void ASTWriter::WriteType(QualType T) {
  TypeIdx &IdxRef = TypeIdxs[T];
  if (IdxRef.getIndex() == 0) // we haven't seen this type before.
    IdxRef = TypeIdx(NextTypeID++);
  TypeIdx Idx = IdxRef;

  assert(Idx.getIndex() >= FirstTypeID && "Re-writing a type from a prior AST");

  RecordData Record;

  // Emit the type's representation.
  ASTTypeWriter W(*this, Record);
  W.Visit(T);
  uint64_t Offset = W.Emit();

  // Record the offset for this type.
  unsigned Index = Idx.getIndex() - FirstTypeID;
  if (TypeOffsets.size() == Index)
    TypeOffsets.push_back(Offset);
  else if (TypeOffsets.size() < Index) {
    TypeOffsets.resize(Index + 1);
    TypeOffsets[Index] = Offset;
  } else {
    llvm_unreachable("Types emitted in wrong order");
  }
}

//===----------------------------------------------------------------------===//
// Declaration Serialization
//===----------------------------------------------------------------------===//

/// Write the block containing all of the declaration IDs
/// lexically declared within the given DeclContext.
///
/// \returns the offset of the DECL_CONTEXT_LEXICAL block within the
/// bitstream, or 0 if no block was written.
uint64_t ASTWriter::WriteDeclContextLexicalBlock(ASTContext &Context,
                                                 DeclContext *DC) {
  if (DC->decls_empty())
    return 0;

  uint64_t Offset = Stream.GetCurrentBitNo();
  SmallVector<uint32_t, 128> KindDeclPairs;
  for (const auto *D : DC->decls()) {
    KindDeclPairs.push_back(D->getKind());
    KindDeclPairs.push_back(GetDeclRef(D));
  }

  ++NumLexicalDeclContexts;
  RecordData::value_type Record[] = {DECL_CONTEXT_LEXICAL};
  Stream.EmitRecordWithBlob(DeclContextLexicalAbbrev, Record,
                            bytes(KindDeclPairs));
  return Offset;
}

void ASTWriter::WriteTypeDeclOffsets() {
  using namespace llvm;

  // Write the type offsets array
  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(TYPE_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of types
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // base type index
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // types block
  unsigned TypeOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {TYPE_OFFSET, TypeOffsets.size(),
                                       FirstTypeID - NUM_PREDEF_TYPE_IDS};
    Stream.EmitRecordWithBlob(TypeOffsetAbbrev, Record, bytes(TypeOffsets));
  }

  // Write the declaration offsets array
  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(DECL_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of declarations
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // base decl ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // declarations block
  unsigned DeclOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {DECL_OFFSET, DeclOffsets.size(),
                                       FirstDeclID - NUM_PREDEF_DECL_IDS};
    Stream.EmitRecordWithBlob(DeclOffsetAbbrev, Record, bytes(DeclOffsets));
  }
}

void ASTWriter::WriteFileDeclIDsMap() {
  using namespace llvm;

  SmallVector<std::pair<FileID, DeclIDInFileInfo *>, 64> SortedFileDeclIDs(
      FileDeclIDs.begin(), FileDeclIDs.end());
  llvm::sort(SortedFileDeclIDs, llvm::less_first());

  // Join the vectors of DeclIDs from all files.
  SmallVector<DeclID, 256> FileGroupedDeclIDs;
  for (auto &FileDeclEntry : SortedFileDeclIDs) {
    DeclIDInFileInfo &Info = *FileDeclEntry.second;
    Info.FirstDeclIndex = FileGroupedDeclIDs.size();
    for (auto &LocDeclEntry : Info.DeclIDs)
      FileGroupedDeclIDs.push_back(LocDeclEntry.second);
  }

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(FILE_SORTED_DECLS));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));
  RecordData::value_type Record[] = {FILE_SORTED_DECLS,
                                     FileGroupedDeclIDs.size()};
  Stream.EmitRecordWithBlob(AbbrevCode, Record, bytes(FileGroupedDeclIDs));
}

void ASTWriter::WriteComments() {
  Stream.EnterSubblock(COMMENTS_BLOCK_ID, 3);
  auto _ = llvm::make_scope_exit([this] { Stream.ExitBlock(); });
  if (!PP->getPreprocessorOpts().WriteCommentListToPCH)
    return;
  ArrayRef<RawComment *> RawComments = Context->Comments.getComments();
  RecordData Record;
  for (const auto *I : RawComments) {
    Record.clear();
    AddSourceRange(I->getSourceRange(), Record);
    Record.push_back(I->getKind());
    Record.push_back(I->isTrailingComment());
    Record.push_back(I->isAlmostTrailingComment());
    Stream.EmitRecord(COMMENTS_RAW_COMMENT, Record);
  }
}

//===----------------------------------------------------------------------===//
// Global Method Pool and Selector Serialization
//===----------------------------------------------------------------------===//

namespace {

// Trait used for the on-disk hash table used in the method pool.
class ASTMethodPoolTrait {
  ASTWriter &Writer;

public:
  using key_type = Selector;
  using key_type_ref = key_type;

  struct data_type {
    SelectorID ID;
    ObjCMethodList Instance, Factory;
  };
  using data_type_ref = const data_type &;

  using hash_value_type = unsigned;
  using offset_type = unsigned;

  explicit ASTMethodPoolTrait(ASTWriter &Writer) : Writer(Writer) {}

  static hash_value_type ComputeHash(Selector Sel) {
    return serialization::ComputeHash(Sel);
  }

  std::pair<unsigned, unsigned>
    EmitKeyDataLength(raw_ostream& Out, Selector Sel,
                      data_type_ref Methods) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);
    unsigned KeyLen = 2 + (Sel.getNumArgs()? Sel.getNumArgs() * 4 : 4);
    LE.write<uint16_t>(KeyLen);
    unsigned DataLen = 4 + 2 + 2; // 2 bytes for each of the method counts
    for (const ObjCMethodList *Method = &Methods.Instance; Method;
         Method = Method->getNext())
      if (Method->getMethod())
        DataLen += 4;
    for (const ObjCMethodList *Method = &Methods.Factory; Method;
         Method = Method->getNext())
      if (Method->getMethod())
        DataLen += 4;
    LE.write<uint16_t>(DataLen);
    return std::make_pair(KeyLen, DataLen);
  }

  void EmitKey(raw_ostream& Out, Selector Sel, unsigned) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);
    uint64_t Start = Out.tell();
    assert((Start >> 32) == 0 && "Selector key offset too large");
    Writer.SetSelectorOffset(Sel, Start);
    unsigned N = Sel.getNumArgs();
    LE.write<uint16_t>(N);
    if (N == 0)
      N = 1;
    for (unsigned I = 0; I != N; ++I)
      LE.write<uint32_t>(
          Writer.getIdentifierRef(Sel.getIdentifierInfoForSlot(I)));
  }

  void EmitData(raw_ostream& Out, key_type_ref,
                data_type_ref Methods, unsigned DataLen) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);
    uint64_t Start = Out.tell(); (void)Start;
    LE.write<uint32_t>(Methods.ID);
    unsigned NumInstanceMethods = 0;
    for (const ObjCMethodList *Method = &Methods.Instance; Method;
         Method = Method->getNext())
      if (Method->getMethod())
        ++NumInstanceMethods;

    unsigned NumFactoryMethods = 0;
    for (const ObjCMethodList *Method = &Methods.Factory; Method;
         Method = Method->getNext())
      if (Method->getMethod())
        ++NumFactoryMethods;

    unsigned InstanceBits = Methods.Instance.getBits();
    assert(InstanceBits < 4);
    unsigned InstanceHasMoreThanOneDeclBit =
        Methods.Instance.hasMoreThanOneDecl();
    unsigned FullInstanceBits = (NumInstanceMethods << 3) |
                                (InstanceHasMoreThanOneDeclBit << 2) |
                                InstanceBits;
    unsigned FactoryBits = Methods.Factory.getBits();
    assert(FactoryBits < 4);
    unsigned FactoryHasMoreThanOneDeclBit =
        Methods.Factory.hasMoreThanOneDecl();
    unsigned FullFactoryBits = (NumFactoryMethods << 3) |
                               (FactoryHasMoreThanOneDeclBit << 2) |
                               FactoryBits;
    LE.write<uint16_t>(FullInstanceBits);
    LE.write<uint16_t>(FullFactoryBits);
    for (const ObjCMethodList *Method = &Methods.Instance; Method;
         Method = Method->getNext())
      if (Method->getMethod())
        LE.write<uint32_t>(Writer.getDeclID(Method->getMethod()));
    for (const ObjCMethodList *Method = &Methods.Factory; Method;
         Method = Method->getNext())
      if (Method->getMethod())
        LE.write<uint32_t>(Writer.getDeclID(Method->getMethod()));

    assert(Out.tell() - Start == DataLen && "Data length is wrong");
  }
};

} // namespace

/// Write ObjC data: selectors and the method pool.
///
/// The method pool contains both instance and factory methods, stored
/// in an on-disk hash table indexed by the selector. The hash table also
/// contains an empty entry for every other selector known to Sema.
void ASTWriter::WriteSelectors(Sema &SemaRef) {
  using namespace llvm;

  // Do we have to do anything at all?
  if (SemaRef.MethodPool.empty() && SelectorIDs.empty())
    return;
  unsigned NumTableEntries = 0;
  // Create and write out the blob that contains selectors and the method pool.
  {
    llvm::OnDiskChainedHashTableGenerator<ASTMethodPoolTrait> Generator;
    ASTMethodPoolTrait Trait(*this);

    // Create the on-disk hash table representation. We walk through every
    // selector we've seen and look it up in the method pool.
    SelectorOffsets.resize(NextSelectorID - FirstSelectorID);
    for (auto &SelectorAndID : SelectorIDs) {
      Selector S = SelectorAndID.first;
      SelectorID ID = SelectorAndID.second;
      Sema::GlobalMethodPool::iterator F = SemaRef.MethodPool.find(S);
      ASTMethodPoolTrait::data_type Data = {
        ID,
        ObjCMethodList(),
        ObjCMethodList()
      };
      if (F != SemaRef.MethodPool.end()) {
        Data.Instance = F->second.first;
        Data.Factory = F->second.second;
      }
      // Only write this selector if it's not in an existing AST or something
      // changed.
      if (Chain && ID < FirstSelectorID) {
        // Selector already exists. Did it change?
        bool changed = false;
        for (ObjCMethodList *M = &Data.Instance;
             !changed && M && M->getMethod(); M = M->getNext()) {
          if (!M->getMethod()->isFromASTFile())
            changed = true;
        }
        for (ObjCMethodList *M = &Data.Factory; !changed && M && M->getMethod();
             M = M->getNext()) {
          if (!M->getMethod()->isFromASTFile())
            changed = true;
        }
        if (!changed)
          continue;
      } else if (Data.Instance.getMethod() || Data.Factory.getMethod()) {
        // A new method pool entry.
        ++NumTableEntries;
      }
      Generator.insert(S, Data, Trait);
    }

    // Create the on-disk hash table in a buffer.
    SmallString<4096> MethodPool;
    uint32_t BucketOffset;
    {
      using namespace llvm::support;

      ASTMethodPoolTrait Trait(*this);
      llvm::raw_svector_ostream Out(MethodPool);
      // Make sure that no bucket is at offset 0
      endian::write<uint32_t>(Out, 0, little);
      BucketOffset = Generator.Emit(Out, Trait);
    }

    // Create a blob abbreviation
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(METHOD_POOL));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned MethodPoolAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the method pool
    {
      RecordData::value_type Record[] = {METHOD_POOL, BucketOffset,
                                         NumTableEntries};
      Stream.EmitRecordWithBlob(MethodPoolAbbrev, Record, MethodPool);
    }

    // Create a blob abbreviation for the selector table offsets.
    Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(SELECTOR_OFFSETS));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // size
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first ID
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned SelectorOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the selector offsets table.
    {
      RecordData::value_type Record[] = {
          SELECTOR_OFFSETS, SelectorOffsets.size(),
          FirstSelectorID - NUM_PREDEF_SELECTOR_IDS};
      Stream.EmitRecordWithBlob(SelectorOffsetAbbrev, Record,
                                bytes(SelectorOffsets));
    }
  }
}

/// Write the selectors referenced in @selector expression into AST file.
void ASTWriter::WriteReferencedSelectorsPool(Sema &SemaRef) {
  using namespace llvm;

  if (SemaRef.ReferencedSelectors.empty())
    return;

  RecordData Record;
  ASTRecordWriter Writer(*this, Record);

  // Note: this writes out all references even for a dependent AST. But it is
  // very tricky to fix, and given that @selector shouldn't really appear in
  // headers, probably not worth it. It's not a correctness issue.
  for (auto &SelectorAndLocation : SemaRef.ReferencedSelectors) {
    Selector Sel = SelectorAndLocation.first;
    SourceLocation Loc = SelectorAndLocation.second;
    Writer.AddSelectorRef(Sel);
    Writer.AddSourceLocation(Loc);
  }
  Writer.Emit(REFERENCED_SELECTOR_POOL);
}

//===----------------------------------------------------------------------===//
// Identifier Table Serialization
//===----------------------------------------------------------------------===//

/// Determine the declaration that should be put into the name lookup table to
/// represent the given declaration in this module. This is usually D itself,
/// but if D was imported and merged into a local declaration, we want the most
/// recent local declaration instead. The chosen declaration will be the most
/// recent declaration in any module that imports this one.
static NamedDecl *getDeclForLocalLookup(const LangOptions &LangOpts,
                                        NamedDecl *D) {
  if (!LangOpts.Modules || !D->isFromASTFile())
    return D;

  if (Decl *Redecl = D->getPreviousDecl()) {
    // For Redeclarable decls, a prior declaration might be local.
    for (; Redecl; Redecl = Redecl->getPreviousDecl()) {
      // If we find a local decl, we're done.
      if (!Redecl->isFromASTFile()) {
        // Exception: in very rare cases (for injected-class-names), not all
        // redeclarations are in the same semantic context. Skip ones in a
        // different context. They don't go in this lookup table at all.
        if (!Redecl->getDeclContext()->getRedeclContext()->Equals(
                D->getDeclContext()->getRedeclContext()))
          continue;
        return cast<NamedDecl>(Redecl);
      }

      // If we find a decl from a (chained-)PCH stop since we won't find a
      // local one.
      if (Redecl->getOwningModuleID() == 0)
        break;
    }
  } else if (Decl *First = D->getCanonicalDecl()) {
    // For Mergeable decls, the first decl might be local.
    if (!First->isFromASTFile())
      return cast<NamedDecl>(First);
  }

  // All declarations are imported. Our most recent declaration will also be
  // the most recent one in anyone who imports us.
  return D;
}

namespace {

class ASTIdentifierTableTrait {
  ASTWriter &Writer;
  Preprocessor &PP;
  IdentifierResolver &IdResolver;
  bool IsModule;
  bool NeedDecls;
  ASTWriter::RecordData *InterestingIdentifierOffsets;

  /// Determines whether this is an "interesting" identifier that needs a
  /// full IdentifierInfo structure written into the hash table. Notably, this
  /// doesn't check whether the name has macros defined; use PublicMacroIterator
  /// to check that.
  bool isInterestingIdentifier(const IdentifierInfo *II, uint64_t MacroOffset) {
    if (MacroOffset ||
        II->isPoisoned() ||
        (IsModule ? II->hasRevertedBuiltin() : II->getObjCOrBuiltinID()) ||
        II->hasRevertedTokenIDToIdentifier() ||
        (NeedDecls && II->getFETokenInfo()))
      return true;

    return false;
  }

public:
  using key_type = IdentifierInfo *;
  using key_type_ref = key_type;

  using data_type = IdentID;
  using data_type_ref = data_type;

  using hash_value_type = unsigned;
  using offset_type = unsigned;

  ASTIdentifierTableTrait(ASTWriter &Writer, Preprocessor &PP,
                          IdentifierResolver &IdResolver, bool IsModule,
                          ASTWriter::RecordData *InterestingIdentifierOffsets)
      : Writer(Writer), PP(PP), IdResolver(IdResolver), IsModule(IsModule),
        NeedDecls(!IsModule || !Writer.getLangOpts().CPlusPlus),
        InterestingIdentifierOffsets(InterestingIdentifierOffsets) {}

  bool needDecls() const { return NeedDecls; }

  static hash_value_type ComputeHash(const IdentifierInfo* II) {
    return llvm::djbHash(II->getName());
  }

  bool isInterestingIdentifier(const IdentifierInfo *II) {
    auto MacroOffset = Writer.getMacroDirectivesOffset(II);
    return isInterestingIdentifier(II, MacroOffset);
  }

  bool isInterestingNonMacroIdentifier(const IdentifierInfo *II) {
    return isInterestingIdentifier(II, 0);
  }

  std::pair<unsigned, unsigned>
  EmitKeyDataLength(raw_ostream& Out, IdentifierInfo* II, IdentID ID) {
    unsigned KeyLen = II->getLength() + 1;
    unsigned DataLen = 4; // 4 bytes for the persistent ID << 1
    auto MacroOffset = Writer.getMacroDirectivesOffset(II);
    if (isInterestingIdentifier(II, MacroOffset)) {
      DataLen += 2; // 2 bytes for builtin ID
      DataLen += 2; // 2 bytes for flags
      if (MacroOffset)
        DataLen += 4; // MacroDirectives offset.

      if (NeedDecls) {
        for (IdentifierResolver::iterator D = IdResolver.begin(II),
                                       DEnd = IdResolver.end();
             D != DEnd; ++D)
          DataLen += 4;
      }
    }

    using namespace llvm::support;

    endian::Writer LE(Out, little);

    assert((uint16_t)DataLen == DataLen && (uint16_t)KeyLen == KeyLen);
    LE.write<uint16_t>(DataLen);
    // We emit the key length after the data length so that every
    // string is preceded by a 16-bit length. This matches the PTH
    // format for storing identifiers.
    LE.write<uint16_t>(KeyLen);
    return std::make_pair(KeyLen, DataLen);
  }

  void EmitKey(raw_ostream& Out, const IdentifierInfo* II,
               unsigned KeyLen) {
    // Record the location of the key data.  This is used when generating
    // the mapping from persistent IDs to strings.
    Writer.SetIdentifierOffset(II, Out.tell());

    // Emit the offset of the key/data length information to the interesting
    // identifiers table if necessary.
    if (InterestingIdentifierOffsets && isInterestingIdentifier(II))
      InterestingIdentifierOffsets->push_back(Out.tell() - 4);

    Out.write(II->getNameStart(), KeyLen);
  }

  void EmitData(raw_ostream& Out, IdentifierInfo* II,
                IdentID ID, unsigned) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);

    auto MacroOffset = Writer.getMacroDirectivesOffset(II);
    if (!isInterestingIdentifier(II, MacroOffset)) {
      LE.write<uint32_t>(ID << 1);
      return;
    }

    LE.write<uint32_t>((ID << 1) | 0x01);
    uint32_t Bits = (uint32_t)II->getObjCOrBuiltinID();
    assert((Bits & 0xffff) == Bits && "ObjCOrBuiltinID too big for ASTReader.");
    LE.write<uint16_t>(Bits);
    Bits = 0;
    bool HadMacroDefinition = MacroOffset != 0;
    Bits = (Bits << 1) | unsigned(HadMacroDefinition);
    Bits = (Bits << 1) | unsigned(II->isExtensionToken());
    Bits = (Bits << 1) | unsigned(II->isPoisoned());
    Bits = (Bits << 1) | unsigned(II->hasRevertedBuiltin());
    Bits = (Bits << 1) | unsigned(II->hasRevertedTokenIDToIdentifier());
    Bits = (Bits << 1) | unsigned(II->isCPlusPlusOperatorKeyword());
    LE.write<uint16_t>(Bits);

    if (HadMacroDefinition)
      LE.write<uint32_t>(MacroOffset);

    if (NeedDecls) {
      // Emit the declaration IDs in reverse order, because the
      // IdentifierResolver provides the declarations as they would be
      // visible (e.g., the function "stat" would come before the struct
      // "stat"), but the ASTReader adds declarations to the end of the list
      // (so we need to see the struct "stat" before the function "stat").
      // Only emit declarations that aren't from a chained PCH, though.
      SmallVector<NamedDecl *, 16> Decls(IdResolver.begin(II),
                                         IdResolver.end());
      for (SmallVectorImpl<NamedDecl *>::reverse_iterator D = Decls.rbegin(),
                                                          DEnd = Decls.rend();
           D != DEnd; ++D)
        LE.write<uint32_t>(
            Writer.getDeclID(getDeclForLocalLookup(PP.getLangOpts(), *D)));
    }
  }
};

} // namespace

/// Write the identifier table into the AST file.
///
/// The identifier table consists of a blob containing string data
/// (the actual identifiers themselves) and a separate "offsets" index
/// that maps identifier IDs to locations within the blob.
void ASTWriter::WriteIdentifierTable(Preprocessor &PP,
                                     IdentifierResolver &IdResolver,
                                     bool IsModule) {
  using namespace llvm;

  RecordData InterestingIdents;

  // Create and write out the blob that contains the identifier
  // strings.
  {
    llvm::OnDiskChainedHashTableGenerator<ASTIdentifierTableTrait> Generator;
    ASTIdentifierTableTrait Trait(
        *this, PP, IdResolver, IsModule,
        (getLangOpts().CPlusPlus && IsModule) ? &InterestingIdents : nullptr);

    // Look for any identifiers that were named while processing the
    // headers, but are otherwise not needed. We add these to the hash
    // table to enable checking of the predefines buffer in the case
    // where the user adds new macro definitions when building the AST
    // file.
    SmallVector<const IdentifierInfo *, 128> IIs;
    for (const auto &ID : PP.getIdentifierTable())
      IIs.push_back(ID.second);
    // Sort the identifiers lexicographically before getting them references so
    // that their order is stable.
    llvm::sort(IIs, llvm::less_ptr<IdentifierInfo>());
    for (const IdentifierInfo *II : IIs)
      if (Trait.isInterestingNonMacroIdentifier(II))
        getIdentifierRef(II);

    // Create the on-disk hash table representation. We only store offsets
    // for identifiers that appear here for the first time.
    IdentifierOffsets.resize(NextIdentID - FirstIdentID);
    for (auto IdentIDPair : IdentifierIDs) {
      auto *II = const_cast<IdentifierInfo *>(IdentIDPair.first);
      IdentID ID = IdentIDPair.second;
      assert(II && "NULL identifier in identifier table");
      // Write out identifiers if either the ID is local or the identifier has
      // changed since it was loaded.
      if (ID >= FirstIdentID || !Chain || !II->isFromAST()
          || II->hasChangedSinceDeserialization() ||
          (Trait.needDecls() &&
           II->hasFETokenInfoChangedSinceDeserialization()))
        Generator.insert(II, ID, Trait);
    }

    // Create the on-disk hash table in a buffer.
    SmallString<4096> IdentifierTable;
    uint32_t BucketOffset;
    {
      using namespace llvm::support;

      llvm::raw_svector_ostream Out(IdentifierTable);
      // Make sure that no bucket is at offset 0
      endian::write<uint32_t>(Out, 0, little);
      BucketOffset = Generator.Emit(Out, Trait);
    }

    // Create a blob abbreviation
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(IDENTIFIER_TABLE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned IDTableAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the identifier table
    RecordData::value_type Record[] = {IDENTIFIER_TABLE, BucketOffset};
    Stream.EmitRecordWithBlob(IDTableAbbrev, Record, IdentifierTable);
  }

  // Write the offsets table for identifier IDs.
  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(IDENTIFIER_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of identifiers
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned IdentifierOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

#ifndef NDEBUG
  for (unsigned I = 0, N = IdentifierOffsets.size(); I != N; ++I)
    assert(IdentifierOffsets[I] && "Missing identifier offset?");
#endif

  RecordData::value_type Record[] = {IDENTIFIER_OFFSET,
                                     IdentifierOffsets.size(),
                                     FirstIdentID - NUM_PREDEF_IDENT_IDS};
  Stream.EmitRecordWithBlob(IdentifierOffsetAbbrev, Record,
                            bytes(IdentifierOffsets));

  // In C++, write the list of interesting identifiers (those that are
  // defined as macros, poisoned, or similar unusual things).
  if (!InterestingIdents.empty())
    Stream.EmitRecord(INTERESTING_IDENTIFIERS, InterestingIdents);
}

//===----------------------------------------------------------------------===//
// DeclContext's Name Lookup Table Serialization
//===----------------------------------------------------------------------===//

namespace {

// Trait used for the on-disk hash table used in the method pool.
class ASTDeclContextNameLookupTrait {
  ASTWriter &Writer;
  llvm::SmallVector<DeclID, 64> DeclIDs;

public:
  using key_type = DeclarationNameKey;
  using key_type_ref = key_type;

  /// A start and end index into DeclIDs, representing a sequence of decls.
  using data_type = std::pair<unsigned, unsigned>;
  using data_type_ref = const data_type &;

  using hash_value_type = unsigned;
  using offset_type = unsigned;

  explicit ASTDeclContextNameLookupTrait(ASTWriter &Writer) : Writer(Writer) {}

  template<typename Coll>
  data_type getData(const Coll &Decls) {
    unsigned Start = DeclIDs.size();
    for (NamedDecl *D : Decls) {
      DeclIDs.push_back(
          Writer.GetDeclRef(getDeclForLocalLookup(Writer.getLangOpts(), D)));
    }
    return std::make_pair(Start, DeclIDs.size());
  }

  data_type ImportData(const reader::ASTDeclContextNameLookupTrait::data_type &FromReader) {
    unsigned Start = DeclIDs.size();
    for (auto ID : FromReader)
      DeclIDs.push_back(ID);
    return std::make_pair(Start, DeclIDs.size());
  }

  static bool EqualKey(key_type_ref a, key_type_ref b) {
    return a == b;
  }

  hash_value_type ComputeHash(DeclarationNameKey Name) {
    return Name.getHash();
  }

  void EmitFileRef(raw_ostream &Out, ModuleFile *F) const {
    assert(Writer.hasChain() &&
           "have reference to loaded module file but no chain?");

    using namespace llvm::support;

    endian::write<uint32_t>(Out, Writer.getChain()->getModuleFileID(F), little);
  }

  std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &Out,
                                                  DeclarationNameKey Name,
                                                  data_type_ref Lookup) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);
    unsigned KeyLen = 1;
    switch (Name.getKind()) {
    case DeclarationName::Identifier:
    case DeclarationName::ObjCZeroArgSelector:
    case DeclarationName::ObjCOneArgSelector:
    case DeclarationName::ObjCMultiArgSelector:
    case DeclarationName::CXXLiteralOperatorName:
    case DeclarationName::CXXDeductionGuideName:
      KeyLen += 4;
      break;
    case DeclarationName::CXXOperatorName:
      KeyLen += 1;
      break;
    case DeclarationName::CXXConstructorName:
    case DeclarationName::CXXDestructorName:
    case DeclarationName::CXXConversionFunctionName:
    case DeclarationName::CXXUsingDirective:
      break;
    }
    LE.write<uint16_t>(KeyLen);

    // 4 bytes for each DeclID.
    unsigned DataLen = 4 * (Lookup.second - Lookup.first);
    assert(uint16_t(DataLen) == DataLen &&
           "too many decls for serialized lookup result");
    LE.write<uint16_t>(DataLen);

    return std::make_pair(KeyLen, DataLen);
  }

  void EmitKey(raw_ostream &Out, DeclarationNameKey Name, unsigned) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);
    LE.write<uint8_t>(Name.getKind());
    switch (Name.getKind()) {
    case DeclarationName::Identifier:
    case DeclarationName::CXXLiteralOperatorName:
    case DeclarationName::CXXDeductionGuideName:
      LE.write<uint32_t>(Writer.getIdentifierRef(Name.getIdentifier()));
      return;
    case DeclarationName::ObjCZeroArgSelector:
    case DeclarationName::ObjCOneArgSelector:
    case DeclarationName::ObjCMultiArgSelector:
      LE.write<uint32_t>(Writer.getSelectorRef(Name.getSelector()));
      return;
    case DeclarationName::CXXOperatorName:
      assert(Name.getOperatorKind() < NUM_OVERLOADED_OPERATORS &&
             "Invalid operator?");
      LE.write<uint8_t>(Name.getOperatorKind());
      return;
    case DeclarationName::CXXConstructorName:
    case DeclarationName::CXXDestructorName:
    case DeclarationName::CXXConversionFunctionName:
    case DeclarationName::CXXUsingDirective:
      return;
    }

    llvm_unreachable("Invalid name kind?");
  }

  void EmitData(raw_ostream &Out, key_type_ref, data_type Lookup,
                unsigned DataLen) {
    using namespace llvm::support;

    endian::Writer LE(Out, little);
    uint64_t Start = Out.tell(); (void)Start;
    for (unsigned I = Lookup.first, N = Lookup.second; I != N; ++I)
      LE.write<uint32_t>(DeclIDs[I]);
    assert(Out.tell() - Start == DataLen && "Data length is wrong");
  }
};

} // namespace

bool ASTWriter::isLookupResultExternal(StoredDeclsList &Result,
                                       DeclContext *DC) {
  return Result.hasExternalDecls() &&
         DC->hasNeedToReconcileExternalVisibleStorage();
}

bool ASTWriter::isLookupResultEntirelyExternal(StoredDeclsList &Result,
                                               DeclContext *DC) {
  for (auto *D : Result.getLookupResult())
    if (!getDeclForLocalLookup(getLangOpts(), D)->isFromASTFile())
      return false;

  return true;
}

void
ASTWriter::GenerateNameLookupTable(const DeclContext *ConstDC,
                                   llvm::SmallVectorImpl<char> &LookupTable) {
  assert(!ConstDC->hasLazyLocalLexicalLookups() &&
         !ConstDC->hasLazyExternalLexicalLookups() &&
         "must call buildLookups first");

  // FIXME: We need to build the lookups table, which is logically const.
  auto *DC = const_cast<DeclContext*>(ConstDC);
  assert(DC == DC->getPrimaryContext() && "only primary DC has lookup table");

  // Create the on-disk hash table representation.
  MultiOnDiskHashTableGenerator<reader::ASTDeclContextNameLookupTrait,
                                ASTDeclContextNameLookupTrait> Generator;
  ASTDeclContextNameLookupTrait Trait(*this);

  // The first step is to collect the declaration names which we need to
  // serialize into the name lookup table, and to collect them in a stable
  // order.
  SmallVector<DeclarationName, 16> Names;

  // We also build up small sets of the constructor and conversion function
  // names which are visible.
  llvm::SmallSet<DeclarationName, 8> ConstructorNameSet, ConversionNameSet;

  for (auto &Lookup : *DC->buildLookup()) {
    auto &Name = Lookup.first;
    auto &Result = Lookup.second;

    // If there are no local declarations in our lookup result, we
    // don't need to write an entry for the name at all. If we can't
    // write out a lookup set without performing more deserialization,
    // just skip this entry.
    if (isLookupResultExternal(Result, DC) &&
        isLookupResultEntirelyExternal(Result, DC))
      continue;

    // We also skip empty results. If any of the results could be external and
    // the currently available results are empty, then all of the results are
    // external and we skip it above. So the only way we get here with an empty
    // results is when no results could have been external *and* we have
    // external results.
    //
    // FIXME: While we might want to start emitting on-disk entries for negative
    // lookups into a decl context as an optimization, today we *have* to skip
    // them because there are names with empty lookup results in decl contexts
    // which we can't emit in any stable ordering: we lookup constructors and
    // conversion functions in the enclosing namespace scope creating empty
    // results for them. This in almost certainly a bug in Clang's name lookup,
    // but that is likely to be hard or impossible to fix and so we tolerate it
    // here by omitting lookups with empty results.
    if (Lookup.second.getLookupResult().empty())
      continue;

    switch (Lookup.first.getNameKind()) {
    default:
      Names.push_back(Lookup.first);
      break;

    case DeclarationName::CXXConstructorName:
      assert(isa<CXXRecordDecl>(DC) &&
             "Cannot have a constructor name outside of a class!");
      ConstructorNameSet.insert(Name);
      break;

    case DeclarationName::CXXConversionFunctionName:
      assert(isa<CXXRecordDecl>(DC) &&
             "Cannot have a conversion function name outside of a class!");
      ConversionNameSet.insert(Name);
      break;
    }
  }

  // Sort the names into a stable order.
  llvm::sort(Names);

  if (auto *D = dyn_cast<CXXRecordDecl>(DC)) {
    // We need to establish an ordering of constructor and conversion function
    // names, and they don't have an intrinsic ordering.

    // First we try the easy case by forming the current context's constructor
    // name and adding that name first. This is a very useful optimization to
    // avoid walking the lexical declarations in many cases, and it also
    // handles the only case where a constructor name can come from some other
    // lexical context -- when that name is an implicit constructor merged from
    // another declaration in the redecl chain. Any non-implicit constructor or
    // conversion function which doesn't occur in all the lexical contexts
    // would be an ODR violation.
    auto ImplicitCtorName = Context->DeclarationNames.getCXXConstructorName(
        Context->getCanonicalType(Context->getRecordType(D)));
    if (ConstructorNameSet.erase(ImplicitCtorName))
      Names.push_back(ImplicitCtorName);

    // If we still have constructors or conversion functions, we walk all the
    // names in the decl and add the constructors and conversion functions
    // which are visible in the order they lexically occur within the context.
    if (!ConstructorNameSet.empty() || !ConversionNameSet.empty())
      for (Decl *ChildD : cast<CXXRecordDecl>(DC)->decls())
        if (auto *ChildND = dyn_cast<NamedDecl>(ChildD)) {
          auto Name = ChildND->getDeclName();
          switch (Name.getNameKind()) {
          default:
            continue;

          case DeclarationName::CXXConstructorName:
            if (ConstructorNameSet.erase(Name))
              Names.push_back(Name);
            break;

          case DeclarationName::CXXConversionFunctionName:
            if (ConversionNameSet.erase(Name))
              Names.push_back(Name);
            break;
          }

          if (ConstructorNameSet.empty() && ConversionNameSet.empty())
            break;
        }

    assert(ConstructorNameSet.empty() && "Failed to find all of the visible "
                                         "constructors by walking all the "
                                         "lexical members of the context.");
    assert(ConversionNameSet.empty() && "Failed to find all of the visible "
                                        "conversion functions by walking all "
                                        "the lexical members of the context.");
  }

  // Next we need to do a lookup with each name into this decl context to fully
  // populate any results from external sources. We don't actually use the
  // results of these lookups because we only want to use the results after all
  // results have been loaded and the pointers into them will be stable.
  for (auto &Name : Names)
    DC->lookup(Name);

  // Now we need to insert the results for each name into the hash table. For
  // constructor names and conversion function names, we actually need to merge
  // all of the results for them into one list of results each and insert
  // those.
  SmallVector<NamedDecl *, 8> ConstructorDecls;
  SmallVector<NamedDecl *, 8> ConversionDecls;

  // Now loop over the names, either inserting them or appending for the two
  // special cases.
  for (auto &Name : Names) {
    DeclContext::lookup_result Result = DC->noload_lookup(Name);

    switch (Name.getNameKind()) {
    default:
      Generator.insert(Name, Trait.getData(Result), Trait);
      break;

    case DeclarationName::CXXConstructorName:
      ConstructorDecls.append(Result.begin(), Result.end());
      break;

    case DeclarationName::CXXConversionFunctionName:
      ConversionDecls.append(Result.begin(), Result.end());
      break;
    }
  }

  // Handle our two special cases if we ended up having any. We arbitrarily use
  // the first declaration's name here because the name itself isn't part of
  // the key, only the kind of name is used.
  if (!ConstructorDecls.empty())
    Generator.insert(ConstructorDecls.front()->getDeclName(),
                     Trait.getData(ConstructorDecls), Trait);
  if (!ConversionDecls.empty())
    Generator.insert(ConversionDecls.front()->getDeclName(),
                     Trait.getData(ConversionDecls), Trait);

  // Create the on-disk hash table. Also emit the existing imported and
  // merged table if there is one.
  auto *Lookups = Chain ? Chain->getLoadedLookupTables(DC) : nullptr;
  Generator.emit(LookupTable, Trait, Lookups ? &Lookups->Table : nullptr);
}

/// Write the block containing all of the declaration IDs
/// visible from the given DeclContext.
///
/// \returns the offset of the DECL_CONTEXT_VISIBLE block within the
/// bitstream, or 0 if no block was written.
uint64_t ASTWriter::WriteDeclContextVisibleBlock(ASTContext &Context,
                                                 DeclContext *DC) {
  // If we imported a key declaration of this namespace, write the visible
  // lookup results as an update record for it rather than including them
  // on this declaration. We will only look at key declarations on reload.
  if (isa<NamespaceDecl>(DC) && Chain &&
      Chain->getKeyDeclaration(cast<Decl>(DC))->isFromASTFile()) {
    // Only do this once, for the first local declaration of the namespace.
    for (auto *Prev = cast<NamespaceDecl>(DC)->getPreviousDecl(); Prev;
         Prev = Prev->getPreviousDecl())
      if (!Prev->isFromASTFile())
        return 0;

    // Note that we need to emit an update record for the primary context.
    UpdatedDeclContexts.insert(DC->getPrimaryContext());

    // Make sure all visible decls are written. They will be recorded later. We
    // do this using a side data structure so we can sort the names into
    // a deterministic order.
    StoredDeclsMap *Map = DC->getPrimaryContext()->buildLookup();
    SmallVector<std::pair<DeclarationName, DeclContext::lookup_result>, 16>
        LookupResults;
    if (Map) {
      LookupResults.reserve(Map->size());
      for (auto &Entry : *Map)
        LookupResults.push_back(
            std::make_pair(Entry.first, Entry.second.getLookupResult()));
    }

    llvm::sort(LookupResults, llvm::less_first());
    for (auto &NameAndResult : LookupResults) {
      DeclarationName Name = NameAndResult.first;
      DeclContext::lookup_result Result = NameAndResult.second;
      if (Name.getNameKind() == DeclarationName::CXXConstructorName ||
          Name.getNameKind() == DeclarationName::CXXConversionFunctionName) {
        // We have to work around a name lookup bug here where negative lookup
        // results for these names get cached in namespace lookup tables (these
        // names should never be looked up in a namespace).
        assert(Result.empty() && "Cannot have a constructor or conversion "
                                 "function name in a namespace!");
        continue;
      }

      for (NamedDecl *ND : Result)
        if (!ND->isFromASTFile())
          GetDeclRef(ND);
    }

    return 0;
  }

  if (DC->getPrimaryContext() != DC)
    return 0;

  // Skip contexts which don't support name lookup.
  if (!DC->isLookupContext())
    return 0;

  // If not in C++, we perform name lookup for the translation unit via the
  // IdentifierInfo chains, don't bother to build a visible-declarations table.
  if (DC->isTranslationUnit() && !Context.getLangOpts().CPlusPlus)
    return 0;

  // Serialize the contents of the mapping used for lookup. Note that,
  // although we have two very different code paths, the serialized
  // representation is the same for both cases: a declaration name,
  // followed by a size, followed by references to the visible
  // declarations that have that name.
  uint64_t Offset = Stream.GetCurrentBitNo();
  StoredDeclsMap *Map = DC->buildLookup();
  if (!Map || Map->empty())
    return 0;

  // Create the on-disk hash table in a buffer.
  SmallString<4096> LookupTable;
  GenerateNameLookupTable(DC, LookupTable);

  // Write the lookup table
  RecordData::value_type Record[] = {DECL_CONTEXT_VISIBLE};
  Stream.EmitRecordWithBlob(DeclContextVisibleLookupAbbrev, Record,
                            LookupTable);
  ++NumVisibleDeclContexts;
  return Offset;
}

/// Write an UPDATE_VISIBLE block for the given context.
///
/// UPDATE_VISIBLE blocks contain the declarations that are added to an existing
/// DeclContext in a dependent AST file. As such, they only exist for the TU
/// (in C++), for namespaces, and for classes with forward-declared unscoped
/// enumeration members (in C++11).
void ASTWriter::WriteDeclContextVisibleUpdate(const DeclContext *DC) {
  StoredDeclsMap *Map = DC->getLookupPtr();
  if (!Map || Map->empty())
    return;

  // Create the on-disk hash table in a buffer.
  SmallString<4096> LookupTable;
  GenerateNameLookupTable(DC, LookupTable);

  // If we're updating a namespace, select a key declaration as the key for the
  // update record; those are the only ones that will be checked on reload.
  if (isa<NamespaceDecl>(DC))
    DC = cast<DeclContext>(Chain->getKeyDeclaration(cast<Decl>(DC)));

  // Write the lookup table
  RecordData::value_type Record[] = {UPDATE_VISIBLE, getDeclID(cast<Decl>(DC))};
  Stream.EmitRecordWithBlob(UpdateVisibleAbbrev, Record, LookupTable);
}

/// Write an FP_PRAGMA_OPTIONS block for the given FPOptions.
void ASTWriter::WriteFPPragmaOptions(const FPOptions &Opts) {
  RecordData::value_type Record[] = {Opts.getInt()};
  Stream.EmitRecord(FP_PRAGMA_OPTIONS, Record);
}

/// Write an OPENCL_EXTENSIONS block for the given OpenCLOptions.
void ASTWriter::WriteOpenCLExtensions(Sema &SemaRef) {
  if (!SemaRef.Context.getLangOpts().OpenCL)
    return;

  const OpenCLOptions &Opts = SemaRef.getOpenCLOptions();
  RecordData Record;
  for (const auto &I:Opts.OptMap) {
    AddString(I.getKey(), Record);
    auto V = I.getValue();
    Record.push_back(V.Supported ? 1 : 0);
    Record.push_back(V.Enabled ? 1 : 0);
    Record.push_back(V.Avail);
    Record.push_back(V.Core);
  }
  Stream.EmitRecord(OPENCL_EXTENSIONS, Record);
}

void ASTWriter::WriteOpenCLExtensionTypes(Sema &SemaRef) {
  if (!SemaRef.Context.getLangOpts().OpenCL)
    return;

  RecordData Record;
  for (const auto &I : SemaRef.OpenCLTypeExtMap) {
    Record.push_back(
        static_cast<unsigned>(getTypeID(I.first->getCanonicalTypeInternal())));
    Record.push_back(I.second.size());
    for (auto Ext : I.second)
      AddString(Ext, Record);
  }
  Stream.EmitRecord(OPENCL_EXTENSION_TYPES, Record);
}

void ASTWriter::WriteOpenCLExtensionDecls(Sema &SemaRef) {
  if (!SemaRef.Context.getLangOpts().OpenCL)
    return;

  RecordData Record;
  for (const auto &I : SemaRef.OpenCLDeclExtMap) {
    Record.push_back(getDeclID(I.first));
    Record.push_back(static_cast<unsigned>(I.second.size()));
    for (auto Ext : I.second)
      AddString(Ext, Record);
  }
  Stream.EmitRecord(OPENCL_EXTENSION_DECLS, Record);
}

void ASTWriter::WriteCUDAPragmas(Sema &SemaRef) {
  if (SemaRef.ForceCUDAHostDeviceDepth > 0) {
    RecordData::value_type Record[] = {SemaRef.ForceCUDAHostDeviceDepth};
    Stream.EmitRecord(CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH, Record);
  }
}

void ASTWriter::WriteObjCCategories() {
  SmallVector<ObjCCategoriesInfo, 2> CategoriesMap;
  RecordData Categories;

  for (unsigned I = 0, N = ObjCClassesWithCategories.size(); I != N; ++I) {
    unsigned Size = 0;
    unsigned StartIndex = Categories.size();

    ObjCInterfaceDecl *Class = ObjCClassesWithCategories[I];

    // Allocate space for the size.
    Categories.push_back(0);

    // Add the categories.
    for (ObjCInterfaceDecl::known_categories_iterator
           Cat = Class->known_categories_begin(),
           CatEnd = Class->known_categories_end();
         Cat != CatEnd; ++Cat, ++Size) {
      assert(getDeclID(*Cat) != 0 && "Bogus category");
      AddDeclRef(*Cat, Categories);
    }

    // Update the size.
    Categories[StartIndex] = Size;

    // Record this interface -> category map.
    ObjCCategoriesInfo CatInfo = { getDeclID(Class), StartIndex };
    CategoriesMap.push_back(CatInfo);
  }

  // Sort the categories map by the definition ID, since the reader will be
  // performing binary searches on this information.
  llvm::array_pod_sort(CategoriesMap.begin(), CategoriesMap.end());

  // Emit the categories map.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(OBJC_CATEGORIES_MAP));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # of entries
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned AbbrevID = Stream.EmitAbbrev(std::move(Abbrev));

  RecordData::value_type Record[] = {OBJC_CATEGORIES_MAP, CategoriesMap.size()};
  Stream.EmitRecordWithBlob(AbbrevID, Record,
                            reinterpret_cast<char *>(CategoriesMap.data()),
                            CategoriesMap.size() * sizeof(ObjCCategoriesInfo));

  // Emit the category lists.
  Stream.EmitRecord(OBJC_CATEGORIES, Categories);
}

void ASTWriter::WriteLateParsedTemplates(Sema &SemaRef) {
  Sema::LateParsedTemplateMapT &LPTMap = SemaRef.LateParsedTemplateMap;

  if (LPTMap.empty())
    return;

  RecordData Record;
  for (auto &LPTMapEntry : LPTMap) {
    const FunctionDecl *FD = LPTMapEntry.first;
    LateParsedTemplate &LPT = *LPTMapEntry.second;
    AddDeclRef(FD, Record);
    AddDeclRef(LPT.D, Record);
    Record.push_back(LPT.Toks.size());

    for (const auto &Tok : LPT.Toks) {
      AddToken(Tok, Record);
    }
  }
  Stream.EmitRecord(LATE_PARSED_TEMPLATE, Record);
}

/// Write the state of 'pragma clang optimize' at the end of the module.
void ASTWriter::WriteOptimizePragmaOptions(Sema &SemaRef) {
  RecordData Record;
  SourceLocation PragmaLoc = SemaRef.getOptimizeOffPragmaLocation();
  AddSourceLocation(PragmaLoc, Record);
  Stream.EmitRecord(OPTIMIZE_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma ms_struct' at the end of the module.
void ASTWriter::WriteMSStructPragmaOptions(Sema &SemaRef) {
  RecordData Record;
  Record.push_back(SemaRef.MSStructPragmaOn ? PMSST_ON : PMSST_OFF);
  Stream.EmitRecord(MSSTRUCT_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma pointers_to_members' at the end of the
//module.
void ASTWriter::WriteMSPointersToMembersPragmaOptions(Sema &SemaRef) {
  RecordData Record;
  Record.push_back(SemaRef.MSPointerToMemberRepresentationMethod);
  AddSourceLocation(SemaRef.ImplicitMSInheritanceAttrLoc, Record);
  Stream.EmitRecord(POINTERS_TO_MEMBERS_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma pack' at the end of the module.
void ASTWriter::WritePackPragmaOptions(Sema &SemaRef) {
  // Don't serialize pragma pack state for modules, since it should only take
  // effect on a per-submodule basis.
  if (WritingModule)
    return;

  RecordData Record;
  Record.push_back(SemaRef.PackStack.CurrentValue);
  AddSourceLocation(SemaRef.PackStack.CurrentPragmaLocation, Record);
  Record.push_back(SemaRef.PackStack.Stack.size());
  for (const auto &StackEntry : SemaRef.PackStack.Stack) {
    Record.push_back(StackEntry.Value);
    AddSourceLocation(StackEntry.PragmaLocation, Record);
    AddSourceLocation(StackEntry.PragmaPushLocation, Record);
    AddString(StackEntry.StackSlotLabel, Record);
  }
  Stream.EmitRecord(PACK_PRAGMA_OPTIONS, Record);
}

void ASTWriter::WriteModuleFileExtension(Sema &SemaRef,
                                         ModuleFileExtensionWriter &Writer) {
  // Enter the extension block.
  Stream.EnterSubblock(EXTENSION_BLOCK_ID, 4);

  // Emit the metadata record abbreviation.
  auto Abv = std::make_shared<llvm::BitCodeAbbrev>();
  Abv->Add(llvm::BitCodeAbbrevOp(EXTENSION_METADATA));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  unsigned Abbrev = Stream.EmitAbbrev(std::move(Abv));

  // Emit the metadata record.
  RecordData Record;
  auto Metadata = Writer.getExtension()->getExtensionMetadata();
  Record.push_back(EXTENSION_METADATA);
  Record.push_back(Metadata.MajorVersion);
  Record.push_back(Metadata.MinorVersion);
  Record.push_back(Metadata.BlockName.size());
  Record.push_back(Metadata.UserInfo.size());
  SmallString<64> Buffer;
  Buffer += Metadata.BlockName;
  Buffer += Metadata.UserInfo;
  Stream.EmitRecordWithBlob(Abbrev, Record, Buffer);

  // Emit the contents of the extension block.
  Writer.writeExtensionContents(SemaRef, Stream);

  // Exit the extension block.
  Stream.ExitBlock();
}

//===----------------------------------------------------------------------===//
// General Serialization Routines
//===----------------------------------------------------------------------===//

void ASTRecordWriter::AddAttr(const Attr *A) {
  auto &Record = *this;
  if (!A)
    return Record.push_back(0);
  Record.push_back(A->getKind() + 1); // FIXME: stable encoding, target attrs
  Record.AddSourceRange(A->getRange());

#include "clang/Serialization/AttrPCHWrite.inc"
}

/// Emit the list of attributes to the specified record.
void ASTRecordWriter::AddAttributes(ArrayRef<const Attr *> Attrs) {
  push_back(Attrs.size());
  for (const auto *A : Attrs)
    AddAttr(A);
}

void ASTWriter::AddToken(const Token &Tok, RecordDataImpl &Record) {
  AddSourceLocation(Tok.getLocation(), Record);
  Record.push_back(Tok.getLength());

  // FIXME: When reading literal tokens, reconstruct the literal pointer
  // if it is needed.
  AddIdentifierRef(Tok.getIdentifierInfo(), Record);
  // FIXME: Should translate token kind to a stable encoding.
  Record.push_back(Tok.getKind());
  // FIXME: Should translate token flags to a stable encoding.
  Record.push_back(Tok.getFlags());
}

void ASTWriter::AddString(StringRef Str, RecordDataImpl &Record) {
  Record.push_back(Str.size());
  Record.insert(Record.end(), Str.begin(), Str.end());
}

bool ASTWriter::PreparePathForOutput(SmallVectorImpl<char> &Path) {
  assert(Context && "should have context when outputting path");

  bool Changed =
      cleanPathForOutput(Context->getSourceManager().getFileManager(), Path);

  // Remove a prefix to make the path relative, if relevant.
  const char *PathBegin = Path.data();
  const char *PathPtr =
      adjustFilenameForRelocatableAST(PathBegin, BaseDirectory);
  if (PathPtr != PathBegin) {
    Path.erase(Path.begin(), Path.begin() + (PathPtr - PathBegin));
    Changed = true;
  }

  return Changed;
}

void ASTWriter::AddPath(StringRef Path, RecordDataImpl &Record) {
  SmallString<128> FilePath(Path);
  PreparePathForOutput(FilePath);
  AddString(FilePath, Record);
}

void ASTWriter::EmitRecordWithPath(unsigned Abbrev, RecordDataRef Record,
                                   StringRef Path) {
  SmallString<128> FilePath(Path);
  PreparePathForOutput(FilePath);
  Stream.EmitRecordWithBlob(Abbrev, Record, FilePath);
}

void ASTWriter::AddVersionTuple(const VersionTuple &Version,
                                RecordDataImpl &Record) {
  Record.push_back(Version.getMajor());
  if (Optional<unsigned> Minor = Version.getMinor())
    Record.push_back(*Minor + 1);
  else
    Record.push_back(0);
  if (Optional<unsigned> Subminor = Version.getSubminor())
    Record.push_back(*Subminor + 1);
  else
    Record.push_back(0);
}

/// Note that the identifier II occurs at the given offset
/// within the identifier table.
void ASTWriter::SetIdentifierOffset(const IdentifierInfo *II, uint32_t Offset) {
  IdentID ID = IdentifierIDs[II];
  // Only store offsets new to this AST file. Other identifier names are looked
  // up earlier in the chain and thus don't need an offset.
  if (ID >= FirstIdentID)
    IdentifierOffsets[ID - FirstIdentID] = Offset;
}

/// Note that the selector Sel occurs at the given offset
/// within the method pool/selector table.
void ASTWriter::SetSelectorOffset(Selector Sel, uint32_t Offset) {
  unsigned ID = SelectorIDs[Sel];
  assert(ID && "Unknown selector");
  // Don't record offsets for selectors that are also available in a different
  // file.
  if (ID < FirstSelectorID)
    return;
  SelectorOffsets[ID - FirstSelectorID] = Offset;
}

ASTWriter::ASTWriter(llvm::BitstreamWriter &Stream,
                     SmallVectorImpl<char> &Buffer, MemoryBufferCache &PCMCache,
                     ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
                     bool IncludeTimestamps)
    : Stream(Stream), Buffer(Buffer), PCMCache(PCMCache),
      IncludeTimestamps(IncludeTimestamps) {
  for (const auto &Ext : Extensions) {
    if (auto Writer = Ext->createExtensionWriter(*this))
      ModuleFileExtensionWriters.push_back(std::move(Writer));
  }
}

ASTWriter::~ASTWriter() {
  llvm::DeleteContainerSeconds(FileDeclIDs);
}

const LangOptions &ASTWriter::getLangOpts() const {
  assert(WritingAST && "can't determine lang opts when not writing AST");
  return Context->getLangOpts();
}

time_t ASTWriter::getTimestampForOutput(const FileEntry *E) const {
  return IncludeTimestamps ? E->getModificationTime() : 0;
}

ASTFileSignature ASTWriter::WriteAST(Sema &SemaRef,
                                     const std::string &OutputFile,
                                     Module *WritingModule, StringRef isysroot,
                                     bool hasErrors) {
  WritingAST = true;

  ASTHasCompilerErrors = hasErrors;

  // Emit the file header.
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit((unsigned)'P', 8);
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit((unsigned)'H', 8);

  WriteBlockInfoBlock();

  Context = &SemaRef.Context;
  PP = &SemaRef.PP;
  this->WritingModule = WritingModule;
  ASTFileSignature Signature =
      WriteASTCore(SemaRef, isysroot, OutputFile, WritingModule);
  Context = nullptr;
  PP = nullptr;
  this->WritingModule = nullptr;
  this->BaseDirectory.clear();

  WritingAST = false;
  if (SemaRef.Context.getLangOpts().ImplicitModules && WritingModule) {
    // Construct MemoryBuffer and update buffer manager.
    PCMCache.addBuffer(OutputFile,
                       llvm::MemoryBuffer::getMemBufferCopy(
                           StringRef(Buffer.begin(), Buffer.size())));
  }
  return Signature;
}

template<typename Vector>
static void AddLazyVectorDecls(ASTWriter &Writer, Vector &Vec,
                               ASTWriter::RecordData &Record) {
  for (typename Vector::iterator I = Vec.begin(nullptr, true), E = Vec.end();
       I != E; ++I) {
    Writer.AddDeclRef(*I, Record);
  }
}

ASTFileSignature ASTWriter::WriteASTCore(Sema &SemaRef, StringRef isysroot,
                                         const std::string &OutputFile,
                                         Module *WritingModule) {
  using namespace llvm;

  bool isModule = WritingModule != nullptr;

  // Make sure that the AST reader knows to finalize itself.
  if (Chain)
    Chain->finalizeForWriting();

  ASTContext &Context = SemaRef.Context;
  Preprocessor &PP = SemaRef.PP;

  // Set up predefined declaration IDs.
  auto RegisterPredefDecl = [&] (Decl *D, PredefinedDeclIDs ID) {
    if (D) {
      assert(D->isCanonicalDecl() && "predefined decl is not canonical");
      DeclIDs[D] = ID;
    }
  };
  RegisterPredefDecl(Context.getTranslationUnitDecl(),
                     PREDEF_DECL_TRANSLATION_UNIT_ID);
  RegisterPredefDecl(Context.ObjCIdDecl, PREDEF_DECL_OBJC_ID_ID);
  RegisterPredefDecl(Context.ObjCSelDecl, PREDEF_DECL_OBJC_SEL_ID);
  RegisterPredefDecl(Context.ObjCClassDecl, PREDEF_DECL_OBJC_CLASS_ID);
  RegisterPredefDecl(Context.ObjCProtocolClassDecl,
                     PREDEF_DECL_OBJC_PROTOCOL_ID);
  RegisterPredefDecl(Context.Int128Decl, PREDEF_DECL_INT_128_ID);
  RegisterPredefDecl(Context.UInt128Decl, PREDEF_DECL_UNSIGNED_INT_128_ID);
  RegisterPredefDecl(Context.ObjCInstanceTypeDecl,
                     PREDEF_DECL_OBJC_INSTANCETYPE_ID);
  RegisterPredefDecl(Context.BuiltinVaListDecl, PREDEF_DECL_BUILTIN_VA_LIST_ID);
  RegisterPredefDecl(Context.VaListTagDecl, PREDEF_DECL_VA_LIST_TAG);
  RegisterPredefDecl(Context.BuiltinMSVaListDecl,
                     PREDEF_DECL_BUILTIN_MS_VA_LIST_ID);
  RegisterPredefDecl(Context.ExternCContext, PREDEF_DECL_EXTERN_C_CONTEXT_ID);
  RegisterPredefDecl(Context.MakeIntegerSeqDecl,
                     PREDEF_DECL_MAKE_INTEGER_SEQ_ID);
  RegisterPredefDecl(Context.CFConstantStringTypeDecl,
                     PREDEF_DECL_CF_CONSTANT_STRING_ID);
  RegisterPredefDecl(Context.CFConstantStringTagDecl,
                     PREDEF_DECL_CF_CONSTANT_STRING_TAG_ID);
  RegisterPredefDecl(Context.TypePackElementDecl,
                     PREDEF_DECL_TYPE_PACK_ELEMENT_ID);

  // Build a record containing all of the tentative definitions in this file, in
  // TentativeDefinitions order.  Generally, this record will be empty for
  // headers.
  RecordData TentativeDefinitions;
  AddLazyVectorDecls(*this, SemaRef.TentativeDefinitions, TentativeDefinitions);

  // Build a record containing all of the file scoped decls in this file.
  RecordData UnusedFileScopedDecls;
  if (!isModule)
    AddLazyVectorDecls(*this, SemaRef.UnusedFileScopedDecls,
                       UnusedFileScopedDecls);

  // Build a record containing all of the delegating constructors we still need
  // to resolve.
  RecordData DelegatingCtorDecls;
  if (!isModule)
    AddLazyVectorDecls(*this, SemaRef.DelegatingCtorDecls, DelegatingCtorDecls);

  // Write the set of weak, undeclared identifiers. We always write the
  // entire table, since later PCH files in a PCH chain are only interested in
  // the results at the end of the chain.
  RecordData WeakUndeclaredIdentifiers;
  for (auto &WeakUndeclaredIdentifier : SemaRef.WeakUndeclaredIdentifiers) {
    IdentifierInfo *II = WeakUndeclaredIdentifier.first;
    WeakInfo &WI = WeakUndeclaredIdentifier.second;
    AddIdentifierRef(II, WeakUndeclaredIdentifiers);
    AddIdentifierRef(WI.getAlias(), WeakUndeclaredIdentifiers);
    AddSourceLocation(WI.getLocation(), WeakUndeclaredIdentifiers);
    WeakUndeclaredIdentifiers.push_back(WI.getUsed());
  }

  // Build a record containing all of the ext_vector declarations.
  RecordData ExtVectorDecls;
  AddLazyVectorDecls(*this, SemaRef.ExtVectorDecls, ExtVectorDecls);

  // Build a record containing all of the VTable uses information.
  RecordData VTableUses;
  if (!SemaRef.VTableUses.empty()) {
    for (unsigned I = 0, N = SemaRef.VTableUses.size(); I != N; ++I) {
      AddDeclRef(SemaRef.VTableUses[I].first, VTableUses);
      AddSourceLocation(SemaRef.VTableUses[I].second, VTableUses);
      VTableUses.push_back(SemaRef.VTablesUsed[SemaRef.VTableUses[I].first]);
    }
  }

  // Build a record containing all of the UnusedLocalTypedefNameCandidates.
  RecordData UnusedLocalTypedefNameCandidates;
  for (const TypedefNameDecl *TD : SemaRef.UnusedLocalTypedefNameCandidates)
    AddDeclRef(TD, UnusedLocalTypedefNameCandidates);

  // Build a record containing all of pending implicit instantiations.
  RecordData PendingInstantiations;
  for (const auto &I : SemaRef.PendingInstantiations) {
    AddDeclRef(I.first, PendingInstantiations);
    AddSourceLocation(I.second, PendingInstantiations);
  }
  assert(SemaRef.PendingLocalImplicitInstantiations.empty() &&
         "There are local ones at end of translation unit!");

  // Build a record containing some declaration references.
  RecordData SemaDeclRefs;
  if (SemaRef.StdNamespace || SemaRef.StdBadAlloc || SemaRef.StdAlignValT) {
    AddDeclRef(SemaRef.getStdNamespace(), SemaDeclRefs);
    AddDeclRef(SemaRef.getStdBadAlloc(), SemaDeclRefs);
    AddDeclRef(SemaRef.getStdAlignValT(), SemaDeclRefs);
  }

  RecordData CUDASpecialDeclRefs;
  if (Context.getcudaConfigureCallDecl()) {
    AddDeclRef(Context.getcudaConfigureCallDecl(), CUDASpecialDeclRefs);
  }

  // Build a record containing all of the known namespaces.
  RecordData KnownNamespaces;
  for (const auto &I : SemaRef.KnownNamespaces) {
    if (!I.second)
      AddDeclRef(I.first, KnownNamespaces);
  }

  // Build a record of all used, undefined objects that require definitions.
  RecordData UndefinedButUsed;

  SmallVector<std::pair<NamedDecl *, SourceLocation>, 16> Undefined;
  SemaRef.getUndefinedButUsed(Undefined);
  for (const auto &I : Undefined) {
    AddDeclRef(I.first, UndefinedButUsed);
    AddSourceLocation(I.second, UndefinedButUsed);
  }

  // Build a record containing all delete-expressions that we would like to
  // analyze later in AST.
  RecordData DeleteExprsToAnalyze;

  if (!isModule) {
    for (const auto &DeleteExprsInfo :
         SemaRef.getMismatchingDeleteExpressions()) {
      AddDeclRef(DeleteExprsInfo.first, DeleteExprsToAnalyze);
      DeleteExprsToAnalyze.push_back(DeleteExprsInfo.second.size());
      for (const auto &DeleteLoc : DeleteExprsInfo.second) {
        AddSourceLocation(DeleteLoc.first, DeleteExprsToAnalyze);
        DeleteExprsToAnalyze.push_back(DeleteLoc.second);
      }
    }
  }

  // Write the control block
  WriteControlBlock(PP, Context, isysroot, OutputFile);

  // Write the remaining AST contents.
  Stream.EnterSubblock(AST_BLOCK_ID, 5);

  // This is so that older clang versions, before the introduction
  // of the control block, can read and reject the newer PCH format.
  {
    RecordData Record = {VERSION_MAJOR};
    Stream.EmitRecord(METADATA_OLD_FORMAT, Record);
  }

  // Create a lexical update block containing all of the declarations in the
  // translation unit that do not come from other AST files.
  const TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
  SmallVector<uint32_t, 128> NewGlobalKindDeclPairs;
  for (const auto *D : TU->noload_decls()) {
    if (!D->isFromASTFile()) {
      NewGlobalKindDeclPairs.push_back(D->getKind());
      NewGlobalKindDeclPairs.push_back(GetDeclRef(D));
    }
  }

  auto Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(llvm::BitCodeAbbrevOp(TU_UPDATE_LEXICAL));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  unsigned TuUpdateLexicalAbbrev = Stream.EmitAbbrev(std::move(Abv));
  {
    RecordData::value_type Record[] = {TU_UPDATE_LEXICAL};
    Stream.EmitRecordWithBlob(TuUpdateLexicalAbbrev, Record,
                              bytes(NewGlobalKindDeclPairs));
  }

  // And a visible updates block for the translation unit.
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(llvm::BitCodeAbbrevOp(UPDATE_VISIBLE));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  UpdateVisibleAbbrev = Stream.EmitAbbrev(std::move(Abv));
  WriteDeclContextVisibleUpdate(TU);

  // If we have any extern "C" names, write out a visible update for them.
  if (Context.ExternCContext)
    WriteDeclContextVisibleUpdate(Context.ExternCContext);

  // If the translation unit has an anonymous namespace, and we don't already
  // have an update block for it, write it as an update block.
  // FIXME: Why do we not do this if there's already an update block?
  if (NamespaceDecl *NS = TU->getAnonymousNamespace()) {
    ASTWriter::UpdateRecord &Record = DeclUpdates[TU];
    if (Record.empty())
      Record.push_back(DeclUpdate(UPD_CXX_ADDED_ANONYMOUS_NAMESPACE, NS));
  }

  // Add update records for all mangling numbers and static local numbers.
  // These aren't really update records, but this is a convenient way of
  // tagging this rare extra data onto the declarations.
  for (const auto &Number : Context.MangleNumbers)
    if (!Number.first->isFromASTFile())
      DeclUpdates[Number.first].push_back(DeclUpdate(UPD_MANGLING_NUMBER,
                                                     Number.second));
  for (const auto &Number : Context.StaticLocalNumbers)
    if (!Number.first->isFromASTFile())
      DeclUpdates[Number.first].push_back(DeclUpdate(UPD_STATIC_LOCAL_NUMBER,
                                                     Number.second));

  // Make sure visible decls, added to DeclContexts previously loaded from
  // an AST file, are registered for serialization. Likewise for template
  // specializations added to imported templates.
  for (const auto *I : DeclsToEmitEvenIfUnreferenced) {
    GetDeclRef(I);
  }

  // Make sure all decls associated with an identifier are registered for
  // serialization, if we're storing decls with identifiers.
  if (!WritingModule || !getLangOpts().CPlusPlus) {
    llvm::SmallVector<const IdentifierInfo*, 256> IIs;
    for (const auto &ID : PP.getIdentifierTable()) {
      const IdentifierInfo *II = ID.second;
      if (!Chain || !II->isFromAST() || II->hasChangedSinceDeserialization())
        IIs.push_back(II);
    }
    // Sort the identifiers to visit based on their name.
    llvm::sort(IIs, llvm::less_ptr<IdentifierInfo>());
    for (const IdentifierInfo *II : IIs) {
      for (IdentifierResolver::iterator D = SemaRef.IdResolver.begin(II),
                                     DEnd = SemaRef.IdResolver.end();
           D != DEnd; ++D) {
        GetDeclRef(*D);
      }
    }
  }

  // For method pool in the module, if it contains an entry for a selector,
  // the entry should be complete, containing everything introduced by that
  // module and all modules it imports. It's possible that the entry is out of
  // date, so we need to pull in the new content here.

  // It's possible that updateOutOfDateSelector can update SelectorIDs. To be
  // safe, we copy all selectors out.
  llvm::SmallVector<Selector, 256> AllSelectors;
  for (auto &SelectorAndID : SelectorIDs)
    AllSelectors.push_back(SelectorAndID.first);
  for (auto &Selector : AllSelectors)
    SemaRef.updateOutOfDateSelector(Selector);

  // Form the record of special types.
  RecordData SpecialTypes;
  AddTypeRef(Context.getRawCFConstantStringType(), SpecialTypes);
  AddTypeRef(Context.getFILEType(), SpecialTypes);
  AddTypeRef(Context.getjmp_bufType(), SpecialTypes);
  AddTypeRef(Context.getsigjmp_bufType(), SpecialTypes);
  AddTypeRef(Context.ObjCIdRedefinitionType, SpecialTypes);
  AddTypeRef(Context.ObjCClassRedefinitionType, SpecialTypes);
  AddTypeRef(Context.ObjCSelRedefinitionType, SpecialTypes);
  AddTypeRef(Context.getucontext_tType(), SpecialTypes);

  if (Chain) {
    // Write the mapping information describing our module dependencies and how
    // each of those modules were mapped into our own offset/ID space, so that
    // the reader can build the appropriate mapping to its own offset/ID space.
    // The map consists solely of a blob with the following format:
    // *(module-kind:i8
    //   module-name-len:i16 module-name:len*i8
    //   source-location-offset:i32
    //   identifier-id:i32
    //   preprocessed-entity-id:i32
    //   macro-definition-id:i32
    //   submodule-id:i32
    //   selector-id:i32
    //   declaration-id:i32
    //   c++-base-specifiers-id:i32
    //   type-id:i32)
    //
    // module-kind is the ModuleKind enum value. If it is MK_PrebuiltModule or
    // MK_ExplicitModule, then the module-name is the module name. Otherwise,
    // it is the module file name.
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(MODULE_OFFSET_MAP));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned ModuleOffsetMapAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
    SmallString<2048> Buffer;
    {
      llvm::raw_svector_ostream Out(Buffer);
      for (ModuleFile &M : Chain->ModuleMgr) {
        using namespace llvm::support;

        endian::Writer LE(Out, little);
        LE.write<uint8_t>(static_cast<uint8_t>(M.Kind));
        StringRef Name =
          M.Kind == MK_PrebuiltModule || M.Kind == MK_ExplicitModule
          ? M.ModuleName
          : M.FileName;
        LE.write<uint16_t>(Name.size());
        Out.write(Name.data(), Name.size());

        // Note: if a base ID was uint max, it would not be possible to load
        // another module after it or have more than one entity inside it.
        uint32_t None = std::numeric_limits<uint32_t>::max();

        auto writeBaseIDOrNone = [&](uint32_t BaseID, bool ShouldWrite) {
          assert(BaseID < std::numeric_limits<uint32_t>::max() && "base id too high");
          if (ShouldWrite)
            LE.write<uint32_t>(BaseID);
          else
            LE.write<uint32_t>(None);
        };

        // These values should be unique within a chain, since they will be read
        // as keys into ContinuousRangeMaps.
        writeBaseIDOrNone(M.SLocEntryBaseOffset, M.LocalNumSLocEntries);
        writeBaseIDOrNone(M.BaseIdentifierID, M.LocalNumIdentifiers);
        writeBaseIDOrNone(M.BaseMacroID, M.LocalNumMacros);
        writeBaseIDOrNone(M.BasePreprocessedEntityID,
                          M.NumPreprocessedEntities);
        writeBaseIDOrNone(M.BaseSubmoduleID, M.LocalNumSubmodules);
        writeBaseIDOrNone(M.BaseSelectorID, M.LocalNumSelectors);
        writeBaseIDOrNone(M.BaseDeclID, M.LocalNumDecls);
        writeBaseIDOrNone(M.BaseTypeIndex, M.LocalNumTypes);
      }
    }
    RecordData::value_type Record[] = {MODULE_OFFSET_MAP};
    Stream.EmitRecordWithBlob(ModuleOffsetMapAbbrev, Record,
                              Buffer.data(), Buffer.size());
  }

  RecordData DeclUpdatesOffsetsRecord;

  // Keep writing types, declarations, and declaration update records
  // until we've emitted all of them.
  Stream.EnterSubblock(DECLTYPES_BLOCK_ID, /*bits for abbreviations*/5);
  WriteTypeAbbrevs();
  WriteDeclAbbrevs();
  do {
    WriteDeclUpdatesBlocks(DeclUpdatesOffsetsRecord);
    while (!DeclTypesToEmit.empty()) {
      DeclOrType DOT = DeclTypesToEmit.front();
      DeclTypesToEmit.pop();
      if (DOT.isType())
        WriteType(DOT.getType());
      else
        WriteDecl(Context, DOT.getDecl());
    }
  } while (!DeclUpdates.empty());
  Stream.ExitBlock();

  DoneWritingDeclsAndTypes = true;

  // These things can only be done once we've written out decls and types.
  WriteTypeDeclOffsets();
  if (!DeclUpdatesOffsetsRecord.empty())
    Stream.EmitRecord(DECL_UPDATE_OFFSETS, DeclUpdatesOffsetsRecord);
  WriteFileDeclIDsMap();
  WriteSourceManagerBlock(Context.getSourceManager(), PP);
  WriteComments();
  WritePreprocessor(PP, isModule);
  WriteHeaderSearch(PP.getHeaderSearchInfo());
  WriteSelectors(SemaRef);
  WriteReferencedSelectorsPool(SemaRef);
  WriteLateParsedTemplates(SemaRef);
  WriteIdentifierTable(PP, SemaRef.IdResolver, isModule);
  WriteFPPragmaOptions(SemaRef.getFPOptions());
  WriteOpenCLExtensions(SemaRef);
  WriteOpenCLExtensionTypes(SemaRef);
  WriteCUDAPragmas(SemaRef);

  // If we're emitting a module, write out the submodule information.
  if (WritingModule)
    WriteSubmodules(WritingModule);

  // We need to have information about submodules to correctly deserialize
  // decls from OpenCLExtensionDecls block
  WriteOpenCLExtensionDecls(SemaRef);

  Stream.EmitRecord(SPECIAL_TYPES, SpecialTypes);

  // Write the record containing external, unnamed definitions.
  if (!EagerlyDeserializedDecls.empty())
    Stream.EmitRecord(EAGERLY_DESERIALIZED_DECLS, EagerlyDeserializedDecls);

  if (!ModularCodegenDecls.empty())
    Stream.EmitRecord(MODULAR_CODEGEN_DECLS, ModularCodegenDecls);

  // Write the record containing tentative definitions.
  if (!TentativeDefinitions.empty())
    Stream.EmitRecord(TENTATIVE_DEFINITIONS, TentativeDefinitions);

  // Write the record containing unused file scoped decls.
  if (!UnusedFileScopedDecls.empty())
    Stream.EmitRecord(UNUSED_FILESCOPED_DECLS, UnusedFileScopedDecls);

  // Write the record containing weak undeclared identifiers.
  if (!WeakUndeclaredIdentifiers.empty())
    Stream.EmitRecord(WEAK_UNDECLARED_IDENTIFIERS,
                      WeakUndeclaredIdentifiers);

  // Write the record containing ext_vector type names.
  if (!ExtVectorDecls.empty())
    Stream.EmitRecord(EXT_VECTOR_DECLS, ExtVectorDecls);

  // Write the record containing VTable uses information.
  if (!VTableUses.empty())
    Stream.EmitRecord(VTABLE_USES, VTableUses);

  // Write the record containing potentially unused local typedefs.
  if (!UnusedLocalTypedefNameCandidates.empty())
    Stream.EmitRecord(UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES,
                      UnusedLocalTypedefNameCandidates);

  // Write the record containing pending implicit instantiations.
  if (!PendingInstantiations.empty())
    Stream.EmitRecord(PENDING_IMPLICIT_INSTANTIATIONS, PendingInstantiations);

  // Write the record containing declaration references of Sema.
  if (!SemaDeclRefs.empty())
    Stream.EmitRecord(SEMA_DECL_REFS, SemaDeclRefs);

  // Write the record containing CUDA-specific declaration references.
  if (!CUDASpecialDeclRefs.empty())
    Stream.EmitRecord(CUDA_SPECIAL_DECL_REFS, CUDASpecialDeclRefs);

  // Write the delegating constructors.
  if (!DelegatingCtorDecls.empty())
    Stream.EmitRecord(DELEGATING_CTORS, DelegatingCtorDecls);

  // Write the known namespaces.
  if (!KnownNamespaces.empty())
    Stream.EmitRecord(KNOWN_NAMESPACES, KnownNamespaces);

  // Write the undefined internal functions and variables, and inline functions.
  if (!UndefinedButUsed.empty())
    Stream.EmitRecord(UNDEFINED_BUT_USED, UndefinedButUsed);

  if (!DeleteExprsToAnalyze.empty())
    Stream.EmitRecord(DELETE_EXPRS_TO_ANALYZE, DeleteExprsToAnalyze);

  // Write the visible updates to DeclContexts.
  for (auto *DC : UpdatedDeclContexts)
    WriteDeclContextVisibleUpdate(DC);

  if (!WritingModule) {
    // Write the submodules that were imported, if any.
    struct ModuleInfo {
      uint64_t ID;
      Module *M;
      ModuleInfo(uint64_t ID, Module *M) : ID(ID), M(M) {}
    };
    llvm::SmallVector<ModuleInfo, 64> Imports;
    for (const auto *I : Context.local_imports()) {
      assert(SubmoduleIDs.find(I->getImportedModule()) != SubmoduleIDs.end());
      Imports.push_back(ModuleInfo(SubmoduleIDs[I->getImportedModule()],
                         I->getImportedModule()));
    }

    if (!Imports.empty()) {
      auto Cmp = [](const ModuleInfo &A, const ModuleInfo &B) {
        return A.ID < B.ID;
      };
      auto Eq = [](const ModuleInfo &A, const ModuleInfo &B) {
        return A.ID == B.ID;
      };

      // Sort and deduplicate module IDs.
      llvm::sort(Imports, Cmp);
      Imports.erase(std::unique(Imports.begin(), Imports.end(), Eq),
                    Imports.end());

      RecordData ImportedModules;
      for (const auto &Import : Imports) {
        ImportedModules.push_back(Import.ID);
        // FIXME: If the module has macros imported then later has declarations
        // imported, this location won't be the right one as a location for the
        // declaration imports.
        AddSourceLocation(PP.getModuleImportLoc(Import.M), ImportedModules);
      }

      Stream.EmitRecord(IMPORTED_MODULES, ImportedModules);
    }
  }

  WriteObjCCategories();
  if(!WritingModule) {
    WriteOptimizePragmaOptions(SemaRef);
    WriteMSStructPragmaOptions(SemaRef);
    WriteMSPointersToMembersPragmaOptions(SemaRef);
  }
  WritePackPragmaOptions(SemaRef);

  // Some simple statistics
  RecordData::value_type Record[] = {
      NumStatements, NumMacros, NumLexicalDeclContexts, NumVisibleDeclContexts};
  Stream.EmitRecord(STATISTICS, Record);
  Stream.ExitBlock();

  // Write the module file extension blocks.
  for (const auto &ExtWriter : ModuleFileExtensionWriters)
    WriteModuleFileExtension(SemaRef, *ExtWriter);

  return writeUnhashedControlBlock(PP, Context);
}

void ASTWriter::WriteDeclUpdatesBlocks(RecordDataImpl &OffsetsRecord) {
  if (DeclUpdates.empty())
    return;

  DeclUpdateMap LocalUpdates;
  LocalUpdates.swap(DeclUpdates);

  for (auto &DeclUpdate : LocalUpdates) {
    const Decl *D = DeclUpdate.first;

    bool HasUpdatedBody = false;
    RecordData RecordData;
    ASTRecordWriter Record(*this, RecordData);
    for (auto &Update : DeclUpdate.second) {
      DeclUpdateKind Kind = (DeclUpdateKind)Update.getKind();

      // An updated body is emitted last, so that the reader doesn't need
      // to skip over the lazy body to reach statements for other records.
      if (Kind == UPD_CXX_ADDED_FUNCTION_DEFINITION)
        HasUpdatedBody = true;
      else
        Record.push_back(Kind);

      switch (Kind) {
      case UPD_CXX_ADDED_IMPLICIT_MEMBER:
      case UPD_CXX_ADDED_TEMPLATE_SPECIALIZATION:
      case UPD_CXX_ADDED_ANONYMOUS_NAMESPACE:
        assert(Update.getDecl() && "no decl to add?");
        Record.push_back(GetDeclRef(Update.getDecl()));
        break;

      case UPD_CXX_ADDED_FUNCTION_DEFINITION:
        break;

      case UPD_CXX_POINT_OF_INSTANTIATION:
        // FIXME: Do we need to also save the template specialization kind here?
        Record.AddSourceLocation(Update.getLoc());
        break;

      case UPD_CXX_ADDED_VAR_DEFINITION: {
        const VarDecl *VD = cast<VarDecl>(D);
        Record.push_back(VD->isInline());
        Record.push_back(VD->isInlineSpecified());
        if (VD->getInit()) {
          Record.push_back(!VD->isInitKnownICE() ? 1
                                                 : (VD->isInitICE() ? 3 : 2));
          Record.AddStmt(const_cast<Expr*>(VD->getInit()));
        } else {
          Record.push_back(0);
        }
        break;
      }

      case UPD_CXX_INSTANTIATED_DEFAULT_ARGUMENT:
        Record.AddStmt(const_cast<Expr *>(
            cast<ParmVarDecl>(Update.getDecl())->getDefaultArg()));
        break;

      case UPD_CXX_INSTANTIATED_DEFAULT_MEMBER_INITIALIZER:
        Record.AddStmt(
            cast<FieldDecl>(Update.getDecl())->getInClassInitializer());
        break;

      case UPD_CXX_INSTANTIATED_CLASS_DEFINITION: {
        auto *RD = cast<CXXRecordDecl>(D);
        UpdatedDeclContexts.insert(RD->getPrimaryContext());
        Record.push_back(RD->isParamDestroyedInCallee());
        Record.push_back(RD->getArgPassingRestrictions());
        Record.AddCXXDefinitionData(RD);
        Record.AddOffset(WriteDeclContextLexicalBlock(
            *Context, const_cast<CXXRecordDecl *>(RD)));

        // This state is sometimes updated by template instantiation, when we
        // switch from the specialization referring to the template declaration
        // to it referring to the template definition.
        if (auto *MSInfo = RD->getMemberSpecializationInfo()) {
          Record.push_back(MSInfo->getTemplateSpecializationKind());
          Record.AddSourceLocation(MSInfo->getPointOfInstantiation());
        } else {
          auto *Spec = cast<ClassTemplateSpecializationDecl>(RD);
          Record.push_back(Spec->getTemplateSpecializationKind());
          Record.AddSourceLocation(Spec->getPointOfInstantiation());

          // The instantiation might have been resolved to a partial
          // specialization. If so, record which one.
          auto From = Spec->getInstantiatedFrom();
          if (auto PartialSpec =
                From.dyn_cast<ClassTemplatePartialSpecializationDecl*>()) {
            Record.push_back(true);
            Record.AddDeclRef(PartialSpec);
            Record.AddTemplateArgumentList(
                &Spec->getTemplateInstantiationArgs());
          } else {
            Record.push_back(false);
          }
        }
        Record.push_back(RD->getTagKind());
        Record.AddSourceLocation(RD->getLocation());
        Record.AddSourceLocation(RD->getBeginLoc());
        Record.AddSourceRange(RD->getBraceRange());

        // Instantiation may change attributes; write them all out afresh.
        Record.push_back(D->hasAttrs());
        if (D->hasAttrs())
          Record.AddAttributes(D->getAttrs());

        // FIXME: Ensure we don't get here for explicit instantiations.
        break;
      }

      case UPD_CXX_RESOLVED_DTOR_DELETE:
        Record.AddDeclRef(Update.getDecl());
        Record.AddStmt(cast<CXXDestructorDecl>(D)->getOperatorDeleteThisArg());
        break;

      case UPD_CXX_RESOLVED_EXCEPTION_SPEC:
        addExceptionSpec(
            cast<FunctionDecl>(D)->getType()->castAs<FunctionProtoType>(),
            Record);
        break;

      case UPD_CXX_DEDUCED_RETURN_TYPE:
        Record.push_back(GetOrCreateTypeID(Update.getType()));
        break;

      case UPD_DECL_MARKED_USED:
        break;

      case UPD_MANGLING_NUMBER:
      case UPD_STATIC_LOCAL_NUMBER:
        Record.push_back(Update.getNumber());
        break;

      case UPD_DECL_MARKED_OPENMP_THREADPRIVATE:
        Record.AddSourceRange(
            D->getAttr<OMPThreadPrivateDeclAttr>()->getRange());
        break;

      case UPD_DECL_MARKED_OPENMP_DECLARETARGET:
        Record.push_back(D->getAttr<OMPDeclareTargetDeclAttr>()->getMapType());
        Record.AddSourceRange(
            D->getAttr<OMPDeclareTargetDeclAttr>()->getRange());
        break;

      case UPD_DECL_EXPORTED:
        Record.push_back(getSubmoduleID(Update.getModule()));
        break;

      case UPD_ADDED_ATTR_TO_RECORD:
        Record.AddAttributes(llvm::makeArrayRef(Update.getAttr()));
        break;
      }
    }

    if (HasUpdatedBody) {
      const auto *Def = cast<FunctionDecl>(D);
      Record.push_back(UPD_CXX_ADDED_FUNCTION_DEFINITION);
      Record.push_back(Def->isInlined());
      Record.AddSourceLocation(Def->getInnerLocStart());
      Record.AddFunctionDefinition(Def);
    }

    OffsetsRecord.push_back(GetDeclRef(D));
    OffsetsRecord.push_back(Record.Emit(DECL_UPDATES));
  }
}

void ASTWriter::AddSourceLocation(SourceLocation Loc, RecordDataImpl &Record) {
  uint32_t Raw = Loc.getRawEncoding();
  Record.push_back((Raw << 1) | (Raw >> 31));
}

void ASTWriter::AddSourceRange(SourceRange Range, RecordDataImpl &Record) {
  AddSourceLocation(Range.getBegin(), Record);
  AddSourceLocation(Range.getEnd(), Record);
}

void ASTRecordWriter::AddAPInt(const llvm::APInt &Value) {
  Record->push_back(Value.getBitWidth());
  const uint64_t *Words = Value.getRawData();
  Record->append(Words, Words + Value.getNumWords());
}

void ASTRecordWriter::AddAPSInt(const llvm::APSInt &Value) {
  Record->push_back(Value.isUnsigned());
  AddAPInt(Value);
}

void ASTRecordWriter::AddAPFloat(const llvm::APFloat &Value) {
  AddAPInt(Value.bitcastToAPInt());
}

void ASTWriter::AddIdentifierRef(const IdentifierInfo *II, RecordDataImpl &Record) {
  Record.push_back(getIdentifierRef(II));
}

IdentID ASTWriter::getIdentifierRef(const IdentifierInfo *II) {
  if (!II)
    return 0;

  IdentID &ID = IdentifierIDs[II];
  if (ID == 0)
    ID = NextIdentID++;
  return ID;
}

MacroID ASTWriter::getMacroRef(MacroInfo *MI, const IdentifierInfo *Name) {
  // Don't emit builtin macros like __LINE__ to the AST file unless they
  // have been redefined by the header (in which case they are not
  // isBuiltinMacro).
  if (!MI || MI->isBuiltinMacro())
    return 0;

  MacroID &ID = MacroIDs[MI];
  if (ID == 0) {
    ID = NextMacroID++;
    MacroInfoToEmitData Info = { Name, MI, ID };
    MacroInfosToEmit.push_back(Info);
  }
  return ID;
}

MacroID ASTWriter::getMacroID(MacroInfo *MI) {
  if (!MI || MI->isBuiltinMacro())
    return 0;

  assert(MacroIDs.find(MI) != MacroIDs.end() && "Macro not emitted!");
  return MacroIDs[MI];
}

uint64_t ASTWriter::getMacroDirectivesOffset(const IdentifierInfo *Name) {
  return IdentMacroDirectivesOffsetMap.lookup(Name);
}

void ASTRecordWriter::AddSelectorRef(const Selector SelRef) {
  Record->push_back(Writer->getSelectorRef(SelRef));
}

SelectorID ASTWriter::getSelectorRef(Selector Sel) {
  if (Sel.getAsOpaquePtr() == nullptr) {
    return 0;
  }

  SelectorID SID = SelectorIDs[Sel];
  if (SID == 0 && Chain) {
    // This might trigger a ReadSelector callback, which will set the ID for
    // this selector.
    Chain->LoadSelector(Sel);
    SID = SelectorIDs[Sel];
  }
  if (SID == 0) {
    SID = NextSelectorID++;
    SelectorIDs[Sel] = SID;
  }
  return SID;
}

void ASTRecordWriter::AddCXXTemporary(const CXXTemporary *Temp) {
  AddDeclRef(Temp->getDestructor());
}

void ASTRecordWriter::AddTemplateArgumentLocInfo(
    TemplateArgument::ArgKind Kind, const TemplateArgumentLocInfo &Arg) {
  switch (Kind) {
  case TemplateArgument::Expression:
    AddStmt(Arg.getAsExpr());
    break;
  case TemplateArgument::Type:
    AddTypeSourceInfo(Arg.getAsTypeSourceInfo());
    break;
  case TemplateArgument::Template:
    AddNestedNameSpecifierLoc(Arg.getTemplateQualifierLoc());
    AddSourceLocation(Arg.getTemplateNameLoc());
    break;
  case TemplateArgument::TemplateExpansion:
    AddNestedNameSpecifierLoc(Arg.getTemplateQualifierLoc());
    AddSourceLocation(Arg.getTemplateNameLoc());
    AddSourceLocation(Arg.getTemplateEllipsisLoc());
    break;
  case TemplateArgument::Null:
  case TemplateArgument::Integral:
  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::Pack:
    // FIXME: Is this right?
    break;
  }
}

void ASTRecordWriter::AddTemplateArgumentLoc(const TemplateArgumentLoc &Arg) {
  AddTemplateArgument(Arg.getArgument());

  if (Arg.getArgument().getKind() == TemplateArgument::Expression) {
    bool InfoHasSameExpr
      = Arg.getArgument().getAsExpr() == Arg.getLocInfo().getAsExpr();
    Record->push_back(InfoHasSameExpr);
    if (InfoHasSameExpr)
      return; // Avoid storing the same expr twice.
  }
  AddTemplateArgumentLocInfo(Arg.getArgument().getKind(), Arg.getLocInfo());
}

void ASTRecordWriter::AddTypeSourceInfo(TypeSourceInfo *TInfo) {
  if (!TInfo) {
    AddTypeRef(QualType());
    return;
  }

  AddTypeRef(TInfo->getType());
  AddTypeLoc(TInfo->getTypeLoc());
}

void ASTRecordWriter::AddTypeLoc(TypeLoc TL) {
  TypeLocWriter TLW(*this);
  for (; !TL.isNull(); TL = TL.getNextTypeLoc())
    TLW.Visit(TL);
}

void ASTWriter::AddTypeRef(QualType T, RecordDataImpl &Record) {
  Record.push_back(GetOrCreateTypeID(T));
}

TypeID ASTWriter::GetOrCreateTypeID(QualType T) {
  assert(Context);
  return MakeTypeID(*Context, T, [&](QualType T) -> TypeIdx {
    if (T.isNull())
      return TypeIdx();
    assert(!T.getLocalFastQualifiers());

    TypeIdx &Idx = TypeIdxs[T];
    if (Idx.getIndex() == 0) {
      if (DoneWritingDeclsAndTypes) {
        assert(0 && "New type seen after serializing all the types to emit!");
        return TypeIdx();
      }

      // We haven't seen this type before. Assign it a new ID and put it
      // into the queue of types to emit.
      Idx = TypeIdx(NextTypeID++);
      DeclTypesToEmit.push(T);
    }
    return Idx;
  });
}

TypeID ASTWriter::getTypeID(QualType T) const {
  assert(Context);
  return MakeTypeID(*Context, T, [&](QualType T) -> TypeIdx {
    if (T.isNull())
      return TypeIdx();
    assert(!T.getLocalFastQualifiers());

    TypeIdxMap::const_iterator I = TypeIdxs.find(T);
    assert(I != TypeIdxs.end() && "Type not emitted!");
    return I->second;
  });
}

void ASTWriter::AddDeclRef(const Decl *D, RecordDataImpl &Record) {
  Record.push_back(GetDeclRef(D));
}

DeclID ASTWriter::GetDeclRef(const Decl *D) {
  assert(WritingAST && "Cannot request a declaration ID before AST writing");

  if (!D) {
    return 0;
  }

  // If D comes from an AST file, its declaration ID is already known and
  // fixed.
  if (D->isFromASTFile())
    return D->getGlobalID();

  assert(!(reinterpret_cast<uintptr_t>(D) & 0x01) && "Invalid decl pointer");
  DeclID &ID = DeclIDs[D];
  if (ID == 0) {
    if (DoneWritingDeclsAndTypes) {
      assert(0 && "New decl seen after serializing all the decls to emit!");
      return 0;
    }

    // We haven't seen this declaration before. Give it a new ID and
    // enqueue it in the list of declarations to emit.
    ID = NextDeclID++;
    DeclTypesToEmit.push(const_cast<Decl *>(D));
  }

  return ID;
}

DeclID ASTWriter::getDeclID(const Decl *D) {
  if (!D)
    return 0;

  // If D comes from an AST file, its declaration ID is already known and
  // fixed.
  if (D->isFromASTFile())
    return D->getGlobalID();

  assert(DeclIDs.find(D) != DeclIDs.end() && "Declaration not emitted!");
  return DeclIDs[D];
}

void ASTWriter::associateDeclWithFile(const Decl *D, DeclID ID) {
  assert(ID);
  assert(D);

  SourceLocation Loc = D->getLocation();
  if (Loc.isInvalid())
    return;

  // We only keep track of the file-level declarations of each file.
  if (!D->getLexicalDeclContext()->isFileContext())
    return;
  // FIXME: ParmVarDecls that are part of a function type of a parameter of
  // a function/objc method, should not have TU as lexical context.
  // TemplateTemplateParmDecls that are part of an alias template, should not
  // have TU as lexical context.
  if (isa<ParmVarDecl>(D) || isa<TemplateTemplateParmDecl>(D))
    return;

  SourceManager &SM = Context->getSourceManager();
  SourceLocation FileLoc = SM.getFileLoc(Loc);
  assert(SM.isLocalSourceLocation(FileLoc));
  FileID FID;
  unsigned Offset;
  std::tie(FID, Offset) = SM.getDecomposedLoc(FileLoc);
  if (FID.isInvalid())
    return;
  assert(SM.getSLocEntry(FID).isFile());

  DeclIDInFileInfo *&Info = FileDeclIDs[FID];
  if (!Info)
    Info = new DeclIDInFileInfo();

  std::pair<unsigned, serialization::DeclID> LocDecl(Offset, ID);
  LocDeclIDsTy &Decls = Info->DeclIDs;

  if (Decls.empty() || Decls.back().first <= Offset) {
    Decls.push_back(LocDecl);
    return;
  }

  LocDeclIDsTy::iterator I =
      std::upper_bound(Decls.begin(), Decls.end(), LocDecl, llvm::less_first());

  Decls.insert(I, LocDecl);
}

void ASTRecordWriter::AddDeclarationName(DeclarationName Name) {
  // FIXME: Emit a stable enum for NameKind.  0 = Identifier etc.
  Record->push_back(Name.getNameKind());
  switch (Name.getNameKind()) {
  case DeclarationName::Identifier:
    AddIdentifierRef(Name.getAsIdentifierInfo());
    break;

  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
    AddSelectorRef(Name.getObjCSelector());
    break;

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    AddTypeRef(Name.getCXXNameType());
    break;

  case DeclarationName::CXXDeductionGuideName:
    AddDeclRef(Name.getCXXDeductionGuideTemplate());
    break;

  case DeclarationName::CXXOperatorName:
    Record->push_back(Name.getCXXOverloadedOperator());
    break;

  case DeclarationName::CXXLiteralOperatorName:
    AddIdentifierRef(Name.getCXXLiteralIdentifier());
    break;

  case DeclarationName::CXXUsingDirective:
    // No extra data to emit
    break;
  }
}

unsigned ASTWriter::getAnonymousDeclarationNumber(const NamedDecl *D) {
  assert(needsAnonymousDeclarationNumber(D) &&
         "expected an anonymous declaration");

  // Number the anonymous declarations within this context, if we've not
  // already done so.
  auto It = AnonymousDeclarationNumbers.find(D);
  if (It == AnonymousDeclarationNumbers.end()) {
    auto *DC = D->getLexicalDeclContext();
    numberAnonymousDeclsWithin(DC, [&](const NamedDecl *ND, unsigned Number) {
      AnonymousDeclarationNumbers[ND] = Number;
    });

    It = AnonymousDeclarationNumbers.find(D);
    assert(It != AnonymousDeclarationNumbers.end() &&
           "declaration not found within its lexical context");
  }

  return It->second;
}

void ASTRecordWriter::AddDeclarationNameLoc(const DeclarationNameLoc &DNLoc,
                                            DeclarationName Name) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    AddTypeSourceInfo(DNLoc.NamedType.TInfo);
    break;

  case DeclarationName::CXXOperatorName:
    AddSourceLocation(SourceLocation::getFromRawEncoding(
        DNLoc.CXXOperatorName.BeginOpNameLoc));
    AddSourceLocation(
        SourceLocation::getFromRawEncoding(DNLoc.CXXOperatorName.EndOpNameLoc));
    break;

  case DeclarationName::CXXLiteralOperatorName:
    AddSourceLocation(SourceLocation::getFromRawEncoding(
        DNLoc.CXXLiteralOperatorName.OpNameLoc));
    break;

  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    break;
  }
}

void ASTRecordWriter::AddDeclarationNameInfo(
    const DeclarationNameInfo &NameInfo) {
  AddDeclarationName(NameInfo.getName());
  AddSourceLocation(NameInfo.getLoc());
  AddDeclarationNameLoc(NameInfo.getInfo(), NameInfo.getName());
}

void ASTRecordWriter::AddQualifierInfo(const QualifierInfo &Info) {
  AddNestedNameSpecifierLoc(Info.QualifierLoc);
  Record->push_back(Info.NumTemplParamLists);
  for (unsigned i = 0, e = Info.NumTemplParamLists; i != e; ++i)
    AddTemplateParameterList(Info.TemplParamLists[i]);
}

void ASTRecordWriter::AddNestedNameSpecifier(NestedNameSpecifier *NNS) {
  // Nested name specifiers usually aren't too long. I think that 8 would
  // typically accommodate the vast majority.
  SmallVector<NestedNameSpecifier *, 8> NestedNames;

  // Push each of the NNS's onto a stack for serialization in reverse order.
  while (NNS) {
    NestedNames.push_back(NNS);
    NNS = NNS->getPrefix();
  }

  Record->push_back(NestedNames.size());
  while(!NestedNames.empty()) {
    NNS = NestedNames.pop_back_val();
    NestedNameSpecifier::SpecifierKind Kind = NNS->getKind();
    Record->push_back(Kind);
    switch (Kind) {
    case NestedNameSpecifier::Identifier:
      AddIdentifierRef(NNS->getAsIdentifier());
      break;

    case NestedNameSpecifier::Namespace:
      AddDeclRef(NNS->getAsNamespace());
      break;

    case NestedNameSpecifier::NamespaceAlias:
      AddDeclRef(NNS->getAsNamespaceAlias());
      break;

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate:
      AddTypeRef(QualType(NNS->getAsType(), 0));
      Record->push_back(Kind == NestedNameSpecifier::TypeSpecWithTemplate);
      break;

    case NestedNameSpecifier::Global:
      // Don't need to write an associated value.
      break;

    case NestedNameSpecifier::Super:
      AddDeclRef(NNS->getAsRecordDecl());
      break;
    }
  }
}

void ASTRecordWriter::AddNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS) {
  // Nested name specifiers usually aren't too long. I think that 8 would
  // typically accommodate the vast majority.
  SmallVector<NestedNameSpecifierLoc , 8> NestedNames;

  // Push each of the nested-name-specifiers's onto a stack for
  // serialization in reverse order.
  while (NNS) {
    NestedNames.push_back(NNS);
    NNS = NNS.getPrefix();
  }

  Record->push_back(NestedNames.size());
  while(!NestedNames.empty()) {
    NNS = NestedNames.pop_back_val();
    NestedNameSpecifier::SpecifierKind Kind
      = NNS.getNestedNameSpecifier()->getKind();
    Record->push_back(Kind);
    switch (Kind) {
    case NestedNameSpecifier::Identifier:
      AddIdentifierRef(NNS.getNestedNameSpecifier()->getAsIdentifier());
      AddSourceRange(NNS.getLocalSourceRange());
      break;

    case NestedNameSpecifier::Namespace:
      AddDeclRef(NNS.getNestedNameSpecifier()->getAsNamespace());
      AddSourceRange(NNS.getLocalSourceRange());
      break;

    case NestedNameSpecifier::NamespaceAlias:
      AddDeclRef(NNS.getNestedNameSpecifier()->getAsNamespaceAlias());
      AddSourceRange(NNS.getLocalSourceRange());
      break;

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate:
      Record->push_back(Kind == NestedNameSpecifier::TypeSpecWithTemplate);
      AddTypeRef(NNS.getTypeLoc().getType());
      AddTypeLoc(NNS.getTypeLoc());
      AddSourceLocation(NNS.getLocalSourceRange().getEnd());
      break;

    case NestedNameSpecifier::Global:
      AddSourceLocation(NNS.getLocalSourceRange().getEnd());
      break;

    case NestedNameSpecifier::Super:
      AddDeclRef(NNS.getNestedNameSpecifier()->getAsRecordDecl());
      AddSourceRange(NNS.getLocalSourceRange());
      break;
    }
  }
}

void ASTRecordWriter::AddTemplateName(TemplateName Name) {
  TemplateName::NameKind Kind = Name.getKind();
  Record->push_back(Kind);
  switch (Kind) {
  case TemplateName::Template:
    AddDeclRef(Name.getAsTemplateDecl());
    break;

  case TemplateName::OverloadedTemplate: {
    OverloadedTemplateStorage *OvT = Name.getAsOverloadedTemplate();
    Record->push_back(OvT->size());
    for (const auto &I : *OvT)
      AddDeclRef(I);
    break;
  }

  case TemplateName::QualifiedTemplate: {
    QualifiedTemplateName *QualT = Name.getAsQualifiedTemplateName();
    AddNestedNameSpecifier(QualT->getQualifier());
    Record->push_back(QualT->hasTemplateKeyword());
    AddDeclRef(QualT->getTemplateDecl());
    break;
  }

  case TemplateName::DependentTemplate: {
    DependentTemplateName *DepT = Name.getAsDependentTemplateName();
    AddNestedNameSpecifier(DepT->getQualifier());
    Record->push_back(DepT->isIdentifier());
    if (DepT->isIdentifier())
      AddIdentifierRef(DepT->getIdentifier());
    else
      Record->push_back(DepT->getOperator());
    break;
  }

  case TemplateName::SubstTemplateTemplateParm: {
    SubstTemplateTemplateParmStorage *subst
      = Name.getAsSubstTemplateTemplateParm();
    AddDeclRef(subst->getParameter());
    AddTemplateName(subst->getReplacement());
    break;
  }

  case TemplateName::SubstTemplateTemplateParmPack: {
    SubstTemplateTemplateParmPackStorage *SubstPack
      = Name.getAsSubstTemplateTemplateParmPack();
    AddDeclRef(SubstPack->getParameterPack());
    AddTemplateArgument(SubstPack->getArgumentPack());
    break;
  }
  }
}

void ASTRecordWriter::AddTemplateArgument(const TemplateArgument &Arg) {
  Record->push_back(Arg.getKind());
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    break;
  case TemplateArgument::Type:
    AddTypeRef(Arg.getAsType());
    break;
  case TemplateArgument::Declaration:
    AddDeclRef(Arg.getAsDecl());
    AddTypeRef(Arg.getParamTypeForDecl());
    break;
  case TemplateArgument::NullPtr:
    AddTypeRef(Arg.getNullPtrType());
    break;
  case TemplateArgument::Integral:
    AddAPSInt(Arg.getAsIntegral());
    AddTypeRef(Arg.getIntegralType());
    break;
  case TemplateArgument::Template:
    AddTemplateName(Arg.getAsTemplateOrTemplatePattern());
    break;
  case TemplateArgument::TemplateExpansion:
    AddTemplateName(Arg.getAsTemplateOrTemplatePattern());
    if (Optional<unsigned> NumExpansions = Arg.getNumTemplateExpansions())
      Record->push_back(*NumExpansions + 1);
    else
      Record->push_back(0);
    break;
  case TemplateArgument::Expression:
    AddStmt(Arg.getAsExpr());
    break;
  case TemplateArgument::Pack:
    Record->push_back(Arg.pack_size());
    for (const auto &P : Arg.pack_elements())
      AddTemplateArgument(P);
    break;
  }
}

void ASTRecordWriter::AddTemplateParameterList(
    const TemplateParameterList *TemplateParams) {
  assert(TemplateParams && "No TemplateParams!");
  AddSourceLocation(TemplateParams->getTemplateLoc());
  AddSourceLocation(TemplateParams->getLAngleLoc());
  AddSourceLocation(TemplateParams->getRAngleLoc());
  // TODO: Concepts
  Record->push_back(TemplateParams->size());
  for (const auto &P : *TemplateParams)
    AddDeclRef(P);
}

/// Emit a template argument list.
void ASTRecordWriter::AddTemplateArgumentList(
    const TemplateArgumentList *TemplateArgs) {
  assert(TemplateArgs && "No TemplateArgs!");
  Record->push_back(TemplateArgs->size());
  for (int i = 0, e = TemplateArgs->size(); i != e; ++i)
    AddTemplateArgument(TemplateArgs->get(i));
}

void ASTRecordWriter::AddASTTemplateArgumentListInfo(
    const ASTTemplateArgumentListInfo *ASTTemplArgList) {
  assert(ASTTemplArgList && "No ASTTemplArgList!");
  AddSourceLocation(ASTTemplArgList->LAngleLoc);
  AddSourceLocation(ASTTemplArgList->RAngleLoc);
  Record->push_back(ASTTemplArgList->NumTemplateArgs);
  const TemplateArgumentLoc *TemplArgs = ASTTemplArgList->getTemplateArgs();
  for (int i = 0, e = ASTTemplArgList->NumTemplateArgs; i != e; ++i)
    AddTemplateArgumentLoc(TemplArgs[i]);
}

void ASTRecordWriter::AddUnresolvedSet(const ASTUnresolvedSet &Set) {
  Record->push_back(Set.size());
  for (ASTUnresolvedSet::const_iterator
         I = Set.begin(), E = Set.end(); I != E; ++I) {
    AddDeclRef(I.getDecl());
    Record->push_back(I.getAccess());
  }
}

// FIXME: Move this out of the main ASTRecordWriter interface.
void ASTRecordWriter::AddCXXBaseSpecifier(const CXXBaseSpecifier &Base) {
  Record->push_back(Base.isVirtual());
  Record->push_back(Base.isBaseOfClass());
  Record->push_back(Base.getAccessSpecifierAsWritten());
  Record->push_back(Base.getInheritConstructors());
  AddTypeSourceInfo(Base.getTypeSourceInfo());
  AddSourceRange(Base.getSourceRange());
  AddSourceLocation(Base.isPackExpansion()? Base.getEllipsisLoc()
                                          : SourceLocation());
}

static uint64_t EmitCXXBaseSpecifiers(ASTWriter &W,
                                      ArrayRef<CXXBaseSpecifier> Bases) {
  ASTWriter::RecordData Record;
  ASTRecordWriter Writer(W, Record);
  Writer.push_back(Bases.size());

  for (auto &Base : Bases)
    Writer.AddCXXBaseSpecifier(Base);

  return Writer.Emit(serialization::DECL_CXX_BASE_SPECIFIERS);
}

// FIXME: Move this out of the main ASTRecordWriter interface.
void ASTRecordWriter::AddCXXBaseSpecifiers(ArrayRef<CXXBaseSpecifier> Bases) {
  AddOffset(EmitCXXBaseSpecifiers(*Writer, Bases));
}

static uint64_t
EmitCXXCtorInitializers(ASTWriter &W,
                        ArrayRef<CXXCtorInitializer *> CtorInits) {
  ASTWriter::RecordData Record;
  ASTRecordWriter Writer(W, Record);
  Writer.push_back(CtorInits.size());

  for (auto *Init : CtorInits) {
    if (Init->isBaseInitializer()) {
      Writer.push_back(CTOR_INITIALIZER_BASE);
      Writer.AddTypeSourceInfo(Init->getTypeSourceInfo());
      Writer.push_back(Init->isBaseVirtual());
    } else if (Init->isDelegatingInitializer()) {
      Writer.push_back(CTOR_INITIALIZER_DELEGATING);
      Writer.AddTypeSourceInfo(Init->getTypeSourceInfo());
    } else if (Init->isMemberInitializer()){
      Writer.push_back(CTOR_INITIALIZER_MEMBER);
      Writer.AddDeclRef(Init->getMember());
    } else {
      Writer.push_back(CTOR_INITIALIZER_INDIRECT_MEMBER);
      Writer.AddDeclRef(Init->getIndirectMember());
    }

    Writer.AddSourceLocation(Init->getMemberLocation());
    Writer.AddStmt(Init->getInit());
    Writer.AddSourceLocation(Init->getLParenLoc());
    Writer.AddSourceLocation(Init->getRParenLoc());
    Writer.push_back(Init->isWritten());
    if (Init->isWritten())
      Writer.push_back(Init->getSourceOrder());
  }

  return Writer.Emit(serialization::DECL_CXX_CTOR_INITIALIZERS);
}

// FIXME: Move this out of the main ASTRecordWriter interface.
void ASTRecordWriter::AddCXXCtorInitializers(
    ArrayRef<CXXCtorInitializer *> CtorInits) {
  AddOffset(EmitCXXCtorInitializers(*Writer, CtorInits));
}

void ASTRecordWriter::AddCXXDefinitionData(const CXXRecordDecl *D) {
  auto &Data = D->data();
  Record->push_back(Data.IsLambda);
  Record->push_back(Data.UserDeclaredConstructor);
  Record->push_back(Data.UserDeclaredSpecialMembers);
  Record->push_back(Data.Aggregate);
  Record->push_back(Data.PlainOldData);
  Record->push_back(Data.Empty);
  Record->push_back(Data.Polymorphic);
  Record->push_back(Data.Abstract);
  Record->push_back(Data.IsStandardLayout);
  Record->push_back(Data.IsCXX11StandardLayout);
  Record->push_back(Data.HasBasesWithFields);
  Record->push_back(Data.HasBasesWithNonStaticDataMembers);
  Record->push_back(Data.HasPrivateFields);
  Record->push_back(Data.HasProtectedFields);
  Record->push_back(Data.HasPublicFields);
  Record->push_back(Data.HasMutableFields);
  Record->push_back(Data.HasVariantMembers);
  Record->push_back(Data.HasOnlyCMembers);
  Record->push_back(Data.HasInClassInitializer);
  Record->push_back(Data.HasUninitializedReferenceMember);
  Record->push_back(Data.HasUninitializedFields);
  Record->push_back(Data.HasInheritedConstructor);
  Record->push_back(Data.HasInheritedAssignment);
  Record->push_back(Data.NeedOverloadResolutionForCopyConstructor);
  Record->push_back(Data.NeedOverloadResolutionForMoveConstructor);
  Record->push_back(Data.NeedOverloadResolutionForMoveAssignment);
  Record->push_back(Data.NeedOverloadResolutionForDestructor);
  Record->push_back(Data.DefaultedCopyConstructorIsDeleted);
  Record->push_back(Data.DefaultedMoveConstructorIsDeleted);
  Record->push_back(Data.DefaultedMoveAssignmentIsDeleted);
  Record->push_back(Data.DefaultedDestructorIsDeleted);
  Record->push_back(Data.HasTrivialSpecialMembers);
  Record->push_back(Data.HasTrivialSpecialMembersForCall);
  Record->push_back(Data.DeclaredNonTrivialSpecialMembers);
  Record->push_back(Data.DeclaredNonTrivialSpecialMembersForCall);
  Record->push_back(Data.HasIrrelevantDestructor);
  Record->push_back(Data.HasConstexprNonCopyMoveConstructor);
  Record->push_back(Data.HasDefaultedDefaultConstructor);
  Record->push_back(Data.DefaultedDefaultConstructorIsConstexpr);
  Record->push_back(Data.HasConstexprDefaultConstructor);
  Record->push_back(Data.HasNonLiteralTypeFieldsOrBases);
  Record->push_back(Data.ComputedVisibleConversions);
  Record->push_back(Data.UserProvidedDefaultConstructor);
  Record->push_back(Data.DeclaredSpecialMembers);
  Record->push_back(Data.ImplicitCopyConstructorCanHaveConstParamForVBase);
  Record->push_back(Data.ImplicitCopyConstructorCanHaveConstParamForNonVBase);
  Record->push_back(Data.ImplicitCopyAssignmentHasConstParam);
  Record->push_back(Data.HasDeclaredCopyConstructorWithConstParam);
  Record->push_back(Data.HasDeclaredCopyAssignmentWithConstParam);

  // getODRHash will compute the ODRHash if it has not been previously computed.
  Record->push_back(D->getODRHash());
  bool ModulesDebugInfo = Writer->Context->getLangOpts().ModulesDebugInfo &&
                          Writer->WritingModule && !D->isDependentType();
  Record->push_back(ModulesDebugInfo);
  if (ModulesDebugInfo)
    Writer->ModularCodegenDecls.push_back(Writer->GetDeclRef(D));

  // IsLambda bit is already saved.

  Record->push_back(Data.NumBases);
  if (Data.NumBases > 0)
    AddCXXBaseSpecifiers(Data.bases());

  // FIXME: Make VBases lazily computed when needed to avoid storing them.
  Record->push_back(Data.NumVBases);
  if (Data.NumVBases > 0)
    AddCXXBaseSpecifiers(Data.vbases());

  AddUnresolvedSet(Data.Conversions.get(*Writer->Context));
  AddUnresolvedSet(Data.VisibleConversions.get(*Writer->Context));
  // Data.Definition is the owning decl, no need to write it.
  AddDeclRef(D->getFirstFriend());

  // Add lambda-specific data.
  if (Data.IsLambda) {
    auto &Lambda = D->getLambdaData();
    Record->push_back(Lambda.Dependent);
    Record->push_back(Lambda.IsGenericLambda);
    Record->push_back(Lambda.CaptureDefault);
    Record->push_back(Lambda.NumCaptures);
    Record->push_back(Lambda.NumExplicitCaptures);
    Record->push_back(Lambda.ManglingNumber);
    AddDeclRef(D->getLambdaContextDecl());
    AddTypeSourceInfo(Lambda.MethodTyInfo);
    for (unsigned I = 0, N = Lambda.NumCaptures; I != N; ++I) {
      const LambdaCapture &Capture = Lambda.Captures[I];
      AddSourceLocation(Capture.getLocation());
      Record->push_back(Capture.isImplicit());
      Record->push_back(Capture.getCaptureKind());
      switch (Capture.getCaptureKind()) {
      case LCK_StarThis:
      case LCK_This:
      case LCK_VLAType:
        break;
      case LCK_ByCopy:
      case LCK_ByRef:
        VarDecl *Var =
            Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr;
        AddDeclRef(Var);
        AddSourceLocation(Capture.isPackExpansion() ? Capture.getEllipsisLoc()
                                                    : SourceLocation());
        break;
      }
    }
  }
}

void ASTWriter::ReaderInitialized(ASTReader *Reader) {
  assert(Reader && "Cannot remove chain");
  assert((!Chain || Chain == Reader) && "Cannot replace chain");
  assert(FirstDeclID == NextDeclID &&
         FirstTypeID == NextTypeID &&
         FirstIdentID == NextIdentID &&
         FirstMacroID == NextMacroID &&
         FirstSubmoduleID == NextSubmoduleID &&
         FirstSelectorID == NextSelectorID &&
         "Setting chain after writing has started.");

  Chain = Reader;

  // Note, this will get called multiple times, once one the reader starts up
  // and again each time it's done reading a PCH or module.
  FirstDeclID = NUM_PREDEF_DECL_IDS + Chain->getTotalNumDecls();
  FirstTypeID = NUM_PREDEF_TYPE_IDS + Chain->getTotalNumTypes();
  FirstIdentID = NUM_PREDEF_IDENT_IDS + Chain->getTotalNumIdentifiers();
  FirstMacroID = NUM_PREDEF_MACRO_IDS + Chain->getTotalNumMacros();
  FirstSubmoduleID = NUM_PREDEF_SUBMODULE_IDS + Chain->getTotalNumSubmodules();
  FirstSelectorID = NUM_PREDEF_SELECTOR_IDS + Chain->getTotalNumSelectors();
  NextDeclID = FirstDeclID;
  NextTypeID = FirstTypeID;
  NextIdentID = FirstIdentID;
  NextMacroID = FirstMacroID;
  NextSelectorID = FirstSelectorID;
  NextSubmoduleID = FirstSubmoduleID;
}

void ASTWriter::IdentifierRead(IdentID ID, IdentifierInfo *II) {
  // Always keep the highest ID. See \p TypeRead() for more information.
  IdentID &StoredID = IdentifierIDs[II];
  if (ID > StoredID)
    StoredID = ID;
}

void ASTWriter::MacroRead(serialization::MacroID ID, MacroInfo *MI) {
  // Always keep the highest ID. See \p TypeRead() for more information.
  MacroID &StoredID = MacroIDs[MI];
  if (ID > StoredID)
    StoredID = ID;
}

void ASTWriter::TypeRead(TypeIdx Idx, QualType T) {
  // Always take the highest-numbered type index. This copes with an interesting
  // case for chained AST writing where we schedule writing the type and then,
  // later, deserialize the type from another AST. In this case, we want to
  // keep the higher-numbered entry so that we can properly write it out to
  // the AST file.
  TypeIdx &StoredIdx = TypeIdxs[T];
  if (Idx.getIndex() >= StoredIdx.getIndex())
    StoredIdx = Idx;
}

void ASTWriter::SelectorRead(SelectorID ID, Selector S) {
  // Always keep the highest ID. See \p TypeRead() for more information.
  SelectorID &StoredID = SelectorIDs[S];
  if (ID > StoredID)
    StoredID = ID;
}

void ASTWriter::MacroDefinitionRead(serialization::PreprocessedEntityID ID,
                                    MacroDefinitionRecord *MD) {
  assert(MacroDefinitions.find(MD) == MacroDefinitions.end());
  MacroDefinitions[MD] = ID;
}

void ASTWriter::ModuleRead(serialization::SubmoduleID ID, Module *Mod) {
  assert(SubmoduleIDs.find(Mod) == SubmoduleIDs.end());
  SubmoduleIDs[Mod] = ID;
}

void ASTWriter::CompletedTagDefinition(const TagDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(D->isCompleteDefinition());
  assert(!WritingAST && "Already writing the AST!");
  if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
    // We are interested when a PCH decl is modified.
    if (RD->isFromASTFile()) {
      // A forward reference was mutated into a definition. Rewrite it.
      // FIXME: This happens during template instantiation, should we
      // have created a new definition decl instead ?
      assert(isTemplateInstantiation(RD->getTemplateSpecializationKind()) &&
             "completed a tag from another module but not by instantiation?");
      DeclUpdates[RD].push_back(
          DeclUpdate(UPD_CXX_INSTANTIATED_CLASS_DEFINITION));
    }
  }
}

static bool isImportedDeclContext(ASTReader *Chain, const Decl *D) {
  if (D->isFromASTFile())
    return true;

  // The predefined __va_list_tag struct is imported if we imported any decls.
  // FIXME: This is a gross hack.
  return D == D->getASTContext().getVaListTagDecl();
}

void ASTWriter::AddedVisibleDecl(const DeclContext *DC, const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(DC->isLookupContext() &&
          "Should not add lookup results to non-lookup contexts!");

  // TU is handled elsewhere.
  if (isa<TranslationUnitDecl>(DC))
    return;

  // Namespaces are handled elsewhere, except for template instantiations of
  // FunctionTemplateDecls in namespaces. We are interested in cases where the
  // local instantiations are added to an imported context. Only happens when
  // adding ADL lookup candidates, for example templated friends.
  if (isa<NamespaceDecl>(DC) && D->getFriendObjectKind() == Decl::FOK_None &&
      !isa<FunctionTemplateDecl>(D))
    return;

  // We're only interested in cases where a local declaration is added to an
  // imported context.
  if (D->isFromASTFile() || !isImportedDeclContext(Chain, cast<Decl>(DC)))
    return;

  assert(DC == DC->getPrimaryContext() && "added to non-primary context");
  assert(!getDefinitiveDeclContext(DC) && "DeclContext not definitive!");
  assert(!WritingAST && "Already writing the AST!");
  if (UpdatedDeclContexts.insert(DC) && !cast<Decl>(DC)->isFromASTFile()) {
    // We're adding a visible declaration to a predefined decl context. Ensure
    // that we write out all of its lookup results so we don't get a nasty
    // surprise when we try to emit its lookup table.
    for (auto *Child : DC->decls())
      DeclsToEmitEvenIfUnreferenced.push_back(Child);
  }
  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

void ASTWriter::AddedCXXImplicitMember(const CXXRecordDecl *RD, const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(D->isImplicit());

  // We're only interested in cases where a local declaration is added to an
  // imported context.
  if (D->isFromASTFile() || !isImportedDeclContext(Chain, RD))
    return;

  if (!isa<CXXMethodDecl>(D))
    return;

  // A decl coming from PCH was modified.
  assert(RD->isCompleteDefinition());
  assert(!WritingAST && "Already writing the AST!");
  DeclUpdates[RD].push_back(DeclUpdate(UPD_CXX_ADDED_IMPLICIT_MEMBER, D));
}

void ASTWriter::ResolvedExceptionSpec(const FunctionDecl *FD) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!DoneWritingDeclsAndTypes && "Already done writing updates!");
  if (!Chain) return;
  Chain->forEachImportedKeyDecl(FD, [&](const Decl *D) {
    // If we don't already know the exception specification for this redecl
    // chain, add an update record for it.
    if (isUnresolvedExceptionSpec(cast<FunctionDecl>(D)
                                      ->getType()
                                      ->castAs<FunctionProtoType>()
                                      ->getExceptionSpecType()))
      DeclUpdates[D].push_back(UPD_CXX_RESOLVED_EXCEPTION_SPEC);
  });
}

void ASTWriter::DeducedReturnType(const FunctionDecl *FD, QualType ReturnType) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!Chain) return;
  Chain->forEachImportedKeyDecl(FD, [&](const Decl *D) {
    DeclUpdates[D].push_back(
        DeclUpdate(UPD_CXX_DEDUCED_RETURN_TYPE, ReturnType));
  });
}

void ASTWriter::ResolvedOperatorDelete(const CXXDestructorDecl *DD,
                                       const FunctionDecl *Delete,
                                       Expr *ThisArg) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  assert(Delete && "Not given an operator delete");
  if (!Chain) return;
  Chain->forEachImportedKeyDecl(DD, [&](const Decl *D) {
    DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_RESOLVED_DTOR_DELETE, Delete));
  });
}

void ASTWriter::CompletedImplicitDefinition(const FunctionDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return; // Declaration not imported from PCH.

  // Implicit function decl from a PCH was defined.
  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_ADDED_FUNCTION_DEFINITION));
}

void ASTWriter::VariableDefinitionInstantiated(const VarDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_ADDED_VAR_DEFINITION));
}

void ASTWriter::FunctionDefinitionInstantiated(const FunctionDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_ADDED_FUNCTION_DEFINITION));
}

void ASTWriter::InstantiationRequested(const ValueDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  // Since the actual instantiation is delayed, this really means that we need
  // to update the instantiation location.
  SourceLocation POI;
  if (auto *VD = dyn_cast<VarDecl>(D))
    POI = VD->getPointOfInstantiation();
  else
    POI = cast<FunctionDecl>(D)->getPointOfInstantiation();
  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_POINT_OF_INSTANTIATION, POI));
}

void ASTWriter::DefaultArgumentInstantiated(const ParmVarDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(
      DeclUpdate(UPD_CXX_INSTANTIATED_DEFAULT_ARGUMENT, D));
}

void ASTWriter::DefaultMemberInitializerInstantiated(const FieldDecl *D) {
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(
      DeclUpdate(UPD_CXX_INSTANTIATED_DEFAULT_MEMBER_INITIALIZER, D));
}

void ASTWriter::AddedObjCCategoryToInterface(const ObjCCategoryDecl *CatD,
                                             const ObjCInterfaceDecl *IFD) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!IFD->isFromASTFile())
    return; // Declaration not imported from PCH.

  assert(IFD->getDefinition() && "Category on a class without a definition?");
  ObjCClassesWithCategories.insert(
    const_cast<ObjCInterfaceDecl *>(IFD->getDefinition()));
}

void ASTWriter::DeclarationMarkedUsed(const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");

  // If there is *any* declaration of the entity that's not from an AST file,
  // we can skip writing the update record. We make sure that isUsed() triggers
  // completion of the redeclaration chain of the entity.
  for (auto Prev = D->getMostRecentDecl(); Prev; Prev = Prev->getPreviousDecl())
    if (IsLocalDecl(Prev))
      return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_MARKED_USED));
}

void ASTWriter::DeclarationMarkedOpenMPThreadPrivate(const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_MARKED_OPENMP_THREADPRIVATE));
}

void ASTWriter::DeclarationMarkedOpenMPDeclareTarget(const Decl *D,
                                                     const Attr *Attr) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(
      DeclUpdate(UPD_DECL_MARKED_OPENMP_DECLARETARGET, Attr));
}

void ASTWriter::RedefinedHiddenDefinition(const NamedDecl *D, Module *M) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  assert(D->isHidden() && "expected a hidden declaration");
  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_EXPORTED, M));
}

void ASTWriter::AddedAttributeToRecord(const Attr *Attr,
                                       const RecordDecl *Record) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!Record->isFromASTFile())
    return;
  DeclUpdates[Record].push_back(DeclUpdate(UPD_ADDED_ATTR_TO_RECORD, Attr));
}

void ASTWriter::AddedCXXTemplateSpecialization(
    const ClassTemplateDecl *TD, const ClassTemplateSpecializationDecl *D) {
  assert(!WritingAST && "Already writing the AST!");

  if (!TD->getFirstDecl()->isFromASTFile())
    return;
  if (Chain && Chain->isProcessingUpdateRecords())
    return;

  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

void ASTWriter::AddedCXXTemplateSpecialization(
    const VarTemplateDecl *TD, const VarTemplateSpecializationDecl *D) {
  assert(!WritingAST && "Already writing the AST!");

  if (!TD->getFirstDecl()->isFromASTFile())
    return;
  if (Chain && Chain->isProcessingUpdateRecords())
    return;

  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

void ASTWriter::AddedCXXTemplateSpecialization(const FunctionTemplateDecl *TD,
                                               const FunctionDecl *D) {
  assert(!WritingAST && "Already writing the AST!");

  if (!TD->getFirstDecl()->isFromASTFile())
    return;
  if (Chain && Chain->isProcessingUpdateRecords())
    return;

  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

//===----------------------------------------------------------------------===//
//// OMPClause Serialization
////===----------------------------------------------------------------------===//

void OMPClauseWriter::writeClause(OMPClause *C) {
  Record.push_back(C->getClauseKind());
  Visit(C);
  Record.AddSourceLocation(C->getBeginLoc());
  Record.AddSourceLocation(C->getEndLoc());
}

void OMPClauseWriter::VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C) {
  Record.push_back(C->getCaptureRegion());
  Record.AddStmt(C->getPreInitStmt());
}

void OMPClauseWriter::VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getPostUpdateExpr());
}

void OMPClauseWriter::VisitOMPIfClause(OMPIfClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.push_back(C->getNameModifier());
  Record.AddSourceLocation(C->getNameModifierLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddStmt(C->getCondition());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPFinalClause(OMPFinalClause *C) {
  Record.AddStmt(C->getCondition());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNumThreadsClause(OMPNumThreadsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getNumThreads());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPSafelenClause(OMPSafelenClause *C) {
  Record.AddStmt(C->getSafelen());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPSimdlenClause(OMPSimdlenClause *C) {
  Record.AddStmt(C->getSimdlen());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPCollapseClause(OMPCollapseClause *C) {
  Record.AddStmt(C->getNumForLoops());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDefaultClause(OMPDefaultClause *C) {
  Record.push_back(C->getDefaultKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getDefaultKindKwLoc());
}

void OMPClauseWriter::VisitOMPProcBindClause(OMPProcBindClause *C) {
  Record.push_back(C->getProcBindKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getProcBindKindKwLoc());
}

void OMPClauseWriter::VisitOMPScheduleClause(OMPScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.push_back(C->getScheduleKind());
  Record.push_back(C->getFirstScheduleModifier());
  Record.push_back(C->getSecondScheduleModifier());
  Record.AddStmt(C->getChunkSize());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getFirstScheduleModifierLoc());
  Record.AddSourceLocation(C->getSecondScheduleModifierLoc());
  Record.AddSourceLocation(C->getScheduleKindLoc());
  Record.AddSourceLocation(C->getCommaLoc());
}

void OMPClauseWriter::VisitOMPOrderedClause(OMPOrderedClause *C) {
  Record.push_back(C->getLoopNumIterations().size());
  Record.AddStmt(C->getNumForLoops());
  for (Expr *NumIter : C->getLoopNumIterations())
    Record.AddStmt(NumIter);
  for (unsigned I = 0, E = C->getLoopNumIterations().size(); I <E; ++I)
    Record.AddStmt(C->getLoopCounter(I));
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNowaitClause(OMPNowaitClause *) {}

void OMPClauseWriter::VisitOMPUntiedClause(OMPUntiedClause *) {}

void OMPClauseWriter::VisitOMPMergeableClause(OMPMergeableClause *) {}

void OMPClauseWriter::VisitOMPReadClause(OMPReadClause *) {}

void OMPClauseWriter::VisitOMPWriteClause(OMPWriteClause *) {}

void OMPClauseWriter::VisitOMPUpdateClause(OMPUpdateClause *) {}

void OMPClauseWriter::VisitOMPCaptureClause(OMPCaptureClause *) {}

void OMPClauseWriter::VisitOMPSeqCstClause(OMPSeqCstClause *) {}

void OMPClauseWriter::VisitOMPThreadsClause(OMPThreadsClause *) {}

void OMPClauseWriter::VisitOMPSIMDClause(OMPSIMDClause *) {}

void OMPClauseWriter::VisitOMPNogroupClause(OMPNogroupClause *) {}

void OMPClauseWriter::VisitOMPPrivateClause(OMPPrivateClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->private_copies()) {
    Record.AddStmt(VE);
  }
}

void OMPClauseWriter::VisitOMPFirstprivateClause(OMPFirstprivateClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPreInit(C);
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->private_copies()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->inits()) {
    Record.AddStmt(VE);
  }
}

void OMPClauseWriter::VisitOMPLastprivateClause(OMPLastprivateClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->private_copies())
    Record.AddStmt(E);
  for (auto *E : C->source_exprs())
    Record.AddStmt(E);
  for (auto *E : C->destination_exprs())
    Record.AddStmt(E);
  for (auto *E : C->assignment_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPSharedClause(OMPSharedClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPReductionClause(OMPReductionClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddNestedNameSpecifierLoc(C->getQualifierLoc());
  Record.AddDeclarationNameInfo(C->getNameInfo());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *VE : C->privates())
    Record.AddStmt(VE);
  for (auto *E : C->lhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->rhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->reduction_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPTaskReductionClause(OMPTaskReductionClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddNestedNameSpecifierLoc(C->getQualifierLoc());
  Record.AddDeclarationNameInfo(C->getNameInfo());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *VE : C->privates())
    Record.AddStmt(VE);
  for (auto *E : C->lhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->rhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->reduction_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPInReductionClause(OMPInReductionClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddNestedNameSpecifierLoc(C->getQualifierLoc());
  Record.AddDeclarationNameInfo(C->getNameInfo());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *VE : C->privates())
    Record.AddStmt(VE);
  for (auto *E : C->lhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->rhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->reduction_ops())
    Record.AddStmt(E);
  for (auto *E : C->taskgroup_descriptors())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPLinearClause(OMPLinearClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.push_back(C->getModifier());
  Record.AddSourceLocation(C->getModifierLoc());
  for (auto *VE : C->varlists()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->privates()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->inits()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->updates()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->finals()) {
    Record.AddStmt(VE);
  }
  Record.AddStmt(C->getStep());
  Record.AddStmt(C->getCalcStep());
}

void OMPClauseWriter::VisitOMPAlignedClause(OMPAlignedClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  Record.AddStmt(C->getAlignment());
}

void OMPClauseWriter::VisitOMPCopyinClause(OMPCopyinClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->source_exprs())
    Record.AddStmt(E);
  for (auto *E : C->destination_exprs())
    Record.AddStmt(E);
  for (auto *E : C->assignment_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPCopyprivateClause(OMPCopyprivateClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->source_exprs())
    Record.AddStmt(E);
  for (auto *E : C->destination_exprs())
    Record.AddStmt(E);
  for (auto *E : C->assignment_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPFlushClause(OMPFlushClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPDependClause(OMPDependClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getNumLoops());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.push_back(C->getDependencyKind());
  Record.AddSourceLocation(C->getDependencyLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I)
    Record.AddStmt(C->getLoopData(I));
}

void OMPClauseWriter::VisitOMPDeviceClause(OMPDeviceClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getDevice());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPMapClause(OMPMapClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (unsigned I = 0; I < OMPMapClause::NumberOfModifiers; ++I) {
    Record.push_back(C->getMapTypeModifier(I));
    Record.AddSourceLocation(C->getMapTypeModifierLoc(I));
  }
  Record.push_back(C->getMapType());
  Record.AddSourceLocation(C->getMapLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPNumTeamsClause(OMPNumTeamsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getNumTeams());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPThreadLimitClause(OMPThreadLimitClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getThreadLimit());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPPriorityClause(OMPPriorityClause *C) {
  Record.AddStmt(C->getPriority());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPGrainsizeClause(OMPGrainsizeClause *C) {
  Record.AddStmt(C->getGrainsize());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNumTasksClause(OMPNumTasksClause *C) {
  Record.AddStmt(C->getNumTasks());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPHintClause(OMPHintClause *C) {
  Record.AddStmt(C->getHint());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDistScheduleClause(OMPDistScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.push_back(C->getDistScheduleKind());
  Record.AddStmt(C->getChunkSize());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getDistScheduleKindLoc());
  Record.AddSourceLocation(C->getCommaLoc());
}

void OMPClauseWriter::VisitOMPDefaultmapClause(OMPDefaultmapClause *C) {
  Record.push_back(C->getDefaultmapKind());
  Record.push_back(C->getDefaultmapModifier());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getDefaultmapModifierLoc());
  Record.AddSourceLocation(C->getDefaultmapKindLoc());
}

void OMPClauseWriter::VisitOMPToClause(OMPToClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPFromClause(OMPFromClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPUseDevicePtrClause(OMPUseDevicePtrClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *VE : C->private_copies())
    Record.AddStmt(VE);
  for (auto *VE : C->inits())
    Record.AddStmt(VE);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPIsDevicePtrClause(OMPIsDevicePtrClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPUnifiedAddressClause(OMPUnifiedAddressClause *) {}

void OMPClauseWriter::VisitOMPUnifiedSharedMemoryClause(
    OMPUnifiedSharedMemoryClause *) {}

void OMPClauseWriter::VisitOMPReverseOffloadClause(OMPReverseOffloadClause *) {}

void
OMPClauseWriter::VisitOMPDynamicAllocatorsClause(OMPDynamicAllocatorsClause *) {
}

void OMPClauseWriter::VisitOMPAtomicDefaultMemOrderClause(
    OMPAtomicDefaultMemOrderClause *C) {
  Record.push_back(C->getAtomicDefaultMemOrderKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getAtomicDefaultMemOrderKindKwLoc());
}
