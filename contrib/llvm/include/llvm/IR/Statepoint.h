//===- llvm/IR/Statepoint.h - gc.statepoint utilities -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains utility functions and a wrapper class analogous to
// CallSite for accessing the fields of gc.statepoint, gc.relocate,
// gc.result intrinsics; and some general utilities helpful when dealing with
// gc.statepoint.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_STATEPOINT_H
#define LLVM_IR_STATEPOINT_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {

/// The statepoint intrinsic accepts a set of flags as its third argument.
/// Valid values come out of this set.
enum class StatepointFlags {
  None = 0,
  GCTransition = 1, ///< Indicates that this statepoint is a transition from
                    ///< GC-aware code to code that is not GC-aware.
  /// Mark the deopt arguments associated with the statepoint as only being
  /// "live-in". By default, deopt arguments are "live-through".  "live-through"
  /// requires that they the value be live on entry, on exit, and at any point
  /// during the call.  "live-in" only requires the value be available at the
  /// start of the call.  In particular, "live-in" values can be placed in
  /// unused argument registers or other non-callee saved registers.
  DeoptLiveIn = 2,

  MaskAll = 3 ///< A bitmask that includes all valid flags.
};

class GCRelocateInst;
class GCResultInst;

bool isStatepoint(ImmutableCallSite CS);
bool isStatepoint(const Value *V);
bool isStatepoint(const Value &V);

bool isGCRelocate(ImmutableCallSite CS);
bool isGCRelocate(const Value *V);

bool isGCResult(ImmutableCallSite CS);
bool isGCResult(const Value *V);

/// Analogous to CallSiteBase, this provides most of the actual
/// functionality for Statepoint and ImmutableStatepoint.  It is
/// templatized to allow easily specializing of const and non-const
/// concrete subtypes.  This is structured analogous to CallSite
/// rather than the IntrinsicInst.h helpers since we need to support
/// invokable statepoints.
template <typename FunTy, typename InstructionTy, typename ValueTy,
          typename CallSiteTy>
class StatepointBase {
  CallSiteTy StatepointCS;

protected:
  explicit StatepointBase(InstructionTy *I) {
    if (isStatepoint(I)) {
      StatepointCS = CallSiteTy(I);
      assert(StatepointCS && "isStatepoint implies CallSite");
    }
  }

  explicit StatepointBase(CallSiteTy CS) {
    if (isStatepoint(CS))
      StatepointCS = CS;
  }

public:
  using arg_iterator = typename CallSiteTy::arg_iterator;

  enum {
    IDPos = 0,
    NumPatchBytesPos = 1,
    CalledFunctionPos = 2,
    NumCallArgsPos = 3,
    FlagsPos = 4,
    CallArgsBeginPos = 5,
  };

  void *operator new(size_t, unsigned) = delete;
  void *operator new(size_t s) = delete;

  explicit operator bool() const {
    // We do not assign non-statepoint CallSites to StatepointCS.
    return (bool)StatepointCS;
  }

  /// Return the underlying CallSite.
  CallSiteTy getCallSite() const {
    assert(*this && "check validity first!");
    return StatepointCS;
  }

  uint64_t getFlags() const {
    return cast<ConstantInt>(getCallSite().getArgument(FlagsPos))
        ->getZExtValue();
  }

  /// Return the ID associated with this statepoint.
  uint64_t getID() const {
    const Value *IDVal = getCallSite().getArgument(IDPos);
    return cast<ConstantInt>(IDVal)->getZExtValue();
  }

  /// Return the number of patchable bytes associated with this statepoint.
  uint32_t getNumPatchBytes() const {
    const Value *NumPatchBytesVal = getCallSite().getArgument(NumPatchBytesPos);
    uint64_t NumPatchBytes =
      cast<ConstantInt>(NumPatchBytesVal)->getZExtValue();
    assert(isInt<32>(NumPatchBytes) && "should fit in 32 bits!");
    return NumPatchBytes;
  }

  /// Return the value actually being called or invoked.
  ValueTy *getCalledValue() const {
    return getCallSite().getArgument(CalledFunctionPos);
  }

  InstructionTy *getInstruction() const {
    return getCallSite().getInstruction();
  }

  /// Return the function being called if this is a direct call, otherwise
  /// return null (if it's an indirect call).
  FunTy *getCalledFunction() const {
    return dyn_cast<Function>(getCalledValue());
  }

  /// Return the caller function for this statepoint.
  FunTy *getCaller() const { return getCallSite().getCaller(); }

  /// Determine if the statepoint cannot unwind.
  bool doesNotThrow() const {
    Function *F = getCalledFunction();
    return getCallSite().doesNotThrow() || (F ? F->doesNotThrow() : false);
  }

  /// Return the type of the value returned by the call underlying the
  /// statepoint.
  Type *getActualReturnType() const {
    auto *FTy = cast<FunctionType>(
        cast<PointerType>(getCalledValue()->getType())->getElementType());
    return FTy->getReturnType();
  }

