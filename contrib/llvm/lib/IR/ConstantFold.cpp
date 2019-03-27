//===- ConstantFold.cpp - LLVM constant folder ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements folding of constants for LLVM.  This implements the
// (internal) ConstantFold.h interface, which is used by the
// ConstantExpr::get* methods to automatically fold constants when possible.
//
// The current constant folding implementation is implemented in two pieces: the
// pieces that don't need DataLayout, and the pieces that do. This is to avoid
// a dependence in IR on Target.
//
//===----------------------------------------------------------------------===//

#include "ConstantFold.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
using namespace llvm;
using namespace llvm::PatternMatch;

//===----------------------------------------------------------------------===//
//                ConstantFold*Instruction Implementations
//===----------------------------------------------------------------------===//

/// Convert the specified vector Constant node to the specified vector type.
/// At this point, we know that the elements of the input vector constant are
/// all simple integer or FP values.
static Constant *BitCastConstantVector(Constant *CV, VectorType *DstTy) {

  if (CV->isAllOnesValue()) return Constant::getAllOnesValue(DstTy);
  if (CV->isNullValue()) return Constant::getNullValue(DstTy);

  // If this cast changes element count then we can't handle it here:
  // doing so requires endianness information.  This should be handled by
  // Analysis/ConstantFolding.cpp
  unsigned NumElts = DstTy->getNumElements();
  if (NumElts != CV->getType()->getVectorNumElements())
    return nullptr;

  Type *DstEltTy = DstTy->getElementType();

  SmallVector<Constant*, 16> Result;
  Type *Ty = IntegerType::get(CV->getContext(), 32);
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *C =
      ConstantExpr::getExtractElement(CV, ConstantInt::get(Ty, i));
    C = ConstantExpr::getBitCast(C, DstEltTy);
    Result.push_back(C);
  }

  return ConstantVector::get(Result);
}

/// This function determines which opcode to use to fold two constant cast
/// expressions together. It uses CastInst::isEliminableCastPair to determine
/// the opcode. Consequently its just a wrapper around that function.
/// Determine if it is valid to fold a cast of a cast
static unsigned
foldConstantCastPair(
  unsigned opc,          ///< opcode of the second cast constant expression
  ConstantExpr *Op,      ///< the first cast constant expression
  Type *DstTy            ///< destination type of the first cast
) {
  assert(Op && Op->isCast() && "Can't fold cast of cast without a cast!");
  assert(DstTy && DstTy->isFirstClassType() && "Invalid cast destination type");
  assert(CastInst::isCast(opc) && "Invalid cast opcode");

  // The types and opcodes for the two Cast constant expressions
  Type *SrcTy = Op->getOperand(0)->getType();
  Type *MidTy = Op->getType();
  Instruction::CastOps firstOp = Instruction::CastOps(Op->getOpcode());
  Instruction::CastOps secondOp = Instruction::CastOps(opc);

  // Assume that pointers are never more than 64 bits wide, and only use this
  // for the middle type. Otherwise we could end up folding away illegal
  // bitcasts between address spaces with different sizes.
  IntegerType *FakeIntPtrTy = Type::getInt64Ty(DstTy->getContext());

  // Let CastInst::isEliminableCastPair do the heavy lifting.
  return CastInst::isEliminableCastPair(firstOp, secondOp, SrcTy, MidTy, DstTy,
                                        nullptr, FakeIntPtrTy, nullptr);
}

static Constant *FoldBitCast(Constant *V, Type *DestTy) {
  Type *SrcTy = V->getType();
  if (SrcTy == DestTy)
    return V; // no-op cast

  // Check to see if we are casting a pointer to an aggregate to a pointer to
  // the first element.  If so, return the appropriate GEP instruction.
  if (PointerType *PTy = dyn_cast<PointerType>(V->getType()))
    if (PointerType *DPTy = dyn_cast<PointerType>(DestTy))
      if (PTy->getAddressSpace() == DPTy->getAddressSpace()
          && PTy->getElementType()->isSized()) {
        SmallVector<Value*, 8> IdxList;
        Value *Zero =
          Constant::getNullValue(Type::getInt32Ty(DPTy->getContext()));
        IdxList.push_back(Zero);
        Type *ElTy = PTy->getElementType();
        while (ElTy != DPTy->getElementType()) {
          if (StructType *STy = dyn_cast<StructType>(ElTy)) {
            if (STy->getNumElements() == 0) break;
            ElTy = STy->getElementType(0);
            IdxList.push_back(Zero);
          } else if (SequentialType *STy =
                     dyn_cast<SequentialType>(ElTy)) {
            ElTy = STy->getElementType();
            IdxList.push_back(Zero);
          } else {
            break;
          }
        }

        if (ElTy == DPTy->getElementType())
          // This GEP is inbounds because all indices are zero.
          return ConstantExpr::getInBoundsGetElementPtr(PTy->getElementType(),
                                                        V, IdxList);
      }

  // Handle casts from one vector constant to another.  We know that the src
  // and dest type have the same size (otherwise its an illegal cast).
  if (VectorType *DestPTy = dyn_cast<VectorType>(DestTy)) {
    if (VectorType *SrcTy = dyn_cast<VectorType>(V->getType())) {
      assert(DestPTy->getBitWidth() == SrcTy->getBitWidth() &&
             "Not cast between same sized vectors!");
      SrcTy = nullptr;
      // First, check for null.  Undef is already handled.
      if (isa<ConstantAggregateZero>(V))
        return Constant::getNullValue(DestTy);

      // Handle ConstantVector and ConstantAggregateVector.
      return BitCastConstantVector(V, DestPTy);
    }

    // Canonicalize scalar-to-vector bitcasts into vector-to-vector bitcasts
    // This allows for other simplifications (although some of them
    // can only be handled by Analysis/ConstantFolding.cpp).
    if (isa<ConstantInt>(V) || isa<ConstantFP>(V))
      return ConstantExpr::getBitCast(ConstantVector::get(V), DestPTy);
  }

  // Finally, implement bitcast folding now.   The code below doesn't handle
  // bitcast right.
  if (isa<ConstantPointerNull>(V))  // ptr->ptr cast.
    return ConstantPointerNull::get(cast<PointerType>(DestTy));

  // Handle integral constant input.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    if (DestTy->isIntegerTy())
      // Integral -> Integral. This is a no-op because the bit widths must
      // be the same. Consequently, we just fold to V.
      return V;

    // See note below regarding the PPC_FP128 restriction.
    if (DestTy->isFloatingPointTy() && !DestTy->isPPC_FP128Ty())
      return ConstantFP::get(DestTy->getContext(),
                             APFloat(DestTy->getFltSemantics(),
                                     CI->getValue()));

    // Otherwise, can't fold this (vector?)
    return nullptr;
  }

  // Handle ConstantFP input: FP -> Integral.
  if (ConstantFP *FP = dyn_cast<ConstantFP>(V)) {
    // PPC_FP128 is really the sum of two consecutive doubles, where the first
    // double is always stored first in memory, regardless of the target
    // endianness. The memory layout of i128, however, depends on the target
    // endianness, and so we can't fold this without target endianness
    // information. This should instead be handled by
    // Analysis/ConstantFolding.cpp
    if (FP->getType()->isPPC_FP128Ty())
      return nullptr;

    // Make sure dest type is compatible with the folded integer constant.
    if (!DestTy->isIntegerTy())
      return nullptr;

    return ConstantInt::get(FP->getContext(),
                            FP->getValueAPF().bitcastToAPInt());
  }

  return nullptr;
}


/// V is an integer constant which only has a subset of its bytes used.
/// The bytes used are indicated by ByteStart (which is the first byte used,
/// counting from the least significant byte) and ByteSize, which is the number
/// of bytes used.
///
/// This function analyzes the specified constant to see if the specified byte
/// range can be returned as a simplified constant.  If so, the constant is
/// returned, otherwise null is returned.
static Constant *ExtractConstantBytes(Constant *C, unsigned ByteStart,
                                      unsigned ByteSize) {
  assert(C->getType()->isIntegerTy() &&
         (cast<IntegerType>(C->getType())->getBitWidth() & 7) == 0 &&
         "Non-byte sized integer input");
  unsigned CSize = cast<IntegerType>(C->getType())->getBitWidth()/8;
  assert(ByteSize && "Must be accessing some piece");
  assert(ByteStart+ByteSize <= CSize && "Extracting invalid piece from input");
  assert(ByteSize != CSize && "Should not extract everything");

  // Constant Integers are simple.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
    APInt V = CI->getValue();
    if (ByteStart)
      V.lshrInPlace(ByteStart*8);
    V = V.trunc(ByteSize*8);
    return ConstantInt::get(CI->getContext(), V);
  }

  // In the input is a constant expr, we might be able to recursively simplify.
  // If not, we definitely can't do anything.
  ConstantExpr *CE = dyn_cast<ConstantExpr>(C);
  if (!CE) return nullptr;

  switch (CE->getOpcode()) {
  default: return nullptr;
  case Instruction::Or: {
    Constant *RHS = ExtractConstantBytes(CE->getOperand(1), ByteStart,ByteSize);
    if (!RHS)
      return nullptr;

    // X | -1 -> -1.
    if (ConstantInt *RHSC = dyn_cast<ConstantInt>(RHS))
      if (RHSC->isMinusOne())
        return RHSC;

    Constant *LHS = ExtractConstantBytes(CE->getOperand(0), ByteStart,ByteSize);
    if (!LHS)
      return nullptr;
    return ConstantExpr::getOr(LHS, RHS);
  }
  case Instruction::And: {
    Constant *RHS = ExtractConstantBytes(CE->getOperand(1), ByteStart,ByteSize);
    if (!RHS)
      return nullptr;

    // X & 0 -> 0.
    if (RHS->isNullValue())
      return RHS;

    Constant *LHS = ExtractConstantBytes(CE->getOperand(0), ByteStart,ByteSize);
    if (!LHS)
      return nullptr;
    return ConstantExpr::getAnd(LHS, RHS);
  }
  case Instruction::LShr: {
    ConstantInt *Amt = dyn_cast<ConstantInt>(CE->getOperand(1));
    if (!Amt)
      return nullptr;
    unsigned ShAmt = Amt->getZExtValue();
    // Cannot analyze non-byte shifts.
    if ((ShAmt & 7) != 0)
      return nullptr;
    ShAmt >>= 3;

    // If the extract is known to be all zeros, return zero.
    if (ByteStart >= CSize-ShAmt)
      return Constant::getNullValue(IntegerType::get(CE->getContext(),
                                                     ByteSize*8));
    // If the extract is known to be fully in the input, extract it.
    if (ByteStart+ByteSize+ShAmt <= CSize)
      return ExtractConstantBytes(CE->getOperand(0), ByteStart+ShAmt, ByteSize);

    // TODO: Handle the 'partially zero' case.
    return nullptr;
  }

  case Instruction::Shl: {
    ConstantInt *Amt = dyn_cast<ConstantInt>(CE->getOperand(1));
    if (!Amt)
      return nullptr;
    unsigned ShAmt = Amt->getZExtValue();
    // Cannot analyze non-byte shifts.
    if ((ShAmt & 7) != 0)
      return nullptr;
    ShAmt >>= 3;

    // If the extract is known to be all zeros, return zero.
    if (ByteStart+ByteSize <= ShAmt)
      return Constant::getNullValue(IntegerType::get(CE->getContext(),
                                                     ByteSize*8));
    // If the extract is known to be fully in the input, extract it.
    if (ByteStart >= ShAmt)
      return ExtractConstantBytes(CE->getOperand(0), ByteStart-ShAmt, ByteSize);

    // TODO: Handle the 'partially zero' case.
    return nullptr;
  }

  case Instruction::ZExt: {
    unsigned SrcBitSize =
      cast<IntegerType>(CE->getOperand(0)->getType())->getBitWidth();

    // If extracting something that is completely zero, return 0.
    if (ByteStart*8 >= SrcBitSize)
      return Constant::getNullValue(IntegerType::get(CE->getContext(),
                                                     ByteSize*8));

    // If exactly extracting the input, return it.
    if (ByteStart == 0 && ByteSize*8 == SrcBitSize)
      return CE->getOperand(0);

    // If extracting something completely in the input, if the input is a
    // multiple of 8 bits, recurse.
    if ((SrcBitSize&7) == 0 && (ByteStart+ByteSize)*8 <= SrcBitSize)
      return ExtractConstantBytes(CE->getOperand(0), ByteStart, ByteSize);

    // Otherwise, if extracting a subset of the input, which is not multiple of
    // 8 bits, do a shift and trunc to get the bits.
    if ((ByteStart+ByteSize)*8 < SrcBitSize) {
      assert((SrcBitSize&7) && "Shouldn't get byte sized case here");
      Constant *Res = CE->getOperand(0);
      if (ByteStart)
        Res = ConstantExpr::getLShr(Res,
                                 ConstantInt::get(Res->getType(), ByteStart*8));
      return ConstantExpr::getTrunc(Res, IntegerType::get(C->getContext(),
                                                          ByteSize*8));
    }

    // TODO: Handle the 'partially zero' case.
    return nullptr;
  }
  }
}

