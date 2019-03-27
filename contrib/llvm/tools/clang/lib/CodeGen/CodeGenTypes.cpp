//===--- CodeGenTypes.cpp - Type translation for LLVM CodeGen -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the code that handles AST -> LLVM type lowering.
//
//===----------------------------------------------------------------------===//

#include "CodeGenTypes.h"
#include "CGCXXABI.h"
#include "CGCall.h"
#include "CGOpenCLRuntime.h"
#include "CGRecordLayout.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
using namespace clang;
using namespace CodeGen;

CodeGenTypes::CodeGenTypes(CodeGenModule &cgm)
  : CGM(cgm), Context(cgm.getContext()), TheModule(cgm.getModule()),
    Target(cgm.getTarget()), TheCXXABI(cgm.getCXXABI()),
    TheABIInfo(cgm.getTargetCodeGenInfo().getABIInfo()) {
  SkippedLayout = false;
}

CodeGenTypes::~CodeGenTypes() {
  llvm::DeleteContainerSeconds(CGRecordLayouts);

  for (llvm::FoldingSet<CGFunctionInfo>::iterator
       I = FunctionInfos.begin(), E = FunctionInfos.end(); I != E; )
    delete &*I++;
}

const CodeGenOptions &CodeGenTypes::getCodeGenOpts() const {
  return CGM.getCodeGenOpts();
}

void CodeGenTypes::addRecordTypeName(const RecordDecl *RD,
                                     llvm::StructType *Ty,
                                     StringRef suffix) {
  SmallString<256> TypeName;
  llvm::raw_svector_ostream OS(TypeName);
  OS << RD->getKindName() << '.';

  // Name the codegen type after the typedef name
  // if there is no tag type name available
  if (RD->getIdentifier()) {
    // FIXME: We should not have to check for a null decl context here.
    // Right now we do it because the implicit Obj-C decls don't have one.
    if (RD->getDeclContext())
      RD->printQualifiedName(OS);
    else
      RD->printName(OS);
  } else if (const TypedefNameDecl *TDD = RD->getTypedefNameForAnonDecl()) {
    // FIXME: We should not have to check for a null decl context here.
    // Right now we do it because the implicit Obj-C decls don't have one.
    if (TDD->getDeclContext())
      TDD->printQualifiedName(OS);
    else
      TDD->printName(OS);
  } else
    OS << "anon";

  if (!suffix.empty())
    OS << suffix;

  Ty->setName(OS.str());
}

/// ConvertTypeForMem - Convert type T into a llvm::Type.  This differs from
/// ConvertType in that it is used to convert to the memory representation for
/// a type.  For example, the scalar representation for _Bool is i1, but the
/// memory representation is usually i8 or i32, depending on the target.
llvm::Type *CodeGenTypes::ConvertTypeForMem(QualType T) {
  llvm::Type *R = ConvertType(T);

  // If this is a non-bool type, don't map it.
  if (!R->isIntegerTy(1))
    return R;

  // Otherwise, return an integer of the target-specified size.
  return llvm::IntegerType::get(getLLVMContext(),
                                (unsigned)Context.getTypeSize(T));
}


/// isRecordLayoutComplete - Return true if the specified type is already
/// completely laid out.
bool CodeGenTypes::isRecordLayoutComplete(const Type *Ty) const {
  llvm::DenseMap<const Type*, llvm::StructType *>::const_iterator I =
  RecordDeclTypes.find(Ty);
  return I != RecordDeclTypes.end() && !I->second->isOpaque();
}

static bool
isSafeToConvert(QualType T, CodeGenTypes &CGT,
                llvm::SmallPtrSet<const RecordDecl*, 16> &AlreadyChecked);


