//===- llvm/Analysis/AliasAnalysis.h - Alias Analysis Interface -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic AliasAnalysis interface, which is used as the
// common interface used by all clients of alias analysis information, and
// implemented by all alias analysis implementations.  Mod/Ref information is
// also captured by this interface.
//
// Implementations of this interface must implement the various virtual methods,
// which automatically provides functionality for the entire suite of client
// APIs.
//
// This API identifies memory regions with the MemoryLocation class. The pointer
// component specifies the base memory address of the region. The Size specifies
// the maximum size (in address units) of the memory region, or
// MemoryLocation::UnknownSize if the size is not known. The TBAA tag
// identifies the "type" of the memory reference; see the
// TypeBasedAliasAnalysis class for details.
//
// Some non-obvious details include:
//  - Pointers that point to two completely different objects in memory never
//    alias, regardless of the value of the Size component.
//  - NoAlias doesn't imply inequal pointers. The most obvious example of this
//    is two pointers to constant memory. Even if they are equal, constant
//    memory is never stored to, so there will never be any dependencies.
//    In this and other situations, the pointers may be both NoAlias and
//    MustAlias at the same time. The current API can only return one result,
//    though this is rarely a problem in practice.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASANALYSIS_H
#define LLVM_ANALYSIS_ALIASANALYSIS_H

#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace llvm {

class AnalysisUsage;
class BasicAAResult;
class BasicBlock;
class DominatorTree;
class OrderedBasicBlock;
class Value;

/// The possible results of an alias query.
///
/// These results are always computed between two MemoryLocation objects as
/// a query to some alias analysis.
///
/// Note that these are unscoped enumerations because we would like to support
/// implicitly testing a result for the existence of any possible aliasing with
/// a conversion to bool, but an "enum class" doesn't support this. The
/// canonical names from the literature are suffixed and unique anyways, and so
/// they serve as global constants in LLVM for these results.
///
/// See docs/AliasAnalysis.html for more information on the specific meanings
/// of these values.
enum AliasResult : uint8_t {
  /// The two locations do not alias at all.
  ///
  /// This value is arranged to convert to false, while all other values
  /// convert to true. This allows a boolean context to convert the result to
  /// a binary flag indicating whether there is the possibility of aliasing.
  NoAlias = 0,
  /// The two locations may or may not alias. This is the least precise result.
  MayAlias,
  /// The two locations alias, but only due to a partial overlap.
  PartialAlias,
  /// The two locations precisely alias each other.
  MustAlias,
};

/// << operator for AliasResult.
raw_ostream &operator<<(raw_ostream &OS, AliasResult AR);

/// Flags indicating whether a memory access modifies or references memory.
///
/// This is no access at all, a modification, a reference, or both
/// a modification and a reference. These are specifically structured such that
/// they form a three bit matrix and bit-tests for 'mod' or 'ref' or 'must'
/// work with any of the possible values.
enum class ModRefInfo : uint8_t {
  /// Must is provided for completeness, but no routines will return only
  /// Must today. See definition of Must below.
  Must = 0,
  /// The access may reference the value stored in memory,
  /// a mustAlias relation was found, and no mayAlias or partialAlias found.
  MustRef = 1,
  /// The access may modify the value stored in memory,
  /// a mustAlias relation was found, and no mayAlias or partialAlias found.
  MustMod = 2,
  /// The access may reference, modify or both the value stored in memory,
  /// a mustAlias relation was found, and no mayAlias or partialAlias found.
  MustModRef = MustRef | MustMod,
  /// The access neither references nor modifies the value stored in memory.
  NoModRef = 4,
  /// The access may reference the value stored in memory.
  Ref = NoModRef | MustRef,
  /// The access may modify the value stored in memory.
  Mod = NoModRef | MustMod,
  /// The access may reference and may modify the value stored in memory.
  ModRef = Ref | Mod,

  /// About Must:
  /// Must is set in a best effort manner.
  /// We usually do not try our best to infer Must, instead it is merely
  /// another piece of "free" information that is presented when available.
  /// Must set means there was certainly a MustAlias found. For calls,
  /// where multiple arguments are checked (argmemonly), this translates to
  /// only MustAlias or NoAlias was found.
  /// Must is not set for RAR accesses, even if the two locations must
  /// alias. The reason is that two read accesses translate to an early return
  /// of NoModRef. An additional alias check to set Must may be
  /// expensive. Other cases may also not set Must(e.g. callCapturesBefore).
  /// We refer to Must being *set* when the most significant bit is *cleared*.
  /// Conversely we *clear* Must information by *setting* the Must bit to 1.
};