/// Return a ConstantExpr with type DestTy for sizeof on Ty, with any known
/// factors factored out. If Folded is false, return null if no factoring was
/// possible, to avoid endlessly bouncing an unfoldable expression back into the
/// top-level folder.
static Constant *getFoldedSizeOf(Type *Ty, Type *DestTy, bool Folded) {
  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Constant *N = ConstantInt::get(DestTy, ATy->getNumElements());
    Constant *E = getFoldedSizeOf(ATy->getElementType(), DestTy, true);
    return ConstantExpr::getNUWMul(E, N);
  }

  if (StructType *STy = dyn_cast<StructType>(Ty))
    if (!STy->isPacked()) {
      unsigned NumElems = STy->getNumElements();
      // An empty struct has size zero.
      if (NumElems == 0)
        return ConstantExpr::getNullValue(DestTy);
      // Check for a struct with all members having the same size.
      Constant *MemberSize =
        getFoldedSizeOf(STy->getElementType(0), DestTy, true);
      bool AllSame = true;
      for (unsigned i = 1; i != NumElems; ++i)
        if (MemberSize !=
            getFoldedSizeOf(STy->getElementType(i), DestTy, true)) {
          AllSame = false;
          break;
        }
      if (AllSame) {
        Constant *N = ConstantInt::get(DestTy, NumElems);
        return ConstantExpr::getNUWMul(MemberSize, N);
      }
    }

  // Pointer size doesn't depend on the pointee type, so canonicalize them
  // to an arbitrary pointee.
  if (PointerType *PTy = dyn_cast<PointerType>(Ty))
    if (!PTy->getElementType()->isIntegerTy(1))
      return
        getFoldedSizeOf(PointerType::get(IntegerType::get(PTy->getContext(), 1),
                                         PTy->getAddressSpace()),
                        DestTy, true);

  // If there's no interesting folding happening, bail so that we don't create
  // a constant that looks like it needs folding but really doesn't.
  if (!Folded)
    return nullptr;

  // Base case: Get a regular sizeof expression.
  Constant *C = ConstantExpr::getSizeOf(Ty);
  C = ConstantExpr::getCast(CastInst::getCastOpcode(C, false,
                                                    DestTy, false),
                            C, DestTy);
  return C;
}

/// Return a ConstantExpr with type DestTy for alignof on Ty, with any known
/// factors factored out. If Folded is false, return null if no factoring was
/// possible, to avoid endlessly bouncing an unfoldable expression back into the
/// top-level folder.
static Constant *getFoldedAlignOf(Type *Ty, Type *DestTy, bool Folded) {
  // The alignment of an array is equal to the alignment of the
  // array element. Note that this is not always true for vectors.
  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Constant *C = ConstantExpr::getAlignOf(ATy->getElementType());
    C = ConstantExpr::getCast(CastInst::getCastOpcode(C, false,
                                                      DestTy,
                                                      false),
                              C, DestTy);
    return C;
  }

  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    // Packed structs always have an alignment of 1.
    if (STy->isPacked())
      return ConstantInt::get(DestTy, 1);

    // Otherwise, struct alignment is the maximum alignment of any member.
    // Without target data, we can't compare much, but we can check to see
    // if all the members have the same alignment.
    unsigned NumElems = STy->getNumElements();
    // An empty struct has minimal alignment.
    if (NumElems == 0)
      return ConstantInt::get(DestTy, 1);
    // Check for a struct with all members having the same alignment.
    Constant *MemberAlign =
      getFoldedAlignOf(STy->getElementType(0), DestTy, true);
    bool AllSame = true;
    for (unsigned i = 1; i != NumElems; ++i)
      if (MemberAlign != getFoldedAlignOf(STy->getElementType(i), DestTy, true)) {
        AllSame = false;
        break;
      }
    if (AllSame)
      return MemberAlign;
  }

  // Pointer alignment doesn't depend on the pointee type, so canonicalize them
  // to an arbitrary pointee.
  if (PointerType *PTy = dyn_cast<PointerType>(Ty))
    if (!PTy->getElementType()->isIntegerTy(1))
      return
        getFoldedAlignOf(PointerType::get(IntegerType::get(PTy->getContext(),
                                                           1),
                                          PTy->getAddressSpace()),
                         DestTy, true);

  // If there's no interesting folding happening, bail so that we don't create
  // a constant that looks like it needs folding but really doesn't.
  if (!Folded)
    return nullptr;

  // Base case: Get a regular alignof expression.
  Constant *C = ConstantExpr::getAlignOf(Ty);
  C = ConstantExpr::getCast(CastInst::getCastOpcode(C, false,
                                                    DestTy, false),
                            C, DestTy);
  return C;
}

/// Return a ConstantExpr with type DestTy for offsetof on Ty and FieldNo, with
/// any known factors factored out. If Folded is false, return null if no
/// factoring was possible, to avoid endlessly bouncing an unfoldable expression
/// back into the top-level folder.
static Constant *getFoldedOffsetOf(Type *Ty, Constant *FieldNo, Type *DestTy,
                                   bool Folded) {
  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Constant *N = ConstantExpr::getCast(CastInst::getCastOpcode(FieldNo, false,
                                                                DestTy, false),
                                        FieldNo, DestTy);
    Constant *E = getFoldedSizeOf(ATy->getElementType(), DestTy, true);
    return ConstantExpr::getNUWMul(E, N);
  }

  if (StructType *STy = dyn_cast<StructType>(Ty))
    if (!STy->isPacked()) {
      unsigned NumElems = STy->getNumElements();
      // An empty struct has no members.
      if (NumElems == 0)
        return nullptr;
      // Check for a struct with all members having the same size.
      Constant *MemberSize =
        getFoldedSizeOf(STy->getElementType(0), DestTy, true);
      bool AllSame = true;
      for (unsigned i = 1; i != NumElems; ++i)
        if (MemberSize !=
            getFoldedSizeOf(STy->getElementType(i), DestTy, true)) {
          AllSame = false;
          break;
        }
      if (AllSame) {
        Constant *N = ConstantExpr::getCast(CastInst::getCastOpcode(FieldNo,
                                                                    false,
                                                                    DestTy,
                                                                    false),
                                            FieldNo, DestTy);
        return ConstantExpr::getNUWMul(MemberSize, N);
      }
    }

  // If there's no interesting folding happening, bail so that we don't create
  // a constant that looks like it needs folding but really doesn't.
  if (!Folded)
    return nullptr;

  // Base case: Get a regular offsetof expression.
  Constant *C = ConstantExpr::getOffsetOf(Ty, FieldNo);
  C = ConstantExpr::getCast(CastInst::getCastOpcode(C, false,
                                                    DestTy, false),
                            C, DestTy);
  return C;
}

Constant *llvm::ConstantFoldCastInstruction(unsigned opc, Constant *V,
                                            Type *DestTy) {
  if (isa<UndefValue>(V)) {
    // zext(undef) = 0, because the top bits will be zero.
    // sext(undef) = 0, because the top bits will all be the same.
    // [us]itofp(undef) = 0, because the result value is bounded.
    if (opc == Instruction::ZExt || opc == Instruction::SExt ||
        opc == Instruction::UIToFP || opc == Instruction::SIToFP)
      return Constant::getNullValue(DestTy);
    return UndefValue::get(DestTy);
  }

  if (V->isNullValue() && !DestTy->isX86_MMXTy() &&
      opc != Instruction::AddrSpaceCast)
    return Constant::getNullValue(DestTy);

  // If the cast operand is a constant expression, there's a few things we can
  // do to try to simplify it.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    if (CE->isCast()) {
      // Try hard to fold cast of cast because they are often eliminable.
      if (unsigned newOpc = foldConstantCastPair(opc, CE, DestTy))
        return ConstantExpr::getCast(newOpc, CE->getOperand(0), DestTy);
    } else if (CE->getOpcode() == Instruction::GetElementPtr &&
               // Do not fold addrspacecast (gep 0, .., 0). It might make the
               // addrspacecast uncanonicalized.
               opc != Instruction::AddrSpaceCast &&
               // Do not fold bitcast (gep) with inrange index, as this loses
               // information.
               !cast<GEPOperator>(CE)->getInRangeIndex().hasValue() &&
               // Do not fold if the gep type is a vector, as bitcasting
               // operand 0 of a vector gep will result in a bitcast between
               // different sizes.
               !CE->getType()->isVectorTy()) {
      // If all of the indexes in the GEP are null values, there is no pointer
      // adjustment going on.  We might as well cast the source pointer.
      bool isAllNull = true;
      for (unsigned i = 1, e = CE->getNumOperands(); i != e; ++i)
        if (!CE->getOperand(i)->isNullValue()) {
          isAllNull = false;
          break;
        }
      if (isAllNull)
        // This is casting one pointer type to another, always BitCast
        return ConstantExpr::getPointerCast(CE->getOperand(0), DestTy);
    }
  }

  // If the cast operand is a constant vector, perform the cast by
  // operating on each element. In the cast of bitcasts, the element
  // count may be mismatched; don't attempt to handle that here.
  if ((isa<ConstantVector>(V) || isa<ConstantDataVector>(V)) &&
      DestTy->isVectorTy() &&
      DestTy->getVectorNumElements() == V->getType()->getVectorNumElements()) {
    SmallVector<Constant*, 16> res;
    VectorType *DestVecTy = cast<VectorType>(DestTy);
    Type *DstEltTy = DestVecTy->getElementType();
    Type *Ty = IntegerType::get(V->getContext(), 32);
    for (unsigned i = 0, e = V->getType()->getVectorNumElements(); i != e; ++i) {
      Constant *C =
        ConstantExpr::getExtractElement(V, ConstantInt::get(Ty, i));
      res.push_back(ConstantExpr::getCast(opc, C, DstEltTy));
    }
    return ConstantVector::get(res);
  }

  // We actually have to do a cast now. Perform the cast according to the
  // opcode specified.
  switch (opc) {
  default:
    llvm_unreachable("Failed to cast constant expression");
  case Instruction::FPTrunc:
  case Instruction::FPExt:
    if (ConstantFP *FPC = dyn_cast<ConstantFP>(V)) {
      bool ignored;
      APFloat Val = FPC->getValueAPF();
      Val.convert(DestTy->isHalfTy() ? APFloat::IEEEhalf() :
                  DestTy->isFloatTy() ? APFloat::IEEEsingle() :
                  DestTy->isDoubleTy() ? APFloat::IEEEdouble() :
                  DestTy->isX86_FP80Ty() ? APFloat::x87DoubleExtended() :
                  DestTy->isFP128Ty() ? APFloat::IEEEquad() :
                  DestTy->isPPC_FP128Ty() ? APFloat::PPCDoubleDouble() :
                  APFloat::Bogus(),
                  APFloat::rmNearestTiesToEven, &ignored);
      return ConstantFP::get(V->getContext(), Val);
    }
    return nullptr; // Can't fold.
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    if (ConstantFP *FPC = dyn_cast<ConstantFP>(V)) {
      const APFloat &V = FPC->getValueAPF();
      bool ignored;
      uint32_t DestBitWidth = cast<IntegerType>(DestTy)->getBitWidth();
      APSInt IntVal(DestBitWidth, opc == Instruction::FPToUI);
      if (APFloat::opInvalidOp ==
          V.convertToInteger(IntVal, APFloat::rmTowardZero, &ignored)) {
        // Undefined behavior invoked - the destination type can't represent
        // the input constant.
        return UndefValue::get(DestTy);
      }
      return ConstantInt::get(FPC->getContext(), IntVal);
    }
    return nullptr; // Can't fold.
  case Instruction::IntToPtr:   //always treated as unsigned
    if (V->isNullValue())       // Is it an integral null value?
      return ConstantPointerNull::get(cast<PointerType>(DestTy));
    return nullptr;                   // Other pointer types cannot be casted
  case Instruction::PtrToInt:   // always treated as unsigned
    // Is it a null pointer value?
    if (V->isNullValue())
      return ConstantInt::get(DestTy, 0);
    // If this is a sizeof-like expression, pull out multiplications by
    // known factors to expose them to subsequent folding. If it's an
    // alignof-like expression, factor out known factors.
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
      if (CE->getOpcode() == Instruction::GetElementPtr &&
          CE->getOperand(0)->isNullValue()) {
        // FIXME: Looks like getFoldedSizeOf(), getFoldedOffsetOf() and
        // getFoldedAlignOf() don't handle the case when DestTy is a vector of
        // pointers yet. We end up in asserts in CastInst::getCastOpcode (see
        // test/Analysis/ConstantFolding/cast-vector.ll). I've only seen this
        // happen in one "real" C-code test case, so it does not seem to be an
        // important optimization to handle vectors here. For now, simply bail
        // out.
        if (DestTy->isVectorTy())
          return nullptr;
        GEPOperator *GEPO = cast<GEPOperator>(CE);
        Type *Ty = GEPO->getSourceElementType();
        if (CE->getNumOperands() == 2) {
          // Handle a sizeof-like expression.
          Constant *Idx = CE->getOperand(1);
          bool isOne = isa<ConstantInt>(Idx) && cast<ConstantInt>(Idx)->isOne();
          if (Constant *C = getFoldedSizeOf(Ty, DestTy, !isOne)) {
            Idx = ConstantExpr::getCast(CastInst::getCastOpcode(Idx, true,
                                                                DestTy, false),
                                        Idx, DestTy);
            return ConstantExpr::getMul(C, Idx);
          }
        } else if (CE->getNumOperands() == 3 &&
                   CE->getOperand(1)->isNullValue()) {
          // Handle an alignof-like expression.
          if (StructType *STy = dyn_cast<StructType>(Ty))
            if (!STy->isPacked()) {
              ConstantInt *CI = cast<ConstantInt>(CE->getOperand(2));
              if (CI->isOne() &&
                  STy->getNumElements() == 2 &&
                  STy->getElementType(0)->isIntegerTy(1)) {
                return getFoldedAlignOf(STy->getElementType(1), DestTy, false);
              }
            }
          // Handle an offsetof-like expression.
          if (Ty->isStructTy() || Ty->isArrayTy()) {
            if (Constant *C = getFoldedOffsetOf(Ty, CE->getOperand(2),
                                                DestTy, false))
              return C;
          }
        }
      }
    // Other pointer types cannot be casted
    return nullptr;
  case Instruction::UIToFP:
  case Instruction::SIToFP:
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      const APInt &api = CI->getValue();
      APFloat apf(DestTy->getFltSemantics(),
                  APInt::getNullValue(DestTy->getPrimitiveSizeInBits()));
      apf.convertFromAPInt(api, opc==Instruction::SIToFP,
                           APFloat::rmNearestTiesToEven);
      return ConstantFP::get(V->getContext(), apf);
    }
    return nullptr;
  case Instruction::ZExt:
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      uint32_t BitWidth = cast<IntegerType>(DestTy)->getBitWidth();
      return ConstantInt::get(V->getContext(),
                              CI->getValue().zext(BitWidth));
    }
    return nullptr;
  case Instruction::SExt:
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      uint32_t BitWidth = cast<IntegerType>(DestTy)->getBitWidth();
      return ConstantInt::get(V->getContext(),
                              CI->getValue().sext(BitWidth));
    }
    return nullptr;
  case Instruction::Trunc: {
    if (V->getType()->isVectorTy())
      return nullptr;

    uint32_t DestBitWidth = cast<IntegerType>(DestTy)->getBitWidth();
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
      return ConstantInt::get(V->getContext(),
                              CI->getValue().trunc(DestBitWidth));
    }

    // The input must be a constantexpr.  See if we can simplify this based on
    // the bytes we are demanding.  Only do this if the source and dest are an
    // even multiple of a byte.
    if ((DestBitWidth & 7) == 0 &&
        (cast<IntegerType>(V->getType())->getBitWidth() & 7) == 0)
      if (Constant *Res = ExtractConstantBytes(V, 0, DestBitWidth / 8))
        return Res;

    return nullptr;
  }
  case Instruction::BitCast:
    return FoldBitCast(V, DestTy);
  case Instruction::AddrSpaceCast:
    return nullptr;
  }
}

