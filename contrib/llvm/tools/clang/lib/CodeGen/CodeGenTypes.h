//===--- CodeGenTypes.h - Type translation for LLVM CodeGen -----*- C++ -*-===//
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

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENTYPES_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENTYPES_H

#include "CGCall.h"
#include "clang/Basic/ABI.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Module.h"

namespace llvm {
class FunctionType;
class DataLayout;
class Type;
class LLVMContext;
class StructType;
}

namespace clang {
class ASTContext;
template <typename> class CanQual;
class CXXConstructorDecl;
class CXXDestructorDecl;
class CXXMethodDecl;
class CodeGenOptions;
class FieldDecl;
class FunctionProtoType;
class ObjCInterfaceDecl;
class ObjCIvarDecl;
class PointerType;
class QualType;
class RecordDecl;
class TagDecl;
class TargetInfo;
class Type;
typedef CanQual<Type> CanQualType;
class GlobalDecl;

namespace CodeGen {
class ABIInfo;
class CGCXXABI;
class CGRecordLayout;
class CodeGenModule;
class RequiredArgs;

enum class StructorType {
  Complete, // constructor or destructor
  Base,     // constructor or destructor
  Deleting  // destructor only
};

inline CXXCtorType toCXXCtorType(StructorType T) {
  switch (T) {
  case StructorType::Complete:
    return Ctor_Complete;
  case StructorType::Base:
    return Ctor_Base;
  case StructorType::Deleting:
    llvm_unreachable("cannot have a deleting ctor");
  }
  llvm_unreachable("not a StructorType");
}

inline StructorType getFromCtorType(CXXCtorType T) {
  switch (T) {
  case Ctor_Complete:
    return StructorType::Complete;
  case Ctor_Base:
    return StructorType::Base;
  case Ctor_Comdat:
    llvm_unreachable("not expecting a COMDAT");
  case Ctor_CopyingClosure:
  case Ctor_DefaultClosure:
    llvm_unreachable("not expecting a closure");
  }
  llvm_unreachable("not a CXXCtorType");
}

inline CXXDtorType toCXXDtorType(StructorType T) {
  switch (T) {
  case StructorType::Complete:
    return Dtor_Complete;
  case StructorType::Base:
    return Dtor_Base;
  case StructorType::Deleting:
    return Dtor_Deleting;
  }
  llvm_unreachable("not a StructorType");
}

inline StructorType getFromDtorType(CXXDtorType T) {
  switch (T) {
  case Dtor_Deleting:
    return StructorType::Deleting;
  case Dtor_Complete:
    return StructorType::Complete;
  case Dtor_Base:
    return StructorType::Base;
  case Dtor_Comdat:
    llvm_unreachable("not expecting a COMDAT");
  }
  llvm_unreachable("not a CXXDtorType");
}

/// This class organizes the cross-module state that is used while lowering
/// AST types to LLVM types.
class CodeGenTypes {
  CodeGenModule &CGM;
  // Some of this stuff should probably be left on the CGM.
  ASTContext &Context;
  llvm::Module &TheModule;
  const TargetInfo &Target;
  CGCXXABI &TheCXXABI;

  // This should not be moved earlier, since its initialization depends on some
  // of the previous reference members being already initialized
  const ABIInfo &TheABIInfo;

  /// The opaque type map for Objective-C interfaces. All direct
  /// manipulation is done by the runtime interfaces, which are
  /// responsible for coercing to the appropriate type; these opaque
  /// types are never refined.
  llvm::DenseMap<const ObjCInterfaceType*, llvm::Type *> InterfaceTypes;

  /// Maps clang struct type with corresponding record layout info.
  llvm::DenseMap<const Type*, CGRecordLayout *> CGRecordLayouts;

  /// Contains the LLVM IR type for any converted RecordDecl.
  llvm::DenseMap<const Type*, llvm::StructType *> RecordDeclTypes;

  /// Hold memoized CGFunctionInfo results.
  llvm::FoldingSet<CGFunctionInfo> FunctionInfos;

  /// This set keeps track of records that we're currently converting
  /// to an IR type.  For example, when converting:
  /// struct A { struct B { int x; } } when processing 'x', the 'A' and 'B'
  /// types will be in this set.
  llvm::SmallPtrSet<const Type*, 4> RecordsBeingLaidOut;

  llvm::SmallPtrSet<const CGFunctionInfo*, 4> FunctionsBeingProcessed;

  /// True if we didn't layout a function due to a being inside
  /// a recursive struct conversion, set this to true.
  bool SkippedLayout;

  SmallVector<const RecordDecl *, 8> DeferredRecords;

  /// This map keeps cache of llvm::Types and maps clang::Type to
  /// corresponding llvm::Type.
  llvm::DenseMap<const Type *, llvm::Type *> TypeCache;

  llvm::SmallSet<const Type *, 8> RecordsWithOpaqueMemberPointers;

public:
  CodeGenTypes(CodeGenModule &cgm);
  ~CodeGenTypes();