LLVM_NODISCARD inline bool isNoModRef(const ModRefInfo MRI) {
  return (static_cast<int>(MRI) & static_cast<int>(ModRefInfo::MustModRef)) ==
         static_cast<int>(ModRefInfo::Must);
}
LLVM_NODISCARD inline bool isModOrRefSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::MustModRef);
}
LLVM_NODISCARD inline bool isModAndRefSet(const ModRefInfo MRI) {
  return (static_cast<int>(MRI) & static_cast<int>(ModRefInfo::MustModRef)) ==
         static_cast<int>(ModRefInfo::MustModRef);
}
LLVM_NODISCARD inline bool isModSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::MustMod);
}
LLVM_NODISCARD inline bool isRefSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::MustRef);
}
LLVM_NODISCARD inline bool isMustSet(const ModRefInfo MRI) {
  return !(static_cast<int>(MRI) & static_cast<int>(ModRefInfo::NoModRef));
}

LLVM_NODISCARD inline ModRefInfo setMod(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) |
                    static_cast<int>(ModRefInfo::MustMod));
}
LLVM_NODISCARD inline ModRefInfo setRef(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) |
                    static_cast<int>(ModRefInfo::MustRef));
}
LLVM_NODISCARD inline ModRefInfo setMust(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) &
                    static_cast<int>(ModRefInfo::MustModRef));
}
LLVM_NODISCARD inline ModRefInfo setModAndRef(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) |
                    static_cast<int>(ModRefInfo::MustModRef));
}
LLVM_NODISCARD inline ModRefInfo clearMod(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) & static_cast<int>(ModRefInfo::Ref));
}
LLVM_NODISCARD inline ModRefInfo clearRef(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) & static_cast<int>(ModRefInfo::Mod));
}
LLVM_NODISCARD inline ModRefInfo clearMust(const ModRefInfo MRI) {
  return ModRefInfo(static_cast<int>(MRI) |
                    static_cast<int>(ModRefInfo::NoModRef));
}
LLVM_NODISCARD inline ModRefInfo unionModRef(const ModRefInfo MRI1,
                                             const ModRefInfo MRI2) {
  return ModRefInfo(static_cast<int>(MRI1) | static_cast<int>(MRI2));
}
LLVM_NODISCARD inline ModRefInfo intersectModRef(const ModRefInfo MRI1,
                                                 const ModRefInfo MRI2) {
  return ModRefInfo(static_cast<int>(MRI1) & static_cast<int>(MRI2));
}

/// The locations at which a function might access memory.
///
/// These are primarily used in conjunction with the \c AccessKind bits to
/// describe both the nature of access and the locations of access for a
/// function call.
enum FunctionModRefLocation {
  /// Base case is no access to memory.
  FMRL_Nowhere = 0,
  /// Access to memory via argument pointers.
  FMRL_ArgumentPointees = 8,
  /// Memory that is inaccessible via LLVM IR.
  FMRL_InaccessibleMem = 16,
  /// Access to any memory.
  FMRL_Anywhere = 32 | FMRL_InaccessibleMem | FMRL_ArgumentPointees
};

/// Summary of how a function affects memory in the program.
///
/// Loads from constant globals are not considered memory accesses for this
/// interface. Also, functions may freely modify stack space local to their
/// invocation without having to report it through these interfaces.
enum FunctionModRefBehavior {
  /// This function does not perform any non-local loads or stores to memory.
  ///
  /// This property corresponds to the GCC 'const' attribute.
  /// This property corresponds to the LLVM IR 'readnone' attribute.
  /// This property corresponds to the IntrNoMem LLVM intrinsic flag.
  FMRB_DoesNotAccessMemory =
      FMRL_Nowhere | static_cast<int>(ModRefInfo::NoModRef),

  /// The only memory references in this function (if it has any) are
  /// non-volatile loads from objects pointed to by its pointer-typed
  /// arguments, with arbitrary offsets.
  ///
  /// This property corresponds to the IntrReadArgMem LLVM intrinsic flag.
  FMRB_OnlyReadsArgumentPointees =
      FMRL_ArgumentPointees | static_cast<int>(ModRefInfo::Ref),

  /// The only memory references in this function (if it has any) are
  /// non-volatile loads and stores from objects pointed to by its
  /// pointer-typed arguments, with arbitrary offsets.
  ///
  /// This property corresponds to the IntrArgMemOnly LLVM intrinsic flag.
  FMRB_OnlyAccessesArgumentPointees =
      FMRL_ArgumentPointees | static_cast<int>(ModRefInfo::ModRef),

  /// The only memory references in this function (if it has any) are
  /// references of memory that is otherwise inaccessible via LLVM IR.
  ///
  /// This property corresponds to the LLVM IR inaccessiblememonly attribute.
  FMRB_OnlyAccessesInaccessibleMem =
      FMRL_InaccessibleMem | static_cast<int>(ModRefInfo::ModRef),

