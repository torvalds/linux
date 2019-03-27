//===-- CGCleanup.h - Classes for cleanups IR generation --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes support the generation of LLVM IR for cleanups.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGCLEANUP_H
#define LLVM_CLANG_LIB_CODEGEN_CGCLEANUP_H

#include "EHScopeStack.h"

#include "Address.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class BasicBlock;
class Value;
class ConstantInt;
class AllocaInst;
}

namespace clang {
class FunctionDecl;
namespace CodeGen {
class CodeGenModule;
class CodeGenFunction;

/// The MS C++ ABI needs a pointer to RTTI data plus some flags to describe the
/// type of a catch handler, so we use this wrapper.
struct CatchTypeInfo {
  llvm::Constant *RTTI;
  unsigned Flags;
};

/// A protected scope for zero-cost EH handling.
class EHScope {
  llvm::BasicBlock *CachedLandingPad;
  llvm::BasicBlock *CachedEHDispatchBlock;

  EHScopeStack::stable_iterator EnclosingEHScope;

  class CommonBitFields {
    friend class EHScope;
    unsigned Kind : 3;
  };
  enum { NumCommonBits = 3 };

protected:
  class CatchBitFields {
    friend class EHCatchScope;
    unsigned : NumCommonBits;

    unsigned NumHandlers : 32 - NumCommonBits;
  };

  class CleanupBitFields {
    friend class EHCleanupScope;
    unsigned : NumCommonBits;

    /// Whether this cleanup needs to be run along normal edges.
    unsigned IsNormalCleanup : 1;

    /// Whether this cleanup needs to be run along exception edges.
    unsigned IsEHCleanup : 1;

    /// Whether this cleanup is currently active.
    unsigned IsActive : 1;

    /// Whether this cleanup is a lifetime marker
    unsigned IsLifetimeMarker : 1;

    /// Whether the normal cleanup should test the activation flag.
    unsigned TestFlagInNormalCleanup : 1;

    /// Whether the EH cleanup should test the activation flag.
    unsigned TestFlagInEHCleanup : 1;

    /// The amount of extra storage needed by the Cleanup.
    /// Always a multiple of the scope-stack alignment.
    unsigned CleanupSize : 12;
  };

  class FilterBitFields {
    friend class EHFilterScope;
    unsigned : NumCommonBits;

    unsigned NumFilters : 32 - NumCommonBits;
  };

  union {
    CommonBitFields CommonBits;
    CatchBitFields CatchBits;
    CleanupBitFields CleanupBits;
    FilterBitFields FilterBits;
  };

public:
  enum Kind { Cleanup, Catch, Terminate, Filter, PadEnd };

  EHScope(Kind kind, EHScopeStack::stable_iterator enclosingEHScope)
    : CachedLandingPad(nullptr), CachedEHDispatchBlock(nullptr),
      EnclosingEHScope(enclosingEHScope) {
    CommonBits.Kind = kind;
  }

  Kind getKind() const { return static_cast<Kind>(CommonBits.Kind); }

  llvm::BasicBlock *getCachedLandingPad() const {
    return CachedLandingPad;
  }

  void setCachedLandingPad(llvm::BasicBlock *block) {
    CachedLandingPad = block;
  }

  llvm::BasicBlock *getCachedEHDispatchBlock() const {
    return CachedEHDispatchBlock;
  }

  void setCachedEHDispatchBlock(llvm::BasicBlock *block) {
    CachedEHDispatchBlock = block;
  }

  bool hasEHBranches() const {
    if (llvm::BasicBlock *block = getCachedEHDispatchBlock())
      return !block->use_empty();
    return false;
  }

  EHScopeStack::stable_iterator getEnclosingEHScope() const {
    return EnclosingEHScope;
  }
};

/// A scope which attempts to handle some, possibly all, types of
/// exceptions.
///
/// Objective C \@finally blocks are represented using a cleanup scope
/// after the catch scope.
class EHCatchScope : public EHScope {
  // In effect, we have a flexible array member
  //   Handler Handlers[0];
  // But that's only standard in C99, not C++, so we have to do
  // annoying pointer arithmetic instead.

public:
  struct Handler {
    /// A type info value, or null (C++ null, not an LLVM null pointer)
    /// for a catch-all.
    CatchTypeInfo Type;

    /// The catch handler for this type.
    llvm::BasicBlock *Block;

