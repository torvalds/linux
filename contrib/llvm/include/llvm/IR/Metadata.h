//===- llvm/IR/Metadata.h - Metadata definitions ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations for metadata subclasses.
/// They represent the different flavors of metadata that live in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_METADATA_H
#define LLVM_IR_METADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace llvm {

class Module;
class ModuleSlotTracker;
class raw_ostream;
class Type;

enum LLVMConstants : uint32_t {
  DEBUG_METADATA_VERSION = 3 // Current debug info version number.
};

/// Root of the metadata hierarchy.
///
/// This is a root class for typeless data in the IR.
class Metadata {
  friend class ReplaceableMetadataImpl;

  /// RTTI.
  const unsigned char SubclassID;

protected:
  /// Active type of storage.
  enum StorageType { Uniqued, Distinct, Temporary };

  /// Storage flag for non-uniqued, otherwise unowned, metadata.
  unsigned char Storage : 7;
  // TODO: expose remaining bits to subclasses.

  unsigned char ImplicitCode : 1;

  unsigned short SubclassData16 = 0;
  unsigned SubclassData32 = 0;

public:
  enum MetadataKind {
#define HANDLE_METADATA_LEAF(CLASS) CLASS##Kind,
#include "llvm/IR/Metadata.def"
  };

protected:
  Metadata(unsigned ID, StorageType Storage)
      : SubclassID(ID), Storage(Storage), ImplicitCode(false) {
    static_assert(sizeof(*this) == 8, "Metadata fields poorly packed");
  }

  ~Metadata() = default;

  /// Default handling of a changed operand, which asserts.
  ///
  /// If subclasses pass themselves in as owners to a tracking node reference,
  /// they must provide an implementation of this method.
  void handleChangedOperand(void *, Metadata *) {
    llvm_unreachable("Unimplemented in Metadata subclass");
  }

public:
  unsigned getMetadataID() const { return SubclassID; }

  /// User-friendly dump.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  ///
  /// Note: this uses an explicit overload instead of default arguments so that
  /// the nullptr version is easy to call from a debugger.
  ///
  /// @{
  void dump() const;
  void dump(const Module *M) const;
  /// @}

  /// Print.
  ///
  /// Prints definition of \c this.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  void print(raw_ostream &OS, const Module *M = nullptr,
             bool IsForDebug = false) const;
  void print(raw_ostream &OS, ModuleSlotTracker &MST, const Module *M = nullptr,
             bool IsForDebug = false) const;
  /// @}

  /// Print as operand.
  ///
  /// Prints reference of \c this.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  void printAsOperand(raw_ostream &OS, const Module *M = nullptr) const;
  void printAsOperand(raw_ostream &OS, ModuleSlotTracker &MST,
                      const Module *M = nullptr) const;
  /// @}
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(Metadata, LLVMMetadataRef)

// Specialized opaque metadata conversions.
inline Metadata **unwrap(LLVMMetadataRef *MDs) {
  return reinterpret_cast<Metadata**>(MDs);
}

#define HANDLE_METADATA(CLASS) class CLASS;
#include "llvm/IR/Metadata.def"

// Provide specializations of isa so that we don't need definitions of
// subclasses to see if the metadata is a subclass.
#define HANDLE_METADATA_LEAF(CLASS)                                            \
  template <> struct isa_impl<CLASS, Metadata> {                               \
    static inline bool doit(const Metadata &MD) {                              \
      return MD.getMetadataID() == Metadata::CLASS##Kind;                      \
    }                                                                          \
  };
#include "llvm/IR/Metadata.def"

inline raw_ostream &operator<<(raw_ostream &OS, const Metadata &MD) {
  MD.print(OS);
  return OS;
}

/// Metadata wrapper in the Value hierarchy.
///
/// A member of the \a Value hierarchy to represent a reference to metadata.
/// This allows, e.g., instrinsics to have metadata as operands.
///
/// Notably, this is the only thing in either hierarchy that is allowed to
/// reference \a LocalAsMetadata.
class MetadataAsValue : public Value {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  Metadata *MD;

  MetadataAsValue(Type *Ty, Metadata *MD);

  /// Drop use of metadata (during teardown).
  void dropUse() { MD = nullptr; }

public:
  ~MetadataAsValue();

  static MetadataAsValue *get(LLVMContext &Context, Metadata *MD);
  static MetadataAsValue *getIfExists(LLVMContext &Context, Metadata *MD);

  Metadata *getMetadata() const { return MD; }

  static bool classof(const Value *V) {
    return V->getValueID() == MetadataAsValueVal;
  }

private:
  void handleChangedMetadata(Metadata *MD);
  void track();
  void untrack();
};

/// API for tracking metadata references through RAUW and deletion.
///
/// Shared API for updating \a Metadata pointers in subclasses that support
/// RAUW.
///
/// This API is not meant to be used directly.  See \a TrackingMDRef for a
/// user-friendly tracking reference.
class MetadataTracking {
public:
  /// Track the reference to metadata.
  ///
  /// Register \c MD with \c *MD, if the subclass supports tracking.  If \c *MD
  /// gets RAUW'ed, \c MD will be updated to the new address.  If \c *MD gets
  /// deleted, \c MD will be set to \c nullptr.
  ///
  /// If tracking isn't supported, \c *MD will not change.
  ///
  /// \return true iff tracking is supported by \c MD.
  static bool track(Metadata *&MD) {
    return track(&MD, *MD, static_cast<Metadata *>(nullptr));
  }

