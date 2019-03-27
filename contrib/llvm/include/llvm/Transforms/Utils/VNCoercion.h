//===- VNCoercion.h - Value Numbering Coercion Utilities --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file / This file provides routines used by LLVM's value numbering passes to
/// perform various forms of value extraction from memory when the types are not
/// identical.  For example, given
///
/// store i32 8, i32 *%foo
/// %a = bitcast i32 *%foo to i16
/// %val = load i16, i16 *%a
///
/// It possible to extract the value of the load of %a from the store to %foo.
/// These routines know how to tell whether they can do that (the analyze*
/// routines), and can also insert the necessary IR to do it (the get*
/// routines).

#ifndef LLVM_TRANSFORMS_UTILS_VNCOERCION_H
#define LLVM_TRANSFORMS_UTILS_VNCOERCION_H
#include "llvm/IR/IRBuilder.h"

namespace llvm {
class Function;
class StoreInst;
class LoadInst;
class MemIntrinsic;
class Instruction;
class Value;
class Type;
class DataLayout;
namespace VNCoercion {
/// Return true if CoerceAvailableValueToLoadType would succeed if it was
/// called.
bool canCoerceMustAliasedValueToLoad(Value *StoredVal, Type *LoadTy,
                                     const DataLayout &DL);

/// If we saw a store of a value to memory, and then a load from a must-aliased
/// pointer of a different type, try to coerce the stored value to the loaded
/// type.  LoadedTy is the type of the load we want to replace.  IRB is
/// IRBuilder used to insert new instructions.
///
/// If we can't do it, return null.
Value *coerceAvailableValueToLoadType(Value *StoredVal, Type *LoadedTy,
                                      IRBuilder<> &IRB, const DataLayout &DL);

/// This function determines whether a value for the pointer LoadPtr can be
/// extracted from the store at DepSI.
///
/// On success, it returns the offset into DepSI that extraction would start.
/// On failure, it returns -1.
int analyzeLoadFromClobberingStore(Type *LoadTy, Value *LoadPtr,
                                   StoreInst *DepSI, const DataLayout &DL);

/// This function determines whether a value for the pointer LoadPtr can be
/// extracted from the load at DepLI.
///
/// On success, it returns the offset into DepLI that extraction would start.
/// On failure, it returns -1.
int analyzeLoadFromClobberingLoad(Type *LoadTy, Value *LoadPtr, LoadInst *DepLI,
                                  const DataLayout &DL);

/// This function determines whether a value for the pointer LoadPtr can be
/// extracted from the memory intrinsic at DepMI.
///
/// On success, it returns the offset into DepMI that extraction would start.
/// On failure, it returns -1.
int analyzeLoadFromClobberingMemInst(Type *LoadTy, Value *LoadPtr,
                                     MemIntrinsic *DepMI, const DataLayout &DL);

/// If analyzeLoadFromClobberingStore returned an offset, this function can be
/// used to actually perform the extraction of the bits from the store. It
/// inserts instructions to do so at InsertPt, and returns the extracted value.
Value *getStoreValueForLoad(Value *SrcVal, unsigned Offset, Type *LoadTy,
                            Instruction *InsertPt, const DataLayout &DL);
// This is the same as getStoreValueForLoad, except it performs no insertion
// It only allows constant inputs.
Constant *getConstantStoreValueForLoad(Constant *SrcVal, unsigned Offset,
                                       Type *LoadTy, const DataLayout &DL);

/// If analyzeLoadFromClobberingLoad returned an offset, this function can be
/// used to actually perform the extraction of the bits from the load, including
/// any necessary load widening.  It inserts instructions to do so at InsertPt,
/// and returns the extracted value.
Value *getLoadValueForLoad(LoadInst *SrcVal, unsigned Offset, Type *LoadTy,
                           Instruction *InsertPt, const DataLayout &DL);
// This is the same as getLoadValueForLoad, except it is given the load value as
// a constant. It returns nullptr if it would require widening the load.
Constant *getConstantLoadValueForLoad(Constant *SrcVal, unsigned Offset,
                                      Type *LoadTy, const DataLayout &DL);

/// If analyzeLoadFromClobberingMemInst returned an offset, this function can be
/// used to actually perform the extraction of the bits from the memory
/// intrinsic.  It inserts instructions to do so at InsertPt, and returns the
/// extracted value.
Value *getMemInstValueForLoad(MemIntrinsic *SrcInst, unsigned Offset,
                              Type *LoadTy, Instruction *InsertPt,
                              const DataLayout &DL);
// This is the same as getStoreValueForLoad, except it performs no insertion.
// It returns nullptr if it cannot produce a constant.
Constant *getConstantMemInstValueForLoad(MemIntrinsic *SrcInst, unsigned Offset,
                                         Type *LoadTy, const DataLayout &DL);
}
}
#endif