Constant *llvm::ConstantFoldSelectInstruction(Constant *Cond,
                                              Constant *V1, Constant *V2) {
  // Check for i1 and vector true/false conditions.
  if (Cond->isNullValue()) return V2;
  if (Cond->isAllOnesValue()) return V1;

  // If the condition is a vector constant, fold the result elementwise.
  if (ConstantVector *CondV = dyn_cast<ConstantVector>(Cond)) {
    SmallVector<Constant*, 16> Result;
    Type *Ty = IntegerType::get(CondV->getContext(), 32);
    for (unsigned i = 0, e = V1->getType()->getVectorNumElements(); i != e;++i){
      Constant *V;
      Constant *V1Element = ConstantExpr::getExtractElement(V1,
                                                    ConstantInt::get(Ty, i));
      Constant *V2Element = ConstantExpr::getExtractElement(V2,
                                                    ConstantInt::get(Ty, i));
      Constant *Cond = dyn_cast<Constant>(CondV->getOperand(i));
      if (V1Element == V2Element) {
        V = V1Element;
      } else if (isa<UndefValue>(Cond)) {
        V = isa<UndefValue>(V1Element) ? V1Element : V2Element;
      } else {
        if (!isa<ConstantInt>(Cond)) break;
        V = Cond->isNullValue() ? V2Element : V1Element;
      }
      Result.push_back(V);
    }

    // If we were able to build the vector, return it.
    if (Result.size() == V1->getType()->getVectorNumElements())
      return ConstantVector::get(Result);
  }

  if (isa<UndefValue>(Cond)) {
    if (isa<UndefValue>(V1)) return V1;
    return V2;
  }
  if (isa<UndefValue>(V1)) return V2;
  if (isa<UndefValue>(V2)) return V1;
  if (V1 == V2) return V1;

  if (ConstantExpr *TrueVal = dyn_cast<ConstantExpr>(V1)) {
    if (TrueVal->getOpcode() == Instruction::Select)
      if (TrueVal->getOperand(0) == Cond)
        return ConstantExpr::getSelect(Cond, TrueVal->getOperand(1), V2);
  }
  if (ConstantExpr *FalseVal = dyn_cast<ConstantExpr>(V2)) {
    if (FalseVal->getOpcode() == Instruction::Select)
      if (FalseVal->getOperand(0) == Cond)
        return ConstantExpr::getSelect(Cond, V1, FalseVal->getOperand(2));
  }

  return nullptr;
}

Constant *llvm::ConstantFoldExtractElementInstruction(Constant *Val,
                                                      Constant *Idx) {
  if (isa<UndefValue>(Val))  // ee(undef, x) -> undef
    return UndefValue::get(Val->getType()->getVectorElementType());
  if (Val->isNullValue())  // ee(zero, x) -> zero
    return Constant::getNullValue(Val->getType()->getVectorElementType());
  // ee({w,x,y,z}, undef) -> undef
  if (isa<UndefValue>(Idx))
    return UndefValue::get(Val->getType()->getVectorElementType());

  if (ConstantInt *CIdx = dyn_cast<ConstantInt>(Idx)) {
    // ee({w,x,y,z}, wrong_value) -> undef
    if (CIdx->uge(Val->getType()->getVectorNumElements()))
      return UndefValue::get(Val->getType()->getVectorElementType());
    return Val->getAggregateElement(CIdx->getZExtValue());
  }
  return nullptr;
}

Constant *llvm::ConstantFoldInsertElementInstruction(Constant *Val,
                                                     Constant *Elt,
                                                     Constant *Idx) {
  if (isa<UndefValue>(Idx))
    return UndefValue::get(Val->getType());

  ConstantInt *CIdx = dyn_cast<ConstantInt>(Idx);
  if (!CIdx) return nullptr;

  unsigned NumElts = Val->getType()->getVectorNumElements();
  if (CIdx->uge(NumElts))
    return UndefValue::get(Val->getType());

  SmallVector<Constant*, 16> Result;
  Result.reserve(NumElts);
  auto *Ty = Type::getInt32Ty(Val->getContext());
  uint64_t IdxVal = CIdx->getZExtValue();
  for (unsigned i = 0; i != NumElts; ++i) {
    if (i == IdxVal) {
      Result.push_back(Elt);
      continue;
    }

    Constant *C = ConstantExpr::getExtractElement(Val, ConstantInt::get(Ty, i));
    Result.push_back(C);
  }

  return ConstantVector::get(Result);
}

Constant *llvm::ConstantFoldShuffleVectorInstruction(Constant *V1,
                                                     Constant *V2,
                                                     Constant *Mask) {
  unsigned MaskNumElts = Mask->getType()->getVectorNumElements();
  Type *EltTy = V1->getType()->getVectorElementType();

  // Undefined shuffle mask -> undefined value.
  if (isa<UndefValue>(Mask))
    return UndefValue::get(VectorType::get(EltTy, MaskNumElts));

  // Don't break the bitcode reader hack.
  if (isa<ConstantExpr>(Mask)) return nullptr;

  unsigned SrcNumElts = V1->getType()->getVectorNumElements();

  // Loop over the shuffle mask, evaluating each element.
  SmallVector<Constant*, 32> Result;
  for (unsigned i = 0; i != MaskNumElts; ++i) {
    int Elt = ShuffleVectorInst::getMaskValue(Mask, i);
    if (Elt == -1) {
      Result.push_back(UndefValue::get(EltTy));
      continue;
    }
    Constant *InElt;
    if (unsigned(Elt) >= SrcNumElts*2)
      InElt = UndefValue::get(EltTy);
    else if (unsigned(Elt) >= SrcNumElts) {
      Type *Ty = IntegerType::get(V2->getContext(), 32);
      InElt =
        ConstantExpr::getExtractElement(V2,
                                        ConstantInt::get(Ty, Elt - SrcNumElts));
    } else {
      Type *Ty = IntegerType::get(V1->getContext(), 32);
      InElt = ConstantExpr::getExtractElement(V1, ConstantInt::get(Ty, Elt));
    }
    Result.push_back(InElt);
  }

  return ConstantVector::get(Result);
}

Constant *llvm::ConstantFoldExtractValueInstruction(Constant *Agg,
                                                    ArrayRef<unsigned> Idxs) {
  // Base case: no indices, so return the entire value.
  if (Idxs.empty())
    return Agg;

  if (Constant *C = Agg->getAggregateElement(Idxs[0]))
    return ConstantFoldExtractValueInstruction(C, Idxs.slice(1));

  return nullptr;
}

Constant *llvm::ConstantFoldInsertValueInstruction(Constant *Agg,
                                                   Constant *Val,
                                                   ArrayRef<unsigned> Idxs) {
  // Base case: no indices, so replace the entire value.
  if (Idxs.empty())
    return Val;

  unsigned NumElts;
  if (StructType *ST = dyn_cast<StructType>(Agg->getType()))
    NumElts = ST->getNumElements();
  else
    NumElts = cast<SequentialType>(Agg->getType())->getNumElements();

  SmallVector<Constant*, 32> Result;
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *C = Agg->getAggregateElement(i);
    if (!C) return nullptr;

    if (Idxs[0] == i)
      C = ConstantFoldInsertValueInstruction(C, Val, Idxs.slice(1));

    Result.push_back(C);
  }

  if (StructType *ST = dyn_cast<StructType>(Agg->getType()))
    return ConstantStruct::get(ST, Result);
  if (ArrayType *AT = dyn_cast<ArrayType>(Agg->getType()))
    return ConstantArray::get(AT, Result);
  return ConstantVector::get(Result);
}

