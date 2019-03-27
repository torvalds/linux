//===- FunctionComparator.h - Function Comparator -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the FunctionComparator and GlobalNumberState classes
// which are used by the MergeFunctions pass for comparing functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/FunctionComparator.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "functioncomparator"

int FunctionComparator::cmpNumbers(uint64_t L, uint64_t R) const {
  if (L < R) return -1;
  if (L > R) return 1;
  return 0;
}

int FunctionComparator::cmpOrderings(AtomicOrdering L, AtomicOrdering R) const {
  if ((int)L < (int)R) return -1;
  if ((int)L > (int)R) return 1;
  return 0;
}

int FunctionComparator::cmpAPInts(const APInt &L, const APInt &R) const {
  if (int Res = cmpNumbers(L.getBitWidth(), R.getBitWidth()))
    return Res;
  if (L.ugt(R)) return 1;
  if (R.ugt(L)) return -1;
  return 0;
}

int FunctionComparator::cmpAPFloats(const APFloat &L, const APFloat &R) const {
  // Floats are ordered first by semantics (i.e. float, double, half, etc.),
  // then by value interpreted as a bitstring (aka APInt).
  const fltSemantics &SL = L.getSemantics(), &SR = R.getSemantics();
  if (int Res = cmpNumbers(APFloat::semanticsPrecision(SL),
                           APFloat::semanticsPrecision(SR)))
    return Res;
  if (int Res = cmpNumbers(APFloat::semanticsMaxExponent(SL),
                           APFloat::semanticsMaxExponent(SR)))
    return Res;
  if (int Res = cmpNumbers(APFloat::semanticsMinExponent(SL),
                           APFloat::semanticsMinExponent(SR)))
    return Res;
  if (int Res = cmpNumbers(APFloat::semanticsSizeInBits(SL),
                           APFloat::semanticsSizeInBits(SR)))
    return Res;
  return cmpAPInts(L.bitcastToAPInt(), R.bitcastToAPInt());
}

int FunctionComparator::cmpMem(StringRef L, StringRef R) const {
  // Prevent heavy comparison, compare sizes first.
  if (int Res = cmpNumbers(L.size(), R.size()))
    return Res;

  // Compare strings lexicographically only when it is necessary: only when
  // strings are equal in size.
  return L.compare(R);
}

int FunctionComparator::cmpAttrs(const AttributeList L,
                                 const AttributeList R) const {
  if (int Res = cmpNumbers(L.getNumAttrSets(), R.getNumAttrSets()))
    return Res;

  for (unsigned i = L.index_begin(), e = L.index_end(); i != e; ++i) {
    AttributeSet LAS = L.getAttributes(i);
    AttributeSet RAS = R.getAttributes(i);
    AttributeSet::iterator LI = LAS.begin(), LE = LAS.end();
    AttributeSet::iterator RI = RAS.begin(), RE = RAS.end();
    for (; LI != LE && RI != RE; ++LI, ++RI) {
      Attribute LA = *LI;
      Attribute RA = *RI;
      if (LA < RA)
        return -1;
      if (RA < LA)
        return 1;
    }
    if (LI != LE)
      return 1;
    if (RI != RE)
      return -1;
  }
  return 0;
}

int FunctionComparator::cmpRangeMetadata(const MDNode *L,
                                         const MDNode *R) const {
  if (L == R)
    return 0;
  if (!L)
    return -1;
  if (!R)
    return 1;
  // Range metadata is a sequence of numbers. Make sure they are the same
  // sequence.
  // TODO: Note that as this is metadata, it is possible to drop and/or merge
  // this data when considering functions to merge. Thus this comparison would
  // return 0 (i.e. equivalent), but merging would become more complicated
  // because the ranges would need to be unioned. It is not likely that
  // functions differ ONLY in this metadata if they are actually the same
  // function semantically.
  if (int Res = cmpNumbers(L->getNumOperands(), R->getNumOperands()))
    return Res;
  for (size_t I = 0; I < L->getNumOperands(); ++I) {
    ConstantInt *LLow = mdconst::extract<ConstantInt>(L->getOperand(I));
    ConstantInt *RLow = mdconst::extract<ConstantInt>(R->getOperand(I));
    if (int Res = cmpAPInts(LLow->getValue(), RLow->getValue()))
      return Res;
  }
  return 0;
}

