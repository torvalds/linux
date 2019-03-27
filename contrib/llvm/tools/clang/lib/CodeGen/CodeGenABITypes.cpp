//==--- CodeGenABITypes.cpp - Convert Clang types to LLVM types for ABI ----==//
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
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/CodeGenABITypes.h"
#include "CGRecordLayout.h"
#include "CodeGenModule.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"

using namespace clang;
using namespace CodeGen;

const CGFunctionInfo &
CodeGen::arrangeObjCMessageSendSignature(CodeGenModule &CGM,
                                         const ObjCMethodDecl *MD,
                                         QualType receiverType) {
  return CGM.getTypes().arrangeObjCMessageSendSignature(MD, receiverType);
}

const CGFunctionInfo &
CodeGen::arrangeFreeFunctionType(CodeGenModule &CGM,
                                 CanQual<FunctionProtoType> Ty,
                                 const FunctionDecl *FD) {
  return CGM.getTypes().arrangeFreeFunctionType(Ty, FD);
}

const CGFunctionInfo &
CodeGen::arrangeFreeFunctionType(CodeGenModule &CGM,
                                 CanQual<FunctionNoProtoType> Ty) {
  return CGM.getTypes().arrangeFreeFunctionType(Ty);
}

const CGFunctionInfo &
CodeGen::arrangeCXXMethodType(CodeGenModule &CGM,
                              const CXXRecordDecl *RD,
                              const FunctionProtoType *FTP,
                              const CXXMethodDecl *MD) {
  return CGM.getTypes().arrangeCXXMethodType(RD, FTP, MD);
}

const CGFunctionInfo &
CodeGen::arrangeFreeFunctionCall(CodeGenModule &CGM,
                                 CanQualType returnType,
                                 ArrayRef<CanQualType> argTypes,
                                 FunctionType::ExtInfo info,
                                 RequiredArgs args) {
  return CGM.getTypes().arrangeLLVMFunctionInfo(
      returnType, /*IsInstanceMethod=*/false, /*IsChainCall=*/false, argTypes,
      info, {}, args);
}

llvm::FunctionType *
CodeGen::convertFreeFunctionType(CodeGenModule &CGM, const FunctionDecl *FD) {
  assert(FD != nullptr && "Expected a non-null function declaration!");
  llvm::Type *T = CGM.getTypes().ConvertFunctionType(FD->getType(), FD);

  if (auto FT = dyn_cast<llvm::FunctionType>(T))
    return FT;

  return nullptr;
}

llvm::Type *
CodeGen::convertTypeForMemory(CodeGenModule &CGM, QualType T) {
  return CGM.getTypes().ConvertTypeForMem(T);
}

unsigned CodeGen::getLLVMFieldNumber(CodeGenModule &CGM,
                                     const RecordDecl *RD,
                                     const FieldDecl *FD) {
  return CGM.getTypes().getCGRecordLayout(RD).getLLVMFieldNo(FD);
}