    bool isCatchAll() const { return Type.RTTI == nullptr; }
  };

private:
  friend class EHScopeStack;

  Handler *getHandlers() {
    return reinterpret_cast<Handler*>(this+1);
  }

  const Handler *getHandlers() const {
    return reinterpret_cast<const Handler*>(this+1);
  }

public:
  static size_t getSizeForNumHandlers(unsigned N) {
    return sizeof(EHCatchScope) + N * sizeof(Handler);
  }

  EHCatchScope(unsigned numHandlers,
               EHScopeStack::stable_iterator enclosingEHScope)
    : EHScope(Catch, enclosingEHScope) {
    CatchBits.NumHandlers = numHandlers;
    assert(CatchBits.NumHandlers == numHandlers && "NumHandlers overflow?");
  }

  unsigned getNumHandlers() const {
    return CatchBits.NumHandlers;
  }

  void setCatchAllHandler(unsigned I, llvm::BasicBlock *Block) {
    setHandler(I, CatchTypeInfo{nullptr, 0}, Block);
  }

  void setHandler(unsigned I, llvm::Constant *Type, llvm::BasicBlock *Block) {
    assert(I < getNumHandlers());
    getHandlers()[I].Type = CatchTypeInfo{Type, 0};
    getHandlers()[I].Block = Block;
  }

  void setHandler(unsigned I, CatchTypeInfo Type, llvm::BasicBlock *Block) {
    assert(I < getNumHandlers());
    getHandlers()[I].Type = Type;
    getHandlers()[I].Block = Block;
  }

  const Handler &getHandler(unsigned I) const {
    assert(I < getNumHandlers());
    return getHandlers()[I];
  }

  // Clear all handler blocks.
  // FIXME: it's better to always call clearHandlerBlocks in DTOR and have a
  // 'takeHandler' or some such function which removes ownership from the
  // EHCatchScope object if the handlers should live longer than EHCatchScope.
  void clearHandlerBlocks() {
    for (unsigned I = 0, N = getNumHandlers(); I != N; ++I)
      delete getHandler(I).Block;
  }

  typedef const Handler *iterator;
  iterator begin() const { return getHandlers(); }
  iterator end() const { return getHandlers() + getNumHandlers(); }

  static bool classof(const EHScope *Scope) {
    return Scope->getKind() == Catch;
  }
};

/// A cleanup scope which generates the cleanup blocks lazily.
class alignas(8) EHCleanupScope : public EHScope {
  /// The nearest normal cleanup scope enclosing this one.
  EHScopeStack::stable_iterator EnclosingNormal;

  /// The nearest EH scope enclosing this one.
  EHScopeStack::stable_iterator EnclosingEH;

  /// The dual entry/exit block along the normal edge.  This is lazily
  /// created if needed before the cleanup is popped.
  llvm::BasicBlock *NormalBlock;

  /// An optional i1 variable indicating whether this cleanup has been
  /// activated yet.
  llvm::AllocaInst *ActiveFlag;

  /// Extra information required for cleanups that have resolved
  /// branches through them.  This has to be allocated on the side
  /// because everything on the cleanup stack has be trivially
  /// movable.
  struct ExtInfo {
    /// The destinations of normal branch-afters and branch-throughs.
    llvm::SmallPtrSet<llvm::BasicBlock*, 4> Branches;

    /// Normal branch-afters.
    SmallVector<std::pair<llvm::BasicBlock*,llvm::ConstantInt*>, 4>
      BranchAfters;
  };
  mutable struct ExtInfo *ExtInfo;

  /// The number of fixups required by enclosing scopes (not including
  /// this one).  If this is the top cleanup scope, all the fixups
  /// from this index onwards belong to this scope.
  unsigned FixupDepth;

  struct ExtInfo &getExtInfo() {
    if (!ExtInfo) ExtInfo = new struct ExtInfo();
    return *ExtInfo;
  }

  const struct ExtInfo &getExtInfo() const {
    if (!ExtInfo) ExtInfo = new struct ExtInfo();
    return *ExtInfo;
  }

public:
  /// Gets the size required for a lazy cleanup scope with the given
  /// cleanup-data requirements.
  static size_t getSizeForCleanupSize(size_t Size) {
    return sizeof(EHCleanupScope) + Size;
  }

  size_t getAllocatedSize() const {
    return sizeof(EHCleanupScope) + CleanupBits.CleanupSize;
  }

