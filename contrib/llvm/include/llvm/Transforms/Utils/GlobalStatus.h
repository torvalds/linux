//===- GlobalStatus.h - Compute status info for globals ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_GLOBALSTATUS_H
#define LLVM_TRANSFORMS_UTILS_GLOBALSTATUS_H

#include "llvm/Support/AtomicOrdering.h"

namespace llvm {

class Constant;
class Function;
class Value;

/// It is safe to destroy a constant iff it is only used by constants itself.
/// Note that constants cannot be cyclic, so this test is pretty easy to
/// implement recursively.
///
bool isSafeToDestroyConstant(const Constant *C);

/// As we analyze each global, keep track of some information about it.  If we
/// find out that the address of the global is taken, none of this info will be
/// accurate.
struct GlobalStatus {
  /// True if the global's address is used in a comparison.
  bool IsCompared = false;

  /// True if the global is ever loaded.  If the global isn't ever loaded it
  /// can be deleted.
  bool IsLoaded = false;

  /// Keep track of what stores to the global look like.
  enum StoredType {
    /// There is no store to this global.  It can thus be marked constant.
    NotStored,

    /// This global is stored to, but the only thing stored is the constant it
    /// was initialized with. This is only tracked for scalar globals.
    InitializerStored,

    /// This global is stored to, but only its initializer and one other value
    /// is ever stored to it.  If this global isStoredOnce, we track the value
    /// stored to it in StoredOnceValue below.  This is only tracked for scalar
    /// globals.
    StoredOnce,

    /// This global is stored to by multiple values or something else that we
    /// cannot track.
    Stored
  } StoredType = NotStored;

  /// If only one value (besides the initializer constant) is ever stored to
  /// this global, keep track of what value it is.
  Value *StoredOnceValue = nullptr;

  /// These start out null/false.  When the first accessing function is noticed,
  /// it is recorded. When a second different accessing function is noticed,
  /// HasMultipleAccessingFunctions is set to true.
  const Function *AccessingFunction = nullptr;
  bool HasMultipleAccessingFunctions = false;

  /// Set to true if this global has a user that is not an instruction (e.g. a
  /// constant expr or GV initializer).
  bool HasNonInstructionUser = false;

  /// Set to the strongest atomic ordering requirement.
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;

  GlobalStatus();

  /// Look at all uses of the global and fill in the GlobalStatus structure.  If
  /// the global has its address taken, return true to indicate we can't do
  /// anything with it.
  static bool analyzeGlobal(const Value *V, GlobalStatus &GS);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_GLOBALSTATUS_H
