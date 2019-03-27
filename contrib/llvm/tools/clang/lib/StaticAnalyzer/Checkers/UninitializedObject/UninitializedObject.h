//===----- UninitializedObject.h ---------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines helper classes for UninitializedObjectChecker and
// documentation about the logic of it.
//
// The checker reports uninitialized fields in objects created after a
// constructor call.
//
// This checker has several options:
//   - "Pedantic" (boolean). If its not set or is set to false, the checker
//     won't emit warnings for objects that don't have at least one initialized
//     field. This may be set with
//
//     `-analyzer-config alpha.cplusplus.UninitializedObject:Pedantic=true`.
//
//   - "NotesAsWarnings" (boolean). If set to true, the checker will emit a
//     warning for each uninitialized field, as opposed to emitting one warning
//     per constructor call, and listing the uninitialized fields that belongs
//     to it in notes. Defaults to false.
//
//     `-analyzer-config \
//         alpha.cplusplus.UninitializedObject:NotesAsWarnings=true`.
//
//   - "CheckPointeeInitialization" (boolean). If set to false, the checker will
//     not analyze the pointee of pointer/reference fields, and will only check
//     whether the object itself is initialized. Defaults to false.
//
//     `-analyzer-config \
//         alpha.cplusplus.UninitializedObject:CheckPointeeInitialization=true`.
//
//   - "IgnoreRecordsWithField" (string). If supplied, the checker will not
//     analyze structures that have a field with a name or type name that
//     matches the given pattern. Defaults to "".
//
//     `-analyzer-config \
// alpha.cplusplus.UninitializedObject:IgnoreRecordsWithField="[Tt]ag|[Kk]ind"`.
//
//     TODO: With some clever heuristics, some pointers should be dereferenced
//     by default. For example, if the pointee is constructed within the
//     constructor call, it's reasonable to say that no external object
//     references it, and we wouldn't generate multiple report on the same
//     pointee.
//
// Most of the following methods as well as the checker itself is defined in
// UninitializedObjectChecker.cpp.
//
// Some methods are implemented in UninitializedPointee.cpp, to reduce the
// complexity of the main checker file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_UNINITIALIZEDOBJECT_H
#define LLVM_CLANG_STATICANALYZER_UNINITIALIZEDOBJECT_H

#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

namespace clang {
namespace ento {

struct UninitObjCheckerOptions {
  bool IsPedantic = false;
  bool ShouldConvertNotesToWarnings = false;
  bool CheckPointeeInitialization = false;
  std::string IgnoredRecordsWithFieldPattern;
};

/// A lightweight polymorphic wrapper around FieldRegion *. We'll use this
/// interface to store addinitional information about fields. As described
/// later, a list of these objects (i.e. "fieldchain") will be constructed and
/// used for printing note messages should an uninitialized value be found.
class FieldNode {
protected:
  const FieldRegion *FR;

  /// FieldNodes are never meant to be created on the heap, see
  /// FindUninitializedFields::addFieldToUninits().
  /* non-virtual */ ~FieldNode() = default;

public:
  FieldNode(const FieldRegion *FR) : FR(FR) {}

  // We'll delete all of these special member functions to force the users of
  // this interface to only store references to FieldNode objects in containers.
  FieldNode() = delete;
  FieldNode(const FieldNode &) = delete;
  FieldNode(FieldNode &&) = delete;
  FieldNode &operator=(const FieldNode &) = delete;
  FieldNode &operator=(const FieldNode &&) = delete;

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddPointer(this); }

  /// Helper method for uniqueing.
  bool isSameRegion(const FieldRegion *OtherFR) const {
    // Special FieldNode descendants may wrap nullpointers (for example if they
    // describe a special relationship between two elements of the fieldchain)
    // -- we wouldn't like to unique these objects.
    if (FR == nullptr)
      return false;

    return FR == OtherFR;
  }

  const FieldRegion *getRegion() const { return FR; }
  const FieldDecl *getDecl() const {
    assert(FR);
    return FR->getDecl();
  }

  // When a fieldchain is printed, it will have the following format (without
  // newline, indices are in order of insertion, from 1 to n):
  //
  // <note_message_n>'<prefix_n><prefix_n-1>...<prefix_1>
  //       this-><node_1><separator_1><node_2><separator_2>...<node_n>'