/// isSafeToConvert - Return true if it is safe to convert the specified record
/// decl to IR and lay it out, false if doing so would cause us to get into a
/// recursive compilation mess.
static bool
isSafeToConvert(const RecordDecl *RD, CodeGenTypes &CGT,
                llvm::SmallPtrSet<const RecordDecl*, 16> &AlreadyChecked) {
  // If we have already checked this type (maybe the same type is used by-value
  // multiple times in multiple structure fields, don't check again.
  if (!AlreadyChecked.insert(RD).second)
    return true;

  const Type *Key = CGT.getContext().getTagDeclType(RD).getTypePtr();

  // If this type is already laid out, converting it is a noop.
  if (CGT.isRecordLayoutComplete(Key)) return true;

  // If this type is currently being laid out, we can't recursively compile it.
  if (CGT.isRecordBeingLaidOut(Key))
    return false;

  // If this type would require laying out bases that are currently being laid
  // out, don't do it.  This includes virtual base classes which get laid out
  // when a class is translated, even though they aren't embedded by-value into
  // the class.
  if (const CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(RD)) {
    for (const auto &I : CRD->bases())
      if (!isSafeToConvert(I.getType()->getAs<RecordType>()->getDecl(),
                           CGT, AlreadyChecked))
        return false;
  }

  // If this type would require laying out members that are currently being laid
  // out, don't do it.
  for (const auto *I : RD->fields())
    if (!isSafeToConvert(I->getType(), CGT, AlreadyChecked))
      return false;

  // If there are no problems, lets do it.
  return true;
}

/// isSafeToConvert - Return true if it is safe to convert this field type,
/// which requires the structure elements contained by-value to all be
/// recursively safe to convert.
static bool
isSafeToConvert(QualType T, CodeGenTypes &CGT,
                llvm::SmallPtrSet<const RecordDecl*, 16> &AlreadyChecked) {
  // Strip off atomic type sugar.
  if (const auto *AT = T->getAs<AtomicType>())
    T = AT->getValueType();

  // If this is a record, check it.
  if (const auto *RT = T->getAs<RecordType>())
    return isSafeToConvert(RT->getDecl(), CGT, AlreadyChecked);

  // If this is an array, check the elements, which are embedded inline.
  if (const auto *AT = CGT.getContext().getAsArrayType(T))
    return isSafeToConvert(AT->getElementType(), CGT, AlreadyChecked);

  // Otherwise, there is no concern about transforming this.  We only care about
  // things that are contained by-value in a structure that can have another
  // structure as a member.
  return true;
}


/// isSafeToConvert - Return true if it is safe to convert the specified record
/// decl to IR and lay it out, false if doing so would cause us to get into a
/// recursive compilation mess.
static bool isSafeToConvert(const RecordDecl *RD, CodeGenTypes &CGT) {
  // If no structs are being laid out, we can certainly do this one.
  if (CGT.noRecordsBeingLaidOut()) return true;

  llvm::SmallPtrSet<const RecordDecl*, 16> AlreadyChecked;
  return isSafeToConvert(RD, CGT, AlreadyChecked);
}

/// isFuncParamTypeConvertible - Return true if the specified type in a
/// function parameter or result position can be converted to an IR type at this
/// point.  This boils down to being whether it is complete, as well as whether
/// we've temporarily deferred expanding the type because we're in a recursive
/// context.
bool CodeGenTypes::isFuncParamTypeConvertible(QualType Ty) {
  // Some ABIs cannot have their member pointers represented in IR unless
  // certain circumstances have been reached.
  if (const auto *MPT = Ty->getAs<MemberPointerType>())
    return getCXXABI().isMemberPointerConvertible(MPT);

  // If this isn't a tagged type, we can convert it!
  const TagType *TT = Ty->getAs<TagType>();
  if (!TT) return true;

  // Incomplete types cannot be converted.
  if (TT->isIncompleteType())
    return false;

  // If this is an enum, then it is always safe to convert.
  const RecordType *RT = dyn_cast<RecordType>(TT);
  if (!RT) return true;

  // Otherwise, we have to be careful.  If it is a struct that we're in the
  // process of expanding, then we can't convert the function type.  That's ok
  // though because we must be in a pointer context under the struct, so we can
  // just convert it to a dummy type.
  //
  // We decide this by checking whether ConvertRecordDeclType returns us an
  // opaque type for a struct that we know is defined.
  return isSafeToConvert(RT->getDecl(), *this);
}