  /// The function may perform non-volatile loads and stores of objects
  /// pointed to by its pointer-typed arguments, with arbitrary offsets, and
  /// it may also perform loads and stores of memory that is otherwise
  /// inaccessible via LLVM IR.
  ///
  /// This property corresponds to the LLVM IR
  /// inaccessiblemem_or_argmemonly attribute.
  FMRB_OnlyAccessesInaccessibleOrArgMem = FMRL_InaccessibleMem |
                                          FMRL_ArgumentPointees |
                                          static_cast<int>(ModRefInfo::ModRef),

  /// This function does not perform any non-local stores or volatile loads,
  /// but may read from any memory location.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  /// This property corresponds to the LLVM IR 'readonly' attribute.
  /// This property corresponds to the IntrReadMem LLVM intrinsic flag.
  FMRB_OnlyReadsMemory = FMRL_Anywhere | static_cast<int>(ModRefInfo::Ref),

  // This function does not read from memory anywhere, but may write to any
  // memory location.
  //
  // This property corresponds to the LLVM IR 'writeonly' attribute.
  // This property corresponds to the IntrWriteMem LLVM intrinsic flag.
  FMRB_DoesNotReadMemory = FMRL_Anywhere | static_cast<int>(ModRefInfo::Mod),

  /// This indicates that the function could not be classified into one of the
  /// behaviors above.
  FMRB_UnknownModRefBehavior =
      FMRL_Anywhere | static_cast<int>(ModRefInfo::ModRef)
};

// Wrapper method strips bits significant only in FunctionModRefBehavior,
// to obtain a valid ModRefInfo. The benefit of using the wrapper is that if
// ModRefInfo enum changes, the wrapper can be updated to & with the new enum
// entry with all bits set to 1.
LLVM_NODISCARD inline ModRefInfo
createModRefInfo(const FunctionModRefBehavior FMRB) {
  return ModRefInfo(FMRB & static_cast<int>(ModRefInfo::ModRef));
}

class AAResults {
public:
  // Make these results default constructable and movable. We have to spell
  // these out because MSVC won't synthesize them.
  AAResults(const TargetLibraryInfo &TLI) : TLI(TLI) {}
  AAResults(AAResults &&Arg);
  ~AAResults();

  /// Register a specific AA result.
  template <typename AAResultT> void addAAResult(AAResultT &AAResult) {
    // FIXME: We should use a much lighter weight system than the usual
    // polymorphic pattern because we don't own AAResult. It should
    // ideally involve two pointers and no separate allocation.
    AAs.emplace_back(new Model<AAResultT>(AAResult, *this));
  }

  /// Register a function analysis ID that the results aggregation depends on.
  ///
  /// This is used in the new pass manager to implement the invalidation logic
  /// where we must invalidate the results aggregation if any of our component
  /// analyses become invalid.
  void addAADependencyID(AnalysisKey *ID) { AADeps.push_back(ID); }

  /// Handle invalidation events in the new pass manager.
  ///
  /// The aggregation is invalidated if any of the underlying analyses is
  /// invalidated.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);

  /// A convenience wrapper around the primary \c alias interface.
  AliasResult alias(const Value *V1, LocationSize V1Size, const Value *V2,
                    LocationSize V2Size) {
    return alias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the primary \c alias interface.
  AliasResult alias(const Value *V1, const Value *V2) {
    return alias(V1, LocationSize::unknown(), V2, LocationSize::unknown());
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// no-alias.
  bool isNoAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == NoAlias;
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  bool isNoAlias(const Value *V1, LocationSize V1Size, const Value *V2,
                 LocationSize V2Size) {
    return isNoAlias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  bool isNoAlias(const Value *V1, const Value *V2) {
    return isNoAlias(MemoryLocation(V1), MemoryLocation(V2));
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// must-alias.
  bool isMustAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == MustAlias;
  }

  /// A convenience wrapper around the \c isMustAlias helper interface.
  bool isMustAlias(const Value *V1, const Value *V2) {
    return alias(V1, LocationSize::precise(1), V2, LocationSize::precise(1)) ==
           MustAlias;
  }

  /// Checks whether the given location points to constant memory, or if
  /// \p OrLocal is true whether it points to a local alloca.
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal = false);

  /// A convenience wrapper around the primary \c pointsToConstantMemory
  /// interface.
  bool pointsToConstantMemory(const Value *P, bool OrLocal = false) {
    return pointsToConstantMemory(MemoryLocation(P), OrLocal);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Get the ModRef info associated with a pointer argument of a call. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information. This never sets ModRefInfo::Must.
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx);

  /// Return the behavior of the given call site.
  FunctionModRefBehavior getModRefBehavior(const CallBase *Call);

  /// Return the behavior when calling the given function.
  FunctionModRefBehavior getModRefBehavior(const Function *F);

  /// Checks if the specified call is known to never read or write memory.
  ///
  /// Note that if the call only reads from known-constant memory, it is also
  /// legal to return true. Also, calls that unwind the stack are legal for
  /// this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// without worrying about aliasing properties, and many calls have this
  /// property (e.g. calls to 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  bool doesNotAccessMemory(const CallBase *Call) {
    return getModRefBehavior(Call) == FMRB_DoesNotAccessMemory;
  }

  /// Checks if the specified function is known to never read or write memory.
  ///
  /// Note that if the function only reads from known-constant memory, it is
  /// also legal to return true. Also, function that unwind the stack are legal
  /// for this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// to such functions without worrying about aliasing properties, and many
  /// functions have this property (e.g. 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  bool doesNotAccessMemory(const Function *F) {
    return getModRefBehavior(F) == FMRB_DoesNotAccessMemory;
  }