int FunctionComparator::cmpOperandBundlesSchema(const Instruction *L,
                                                const Instruction *R) const {
  ImmutableCallSite LCS(L);
  ImmutableCallSite RCS(R);

  assert(LCS && RCS && "Must be calls or invokes!");
  assert(LCS.isCall() == RCS.isCall() && "Can't compare otherwise!");

  if (int Res =
          cmpNumbers(LCS.getNumOperandBundles(), RCS.getNumOperandBundles()))
    return Res;

  for (unsigned i = 0, e = LCS.getNumOperandBundles(); i != e; ++i) {
    auto OBL = LCS.getOperandBundleAt(i);
    auto OBR = RCS.getOperandBundleAt(i);

    if (int Res = OBL.getTagName().compare(OBR.getTagName()))
      return Res;

    if (int Res = cmpNumbers(OBL.Inputs.size(), OBR.Inputs.size()))
      return Res;
  }

  return 0;
}

/// Constants comparison:
/// 1. Check whether type of L constant could be losslessly bitcasted to R
/// type.
/// 2. Compare constant contents.
/// For more details see declaration comments.
int FunctionComparator::cmpConstants(const Constant *L,
                                     const Constant *R) const {
  Type *TyL = L->getType();
  Type *TyR = R->getType();

  // Check whether types are bitcastable. This part is just re-factored
  // Type::canLosslesslyBitCastTo method, but instead of returning true/false,
  // we also pack into result which type is "less" for us.
  int TypesRes = cmpTypes(TyL, TyR);
  if (TypesRes != 0) {
    // Types are different, but check whether we can bitcast them.
    if (!TyL->isFirstClassType()) {
      if (TyR->isFirstClassType())
        return -1;
      // Neither TyL nor TyR are values of first class type. Return the result
      // of comparing the types
      return TypesRes;
    }
    if (!TyR->isFirstClassType()) {
      if (TyL->isFirstClassType())
        return 1;
      return TypesRes;
    }

    // Vector -> Vector conversions are always lossless if the two vector types
    // have the same size, otherwise not.
    unsigned TyLWidth = 0;
    unsigned TyRWidth = 0;

    if (auto *VecTyL = dyn_cast<VectorType>(TyL))
      TyLWidth = VecTyL->getBitWidth();
    if (auto *VecTyR = dyn_cast<VectorType>(TyR))
      TyRWidth = VecTyR->getBitWidth();

    if (TyLWidth != TyRWidth)
      return cmpNumbers(TyLWidth, TyRWidth);

    // Zero bit-width means neither TyL nor TyR are vectors.
    if (!TyLWidth) {
      PointerType *PTyL = dyn_cast<PointerType>(TyL);
      PointerType *PTyR = dyn_cast<PointerType>(TyR);
      if (PTyL && PTyR) {
        unsigned AddrSpaceL = PTyL->getAddressSpace();
        unsigned AddrSpaceR = PTyR->getAddressSpace();
        if (int Res = cmpNumbers(AddrSpaceL, AddrSpaceR))
          return Res;
      }
      if (PTyL)
        return 1;
      if (PTyR)
        return -1;

      // TyL and TyR aren't vectors, nor pointers. We don't know how to
      // bitcast them.
      return TypesRes;
    }
  }

  // OK, types are bitcastable, now check constant contents.

  if (L->isNullValue() && R->isNullValue())
    return TypesRes;
  if (L->isNullValue() && !R->isNullValue())
    return 1;
  if (!L->isNullValue() && R->isNullValue())
    return -1;

  auto GlobalValueL = const_cast<GlobalValue *>(dyn_cast<GlobalValue>(L));
  auto GlobalValueR = const_cast<GlobalValue *>(dyn_cast<GlobalValue>(R));
  if (GlobalValueL && GlobalValueR) {
    return cmpGlobalValues(GlobalValueL, GlobalValueR);
  }

  if (int Res = cmpNumbers(L->getValueID(), R->getValueID()))
    return Res;

  if (const auto *SeqL = dyn_cast<ConstantDataSequential>(L)) {
    const auto *SeqR = cast<ConstantDataSequential>(R);
    // This handles ConstantDataArray and ConstantDataVector. Note that we
    // compare the two raw data arrays, which might differ depending on the host
    // endianness. This isn't a problem though, because the endiness of a module
    // will affect the order of the constants, but this order is the same
    // for a given input module and host platform.
    return cmpMem(SeqL->getRawDataValues(), SeqR->getRawDataValues());
  }

  switch (L->getValueID()) {
  case Value::UndefValueVal:
  case Value::ConstantTokenNoneVal:
    return TypesRes;
  case Value::ConstantIntVal: {
    const APInt &LInt = cast<ConstantInt>(L)->getValue();
    const APInt &RInt = cast<ConstantInt>(R)->getValue();
    return cmpAPInts(LInt, RInt);
  }
  case Value::ConstantFPVal: {
    const APFloat &LAPF = cast<ConstantFP>(L)->getValueAPF();
    const APFloat &RAPF = cast<ConstantFP>(R)->getValueAPF();
    return cmpAPFloats(LAPF, RAPF);
  }
  case Value::ConstantArrayVal: {
    const ConstantArray *LA = cast<ConstantArray>(L);
    const ConstantArray *RA = cast<ConstantArray>(R);
    uint64_t NumElementsL = cast<ArrayType>(TyL)->getNumElements();
    uint64_t NumElementsR = cast<ArrayType>(TyR)->getNumElements();
    if (int Res = cmpNumbers(NumElementsL, NumElementsR))
      return Res;
    for (uint64_t i = 0; i < NumElementsL; ++i) {
      if (int Res = cmpConstants(cast<Constant>(LA->getOperand(i)),
                                 cast<Constant>(RA->getOperand(i))))
        return Res;
    }
    return 0;
  }
  case Value::ConstantStructVal: {
    const ConstantStruct *LS = cast<ConstantStruct>(L);
    const ConstantStruct *RS = cast<ConstantStruct>(R);
    unsigned NumElementsL = cast<StructType>(TyL)->getNumElements();
    unsigned NumElementsR = cast<StructType>(TyR)->getNumElements();
    if (int Res = cmpNumbers(NumElementsL, NumElementsR))
      return Res;
    for (unsigned i = 0; i != NumElementsL; ++i) {
      if (int Res = cmpConstants(cast<Constant>(LS->getOperand(i)),
                                 cast<Constant>(RS->getOperand(i))))
        return Res;
    }
    return 0;
  }
  case Value::ConstantVectorVal: {
    const ConstantVector *LV = cast<ConstantVector>(L);
    const ConstantVector *RV = cast<ConstantVector>(R);
    unsigned NumElementsL = cast<VectorType>(TyL)->getNumElements();
    unsigned NumElementsR = cast<VectorType>(TyR)->getNumElements();
    if (int Res = cmpNumbers(NumElementsL, NumElementsR))
      return Res;
    for (uint64_t i = 0; i < NumElementsL; ++i) {
      if (int Res = cmpConstants(cast<Constant>(LV->getOperand(i)),
                                 cast<Constant>(RV->getOperand(i))))
        return Res;
    }
    return 0;
  }
  case Value::ConstantExprVal: {
    const ConstantExpr *LE = cast<ConstantExpr>(L);
    const ConstantExpr *RE = cast<ConstantExpr>(R);
    unsigned NumOperandsL = LE->getNumOperands();
    unsigned NumOperandsR = RE->getNumOperands();
    if (int Res = cmpNumbers(NumOperandsL, NumOperandsR))
      return Res;
    for (unsigned i = 0; i < NumOperandsL; ++i) {
      if (int Res = cmpConstants(cast<Constant>(LE->getOperand(i)),
                                 cast<Constant>(RE->getOperand(i))))
        return Res;
    }
    return 0;
  }
  case Value::BlockAddressVal: {
    const BlockAddress *LBA = cast<BlockAddress>(L);
    const BlockAddress *RBA = cast<BlockAddress>(R);
    if (int Res = cmpValues(LBA->getFunction(), RBA->getFunction()))
      return Res;
    if (LBA->getFunction() == RBA->getFunction()) {
      // They are BBs in the same function. Order by which comes first in the
      // BB order of the function. This order is deterministic.
      Function* F = LBA->getFunction();
      BasicBlock *LBB = LBA->getBasicBlock();
      BasicBlock *RBB = RBA->getBasicBlock();
      if (LBB == RBB)
        return 0;
      for(BasicBlock &BB : F->getBasicBlockList()) {
        if (&BB == LBB) {
          assert(&BB != RBB);
          return -1;
        }
        if (&BB == RBB)
          return 1;
      }
      llvm_unreachable("Basic Block Address does not point to a basic block in "
                       "its function.");
      return -1;
    } else {
      // cmpValues said the functions are the same. So because they aren't
      // literally the same pointer, they must respectively be the left and
      // right functions.
      assert(LBA->getFunction() == FnL && RBA->getFunction() == FnR);
      // cmpValues will tell us if these are equivalent BasicBlocks, in the
      // context of their respective functions.
      return cmpValues(LBA->getBasicBlock(), RBA->getBasicBlock());
    }
  }
  default: // Unknown constant, abort.
    LLVM_DEBUG(dbgs() << "Looking at valueID " << L->getValueID() << "\n");
    llvm_unreachable("Constant ValueID not recognized.");
    return -1;
  }
}