/// Code to verify a given function type is complete, i.e. the return type
/// and all of the parameter types are complete.  Also check to see if we are in
/// a RS_StructPointer context, and if so whether any struct types have been
/// pended.  If so, we don't want to ask the ABI lowering code to handle a type
/// that cannot be converted to an IR type.
bool CodeGenTypes::isFuncTypeConvertible(const FunctionType *FT) {
  if (!isFuncParamTypeConvertible(FT->getReturnType()))
    return false;

  if (const FunctionProtoType *FPT = dyn_cast<FunctionProtoType>(FT))
    for (unsigned i = 0, e = FPT->getNumParams(); i != e; i++)
      if (!isFuncParamTypeConvertible(FPT->getParamType(i)))
        return false;

  return true;
}

/// UpdateCompletedType - When we find the full definition for a TagDecl,
/// replace the 'opaque' type we previously made for it if applicable.
void CodeGenTypes::UpdateCompletedType(const TagDecl *TD) {
  // If this is an enum being completed, then we flush all non-struct types from
  // the cache.  This allows function types and other things that may be derived
  // from the enum to be recomputed.
  if (const EnumDecl *ED = dyn_cast<EnumDecl>(TD)) {
    // Only flush the cache if we've actually already converted this type.
    if (TypeCache.count(ED->getTypeForDecl())) {
      // Okay, we formed some types based on this.  We speculated that the enum
      // would be lowered to i32, so we only need to flush the cache if this
      // didn't happen.
      if (!ConvertType(ED->getIntegerType())->isIntegerTy(32))
        TypeCache.clear();
    }
    // If necessary, provide the full definition of a type only used with a
    // declaration so far.
    if (CGDebugInfo *DI = CGM.getModuleDebugInfo())
      DI->completeType(ED);
    return;
  }

  // If we completed a RecordDecl that we previously used and converted to an
  // anonymous type, then go ahead and complete it now.
  const RecordDecl *RD = cast<RecordDecl>(TD);
  if (RD->isDependentType()) return;

  // Only complete it if we converted it already.  If we haven't converted it
  // yet, we'll just do it lazily.
  if (RecordDeclTypes.count(Context.getTagDeclType(RD).getTypePtr()))
    ConvertRecordDeclType(RD);

  // If necessary, provide the full definition of a type only used with a
  // declaration so far.
  if (CGDebugInfo *DI = CGM.getModuleDebugInfo())
    DI->completeType(RD);
}

void CodeGenTypes::RefreshTypeCacheForClass(const CXXRecordDecl *RD) {
  QualType T = Context.getRecordType(RD);
  T = Context.getCanonicalType(T);

  const Type *Ty = T.getTypePtr();
  if (RecordsWithOpaqueMemberPointers.count(Ty)) {
    TypeCache.clear();
    RecordsWithOpaqueMemberPointers.clear();
  }
}

static llvm::Type *getTypeForFormat(llvm::LLVMContext &VMContext,
                                    const llvm::fltSemantics &format,
                                    bool UseNativeHalf = false) {
  if (&format == &llvm::APFloat::IEEEhalf()) {
    if (UseNativeHalf)
      return llvm::Type::getHalfTy(VMContext);
    else
      return llvm::Type::getInt16Ty(VMContext);
  }
  if (&format == &llvm::APFloat::IEEEsingle())
    return llvm::Type::getFloatTy(VMContext);
  if (&format == &llvm::APFloat::IEEEdouble())
    return llvm::Type::getDoubleTy(VMContext);
  if (&format == &llvm::APFloat::IEEEquad())
    return llvm::Type::getFP128Ty(VMContext);
  if (&format == &llvm::APFloat::PPCDoubleDouble())
    return llvm::Type::getPPC_FP128Ty(VMContext);
  if (&format == &llvm::APFloat::x87DoubleExtended())
    return llvm::Type::getX86_FP80Ty(VMContext);
  llvm_unreachable("Unknown float format!");
}

