//===-- EHScopeStack.h - Stack for cleanup IR generation --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes should be the minimum interface required for other parts of
// CodeGen to emit cleanups.  The implementation is in CGCleanup.cpp and other
// implemenentation details that are not widely needed are in CGCleanup.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_EHSCOPESTACK_H
#define LLVM_CLANG_LIB_CODEGEN_EHSCOPESTACK_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"

namespace clang {
namespace CodeGen {

class CodeGenFunction;

/// A branch fixup.  These are required when emitting a goto to a
/// label which hasn't been emitted yet.  The goto is optimistically
/// emitted as a branch to the basic block for the label, and (if it
/// occurs in a scope with non-trivial cleanups) a fixup is added to
/// the innermost cleanup.  When a (normal) cleanup is popped, any
/// unresolved fixups in that scope are threaded through the cleanup.
struct BranchFixup {
  /// The block containing the terminator which needs to be modified
  /// into a switch if this fixup is resolved into the current scope.
  /// If null, LatestBranch points directly to the destination.
  llvm::BasicBlock *OptimisticBranchBlock;

  /// The ultimate destination of the branch.
  ///
  /// This can be set to null to indicate that this fixup was
  /// successfully resolved.
  llvm::BasicBlock *Destination;

  /// The destination index value.
  unsigned DestinationIndex;

  /// The initial branch of the fixup.
  llvm::BranchInst *InitialBranch;
};

template <class T> struct InvariantValue {
  typedef T type;
  typedef T saved_type;
  static bool needsSaving(type value) { return false; }
  static saved_type save(CodeGenFunction &CGF, type value) { return value; }
  static type restore(CodeGenFunction &CGF, saved_type value) { return value; }
};

/// A metaprogramming class for ensuring that a value will dominate an
/// arbitrary position in a function.
template <class T> struct DominatingValue : InvariantValue<T> {};

template <class T, bool mightBeInstruction =
            std::is_base_of<llvm::Value, T>::value &&
            !std::is_base_of<llvm::Constant, T>::value &&
            !std::is_base_of<llvm::BasicBlock, T>::value>
struct DominatingPointer;
template <class T> struct DominatingPointer<T,false> : InvariantValue<T*> {};
// template <class T> struct DominatingPointer<T,true> at end of file

template <class T> struct DominatingValue<T*> : DominatingPointer<T> {};

enum CleanupKind : unsigned {
  /// Denotes a cleanup that should run when a scope is exited using exceptional
  /// control flow (a throw statement leading to stack unwinding, ).
  EHCleanup = 0x1,

  /// Denotes a cleanup that should run when a scope is exited using normal
  /// control flow (falling off the end of the scope, return, goto, ...).
  NormalCleanup = 0x2,

  NormalAndEHCleanup = EHCleanup | NormalCleanup,

  InactiveCleanup = 0x4,
  InactiveEHCleanup = EHCleanup | InactiveCleanup,
  InactiveNormalCleanup = NormalCleanup | InactiveCleanup,
  InactiveNormalAndEHCleanup = NormalAndEHCleanup | InactiveCleanup,

  LifetimeMarker = 0x8,
  NormalEHLifetimeMarker = LifetimeMarker | NormalAndEHCleanup,
};

/// A stack of scopes which respond to exceptions, including cleanups
/// and catch blocks.
class EHScopeStack {
public:
  /* Should switch to alignof(uint64_t) instead of 8, when EHCleanupScope can */
  enum { ScopeStackAlignment = 8 };

  /// A saved depth on the scope stack.  This is necessary because
  /// pushing scopes onto the stack invalidates iterators.
  class stable_iterator {
    friend class EHScopeStack;

    /// Offset from StartOfData to EndOfBuffer.
    ptrdiff_t Size;

    stable_iterator(ptrdiff_t Size) : Size(Size) {}

  public:
    static stable_iterator invalid() { return stable_iterator(-1); }
    stable_iterator() : Size(-1) {}

    bool isValid() const { return Size >= 0; }

    /// Returns true if this scope encloses I.
    /// Returns false if I is invalid.
    /// This scope must be valid.
    bool encloses(stable_iterator I) const { return Size <= I.Size; }

    /// Returns true if this scope strictly encloses I: that is,
    /// if it encloses I and is not I.
    /// Returns false is I is invalid.
    /// This scope must be valid.
    bool strictlyEncloses(stable_iterator I) const { return Size < I.Size; }

    friend bool operator==(stable_iterator A, stable_iterator B) {
      return A.Size == B.Size;
    }
    friend bool operator!=(stable_iterator A, stable_iterator B) {
      return A.Size != B.Size;
    }
  };

  /// Information for lazily generating a cleanup.  Subclasses must be
  /// POD-like: cleanups will not be destructed, and they will be
  /// allocated on the cleanup stack and freely copied and moved
  /// around.
  ///
  /// Cleanup implementations should generally be declared in an
  /// anonymous namespace.
  class Cleanup {
    // Anchor the construction vtable.
    virtual void anchor();