  /// Checks if the specified call is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Calls that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  bool onlyReadsMemory(const CallBase *Call) {
    return onlyReadsMemory(getModRefBehavior(Call));
  }

  /// Checks if the specified function is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Functions that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  bool onlyReadsMemory(const Function *F) {
    return onlyReadsMemory(getModRefBehavior(F));
  }

  /// Checks if functions with the specified behavior are known to only read
  /// from non-volatile memory (or not access memory at all).
  static bool onlyReadsMemory(FunctionModRefBehavior MRB) {
    return !isModSet(createModRefInfo(MRB));
  }

  /// Checks if functions with the specified behavior are known to only write
  /// memory (or not access memory at all).
  static bool doesNotReadMemory(FunctionModRefBehavior MRB) {
    return !isRefSet(createModRefInfo(MRB));
  }

  /// Checks if functions with the specified behavior are known to read and
  /// write at most from objects pointed to by their pointer-typed arguments
  /// (with arbitrary offsets).
  static bool onlyAccessesArgPointees(FunctionModRefBehavior MRB) {
    return !(MRB & FMRL_Anywhere & ~FMRL_ArgumentPointees);
  }

  /// Checks if functions with the specified behavior are known to potentially
  /// read or write from objects pointed to be their pointer-typed arguments
  /// (with arbitrary offsets).
  static bool doesAccessArgPointees(FunctionModRefBehavior MRB) {
    return isModOrRefSet(createModRefInfo(MRB)) &&
           (MRB & FMRL_ArgumentPointees);
  }

  /// Checks if functions with the specified behavior are known to read and
  /// write at most from memory that is inaccessible from LLVM IR.
  static bool onlyAccessesInaccessibleMem(FunctionModRefBehavior MRB) {
    return !(MRB & FMRL_Anywhere & ~FMRL_InaccessibleMem);
  }

  /// Checks if functions with the specified behavior are known to potentially
  /// read or write from memory that is inaccessible from LLVM IR.
  static bool doesAccessInaccessibleMem(FunctionModRefBehavior MRB) {
    return isModOrRefSet(createModRefInfo(MRB)) && (MRB & FMRL_InaccessibleMem);
  }

  /// Checks if functions with the specified behavior are known to read and
  /// write at most from memory that is inaccessible from LLVM IR or objects
  /// pointed to by their pointer-typed arguments (with arbitrary offsets).
  static bool onlyAccessesInaccessibleOrArgMem(FunctionModRefBehavior MRB) {
    return !(MRB & FMRL_Anywhere &
             ~(FMRL_InaccessibleMem | FMRL_ArgumentPointees));
  }

  /// getModRefInfo (for call sites) - Return information about whether
  /// a particular call site modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc);

  /// getModRefInfo (for call sites) - A convenience wrapper.
  ModRefInfo getModRefInfo(const CallBase *Call, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(Call, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for loads) - Return information about whether
  /// a particular load modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const LoadInst *L, const MemoryLocation &Loc);

  /// getModRefInfo (for loads) - A convenience wrapper.
  ModRefInfo getModRefInfo(const LoadInst *L, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(L, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for stores) - Return information about whether
  /// a particular store modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const StoreInst *S, const MemoryLocation &Loc);

  /// getModRefInfo (for stores) - A convenience wrapper.
  ModRefInfo getModRefInfo(const StoreInst *S, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(S, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for fences) - Return information about whether
  /// a particular store modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const FenceInst *S, const MemoryLocation &Loc);

  /// getModRefInfo (for fences) - A convenience wrapper.
  ModRefInfo getModRefInfo(const FenceInst *S, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(S, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for cmpxchges) - Return information about whether
  /// a particular cmpxchg modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const AtomicCmpXchgInst *CX,
                           const MemoryLocation &Loc);

  /// getModRefInfo (for cmpxchges) - A convenience wrapper.
  ModRefInfo getModRefInfo(const AtomicCmpXchgInst *CX, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(CX, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for atomicrmws) - Return information about whether
  /// a particular atomicrmw modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const AtomicRMWInst *RMW, const MemoryLocation &Loc);

  /// getModRefInfo (for atomicrmws) - A convenience wrapper.
  ModRefInfo getModRefInfo(const AtomicRMWInst *RMW, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(RMW, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for va_args) - Return information about whether
  /// a particular va_arg modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const VAArgInst *I, const MemoryLocation &Loc);

  /// getModRefInfo (for va_args) - A convenience wrapper.
  ModRefInfo getModRefInfo(const VAArgInst *I, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for catchpads) - Return information about whether
  /// a particular catchpad modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const CatchPadInst *I, const MemoryLocation &Loc);

  /// getModRefInfo (for catchpads) - A convenience wrapper.
  ModRefInfo getModRefInfo(const CatchPadInst *I, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// getModRefInfo (for catchrets) - Return information about whether
  /// a particular catchret modifies or reads the specified memory location.
  ModRefInfo getModRefInfo(const CatchReturnInst *I, const MemoryLocation &Loc);

  /// getModRefInfo (for catchrets) - A convenience wrapper.
  ModRefInfo getModRefInfo(const CatchReturnInst *I, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// Check whether or not an instruction may read or write the optionally
  /// specified memory location.
  ///
  ///
  /// An instruction that doesn't read or write memory may be trivially LICM'd
  /// for example.
  ///
  /// For function calls, this delegates to the alias-analysis specific
  /// call-site mod-ref behavior queries. Otherwise it delegates to the specific
  /// helpers above.
  ModRefInfo getModRefInfo(const Instruction *I,
                           const Optional<MemoryLocation> &OptLoc) {
    if (OptLoc == None) {
      if (const auto *Call = dyn_cast<CallBase>(I)) {
        return createModRefInfo(getModRefBehavior(Call));
      }
    }

    const MemoryLocation &Loc = OptLoc.getValueOr(MemoryLocation());

    switch (I->getOpcode()) {
    case Instruction::VAArg:  return getModRefInfo((const VAArgInst*)I, Loc);
    case Instruction::Load:   return getModRefInfo((const LoadInst*)I,  Loc);
    case Instruction::Store:  return getModRefInfo((const StoreInst*)I, Loc);
    case Instruction::Fence:  return getModRefInfo((const FenceInst*)I, Loc);
    case Instruction::AtomicCmpXchg:
      return getModRefInfo((const AtomicCmpXchgInst*)I, Loc);
    case Instruction::AtomicRMW:
      return getModRefInfo((const AtomicRMWInst*)I, Loc);
    case Instruction::Call:   return getModRefInfo((const CallInst*)I,  Loc);
    case Instruction::Invoke: return getModRefInfo((const InvokeInst*)I,Loc);
    case Instruction::CatchPad:
      return getModRefInfo((const CatchPadInst *)I, Loc);
    case Instruction::CatchRet:
      return getModRefInfo((const CatchReturnInst *)I, Loc);
    default:
      return ModRefInfo::NoModRef;
    }
  }

  /// A convenience wrapper for constructing the memory location.
  ModRefInfo getModRefInfo(const Instruction *I, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// Return information about whether a call and an instruction may refer to
  /// the same memory locations.
  ModRefInfo getModRefInfo(Instruction *I, const CallBase *Call);

  /// Return information about whether two call sites may refer to the same set
  /// of memory locations. See the AA documentation for details:
  ///   http://llvm.org/docs/AliasAnalysis.html#ModRefInfo
  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2);

  /// Return information about whether a particular call site modifies
  /// or reads the specified memory location \p MemLoc before instruction \p I
  /// in a BasicBlock. An ordered basic block \p OBB can be used to speed up
  /// instruction ordering queries inside the BasicBlock containing \p I.
  /// Early exits in callCapturesBefore may lead to ModRefInfo::Must not being
  /// set.
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc, DominatorTree *DT,
                                OrderedBasicBlock *OBB = nullptr);

  /// A convenience wrapper to synthesize a memory location.
  ModRefInfo callCapturesBefore(const Instruction *I, const Value *P,
                                LocationSize Size, DominatorTree *DT,
                                OrderedBasicBlock *OBB = nullptr) {
    return callCapturesBefore(I, MemoryLocation(P, Size), DT, OBB);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Higher level methods for querying mod/ref information.
  /// @{

  /// Check if it is possible for execution of the specified basic block to
  /// modify the location Loc.
  bool canBasicBlockModify(const BasicBlock &BB, const MemoryLocation &Loc);

  /// A convenience wrapper synthesizing a memory location.
  bool canBasicBlockModify(const BasicBlock &BB, const Value *P,
                           LocationSize Size) {
    return canBasicBlockModify(BB, MemoryLocation(P, Size));
  }

  /// Check if it is possible for the execution of the specified instructions
  /// to mod\ref (according to the mode) the location Loc.
  ///
  /// The instructions to consider are all of the instructions in the range of
  /// [I1,I2] INCLUSIVE. I1 and I2 must be in the same basic block.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const MemoryLocation &Loc,
                                 const ModRefInfo Mode);

  /// A convenience wrapper synthesizing a memory location.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const Value *Ptr, LocationSize Size,
                                 const ModRefInfo Mode) {
    return canInstructionRangeModRef(I1, I2, MemoryLocation(Ptr, Size), Mode);
  }

private:
  class Concept;

  template <typename T> class Model;

  template <typename T> friend class AAResultBase;

  const TargetLibraryInfo &TLI;

  std::vector<std::unique_ptr<Concept>> AAs;

  std::vector<AnalysisKey *> AADeps;
};

/// Temporary typedef for legacy code that uses a generic \c AliasAnalysis
/// pointer or reference.
using AliasAnalysis = AAResults;

/// A private abstract base class describing the concept of an individual alias
/// analysis implementation.
///
/// This interface is implemented by any \c Model instantiation. It is also the
/// interface which a type used to instantiate the model must provide.
///
/// All of these methods model methods by the same name in the \c
/// AAResults class. Only differences and specifics to how the
/// implementations are called are documented here.
class AAResults::Concept {
public:
  virtual ~Concept() = 0;

  /// An update API used internally by the AAResults to provide
  /// a handle back to the top level aggregation.
  virtual void setAAResults(AAResults *NewAAR) = 0;

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  virtual AliasResult alias(const MemoryLocation &LocA,
                            const MemoryLocation &LocB) = 0;

  /// Checks whether the given location points to constant memory, or if
  /// \p OrLocal is true whether it points to a local alloca.
  virtual bool pointsToConstantMemory(const MemoryLocation &Loc,
                                      bool OrLocal) = 0;

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Get the ModRef info associated with a pointer argument of a callsite. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information.
  virtual ModRefInfo getArgModRefInfo(const CallBase *Call,
                                      unsigned ArgIdx) = 0;

  /// Return the behavior of the given call site.
  virtual FunctionModRefBehavior getModRefBehavior(const CallBase *Call) = 0;

  /// Return the behavior when calling the given function.
  virtual FunctionModRefBehavior getModRefBehavior(const Function *F) = 0;

  /// getModRefInfo (for call sites) - Return information about whether
  /// a particular call site modifies or reads the specified memory location.
  virtual ModRefInfo getModRefInfo(const CallBase *Call,
                                   const MemoryLocation &Loc) = 0;

  /// Return information about whether two call sites may refer to the same set
  /// of memory locations. See the AA documentation for details:
  ///   http://llvm.org/docs/AliasAnalysis.html#ModRefInfo
  virtual ModRefInfo getModRefInfo(const CallBase *Call1,
                                   const CallBase *Call2) = 0;

  /// @}
};

/// A private class template which derives from \c Concept and wraps some other
/// type.
///
/// This models the concept by directly forwarding each interface point to the
/// wrapped type which must implement a compatible interface. This provides
/// a type erased binding.
template <typename AAResultT> class AAResults::Model final : public Concept {
  AAResultT &Result;

public:
  explicit Model(AAResultT &Result, AAResults &AAR) : Result(Result) {
    Result.setAAResults(&AAR);
  }
  ~Model() override = default;

  void setAAResults(AAResults *NewAAR) override { Result.setAAResults(NewAAR); }

  AliasResult alias(const MemoryLocation &LocA,
                    const MemoryLocation &LocB) override {
    return Result.alias(LocA, LocB);
  }

  bool pointsToConstantMemory(const MemoryLocation &Loc,
                              bool OrLocal) override {
    return Result.pointsToConstantMemory(Loc, OrLocal);
  }

  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) override {
    return Result.getArgModRefInfo(Call, ArgIdx);
  }

  FunctionModRefBehavior getModRefBehavior(const CallBase *Call) override {
    return Result.getModRefBehavior(Call);
  }

  FunctionModRefBehavior getModRefBehavior(const Function *F) override {
    return Result.getModRefBehavior(F);
  }

  ModRefInfo getModRefInfo(const CallBase *Call,
                           const MemoryLocation &Loc) override {
    return Result.getModRefInfo(Call, Loc);
  }

  ModRefInfo getModRefInfo(const CallBase *Call1,
                           const CallBase *Call2) override {
    return Result.getModRefInfo(Call1, Call2);
  }
};

/// A CRTP-driven "mixin" base class to help implement the function alias
/// analysis results concept.
///
/// Because of the nature of many alias analysis implementations, they often
/// only implement a subset of the interface. This base class will attempt to
/// implement the remaining portions of the interface in terms of simpler forms
/// of the interface where possible, and otherwise provide conservatively
/// correct fallback implementations.
///
/// Implementors of an alias analysis should derive from this CRTP, and then
/// override specific methods that they wish to customize. There is no need to
/// use virtual anywhere, the CRTP base class does static dispatch to the
/// derived type passed into it.
template <typename DerivedT> class AAResultBase {
  // Expose some parts of the interface only to the AAResults::Model
  // for wrapping. Specifically, this allows the model to call our
  // setAAResults method without exposing it as a fully public API.
  friend class AAResults::Model<DerivedT>;

  /// A pointer to the AAResults object that this AAResult is
  /// aggregated within. May be null if not aggregated.
  AAResults *AAR;

  /// Helper to dispatch calls back through the derived type.
  DerivedT &derived() { return static_cast<DerivedT &>(*this); }

  /// A setter for the AAResults pointer, which is used to satisfy the
  /// AAResults::Model contract.
  void setAAResults(AAResults *NewAAR) { AAR = NewAAR; }

protected:
  /// This proxy class models a common pattern where we delegate to either the
  /// top-level \c AAResults aggregation if one is registered, or to the
  /// current result if none are registered.
  class AAResultsProxy {
    AAResults *AAR;
    DerivedT &CurrentResult;

  public:
    AAResultsProxy(AAResults *AAR, DerivedT &CurrentResult)
        : AAR(AAR), CurrentResult(CurrentResult) {}

    AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
      return AAR ? AAR->alias(LocA, LocB) : CurrentResult.alias(LocA, LocB);
    }

    bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal) {
      return AAR ? AAR->pointsToConstantMemory(Loc, OrLocal)
                 : CurrentResult.pointsToConstantMemory(Loc, OrLocal);
    }

    ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
      return AAR ? AAR->getArgModRefInfo(Call, ArgIdx)
                 : CurrentResult.getArgModRefInfo(Call, ArgIdx);
    }

    FunctionModRefBehavior getModRefBehavior(const CallBase *Call) {
      return AAR ? AAR->getModRefBehavior(Call)
                 : CurrentResult.getModRefBehavior(Call);
    }

    FunctionModRefBehavior getModRefBehavior(const Function *F) {
      return AAR ? AAR->getModRefBehavior(F) : CurrentResult.getModRefBehavior(F);
    }

    ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc) {
      return AAR ? AAR->getModRefInfo(Call, Loc)
                 : CurrentResult.getModRefInfo(Call, Loc);
    }

    ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2) {
      return AAR ? AAR->getModRefInfo(Call1, Call2)
                 : CurrentResult.getModRefInfo(Call1, Call2);
    }
  };

  explicit AAResultBase() = default;

  // Provide all the copy and move constructors so that derived types aren't
  // constrained.
  AAResultBase(const AAResultBase &Arg) {}
  AAResultBase(AAResultBase &&Arg) {}

  /// Get a proxy for the best AA result set to query at this time.
  ///
  /// When this result is part of a larger aggregation, this will proxy to that
  /// aggregation. When this result is used in isolation, it will just delegate
  /// back to the derived class's implementation.
  ///
  /// Note that callers of this need to take considerable care to not cause
  /// performance problems when they use this routine, in the case of a large
  /// number of alias analyses being aggregated, it can be expensive to walk
  /// back across the chain.
  AAResultsProxy getBestAAResults() { return AAResultsProxy(AAR, derived()); }

