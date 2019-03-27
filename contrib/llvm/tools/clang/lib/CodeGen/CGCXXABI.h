//===----- CGCXXABI.h - Interface to C++ ABIs -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for C++ code generation. Concrete subclasses
// of this implement code generation for specific C++ ABIs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGCXXABI_H
#define LLVM_CLANG_LIB_CODEGEN_CGCXXABI_H

#include "CodeGenFunction.h"
#include "clang/Basic/LLVM.h"

namespace llvm {
class Constant;
class Type;
class Value;
class CallInst;
}

namespace clang {
class CastExpr;
class CXXConstructorDecl;
class CXXDestructorDecl;
class CXXMethodDecl;
class CXXRecordDecl;
class FieldDecl;
class MangleContext;

namespace CodeGen {
class CGCallee;
class CodeGenFunction;
class CodeGenModule;
struct CatchTypeInfo;

/// Implements C++ ABI-specific code generation functions.
class CGCXXABI {
protected:
  CodeGenModule &CGM;
  std::unique_ptr<MangleContext> MangleCtx;

  CGCXXABI(CodeGenModule &CGM)
    : CGM(CGM), MangleCtx(CGM.getContext().createMangleContext()) {}

protected:
  ImplicitParamDecl *getThisDecl(CodeGenFunction &CGF) {
    return CGF.CXXABIThisDecl;
  }
  llvm::Value *getThisValue(CodeGenFunction &CGF) {
    return CGF.CXXABIThisValue;
  }
  Address getThisAddress(CodeGenFunction &CGF) {
    return Address(CGF.CXXABIThisValue, CGF.CXXABIThisAlignment);
  }

  /// Issue a diagnostic about unsupported features in the ABI.
  void ErrorUnsupportedABI(CodeGenFunction &CGF, StringRef S);

  /// Get a null value for unsupported member pointers.
  llvm::Constant *GetBogusMemberPointer(QualType T);

  ImplicitParamDecl *&getStructorImplicitParamDecl(CodeGenFunction &CGF) {
    return CGF.CXXStructorImplicitParamDecl;
  }
  llvm::Value *&getStructorImplicitParamValue(CodeGenFunction &CGF) {
    return CGF.CXXStructorImplicitParamValue;
  }

  /// Loads the incoming C++ this pointer as it was passed by the caller.
  llvm::Value *loadIncomingCXXThis(CodeGenFunction &CGF);

  void setCXXABIThisValue(CodeGenFunction &CGF, llvm::Value *ThisPtr);

  ASTContext &getContext() const { return CGM.getContext(); }

  virtual bool requiresArrayCookie(const CXXDeleteExpr *E, QualType eltType);
  virtual bool requiresArrayCookie(const CXXNewExpr *E);

  /// Determine whether there's something special about the rules of
  /// the ABI tell us that 'this' is a complete object within the
  /// given function.  Obvious common logic like being defined on a
  /// final class will have been taken care of by the caller.
  virtual bool isThisCompleteObject(GlobalDecl GD) const = 0;

public:

  virtual ~CGCXXABI();

  /// Gets the mangle context.
  MangleContext &getMangleContext() {
    return *MangleCtx;
  }

  /// Returns true if the given constructor or destructor is one of the
  /// kinds that the ABI says returns 'this' (only applies when called
  /// non-virtually for destructors).
  ///
  /// There currently is no way to indicate if a destructor returns 'this'
  /// when called virtually, and code generation does not support the case.
  virtual bool HasThisReturn(GlobalDecl GD) const { return false; }

  virtual bool hasMostDerivedReturn(GlobalDecl GD) const { return false; }

  /// Returns true if the target allows calling a function through a pointer
  /// with a different signature than the actual function (or equivalently,
  /// bitcasting a function or function pointer to a different function type).
  /// In principle in the most general case this could depend on the target, the
  /// calling convention, and the actual types of the arguments and return
  /// value. Here it just means whether the signature mismatch could *ever* be
  /// allowed; in other words, does the target do strict checking of signatures
  /// for all calls.
  virtual bool canCallMismatchedFunctionType() const { return true; }

  /// If the C++ ABI requires the given type be returned in a particular way,
  /// this method sets RetAI and returns true.
  virtual bool classifyReturnType(CGFunctionInfo &FI) const = 0;