  const llvm::DataLayout &getDataLayout() const {
    return TheModule.getDataLayout();
  }
  ASTContext &getContext() const { return Context; }
  const ABIInfo &getABIInfo() const { return TheABIInfo; }
  const TargetInfo &getTarget() const { return Target; }
  CGCXXABI &getCXXABI() const { return TheCXXABI; }
  llvm::LLVMContext &getLLVMContext() { return TheModule.getContext(); }
  const CodeGenOptions &getCodeGenOpts() const;

  /// Convert clang calling convention to LLVM callilng convention.
  unsigned ClangCallConvToLLVMCallConv(CallingConv CC);

  /// ConvertType - Convert type T into a llvm::Type.
  llvm::Type *ConvertType(QualType T);

  /// Converts the GlobalDecl into an llvm::Type. This should be used
  /// when we know the target of the function we want to convert.  This is
  /// because some functions (explicitly, those with pass_object_size
  /// parameters) may not have the same signature as their type portrays, and
  /// can only be called directly.
  llvm::Type *ConvertFunctionType(QualType FT,
                                  const FunctionDecl *FD = nullptr);

  /// ConvertTypeForMem - Convert type T into a llvm::Type.  This differs from
  /// ConvertType in that it is used to convert to the memory representation for
  /// a type.  For example, the scalar representation for _Bool is i1, but the
  /// memory representation is usually i8 or i32, depending on the target.
  llvm::Type *ConvertTypeForMem(QualType T);

  /// GetFunctionType - Get the LLVM function type for \arg Info.
  llvm::FunctionType *GetFunctionType(const CGFunctionInfo &Info);

  llvm::FunctionType *GetFunctionType(GlobalDecl GD);

  /// isFuncTypeConvertible - Utility to check whether a function type can
  /// be converted to an LLVM type (i.e. doesn't depend on an incomplete tag
  /// type).
  bool isFuncTypeConvertible(const FunctionType *FT);
  bool isFuncParamTypeConvertible(QualType Ty);

  /// Determine if a C++ inheriting constructor should have parameters matching
  /// those of its inherited constructor.
  bool inheritingCtorHasParams(const InheritedConstructor &Inherited,
                               CXXCtorType Type);

  /// GetFunctionTypeForVTable - Get the LLVM function type for use in a vtable,
  /// given a CXXMethodDecl. If the method to has an incomplete return type,
  /// and/or incomplete argument types, this will return the opaque type.
  llvm::Type *GetFunctionTypeForVTable(GlobalDecl GD);

  const CGRecordLayout &getCGRecordLayout(const RecordDecl*);

  /// UpdateCompletedType - When we find the full definition for a TagDecl,
  /// replace the 'opaque' type we previously made for it if applicable.
  void UpdateCompletedType(const TagDecl *TD);

  /// Remove stale types from the type cache when an inheritance model
  /// gets assigned to a class.
  void RefreshTypeCacheForClass(const CXXRecordDecl *RD);

  // The arrangement methods are split into three families:
  //   - those meant to drive the signature and prologue/epilogue
  //     of a function declaration or definition,
  //   - those meant for the computation of the LLVM type for an abstract
  //     appearance of a function, and
  //   - those meant for performing the IR-generation of a call.
  // They differ mainly in how they deal with optional (i.e. variadic)
  // arguments, as well as unprototyped functions.
  //
  // Key points:
  // - The CGFunctionInfo for emitting a specific call site must include
  //   entries for the optional arguments.
  // - The function type used at the call site must reflect the formal
  //   signature of the declaration being called, or else the call will
  //   go awry.
  // - For the most part, unprototyped functions are called by casting to
  //   a formal signature inferred from the specific argument types used
  //   at the call-site.  However, some targets (e.g. x86-64) screw with
  //   this for compatibility reasons.

  const CGFunctionInfo &arrangeGlobalDeclaration(GlobalDecl GD);

  /// Given a function info for a declaration, return the function info
  /// for a call with the given arguments.
  ///
  /// Often this will be able to simply return the declaration info.
  const CGFunctionInfo &arrangeCall(const CGFunctionInfo &declFI,
                                    const CallArgList &args);

  /// Free functions are functions that are compatible with an ordinary
  /// C function pointer type.
  const CGFunctionInfo &arrangeFunctionDeclaration(const FunctionDecl *FD);
  const CGFunctionInfo &arrangeFreeFunctionCall(const CallArgList &Args,
                                                const FunctionType *Ty,
                                                bool ChainCall);
  const CGFunctionInfo &arrangeFreeFunctionType(CanQual<FunctionProtoType> Ty,
                                                const FunctionDecl *FD);
  const CGFunctionInfo &arrangeFreeFunctionType(CanQual<FunctionNoProtoType> Ty);

  /// A nullary function is a freestanding function of type 'void ()'.
  /// This method works for both calls and declarations.
  const CGFunctionInfo &arrangeNullaryFunction();