  /// If this is the last element of the fieldchain, this method will print the
  /// note message associated with it.
  /// The note message should state something like "uninitialized field" or
  /// "uninitialized pointee" etc.
  virtual void printNoteMsg(llvm::raw_ostream &Out) const = 0;

  /// Print any prefixes before the fieldchain. Could contain casts, etc.
  virtual void printPrefix(llvm::raw_ostream &Out) const = 0;

  /// Print the node. Should contain the name of the field stored in FR.
  virtual void printNode(llvm::raw_ostream &Out) const = 0;

  /// Print the separator. For example, fields may be separated with '.' or
  /// "->".
  virtual void printSeparator(llvm::raw_ostream &Out) const = 0;

  virtual bool isBase() const { return false; }
};

/// Returns with Field's name. This is a helper function to get the correct name
/// even if Field is a captured lambda variable.
std::string getVariableName(const FieldDecl *Field);

/// Represents a field chain. A field chain is a list of fields where the first
/// element of the chain is the object under checking (not stored), and every
/// other element is a field, and the element that precedes it is the object
/// that contains it.
///
/// Note that this class is immutable (essentially a wrapper around an
/// ImmutableList), new FieldChainInfo objects may be created by member
/// functions such as add() and replaceHead().
class FieldChainInfo {
public:
  using FieldChain = llvm::ImmutableList<const FieldNode &>;

private:
  FieldChain::Factory &ChainFactory;
  FieldChain Chain;

  FieldChainInfo(FieldChain::Factory &F, FieldChain NewChain)
      : FieldChainInfo(F) {
    Chain = NewChain;
  }

public:
  FieldChainInfo() = delete;
  FieldChainInfo(FieldChain::Factory &F) : ChainFactory(F) {}
  FieldChainInfo(const FieldChainInfo &Other) = default;

  /// Constructs a new FieldChainInfo object with \p FN appended.
  template <class FieldNodeT> FieldChainInfo add(const FieldNodeT &FN);

  /// Constructs a new FieldChainInfo object with \p FN as the new head of the
  /// list.
  template <class FieldNodeT> FieldChainInfo replaceHead(const FieldNodeT &FN);

  bool contains(const FieldRegion *FR) const;
  bool isEmpty() const { return Chain.isEmpty(); }

  const FieldNode &getHead() const { return Chain.getHead(); }
  const FieldRegion *getUninitRegion() const { return getHead().getRegion(); }

  void printNoteMsg(llvm::raw_ostream &Out) const;
};

using UninitFieldMap = std::map<const FieldRegion *, llvm::SmallString<50>>;

/// Searches for and stores uninitialized fields in a non-union object.
class FindUninitializedFields {
  ProgramStateRef State;
  const TypedValueRegion *const ObjectR;

  const UninitObjCheckerOptions Opts;
  bool IsAnyFieldInitialized = false;

  FieldChainInfo::FieldChain::Factory ChainFactory;

  /// A map for assigning uninitialized regions to note messages. For example,
  ///
  ///   struct A {
  ///     int x;
  ///   };
  ///
  ///   A a;
  ///
  /// After analyzing `a`, the map will contain a pair for `a.x`'s region and
  /// the note message "uninitialized field 'this->x'.
  UninitFieldMap UninitFields;

public:
  /// Constructs the FindUninitializedField object, searches for and stores
  /// uninitialized fields in R.
  FindUninitializedFields(ProgramStateRef State,
                          const TypedValueRegion *const R,
                          const UninitObjCheckerOptions &Opts);

  /// Returns with the modified state and a map of (uninitialized region,
  /// note message) pairs.
  std::pair<ProgramStateRef, const UninitFieldMap &> getResults() {
    return {State, UninitFields};
  }

