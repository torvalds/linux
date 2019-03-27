//===- llvm/Instructions.h - Instruction subclass definitions ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes the class definitions of all of the subclasses of the
// Instruction class.  This is meant to be an easy way to get access to all
// instruction subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRUCTIONS_H
#define LLVM_IR_INSTRUCTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace llvm {

class APInt;
class ConstantInt;
class DataLayout;
class LLVMContext;

//===----------------------------------------------------------------------===//
//                                AllocaInst Class
//===----------------------------------------------------------------------===//

/// an instruction to allocate memory on the stack
class AllocaInst : public UnaryInstruction {
  Type *AllocatedType;

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  AllocaInst *cloneImpl() const;

public:
  explicit AllocaInst(Type *Ty, unsigned AddrSpace,
                      Value *ArraySize = nullptr,
                      const Twine &Name = "",
                      Instruction *InsertBefore = nullptr);
  AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize,
             const Twine &Name, BasicBlock *InsertAtEnd);

  AllocaInst(Type *Ty, unsigned AddrSpace,
             const Twine &Name, Instruction *InsertBefore = nullptr);
  AllocaInst(Type *Ty, unsigned AddrSpace,
             const Twine &Name, BasicBlock *InsertAtEnd);

  AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize, unsigned Align,
             const Twine &Name = "", Instruction *InsertBefore = nullptr);
  AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize, unsigned Align,
             const Twine &Name, BasicBlock *InsertAtEnd);

  /// Return true if there is an allocation size parameter to the allocation
  /// instruction that is not 1.
  bool isArrayAllocation() const;

  /// Get the number of elements allocated. For a simple allocation of a single
  /// element, this will return a constant 1 value.
  const Value *getArraySize() const { return getOperand(0); }
  Value *getArraySize() { return getOperand(0); }

  /// Overload to return most specific pointer type.
  PointerType *getType() const {
    return cast<PointerType>(Instruction::getType());
  }

  /// Get allocation size in bits. Returns None if size can't be determined,
  /// e.g. in case of a VLA.
  Optional<uint64_t> getAllocationSizeInBits(const DataLayout &DL) const;

  /// Return the type that is being allocated by the instruction.
  Type *getAllocatedType() const { return AllocatedType; }
  /// for use only in special circumstances that need to generically
  /// transform a whole instruction (eg: IR linking and vectorization).
  void setAllocatedType(Type *Ty) { AllocatedType = Ty; }

  /// Return the alignment of the memory that is being allocated by the
  /// instruction.
  unsigned getAlignment() const {
    return (1u << (getSubclassDataFromInstruction() & 31)) >> 1;
  }
  void setAlignment(unsigned Align);

  /// Return true if this alloca is in the entry block of the function and is a
  /// constant size. If so, the code generator will fold it into the
  /// prolog/epilog code, so it is basically free.
  bool isStaticAlloca() const;

  /// Return true if this alloca is used as an inalloca argument to a call. Such
  /// allocas are never considered static even if they are in the entry block.
  bool isUsedWithInAlloca() const {
    return getSubclassDataFromInstruction() & 32;
  }

  /// Specify whether this alloca is used to represent the arguments to a call.
  void setUsedWithInAlloca(bool V) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~32) |
                               (V ? 32 : 0));
  }

  /// Return true if this alloca is used as a swifterror argument to a call.
  bool isSwiftError() const {
    return getSubclassDataFromInstruction() & 64;
  }

  /// Specify whether this alloca is used to represent a swifterror.
  void setSwiftError(bool V) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~64) |
                               (V ? 64 : 0));
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Alloca);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }
};

//===----------------------------------------------------------------------===//
//                                LoadInst Class
//===----------------------------------------------------------------------===//

/// An instruction for reading from memory. This uses the SubclassData field in
/// Value to store whether or not the load is volatile.
class LoadInst : public UnaryInstruction {
  void AssertOK();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  LoadInst *cloneImpl() const;

public:
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr = "",
           Instruction *InsertBefore = nullptr);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, BasicBlock *InsertAtEnd);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           Instruction *InsertBefore = nullptr);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           BasicBlock *InsertAtEnd);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           unsigned Align, Instruction *InsertBefore = nullptr);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           unsigned Align, BasicBlock *InsertAtEnd);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           unsigned Align, AtomicOrdering Order,
           SyncScope::ID SSID = SyncScope::System,
           Instruction *InsertBefore = nullptr);
  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr, bool isVolatile,
           unsigned Align, AtomicOrdering Order, SyncScope::ID SSID,
           BasicBlock *InsertAtEnd);

  // Deprecated [opaque pointer types]
  explicit LoadInst(Value *Ptr, const Twine &NameStr = "",
                    Instruction *InsertBefore = nullptr)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 InsertBefore) {}
  LoadInst(Value *Ptr, const Twine &NameStr, BasicBlock *InsertAtEnd)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 InsertAtEnd) {}
  LoadInst(Value *Ptr, const Twine &NameStr, bool isVolatile,
           Instruction *InsertBefore = nullptr)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 isVolatile, InsertBefore) {}
  LoadInst(Value *Ptr, const Twine &NameStr, bool isVolatile,
           BasicBlock *InsertAtEnd)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 isVolatile, InsertAtEnd) {}
  LoadInst(Value *Ptr, const Twine &NameStr, bool isVolatile, unsigned Align,
           Instruction *InsertBefore = nullptr)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 isVolatile, Align, InsertBefore) {}
  LoadInst(Value *Ptr, const Twine &NameStr, bool isVolatile, unsigned Align,
           BasicBlock *InsertAtEnd)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 isVolatile, Align, InsertAtEnd) {}
  LoadInst(Value *Ptr, const Twine &NameStr, bool isVolatile, unsigned Align,
           AtomicOrdering Order, SyncScope::ID SSID = SyncScope::System,
           Instruction *InsertBefore = nullptr)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 isVolatile, Align, Order, SSID, InsertBefore) {}
  LoadInst(Value *Ptr, const Twine &NameStr, bool isVolatile, unsigned Align,
           AtomicOrdering Order, SyncScope::ID SSID, BasicBlock *InsertAtEnd)
      : LoadInst(Ptr->getType()->getPointerElementType(), Ptr, NameStr,
                 isVolatile, Align, Order, SSID, InsertAtEnd) {}

  /// Return true if this is a load from a volatile memory location.
  bool isVolatile() const { return getSubclassDataFromInstruction() & 1; }

  /// Specify whether this is a volatile load or not.
  void setVolatile(bool V) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~1) |
                               (V ? 1 : 0));
  }

  /// Return the alignment of the access that is being performed.
  unsigned getAlignment() const {
    return (1 << ((getSubclassDataFromInstruction() >> 1) & 31)) >> 1;
  }

  void setAlignment(unsigned Align);

  /// Returns the ordering constraint of this load instruction.
  AtomicOrdering getOrdering() const {
    return AtomicOrdering((getSubclassDataFromInstruction() >> 7) & 7);
  }

  /// Sets the ordering constraint of this load instruction.  May not be Release
  /// or AcquireRelease.
  void setOrdering(AtomicOrdering Ordering) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~(7 << 7)) |
                               ((unsigned)Ordering << 7));
  }

  /// Returns the synchronization scope ID of this load instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this load instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  /// Sets the ordering constraint and the synchronization scope ID of this load
  /// instruction.
  void setAtomic(AtomicOrdering Ordering,
                 SyncScope::ID SSID = SyncScope::System) {
    setOrdering(Ordering);
    setSyncScopeID(SSID);
  }

  bool isSimple() const { return !isAtomic() && !isVolatile(); }

  bool isUnordered() const {
    return (getOrdering() == AtomicOrdering::NotAtomic ||
            getOrdering() == AtomicOrdering::Unordered) &&
           !isVolatile();
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Load;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }

  /// The synchronization scope ID of this load instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

//===----------------------------------------------------------------------===//
//                                StoreInst Class
//===----------------------------------------------------------------------===//

/// An instruction for storing to memory.
class StoreInst : public Instruction {
  void AssertOK();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  StoreInst *cloneImpl() const;

public:
  StoreInst(Value *Val, Value *Ptr, Instruction *InsertBefore);
  StoreInst(Value *Val, Value *Ptr, BasicBlock *InsertAtEnd);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile = false,
            Instruction *InsertBefore = nullptr);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile, BasicBlock *InsertAtEnd);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile,
            unsigned Align, Instruction *InsertBefore = nullptr);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile,
            unsigned Align, BasicBlock *InsertAtEnd);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile,
            unsigned Align, AtomicOrdering Order,
            SyncScope::ID SSID = SyncScope::System,
            Instruction *InsertBefore = nullptr);
  StoreInst(Value *Val, Value *Ptr, bool isVolatile,
            unsigned Align, AtomicOrdering Order, SyncScope::ID SSID,
            BasicBlock *InsertAtEnd);

  // allocate space for exactly two operands
  void *operator new(size_t s) {
    return User::operator new(s, 2);
  }

  /// Return true if this is a store to a volatile memory location.
  bool isVolatile() const { return getSubclassDataFromInstruction() & 1; }

  /// Specify whether this is a volatile store or not.
  void setVolatile(bool V) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~1) |
                               (V ? 1 : 0));
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Return the alignment of the access that is being performed
  unsigned getAlignment() const {
    return (1 << ((getSubclassDataFromInstruction() >> 1) & 31)) >> 1;
  }

  void setAlignment(unsigned Align);

  /// Returns the ordering constraint of this store instruction.
  AtomicOrdering getOrdering() const {
    return AtomicOrdering((getSubclassDataFromInstruction() >> 7) & 7);
  }

  /// Sets the ordering constraint of this store instruction.  May not be
  /// Acquire or AcquireRelease.
  void setOrdering(AtomicOrdering Ordering) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~(7 << 7)) |
                               ((unsigned)Ordering << 7));
  }

  /// Returns the synchronization scope ID of this store instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this store instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  /// Sets the ordering constraint and the synchronization scope ID of this
  /// store instruction.
  void setAtomic(AtomicOrdering Ordering,
                 SyncScope::ID SSID = SyncScope::System) {
    setOrdering(Ordering);
    setSyncScopeID(SSID);
  }

  bool isSimple() const { return !isAtomic() && !isVolatile(); }

  bool isUnordered() const {
    return (getOrdering() == AtomicOrdering::NotAtomic ||
            getOrdering() == AtomicOrdering::Unordered) &&
           !isVolatile();
  }

  Value *getValueOperand() { return getOperand(0); }
  const Value *getValueOperand() const { return getOperand(0); }

  Value *getPointerOperand() { return getOperand(1); }
  const Value *getPointerOperand() const { return getOperand(1); }
  static unsigned getPointerOperandIndex() { return 1U; }
  Type *getPointerOperandType() const { return getPointerOperand()->getType(); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Store;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }

  /// The synchronization scope ID of this store instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

template <>
struct OperandTraits<StoreInst> : public FixedNumOperandTraits<StoreInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(StoreInst, Value)

//===----------------------------------------------------------------------===//
//                                FenceInst Class
//===----------------------------------------------------------------------===//

/// An instruction for ordering other memory operations.
class FenceInst : public Instruction {
  void Init(AtomicOrdering Ordering, SyncScope::ID SSID);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  FenceInst *cloneImpl() const;

public:
  // Ordering may only be Acquire, Release, AcquireRelease, or
  // SequentiallyConsistent.
  FenceInst(LLVMContext &C, AtomicOrdering Ordering,
            SyncScope::ID SSID = SyncScope::System,
            Instruction *InsertBefore = nullptr);
  FenceInst(LLVMContext &C, AtomicOrdering Ordering, SyncScope::ID SSID,
            BasicBlock *InsertAtEnd);

  // allocate space for exactly zero operands
  void *operator new(size_t s) {
    return User::operator new(s, 0);
  }

  /// Returns the ordering constraint of this fence instruction.
  AtomicOrdering getOrdering() const {
    return AtomicOrdering(getSubclassDataFromInstruction() >> 1);
  }

  /// Sets the ordering constraint of this fence instruction.  May only be
  /// Acquire, Release, AcquireRelease, or SequentiallyConsistent.
  void setOrdering(AtomicOrdering Ordering) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & 1) |
                               ((unsigned)Ordering << 1));
  }

  /// Returns the synchronization scope ID of this fence instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this fence instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Fence;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }

  /// The synchronization scope ID of this fence instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

//===----------------------------------------------------------------------===//
//                                AtomicCmpXchgInst Class
//===----------------------------------------------------------------------===//

/// an instruction that atomically checks whether a
/// specified value is in a memory location, and, if it is, stores a new value
/// there.  Returns the value that was loaded.
///
class AtomicCmpXchgInst : public Instruction {
  void Init(Value *Ptr, Value *Cmp, Value *NewVal,
            AtomicOrdering SuccessOrdering, AtomicOrdering FailureOrdering,
            SyncScope::ID SSID);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  AtomicCmpXchgInst *cloneImpl() const;

public:
  AtomicCmpXchgInst(Value *Ptr, Value *Cmp, Value *NewVal,
                    AtomicOrdering SuccessOrdering,
                    AtomicOrdering FailureOrdering,
                    SyncScope::ID SSID, Instruction *InsertBefore = nullptr);
  AtomicCmpXchgInst(Value *Ptr, Value *Cmp, Value *NewVal,
                    AtomicOrdering SuccessOrdering,
                    AtomicOrdering FailureOrdering,
                    SyncScope::ID SSID, BasicBlock *InsertAtEnd);

  // allocate space for exactly three operands
  void *operator new(size_t s) {
    return User::operator new(s, 3);
  }

  /// Return true if this is a cmpxchg from a volatile memory
  /// location.
  ///
  bool isVolatile() const {
    return getSubclassDataFromInstruction() & 1;
  }

  /// Specify whether this is a volatile cmpxchg.
  ///
  void setVolatile(bool V) {
     setInstructionSubclassData((getSubclassDataFromInstruction() & ~1) |
                                (unsigned)V);
  }

  /// Return true if this cmpxchg may spuriously fail.
  bool isWeak() const {
    return getSubclassDataFromInstruction() & 0x100;
  }

  void setWeak(bool IsWeak) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~0x100) |
                               (IsWeak << 8));
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Returns the success ordering constraint of this cmpxchg instruction.
  AtomicOrdering getSuccessOrdering() const {
    return AtomicOrdering((getSubclassDataFromInstruction() >> 2) & 7);
  }

  /// Sets the success ordering constraint of this cmpxchg instruction.
  void setSuccessOrdering(AtomicOrdering Ordering) {
    assert(Ordering != AtomicOrdering::NotAtomic &&
           "CmpXchg instructions can only be atomic.");
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~0x1c) |
                               ((unsigned)Ordering << 2));
  }

  /// Returns the failure ordering constraint of this cmpxchg instruction.
  AtomicOrdering getFailureOrdering() const {
    return AtomicOrdering((getSubclassDataFromInstruction() >> 5) & 7);
  }

  /// Sets the failure ordering constraint of this cmpxchg instruction.
  void setFailureOrdering(AtomicOrdering Ordering) {
    assert(Ordering != AtomicOrdering::NotAtomic &&
           "CmpXchg instructions can only be atomic.");
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~0xe0) |
                               ((unsigned)Ordering << 5));
  }

  /// Returns the synchronization scope ID of this cmpxchg instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this cmpxchg instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }

  Value *getCompareOperand() { return getOperand(1); }
  const Value *getCompareOperand() const { return getOperand(1); }

  Value *getNewValOperand() { return getOperand(2); }
  const Value *getNewValOperand() const { return getOperand(2); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  /// Returns the strongest permitted ordering on failure, given the
  /// desired ordering on success.
  ///
  /// If the comparison in a cmpxchg operation fails, there is no atomic store
  /// so release semantics cannot be provided. So this function drops explicit
  /// Release requests from the AtomicOrdering. A SequentiallyConsistent
  /// operation would remain SequentiallyConsistent.
  static AtomicOrdering
  getStrongestFailureOrdering(AtomicOrdering SuccessOrdering) {
    switch (SuccessOrdering) {
    default:
      llvm_unreachable("invalid cmpxchg success ordering");
    case AtomicOrdering::Release:
    case AtomicOrdering::Monotonic:
      return AtomicOrdering::Monotonic;
    case AtomicOrdering::AcquireRelease:
    case AtomicOrdering::Acquire:
      return AtomicOrdering::Acquire;
    case AtomicOrdering::SequentiallyConsistent:
      return AtomicOrdering::SequentiallyConsistent;
    }
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::AtomicCmpXchg;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }

  /// The synchronization scope ID of this cmpxchg instruction.  Not quite
  /// enough room in SubClassData for everything, so synchronization scope ID
  /// gets its own field.
  SyncScope::ID SSID;
};

template <>
struct OperandTraits<AtomicCmpXchgInst> :
    public FixedNumOperandTraits<AtomicCmpXchgInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(AtomicCmpXchgInst, Value)

//===----------------------------------------------------------------------===//
//                                AtomicRMWInst Class
//===----------------------------------------------------------------------===//

/// an instruction that atomically reads a memory location,
/// combines it with another value, and then stores the result back.  Returns
/// the old value.
///
class AtomicRMWInst : public Instruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  AtomicRMWInst *cloneImpl() const;