Constant *llvm::ConstantFoldBinaryInstruction(unsigned Opcode, Constant *C1,
                                              Constant *C2) {
  assert(Instruction::isBinaryOp(Opcode) && "Non-binary instruction detected");

  // Handle scalar UndefValue. Vectors are always evaluated per element.
  bool HasScalarUndef = !C1->getType()->isVectorTy() &&
                        (isa<UndefValue>(C1) || isa<UndefValue>(C2));
  if (HasScalarUndef) {
    switch (static_cast<Instruction::BinaryOps>(Opcode)) {
    case Instruction::Xor:
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2))
        // Handle undef ^ undef -> 0 special case. This is a common
        // idiom (misuse).
        return Constant::getNullValue(C1->getType());
      LLVM_FALLTHROUGH;
    case Instruction::Add:
    case Instruction::Sub:
      return UndefValue::get(C1->getType());
    case Instruction::And:
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2)) // undef & undef -> undef
        return C1;
      return Constant::getNullValue(C1->getType());   // undef & X -> 0
    case Instruction::Mul: {
      // undef * undef -> undef
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2))
        return C1;
      const APInt *CV;
      // X * undef -> undef   if X is odd
      if (match(C1, m_APInt(CV)) || match(C2, m_APInt(CV)))
        if ((*CV)[0])
          return UndefValue::get(C1->getType());

      // X * undef -> 0       otherwise
      return Constant::getNullValue(C1->getType());
    }
    case Instruction::SDiv:
    case Instruction::UDiv:
      // X / undef -> undef
      if (isa<UndefValue>(C2))
        return C2;
      // undef / 0 -> undef
      // undef / 1 -> undef
      if (match(C2, m_Zero()) || match(C2, m_One()))
        return C1;
      // undef / X -> 0       otherwise
      return Constant::getNullValue(C1->getType());
    case Instruction::URem:
    case Instruction::SRem:
      // X % undef -> undef
      if (match(C2, m_Undef()))
        return C2;
      // undef % 0 -> undef
      if (match(C2, m_Zero()))
        return C1;
      // undef % X -> 0       otherwise
      return Constant::getNullValue(C1->getType());
    case Instruction::Or:                          // X | undef -> -1
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2)) // undef | undef -> undef
        return C1;
      return Constant::getAllOnesValue(C1->getType()); // undef | X -> ~0
    case Instruction::LShr:
      // X >>l undef -> undef
      if (isa<UndefValue>(C2))
        return C2;
      // undef >>l 0 -> undef
      if (match(C2, m_Zero()))
        return C1;
      // undef >>l X -> 0
      return Constant::getNullValue(C1->getType());
    case Instruction::AShr:
      // X >>a undef -> undef
      if (isa<UndefValue>(C2))
        return C2;
      // undef >>a 0 -> undef
      if (match(C2, m_Zero()))
        return C1;
      // TODO: undef >>a X -> undef if the shift is exact
      // undef >>a X -> 0
      return Constant::getNullValue(C1->getType());
    case Instruction::Shl:
      // X << undef -> undef
      if (isa<UndefValue>(C2))
        return C2;
      // undef << 0 -> undef
      if (match(C2, m_Zero()))
        return C1;
      // undef << X -> 0
      return Constant::getNullValue(C1->getType());
    case Instruction::FAdd:
    case Instruction::FSub:
    case Instruction::FMul:
    case Instruction::FDiv:
    case Instruction::FRem:
      // [any flop] undef, undef -> undef
      if (isa<UndefValue>(C1) && isa<UndefValue>(C2))
        return C1;
      // [any flop] C, undef -> NaN
      // [any flop] undef, C -> NaN
      // We could potentially specialize NaN/Inf constants vs. 'normal'
      // constants (possibly differently depending on opcode and operand). This
      // would allow returning undef sometimes. But it is always safe to fold to
      // NaN because we can choose the undef operand as NaN, and any FP opcode
      // with a NaN operand will propagate NaN.
      return ConstantFP::getNaN(C1->getType());
    case Instruction::BinaryOpsEnd:
      llvm_unreachable("Invalid BinaryOp");
    }
  }

  // Neither constant should be UndefValue, unless these are vector constants.
  assert(!HasScalarUndef && "Unexpected UndefValue");

  // Handle simplifications when the RHS is a constant int.
  if (ConstantInt *CI2 = dyn_cast<ConstantInt>(C2)) {
    switch (Opcode) {
    case Instruction::Add:
      if (CI2->isZero()) return C1;                             // X + 0 == X
      break;
    case Instruction::Sub:
      if (CI2->isZero()) return C1;                             // X - 0 == X
      break;
    case Instruction::Mul:
      if (CI2->isZero()) return C2;                             // X * 0 == 0
      if (CI2->isOne())
        return C1;                                              // X * 1 == X
      break;
    case Instruction::UDiv:
    case Instruction::SDiv:
      if (CI2->isOne())
        return C1;                                            // X / 1 == X
      if (CI2->isZero())
        return UndefValue::get(CI2->getType());               // X / 0 == undef
      break;
    case Instruction::URem:
    case Instruction::SRem:
      if (CI2->isOne())
        return Constant::getNullValue(CI2->getType());        // X % 1 == 0
      if (CI2->isZero())
        return UndefValue::get(CI2->getType());               // X % 0 == undef
      break;
    case Instruction::And:
      if (CI2->isZero()) return C2;                           // X & 0 == 0
      if (CI2->isMinusOne())
        return C1;                                            // X & -1 == X

      if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1)) {
        // (zext i32 to i64) & 4294967295 -> (zext i32 to i64)
        if (CE1->getOpcode() == Instruction::ZExt) {
          unsigned DstWidth = CI2->getType()->getBitWidth();
          unsigned SrcWidth =
            CE1->getOperand(0)->getType()->getPrimitiveSizeInBits();
          APInt PossiblySetBits(APInt::getLowBitsSet(DstWidth, SrcWidth));
          if ((PossiblySetBits & CI2->getValue()) == PossiblySetBits)
            return C1;
        }

        // If and'ing the address of a global with a constant, fold it.
        if (CE1->getOpcode() == Instruction::PtrToInt &&
            isa<GlobalValue>(CE1->getOperand(0))) {
          GlobalValue *GV = cast<GlobalValue>(CE1->getOperand(0));

          // Functions are at least 4-byte aligned.
          unsigned GVAlign = GV->getAlignment();
          if (isa<Function>(GV))
            GVAlign = std::max(GVAlign, 4U);

          if (GVAlign > 1) {
            unsigned DstWidth = CI2->getType()->getBitWidth();
            unsigned SrcWidth = std::min(DstWidth, Log2_32(GVAlign));
            APInt BitsNotSet(APInt::getLowBitsSet(DstWidth, SrcWidth));

            // If checking bits we know are clear, return zero.
            if ((CI2->getValue() & BitsNotSet) == CI2->getValue())
              return Constant::getNullValue(CI2->getType());
          }
        }
      }
      break;
    case Instruction::Or:
      if (CI2->isZero()) return C1;        // X | 0 == X
      if (CI2->isMinusOne())
        return C2;                         // X | -1 == -1
      break;
    case Instruction::Xor:
      if (CI2->isZero()) return C1;        // X ^ 0 == X

      if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1)) {
        switch (CE1->getOpcode()) {
        default: break;
        case Instruction::ICmp:
        case Instruction::FCmp:
          // cmp pred ^ true -> cmp !pred
          assert(CI2->isOne());
          CmpInst::Predicate pred = (CmpInst::Predicate)CE1->getPredicate();
          pred = CmpInst::getInversePredicate(pred);
          return ConstantExpr::getCompare(pred, CE1->getOperand(0),
                                          CE1->getOperand(1));
        }
      }
      break;
    case Instruction::AShr:
      // ashr (zext C to Ty), C2 -> lshr (zext C, CSA), C2
      if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1))
        if (CE1->getOpcode() == Instruction::ZExt)  // Top bits known zero.
          return ConstantExpr::getLShr(C1, C2);
      break;
    }
  } else if (isa<ConstantInt>(C1)) {
    // If C1 is a ConstantInt and C2 is not, swap the operands.
    if (Instruction::isCommutative(Opcode))
      return ConstantExpr::get(Opcode, C2, C1);
  }

  if (ConstantInt *CI1 = dyn_cast<ConstantInt>(C1)) {
    if (ConstantInt *CI2 = dyn_cast<ConstantInt>(C2)) {
      const APInt &C1V = CI1->getValue();
      const APInt &C2V = CI2->getValue();
      switch (Opcode) {
      default:
        break;
      case Instruction::Add:
        return ConstantInt::get(CI1->getContext(), C1V + C2V);
      case Instruction::Sub:
        return ConstantInt::get(CI1->getContext(), C1V - C2V);
      case Instruction::Mul:
        return ConstantInt::get(CI1->getContext(), C1V * C2V);
      case Instruction::UDiv:
        assert(!CI2->isZero() && "Div by zero handled above");
        return ConstantInt::get(CI1->getContext(), C1V.udiv(C2V));
      case Instruction::SDiv:
        assert(!CI2->isZero() && "Div by zero handled above");
        if (C2V.isAllOnesValue() && C1V.isMinSignedValue())
          return UndefValue::get(CI1->getType());   // MIN_INT / -1 -> undef
        return ConstantInt::get(CI1->getContext(), C1V.sdiv(C2V));
      case Instruction::URem:
        assert(!CI2->isZero() && "Div by zero handled above");
        return ConstantInt::get(CI1->getContext(), C1V.urem(C2V));
      case Instruction::SRem:
        assert(!CI2->isZero() && "Div by zero handled above");
        if (C2V.isAllOnesValue() && C1V.isMinSignedValue())
          return UndefValue::get(CI1->getType());   // MIN_INT % -1 -> undef
        return ConstantInt::get(CI1->getContext(), C1V.srem(C2V));
      case Instruction::And:
        return ConstantInt::get(CI1->getContext(), C1V & C2V);
      case Instruction::Or:
        return ConstantInt::get(CI1->getContext(), C1V | C2V);
      case Instruction::Xor:
        return ConstantInt::get(CI1->getContext(), C1V ^ C2V);
      case Instruction::Shl:
        if (C2V.ult(C1V.getBitWidth()))
          return ConstantInt::get(CI1->getContext(), C1V.shl(C2V));
        return UndefValue::get(C1->getType()); // too big shift is undef
      case Instruction::LShr:
        if (C2V.ult(C1V.getBitWidth()))
          return ConstantInt::get(CI1->getContext(), C1V.lshr(C2V));
        return UndefValue::get(C1->getType()); // too big shift is undef
      case Instruction::AShr:
        if (C2V.ult(C1V.getBitWidth()))
          return ConstantInt::get(CI1->getContext(), C1V.ashr(C2V));
        return UndefValue::get(C1->getType()); // too big shift is undef
      }
    }

    switch (Opcode) {
    case Instruction::SDiv:
    case Instruction::UDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Shl:
      if (CI1->isZero()) return C1;
      break;
    default:
      break;
    }
  } else if (ConstantFP *CFP1 = dyn_cast<ConstantFP>(C1)) {
    if (ConstantFP *CFP2 = dyn_cast<ConstantFP>(C2)) {
      const APFloat &C1V = CFP1->getValueAPF();
      const APFloat &C2V = CFP2->getValueAPF();
      APFloat C3V = C1V;  // copy for modification
      switch (Opcode) {
      default:
        break;
      case Instruction::FAdd:
        (void)C3V.add(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FSub:
        (void)C3V.subtract(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FMul:
        (void)C3V.multiply(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FDiv:
        (void)C3V.divide(C2V, APFloat::rmNearestTiesToEven);
        return ConstantFP::get(C1->getContext(), C3V);
      case Instruction::FRem:
        (void)C3V.mod(C2V);
        return ConstantFP::get(C1->getContext(), C3V);
      }
    }
  } else if (VectorType *VTy = dyn_cast<VectorType>(C1->getType())) {
    // Fold each element and create a vector constant from those constants.
    SmallVector<Constant*, 16> Result;
    Type *Ty = IntegerType::get(VTy->getContext(), 32);
    for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i) {
      Constant *ExtractIdx = ConstantInt::get(Ty, i);
      Constant *LHS = ConstantExpr::getExtractElement(C1, ExtractIdx);
      Constant *RHS = ConstantExpr::getExtractElement(C2, ExtractIdx);

      // If any element of a divisor vector is zero, the whole op is undef.
      if (Instruction::isIntDivRem(Opcode) && RHS->isNullValue())
        return UndefValue::get(VTy);

      Result.push_back(ConstantExpr::get(Opcode, LHS, RHS));
    }

    return ConstantVector::get(Result);
  }

  if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1)) {
    // There are many possible foldings we could do here.  We should probably
    // at least fold add of a pointer with an integer into the appropriate
    // getelementptr.  This will improve alias analysis a bit.

    // Given ((a + b) + c), if (b + c) folds to something interesting, return
    // (a + (b + c)).
    if (Instruction::isAssociative(Opcode) && CE1->getOpcode() == Opcode) {
      Constant *T = ConstantExpr::get(Opcode, CE1->getOperand(1), C2);
      if (!isa<ConstantExpr>(T) || cast<ConstantExpr>(T)->getOpcode() != Opcode)
        return ConstantExpr::get(Opcode, CE1->getOperand(0), T);
    }
  } else if (isa<ConstantExpr>(C2)) {
    // If C2 is a constant expr and C1 isn't, flop them around and fold the
    // other way if possible.
    if (Instruction::isCommutative(Opcode))
      return ConstantFoldBinaryInstruction(Opcode, C2, C1);
  }

  // i1 can be simplified in many cases.
  if (C1->getType()->isIntegerTy(1)) {
    switch (Opcode) {
    case Instruction::Add:
    case Instruction::Sub:
      return ConstantExpr::getXor(C1, C2);
    case Instruction::Mul:
      return ConstantExpr::getAnd(C1, C2);
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      // We can assume that C2 == 0.  If it were one the result would be
      // undefined because the shift value is as large as the bitwidth.
      return C1;
    case Instruction::SDiv:
    case Instruction::UDiv:
      // We can assume that C2 == 1.  If it were zero the result would be
      // undefined through division by zero.
      return C1;
    case Instruction::URem:
    case Instruction::SRem:
      // We can assume that C2 == 1.  If it were zero the result would be
      // undefined through division by zero.
      return ConstantInt::getFalse(C1->getContext());
    default:
      break;
    }
  }

  // We don't know how to fold this.
  return nullptr;
}