  /// Track the reference to metadata for \a Metadata.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  static bool track(void *Ref, Metadata &MD, Metadata &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Track the reference to metadata for \a MetadataAsValue.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  static bool track(void *Ref, Metadata &MD, MetadataAsValue &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Stop tracking a reference to metadata.
  ///
  /// Stops \c *MD from tracking \c MD.
  static void untrack(Metadata *&MD) { untrack(&MD, *MD); }
  static void untrack(void *Ref, Metadata &MD);

  /// Move tracking from one reference to another.
  ///
  /// Semantically equivalent to \c untrack(MD) followed by \c track(New),
  /// except that ownership callbacks are maintained.
  ///
  /// Note: it is an error if \c *MD does not equal \c New.
  ///
  /// \return true iff tracking is supported by \c MD.
  static bool retrack(Metadata *&MD, Metadata *&New) {
    return retrack(&MD, *MD, &New);
  }
  static bool retrack(void *Ref, Metadata &MD, void *New);

  /// Check whether metadata is replaceable.
  static bool isReplaceable(const Metadata &MD);

  using OwnerTy = PointerUnion<MetadataAsValue *, Metadata *>;

private:
  /// Track a reference to metadata for an owner.
  ///
  /// Generalized version of tracking.
  static bool track(void *Ref, Metadata &MD, OwnerTy Owner);
};

/// Shared implementation of use-lists for replaceable metadata.
///
/// Most metadata cannot be RAUW'ed.  This is a shared implementation of
/// use-lists and associated API for the two that support it (\a ValueAsMetadata
/// and \a TempMDNode).
class ReplaceableMetadataImpl {
  friend class MetadataTracking;

public:
  using OwnerTy = MetadataTracking::OwnerTy;

private:
  LLVMContext &Context;
  uint64_t NextIndex = 0;
  SmallDenseMap<void *, std::pair<OwnerTy, uint64_t>, 4> UseMap;

public:
  ReplaceableMetadataImpl(LLVMContext &Context) : Context(Context) {}

  ~ReplaceableMetadataImpl() {
    assert(UseMap.empty() && "Cannot destroy in-use replaceable metadata");
  }

  LLVMContext &getContext() const { return Context; }

  /// Replace all uses of this with MD.
  ///
  /// Replace all uses of this with \c MD, which is allowed to be null.
  void replaceAllUsesWith(Metadata *MD);

  /// Resolve all uses of this.
  ///
  /// Resolve all uses of this, turning off RAUW permanently.  If \c
  /// ResolveUsers, call \a MDNode::resolve() on any users whose last operand
  /// is resolved.
  void resolveAllUses(bool ResolveUsers = true);

private:
  void addRef(void *Ref, OwnerTy Owner);
  void dropRef(void *Ref);
  void moveRef(void *Ref, void *New, const Metadata &MD);

  /// Lazily construct RAUW support on MD.
  ///
  /// If this is an unresolved MDNode, RAUW support will be created on-demand.
  /// ValueAsMetadata always has RAUW support.
  static ReplaceableMetadataImpl *getOrCreate(Metadata &MD);

  /// Get RAUW support on MD, if it exists.
  static ReplaceableMetadataImpl *getIfExists(Metadata &MD);

  /// Check whether this node will support RAUW.
  ///
  /// Returns \c true unless getOrCreate() would return null.
  static bool isReplaceable(const Metadata &MD);
};

/// Value wrapper in the Metadata hierarchy.
///
/// This is a custom value handle that allows other metadata to refer to
/// classes in the Value hierarchy.
///
/// Because of full uniquing support, each value is only wrapped by a single \a
/// ValueAsMetadata object, so the lookup maps are far more efficient than
/// those using ValueHandleBase.
class ValueAsMetadata : public Metadata, ReplaceableMetadataImpl {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  Value *V;

  /// Drop users without RAUW (during teardown).
  void dropUsers() {
    ReplaceableMetadataImpl::resolveAllUses(/* ResolveUsers */ false);
  }

protected:
  ValueAsMetadata(unsigned ID, Value *V)
      : Metadata(ID, Uniqued), ReplaceableMetadataImpl(V->getContext()), V(V) {
    assert(V && "Expected valid value");
  }

  ~ValueAsMetadata() = default;

public:
  static ValueAsMetadata *get(Value *V);

  static ConstantAsMetadata *getConstant(Value *C) {
    return cast<ConstantAsMetadata>(get(C));
  }

  static LocalAsMetadata *getLocal(Value *Local) {
    return cast<LocalAsMetadata>(get(Local));
  }

  static ValueAsMetadata *getIfExists(Value *V);

  static ConstantAsMetadata *getConstantIfExists(Value *C) {
    return cast_or_null<ConstantAsMetadata>(getIfExists(C));
  }

  static LocalAsMetadata *getLocalIfExists(Value *Local) {
    return cast_or_null<LocalAsMetadata>(getIfExists(Local));
  }

  Value *getValue() const { return V; }
  Type *getType() const { return V->getType(); }
  LLVMContext &getContext() const { return V->getContext(); }

  static void handleDeletion(Value *V);
  static void handleRAUW(Value *From, Value *To);

protected:
  /// Handle collisions after \a Value::replaceAllUsesWith().
  ///
  /// RAUW isn't supported directly for \a ValueAsMetadata, but if the wrapped
  /// \a Value gets RAUW'ed and the target already exists, this is used to
  /// merge the two metadata nodes.
  void replaceAllUsesWith(Metadata *MD) {
    ReplaceableMetadataImpl::replaceAllUsesWith(MD);
  }

public:
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == LocalAsMetadataKind ||
           MD->getMetadataID() == ConstantAsMetadataKind;
  }
};

class ConstantAsMetadata : public ValueAsMetadata {
  friend class ValueAsMetadata;

