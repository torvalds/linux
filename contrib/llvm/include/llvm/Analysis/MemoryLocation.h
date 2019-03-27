//===- MemoryLocation.h - Memory location descriptions ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides utility analysis objects describing memory locations.
/// These are used both by the Alias Analysis infrastructure and more
/// specialized memory analysis layers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYLOCATION_H
#define LLVM_ANALYSIS_MEMORYLOCATION_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Optional.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"

namespace llvm {

class LoadInst;
class StoreInst;
class MemTransferInst;
class MemIntrinsic;
class AtomicMemTransferInst;
class AtomicMemIntrinsic;
class AnyMemTransferInst;
class AnyMemIntrinsic;
class TargetLibraryInfo;

// Represents the size of a MemoryLocation. Logically, it's an
// Optional<uint63_t> that also carries a bit to represent whether the integer
// it contains, N, is 'precise'. Precise, in this context, means that we know
// that the area of storage referenced by the given MemoryLocation must be
// precisely N bytes. An imprecise value is formed as the union of two or more
// precise values, and can conservatively represent all of the values unioned
// into it. Importantly, imprecise values are an *upper-bound* on the size of a
// MemoryLocation.
//
// Concretely, a precise MemoryLocation is (%p, 4) in
// store i32 0, i32* %p
//
// Since we know that %p must be at least 4 bytes large at this point.
// Otherwise, we have UB. An example of an imprecise MemoryLocation is (%p, 4)
// at the memcpy in
//
//   %n = select i1 %foo, i64 1, i64 4
//   call void @llvm.memcpy.p0i8.p0i8.i64(i8* %p, i8* %baz, i64 %n, i32 1,
//                                        i1 false)
//
// ...Since we'll copy *up to* 4 bytes into %p, but we can't guarantee that
// we'll ever actually do so.
//
// If asked to represent a pathologically large value, this will degrade to
// None.
class LocationSize {
  enum : uint64_t {
    Unknown = ~uint64_t(0),
    ImpreciseBit = uint64_t(1) << 63,
    MapEmpty = Unknown - 1,
    MapTombstone = Unknown - 2,

    // The maximum value we can represent without falling back to 'unknown'.
    MaxValue = (MapTombstone - 1) & ~ImpreciseBit,
  };

  uint64_t Value;

  // Hack to support implicit construction. This should disappear when the
  // public LocationSize ctor goes away.
  enum DirectConstruction { Direct };

  constexpr LocationSize(uint64_t Raw, DirectConstruction): Value(Raw) {}

  static_assert(Unknown & ImpreciseBit, "Unknown is imprecise by definition.");
public:
  // FIXME: Migrate all users to construct via either `precise` or `upperBound`,
  // to make it more obvious at the callsite the kind of size that they're
  // providing.
  //
  // Since the overwhelming majority of users of this provide precise values,
  // this assumes the provided value is precise.
  constexpr LocationSize(uint64_t Raw)
      : Value(Raw > MaxValue ? Unknown : Raw) {}

  static LocationSize precise(uint64_t Value) { return LocationSize(Value); }

  static LocationSize upperBound(uint64_t Value) {
    // You can't go lower than 0, so give a precise result.
    if (LLVM_UNLIKELY(Value == 0))
      return precise(0);
    if (LLVM_UNLIKELY(Value > MaxValue))
      return unknown();
    return LocationSize(Value | ImpreciseBit, Direct);
  }

  constexpr static LocationSize unknown() {
    return LocationSize(Unknown, Direct);
  }

  // Sentinel values, generally used for maps.
  constexpr static LocationSize mapTombstone() {
    return LocationSize(MapTombstone, Direct);
  }
  constexpr static LocationSize mapEmpty() {
    return LocationSize(MapEmpty, Direct);
  }

  // Returns a LocationSize that can correctly represent either `*this` or
  // `Other`.
  LocationSize unionWith(LocationSize Other) const {
    if (Other == *this)
      return *this;

    if (!hasValue() || !Other.hasValue())
      return unknown();

    return upperBound(std::max(getValue(), Other.getValue()));
  }

  bool hasValue() const { return Value != Unknown; }
  uint64_t getValue() const {
    assert(hasValue() && "Getting value from an unknown LocationSize!");
    return Value & ~ImpreciseBit;
  }

  // Returns whether or not this value is precise. Note that if a value is
  // precise, it's guaranteed to not be `unknown()`.
  bool isPrecise() const {
    return (Value & ImpreciseBit) == 0;
  }

  // Convenience method to check if this LocationSize's value is 0.
  bool isZero() const { return hasValue() && getValue() == 0; }

  bool operator==(const LocationSize &Other) const {
    return Value == Other.Value;
  }

  bool operator!=(const LocationSize &Other) const {
    return !(*this == Other);
  }

  // Ordering operators are not provided, since it's unclear if there's only one
  // reasonable way to compare:
  // - values that don't exist against values that do, and
  // - precise values to imprecise values

  void print(raw_ostream &OS) const;

  // Returns an opaque value that represents this LocationSize. Cannot be
  // reliably converted back into a LocationSize.
  uint64_t toRaw() const { return Value; }
};

inline raw_ostream &operator<<(raw_ostream &OS, LocationSize Size) {
  Size.print(OS);
  return OS;
}

/// Representation for a specific memory location.
///
/// This abstraction can be used to represent a specific location in memory.
/// The goal of the location is to represent enough information to describe
/// abstract aliasing, modification, and reference behaviors of whatever
/// value(s) are stored in memory at the particular location.
///
/// The primary user of this interface is LLVM's Alias Analysis, but other
/// memory analyses such as MemoryDependence can use it as well.
class MemoryLocation {
public:
  /// UnknownSize - This is a special value which can be used with the
  /// size arguments in alias queries to indicate that the caller does not
  /// know the sizes of the potential memory references.
  enum : uint64_t { UnknownSize = ~UINT64_C(0) };

