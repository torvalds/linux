//===- llvm/InstrTypes.h - Important Instruction subclasses -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines various meta classes of instructions that exist in the VM
// representation.  Specific concrete subclasses of these may be found in the
// i*.h files...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRTYPES_H
#define LLVM_IR_INSTRTYPES_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace llvm {

namespace Intrinsic {
enum ID : unsigned;
}

//===----------------------------------------------------------------------===//
//                          UnaryInstruction Class
//===----------------------------------------------------------------------===//

class UnaryInstruction : public Instruction {
protected:
  UnaryInstruction(Type *Ty, unsigned iType, Value *V,
                   Instruction *IB = nullptr)
    : Instruction(Ty, iType, &Op<0>(), 1, IB) {
    Op<0>() = V;
  }
  UnaryInstruction(Type *Ty, unsigned iType, Value *V, BasicBlock *IAE)
    : Instruction(Ty, iType, &Op<0>(), 1, IAE) {
    Op<0>() = V;
  }

public:
  // allocate space for exactly one operand
  void *operator new(size_t s) {
    return User::operator new(s, 1);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Alloca ||
           I->getOpcode() == Instruction::Load ||
           I->getOpcode() == Instruction::VAArg ||
           I->getOpcode() == Instruction::ExtractValue ||
           (I->getOpcode() >= CastOpsBegin && I->getOpcode() < CastOpsEnd);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<UnaryInstruction> :
  public FixedNumOperandTraits<UnaryInstruction, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(UnaryInstruction, Value)

//===----------------------------------------------------------------------===//
//                           BinaryOperator Class
//===----------------------------------------------------------------------===//

class BinaryOperator : public Instruction {
  void AssertOK();

protected:
  BinaryOperator(BinaryOps iType, Value *S1, Value *S2, Type *Ty,
                 const Twine &Name, Instruction *InsertBefore);
  BinaryOperator(BinaryOps iType, Value *S1, Value *S2, Type *Ty,
                 const Twine &Name, BasicBlock *InsertAtEnd);

  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  BinaryOperator *cloneImpl() const;

public:
  // allocate space for exactly two operands
  void *operator new(size_t s) {
    return User::operator new(s, 2);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Construct a binary instruction, given the opcode and the two
  /// operands.  Optionally (if InstBefore is specified) insert the instruction
  /// into a BasicBlock right before the specified instruction.  The specified
  /// Instruction is allowed to be a dereferenced end iterator.
  ///
  static BinaryOperator *Create(BinaryOps Op, Value *S1, Value *S2,
                                const Twine &Name = Twine(),
                                Instruction *InsertBefore = nullptr);

  /// Construct a binary instruction, given the opcode and the two
  /// operands.  Also automatically insert this instruction to the end of the
  /// BasicBlock specified.
  ///
  static BinaryOperator *Create(BinaryOps Op, Value *S1, Value *S2,
                                const Twine &Name, BasicBlock *InsertAtEnd);

  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name = "") {\
    return Create(Instruction::OPC, V1, V2, Name);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name, BasicBlock *BB) {\
    return Create(Instruction::OPC, V1, V2, Name, BB);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_BINARY_INST(N, OPC, CLASS) \
  static BinaryOperator *Create##OPC(Value *V1, Value *V2, \
                                     const Twine &Name, Instruction *I) {\
    return Create(Instruction::OPC, V1, V2, Name, I);\
  }
#include "llvm/IR/Instruction.def"

  static BinaryOperator *CreateWithCopiedFlags(BinaryOps Opc,
                                               Value *V1, Value *V2,
                                               BinaryOperator *CopyBO,
                                               const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->copyIRFlags(CopyBO);
    return BO;
  }

  static BinaryOperator *CreateFAddFMF(Value *V1, Value *V2,
                                       BinaryOperator *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FAdd, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFSubFMF(Value *V1, Value *V2,
                                       BinaryOperator *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FSub, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFMulFMF(Value *V1, Value *V2,
                                       BinaryOperator *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FMul, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFDivFMF(Value *V1, Value *V2,
                                       BinaryOperator *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FDiv, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFRemFMF(Value *V1, Value *V2,
                                       BinaryOperator *FMFSource,
                                       const Twine &Name = "") {
    return CreateWithCopiedFlags(Instruction::FRem, V1, V2, FMFSource, Name);
  }
  static BinaryOperator *CreateFNegFMF(Value *Op, BinaryOperator *FMFSource,
                                       const Twine &Name = "") {
    Value *Zero = ConstantFP::getNegativeZero(Op->getType());
    return CreateWithCopiedFlags(Instruction::FSub, Zero, Op, FMFSource);
  }

  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setHasNoSignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, BasicBlock *BB) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
    BO->setHasNoSignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNSW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, Instruction *I) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
    BO->setHasNoSignedWrap(true);
    return BO;
  }

  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, BasicBlock *BB) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }
  static BinaryOperator *CreateNUW(BinaryOps Opc, Value *V1, Value *V2,
                                   const Twine &Name, Instruction *I) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
    BO->setHasNoUnsignedWrap(true);
    return BO;
  }

  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name = "") {
    BinaryOperator *BO = Create(Opc, V1, V2, Name);
    BO->setIsExact(true);
    return BO;
  }
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name, BasicBlock *BB) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, BB);
    BO->setIsExact(true);
    return BO;
  }
  static BinaryOperator *CreateExact(BinaryOps Opc, Value *V1, Value *V2,
                                     const Twine &Name, Instruction *I) {
    BinaryOperator *BO = Create(Opc, V1, V2, Name, I);
    BO->setIsExact(true);
    return BO;
  }

#define DEFINE_HELPERS(OPC, NUWNSWEXACT)                                       \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(Value *V1, Value *V2,        \
                                                  const Twine &Name = "") {    \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name);                \
  }                                                                            \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(                             \
      Value *V1, Value *V2, const Twine &Name, BasicBlock *BB) {               \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name, BB);            \
  }                                                                            \
  static BinaryOperator *Create##NUWNSWEXACT##OPC(                             \
      Value *V1, Value *V2, const Twine &Name, Instruction *I) {               \
    return Create##NUWNSWEXACT(Instruction::OPC, V1, V2, Name, I);             \
  }

  DEFINE_HELPERS(Add, NSW) // CreateNSWAdd
  DEFINE_HELPERS(Add, NUW) // CreateNUWAdd
  DEFINE_HELPERS(Sub, NSW) // CreateNSWSub
  DEFINE_HELPERS(Sub, NUW) // CreateNUWSub
  DEFINE_HELPERS(Mul, NSW) // CreateNSWMul
  DEFINE_HELPERS(Mul, NUW) // CreateNUWMul
  DEFINE_HELPERS(Shl, NSW) // CreateNSWShl
  DEFINE_HELPERS(Shl, NUW) // CreateNUWShl

  DEFINE_HELPERS(SDiv, Exact)  // CreateExactSDiv
  DEFINE_HELPERS(UDiv, Exact)  // CreateExactUDiv
  DEFINE_HELPERS(AShr, Exact)  // CreateExactAShr
  DEFINE_HELPERS(LShr, Exact)  // CreateExactLShr

#undef DEFINE_HELPERS

  /// Helper functions to construct and inspect unary operations (NEG and NOT)
  /// via binary operators SUB and XOR:
  ///
  /// Create the NEG and NOT instructions out of SUB and XOR instructions.
  ///
  static BinaryOperator *CreateNeg(Value *Op, const Twine &Name = "",
                                   Instruction *InsertBefore = nullptr);
  static BinaryOperator *CreateNeg(Value *Op, const Twine &Name,
                                   BasicBlock *InsertAtEnd);
  static BinaryOperator *CreateNSWNeg(Value *Op, const Twine &Name = "",
                                      Instruction *InsertBefore = nullptr);
  static BinaryOperator *CreateNSWNeg(Value *Op, const Twine &Name,
                                      BasicBlock *InsertAtEnd);
  static BinaryOperator *CreateNUWNeg(Value *Op, const Twine &Name = "",
                                      Instruction *InsertBefore = nullptr);
  static BinaryOperator *CreateNUWNeg(Value *Op, const Twine &Name,
                                      BasicBlock *InsertAtEnd);
  static BinaryOperator *CreateFNeg(Value *Op, const Twine &Name = "",
                                    Instruction *InsertBefore = nullptr);
  static BinaryOperator *CreateFNeg(Value *Op, const Twine &Name,
                                    BasicBlock *InsertAtEnd);
  static BinaryOperator *CreateNot(Value *Op, const Twine &Name = "",
                                   Instruction *InsertBefore = nullptr);
  static BinaryOperator *CreateNot(Value *Op, const Twine &Name,
                                   BasicBlock *InsertAtEnd);

  BinaryOps getOpcode() const {
    return static_cast<BinaryOps>(Instruction::getOpcode());
  }

  /// Exchange the two operands to this instruction.
  /// This instruction is safe to use on any binary instruction and
  /// does not modify the semantics of the instruction.  If the instruction
  /// cannot be reversed (ie, it's a Div), then return true.
  ///
  bool swapOperands();

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->isBinaryOp();
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<BinaryOperator> :
  public FixedNumOperandTraits<BinaryOperator, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BinaryOperator, Value)