llvm::Type *CodeGenTypes::ConvertFunctionType(QualType QFT,
                                              const FunctionDecl *FD) {
  assert(QFT.isCanonical());
  const Type *Ty = QFT.getTypePtr();
  const FunctionType *FT = cast<FunctionType>(QFT.getTypePtr());
  // First, check whether we can build the full function type.  If the
  // function type depends on an incomplete type (e.g. a struct or enum), we
  // cannot lower the function type.
  if (!isFuncTypeConvertible(FT)) {
    // This function's type depends on an incomplete tag type.

    // Force conversion of all the relevant record types, to make sure
    // we re-convert the FunctionType when appropriate.
    if (const RecordType *RT = FT->getReturnType()->getAs<RecordType>())
      ConvertRecordDeclType(RT->getDecl());
    if (const FunctionProtoType *FPT = dyn_cast<FunctionProtoType>(FT))
      for (unsigned i = 0, e = FPT->getNumParams(); i != e; i++)
        if (const RecordType *RT = FPT->getParamType(i)->getAs<RecordType>())
          ConvertRecordDeclType(RT->getDecl());

    SkippedLayout = true;

    // Return a placeholder type.
    return llvm::StructType::get(getLLVMContext());
  }

  // While we're converting the parameter types for a function, we don't want
  // to recursively convert any pointed-to structs.  Converting directly-used
  // structs is ok though.
  if (!RecordsBeingLaidOut.insert(Ty).second) {
    SkippedLayout = true;
    return llvm::StructType::get(getLLVMContext());
  }

  // The function type can be built; call the appropriate routines to
  // build it.
  const CGFunctionInfo *FI;
  if (const FunctionProtoType *FPT = dyn_cast<FunctionProtoType>(FT)) {
    FI = &arrangeFreeFunctionType(
        CanQual<FunctionProtoType>::CreateUnsafe(QualType(FPT, 0)), FD);
  } else {
    const FunctionNoProtoType *FNPT = cast<FunctionNoProtoType>(FT);
    FI = &arrangeFreeFunctionType(
        CanQual<FunctionNoProtoType>::CreateUnsafe(QualType(FNPT, 0)));
  }

  llvm::Type *ResultType = nullptr;
  // If there is something higher level prodding our CGFunctionInfo, then
  // don't recurse into it again.
  if (FunctionsBeingProcessed.count(FI)) {

    ResultType = llvm::StructType::get(getLLVMContext());
    SkippedLayout = true;
  } else {

    // Otherwise, we're good to go, go ahead and convert it.
    ResultType = GetFunctionType(*FI);
  }

  RecordsBeingLaidOut.erase(Ty);

  if (SkippedLayout)
    TypeCache.clear();

  if (RecordsBeingLaidOut.empty())
    while (!DeferredRecords.empty())
      ConvertRecordDeclType(DeferredRecords.pop_back_val());
  return ResultType;
}

