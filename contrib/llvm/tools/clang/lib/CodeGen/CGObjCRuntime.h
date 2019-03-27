//===----- CGObjCRuntime.h - Interface to ObjC Runtimes ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for Objective-C code generation.  Concrete
// subclasses of this implement code generation for specific Objective-C
// runtime libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGOBJCRUNTIME_H
#define LLVM_CLANG_LIB_CODEGEN_CGOBJCRUNTIME_H
#include "CGBuilder.h"
#include "CGCall.h"
#include "CGCleanup.h"
#include "CGValue.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/IdentifierTable.h" // Selector

namespace llvm {
  class Constant;
  class Function;
  class Module;
  class StructLayout;
  class StructType;
  class Type;
  class Value;
}

namespace clang {
namespace CodeGen {
  class CodeGenFunction;
}

  class FieldDecl;
  class ObjCAtTryStmt;
  class ObjCAtThrowStmt;
  class ObjCAtSynchronizedStmt;
  class ObjCContainerDecl;
  class ObjCCategoryImplDecl;
  class ObjCImplementationDecl;
  class ObjCInterfaceDecl;
  class ObjCMessageExpr;
  class ObjCMethodDecl;
  class ObjCProtocolDecl;
  class Selector;
  class ObjCIvarDecl;
  class ObjCStringLiteral;
  class BlockDeclRefExpr;

namespace CodeGen {
  class CodeGenModule;
  class CGBlockInfo;

// FIXME: Several methods should be pure virtual but aren't to avoid the
// partially-implemented subclass breaking.

/// Implements runtime-specific code generation functions.
class CGObjCRuntime {
protected:
  CodeGen::CodeGenModule &CGM;
  CGObjCRuntime(CodeGen::CodeGenModule &CGM) : CGM(CGM) {}

  // Utility functions for unified ivar access. These need to
  // eventually be folded into other places (the structure layout
  // code).

  /// Compute an offset to the given ivar, suitable for passing to
  /// EmitValueForIvarAtOffset.  Note that the correct handling of
  /// bit-fields is carefully coordinated by these two, use caution!
  ///
  /// The latter overload is suitable for computing the offset of a
  /// sythesized ivar.
  uint64_t ComputeIvarBaseOffset(CodeGen::CodeGenModule &CGM,
                                 const ObjCInterfaceDecl *OID,
                                 const ObjCIvarDecl *Ivar);
  uint64_t ComputeIvarBaseOffset(CodeGen::CodeGenModule &CGM,
                                 const ObjCImplementationDecl *OID,
                                 const ObjCIvarDecl *Ivar);

  LValue EmitValueForIvarAtOffset(CodeGen::CodeGenFunction &CGF,
                                  const ObjCInterfaceDecl *OID,
                                  llvm::Value *BaseValue,
                                  const ObjCIvarDecl *Ivar,
                                  unsigned CVRQualifiers,
                                  llvm::Value *Offset);
  /// Emits a try / catch statement.  This function is intended to be called by
  /// subclasses, and provides a generic mechanism for generating these, which
  /// should be usable by all runtimes.  The caller must provide the functions
  /// to call when entering and exiting a \@catch() block, and the function
  /// used to rethrow exceptions.  If the begin and end catch functions are
  /// NULL, then the function assumes that the EH personality function provides
  /// the thrown object directly.
  void EmitTryCatchStmt(CodeGenFunction &CGF,
                        const ObjCAtTryStmt &S,
                        llvm::Constant *beginCatchFn,
                        llvm::Constant *endCatchFn,
                        llvm::Constant *exceptionRethrowFn);

  void EmitInitOfCatchParam(CodeGenFunction &CGF, llvm::Value *exn,
                            const VarDecl *paramDecl);

  /// Emits an \@synchronize() statement, using the \p syncEnterFn and
  /// \p syncExitFn arguments as the functions called to lock and unlock
  /// the object.  This function can be called by subclasses that use
  /// zero-cost exception handling.
  void EmitAtSynchronizedStmt(CodeGenFunction &CGF,
                            const ObjCAtSynchronizedStmt &S,
                            llvm::Function *syncEnterFn,
                            llvm::Function *syncExitFn);

public:
  virtual ~CGObjCRuntime();

  /// Generate the function required to register all Objective-C components in
  /// this compilation unit with the runtime library.
  virtual llvm::Function *ModuleInitFunction() = 0;

  /// Get a selector for the specified name and type values.
  /// The result should have the LLVM type for ASTContext::getObjCSelType().
  virtual llvm::Value *GetSelector(CodeGenFunction &CGF, Selector Sel) = 0;