  EHCleanupScope(bool isNormal, bool isEH, bool isActive,
                 unsigned cleanupSize, unsigned fixupDepth,
                 EHScopeStack::stable_iterator enclosingNormal,
                 EHScopeStack::stable_iterator enclosingEH)
      : EHScope(EHScope::Cleanup, enclosingEH),
        EnclosingNormal(enclosingNormal), NormalBlock(nullptr),
        ActiveFlag(nullptr), ExtInfo(nullptr), FixupDepth(fixupDepth) {
    CleanupBits.IsNormalCleanup = isNormal;
    CleanupBits.IsEHCleanup = isEH;
    CleanupBits.IsActive = isActive;
    CleanupBits.IsLifetimeMarker = false;
    CleanupBits.TestFlagInNormalCleanup = false;
    CleanupBits.TestFlagInEHCleanup = false;
    CleanupBits.CleanupSize = cleanupSize;

    assert(CleanupBits.CleanupSize == cleanupSize && "cleanup size overflow");
  }

  void Destroy() {
    delete ExtInfo;
  }
  // Objects of EHCleanupScope are not destructed. Use Destroy().
  ~EHCleanupScope() = delete;

  bool isNormalCleanup() const { return CleanupBits.IsNormalCleanup; }
  llvm::BasicBlock *getNormalBlock() const { return NormalBlock; }
  void setNormalBlock(llvm::BasicBlock *BB) { NormalBlock = BB; }

  bool isEHCleanup() const { return CleanupBits.IsEHCleanup; }

  bool isActive() const { return CleanupBits.IsActive; }
  void setActive(bool A) { CleanupBits.IsActive = A; }

  bool isLifetimeMarker() const { return CleanupBits.IsLifetimeMarker; }
  void setLifetimeMarker() { CleanupBits.IsLifetimeMarker = true; }

  bool hasActiveFlag() const { return ActiveFlag != nullptr; }
  Address getActiveFlag() const {
    return Address(ActiveFlag, CharUnits::One());
  }
  void setActiveFlag(Address Var) {
    assert(Var.getAlignment().isOne());
    ActiveFlag = cast<llvm::AllocaInst>(Var.getPointer());
  }

  void setTestFlagInNormalCleanup() {
    CleanupBits.TestFlagInNormalCleanup = true;
  }
  bool shouldTestFlagInNormalCleanup() const {
    return CleanupBits.TestFlagInNormalCleanup;
  }

  void setTestFlagInEHCleanup() {
    CleanupBits.TestFlagInEHCleanup = true;
  }
  bool shouldTestFlagInEHCleanup() const {
    return CleanupBits.TestFlagInEHCleanup;
  }

  unsigned getFixupDepth() const { return FixupDepth; }
  EHScopeStack::stable_iterator getEnclosingNormalCleanup() const {
    return EnclosingNormal;
  }

  size_t getCleanupSize() const { return CleanupBits.CleanupSize; }
  void *getCleanupBuffer() { return this + 1; }

  EHScopeStack::Cleanup *getCleanup() {
    return reinterpret_cast<EHScopeStack::Cleanup*>(getCleanupBuffer());
  }

  /// True if this cleanup scope has any branch-afters or branch-throughs.
  bool hasBranches() const { return ExtInfo && !ExtInfo->Branches.empty(); }

  /// Add a branch-after to this cleanup scope.  A branch-after is a
  /// branch from a point protected by this (normal) cleanup to a
  /// point in the normal cleanup scope immediately containing it.
  /// For example,
  ///   for (;;) { A a; break; }
  /// contains a branch-after.
  ///
  /// Branch-afters each have their own destination out of the
  /// cleanup, guaranteed distinct from anything else threaded through
  /// it.  Therefore branch-afters usually force a switch after the
  /// cleanup.
  void addBranchAfter(llvm::ConstantInt *Index,
                      llvm::BasicBlock *Block) {
    struct ExtInfo &ExtInfo = getExtInfo();
    if (ExtInfo.Branches.insert(Block).second)
      ExtInfo.BranchAfters.push_back(std::make_pair(Block, Index));
  }

  /// Return the number of unique branch-afters on this scope.
  unsigned getNumBranchAfters() const {
    return ExtInfo ? ExtInfo->BranchAfters.size() : 0;
  }

  llvm::BasicBlock *getBranchAfterBlock(unsigned I) const {
    assert(I < getNumBranchAfters());
    return ExtInfo->BranchAfters[I].first;
  }