/// ConvertType - Convert the specified type to its LLVM form.
llvm::Type *CodeGenTypes::ConvertType(QualType T) {
  T = Context.getCanonicalType(T);

  const Type *Ty = T.getTypePtr();

  // RecordTypes are cached and processed specially.
  if (const RecordType *RT = dyn_cast<RecordType>(Ty))
    return ConvertRecordDeclType(RT->getDecl());

  // See if type is already cached.
  llvm::DenseMap<const Type *, llvm::Type *>::iterator TCI = TypeCache.find(Ty);
  // If type is found in map then use it. Otherwise, convert type T.
  if (TCI != TypeCache.end())
    return TCI->second;

  // If we don't have it in the cache, convert it now.
  llvm::Type *ResultType = nullptr;
  switch (Ty->getTypeClass()) {
  case Type::Record: // Handled above.
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.def"
    llvm_unreachable("Non-canonical or dependent types aren't possible.");

  case Type::Builtin: {
    switch (cast<BuiltinType>(Ty)->getKind()) {
    case BuiltinType::Void:
    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
      // LLVM void type can only be used as the result of a function call.  Just
      // map to the same as char.
      ResultType = llvm::Type::getInt8Ty(getLLVMContext());
      break;

    case BuiltinType::Bool:
      // Note that we always return bool as i1 for use as a scalar type.
      ResultType = llvm::Type::getInt1Ty(getLLVMContext());
      break;

    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
    case BuiltinType::Short:
    case BuiltinType::UShort:
    case BuiltinType::Int:
    case BuiltinType::UInt:
    case BuiltinType::Long:
    case BuiltinType::ULong:
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
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
      ResultType = llvm::IntegerType::get(getLLVMContext(),
                                 static_cast<unsigned>(Context.getTypeSize(T)));
      break;

    case BuiltinType::Float16:
      ResultType =
          getTypeForFormat(getLLVMContext(), Context.getFloatTypeSemantics(T),
                           /* UseNativeHalf = */ true);
      break;

    case BuiltinType::Half:
      // Half FP can either be storage-only (lowered to i16) or native.
      ResultType = getTypeForFormat(
          getLLVMContext(), Context.getFloatTypeSemantics(T),
          Context.getLangOpts().NativeHalfType ||
              !Context.getTargetInfo().useFP16ConversionIntrinsics());
      break;
    case BuiltinType::Float:
    case BuiltinType::Double:
    case BuiltinType::LongDouble:
    case BuiltinType::Float128:
      ResultType = getTypeForFormat(getLLVMContext(),
                                    Context.getFloatTypeSemantics(T),
                                    /* UseNativeHalf = */ false);
      break;

    case BuiltinType::NullPtr:
      // Model std::nullptr_t as i8*
      ResultType = llvm::Type::getInt8PtrTy(getLLVMContext());
      break;

    case BuiltinType::UInt128:
    case BuiltinType::Int128:
      ResultType = llvm::IntegerType::get(getLLVMContext(), 128);
      break;

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
      ResultType = CGM.getOpenCLRuntime().convertOpenCLSpecificType(Ty);
      break;

    case BuiltinType::Dependent:
#define BUILTIN_TYPE(Id, SingletonId)
#define PLACEHOLDER_TYPE(Id, SingletonId) \
    case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
      llvm_unreachable("Unexpected placeholder builtin type!");
    }
    break;
  }
  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    llvm_unreachable("Unexpected undeduced type!");
  case Type::Complex: {
    llvm::Type *EltTy = ConvertType(cast<ComplexType>(Ty)->getElementType());
    ResultType = llvm::StructType::get(EltTy, EltTy);
    break;
  }
  case Type::LValueReference:
  case Type::RValueReference: {
    const ReferenceType *RTy = cast<ReferenceType>(Ty);
    QualType ETy = RTy->getPointeeType();
    llvm::Type *PointeeType = ConvertTypeForMem(ETy);
    unsigned AS = Context.getTargetAddressSpace(ETy);
    ResultType = llvm::PointerType::get(PointeeType, AS);
    break;
  }
  case Type::Pointer: {
    const PointerType *PTy = cast<PointerType>(Ty);
    QualType ETy = PTy->getPointeeType();
    llvm::Type *PointeeType = ConvertTypeForMem(ETy);
    if (PointeeType->isVoidTy())
      PointeeType = llvm::Type::getInt8Ty(getLLVMContext());
    unsigned AS = Context.getTargetAddressSpace(ETy);
    ResultType = llvm::PointerType::get(PointeeType, AS);
    break;
  }

  case Type::VariableArray: {
    const VariableArrayType *A = cast<VariableArrayType>(Ty);
    assert(A->getIndexTypeCVRQualifiers() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // VLAs resolve to the innermost element type; this matches
    // the return of alloca, and there isn't any obviously better choice.
    ResultType = ConvertTypeForMem(A->getElementType());
    break;
  }
  case Type::IncompleteArray: {
    const IncompleteArrayType *A = cast<IncompleteArrayType>(Ty);
    assert(A->getIndexTypeCVRQualifiers() == 0 &&
           "FIXME: We only handle trivial array types so far!");
    // int X[] -> [0 x int], unless the element type is not sized.  If it is
    // unsized (e.g. an incomplete struct) just use [0 x i8].
    ResultType = ConvertTypeForMem(A->getElementType());
    if (!ResultType->isSized()) {
      SkippedLayout = true;
      ResultType = llvm::Type::getInt8Ty(getLLVMContext());
    }
    ResultType = llvm::ArrayType::get(ResultType, 0);
    break;
  }
  case Type::ConstantArray: {
    const ConstantArrayType *A = cast<ConstantArrayType>(Ty);
    llvm::Type *EltTy = ConvertTypeForMem(A->getElementType());

    // Lower arrays of undefined struct type to arrays of i8 just to have a
    // concrete type.
    if (!EltTy->isSized()) {
      SkippedLayout = true;
      EltTy = llvm::Type::getInt8Ty(getLLVMContext());
    }

    ResultType = llvm::ArrayType::get(EltTy, A->getSize().getZExtValue());
    break;
  }
  case Type::ExtVector:
  case Type::Vector: {
    const VectorType *VT = cast<VectorType>(Ty);
    ResultType = llvm::VectorType::get(ConvertType(VT->getElementType()),
                                       VT->getNumElements());
    break;
  }
  case Type::FunctionNoProto:
  case Type::FunctionProto:
    ResultType = ConvertFunctionType(T);
    break;
  case Type::ObjCObject:
    ResultType = ConvertType(cast<ObjCObjectType>(Ty)->getBaseType());
    break;

  case Type::ObjCInterface: {
    // Objective-C interfaces are always opaque (outside of the
    // runtime, which can do whatever it likes); we never refine
    // these.
    llvm::Type *&T = InterfaceTypes[cast<ObjCInterfaceType>(Ty)];
    if (!T)
      T = llvm::StructType::create(getLLVMContext());
    ResultType = T;
    break;
  }

  case Type::ObjCObjectPointer: {
    // Protocol qualifications do not influence the LLVM type, we just return a
    // pointer to the underlying interface type. We don't need to worry about
    // recursive conversion.
    llvm::Type *T =
      ConvertTypeForMem(cast<ObjCObjectPointerType>(Ty)->getPointeeType());
    ResultType = T->getPointerTo();
    break;
  }

  case Type::Enum: {
    const EnumDecl *ED = cast<EnumType>(Ty)->getDecl();
    if (ED->isCompleteDefinition() || ED->isFixed())
      return ConvertType(ED->getIntegerType());
    // Return a placeholder 'i32' type.  This can be changed later when the
    // type is defined (see UpdateCompletedType), but is likely to be the
    // "right" answer.
    ResultType = llvm::Type::getInt32Ty(getLLVMContext());
    break;
  }

  case Type::BlockPointer: {
    const QualType FTy = cast<BlockPointerType>(Ty)->getPointeeType();
    llvm::Type *PointeeType = ConvertTypeForMem(FTy);
    unsigned AS = Context.getTargetAddressSpace(FTy);
    ResultType = llvm::PointerType::get(PointeeType, AS);
    break;
  }

  case Type::MemberPointer: {
    auto *MPTy = cast<MemberPointerType>(Ty);
    if (!getCXXABI().isMemberPointerConvertible(MPTy)) {
      RecordsWithOpaqueMemberPointers.insert(MPTy->getClass());
      ResultType = llvm::StructType::create(getLLVMContext());
    } else {
      ResultType = getCXXABI().ConvertMemberPointerType(MPTy);
    }
    break;
  }

  case Type::Atomic: {
    QualType valueType = cast<AtomicType>(Ty)->getValueType();
    ResultType = ConvertTypeForMem(valueType);

    // Pad out to the inflated size if necessary.
    uint64_t valueSize = Context.getTypeSize(valueType);
    uint64_t atomicSize = Context.getTypeSize(Ty);
    if (valueSize != atomicSize) {
      assert(valueSize < atomicSize);
      llvm::Type *elts[] = {
        ResultType,
        llvm::ArrayType::get(CGM.Int8Ty, (atomicSize - valueSize) / 8)
      };
      ResultType = llvm::StructType::get(getLLVMContext(),
                                         llvm::makeArrayRef(elts));
    }
    break;
  }
  case Type::Pipe: {
    ResultType = CGM.getOpenCLRuntime().getPipeType(cast<PipeType>(Ty));
    break;
  }
  }

  assert(ResultType && "Didn't convert a type?");

  TypeCache[Ty] = ResultType;
  return ResultType;
}

bool CodeGenModule::isPaddedAtomicType(QualType type) {
  return isPaddedAtomicType(type->castAs<AtomicType>());
}

bool CodeGenModule::isPaddedAtomicType(const AtomicType *type) {
  return Context.getTypeSize(type) != Context.getTypeSize(type->getValueType());
}

/// ConvertRecordDeclType - Lay out a tagged decl type like struct or union.
llvm::StructType *CodeGenTypes::ConvertRecordDeclType(const RecordDecl *RD) {
  // TagDecl's are not necessarily unique, instead use the (clang)
  // type connected to the decl.
  const Type *Key = Context.getTagDeclType(RD).getTypePtr();

  llvm::StructType *&Entry = RecordDeclTypes[Key];

  // If we don't have a StructType at all yet, create the forward declaration.
  if (!Entry) {
    Entry = llvm::StructType::create(getLLVMContext());
    addRecordTypeName(RD, Entry, "");
  }
  llvm::StructType *Ty = Entry;

  // If this is still a forward declaration, or the LLVM type is already
  // complete, there's nothing more to do.
  RD = RD->getDefinition();
  if (!RD || !RD->isCompleteDefinition() || !Ty->isOpaque())
    return Ty;

  // If converting this type would cause us to infinitely loop, don't do it!
  if (!isSafeToConvert(RD, *this)) {
    DeferredRecords.push_back(RD);
    return Ty;
  }

  // Okay, this is a definition of a type.  Compile the implementation now.
  bool InsertResult = RecordsBeingLaidOut.insert(Key).second;
  (void)InsertResult;
  assert(InsertResult && "Recursively compiling a struct?");

  // Force conversion of non-virtual base classes recursively.
  if (const CXXRecordDecl *CRD = dyn_cast<CXXRecordDecl>(RD)) {
    for (const auto &I : CRD->bases()) {
      if (I.isVirtual()) continue;

      ConvertRecordDeclType(I.getType()->getAs<RecordType>()->getDecl());
    }
  }

  // Layout fields.
  CGRecordLayout *Layout = ComputeRecordLayout(RD, Ty);
  CGRecordLayouts[Key] = Layout;

  // We're done laying out this struct.
  bool EraseResult = RecordsBeingLaidOut.erase(Key); (void)EraseResult;
  assert(EraseResult && "struct not in RecordsBeingLaidOut set?");

  // If this struct blocked a FunctionType conversion, then recompute whatever
  // was derived from that.
  // FIXME: This is hugely overconservative.
  if (SkippedLayout)
    TypeCache.clear();

  // If we're done converting the outer-most record, then convert any deferred
  // structs as well.
  if (RecordsBeingLaidOut.empty())
    while (!DeferredRecords.empty())
      ConvertRecordDeclType(DeferredRecords.pop_back_val());

  return Ty;
}

/// getCGRecordLayout - Return record layout info for the given record decl.
const CGRecordLayout &
CodeGenTypes::getCGRecordLayout(const RecordDecl *RD) {
  const Type *Key = Context.getTagDeclType(RD).getTypePtr();

  const CGRecordLayout *Layout = CGRecordLayouts.lookup(Key);
  if (!Layout) {
    // Compute the type information.
    ConvertRecordDeclType(RD);

    // Now try again.
    Layout = CGRecordLayouts.lookup(Key);
  }

  assert(Layout && "Unable to find record layout information for type");
  return *Layout;
}

bool CodeGenTypes::isPointerZeroInitializable(QualType T) {
  assert((T->isAnyPointerType() || T->isBlockPointerType()) && "Invalid type");
  return isZeroInitializable(T);
}

bool CodeGenTypes::isZeroInitializable(QualType T) {
  if (T->getAs<PointerType>())
    return Context.getTargetNullPointerValue(T) == 0;

  if (const auto *AT = Context.getAsArrayType(T)) {
    if (isa<IncompleteArrayType>(AT))
      return true;
    if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
      if (Context.getConstantArrayElementCount(CAT) == 0)
        return true;
    T = Context.getBaseElementType(T);
  }

  // Records are non-zero-initializable if they contain any
  // non-zero-initializable subobjects.
  if (const RecordType *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    return isZeroInitializable(RD);
  }

  // We have to ask the ABI about member pointers.
  if (const MemberPointerType *MPT = T->getAs<MemberPointerType>())
    return getCXXABI().isZeroInitializable(MPT);

  // Everything else is okay.
  return true;
}

bool CodeGenTypes::isZeroInitializable(const RecordDecl *RD) {
  return getCGRecordLayout(RD).isZeroInitializable();
}