  /// Specify how one should pass an argument of a record type.
  enum RecordArgABI {
    /// Pass it using the normal C aggregate rules for the ABI, potentially
    /// introducing extra copies and passing some or all of it in registers.
    RAA_Default = 0,

    /// Pass it on the stack using its defined layout.  The argument must be
    /// evaluated directly into the correct stack position in the arguments area,
    /// and the call machinery must not move it or introduce extra copies.
    RAA_DirectInMemory,

    /// Pass it as a pointer to temporary memory.
    RAA_Indirect
  };

  /// Returns true if C++ allows us to copy the memory of an object of type RD
  /// when it is passed as an argument.
  bool canCopyArgument(const CXXRecordDecl *RD) const;

  /// Returns how an argument of the given record type should be passed.
  virtual RecordArgABI getRecordArgABI(const CXXRecordDecl *RD) const = 0;

  /// Returns true if the implicit 'sret' parameter comes after the implicit
  /// 'this' parameter of C++ instance methods.
  virtual bool isSRetParameterAfterThis() const { return false; }

  /// Find the LLVM type used to represent the given member pointer
  /// type.
  virtual llvm::Type *
  ConvertMemberPointerType(const MemberPointerType *MPT);

  /// Load a member function from an object and a member function
  /// pointer.  Apply the this-adjustment and set 'This' to the
  /// adjusted value.
  virtual CGCallee EmitLoadOfMemberFunctionPointer(
      CodeGenFunction &CGF, const Expr *E, Address This,
      llvm::Value *&ThisPtrForCall, llvm::Value *MemPtr,
      const MemberPointerType *MPT);

  /// Calculate an l-value from an object and a data member pointer.
  virtual llvm::Value *
  EmitMemberDataPointerAddress(CodeGenFunction &CGF, const Expr *E,
                               Address Base, llvm::Value *MemPtr,
                               const MemberPointerType *MPT);

  /// Perform a derived-to-base, base-to-derived, or bitcast member
  /// pointer conversion.
  virtual llvm::Value *EmitMemberPointerConversion(CodeGenFunction &CGF,
                                                   const CastExpr *E,
                                                   llvm::Value *Src);

  /// Perform a derived-to-base, base-to-derived, or bitcast member
  /// pointer conversion on a constant value.
  virtual llvm::Constant *EmitMemberPointerConversion(const CastExpr *E,
                                                      llvm::Constant *Src);

  /// Return true if the given member pointer can be zero-initialized
  /// (in the C++ sense) with an LLVM zeroinitializer.
  virtual bool isZeroInitializable(const MemberPointerType *MPT);

  /// Return whether or not a member pointers type is convertible to an IR type.
  virtual bool isMemberPointerConvertible(const MemberPointerType *MPT) const {
    return true;
  }

  /// Create a null member pointer of the given type.
  virtual llvm::Constant *EmitNullMemberPointer(const MemberPointerType *MPT);

  /// Create a member pointer for the given method.
  virtual llvm::Constant *EmitMemberFunctionPointer(const CXXMethodDecl *MD);

  /// Create a member pointer for the given field.
  virtual llvm::Constant *EmitMemberDataPointer(const MemberPointerType *MPT,
                                                CharUnits offset);

  /// Create a member pointer for the given member pointer constant.
  virtual llvm::Constant *EmitMemberPointer(const APValue &MP, QualType MPT);

  /// Emit a comparison between two member pointers.  Returns an i1.
  virtual llvm::Value *
  EmitMemberPointerComparison(CodeGenFunction &CGF,
                              llvm::Value *L,
                              llvm::Value *R,
                              const MemberPointerType *MPT,
                              bool Inequality);

  /// Determine if a member pointer is non-null.  Returns an i1.
  virtual llvm::Value *
  EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                             llvm::Value *MemPtr,
                             const MemberPointerType *MPT);

protected:
  /// A utility method for computing the offset required for the given
  /// base-to-derived or derived-to-base member-pointer conversion.
  /// Does not handle virtual conversions (in case we ever fully
  /// support an ABI that allows this).  Returns null if no adjustment
  /// is required.
  llvm::Constant *getMemberPointerAdjustment(const CastExpr *E);