  /// Number of arguments to be passed to the actual callee.
  int getNumCallArgs() const {
    const Value *NumCallArgsVal = getCallSite().getArgument(NumCallArgsPos);
    return cast<ConstantInt>(NumCallArgsVal)->getZExtValue();
  }

  size_t arg_size() const { return getNumCallArgs(); }
  typename CallSiteTy::arg_iterator arg_begin() const {
    assert(CallArgsBeginPos <= (int)getCallSite().arg_size());
    return getCallSite().arg_begin() + CallArgsBeginPos;
  }
  typename CallSiteTy::arg_iterator arg_end() const {
    auto I = arg_begin() + arg_size();
    assert((getCallSite().arg_end() - I) >= 0);
    return I;
  }

  ValueTy *getArgument(unsigned Index) {
    assert(Index < arg_size() && "out of bounds!");
    return *(arg_begin() + Index);
  }

  /// range adapter for call arguments
  iterator_range<arg_iterator> call_args() const {
    return make_range(arg_begin(), arg_end());
  }

  /// Return true if the call or the callee has the given attribute.
  bool paramHasAttr(unsigned i, Attribute::AttrKind A) const {
    Function *F = getCalledFunction();
    return getCallSite().paramHasAttr(i + CallArgsBeginPos, A) ||
          (F ? F->getAttributes().hasAttribute(i, A) : false);
  }

  /// Number of GC transition args.
  int getNumTotalGCTransitionArgs() const {
    const Value *NumGCTransitionArgs = *arg_end();
    return cast<ConstantInt>(NumGCTransitionArgs)->getZExtValue();
  }
  typename CallSiteTy::arg_iterator gc_transition_args_begin() const {
    auto I = arg_end() + 1;
    assert((getCallSite().arg_end() - I) >= 0);
    return I;
  }
  typename CallSiteTy::arg_iterator gc_transition_args_end() const {
    auto I = gc_transition_args_begin() + getNumTotalGCTransitionArgs();
    assert((getCallSite().arg_end() - I) >= 0);
    return I;
  }

  /// range adapter for GC transition arguments
  iterator_range<arg_iterator> gc_transition_args() const {
    return make_range(gc_transition_args_begin(), gc_transition_args_end());
  }

  /// Number of additional arguments excluding those intended
  /// for garbage collection.
  int getNumTotalVMSArgs() const {
    const Value *NumVMSArgs = *gc_transition_args_end();
    return cast<ConstantInt>(NumVMSArgs)->getZExtValue();
  }

  typename CallSiteTy::arg_iterator deopt_begin() const {
    auto I = gc_transition_args_end() + 1;
    assert((getCallSite().arg_end() - I) >= 0);
    return I;
  }
  typename CallSiteTy::arg_iterator deopt_end() const {
    auto I = deopt_begin() + getNumTotalVMSArgs();
    assert((getCallSite().arg_end() - I) >= 0);
    return I;
  }

  /// range adapter for vm state arguments
  iterator_range<arg_iterator> deopt_operands() const {
    return make_range(deopt_begin(), deopt_end());
  }

  typename CallSiteTy::arg_iterator gc_args_begin() const {
    return deopt_end();
  }
  typename CallSiteTy::arg_iterator gc_args_end() const {
    return getCallSite().arg_end();
  }

  unsigned gcArgsStartIdx() const {
    return gc_args_begin() - getInstruction()->op_begin();
  }

  /// range adapter for gc arguments
  iterator_range<arg_iterator> gc_args() const {
    return make_range(gc_args_begin(), gc_args_end());
  }

  /// Get list of all gc reloactes linked to this statepoint
  /// May contain several relocations for the same base/derived pair.
  /// For example this could happen due to relocations on unwinding
  /// path of invoke.
  std::vector<const GCRelocateInst *> getRelocates() const;

  /// Get the experimental_gc_result call tied to this statepoint.  Can be
  /// nullptr if there isn't a gc_result tied to this statepoint.  Guaranteed to
  /// be a CallInst if non-null.
  const GCResultInst *getGCResult() const {
    for (auto *U : getInstruction()->users())
      if (auto *GRI = dyn_cast<GCResultInst>(U))
        return GRI;
    return nullptr;
  }

#ifndef NDEBUG
  /// Asserts if this statepoint is malformed.  Common cases for failure
  /// include incorrect length prefixes for variable length sections or
  /// illegal values for parameters.
  void verify() {
    assert(getNumCallArgs() >= 0 &&
           "number of arguments to actually callee can't be negative");

    // The internal asserts in the iterator accessors do the rest.
    (void)arg_begin();
    (void)arg_end();
    (void)gc_transition_args_begin();
    (void)gc_transition_args_end();
    (void)deopt_begin();
    (void)deopt_end();
    (void)gc_args_begin();
    (void)gc_args_end();
  }
#endif
};

/// A specialization of it's base class for read only access
/// to a gc.statepoint.
class ImmutableStatepoint
    : public StatepointBase<const Function, const Instruction, const Value,
                            ImmutableCallSite> {
  using Base =
      StatepointBase<const Function, const Instruction, const Value,
                     ImmutableCallSite>;

public:
  explicit ImmutableStatepoint(const Instruction *I) : Base(I) {}
  explicit ImmutableStatepoint(ImmutableCallSite CS) : Base(CS) {}
};