//===----------------------------------------------------------------------===//
//                               CastInst Class
//===----------------------------------------------------------------------===//

/// This is the base class for all instructions that perform data
/// casts. It is simply provided so that instruction category testing
/// can be performed with code like:
///
/// if (isa<CastInst>(Instr)) { ... }
/// Base class of casting instructions.
class CastInst : public UnaryInstruction {
protected:
  /// Constructor with insert-before-instruction semantics for subclasses
  CastInst(Type *Ty, unsigned iType, Value *S,
           const Twine &NameStr = "", Instruction *InsertBefore = nullptr)
    : UnaryInstruction(Ty, iType, S, InsertBefore) {
    setName(NameStr);
  }
  /// Constructor with insert-at-end-of-block semantics for subclasses
  CastInst(Type *Ty, unsigned iType, Value *S,
           const Twine &NameStr, BasicBlock *InsertAtEnd)
    : UnaryInstruction(Ty, iType, S, InsertAtEnd) {
    setName(NameStr);
  }

public:
  /// Provides a way to construct any of the CastInst subclasses using an
  /// opcode instead of the subclass's constructor. The opcode must be in the
  /// CastOps category (Instruction::isCast(opcode) returns true). This
  /// constructor has insert-before-instruction semantics to automatically
  /// insert the new CastInst before InsertBefore (if it is non-null).
  /// Construct any of the CastInst subclasses
  static CastInst *Create(
    Instruction::CastOps,    ///< The opcode of the cast instruction
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );
  /// Provides a way to construct any of the CastInst subclasses using an
  /// opcode instead of the subclass's constructor. The opcode must be in the
  /// CastOps category. This constructor has insert-at-end-of-block semantics
  /// to automatically insert the new CastInst at the end of InsertAtEnd (if
  /// its non-null).
  /// Construct any of the CastInst subclasses
  static CastInst *Create(
    Instruction::CastOps,    ///< The opcode for the cast instruction
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which operand is casted
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create a ZExt or BitCast cast instruction
  static CastInst *CreateZExtOrBitCast(
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a ZExt or BitCast cast instruction
  static CastInst *CreateZExtOrBitCast(
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which operand is casted
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create a SExt or BitCast cast instruction
  static CastInst *CreateSExtOrBitCast(
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a SExt or BitCast cast instruction
  static CastInst *CreateSExtOrBitCast(
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which operand is casted
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create a BitCast AddrSpaceCast, or a PtrToInt cast instruction.
  static CastInst *CreatePointerCast(
    Value *S,                ///< The pointer value to be casted (operand 0)
    Type *Ty,          ///< The type to which operand is casted
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create a BitCast, AddrSpaceCast or a PtrToInt cast instruction.
  static CastInst *CreatePointerCast(
    Value *S,                ///< The pointer value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a BitCast or an AddrSpaceCast cast instruction.
  static CastInst *CreatePointerBitCastOrAddrSpaceCast(
    Value *S,                ///< The pointer value to be casted (operand 0)
    Type *Ty,          ///< The type to which operand is casted
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create a BitCast or an AddrSpaceCast cast instruction.
  static CastInst *CreatePointerBitCastOrAddrSpaceCast(
    Value *S,                ///< The pointer value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a BitCast, a PtrToInt, or an IntToPTr cast instruction.
  ///
  /// If the value is a pointer type and the destination an integer type,
  /// creates a PtrToInt cast. If the value is an integer type and the
  /// destination a pointer type, creates an IntToPtr cast. Otherwise, creates
  /// a bitcast.
  static CastInst *CreateBitOrPointerCast(
    Value *S,                ///< The pointer value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a ZExt, BitCast, or Trunc for int -> int casts.
  static CastInst *CreateIntegerCast(
    Value *S,                ///< The pointer value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    bool isSigned,           ///< Whether to regard S as signed or not
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a ZExt, BitCast, or Trunc for int -> int casts.
  static CastInst *CreateIntegerCast(
    Value *S,                ///< The integer value to be casted (operand 0)
    Type *Ty,          ///< The integer type to which operand is casted
    bool isSigned,           ///< Whether to regard S as signed or not
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create an FPExt, BitCast, or FPTrunc for fp -> fp casts
  static CastInst *CreateFPCast(
    Value *S,                ///< The floating point value to be casted
    Type *Ty,          ///< The floating point type to cast to
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create an FPExt, BitCast, or FPTrunc for fp -> fp casts
  static CastInst *CreateFPCast(
    Value *S,                ///< The floating point value to be casted
    Type *Ty,          ///< The floating point type to cast to
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Create a Trunc or BitCast cast instruction
  static CastInst *CreateTruncOrBitCast(
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which cast should be made
    const Twine &Name = "", ///< Name for the instruction
    Instruction *InsertBefore = nullptr ///< Place to insert the instruction
  );

  /// Create a Trunc or BitCast cast instruction
  static CastInst *CreateTruncOrBitCast(
    Value *S,                ///< The value to be casted (operand 0)
    Type *Ty,          ///< The type to which operand is casted
    const Twine &Name, ///< The name for the instruction
    BasicBlock *InsertAtEnd  ///< The block to insert the instruction into
  );

  /// Check whether it is valid to call getCastOpcode for these types.
  static bool isCastable(
    Type *SrcTy, ///< The Type from which the value should be cast.
    Type *DestTy ///< The Type to which the value should be cast.
  );

  /// Check whether a bitcast between these types is valid
  static bool isBitCastable(
    Type *SrcTy, ///< The Type from which the value should be cast.
    Type *DestTy ///< The Type to which the value should be cast.
  );

  /// Check whether a bitcast, inttoptr, or ptrtoint cast between these
  /// types is valid and a no-op.
  ///
  /// This ensures that any pointer<->integer cast has enough bits in the
  /// integer and any other cast is a bitcast.
  static bool isBitOrNoopPointerCastable(
      Type *SrcTy,  ///< The Type from which the value should be cast.
      Type *DestTy, ///< The Type to which the value should be cast.
      const DataLayout &DL);

  /// Returns the opcode necessary to cast Val into Ty using usual casting
  /// rules.
  /// Infer the opcode for cast operand and type
  static Instruction::CastOps getCastOpcode(
    const Value *Val, ///< The value to cast
    bool SrcIsSigned, ///< Whether to treat the source as signed
    Type *Ty,   ///< The Type to which the value should be casted
    bool DstIsSigned  ///< Whether to treate the dest. as signed
  );

  /// There are several places where we need to know if a cast instruction
  /// only deals with integer source and destination types. To simplify that
  /// logic, this method is provided.
  /// @returns true iff the cast has only integral typed operand and dest type.
  /// Determine if this is an integer-only cast.
  bool isIntegerCast() const;

  /// A lossless cast is one that does not alter the basic value. It implies
  /// a no-op cast but is more stringent, preventing things like int->float,
  /// long->double, or int->ptr.
  /// @returns true iff the cast is lossless.
  /// Determine if this is a lossless cast.
  bool isLosslessCast() const;

  /// A no-op cast is one that can be effected without changing any bits.
  /// It implies that the source and destination types are the same size. The
  /// DataLayout argument is to determine the pointer size when examining casts
  /// involving Integer and Pointer types. They are no-op casts if the integer
  /// is the same size as the pointer. However, pointer size varies with
  /// platform.
  /// Determine if the described cast is a no-op cast.
  static bool isNoopCast(
    Instruction::CastOps Opcode, ///< Opcode of cast
    Type *SrcTy,         ///< SrcTy of cast
    Type *DstTy,         ///< DstTy of cast
    const DataLayout &DL ///< DataLayout to get the Int Ptr type from.
  );

  /// Determine if this cast is a no-op cast.
  ///
  /// \param DL is the DataLayout to determine pointer size.
  bool isNoopCast(const DataLayout &DL) const;

  /// Determine how a pair of casts can be eliminated, if they can be at all.
  /// This is a helper function for both CastInst and ConstantExpr.
  /// @returns 0 if the CastInst pair can't be eliminated, otherwise
  /// returns Instruction::CastOps value for a cast that can replace
  /// the pair, casting SrcTy to DstTy.
  /// Determine if a cast pair is eliminable
  static unsigned isEliminableCastPair(
    Instruction::CastOps firstOpcode,  ///< Opcode of first cast
    Instruction::CastOps secondOpcode, ///< Opcode of second cast
    Type *SrcTy, ///< SrcTy of 1st cast
    Type *MidTy, ///< DstTy of 1st cast & SrcTy of 2nd cast
    Type *DstTy, ///< DstTy of 2nd cast
    Type *SrcIntPtrTy, ///< Integer type corresponding to Ptr SrcTy, or null
    Type *MidIntPtrTy, ///< Integer type corresponding to Ptr MidTy, or null
    Type *DstIntPtrTy  ///< Integer type corresponding to Ptr DstTy, or null
  );

  /// Return the opcode of this CastInst
  Instruction::CastOps getOpcode() const {
    return Instruction::CastOps(Instruction::getOpcode());
  }

  /// Return the source type, as a convenience
  Type* getSrcTy() const { return getOperand(0)->getType(); }
  /// Return the destination type, as a convenience
  Type* getDestTy() const { return getType(); }

  /// This method can be used to determine if a cast from S to DstTy using
  /// Opcode op is valid or not.
  /// @returns true iff the proposed cast is valid.
  /// Determine if a cast is valid without creating one.
  static bool castIsValid(Instruction::CastOps op, Value *S, Type *DstTy);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->isCast();
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CmpInst Class
//===----------------------------------------------------------------------===//

/// This class is the base class for the comparison instructions.
/// Abstract base class of comparison instructions.
class CmpInst : public Instruction {
public:
  /// This enumeration lists the possible predicates for CmpInst subclasses.
  /// Values in the range 0-31 are reserved for FCmpInst, while values in the
  /// range 32-64 are reserved for ICmpInst. This is necessary to ensure the
  /// predicate values are not overlapping between the classes.
  ///
  /// Some passes (e.g. InstCombine) depend on the bit-wise characteristics of
  /// FCMP_* values. Changing the bit patterns requires a potential change to
  /// those passes.
  enum Predicate {
    // Opcode              U L G E    Intuitive operation
    FCMP_FALSE =  0,  ///< 0 0 0 0    Always false (always folded)
    FCMP_OEQ   =  1,  ///< 0 0 0 1    True if ordered and equal
    FCMP_OGT   =  2,  ///< 0 0 1 0    True if ordered and greater than
    FCMP_OGE   =  3,  ///< 0 0 1 1    True if ordered and greater than or equal
    FCMP_OLT   =  4,  ///< 0 1 0 0    True if ordered and less than
    FCMP_OLE   =  5,  ///< 0 1 0 1    True if ordered and less than or equal
    FCMP_ONE   =  6,  ///< 0 1 1 0    True if ordered and operands are unequal
    FCMP_ORD   =  7,  ///< 0 1 1 1    True if ordered (no nans)
    FCMP_UNO   =  8,  ///< 1 0 0 0    True if unordered: isnan(X) | isnan(Y)
    FCMP_UEQ   =  9,  ///< 1 0 0 1    True if unordered or equal
    FCMP_UGT   = 10,  ///< 1 0 1 0    True if unordered or greater than
    FCMP_UGE   = 11,  ///< 1 0 1 1    True if unordered, greater than, or equal
    FCMP_ULT   = 12,  ///< 1 1 0 0    True if unordered or less than
    FCMP_ULE   = 13,  ///< 1 1 0 1    True if unordered, less than, or equal
    FCMP_UNE   = 14,  ///< 1 1 1 0    True if unordered or not equal
    FCMP_TRUE  = 15,  ///< 1 1 1 1    Always true (always folded)
    FIRST_FCMP_PREDICATE = FCMP_FALSE,
    LAST_FCMP_PREDICATE = FCMP_TRUE,
    BAD_FCMP_PREDICATE = FCMP_TRUE + 1,
    ICMP_EQ    = 32,  ///< equal
    ICMP_NE    = 33,  ///< not equal
    ICMP_UGT   = 34,  ///< unsigned greater than
    ICMP_UGE   = 35,  ///< unsigned greater or equal
    ICMP_ULT   = 36,  ///< unsigned less than
    ICMP_ULE   = 37,  ///< unsigned less or equal
    ICMP_SGT   = 38,  ///< signed greater than
    ICMP_SGE   = 39,  ///< signed greater or equal
    ICMP_SLT   = 40,  ///< signed less than
    ICMP_SLE   = 41,  ///< signed less or equal
    FIRST_ICMP_PREDICATE = ICMP_EQ,
    LAST_ICMP_PREDICATE = ICMP_SLE,
    BAD_ICMP_PREDICATE = ICMP_SLE + 1
  };

protected:
  CmpInst(Type *ty, Instruction::OtherOps op, Predicate pred,
          Value *LHS, Value *RHS, const Twine &Name = "",
          Instruction *InsertBefore = nullptr,
          Instruction *FlagsSource = nullptr);

  CmpInst(Type *ty, Instruction::OtherOps op, Predicate pred,
          Value *LHS, Value *RHS, const Twine &Name,
          BasicBlock *InsertAtEnd);

public:
  // allocate space for exactly two operands
  void *operator new(size_t s) {
    return User::operator new(s, 2);
  }

  /// Construct a compare instruction, given the opcode, the predicate and
  /// the two operands.  Optionally (if InstBefore is specified) insert the
  /// instruction into a BasicBlock right before the specified instruction.
  /// The specified Instruction is allowed to be a dereferenced end iterator.
  /// Create a CmpInst
  static CmpInst *Create(OtherOps Op,
                         Predicate predicate, Value *S1,
                         Value *S2, const Twine &Name = "",
                         Instruction *InsertBefore = nullptr);

  /// Construct a compare instruction, given the opcode, the predicate and the
  /// two operands.  Also automatically insert this instruction to the end of
  /// the BasicBlock specified.
  /// Create a CmpInst
  static CmpInst *Create(OtherOps Op, Predicate predicate, Value *S1,
                         Value *S2, const Twine &Name, BasicBlock *InsertAtEnd);

  /// Get the opcode casted to the right type
  OtherOps getOpcode() const {
    return static_cast<OtherOps>(Instruction::getOpcode());
  }

  /// Return the predicate for this instruction.
  Predicate getPredicate() const {
    return Predicate(getSubclassDataFromInstruction());
  }

  /// Set the predicate for this instruction to the specified value.
  void setPredicate(Predicate P) { setInstructionSubclassData(P); }

  static bool isFPPredicate(Predicate P) {
    return P >= FIRST_FCMP_PREDICATE && P <= LAST_FCMP_PREDICATE;
  }

  static bool isIntPredicate(Predicate P) {
    return P >= FIRST_ICMP_PREDICATE && P <= LAST_ICMP_PREDICATE;
  }

  static StringRef getPredicateName(Predicate P);

  bool isFPPredicate() const { return isFPPredicate(getPredicate()); }
  bool isIntPredicate() const { return isIntPredicate(getPredicate()); }

  /// For example, EQ -> NE, UGT -> ULE, SLT -> SGE,
  ///              OEQ -> UNE, UGT -> OLE, OLT -> UGE, etc.
  /// @returns the inverse predicate for the instruction's current predicate.
  /// Return the inverse of the instruction's predicate.
  Predicate getInversePredicate() const {
    return getInversePredicate(getPredicate());
  }

  /// For example, EQ -> NE, UGT -> ULE, SLT -> SGE,
  ///              OEQ -> UNE, UGT -> OLE, OLT -> UGE, etc.
  /// @returns the inverse predicate for predicate provided in \p pred.
  /// Return the inverse of a given predicate
  static Predicate getInversePredicate(Predicate pred);

  /// For example, EQ->EQ, SLE->SGE, ULT->UGT,
  ///              OEQ->OEQ, ULE->UGE, OLT->OGT, etc.
  /// @returns the predicate that would be the result of exchanging the two
  /// operands of the CmpInst instruction without changing the result
  /// produced.
  /// Return the predicate as if the operands were swapped
  Predicate getSwappedPredicate() const {
    return getSwappedPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction
  /// available.
  /// Return the predicate as if the operands were swapped.
  static Predicate getSwappedPredicate(Predicate pred);

  /// For predicate of kind "is X or equal to 0" returns the predicate "is X".
  /// For predicate of kind "is X" returns the predicate "is X or equal to 0".
  /// does not support other kind of predicates.
  /// @returns the predicate that does not contains is equal to zero if
  /// it had and vice versa.
  /// Return the flipped strictness of predicate
  Predicate getFlippedStrictnessPredicate() const {
    return getFlippedStrictnessPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction
  /// available.
  /// Return the flipped strictness of predicate
  static Predicate getFlippedStrictnessPredicate(Predicate pred);

  /// For example, SGT -> SGE, SLT -> SLE, ULT -> ULE, UGT -> UGE.
  /// Returns the non-strict version of strict comparisons.
  Predicate getNonStrictPredicate() const {
    return getNonStrictPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction
  /// available.
  /// @returns the non-strict version of comparison provided in \p pred.
  /// If \p pred is not a strict comparison predicate, returns \p pred.
  /// Returns the non-strict version of strict comparisons.
  static Predicate getNonStrictPredicate(Predicate pred);

  /// Provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// This is just a convenience that dispatches to the subclasses.
  /// Swap the operands and adjust predicate accordingly to retain
  /// the same comparison.
  void swapOperands();

  /// This is just a convenience that dispatches to the subclasses.
  /// Determine if this CmpInst is commutative.
  bool isCommutative() const;

  /// This is just a convenience that dispatches to the subclasses.
  /// Determine if this is an equals/not equals predicate.
  bool isEquality() const;

  /// @returns true if the comparison is signed, false otherwise.
  /// Determine if this instruction is using a signed comparison.
  bool isSigned() const {
    return isSigned(getPredicate());
  }

  /// @returns true if the comparison is unsigned, false otherwise.
  /// Determine if this instruction is using an unsigned comparison.
  bool isUnsigned() const {
    return isUnsigned(getPredicate());
  }

  /// For example, ULT->SLT, ULE->SLE, UGT->SGT, UGE->SGE, SLT->Failed assert
  /// @returns the signed version of the unsigned predicate pred.
  /// return the signed version of a predicate
  static Predicate getSignedPredicate(Predicate pred);

  /// For example, ULT->SLT, ULE->SLE, UGT->SGT, UGE->SGE, SLT->Failed assert
  /// @returns the signed version of the predicate for this instruction (which
  /// has to be an unsigned predicate).
  /// return the signed version of a predicate
  Predicate getSignedPredicate() {
    return getSignedPredicate(getPredicate());
  }

  /// This is just a convenience.
  /// Determine if this is true when both operands are the same.
  bool isTrueWhenEqual() const {
    return isTrueWhenEqual(getPredicate());
  }

  /// This is just a convenience.
  /// Determine if this is false when both operands are the same.
  bool isFalseWhenEqual() const {
    return isFalseWhenEqual(getPredicate());
  }

  /// @returns true if the predicate is unsigned, false otherwise.
  /// Determine if the predicate is an unsigned operation.
  static bool isUnsigned(Predicate predicate);

  /// @returns true if the predicate is signed, false otherwise.
  /// Determine if the predicate is an signed operation.
  static bool isSigned(Predicate predicate);

  /// Determine if the predicate is an ordered operation.
  static bool isOrdered(Predicate predicate);

  /// Determine if the predicate is an unordered operation.
  static bool isUnordered(Predicate predicate);

  /// Determine if the predicate is true when comparing a value with itself.
  static bool isTrueWhenEqual(Predicate predicate);

  /// Determine if the predicate is false when comparing a value with itself.
  static bool isFalseWhenEqual(Predicate predicate);

  /// Determine if Pred1 implies Pred2 is true when two compares have matching
  /// operands.
  static bool isImpliedTrueByMatchingCmp(Predicate Pred1, Predicate Pred2);

  /// Determine if Pred1 implies Pred2 is false when two compares have matching
  /// operands.
  static bool isImpliedFalseByMatchingCmp(Predicate Pred1, Predicate Pred2);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ICmp ||
           I->getOpcode() == Instruction::FCmp;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Create a result type for fcmp/icmp
  static Type* makeCmpResultType(Type* opnd_type) {
    if (VectorType* vt = dyn_cast<VectorType>(opnd_type)) {
      return VectorType::get(Type::getInt1Ty(opnd_type->getContext()),
                             vt->getNumElements());
    }
    return Type::getInt1Ty(opnd_type->getContext());
  }

private:
  // Shadow Value::setValueSubclassData with a private forwarding method so that
  // subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }
};

// FIXME: these are redundant if CmpInst < BinaryOperator
template <>
struct OperandTraits<CmpInst> : public FixedNumOperandTraits<CmpInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CmpInst, Value)

/// A lightweight accessor for an operand bundle meant to be passed
/// around by value.
struct OperandBundleUse {
  ArrayRef<Use> Inputs;

  OperandBundleUse() = default;
  explicit OperandBundleUse(StringMapEntry<uint32_t> *Tag, ArrayRef<Use> Inputs)
      : Inputs(Inputs), Tag(Tag) {}

  /// Return true if the operand at index \p Idx in this operand bundle
  /// has the attribute A.
  bool operandHasAttr(unsigned Idx, Attribute::AttrKind A) const {
    if (isDeoptOperandBundle())
      if (A == Attribute::ReadOnly || A == Attribute::NoCapture)
        return Inputs[Idx]->getType()->isPointerTy();

    // Conservative answer:  no operands have any attributes.
    return false;
  }

  /// Return the tag of this operand bundle as a string.
  StringRef getTagName() const {
    return Tag->getKey();
  }

  /// Return the tag of this operand bundle as an integer.
  ///
  /// Operand bundle tags are interned by LLVMContextImpl::getOrInsertBundleTag,
  /// and this function returns the unique integer getOrInsertBundleTag
  /// associated the tag of this operand bundle to.
  uint32_t getTagID() const {
    return Tag->getValue();
  }

  /// Return true if this is a "deopt" operand bundle.
  bool isDeoptOperandBundle() const {
    return getTagID() == LLVMContext::OB_deopt;
  }

  /// Return true if this is a "funclet" operand bundle.
  bool isFuncletOperandBundle() const {
    return getTagID() == LLVMContext::OB_funclet;
  }

private:
  /// Pointer to an entry in LLVMContextImpl::getOrInsertBundleTag.
  StringMapEntry<uint32_t> *Tag;
};

/// A container for an operand bundle being viewed as a set of values
/// rather than a set of uses.
///
/// Unlike OperandBundleUse, OperandBundleDefT owns the memory it carries, and
/// so it is possible to create and pass around "self-contained" instances of
/// OperandBundleDef and ConstOperandBundleDef.
template <typename InputTy> class OperandBundleDefT {
  std::string Tag;
  std::vector<InputTy> Inputs;

public:
  explicit OperandBundleDefT(std::string Tag, std::vector<InputTy> Inputs)
      : Tag(std::move(Tag)), Inputs(std::move(Inputs)) {}
  explicit OperandBundleDefT(std::string Tag, ArrayRef<InputTy> Inputs)
      : Tag(std::move(Tag)), Inputs(Inputs) {}

  explicit OperandBundleDefT(const OperandBundleUse &OBU) {
    Tag = OBU.getTagName();
    Inputs.insert(Inputs.end(), OBU.Inputs.begin(), OBU.Inputs.end());
  }

  ArrayRef<InputTy> inputs() const { return Inputs; }

  using input_iterator = typename std::vector<InputTy>::const_iterator;

  size_t input_size() const { return Inputs.size(); }
  input_iterator input_begin() const { return Inputs.begin(); }
  input_iterator input_end() const { return Inputs.end(); }

  StringRef getTag() const { return Tag; }
};

using OperandBundleDef = OperandBundleDefT<Value *>;
using ConstOperandBundleDef = OperandBundleDefT<const Value *>;

//===----------------------------------------------------------------------===//
//                               CallBase Class
//===----------------------------------------------------------------------===//

/// Base class for all callable instructions (InvokeInst and CallInst)
/// Holds everything related to calling a function.
///
/// All call-like instructions are required to use a common operand layout:
/// - Zero or more arguments to the call,
/// - Zero or more operand bundles with zero or more operand inputs each
///   bundle,
/// - Zero or more subclass controlled operands
/// - The called function.
///
/// This allows this base class to easily access the called function and the
/// start of the arguments without knowing how many other operands a particular
/// subclass requires. Note that accessing the end of the argument list isn't
/// as cheap as most other operations on the base class.
class CallBase : public Instruction {
protected:
  /// The last operand is the called operand.
  static constexpr int CalledOperandOpEndIdx = -1;

  AttributeList Attrs; ///< parameter attributes for callable
  FunctionType *FTy;

  template <class... ArgsTy>
  CallBase(AttributeList const &A, FunctionType *FT, ArgsTy &&... Args)
      : Instruction(std::forward<ArgsTy>(Args)...), Attrs(A), FTy(FT) {}

  using Instruction::Instruction;

  bool hasDescriptor() const { return Value::HasDescriptor; }

  unsigned getNumSubclassExtraOperands() const {
    switch (getOpcode()) {
    case Instruction::Call:
      return 0;
    case Instruction::Invoke:
      return 2;
    }
    llvm_unreachable("Invalid opcode!");
  }

public:
  using Instruction::getContext;

  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Call ||
           I->getOpcode() == Instruction::Invoke;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  FunctionType *getFunctionType() const { return FTy; }

  void mutateFunctionType(FunctionType *FTy) {
    Value::mutateType(FTy->getReturnType());
    this->FTy = FTy;
  }

  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// data_operands_begin/data_operands_end - Return iterators iterating over
  /// the call / invoke argument list and bundle operands.  For invokes, this is
  /// the set of instruction operands except the invoke target and the two
  /// successor blocks; and for calls this is the set of instruction operands
  /// except the call target.
  User::op_iterator data_operands_begin() { return op_begin(); }
  User::const_op_iterator data_operands_begin() const {
    return const_cast<CallBase *>(this)->data_operands_begin();
  }
  User::op_iterator data_operands_end() {
    // Walk from the end of the operands over the called operand and any
    // subclass operands.
    return op_end() - getNumSubclassExtraOperands() - 1;
  }
  User::const_op_iterator data_operands_end() const {
    return const_cast<CallBase *>(this)->data_operands_end();
  }
  iterator_range<User::op_iterator> data_ops() {
    return make_range(data_operands_begin(), data_operands_end());
  }
  iterator_range<User::const_op_iterator> data_ops() const {
    return make_range(data_operands_begin(), data_operands_end());
  }
  bool data_operands_empty() const {
    return data_operands_end() == data_operands_begin();
  }
  unsigned data_operands_size() const {
    return std::distance(data_operands_begin(), data_operands_end());
  }

  bool isDataOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return data_operands_begin() <= U && U < data_operands_end();
  }
  bool isDataOperand(Value::const_user_iterator UI) const {
    return isDataOperand(&UI.getUse());
  }

  /// Return the iterator pointing to the beginning of the argument list.
  User::op_iterator arg_begin() { return op_begin(); }
  User::const_op_iterator arg_begin() const {
    return const_cast<CallBase *>(this)->arg_begin();
  }

  /// Return the iterator pointing to the end of the argument list.
  User::op_iterator arg_end() {
    // From the end of the data operands, walk backwards past the bundle
    // operands.
    return data_operands_end() - getNumTotalBundleOperands();
  }
  User::const_op_iterator arg_end() const {
    return const_cast<CallBase *>(this)->arg_end();
  }

  /// Iteration adapter for range-for loops.
  iterator_range<User::op_iterator> args() {
    return make_range(arg_begin(), arg_end());
  }
  iterator_range<User::const_op_iterator> args() const {
    return make_range(arg_begin(), arg_end());
  }
  bool arg_empty() const { return arg_end() == arg_begin(); }
  unsigned arg_size() const { return arg_end() - arg_begin(); }

  // Legacy API names that duplicate the above and will be removed once users
  // are migrated.
  iterator_range<User::op_iterator> arg_operands() {
    return make_range(arg_begin(), arg_end());
  }
  iterator_range<User::const_op_iterator> arg_operands() const {
    return make_range(arg_begin(), arg_end());
  }
  unsigned getNumArgOperands() const { return arg_size(); }

  Value *getArgOperand(unsigned i) const {
    assert(i < getNumArgOperands() && "Out of bounds!");
    return getOperand(i);
  }

  void setArgOperand(unsigned i, Value *v) {
    assert(i < getNumArgOperands() && "Out of bounds!");
    setOperand(i, v);
  }

  /// Wrappers for getting the \c Use of a call argument.
  const Use &getArgOperandUse(unsigned i) const {
    assert(i < getNumArgOperands() && "Out of bounds!");
    return User::getOperandUse(i);
  }
  Use &getArgOperandUse(unsigned i) {
    assert(i < getNumArgOperands() && "Out of bounds!");
    return User::getOperandUse(i);
  }

  bool isArgOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return arg_begin() <= U && U < arg_end();
  }
  bool isArgOperand(Value::const_user_iterator UI) const {
    return isArgOperand(&UI.getUse());
  }

  /// Returns true if this CallSite passes the given Value* as an argument to
  /// the called function.
  bool hasArgument(const Value *V) const {
    return llvm::any_of(args(), [V](const Value *Arg) { return Arg == V; });
  }

  Value *getCalledOperand() const { return Op<CalledOperandOpEndIdx>(); }

  // DEPRECATED: This routine will be removed in favor of `getCalledOperand` in
  // the near future.
  Value *getCalledValue() const { return getCalledOperand(); }

  const Use &getCalledOperandUse() const { return Op<CalledOperandOpEndIdx>(); }
  Use &getCalledOperandUse() { return Op<CalledOperandOpEndIdx>(); }

  /// Returns the function called, or null if this is an
  /// indirect function invocation.
  Function *getCalledFunction() const {
    return dyn_cast_or_null<Function>(getCalledOperand());
  }

  /// Return true if the callsite is an indirect call.
  bool isIndirectCall() const;

  /// Determine whether the passed iterator points to the callee operand's Use.
  bool isCallee(Value::const_user_iterator UI) const {
    return isCallee(&UI.getUse());
  }

  /// Determine whether this Use is the callee operand's Use.
  bool isCallee(const Use *U) const { return &getCalledOperandUse() == U; }

  /// Helper to get the caller (the parent function).
  Function *getCaller();
  const Function *getCaller() const {
    return const_cast<CallBase *>(this)->getCaller();
  }

  /// Returns the intrinsic ID of the intrinsic called or
  /// Intrinsic::not_intrinsic if the called function is not an intrinsic, or if
  /// this is an indirect call.
  Intrinsic::ID getIntrinsicID() const;

  void setCalledOperand(Value *V) { Op<CalledOperandOpEndIdx>() = V; }

  /// Sets the function called, including updating the function type.
  void setCalledFunction(Value *Fn) {
    setCalledFunction(
        cast<FunctionType>(cast<PointerType>(Fn->getType())->getElementType()),
        Fn);
  }

  /// Sets the function called, including updating to the specified function
  /// type.
  void setCalledFunction(FunctionType *FTy, Value *Fn) {
    this->FTy = FTy;
    assert(FTy == cast<FunctionType>(
                      cast<PointerType>(Fn->getType())->getElementType()));
    setCalledOperand(Fn);
  }

  CallingConv::ID getCallingConv() const {
    return static_cast<CallingConv::ID>(getSubclassDataFromInstruction() >> 2);
  }

  void setCallingConv(CallingConv::ID CC) {
    auto ID = static_cast<unsigned>(CC);
    assert(!(ID & ~CallingConv::MaxID) && "Unsupported calling convention");
    setInstructionSubclassData((getSubclassDataFromInstruction() & 3) |
                               (ID << 2));
  }

  /// \name Attribute API
  ///
  /// These methods access and modify attributes on this call (including
  /// looking through to the attributes on the called function when necessary).
  ///@{

  /// Return the parameter attributes for this call.
  ///
  AttributeList getAttributes() const { return Attrs; }

  /// Set the parameter attributes for this call.
  ///
  void setAttributes(AttributeList A) { Attrs = A; }

  /// Determine whether this call has the given attribute.
  bool hasFnAttr(Attribute::AttrKind Kind) const {
    assert(Kind != Attribute::NoBuiltin &&
           "Use CallBase::isNoBuiltin() to check for Attribute::NoBuiltin");
    return hasFnAttrImpl(Kind);
  }

  /// Determine whether this call has the given attribute.
  bool hasFnAttr(StringRef Kind) const { return hasFnAttrImpl(Kind); }

  /// adds the attribute to the list of attributes.
  void addAttribute(unsigned i, Attribute::AttrKind Kind) {
    AttributeList PAL = getAttributes();
    PAL = PAL.addAttribute(getContext(), i, Kind);
    setAttributes(PAL);
  }

  /// adds the attribute to the list of attributes.
  void addAttribute(unsigned i, Attribute Attr) {
    AttributeList PAL = getAttributes();
    PAL = PAL.addAttribute(getContext(), i, Attr);
    setAttributes(PAL);
  }

  /// Adds the attribute to the indicated argument
  void addParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
    assert(ArgNo < getNumArgOperands() && "Out of bounds");
    AttributeList PAL = getAttributes();
    PAL = PAL.addParamAttribute(getContext(), ArgNo, Kind);
    setAttributes(PAL);
  }

  /// Adds the attribute to the indicated argument
  void addParamAttr(unsigned ArgNo, Attribute Attr) {
    assert(ArgNo < getNumArgOperands() && "Out of bounds");
    AttributeList PAL = getAttributes();
    PAL = PAL.addParamAttribute(getContext(), ArgNo, Attr);
    setAttributes(PAL);
  }

  /// removes the attribute from the list of attributes.
  void removeAttribute(unsigned i, Attribute::AttrKind Kind) {
    AttributeList PAL = getAttributes();
    PAL = PAL.removeAttribute(getContext(), i, Kind);
    setAttributes(PAL);
  }

  /// removes the attribute from the list of attributes.
  void removeAttribute(unsigned i, StringRef Kind) {
    AttributeList PAL = getAttributes();
    PAL = PAL.removeAttribute(getContext(), i, Kind);
    setAttributes(PAL);
  }

  /// Removes the attribute from the given argument
  void removeParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) {
    assert(ArgNo < getNumArgOperands() && "Out of bounds");
    AttributeList PAL = getAttributes();
    PAL = PAL.removeParamAttribute(getContext(), ArgNo, Kind);
    setAttributes(PAL);
  }

  /// Removes the attribute from the given argument
  void removeParamAttr(unsigned ArgNo, StringRef Kind) {
    assert(ArgNo < getNumArgOperands() && "Out of bounds");
    AttributeList PAL = getAttributes();
    PAL = PAL.removeParamAttribute(getContext(), ArgNo, Kind);
    setAttributes(PAL);
  }

  /// adds the dereferenceable attribute to the list of attributes.
  void addDereferenceableAttr(unsigned i, uint64_t Bytes) {
    AttributeList PAL = getAttributes();
    PAL = PAL.addDereferenceableAttr(getContext(), i, Bytes);
    setAttributes(PAL);
  }

  /// adds the dereferenceable_or_null attribute to the list of
  /// attributes.
  void addDereferenceableOrNullAttr(unsigned i, uint64_t Bytes) {
    AttributeList PAL = getAttributes();
    PAL = PAL.addDereferenceableOrNullAttr(getContext(), i, Bytes);
    setAttributes(PAL);
  }

  /// Determine whether the return value has the given attribute.
  bool hasRetAttr(Attribute::AttrKind Kind) const;

  /// Determine whether the argument or parameter has the given attribute.
  bool paramHasAttr(unsigned ArgNo, Attribute::AttrKind Kind) const;

  /// Get the attribute of a given kind at a position.
  Attribute getAttribute(unsigned i, Attribute::AttrKind Kind) const {
    return getAttributes().getAttribute(i, Kind);
  }

  /// Get the attribute of a given kind at a position.
  Attribute getAttribute(unsigned i, StringRef Kind) const {
    return getAttributes().getAttribute(i, Kind);
  }

  /// Get the attribute of a given kind from a given arg
  Attribute getParamAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
    assert(ArgNo < getNumArgOperands() && "Out of bounds");
    return getAttributes().getParamAttr(ArgNo, Kind);
  }

  /// Get the attribute of a given kind from a given arg
  Attribute getParamAttr(unsigned ArgNo, StringRef Kind) const {
    assert(ArgNo < getNumArgOperands() && "Out of bounds");
    return getAttributes().getParamAttr(ArgNo, Kind);
  }

  /// Return true if the data operand at index \p i has the attribute \p
  /// A.
  ///
  /// Data operands include call arguments and values used in operand bundles,
  /// but does not include the callee operand.  This routine dispatches to the
  /// underlying AttributeList or the OperandBundleUser as appropriate.
  ///
  /// The index \p i is interpreted as
  ///
  ///  \p i == Attribute::ReturnIndex  -> the return value
  ///  \p i in [1, arg_size + 1)  -> argument number (\p i - 1)
  ///  \p i in [arg_size + 1, data_operand_size + 1) -> bundle operand at index
  ///     (\p i - 1) in the operand list.
  bool dataOperandHasImpliedAttr(unsigned i, Attribute::AttrKind Kind) const {
    // Note that we have to add one because `i` isn't zero-indexed.
    assert(i < (getNumArgOperands() + getNumTotalBundleOperands() + 1) &&
           "Data operand index out of bounds!");

    // The attribute A can either be directly specified, if the operand in
    // question is a call argument; or be indirectly implied by the kind of its
    // containing operand bundle, if the operand is a bundle operand.

    if (i == AttributeList::ReturnIndex)
      return hasRetAttr(Kind);

    // FIXME: Avoid these i - 1 calculations and update the API to use
    // zero-based indices.
    if (i < (getNumArgOperands() + 1))
      return paramHasAttr(i - 1, Kind);

    assert(hasOperandBundles() && i >= (getBundleOperandsStartIndex() + 1) &&
           "Must be either a call argument or an operand bundle!");
    return bundleOperandHasAttr(i - 1, Kind);
  }

  /// Determine whether this data operand is not captured.
  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool doesNotCapture(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo + 1, Attribute::NoCapture);
  }

  /// Determine whether this argument is passed by value.
  bool isByValArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::ByVal);
  }

  /// Determine whether this argument is passed in an alloca.
  bool isInAllocaArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::InAlloca);
  }

  /// Determine whether this argument is passed by value or in an alloca.
  bool isByValOrInAllocaArgument(unsigned ArgNo) const {
    return paramHasAttr(ArgNo, Attribute::ByVal) ||
           paramHasAttr(ArgNo, Attribute::InAlloca);
  }

  /// Determine if there are is an inalloca argument. Only the last argument can
  /// have the inalloca attribute.
  bool hasInAllocaArgument() const {
    return !arg_empty() && paramHasAttr(arg_size() - 1, Attribute::InAlloca);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool doesNotAccessMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo + 1, Attribute::ReadNone);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool onlyReadsMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo + 1, Attribute::ReadOnly) ||
           dataOperandHasImpliedAttr(OpNo + 1, Attribute::ReadNone);
  }

  // FIXME: Once this API is no longer duplicated in `CallSite`, rename this to
  // better indicate that this may return a conservative answer.
  bool doesNotReadMemory(unsigned OpNo) const {
    return dataOperandHasImpliedAttr(OpNo + 1, Attribute::WriteOnly) ||
           dataOperandHasImpliedAttr(OpNo + 1, Attribute::ReadNone);
  }

  /// Extract the alignment of the return value.
  unsigned getRetAlignment() const { return Attrs.getRetAlignment(); }

  /// Extract the alignment for a call or parameter (0=unknown).
  unsigned getParamAlignment(unsigned ArgNo) const {
    return Attrs.getParamAlignment(ArgNo);
  }

  /// Extract the number of dereferenceable bytes for a call or
  /// parameter (0=unknown).
  uint64_t getDereferenceableBytes(unsigned i) const {
    return Attrs.getDereferenceableBytes(i);
  }

  /// Extract the number of dereferenceable_or_null bytes for a call or
  /// parameter (0=unknown).
  uint64_t getDereferenceableOrNullBytes(unsigned i) const {
    return Attrs.getDereferenceableOrNullBytes(i);
  }

  /// Return true if the return value is known to be not null.
  /// This may be because it has the nonnull attribute, or because at least
  /// one byte is dereferenceable and the pointer is in addrspace(0).
  bool isReturnNonNull() const;

  /// Determine if the return value is marked with NoAlias attribute.
  bool returnDoesNotAlias() const {
    return Attrs.hasAttribute(AttributeList::ReturnIndex, Attribute::NoAlias);
  }

  /// If one of the arguments has the 'returned' attribute, returns its
  /// operand value. Otherwise, return nullptr.
  Value *getReturnedArgOperand() const;

  /// Return true if the call should not be treated as a call to a
  /// builtin.
  bool isNoBuiltin() const {
    return hasFnAttrImpl(Attribute::NoBuiltin) &&
           !hasFnAttrImpl(Attribute::Builtin);
  }

  /// Determine if the call requires strict floating point semantics.
  bool isStrictFP() const { return hasFnAttr(Attribute::StrictFP); }

  /// Return true if the call should not be inlined.
  bool isNoInline() const { return hasFnAttr(Attribute::NoInline); }
  void setIsNoInline() {
    addAttribute(AttributeList::FunctionIndex, Attribute::NoInline);
  }
  /// Determine if the call does not access memory.
  bool doesNotAccessMemory() const { return hasFnAttr(Attribute::ReadNone); }
  void setDoesNotAccessMemory() {
    addAttribute(AttributeList::FunctionIndex, Attribute::ReadNone);
  }

  /// Determine if the call does not access or only reads memory.
  bool onlyReadsMemory() const {
    return doesNotAccessMemory() || hasFnAttr(Attribute::ReadOnly);
  }
  void setOnlyReadsMemory() {
    addAttribute(AttributeList::FunctionIndex, Attribute::ReadOnly);
  }

  /// Determine if the call does not access or only writes memory.
  bool doesNotReadMemory() const {
    return doesNotAccessMemory() || hasFnAttr(Attribute::WriteOnly);
  }
  void setDoesNotReadMemory() {
    addAttribute(AttributeList::FunctionIndex, Attribute::WriteOnly);
  }

  /// Determine if the call can access memmory only using pointers based
  /// on its arguments.
  bool onlyAccessesArgMemory() const {
    return hasFnAttr(Attribute::ArgMemOnly);
  }
  void setOnlyAccessesArgMemory() {
    addAttribute(AttributeList::FunctionIndex, Attribute::ArgMemOnly);
  }

  /// Determine if the function may only access memory that is
  /// inaccessible from the IR.
  bool onlyAccessesInaccessibleMemory() const {
    return hasFnAttr(Attribute::InaccessibleMemOnly);
  }
  void setOnlyAccessesInaccessibleMemory() {
    addAttribute(AttributeList::FunctionIndex, Attribute::InaccessibleMemOnly);
  }

  /// Determine if the function may only access memory that is
  /// either inaccessible from the IR or pointed to by its arguments.
  bool onlyAccessesInaccessibleMemOrArgMem() const {
    return hasFnAttr(Attribute::InaccessibleMemOrArgMemOnly);
  }
  void setOnlyAccessesInaccessibleMemOrArgMem() {
    addAttribute(AttributeList::FunctionIndex,
                 Attribute::InaccessibleMemOrArgMemOnly);
  }
  /// Determine if the call cannot return.
  bool doesNotReturn() const { return hasFnAttr(Attribute::NoReturn); }
  void setDoesNotReturn() {
    addAttribute(AttributeList::FunctionIndex, Attribute::NoReturn);
  }

  /// Determine if the call should not perform indirect branch tracking.
  bool doesNoCfCheck() const { return hasFnAttr(Attribute::NoCfCheck); }

  /// Determine if the call cannot unwind.
  bool doesNotThrow() const { return hasFnAttr(Attribute::NoUnwind); }
  void setDoesNotThrow() {
    addAttribute(AttributeList::FunctionIndex, Attribute::NoUnwind);
  }

  /// Determine if the invoke cannot be duplicated.
  bool cannotDuplicate() const { return hasFnAttr(Attribute::NoDuplicate); }
  void setCannotDuplicate() {
    addAttribute(AttributeList::FunctionIndex, Attribute::NoDuplicate);
  }

  /// Determine if the invoke is convergent
  bool isConvergent() const { return hasFnAttr(Attribute::Convergent); }
  void setConvergent() {
    addAttribute(AttributeList::FunctionIndex, Attribute::Convergent);
  }
  void setNotConvergent() {
    removeAttribute(AttributeList::FunctionIndex, Attribute::Convergent);
  }

  /// Determine if the call returns a structure through first
  /// pointer argument.
  bool hasStructRetAttr() const {
    if (getNumArgOperands() == 0)
      return false;

    // Be friendly and also check the callee.
    return paramHasAttr(0, Attribute::StructRet);
  }

  /// Determine if any call argument is an aggregate passed by value.
  bool hasByValArgument() const {
    return Attrs.hasAttrSomewhere(Attribute::ByVal);
  }

  ///@{
  // End of attribute API.

  /// \name Operand Bundle API
  ///
  /// This group of methods provides the API to access and manipulate operand
  /// bundles on this call.
  /// @{

  /// Return the number of operand bundles associated with this User.
  unsigned getNumOperandBundles() const {
    return std::distance(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Return true if this User has any operand bundles.
  bool hasOperandBundles() const { return getNumOperandBundles() != 0; }

  /// Return the index of the first bundle operand in the Use array.
  unsigned getBundleOperandsStartIndex() const {
    assert(hasOperandBundles() && "Don't call otherwise!");
    return bundle_op_info_begin()->Begin;
  }

  /// Return the index of the last bundle operand in the Use array.
  unsigned getBundleOperandsEndIndex() const {
    assert(hasOperandBundles() && "Don't call otherwise!");
    return bundle_op_info_end()[-1].End;
  }

  /// Return true if the operand at index \p Idx is a bundle operand.
  bool isBundleOperand(unsigned Idx) const {
    return hasOperandBundles() && Idx >= getBundleOperandsStartIndex() &&
           Idx < getBundleOperandsEndIndex();
  }

  /// Returns true if the use is a bundle operand.
  bool isBundleOperand(const Use *U) const {
    assert(this == U->getUser() &&
           "Only valid to query with a use of this instruction!");
    return hasOperandBundles() && isBundleOperand(U - op_begin());
  }
  bool isBundleOperand(Value::const_user_iterator UI) const {
    return isBundleOperand(&UI.getUse());
  }

  /// Return the total number operands (not operand bundles) used by
  /// every operand bundle in this OperandBundleUser.
  unsigned getNumTotalBundleOperands() const {
    if (!hasOperandBundles())
      return 0;

    unsigned Begin = getBundleOperandsStartIndex();
    unsigned End = getBundleOperandsEndIndex();

    assert(Begin <= End && "Should be!");
    return End - Begin;
  }

  /// Return the operand bundle at a specific index.
  OperandBundleUse getOperandBundleAt(unsigned Index) const {
    assert(Index < getNumOperandBundles() && "Index out of bounds!");
    return operandBundleFromBundleOpInfo(*(bundle_op_info_begin() + Index));
  }

  /// Return the number of operand bundles with the tag Name attached to
  /// this instruction.
  unsigned countOperandBundlesOfType(StringRef Name) const {
    unsigned Count = 0;
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      if (getOperandBundleAt(i).getTagName() == Name)
        Count++;

    return Count;
  }

  /// Return the number of operand bundles with the tag ID attached to
  /// this instruction.
  unsigned countOperandBundlesOfType(uint32_t ID) const {
    unsigned Count = 0;
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      if (getOperandBundleAt(i).getTagID() == ID)
        Count++;

    return Count;
  }

  /// Return an operand bundle by name, if present.
  ///
  /// It is an error to call this for operand bundle types that may have
  /// multiple instances of them on the same instruction.
  Optional<OperandBundleUse> getOperandBundle(StringRef Name) const {
    assert(countOperandBundlesOfType(Name) < 2 && "Precondition violated!");

    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse U = getOperandBundleAt(i);
      if (U.getTagName() == Name)
        return U;
    }

    return None;
  }

  /// Return an operand bundle by tag ID, if present.
  ///
  /// It is an error to call this for operand bundle types that may have
  /// multiple instances of them on the same instruction.
  Optional<OperandBundleUse> getOperandBundle(uint32_t ID) const {
    assert(countOperandBundlesOfType(ID) < 2 && "Precondition violated!");

    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      OperandBundleUse U = getOperandBundleAt(i);
      if (U.getTagID() == ID)
        return U;
    }

    return None;
  }

  /// Return the list of operand bundles attached to this instruction as
  /// a vector of OperandBundleDefs.
  ///
  /// This function copies the OperandBundeUse instances associated with this
  /// OperandBundleUser to a vector of OperandBundleDefs.  Note:
  /// OperandBundeUses and OperandBundleDefs are non-trivially *different*
  /// representations of operand bundles (see documentation above).
  void getOperandBundlesAsDefs(SmallVectorImpl<OperandBundleDef> &Defs) const {
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
      Defs.emplace_back(getOperandBundleAt(i));
  }

  /// Return the operand bundle for the operand at index OpIdx.
  ///
  /// It is an error to call this with an OpIdx that does not correspond to an
  /// bundle operand.
  OperandBundleUse getOperandBundleForOperand(unsigned OpIdx) const {
    return operandBundleFromBundleOpInfo(getBundleOpInfoForOperand(OpIdx));
  }

  /// Return true if this operand bundle user has operand bundles that
  /// may read from the heap.
  bool hasReadingOperandBundles() const {
    // Implementation note: this is a conservative implementation of operand
    // bundle semantics, where *any* operand bundle forces a callsite to be at
    // least readonly.
    return hasOperandBundles();
  }

  /// Return true if this operand bundle user has operand bundles that
  /// may write to the heap.
  bool hasClobberingOperandBundles() const {
    for (auto &BOI : bundle_op_infos()) {
      if (BOI.Tag->second == LLVMContext::OB_deopt ||
          BOI.Tag->second == LLVMContext::OB_funclet)
        continue;

      // This instruction has an operand bundle that is not known to us.
      // Assume the worst.
      return true;
    }

    return false;
  }

  /// Return true if the bundle operand at index \p OpIdx has the
  /// attribute \p A.
  bool bundleOperandHasAttr(unsigned OpIdx,  Attribute::AttrKind A) const {
    auto &BOI = getBundleOpInfoForOperand(OpIdx);
    auto OBU = operandBundleFromBundleOpInfo(BOI);
    return OBU.operandHasAttr(OpIdx - BOI.Begin, A);
  }

  /// Return true if \p Other has the same sequence of operand bundle
  /// tags with the same number of operands on each one of them as this
  /// OperandBundleUser.
  bool hasIdenticalOperandBundleSchema(const CallBase &Other) const {
    if (getNumOperandBundles() != Other.getNumOperandBundles())
      return false;

    return std::equal(bundle_op_info_begin(), bundle_op_info_end(),
                      Other.bundle_op_info_begin());
  }

  /// Return true if this operand bundle user contains operand bundles
  /// with tags other than those specified in \p IDs.
  bool hasOperandBundlesOtherThan(ArrayRef<uint32_t> IDs) const {
    for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i) {
      uint32_t ID = getOperandBundleAt(i).getTagID();
      if (!is_contained(IDs, ID))
        return true;
    }
    return false;
  }

  /// Is the function attribute S disallowed by some operand bundle on
  /// this operand bundle user?
  bool isFnAttrDisallowedByOpBundle(StringRef S) const {
    // Operand bundles only possibly disallow readnone, readonly and argmenonly
    // attributes.  All String attributes are fine.
    return false;
  }

  /// Is the function attribute A disallowed by some operand bundle on
  /// this operand bundle user?
  bool isFnAttrDisallowedByOpBundle(Attribute::AttrKind A) const {
    switch (A) {
    default:
      return false;

    case Attribute::InaccessibleMemOrArgMemOnly:
      return hasReadingOperandBundles();

    case Attribute::InaccessibleMemOnly:
      return hasReadingOperandBundles();

    case Attribute::ArgMemOnly:
      return hasReadingOperandBundles();

    case Attribute::ReadNone:
      return hasReadingOperandBundles();

    case Attribute::ReadOnly:
      return hasClobberingOperandBundles();
    }

    llvm_unreachable("switch has a default case!");
  }

  /// Used to keep track of an operand bundle.  See the main comment on
  /// OperandBundleUser above.
  struct BundleOpInfo {
    /// The operand bundle tag, interned by
    /// LLVMContextImpl::getOrInsertBundleTag.
    StringMapEntry<uint32_t> *Tag;

    /// The index in the Use& vector where operands for this operand
    /// bundle starts.
    uint32_t Begin;

    /// The index in the Use& vector where operands for this operand
    /// bundle ends.
    uint32_t End;

    bool operator==(const BundleOpInfo &Other) const {
      return Tag == Other.Tag && Begin == Other.Begin && End == Other.End;
    }
  };

  /// Simple helper function to map a BundleOpInfo to an
  /// OperandBundleUse.
  OperandBundleUse
  operandBundleFromBundleOpInfo(const BundleOpInfo &BOI) const {
    auto begin = op_begin();
    ArrayRef<Use> Inputs(begin + BOI.Begin, begin + BOI.End);
    return OperandBundleUse(BOI.Tag, Inputs);
  }

  using bundle_op_iterator = BundleOpInfo *;
  using const_bundle_op_iterator = const BundleOpInfo *;

  /// Return the start of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  ///
  /// OperandBundleUser uses the descriptor area co-allocated with the host User
  /// to store some meta information about which operands are "normal" operands,
  /// and which ones belong to some operand bundle.
  ///
  /// The layout of an operand bundle user is
  ///
  ///          +-----------uint32_t End-------------------------------------+
  ///          |                                                            |
  ///          |  +--------uint32_t Begin--------------------+              |
  ///          |  |                                          |              |
  ///          ^  ^                                          v              v
  ///  |------|------|----|----|----|----|----|---------|----|---------|----|-----
  ///  | BOI0 | BOI1 | .. | DU | U0 | U1 | .. | BOI0_U0 | .. | BOI1_U0 | .. | Un
  ///  |------|------|----|----|----|----|----|---------|----|---------|----|-----
  ///   v  v                                  ^              ^
  ///   |  |                                  |              |
  ///   |  +--------uint32_t Begin------------+              |
  ///   |                                                    |
  ///   +-----------uint32_t End-----------------------------+
  ///
  ///
  /// BOI0, BOI1 ... are descriptions of operand bundles in this User's use
  /// list. These descriptions are installed and managed by this class, and
  /// they're all instances of OperandBundleUser<T>::BundleOpInfo.
  ///
  /// DU is an additional descriptor installed by User's 'operator new' to keep
  /// track of the 'BOI0 ... BOIN' co-allocation.  OperandBundleUser does not
  /// access or modify DU in any way, it's an implementation detail private to
  /// User.
  ///
  /// The regular Use& vector for the User starts at U0.  The operand bundle
  /// uses are part of the Use& vector, just like normal uses.  In the diagram
  /// above, the operand bundle uses start at BOI0_U0.  Each instance of
  /// BundleOpInfo has information about a contiguous set of uses constituting
  /// an operand bundle, and the total set of operand bundle uses themselves
  /// form a contiguous set of uses (i.e. there are no gaps between uses
  /// corresponding to individual operand bundles).
  ///
  /// This class does not know the location of the set of operand bundle uses
  /// within the use list -- that is decided by the User using this class via
  /// the BeginIdx argument in populateBundleOperandInfos.
  ///
  /// Currently operand bundle users with hung-off operands are not supported.
  bundle_op_iterator bundle_op_info_begin() {
    if (!hasDescriptor())
      return nullptr;

    uint8_t *BytesBegin = getDescriptor().begin();
    return reinterpret_cast<bundle_op_iterator>(BytesBegin);
  }

  /// Return the start of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  const_bundle_op_iterator bundle_op_info_begin() const {
    auto *NonConstThis = const_cast<CallBase *>(this);
    return NonConstThis->bundle_op_info_begin();
  }

  /// Return the end of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  bundle_op_iterator bundle_op_info_end() {
    if (!hasDescriptor())
      return nullptr;

    uint8_t *BytesEnd = getDescriptor().end();
    return reinterpret_cast<bundle_op_iterator>(BytesEnd);
  }

  /// Return the end of the list of BundleOpInfo instances associated
  /// with this OperandBundleUser.
  const_bundle_op_iterator bundle_op_info_end() const {
    auto *NonConstThis = const_cast<CallBase *>(this);
    return NonConstThis->bundle_op_info_end();
  }

  /// Return the range [\p bundle_op_info_begin, \p bundle_op_info_end).
  iterator_range<bundle_op_iterator> bundle_op_infos() {
    return make_range(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Return the range [\p bundle_op_info_begin, \p bundle_op_info_end).
  iterator_range<const_bundle_op_iterator> bundle_op_infos() const {
    return make_range(bundle_op_info_begin(), bundle_op_info_end());
  }

  /// Populate the BundleOpInfo instances and the Use& vector from \p
  /// Bundles.  Return the op_iterator pointing to the Use& one past the last
  /// last bundle operand use.
  ///
  /// Each \p OperandBundleDef instance is tracked by a OperandBundleInfo
  /// instance allocated in this User's descriptor.
  op_iterator populateBundleOperandInfos(ArrayRef<OperandBundleDef> Bundles,
                                         const unsigned BeginIndex);

  /// Return the BundleOpInfo for the operand at index OpIdx.
  ///
  /// It is an error to call this with an OpIdx that does not correspond to an
  /// bundle operand.
  const BundleOpInfo &getBundleOpInfoForOperand(unsigned OpIdx) const {
    for (auto &BOI : bundle_op_infos())
      if (BOI.Begin <= OpIdx && OpIdx < BOI.End)
        return BOI;

    llvm_unreachable("Did not find operand bundle for operand!");
  }

protected:
  /// Return the total number of values used in \p Bundles.
  static unsigned CountBundleInputs(ArrayRef<OperandBundleDef> Bundles) {
    unsigned Total = 0;
    for (auto &B : Bundles)
      Total += B.input_size();
    return Total;
  }

  /// @}
  // End of operand bundle API.

private:
  bool hasFnAttrOnCalledFunction(Attribute::AttrKind Kind) const;
  bool hasFnAttrOnCalledFunction(StringRef Kind) const;

  template <typename AttrKind> bool hasFnAttrImpl(AttrKind Kind) const {
    if (Attrs.hasAttribute(AttributeList::FunctionIndex, Kind))
      return true;

    // Operand bundles override attributes on the called function, but don't
    // override attributes directly present on the call instruction.
    if (isFnAttrDisallowedByOpBundle(Kind))
      return false;

    return hasFnAttrOnCalledFunction(Kind);
  }
};

template <>
struct OperandTraits<CallBase> : public VariadicOperandTraits<CallBase, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CallBase, Value)

//===----------------------------------------------------------------------===//
//                           FuncletPadInst Class
//===----------------------------------------------------------------------===//
class FuncletPadInst : public Instruction {
private:
  FuncletPadInst(const FuncletPadInst &CPI);

  explicit FuncletPadInst(Instruction::FuncletPadOps Op, Value *ParentPad,
                          ArrayRef<Value *> Args, unsigned Values,
                          const Twine &NameStr, Instruction *InsertBefore);
  explicit FuncletPadInst(Instruction::FuncletPadOps Op, Value *ParentPad,
                          ArrayRef<Value *> Args, unsigned Values,
                          const Twine &NameStr, BasicBlock *InsertAtEnd);

  void init(Value *ParentPad, ArrayRef<Value *> Args, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;
  friend class CatchPadInst;
  friend class CleanupPadInst;

  FuncletPadInst *cloneImpl() const;

public:
  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// getNumArgOperands - Return the number of funcletpad arguments.
  ///
  unsigned getNumArgOperands() const { return getNumOperands() - 1; }

  /// Convenience accessors

  /// Return the outer EH-pad this funclet is nested within.
  ///
  /// Note: This returns the associated CatchSwitchInst if this FuncletPadInst
  /// is a CatchPadInst.
  Value *getParentPad() const { return Op<-1>(); }
  void setParentPad(Value *ParentPad) {
    assert(ParentPad);
    Op<-1>() = ParentPad;
  }

  /// getArgOperand/setArgOperand - Return/set the i-th funcletpad argument.
  ///
  Value *getArgOperand(unsigned i) const { return getOperand(i); }
  void setArgOperand(unsigned i, Value *v) { setOperand(i, v); }

  /// arg_operands - iteration adapter for range-for loops.
  op_range arg_operands() { return op_range(op_begin(), op_end() - 1); }

  /// arg_operands - iteration adapter for range-for loops.
  const_op_range arg_operands() const {
    return const_op_range(op_begin(), op_end() - 1);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) { return I->isFuncletPad(); }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<FuncletPadInst>
    : public VariadicOperandTraits<FuncletPadInst, /*MINARITY=*/1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(FuncletPadInst, Value)

} // end namespace llvm

#endif // LLVM_IR_INSTRTYPES_H