  /// Computes the non-virtual adjustment needed for a member pointer
  /// conversion along an inheritance path stored in an APValue.  Unlike
  /// getMemberPointerAdjustment(), the adjustment can be negative if the path
  /// is from a derived type to a base type.
  CharUnits getMemberPointerPathAdjustment(const APValue &MP);

public:
  virtual void emitVirtualObjectDelete(CodeGenFunction &CGF,
                                       const CXXDeleteExpr *DE,
                                       Address Ptr, QualType ElementType,
                                       const CXXDestructorDecl *Dtor) = 0;
  virtual void emitRethrow(CodeGenFunction &CGF, bool isNoReturn) = 0;
  virtual void emitThrow(CodeGenFunction &CGF, const CXXThrowExpr *E) = 0;
  virtual llvm::GlobalVariable *getThrowInfo(QualType T) { return nullptr; }

  /// Determine whether it's possible to emit a vtable for \p RD, even
  /// though we do not know that the vtable has been marked as used by semantic
  /// analysis.
  virtual bool canSpeculativelyEmitVTable(const CXXRecordDecl *RD) const = 0;

  virtual void emitBeginCatch(CodeGenFunction &CGF, const CXXCatchStmt *C) = 0;

  virtual llvm::CallInst *
  emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                      llvm::Value *Exn);

  virtual llvm::Constant *getAddrOfRTTIDescriptor(QualType Ty) = 0;
  virtual CatchTypeInfo
  getAddrOfCXXCatchHandlerType(QualType Ty, QualType CatchHandlerType) = 0;
  virtual CatchTypeInfo getCatchAllTypeInfo();

  virtual bool shouldTypeidBeNullChecked(bool IsDeref,
                                         QualType SrcRecordTy) = 0;
  virtual void EmitBadTypeidCall(CodeGenFunction &CGF) = 0;
  virtual llvm::Value *EmitTypeid(CodeGenFunction &CGF, QualType SrcRecordTy,
                                  Address ThisPtr,
                                  llvm::Type *StdTypeInfoPtrTy) = 0;

  virtual bool shouldDynamicCastCallBeNullChecked(bool SrcIsPtr,
                                                  QualType SrcRecordTy) = 0;

  virtual llvm::Value *
  EmitDynamicCastCall(CodeGenFunction &CGF, Address Value,
                      QualType SrcRecordTy, QualType DestTy,
                      QualType DestRecordTy, llvm::BasicBlock *CastEnd) = 0;

  virtual llvm::Value *EmitDynamicCastToVoid(CodeGenFunction &CGF,
                                             Address Value,
                                             QualType SrcRecordTy,
                                             QualType DestTy) = 0;

  virtual bool EmitBadCastCall(CodeGenFunction &CGF) = 0;

  virtual llvm::Value *GetVirtualBaseClassOffset(CodeGenFunction &CGF,
                                                 Address This,
                                                 const CXXRecordDecl *ClassDecl,
                                        const CXXRecordDecl *BaseClassDecl) = 0;

  virtual llvm::BasicBlock *EmitCtorCompleteObjectHandler(CodeGenFunction &CGF,
                                                          const CXXRecordDecl *RD);

  /// Emit the code to initialize hidden members required
  /// to handle virtual inheritance, if needed by the ABI.
  virtual void
  initializeHiddenVirtualInheritanceMembers(CodeGenFunction &CGF,
                                            const CXXRecordDecl *RD) {}

  /// Emit constructor variants required by this ABI.
  virtual void EmitCXXConstructors(const CXXConstructorDecl *D) = 0;

  /// Notes how many arguments were added to the beginning (Prefix) and ending
  /// (Suffix) of an arg list.
  ///
  /// Note that Prefix actually refers to the number of args *after* the first
  /// one: `this` arguments always come first.
  struct AddedStructorArgs {
    unsigned Prefix = 0;
    unsigned Suffix = 0;
    AddedStructorArgs() = default;
    AddedStructorArgs(unsigned P, unsigned S) : Prefix(P), Suffix(S) {}
    static AddedStructorArgs prefix(unsigned N) { return {N, 0}; }
    static AddedStructorArgs suffix(unsigned N) { return {0, N}; }
  };

  /// Build the signature of the given constructor or destructor variant by
  /// adding any required parameters.  For convenience, ArgTys has been
  /// initialized with the type of 'this'.
  virtual AddedStructorArgs
  buildStructorSignature(const CXXMethodDecl *MD, StructorType T,
                         SmallVectorImpl<CanQualType> &ArgTys) = 0;

  /// Returns true if the given destructor type should be emitted as a linkonce
  /// delegating thunk, regardless of whether the dtor is defined in this TU or
  /// not.
  virtual bool useThunkForDtorVariant(const CXXDestructorDecl *Dtor,
                                      CXXDtorType DT) const = 0;