  /// Returns whether the analyzed region contains at least one initialized
  /// field. Note that this includes subfields as well, not just direct ones,
  /// and will return false if an uninitialized pointee is found with
  /// CheckPointeeInitialization enabled.
  bool isAnyFieldInitialized() { return IsAnyFieldInitialized; }

private:
  // For the purposes of this checker, we'll regard the analyzed region as a
  // directed tree, where
  //   * the root is the object under checking
  //   * every node is an object that is
  //     - a union
  //     - a non-union record
  //     - dereferenceable (see isDereferencableType())
  //     - an array
  //     - of a primitive type (see isPrimitiveType())
  //   * the parent of each node is the object that contains it
  //   * every leaf is an array, a primitive object, a nullptr or an undefined
  //   pointer.
  //
  // Example:
  //
  //   struct A {
  //      struct B {
  //        int x, y = 0;
  //      };
  //      B b;
  //      int *iptr = new int;
  //      B* bptr;
  //
  //      A() {}
  //   };
  //
  // The directed tree:
  //
  //           ->x
  //          /
  //      ->b--->y
  //     /
  //    A-->iptr->(int value)
  //     \
  //      ->bptr
  //
  // From this we'll construct a vector of fieldchains, where each fieldchain
  // represents an uninitialized field. An uninitialized field may be a
  // primitive object, a pointer, a pointee or a union without a single
  // initialized field.
  // In the above example, for the default constructor call we'll end up with
  // these fieldchains:
  //
  //   this->b.x
  //   this->iptr (pointee uninit)
  //   this->bptr (pointer uninit)
  //
  // We'll traverse each node of the above graph with the appropriate one of
  // these methods:

  /// Checks the region of a union object, and returns true if no field is
  /// initialized within the region.
  bool isUnionUninit(const TypedValueRegion *R);

  /// Checks a region of a non-union object, and returns true if an
  /// uninitialized field is found within the region.
  bool isNonUnionUninit(const TypedValueRegion *R, FieldChainInfo LocalChain);

  /// Checks a region of a pointer or reference object, and returns true if the
  /// ptr/ref object itself or any field within the pointee's region is
  /// uninitialized.
  bool isDereferencableUninit(const FieldRegion *FR, FieldChainInfo LocalChain);

  /// Returns true if the value of a primitive object is uninitialized.
  bool isPrimitiveUninit(const SVal &V);

  // Note that we don't have a method for arrays -- the elements of an array are
  // often left uninitialized intentionally even when it is of a C++ record
  // type, so we'll assume that an array is always initialized.
  // TODO: Add a support for nonloc::LocAsInteger.

  /// Processes LocalChain and attempts to insert it into UninitFields. Returns
  /// true on success. Also adds the head of the list and \p PointeeR (if
  /// supplied) to the GDM as already analyzed objects.
  ///
  /// Since this class analyzes regions with recursion, we'll only store
  /// references to temporary FieldNode objects created on the stack. This means
  /// that after analyzing a leaf of the directed tree described above, the
  /// elements LocalChain references will be destructed, so we can't store it
  /// directly.
  bool addFieldToUninits(FieldChainInfo LocalChain,
                         const MemRegion *PointeeR = nullptr);
};

/// Returns true if T is a primitive type. An object of a primitive type only
/// needs to be analyzed as much as checking whether their value is undefined.
inline bool isPrimitiveType(const QualType &T) {
  return T->isBuiltinType() || T->isEnumeralType() ||
         T->isMemberPointerType() || T->isBlockPointerType() ||
         T->isFunctionType();
}

inline bool isDereferencableType(const QualType &T) {
  return T->isAnyPointerType() || T->isReferenceType();
}

// Template method definitions.

template <class FieldNodeT>
inline FieldChainInfo FieldChainInfo::add(const FieldNodeT &FN) {
  assert(!contains(FN.getRegion()) &&
         "Can't add a field that is already a part of the "
         "fieldchain! Is this a cyclic reference?");

  FieldChainInfo NewChain = *this;
  NewChain.Chain = ChainFactory.add(FN, Chain);
  return NewChain;
}

template <class FieldNodeT>
inline FieldChainInfo FieldChainInfo::replaceHead(const FieldNodeT &FN) {
  FieldChainInfo NewChain(ChainFactory, Chain.getTail());
  return NewChain.add(FN);
}

} // end of namespace ento
} // end of namespace clang

#endif // LLVM_CLANG_STATICANALYZER_UNINITIALIZEDOBJECT_H
