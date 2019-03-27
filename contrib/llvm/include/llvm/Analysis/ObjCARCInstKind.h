//===- ObjCARCInstKind.h - ARC instruction equivalence classes --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCINSTKIND_H
#define LLVM_ANALYSIS_OBJCARCINSTKIND_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Instructions.h"

namespace llvm {
namespace objcarc {

/// \enum ARCInstKind
///
/// Equivalence classes of instructions in the ARC Model.
///
/// Since we do not have "instructions" to represent ARC concepts in LLVM IR,
/// we instead operate on equivalence classes of instructions.
///
/// TODO: This should be split into two enums: a runtime entry point enum
/// (possibly united with the ARCRuntimeEntrypoint class) and an enum that deals
/// with effects of instructions in the ARC model (which would handle the notion
/// of a User or CallOrUser).
enum class ARCInstKind {
  Retain,                   ///< objc_retain
  RetainRV,                 ///< objc_retainAutoreleasedReturnValue
  ClaimRV,                  ///< objc_unsafeClaimAutoreleasedReturnValue
  RetainBlock,              ///< objc_retainBlock
  Release,                  ///< objc_release
  Autorelease,              ///< objc_autorelease
  AutoreleaseRV,            ///< objc_autoreleaseReturnValue
  AutoreleasepoolPush,      ///< objc_autoreleasePoolPush
  AutoreleasepoolPop,       ///< objc_autoreleasePoolPop
  NoopCast,                 ///< objc_retainedObject, etc.
  FusedRetainAutorelease,   ///< objc_retainAutorelease
  FusedRetainAutoreleaseRV, ///< objc_retainAutoreleaseReturnValue
  LoadWeakRetained,         ///< objc_loadWeakRetained (primitive)
  StoreWeak,                ///< objc_storeWeak (primitive)
  InitWeak,                 ///< objc_initWeak (derived)
  LoadWeak,                 ///< objc_loadWeak (derived)
  MoveWeak,                 ///< objc_moveWeak (derived)
  CopyWeak,                 ///< objc_copyWeak (derived)
  DestroyWeak,              ///< objc_destroyWeak (derived)
  StoreStrong,              ///< objc_storeStrong (derived)
  IntrinsicUser,            ///< llvm.objc.clang.arc.use
  CallOrUser,               ///< could call objc_release and/or "use" pointers
  Call,                     ///< could call objc_release
  User,                     ///< could "use" a pointer
  None                      ///< anything that is inert from an ARC perspective.
};

raw_ostream &operator<<(raw_ostream &OS, const ARCInstKind Class);

/// Test if the given class is a kind of user.
bool IsUser(ARCInstKind Class);

/// Test if the given class is objc_retain or equivalent.
bool IsRetain(ARCInstKind Class);

/// Test if the given class is objc_autorelease or equivalent.
bool IsAutorelease(ARCInstKind Class);

/// Test if the given class represents instructions which return their
/// argument verbatim.
bool IsForwarding(ARCInstKind Class);

/// Test if the given class represents instructions which do nothing if
/// passed a null pointer.
bool IsNoopOnNull(ARCInstKind Class);

/// Test if the given class represents instructions which are always safe
/// to mark with the "tail" keyword.
bool IsAlwaysTail(ARCInstKind Class);

/// Test if the given class represents instructions which are never safe
/// to mark with the "tail" keyword.
bool IsNeverTail(ARCInstKind Class);

/// Test if the given class represents instructions which are always safe
/// to mark with the nounwind attribute.
bool IsNoThrow(ARCInstKind Class);

/// Test whether the given instruction can autorelease any pointer or cause an
/// autoreleasepool pop.
bool CanInterruptRV(ARCInstKind Class);

/// Determine if F is one of the special known Functions.  If it isn't,
/// return ARCInstKind::CallOrUser.
ARCInstKind GetFunctionClass(const Function *F);

/// Determine which objc runtime call instruction class V belongs to.
///
/// This is similar to GetARCInstKind except that it only detects objc
/// runtime calls. This allows it to be faster.
///
inline ARCInstKind GetBasicARCInstKind(const Value *V) {
  if (const CallInst *CI = dyn_cast<CallInst>(V)) {
    if (const Function *F = CI->getCalledFunction())
      return GetFunctionClass(F);
    // Otherwise, be conservative.
    return ARCInstKind::CallOrUser;
  }

  // Otherwise, be conservative.
  return isa<InvokeInst>(V) ? ARCInstKind::CallOrUser : ARCInstKind::User;
}

/// Map V to its ARCInstKind equivalence class.
ARCInstKind GetARCInstKind(const Value *V);

/// Returns false if conservatively we can prove that any instruction mapped to
/// this kind can not decrement ref counts. Returns true otherwise.
bool CanDecrementRefCount(ARCInstKind Kind);

} // end namespace objcarc
} // end namespace llvm

#endif