public:
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return MayAlias;
  }

  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal) {
    return false;
  }

  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
    return ModRefInfo::ModRef;
  }

  FunctionModRefBehavior getModRefBehavior(const CallBase *Call) {
    return FMRB_UnknownModRefBehavior;
  }

  FunctionModRefBehavior getModRefBehavior(const Function *F) {
    return FMRB_UnknownModRefBehavior;
  }

  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc) {
    return ModRefInfo::ModRef;
  }

  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2) {
    return ModRefInfo::ModRef;
  }
};

/// Return true if this pointer is returned by a noalias function.
bool isNoAliasCall(const Value *V);

/// Return true if this is an argument with the noalias attribute.
bool isNoAliasArgument(const Value *V);

/// Return true if this pointer refers to a distinct and identifiable object.
/// This returns true for:
///    Global Variables and Functions (but not Global Aliases)
///    Allocas
///    ByVal and NoAlias Arguments
///    NoAlias returns (e.g. calls to malloc)
///
bool isIdentifiedObject(const Value *V);

/// Return true if V is umabigously identified at the function-level.
/// Different IdentifiedFunctionLocals can't alias.
/// Further, an IdentifiedFunctionLocal can not alias with any function
/// arguments other than itself, which is not necessarily true for
/// IdentifiedObjects.
bool isIdentifiedFunctionLocal(const Value *V);