public:
  /// This enumeration lists the possible modifications atomicrmw can make.  In
  /// the descriptions, 'p' is the pointer to the instruction's memory location,
  /// 'old' is the initial value of *p, and 'v' is the other value passed to the
  /// instruction.  These instructions always return 'old'.
  enum BinOp {
    /// *p = v
    Xchg,
    /// *p = old + v
    Add,
    /// *p = old - v
    Sub,
    /// *p = old & v
    And,
    /// *p = ~(old & v)
    Nand,
    /// *p = old | v
    Or,
    /// *p = old ^ v
    Xor,
    /// *p = old >signed v ? old : v
    Max,
    /// *p = old <signed v ? old : v
    Min,
    /// *p = old >unsigned v ? old : v
    UMax,
    /// *p = old <unsigned v ? old : v
    UMin,

    FIRST_BINOP = Xchg,
    LAST_BINOP = UMin,
    BAD_BINOP
  };

  AtomicRMWInst(BinOp Operation, Value *Ptr, Value *Val,
                AtomicOrdering Ordering, SyncScope::ID SSID,
                Instruction *InsertBefore = nullptr);
  AtomicRMWInst(BinOp Operation, Value *Ptr, Value *Val,
                AtomicOrdering Ordering, SyncScope::ID SSID,
                BasicBlock *InsertAtEnd);

  // allocate space for exactly two operands
  void *operator new(size_t s) {
    return User::operator new(s, 2);
  }

  BinOp getOperation() const {
    return static_cast<BinOp>(getSubclassDataFromInstruction() >> 5);
  }

  static StringRef getOperationName(BinOp Op);

  void setOperation(BinOp Operation) {
    unsigned short SubclassData = getSubclassDataFromInstruction();
    setInstructionSubclassData((SubclassData & 31) |
                               (Operation << 5));
  }

  /// Return true if this is a RMW on a volatile memory location.
  ///
  bool isVolatile() const {
    return getSubclassDataFromInstruction() & 1;
  }

  /// Specify whether this is a volatile RMW or not.
  ///
  void setVolatile(bool V) {
     setInstructionSubclassData((getSubclassDataFromInstruction() & ~1) |
                                (unsigned)V);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Returns the ordering constraint of this rmw instruction.
  AtomicOrdering getOrdering() const {
    return AtomicOrdering((getSubclassDataFromInstruction() >> 2) & 7);
  }

  /// Sets the ordering constraint of this rmw instruction.
  void setOrdering(AtomicOrdering Ordering) {
    assert(Ordering != AtomicOrdering::NotAtomic &&
           "atomicrmw instructions can only be atomic.");
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~(7 << 2)) |
                               ((unsigned)Ordering << 2));
  }

  /// Returns the synchronization scope ID of this rmw instruction.
  SyncScope::ID getSyncScopeID() const {
    return SSID;
  }

  /// Sets the synchronization scope ID of this rmw instruction.
  void setSyncScopeID(SyncScope::ID SSID) {
    this->SSID = SSID;
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }

  Value *getValOperand() { return getOperand(1); }
  const Value *getValOperand() const { return getOperand(1); }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::AtomicRMW;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  void Init(BinOp Operation, Value *Ptr, Value *Val,
            AtomicOrdering Ordering, SyncScope::ID SSID);

  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }

  /// The synchronization scope ID of this rmw instruction.  Not quite enough
  /// room in SubClassData for everything, so synchronization scope ID gets its
  /// own field.
  SyncScope::ID SSID;
};

template <>
struct OperandTraits<AtomicRMWInst>
    : public FixedNumOperandTraits<AtomicRMWInst,2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(AtomicRMWInst, Value)

//===----------------------------------------------------------------------===//
//                             GetElementPtrInst Class
//===----------------------------------------------------------------------===//

// checkGEPType - Simple wrapper function to give a better assertion failure
// message on bad indexes for a gep instruction.
//
inline Type *checkGEPType(Type *Ty) {
  assert(Ty && "Invalid GetElementPtrInst indices for type!");
  return Ty;
}

/// an instruction for type-safe pointer arithmetic to
/// access elements of arrays and structs
///
class GetElementPtrInst : public Instruction {
  Type *SourceElementType;
  Type *ResultElementType;

  GetElementPtrInst(const GetElementPtrInst &GEPI);

  /// Constructors - Create a getelementptr instruction with a base pointer an
  /// list of indices. The first ctor can optionally insert before an existing
  /// instruction, the second appends the new instruction to the specified
  /// BasicBlock.
  inline GetElementPtrInst(Type *PointeeType, Value *Ptr,
                           ArrayRef<Value *> IdxList, unsigned Values,
                           const Twine &NameStr, Instruction *InsertBefore);
  inline GetElementPtrInst(Type *PointeeType, Value *Ptr,
                           ArrayRef<Value *> IdxList, unsigned Values,
                           const Twine &NameStr, BasicBlock *InsertAtEnd);

  void init(Value *Ptr, ArrayRef<Value *> IdxList, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  GetElementPtrInst *cloneImpl() const;

public:
  static GetElementPtrInst *Create(Type *PointeeType, Value *Ptr,
                                   ArrayRef<Value *> IdxList,
                                   const Twine &NameStr = "",
                                   Instruction *InsertBefore = nullptr) {
    unsigned Values = 1 + unsigned(IdxList.size());
    if (!PointeeType)
      PointeeType =
          cast<PointerType>(Ptr->getType()->getScalarType())->getElementType();
    else
      assert(
          PointeeType ==
          cast<PointerType>(Ptr->getType()->getScalarType())->getElementType());
    return new (Values) GetElementPtrInst(PointeeType, Ptr, IdxList, Values,
                                          NameStr, InsertBefore);
  }

  static GetElementPtrInst *Create(Type *PointeeType, Value *Ptr,
                                   ArrayRef<Value *> IdxList,
                                   const Twine &NameStr,
                                   BasicBlock *InsertAtEnd) {
    unsigned Values = 1 + unsigned(IdxList.size());
    if (!PointeeType)
      PointeeType =
          cast<PointerType>(Ptr->getType()->getScalarType())->getElementType();
    else
      assert(
          PointeeType ==
          cast<PointerType>(Ptr->getType()->getScalarType())->getElementType());
    return new (Values) GetElementPtrInst(PointeeType, Ptr, IdxList, Values,
                                          NameStr, InsertAtEnd);
  }

  /// Create an "inbounds" getelementptr. See the documentation for the
  /// "inbounds" flag in LangRef.html for details.
  static GetElementPtrInst *CreateInBounds(Value *Ptr,
                                           ArrayRef<Value *> IdxList,
                                           const Twine &NameStr = "",
                                           Instruction *InsertBefore = nullptr){
    return CreateInBounds(nullptr, Ptr, IdxList, NameStr, InsertBefore);
  }

  static GetElementPtrInst *
  CreateInBounds(Type *PointeeType, Value *Ptr, ArrayRef<Value *> IdxList,
                 const Twine &NameStr = "",
                 Instruction *InsertBefore = nullptr) {
    GetElementPtrInst *GEP =
        Create(PointeeType, Ptr, IdxList, NameStr, InsertBefore);
    GEP->setIsInBounds(true);
    return GEP;
  }

  static GetElementPtrInst *CreateInBounds(Value *Ptr,
                                           ArrayRef<Value *> IdxList,
                                           const Twine &NameStr,
                                           BasicBlock *InsertAtEnd) {
    return CreateInBounds(nullptr, Ptr, IdxList, NameStr, InsertAtEnd);
  }

  static GetElementPtrInst *CreateInBounds(Type *PointeeType, Value *Ptr,
                                           ArrayRef<Value *> IdxList,
                                           const Twine &NameStr,
                                           BasicBlock *InsertAtEnd) {
    GetElementPtrInst *GEP =
        Create(PointeeType, Ptr, IdxList, NameStr, InsertAtEnd);
    GEP->setIsInBounds(true);
    return GEP;
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Type *getSourceElementType() const { return SourceElementType; }

  void setSourceElementType(Type *Ty) { SourceElementType = Ty; }
  void setResultElementType(Type *Ty) { ResultElementType = Ty; }

  Type *getResultElementType() const {
    assert(ResultElementType ==
           cast<PointerType>(getType()->getScalarType())->getElementType());
    return ResultElementType;
  }

  /// Returns the address space of this instruction's pointer type.
  unsigned getAddressSpace() const {
    // Note that this is always the same as the pointer operand's address space
    // and that is cheaper to compute, so cheat here.
    return getPointerAddressSpace();
  }

  /// Returns the type of the element that would be loaded with
  /// a load instruction with the specified parameters.
  ///
  /// Null is returned if the indices are invalid for the specified
  /// pointer type.
  ///
  static Type *getIndexedType(Type *Ty, ArrayRef<Value *> IdxList);
  static Type *getIndexedType(Type *Ty, ArrayRef<Constant *> IdxList);
  static Type *getIndexedType(Type *Ty, ArrayRef<uint64_t> IdxList);

  inline op_iterator       idx_begin()       { return op_begin()+1; }
  inline const_op_iterator idx_begin() const { return op_begin()+1; }
  inline op_iterator       idx_end()         { return op_end(); }
  inline const_op_iterator idx_end()   const { return op_end(); }

  inline iterator_range<op_iterator> indices() {
    return make_range(idx_begin(), idx_end());
  }

  inline iterator_range<const_op_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getPointerOperand() {
    return getOperand(0);
  }
  const Value *getPointerOperand() const {
    return getOperand(0);
  }
  static unsigned getPointerOperandIndex() {
    return 0U;    // get index for modifying correct operand.
  }

  /// Method to return the pointer operand as a
  /// PointerType.
  Type *getPointerOperandType() const {
    return getPointerOperand()->getType();
  }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperandType()->getPointerAddressSpace();
  }

  /// Returns the pointer type returned by the GEP
  /// instruction, which may be a vector of pointers.
  static Type *getGEPReturnType(Value *Ptr, ArrayRef<Value *> IdxList) {
    return getGEPReturnType(
      cast<PointerType>(Ptr->getType()->getScalarType())->getElementType(),
      Ptr, IdxList);
  }
  static Type *getGEPReturnType(Type *ElTy, Value *Ptr,
                                ArrayRef<Value *> IdxList) {
    Type *PtrTy = PointerType::get(checkGEPType(getIndexedType(ElTy, IdxList)),
                                   Ptr->getType()->getPointerAddressSpace());
    // Vector GEP
    if (Ptr->getType()->isVectorTy()) {
      unsigned NumElem = Ptr->getType()->getVectorNumElements();
      return VectorType::get(PtrTy, NumElem);
    }
    for (Value *Index : IdxList)
      if (Index->getType()->isVectorTy()) {
        unsigned NumElem = Index->getType()->getVectorNumElements();
        return VectorType::get(PtrTy, NumElem);
      }
    // Scalar GEP
    return PtrTy;
  }

  unsigned getNumIndices() const {  // Note: always non-negative
    return getNumOperands() - 1;
  }

  bool hasIndices() const {
    return getNumOperands() > 1;
  }

  /// Return true if all of the indices of this GEP are
  /// zeros.  If so, the result pointer and the first operand have the same
  /// value, just potentially different types.
  bool hasAllZeroIndices() const;

  /// Return true if all of the indices of this GEP are
  /// constant integers.  If so, the result pointer and the first operand have
  /// a constant offset between them.
  bool hasAllConstantIndices() const;

  /// Set or clear the inbounds flag on this GEP instruction.
  /// See LangRef.html for the meaning of inbounds on a getelementptr.
  void setIsInBounds(bool b = true);

  /// Determine whether the GEP has the inbounds flag.
  bool isInBounds() const;

  /// Accumulate the constant address offset of this GEP if possible.
  ///
  /// This routine accepts an APInt into which it will accumulate the constant
  /// offset of this GEP if the GEP is in fact constant. If the GEP is not
  /// all-constant, it returns false and the value of the offset APInt is
  /// undefined (it is *not* preserved!). The APInt passed into this routine
  /// must be at least as wide as the IntPtr type for the address space of
  /// the base GEP pointer.
  bool accumulateConstantOffset(const DataLayout &DL, APInt &Offset) const;

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::GetElementPtr);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<GetElementPtrInst> :
  public VariadicOperandTraits<GetElementPtrInst, 1> {
};

GetElementPtrInst::GetElementPtrInst(Type *PointeeType, Value *Ptr,
                                     ArrayRef<Value *> IdxList, unsigned Values,
                                     const Twine &NameStr,
                                     Instruction *InsertBefore)
    : Instruction(getGEPReturnType(PointeeType, Ptr, IdxList), GetElementPtr,
                  OperandTraits<GetElementPtrInst>::op_end(this) - Values,
                  Values, InsertBefore),
      SourceElementType(PointeeType),
      ResultElementType(getIndexedType(PointeeType, IdxList)) {
  assert(ResultElementType ==
         cast<PointerType>(getType()->getScalarType())->getElementType());
  init(Ptr, IdxList, NameStr);
}

GetElementPtrInst::GetElementPtrInst(Type *PointeeType, Value *Ptr,
                                     ArrayRef<Value *> IdxList, unsigned Values,
                                     const Twine &NameStr,
                                     BasicBlock *InsertAtEnd)
    : Instruction(getGEPReturnType(PointeeType, Ptr, IdxList), GetElementPtr,
                  OperandTraits<GetElementPtrInst>::op_end(this) - Values,
                  Values, InsertAtEnd),
      SourceElementType(PointeeType),
      ResultElementType(getIndexedType(PointeeType, IdxList)) {
  assert(ResultElementType ==
         cast<PointerType>(getType()->getScalarType())->getElementType());
  init(Ptr, IdxList, NameStr);
}

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GetElementPtrInst, Value)

//===----------------------------------------------------------------------===//
//                                UnaryOperator Class
//===----------------------------------------------------------------------===//

/// a unary instruction 
class UnaryOperator : public UnaryInstruction {
  void AssertOK();

protected:
  UnaryOperator(UnaryOps iType, Value *S, Type *Ty,
                const Twine &Name, Instruction *InsertBefore);
  UnaryOperator(UnaryOps iType, Value *S, Type *Ty,
                const Twine &Name, BasicBlock *InsertAtEnd);

  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  UnaryOperator *cloneImpl() const;

public:

  /// Construct a unary instruction, given the opcode and an operand.
  /// Optionally (if InstBefore is specified) insert the instruction
  /// into a BasicBlock right before the specified instruction.  The specified
  /// Instruction is allowed to be a dereferenced end iterator.
  ///
  static UnaryOperator *Create(UnaryOps Op, Value *S,
                               const Twine &Name = Twine(),
                               Instruction *InsertBefore = nullptr);

  /// Construct a unary instruction, given the opcode and an operand.
  /// Also automatically insert this instruction to the end of the
  /// BasicBlock specified.
  ///
  static UnaryOperator *Create(UnaryOps Op, Value *S,
                               const Twine &Name,
                               BasicBlock *InsertAtEnd);

  /// These methods just forward to Create, and are useful when you
  /// statically know what type of instruction you're going to create.  These
  /// helpers just save some typing.
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryInstruction *Create##OPC(Value *V, \
                                       const Twine &Name = "") {\
    return Create(Instruction::OPC, V, Name);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryInstruction *Create##OPC(Value *V, \
                                       const Twine &Name, BasicBlock *BB) {\
    return Create(Instruction::OPC, V, Name, BB);\
  }
#include "llvm/IR/Instruction.def"
#define HANDLE_UNARY_INST(N, OPC, CLASS) \
  static UnaryInstruction *Create##OPC(Value *V, \
                                       const Twine &Name, Instruction *I) {\
    return Create(Instruction::OPC, V, Name, I);\
  }
#include "llvm/IR/Instruction.def"

  UnaryOps getOpcode() const {
    return static_cast<UnaryOps>(Instruction::getOpcode());
  }
};

//===----------------------------------------------------------------------===//
//                               ICmpInst Class
//===----------------------------------------------------------------------===//