int FunctionComparator::cmpGlobalValues(GlobalValue *L, GlobalValue *R) const {
  uint64_t LNumber = GlobalNumbers->getNumber(L);
  uint64_t RNumber = GlobalNumbers->getNumber(R);
  return cmpNumbers(LNumber, RNumber);
}

/// cmpType - compares two types,
/// defines total ordering among the types set.
/// See method declaration comments for more details.
int FunctionComparator::cmpTypes(Type *TyL, Type *TyR) const {
  PointerType *PTyL = dyn_cast<PointerType>(TyL);
  PointerType *PTyR = dyn_cast<PointerType>(TyR);

  const DataLayout &DL = FnL->getParent()->getDataLayout();
  if (PTyL && PTyL->getAddressSpace() == 0)
    TyL = DL.getIntPtrType(TyL);
  if (PTyR && PTyR->getAddressSpace() == 0)
    TyR = DL.getIntPtrType(TyR);

  if (TyL == TyR)
    return 0;

  if (int Res = cmpNumbers(TyL->getTypeID(), TyR->getTypeID()))
    return Res;

  switch (TyL->getTypeID()) {
  default:
    llvm_unreachable("Unknown type!");
  case Type::IntegerTyID:
    return cmpNumbers(cast<IntegerType>(TyL)->getBitWidth(),
                      cast<IntegerType>(TyR)->getBitWidth());
  // TyL == TyR would have returned true earlier, because types are uniqued.
  case Type::VoidTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
  case Type::LabelTyID:
  case Type::MetadataTyID:
  case Type::TokenTyID:
    return 0;

  case Type::PointerTyID:
    assert(PTyL && PTyR && "Both types must be pointers here.");
    return cmpNumbers(PTyL->getAddressSpace(), PTyR->getAddressSpace());

  case Type::StructTyID: {
    StructType *STyL = cast<StructType>(TyL);
    StructType *STyR = cast<StructType>(TyR);
    if (STyL->getNumElements() != STyR->getNumElements())
      return cmpNumbers(STyL->getNumElements(), STyR->getNumElements());

    if (STyL->isPacked() != STyR->isPacked())
      return cmpNumbers(STyL->isPacked(), STyR->isPacked());

    for (unsigned i = 0, e = STyL->getNumElements(); i != e; ++i) {
      if (int Res = cmpTypes(STyL->getElementType(i), STyR->getElementType(i)))
        return Res;
    }
    return 0;
  }

  case Type::FunctionTyID: {
    FunctionType *FTyL = cast<FunctionType>(TyL);
    FunctionType *FTyR = cast<FunctionType>(TyR);
    if (FTyL->getNumParams() != FTyR->getNumParams())
      return cmpNumbers(FTyL->getNumParams(), FTyR->getNumParams());

    if (FTyL->isVarArg() != FTyR->isVarArg())
      return cmpNumbers(FTyL->isVarArg(), FTyR->isVarArg());

    if (int Res = cmpTypes(FTyL->getReturnType(), FTyR->getReturnType()))
      return Res;

    for (unsigned i = 0, e = FTyL->getNumParams(); i != e; ++i) {
      if (int Res = cmpTypes(FTyL->getParamType(i), FTyR->getParamType(i)))
        return Res;
    }
    return 0;
  }

  case Type::ArrayTyID:
  case Type::VectorTyID: {
    auto *STyL = cast<SequentialType>(TyL);
    auto *STyR = cast<SequentialType>(TyR);
    if (STyL->getNumElements() != STyR->getNumElements())
      return cmpNumbers(STyL->getNumElements(), STyR->getNumElements());
    return cmpTypes(STyL->getElementType(), STyR->getElementType());
  }
  }
}