  /// The address of the start of the location.
  const Value *Ptr;

  /// The maximum size of the location, in address-units, or
  /// UnknownSize if the size is not known.
  ///
  /// Note that an unknown size does not mean the pointer aliases the entire
  /// virtual address space, because there are restrictions on stepping out of
  /// one object and into another. See
  /// http://llvm.org/docs/LangRef.html#pointeraliasing
  LocationSize Size;

  /// The metadata nodes which describes the aliasing of the location (each
  /// member is null if that kind of information is unavailable).
  AAMDNodes AATags;

  /// Return a location with information about the memory reference by the given
  /// instruction.
  static MemoryLocation get(const LoadInst *LI);
  static MemoryLocation get(const StoreInst *SI);
  static MemoryLocation get(const VAArgInst *VI);
  static MemoryLocation get(const AtomicCmpXchgInst *CXI);
  static MemoryLocation get(const AtomicRMWInst *RMWI);
  static MemoryLocation get(const Instruction *Inst) {
    return *MemoryLocation::getOrNone(Inst);
  }
  static Optional<MemoryLocation> getOrNone(const Instruction *Inst) {
    switch (Inst->getOpcode()) {
    case Instruction::Load:
      return get(cast<LoadInst>(Inst));
    case Instruction::Store:
      return get(cast<StoreInst>(Inst));
    case Instruction::VAArg:
      return get(cast<VAArgInst>(Inst));
    case Instruction::AtomicCmpXchg:
      return get(cast<AtomicCmpXchgInst>(Inst));
    case Instruction::AtomicRMW:
      return get(cast<AtomicRMWInst>(Inst));
    default:
      return None;
    }
  }

  /// Return a location representing the source of a memory transfer.
  static MemoryLocation getForSource(const MemTransferInst *MTI);
  static MemoryLocation getForSource(const AtomicMemTransferInst *MTI);
  static MemoryLocation getForSource(const AnyMemTransferInst *MTI);

  /// Return a location representing the destination of a memory set or
  /// transfer.
  static MemoryLocation getForDest(const MemIntrinsic *MI);
  static MemoryLocation getForDest(const AtomicMemIntrinsic *MI);
  static MemoryLocation getForDest(const AnyMemIntrinsic *MI);

  /// Return a location representing a particular argument of a call.
  static MemoryLocation getForArgument(const CallBase *Call, unsigned ArgIdx,
                                       const TargetLibraryInfo *TLI);
  static MemoryLocation getForArgument(const CallBase *Call, unsigned ArgIdx,
                                       const TargetLibraryInfo &TLI) {
    return getForArgument(Call, ArgIdx, &TLI);
  }

  explicit MemoryLocation(const Value *Ptr = nullptr,
                          LocationSize Size = LocationSize::unknown(),
                          const AAMDNodes &AATags = AAMDNodes())
      : Ptr(Ptr), Size(Size), AATags(AATags) {}

  MemoryLocation getWithNewPtr(const Value *NewPtr) const {
    MemoryLocation Copy(*this);
    Copy.Ptr = NewPtr;
    return Copy;
  }

  MemoryLocation getWithNewSize(LocationSize NewSize) const {
    MemoryLocation Copy(*this);
    Copy.Size = NewSize;
    return Copy;
  }

  MemoryLocation getWithoutAATags() const {
    MemoryLocation Copy(*this);
    Copy.AATags = AAMDNodes();
    return Copy;
  }

  bool operator==(const MemoryLocation &Other) const {
    return Ptr == Other.Ptr && Size == Other.Size && AATags == Other.AATags;
  }
};

// Specialize DenseMapInfo.
template <> struct DenseMapInfo<LocationSize> {
  static inline LocationSize getEmptyKey() {
    return LocationSize::mapEmpty();
  }
  static inline LocationSize getTombstoneKey() {
    return LocationSize::mapTombstone();
  }
  static unsigned getHashValue(const LocationSize &Val) {
    return DenseMapInfo<uint64_t>::getHashValue(Val.toRaw());
  }
  static bool isEqual(const LocationSize &LHS, const LocationSize &RHS) {
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<MemoryLocation> {
  static inline MemoryLocation getEmptyKey() {
    return MemoryLocation(DenseMapInfo<const Value *>::getEmptyKey(),
                          DenseMapInfo<LocationSize>::getEmptyKey());
  }
  static inline MemoryLocation getTombstoneKey() {
    return MemoryLocation(DenseMapInfo<const Value *>::getTombstoneKey(),
                          DenseMapInfo<LocationSize>::getTombstoneKey());
  }
  static unsigned getHashValue(const MemoryLocation &Val) {
    return DenseMapInfo<const Value *>::getHashValue(Val.Ptr) ^
           DenseMapInfo<LocationSize>::getHashValue(Val.Size) ^
           DenseMapInfo<AAMDNodes>::getHashValue(Val.AATags);
  }
  static bool isEqual(const MemoryLocation &LHS, const MemoryLocation &RHS) {
    return LHS == RHS;
  }
};
}

#endif