  ConstantAsMetadata(Constant *C)
      : ValueAsMetadata(ConstantAsMetadataKind, C) {}

public:
  static ConstantAsMetadata *get(Constant *C) {
    return ValueAsMetadata::getConstant(C);
  }

  static ConstantAsMetadata *getIfExists(Constant *C) {
    return ValueAsMetadata::getConstantIfExists(C);
  }

  Constant *getValue() const {
    return cast<Constant>(ValueAsMetadata::getValue());
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == ConstantAsMetadataKind;
  }
};

class LocalAsMetadata : public ValueAsMetadata {
  friend class ValueAsMetadata;

  LocalAsMetadata(Value *Local)
      : ValueAsMetadata(LocalAsMetadataKind, Local) {
    assert(!isa<Constant>(Local) && "Expected local value");
  }

public:
  static LocalAsMetadata *get(Value *Local) {
    return ValueAsMetadata::getLocal(Local);
  }

  static LocalAsMetadata *getIfExists(Value *Local) {
    return ValueAsMetadata::getLocalIfExists(Local);
  }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == LocalAsMetadataKind;
  }
};

/// Transitional API for extracting constants from Metadata.
///
/// This namespace contains transitional functions for metadata that points to
/// \a Constants.
///
/// In prehistory -- when metadata was a subclass of \a Value -- \a MDNode
/// operands could refer to any \a Value.  There's was a lot of code like this:
///
/// \code
///     MDNode *N = ...;
///     auto *CI = dyn_cast<ConstantInt>(N->getOperand(2));
/// \endcode
///
/// Now that \a Value and \a Metadata are in separate hierarchies, maintaining
/// the semantics for \a isa(), \a cast(), \a dyn_cast() (etc.) requires three
/// steps: cast in the \a Metadata hierarchy, extraction of the \a Value, and
/// cast in the \a Value hierarchy.  Besides creating boiler-plate, this
/// requires subtle control flow changes.
///
/// The end-goal is to create a new type of metadata, called (e.g.) \a MDInt,
/// so that metadata can refer to numbers without traversing a bridge to the \a
/// Value hierarchy.  In this final state, the code above would look like this:
///
/// \code
///     MDNode *N = ...;
///     auto *MI = dyn_cast<MDInt>(N->getOperand(2));
/// \endcode
///
/// The API in this namespace supports the transition.  \a MDInt doesn't exist
/// yet, and even once it does, changing each metadata schema to use it is its
/// own mini-project.  In the meantime this API prevents us from introducing
/// complex and bug-prone control flow that will disappear in the end.  In
/// particular, the above code looks like this:
///
/// \code
///     MDNode *N = ...;
///     auto *CI = mdconst::dyn_extract<ConstantInt>(N->getOperand(2));
/// \endcode
///
/// The full set of provided functions includes:
///
///   mdconst::hasa                <=> isa
///   mdconst::extract             <=> cast
///   mdconst::extract_or_null     <=> cast_or_null
///   mdconst::dyn_extract         <=> dyn_cast
///   mdconst::dyn_extract_or_null <=> dyn_cast_or_null
///
/// The target of the cast must be a subclass of \a Constant.
namespace mdconst {

namespace detail {

template <class T> T &make();
template <class T, class Result> struct HasDereference {
  using Yes = char[1];
  using No = char[2];
  template <size_t N> struct SFINAE {};

  template <class U, class V>
  static Yes &hasDereference(SFINAE<sizeof(static_cast<V>(*make<U>()))> * = 0);
  template <class U, class V> static No &hasDereference(...);

  static const bool value =
      sizeof(hasDereference<T, Result>(nullptr)) == sizeof(Yes);
};
template <class V, class M> struct IsValidPointer {
  static const bool value = std::is_base_of<Constant, V>::value &&
                            HasDereference<M, const Metadata &>::value;
};
template <class V, class M> struct IsValidReference {
  static const bool value = std::is_base_of<Constant, V>::value &&
                            std::is_convertible<M, const Metadata &>::value;
};

} // end namespace detail

/// Check whether Metadata has a Value.
///
/// As an analogue to \a isa(), check whether \c MD has an \a Value inside of
/// type \c X.
template <class X, class Y>
inline typename std::enable_if<detail::IsValidPointer<X, Y>::value, bool>::type
hasa(Y &&MD) {
  assert(MD && "Null pointer sent into hasa");
  if (auto *V = dyn_cast<ConstantAsMetadata>(MD))
    return isa<X>(V->getValue());
  return false;
}
template <class X, class Y>
inline
    typename std::enable_if<detail::IsValidReference<X, Y &>::value, bool>::type
    hasa(Y &MD) {
  return hasa(&MD);
}

/// Extract a Value from Metadata.
///
/// As an analogue to \a cast(), extract the \a Value subclass \c X from \c MD.
template <class X, class Y>
inline typename std::enable_if<detail::IsValidPointer<X, Y>::value, X *>::type
extract(Y &&MD) {
  return cast<X>(cast<ConstantAsMetadata>(MD)->getValue());
}
template <class X, class Y>
inline
    typename std::enable_if<detail::IsValidReference<X, Y &>::value, X *>::type
    extract(Y &MD) {
  return extract(&MD);
}

/// Extract a Value from Metadata, allowing null.
///
/// As an analogue to \a cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, allowing \c MD to be null.
template <class X, class Y>
inline typename std::enable_if<detail::IsValidPointer<X, Y>::value, X *>::type
extract_or_null(Y &&MD) {
  if (auto *V = cast_or_null<ConstantAsMetadata>(MD))
    return cast<X>(V->getValue());
  return nullptr;
}

/// Extract a Value from Metadata, if any.
///
/// As an analogue to \a dyn_cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, return null if \c MD doesn't contain a \a Value or if the \a
/// Value it does contain is of the wrong subclass.
template <class X, class Y>
inline typename std::enable_if<detail::IsValidPointer<X, Y>::value, X *>::type
dyn_extract(Y &&MD) {
  if (auto *V = dyn_cast<ConstantAsMetadata>(MD))
    return dyn_cast<X>(V->getValue());
  return nullptr;
}

/// Extract a Value from Metadata, if any, allowing null.
///
/// As an analogue to \a dyn_cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, return null if \c MD doesn't contain a \a Value or if the \a
/// Value it does contain is of the wrong subclass, allowing \c MD to be null.
template <class X, class Y>
inline typename std::enable_if<detail::IsValidPointer<X, Y>::value, X *>::type
dyn_extract_or_null(Y &&MD) {
  if (auto *V = dyn_cast_or_null<ConstantAsMetadata>(MD))
    return dyn_cast<X>(V->getValue());
  return nullptr;
}

} // end namespace mdconst