// Determine whether the two operations are the same except that pointer-to-A
// and pointer-to-B are equivalent. This should be kept in sync with
// Instruction::isSameOperationAs.
// Read method declaration comments for more details.
int FunctionComparator::cmpOperations(const Instruction *L,
                                      const Instruction *R,
                                      bool &needToCmpOperands) const {
  needToCmpOperands = true;
  if (int Res = cmpValues(L, R))
    return Res;

  // Differences from Instruction::isSameOperationAs:
  //  * replace type comparison with calls to cmpTypes.
  //  * we test for I->getRawSubclassOptionalData (nuw/nsw/tail) at the top.
  //  * because of the above, we don't test for the tail bit on calls later on.
  if (int Res = cmpNumbers(L->getOpcode(), R->getOpcode()))
    return Res;

  if (const GetElementPtrInst *GEPL = dyn_cast<GetElementPtrInst>(L)) {
    needToCmpOperands = false;
    const GetElementPtrInst *GEPR = cast<GetElementPtrInst>(R);
    if (int Res =
            cmpValues(GEPL->getPointerOperand(), GEPR->getPointerOperand()))
      return Res;
    return cmpGEPs(GEPL, GEPR);
  }

  if (int Res = cmpNumbers(L->getNumOperands(), R->getNumOperands()))
    return Res;

  if (int Res = cmpTypes(L->getType(), R->getType()))
    return Res;

  if (int Res = cmpNumbers(L->getRawSubclassOptionalData(),
                           R->getRawSubclassOptionalData()))
    return Res;

  // We have two instructions of identical opcode and #operands.  Check to see
  // if all operands are the same type
  for (unsigned i = 0, e = L->getNumOperands(); i != e; ++i) {
    if (int Res =
            cmpTypes(L->getOperand(i)->getType(), R->getOperand(i)->getType()))
      return Res;
  }

  // Check special state that is a part of some instructions.
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(L)) {
    if (int Res = cmpTypes(AI->getAllocatedType(),
                           cast<AllocaInst>(R)->getAllocatedType()))
      return Res;
    return cmpNumbers(AI->getAlignment(), cast<AllocaInst>(R)->getAlignment());
  }
  if (const LoadInst *LI = dyn_cast<LoadInst>(L)) {
    if (int Res = cmpNumbers(LI->isVolatile(), cast<LoadInst>(R)->isVolatile()))
      return Res;
    if (int Res =
            cmpNumbers(LI->getAlignment(), cast<LoadInst>(R)->getAlignment()))
      return Res;
    if (int Res =
            cmpOrderings(LI->getOrdering(), cast<LoadInst>(R)->getOrdering()))
      return Res;
    if (int Res = cmpNumbers(LI->getSyncScopeID(),
                             cast<LoadInst>(R)->getSyncScopeID()))
      return Res;
    return cmpRangeMetadata(LI->getMetadata(LLVMContext::MD_range),
        cast<LoadInst>(R)->getMetadata(LLVMContext::MD_range));
  }
  if (const StoreInst *SI = dyn_cast<StoreInst>(L)) {
    if (int Res =
            cmpNumbers(SI->isVolatile(), cast<StoreInst>(R)->isVolatile()))
      return Res;
    if (int Res =
            cmpNumbers(SI->getAlignment(), cast<StoreInst>(R)->getAlignment()))
      return Res;
    if (int Res =
            cmpOrderings(SI->getOrdering(), cast<StoreInst>(R)->getOrdering()))
      return Res;
    return cmpNumbers(SI->getSyncScopeID(),
                      cast<StoreInst>(R)->getSyncScopeID());
  }
  if (const CmpInst *CI = dyn_cast<CmpInst>(L))
    return cmpNumbers(CI->getPredicate(), cast<CmpInst>(R)->getPredicate());
  if (const CallInst *CI = dyn_cast<CallInst>(L)) {
    if (int Res = cmpNumbers(CI->getCallingConv(),
                             cast<CallInst>(R)->getCallingConv()))
      return Res;
    if (int Res =
            cmpAttrs(CI->getAttributes(), cast<CallInst>(R)->getAttributes()))
      return Res;
    if (int Res = cmpOperandBundlesSchema(CI, R))
      return Res;
    return cmpRangeMetadata(
        CI->getMetadata(LLVMContext::MD_range),
        cast<CallInst>(R)->getMetadata(LLVMContext::MD_range));
  }
  if (const InvokeInst *II = dyn_cast<InvokeInst>(L)) {
    if (int Res = cmpNumbers(II->getCallingConv(),
                             cast<InvokeInst>(R)->getCallingConv()))
      return Res;
    if (int Res =
            cmpAttrs(II->getAttributes(), cast<InvokeInst>(R)->getAttributes()))
      return Res;
    if (int Res = cmpOperandBundlesSchema(II, R))
      return Res;
    return cmpRangeMetadata(
        II->getMetadata(LLVMContext::MD_range),
        cast<InvokeInst>(R)->getMetadata(LLVMContext::MD_range));
  }
  if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(L)) {
    ArrayRef<unsigned> LIndices = IVI->getIndices();
    ArrayRef<unsigned> RIndices = cast<InsertValueInst>(R)->getIndices();
    if (int Res = cmpNumbers(LIndices.size(), RIndices.size()))
      return Res;
    for (size_t i = 0, e = LIndices.size(); i != e; ++i) {
      if (int Res = cmpNumbers(LIndices[i], RIndices[i]))
        return Res;
    }
    return 0;
  }
  if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(L)) {
    ArrayRef<unsigned> LIndices = EVI->getIndices();
    ArrayRef<unsigned> RIndices = cast<ExtractValueInst>(R)->getIndices();
    if (int Res = cmpNumbers(LIndices.size(), RIndices.size()))
      return Res;
    for (size_t i = 0, e = LIndices.size(); i != e; ++i) {
      if (int Res = cmpNumbers(LIndices[i], RIndices[i]))
        return Res;
    }
  }
  if (const FenceInst *FI = dyn_cast<FenceInst>(L)) {
    if (int Res =
            cmpOrderings(FI->getOrdering(), cast<FenceInst>(R)->getOrdering()))
      return Res;
    return cmpNumbers(FI->getSyncScopeID(),
                      cast<FenceInst>(R)->getSyncScopeID());
  }
  if (const AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(L)) {
    if (int Res = cmpNumbers(CXI->isVolatile(),
                             cast<AtomicCmpXchgInst>(R)->isVolatile()))
      return Res;
    if (int Res = cmpNumbers(CXI->isWeak(),
                             cast<AtomicCmpXchgInst>(R)->isWeak()))
      return Res;
    if (int Res =
            cmpOrderings(CXI->getSuccessOrdering(),
                         cast<AtomicCmpXchgInst>(R)->getSuccessOrdering()))
      return Res;
    if (int Res =
            cmpOrderings(CXI->getFailureOrdering(),
                         cast<AtomicCmpXchgInst>(R)->getFailureOrdering()))
      return Res;
    return cmpNumbers(CXI->getSyncScopeID(),
                      cast<AtomicCmpXchgInst>(R)->getSyncScopeID());
  }
  if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(L)) {
    if (int Res = cmpNumbers(RMWI->getOperation(),
                             cast<AtomicRMWInst>(R)->getOperation()))
      return Res;
    if (int Res = cmpNumbers(RMWI->isVolatile(),
                             cast<AtomicRMWInst>(R)->isVolatile()))
      return Res;
    if (int Res = cmpOrderings(RMWI->getOrdering(),
                             cast<AtomicRMWInst>(R)->getOrdering()))
      return Res;
    return cmpNumbers(RMWI->getSyncScopeID(),
                      cast<AtomicRMWInst>(R)->getSyncScopeID());
  }
  if (const PHINode *PNL = dyn_cast<PHINode>(L)) {
    const PHINode *PNR = cast<PHINode>(R);
    // Ensure that in addition to the incoming values being identical
    // (checked by the caller of this function), the incoming blocks
    // are also identical.
    for (unsigned i = 0, e = PNL->getNumIncomingValues(); i != e; ++i) {
      if (int Res =
              cmpValues(PNL->getIncomingBlock(i), PNR->getIncomingBlock(i)))
        return Res;
    }
  }
  return 0;
}