/// A manager for alias analyses.
///
/// This class can have analyses registered with it and when run, it will run
/// all of them and aggregate their results into single AA results interface
/// that dispatches across all of the alias analysis results available.
///
/// Note that the order in which analyses are registered is very significant.
/// That is the order in which the results will be aggregated and queried.
///
/// This manager effectively wraps the AnalysisManager for registering alias
/// analyses. When you register your alias analysis with this manager, it will
/// ensure the analysis itself is registered with its AnalysisManager.
class AAManager : public AnalysisInfoMixin<AAManager> {
public:
  using Result = AAResults;

  /// Register a specific AA result.
  template <typename AnalysisT> void registerFunctionAnalysis() {
    ResultGetters.push_back(&getFunctionAAResultImpl<AnalysisT>);
  }

  /// Register a specific AA result.
  template <typename AnalysisT> void registerModuleAnalysis() {
    ResultGetters.push_back(&getModuleAAResultImpl<AnalysisT>);
  }

  Result run(Function &F, FunctionAnalysisManager &AM) {
    Result R(AM.getResult<TargetLibraryAnalysis>(F));
    for (auto &Getter : ResultGetters)
      (*Getter)(F, AM, R);
    return R;
  }

private:
  friend AnalysisInfoMixin<AAManager>;