//===----------------------------------------------------------------------===//
/// A single uniqued string.
///
/// These are used to efficiently contain a byte sequence for metadata.
/// MDString is always unnamed.
class MDString : public Metadata {
  friend class StringMapEntry<MDString>;

  StringMapEntry<MDString> *Entry = nullptr;

  MDString() : Metadata(MDStringKind, Uniqued) {}

public:
  MDString(const MDString &) = delete;
  MDString &operator=(MDString &&) = delete;
  MDString &operator=(const MDString &) = delete;

  static MDString *get(LLVMContext &Context, StringRef Str);
  static MDString *get(LLVMContext &Context, const char *Str) {
    return get(Context, Str ? StringRef(Str) : StringRef());
  }

  StringRef getString() const;

  unsigned getLength() const { return (unsigned)getString().size(); }

  using iterator = StringRef::iterator;

  /// Pointer to the first byte of the string.
  iterator begin() const { return getString().begin(); }

  /// Pointer to one byte past the end of the string.
  iterator end() const { return getString().end(); }

  const unsigned char *bytes_begin() const { return getString().bytes_begin(); }
  const unsigned char *bytes_end() const { return getString().bytes_end(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == MDStringKind;
  }
};

/// A collection of metadata nodes that might be associated with a
/// memory access used by the alias-analysis infrastructure.
struct AAMDNodes {
  explicit AAMDNodes(MDNode *T = nullptr, MDNode *S = nullptr,
                     MDNode *N = nullptr)
      : TBAA(T), Scope(S), NoAlias(N) {}

  bool operator==(const AAMDNodes &A) const {
    return TBAA == A.TBAA && Scope == A.Scope && NoAlias == A.NoAlias;
  }

  bool operator!=(const AAMDNodes &A) const { return !(*this == A); }

  explicit operator bool() const { return TBAA || Scope || NoAlias; }

  /// The tag for type-based alias analysis.
  MDNode *TBAA;

  /// The tag for alias scope specification (used with noalias).
  MDNode *Scope;

  /// The tag specifying the noalias scope.
  MDNode *NoAlias;

  /// Given two sets of AAMDNodes that apply to the same pointer,
  /// give the best AAMDNodes that are compatible with both (i.e. a set of
  /// nodes whose allowable aliasing conclusions are a subset of those
  /// allowable by both of the inputs). However, for efficiency
  /// reasons, do not create any new MDNodes.
  AAMDNodes intersect(const AAMDNodes &Other) {
    AAMDNodes Result;
    Result.TBAA = Other.TBAA == TBAA ? TBAA : nullptr;
    Result.Scope = Other.Scope == Scope ? Scope : nullptr;
    Result.NoAlias = Other.NoAlias == NoAlias ? NoAlias : nullptr;
    return Result;
  }
};

// Specialize DenseMapInfo for AAMDNodes.
template<>
struct DenseMapInfo<AAMDNodes> {
  static inline AAMDNodes getEmptyKey() {
    return AAMDNodes(DenseMapInfo<MDNode *>::getEmptyKey(),
                     nullptr, nullptr);
  }

  static inline AAMDNodes getTombstoneKey() {
    return AAMDNodes(DenseMapInfo<MDNode *>::getTombstoneKey(),
                     nullptr, nullptr);
  }

  static unsigned getHashValue(const AAMDNodes &Val) {
    return DenseMapInfo<MDNode *>::getHashValue(Val.TBAA) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.Scope) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.NoAlias);
  }

  static bool isEqual(const AAMDNodes &LHS, const AAMDNodes &RHS) {
    return LHS == RHS;
  }
};

/// Tracking metadata reference owned by Metadata.
///
/// Similar to \a TrackingMDRef, but it's expected to be owned by an instance
/// of \a Metadata, which has the option of registering itself for callbacks to
/// re-unique itself.
///
/// In particular, this is used by \a MDNode.
class MDOperand {
  Metadata *MD = nullptr;

public:
  MDOperand() = default;
  MDOperand(MDOperand &&) = delete;
  MDOperand(const MDOperand &) = delete;
  MDOperand &operator=(MDOperand &&) = delete;
  MDOperand &operator=(const MDOperand &) = delete;
  ~MDOperand() { untrack(); }

  Metadata *get() const { return MD; }
  operator Metadata *() const { return get(); }
  Metadata *operator->() const { return get(); }
  Metadata &operator*() const { return *get(); }

  void reset() {
    untrack();
    MD = nullptr;
  }
  void reset(Metadata *MD, Metadata *Owner) {
    untrack();
    this->MD = MD;
    track(Owner);
  }

private:
  void track(Metadata *Owner) {
    if (MD) {
      if (Owner)
        MetadataTracking::track(this, *MD, *Owner);
      else
        MetadataTracking::track(MD);
    }
  }