/// This type is zero-sized if it's an array or structure of zero-sized types.
/// The only leaf zero-sized type is an empty structure.
static bool isMaybeZeroSizedType(Type *Ty) {
  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    if (STy->isOpaque()) return true;  // Can't say.

    // If all of elements have zero size, this does too.
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i)
      if (!isMaybeZeroSizedType(STy->getElementType(i))) return false;
    return true;

  } else if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    return isMaybeZeroSizedType(ATy->getElementType());
  }
  return false;
}

/// Compare the two constants as though they were getelementptr indices.
/// This allows coercion of the types to be the same thing.
///
/// If the two constants are the "same" (after coercion), return 0.  If the
/// first is less than the second, return -1, if the second is less than the
/// first, return 1.  If the constants are not integral, return -2.
///
static int IdxCompare(Constant *C1, Constant *C2, Type *ElTy) {
  if (C1 == C2) return 0;

  // Ok, we found a different index.  If they are not ConstantInt, we can't do
  // anything with them.
  if (!isa<ConstantInt>(C1) || !isa<ConstantInt>(C2))
    return -2; // don't know!

  // We cannot compare the indices if they don't fit in an int64_t.
  if (cast<ConstantInt>(C1)->getValue().getActiveBits() > 64 ||
      cast<ConstantInt>(C2)->getValue().getActiveBits() > 64)
    return -2; // don't know!

  // Ok, we have two differing integer indices.  Sign extend them to be the same
  // type.
  int64_t C1Val = cast<ConstantInt>(C1)->getSExtValue();
  int64_t C2Val = cast<ConstantInt>(C2)->getSExtValue();

  if (C1Val == C2Val) return 0;  // They are equal

  // If the type being indexed over is really just a zero sized type, there is
  // no pointer difference being made here.
  if (isMaybeZeroSizedType(ElTy))
    return -2; // dunno.

  // If they are really different, now that they are the same type, then we
  // found a difference!
  if (C1Val < C2Val)
    return -1;
  else
    return 1;
}

/// This function determines if there is anything we can decide about the two
/// constants provided. This doesn't need to handle simple things like
/// ConstantFP comparisons, but should instead handle ConstantExprs.
/// If we can determine that the two constants have a particular relation to
/// each other, we should return the corresponding FCmpInst predicate,
/// otherwise return FCmpInst::BAD_FCMP_PREDICATE. This is used below in
/// ConstantFoldCompareInstruction.
///
/// To simplify this code we canonicalize the relation so that the first
/// operand is always the most "complex" of the two.  We consider ConstantFP
/// to be the simplest, and ConstantExprs to be the most complex.
static FCmpInst::Predicate evaluateFCmpRelation(Constant *V1, Constant *V2) {
  assert(V1->getType() == V2->getType() &&
         "Cannot compare values of different types!");

  // Handle degenerate case quickly
  if (V1 == V2) return FCmpInst::FCMP_OEQ;

  if (!isa<ConstantExpr>(V1)) {
    if (!isa<ConstantExpr>(V2)) {
      // Simple case, use the standard constant folder.
      ConstantInt *R = nullptr;
      R = dyn_cast<ConstantInt>(
                      ConstantExpr::getFCmp(FCmpInst::FCMP_OEQ, V1, V2));
      if (R && !R->isZero())
        return FCmpInst::FCMP_OEQ;
      R = dyn_cast<ConstantInt>(
                      ConstantExpr::getFCmp(FCmpInst::FCMP_OLT, V1, V2));
      if (R && !R->isZero())
        return FCmpInst::FCMP_OLT;
      R = dyn_cast<ConstantInt>(
                      ConstantExpr::getFCmp(FCmpInst::FCMP_OGT, V1, V2));
      if (R && !R->isZero())
        return FCmpInst::FCMP_OGT;

      // Nothing more we can do
      return FCmpInst::BAD_FCMP_PREDICATE;
    }

    // If the first operand is simple and second is ConstantExpr, swap operands.
    FCmpInst::Predicate SwappedRelation = evaluateFCmpRelation(V2, V1);
    if (SwappedRelation != FCmpInst::BAD_FCMP_PREDICATE)
      return FCmpInst::getSwappedPredicate(SwappedRelation);
  } else {
    // Ok, the LHS is known to be a constantexpr.  The RHS can be any of a
    // constantexpr or a simple constant.
    ConstantExpr *CE1 = cast<ConstantExpr>(V1);
    switch (CE1->getOpcode()) {
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
      // We might be able to do something with these but we don't right now.
      break;
    default:
      break;
    }
  }
  // There are MANY other foldings that we could perform here.  They will
  // probably be added on demand, as they seem needed.
  return FCmpInst::BAD_FCMP_PREDICATE;
}

static ICmpInst::Predicate areGlobalsPotentiallyEqual(const GlobalValue *GV1,
                                                      const GlobalValue *GV2) {
  auto isGlobalUnsafeForEquality = [](const GlobalValue *GV) {
    if (GV->hasExternalWeakLinkage() || GV->hasWeakAnyLinkage())
      return true;
    if (const auto *GVar = dyn_cast<GlobalVariable>(GV)) {
      Type *Ty = GVar->getValueType();
      // A global with opaque type might end up being zero sized.
      if (!Ty->isSized())
        return true;
      // A global with an empty type might lie at the address of any other
      // global.
      if (Ty->isEmptyTy())
        return true;
    }
    return false;
  };
  // Don't try to decide equality of aliases.
  if (!isa<GlobalAlias>(GV1) && !isa<GlobalAlias>(GV2))
    if (!isGlobalUnsafeForEquality(GV1) && !isGlobalUnsafeForEquality(GV2))
      return ICmpInst::ICMP_NE;
  return ICmpInst::BAD_ICMP_PREDICATE;
}

/// This function determines if there is anything we can decide about the two
/// constants provided. This doesn't need to handle simple things like integer
/// comparisons, but should instead handle ConstantExprs and GlobalValues.
/// If we can determine that the two constants have a particular relation to
/// each other, we should return the corresponding ICmp predicate, otherwise
/// return ICmpInst::BAD_ICMP_PREDICATE.
///
/// To simplify this code we canonicalize the relation so that the first
/// operand is always the most "complex" of the two.  We consider simple
/// constants (like ConstantInt) to be the simplest, followed by
/// GlobalValues, followed by ConstantExpr's (the most complex).
///
static ICmpInst::Predicate evaluateICmpRelation(Constant *V1, Constant *V2,
                                                bool isSigned) {
  assert(V1->getType() == V2->getType() &&
         "Cannot compare different types of values!");
  if (V1 == V2) return ICmpInst::ICMP_EQ;

  if (!isa<ConstantExpr>(V1) && !isa<GlobalValue>(V1) &&
      !isa<BlockAddress>(V1)) {
    if (!isa<GlobalValue>(V2) && !isa<ConstantExpr>(V2) &&
        !isa<BlockAddress>(V2)) {
      // We distilled this down to a simple case, use the standard constant
      // folder.
      ConstantInt *R = nullptr;
      ICmpInst::Predicate pred = ICmpInst::ICMP_EQ;
      R = dyn_cast<ConstantInt>(ConstantExpr::getICmp(pred, V1, V2));
      if (R && !R->isZero())
        return pred;
      pred = isSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
      R = dyn_cast<ConstantInt>(ConstantExpr::getICmp(pred, V1, V2));
      if (R && !R->isZero())
        return pred;
      pred = isSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
      R = dyn_cast<ConstantInt>(ConstantExpr::getICmp(pred, V1, V2));
      if (R && !R->isZero())
        return pred;

      // If we couldn't figure it out, bail.
      return ICmpInst::BAD_ICMP_PREDICATE;
    }

    // If the first operand is simple, swap operands.
    ICmpInst::Predicate SwappedRelation =
      evaluateICmpRelation(V2, V1, isSigned);
    if (SwappedRelation != ICmpInst::BAD_ICMP_PREDICATE)
      return ICmpInst::getSwappedPredicate(SwappedRelation);

  } else if (const GlobalValue *GV = dyn_cast<GlobalValue>(V1)) {
    if (isa<ConstantExpr>(V2)) {  // Swap as necessary.
      ICmpInst::Predicate SwappedRelation =
        evaluateICmpRelation(V2, V1, isSigned);
      if (SwappedRelation != ICmpInst::BAD_ICMP_PREDICATE)
        return ICmpInst::getSwappedPredicate(SwappedRelation);
      return ICmpInst::BAD_ICMP_PREDICATE;
    }

    // Now we know that the RHS is a GlobalValue, BlockAddress or simple
    // constant (which, since the types must match, means that it's a
    // ConstantPointerNull).
    if (const GlobalValue *GV2 = dyn_cast<GlobalValue>(V2)) {
      return areGlobalsPotentiallyEqual(GV, GV2);
    } else if (isa<BlockAddress>(V2)) {
      return ICmpInst::ICMP_NE; // Globals never equal labels.
    } else {
      assert(isa<ConstantPointerNull>(V2) && "Canonicalization guarantee!");
      // GlobalVals can never be null unless they have external weak linkage.
      // We don't try to evaluate aliases here.
      // NOTE: We should not be doing this constant folding if null pointer
      // is considered valid for the function. But currently there is no way to
      // query it from the Constant type.
      if (!GV->hasExternalWeakLinkage() && !isa<GlobalAlias>(GV) &&
          !NullPointerIsDefined(nullptr /* F */,
                                GV->getType()->getAddressSpace()))
        return ICmpInst::ICMP_NE;
    }
  } else if (const BlockAddress *BA = dyn_cast<BlockAddress>(V1)) {
    if (isa<ConstantExpr>(V2)) {  // Swap as necessary.
      ICmpInst::Predicate SwappedRelation =
        evaluateICmpRelation(V2, V1, isSigned);
      if (SwappedRelation != ICmpInst::BAD_ICMP_PREDICATE)
        return ICmpInst::getSwappedPredicate(SwappedRelation);
      return ICmpInst::BAD_ICMP_PREDICATE;
    }

    // Now we know that the RHS is a GlobalValue, BlockAddress or simple
    // constant (which, since the types must match, means that it is a
    // ConstantPointerNull).
    if (const BlockAddress *BA2 = dyn_cast<BlockAddress>(V2)) {
      // Block address in another function can't equal this one, but block
      // addresses in the current function might be the same if blocks are
      // empty.
      if (BA2->getFunction() != BA->getFunction())
        return ICmpInst::ICMP_NE;
    } else {
      // Block addresses aren't null, don't equal the address of globals.
      assert((isa<ConstantPointerNull>(V2) || isa<GlobalValue>(V2)) &&
             "Canonicalization guarantee!");
      return ICmpInst::ICMP_NE;
    }
  } else {
    // Ok, the LHS is known to be a constantexpr.  The RHS can be any of a
    // constantexpr, a global, block address, or a simple constant.
    ConstantExpr *CE1 = cast<ConstantExpr>(V1);
    Constant *CE1Op0 = CE1->getOperand(0);

    switch (CE1->getOpcode()) {
    case Instruction::Trunc:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
      break; // We can't evaluate floating point casts or truncations.

    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::BitCast:
    case Instruction::ZExt:
    case Instruction::SExt:
      // We can't evaluate floating point casts or truncations.
      if (CE1Op0->getType()->isFloatingPointTy())
        break;

      // If the cast is not actually changing bits, and the second operand is a
      // null pointer, do the comparison with the pre-casted value.
      if (V2->isNullValue() && CE1->getType()->isIntOrPtrTy()) {
        if (CE1->getOpcode() == Instruction::ZExt) isSigned = false;
        if (CE1->getOpcode() == Instruction::SExt) isSigned = true;
        return evaluateICmpRelation(CE1Op0,
                                    Constant::getNullValue(CE1Op0->getType()),
                                    isSigned);
      }
      break;

    case Instruction::GetElementPtr: {
      GEPOperator *CE1GEP = cast<GEPOperator>(CE1);
      // Ok, since this is a getelementptr, we know that the constant has a
      // pointer type.  Check the various cases.
      if (isa<ConstantPointerNull>(V2)) {
        // If we are comparing a GEP to a null pointer, check to see if the base
        // of the GEP equals the null pointer.
        if (const GlobalValue *GV = dyn_cast<GlobalValue>(CE1Op0)) {
          if (GV->hasExternalWeakLinkage())
            // Weak linkage GVals could be zero or not. We're comparing that
            // to null pointer so its greater-or-equal
            return isSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE;
          else
            // If its not weak linkage, the GVal must have a non-zero address
            // so the result is greater-than
            return isSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
        } else if (isa<ConstantPointerNull>(CE1Op0)) {
          // If we are indexing from a null pointer, check to see if we have any
          // non-zero indices.
          for (unsigned i = 1, e = CE1->getNumOperands(); i != e; ++i)
            if (!CE1->getOperand(i)->isNullValue())
              // Offsetting from null, must not be equal.
              return isSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
          // Only zero indexes from null, must still be zero.
          return ICmpInst::ICMP_EQ;
        }
        // Otherwise, we can't really say if the first operand is null or not.
      } else if (const GlobalValue *GV2 = dyn_cast<GlobalValue>(V2)) {
        if (isa<ConstantPointerNull>(CE1Op0)) {
          if (GV2->hasExternalWeakLinkage())
            // Weak linkage GVals could be zero or not. We're comparing it to
            // a null pointer, so its less-or-equal
            return isSigned ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
          else
            // If its not weak linkage, the GVal must have a non-zero address
            // so the result is less-than
            return isSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
        } else if (const GlobalValue *GV = dyn_cast<GlobalValue>(CE1Op0)) {
          if (GV == GV2) {
            // If this is a getelementptr of the same global, then it must be
            // different.  Because the types must match, the getelementptr could
            // only have at most one index, and because we fold getelementptr's
            // with a single zero index, it must be nonzero.
            assert(CE1->getNumOperands() == 2 &&
                   !CE1->getOperand(1)->isNullValue() &&
                   "Surprising getelementptr!");
            return isSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
          } else {
            if (CE1GEP->hasAllZeroIndices())
              return areGlobalsPotentiallyEqual(GV, GV2);
            return ICmpInst::BAD_ICMP_PREDICATE;
          }
        }
      } else {
        ConstantExpr *CE2 = cast<ConstantExpr>(V2);
        Constant *CE2Op0 = CE2->getOperand(0);

        // There are MANY other foldings that we could perform here.  They will
        // probably be added on demand, as they seem needed.
        switch (CE2->getOpcode()) {
        default: break;
        case Instruction::GetElementPtr:
          // By far the most common case to handle is when the base pointers are
          // obviously to the same global.
          if (isa<GlobalValue>(CE1Op0) && isa<GlobalValue>(CE2Op0)) {
            // Don't know relative ordering, but check for inequality.
            if (CE1Op0 != CE2Op0) {
              GEPOperator *CE2GEP = cast<GEPOperator>(CE2);
              if (CE1GEP->hasAllZeroIndices() && CE2GEP->hasAllZeroIndices())
                return areGlobalsPotentiallyEqual(cast<GlobalValue>(CE1Op0),
                                                  cast<GlobalValue>(CE2Op0));
              return ICmpInst::BAD_ICMP_PREDICATE;
            }
            // Ok, we know that both getelementptr instructions are based on the
            // same global.  From this, we can precisely determine the relative
            // ordering of the resultant pointers.
            unsigned i = 1;

            // The logic below assumes that the result of the comparison
            // can be determined by finding the first index that differs.
            // This doesn't work if there is over-indexing in any
            // subsequent indices, so check for that case first.
            if (!CE1->isGEPWithNoNotionalOverIndexing() ||
                !CE2->isGEPWithNoNotionalOverIndexing())
               return ICmpInst::BAD_ICMP_PREDICATE; // Might be equal.

            // Compare all of the operands the GEP's have in common.
            gep_type_iterator GTI = gep_type_begin(CE1);
            for (;i != CE1->getNumOperands() && i != CE2->getNumOperands();
                 ++i, ++GTI)
              switch (IdxCompare(CE1->getOperand(i),
                                 CE2->getOperand(i), GTI.getIndexedType())) {
              case -1: return isSigned ? ICmpInst::ICMP_SLT:ICmpInst::ICMP_ULT;
              case 1:  return isSigned ? ICmpInst::ICMP_SGT:ICmpInst::ICMP_UGT;
              case -2: return ICmpInst::BAD_ICMP_PREDICATE;
              }

            // Ok, we ran out of things they have in common.  If any leftovers
            // are non-zero then we have a difference, otherwise we are equal.
            for (; i < CE1->getNumOperands(); ++i)
              if (!CE1->getOperand(i)->isNullValue()) {
                if (isa<ConstantInt>(CE1->getOperand(i)))
                  return isSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
                else
                  return ICmpInst::BAD_ICMP_PREDICATE; // Might be equal.
              }

            for (; i < CE2->getNumOperands(); ++i)
              if (!CE2->getOperand(i)->isNullValue()) {
                if (isa<ConstantInt>(CE2->getOperand(i)))
                  return isSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
                else
                  return ICmpInst::BAD_ICMP_PREDICATE; // Might be equal.
              }
            return ICmpInst::ICMP_EQ;
          }
        }
      }
      break;
    }
    default:
      break;
    }
  }

  return ICmpInst::BAD_ICMP_PREDICATE;
}