/// This instruction compares its operands according to the predicate given
/// to the constructor. It only operates on integers or pointers. The operands
/// must be identical types.
/// Represent an integer comparison operator.
class ICmpInst: public CmpInst {
  void AssertOK() {
    assert(isIntPredicate() &&
           "Invalid ICmp predicate value");
    assert(getOperand(0)->getType() == getOperand(1)->getType() &&
          "Both operands to ICmp instruction are not of the same type!");
    // Check that the operands are the right type
    assert((getOperand(0)->getType()->isIntOrIntVectorTy() ||
            getOperand(0)->getType()->isPtrOrPtrVectorTy()) &&
           "Invalid operand types for ICmp instruction");
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical ICmpInst
  ICmpInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics.
  ICmpInst(
    Instruction *InsertBefore,  ///< Where to insert
    Predicate pred,  ///< The predicate to use for the comparison
    Value *LHS,      ///< The left-hand-side of the expression
    Value *RHS,      ///< The right-hand-side of the expression
    const Twine &NameStr = ""  ///< Name of the instruction
  ) : CmpInst(makeCmpResultType(LHS->getType()),
              Instruction::ICmp, pred, LHS, RHS, NameStr,
              InsertBefore) {
#ifndef NDEBUG
  AssertOK();
#endif
  }

  /// Constructor with insert-at-end semantics.
  ICmpInst(
    BasicBlock &InsertAtEnd, ///< Block to insert into.
    Predicate pred,  ///< The predicate to use for the comparison
    Value *LHS,      ///< The left-hand-side of the expression
    Value *RHS,      ///< The right-hand-side of the expression
    const Twine &NameStr = ""  ///< Name of the instruction
  ) : CmpInst(makeCmpResultType(LHS->getType()),
              Instruction::ICmp, pred, LHS, RHS, NameStr,
              &InsertAtEnd) {
#ifndef NDEBUG
  AssertOK();
#endif
  }

  /// Constructor with no-insertion semantics
  ICmpInst(
    Predicate pred, ///< The predicate to use for the comparison
    Value *LHS,     ///< The left-hand-side of the expression
    Value *RHS,     ///< The right-hand-side of the expression
    const Twine &NameStr = "" ///< Name of the instruction
  ) : CmpInst(makeCmpResultType(LHS->getType()),
              Instruction::ICmp, pred, LHS, RHS, NameStr) {
#ifndef NDEBUG
  AssertOK();
#endif
  }

  /// For example, EQ->EQ, SLE->SLE, UGT->SGT, etc.
  /// @returns the predicate that would be the result if the operand were
  /// regarded as signed.
  /// Return the signed version of the predicate
  Predicate getSignedPredicate() const {
    return getSignedPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction.
  /// Return the signed version of the predicate.
  static Predicate getSignedPredicate(Predicate pred);

  /// For example, EQ->EQ, SLE->ULE, UGT->UGT, etc.
  /// @returns the predicate that would be the result if the operand were
  /// regarded as unsigned.
  /// Return the unsigned version of the predicate
  Predicate getUnsignedPredicate() const {
    return getUnsignedPredicate(getPredicate());
  }

  /// This is a static version that you can use without an instruction.
  /// Return the unsigned version of the predicate.
  static Predicate getUnsignedPredicate(Predicate pred);

  /// Return true if this predicate is either EQ or NE.  This also
  /// tests for commutativity.
  static bool isEquality(Predicate P) {
    return P == ICMP_EQ || P == ICMP_NE;
  }

  /// Return true if this predicate is either EQ or NE.  This also
  /// tests for commutativity.
  bool isEquality() const {
    return isEquality(getPredicate());
  }

  /// @returns true if the predicate of this ICmpInst is commutative
  /// Determine if this relation is commutative.
  bool isCommutative() const { return isEquality(); }

  /// Return true if the predicate is relational (not EQ or NE).
  ///
  bool isRelational() const {
    return !isEquality();
  }

  /// Return true if the predicate is relational (not EQ or NE).
  ///
  static bool isRelational(Predicate P) {
    return !isEquality(P);
  }

  /// Exchange the two operands to this instruction in such a way that it does
  /// not modify the semantics of the instruction. The predicate value may be
  /// changed to retain the same result if the predicate is order dependent
  /// (e.g. ult).
  /// Swap operands and adjust predicate.
  void swapOperands() {
    setPredicate(getSwappedPredicate());
    Op<0>().swap(Op<1>());
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ICmp;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               FCmpInst Class
//===----------------------------------------------------------------------===//

/// This instruction compares its operands according to the predicate given
/// to the constructor. It only operates on floating point values or packed
/// vectors of floating point values. The operands must be identical types.
/// Represents a floating point comparison operator.
class FCmpInst: public CmpInst {
  void AssertOK() {
    assert(isFPPredicate() && "Invalid FCmp predicate value");
    assert(getOperand(0)->getType() == getOperand(1)->getType() &&
           "Both operands to FCmp instruction are not of the same type!");
    // Check that the operands are the right type
    assert(getOperand(0)->getType()->isFPOrFPVectorTy() &&
           "Invalid operand types for FCmp instruction");
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FCmpInst
  FCmpInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics.
  FCmpInst(
    Instruction *InsertBefore, ///< Where to insert
    Predicate pred,  ///< The predicate to use for the comparison
    Value *LHS,      ///< The left-hand-side of the expression
    Value *RHS,      ///< The right-hand-side of the expression
    const Twine &NameStr = ""  ///< Name of the instruction
  ) : CmpInst(makeCmpResultType(LHS->getType()),
              Instruction::FCmp, pred, LHS, RHS, NameStr,
              InsertBefore) {
    AssertOK();
  }

  /// Constructor with insert-at-end semantics.
  FCmpInst(
    BasicBlock &InsertAtEnd, ///< Block to insert into.
    Predicate pred,  ///< The predicate to use for the comparison
    Value *LHS,      ///< The left-hand-side of the expression
    Value *RHS,      ///< The right-hand-side of the expression
    const Twine &NameStr = ""  ///< Name of the instruction
  ) : CmpInst(makeCmpResultType(LHS->getType()),
              Instruction::FCmp, pred, LHS, RHS, NameStr,
              &InsertAtEnd) {
    AssertOK();
  }

  /// Constructor with no-insertion semantics
  FCmpInst(
    Predicate Pred, ///< The predicate to use for the comparison
    Value *LHS,     ///< The left-hand-side of the expression
    Value *RHS,     ///< The right-hand-side of the expression
    const Twine &NameStr = "", ///< Name of the instruction
    Instruction *FlagsSource = nullptr
  ) : CmpInst(makeCmpResultType(LHS->getType()), Instruction::FCmp, Pred, LHS,
              RHS, NameStr, nullptr, FlagsSource) {
    AssertOK();
  }

  /// @returns true if the predicate of this instruction is EQ or NE.
  /// Determine if this is an equality predicate.
  static bool isEquality(Predicate Pred) {
    return Pred == FCMP_OEQ || Pred == FCMP_ONE || Pred == FCMP_UEQ ||
           Pred == FCMP_UNE;
  }

  /// @returns true if the predicate of this instruction is EQ or NE.
  /// Determine if this is an equality predicate.
  bool isEquality() const { return isEquality(getPredicate()); }

  /// @returns true if the predicate of this instruction is commutative.
  /// Determine if this is a commutative predicate.
  bool isCommutative() const {
    return isEquality() ||
           getPredicate() == FCMP_FALSE ||
           getPredicate() == FCMP_TRUE ||
           getPredicate() == FCMP_ORD ||
           getPredicate() == FCMP_UNO;
  }

  /// @returns true if the predicate is relational (not EQ or NE).
  /// Determine if this a relational predicate.
  bool isRelational() const { return !isEquality(); }

  /// Exchange the two operands to this instruction in such a way that it does
  /// not modify the semantics of the instruction. The predicate value may be
  /// changed to retain the same result if the predicate is order dependent
  /// (e.g. ult).
  /// Swap operands and adjust predicate.
  void swapOperands() {
    setPredicate(getSwappedPredicate());
    Op<0>().swap(Op<1>());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::FCmp;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
/// This class represents a function call, abstracting a target
/// machine's calling convention.  This class uses low bit of the SubClassData
/// field to indicate whether or not this is a tail call.  The rest of the bits
/// hold the calling convention of the call.
///
class CallInst : public CallBase {
  CallInst(const CallInst &CI);

  /// Construct a CallInst given a range of arguments.
  /// Construct a CallInst from a range of arguments
  inline CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                  ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr,
                  Instruction *InsertBefore);

  inline CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                  const Twine &NameStr, Instruction *InsertBefore)
      : CallInst(Ty, Func, Args, None, NameStr, InsertBefore) {}

  /// Construct a CallInst given a range of arguments.
  /// Construct a CallInst from a range of arguments
  inline CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                  ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr,
                  BasicBlock *InsertAtEnd);

  explicit CallInst(FunctionType *Ty, Value *F, const Twine &NameStr,
                    Instruction *InsertBefore);

  CallInst(FunctionType *ty, Value *F, const Twine &NameStr,
           BasicBlock *InsertAtEnd);

  void init(FunctionType *FTy, Value *Func, ArrayRef<Value *> Args,
            ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr);
  void init(FunctionType *FTy, Value *Func, const Twine &NameStr);

  /// Compute the number of operands to allocate.
  static int ComputeNumOperands(int NumArgs, int NumBundleInputs = 0) {
    // We need one operand for the called function, plus the input operand
    // counts provided.
    return 1 + NumArgs + NumBundleInputs;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CallInst *cloneImpl() const;

public:
  static CallInst *Create(FunctionType *Ty, Value *F, const Twine &NameStr = "",
                          Instruction *InsertBefore = nullptr) {
    return new (ComputeNumOperands(0)) CallInst(Ty, F, NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr,
                          Instruction *InsertBefore = nullptr) {
    return new (ComputeNumOperands(Args.size()))
        CallInst(Ty, Func, Args, None, NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                          ArrayRef<OperandBundleDef> Bundles = None,
                          const Twine &NameStr = "",
                          Instruction *InsertBefore = nullptr) {
    const int NumOperands =
        ComputeNumOperands(Args.size(), CountBundleInputs(Bundles));
    const unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        CallInst(Ty, Func, Args, Bundles, NameStr, InsertBefore);
  }

  static CallInst *Create(FunctionType *Ty, Value *F, const Twine &NameStr,
                          BasicBlock *InsertAtEnd) {
    return new (ComputeNumOperands(0)) CallInst(Ty, F, NameStr, InsertAtEnd);
  }

  static CallInst *Create(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return new (ComputeNumOperands(Args.size()))
        CallInst(Ty, Func, Args, None, NameStr, InsertAtEnd);
  }

  static CallInst *Create(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                          ArrayRef<OperandBundleDef> Bundles,
                          const Twine &NameStr, BasicBlock *InsertAtEnd) {
    const int NumOperands =
        ComputeNumOperands(Args.size(), CountBundleInputs(Bundles));
    const unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        CallInst(Ty, Func, Args, Bundles, NameStr, InsertAtEnd);
  }

  static CallInst *Create(Function *Func, const Twine &NameStr = "",
                          Instruction *InsertBefore = nullptr) {
    return Create(Func->getFunctionType(), Func, NameStr, InsertBefore);
  }

  static CallInst *Create(Function *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr = "",
                          Instruction *InsertBefore = nullptr) {
    return Create(Func->getFunctionType(), Func, Args, NameStr, InsertBefore);
  }

  static CallInst *Create(Function *Func, const Twine &NameStr,
                          BasicBlock *InsertAtEnd) {
    return Create(Func->getFunctionType(), Func, NameStr, InsertAtEnd);
  }

  static CallInst *Create(Function *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(Func->getFunctionType(), Func, Args, NameStr, InsertAtEnd);
  }

  // Deprecated [opaque pointer types]
  static CallInst *Create(Value *Func, const Twine &NameStr = "",
                          Instruction *InsertBefore = nullptr) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, NameStr, InsertBefore);
  }

  // Deprecated [opaque pointer types]
  static CallInst *Create(Value *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr,
                          Instruction *InsertBefore = nullptr) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, Args, NameStr, InsertBefore);
  }

  // Deprecated [opaque pointer types]
  static CallInst *Create(Value *Func, ArrayRef<Value *> Args,
                          ArrayRef<OperandBundleDef> Bundles = None,
                          const Twine &NameStr = "",
                          Instruction *InsertBefore = nullptr) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, Args, Bundles, NameStr, InsertBefore);
  }

  // Deprecated [opaque pointer types]
  static CallInst *Create(Value *Func, const Twine &NameStr,
                          BasicBlock *InsertAtEnd) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, NameStr, InsertAtEnd);
  }

  // Deprecated [opaque pointer types]
  static CallInst *Create(Value *Func, ArrayRef<Value *> Args,
                          const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, Args, NameStr, InsertAtEnd);
  }

  // Deprecated [opaque pointer types]
  static CallInst *Create(Value *Func, ArrayRef<Value *> Args,
                          ArrayRef<OperandBundleDef> Bundles,
                          const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, Args, Bundles, NameStr, InsertAtEnd);
  }

  /// Create a clone of \p CI with a different set of operand bundles and
  /// insert it before \p InsertPt.
  ///
  /// The returned call instruction is identical \p CI in every way except that
  /// the operand bundles for the new instruction are set to the operand bundles
  /// in \p Bundles.
  static CallInst *Create(CallInst *CI, ArrayRef<OperandBundleDef> Bundles,
                          Instruction *InsertPt = nullptr);

  /// Generate the IR for a call to malloc:
  /// 1. Compute the malloc call's argument as the specified type's size,
  ///    possibly multiplied by the array size if the array size is not
  ///    constant 1.
  /// 2. Call malloc with that argument.
  /// 3. Bitcast the result of the malloc call to the specified type.
  static Instruction *CreateMalloc(Instruction *InsertBefore, Type *IntPtrTy,
                                   Type *AllocTy, Value *AllocSize,
                                   Value *ArraySize = nullptr,
                                   Function *MallocF = nullptr,
                                   const Twine &Name = "");
  static Instruction *CreateMalloc(BasicBlock *InsertAtEnd, Type *IntPtrTy,
                                   Type *AllocTy, Value *AllocSize,
                                   Value *ArraySize = nullptr,
                                   Function *MallocF = nullptr,
                                   const Twine &Name = "");
  static Instruction *CreateMalloc(Instruction *InsertBefore, Type *IntPtrTy,
                                   Type *AllocTy, Value *AllocSize,
                                   Value *ArraySize = nullptr,
                                   ArrayRef<OperandBundleDef> Bundles = None,
                                   Function *MallocF = nullptr,
                                   const Twine &Name = "");
  static Instruction *CreateMalloc(BasicBlock *InsertAtEnd, Type *IntPtrTy,
                                   Type *AllocTy, Value *AllocSize,
                                   Value *ArraySize = nullptr,
                                   ArrayRef<OperandBundleDef> Bundles = None,
                                   Function *MallocF = nullptr,
                                   const Twine &Name = "");
  /// Generate the IR for a call to the builtin free function.
  static Instruction *CreateFree(Value *Source, Instruction *InsertBefore);
  static Instruction *CreateFree(Value *Source, BasicBlock *InsertAtEnd);
  static Instruction *CreateFree(Value *Source,
                                 ArrayRef<OperandBundleDef> Bundles,
                                 Instruction *InsertBefore);
  static Instruction *CreateFree(Value *Source,
                                 ArrayRef<OperandBundleDef> Bundles,
                                 BasicBlock *InsertAtEnd);

  // Note that 'musttail' implies 'tail'.
  enum TailCallKind {
    TCK_None = 0,
    TCK_Tail = 1,
    TCK_MustTail = 2,
    TCK_NoTail = 3
  };
  TailCallKind getTailCallKind() const {
    return TailCallKind(getSubclassDataFromInstruction() & 3);
  }

  bool isTailCall() const {
    unsigned Kind = getSubclassDataFromInstruction() & 3;
    return Kind == TCK_Tail || Kind == TCK_MustTail;
  }

  bool isMustTailCall() const {
    return (getSubclassDataFromInstruction() & 3) == TCK_MustTail;
  }

  bool isNoTailCall() const {
    return (getSubclassDataFromInstruction() & 3) == TCK_NoTail;
  }

  void setTailCall(bool isTC = true) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~3) |
                               unsigned(isTC ? TCK_Tail : TCK_None));
  }

  void setTailCallKind(TailCallKind TCK) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~3) |
                               unsigned(TCK));
  }

  /// Return true if the call can return twice
  bool canReturnTwice() const { return hasFnAttr(Attribute::ReturnsTwice); }
  void setCanReturnTwice() {
    addAttribute(AttributeList::FunctionIndex, Attribute::ReturnsTwice);
  }

  /// Check if this call is an inline asm statement.
  bool isInlineAsm() const { return isa<InlineAsm>(getCalledOperand()); }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Call;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }
};

CallInst::CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                   ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr,
                   BasicBlock *InsertAtEnd)
    : CallBase(Ty->getReturnType(), Instruction::Call,
               OperandTraits<CallBase>::op_end(this) -
                   (Args.size() + CountBundleInputs(Bundles) + 1),
               unsigned(Args.size() + CountBundleInputs(Bundles) + 1),
               InsertAtEnd) {
  init(Ty, Func, Args, Bundles, NameStr);
}

CallInst::CallInst(FunctionType *Ty, Value *Func, ArrayRef<Value *> Args,
                   ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr,
                   Instruction *InsertBefore)
    : CallBase(Ty->getReturnType(), Instruction::Call,
               OperandTraits<CallBase>::op_end(this) -
                   (Args.size() + CountBundleInputs(Bundles) + 1),
               unsigned(Args.size() + CountBundleInputs(Bundles) + 1),
               InsertBefore) {
  init(Ty, Func, Args, Bundles, NameStr);
}

//===----------------------------------------------------------------------===//
//                               SelectInst Class
//===----------------------------------------------------------------------===//

/// This class represents the LLVM 'select' instruction.
///
class SelectInst : public Instruction {
  SelectInst(Value *C, Value *S1, Value *S2, const Twine &NameStr,
             Instruction *InsertBefore)
    : Instruction(S1->getType(), Instruction::Select,
                  &Op<0>(), 3, InsertBefore) {
    init(C, S1, S2);
    setName(NameStr);
  }

  SelectInst(Value *C, Value *S1, Value *S2, const Twine &NameStr,
             BasicBlock *InsertAtEnd)
    : Instruction(S1->getType(), Instruction::Select,
                  &Op<0>(), 3, InsertAtEnd) {
    init(C, S1, S2);
    setName(NameStr);
  }