  void untrack() {
    assert(static_cast<void *>(this) == &MD && "Expected same address");
    if (MD)
      MetadataTracking::untrack(MD);
  }
};

template <> struct simplify_type<MDOperand> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(MDOperand &MD) { return MD.get(); }
};

template <> struct simplify_type<const MDOperand> {
  using SimpleType = Metadata *;

  static SimpleType getSimplifiedValue(const MDOperand &MD) { return MD.get(); }
};

/// Pointer to the context, with optional RAUW support.
///
/// Either a raw (non-null) pointer to the \a LLVMContext, or an owned pointer
/// to \a ReplaceableMetadataImpl (which has a reference to \a LLVMContext).
class ContextAndReplaceableUses {
  PointerUnion<LLVMContext *, ReplaceableMetadataImpl *> Ptr;

public:
  ContextAndReplaceableUses(LLVMContext &Context) : Ptr(&Context) {}
  ContextAndReplaceableUses(
      std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses)
      : Ptr(ReplaceableUses.release()) {
    assert(getReplaceableUses() && "Expected non-null replaceable uses");
  }
  ContextAndReplaceableUses() = delete;
  ContextAndReplaceableUses(ContextAndReplaceableUses &&) = delete;
  ContextAndReplaceableUses(const ContextAndReplaceableUses &) = delete;
  ContextAndReplaceableUses &operator=(ContextAndReplaceableUses &&) = delete;
  ContextAndReplaceableUses &
  operator=(const ContextAndReplaceableUses &) = delete;
  ~ContextAndReplaceableUses() { delete getReplaceableUses(); }

  operator LLVMContext &() { return getContext(); }

  /// Whether this contains RAUW support.
  bool hasReplaceableUses() const {
    return Ptr.is<ReplaceableMetadataImpl *>();
  }

  LLVMContext &getContext() const {
    if (hasReplaceableUses())
      return getReplaceableUses()->getContext();
    return *Ptr.get<LLVMContext *>();
  }

  ReplaceableMetadataImpl *getReplaceableUses() const {
    if (hasReplaceableUses())
      return Ptr.get<ReplaceableMetadataImpl *>();
    return nullptr;
  }

  /// Ensure that this has RAUW support, and then return it.
  ReplaceableMetadataImpl *getOrCreateReplaceableUses() {
    if (!hasReplaceableUses())
      makeReplaceable(llvm::make_unique<ReplaceableMetadataImpl>(getContext()));
    return getReplaceableUses();
  }

  /// Assign RAUW support to this.
  ///
  /// Make this replaceable, taking ownership of \c ReplaceableUses (which must
  /// not be null).
  void
  makeReplaceable(std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses) {
    assert(ReplaceableUses && "Expected non-null replaceable uses");
    assert(&ReplaceableUses->getContext() == &getContext() &&
           "Expected same context");
    delete getReplaceableUses();
    Ptr = ReplaceableUses.release();
  }

  /// Drop RAUW support.
  ///
  /// Cede ownership of RAUW support, returning it.
  std::unique_ptr<ReplaceableMetadataImpl> takeReplaceableUses() {
    assert(hasReplaceableUses() && "Expected to own replaceable uses");
    std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses(
        getReplaceableUses());
    Ptr = &ReplaceableUses->getContext();
    return ReplaceableUses;
  }
};

struct TempMDNodeDeleter {
  inline void operator()(MDNode *Node) const;
};

#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  using Temp##CLASS = std::unique_ptr<CLASS, TempMDNodeDeleter>;
#define HANDLE_MDNODE_BRANCH(CLASS) HANDLE_MDNODE_LEAF(CLASS)
#include "llvm/IR/Metadata.def"

/// Metadata node.
///
/// Metadata nodes can be uniqued, like constants, or distinct.  Temporary
/// metadata nodes (with full support for RAUW) can be used to delay uniquing
/// until forward references are known.  The basic metadata node is an \a
/// MDTuple.
///
/// There is limited support for RAUW at construction time.  At construction
/// time, if any operand is a temporary node (or an unresolved uniqued node,
/// which indicates a transitive temporary operand), the node itself will be
/// unresolved.  As soon as all operands become resolved, it will drop RAUW
/// support permanently.
///
/// If an unresolved node is part of a cycle, \a resolveCycles() needs
/// to be called on some member of the cycle once all temporary nodes have been
/// replaced.
class MDNode : public Metadata {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  unsigned NumOperands;
  unsigned NumUnresolved;

  ContextAndReplaceableUses Context;

protected:
  MDNode(LLVMContext &Context, unsigned ID, StorageType Storage,
         ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = None);
  ~MDNode() = default;

  void *operator new(size_t Size, unsigned NumOps);
  void operator delete(void *Mem);

  /// Required by std, but never called.
  void operator delete(void *, unsigned) {
    llvm_unreachable("Constructor throws?");
  }

  /// Required by std, but never called.
  void operator delete(void *, unsigned, bool) {
    llvm_unreachable("Constructor throws?");
  }

  void dropAllReferences();

  MDOperand *mutable_begin() { return mutable_end() - NumOperands; }
  MDOperand *mutable_end() { return reinterpret_cast<MDOperand *>(this); }

  using mutable_op_range = iterator_range<MDOperand *>;

  mutable_op_range mutable_operands() {
    return mutable_op_range(mutable_begin(), mutable_end());
  }

public:
  MDNode(const MDNode &) = delete;
  void operator=(const MDNode &) = delete;
  void *operator new(size_t) = delete;