// Determine whether two GEP operations perform the same underlying arithmetic.
// Read method declaration comments for more details.
int FunctionComparator::cmpGEPs(const GEPOperator *GEPL,
                                const GEPOperator *GEPR) const {
  unsigned int ASL = GEPL->getPointerAddressSpace();
  unsigned int ASR = GEPR->getPointerAddressSpace();

  if (int Res = cmpNumbers(ASL, ASR))
    return Res;

  // When we have target data, we can reduce the GEP down to the value in bytes
  // added to the address.
  const DataLayout &DL = FnL->getParent()->getDataLayout();
  unsigned BitWidth = DL.getPointerSizeInBits(ASL);
  APInt OffsetL(BitWidth, 0), OffsetR(BitWidth, 0);
  if (GEPL->accumulateConstantOffset(DL, OffsetL) &&
      GEPR->accumulateConstantOffset(DL, OffsetR))
    return cmpAPInts(OffsetL, OffsetR);
  if (int Res = cmpTypes(GEPL->getSourceElementType(),
                         GEPR->getSourceElementType()))
    return Res;

  if (int Res = cmpNumbers(GEPL->getNumOperands(), GEPR->getNumOperands()))
    return Res;

  for (unsigned i = 0, e = GEPL->getNumOperands(); i != e; ++i) {
    if (int Res = cmpValues(GEPL->getOperand(i), GEPR->getOperand(i)))
      return Res;
  }

  return 0;
}

