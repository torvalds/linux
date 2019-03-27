//===---- TargetInfo.h - Encapsulate target details -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function
// definition used to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_TARGETINFO_H
#define LLVM_CLANG_LIB_CODEGEN_TARGETINFO_H

#include "CodeGenModule.h"
#include "CGValue.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SyncScope.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class Constant;
class GlobalValue;
class Type;
class Value;
}

namespace clang {
class Decl;

namespace CodeGen {
class ABIInfo;
class CallArgList;
class CodeGenFunction;
class CGBlockInfo;
class CGFunctionInfo;

/// TargetCodeGenInfo - This class organizes various target-specific
/// codegeneration issues, like target-specific attributes, builtins and so
/// on.
class TargetCodeGenInfo {
  ABIInfo *Info;

public:
  // WARNING: Acquires the ownership of ABIInfo.
  TargetCodeGenInfo(ABIInfo *info = nullptr) : Info(info) {}
  virtual ~TargetCodeGenInfo();

  /// getABIInfo() - Returns ABI info helper for the target.
  const ABIInfo &getABIInfo() const { return *Info; }

  /// setTargetAttributes - Provides a convenient hook to handle extra
  /// target-specific attributes for the given global.
  virtual void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                                   CodeGen::CodeGenModule &M) const {}

  /// emitTargetMD - Provides a convenient hook to handle extra
  /// target-specific metadata for the given global.
  virtual void emitTargetMD(const Decl *D, llvm::GlobalValue *GV,
                            CodeGen::CodeGenModule &M) const {}

  /// Determines the size of struct _Unwind_Exception on this platform,
  /// in 8-bit units.  The Itanium ABI defines this as:
  ///   struct _Unwind_Exception {
  ///     uint64 exception_class;
  ///     _Unwind_Exception_Cleanup_Fn exception_cleanup;
  ///     uint64 private_1;
  ///     uint64 private_2;
  ///   };
  virtual unsigned getSizeOfUnwindException() const;

  /// Controls whether __builtin_extend_pointer should sign-extend
  /// pointers to uint64_t or zero-extend them (the default).  Has
  /// no effect for targets:
  ///   - that have 64-bit pointers, or
  ///   - that cannot address through registers larger than pointers, or
  ///   - that implicitly ignore/truncate the top bits when addressing
  ///     through such registers.
  virtual bool extendPointerWithSExt() const { return false; }

  /// Determines the DWARF register number for the stack pointer, for
  /// exception-handling purposes.  Implements __builtin_dwarf_sp_column.
  ///
  /// Returns -1 if the operation is unsupported by this target.
  virtual int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const {
    return -1;
  }

  /// Initializes the given DWARF EH register-size table, a char*.
  /// Implements __builtin_init_dwarf_reg_size_table.
  ///
  /// Returns true if the operation is unsupported by this target.
  virtual bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                       llvm::Value *Address) const {
    return true;
  }