  static inline MDTuple *get(LLVMContext &Context, ArrayRef<Metadata *> MDs);
  static inline MDTuple *getIfExists(LLVMContext &Context,
                                     ArrayRef<Metadata *> MDs);
  static inline MDTuple *getDistinct(LLVMContext &Context,
                                     ArrayRef<Metadata *> MDs);
  static inline TempMDTuple getTemporary(LLVMContext &Context,
                                         ArrayRef<Metadata *> MDs);

  /// Create a (temporary) clone of this.
  TempMDNode clone() const;

  /// Deallocate a node created by getTemporary.
  ///
  /// Calls \c replaceAllUsesWith(nullptr) before deleting, so any remaining
  /// references will be reset.
  static void deleteTemporary(MDNode *N);

  LLVMContext &getContext() const { return Context.getContext(); }

  /// Replace a specific operand.
  void replaceOperandWith(unsigned I, Metadata *New);

  /// Check if node is fully resolved.
  ///
  /// If \a isTemporary(), this always returns \c false; if \a isDistinct(),
  /// this always returns \c true.
  ///
  /// If \a isUniqued(), returns \c true if this has already dropped RAUW
  /// support (because all operands are resolved).
  ///
  /// As forward declarations are resolved, their containers should get
  /// resolved automatically.  However, if this (or one of its operands) is
  /// involved in a cycle, \a resolveCycles() needs to be called explicitly.
  bool isResolved() const { return !isTemporary() && !NumUnresolved; }

  bool isUniqued() const { return Storage == Uniqued; }
  bool isDistinct() const { return Storage == Distinct; }
  bool isTemporary() const { return Storage == Temporary; }

  /// RAUW a temporary.
  ///
  /// \pre \a isTemporary() must be \c true.
  void replaceAllUsesWith(Metadata *MD) {
    assert(isTemporary() && "Expected temporary node");
    if (Context.hasReplaceableUses())
      Context.getReplaceableUses()->replaceAllUsesWith(MD);
  }

  /// Resolve cycles.
  ///
  /// Once all forward declarations have been resolved, force cycles to be
  /// resolved.
  ///
  /// \pre No operands (or operands' operands, etc.) have \a isTemporary().
  void resolveCycles();

  /// Resolve a unique, unresolved node.
  void resolve();

  /// Replace a temporary node with a permanent one.
  ///
  /// Try to create a uniqued version of \c N -- in place, if possible -- and
  /// return it.  If \c N cannot be uniqued, return a distinct node instead.
  template <class T>
  static typename std::enable_if<std::is_base_of<MDNode, T>::value, T *>::type
  replaceWithPermanent(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithPermanentImpl());
  }

  /// Replace a temporary node with a uniqued one.
  ///
  /// Create a uniqued version of \c N -- in place, if possible -- and return
  /// it.  Takes ownership of the temporary node.
  ///
  /// \pre N does not self-reference.
  template <class T>
  static typename std::enable_if<std::is_base_of<MDNode, T>::value, T *>::type
  replaceWithUniqued(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithUniquedImpl());
  }

  /// Replace a temporary node with a distinct one.
  ///
  /// Create a distinct version of \c N -- in place, if possible -- and return
  /// it.  Takes ownership of the temporary node.
  template <class T>
  static typename std::enable_if<std::is_base_of<MDNode, T>::value, T *>::type
  replaceWithDistinct(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithDistinctImpl());
  }

private:
  MDNode *replaceWithPermanentImpl();
  MDNode *replaceWithUniquedImpl();
  MDNode *replaceWithDistinctImpl();

protected:
  /// Set an operand.
  ///
  /// Sets the operand directly, without worrying about uniquing.
  void setOperand(unsigned I, Metadata *New);

  void storeDistinctInContext();
  template <class T, class StoreT>
  static T *storeImpl(T *N, StorageType Storage, StoreT &Store);
  template <class T> static T *storeImpl(T *N, StorageType Storage);

private:
  void handleChangedOperand(void *Ref, Metadata *New);

  /// Drop RAUW support, if any.
  void dropReplaceableUses();

  void resolveAfterOperandChange(Metadata *Old, Metadata *New);
  void decrementUnresolvedOperandCount();
  void countUnresolvedOperands();

  /// Mutate this to be "uniqued".
  ///
  /// Mutate this so that \a isUniqued().
  /// \pre \a isTemporary().
  /// \pre already added to uniquing set.
  void makeUniqued();

  /// Mutate this to be "distinct".
  ///
  /// Mutate this so that \a isDistinct().
  /// \pre \a isTemporary().
  void makeDistinct();

  void deleteAsSubclass();
  MDNode *uniquify();
  void eraseFromStore();

  template <class NodeTy> struct HasCachedHash;
  template <class NodeTy>
  static void dispatchRecalculateHash(NodeTy *N, std::true_type) {
    N->recalculateHash();
  }
  template <class NodeTy>
  static void dispatchRecalculateHash(NodeTy *, std::false_type) {}
  template <class NodeTy>
  static void dispatchResetHash(NodeTy *N, std::true_type) {
    N->setHash(0);
  }
  template <class NodeTy>
  static void dispatchResetHash(NodeTy *, std::false_type) {}