Constant *llvm::ConstantFoldCompareInstruction(unsigned short pred,
                                               Constant *C1, Constant *C2) {
  Type *ResultTy;
  if (VectorType *VT = dyn_cast<VectorType>(C1->getType()))
    ResultTy = VectorType::get(Type::getInt1Ty(C1->getContext()),
                               VT->getNumElements());
  else
    ResultTy = Type::getInt1Ty(C1->getContext());

  // Fold FCMP_FALSE/FCMP_TRUE unconditionally.
  if (pred == FCmpInst::FCMP_FALSE)
    return Constant::getNullValue(ResultTy);

  if (pred == FCmpInst::FCMP_TRUE)
    return Constant::getAllOnesValue(ResultTy);

  // Handle some degenerate cases first
  if (isa<UndefValue>(C1) || isa<UndefValue>(C2)) {
    CmpInst::Predicate Predicate = CmpInst::Predicate(pred);
    bool isIntegerPredicate = ICmpInst::isIntPredicate(Predicate);
    // For EQ and NE, we can always pick a value for the undef to make the
    // predicate pass or fail, so we can return undef.
    // Also, if both operands are undef, we can return undef for int comparison.
    if (ICmpInst::isEquality(Predicate) || (isIntegerPredicate && C1 == C2))
      return UndefValue::get(ResultTy);

    // Otherwise, for integer compare, pick the same value as the non-undef
    // operand, and fold it to true or false.
    if (isIntegerPredicate)
      return ConstantInt::get(ResultTy, CmpInst::isTrueWhenEqual(Predicate));

    // Choosing NaN for the undef will always make unordered comparison succeed
    // and ordered comparison fails.
    return ConstantInt::get(ResultTy, CmpInst::isUnordered(Predicate));
  }

  // icmp eq/ne(null,GV) -> false/true
  if (C1->isNullValue()) {
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(C2))
      // Don't try to evaluate aliases.  External weak GV can be null.
      if (!isa<GlobalAlias>(GV) && !GV->hasExternalWeakLinkage() &&
          !NullPointerIsDefined(nullptr /* F */,
                                GV->getType()->getAddressSpace())) {
        if (pred == ICmpInst::ICMP_EQ)
          return ConstantInt::getFalse(C1->getContext());
        else if (pred == ICmpInst::ICMP_NE)
          return ConstantInt::getTrue(C1->getContext());
      }
  // icmp eq/ne(GV,null) -> false/true
  } else if (C2->isNullValue()) {
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(C1))
      // Don't try to evaluate aliases.  External weak GV can be null.
      if (!isa<GlobalAlias>(GV) && !GV->hasExternalWeakLinkage() &&
          !NullPointerIsDefined(nullptr /* F */,
                                GV->getType()->getAddressSpace())) {
        if (pred == ICmpInst::ICMP_EQ)
          return ConstantInt::getFalse(C1->getContext());
        else if (pred == ICmpInst::ICMP_NE)
          return ConstantInt::getTrue(C1->getContext());
      }
  }

  // If the comparison is a comparison between two i1's, simplify it.
  if (C1->getType()->isIntegerTy(1)) {
    switch(pred) {
    case ICmpInst::ICMP_EQ:
      if (isa<ConstantInt>(C2))
        return ConstantExpr::getXor(C1, ConstantExpr::getNot(C2));
      return ConstantExpr::getXor(ConstantExpr::getNot(C1), C2);
    case ICmpInst::ICMP_NE:
      return ConstantExpr::getXor(C1, C2);
    default:
      break;
    }
  }

  if (isa<ConstantInt>(C1) && isa<ConstantInt>(C2)) {
    const APInt &V1 = cast<ConstantInt>(C1)->getValue();
    const APInt &V2 = cast<ConstantInt>(C2)->getValue();
    switch (pred) {
    default: llvm_unreachable("Invalid ICmp Predicate");
    case ICmpInst::ICMP_EQ:  return ConstantInt::get(ResultTy, V1 == V2);
    case ICmpInst::ICMP_NE:  return ConstantInt::get(ResultTy, V1 != V2);
    case ICmpInst::ICMP_SLT: return ConstantInt::get(ResultTy, V1.slt(V2));
    case ICmpInst::ICMP_SGT: return ConstantInt::get(ResultTy, V1.sgt(V2));
    case ICmpInst::ICMP_SLE: return ConstantInt::get(ResultTy, V1.sle(V2));
    case ICmpInst::ICMP_SGE: return ConstantInt::get(ResultTy, V1.sge(V2));
    case ICmpInst::ICMP_ULT: return ConstantInt::get(ResultTy, V1.ult(V2));
    case ICmpInst::ICMP_UGT: return ConstantInt::get(ResultTy, V1.ugt(V2));
    case ICmpInst::ICMP_ULE: return ConstantInt::get(ResultTy, V1.ule(V2));
    case ICmpInst::ICMP_UGE: return ConstantInt::get(ResultTy, V1.uge(V2));
    }
  } else if (isa<ConstantFP>(C1) && isa<ConstantFP>(C2)) {
    const APFloat &C1V = cast<ConstantFP>(C1)->getValueAPF();
    const APFloat &C2V = cast<ConstantFP>(C2)->getValueAPF();
    APFloat::cmpResult R = C1V.compare(C2V);
    switch (pred) {
    default: llvm_unreachable("Invalid FCmp Predicate");
    case FCmpInst::FCMP_FALSE: return Constant::getNullValue(ResultTy);
    case FCmpInst::FCMP_TRUE:  return Constant::getAllOnesValue(ResultTy);
    case FCmpInst::FCMP_UNO:
      return ConstantInt::get(ResultTy, R==APFloat::cmpUnordered);
    case FCmpInst::FCMP_ORD:
      return ConstantInt::get(ResultTy, R!=APFloat::cmpUnordered);
    case FCmpInst::FCMP_UEQ:
      return ConstantInt::get(ResultTy, R==APFloat::cmpUnordered ||
                                        R==APFloat::cmpEqual);
    case FCmpInst::FCMP_OEQ:
      return ConstantInt::get(ResultTy, R==APFloat::cmpEqual);
    case FCmpInst::FCMP_UNE:
      return ConstantInt::get(ResultTy, R!=APFloat::cmpEqual);
    case FCmpInst::FCMP_ONE:
      return ConstantInt::get(ResultTy, R==APFloat::cmpLessThan ||
                                        R==APFloat::cmpGreaterThan);
    case FCmpInst::FCMP_ULT:
      return ConstantInt::get(ResultTy, R==APFloat::cmpUnordered ||
                                        R==APFloat::cmpLessThan);
    case FCmpInst::FCMP_OLT:
      return ConstantInt::get(ResultTy, R==APFloat::cmpLessThan);
    case FCmpInst::FCMP_UGT:
      return ConstantInt::get(ResultTy, R==APFloat::cmpUnordered ||
                                        R==APFloat::cmpGreaterThan);
    case FCmpInst::FCMP_OGT:
      return ConstantInt::get(ResultTy, R==APFloat::cmpGreaterThan);
    case FCmpInst::FCMP_ULE:
      return ConstantInt::get(ResultTy, R!=APFloat::cmpGreaterThan);
    case FCmpInst::FCMP_OLE:
      return ConstantInt::get(ResultTy, R==APFloat::cmpLessThan ||
                                        R==APFloat::cmpEqual);
    case FCmpInst::FCMP_UGE:
      return ConstantInt::get(ResultTy, R!=APFloat::cmpLessThan);
    case FCmpInst::FCMP_OGE:
      return ConstantInt::get(ResultTy, R==APFloat::cmpGreaterThan ||
                                        R==APFloat::cmpEqual);
    }
  } else if (C1->getType()->isVectorTy()) {
    // If we can constant fold the comparison of each element, constant fold
    // the whole vector comparison.
    SmallVector<Constant*, 4> ResElts;
    Type *Ty = IntegerType::get(C1->getContext(), 32);
    // Compare the elements, producing an i1 result or constant expr.
    for (unsigned i = 0, e = C1->getType()->getVectorNumElements(); i != e;++i){
      Constant *C1E =
        ConstantExpr::getExtractElement(C1, ConstantInt::get(Ty, i));
      Constant *C2E =
        ConstantExpr::getExtractElement(C2, ConstantInt::get(Ty, i));

      ResElts.push_back(ConstantExpr::getCompare(pred, C1E, C2E));
    }

    return ConstantVector::get(ResElts);
  }

  if (C1->getType()->isFloatingPointTy() &&
      // Only call evaluateFCmpRelation if we have a constant expr to avoid
      // infinite recursive loop
      (isa<ConstantExpr>(C1) || isa<ConstantExpr>(C2))) {
    int Result = -1;  // -1 = unknown, 0 = known false, 1 = known true.
    switch (evaluateFCmpRelation(C1, C2)) {
    default: llvm_unreachable("Unknown relation!");
    case FCmpInst::FCMP_UNO:
    case FCmpInst::FCMP_ORD:
    case FCmpInst::FCMP_UEQ:
    case FCmpInst::FCMP_UNE:
    case FCmpInst::FCMP_ULT:
    case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_ULE:
    case FCmpInst::FCMP_UGE:
    case FCmpInst::FCMP_TRUE:
    case FCmpInst::FCMP_FALSE:
    case FCmpInst::BAD_FCMP_PREDICATE:
      break; // Couldn't determine anything about these constants.
    case FCmpInst::FCMP_OEQ: // We know that C1 == C2
      Result = (pred == FCmpInst::FCMP_UEQ || pred == FCmpInst::FCMP_OEQ ||
                pred == FCmpInst::FCMP_ULE || pred == FCmpInst::FCMP_OLE ||
                pred == FCmpInst::FCMP_UGE || pred == FCmpInst::FCMP_OGE);
      break;
    case FCmpInst::FCMP_OLT: // We know that C1 < C2
      Result = (pred == FCmpInst::FCMP_UNE || pred == FCmpInst::FCMP_ONE ||
                pred == FCmpInst::FCMP_ULT || pred == FCmpInst::FCMP_OLT ||
                pred == FCmpInst::FCMP_ULE || pred == FCmpInst::FCMP_OLE);
      break;
    case FCmpInst::FCMP_OGT: // We know that C1 > C2
      Result = (pred == FCmpInst::FCMP_UNE || pred == FCmpInst::FCMP_ONE ||
                pred == FCmpInst::FCMP_UGT || pred == FCmpInst::FCMP_OGT ||
                pred == FCmpInst::FCMP_UGE || pred == FCmpInst::FCMP_OGE);
      break;
    case FCmpInst::FCMP_OLE: // We know that C1 <= C2
      // We can only partially decide this relation.
      if (pred == FCmpInst::FCMP_UGT || pred == FCmpInst::FCMP_OGT)
        Result = 0;
      else if (pred == FCmpInst::FCMP_ULT || pred == FCmpInst::FCMP_OLT)
        Result = 1;
      break;
    case FCmpInst::FCMP_OGE: // We known that C1 >= C2
      // We can only partially decide this relation.
      if (pred == FCmpInst::FCMP_ULT || pred == FCmpInst::FCMP_OLT)
        Result = 0;
      else if (pred == FCmpInst::FCMP_UGT || pred == FCmpInst::FCMP_OGT)
        Result = 1;
      break;
    case FCmpInst::FCMP_ONE: // We know that C1 != C2
      // We can only partially decide this relation.
      if (pred == FCmpInst::FCMP_OEQ || pred == FCmpInst::FCMP_UEQ)
        Result = 0;
      else if (pred == FCmpInst::FCMP_ONE || pred == FCmpInst::FCMP_UNE)
        Result = 1;
      break;
    }

    // If we evaluated the result, return it now.
    if (Result != -1)
      return ConstantInt::get(ResultTy, Result);

  } else {
    // Evaluate the relation between the two constants, per the predicate.
    int Result = -1;  // -1 = unknown, 0 = known false, 1 = known true.
    switch (evaluateICmpRelation(C1, C2,
                                 CmpInst::isSigned((CmpInst::Predicate)pred))) {
    default: llvm_unreachable("Unknown relational!");
    case ICmpInst::BAD_ICMP_PREDICATE:
      break;  // Couldn't determine anything about these constants.
    case ICmpInst::ICMP_EQ:   // We know the constants are equal!
      // If we know the constants are equal, we can decide the result of this
      // computation precisely.
      Result = ICmpInst::isTrueWhenEqual((ICmpInst::Predicate)pred);
      break;
    case ICmpInst::ICMP_ULT:
      switch (pred) {
      case ICmpInst::ICMP_ULT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_ULE:
        Result = 1; break;
      case ICmpInst::ICMP_UGT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_UGE:
        Result = 0; break;
      }
      break;
    case ICmpInst::ICMP_SLT:
      switch (pred) {
      case ICmpInst::ICMP_SLT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_SLE:
        Result = 1; break;
      case ICmpInst::ICMP_SGT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_SGE:
        Result = 0; break;
      }
      break;
    case ICmpInst::ICMP_UGT:
      switch (pred) {
      case ICmpInst::ICMP_UGT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_UGE:
        Result = 1; break;
      case ICmpInst::ICMP_ULT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_ULE:
        Result = 0; break;
      }
      break;
    case ICmpInst::ICMP_SGT:
      switch (pred) {
      case ICmpInst::ICMP_SGT: case ICmpInst::ICMP_NE: case ICmpInst::ICMP_SGE:
        Result = 1; break;
      case ICmpInst::ICMP_SLT: case ICmpInst::ICMP_EQ: case ICmpInst::ICMP_SLE:
        Result = 0; break;
      }
      break;
    case ICmpInst::ICMP_ULE:
      if (pred == ICmpInst::ICMP_UGT) Result = 0;
      if (pred == ICmpInst::ICMP_ULT || pred == ICmpInst::ICMP_ULE) Result = 1;
      break;
    case ICmpInst::ICMP_SLE:
      if (pred == ICmpInst::ICMP_SGT) Result = 0;
      if (pred == ICmpInst::ICMP_SLT || pred == ICmpInst::ICMP_SLE) Result = 1;
      break;
    case ICmpInst::ICMP_UGE:
      if (pred == ICmpInst::ICMP_ULT) Result = 0;
      if (pred == ICmpInst::ICMP_UGT || pred == ICmpInst::ICMP_UGE) Result = 1;
      break;
    case ICmpInst::ICMP_SGE:
      if (pred == ICmpInst::ICMP_SLT) Result = 0;
      if (pred == ICmpInst::ICMP_SGT || pred == ICmpInst::ICMP_SGE) Result = 1;
      break;
    case ICmpInst::ICMP_NE:
      if (pred == ICmpInst::ICMP_EQ) Result = 0;
      if (pred == ICmpInst::ICMP_NE) Result = 1;
      break;
    }

    // If we evaluated the result, return it now.
    if (Result != -1)
      return ConstantInt::get(ResultTy, Result);

    // If the right hand side is a bitcast, try using its inverse to simplify
    // it by moving it to the left hand side.  We can't do this if it would turn
    // a vector compare into a scalar compare or visa versa.
    if (ConstantExpr *CE2 = dyn_cast<ConstantExpr>(C2)) {
      Constant *CE2Op0 = CE2->getOperand(0);
      if (CE2->getOpcode() == Instruction::BitCast &&
          CE2->getType()->isVectorTy() == CE2Op0->getType()->isVectorTy()) {
        Constant *Inverse = ConstantExpr::getBitCast(C1, CE2Op0->getType());
        return ConstantExpr::getICmp(pred, Inverse, CE2Op0);
      }
    }

    // If the left hand side is an extension, try eliminating it.
    if (ConstantExpr *CE1 = dyn_cast<ConstantExpr>(C1)) {
      if ((CE1->getOpcode() == Instruction::SExt &&
           ICmpInst::isSigned((ICmpInst::Predicate)pred)) ||
          (CE1->getOpcode() == Instruction::ZExt &&
           !ICmpInst::isSigned((ICmpInst::Predicate)pred))){
        Constant *CE1Op0 = CE1->getOperand(0);
        Constant *CE1Inverse = ConstantExpr::getTrunc(CE1, CE1Op0->getType());
        if (CE1Inverse == CE1Op0) {
          // Check whether we can safely truncate the right hand side.
          Constant *C2Inverse = ConstantExpr::getTrunc(C2, CE1Op0->getType());
          if (ConstantExpr::getCast(CE1->getOpcode(), C2Inverse,
                                    C2->getType()) == C2)
            return ConstantExpr::getICmp(pred, CE1Inverse, C2Inverse);
        }
      }
    }

    if ((!isa<ConstantExpr>(C1) && isa<ConstantExpr>(C2)) ||
        (C1->isNullValue() && !C2->isNullValue())) {
      // If C2 is a constant expr and C1 isn't, flip them around and fold the
      // other way if possible.
      // Also, if C1 is null and C2 isn't, flip them around.
      pred = ICmpInst::getSwappedPredicate((ICmpInst::Predicate)pred);
      return ConstantExpr::getICmp(pred, C2, C1);
    }
  }
  return nullptr;
}