  void init(Value *C, Value *S1, Value *S2) {
    assert(!areInvalidOperands(C, S1, S2) && "Invalid operands for select");
    Op<0>() = C;
    Op<1>() = S1;
    Op<2>() = S2;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  SelectInst *cloneImpl() const;

public:
  static SelectInst *Create(Value *C, Value *S1, Value *S2,
                            const Twine &NameStr = "",
                            Instruction *InsertBefore = nullptr,
                            Instruction *MDFrom = nullptr) {
    SelectInst *Sel = new(3) SelectInst(C, S1, S2, NameStr, InsertBefore);
    if (MDFrom)
      Sel->copyMetadata(*MDFrom);
    return Sel;
  }

  static SelectInst *Create(Value *C, Value *S1, Value *S2,
                            const Twine &NameStr,
                            BasicBlock *InsertAtEnd) {
    return new(3) SelectInst(C, S1, S2, NameStr, InsertAtEnd);
  }

  const Value *getCondition() const { return Op<0>(); }
  const Value *getTrueValue() const { return Op<1>(); }
  const Value *getFalseValue() const { return Op<2>(); }
  Value *getCondition() { return Op<0>(); }
  Value *getTrueValue() { return Op<1>(); }
  Value *getFalseValue() { return Op<2>(); }

  void setCondition(Value *V) { Op<0>() = V; }
  void setTrueValue(Value *V) { Op<1>() = V; }
  void setFalseValue(Value *V) { Op<2>() = V; }

  /// Return a string if the specified operands are invalid
  /// for a select operation, otherwise return null.
  static const char *areInvalidOperands(Value *Cond, Value *True, Value *False);

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  OtherOps getOpcode() const {
    return static_cast<OtherOps>(Instruction::getOpcode());
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Select;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<SelectInst> : public FixedNumOperandTraits<SelectInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(SelectInst, Value)

//===----------------------------------------------------------------------===//
//                                VAArgInst Class
//===----------------------------------------------------------------------===//

/// This class represents the va_arg llvm instruction, which returns
/// an argument of the specified type given a va_list and increments that list
///
class VAArgInst : public UnaryInstruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  VAArgInst *cloneImpl() const;

public:
  VAArgInst(Value *List, Type *Ty, const Twine &NameStr = "",
             Instruction *InsertBefore = nullptr)
    : UnaryInstruction(Ty, VAArg, List, InsertBefore) {
    setName(NameStr);
  }

  VAArgInst(Value *List, Type *Ty, const Twine &NameStr,
            BasicBlock *InsertAtEnd)
    : UnaryInstruction(Ty, VAArg, List, InsertAtEnd) {
    setName(NameStr);
  }

  Value *getPointerOperand() { return getOperand(0); }
  const Value *getPointerOperand() const { return getOperand(0); }
  static unsigned getPointerOperandIndex() { return 0U; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == VAArg;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                ExtractElementInst Class
//===----------------------------------------------------------------------===//

/// This instruction extracts a single (scalar)
/// element from a VectorType value
///
class ExtractElementInst : public Instruction {
  ExtractElementInst(Value *Vec, Value *Idx, const Twine &NameStr = "",
                     Instruction *InsertBefore = nullptr);
  ExtractElementInst(Value *Vec, Value *Idx, const Twine &NameStr,
                     BasicBlock *InsertAtEnd);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ExtractElementInst *cloneImpl() const;

public:
  static ExtractElementInst *Create(Value *Vec, Value *Idx,
                                   const Twine &NameStr = "",
                                   Instruction *InsertBefore = nullptr) {
    return new(2) ExtractElementInst(Vec, Idx, NameStr, InsertBefore);
  }

  static ExtractElementInst *Create(Value *Vec, Value *Idx,
                                   const Twine &NameStr,
                                   BasicBlock *InsertAtEnd) {
    return new(2) ExtractElementInst(Vec, Idx, NameStr, InsertAtEnd);
  }

  /// Return true if an extractelement instruction can be
  /// formed with the specified operands.
  static bool isValidOperands(const Value *Vec, const Value *Idx);

  Value *getVectorOperand() { return Op<0>(); }
  Value *getIndexOperand() { return Op<1>(); }
  const Value *getVectorOperand() const { return Op<0>(); }
  const Value *getIndexOperand() const { return Op<1>(); }

  VectorType *getVectorOperandType() const {
    return cast<VectorType>(getVectorOperand()->getType());
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ExtractElement;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<ExtractElementInst> :
  public FixedNumOperandTraits<ExtractElementInst, 2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ExtractElementInst, Value)

//===----------------------------------------------------------------------===//
//                                InsertElementInst Class
//===----------------------------------------------------------------------===//

/// This instruction inserts a single (scalar)
/// element into a VectorType value
///
class InsertElementInst : public Instruction {
  InsertElementInst(Value *Vec, Value *NewElt, Value *Idx,
                    const Twine &NameStr = "",
                    Instruction *InsertBefore = nullptr);
  InsertElementInst(Value *Vec, Value *NewElt, Value *Idx, const Twine &NameStr,
                    BasicBlock *InsertAtEnd);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  InsertElementInst *cloneImpl() const;

public:
  static InsertElementInst *Create(Value *Vec, Value *NewElt, Value *Idx,
                                   const Twine &NameStr = "",
                                   Instruction *InsertBefore = nullptr) {
    return new(3) InsertElementInst(Vec, NewElt, Idx, NameStr, InsertBefore);
  }

  static InsertElementInst *Create(Value *Vec, Value *NewElt, Value *Idx,
                                   const Twine &NameStr,
                                   BasicBlock *InsertAtEnd) {
    return new(3) InsertElementInst(Vec, NewElt, Idx, NameStr, InsertAtEnd);
  }

  /// Return true if an insertelement instruction can be
  /// formed with the specified operands.
  static bool isValidOperands(const Value *Vec, const Value *NewElt,
                              const Value *Idx);

  /// Overload to return most specific vector type.
  ///
  VectorType *getType() const {
    return cast<VectorType>(Instruction::getType());
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::InsertElement;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<InsertElementInst> :
  public FixedNumOperandTraits<InsertElementInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(InsertElementInst, Value)

//===----------------------------------------------------------------------===//
//                           ShuffleVectorInst Class
//===----------------------------------------------------------------------===//

/// This instruction constructs a fixed permutation of two
/// input vectors.
///
class ShuffleVectorInst : public Instruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ShuffleVectorInst *cloneImpl() const;

public:
  ShuffleVectorInst(Value *V1, Value *V2, Value *Mask,
                    const Twine &NameStr = "",
                    Instruction *InsertBefor = nullptr);
  ShuffleVectorInst(Value *V1, Value *V2, Value *Mask,
                    const Twine &NameStr, BasicBlock *InsertAtEnd);

  // allocate space for exactly three operands
  void *operator new(size_t s) {
    return User::operator new(s, 3);
  }

  /// Return true if a shufflevector instruction can be
  /// formed with the specified operands.
  static bool isValidOperands(const Value *V1, const Value *V2,
                              const Value *Mask);

  /// Overload to return most specific vector type.
  ///
  VectorType *getType() const {
    return cast<VectorType>(Instruction::getType());
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  Constant *getMask() const {
    return cast<Constant>(getOperand(2));
  }

  /// Return the shuffle mask value for the specified element of the mask.
  /// Return -1 if the element is undef.
  static int getMaskValue(const Constant *Mask, unsigned Elt);

  /// Return the shuffle mask value of this instruction for the given element
  /// index. Return -1 if the element is undef.
  int getMaskValue(unsigned Elt) const {
    return getMaskValue(getMask(), Elt);
  }

  /// Convert the input shuffle mask operand to a vector of integers. Undefined
  /// elements of the mask are returned as -1.
  static void getShuffleMask(const Constant *Mask,
                             SmallVectorImpl<int> &Result);

  /// Return the mask for this instruction as a vector of integers. Undefined
  /// elements of the mask are returned as -1.
  void getShuffleMask(SmallVectorImpl<int> &Result) const {
    return getShuffleMask(getMask(), Result);
  }

  SmallVector<int, 16> getShuffleMask() const {
    SmallVector<int, 16> Mask;
    getShuffleMask(Mask);
    return Mask;
  }

  /// Return true if this shuffle returns a vector with a different number of
  /// elements than its source vectors.
  /// Examples: shufflevector <4 x n> A, <4 x n> B, <1,2,3>
  ///           shufflevector <4 x n> A, <4 x n> B, <1,2,3,4,5>
  bool changesLength() const {
    unsigned NumSourceElts = Op<0>()->getType()->getVectorNumElements();
    unsigned NumMaskElts = getMask()->getType()->getVectorNumElements();
    return NumSourceElts != NumMaskElts;
  }

  /// Return true if this shuffle returns a vector with a greater number of
  /// elements than its source vectors.
  /// Example: shufflevector <2 x n> A, <2 x n> B, <1,2,3>
  bool increasesLength() const {
    unsigned NumSourceElts = Op<0>()->getType()->getVectorNumElements();
    unsigned NumMaskElts = getMask()->getType()->getVectorNumElements();
    return NumSourceElts < NumMaskElts;
  }

  /// Return true if this shuffle mask chooses elements from exactly one source
  /// vector.
  /// Example: <7,5,undef,7>
  /// This assumes that vector operands are the same length as the mask.
  static bool isSingleSourceMask(ArrayRef<int> Mask);
  static bool isSingleSourceMask(const Constant *Mask) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isSingleSourceMask(MaskAsInts);
  }

  /// Return true if this shuffle chooses elements from exactly one source
  /// vector without changing the length of that vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <3,0,undef,3>
  /// TODO: Optionally allow length-changing shuffles.
  bool isSingleSource() const {
    return !changesLength() && isSingleSourceMask(getMask());
  }

  /// Return true if this shuffle mask chooses elements from exactly one source
  /// vector without lane crossings. A shuffle using this mask is not
  /// necessarily a no-op because it may change the number of elements from its
  /// input vectors or it may provide demanded bits knowledge via undef lanes.
  /// Example: <undef,undef,2,3>
  static bool isIdentityMask(ArrayRef<int> Mask);
  static bool isIdentityMask(const Constant *Mask) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isIdentityMask(MaskAsInts);
  }

  /// Return true if this shuffle chooses elements from exactly one source
  /// vector without lane crossings and does not change the number of elements
  /// from its input vectors.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <4,undef,6,undef>
  bool isIdentity() const {
    return !changesLength() && isIdentityMask(getShuffleMask());
  }

  /// Return true if this shuffle lengthens exactly one source vector with
  /// undefs in the high elements.
  bool isIdentityWithPadding() const;

  /// Return true if this shuffle extracts the first N elements of exactly one
  /// source vector.
  bool isIdentityWithExtract() const;

  /// Return true if this shuffle concatenates its 2 source vectors. This
  /// returns false if either input is undefined. In that case, the shuffle is
  /// is better classified as an identity with padding operation.
  bool isConcat() const;

  /// Return true if this shuffle mask chooses elements from its source vectors
  /// without lane crossings. A shuffle using this mask would be
  /// equivalent to a vector select with a constant condition operand.
  /// Example: <4,1,6,undef>
  /// This returns false if the mask does not choose from both input vectors.
  /// In that case, the shuffle is better classified as an identity shuffle.
  /// This assumes that vector operands are the same length as the mask
  /// (a length-changing shuffle can never be equivalent to a vector select).
  static bool isSelectMask(ArrayRef<int> Mask);
  static bool isSelectMask(const Constant *Mask) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isSelectMask(MaskAsInts);
  }

  /// Return true if this shuffle chooses elements from its source vectors
  /// without lane crossings and all operands have the same number of elements.
  /// In other words, this shuffle is equivalent to a vector select with a
  /// constant condition operand.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <undef,1,6,3>
  /// This returns false if the mask does not choose from both input vectors.
  /// In that case, the shuffle is better classified as an identity shuffle.
  /// TODO: Optionally allow length-changing shuffles.
  bool isSelect() const {
    return !changesLength() && isSelectMask(getMask());
  }

  /// Return true if this shuffle mask swaps the order of elements from exactly
  /// one source vector.
  /// Example: <7,6,undef,4>
  /// This assumes that vector operands are the same length as the mask.
  static bool isReverseMask(ArrayRef<int> Mask);
  static bool isReverseMask(const Constant *Mask) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isReverseMask(MaskAsInts);
  }

  /// Return true if this shuffle swaps the order of elements from exactly
  /// one source vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <3,undef,1,undef>
  /// TODO: Optionally allow length-changing shuffles.
  bool isReverse() const {
    return !changesLength() && isReverseMask(getMask());
  }

  /// Return true if this shuffle mask chooses all elements with the same value
  /// as the first element of exactly one source vector.
  /// Example: <4,undef,undef,4>
  /// This assumes that vector operands are the same length as the mask.
  static bool isZeroEltSplatMask(ArrayRef<int> Mask);
  static bool isZeroEltSplatMask(const Constant *Mask) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isZeroEltSplatMask(MaskAsInts);
  }

  /// Return true if all elements of this shuffle are the same value as the
  /// first element of exactly one source vector without changing the length
  /// of that vector.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <undef,0,undef,0>
  /// TODO: Optionally allow length-changing shuffles.
  /// TODO: Optionally allow splats from other elements.
  bool isZeroEltSplat() const {
    return !changesLength() && isZeroEltSplatMask(getMask());
  }

  /// Return true if this shuffle mask is a transpose mask.
  /// Transpose vector masks transpose a 2xn matrix. They read corresponding
  /// even- or odd-numbered vector elements from two n-dimensional source
  /// vectors and write each result into consecutive elements of an
  /// n-dimensional destination vector. Two shuffles are necessary to complete
  /// the transpose, one for the even elements and another for the odd elements.
  /// This description closely follows how the TRN1 and TRN2 AArch64
  /// instructions operate.
  ///
  /// For example, a simple 2x2 matrix can be transposed with:
  ///
  ///   ; Original matrix
  ///   m0 = < a, b >
  ///   m1 = < c, d >
  ///
  ///   ; Transposed matrix
  ///   t0 = < a, c > = shufflevector m0, m1, < 0, 2 >
  ///   t1 = < b, d > = shufflevector m0, m1, < 1, 3 >
  ///
  /// For matrices having greater than n columns, the resulting nx2 transposed
  /// matrix is stored in two result vectors such that one vector contains
  /// interleaved elements from all the even-numbered rows and the other vector
  /// contains interleaved elements from all the odd-numbered rows. For example,
  /// a 2x4 matrix can be transposed with:
  ///
  ///   ; Original matrix
  ///   m0 = < a, b, c, d >
  ///   m1 = < e, f, g, h >
  ///
  ///   ; Transposed matrix
  ///   t0 = < a, e, c, g > = shufflevector m0, m1 < 0, 4, 2, 6 >
  ///   t1 = < b, f, d, h > = shufflevector m0, m1 < 1, 5, 3, 7 >
  static bool isTransposeMask(ArrayRef<int> Mask);
  static bool isTransposeMask(const Constant *Mask) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isTransposeMask(MaskAsInts);
  }

  /// Return true if this shuffle transposes the elements of its inputs without
  /// changing the length of the vectors. This operation may also be known as a
  /// merge or interleave. See the description for isTransposeMask() for the
  /// exact specification.
  /// Example: shufflevector <4 x n> A, <4 x n> B, <0,4,2,6>
  bool isTranspose() const {
    return !changesLength() && isTransposeMask(getMask());
  }

  /// Return true if this shuffle mask is an extract subvector mask.
  /// A valid extract subvector mask returns a smaller vector from a single
  /// source operand. The base extraction index is returned as well.
  static bool isExtractSubvectorMask(ArrayRef<int> Mask, int NumSrcElts,
                                     int &Index);
  static bool isExtractSubvectorMask(const Constant *Mask, int NumSrcElts,
                                     int &Index) {
    assert(Mask->getType()->isVectorTy() && "Shuffle needs vector constant.");
    SmallVector<int, 16> MaskAsInts;
    getShuffleMask(Mask, MaskAsInts);
    return isExtractSubvectorMask(MaskAsInts, NumSrcElts, Index);
  }

  /// Return true if this shuffle mask is an extract subvector mask.
  bool isExtractSubvectorMask(int &Index) const {
    int NumSrcElts = Op<0>()->getType()->getVectorNumElements();
    return isExtractSubvectorMask(getMask(), NumSrcElts, Index);
  }

  /// Change values in a shuffle permute mask assuming the two vector operands
  /// of length InVecNumElts have swapped position.
  static void commuteShuffleMask(MutableArrayRef<int> Mask,
                                 unsigned InVecNumElts) {
    for (int &Idx : Mask) {
      if (Idx == -1)
        continue;
      Idx = Idx < (int)InVecNumElts ? Idx + InVecNumElts : Idx - InVecNumElts;
      assert(Idx >= 0 && Idx < (int)InVecNumElts * 2 &&
             "shufflevector mask index out of range");
    }
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ShuffleVector;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<ShuffleVectorInst> :
  public FixedNumOperandTraits<ShuffleVectorInst, 3> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ShuffleVectorInst, Value)

//===----------------------------------------------------------------------===//
//                                ExtractValueInst Class
//===----------------------------------------------------------------------===//

/// This instruction extracts a struct member or array
/// element value from an aggregate value.
///
class ExtractValueInst : public UnaryInstruction {
  SmallVector<unsigned, 4> Indices;

  ExtractValueInst(const ExtractValueInst &EVI);

  /// Constructors - Create a extractvalue instruction with a base aggregate
  /// value and a list of indices.  The first ctor can optionally insert before
  /// an existing instruction, the second appends the new instruction to the
  /// specified BasicBlock.
  inline ExtractValueInst(Value *Agg,
                          ArrayRef<unsigned> Idxs,
                          const Twine &NameStr,
                          Instruction *InsertBefore);
  inline ExtractValueInst(Value *Agg,
                          ArrayRef<unsigned> Idxs,
                          const Twine &NameStr, BasicBlock *InsertAtEnd);

  void init(ArrayRef<unsigned> Idxs, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ExtractValueInst *cloneImpl() const;

public:
  static ExtractValueInst *Create(Value *Agg,
                                  ArrayRef<unsigned> Idxs,
                                  const Twine &NameStr = "",
                                  Instruction *InsertBefore = nullptr) {
    return new
      ExtractValueInst(Agg, Idxs, NameStr, InsertBefore);
  }

  static ExtractValueInst *Create(Value *Agg,
                                  ArrayRef<unsigned> Idxs,
                                  const Twine &NameStr,
                                  BasicBlock *InsertAtEnd) {
    return new ExtractValueInst(Agg, Idxs, NameStr, InsertAtEnd);
  }

  /// Returns the type of the element that would be extracted
  /// with an extractvalue instruction with the specified parameters.
  ///
  /// Null is returned if the indices are invalid for the specified type.
  static Type *getIndexedType(Type *Agg, ArrayRef<unsigned> Idxs);

  using idx_iterator = const unsigned*;

  inline idx_iterator idx_begin() const { return Indices.begin(); }
  inline idx_iterator idx_end()   const { return Indices.end(); }
  inline iterator_range<idx_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getAggregateOperand() {
    return getOperand(0);
  }
  const Value *getAggregateOperand() const {
    return getOperand(0);
  }
  static unsigned getAggregateOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  ArrayRef<unsigned> getIndices() const {
    return Indices;
  }

  unsigned getNumIndices() const {
    return (unsigned)Indices.size();
  }

  bool hasIndices() const {
    return true;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::ExtractValue;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

ExtractValueInst::ExtractValueInst(Value *Agg,
                                   ArrayRef<unsigned> Idxs,
                                   const Twine &NameStr,
                                   Instruction *InsertBefore)
  : UnaryInstruction(checkGEPType(getIndexedType(Agg->getType(), Idxs)),
                     ExtractValue, Agg, InsertBefore) {
  init(Idxs, NameStr);
}

ExtractValueInst::ExtractValueInst(Value *Agg,
                                   ArrayRef<unsigned> Idxs,
                                   const Twine &NameStr,
                                   BasicBlock *InsertAtEnd)
  : UnaryInstruction(checkGEPType(getIndexedType(Agg->getType(), Idxs)),
                     ExtractValue, Agg, InsertAtEnd) {
  init(Idxs, NameStr);
}

//===----------------------------------------------------------------------===//
//                                InsertValueInst Class
//===----------------------------------------------------------------------===//

/// This instruction inserts a struct field of array element
/// value into an aggregate value.
///
class InsertValueInst : public Instruction {
  SmallVector<unsigned, 4> Indices;

  InsertValueInst(const InsertValueInst &IVI);

  /// Constructors - Create a insertvalue instruction with a base aggregate
  /// value, a value to insert, and a list of indices.  The first ctor can
  /// optionally insert before an existing instruction, the second appends
  /// the new instruction to the specified BasicBlock.
  inline InsertValueInst(Value *Agg, Value *Val,
                         ArrayRef<unsigned> Idxs,
                         const Twine &NameStr,
                         Instruction *InsertBefore);
  inline InsertValueInst(Value *Agg, Value *Val,
                         ArrayRef<unsigned> Idxs,
                         const Twine &NameStr, BasicBlock *InsertAtEnd);

  /// Constructors - These two constructors are convenience methods because one
  /// and two index insertvalue instructions are so common.
  InsertValueInst(Value *Agg, Value *Val, unsigned Idx,
                  const Twine &NameStr = "",
                  Instruction *InsertBefore = nullptr);
  InsertValueInst(Value *Agg, Value *Val, unsigned Idx, const Twine &NameStr,
                  BasicBlock *InsertAtEnd);

  void init(Value *Agg, Value *Val, ArrayRef<unsigned> Idxs,
            const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  InsertValueInst *cloneImpl() const;

public:
  // allocate space for exactly two operands
  void *operator new(size_t s) {
    return User::operator new(s, 2);
  }

  static InsertValueInst *Create(Value *Agg, Value *Val,
                                 ArrayRef<unsigned> Idxs,
                                 const Twine &NameStr = "",
                                 Instruction *InsertBefore = nullptr) {
    return new InsertValueInst(Agg, Val, Idxs, NameStr, InsertBefore);
  }

  static InsertValueInst *Create(Value *Agg, Value *Val,
                                 ArrayRef<unsigned> Idxs,
                                 const Twine &NameStr,
                                 BasicBlock *InsertAtEnd) {
    return new InsertValueInst(Agg, Val, Idxs, NameStr, InsertAtEnd);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  using idx_iterator = const unsigned*;

  inline idx_iterator idx_begin() const { return Indices.begin(); }
  inline idx_iterator idx_end()   const { return Indices.end(); }
  inline iterator_range<idx_iterator> indices() const {
    return make_range(idx_begin(), idx_end());
  }

  Value *getAggregateOperand() {
    return getOperand(0);
  }
  const Value *getAggregateOperand() const {
    return getOperand(0);
  }
  static unsigned getAggregateOperandIndex() {
    return 0U;                      // get index for modifying correct operand
  }

  Value *getInsertedValueOperand() {
    return getOperand(1);
  }
  const Value *getInsertedValueOperand() const {
    return getOperand(1);
  }
  static unsigned getInsertedValueOperandIndex() {
    return 1U;                      // get index for modifying correct operand
  }

  ArrayRef<unsigned> getIndices() const {
    return Indices;
  }

  unsigned getNumIndices() const {
    return (unsigned)Indices.size();
  }

  bool hasIndices() const {
    return true;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::InsertValue;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<InsertValueInst> :
  public FixedNumOperandTraits<InsertValueInst, 2> {
};

InsertValueInst::InsertValueInst(Value *Agg,
                                 Value *Val,
                                 ArrayRef<unsigned> Idxs,
                                 const Twine &NameStr,
                                 Instruction *InsertBefore)
  : Instruction(Agg->getType(), InsertValue,
                OperandTraits<InsertValueInst>::op_begin(this),
                2, InsertBefore) {
  init(Agg, Val, Idxs, NameStr);
}

InsertValueInst::InsertValueInst(Value *Agg,
                                 Value *Val,
                                 ArrayRef<unsigned> Idxs,
                                 const Twine &NameStr,
                                 BasicBlock *InsertAtEnd)
  : Instruction(Agg->getType(), InsertValue,
                OperandTraits<InsertValueInst>::op_begin(this),
                2, InsertAtEnd) {
  init(Agg, Val, Idxs, NameStr);
}

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(InsertValueInst, Value)

//===----------------------------------------------------------------------===//
//                               PHINode Class
//===----------------------------------------------------------------------===//

// PHINode - The PHINode class is used to represent the magical mystical PHI
// node, that can not exist in nature, but can be synthesized in a computer
// scientist's overactive imagination.
//
class PHINode : public Instruction {
  /// The number of operands actually allocated.  NumOperands is
  /// the number actually in use.
  unsigned ReservedSpace;

  PHINode(const PHINode &PN);

  explicit PHINode(Type *Ty, unsigned NumReservedValues,
                   const Twine &NameStr = "",
                   Instruction *InsertBefore = nullptr)
    : Instruction(Ty, Instruction::PHI, nullptr, 0, InsertBefore),
      ReservedSpace(NumReservedValues) {
    setName(NameStr);
    allocHungoffUses(ReservedSpace);
  }

  PHINode(Type *Ty, unsigned NumReservedValues, const Twine &NameStr,
          BasicBlock *InsertAtEnd)
    : Instruction(Ty, Instruction::PHI, nullptr, 0, InsertAtEnd),
      ReservedSpace(NumReservedValues) {
    setName(NameStr);
    allocHungoffUses(ReservedSpace);
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  PHINode *cloneImpl() const;

  // allocHungoffUses - this is more complicated than the generic
  // User::allocHungoffUses, because we have to allocate Uses for the incoming
  // values and pointers to the incoming blocks, all in one allocation.
  void allocHungoffUses(unsigned N) {
    User::allocHungoffUses(N, /* IsPhi */ true);
  }

public:
  /// Constructors - NumReservedValues is a hint for the number of incoming
  /// edges that this phi node will have (use 0 if you really have no idea).
  static PHINode *Create(Type *Ty, unsigned NumReservedValues,
                         const Twine &NameStr = "",
                         Instruction *InsertBefore = nullptr) {
    return new PHINode(Ty, NumReservedValues, NameStr, InsertBefore);
  }

  static PHINode *Create(Type *Ty, unsigned NumReservedValues,
                         const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return new PHINode(Ty, NumReservedValues, NameStr, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Block iterator interface. This provides access to the list of incoming
  // basic blocks, which parallels the list of incoming values.

  using block_iterator = BasicBlock **;
  using const_block_iterator = BasicBlock * const *;

  block_iterator block_begin() {
    Use::UserRef *ref =
      reinterpret_cast<Use::UserRef*>(op_begin() + ReservedSpace);
    return reinterpret_cast<block_iterator>(ref + 1);
  }

  const_block_iterator block_begin() const {
    const Use::UserRef *ref =
      reinterpret_cast<const Use::UserRef*>(op_begin() + ReservedSpace);
    return reinterpret_cast<const_block_iterator>(ref + 1);
  }

  block_iterator block_end() {
    return block_begin() + getNumOperands();
  }

  const_block_iterator block_end() const {
    return block_begin() + getNumOperands();
  }

  iterator_range<block_iterator> blocks() {
    return make_range(block_begin(), block_end());
  }

  iterator_range<const_block_iterator> blocks() const {
    return make_range(block_begin(), block_end());
  }

  op_range incoming_values() { return operands(); }

  const_op_range incoming_values() const { return operands(); }

  /// Return the number of incoming edges
  ///
  unsigned getNumIncomingValues() const { return getNumOperands(); }

  /// Return incoming value number x
  ///
  Value *getIncomingValue(unsigned i) const {
    return getOperand(i);
  }
  void setIncomingValue(unsigned i, Value *V) {
    assert(V && "PHI node got a null value!");
    assert(getType() == V->getType() &&
           "All operands to PHI node must be the same type as the PHI node!");
    setOperand(i, V);
  }

  static unsigned getOperandNumForIncomingValue(unsigned i) {
    return i;
  }

  static unsigned getIncomingValueNumForOperand(unsigned i) {
    return i;
  }

  /// Return incoming basic block number @p i.
  ///
  BasicBlock *getIncomingBlock(unsigned i) const {
    return block_begin()[i];
  }

  /// Return incoming basic block corresponding
  /// to an operand of the PHI.
  ///
  BasicBlock *getIncomingBlock(const Use &U) const {
    assert(this == U.getUser() && "Iterator doesn't point to PHI's Uses?");
    return getIncomingBlock(unsigned(&U - op_begin()));
  }

  /// Return incoming basic block corresponding
  /// to value use iterator.
  ///
  BasicBlock *getIncomingBlock(Value::const_user_iterator I) const {
    return getIncomingBlock(I.getUse());
  }

  void setIncomingBlock(unsigned i, BasicBlock *BB) {
    assert(BB && "PHI node got a null basic block!");
    block_begin()[i] = BB;
  }

  /// Add an incoming value to the end of the PHI list
  ///
  void addIncoming(Value *V, BasicBlock *BB) {
    if (getNumOperands() == ReservedSpace)
      growOperands();  // Get more space!
    // Initialize some new operands.
    setNumHungOffUseOperands(getNumOperands() + 1);
    setIncomingValue(getNumOperands() - 1, V);
    setIncomingBlock(getNumOperands() - 1, BB);
  }

  /// Remove an incoming value.  This is useful if a
  /// predecessor basic block is deleted.  The value removed is returned.
  ///
  /// If the last incoming value for a PHI node is removed (and DeletePHIIfEmpty
  /// is true), the PHI node is destroyed and any uses of it are replaced with
  /// dummy values.  The only time there should be zero incoming values to a PHI
  /// node is when the block is dead, so this strategy is sound.
  ///
  Value *removeIncomingValue(unsigned Idx, bool DeletePHIIfEmpty = true);

  Value *removeIncomingValue(const BasicBlock *BB, bool DeletePHIIfEmpty=true) {
    int Idx = getBasicBlockIndex(BB);
    assert(Idx >= 0 && "Invalid basic block argument to remove!");
    return removeIncomingValue(Idx, DeletePHIIfEmpty);
  }

  /// Return the first index of the specified basic
  /// block in the value list for this PHI.  Returns -1 if no instance.
  ///
  int getBasicBlockIndex(const BasicBlock *BB) const {
    for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
      if (block_begin()[i] == BB)
        return i;
    return -1;
  }

  Value *getIncomingValueForBlock(const BasicBlock *BB) const {
    int Idx = getBasicBlockIndex(BB);
    assert(Idx >= 0 && "Invalid basic block argument!");
    return getIncomingValue(Idx);
  }

  /// If the specified PHI node always merges together the
  /// same value, return the value, otherwise return null.
  Value *hasConstantValue() const;

  /// Whether the specified PHI node always merges
  /// together the same value, assuming undefs are equal to a unique
  /// non-undef value.
  bool hasConstantOrUndefValue() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::PHI;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  void growOperands();
};

template <>
struct OperandTraits<PHINode> : public HungoffOperandTraits<2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(PHINode, Value)

//===----------------------------------------------------------------------===//
//                           LandingPadInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// The landingpad instruction holds all of the information
/// necessary to generate correct exception handling. The landingpad instruction
/// cannot be moved from the top of a landing pad block, which itself is
/// accessible only from the 'unwind' edge of an invoke. This uses the
/// SubclassData field in Value to store whether or not the landingpad is a
/// cleanup.
///
class LandingPadInst : public Instruction {
  /// The number of operands actually allocated.  NumOperands is
  /// the number actually in use.
  unsigned ReservedSpace;

  LandingPadInst(const LandingPadInst &LP);

public:
  enum ClauseType { Catch, Filter };

private:
  explicit LandingPadInst(Type *RetTy, unsigned NumReservedValues,
                          const Twine &NameStr, Instruction *InsertBefore);
  explicit LandingPadInst(Type *RetTy, unsigned NumReservedValues,
                          const Twine &NameStr, BasicBlock *InsertAtEnd);

  // Allocate space for exactly zero operands.
  void *operator new(size_t s) {
    return User::operator new(s);
  }

  void growOperands(unsigned Size);
  void init(unsigned NumReservedValues, const Twine &NameStr);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  LandingPadInst *cloneImpl() const;

public:
  /// Constructors - NumReservedClauses is a hint for the number of incoming
  /// clauses that this landingpad will have (use 0 if you really have no idea).
  static LandingPadInst *Create(Type *RetTy, unsigned NumReservedClauses,
                                const Twine &NameStr = "",
                                Instruction *InsertBefore = nullptr);
  static LandingPadInst *Create(Type *RetTy, unsigned NumReservedClauses,
                                const Twine &NameStr, BasicBlock *InsertAtEnd);

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Return 'true' if this landingpad instruction is a
  /// cleanup. I.e., it should be run when unwinding even if its landing pad
  /// doesn't catch the exception.
  bool isCleanup() const { return getSubclassDataFromInstruction() & 1; }

  /// Indicate that this landingpad instruction is a cleanup.
  void setCleanup(bool V) {
    setInstructionSubclassData((getSubclassDataFromInstruction() & ~1) |
                               (V ? 1 : 0));
  }

  /// Add a catch or filter clause to the landing pad.
  void addClause(Constant *ClauseVal);

  /// Get the value of the clause at index Idx. Use isCatch/isFilter to
  /// determine what type of clause this is.
  Constant *getClause(unsigned Idx) const {
    return cast<Constant>(getOperandList()[Idx]);
  }

  /// Return 'true' if the clause and index Idx is a catch clause.
  bool isCatch(unsigned Idx) const {
    return !isa<ArrayType>(getOperandList()[Idx]->getType());
  }

  /// Return 'true' if the clause and index Idx is a filter clause.
  bool isFilter(unsigned Idx) const {
    return isa<ArrayType>(getOperandList()[Idx]->getType());
  }

  /// Get the number of clauses for this landing pad.
  unsigned getNumClauses() const { return getNumOperands(); }

  /// Grow the size of the operand list to accommodate the new
  /// number of clauses.
  void reserveClauses(unsigned Size) { growOperands(Size); }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::LandingPad;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<LandingPadInst> : public HungoffOperandTraits<1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(LandingPadInst, Value)

//===----------------------------------------------------------------------===//
//                               ReturnInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Return a value (possibly void), from a function.  Execution
/// does not continue in this function any longer.
///
class ReturnInst : public Instruction {
  ReturnInst(const ReturnInst &RI);

private:
  // ReturnInst constructors:
  // ReturnInst()                  - 'ret void' instruction
  // ReturnInst(    null)          - 'ret void' instruction
  // ReturnInst(Value* X)          - 'ret X'    instruction
  // ReturnInst(    null, Inst *I) - 'ret void' instruction, insert before I
  // ReturnInst(Value* X, Inst *I) - 'ret X'    instruction, insert before I
  // ReturnInst(    null, BB *B)   - 'ret void' instruction, insert @ end of B
  // ReturnInst(Value* X, BB *B)   - 'ret X'    instruction, insert @ end of B
  //
  // NOTE: If the Value* passed is of type void then the constructor behaves as
  // if it was passed NULL.
  explicit ReturnInst(LLVMContext &C, Value *retVal = nullptr,
                      Instruction *InsertBefore = nullptr);
  ReturnInst(LLVMContext &C, Value *retVal, BasicBlock *InsertAtEnd);
  explicit ReturnInst(LLVMContext &C, BasicBlock *InsertAtEnd);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ReturnInst *cloneImpl() const;

public:
  static ReturnInst* Create(LLVMContext &C, Value *retVal = nullptr,
                            Instruction *InsertBefore = nullptr) {
    return new(!!retVal) ReturnInst(C, retVal, InsertBefore);
  }

  static ReturnInst* Create(LLVMContext &C, Value *retVal,
                            BasicBlock *InsertAtEnd) {
    return new(!!retVal) ReturnInst(C, retVal, InsertAtEnd);
  }

  static ReturnInst* Create(LLVMContext &C, BasicBlock *InsertAtEnd) {
    return new(0) ReturnInst(C, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Convenience accessor. Returns null if there is no return value.
  Value *getReturnValue() const {
    return getNumOperands() != 0 ? getOperand(0) : nullptr;
  }

  unsigned getNumSuccessors() const { return 0; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Ret);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned idx) const {
    llvm_unreachable("ReturnInst has no successors!");
  }

  void setSuccessor(unsigned idx, BasicBlock *B) {
    llvm_unreachable("ReturnInst has no successors!");
  }
};

template <>
struct OperandTraits<ReturnInst> : public VariadicOperandTraits<ReturnInst> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ReturnInst, Value)

//===----------------------------------------------------------------------===//
//                               BranchInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Conditional or Unconditional Branch instruction.
///
class BranchInst : public Instruction {
  /// Ops list - Branches are strange.  The operands are ordered:
  ///  [Cond, FalseDest,] TrueDest.  This makes some accessors faster because
  /// they don't have to check for cond/uncond branchness. These are mostly
  /// accessed relative from op_end().
  BranchInst(const BranchInst &BI);
  // BranchInst constructors (where {B, T, F} are blocks, and C is a condition):
  // BranchInst(BB *B)                           - 'br B'
  // BranchInst(BB* T, BB *F, Value *C)          - 'br C, T, F'
  // BranchInst(BB* B, Inst *I)                  - 'br B'        insert before I
  // BranchInst(BB* T, BB *F, Value *C, Inst *I) - 'br C, T, F', insert before I
  // BranchInst(BB* B, BB *I)                    - 'br B'        insert at end
  // BranchInst(BB* T, BB *F, Value *C, BB *I)   - 'br C, T, F', insert at end
  explicit BranchInst(BasicBlock *IfTrue, Instruction *InsertBefore = nullptr);
  BranchInst(BasicBlock *IfTrue, BasicBlock *IfFalse, Value *Cond,
             Instruction *InsertBefore = nullptr);
  BranchInst(BasicBlock *IfTrue, BasicBlock *InsertAtEnd);
  BranchInst(BasicBlock *IfTrue, BasicBlock *IfFalse, Value *Cond,
             BasicBlock *InsertAtEnd);

  void AssertOK();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  BranchInst *cloneImpl() const;

public:
  /// Iterator type that casts an operand to a basic block.
  ///
  /// This only makes sense because the successors are stored as adjacent
  /// operands for branch instructions.
  struct succ_op_iterator
      : iterator_adaptor_base<succ_op_iterator, value_op_iterator,
                              std::random_access_iterator_tag, BasicBlock *,
                              ptrdiff_t, BasicBlock *, BasicBlock *> {
    explicit succ_op_iterator(value_op_iterator I) : iterator_adaptor_base(I) {}

    BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    BasicBlock *operator->() const { return operator*(); }
  };

  /// The const version of `succ_op_iterator`.
  struct const_succ_op_iterator
      : iterator_adaptor_base<const_succ_op_iterator, const_value_op_iterator,
                              std::random_access_iterator_tag,
                              const BasicBlock *, ptrdiff_t, const BasicBlock *,
                              const BasicBlock *> {
    explicit const_succ_op_iterator(const_value_op_iterator I)
        : iterator_adaptor_base(I) {}

    const BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    const BasicBlock *operator->() const { return operator*(); }
  };

  static BranchInst *Create(BasicBlock *IfTrue,
                            Instruction *InsertBefore = nullptr) {
    return new(1) BranchInst(IfTrue, InsertBefore);
  }

  static BranchInst *Create(BasicBlock *IfTrue, BasicBlock *IfFalse,
                            Value *Cond, Instruction *InsertBefore = nullptr) {
    return new(3) BranchInst(IfTrue, IfFalse, Cond, InsertBefore);
  }

  static BranchInst *Create(BasicBlock *IfTrue, BasicBlock *InsertAtEnd) {
    return new(1) BranchInst(IfTrue, InsertAtEnd);
  }

  static BranchInst *Create(BasicBlock *IfTrue, BasicBlock *IfFalse,
                            Value *Cond, BasicBlock *InsertAtEnd) {
    return new(3) BranchInst(IfTrue, IfFalse, Cond, InsertAtEnd);
  }

  /// Transparently provide more efficient getOperand methods.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  bool isUnconditional() const { return getNumOperands() == 1; }
  bool isConditional()   const { return getNumOperands() == 3; }

  Value *getCondition() const {
    assert(isConditional() && "Cannot get condition of an uncond branch!");
    return Op<-3>();
  }

  void setCondition(Value *V) {
    assert(isConditional() && "Cannot set condition of unconditional branch!");
    Op<-3>() = V;
  }

  unsigned getNumSuccessors() const { return 1+isConditional(); }

  BasicBlock *getSuccessor(unsigned i) const {
    assert(i < getNumSuccessors() && "Successor # out of range for Branch!");
    return cast_or_null<BasicBlock>((&Op<-1>() - i)->get());
  }

  void setSuccessor(unsigned idx, BasicBlock *NewSucc) {
    assert(idx < getNumSuccessors() && "Successor # out of range for Branch!");
    *(&Op<-1>() - idx) = NewSucc;
  }

  /// Swap the successors of this branch instruction.
  ///
  /// Swaps the successors of the branch instruction. This also swaps any
  /// branch weight metadata associated with the instruction so that it
  /// continues to map correctly to each operand.
  void swapSuccessors();

  iterator_range<succ_op_iterator> successors() {
    return make_range(
        succ_op_iterator(std::next(value_op_begin(), isConditional() ? 1 : 0)),
        succ_op_iterator(value_op_end()));
  }

  iterator_range<const_succ_op_iterator> successors() const {
    return make_range(const_succ_op_iterator(
                          std::next(value_op_begin(), isConditional() ? 1 : 0)),
                      const_succ_op_iterator(value_op_end()));
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Br);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<BranchInst> : public VariadicOperandTraits<BranchInst, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(BranchInst, Value)

//===----------------------------------------------------------------------===//
//                               SwitchInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Multiway switch
///
class SwitchInst : public Instruction {
  unsigned ReservedSpace;

  // Operand[0]    = Value to switch on
  // Operand[1]    = Default basic block destination
  // Operand[2n  ] = Value to match
  // Operand[2n+1] = BasicBlock to go to on match
  SwitchInst(const SwitchInst &SI);

  /// Create a new switch instruction, specifying a value to switch on and a
  /// default destination. The number of additional cases can be specified here
  /// to make memory allocation more efficient. This constructor can also
  /// auto-insert before another instruction.
  SwitchInst(Value *Value, BasicBlock *Default, unsigned NumCases,
             Instruction *InsertBefore);

  /// Create a new switch instruction, specifying a value to switch on and a
  /// default destination. The number of additional cases can be specified here
  /// to make memory allocation more efficient. This constructor also
  /// auto-inserts at the end of the specified BasicBlock.
  SwitchInst(Value *Value, BasicBlock *Default, unsigned NumCases,
             BasicBlock *InsertAtEnd);

  // allocate space for exactly zero operands
  void *operator new(size_t s) {
    return User::operator new(s);
  }

  void init(Value *Value, BasicBlock *Default, unsigned NumReserved);
  void growOperands();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  SwitchInst *cloneImpl() const;

public:
  // -2
  static const unsigned DefaultPseudoIndex = static_cast<unsigned>(~0L-1);

  template <typename CaseHandleT> class CaseIteratorImpl;

  /// A handle to a particular switch case. It exposes a convenient interface
  /// to both the case value and the successor block.
  ///
  /// We define this as a template and instantiate it to form both a const and
  /// non-const handle.
  template <typename SwitchInstT, typename ConstantIntT, typename BasicBlockT>
  class CaseHandleImpl {
    // Directly befriend both const and non-const iterators.
    friend class SwitchInst::CaseIteratorImpl<
        CaseHandleImpl<SwitchInstT, ConstantIntT, BasicBlockT>>;

  protected:
    // Expose the switch type we're parameterized with to the iterator.
    using SwitchInstType = SwitchInstT;

    SwitchInstT *SI;
    ptrdiff_t Index;

    CaseHandleImpl() = default;
    CaseHandleImpl(SwitchInstT *SI, ptrdiff_t Index) : SI(SI), Index(Index) {}

  public:
    /// Resolves case value for current case.
    ConstantIntT *getCaseValue() const {
      assert((unsigned)Index < SI->getNumCases() &&
             "Index out the number of cases.");
      return reinterpret_cast<ConstantIntT *>(SI->getOperand(2 + Index * 2));
    }

    /// Resolves successor for current case.
    BasicBlockT *getCaseSuccessor() const {
      assert(((unsigned)Index < SI->getNumCases() ||
              (unsigned)Index == DefaultPseudoIndex) &&
             "Index out the number of cases.");
      return SI->getSuccessor(getSuccessorIndex());
    }

    /// Returns number of current case.
    unsigned getCaseIndex() const { return Index; }

    /// Returns successor index for current case successor.
    unsigned getSuccessorIndex() const {
      assert(((unsigned)Index == DefaultPseudoIndex ||
              (unsigned)Index < SI->getNumCases()) &&
             "Index out the number of cases.");
      return (unsigned)Index != DefaultPseudoIndex ? Index + 1 : 0;
    }

    bool operator==(const CaseHandleImpl &RHS) const {
      assert(SI == RHS.SI && "Incompatible operators.");
      return Index == RHS.Index;
    }
  };

  using ConstCaseHandle =
      CaseHandleImpl<const SwitchInst, const ConstantInt, const BasicBlock>;

  class CaseHandle
      : public CaseHandleImpl<SwitchInst, ConstantInt, BasicBlock> {
    friend class SwitchInst::CaseIteratorImpl<CaseHandle>;

  public:
    CaseHandle(SwitchInst *SI, ptrdiff_t Index) : CaseHandleImpl(SI, Index) {}

    /// Sets the new value for current case.
    void setValue(ConstantInt *V) {
      assert((unsigned)Index < SI->getNumCases() &&
             "Index out the number of cases.");
      SI->setOperand(2 + Index*2, reinterpret_cast<Value*>(V));
    }

    /// Sets the new successor for current case.
    void setSuccessor(BasicBlock *S) {
      SI->setSuccessor(getSuccessorIndex(), S);
    }
  };

  template <typename CaseHandleT>
  class CaseIteratorImpl
      : public iterator_facade_base<CaseIteratorImpl<CaseHandleT>,
                                    std::random_access_iterator_tag,
                                    CaseHandleT> {
    using SwitchInstT = typename CaseHandleT::SwitchInstType;

    CaseHandleT Case;

  public:
    /// Default constructed iterator is in an invalid state until assigned to
    /// a case for a particular switch.
    CaseIteratorImpl() = default;

    /// Initializes case iterator for given SwitchInst and for given
    /// case number.
    CaseIteratorImpl(SwitchInstT *SI, unsigned CaseNum) : Case(SI, CaseNum) {}

    /// Initializes case iterator for given SwitchInst and for given
    /// successor index.
    static CaseIteratorImpl fromSuccessorIndex(SwitchInstT *SI,
                                               unsigned SuccessorIndex) {
      assert(SuccessorIndex < SI->getNumSuccessors() &&
             "Successor index # out of range!");
      return SuccessorIndex != 0 ? CaseIteratorImpl(SI, SuccessorIndex - 1)
                                 : CaseIteratorImpl(SI, DefaultPseudoIndex);
    }

    /// Support converting to the const variant. This will be a no-op for const
    /// variant.
    operator CaseIteratorImpl<ConstCaseHandle>() const {
      return CaseIteratorImpl<ConstCaseHandle>(Case.SI, Case.Index);
    }

    CaseIteratorImpl &operator+=(ptrdiff_t N) {
      // Check index correctness after addition.
      // Note: Index == getNumCases() means end().
      assert(Case.Index + N >= 0 &&
             (unsigned)(Case.Index + N) <= Case.SI->getNumCases() &&
             "Case.Index out the number of cases.");
      Case.Index += N;
      return *this;
    }
    CaseIteratorImpl &operator-=(ptrdiff_t N) {
      // Check index correctness after subtraction.
      // Note: Case.Index == getNumCases() means end().
      assert(Case.Index - N >= 0 &&
             (unsigned)(Case.Index - N) <= Case.SI->getNumCases() &&
             "Case.Index out the number of cases.");
      Case.Index -= N;
      return *this;
    }
    ptrdiff_t operator-(const CaseIteratorImpl &RHS) const {
      assert(Case.SI == RHS.Case.SI && "Incompatible operators.");
      return Case.Index - RHS.Case.Index;
    }
    bool operator==(const CaseIteratorImpl &RHS) const {
      return Case == RHS.Case;
    }
    bool operator<(const CaseIteratorImpl &RHS) const {
      assert(Case.SI == RHS.Case.SI && "Incompatible operators.");
      return Case.Index < RHS.Case.Index;
    }
    CaseHandleT &operator*() { return Case; }
    const CaseHandleT &operator*() const { return Case; }
  };

  using CaseIt = CaseIteratorImpl<CaseHandle>;
  using ConstCaseIt = CaseIteratorImpl<ConstCaseHandle>;

  static SwitchInst *Create(Value *Value, BasicBlock *Default,
                            unsigned NumCases,
                            Instruction *InsertBefore = nullptr) {
    return new SwitchInst(Value, Default, NumCases, InsertBefore);
  }

  static SwitchInst *Create(Value *Value, BasicBlock *Default,
                            unsigned NumCases, BasicBlock *InsertAtEnd) {
    return new SwitchInst(Value, Default, NumCases, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Accessor Methods for Switch stmt
  Value *getCondition() const { return getOperand(0); }
  void setCondition(Value *V) { setOperand(0, V); }

  BasicBlock *getDefaultDest() const {
    return cast<BasicBlock>(getOperand(1));
  }

  void setDefaultDest(BasicBlock *DefaultCase) {
    setOperand(1, reinterpret_cast<Value*>(DefaultCase));
  }

  /// Return the number of 'cases' in this switch instruction, excluding the
  /// default case.
  unsigned getNumCases() const {
    return getNumOperands()/2 - 1;
  }

  /// Returns a read/write iterator that points to the first case in the
  /// SwitchInst.
  CaseIt case_begin() {
    return CaseIt(this, 0);
  }

  /// Returns a read-only iterator that points to the first case in the
  /// SwitchInst.
  ConstCaseIt case_begin() const {
    return ConstCaseIt(this, 0);
  }

  /// Returns a read/write iterator that points one past the last in the
  /// SwitchInst.
  CaseIt case_end() {
    return CaseIt(this, getNumCases());
  }

  /// Returns a read-only iterator that points one past the last in the
  /// SwitchInst.
  ConstCaseIt case_end() const {
    return ConstCaseIt(this, getNumCases());
  }

  /// Iteration adapter for range-for loops.
  iterator_range<CaseIt> cases() {
    return make_range(case_begin(), case_end());
  }

  /// Constant iteration adapter for range-for loops.
  iterator_range<ConstCaseIt> cases() const {
    return make_range(case_begin(), case_end());
  }

  /// Returns an iterator that points to the default case.
  /// Note: this iterator allows to resolve successor only. Attempt
  /// to resolve case value causes an assertion.
  /// Also note, that increment and decrement also causes an assertion and
  /// makes iterator invalid.
  CaseIt case_default() {
    return CaseIt(this, DefaultPseudoIndex);
  }
  ConstCaseIt case_default() const {
    return ConstCaseIt(this, DefaultPseudoIndex);
  }

  /// Search all of the case values for the specified constant. If it is
  /// explicitly handled, return the case iterator of it, otherwise return
  /// default case iterator to indicate that it is handled by the default
  /// handler.
  CaseIt findCaseValue(const ConstantInt *C) {
    CaseIt I = llvm::find_if(
        cases(), [C](CaseHandle &Case) { return Case.getCaseValue() == C; });
    if (I != case_end())
      return I;

    return case_default();
  }
  ConstCaseIt findCaseValue(const ConstantInt *C) const {
    ConstCaseIt I = llvm::find_if(cases(), [C](ConstCaseHandle &Case) {
      return Case.getCaseValue() == C;
    });
    if (I != case_end())
      return I;

    return case_default();
  }

  /// Finds the unique case value for a given successor. Returns null if the
  /// successor is not found, not unique, or is the default case.
  ConstantInt *findCaseDest(BasicBlock *BB) {
    if (BB == getDefaultDest())
      return nullptr;

    ConstantInt *CI = nullptr;
    for (auto Case : cases()) {
      if (Case.getCaseSuccessor() != BB)
        continue;

      if (CI)
        return nullptr; // Multiple cases lead to BB.

      CI = Case.getCaseValue();
    }

    return CI;
  }

  /// Add an entry to the switch instruction.
  /// Note:
  /// This action invalidates case_end(). Old case_end() iterator will
  /// point to the added case.
  void addCase(ConstantInt *OnVal, BasicBlock *Dest);

  /// This method removes the specified case and its successor from the switch
  /// instruction. Note that this operation may reorder the remaining cases at
  /// index idx and above.
  /// Note:
  /// This action invalidates iterators for all cases following the one removed,
  /// including the case_end() iterator. It returns an iterator for the next
  /// case.
  CaseIt removeCase(CaseIt I);

  unsigned getNumSuccessors() const { return getNumOperands()/2; }
  BasicBlock *getSuccessor(unsigned idx) const {
    assert(idx < getNumSuccessors() &&"Successor idx out of range for switch!");
    return cast<BasicBlock>(getOperand(idx*2+1));
  }
  void setSuccessor(unsigned idx, BasicBlock *NewSucc) {
    assert(idx < getNumSuccessors() && "Successor # out of range for switch!");
    setOperand(idx * 2 + 1, NewSucc);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Switch;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<SwitchInst> : public HungoffOperandTraits<2> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(SwitchInst, Value)

//===----------------------------------------------------------------------===//
//                             IndirectBrInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Indirect Branch Instruction.
///
class IndirectBrInst : public Instruction {
  unsigned ReservedSpace;

  // Operand[0]   = Address to jump to
  // Operand[n+1] = n-th destination
  IndirectBrInst(const IndirectBrInst &IBI);

  /// Create a new indirectbr instruction, specifying an
  /// Address to jump to.  The number of expected destinations can be specified
  /// here to make memory allocation more efficient.  This constructor can also
  /// autoinsert before another instruction.
  IndirectBrInst(Value *Address, unsigned NumDests, Instruction *InsertBefore);

  /// Create a new indirectbr instruction, specifying an
  /// Address to jump to.  The number of expected destinations can be specified
  /// here to make memory allocation more efficient.  This constructor also
  /// autoinserts at the end of the specified BasicBlock.
  IndirectBrInst(Value *Address, unsigned NumDests, BasicBlock *InsertAtEnd);

  // allocate space for exactly zero operands
  void *operator new(size_t s) {
    return User::operator new(s);
  }

  void init(Value *Address, unsigned NumDests);
  void growOperands();

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  IndirectBrInst *cloneImpl() const;

public:
  /// Iterator type that casts an operand to a basic block.
  ///
  /// This only makes sense because the successors are stored as adjacent
  /// operands for indirectbr instructions.
  struct succ_op_iterator
      : iterator_adaptor_base<succ_op_iterator, value_op_iterator,
                              std::random_access_iterator_tag, BasicBlock *,
                              ptrdiff_t, BasicBlock *, BasicBlock *> {
    explicit succ_op_iterator(value_op_iterator I) : iterator_adaptor_base(I) {}

    BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    BasicBlock *operator->() const { return operator*(); }
  };

  /// The const version of `succ_op_iterator`.
  struct const_succ_op_iterator
      : iterator_adaptor_base<const_succ_op_iterator, const_value_op_iterator,
                              std::random_access_iterator_tag,
                              const BasicBlock *, ptrdiff_t, const BasicBlock *,
                              const BasicBlock *> {
    explicit const_succ_op_iterator(const_value_op_iterator I)
        : iterator_adaptor_base(I) {}

    const BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    const BasicBlock *operator->() const { return operator*(); }
  };

  static IndirectBrInst *Create(Value *Address, unsigned NumDests,
                                Instruction *InsertBefore = nullptr) {
    return new IndirectBrInst(Address, NumDests, InsertBefore);
  }

  static IndirectBrInst *Create(Value *Address, unsigned NumDests,
                                BasicBlock *InsertAtEnd) {
    return new IndirectBrInst(Address, NumDests, InsertAtEnd);
  }

  /// Provide fast operand accessors.
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Accessor Methods for IndirectBrInst instruction.
  Value *getAddress() { return getOperand(0); }
  const Value *getAddress() const { return getOperand(0); }
  void setAddress(Value *V) { setOperand(0, V); }

  /// return the number of possible destinations in this
  /// indirectbr instruction.
  unsigned getNumDestinations() const { return getNumOperands()-1; }

  /// Return the specified destination.
  BasicBlock *getDestination(unsigned i) { return getSuccessor(i); }
  const BasicBlock *getDestination(unsigned i) const { return getSuccessor(i); }

  /// Add a destination.
  ///
  void addDestination(BasicBlock *Dest);

  /// This method removes the specified successor from the
  /// indirectbr instruction.
  void removeDestination(unsigned i);

  unsigned getNumSuccessors() const { return getNumOperands()-1; }
  BasicBlock *getSuccessor(unsigned i) const {
    return cast<BasicBlock>(getOperand(i+1));
  }
  void setSuccessor(unsigned i, BasicBlock *NewSucc) {
    setOperand(i + 1, NewSucc);
  }

  iterator_range<succ_op_iterator> successors() {
    return make_range(succ_op_iterator(std::next(value_op_begin())),
                      succ_op_iterator(value_op_end()));
  }

  iterator_range<const_succ_op_iterator> successors() const {
    return make_range(const_succ_op_iterator(std::next(value_op_begin())),
                      const_succ_op_iterator(value_op_end()));
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::IndirectBr;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<IndirectBrInst> : public HungoffOperandTraits<1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(IndirectBrInst, Value)

//===----------------------------------------------------------------------===//
//                               InvokeInst Class
//===----------------------------------------------------------------------===//

/// Invoke instruction.  The SubclassData field is used to hold the
/// calling convention of the call.
///
class InvokeInst : public CallBase {
  /// The number of operands for this call beyond the called function,
  /// arguments, and operand bundles.
  static constexpr int NumExtraOperands = 2;

  /// The index from the end of the operand array to the normal destination.
  static constexpr int NormalDestOpEndIdx = -3;

  /// The index from the end of the operand array to the unwind destination.
  static constexpr int UnwindDestOpEndIdx = -2;

  InvokeInst(const InvokeInst &BI);

  /// Construct an InvokeInst given a range of arguments.
  ///
  /// Construct an InvokeInst from a range of arguments
  inline InvokeInst(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                    BasicBlock *IfException, ArrayRef<Value *> Args,
                    ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                    const Twine &NameStr, Instruction *InsertBefore);

  inline InvokeInst(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                    BasicBlock *IfException, ArrayRef<Value *> Args,
                    ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                    const Twine &NameStr, BasicBlock *InsertAtEnd);

  void init(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
            BasicBlock *IfException, ArrayRef<Value *> Args,
            ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr);

  /// Compute the number of operands to allocate.
  static int ComputeNumOperands(int NumArgs, int NumBundleInputs = 0) {
    // We need one operand for the called function, plus our extra operands and
    // the input operand counts provided.
    return 1 + NumExtraOperands + NumArgs + NumBundleInputs;
  }

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  InvokeInst *cloneImpl() const;

public:
  static InvokeInst *Create(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr,
                            Instruction *InsertBefore = nullptr) {
    int NumOperands = ComputeNumOperands(Args.size());
    return new (NumOperands)
        InvokeInst(Ty, Func, IfNormal, IfException, Args, None, NumOperands,
                   NameStr, InsertBefore);
  }

  static InvokeInst *Create(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles = None,
                            const Twine &NameStr = "",
                            Instruction *InsertBefore = nullptr) {
    int NumOperands =
        ComputeNumOperands(Args.size(), CountBundleInputs(Bundles));
    unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        InvokeInst(Ty, Func, IfNormal, IfException, Args, Bundles, NumOperands,
                   NameStr, InsertBefore);
  }

  static InvokeInst *Create(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr, BasicBlock *InsertAtEnd) {
    int NumOperands = ComputeNumOperands(Args.size());
    return new (NumOperands)
        InvokeInst(Ty, Func, IfNormal, IfException, Args, None, NumOperands,
                   NameStr, InsertAtEnd);
  }

  static InvokeInst *Create(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles,
                            const Twine &NameStr, BasicBlock *InsertAtEnd) {
    int NumOperands =
        ComputeNumOperands(Args.size(), CountBundleInputs(Bundles));
    unsigned DescriptorBytes = Bundles.size() * sizeof(BundleOpInfo);

    return new (NumOperands, DescriptorBytes)
        InvokeInst(Ty, Func, IfNormal, IfException, Args, Bundles, NumOperands,
                   NameStr, InsertAtEnd);
  }

  static InvokeInst *Create(Function *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr,
                            Instruction *InsertBefore = nullptr) {
    return Create(Func->getFunctionType(), Func, IfNormal, IfException, Args,
                  None, NameStr, InsertBefore);
  }

  static InvokeInst *Create(Function *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles = None,
                            const Twine &NameStr = "",
                            Instruction *InsertBefore = nullptr) {
    return Create(Func->getFunctionType(), Func, IfNormal, IfException, Args,
                  Bundles, NameStr, InsertBefore);
  }

  static InvokeInst *Create(Function *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(Func->getFunctionType(), Func, IfNormal, IfException, Args,
                  NameStr, InsertAtEnd);
  }

  static InvokeInst *Create(Function *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles,
                            const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(Func->getFunctionType(), Func, IfNormal, IfException, Args,
                  Bundles, NameStr, InsertAtEnd);
  }

  // Deprecated [opaque pointer types]
  static InvokeInst *Create(Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr,
                            Instruction *InsertBefore = nullptr) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, IfNormal, IfException, Args, None, NameStr,
                  InsertBefore);
  }

  // Deprecated [opaque pointer types]
  static InvokeInst *Create(Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles = None,
                            const Twine &NameStr = "",
                            Instruction *InsertBefore = nullptr) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, IfNormal, IfException, Args, Bundles, NameStr,
                  InsertBefore);
  }

  // Deprecated [opaque pointer types]
  static InvokeInst *Create(Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, IfNormal, IfException, Args, NameStr, InsertAtEnd);
  }

  // Deprecated [opaque pointer types]
  static InvokeInst *Create(Value *Func, BasicBlock *IfNormal,
                            BasicBlock *IfException, ArrayRef<Value *> Args,
                            ArrayRef<OperandBundleDef> Bundles,
                            const Twine &NameStr, BasicBlock *InsertAtEnd) {
    return Create(cast<FunctionType>(
                      cast<PointerType>(Func->getType())->getElementType()),
                  Func, IfNormal, IfException, Args, Bundles, NameStr,
                  InsertAtEnd);
  }

  /// Create a clone of \p II with a different set of operand bundles and
  /// insert it before \p InsertPt.
  ///
  /// The returned invoke instruction is identical to \p II in every way except
  /// that the operand bundles for the new instruction are set to the operand
  /// bundles in \p Bundles.
  static InvokeInst *Create(InvokeInst *II, ArrayRef<OperandBundleDef> Bundles,
                            Instruction *InsertPt = nullptr);

  /// Determine if the call should not perform indirect branch tracking.
  bool doesNoCfCheck() const { return hasFnAttr(Attribute::NoCfCheck); }

  /// Determine if the call cannot unwind.
  bool doesNotThrow() const { return hasFnAttr(Attribute::NoUnwind); }
  void setDoesNotThrow() {
    addAttribute(AttributeList::FunctionIndex, Attribute::NoUnwind);
  }

  // get*Dest - Return the destination basic blocks...
  BasicBlock *getNormalDest() const {
    return cast<BasicBlock>(Op<NormalDestOpEndIdx>());
  }
  BasicBlock *getUnwindDest() const {
    return cast<BasicBlock>(Op<UnwindDestOpEndIdx>());
  }
  void setNormalDest(BasicBlock *B) {
    Op<NormalDestOpEndIdx>() = reinterpret_cast<Value *>(B);
  }
  void setUnwindDest(BasicBlock *B) {
    Op<UnwindDestOpEndIdx>() = reinterpret_cast<Value *>(B);
  }

  /// Get the landingpad instruction from the landing pad
  /// block (the unwind destination).
  LandingPadInst *getLandingPadInst() const;

  BasicBlock *getSuccessor(unsigned i) const {
    assert(i < 2 && "Successor # out of range for invoke!");
    return i == 0 ? getNormalDest() : getUnwindDest();
  }

  void setSuccessor(unsigned i, BasicBlock *NewSucc) {
    assert(i < 2 && "Successor # out of range for invoke!");
    if (i == 0)
      setNormalDest(NewSucc);
    else
      setUnwindDest(NewSucc);
  }

  unsigned getNumSuccessors() const { return 2; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::Invoke);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:

  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }
};

InvokeInst::InvokeInst(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                       BasicBlock *IfException, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                       const Twine &NameStr, Instruction *InsertBefore)
    : CallBase(Ty->getReturnType(), Instruction::Invoke,
               OperandTraits<CallBase>::op_end(this) - NumOperands, NumOperands,
               InsertBefore) {
  init(Ty, Func, IfNormal, IfException, Args, Bundles, NameStr);
}

InvokeInst::InvokeInst(FunctionType *Ty, Value *Func, BasicBlock *IfNormal,
                       BasicBlock *IfException, ArrayRef<Value *> Args,
                       ArrayRef<OperandBundleDef> Bundles, int NumOperands,
                       const Twine &NameStr, BasicBlock *InsertAtEnd)
    : CallBase(Ty->getReturnType(), Instruction::Invoke,
               OperandTraits<CallBase>::op_end(this) - NumOperands, NumOperands,
               InsertAtEnd) {
  init(Ty, Func, IfNormal, IfException, Args, Bundles, NameStr);
}

//===----------------------------------------------------------------------===//
//                              ResumeInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// Resume the propagation of an exception.
///
class ResumeInst : public Instruction {
  ResumeInst(const ResumeInst &RI);

  explicit ResumeInst(Value *Exn, Instruction *InsertBefore=nullptr);
  ResumeInst(Value *Exn, BasicBlock *InsertAtEnd);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  ResumeInst *cloneImpl() const;

public:
  static ResumeInst *Create(Value *Exn, Instruction *InsertBefore = nullptr) {
    return new(1) ResumeInst(Exn, InsertBefore);
  }

  static ResumeInst *Create(Value *Exn, BasicBlock *InsertAtEnd) {
    return new(1) ResumeInst(Exn, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Convenience accessor.
  Value *getValue() const { return Op<0>(); }

  unsigned getNumSuccessors() const { return 0; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Resume;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned idx) const {
    llvm_unreachable("ResumeInst has no successors!");
  }

  void setSuccessor(unsigned idx, BasicBlock *NewSucc) {
    llvm_unreachable("ResumeInst has no successors!");
  }
};

template <>
struct OperandTraits<ResumeInst> :
    public FixedNumOperandTraits<ResumeInst, 1> {
};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ResumeInst, Value)

//===----------------------------------------------------------------------===//
//                         CatchSwitchInst Class
//===----------------------------------------------------------------------===//
class CatchSwitchInst : public Instruction {
  /// The number of operands actually allocated.  NumOperands is
  /// the number actually in use.
  unsigned ReservedSpace;

  // Operand[0] = Outer scope
  // Operand[1] = Unwind block destination
  // Operand[n] = BasicBlock to go to on match
  CatchSwitchInst(const CatchSwitchInst &CSI);

  /// Create a new switch instruction, specifying a
  /// default destination.  The number of additional handlers can be specified
  /// here to make memory allocation more efficient.
  /// This constructor can also autoinsert before another instruction.
  CatchSwitchInst(Value *ParentPad, BasicBlock *UnwindDest,
                  unsigned NumHandlers, const Twine &NameStr,
                  Instruction *InsertBefore);

  /// Create a new switch instruction, specifying a
  /// default destination.  The number of additional handlers can be specified
  /// here to make memory allocation more efficient.
  /// This constructor also autoinserts at the end of the specified BasicBlock.
  CatchSwitchInst(Value *ParentPad, BasicBlock *UnwindDest,
                  unsigned NumHandlers, const Twine &NameStr,
                  BasicBlock *InsertAtEnd);

  // allocate space for exactly zero operands
  void *operator new(size_t s) { return User::operator new(s); }

  void init(Value *ParentPad, BasicBlock *UnwindDest, unsigned NumReserved);
  void growOperands(unsigned Size);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CatchSwitchInst *cloneImpl() const;

public:
  static CatchSwitchInst *Create(Value *ParentPad, BasicBlock *UnwindDest,
                                 unsigned NumHandlers,
                                 const Twine &NameStr = "",
                                 Instruction *InsertBefore = nullptr) {
    return new CatchSwitchInst(ParentPad, UnwindDest, NumHandlers, NameStr,
                               InsertBefore);
  }

  static CatchSwitchInst *Create(Value *ParentPad, BasicBlock *UnwindDest,
                                 unsigned NumHandlers, const Twine &NameStr,
                                 BasicBlock *InsertAtEnd) {
    return new CatchSwitchInst(ParentPad, UnwindDest, NumHandlers, NameStr,
                               InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  // Accessor Methods for CatchSwitch stmt
  Value *getParentPad() const { return getOperand(0); }
  void setParentPad(Value *ParentPad) { setOperand(0, ParentPad); }

  // Accessor Methods for CatchSwitch stmt
  bool hasUnwindDest() const { return getSubclassDataFromInstruction() & 1; }
  bool unwindsToCaller() const { return !hasUnwindDest(); }
  BasicBlock *getUnwindDest() const {
    if (hasUnwindDest())
      return cast<BasicBlock>(getOperand(1));
    return nullptr;
  }
  void setUnwindDest(BasicBlock *UnwindDest) {
    assert(UnwindDest);
    assert(hasUnwindDest());
    setOperand(1, UnwindDest);
  }

  /// return the number of 'handlers' in this catchswitch
  /// instruction, except the default handler
  unsigned getNumHandlers() const {
    if (hasUnwindDest())
      return getNumOperands() - 2;
    return getNumOperands() - 1;
  }

private:
  static BasicBlock *handler_helper(Value *V) { return cast<BasicBlock>(V); }
  static const BasicBlock *handler_helper(const Value *V) {
    return cast<BasicBlock>(V);
  }

public:
  using DerefFnTy = BasicBlock *(*)(Value *);
  using handler_iterator = mapped_iterator<op_iterator, DerefFnTy>;
  using handler_range = iterator_range<handler_iterator>;
  using ConstDerefFnTy = const BasicBlock *(*)(const Value *);
  using const_handler_iterator =
      mapped_iterator<const_op_iterator, ConstDerefFnTy>;
  using const_handler_range = iterator_range<const_handler_iterator>;

  /// Returns an iterator that points to the first handler in CatchSwitchInst.
  handler_iterator handler_begin() {
    op_iterator It = op_begin() + 1;
    if (hasUnwindDest())
      ++It;
    return handler_iterator(It, DerefFnTy(handler_helper));
  }

  /// Returns an iterator that points to the first handler in the
  /// CatchSwitchInst.
  const_handler_iterator handler_begin() const {
    const_op_iterator It = op_begin() + 1;
    if (hasUnwindDest())
      ++It;
    return const_handler_iterator(It, ConstDerefFnTy(handler_helper));
  }

  /// Returns a read-only iterator that points one past the last
  /// handler in the CatchSwitchInst.
  handler_iterator handler_end() {
    return handler_iterator(op_end(), DerefFnTy(handler_helper));
  }

  /// Returns an iterator that points one past the last handler in the
  /// CatchSwitchInst.
  const_handler_iterator handler_end() const {
    return const_handler_iterator(op_end(), ConstDerefFnTy(handler_helper));
  }

  /// iteration adapter for range-for loops.
  handler_range handlers() {
    return make_range(handler_begin(), handler_end());
  }

  /// iteration adapter for range-for loops.
  const_handler_range handlers() const {
    return make_range(handler_begin(), handler_end());
  }

  /// Add an entry to the switch instruction...
  /// Note:
  /// This action invalidates handler_end(). Old handler_end() iterator will
  /// point to the added handler.
  void addHandler(BasicBlock *Dest);

  void removeHandler(handler_iterator HI);

  unsigned getNumSuccessors() const { return getNumOperands() - 1; }
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx < getNumSuccessors() &&
           "Successor # out of range for catchswitch!");
    return cast<BasicBlock>(getOperand(Idx + 1));
  }
  void setSuccessor(unsigned Idx, BasicBlock *NewSucc) {
    assert(Idx < getNumSuccessors() &&
           "Successor # out of range for catchswitch!");
    setOperand(Idx + 1, NewSucc);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::CatchSwitch;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

template <>
struct OperandTraits<CatchSwitchInst> : public HungoffOperandTraits<2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CatchSwitchInst, Value)

//===----------------------------------------------------------------------===//
//                               CleanupPadInst Class
//===----------------------------------------------------------------------===//
class CleanupPadInst : public FuncletPadInst {
private:
  explicit CleanupPadInst(Value *ParentPad, ArrayRef<Value *> Args,
                          unsigned Values, const Twine &NameStr,
                          Instruction *InsertBefore)
      : FuncletPadInst(Instruction::CleanupPad, ParentPad, Args, Values,
                       NameStr, InsertBefore) {}
  explicit CleanupPadInst(Value *ParentPad, ArrayRef<Value *> Args,
                          unsigned Values, const Twine &NameStr,
                          BasicBlock *InsertAtEnd)
      : FuncletPadInst(Instruction::CleanupPad, ParentPad, Args, Values,
                       NameStr, InsertAtEnd) {}

public:
  static CleanupPadInst *Create(Value *ParentPad, ArrayRef<Value *> Args = None,
                                const Twine &NameStr = "",
                                Instruction *InsertBefore = nullptr) {
    unsigned Values = 1 + Args.size();
    return new (Values)
        CleanupPadInst(ParentPad, Args, Values, NameStr, InsertBefore);
  }

  static CleanupPadInst *Create(Value *ParentPad, ArrayRef<Value *> Args,
                                const Twine &NameStr, BasicBlock *InsertAtEnd) {
    unsigned Values = 1 + Args.size();
    return new (Values)
        CleanupPadInst(ParentPad, Args, Values, NameStr, InsertAtEnd);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::CleanupPad;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CatchPadInst Class
//===----------------------------------------------------------------------===//
class CatchPadInst : public FuncletPadInst {
private:
  explicit CatchPadInst(Value *CatchSwitch, ArrayRef<Value *> Args,
                        unsigned Values, const Twine &NameStr,
                        Instruction *InsertBefore)
      : FuncletPadInst(Instruction::CatchPad, CatchSwitch, Args, Values,
                       NameStr, InsertBefore) {}
  explicit CatchPadInst(Value *CatchSwitch, ArrayRef<Value *> Args,
                        unsigned Values, const Twine &NameStr,
                        BasicBlock *InsertAtEnd)
      : FuncletPadInst(Instruction::CatchPad, CatchSwitch, Args, Values,
                       NameStr, InsertAtEnd) {}

public:
  static CatchPadInst *Create(Value *CatchSwitch, ArrayRef<Value *> Args,
                              const Twine &NameStr = "",
                              Instruction *InsertBefore = nullptr) {
    unsigned Values = 1 + Args.size();
    return new (Values)
        CatchPadInst(CatchSwitch, Args, Values, NameStr, InsertBefore);
  }

  static CatchPadInst *Create(Value *CatchSwitch, ArrayRef<Value *> Args,
                              const Twine &NameStr, BasicBlock *InsertAtEnd) {
    unsigned Values = 1 + Args.size();
    return new (Values)
        CatchPadInst(CatchSwitch, Args, Values, NameStr, InsertAtEnd);
  }

  /// Convenience accessors
  CatchSwitchInst *getCatchSwitch() const {
    return cast<CatchSwitchInst>(Op<-1>());
  }
  void setCatchSwitch(Value *CatchSwitch) {
    assert(CatchSwitch);
    Op<-1>() = CatchSwitch;
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::CatchPad;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                               CatchReturnInst Class
//===----------------------------------------------------------------------===//

class CatchReturnInst : public Instruction {
  CatchReturnInst(const CatchReturnInst &RI);
  CatchReturnInst(Value *CatchPad, BasicBlock *BB, Instruction *InsertBefore);
  CatchReturnInst(Value *CatchPad, BasicBlock *BB, BasicBlock *InsertAtEnd);

  void init(Value *CatchPad, BasicBlock *BB);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CatchReturnInst *cloneImpl() const;

public:
  static CatchReturnInst *Create(Value *CatchPad, BasicBlock *BB,
                                 Instruction *InsertBefore = nullptr) {
    assert(CatchPad);
    assert(BB);
    return new (2) CatchReturnInst(CatchPad, BB, InsertBefore);
  }

  static CatchReturnInst *Create(Value *CatchPad, BasicBlock *BB,
                                 BasicBlock *InsertAtEnd) {
    assert(CatchPad);
    assert(BB);
    return new (2) CatchReturnInst(CatchPad, BB, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  /// Convenience accessors.
  CatchPadInst *getCatchPad() const { return cast<CatchPadInst>(Op<0>()); }
  void setCatchPad(CatchPadInst *CatchPad) {
    assert(CatchPad);
    Op<0>() = CatchPad;
  }

  BasicBlock *getSuccessor() const { return cast<BasicBlock>(Op<1>()); }
  void setSuccessor(BasicBlock *NewSucc) {
    assert(NewSucc);
    Op<1>() = NewSucc;
  }
  unsigned getNumSuccessors() const { return 1; }

  /// Get the parentPad of this catchret's catchpad's catchswitch.
  /// The successor block is implicitly a member of this funclet.
  Value *getCatchSwitchParentPad() const {
    return getCatchPad()->getCatchSwitch()->getParentPad();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::CatchRet);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx < getNumSuccessors() && "Successor # out of range for catchret!");
    return getSuccessor();
  }

  void setSuccessor(unsigned Idx, BasicBlock *B) {
    assert(Idx < getNumSuccessors() && "Successor # out of range for catchret!");
    setSuccessor(B);
  }
};

template <>
struct OperandTraits<CatchReturnInst>
    : public FixedNumOperandTraits<CatchReturnInst, 2> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CatchReturnInst, Value)

//===----------------------------------------------------------------------===//
//                               CleanupReturnInst Class
//===----------------------------------------------------------------------===//

class CleanupReturnInst : public Instruction {
private:
  CleanupReturnInst(const CleanupReturnInst &RI);
  CleanupReturnInst(Value *CleanupPad, BasicBlock *UnwindBB, unsigned Values,
                    Instruction *InsertBefore = nullptr);
  CleanupReturnInst(Value *CleanupPad, BasicBlock *UnwindBB, unsigned Values,
                    BasicBlock *InsertAtEnd);

  void init(Value *CleanupPad, BasicBlock *UnwindBB);

protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  CleanupReturnInst *cloneImpl() const;

public:
  static CleanupReturnInst *Create(Value *CleanupPad,
                                   BasicBlock *UnwindBB = nullptr,
                                   Instruction *InsertBefore = nullptr) {
    assert(CleanupPad);
    unsigned Values = 1;
    if (UnwindBB)
      ++Values;
    return new (Values)
        CleanupReturnInst(CleanupPad, UnwindBB, Values, InsertBefore);
  }

  static CleanupReturnInst *Create(Value *CleanupPad, BasicBlock *UnwindBB,
                                   BasicBlock *InsertAtEnd) {
    assert(CleanupPad);
    unsigned Values = 1;
    if (UnwindBB)
      ++Values;
    return new (Values)
        CleanupReturnInst(CleanupPad, UnwindBB, Values, InsertAtEnd);
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);

  bool hasUnwindDest() const { return getSubclassDataFromInstruction() & 1; }
  bool unwindsToCaller() const { return !hasUnwindDest(); }

  /// Convenience accessor.
  CleanupPadInst *getCleanupPad() const {
    return cast<CleanupPadInst>(Op<0>());
  }
  void setCleanupPad(CleanupPadInst *CleanupPad) {
    assert(CleanupPad);
    Op<0>() = CleanupPad;
  }

  unsigned getNumSuccessors() const { return hasUnwindDest() ? 1 : 0; }

  BasicBlock *getUnwindDest() const {
    return hasUnwindDest() ? cast<BasicBlock>(Op<1>()) : nullptr;
  }
  void setUnwindDest(BasicBlock *NewDest) {
    assert(NewDest);
    assert(hasUnwindDest());
    Op<1>() = NewDest;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return (I->getOpcode() == Instruction::CleanupRet);
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned Idx) const {
    assert(Idx == 0);
    return getUnwindDest();
  }

  void setSuccessor(unsigned Idx, BasicBlock *B) {
    assert(Idx == 0);
    setUnwindDest(B);
  }

  // Shadow Instruction::setInstructionSubclassData with a private forwarding
  // method so that subclasses cannot accidentally use it.
  void setInstructionSubclassData(unsigned short D) {
    Instruction::setInstructionSubclassData(D);
  }
};

template <>
struct OperandTraits<CleanupReturnInst>
    : public VariadicOperandTraits<CleanupReturnInst, /*MINARITY=*/1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(CleanupReturnInst, Value)

//===----------------------------------------------------------------------===//
//                           UnreachableInst Class
//===----------------------------------------------------------------------===//

//===---------------------------------------------------------------------------
/// This function has undefined behavior.  In particular, the
/// presence of this instruction indicates some higher level knowledge that the
/// end of the block cannot be reached.
///
class UnreachableInst : public Instruction {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  UnreachableInst *cloneImpl() const;

public:
  explicit UnreachableInst(LLVMContext &C, Instruction *InsertBefore = nullptr);
  explicit UnreachableInst(LLVMContext &C, BasicBlock *InsertAtEnd);

  // allocate space for exactly zero operands
  void *operator new(size_t s) {
    return User::operator new(s, 0);
  }

  unsigned getNumSuccessors() const { return 0; }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Instruction::Unreachable;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

private:
  BasicBlock *getSuccessor(unsigned idx) const {
    llvm_unreachable("UnreachableInst has no successors!");
  }

  void setSuccessor(unsigned idx, BasicBlock *B) {
    llvm_unreachable("UnreachableInst has no successors!");
  }
};

//===----------------------------------------------------------------------===//
//                                 TruncInst Class
//===----------------------------------------------------------------------===//

/// This class represents a truncation of integer types.
class TruncInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical TruncInst
  TruncInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  TruncInst(
    Value *S,                           ///< The value to be truncated
    Type *Ty,                           ///< The (smaller) type to truncate to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  TruncInst(
    Value *S,                     ///< The value to be truncated
    Type *Ty,                     ///< The (smaller) type to truncate to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == Trunc;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 ZExtInst Class
//===----------------------------------------------------------------------===//

/// This class represents zero extension of integer types.
class ZExtInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical ZExtInst
  ZExtInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  ZExtInst(
    Value *S,                           ///< The value to be zero extended
    Type *Ty,                           ///< The type to zero extend to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end semantics.
  ZExtInst(
    Value *S,                     ///< The value to be zero extended
    Type *Ty,                     ///< The type to zero extend to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == ZExt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 SExtInst Class
//===----------------------------------------------------------------------===//

/// This class represents a sign extension of integer types.
class SExtInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical SExtInst
  SExtInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  SExtInst(
    Value *S,                           ///< The value to be sign extended
    Type *Ty,                           ///< The type to sign extend to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  SExtInst(
    Value *S,                     ///< The value to be sign extended
    Type *Ty,                     ///< The type to sign extend to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == SExt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPTruncInst Class
//===----------------------------------------------------------------------===//

/// This class represents a truncation of floating point types.
class FPTruncInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPTruncInst
  FPTruncInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPTruncInst(
    Value *S,                           ///< The value to be truncated
    Type *Ty,                           ///< The type to truncate to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-before-instruction semantics
  FPTruncInst(
    Value *S,                     ///< The value to be truncated
    Type *Ty,                     ///< The type to truncate to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPTrunc;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPExtInst Class
//===----------------------------------------------------------------------===//

/// This class represents an extension of floating point types.
class FPExtInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPExtInst
  FPExtInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPExtInst(
    Value *S,                           ///< The value to be extended
    Type *Ty,                           ///< The type to extend to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  FPExtInst(
    Value *S,                     ///< The value to be extended
    Type *Ty,                     ///< The type to extend to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPExt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 UIToFPInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast unsigned integer to floating point.
class UIToFPInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical UIToFPInst
  UIToFPInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  UIToFPInst(
    Value *S,                           ///< The value to be converted
    Type *Ty,                           ///< The type to convert to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  UIToFPInst(
    Value *S,                     ///< The value to be converted
    Type *Ty,                     ///< The type to convert to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == UIToFP;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 SIToFPInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from signed integer to floating point.
class SIToFPInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical SIToFPInst
  SIToFPInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  SIToFPInst(
    Value *S,                           ///< The value to be converted
    Type *Ty,                           ///< The type to convert to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  SIToFPInst(
    Value *S,                     ///< The value to be converted
    Type *Ty,                     ///< The type to convert to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == SIToFP;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPToUIInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from floating point to unsigned integer
class FPToUIInst  : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPToUIInst
  FPToUIInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPToUIInst(
    Value *S,                           ///< The value to be converted
    Type *Ty,                           ///< The type to convert to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  FPToUIInst(
    Value *S,                     ///< The value to be converted
    Type *Ty,                     ///< The type to convert to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< Where to insert the new instruction
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPToUI;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 FPToSIInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from floating point to signed integer.
class FPToSIInst  : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical FPToSIInst
  FPToSIInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  FPToSIInst(
    Value *S,                           ///< The value to be converted
    Type *Ty,                           ///< The type to convert to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  FPToSIInst(
    Value *S,                     ///< The value to be converted
    Type *Ty,                     ///< The type to convert to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == FPToSI;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 IntToPtrInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from an integer to a pointer.
class IntToPtrInst : public CastInst {
public:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Constructor with insert-before-instruction semantics
  IntToPtrInst(
    Value *S,                           ///< The value to be converted
    Type *Ty,                           ///< The type to convert to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  IntToPtrInst(
    Value *S,                     ///< The value to be converted
    Type *Ty,                     ///< The type to convert to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Clone an identical IntToPtrInst.
  IntToPtrInst *cloneImpl() const;

  /// Returns the address space of this instruction's pointer type.
  unsigned getAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == IntToPtr;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                                 PtrToIntInst Class
//===----------------------------------------------------------------------===//

/// This class represents a cast from a pointer to an integer.
class PtrToIntInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical PtrToIntInst.
  PtrToIntInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  PtrToIntInst(
    Value *S,                           ///< The value to be converted
    Type *Ty,                           ///< The type to convert to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  PtrToIntInst(
    Value *S,                     ///< The value to be converted
    Type *Ty,                     ///< The type to convert to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  /// Gets the pointer operand.
  Value *getPointerOperand() { return getOperand(0); }
  /// Gets the pointer operand.
  const Value *getPointerOperand() const { return getOperand(0); }
  /// Gets the operand index of the pointer operand.
  static unsigned getPointerOperandIndex() { return 0U; }

  /// Returns the address space of the pointer operand.
  unsigned getPointerAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == PtrToInt;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                             BitCastInst Class
//===----------------------------------------------------------------------===//

/// This class represents a no-op cast from one type to another.
class BitCastInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical BitCastInst.
  BitCastInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  BitCastInst(
    Value *S,                           ///< The value to be casted
    Type *Ty,                           ///< The type to casted to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  BitCastInst(
    Value *S,                     ///< The value to be casted
    Type *Ty,                     ///< The type to casted to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == BitCast;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }
};

//===----------------------------------------------------------------------===//
//                          AddrSpaceCastInst Class
//===----------------------------------------------------------------------===//

/// This class represents a conversion between pointers from one address space
/// to another.
class AddrSpaceCastInst : public CastInst {
protected:
  // Note: Instruction needs to be a friend here to call cloneImpl.
  friend class Instruction;

  /// Clone an identical AddrSpaceCastInst.
  AddrSpaceCastInst *cloneImpl() const;

public:
  /// Constructor with insert-before-instruction semantics
  AddrSpaceCastInst(
    Value *S,                           ///< The value to be casted
    Type *Ty,                           ///< The type to casted to
    const Twine &NameStr = "",          ///< A name for the new instruction
    Instruction *InsertBefore = nullptr ///< Where to insert the new instruction
  );

  /// Constructor with insert-at-end-of-block semantics
  AddrSpaceCastInst(
    Value *S,                     ///< The value to be casted
    Type *Ty,                     ///< The type to casted to
    const Twine &NameStr,         ///< A name for the new instruction
    BasicBlock *InsertAtEnd       ///< The block to insert the instruction into
  );

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Instruction *I) {
    return I->getOpcode() == AddrSpaceCast;
  }
  static bool classof(const Value *V) {
    return isa<Instruction>(V) && classof(cast<Instruction>(V));
  }

  /// Gets the pointer operand.
  Value *getPointerOperand() {
    return getOperand(0);
  }

  /// Gets the pointer operand.
  const Value *getPointerOperand() const {
    return getOperand(0);
  }

  /// Gets the operand index of the pointer operand.
  static unsigned getPointerOperandIndex() {
    return 0U;
  }

  /// Returns the address space of the pointer operand.
  unsigned getSrcAddressSpace() const {
    return getPointerOperand()->getType()->getPointerAddressSpace();
  }

  /// Returns the address space of the result.
  unsigned getDestAddressSpace() const {
    return getType()->getPointerAddressSpace();
  }
};

/// A helper function that returns the pointer operand of a load or store
/// instruction. Returns nullptr if not load or store.
inline Value *getLoadStorePointerOperand(Value *V) {
  if (auto *Load = dyn_cast<LoadInst>(V))
    return Load->getPointerOperand();
  if (auto *Store = dyn_cast<StoreInst>(V))
    return Store->getPointerOperand();
  return nullptr;
}

/// A helper function that returns the pointer operand of a load, store
/// or GEP instruction. Returns nullptr if not load, store, or GEP.
inline Value *getPointerOperand(Value *V) {
  if (auto *Ptr = getLoadStorePointerOperand(V))
    return Ptr;
  if (auto *Gep = dyn_cast<GetElementPtrInst>(V))
    return Gep->getPointerOperand();
  return nullptr;
}

/// A helper function that returns the alignment of load or store instruction.
inline unsigned getLoadStoreAlignment(Value *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getAlignment();
  return cast<StoreInst>(I)->getAlignment();
}

/// A helper function that returns the address space of the pointer operand of
/// load or store instruction.
inline unsigned getLoadStoreAddressSpace(Value *I) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) &&
         "Expected Load or Store instruction");
  if (auto *LI = dyn_cast<LoadInst>(I))
    return LI->getPointerAddressSpace();
  return cast<StoreInst>(I)->getPointerAddressSpace();
}

} // end namespace llvm

#endif // LLVM_IR_INSTRUCTIONS_H