public:
  using op_iterator = const MDOperand *;
  using op_range = iterator_range<op_iterator>;

  op_iterator op_begin() const {
    return const_cast<MDNode *>(this)->mutable_begin();
  }

  op_iterator op_end() const {
    return const_cast<MDNode *>(this)->mutable_end();
  }

  op_range operands() const { return op_range(op_begin(), op_end()); }

  const MDOperand &getOperand(unsigned I) const {
    assert(I < NumOperands && "Out of range");
    return op_begin()[I];
  }

  /// Return number of MDNode operands.
  unsigned getNumOperands() const { return NumOperands; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case CLASS##Kind:                                                            \
    return true;
#include "llvm/IR/Metadata.def"
    }
  }

  /// Check whether MDNode is a vtable access.
  bool isTBAAVtableAccess() const;

  /// Methods for metadata merging.
  static MDNode *concatenate(MDNode *A, MDNode *B);
  static MDNode *intersect(MDNode *A, MDNode *B);
  static MDNode *getMostGenericTBAA(MDNode *A, MDNode *B);
  static MDNode *getMostGenericFPMath(MDNode *A, MDNode *B);
  static MDNode *getMostGenericRange(MDNode *A, MDNode *B);
  static MDNode *getMostGenericAliasScope(MDNode *A, MDNode *B);
  static MDNode *getMostGenericAlignmentOrDereferenceable(MDNode *A, MDNode *B);
};

/// Tuple of metadata.
///
/// This is the simple \a MDNode arbitrary tuple.  Nodes are uniqued by
/// default based on their operands.
class MDTuple : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  MDTuple(LLVMContext &C, StorageType Storage, unsigned Hash,
          ArrayRef<Metadata *> Vals)
      : MDNode(C, MDTupleKind, Storage, Vals) {
    setHash(Hash);
  }

  ~MDTuple() { dropAllReferences(); }

  void setHash(unsigned Hash) { SubclassData32 = Hash; }
  void recalculateHash();

  static MDTuple *getImpl(LLVMContext &Context, ArrayRef<Metadata *> MDs,
                          StorageType Storage, bool ShouldCreate = true);

  TempMDTuple cloneImpl() const {
    return getTemporary(getContext(),
                        SmallVector<Metadata *, 4>(op_begin(), op_end()));
  }

public:
  /// Get the hash, if any.
  unsigned getHash() const { return SubclassData32; }

  static MDTuple *get(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Uniqued);
  }

  static MDTuple *getIfExists(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Uniqued, /* ShouldCreate */ false);
  }

  /// Return a distinct node.
  ///
  /// Return a distinct node -- i.e., a node that is not uniqued.
  static MDTuple *getDistinct(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Distinct);
  }

  /// Return a temporary node.
  ///
  /// For use in constructing cyclic MDNode structures. A temporary MDNode is
  /// not uniqued, may be RAUW'd, and must be manually deleted with
  /// deleteTemporary.
  static TempMDTuple getTemporary(LLVMContext &Context,
                                  ArrayRef<Metadata *> MDs) {
    return TempMDTuple(getImpl(Context, MDs, Temporary));
  }

  /// Return a (temporary) clone of this.
  TempMDTuple clone() const { return cloneImpl(); }

  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == MDTupleKind;
  }
};

MDTuple *MDNode::get(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::get(Context, MDs);
}

MDTuple *MDNode::getIfExists(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::getIfExists(Context, MDs);
}

MDTuple *MDNode::getDistinct(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::getDistinct(Context, MDs);
}

TempMDTuple MDNode::getTemporary(LLVMContext &Context,
                                 ArrayRef<Metadata *> MDs) {
  return MDTuple::getTemporary(Context, MDs);
}

void TempMDNodeDeleter::operator()(MDNode *Node) const {
  MDNode::deleteTemporary(Node);
}

/// Typed iterator through MDNode operands.
///
/// An iterator that transforms an \a MDNode::iterator into an iterator over a
/// particular Metadata subclass.
template <class T>
class TypedMDOperandIterator
    : public std::iterator<std::input_iterator_tag, T *, std::ptrdiff_t, void,
                           T *> {
  MDNode::op_iterator I = nullptr;

public:
  TypedMDOperandIterator() = default;
  explicit TypedMDOperandIterator(MDNode::op_iterator I) : I(I) {}

  T *operator*() const { return cast_or_null<T>(*I); }

  TypedMDOperandIterator &operator++() {
    ++I;
    return *this;
  }

  TypedMDOperandIterator operator++(int) {
    TypedMDOperandIterator Temp(*this);
    ++I;
    return Temp;
  }

  bool operator==(const TypedMDOperandIterator &X) const { return I == X.I; }
  bool operator!=(const TypedMDOperandIterator &X) const { return I != X.I; }
};

/// Typed, array-like tuple of metadata.
///
/// This is a wrapper for \a MDTuple that makes it act like an array holding a
/// particular type of metadata.
template <class T> class MDTupleTypedArrayWrapper {
  const MDTuple *N = nullptr;

public:
  MDTupleTypedArrayWrapper() = default;
  MDTupleTypedArrayWrapper(const MDTuple *N) : N(N) {}

  template <class U>
  MDTupleTypedArrayWrapper(
      const MDTupleTypedArrayWrapper<U> &Other,
      typename std::enable_if<std::is_convertible<U *, T *>::value>::type * =
          nullptr)
      : N(Other.get()) {}

  template <class U>
  explicit MDTupleTypedArrayWrapper(
      const MDTupleTypedArrayWrapper<U> &Other,
      typename std::enable_if<!std::is_convertible<U *, T *>::value>::type * =
          nullptr)
      : N(Other.get()) {}

  explicit operator bool() const { return get(); }
  explicit operator MDTuple *() const { return get(); }

  MDTuple *get() const { return const_cast<MDTuple *>(N); }
  MDTuple *operator->() const { return get(); }
  MDTuple &operator*() const { return *get(); }

  // FIXME: Fix callers and remove condition on N.
  unsigned size() const { return N ? N->getNumOperands() : 0u; }
  bool empty() const { return N ? N->getNumOperands() == 0 : true; }
  T *operator[](unsigned I) const { return cast_or_null<T>(N->getOperand(I)); }

  // FIXME: Fix callers and remove condition on N.
  using iterator = TypedMDOperandIterator<T>;

  iterator begin() const { return N ? iterator(N->op_begin()) : iterator(); }
  iterator end() const { return N ? iterator(N->op_end()) : iterator(); }
};

