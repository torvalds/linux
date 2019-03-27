//==---- CodeGenABITypes.h - Convert Clang types to LLVM types for ABI -----==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// CodeGenABITypes is a simple interface for getting LLVM types for
// the parameters and the return value of a function given the Clang
// types.
//
// The class is implemented as a public wrapper around the private
// CodeGenTypes class in lib/CodeGen.
//
// It allows other clients, like LLDB, to determine the LLVM types that are
// actually used in function calls, which makes it possible to then determine
// the actual ABI locations (e.g. registers, stack locations, etc.) that
// these parameters are stored in.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_CODEGENABITYPES_H
#define LLVM_CLANG_CODEGEN_CODEGENABITYPES_H

#include "clang/AST/CanonicalType.h"
#include "clang/AST/Type.h"
#include "clang/CodeGen/CGFunctionInfo.h"

namespace llvm {
  class DataLayout;
  class Module;
  class FunctionType;
  class Type;
}

namespace clang {
class ASTContext;
class CXXRecordDecl;
class CXXMethodDecl;
class CodeGenOptions;
class CoverageSourceInfo;
class DiagnosticsEngine;
class HeaderSearchOptions;
class ObjCMethodDecl;
class PreprocessorOptions;

namespace CodeGen {
class CGFunctionInfo;
class CodeGenModule;

const CGFunctionInfo &arrangeObjCMessageSendSignature(CodeGenModule &CGM,
                                                      const ObjCMethodDecl *MD,
                                                      QualType receiverType);

const CGFunctionInfo &arrangeFreeFunctionType(CodeGenModule &CGM,
                                              CanQual<FunctionProtoType> Ty,
                                              const FunctionDecl *FD);

const CGFunctionInfo &arrangeFreeFunctionType(CodeGenModule &CGM,
                                              CanQual<FunctionNoProtoType> Ty);

const CGFunctionInfo &arrangeCXXMethodType(CodeGenModule &CGM,
                                           const CXXRecordDecl *RD,
                                           const FunctionProtoType *FTP,
                                           const CXXMethodDecl *MD);

const CGFunctionInfo &arrangeFreeFunctionCall(CodeGenModule &CGM,
                                              CanQualType returnType,
                                              ArrayRef<CanQualType> argTypes,
                                              FunctionType::ExtInfo info,
                                              RequiredArgs args);

/// Returns null if the function type is incomplete and can't be lowered.
llvm::FunctionType *convertFreeFunctionType(CodeGenModule &CGM,
                                            const FunctionDecl *FD);

llvm::Type *convertTypeForMemory(CodeGenModule &CGM, QualType T);

/// Given a non-bitfield struct field, return its index within the elements of
/// the struct's converted type.  The returned index refers to a field number in
/// the complete object type which is returned by convertTypeForMemory.  FD must
/// be a field in RD directly (i.e. not an inherited field).
unsigned getLLVMFieldNumber(CodeGenModule &CGM,
                            const RecordDecl *RD, const FieldDecl *FD);

}  // end namespace CodeGen
}  // end namespace clang

#endif