  virtual void setCXXDestructorDLLStorage(llvm::GlobalValue *GV,
                                          const CXXDestructorDecl *Dtor,
                                          CXXDtorType DT) const;

  virtual llvm::GlobalValue::LinkageTypes
  getCXXDestructorLinkage(GVALinkage Linkage, const CXXDestructorDecl *Dtor,
                          CXXDtorType DT) const;

  /// Emit destructor variants required by this ABI.
  virtual void EmitCXXDestructors(const CXXDestructorDecl *D) = 0;

  /// Get the type of the implicit "this" parameter used by a method. May return
  /// zero if no specific type is applicable, e.g. if the ABI expects the "this"
  /// parameter to point to some artificial offset in a complete object due to
  /// vbases being reordered.
  virtual const CXXRecordDecl *
  getThisArgumentTypeForMethod(const CXXMethodDecl *MD) {
    return MD->getParent();
  }

  /// Perform ABI-specific "this" argument adjustment required prior to
  /// a call of a virtual function.
  /// The "VirtualCall" argument is true iff the call itself is virtual.
  virtual Address
  adjustThisArgumentForVirtualFunctionCall(CodeGenFunction &CGF, GlobalDecl GD,
                                           Address This, bool VirtualCall) {
    return This;
  }

  /// Build a parameter variable suitable for 'this'.
  void buildThisParam(CodeGenFunction &CGF, FunctionArgList &Params);

  /// Insert any ABI-specific implicit parameters into the parameter list for a
  /// function.  This generally involves extra data for constructors and
  /// destructors.
  ///
  /// ABIs may also choose to override the return type, which has been
  /// initialized with the type of 'this' if HasThisReturn(CGF.CurGD) is true or
  /// the formal return type of the function otherwise.
  virtual void addImplicitStructorParams(CodeGenFunction &CGF, QualType &ResTy,
                                         FunctionArgList &Params) = 0;

  /// Get the ABI-specific "this" parameter adjustment to apply in the prologue
  /// of a virtual function.
  virtual CharUnits getVirtualFunctionPrologueThisAdjustment(GlobalDecl GD) {
    return CharUnits::Zero();
  }

  /// Emit the ABI-specific prolog for the function.
  virtual void EmitInstanceFunctionProlog(CodeGenFunction &CGF) = 0;

  /// Add any ABI-specific implicit arguments needed to call a constructor.
  ///
  /// \return The number of arguments added at the beginning and end of the
  /// call, which is typically zero or one.
  virtual AddedStructorArgs
  addImplicitConstructorArgs(CodeGenFunction &CGF, const CXXConstructorDecl *D,
                             CXXCtorType Type, bool ForVirtualBase,
                             bool Delegating, CallArgList &Args) = 0;

  /// Emit the destructor call.
  virtual void EmitDestructorCall(CodeGenFunction &CGF,
                                  const CXXDestructorDecl *DD, CXXDtorType Type,
                                  bool ForVirtualBase, bool Delegating,
                                  Address This) = 0;

  /// Emits the VTable definitions required for the given record type.
  virtual void emitVTableDefinitions(CodeGenVTables &CGVT,
                                     const CXXRecordDecl *RD) = 0;

  /// Checks if ABI requires extra virtual offset for vtable field.
  virtual bool
  isVirtualOffsetNeededForVTableField(CodeGenFunction &CGF,
                                      CodeGenFunction::VPtr Vptr) = 0;

  /// Checks if ABI requires to initialize vptrs for given dynamic class.
  virtual bool doStructorsInitializeVPtrs(const CXXRecordDecl *VTableClass) = 0;

  /// Get the address point of the vtable for the given base subobject.
  virtual llvm::Constant *
  getVTableAddressPoint(BaseSubobject Base,
                        const CXXRecordDecl *VTableClass) = 0;

  /// Get the address point of the vtable for the given base subobject while
  /// building a constructor or a destructor.
  virtual llvm::Value *
  getVTableAddressPointInStructor(CodeGenFunction &CGF, const CXXRecordDecl *RD,
                                  BaseSubobject Base,
                                  const CXXRecordDecl *NearestVBase) = 0;

  /// Get the address point of the vtable for the given base subobject while
  /// building a constexpr.
  virtual llvm::Constant *
  getVTableAddressPointForConstExpr(BaseSubobject Base,
                                    const CXXRecordDecl *VTableClass) = 0;