  /// Get the address of a selector for the specified name and type values.
  /// This is a rarely-used language extension, but sadly it exists.
  ///
  /// The result should have the LLVM type for a pointer to
  /// ASTContext::getObjCSelType().
  virtual Address GetAddrOfSelector(CodeGenFunction &CGF, Selector Sel) = 0;

  /// Get a typed selector.
  virtual llvm::Value *GetSelector(CodeGenFunction &CGF,
                                   const ObjCMethodDecl *Method) = 0;

  /// Get the type constant to catch for the given ObjC pointer type.
  /// This is used externally to implement catching ObjC types in C++.
  /// Runtimes which don't support this should add the appropriate
  /// error to Sema.
  virtual llvm::Constant *GetEHType(QualType T) = 0;

  virtual CatchTypeInfo getCatchAllTypeInfo() { return { nullptr, 0 }; }

  /// Generate a constant string object.
  virtual ConstantAddress GenerateConstantString(const StringLiteral *) = 0;

  /// Generate a category.  A category contains a list of methods (and
  /// accompanying metadata) and a list of protocols.
  virtual void GenerateCategory(const ObjCCategoryImplDecl *OCD) = 0;

  /// Generate a class structure for this class.
  virtual void GenerateClass(const ObjCImplementationDecl *OID) = 0;

  /// Register an class alias.
  virtual void RegisterAlias(const ObjCCompatibleAliasDecl *OAD) = 0;

  /// Generate an Objective-C message send operation.
  ///
  /// \param Method - The method being called, this may be null if synthesizing
  /// a property setter or getter.
  virtual CodeGen::RValue
  GenerateMessageSend(CodeGen::CodeGenFunction &CGF,
                      ReturnValueSlot ReturnSlot,
                      QualType ResultType,
                      Selector Sel,
                      llvm::Value *Receiver,
                      const CallArgList &CallArgs,
                      const ObjCInterfaceDecl *Class = nullptr,
                      const ObjCMethodDecl *Method = nullptr) = 0;

  /// Generate an Objective-C message send operation to the super
  /// class initiated in a method for Class and with the given Self
  /// object.
  ///
  /// \param Method - The method being called, this may be null if synthesizing
  /// a property setter or getter.
  virtual CodeGen::RValue
  GenerateMessageSendSuper(CodeGen::CodeGenFunction &CGF,
                           ReturnValueSlot ReturnSlot,
                           QualType ResultType,
                           Selector Sel,
                           const ObjCInterfaceDecl *Class,
                           bool isCategoryImpl,
                           llvm::Value *Self,
                           bool IsClassMessage,
                           const CallArgList &CallArgs,
                           const ObjCMethodDecl *Method = nullptr) = 0;

  /// Emit the code to return the named protocol as an object, as in a
  /// \@protocol expression.
  virtual llvm::Value *GenerateProtocolRef(CodeGenFunction &CGF,
                                           const ObjCProtocolDecl *OPD) = 0;

  /// Generate the named protocol.  Protocols contain method metadata but no
  /// implementations.
  virtual void GenerateProtocol(const ObjCProtocolDecl *OPD) = 0;

  /// Generate a function preamble for a method with the specified
  /// types.

  // FIXME: Current this just generates the Function definition, but really this
  // should also be generating the loads of the parameters, as the runtime
  // should have full control over how parameters are passed.
  virtual llvm::Function *GenerateMethod(const ObjCMethodDecl *OMD,
                                         const ObjCContainerDecl *CD) = 0;

  /// Return the runtime function for getting properties.
  virtual llvm::Constant *GetPropertyGetFunction() = 0;

  /// Return the runtime function for setting properties.
  virtual llvm::Constant *GetPropertySetFunction() = 0;

  /// Return the runtime function for optimized setting properties.
  virtual llvm::Constant *GetOptimizedPropertySetFunction(bool atomic,
                                                          bool copy) = 0;

  // API for atomic copying of qualified aggregates in getter.
  virtual llvm::Constant *GetGetStructFunction() = 0;
  // API for atomic copying of qualified aggregates in setter.
  virtual llvm::Constant *GetSetStructFunction() = 0;
  /// API for atomic copying of qualified aggregates with non-trivial copy
  /// assignment (c++) in setter.
  virtual llvm::Constant *GetCppAtomicObjectSetFunction() = 0;
  /// API for atomic copying of qualified aggregates with non-trivial copy
  /// assignment (c++) in getter.
  virtual llvm::Constant *GetCppAtomicObjectGetFunction() = 0;

