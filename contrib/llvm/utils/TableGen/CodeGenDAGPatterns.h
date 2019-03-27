//===- CodeGenDAGPatterns.h - Read DAG patterns from .td file ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the CodeGenDAGPatterns class, which is used to read and
// represent the patterns present in a .td file for instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENDAGPATTERNS_H
#define LLVM_UTILS_TABLEGEN_CODEGENDAGPATTERNS_H

#include "CodeGenHwModes.h"
#include "CodeGenIntrinsics.h"
#include "CodeGenTarget.h"
#include "SDNodeProperties.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <vector>

namespace llvm {

class Record;
class Init;
class ListInit;
class DagInit;
class SDNodeInfo;
class TreePattern;
class TreePatternNode;
class CodeGenDAGPatterns;
class ComplexPattern;

/// Shared pointer for TreePatternNode.
using TreePatternNodePtr = std::shared_ptr<TreePatternNode>;

/// This represents a set of MVTs. Since the underlying type for the MVT
/// is uint8_t, there are at most 256 values. To reduce the number of memory
/// allocations and deallocations, represent the set as a sequence of bits.
/// To reduce the allocations even further, make MachineValueTypeSet own
/// the storage and use std::array as the bit container.
struct MachineValueTypeSet {
  static_assert(std::is_same<std::underlying_type<MVT::SimpleValueType>::type,
                             uint8_t>::value,
                "Change uint8_t here to the SimpleValueType's type");
  static unsigned constexpr Capacity = std::numeric_limits<uint8_t>::max()+1;
  using WordType = uint64_t;
  static unsigned constexpr WordWidth = CHAR_BIT*sizeof(WordType);
  static unsigned constexpr NumWords = Capacity/WordWidth;
  static_assert(NumWords*WordWidth == Capacity,
                "Capacity should be a multiple of WordWidth");

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  MachineValueTypeSet() {
    clear();
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  unsigned size() const {
    unsigned Count = 0;
    for (WordType W : Words)
      Count += countPopulation(W);
    return Count;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  void clear() {
    std::memset(Words.data(), 0, NumWords*sizeof(WordType));
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool empty() const {
    for (WordType W : Words)
      if (W != 0)
        return false;
    return true;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  unsigned count(MVT T) const {
    return (Words[T.SimpleTy / WordWidth] >> (T.SimpleTy % WordWidth)) & 1;
  }
  std::pair<MachineValueTypeSet&,bool> insert(MVT T) {
    bool V = count(T.SimpleTy);
    Words[T.SimpleTy / WordWidth] |= WordType(1) << (T.SimpleTy % WordWidth);
    return {*this, V};
  }
  MachineValueTypeSet &insert(const MachineValueTypeSet &S) {
    for (unsigned i = 0; i != NumWords; ++i)
      Words[i] |= S.Words[i];
    return *this;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  void erase(MVT T) {
    Words[T.SimpleTy / WordWidth] &= ~(WordType(1) << (T.SimpleTy % WordWidth));
  }

  struct const_iterator {
    // Some implementations of the C++ library require these traits to be
    // defined.
    using iterator_category = std::forward_iterator_tag;
    using value_type = MVT;
    using difference_type = ptrdiff_t;
    using pointer = const MVT*;
    using reference = const MVT&;

    LLVM_ATTRIBUTE_ALWAYS_INLINE
    MVT operator*() const {
      assert(Pos != Capacity);
      return MVT::SimpleValueType(Pos);
    }
    LLVM_ATTRIBUTE_ALWAYS_INLINE
    const_iterator(const MachineValueTypeSet *S, bool End) : Set(S) {
      Pos = End ? Capacity : find_from_pos(0);
    }
    LLVM_ATTRIBUTE_ALWAYS_INLINE
    const_iterator &operator++() {
      assert(Pos != Capacity);
      Pos = find_from_pos(Pos+1);
      return *this;
    }

    LLVM_ATTRIBUTE_ALWAYS_INLINE
    bool operator==(const const_iterator &It) const {
      return Set == It.Set && Pos == It.Pos;
    }
    LLVM_ATTRIBUTE_ALWAYS_INLINE
    bool operator!=(const const_iterator &It) const {
      return !operator==(It);
    }

  private:
    unsigned find_from_pos(unsigned P) const {
      unsigned SkipWords = P / WordWidth;
      unsigned SkipBits = P % WordWidth;
      unsigned Count = SkipWords * WordWidth;

      // If P is in the middle of a word, process it manually here, because
      // the trailing bits need to be masked off to use findFirstSet.
      if (SkipBits != 0) {
        WordType W = Set->Words[SkipWords];
        W &= maskLeadingOnes<WordType>(WordWidth-SkipBits);
        if (W != 0)
          return Count + findFirstSet(W);
        Count += WordWidth;
        SkipWords++;
      }

      for (unsigned i = SkipWords; i != NumWords; ++i) {
        WordType W = Set->Words[i];
        if (W != 0)
          return Count + findFirstSet(W);
        Count += WordWidth;
      }
      return Capacity;
    }

    const MachineValueTypeSet *Set;
    unsigned Pos;
  };

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const_iterator begin() const { return const_iterator(this, false); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const_iterator end()   const { return const_iterator(this, true); }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool operator==(const MachineValueTypeSet &S) const {
    return Words == S.Words;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool operator!=(const MachineValueTypeSet &S) const {
    return !operator==(S);
  }

private:
  friend struct const_iterator;
  std::array<WordType,NumWords> Words;
};

struct TypeSetByHwMode : public InfoByHwMode<MachineValueTypeSet> {
  using SetType = MachineValueTypeSet;

  TypeSetByHwMode() = default;
  TypeSetByHwMode(const TypeSetByHwMode &VTS) = default;
  TypeSetByHwMode(MVT::SimpleValueType VT)
    : TypeSetByHwMode(ValueTypeByHwMode(VT)) {}
  TypeSetByHwMode(ValueTypeByHwMode VT)
    : TypeSetByHwMode(ArrayRef<ValueTypeByHwMode>(&VT, 1)) {}
  TypeSetByHwMode(ArrayRef<ValueTypeByHwMode> VTList);

  SetType &getOrCreate(unsigned Mode) {
    if (hasMode(Mode))
      return get(Mode);
    return Map.insert({Mode,SetType()}).first->second;
  }

  bool isValueTypeByHwMode(bool AllowEmpty) const;
  ValueTypeByHwMode getValueTypeByHwMode() const;

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool isMachineValueType() const {
    return isDefaultOnly() && Map.begin()->second.size() == 1;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  MVT getMachineValueType() const {
    assert(isMachineValueType());
    return *Map.begin()->second.begin();
  }

  bool isPossible() const;

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool isDefaultOnly() const {
    return Map.size() == 1 && Map.begin()->first == DefaultMode;
  }

  bool insert(const ValueTypeByHwMode &VVT);
  bool constrain(const TypeSetByHwMode &VTS);
  template <typename Predicate> bool constrain(Predicate P);
  template <typename Predicate>
  bool assign_if(const TypeSetByHwMode &VTS, Predicate P);

  void writeToStream(raw_ostream &OS) const;
  static void writeToStream(const SetType &S, raw_ostream &OS);

  bool operator==(const TypeSetByHwMode &VTS) const;
  bool operator!=(const TypeSetByHwMode &VTS) const { return !(*this == VTS); }

  void dump() const;
  bool validate() const;

private:
  /// Intersect two sets. Return true if anything has changed.
  bool intersect(SetType &Out, const SetType &In);
};

raw_ostream &operator<<(raw_ostream &OS, const TypeSetByHwMode &T);

struct TypeInfer {
  TypeInfer(TreePattern &T) : TP(T), ForceMode(0) {}

  bool isConcrete(const TypeSetByHwMode &VTS, bool AllowEmpty) const {
    return VTS.isValueTypeByHwMode(AllowEmpty);
  }
  ValueTypeByHwMode getConcrete(const TypeSetByHwMode &VTS,
                                bool AllowEmpty) const {
    assert(VTS.isValueTypeByHwMode(AllowEmpty));
    return VTS.getValueTypeByHwMode();
  }

  /// The protocol in the following functions (Merge*, force*, Enforce*,
  /// expand*) is to return "true" if a change has been made, "false"
  /// otherwise.

  bool MergeInTypeInfo(TypeSetByHwMode &Out, const TypeSetByHwMode &In);
  bool MergeInTypeInfo(TypeSetByHwMode &Out, MVT::SimpleValueType InVT) {
    return MergeInTypeInfo(Out, TypeSetByHwMode(InVT));
  }
  bool MergeInTypeInfo(TypeSetByHwMode &Out, ValueTypeByHwMode InVT) {
    return MergeInTypeInfo(Out, TypeSetByHwMode(InVT));
  }

  /// Reduce the set \p Out to have at most one element for each mode.
  bool forceArbitrary(TypeSetByHwMode &Out);

  /// The following four functions ensure that upon return the set \p Out
  /// will only contain types of the specified kind: integer, floating-point,
  /// scalar, or vector.
  /// If \p Out is empty, all legal types of the specified kind will be added
  /// to it. Otherwise, all types that are not of the specified kind will be
  /// removed from \p Out.
  bool EnforceInteger(TypeSetByHwMode &Out);
  bool EnforceFloatingPoint(TypeSetByHwMode &Out);
  bool EnforceScalar(TypeSetByHwMode &Out);
  bool EnforceVector(TypeSetByHwMode &Out);

  /// If \p Out is empty, fill it with all legal types. Otherwise, leave it
  /// unchanged.
  bool EnforceAny(TypeSetByHwMode &Out);
  /// Make sure that for each type in \p Small, there exists a larger type
  /// in \p Big.
  bool EnforceSmallerThan(TypeSetByHwMode &Small, TypeSetByHwMode &Big);
  /// 1. Ensure that for each type T in \p Vec, T is a vector type, and that
  ///    for each type U in \p Elem, U is a scalar type.
  /// 2. Ensure that for each (scalar) type U in \p Elem, there exists a
  ///    (vector) type T in \p Vec, such that U is the element type of T.
  bool EnforceVectorEltTypeIs(TypeSetByHwMode &Vec, TypeSetByHwMode &Elem);
  bool EnforceVectorEltTypeIs(TypeSetByHwMode &Vec,
                              const ValueTypeByHwMode &VVT);
  /// Ensure that for each type T in \p Sub, T is a vector type, and there
  /// exists a type U in \p Vec such that U is a vector type with the same
  /// element type as T and at least as many elements as T.
  bool EnforceVectorSubVectorTypeIs(TypeSetByHwMode &Vec,
                                    TypeSetByHwMode &Sub);
  /// 1. Ensure that \p V has a scalar type iff \p W has a scalar type.
  /// 2. Ensure that for each vector type T in \p V, there exists a vector
  ///    type U in \p W, such that T and U have the same number of elements.
  /// 3. Ensure that for each vector type U in \p W, there exists a vector
  ///    type T in \p V, such that T and U have the same number of elements
  ///    (reverse of 2).
  bool EnforceSameNumElts(TypeSetByHwMode &V, TypeSetByHwMode &W);
  /// 1. Ensure that for each type T in \p A, there exists a type U in \p B,
  ///    such that T and U have equal size in bits.
  /// 2. Ensure that for each type U in \p B, there exists a type T in \p A
  ///    such that T and U have equal size in bits (reverse of 1).
  bool EnforceSameSize(TypeSetByHwMode &A, TypeSetByHwMode &B);

  /// For each overloaded type (i.e. of form *Any), replace it with the
  /// corresponding subset of legal, specific types.
  void expandOverloads(TypeSetByHwMode &VTS);
  void expandOverloads(TypeSetByHwMode::SetType &Out,
                       const TypeSetByHwMode::SetType &Legal);

  struct ValidateOnExit {
    ValidateOnExit(TypeSetByHwMode &T, TypeInfer &TI) : Infer(TI), VTS(T) {}
  #ifndef NDEBUG
    ~ValidateOnExit();
  #else
    ~ValidateOnExit() {}  // Empty destructor with NDEBUG.
  #endif
    TypeInfer &Infer;
    TypeSetByHwMode &VTS;
  };

  struct SuppressValidation {
    SuppressValidation(TypeInfer &TI) : Infer(TI), SavedValidate(TI.Validate) {
      Infer.Validate = false;
    }
    ~SuppressValidation() {
      Infer.Validate = SavedValidate;
    }
    TypeInfer &Infer;
    bool SavedValidate;
  };

  TreePattern &TP;
  unsigned ForceMode;     // Mode to use when set.
  bool CodeGen = false;   // Set during generation of matcher code.
  bool Validate = true;   // Indicate whether to validate types.

private:
  const TypeSetByHwMode &getLegalTypes();

  /// Cached legal types (in default mode).
  bool LegalTypesCached = false;
  TypeSetByHwMode LegalCache;
};

/// Set type used to track multiply used variables in patterns
typedef StringSet<> MultipleUseVarSet;

/// SDTypeConstraint - This is a discriminated union of constraints,
/// corresponding to the SDTypeConstraint tablegen class in Target.td.
struct SDTypeConstraint {
  SDTypeConstraint(Record *R, const CodeGenHwModes &CGH);

  unsigned OperandNo;   // The operand # this constraint applies to.
  enum {
    SDTCisVT, SDTCisPtrTy, SDTCisInt, SDTCisFP, SDTCisVec, SDTCisSameAs,
    SDTCisVTSmallerThanOp, SDTCisOpSmallerThanOp, SDTCisEltOfVec,
    SDTCisSubVecOfVec, SDTCVecEltisVT, SDTCisSameNumEltsAs, SDTCisSameSizeAs
  } ConstraintType;

  union {   // The discriminated union.
    struct {
      unsigned OtherOperandNum;
    } SDTCisSameAs_Info;
    struct {
      unsigned OtherOperandNum;
    } SDTCisVTSmallerThanOp_Info;
    struct {
      unsigned BigOperandNum;
    } SDTCisOpSmallerThanOp_Info;
    struct {
      unsigned OtherOperandNum;
    } SDTCisEltOfVec_Info;
    struct {
      unsigned OtherOperandNum;
    } SDTCisSubVecOfVec_Info;
    struct {
      unsigned OtherOperandNum;
    } SDTCisSameNumEltsAs_Info;
    struct {
      unsigned OtherOperandNum;
    } SDTCisSameSizeAs_Info;
  } x;

  // The VT for SDTCisVT and SDTCVecEltisVT.
  // Must not be in the union because it has a non-trivial destructor.
  ValueTypeByHwMode VVT;

  /// ApplyTypeConstraint - Given a node in a pattern, apply this type
  /// constraint to the nodes operands.  This returns true if it makes a
  /// change, false otherwise.  If a type contradiction is found, an error
  /// is flagged.
  bool ApplyTypeConstraint(TreePatternNode *N, const SDNodeInfo &NodeInfo,
                           TreePattern &TP) const;
};

/// ScopedName - A name of a node associated with a "scope" that indicates
/// the context (e.g. instance of Pattern or PatFrag) in which the name was
/// used. This enables substitution of pattern fragments while keeping track
/// of what name(s) were originally given to various nodes in the tree.
class ScopedName {
  unsigned Scope;
  std::string Identifier;
public:
  ScopedName(unsigned Scope, StringRef Identifier)
    : Scope(Scope), Identifier(Identifier) {
    assert(Scope != 0 &&
           "Scope == 0 is used to indicate predicates without arguments");
  }

  unsigned getScope() const { return Scope; }
  const std::string &getIdentifier() const { return Identifier; }

  std::string getFullName() const;

  bool operator==(const ScopedName &o) const;
  bool operator!=(const ScopedName &o) const;
};

/// SDNodeInfo - One of these records is created for each SDNode instance in
/// the target .td file.  This represents the various dag nodes we will be
/// processing.
class SDNodeInfo {
  Record *Def;
  StringRef EnumName;
  StringRef SDClassName;
  unsigned Properties;
  unsigned NumResults;
  int NumOperands;
  std::vector<SDTypeConstraint> TypeConstraints;
public:
  // Parse the specified record.
  SDNodeInfo(Record *R, const CodeGenHwModes &CGH);

  unsigned getNumResults() const { return NumResults; }

  /// getNumOperands - This is the number of operands required or -1 if
  /// variadic.
  int getNumOperands() const { return NumOperands; }
  Record *getRecord() const { return Def; }
  StringRef getEnumName() const { return EnumName; }
  StringRef getSDClassName() const { return SDClassName; }

  const std::vector<SDTypeConstraint> &getTypeConstraints() const {
    return TypeConstraints;
  }

  /// getKnownType - If the type constraints on this node imply a fixed type
  /// (e.g. all stores return void, etc), then return it as an
  /// MVT::SimpleValueType.  Otherwise, return MVT::Other.
  MVT::SimpleValueType getKnownType(unsigned ResNo) const;

  /// hasProperty - Return true if this node has the specified property.
  ///
  bool hasProperty(enum SDNP Prop) const { return Properties & (1 << Prop); }

  /// ApplyTypeConstraints - Given a node in a pattern, apply the type
  /// constraints for this node to the operands of the node.  This returns
  /// true if it makes a change, false otherwise.  If a type contradiction is
  /// found, an error is flagged.
  bool ApplyTypeConstraints(TreePatternNode *N, TreePattern &TP) const;
};

/// TreePredicateFn - This is an abstraction that represents the predicates on
/// a PatFrag node.  This is a simple one-word wrapper around a pointer to
/// provide nice accessors.
class TreePredicateFn {
  /// PatFragRec - This is the TreePattern for the PatFrag that we
  /// originally came from.
  TreePattern *PatFragRec;
public:
  /// TreePredicateFn constructor.  Here 'N' is a subclass of PatFrag.
  TreePredicateFn(TreePattern *N);


  TreePattern *getOrigPatFragRecord() const { return PatFragRec; }

  /// isAlwaysTrue - Return true if this is a noop predicate.
  bool isAlwaysTrue() const;

  bool isImmediatePattern() const { return hasImmCode(); }

  /// getImmediatePredicateCode - Return the code that evaluates this pattern if
  /// this is an immediate predicate.  It is an error to call this on a
  /// non-immediate pattern.
  std::string getImmediatePredicateCode() const {
    std::string Result = getImmCode();
    assert(!Result.empty() && "Isn't an immediate pattern!");
    return Result;
  }

  bool operator==(const TreePredicateFn &RHS) const {
    return PatFragRec == RHS.PatFragRec;
  }

  bool operator!=(const TreePredicateFn &RHS) const { return !(*this == RHS); }

  /// Return the name to use in the generated code to reference this, this is
  /// "Predicate_foo" if from a pattern fragment "foo".
  std::string getFnName() const;

  /// getCodeToRunOnSDNode - Return the code for the function body that
  /// evaluates this predicate.  The argument is expected to be in "Node",
  /// not N.  This handles casting and conversion to a concrete node type as
  /// appropriate.
  std::string getCodeToRunOnSDNode() const;

  /// Get the data type of the argument to getImmediatePredicateCode().
  StringRef getImmType() const;

  /// Get a string that describes the type returned by getImmType() but is
  /// usable as part of an identifier.
  StringRef getImmTypeIdentifier() const;

  // Predicate code uses the PatFrag's captured operands.
  bool usesOperands() const;

  // Is the desired predefined predicate for a load?
  bool isLoad() const;
  // Is the desired predefined predicate for a store?
  bool isStore() const;
  // Is the desired predefined predicate for an atomic?
  bool isAtomic() const;

  /// Is this predicate the predefined unindexed load predicate?
  /// Is this predicate the predefined unindexed store predicate?
  bool isUnindexed() const;
  /// Is this predicate the predefined non-extending load predicate?
  bool isNonExtLoad() const;
  /// Is this predicate the predefined any-extend load predicate?
  bool isAnyExtLoad() const;
  /// Is this predicate the predefined sign-extend load predicate?
  bool isSignExtLoad() const;
  /// Is this predicate the predefined zero-extend load predicate?
  bool isZeroExtLoad() const;
  /// Is this predicate the predefined non-truncating store predicate?
  bool isNonTruncStore() const;
  /// Is this predicate the predefined truncating store predicate?
  bool isTruncStore() const;

  /// Is this predicate the predefined monotonic atomic predicate?
  bool isAtomicOrderingMonotonic() const;
  /// Is this predicate the predefined acquire atomic predicate?
  bool isAtomicOrderingAcquire() const;
  /// Is this predicate the predefined release atomic predicate?
  bool isAtomicOrderingRelease() const;
  /// Is this predicate the predefined acquire-release atomic predicate?
  bool isAtomicOrderingAcquireRelease() const;
  /// Is this predicate the predefined sequentially consistent atomic predicate?
  bool isAtomicOrderingSequentiallyConsistent() const;

  /// Is this predicate the predefined acquire-or-stronger atomic predicate?
  bool isAtomicOrderingAcquireOrStronger() const;
  /// Is this predicate the predefined weaker-than-acquire atomic predicate?
  bool isAtomicOrderingWeakerThanAcquire() const;

  /// Is this predicate the predefined release-or-stronger atomic predicate?
  bool isAtomicOrderingReleaseOrStronger() const;
  /// Is this predicate the predefined weaker-than-release atomic predicate?
  bool isAtomicOrderingWeakerThanRelease() const;

  /// If non-null, indicates that this predicate is a predefined memory VT
  /// predicate for a load/store and returns the ValueType record for the memory VT.
  Record *getMemoryVT() const;
  /// If non-null, indicates that this predicate is a predefined memory VT
  /// predicate (checking only the scalar type) for load/store and returns the
  /// ValueType record for the memory VT.
  Record *getScalarMemoryVT() const;

  // If true, indicates that GlobalISel-based C++ code was supplied.
  bool hasGISelPredicateCode() const;
  std::string getGISelPredicateCode() const;

private:
  bool hasPredCode() const;
  bool hasImmCode() const;
  std::string getPredCode() const;
  std::string getImmCode() const;
  bool immCodeUsesAPInt() const;
  bool immCodeUsesAPFloat() const;

  bool isPredefinedPredicateEqualTo(StringRef Field, bool Value) const;
};

struct TreePredicateCall {
  TreePredicateFn Fn;

  // Scope -- unique identifier for retrieving named arguments. 0 is used when
  // the predicate does not use named arguments.
  unsigned Scope;

  TreePredicateCall(const TreePredicateFn &Fn, unsigned Scope)
    : Fn(Fn), Scope(Scope) {}

  bool operator==(const TreePredicateCall &o) const {
    return Fn == o.Fn && Scope == o.Scope;
  }
  bool operator!=(const TreePredicateCall &o) const {
    return !(*this == o);
  }
};

class TreePatternNode {
  /// The type of each node result.  Before and during type inference, each
  /// result may be a set of possible types.  After (successful) type inference,
  /// each is a single concrete type.
  std::vector<TypeSetByHwMode> Types;

  /// The index of each result in results of the pattern.
  std::vector<unsigned> ResultPerm;

  /// Operator - The Record for the operator if this is an interior node (not
  /// a leaf).
  Record *Operator;

  /// Val - The init value (e.g. the "GPRC" record, or "7") for a leaf.
  ///
  Init *Val;

  /// Name - The name given to this node with the :$foo notation.
  ///
  std::string Name;

  std::vector<ScopedName> NamesAsPredicateArg;

  /// PredicateCalls - The predicate functions to execute on this node to check
  /// for a match.  If this list is empty, no predicate is involved.
  std::vector<TreePredicateCall> PredicateCalls;

  /// TransformFn - The transformation function to execute on this node before
  /// it can be substituted into the resulting instruction on a pattern match.
  Record *TransformFn;

  std::vector<TreePatternNodePtr> Children;

public:
  TreePatternNode(Record *Op, std::vector<TreePatternNodePtr> Ch,
                  unsigned NumResults)
      : Operator(Op), Val(nullptr), TransformFn(nullptr),
        Children(std::move(Ch)) {
    Types.resize(NumResults);
    ResultPerm.resize(NumResults);
    std::iota(ResultPerm.begin(), ResultPerm.end(), 0);
  }
  TreePatternNode(Init *val, unsigned NumResults)    // leaf ctor
    : Operator(nullptr), Val(val), TransformFn(nullptr) {
    Types.resize(NumResults);
    ResultPerm.resize(NumResults);
    std::iota(ResultPerm.begin(), ResultPerm.end(), 0);
  }

  bool hasName() const { return !Name.empty(); }
  const std::string &getName() const { return Name; }
  void setName(StringRef N) { Name.assign(N.begin(), N.end()); }

  const std::vector<ScopedName> &getNamesAsPredicateArg() const {
    return NamesAsPredicateArg;
  }
  void setNamesAsPredicateArg(const std::vector<ScopedName>& Names) {
    NamesAsPredicateArg = Names;
  }
  void addNameAsPredicateArg(const ScopedName &N) {
    NamesAsPredicateArg.push_back(N);
  }

  bool isLeaf() const { return Val != nullptr; }

  // Type accessors.
  unsigned getNumTypes() const { return Types.size(); }
  ValueTypeByHwMode getType(unsigned ResNo) const {
    return Types[ResNo].getValueTypeByHwMode();
  }
  const std::vector<TypeSetByHwMode> &getExtTypes() const { return Types; }
  const TypeSetByHwMode &getExtType(unsigned ResNo) const {
    return Types[ResNo];
  }
  TypeSetByHwMode &getExtType(unsigned ResNo) { return Types[ResNo]; }
  void setType(unsigned ResNo, const TypeSetByHwMode &T) { Types[ResNo] = T; }
  MVT::SimpleValueType getSimpleType(unsigned ResNo) const {
    return Types[ResNo].getMachineValueType().SimpleTy;
  }

  bool hasConcreteType(unsigned ResNo) const {
    return Types[ResNo].isValueTypeByHwMode(false);
  }
  bool isTypeCompletelyUnknown(unsigned ResNo, TreePattern &TP) const {
    return Types[ResNo].empty();
  }

  unsigned getNumResults() const { return ResultPerm.size(); }
  unsigned getResultIndex(unsigned ResNo) const { return ResultPerm[ResNo]; }
  void setResultIndex(unsigned ResNo, unsigned RI) { ResultPerm[ResNo] = RI; }

  Init *getLeafValue() const { assert(isLeaf()); return Val; }
  Record *getOperator() const { assert(!isLeaf()); return Operator; }

  unsigned getNumChildren() const { return Children.size(); }
  TreePatternNode *getChild(unsigned N) const { return Children[N].get(); }
  const TreePatternNodePtr &getChildShared(unsigned N) const {
    return Children[N];
  }
  void setChild(unsigned i, TreePatternNodePtr N) { Children[i] = N; }

  /// hasChild - Return true if N is any of our children.
  bool hasChild(const TreePatternNode *N) const {
    for (unsigned i = 0, e = Children.size(); i != e; ++i)
      if (Children[i].get() == N)
        return true;
    return false;
  }

  bool hasProperTypeByHwMode() const;
  bool hasPossibleType() const;
  bool setDefaultMode(unsigned Mode);

  bool hasAnyPredicate() const { return !PredicateCalls.empty(); }

  const std::vector<TreePredicateCall> &getPredicateCalls() const {
    return PredicateCalls;
  }
  void clearPredicateCalls() { PredicateCalls.clear(); }
  void setPredicateCalls(const std::vector<TreePredicateCall> &Calls) {
    assert(PredicateCalls.empty() && "Overwriting non-empty predicate list!");
    PredicateCalls = Calls;
  }
  void addPredicateCall(const TreePredicateCall &Call) {
    assert(!Call.Fn.isAlwaysTrue() && "Empty predicate string!");
    assert(!is_contained(PredicateCalls, Call) && "predicate applied recursively");
    PredicateCalls.push_back(Call);
  }
  void addPredicateCall(const TreePredicateFn &Fn, unsigned Scope) {
    assert((Scope != 0) == Fn.usesOperands());
    addPredicateCall(TreePredicateCall(Fn, Scope));
  }

  Record *getTransformFn() const { return TransformFn; }
  void setTransformFn(Record *Fn) { TransformFn = Fn; }

  /// getIntrinsicInfo - If this node corresponds to an intrinsic, return the
  /// CodeGenIntrinsic information for it, otherwise return a null pointer.
  const CodeGenIntrinsic *getIntrinsicInfo(const CodeGenDAGPatterns &CDP) const;

  /// getComplexPatternInfo - If this node corresponds to a ComplexPattern,
  /// return the ComplexPattern information, otherwise return null.
  const ComplexPattern *
  getComplexPatternInfo(const CodeGenDAGPatterns &CGP) const;

  /// Returns the number of MachineInstr operands that would be produced by this
  /// node if it mapped directly to an output Instruction's
  /// operand. ComplexPattern specifies this explicitly; MIOperandInfo gives it
  /// for Operands; otherwise 1.
  unsigned getNumMIResults(const CodeGenDAGPatterns &CGP) const;

  /// NodeHasProperty - Return true if this node has the specified property.
  bool NodeHasProperty(SDNP Property, const CodeGenDAGPatterns &CGP) const;

  /// TreeHasProperty - Return true if any node in this tree has the specified
  /// property.
  bool TreeHasProperty(SDNP Property, const CodeGenDAGPatterns &CGP) const;

  /// isCommutativeIntrinsic - Return true if the node is an intrinsic which is
  /// marked isCommutative.
  bool isCommutativeIntrinsic(const CodeGenDAGPatterns &CDP) const;

  void print(raw_ostream &OS) const;
  void dump() const;

public:   // Higher level manipulation routines.

  /// clone - Return a new copy of this tree.
  ///
  TreePatternNodePtr clone() const;

  /// RemoveAllTypes - Recursively strip all the types of this tree.
  void RemoveAllTypes();

  /// isIsomorphicTo - Return true if this node is recursively isomorphic to
  /// the specified node.  For this comparison, all of the state of the node
  /// is considered, except for the assigned name.  Nodes with differing names
  /// that are otherwise identical are considered isomorphic.
  bool isIsomorphicTo(const TreePatternNode *N,
                      const MultipleUseVarSet &DepVars) const;

  /// SubstituteFormalArguments - Replace the formal arguments in this tree
  /// with actual values specified by ArgMap.
  void
  SubstituteFormalArguments(std::map<std::string, TreePatternNodePtr> &ArgMap);

  /// InlinePatternFragments - If this pattern refers to any pattern
  /// fragments, return the set of inlined versions (this can be more than
  /// one if a PatFrags record has multiple alternatives).
  void InlinePatternFragments(TreePatternNodePtr T,
                              TreePattern &TP,
                              std::vector<TreePatternNodePtr> &OutAlternatives);

  /// ApplyTypeConstraints - Apply all of the type constraints relevant to
  /// this node and its children in the tree.  This returns true if it makes a
  /// change, false otherwise.  If a type contradiction is found, flag an error.
  bool ApplyTypeConstraints(TreePattern &TP, bool NotRegisters);

  /// UpdateNodeType - Set the node type of N to VT if VT contains
  /// information.  If N already contains a conflicting type, then flag an
  /// error.  This returns true if any information was updated.
  ///
  bool UpdateNodeType(unsigned ResNo, const TypeSetByHwMode &InTy,
                      TreePattern &TP);
  bool UpdateNodeType(unsigned ResNo, MVT::SimpleValueType InTy,
                      TreePattern &TP);
  bool UpdateNodeType(unsigned ResNo, ValueTypeByHwMode InTy,
                      TreePattern &TP);

  // Update node type with types inferred from an instruction operand or result
  // def from the ins/outs lists.
  // Return true if the type changed.
  bool UpdateNodeTypeFromInst(unsigned ResNo, Record *Operand, TreePattern &TP);

  /// ContainsUnresolvedType - Return true if this tree contains any
  /// unresolved types.
  bool ContainsUnresolvedType(TreePattern &TP) const;

  /// canPatternMatch - If it is impossible for this pattern to match on this
  /// target, fill in Reason and return false.  Otherwise, return true.
  bool canPatternMatch(std::string &Reason, const CodeGenDAGPatterns &CDP);
};

inline raw_ostream &operator<<(raw_ostream &OS, const TreePatternNode &TPN) {
  TPN.print(OS);
  return OS;
}


/// TreePattern - Represent a pattern, used for instructions, pattern
/// fragments, etc.
///
class TreePattern {
  /// Trees - The list of pattern trees which corresponds to this pattern.
  /// Note that PatFrag's only have a single tree.
  ///
  std::vector<TreePatternNodePtr> Trees;

  /// NamedNodes - This is all of the nodes that have names in the trees in this
  /// pattern.
  StringMap<SmallVector<TreePatternNode *, 1>> NamedNodes;

  /// TheRecord - The actual TableGen record corresponding to this pattern.
  ///
  Record *TheRecord;

  /// Args - This is a list of all of the arguments to this pattern (for
  /// PatFrag patterns), which are the 'node' markers in this pattern.
  std::vector<std::string> Args;

  /// CDP - the top-level object coordinating this madness.
  ///
  CodeGenDAGPatterns &CDP;

  /// isInputPattern - True if this is an input pattern, something to match.
  /// False if this is an output pattern, something to emit.
  bool isInputPattern;

  /// hasError - True if the currently processed nodes have unresolvable types
  /// or other non-fatal errors
  bool HasError;

  /// It's important that the usage of operands in ComplexPatterns is
  /// consistent: each named operand can be defined by at most one
  /// ComplexPattern. This records the ComplexPattern instance and the operand
  /// number for each operand encountered in a ComplexPattern to aid in that
  /// check.
  StringMap<std::pair<Record *, unsigned>> ComplexPatternOperands;

  TypeInfer Infer;

public:

  /// TreePattern constructor - Parse the specified DagInits into the
  /// current record.
  TreePattern(Record *TheRec, ListInit *RawPat, bool isInput,
              CodeGenDAGPatterns &ise);
  TreePattern(Record *TheRec, DagInit *Pat, bool isInput,
              CodeGenDAGPatterns &ise);
  TreePattern(Record *TheRec, TreePatternNodePtr Pat, bool isInput,
              CodeGenDAGPatterns &ise);

  /// getTrees - Return the tree patterns which corresponds to this pattern.
  ///
  const std::vector<TreePatternNodePtr> &getTrees() const { return Trees; }
  unsigned getNumTrees() const { return Trees.size(); }
  const TreePatternNodePtr &getTree(unsigned i) const { return Trees[i]; }
  void setTree(unsigned i, TreePatternNodePtr Tree) { Trees[i] = Tree; }
  const TreePatternNodePtr &getOnlyTree() const {
    assert(Trees.size() == 1 && "Doesn't have exactly one pattern!");
    return Trees[0];
  }

  const StringMap<SmallVector<TreePatternNode *, 1>> &getNamedNodesMap() {
    if (NamedNodes.empty())
      ComputeNamedNodes();
    return NamedNodes;
  }

  /// getRecord - Return the actual TableGen record corresponding to this
  /// pattern.
  ///
  Record *getRecord() const { return TheRecord; }

  unsigned getNumArgs() const { return Args.size(); }
  const std::string &getArgName(unsigned i) const {
    assert(i < Args.size() && "Argument reference out of range!");
    return Args[i];
  }
  std::vector<std::string> &getArgList() { return Args; }

  CodeGenDAGPatterns &getDAGPatterns() const { return CDP; }

  /// InlinePatternFragments - If this pattern refers to any pattern
  /// fragments, inline them into place, giving us a pattern without any
  /// PatFrags references.  This may increase the number of trees in the
  /// pattern if a PatFrags has multiple alternatives.
  void InlinePatternFragments() {
    std::vector<TreePatternNodePtr> Copy = Trees;
    Trees.clear();
    for (unsigned i = 0, e = Copy.size(); i != e; ++i)
      Copy[i]->InlinePatternFragments(Copy[i], *this, Trees);
  }

  /// InferAllTypes - Infer/propagate as many types throughout the expression
  /// patterns as possible.  Return true if all types are inferred, false
  /// otherwise.  Bail out if a type contradiction is found.
  bool InferAllTypes(
      const StringMap<SmallVector<TreePatternNode *, 1>> *NamedTypes = nullptr);

  /// error - If this is the first error in the current resolution step,
  /// print it and set the error flag.  Otherwise, continue silently.
  void error(const Twine &Msg);
  bool hasError() const {
    return HasError;
  }
  void resetError() {
    HasError = false;
  }

  TypeInfer &getInfer() { return Infer; }

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  TreePatternNodePtr ParseTreePattern(Init *DI, StringRef OpName);
  void ComputeNamedNodes();
  void ComputeNamedNodes(TreePatternNode *N);
};


inline bool TreePatternNode::UpdateNodeType(unsigned ResNo,
                                            const TypeSetByHwMode &InTy,
                                            TreePattern &TP) {
  TypeSetByHwMode VTS(InTy);
  TP.getInfer().expandOverloads(VTS);
  return TP.getInfer().MergeInTypeInfo(Types[ResNo], VTS);
}

inline bool TreePatternNode::UpdateNodeType(unsigned ResNo,
                                            MVT::SimpleValueType InTy,
                                            TreePattern &TP) {
  TypeSetByHwMode VTS(InTy);
  TP.getInfer().expandOverloads(VTS);
  return TP.getInfer().MergeInTypeInfo(Types[ResNo], VTS);
}

inline bool TreePatternNode::UpdateNodeType(unsigned ResNo,
                                            ValueTypeByHwMode InTy,
                                            TreePattern &TP) {
  TypeSetByHwMode VTS(InTy);
  TP.getInfer().expandOverloads(VTS);
  return TP.getInfer().MergeInTypeInfo(Types[ResNo], VTS);
}


/// DAGDefaultOperand - One of these is created for each OperandWithDefaultOps
/// that has a set ExecuteAlways / DefaultOps field.
struct DAGDefaultOperand {
  std::vector<TreePatternNodePtr> DefaultOps;
};

class DAGInstruction {
  std::vector<Record*> Results;
  std::vector<Record*> Operands;
  std::vector<Record*> ImpResults;
  TreePatternNodePtr SrcPattern;
  TreePatternNodePtr ResultPattern;

public:
  DAGInstruction(const std::vector<Record*> &results,
                 const std::vector<Record*> &operands,
                 const std::vector<Record*> &impresults,
                 TreePatternNodePtr srcpattern = nullptr,
                 TreePatternNodePtr resultpattern = nullptr)
    : Results(results), Operands(operands), ImpResults(impresults),
      SrcPattern(srcpattern), ResultPattern(resultpattern) {}

  unsigned getNumResults() const { return Results.size(); }
  unsigned getNumOperands() const { return Operands.size(); }
  unsigned getNumImpResults() const { return ImpResults.size(); }
  const std::vector<Record*>& getImpResults() const { return ImpResults; }

  Record *getResult(unsigned RN) const {
    assert(RN < Results.size());
    return Results[RN];
  }

  Record *getOperand(unsigned ON) const {
    assert(ON < Operands.size());
    return Operands[ON];
  }

  Record *getImpResult(unsigned RN) const {
    assert(RN < ImpResults.size());
    return ImpResults[RN];
  }

  TreePatternNodePtr getSrcPattern() const { return SrcPattern; }
  TreePatternNodePtr getResultPattern() const { return ResultPattern; }
};

/// This class represents a condition that has to be satisfied for a pattern
/// to be tried. It is a generalization of a class "Pattern" from Target.td:
/// in addition to the Target.td's predicates, this class can also represent
/// conditions associated with HW modes. Both types will eventually become
/// strings containing C++ code to be executed, the difference is in how
/// these strings are generated.
class Predicate {
public:
  Predicate(Record *R, bool C = true) : Def(R), IfCond(C), IsHwMode(false) {
    assert(R->isSubClassOf("Predicate") &&
           "Predicate objects should only be created for records derived"
           "from Predicate class");
  }
  Predicate(StringRef FS, bool C = true) : Def(nullptr), Features(FS.str()),
    IfCond(C), IsHwMode(true) {}

  /// Return a string which contains the C++ condition code that will serve
  /// as a predicate during instruction selection.
  std::string getCondString() const {
    // The string will excute in a subclass of SelectionDAGISel.
    // Cast to std::string explicitly to avoid ambiguity with StringRef.
    std::string C = IsHwMode
        ? std::string("MF->getSubtarget().checkFeatures(\"" + Features + "\")")
        : std::string(Def->getValueAsString("CondString"));
    return IfCond ? C : "!("+C+')';
  }
  bool operator==(const Predicate &P) const {
    return IfCond == P.IfCond && IsHwMode == P.IsHwMode && Def == P.Def;
  }
  bool operator<(const Predicate &P) const {
    if (IsHwMode != P.IsHwMode)
      return IsHwMode < P.IsHwMode;
    assert(!Def == !P.Def && "Inconsistency between Def and IsHwMode");
    if (IfCond != P.IfCond)
      return IfCond < P.IfCond;
    if (Def)
      return LessRecord()(Def, P.Def);
    return Features < P.Features;
  }
  Record *Def;            ///< Predicate definition from .td file, null for
                          ///< HW modes.
  std::string Features;   ///< Feature string for HW mode.
  bool IfCond;            ///< The boolean value that the condition has to
                          ///< evaluate to for this predicate to be true.
  bool IsHwMode;          ///< Does this predicate correspond to a HW mode?
};

/// PatternToMatch - Used by CodeGenDAGPatterns to keep tab of patterns
/// processed to produce isel.
class PatternToMatch {
public:
  PatternToMatch(Record *srcrecord, std::vector<Predicate> preds,
                 TreePatternNodePtr src, TreePatternNodePtr dst,
                 std::vector<Record *> dstregs, int complexity,
                 unsigned uid, unsigned setmode = 0)
      : SrcRecord(srcrecord), SrcPattern(src), DstPattern(dst),
        Predicates(std::move(preds)), Dstregs(std::move(dstregs)),
        AddedComplexity(complexity), ID(uid), ForceMode(setmode) {}

  Record          *SrcRecord;   // Originating Record for the pattern.
  TreePatternNodePtr SrcPattern;      // Source pattern to match.
  TreePatternNodePtr DstPattern;      // Resulting pattern.
  std::vector<Predicate> Predicates;  // Top level predicate conditions
                                      // to match.
  std::vector<Record*> Dstregs; // Physical register defs being matched.
  int              AddedComplexity; // Add to matching pattern complexity.
  unsigned         ID;          // Unique ID for the record.
  unsigned         ForceMode;   // Force this mode in type inference when set.

  Record          *getSrcRecord()  const { return SrcRecord; }
  TreePatternNode *getSrcPattern() const { return SrcPattern.get(); }
  TreePatternNodePtr getSrcPatternShared() const { return SrcPattern; }
  TreePatternNode *getDstPattern() const { return DstPattern.get(); }
  TreePatternNodePtr getDstPatternShared() const { return DstPattern; }
  const std::vector<Record*> &getDstRegs() const { return Dstregs; }
  int         getAddedComplexity() const { return AddedComplexity; }
  const std::vector<Predicate> &getPredicates() const { return Predicates; }

  std::string getPredicateCheck() const;

  /// Compute the complexity metric for the input pattern.  This roughly
  /// corresponds to the number of nodes that are covered.
  int getPatternComplexity(const CodeGenDAGPatterns &CGP) const;
};

class CodeGenDAGPatterns {
  RecordKeeper &Records;
  CodeGenTarget Target;
  CodeGenIntrinsicTable Intrinsics;
  CodeGenIntrinsicTable TgtIntrinsics;

  std::map<Record*, SDNodeInfo, LessRecordByID> SDNodes;
  std::map<Record*, std::pair<Record*, std::string>, LessRecordByID>
      SDNodeXForms;
  std::map<Record*, ComplexPattern, LessRecordByID> ComplexPatterns;
  std::map<Record *, std::unique_ptr<TreePattern>, LessRecordByID>
      PatternFragments;
  std::map<Record*, DAGDefaultOperand, LessRecordByID> DefaultOperands;
  std::map<Record*, DAGInstruction, LessRecordByID> Instructions;

  // Specific SDNode definitions:
  Record *intrinsic_void_sdnode;
  Record *intrinsic_w_chain_sdnode, *intrinsic_wo_chain_sdnode;

  /// PatternsToMatch - All of the things we are matching on the DAG.  The first
  /// value is the pattern to match, the second pattern is the result to
  /// emit.
  std::vector<PatternToMatch> PatternsToMatch;

  TypeSetByHwMode LegalVTS;

  using PatternRewriterFn = std::function<void (TreePattern *)>;
  PatternRewriterFn PatternRewriter;

  unsigned NumScopes = 0;

public:
  CodeGenDAGPatterns(RecordKeeper &R,
                     PatternRewriterFn PatternRewriter = nullptr);

  CodeGenTarget &getTargetInfo() { return Target; }
  const CodeGenTarget &getTargetInfo() const { return Target; }
  const TypeSetByHwMode &getLegalTypes() const { return LegalVTS; }

  Record *getSDNodeNamed(const std::string &Name) const;

  const SDNodeInfo &getSDNodeInfo(Record *R) const {
    auto F = SDNodes.find(R);
    assert(F != SDNodes.end() && "Unknown node!");
    return F->second;
  }

  // Node transformation lookups.
  typedef std::pair<Record*, std::string> NodeXForm;
  const NodeXForm &getSDNodeTransform(Record *R) const {
    auto F = SDNodeXForms.find(R);
    assert(F != SDNodeXForms.end() && "Invalid transform!");
    return F->second;
  }

  typedef std::map<Record*, NodeXForm, LessRecordByID>::const_iterator
          nx_iterator;
  nx_iterator nx_begin() const { return SDNodeXForms.begin(); }
  nx_iterator nx_end() const { return SDNodeXForms.end(); }


  const ComplexPattern &getComplexPattern(Record *R) const {
    auto F = ComplexPatterns.find(R);
    assert(F != ComplexPatterns.end() && "Unknown addressing mode!");
    return F->second;
  }

  const CodeGenIntrinsic &getIntrinsic(Record *R) const {
    for (unsigned i = 0, e = Intrinsics.size(); i != e; ++i)
      if (Intrinsics[i].TheDef == R) return Intrinsics[i];
    for (unsigned i = 0, e = TgtIntrinsics.size(); i != e; ++i)
      if (TgtIntrinsics[i].TheDef == R) return TgtIntrinsics[i];
    llvm_unreachable("Unknown intrinsic!");
  }

  const CodeGenIntrinsic &getIntrinsicInfo(unsigned IID) const {
    if (IID-1 < Intrinsics.size())
      return Intrinsics[IID-1];
    if (IID-Intrinsics.size()-1 < TgtIntrinsics.size())
      return TgtIntrinsics[IID-Intrinsics.size()-1];
    llvm_unreachable("Bad intrinsic ID!");
  }

  unsigned getIntrinsicID(Record *R) const {
    for (unsigned i = 0, e = Intrinsics.size(); i != e; ++i)
      if (Intrinsics[i].TheDef == R) return i;
    for (unsigned i = 0, e = TgtIntrinsics.size(); i != e; ++i)
      if (TgtIntrinsics[i].TheDef == R) return i + Intrinsics.size();
    llvm_unreachable("Unknown intrinsic!");
  }

  const DAGDefaultOperand &getDefaultOperand(Record *R) const {
    auto F = DefaultOperands.find(R);
    assert(F != DefaultOperands.end() &&"Isn't an analyzed default operand!");
    return F->second;
  }

  // Pattern Fragment information.
  TreePattern *getPatternFragment(Record *R) const {
    auto F = PatternFragments.find(R);
    assert(F != PatternFragments.end() && "Invalid pattern fragment request!");
    return F->second.get();
  }
  TreePattern *getPatternFragmentIfRead(Record *R) const {
    auto F = PatternFragments.find(R);
    if (F == PatternFragments.end())
      return nullptr;
    return F->second.get();
  }

  typedef std::map<Record *, std::unique_ptr<TreePattern>,
                   LessRecordByID>::const_iterator pf_iterator;
  pf_iterator pf_begin() const { return PatternFragments.begin(); }
  pf_iterator pf_end() const { return PatternFragments.end(); }
  iterator_range<pf_iterator> ptfs() const { return PatternFragments; }

  // Patterns to match information.
  typedef std::vector<PatternToMatch>::const_iterator ptm_iterator;
  ptm_iterator ptm_begin() const { return PatternsToMatch.begin(); }
  ptm_iterator ptm_end() const { return PatternsToMatch.end(); }
  iterator_range<ptm_iterator> ptms() const { return PatternsToMatch; }

  /// Parse the Pattern for an instruction, and insert the result in DAGInsts.
  typedef std::map<Record*, DAGInstruction, LessRecordByID> DAGInstMap;
  void parseInstructionPattern(
      CodeGenInstruction &CGI, ListInit *Pattern,
      DAGInstMap &DAGInsts);

  const DAGInstruction &getInstruction(Record *R) const {
    auto F = Instructions.find(R);
    assert(F != Instructions.end() && "Unknown instruction!");
    return F->second;
  }

  Record *get_intrinsic_void_sdnode() const {
    return intrinsic_void_sdnode;
  }
  Record *get_intrinsic_w_chain_sdnode() const {
    return intrinsic_w_chain_sdnode;
  }
  Record *get_intrinsic_wo_chain_sdnode() const {
    return intrinsic_wo_chain_sdnode;
  }

  bool hasTargetIntrinsics() { return !TgtIntrinsics.empty(); }

  unsigned allocateScope() { return ++NumScopes; }

private:
  void ParseNodeInfo();
  void ParseNodeTransforms();
  void ParseComplexPatterns();
  void ParsePatternFragments(bool OutFrags = false);
  void ParseDefaultOperands();
  void ParseInstructions();
  void ParsePatterns();
  void ExpandHwModeBasedTypes();
  void InferInstructionFlags();
  void GenerateVariants();
  void VerifyInstructionFlags();

  std::vector<Predicate> makePredList(ListInit *L);

  void ParseOnePattern(Record *TheDef,
                       TreePattern &Pattern, TreePattern &Result,
                       const std::vector<Record *> &InstImpResults);
  void AddPatternToMatch(TreePattern *Pattern, PatternToMatch &&PTM);
  void FindPatternInputsAndOutputs(
      TreePattern &I, TreePatternNodePtr Pat,
      std::map<std::string, TreePatternNodePtr> &InstInputs,
      MapVector<std::string, TreePatternNodePtr,
                std::map<std::string, unsigned>> &InstResults,
      std::vector<Record *> &InstImpResults);
};


inline bool SDNodeInfo::ApplyTypeConstraints(TreePatternNode *N,
                                             TreePattern &TP) const {
    bool MadeChange = false;
    for (unsigned i = 0, e = TypeConstraints.size(); i != e; ++i)
      MadeChange |= TypeConstraints[i].ApplyTypeConstraint(N, *this, TP);
    return MadeChange;
  }

} // end namespace llvm

#endif