int FunctionComparator::cmpInlineAsm(const InlineAsm *L,
                                     const InlineAsm *R) const {
  // InlineAsm's are uniqued. If they are the same pointer, obviously they are
  // the same, otherwise compare the fields.
  if (L == R)
    return 0;
  if (int Res = cmpTypes(L->getFunctionType(), R->getFunctionType()))
    return Res;
  if (int Res = cmpMem(L->getAsmString(), R->getAsmString()))
    return Res;
  if (int Res = cmpMem(L->getConstraintString(), R->getConstraintString()))
    return Res;
  if (int Res = cmpNumbers(L->hasSideEffects(), R->hasSideEffects()))
    return Res;
  if (int Res = cmpNumbers(L->isAlignStack(), R->isAlignStack()))
    return Res;
  if (int Res = cmpNumbers(L->getDialect(), R->getDialect()))
    return Res;
  assert(L->getFunctionType() != R->getFunctionType());
  return 0;
}

/// Compare two values used by the two functions under pair-wise comparison. If
/// this is the first time the values are seen, they're added to the mapping so
/// that we will detect mismatches on next use.
/// See comments in declaration for more details.
int FunctionComparator::cmpValues(const Value *L, const Value *R) const {
  // Catch self-reference case.
  if (L == FnL) {
    if (R == FnR)
      return 0;
    return -1;
  }
  if (R == FnR) {
    if (L == FnL)
      return 0;
    return 1;
  }

  const Constant *ConstL = dyn_cast<Constant>(L);
  const Constant *ConstR = dyn_cast<Constant>(R);
  if (ConstL && ConstR) {
    if (L == R)
      return 0;
    return cmpConstants(ConstL, ConstR);
  }

  if (ConstL)
    return 1;
  if (ConstR)
    return -1;

  const InlineAsm *InlineAsmL = dyn_cast<InlineAsm>(L);
  const InlineAsm *InlineAsmR = dyn_cast<InlineAsm>(R);

  if (InlineAsmL && InlineAsmR)
    return cmpInlineAsm(InlineAsmL, InlineAsmR);
  if (InlineAsmL)
    return 1;
  if (InlineAsmR)
    return -1;

  auto LeftSN = sn_mapL.insert(std::make_pair(L, sn_mapL.size())),
       RightSN = sn_mapR.insert(std::make_pair(R, sn_mapR.size()));

  return cmpNumbers(LeftSN.first->second, RightSN.first->second);
}