  /// Performs the code-generation required to convert a return
  /// address as stored by the system into the actual address of the
  /// next instruction that will be executed.
  ///
  /// Used by __builtin_extract_return_addr().
  virtual llvm::Value *decodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                           llvm::Value *Address) const {
    return Address;
  }

  /// Performs the code-generation required to convert the address
  /// of an instruction into a return address suitable for storage
  /// by the system in a return slot.
  ///
  /// Used by __builtin_frob_return_addr().
  virtual llvm::Value *encodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                           llvm::Value *Address) const {
    return Address;
  }

  /// Corrects the low-level LLVM type for a given constraint and "usual"
  /// type.
  ///
  /// \returns A pointer to a new LLVM type, possibly the same as the original
  /// on success; 0 on failure.
  virtual llvm::Type *adjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                          StringRef Constraint,
                                          llvm::Type *Ty) const {
    return Ty;
  }

  /// Adds constraints and types for result registers.
  virtual void addReturnRegisterOutputs(
      CodeGen::CodeGenFunction &CGF, CodeGen::LValue ReturnValue,
      std::string &Constraints, std::vector<llvm::Type *> &ResultRegTypes,
      std::vector<llvm::Type *> &ResultTruncRegTypes,
      std::vector<CodeGen::LValue> &ResultRegDests, std::string &AsmString,
      unsigned NumOutputs) const {}

  /// doesReturnSlotInterfereWithArgs - Return true if the target uses an
  /// argument slot for an 'sret' type.
  virtual bool doesReturnSlotInterfereWithArgs() const { return true; }

  /// Retrieve the address of a function to call immediately before
  /// calling objc_retainAutoreleasedReturnValue.  The
  /// implementation of objc_autoreleaseReturnValue sniffs the
  /// instruction stream following its return address to decide
  /// whether it's a call to objc_retainAutoreleasedReturnValue.
  /// This can be prohibitively expensive, depending on the
  /// relocation model, and so on some targets it instead sniffs for
  /// a particular instruction sequence.  This functions returns
  /// that instruction sequence in inline assembly, which will be
  /// empty if none is required.
  virtual StringRef getARCRetainAutoreleasedReturnValueMarker() const {
    return "";
  }

  /// Return a constant used by UBSan as a signature to identify functions
  /// possessing type information, or 0 if the platform is unsupported.
  virtual llvm::Constant *
  getUBSanFunctionSignature(CodeGen::CodeGenModule &CGM) const {
    return nullptr;
  }

  /// Determine whether a call to an unprototyped functions under
  /// the given calling convention should use the variadic
  /// convention or the non-variadic convention.
  ///
  /// There's a good reason to make a platform's variadic calling
  /// convention be different from its non-variadic calling
  /// convention: the non-variadic arguments can be passed in
  /// registers (better for performance), and the variadic arguments
  /// can be passed on the stack (also better for performance).  If
  /// this is done, however, unprototyped functions *must* use the
  /// non-variadic convention, because C99 states that a call
  /// through an unprototyped function type must succeed if the
  /// function was defined with a non-variadic prototype with
  /// compatible parameters.  Therefore, splitting the conventions
  /// makes it impossible to call a variadic function through an
  /// unprototyped type.  Since function prototypes came out in the
  /// late 1970s, this is probably an acceptable trade-off.
  /// Nonetheless, not all platforms are willing to make it, and in
  /// particularly x86-64 bends over backwards to make the
  /// conventions compatible.
  ///
  /// The default is false.  This is correct whenever:
  ///   - the conventions are exactly the same, because it does not
  ///     matter and the resulting IR will be somewhat prettier in
  ///     certain cases; or
  ///   - the conventions are substantively different in how they pass
  ///     arguments, because in this case using the variadic convention
  ///     will lead to C99 violations.
  ///
  /// However, some platforms make the conventions identical except
  /// for passing additional out-of-band information to a variadic
  /// function: for example, x86-64 passes the number of SSE
  /// arguments in %al.  On these platforms, it is desirable to
  /// call unprototyped functions using the variadic convention so
  /// that unprototyped calls to varargs functions still succeed.
  ///
  /// Relatedly, platforms which pass the fixed arguments to this:
  ///   A foo(B, C, D);
  /// differently than they would pass them to this:
  ///   A foo(B, C, D, ...);
  /// may need to adjust the debugger-support code in Sema to do the
  /// right thing when calling a function with no know signature.
  virtual bool isNoProtoCallVariadic(const CodeGen::CallArgList &args,
                                     const FunctionNoProtoType *fnType) const;

  /// Gets the linker options necessary to link a dependent library on this
  /// platform.
  virtual void getDependentLibraryOption(llvm::StringRef Lib,
                                         llvm::SmallString<24> &Opt) const;

  /// Gets the linker options necessary to detect object file mismatches on
  /// this platform.
  virtual void getDetectMismatchOption(llvm::StringRef Name,
                                       llvm::StringRef Value,
                                       llvm::SmallString<32> &Opt) const {}

  /// Get LLVM calling convention for OpenCL kernel.
  virtual unsigned getOpenCLKernelCallingConv() const;

  /// Get target specific null pointer.
  /// \param T is the LLVM type of the null pointer.
  /// \param QT is the clang QualType of the null pointer.
  /// \return ConstantPointerNull with the given type \p T.
  /// Each target can override it to return its own desired constant value.
  virtual llvm::Constant *getNullPointer(const CodeGen::CodeGenModule &CGM,
      llvm::PointerType *T, QualType QT) const;

  /// Get target favored AST address space of a global variable for languages
  /// other than OpenCL and CUDA.
  /// If \p D is nullptr, returns the default target favored address space
  /// for global variable.
  virtual LangAS getGlobalVarAddressSpace(CodeGenModule &CGM,
                                          const VarDecl *D) const;

  /// Get the AST address space for alloca.
  virtual LangAS getASTAllocaAddressSpace() const { return LangAS::Default; }

  /// Perform address space cast of an expression of pointer type.
  /// \param V is the LLVM value to be casted to another address space.
  /// \param SrcAddr is the language address space of \p V.
  /// \param DestAddr is the targeted language address space.
  /// \param DestTy is the destination LLVM pointer type.
  /// \param IsNonNull is the flag indicating \p V is known to be non null.
  virtual llvm::Value *performAddrSpaceCast(CodeGen::CodeGenFunction &CGF,
                                            llvm::Value *V, LangAS SrcAddr,
                                            LangAS DestAddr, llvm::Type *DestTy,
                                            bool IsNonNull = false) const;

  /// Perform address space cast of a constant expression of pointer type.
  /// \param V is the LLVM constant to be casted to another address space.
  /// \param SrcAddr is the language address space of \p V.
  /// \param DestAddr is the targeted language address space.
  /// \param DestTy is the destination LLVM pointer type.
  virtual llvm::Constant *performAddrSpaceCast(CodeGenModule &CGM,
                                               llvm::Constant *V,
                                               LangAS SrcAddr, LangAS DestAddr,
                                               llvm::Type *DestTy) const;

  /// Get the syncscope used in LLVM IR.
  virtual llvm::SyncScope::ID getLLVMSyncScopeID(SyncScope S,
                                                 llvm::LLVMContext &C) const;

  /// Interface class for filling custom fields of a block literal for OpenCL.
  class TargetOpenCLBlockHelper {
  public:
    typedef std::pair<llvm::Value *, StringRef> ValueTy;
    TargetOpenCLBlockHelper() {}
    virtual ~TargetOpenCLBlockHelper() {}
    /// Get the custom field types for OpenCL blocks.
    virtual llvm::SmallVector<llvm::Type *, 1> getCustomFieldTypes() = 0;
    /// Get the custom field values for OpenCL blocks.
    virtual llvm::SmallVector<ValueTy, 1>
    getCustomFieldValues(CodeGenFunction &CGF, const CGBlockInfo &Info) = 0;
    virtual bool areAllCustomFieldValuesConstant(const CGBlockInfo &Info) = 0;
    /// Get the custom field values for OpenCL blocks if all values are LLVM
    /// constants.
    virtual llvm::SmallVector<llvm::Constant *, 1>
    getCustomFieldValues(CodeGenModule &CGM, const CGBlockInfo &Info) = 0;
  };
  virtual TargetOpenCLBlockHelper *getTargetOpenCLBlockHelper() const {
    return nullptr;
  }

  /// Create an OpenCL kernel for an enqueued block. The kernel function is
  /// a wrapper for the block invoke function with target-specific calling
  /// convention and ABI as an OpenCL kernel. The wrapper function accepts
  /// block context and block arguments in target-specific way and calls
  /// the original block invoke function.
  virtual llvm::Function *
  createEnqueuedBlockKernel(CodeGenFunction &CGF,
                            llvm::Function *BlockInvokeFunc,
                            llvm::Value *BlockLiteral) const;

  /// \return true if the target supports alias from the unmangled name to the
  /// mangled name of functions declared within an extern "C" region and marked
  /// as 'used', and having internal linkage.
  virtual bool shouldEmitStaticExternCAliases() const { return true; }

  virtual void setCUDAKernelCallingConvention(const FunctionType *&FT) const {}
};

} // namespace CodeGen
} // namespace clang

#endif // LLVM_CLANG_LIB_CODEGEN_TARGETINFO_H
