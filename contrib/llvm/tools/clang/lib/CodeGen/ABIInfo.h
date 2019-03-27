//===----- ABIInfo.h - ABI information access & encapsulation ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_ABIINFO_H
#define LLVM_CLANG_LIB_CODEGEN_ABIINFO_H

#include "clang/AST/CharUnits.h"
#include "clang/AST/Type.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Type.h"

namespace llvm {
  class Value;
  class LLVMContext;
  class DataLayout;
  class Type;
}

namespace clang {
  class ASTContext;
  class CodeGenOptions;
  class TargetInfo;

namespace CodeGen {
  class ABIArgInfo;
  class Address;
  class CGCXXABI;
  class CGFunctionInfo;
  class CodeGenFunction;
  class CodeGenTypes;
  class SwiftABIInfo;

namespace swiftcall {
  class SwiftAggLowering;
}

  // FIXME: All of this stuff should be part of the target interface
  // somehow. It is currently here because it is not clear how to factor
  // the targets to support this, since the Targets currently live in a
  // layer below types n'stuff.


  /// ABIInfo - Target specific hooks for defining how a type should be
  /// passed or returned from functions.
  class ABIInfo {
  public:
    CodeGen::CodeGenTypes &CGT;
  protected:
    llvm::CallingConv::ID RuntimeCC;
  public:
    ABIInfo(CodeGen::CodeGenTypes &cgt)
        : CGT(cgt), RuntimeCC(llvm::CallingConv::C) {}

    virtual ~ABIInfo();

    virtual bool supportsSwift() const { return false; }

    CodeGen::CGCXXABI &getCXXABI() const;
    ASTContext &getContext() const;
    llvm::LLVMContext &getVMContext() const;
    const llvm::DataLayout &getDataLayout() const;
    const TargetInfo &getTarget() const;
    const CodeGenOptions &getCodeGenOpts() const;

    /// Return the calling convention to use for system runtime
    /// functions.
    llvm::CallingConv::ID getRuntimeCC() const {
      return RuntimeCC;
    }

    virtual void computeInfo(CodeGen::CGFunctionInfo &FI) const = 0;

    /// EmitVAArg - Emit the target dependent code to load a value of
    /// \arg Ty from the va_list pointed to by \arg VAListAddr.

    // FIXME: This is a gaping layering violation if we wanted to drop
    // the ABI information any lower than CodeGen. Of course, for
    // VAArg handling it has to be at this level; there is no way to
    // abstract this out.
    virtual CodeGen::Address EmitVAArg(CodeGen::CodeGenFunction &CGF,
                                       CodeGen::Address VAListAddr,
                                       QualType Ty) const = 0;

    bool isAndroid() const;

    /// Emit the target dependent code to load a value of
    /// \arg Ty from the \c __builtin_ms_va_list pointed to by \arg VAListAddr.
    virtual CodeGen::Address EmitMSVAArg(CodeGen::CodeGenFunction &CGF,
                                         CodeGen::Address VAListAddr,
                                         QualType Ty) const;

    virtual bool isHomogeneousAggregateBaseType(QualType Ty) const;

    virtual bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                                   uint64_t Members) const;

    bool isHomogeneousAggregate(QualType Ty, const Type *&Base,
                                uint64_t &Members) const;

    /// A convenience method to return an indirect ABIArgInfo with an
    /// expected alignment equal to the ABI alignment of the given type.
    CodeGen::ABIArgInfo
    getNaturalAlignIndirect(QualType Ty, bool ByRef = true,
                            bool Realign = false,
                            llvm::Type *Padding = nullptr) const;

    CodeGen::ABIArgInfo
    getNaturalAlignIndirectInReg(QualType Ty, bool Realign = false) const;


  };

  /// A refining implementation of ABIInfo for targets that support swiftcall.
  ///
  /// If we find ourselves wanting multiple such refinements, they'll probably
  /// be independent refinements, and we should probably find another way
  /// to do it than simple inheritance.
  class SwiftABIInfo : public ABIInfo {
  public:
    SwiftABIInfo(CodeGen::CodeGenTypes &cgt) : ABIInfo(cgt) {}

    bool supportsSwift() const final override { return true; }

    virtual bool shouldPassIndirectlyForSwift(ArrayRef<llvm::Type*> types,
                                              bool asReturnValue) const = 0;

    virtual bool isLegalVectorTypeForSwift(CharUnits totalSize,
                                           llvm::Type *eltTy,
                                           unsigned elts) const;

    virtual bool isSwiftErrorInRegister() const = 0;

    static bool classof(const ABIInfo *info) {
      return info->supportsSwift();
    }
  };
}  // end namespace CodeGen
}  // end namespace clang

#endif