  llvm::ConstantInt *getBranchAfterIndex(unsigned I) const {
    assert(I < getNumBranchAfters());
    return ExtInfo->BranchAfters[I].second;
  }

  /// Add a branch-through to this cleanup scope.  A branch-through is
  /// a branch from a scope protected by this (normal) cleanup to an
  /// enclosing scope other than the immediately-enclosing normal
  /// cleanup scope.
  ///
  /// In the following example, the branch through B's scope is a
  /// branch-through, while the branch through A's scope is a
  /// branch-after:
  ///   for (;;) { A a; B b; break; }
  ///
  /// All branch-throughs have a common destination out of the
  /// cleanup, one possibly shared with the fall-through.  Therefore
  /// branch-throughs usually don't force a switch after the cleanup.
  ///
  /// \return true if the branch-through was new to this scope
  bool addBranchThrough(llvm::BasicBlock *Block) {
    return getExtInfo().Branches.insert(Block).second;
  }

  /// Determines if this cleanup scope has any branch throughs.
  bool hasBranchThroughs() const {
    if (!ExtInfo) return false;
    return (ExtInfo->BranchAfters.size() != ExtInfo->Branches.size());
  }

  static bool classof(const EHScope *Scope) {
    return (Scope->getKind() == Cleanup);
  }
};
// NOTE: there's a bunch of different data classes tacked on after an
// EHCleanupScope. It is asserted (in EHScopeStack::pushCleanup*) that
// they don't require greater alignment than ScopeStackAlignment. So,
// EHCleanupScope ought to have alignment equal to that -- not more
// (would be misaligned by the stack allocator), and not less (would
// break the appended classes).
static_assert(alignof(EHCleanupScope) == EHScopeStack::ScopeStackAlignment,
              "EHCleanupScope expected alignment");

/// An exceptions scope which filters exceptions thrown through it.
/// Only exceptions matching the filter types will be permitted to be
/// thrown.
///
/// This is used to implement C++ exception specifications.
class EHFilterScope : public EHScope {
  // Essentially ends in a flexible array member:
  // llvm::Value *FilterTypes[0];

  llvm::Value **getFilters() {
    return reinterpret_cast<llvm::Value**>(this+1);
  }

  llvm::Value * const *getFilters() const {
    return reinterpret_cast<llvm::Value* const *>(this+1);
  }

public:
  EHFilterScope(unsigned numFilters)
    : EHScope(Filter, EHScopeStack::stable_end()) {
    FilterBits.NumFilters = numFilters;
    assert(FilterBits.NumFilters == numFilters && "NumFilters overflow");
  }

  static size_t getSizeForNumFilters(unsigned numFilters) {
    return sizeof(EHFilterScope) + numFilters * sizeof(llvm::Value*);
  }

  unsigned getNumFilters() const { return FilterBits.NumFilters; }

  void setFilter(unsigned i, llvm::Value *filterValue) {
    assert(i < getNumFilters());
    getFilters()[i] = filterValue;
  }

  llvm::Value *getFilter(unsigned i) const {
    assert(i < getNumFilters());
    return getFilters()[i];
  }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == Filter;
  }
};

/// An exceptions scope which calls std::terminate if any exception
/// reaches it.
class EHTerminateScope : public EHScope {
public:
  EHTerminateScope(EHScopeStack::stable_iterator enclosingEHScope)
    : EHScope(Terminate, enclosingEHScope) {}
  static size_t getSize() { return sizeof(EHTerminateScope); }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == Terminate;
  }
};

class EHPadEndScope : public EHScope {
public:
  EHPadEndScope(EHScopeStack::stable_iterator enclosingEHScope)
      : EHScope(PadEnd, enclosingEHScope) {}
  static size_t getSize() { return sizeof(EHPadEndScope); }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == PadEnd;
  }
};

/// A non-stable pointer into the scope stack.
class EHScopeStack::iterator {
  char *Ptr;

  friend class EHScopeStack;
  explicit iterator(char *Ptr) : Ptr(Ptr) {}

public:
  iterator() : Ptr(nullptr) {}

  EHScope *get() const {
    return reinterpret_cast<EHScope*>(Ptr);
  }

  EHScope *operator->() const { return get(); }
  EHScope &operator*() const { return *get(); }