  /// Get the address of the vtable for the given record decl which should be
  /// used for the vptr at the given offset in RD.
  virtual llvm::GlobalVariable *getAddrOfVTable(const CXXRecordDecl *RD,
                                                CharUnits VPtrOffset) = 0;

  /// Build a virtual function pointer in the ABI-specific way.
  virtual CGCallee getVirtualFunctionPointer(CodeGenFunction &CGF,
                                             GlobalDecl GD, Address This,
                                             llvm::Type *Ty,
                                             SourceLocation Loc) = 0;

  /// Emit the ABI-specific virtual destructor call.
  virtual llvm::Value *
  EmitVirtualDestructorCall(CodeGenFunction &CGF, const CXXDestructorDecl *Dtor,
                            CXXDtorType DtorType, Address This,
                            const CXXMemberCallExpr *CE) = 0;

  virtual void adjustCallArgsForDestructorThunk(CodeGenFunction &CGF,
                                                GlobalDecl GD,
                                                CallArgList &CallArgs) {}

  /// Emit any tables needed to implement virtual inheritance.  For Itanium,
  /// this emits virtual table tables.  For the MSVC++ ABI, this emits virtual
  /// base tables.
  virtual void emitVirtualInheritanceTables(const CXXRecordDecl *RD) = 0;

  virtual bool exportThunk() = 0;
  virtual void setThunkLinkage(llvm::Function *Thunk, bool ForVTable,
                               GlobalDecl GD, bool ReturnAdjustment) = 0;

  virtual llvm::Value *performThisAdjustment(CodeGenFunction &CGF,
                                             Address This,
                                             const ThisAdjustment &TA) = 0;

  virtual llvm::Value *performReturnAdjustment(CodeGenFunction &CGF,
                                               Address Ret,
                                               const ReturnAdjustment &RA) = 0;

  virtual void EmitReturnFromThunk(CodeGenFunction &CGF,
                                   RValue RV, QualType ResultType);

  virtual size_t getSrcArgforCopyCtor(const CXXConstructorDecl *,
                                      FunctionArgList &Args) const = 0;

  /// Gets the offsets of all the virtual base pointers in a given class.
  virtual std::vector<CharUnits> getVBPtrOffsets(const CXXRecordDecl *RD);

  /// Gets the pure virtual member call function.
  virtual StringRef GetPureVirtualCallName() = 0;

  /// Gets the deleted virtual member call name.
  virtual StringRef GetDeletedVirtualCallName() = 0;

  /**************************** Array cookies ******************************/

  /// Returns the extra size required in order to store the array
  /// cookie for the given new-expression.  May return 0 to indicate that no
  /// array cookie is required.
  ///
  /// Several cases are filtered out before this method is called:
  ///   - non-array allocations never need a cookie
  ///   - calls to \::operator new(size_t, void*) never need a cookie
  ///
  /// \param expr - the new-expression being allocated.
  virtual CharUnits GetArrayCookieSize(const CXXNewExpr *expr);

  /// Initialize the array cookie for the given allocation.
  ///
  /// \param NewPtr - a char* which is the presumed-non-null
  ///   return value of the allocation function
  /// \param NumElements - the computed number of elements,
  ///   potentially collapsed from the multidimensional array case;
  ///   always a size_t
  /// \param ElementType - the base element allocated type,
  ///   i.e. the allocated type after stripping all array types
  virtual Address InitializeArrayCookie(CodeGenFunction &CGF,
                                        Address NewPtr,
                                        llvm::Value *NumElements,
                                        const CXXNewExpr *expr,
                                        QualType ElementType);

  /// Reads the array cookie associated with the given pointer,
  /// if it has one.
  ///
  /// \param Ptr - a pointer to the first element in the array
  /// \param ElementType - the base element type of elements of the array
  /// \param NumElements - an out parameter which will be initialized
  ///   with the number of elements allocated, or zero if there is no
  ///   cookie
  /// \param AllocPtr - an out parameter which will be initialized
  ///   with a char* pointing to the address returned by the allocation
  ///   function
  /// \param CookieSize - an out parameter which will be initialized
  ///   with the size of the cookie, or zero if there is no cookie
  virtual void ReadArrayCookie(CodeGenFunction &CGF, Address Ptr,
                               const CXXDeleteExpr *expr,
                               QualType ElementType, llvm::Value *&NumElements,
                               llvm::Value *&AllocPtr, CharUnits &CookieSize);