  static AnalysisKey Key;

  SmallVector<void (*)(Function &F, FunctionAnalysisManager &AM,
                       AAResults &AAResults),
              4> ResultGetters;

  template <typename AnalysisT>
  static void getFunctionAAResultImpl(Function &F,
                                      FunctionAnalysisManager &AM,
                                      AAResults &AAResults) {
    AAResults.addAAResult(AM.template getResult<AnalysisT>(F));
    AAResults.addAADependencyID(AnalysisT::ID());
  }

  template <typename AnalysisT>
  static void getModuleAAResultImpl(Function &F, FunctionAnalysisManager &AM,
                                    AAResults &AAResults) {
    auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
    auto &MAM = MAMProxy.getManager();
    if (auto *R = MAM.template getCachedResult<AnalysisT>(*F.getParent())) {
      AAResults.addAAResult(*R);
      MAMProxy
          .template registerOuterAnalysisInvalidation<AnalysisT, AAManager>();
    }
  }
};

/// A wrapper pass to provide the legacy pass manager access to a suitably
/// prepared AAResults object.
class AAResultsWrapperPass : public FunctionPass {
  std::unique_ptr<AAResults> AAR;

public:
  static char ID;

  AAResultsWrapperPass();

  AAResults &getAAResults() { return *AAR; }
  const AAResults &getAAResults() const { return *AAR; }

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// A wrapper pass for external alias analyses. This just squirrels away the
/// callback used to run any analyses and register their results.
struct ExternalAAWrapperPass : ImmutablePass {
  using CallbackT = std::function<void(Pass &, Function &, AAResults &)>;