// Test whether two basic blocks have equivalent behaviour.
int FunctionComparator::cmpBasicBlocks(const BasicBlock *BBL,
                                       const BasicBlock *BBR) const {
  BasicBlock::const_iterator InstL = BBL->begin(), InstLE = BBL->end();
  BasicBlock::const_iterator InstR = BBR->begin(), InstRE = BBR->end();

  do {
    bool needToCmpOperands = true;
    if (int Res = cmpOperations(&*InstL, &*InstR, needToCmpOperands))
      return Res;
    if (needToCmpOperands) {
      assert(InstL->getNumOperands() == InstR->getNumOperands());

      for (unsigned i = 0, e = InstL->getNumOperands(); i != e; ++i) {
        Value *OpL = InstL->getOperand(i);
        Value *OpR = InstR->getOperand(i);
        if (int Res = cmpValues(OpL, OpR))
          return Res;
        // cmpValues should ensure this is true.
        assert(cmpTypes(OpL->getType(), OpR->getType()) == 0);
      }
    }

    ++InstL;
    ++InstR;
  } while (InstL != InstLE && InstR != InstRE);

  if (InstL != InstLE && InstR == InstRE)
    return 1;
  if (InstL == InstLE && InstR != InstRE)
    return -1;
  return 0;
}

int FunctionComparator::compareSignature() const {
  if (int Res = cmpAttrs(FnL->getAttributes(), FnR->getAttributes()))
    return Res;

  if (int Res = cmpNumbers(FnL->hasGC(), FnR->hasGC()))
    return Res;

  if (FnL->hasGC()) {
    if (int Res = cmpMem(FnL->getGC(), FnR->getGC()))
      return Res;
  }

  if (int Res = cmpNumbers(FnL->hasSection(), FnR->hasSection()))
    return Res;

  if (FnL->hasSection()) {
    if (int Res = cmpMem(FnL->getSection(), FnR->getSection()))
      return Res;
  }

  if (int Res = cmpNumbers(FnL->isVarArg(), FnR->isVarArg()))
    return Res;

  // TODO: if it's internal and only used in direct calls, we could handle this
  // case too.
  if (int Res = cmpNumbers(FnL->getCallingConv(), FnR->getCallingConv()))
    return Res;

  if (int Res = cmpTypes(FnL->getFunctionType(), FnR->getFunctionType()))
    return Res;

  assert(FnL->arg_size() == FnR->arg_size() &&
         "Identically typed functions have different numbers of args!");

  // Visit the arguments so that they get enumerated in the order they're
  // passed in.
  for (Function::const_arg_iterator ArgLI = FnL->arg_begin(),
       ArgRI = FnR->arg_begin(),
       ArgLE = FnL->arg_end();
       ArgLI != ArgLE; ++ArgLI, ++ArgRI) {
    if (cmpValues(&*ArgLI, &*ArgRI) != 0)
      llvm_unreachable("Arguments repeat!");
  }
  return 0;
}