  protected:
    ~Cleanup() = default;

  public:
    Cleanup(const Cleanup &) = default;
    Cleanup(Cleanup &&) {}
    Cleanup() = default;

    /// Generation flags.
    class Flags {
      enum {
        F_IsForEH             = 0x1,
        F_IsNormalCleanupKind = 0x2,
        F_IsEHCleanupKind     = 0x4
      };
      unsigned flags;

    public:
      Flags() : flags(0) {}

      /// isForEH - true if the current emission is for an EH cleanup.
      bool isForEHCleanup() const { return flags & F_IsForEH; }
      bool isForNormalCleanup() const { return !isForEHCleanup(); }
      void setIsForEHCleanup() { flags |= F_IsForEH; }

      bool isNormalCleanupKind() const { return flags & F_IsNormalCleanupKind; }
      void setIsNormalCleanupKind() { flags |= F_IsNormalCleanupKind; }

      /// isEHCleanupKind - true if the cleanup was pushed as an EH
      /// cleanup.
      bool isEHCleanupKind() const { return flags & F_IsEHCleanupKind; }
      void setIsEHCleanupKind() { flags |= F_IsEHCleanupKind; }
    };


    /// Emit the cleanup.  For normal cleanups, this is run in the
    /// same EH context as when the cleanup was pushed, i.e. the
    /// immediately-enclosing context of the cleanup scope.  For
    /// EH cleanups, this is run in a terminate context.
    ///
    // \param flags cleanup kind.
    virtual void Emit(CodeGenFunction &CGF, Flags flags) = 0;
  };

  /// ConditionalCleanup stores the saved form of its parameters,
  /// then restores them and performs the cleanup.
  template <class T, class... As>
  class ConditionalCleanup final : public Cleanup {
    typedef std::tuple<typename DominatingValue<As>::saved_type...> SavedTuple;
    SavedTuple Saved;

    template <std::size_t... Is>
    T restore(CodeGenFunction &CGF, llvm::index_sequence<Is...>) {
      // It's important that the restores are emitted in order. The braced init
      // list guarantees that.
      return T{DominatingValue<As>::restore(CGF, std::get<Is>(Saved))...};
    }

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      restore(CGF, llvm::index_sequence_for<As...>()).Emit(CGF, flags);
    }

  public:
    ConditionalCleanup(typename DominatingValue<As>::saved_type... A)
        : Saved(A...) {}

    ConditionalCleanup(SavedTuple Tuple) : Saved(std::move(Tuple)) {}
  };

private:
  // The implementation for this class is in CGException.h and
  // CGException.cpp; the definition is here because it's used as a
  // member of CodeGenFunction.

  /// The start of the scope-stack buffer, i.e. the allocated pointer
  /// for the buffer.  All of these pointers are either simultaneously
  /// null or simultaneously valid.
  char *StartOfBuffer;

  /// The end of the buffer.
  char *EndOfBuffer;

  /// The first valid entry in the buffer.
  char *StartOfData;

  /// The innermost normal cleanup on the stack.
  stable_iterator InnermostNormalCleanup;

  /// The innermost EH scope on the stack.
  stable_iterator InnermostEHScope;

  /// The current set of branch fixups.  A branch fixup is a jump to
  /// an as-yet unemitted label, i.e. a label for which we don't yet
  /// know the EH stack depth.  Whenever we pop a cleanup, we have
  /// to thread all the current branch fixups through it.
  ///
  /// Fixups are recorded as the Use of the respective branch or
  /// switch statement.  The use points to the final destination.
  /// When popping out of a cleanup, these uses are threaded through
  /// the cleanup and adjusted to point to the new cleanup.
  ///
  /// Note that branches are allowed to jump into protected scopes
  /// in certain situations;  e.g. the following code is legal:
  ///     struct A { ~A(); }; // trivial ctor, non-trivial dtor
  ///     goto foo;
  ///     A a;
  ///    foo:
  ///     bar();
  SmallVector<BranchFixup, 8> BranchFixups;

  char *allocate(size_t Size);
  void deallocate(size_t Size);

  void *pushCleanup(CleanupKind K, size_t DataSize);

public:
  EHScopeStack() : StartOfBuffer(nullptr), EndOfBuffer(nullptr),
                   StartOfData(nullptr), InnermostNormalCleanup(stable_end()),
                   InnermostEHScope(stable_end()) {}
  ~EHScopeStack() { delete[] StartOfBuffer; }

  /// Push a lazily-created cleanup on the stack.
  template <class T, class... As> void pushCleanup(CleanupKind Kind, As... A) {
    static_assert(alignof(T) <= ScopeStackAlignment,
                  "Cleanup's alignment is too large.");
    void *Buffer = pushCleanup(Kind, sizeof(T));
    Cleanup *Obj = new (Buffer) T(A...);
    (void) Obj;
  }