/// Test whether the given sequence of *normalized* indices is "inbounds".
template<typename IndexTy>
static bool isInBoundsIndices(ArrayRef<IndexTy> Idxs) {
  // No indices means nothing that could be out of bounds.
  if (Idxs.empty()) return true;

  // If the first index is zero, it's in bounds.
  if (cast<Constant>(Idxs[0])->isNullValue()) return true;

  // If the first index is one and all the rest are zero, it's in bounds,
  // by the one-past-the-end rule.
  if (auto *CI = dyn_cast<ConstantInt>(Idxs[0])) {
    if (!CI->isOne())
      return false;
  } else {
    auto *CV = cast<ConstantDataVector>(Idxs[0]);
    CI = dyn_cast_or_null<ConstantInt>(CV->getSplatValue());
    if (!CI || !CI->isOne())
      return false;
  }

  for (unsigned i = 1, e = Idxs.size(); i != e; ++i)
    if (!cast<Constant>(Idxs[i])->isNullValue())
      return false;
  return true;
}

/// Test whether a given ConstantInt is in-range for a SequentialType.
static bool isIndexInRangeOfArrayType(uint64_t NumElements,
                                      const ConstantInt *CI) {
  // We cannot bounds check the index if it doesn't fit in an int64_t.
  if (CI->getValue().getMinSignedBits() > 64)
    return false;

  // A negative index or an index past the end of our sequential type is
  // considered out-of-range.
  int64_t IndexVal = CI->getSExtValue();
  if (IndexVal < 0 || (NumElements > 0 && (uint64_t)IndexVal >= NumElements))
    return false;

  // Otherwise, it is in-range.
  return true;
}

