//===- ConstantFolder.h - Constant folding helper ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ConstantFolder class, a helper for IRBuilder.
// It provides IRBuilder with a set of methods for creating constants
// with minimal folding.  For general constant creation and folding,
// use ConstantExpr and the routines in llvm/Analysis/ConstantFolding.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTFOLDER_H
#define LLVM_IR_CONSTANTFOLDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

/// ConstantFolder - Create constants with minimum, target independent, folding.
class ConstantFolder {
public:
  explicit ConstantFolder() = default;

  //===--------------------------------------------------------------------===//
  // Binary Operators
  //===--------------------------------------------------------------------===//

  Constant *CreateAdd(Constant *LHS, Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const {
    return ConstantExpr::getAdd(LHS, RHS, HasNUW, HasNSW);
  }

  Constant *CreateFAdd(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getFAdd(LHS, RHS);
  }

  Constant *CreateSub(Constant *LHS, Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const {
    return ConstantExpr::getSub(LHS, RHS, HasNUW, HasNSW);
  }

  Constant *CreateFSub(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getFSub(LHS, RHS);
  }

  Constant *CreateMul(Constant *LHS, Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const {
    return ConstantExpr::getMul(LHS, RHS, HasNUW, HasNSW);
  }

  Constant *CreateFMul(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getFMul(LHS, RHS);
  }

  Constant *CreateUDiv(Constant *LHS, Constant *RHS,
                       bool isExact = false) const {
    return ConstantExpr::getUDiv(LHS, RHS, isExact);
  }

  Constant *CreateSDiv(Constant *LHS, Constant *RHS,
                       bool isExact = false) const {
    return ConstantExpr::getSDiv(LHS, RHS, isExact);
  }

  Constant *CreateFDiv(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getFDiv(LHS, RHS);
  }

  Constant *CreateURem(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getURem(LHS, RHS);
  }

  Constant *CreateSRem(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getSRem(LHS, RHS);
  }

  Constant *CreateFRem(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getFRem(LHS, RHS);
  }

  Constant *CreateShl(Constant *LHS, Constant *RHS,
                      bool HasNUW = false, bool HasNSW = false) const {
    return ConstantExpr::getShl(LHS, RHS, HasNUW, HasNSW);
  }

  Constant *CreateLShr(Constant *LHS, Constant *RHS,
                       bool isExact = false) const {
    return ConstantExpr::getLShr(LHS, RHS, isExact);
  }

  Constant *CreateAShr(Constant *LHS, Constant *RHS,
                       bool isExact = false) const {
    return ConstantExpr::getAShr(LHS, RHS, isExact);
  }

  Constant *CreateAnd(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getAnd(LHS, RHS);
  }

  Constant *CreateOr(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getOr(LHS, RHS);
  }

  Constant *CreateXor(Constant *LHS, Constant *RHS) const {
    return ConstantExpr::getXor(LHS, RHS);
  }

  Constant *CreateBinOp(Instruction::BinaryOps Opc,
                        Constant *LHS, Constant *RHS) const {
    return ConstantExpr::get(Opc, LHS, RHS);
  }

  //===--------------------------------------------------------------------===//
  // Unary Operators
  //===--------------------------------------------------------------------===//

  Constant *CreateNeg(Constant *C,
                      bool HasNUW = false, bool HasNSW = false) const {
    return ConstantExpr::getNeg(C, HasNUW, HasNSW);
  }

  Constant *CreateFNeg(Constant *C) const {
    return ConstantExpr::getFNeg(C);
  }

  Constant *CreateNot(Constant *C) const {
    return ConstantExpr::getNot(C);
  }

  //===--------------------------------------------------------------------===//
  // Memory Instructions
  //===--------------------------------------------------------------------===//

  Constant *CreateGetElementPtr(Type *Ty, Constant *C,
                                ArrayRef<Constant *> IdxList) const {
    return ConstantExpr::getGetElementPtr(Ty, C, IdxList);
  }

  Constant *CreateGetElementPtr(Type *Ty, Constant *C, Constant *Idx) const {
    // This form of the function only exists to avoid ambiguous overload
    // warnings about whether to convert Idx to ArrayRef<Constant *> or
    // ArrayRef<Value *>.
    return ConstantExpr::getGetElementPtr(Ty, C, Idx);
  }

  Constant *CreateGetElementPtr(Type *Ty, Constant *C,
                                ArrayRef<Value *> IdxList) const {
    return ConstantExpr::getGetElementPtr(Ty, C, IdxList);
  }

  Constant *CreateInBoundsGetElementPtr(Type *Ty, Constant *C,
                                        ArrayRef<Constant *> IdxList) const {
    return ConstantExpr::getInBoundsGetElementPtr(Ty, C, IdxList);
  }

  Constant *CreateInBoundsGetElementPtr(Type *Ty, Constant *C,
                                        Constant *Idx) const {
    // This form of the function only exists to avoid ambiguous overload
    // warnings about whether to convert Idx to ArrayRef<Constant *> or
    // ArrayRef<Value *>.
    return ConstantExpr::getInBoundsGetElementPtr(Ty, C, Idx);
  }

  Constant *CreateInBoundsGetElementPtr(Type *Ty, Constant *C,
                                        ArrayRef<Value *> IdxList) const {
    return ConstantExpr::getInBoundsGetElementPtr(Ty, C, IdxList);
  }

  //===--------------------------------------------------------------------===//
  // Cast/Conversion Operators
  //===--------------------------------------------------------------------===//

  Constant *CreateCast(Instruction::CastOps Op, Constant *C,
                       Type *DestTy) const {
    return ConstantExpr::getCast(Op, C, DestTy);
  }

  Constant *CreatePointerCast(Constant *C, Type *DestTy) const {
    return ConstantExpr::getPointerCast(C, DestTy);
  }

  Constant *CreatePointerBitCastOrAddrSpaceCast(Constant *C,
                                                Type *DestTy) const {
    return ConstantExpr::getPointerBitCastOrAddrSpaceCast(C, DestTy);
  }

  Constant *CreateIntCast(Constant *C, Type *DestTy,
                          bool isSigned) const {
    return ConstantExpr::getIntegerCast(C, DestTy, isSigned);
  }

  Constant *CreateFPCast(Constant *C, Type *DestTy) const {
    return ConstantExpr::getFPCast(C, DestTy);
  }

  Constant *CreateBitCast(Constant *C, Type *DestTy) const {
    return CreateCast(Instruction::BitCast, C, DestTy);
  }

  Constant *CreateIntToPtr(Constant *C, Type *DestTy) const {
    return CreateCast(Instruction::IntToPtr, C, DestTy);
  }

  Constant *CreatePtrToInt(Constant *C, Type *DestTy) const {
    return CreateCast(Instruction::PtrToInt, C, DestTy);
  }

  Constant *CreateZExtOrBitCast(Constant *C, Type *DestTy) const {
    return ConstantExpr::getZExtOrBitCast(C, DestTy);
  }

  Constant *CreateSExtOrBitCast(Constant *C, Type *DestTy) const {
    return ConstantExpr::getSExtOrBitCast(C, DestTy);
  }

  Constant *CreateTruncOrBitCast(Constant *C, Type *DestTy) const {
    return ConstantExpr::getTruncOrBitCast(C, DestTy);
  }

  //===--------------------------------------------------------------------===//
  // Compare Instructions
  //===--------------------------------------------------------------------===//

  Constant *CreateICmp(CmpInst::Predicate P, Constant *LHS,
                       Constant *RHS) const {
    return ConstantExpr::getCompare(P, LHS, RHS);
  }

  Constant *CreateFCmp(CmpInst::Predicate P, Constant *LHS,
                       Constant *RHS) const {
    return ConstantExpr::getCompare(P, LHS, RHS);
  }

  //===--------------------------------------------------------------------===//
  // Other Instructions
  //===--------------------------------------------------------------------===//

  Constant *CreateSelect(Constant *C, Constant *True, Constant *False) const {
    return ConstantExpr::getSelect(C, True, False);
  }

  Constant *CreateExtractElement(Constant *Vec, Constant *Idx) const {
    return ConstantExpr::getExtractElement(Vec, Idx);
  }

  Constant *CreateInsertElement(Constant *Vec, Constant *NewElt,
                                Constant *Idx) const {
    return ConstantExpr::getInsertElement(Vec, NewElt, Idx);
  }

  Constant *CreateShuffleVector(Constant *V1, Constant *V2,
                                Constant *Mask) const {
    return ConstantExpr::getShuffleVector(V1, V2, Mask);
  }

  Constant *CreateExtractValue(Constant *Agg,
                               ArrayRef<unsigned> IdxList) const {
    return ConstantExpr::getExtractValue(Agg, IdxList);
  }

  Constant *CreateInsertValue(Constant *Agg, Constant *Val,
                              ArrayRef<unsigned> IdxList) const {
    return ConstantExpr::getInsertValue(Agg, Val, IdxList);
  }
};

} // end namespace llvm

#endif // LLVM_IR_CONSTANTFOLDER_H