  /// Push a lazily-created cleanup on the stack. Tuple version.
  template <class T, class... As>
  void pushCleanupTuple(CleanupKind Kind, std::tuple<As...> A) {
    static_assert(alignof(T) <= ScopeStackAlignment,
                  "Cleanup's alignment is too large.");
    void *Buffer = pushCleanup(Kind, sizeof(T));
    Cleanup *Obj = new (Buffer) T(std::move(A));
    (void) Obj;
  }

  // Feel free to add more variants of the following:

  /// Push a cleanup with non-constant storage requirements on the
  /// stack.  The cleanup type must provide an additional static method:
  ///   static size_t getExtraSize(size_t);
  /// The argument to this method will be the value N, which will also
  /// be passed as the first argument to the constructor.
  ///
  /// The data stored in the extra storage must obey the same
  /// restrictions as normal cleanup member data.
  ///
  /// The pointer returned from this method is valid until the cleanup
  /// stack is modified.
  template <class T, class... As>
  T *pushCleanupWithExtra(CleanupKind Kind, size_t N, As... A) {
    static_assert(alignof(T) <= ScopeStackAlignment,
                  "Cleanup's alignment is too large.");
    void *Buffer = pushCleanup(Kind, sizeof(T) + T::getExtraSize(N));
    return new (Buffer) T(N, A...);
  }

  void pushCopyOfCleanup(CleanupKind Kind, const void *Cleanup, size_t Size) {
    void *Buffer = pushCleanup(Kind, Size);
    std::memcpy(Buffer, Cleanup, Size);
  }

  /// Pops a cleanup scope off the stack.  This is private to CGCleanup.cpp.
  void popCleanup();

  /// Push a set of catch handlers on the stack.  The catch is
  /// uninitialized and will need to have the given number of handlers
  /// set on it.
  class EHCatchScope *pushCatch(unsigned NumHandlers);

  /// Pops a catch scope off the stack.  This is private to CGException.cpp.
  void popCatch();

  /// Push an exceptions filter on the stack.
  class EHFilterScope *pushFilter(unsigned NumFilters);

  /// Pops an exceptions filter off the stack.
  void popFilter();

  /// Push a terminate handler on the stack.
  void pushTerminate();

  /// Pops a terminate handler off the stack.
  void popTerminate();

  // Returns true iff the current scope is either empty or contains only
  // lifetime markers, i.e. no real cleanup code
  bool containsOnlyLifetimeMarkers(stable_iterator Old) const;

  /// Determines whether the exception-scopes stack is empty.
  bool empty() const { return StartOfData == EndOfBuffer; }

  bool requiresLandingPad() const;

  /// Determines whether there are any normal cleanups on the stack.
  bool hasNormalCleanups() const {
    return InnermostNormalCleanup != stable_end();
  }

  /// Returns the innermost normal cleanup on the stack, or
  /// stable_end() if there are no normal cleanups.
  stable_iterator getInnermostNormalCleanup() const {
    return InnermostNormalCleanup;
  }
  stable_iterator getInnermostActiveNormalCleanup() const;

  stable_iterator getInnermostEHScope() const {
    return InnermostEHScope;
  }


  /// An unstable reference to a scope-stack depth.  Invalidated by
  /// pushes but not pops.
  class iterator;

  /// Returns an iterator pointing to the innermost EH scope.
  iterator begin() const;

  /// Returns an iterator pointing to the outermost EH scope.
  iterator end() const;

  /// Create a stable reference to the top of the EH stack.  The
  /// returned reference is valid until that scope is popped off the
  /// stack.
  stable_iterator stable_begin() const {
    return stable_iterator(EndOfBuffer - StartOfData);
  }

  /// Create a stable reference to the bottom of the EH stack.
  static stable_iterator stable_end() {
    return stable_iterator(0);
  }

  /// Translates an iterator into a stable_iterator.
  stable_iterator stabilize(iterator it) const;

  /// Turn a stable reference to a scope depth into a unstable pointer
  /// to the EH stack.
  iterator find(stable_iterator save) const;

  /// Add a branch fixup to the current cleanup scope.
  BranchFixup &addBranchFixup() {
    assert(hasNormalCleanups() && "adding fixup in scope without cleanups");
    BranchFixups.push_back(BranchFixup());
    return BranchFixups.back();
  }

  unsigned getNumBranchFixups() const { return BranchFixups.size(); }
  BranchFixup &getBranchFixup(unsigned I) {
    assert(I < getNumBranchFixups());
    return BranchFixups[I];
  }

  /// Pops lazily-removed fixups from the end of the list.  This
  /// should only be called by procedures which have just popped a
  /// cleanup or resolved one or more fixups.
  void popNullFixups();

  /// Clears the branch-fixups list.  This should only be called by
  /// ResolveAllBranchFixups.
  void clearFixups() { BranchFixups.clear(); }
};

} // namespace CodeGen
} // namespace clang

#endif