#define HANDLE_METADATA(CLASS)                                                 \
  using CLASS##Array = MDTupleTypedArrayWrapper<CLASS>;
#include "llvm/IR/Metadata.def"

/// Placeholder metadata for operands of distinct MDNodes.
///
/// This is a lightweight placeholder for an operand of a distinct node.  It's
/// purpose is to help track forward references when creating a distinct node.
/// This allows distinct nodes involved in a cycle to be constructed before
/// their operands without requiring a heavyweight temporary node with
/// full-blown RAUW support.
///
/// Each placeholder supports only a single MDNode user.  Clients should pass
/// an ID, retrieved via \a getID(), to indicate the "real" operand that this
/// should be replaced with.
///
/// While it would be possible to implement move operators, they would be
/// fairly expensive.  Leave them unimplemented to discourage their use
/// (clients can use std::deque, std::list, BumpPtrAllocator, etc.).
class DistinctMDOperandPlaceholder : public Metadata {
  friend class MetadataTracking;

  Metadata **Use = nullptr;

public:
  explicit DistinctMDOperandPlaceholder(unsigned ID)
      : Metadata(DistinctMDOperandPlaceholderKind, Distinct) {
    SubclassData32 = ID;
  }

  DistinctMDOperandPlaceholder() = delete;
  DistinctMDOperandPlaceholder(DistinctMDOperandPlaceholder &&) = delete;
  DistinctMDOperandPlaceholder(const DistinctMDOperandPlaceholder &) = delete;

  ~DistinctMDOperandPlaceholder() {
    if (Use)
      *Use = nullptr;
  }

  unsigned getID() const { return SubclassData32; }

  /// Replace the use of this with MD.
  void replaceUseWith(Metadata *MD) {
    if (!Use)
      return;
    *Use = MD;

    if (*Use)
      MetadataTracking::track(*Use);

    Metadata *T = cast<Metadata>(this);
    MetadataTracking::untrack(T);
    assert(!Use && "Use is still being tracked despite being untracked!");
  }
};

//===----------------------------------------------------------------------===//
/// A tuple of MDNodes.
///
/// Despite its name, a NamedMDNode isn't itself an MDNode.
///
/// NamedMDNodes are named module-level entities that contain lists of MDNodes.
///
/// It is illegal for a NamedMDNode to appear as an operand of an MDNode.
class NamedMDNode : public ilist_node<NamedMDNode> {
  friend class LLVMContextImpl;
  friend class Module;

  std::string Name;
  Module *Parent = nullptr;
  void *Operands; // SmallVector<TrackingMDRef, 4>

  void setParent(Module *M) { Parent = M; }

  explicit NamedMDNode(const Twine &N);

  template<class T1, class T2>
  class op_iterator_impl :
      public std::iterator<std::bidirectional_iterator_tag, T2> {
    friend class NamedMDNode;

    const NamedMDNode *Node = nullptr;
    unsigned Idx = 0;

    op_iterator_impl(const NamedMDNode *N, unsigned i) : Node(N), Idx(i) {}

  public:
    op_iterator_impl() = default;

    bool operator==(const op_iterator_impl &o) const { return Idx == o.Idx; }
    bool operator!=(const op_iterator_impl &o) const { return Idx != o.Idx; }

    op_iterator_impl &operator++() {
      ++Idx;
      return *this;
    }

    op_iterator_impl operator++(int) {
      op_iterator_impl tmp(*this);
      operator++();
      return tmp;
    }

    op_iterator_impl &operator--() {
      --Idx;
      return *this;
    }

    op_iterator_impl operator--(int) {
      op_iterator_impl tmp(*this);
      operator--();
      return tmp;
    }

    T1 operator*() const { return Node->getOperand(Idx); }
  };

public:
  NamedMDNode(const NamedMDNode &) = delete;
  ~NamedMDNode();

  /// Drop all references and remove the node from parent module.
  void eraseFromParent();

  /// Remove all uses and clear node vector.
  void dropAllReferences() { clearOperands(); }
  /// Drop all references to this node's operands.
  void clearOperands();

  /// Get the module that holds this named metadata collection.
  inline Module *getParent() { return Parent; }
  inline const Module *getParent() const { return Parent; }

  MDNode *getOperand(unsigned i) const;
  unsigned getNumOperands() const;
  void addOperand(MDNode *M);
  void setOperand(unsigned I, MDNode *New);
  StringRef getName() const;
  void print(raw_ostream &ROS, bool IsForDebug = false) const;
  void print(raw_ostream &ROS, ModuleSlotTracker &MST,
             bool IsForDebug = false) const;
  void dump() const;

  // ---------------------------------------------------------------------------
  // Operand Iterator interface...
  //
  using op_iterator = op_iterator_impl<MDNode *, MDNode>;

  op_iterator op_begin() { return op_iterator(this, 0); }
  op_iterator op_end()   { return op_iterator(this, getNumOperands()); }

  using const_op_iterator = op_iterator_impl<const MDNode *, MDNode>;

  const_op_iterator op_begin() const { return const_op_iterator(this, 0); }
  const_op_iterator op_end()   const { return const_op_iterator(this, getNumOperands()); }

  inline iterator_range<op_iterator>  operands() {
    return make_range(op_begin(), op_end());
  }
  inline iterator_range<const_op_iterator> operands() const {
    return make_range(op_begin(), op_end());
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_ISA_CONVERSION_FUNCTIONS(NamedMDNode, LLVMNamedMDNodeRef)

} // end namespace llvm

#endif // LLVM_IR_METADATA_H