  CallbackT CB;

  static char ID;

  ExternalAAWrapperPass() : ImmutablePass(ID) {
    initializeExternalAAWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  explicit ExternalAAWrapperPass(CallbackT CB)
      : ImmutablePass(ID), CB(std::move(CB)) {
    initializeExternalAAWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

FunctionPass *createAAResultsWrapperPass();

/// A wrapper pass around a callback which can be used to populate the
/// AAResults in the AAResultsWrapperPass from an external AA.
///
/// The callback provided here will be used each time we prepare an AAResults
/// object, and will receive a reference to the function wrapper pass, the
/// function, and the AAResults object to populate. This should be used when
/// setting up a custom pass pipeline to inject a hook into the AA results.
ImmutablePass *createExternalAAWrapperPass(
    std::function<void(Pass &, Function &, AAResults &)> Callback);

/// A helper for the legacy pass manager to create a \c AAResults
/// object populated to the best of our ability for a particular function when
/// inside of a \c ModulePass or a \c CallGraphSCCPass.
///
/// If a \c ModulePass or a \c CallGraphSCCPass calls \p
/// createLegacyPMAAResults, it also needs to call \p addUsedAAAnalyses in \p
/// getAnalysisUsage.
AAResults createLegacyPMAAResults(Pass &P, Function &F, BasicAAResult &BAR);

/// A helper for the legacy pass manager to populate \p AU to add uses to make
/// sure the analyses required by \p createLegacyPMAAResults are available.
void getAAResultsAnalysisUsage(AnalysisUsage &AU);

} // end namespace llvm

#endif // LLVM_ANALYSIS_ALIASANALYSIS_H