/// A specialization of it's base class for read-write access
/// to a gc.statepoint.
class Statepoint
    : public StatepointBase<Function, Instruction, Value, CallSite> {
  using Base = StatepointBase<Function, Instruction, Value, CallSite>;

public:
  explicit Statepoint(Instruction *I) : Base(I) {}
  explicit Statepoint(CallSite CS) : Base(CS) {}
};

/// Common base class for representing values projected from a statepoint.
/// Currently, the only projections available are gc.result and gc.relocate.
class GCProjectionInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_relocate ||
      I->getIntrinsicID() == Intrinsic::experimental_gc_result;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return true if this relocate is tied to the invoke statepoint.
  /// This includes relocates which are on the unwinding path.
  bool isTiedToInvoke() const {
    const Value *Token = getArgOperand(0);

    return isa<LandingPadInst>(Token) || isa<InvokeInst>(Token);
  }

  /// The statepoint with which this gc.relocate is associated.
  const Instruction *getStatepoint() const {
    const Value *Token = getArgOperand(0);

    // This takes care both of relocates for call statepoints and relocates
    // on normal path of invoke statepoint.
    if (!isa<LandingPadInst>(Token)) {
      assert(isStatepoint(Token));
      return cast<Instruction>(Token);
    }

    // This relocate is on exceptional path of an invoke statepoint
    const BasicBlock *InvokeBB =
        cast<Instruction>(Token)->getParent()->getUniquePredecessor();

    assert(InvokeBB && "safepoints should have unique landingpads");
    assert(InvokeBB->getTerminator() &&
           "safepoint block should be well formed");
    assert(isStatepoint(InvokeBB->getTerminator()));

    return InvokeBB->getTerminator();
  }
};

/// Represents calls to the gc.relocate intrinsic.
class GCRelocateInst : public GCProjectionInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_relocate;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// The index into the associate statepoint's argument list
  /// which contains the base pointer of the pointer whose
  /// relocation this gc.relocate describes.
  unsigned getBasePtrIndex() const {
    return cast<ConstantInt>(getArgOperand(1))->getZExtValue();
  }

  /// The index into the associate statepoint's argument list which
  /// contains the pointer whose relocation this gc.relocate describes.
  unsigned getDerivedPtrIndex() const {
    return cast<ConstantInt>(getArgOperand(2))->getZExtValue();
  }

  Value *getBasePtr() const {
    ImmutableCallSite CS(getStatepoint());
    return *(CS.arg_begin() + getBasePtrIndex());
  }

  Value *getDerivedPtr() const {
    ImmutableCallSite CS(getStatepoint());
    return *(CS.arg_begin() + getDerivedPtrIndex());
  }
};

/// Represents calls to the gc.result intrinsic.
class GCResultInst : public GCProjectionInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_result;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

template <typename FunTy, typename InstructionTy, typename ValueTy,
          typename CallSiteTy>
std::vector<const GCRelocateInst *>
StatepointBase<FunTy, InstructionTy, ValueTy, CallSiteTy>::getRelocates()
    const {

  std::vector<const GCRelocateInst *> Result;

  CallSiteTy StatepointCS = getCallSite();

  // Search for relocated pointers.  Note that working backwards from the
  // gc_relocates ensures that we only get pairs which are actually relocated
  // and used after the statepoint.
  for (const User *U : getInstruction()->users())
    if (auto *Relocate = dyn_cast<GCRelocateInst>(U))
      Result.push_back(Relocate);

  if (!StatepointCS.isInvoke())
    return Result;

  // We need to scan thorough exceptional relocations if it is invoke statepoint
  LandingPadInst *LandingPad =
      cast<InvokeInst>(getInstruction())->getLandingPadInst();

  // Search for gc relocates that are attached to this landingpad.
  for (const User *LandingPadUser : LandingPad->users()) {
    if (auto *Relocate = dyn_cast<GCRelocateInst>(LandingPadUser))
      Result.push_back(Relocate);
  }
  return Result;
}

/// Call sites that get wrapped by a gc.statepoint (currently only in
/// RewriteStatepointsForGC and potentially in other passes in the future) can
/// have attributes that describe properties of gc.statepoint call they will be
/// eventually be wrapped in.  This struct is used represent such directives.
struct StatepointDirectives {
  Optional<uint32_t> NumPatchBytes;
  Optional<uint64_t> StatepointID;

  static const uint64_t DefaultStatepointID = 0xABCDEF00;
  static const uint64_t DeoptBundleStatepointID = 0xABCDEF0F;
};

/// Parse out statepoint directives from the function attributes present in \p
/// AS.
StatepointDirectives parseStatepointDirectivesFromAttrs(AttributeList AS);

/// Return \c true if the \p Attr is an attribute that is a statepoint
/// directive.
bool isStatepointDirectiveAttr(Attribute Attr);

} // end namespace llvm

#endif // LLVM_IR_STATEPOINT_H