// Test whether the two functions have equivalent behaviour.
int FunctionComparator::compare() {
  beginCompare();

  if (int Res = compareSignature())
    return Res;

  // We do a CFG-ordered walk since the actual ordering of the blocks in the
  // linked list is immaterial. Our walk starts at the entry block for both
  // functions, then takes each block from each terminator in order. As an
  // artifact, this also means that unreachable blocks are ignored.
  SmallVector<const BasicBlock *, 8> FnLBBs, FnRBBs;
  SmallPtrSet<const BasicBlock *, 32> VisitedBBs; // in terms of F1.

  FnLBBs.push_back(&FnL->getEntryBlock());
  FnRBBs.push_back(&FnR->getEntryBlock());

  VisitedBBs.insert(FnLBBs[0]);
  while (!FnLBBs.empty()) {
    const BasicBlock *BBL = FnLBBs.pop_back_val();
    const BasicBlock *BBR = FnRBBs.pop_back_val();

    if (int Res = cmpValues(BBL, BBR))
      return Res;

    if (int Res = cmpBasicBlocks(BBL, BBR))
      return Res;

    const Instruction *TermL = BBL->getTerminator();
    const Instruction *TermR = BBR->getTerminator();

    assert(TermL->getNumSuccessors() == TermR->getNumSuccessors());
    for (unsigned i = 0, e = TermL->getNumSuccessors(); i != e; ++i) {
      if (!VisitedBBs.insert(TermL->getSuccessor(i)).second)
        continue;

      FnLBBs.push_back(TermL->getSuccessor(i));
      FnRBBs.push_back(TermR->getSuccessor(i));
    }
  }
  return 0;
}

namespace {

// Accumulate the hash of a sequence of 64-bit integers. This is similar to a
// hash of a sequence of 64bit ints, but the entire input does not need to be
// available at once. This interface is necessary for functionHash because it
// needs to accumulate the hash as the structure of the function is traversed
// without saving these values to an intermediate buffer. This form of hashing
// is not often needed, as usually the object to hash is just read from a
// buffer.
class HashAccumulator64 {
  uint64_t Hash;

public:
  // Initialize to random constant, so the state isn't zero.
  HashAccumulator64() { Hash = 0x6acaa36bef8325c5ULL; }

  void add(uint64_t V) {
     Hash = hashing::detail::hash_16_bytes(Hash, V);
  }

  // No finishing is required, because the entire hash value is used.
  uint64_t getHash() { return Hash; }
};

} // end anonymous namespace

// A function hash is calculated by considering only the number of arguments and
// whether a function is varargs, the order of basic blocks (given by the
// successors of each basic block in depth first order), and the order of
// opcodes of each instruction within each of these basic blocks. This mirrors
// the strategy compare() uses to compare functions by walking the BBs in depth
// first order and comparing each instruction in sequence. Because this hash
// does not look at the operands, it is insensitive to things such as the
// target of calls and the constants used in the function, which makes it useful
// when possibly merging functions which are the same modulo constants and call
// targets.
FunctionComparator::FunctionHash FunctionComparator::functionHash(Function &F) {
  HashAccumulator64 H;
  H.add(F.isVarArg());
  H.add(F.arg_size());

  SmallVector<const BasicBlock *, 8> BBs;
  SmallPtrSet<const BasicBlock *, 16> VisitedBBs;

  // Walk the blocks in the same order as FunctionComparator::cmpBasicBlocks(),
  // accumulating the hash of the function "structure." (BB and opcode sequence)
  BBs.push_back(&F.getEntryBlock());
  VisitedBBs.insert(BBs[0]);
  while (!BBs.empty()) {
    const BasicBlock *BB = BBs.pop_back_val();
    // This random value acts as a block header, as otherwise the partition of
    // opcodes into BBs wouldn't affect the hash, only the order of the opcodes
    H.add(45798);
    for (auto &Inst : *BB) {
      H.add(Inst.getOpcode());
    }
    const Instruction *Term = BB->getTerminator();
    for (unsigned i = 0, e = Term->getNumSuccessors(); i != e; ++i) {
      if (!VisitedBBs.insert(Term->getSuccessor(i)).second)
        continue;
      BBs.push_back(Term->getSuccessor(i));
    }
  }
  return H.getHash();
}