Constant *llvm::ConstantFoldGetElementPtr(Type *PointeeTy, Constant *C,
                                          bool InBounds,
                                          Optional<unsigned> InRangeIndex,
                                          ArrayRef<Value *> Idxs) {
  if (Idxs.empty()) return C;

  Type *GEPTy = GetElementPtrInst::getGEPReturnType(
      C, makeArrayRef((Value *const *)Idxs.data(), Idxs.size()));

  if (isa<UndefValue>(C))
    return UndefValue::get(GEPTy);

  Constant *Idx0 = cast<Constant>(Idxs[0]);
  if (Idxs.size() == 1 && (Idx0->isNullValue() || isa<UndefValue>(Idx0)))
    return GEPTy->isVectorTy() && !C->getType()->isVectorTy()
               ? ConstantVector::getSplat(
                     cast<VectorType>(GEPTy)->getNumElements(), C)
               : C;

  if (C->isNullValue()) {
    bool isNull = true;
    for (unsigned i = 0, e = Idxs.size(); i != e; ++i)
      if (!isa<UndefValue>(Idxs[i]) &&
          !cast<Constant>(Idxs[i])->isNullValue()) {
        isNull = false;
        break;
      }
    if (isNull) {
      PointerType *PtrTy = cast<PointerType>(C->getType()->getScalarType());
      Type *Ty = GetElementPtrInst::getIndexedType(PointeeTy, Idxs);

      assert(Ty && "Invalid indices for GEP!");
      Type *OrigGEPTy = PointerType::get(Ty, PtrTy->getAddressSpace());
      Type *GEPTy = PointerType::get(Ty, PtrTy->getAddressSpace());
      if (VectorType *VT = dyn_cast<VectorType>(C->getType()))
        GEPTy = VectorType::get(OrigGEPTy, VT->getNumElements());

      // The GEP returns a vector of pointers when one of more of
      // its arguments is a vector.
      for (unsigned i = 0, e = Idxs.size(); i != e; ++i) {
        if (auto *VT = dyn_cast<VectorType>(Idxs[i]->getType())) {
          GEPTy = VectorType::get(OrigGEPTy, VT->getNumElements());
          break;
        }
      }

      return Constant::getNullValue(GEPTy);
    }
  }

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    // Combine Indices - If the source pointer to this getelementptr instruction
    // is a getelementptr instruction, combine the indices of the two
    // getelementptr instructions into a single instruction.
    //
    if (CE->getOpcode() == Instruction::GetElementPtr) {
      gep_type_iterator LastI = gep_type_end(CE);
      for (gep_type_iterator I = gep_type_begin(CE), E = gep_type_end(CE);
           I != E; ++I)
        LastI = I;

      // We cannot combine indices if doing so would take us outside of an
      // array or vector.  Doing otherwise could trick us if we evaluated such a
      // GEP as part of a load.
      //
      // e.g. Consider if the original GEP was:
      // i8* getelementptr ({ [2 x i8], i32, i8, [3 x i8] }* @main.c,
      //                    i32 0, i32 0, i64 0)
      //
      // If we then tried to offset it by '8' to get to the third element,
      // an i8, we should *not* get:
      // i8* getelementptr ({ [2 x i8], i32, i8, [3 x i8] }* @main.c,
      //                    i32 0, i32 0, i64 8)
      //
      // This GEP tries to index array element '8  which runs out-of-bounds.
      // Subsequent evaluation would get confused and produce erroneous results.
      //
      // The following prohibits such a GEP from being formed by checking to see
      // if the index is in-range with respect to an array.
      // TODO: This code may be extended to handle vectors as well.
      bool PerformFold = false;
      if (Idx0->isNullValue())
        PerformFold = true;
      else if (LastI.isSequential())
        if (ConstantInt *CI = dyn_cast<ConstantInt>(Idx0))
          PerformFold = (!LastI.isBoundedSequential() ||
                         isIndexInRangeOfArrayType(
                             LastI.getSequentialNumElements(), CI)) &&
                        !CE->getOperand(CE->getNumOperands() - 1)
                             ->getType()
                             ->isVectorTy();

      if (PerformFold) {
        SmallVector<Value*, 16> NewIndices;
        NewIndices.reserve(Idxs.size() + CE->getNumOperands());
        NewIndices.append(CE->op_begin() + 1, CE->op_end() - 1);

        // Add the last index of the source with the first index of the new GEP.
        // Make sure to handle the case when they are actually different types.
        Constant *Combined = CE->getOperand(CE->getNumOperands()-1);
        // Otherwise it must be an array.
        if (!Idx0->isNullValue()) {
          Type *IdxTy = Combined->getType();
          if (IdxTy != Idx0->getType()) {
            unsigned CommonExtendedWidth =
                std::max(IdxTy->getIntegerBitWidth(),
                         Idx0->getType()->getIntegerBitWidth());
            CommonExtendedWidth = std::max(CommonExtendedWidth, 64U);

            Type *CommonTy =
                Type::getIntNTy(IdxTy->getContext(), CommonExtendedWidth);
            Constant *C1 = ConstantExpr::getSExtOrBitCast(Idx0, CommonTy);
            Constant *C2 = ConstantExpr::getSExtOrBitCast(Combined, CommonTy);
            Combined = ConstantExpr::get(Instruction::Add, C1, C2);
          } else {
            Combined =
              ConstantExpr::get(Instruction::Add, Idx0, Combined);
          }
        }

        NewIndices.push_back(Combined);
        NewIndices.append(Idxs.begin() + 1, Idxs.end());

        // The combined GEP normally inherits its index inrange attribute from
        // the inner GEP, but if the inner GEP's last index was adjusted by the
        // outer GEP, any inbounds attribute on that index is invalidated.
        Optional<unsigned> IRIndex = cast<GEPOperator>(CE)->getInRangeIndex();
        if (IRIndex && *IRIndex == CE->getNumOperands() - 2 && !Idx0->isNullValue())
          IRIndex = None;

        return ConstantExpr::getGetElementPtr(
            cast<GEPOperator>(CE)->getSourceElementType(), CE->getOperand(0),
            NewIndices, InBounds && cast<GEPOperator>(CE)->isInBounds(),
            IRIndex);
      }
    }

    // Attempt to fold casts to the same type away.  For example, folding:
    //
    //   i32* getelementptr ([2 x i32]* bitcast ([3 x i32]* %X to [2 x i32]*),
    //                       i64 0, i64 0)
    // into:
    //
    //   i32* getelementptr ([3 x i32]* %X, i64 0, i64 0)
    //
    // Don't fold if the cast is changing address spaces.
    if (CE->isCast() && Idxs.size() > 1 && Idx0->isNullValue()) {
      PointerType *SrcPtrTy =
        dyn_cast<PointerType>(CE->getOperand(0)->getType());
      PointerType *DstPtrTy = dyn_cast<PointerType>(CE->getType());
      if (SrcPtrTy && DstPtrTy) {
        ArrayType *SrcArrayTy =
          dyn_cast<ArrayType>(SrcPtrTy->getElementType());
        ArrayType *DstArrayTy =
          dyn_cast<ArrayType>(DstPtrTy->getElementType());
        if (SrcArrayTy && DstArrayTy
            && SrcArrayTy->getElementType() == DstArrayTy->getElementType()
            && SrcPtrTy->getAddressSpace() == DstPtrTy->getAddressSpace())
          return ConstantExpr::getGetElementPtr(SrcArrayTy,
                                                (Constant *)CE->getOperand(0),
                                                Idxs, InBounds, InRangeIndex);
      }
    }
  }

  // Check to see if any array indices are not within the corresponding
  // notional array or vector bounds. If so, try to determine if they can be
  // factored out into preceding dimensions.
  SmallVector<Constant *, 8> NewIdxs;
  Type *Ty = PointeeTy;
  Type *Prev = C->getType();
  bool Unknown =
      !isa<ConstantInt>(Idxs[0]) && !isa<ConstantDataVector>(Idxs[0]);
  for (unsigned i = 1, e = Idxs.size(); i != e;
       Prev = Ty, Ty = cast<CompositeType>(Ty)->getTypeAtIndex(Idxs[i]), ++i) {
    if (!isa<ConstantInt>(Idxs[i]) && !isa<ConstantDataVector>(Idxs[i])) {
      // We don't know if it's in range or not.
      Unknown = true;
      continue;
    }
    if (!isa<ConstantInt>(Idxs[i - 1]) && !isa<ConstantDataVector>(Idxs[i - 1]))
      // Skip if the type of the previous index is not supported.
      continue;
    if (InRangeIndex && i == *InRangeIndex + 1) {
      // If an index is marked inrange, we cannot apply this canonicalization to
      // the following index, as that will cause the inrange index to point to
      // the wrong element.
      continue;
    }
    if (isa<StructType>(Ty)) {
      // The verify makes sure that GEPs into a struct are in range.
      continue;
    }
    auto *STy = cast<SequentialType>(Ty);
    if (isa<VectorType>(STy)) {
      // There can be awkward padding in after a non-power of two vector.
      Unknown = true;
      continue;
    }
    if (ConstantInt *CI = dyn_cast<ConstantInt>(Idxs[i])) {
      if (isIndexInRangeOfArrayType(STy->getNumElements(), CI))
        // It's in range, skip to the next index.
        continue;
      if (CI->getSExtValue() < 0) {
        // It's out of range and negative, don't try to factor it.
        Unknown = true;
        continue;
      }
    } else {
      auto *CV = cast<ConstantDataVector>(Idxs[i]);
      bool InRange = true;
      for (unsigned I = 0, E = CV->getNumElements(); I != E; ++I) {
        auto *CI = cast<ConstantInt>(CV->getElementAsConstant(I));
        InRange &= isIndexInRangeOfArrayType(STy->getNumElements(), CI);
        if (CI->getSExtValue() < 0) {
          Unknown = true;
          break;
        }
      }
      if (InRange || Unknown)
        // It's in range, skip to the next index.
        // It's out of range and negative, don't try to factor it.
        continue;
    }
    if (isa<StructType>(Prev)) {
      // It's out of range, but the prior dimension is a struct
      // so we can't do anything about it.
      Unknown = true;
      continue;
    }
    // It's out of range, but we can factor it into the prior
    // dimension.
    NewIdxs.resize(Idxs.size());
    // Determine the number of elements in our sequential type.
    uint64_t NumElements = STy->getArrayNumElements();

    // Expand the current index or the previous index to a vector from a scalar
    // if necessary.
    Constant *CurrIdx = cast<Constant>(Idxs[i]);
    auto *PrevIdx =
        NewIdxs[i - 1] ? NewIdxs[i - 1] : cast<Constant>(Idxs[i - 1]);
    bool IsCurrIdxVector = CurrIdx->getType()->isVectorTy();
    bool IsPrevIdxVector = PrevIdx->getType()->isVectorTy();
    bool UseVector = IsCurrIdxVector || IsPrevIdxVector;

    if (!IsCurrIdxVector && IsPrevIdxVector)
      CurrIdx = ConstantDataVector::getSplat(
          PrevIdx->getType()->getVectorNumElements(), CurrIdx);

    if (!IsPrevIdxVector && IsCurrIdxVector)
      PrevIdx = ConstantDataVector::getSplat(
          CurrIdx->getType()->getVectorNumElements(), PrevIdx);

    Constant *Factor =
        ConstantInt::get(CurrIdx->getType()->getScalarType(), NumElements);
    if (UseVector)
      Factor = ConstantDataVector::getSplat(
          IsPrevIdxVector ? PrevIdx->getType()->getVectorNumElements()
                          : CurrIdx->getType()->getVectorNumElements(),
          Factor);

    NewIdxs[i] = ConstantExpr::getSRem(CurrIdx, Factor);

    Constant *Div = ConstantExpr::getSDiv(CurrIdx, Factor);

    unsigned CommonExtendedWidth =
        std::max(PrevIdx->getType()->getScalarSizeInBits(),
                 Div->getType()->getScalarSizeInBits());
    CommonExtendedWidth = std::max(CommonExtendedWidth, 64U);

    // Before adding, extend both operands to i64 to avoid
    // overflow trouble.
    Type *ExtendedTy = Type::getIntNTy(Div->getContext(), CommonExtendedWidth);
    if (UseVector)
      ExtendedTy = VectorType::get(
          ExtendedTy, IsPrevIdxVector
                          ? PrevIdx->getType()->getVectorNumElements()
                          : CurrIdx->getType()->getVectorNumElements());

    if (!PrevIdx->getType()->isIntOrIntVectorTy(CommonExtendedWidth))
      PrevIdx = ConstantExpr::getSExt(PrevIdx, ExtendedTy);

    if (!Div->getType()->isIntOrIntVectorTy(CommonExtendedWidth))
      Div = ConstantExpr::getSExt(Div, ExtendedTy);

    NewIdxs[i - 1] = ConstantExpr::getAdd(PrevIdx, Div);
  }

  // If we did any factoring, start over with the adjusted indices.
  if (!NewIdxs.empty()) {
    for (unsigned i = 0, e = Idxs.size(); i != e; ++i)
      if (!NewIdxs[i]) NewIdxs[i] = cast<Constant>(Idxs[i]);
    return ConstantExpr::getGetElementPtr(PointeeTy, C, NewIdxs, InBounds,
                                          InRangeIndex);
  }

  // If all indices are known integers and normalized, we can do a simple
  // check for the "inbounds" property.
  if (!Unknown && !InBounds)
    if (auto *GV = dyn_cast<GlobalVariable>(C))
      if (!GV->hasExternalWeakLinkage() && isInBoundsIndices(Idxs))
        return ConstantExpr::getGetElementPtr(PointeeTy, C, Idxs,
                                              /*InBounds=*/true, InRangeIndex);

  return nullptr;
}