  /// A builtin function is a freestanding function using the default
  /// C conventions.
  const CGFunctionInfo &
  arrangeBuiltinFunctionDeclaration(QualType resultType,
                                    const FunctionArgList &args);
  const CGFunctionInfo &
  arrangeBuiltinFunctionDeclaration(CanQualType resultType,
                                    ArrayRef<CanQualType> argTypes);
  const CGFunctionInfo &arrangeBuiltinFunctionCall(QualType resultType,
                                                   const CallArgList &args);

  /// Objective-C methods are C functions with some implicit parameters.
  const CGFunctionInfo &arrangeObjCMethodDeclaration(const ObjCMethodDecl *MD);
  const CGFunctionInfo &arrangeObjCMessageSendSignature(const ObjCMethodDecl *MD,
                                                        QualType receiverType);
  const CGFunctionInfo &arrangeUnprototypedObjCMessageSend(
                                                     QualType returnType,
                                                     const CallArgList &args);

  /// Block invocation functions are C functions with an implicit parameter.
  const CGFunctionInfo &arrangeBlockFunctionDeclaration(
                                                 const FunctionProtoType *type,
                                                 const FunctionArgList &args);
  const CGFunctionInfo &arrangeBlockFunctionCall(const CallArgList &args,
                                                 const FunctionType *type);

  /// C++ methods have some special rules and also have implicit parameters.
  const CGFunctionInfo &arrangeCXXMethodDeclaration(const CXXMethodDecl *MD);
  const CGFunctionInfo &arrangeCXXStructorDeclaration(const CXXMethodDecl *MD,
                                                      StructorType Type);
  const CGFunctionInfo &arrangeCXXConstructorCall(const CallArgList &Args,
                                                  const CXXConstructorDecl *D,
                                                  CXXCtorType CtorKind,
                                                  unsigned ExtraPrefixArgs,
                                                  unsigned ExtraSuffixArgs,
                                                  bool PassProtoArgs = true);

  const CGFunctionInfo &arrangeCXXMethodCall(const CallArgList &args,
                                             const FunctionProtoType *type,
                                             RequiredArgs required,
                                             unsigned numPrefixArgs);
  const CGFunctionInfo &
  arrangeUnprototypedMustTailThunk(const CXXMethodDecl *MD);
  const CGFunctionInfo &arrangeMSCtorClosure(const CXXConstructorDecl *CD,
                                                 CXXCtorType CT);
  const CGFunctionInfo &arrangeCXXMethodType(const CXXRecordDecl *RD,
                                             const FunctionProtoType *FTP,
                                             const CXXMethodDecl *MD);

  /// "Arrange" the LLVM information for a call or type with the given
  /// signature.  This is largely an internal method; other clients
  /// should use one of the above routines, which ultimately defer to
  /// this.
  ///
  /// \param argTypes - must all actually be canonical as params
  const CGFunctionInfo &arrangeLLVMFunctionInfo(CanQualType returnType,
                                                bool instanceMethod,
                                                bool chainCall,
                                                ArrayRef<CanQualType> argTypes,
                                                FunctionType::ExtInfo info,
                    ArrayRef<FunctionProtoType::ExtParameterInfo> paramInfos,
                                                RequiredArgs args);

  /// Compute a new LLVM record layout object for the given record.
  CGRecordLayout *ComputeRecordLayout(const RecordDecl *D,
                                      llvm::StructType *Ty);

  /// addRecordTypeName - Compute a name from the given record decl with an
  /// optional suffix and name the given LLVM type using it.
  void addRecordTypeName(const RecordDecl *RD, llvm::StructType *Ty,
                         StringRef suffix);


public:  // These are internal details of CGT that shouldn't be used externally.
  /// ConvertRecordDeclType - Lay out a tagged decl type like struct or union.
  llvm::StructType *ConvertRecordDeclType(const RecordDecl *TD);

  /// getExpandedTypes - Expand the type \arg Ty into the LLVM
  /// argument types it would be passed as. See ABIArgInfo::Expand.
  void getExpandedTypes(QualType Ty,
                        SmallVectorImpl<llvm::Type *>::iterator &TI);

  /// IsZeroInitializable - Return whether a type can be
  /// zero-initialized (in the C++ sense) with an LLVM zeroinitializer.
  bool isZeroInitializable(QualType T);

  /// Check if the pointer type can be zero-initialized (in the C++ sense)
  /// with an LLVM zeroinitializer.
  bool isPointerZeroInitializable(QualType T);

  /// IsZeroInitializable - Return whether a record type can be
  /// zero-initialized (in the C++ sense) with an LLVM zeroinitializer.
  bool isZeroInitializable(const RecordDecl *RD);

  bool isRecordLayoutComplete(const Type *Ty) const;
  bool noRecordsBeingLaidOut() const {
    return RecordsBeingLaidOut.empty();
  }
  bool isRecordBeingLaidOut(const Type *Ty) const {
    return RecordsBeingLaidOut.count(Ty);
  }

};

}  // end namespace CodeGen
}  // end namespace clang

#endif