  /// GetClass - Return a reference to the class for the given
  /// interface decl.
  virtual llvm::Value *GetClass(CodeGenFunction &CGF,
                                const ObjCInterfaceDecl *OID) = 0;


  virtual llvm::Value *EmitNSAutoreleasePoolClassRef(CodeGenFunction &CGF) {
    llvm_unreachable("autoreleasepool unsupported in this ABI");
  }

  /// EnumerationMutationFunction - Return the function that's called by the
  /// compiler when a mutation is detected during foreach iteration.
  virtual llvm::Constant *EnumerationMutationFunction() = 0;

  virtual void EmitSynchronizedStmt(CodeGen::CodeGenFunction &CGF,
                                    const ObjCAtSynchronizedStmt &S) = 0;
  virtual void EmitTryStmt(CodeGen::CodeGenFunction &CGF,
                           const ObjCAtTryStmt &S) = 0;
  virtual void EmitThrowStmt(CodeGen::CodeGenFunction &CGF,
                             const ObjCAtThrowStmt &S,
                             bool ClearInsertionPoint=true) = 0;
  virtual llvm::Value *EmitObjCWeakRead(CodeGen::CodeGenFunction &CGF,
                                        Address AddrWeakObj) = 0;
  virtual void EmitObjCWeakAssign(CodeGen::CodeGenFunction &CGF,
                                  llvm::Value *src, Address dest) = 0;
  virtual void EmitObjCGlobalAssign(CodeGen::CodeGenFunction &CGF,
                                    llvm::Value *src, Address dest,
                                    bool threadlocal=false) = 0;
  virtual void EmitObjCIvarAssign(CodeGen::CodeGenFunction &CGF,
                                  llvm::Value *src, Address dest,
                                  llvm::Value *ivarOffset) = 0;
  virtual void EmitObjCStrongCastAssign(CodeGen::CodeGenFunction &CGF,
                                        llvm::Value *src, Address dest) = 0;

  virtual LValue EmitObjCValueForIvar(CodeGen::CodeGenFunction &CGF,
                                      QualType ObjectTy,
                                      llvm::Value *BaseValue,
                                      const ObjCIvarDecl *Ivar,
                                      unsigned CVRQualifiers) = 0;
  virtual llvm::Value *EmitIvarOffset(CodeGen::CodeGenFunction &CGF,
                                      const ObjCInterfaceDecl *Interface,
                                      const ObjCIvarDecl *Ivar) = 0;
  virtual void EmitGCMemmoveCollectable(CodeGen::CodeGenFunction &CGF,
                                        Address DestPtr,
                                        Address SrcPtr,
                                        llvm::Value *Size) = 0;
  virtual llvm::Constant *BuildGCBlockLayout(CodeGen::CodeGenModule &CGM,
                                  const CodeGen::CGBlockInfo &blockInfo) = 0;
  virtual llvm::Constant *BuildRCBlockLayout(CodeGen::CodeGenModule &CGM,
                                  const CodeGen::CGBlockInfo &blockInfo) = 0;
  virtual std::string getRCBlockLayoutStr(CodeGen::CodeGenModule &CGM,
                                          const CGBlockInfo &blockInfo) {
    return {};
  }

  /// Returns an i8* which points to the byref layout information.
  virtual llvm::Constant *BuildByrefLayout(CodeGen::CodeGenModule &CGM,
                                           QualType T) = 0;

  struct MessageSendInfo {
    const CGFunctionInfo &CallInfo;
    llvm::PointerType *MessengerType;

    MessageSendInfo(const CGFunctionInfo &callInfo,
                    llvm::PointerType *messengerType)
      : CallInfo(callInfo), MessengerType(messengerType) {}
  };

  MessageSendInfo getMessageSendInfo(const ObjCMethodDecl *method,
                                     QualType resultType,
                                     CallArgList &callArgs);

  // FIXME: This probably shouldn't be here, but the code to compute
  // it is here.
  unsigned ComputeBitfieldBitOffset(CodeGen::CodeGenModule &CGM,
                                    const ObjCInterfaceDecl *ID,
                                    const ObjCIvarDecl *Ivar);
};

/// Creates an instance of an Objective-C runtime class.
//TODO: This should include some way of selecting which runtime to target.
CGObjCRuntime *CreateGNUObjCRuntime(CodeGenModule &CGM);
CGObjCRuntime *CreateMacObjCRuntime(CodeGenModule &CGM);
}
}
#endif