  /// Return whether the given global decl needs a VTT parameter.
  virtual bool NeedsVTTParameter(GlobalDecl GD);

protected:
  /// Returns the extra size required in order to store the array
  /// cookie for the given type.  Assumes that an array cookie is
  /// required.
  virtual CharUnits getArrayCookieSizeImpl(QualType elementType);

  /// Reads the array cookie for an allocation which is known to have one.
  /// This is called by the standard implementation of ReadArrayCookie.
  ///
  /// \param ptr - a pointer to the allocation made for an array, as a char*
  /// \param cookieSize - the computed cookie size of an array
  ///
  /// Other parameters are as above.
  ///
  /// \return a size_t
  virtual llvm::Value *readArrayCookieImpl(CodeGenFunction &IGF, Address ptr,
                                           CharUnits cookieSize);

public:

  /*************************** Static local guards ****************************/

  /// Emits the guarded initializer and destructor setup for the given
  /// variable, given that it couldn't be emitted as a constant.
  /// If \p PerformInit is false, the initialization has been folded to a
  /// constant and should not be performed.
  ///
  /// The variable may be:
  ///   - a static local variable
  ///   - a static data member of a class template instantiation
  virtual void EmitGuardedInit(CodeGenFunction &CGF, const VarDecl &D,
                               llvm::GlobalVariable *DeclPtr,
                               bool PerformInit) = 0;

  /// Emit code to force the execution of a destructor during global
  /// teardown.  The default implementation of this uses atexit.
  ///
  /// \param Dtor - a function taking a single pointer argument
  /// \param Addr - a pointer to pass to the destructor function.
  virtual void registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                                  llvm::Constant *Dtor,
                                  llvm::Constant *Addr) = 0;

  /*************************** thread_local initialization ********************/

  /// Emits ABI-required functions necessary to initialize thread_local
  /// variables in this translation unit.
  ///
  /// \param CXXThreadLocals - The thread_local declarations in this translation
  ///        unit.
  /// \param CXXThreadLocalInits - If this translation unit contains any
  ///        non-constant initialization or non-trivial destruction for
  ///        thread_local variables, a list of functions to perform the
  ///        initialization.
  virtual void EmitThreadLocalInitFuncs(
      CodeGenModule &CGM, ArrayRef<const VarDecl *> CXXThreadLocals,
      ArrayRef<llvm::Function *> CXXThreadLocalInits,
      ArrayRef<const VarDecl *> CXXThreadLocalInitVars) = 0;

  // Determine if references to thread_local global variables can be made
  // directly or require access through a thread wrapper function.
  virtual bool usesThreadWrapperFunction() const = 0;

  /// Emit a reference to a non-local thread_local variable (including
  /// triggering the initialization of all thread_local variables in its
  /// translation unit).
  virtual LValue EmitThreadLocalVarDeclLValue(CodeGenFunction &CGF,
                                              const VarDecl *VD,
                                              QualType LValType) = 0;

  /// Emit a single constructor/destructor with the given type from a C++
  /// constructor Decl.
  virtual void emitCXXStructor(const CXXMethodDecl *MD, StructorType Type) = 0;

  /// Load a vtable from This, an object of polymorphic type RD, or from one of
  /// its virtual bases if it does not have its own vtable. Returns the vtable
  /// and the class from which the vtable was loaded.
  virtual std::pair<llvm::Value *, const CXXRecordDecl *>
  LoadVTablePtr(CodeGenFunction &CGF, Address This,
                const CXXRecordDecl *RD) = 0;
};

// Create an instance of a C++ ABI class:

/// Creates an Itanium-family ABI.
CGCXXABI *CreateItaniumCXXABI(CodeGenModule &CGM);

/// Creates a Microsoft-family ABI.
CGCXXABI *CreateMicrosoftCXXABI(CodeGenModule &CGM);

struct CatchRetScope final : EHScopeStack::Cleanup {
  llvm::CatchPadInst *CPI;

  CatchRetScope(llvm::CatchPadInst *CPI) : CPI(CPI) {}

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    llvm::BasicBlock *BB = CGF.createBasicBlock("catchret.dest");
    CGF.Builder.CreateCatchRet(CPI, BB);
    CGF.EmitBlock(BB);
  }
};
}
}

#endif