  iterator &operator++() {
    size_t Size;
    switch (get()->getKind()) {
    case EHScope::Catch:
      Size = EHCatchScope::getSizeForNumHandlers(
          static_cast<const EHCatchScope *>(get())->getNumHandlers());
      break;

    case EHScope::Filter:
      Size = EHFilterScope::getSizeForNumFilters(
          static_cast<const EHFilterScope *>(get())->getNumFilters());
      break;

    case EHScope::Cleanup:
      Size = static_cast<const EHCleanupScope *>(get())->getAllocatedSize();
      break;

    case EHScope::Terminate:
      Size = EHTerminateScope::getSize();
      break;

    case EHScope::PadEnd:
      Size = EHPadEndScope::getSize();
      break;
    }
    Ptr += llvm::alignTo(Size, ScopeStackAlignment);
    return *this;
  }

  iterator next() {
    iterator copy = *this;
    ++copy;
    return copy;
  }

  iterator operator++(int) {
    iterator copy = *this;
    operator++();
    return copy;
  }

  bool encloses(iterator other) const { return Ptr >= other.Ptr; }
  bool strictlyEncloses(iterator other) const { return Ptr > other.Ptr; }

  bool operator==(iterator other) const { return Ptr == other.Ptr; }
  bool operator!=(iterator other) const { return Ptr != other.Ptr; }
};

inline EHScopeStack::iterator EHScopeStack::begin() const {
  return iterator(StartOfData);
}

inline EHScopeStack::iterator EHScopeStack::end() const {
  return iterator(EndOfBuffer);
}

inline void EHScopeStack::popCatch() {
  assert(!empty() && "popping exception stack when not empty");

  EHCatchScope &scope = cast<EHCatchScope>(*begin());
  InnermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHCatchScope::getSizeForNumHandlers(scope.getNumHandlers()));
}

inline void EHScopeStack::popTerminate() {
  assert(!empty() && "popping exception stack when not empty");

  EHTerminateScope &scope = cast<EHTerminateScope>(*begin());
  InnermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHTerminateScope::getSize());
}

inline EHScopeStack::iterator EHScopeStack::find(stable_iterator sp) const {
  assert(sp.isValid() && "finding invalid savepoint");
  assert(sp.Size <= stable_begin().Size && "finding savepoint after pop");
  return iterator(EndOfBuffer - sp.Size);
}

inline EHScopeStack::stable_iterator
EHScopeStack::stabilize(iterator ir) const {
  assert(StartOfData <= ir.Ptr && ir.Ptr <= EndOfBuffer);
  return stable_iterator(EndOfBuffer - ir.Ptr);
}

/// The exceptions personality for a function.
struct EHPersonality {
  const char *PersonalityFn;

  // If this is non-null, this personality requires a non-standard
  // function for rethrowing an exception after a catchall cleanup.
  // This function must have prototype void(void*).
  const char *CatchallRethrowFn;

  static const EHPersonality &get(CodeGenModule &CGM, const FunctionDecl *FD);
  static const EHPersonality &get(CodeGenFunction &CGF);

  static const EHPersonality GNU_C;
  static const EHPersonality GNU_C_SJLJ;
  static const EHPersonality GNU_C_SEH;
  static const EHPersonality GNU_ObjC;
  static const EHPersonality GNU_ObjC_SJLJ;
  static const EHPersonality GNU_ObjC_SEH;
  static const EHPersonality GNUstep_ObjC;
  static const EHPersonality GNU_ObjCXX;
  static const EHPersonality NeXT_ObjC;
  static const EHPersonality GNU_CPlusPlus;
  static const EHPersonality GNU_CPlusPlus_SJLJ;
  static const EHPersonality GNU_CPlusPlus_SEH;
  static const EHPersonality MSVC_except_handler;
  static const EHPersonality MSVC_C_specific_handler;
  static const EHPersonality MSVC_CxxFrameHandler3;
  static const EHPersonality GNU_Wasm_CPlusPlus;

  /// Does this personality use landingpads or the family of pad instructions
  /// designed to form funclets?
  bool usesFuncletPads() const {
    return isMSVCPersonality() || isWasmPersonality();
  }

  bool isMSVCPersonality() const {
    return this == &MSVC_except_handler || this == &MSVC_C_specific_handler ||
           this == &MSVC_CxxFrameHandler3;
  }

  bool isWasmPersonality() const { return this == &GNU_Wasm_CPlusPlus; }

  bool isMSVCXXPersonality() const { return this == &MSVC_CxxFrameHandler3; }
};
}
}

#endif
