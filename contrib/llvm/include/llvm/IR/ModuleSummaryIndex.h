//===- llvm/ModuleSummaryIndex.h - Module Summary Index ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// @file
/// ModuleSummaryIndex.h This file contains the declarations the classes that
///  hold the module index and summary for function importing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULESUMMARYINDEX_H
#define LLVM_IR_MODULESUMMARYINDEX_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/StringSaver.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

namespace yaml {

template <typename T> struct MappingTraits;

} // end namespace yaml

/// Class to accumulate and hold information about a callee.
struct CalleeInfo {
  enum class HotnessType : uint8_t {
    Unknown = 0,
    Cold = 1,
    None = 2,
    Hot = 3,
    Critical = 4
  };

  // The size of the bit-field might need to be adjusted if more values are
  // added to HotnessType enum.
  uint32_t Hotness : 3;

  /// The value stored in RelBlockFreq has to be interpreted as the digits of
  /// a scaled number with a scale of \p -ScaleShift.
  uint32_t RelBlockFreq : 29;
  static constexpr int32_t ScaleShift = 8;
  static constexpr uint64_t MaxRelBlockFreq = (1 << 29) - 1;

  CalleeInfo()
      : Hotness(static_cast<uint32_t>(HotnessType::Unknown)), RelBlockFreq(0) {}
  explicit CalleeInfo(HotnessType Hotness, uint64_t RelBF)
      : Hotness(static_cast<uint32_t>(Hotness)), RelBlockFreq(RelBF) {}

  void updateHotness(const HotnessType OtherHotness) {
    Hotness = std::max(Hotness, static_cast<uint32_t>(OtherHotness));
  }

  HotnessType getHotness() const { return HotnessType(Hotness); }

  /// Update \p RelBlockFreq from \p BlockFreq and \p EntryFreq
  ///
  /// BlockFreq is divided by EntryFreq and added to RelBlockFreq. To represent
  /// fractional values, the result is represented as a fixed point number with
  /// scale of -ScaleShift.
  void updateRelBlockFreq(uint64_t BlockFreq, uint64_t EntryFreq) {
    if (EntryFreq == 0)
      return;
    using Scaled64 = ScaledNumber<uint64_t>;
    Scaled64 Temp(BlockFreq, ScaleShift);
    Temp /= Scaled64::get(EntryFreq);

    uint64_t Sum =
        SaturatingAdd<uint64_t>(Temp.toInt<uint64_t>(), RelBlockFreq);
    Sum = std::min(Sum, uint64_t(MaxRelBlockFreq));
    RelBlockFreq = static_cast<uint32_t>(Sum);
  }
};

inline const char *getHotnessName(CalleeInfo::HotnessType HT) {
  switch (HT) {
  case CalleeInfo::HotnessType::Unknown:
    return "unknown";
  case CalleeInfo::HotnessType::Cold:
    return "cold";
  case CalleeInfo::HotnessType::None:
    return "none";
  case CalleeInfo::HotnessType::Hot:
    return "hot";
  case CalleeInfo::HotnessType::Critical:
    return "critical";
  }
  llvm_unreachable("invalid hotness");
}

class GlobalValueSummary;

using GlobalValueSummaryList = std::vector<std::unique_ptr<GlobalValueSummary>>;

struct GlobalValueSummaryInfo {
  union NameOrGV {
    NameOrGV(bool HaveGVs) {
      if (HaveGVs)
        GV = nullptr;
      else
        Name = "";
    }

    /// The GlobalValue corresponding to this summary. This is only used in
    /// per-module summaries and when the IR is available. E.g. when module
    /// analysis is being run, or when parsing both the IR and the summary
    /// from assembly.
    const GlobalValue *GV;

    /// Summary string representation. This StringRef points to BC module
    /// string table and is valid until module data is stored in memory.
    /// This is guaranteed to happen until runThinLTOBackend function is
    /// called, so it is safe to use this field during thin link. This field
    /// is only valid if summary index was loaded from BC file.
    StringRef Name;
  } U;

  GlobalValueSummaryInfo(bool HaveGVs) : U(HaveGVs) {}

  /// List of global value summary structures for a particular value held
  /// in the GlobalValueMap. Requires a vector in the case of multiple
  /// COMDAT values of the same name.
  GlobalValueSummaryList SummaryList;
};

/// Map from global value GUID to corresponding summary structures. Use a
/// std::map rather than a DenseMap so that pointers to the map's value_type
/// (which are used by ValueInfo) are not invalidated by insertion. Also it will
/// likely incur less overhead, as the value type is not very small and the size
/// of the map is unknown, resulting in inefficiencies due to repeated
/// insertions and resizing.
using GlobalValueSummaryMapTy =
    std::map<GlobalValue::GUID, GlobalValueSummaryInfo>;

/// Struct that holds a reference to a particular GUID in a global value
/// summary.
struct ValueInfo {
  PointerIntPair<const GlobalValueSummaryMapTy::value_type *, 2, int>
      RefAndFlags;

  ValueInfo() = default;
  ValueInfo(bool HaveGVs, const GlobalValueSummaryMapTy::value_type *R) {
    RefAndFlags.setPointer(R);
    RefAndFlags.setInt(HaveGVs);
  }

  operator bool() const { return getRef(); }

  GlobalValue::GUID getGUID() const { return getRef()->first; }
  const GlobalValue *getValue() const {
    assert(haveGVs());
    return getRef()->second.U.GV;
  }

  ArrayRef<std::unique_ptr<GlobalValueSummary>> getSummaryList() const {
    return getRef()->second.SummaryList;
  }

  StringRef name() const {
    return haveGVs() ? getRef()->second.U.GV->getName()
                     : getRef()->second.U.Name;
  }

  bool haveGVs() const { return RefAndFlags.getInt() & 0x1; }
  bool isReadOnly() const { return RefAndFlags.getInt() & 0x2; }
  void setReadOnly() { RefAndFlags.setInt(RefAndFlags.getInt() | 0x2); }

  const GlobalValueSummaryMapTy::value_type *getRef() const {
    return RefAndFlags.getPointer();
  }

  bool isDSOLocal() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const ValueInfo &VI) {
  OS << VI.getGUID();
  if (!VI.name().empty())
    OS << " (" << VI.name() << ")";
  return OS;
}

inline bool operator==(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref for comparison");
  return A.getRef() == B.getRef();
}

inline bool operator!=(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref for comparison");
  return A.getRef() != B.getRef();
}

inline bool operator<(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref to compare GUIDs");
  return A.getGUID() < B.getGUID();
}

template <> struct DenseMapInfo<ValueInfo> {
  static inline ValueInfo getEmptyKey() {
    return ValueInfo(false, (GlobalValueSummaryMapTy::value_type *)-8);
  }

  static inline ValueInfo getTombstoneKey() {
    return ValueInfo(false, (GlobalValueSummaryMapTy::value_type *)-16);
  }

  static inline bool isSpecialKey(ValueInfo V) {
    return V == getTombstoneKey() || V == getEmptyKey();
  }

  static bool isEqual(ValueInfo L, ValueInfo R) {
    // We are not supposed to mix ValueInfo(s) with different HaveGVs flag
    // in a same container.
    assert(isSpecialKey(L) || isSpecialKey(R) || (L.haveGVs() == R.haveGVs()));
    return L.getRef() == R.getRef();
  }
  static unsigned getHashValue(ValueInfo I) { return (uintptr_t)I.getRef(); }
};

/// Function and variable summary information to aid decisions and
/// implementation of importing.
class GlobalValueSummary {
public:
  /// Sububclass discriminator (for dyn_cast<> et al.)
  enum SummaryKind : unsigned { AliasKind, FunctionKind, GlobalVarKind };

  /// Group flags (Linkage, NotEligibleToImport, etc.) as a bitfield.
  struct GVFlags {
    /// The linkage type of the associated global value.
    ///
    /// One use is to flag values that have local linkage types and need to
    /// have module identifier appended before placing into the combined
    /// index, to disambiguate from other values with the same name.
    /// In the future this will be used to update and optimize linkage
    /// types based on global summary-based analysis.
    unsigned Linkage : 4;

    /// Indicate if the global value cannot be imported (e.g. it cannot
    /// be renamed or references something that can't be renamed).
    unsigned NotEligibleToImport : 1;

    /// In per-module summary, indicate that the global value must be considered
    /// a live root for index-based liveness analysis. Used for special LLVM
    /// values such as llvm.global_ctors that the linker does not know about.
    ///
    /// In combined summary, indicate that the global value is live.
    unsigned Live : 1;

    /// Indicates that the linker resolved the symbol to a definition from
    /// within the same linkage unit.
    unsigned DSOLocal : 1;

    /// Convenience Constructors
    explicit GVFlags(GlobalValue::LinkageTypes Linkage,
                     bool NotEligibleToImport, bool Live, bool IsLocal)
        : Linkage(Linkage), NotEligibleToImport(NotEligibleToImport),
          Live(Live), DSOLocal(IsLocal) {}
  };

private:
  /// Kind of summary for use in dyn_cast<> et al.
  SummaryKind Kind;

  GVFlags Flags;

  /// This is the hash of the name of the symbol in the original file. It is
  /// identical to the GUID for global symbols, but differs for local since the
  /// GUID includes the module level id in the hash.
  GlobalValue::GUID OriginalName = 0;

  /// Path of module IR containing value's definition, used to locate
  /// module during importing.
  ///
  /// This is only used during parsing of the combined index, or when
  /// parsing the per-module index for creation of the combined summary index,
  /// not during writing of the per-module index which doesn't contain a
  /// module path string table.
  StringRef ModulePath;

  /// List of values referenced by this global value's definition
  /// (either by the initializer of a global variable, or referenced
  /// from within a function). This does not include functions called, which
  /// are listed in the derived FunctionSummary object.
  std::vector<ValueInfo> RefEdgeList;

protected:
  GlobalValueSummary(SummaryKind K, GVFlags Flags, std::vector<ValueInfo> Refs)
      : Kind(K), Flags(Flags), RefEdgeList(std::move(Refs)) {
    assert((K != AliasKind || Refs.empty()) &&
           "Expect no references for AliasSummary");
  }

public:
  virtual ~GlobalValueSummary() = default;

  /// Returns the hash of the original name, it is identical to the GUID for
  /// externally visible symbols, but not for local ones.
  GlobalValue::GUID getOriginalName() const { return OriginalName; }

  /// Initialize the original name hash in this summary.
  void setOriginalName(GlobalValue::GUID Name) { OriginalName = Name; }

  /// Which kind of summary subclass this is.
  SummaryKind getSummaryKind() const { return Kind; }

  /// Set the path to the module containing this function, for use in
  /// the combined index.
  void setModulePath(StringRef ModPath) { ModulePath = ModPath; }

  /// Get the path to the module containing this function.
  StringRef modulePath() const { return ModulePath; }

  /// Get the flags for this GlobalValue (see \p struct GVFlags).
  GVFlags flags() const { return Flags; }

  /// Return linkage type recorded for this global value.
  GlobalValue::LinkageTypes linkage() const {
    return static_cast<GlobalValue::LinkageTypes>(Flags.Linkage);
  }

  /// Sets the linkage to the value determined by global summary-based
  /// optimization. Will be applied in the ThinLTO backends.
  void setLinkage(GlobalValue::LinkageTypes Linkage) {
    Flags.Linkage = Linkage;
  }

  /// Return true if this global value can't be imported.
  bool notEligibleToImport() const { return Flags.NotEligibleToImport; }

  bool isLive() const { return Flags.Live; }

  void setLive(bool Live) { Flags.Live = Live; }

  void setDSOLocal(bool Local) { Flags.DSOLocal = Local; }

  bool isDSOLocal() const { return Flags.DSOLocal; }

  /// Flag that this global value cannot be imported.
  void setNotEligibleToImport() { Flags.NotEligibleToImport = true; }

  /// Return the list of values referenced by this global value definition.
  ArrayRef<ValueInfo> refs() const { return RefEdgeList; }

  /// If this is an alias summary, returns the summary of the aliased object (a
  /// global variable or function), otherwise returns itself.
  GlobalValueSummary *getBaseObject();
  const GlobalValueSummary *getBaseObject() const;

  friend class ModuleSummaryIndex;
};

/// Alias summary information.
class AliasSummary : public GlobalValueSummary {
  GlobalValueSummary *AliaseeSummary;
  // AliaseeGUID is only set and accessed when we are building a combined index
  // via the BitcodeReader.
  GlobalValue::GUID AliaseeGUID;

public:
  AliasSummary(GVFlags Flags)
      : GlobalValueSummary(AliasKind, Flags, ArrayRef<ValueInfo>{}),
        AliaseeSummary(nullptr), AliaseeGUID(0) {}

  /// Check if this is an alias summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == AliasKind;
  }

  void setAliasee(GlobalValueSummary *Aliasee) { AliaseeSummary = Aliasee; }
  void setAliaseeGUID(GlobalValue::GUID GUID) { AliaseeGUID = GUID; }

  bool hasAliasee() const { return !!AliaseeSummary; }

  const GlobalValueSummary &getAliasee() const {
    assert(AliaseeSummary && "Unexpected missing aliasee summary");
    return *AliaseeSummary;
  }

  GlobalValueSummary &getAliasee() {
    return const_cast<GlobalValueSummary &>(
                         static_cast<const AliasSummary *>(this)->getAliasee());
  }
  bool hasAliaseeGUID() const { return AliaseeGUID != 0; }
  const GlobalValue::GUID &getAliaseeGUID() const {
    assert(AliaseeGUID && "Unexpected missing aliasee GUID");
    return AliaseeGUID;
  }
};

const inline GlobalValueSummary *GlobalValueSummary::getBaseObject() const {
  if (auto *AS = dyn_cast<AliasSummary>(this))
    return &AS->getAliasee();
  return this;
}

inline GlobalValueSummary *GlobalValueSummary::getBaseObject() {
  if (auto *AS = dyn_cast<AliasSummary>(this))
    return &AS->getAliasee();
  return this;
}

/// Function summary information to aid decisions and implementation of
/// importing.
class FunctionSummary : public GlobalValueSummary {
public:
  /// <CalleeValueInfo, CalleeInfo> call edge pair.
  using EdgeTy = std::pair<ValueInfo, CalleeInfo>;

  /// Types for -force-summary-edges-cold debugging option.
  enum ForceSummaryHotnessType : unsigned {
    FSHT_None,
    FSHT_AllNonCritical,
    FSHT_All
  };

  /// An "identifier" for a virtual function. This contains the type identifier
  /// represented as a GUID and the offset from the address point to the virtual
  /// function pointer, where "address point" is as defined in the Itanium ABI:
  /// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable-general
  struct VFuncId {
    GlobalValue::GUID GUID;
    uint64_t Offset;
  };

  /// A specification for a virtual function call with all constant integer
  /// arguments. This is used to perform virtual constant propagation on the
  /// summary.
  struct ConstVCall {
    VFuncId VFunc;
    std::vector<uint64_t> Args;
  };

  /// All type identifier related information. Because these fields are
  /// relatively uncommon we only allocate space for them if necessary.
  struct TypeIdInfo {
    /// List of type identifiers used by this function in llvm.type.test
    /// intrinsics referenced by something other than an llvm.assume intrinsic,
    /// represented as GUIDs.
    std::vector<GlobalValue::GUID> TypeTests;

    /// List of virtual calls made by this function using (respectively)
    /// llvm.assume(llvm.type.test) or llvm.type.checked.load intrinsics that do
    /// not have all constant integer arguments.
    std::vector<VFuncId> TypeTestAssumeVCalls, TypeCheckedLoadVCalls;

    /// List of virtual calls made by this function using (respectively)
    /// llvm.assume(llvm.type.test) or llvm.type.checked.load intrinsics with
    /// all constant integer arguments.
    std::vector<ConstVCall> TypeTestAssumeConstVCalls,
        TypeCheckedLoadConstVCalls;
  };

  /// Flags specific to function summaries.
  struct FFlags {
    // Function attribute flags. Used to track if a function accesses memory,
    // recurses or aliases.
    unsigned ReadNone : 1;
    unsigned ReadOnly : 1;
    unsigned NoRecurse : 1;
    unsigned ReturnDoesNotAlias : 1;

    // Indicate if the global value cannot be inlined.
    unsigned NoInline : 1;
  };

  /// Create an empty FunctionSummary (with specified call edges).
  /// Used to represent external nodes and the dummy root node.
  static FunctionSummary
  makeDummyFunctionSummary(std::vector<FunctionSummary::EdgeTy> Edges) {
    return FunctionSummary(
        FunctionSummary::GVFlags(
            GlobalValue::LinkageTypes::AvailableExternallyLinkage,
            /*NotEligibleToImport=*/true, /*Live=*/true, /*IsLocal=*/false),
        /*InsCount=*/0, FunctionSummary::FFlags{}, /*EntryCount=*/0,
        std::vector<ValueInfo>(), std::move(Edges),
        std::vector<GlobalValue::GUID>(),
        std::vector<FunctionSummary::VFuncId>(),
        std::vector<FunctionSummary::VFuncId>(),
        std::vector<FunctionSummary::ConstVCall>(),
        std::vector<FunctionSummary::ConstVCall>());
  }

  /// A dummy node to reference external functions that aren't in the index
  static FunctionSummary ExternalNode;

private:
  /// Number of instructions (ignoring debug instructions, e.g.) computed
  /// during the initial compile step when the summary index is first built.
  unsigned InstCount;

  /// Function summary specific flags.
  FFlags FunFlags;

  /// The synthesized entry count of the function.
  /// This is only populated during ThinLink phase and remains unused while
  /// generating per-module summaries.
  uint64_t EntryCount = 0;

  /// List of <CalleeValueInfo, CalleeInfo> call edge pairs from this function.
  std::vector<EdgeTy> CallGraphEdgeList;

  std::unique_ptr<TypeIdInfo> TIdInfo;

public:
  FunctionSummary(GVFlags Flags, unsigned NumInsts, FFlags FunFlags,
                  uint64_t EntryCount, std::vector<ValueInfo> Refs,
                  std::vector<EdgeTy> CGEdges,
                  std::vector<GlobalValue::GUID> TypeTests,
                  std::vector<VFuncId> TypeTestAssumeVCalls,
                  std::vector<VFuncId> TypeCheckedLoadVCalls,
                  std::vector<ConstVCall> TypeTestAssumeConstVCalls,
                  std::vector<ConstVCall> TypeCheckedLoadConstVCalls)
      : GlobalValueSummary(FunctionKind, Flags, std::move(Refs)),
        InstCount(NumInsts), FunFlags(FunFlags), EntryCount(EntryCount),
        CallGraphEdgeList(std::move(CGEdges)) {
    if (!TypeTests.empty() || !TypeTestAssumeVCalls.empty() ||
        !TypeCheckedLoadVCalls.empty() || !TypeTestAssumeConstVCalls.empty() ||
        !TypeCheckedLoadConstVCalls.empty())
      TIdInfo = llvm::make_unique<TypeIdInfo>(TypeIdInfo{
          std::move(TypeTests), std::move(TypeTestAssumeVCalls),
          std::move(TypeCheckedLoadVCalls),
          std::move(TypeTestAssumeConstVCalls),
          std::move(TypeCheckedLoadConstVCalls)});
  }
  // Gets the number of immutable refs in RefEdgeList
  unsigned immutableRefCount() const;

  /// Check if this is a function summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == FunctionKind;
  }

  /// Get function summary flags.
  FFlags fflags() const { return FunFlags; }

  /// Get the instruction count recorded for this function.
  unsigned instCount() const { return InstCount; }

  /// Get the synthetic entry count for this function.
  uint64_t entryCount() const { return EntryCount; }

  /// Set the synthetic entry count for this function.
  void setEntryCount(uint64_t EC) { EntryCount = EC; }

  /// Return the list of <CalleeValueInfo, CalleeInfo> pairs.
  ArrayRef<EdgeTy> calls() const { return CallGraphEdgeList; }

  /// Returns the list of type identifiers used by this function in
  /// llvm.type.test intrinsics other than by an llvm.assume intrinsic,
  /// represented as GUIDs.
  ArrayRef<GlobalValue::GUID> type_tests() const {
    if (TIdInfo)
      return TIdInfo->TypeTests;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.assume(llvm.type.test) intrinsics that do not have all constant
  /// integer arguments.
  ArrayRef<VFuncId> type_test_assume_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeTestAssumeVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.type.checked.load intrinsics that do not have all constant integer
  /// arguments.
  ArrayRef<VFuncId> type_checked_load_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeCheckedLoadVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.assume(llvm.type.test) intrinsics with all constant integer
  /// arguments.
  ArrayRef<ConstVCall> type_test_assume_const_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeTestAssumeConstVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.type.checked.load intrinsics with all constant integer arguments.
  ArrayRef<ConstVCall> type_checked_load_const_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeCheckedLoadConstVCalls;
    return {};
  }

  /// Add a type test to the summary. This is used by WholeProgramDevirt if we
  /// were unable to devirtualize a checked call.
  void addTypeTest(GlobalValue::GUID Guid) {
    if (!TIdInfo)
      TIdInfo = llvm::make_unique<TypeIdInfo>();
    TIdInfo->TypeTests.push_back(Guid);
  }

  const TypeIdInfo *getTypeIdInfo() const { return TIdInfo.get(); };

  friend struct GraphTraits<ValueInfo>;
};

template <> struct DenseMapInfo<FunctionSummary::VFuncId> {
  static FunctionSummary::VFuncId getEmptyKey() { return {0, uint64_t(-1)}; }

  static FunctionSummary::VFuncId getTombstoneKey() {
    return {0, uint64_t(-2)};
  }

  static bool isEqual(FunctionSummary::VFuncId L, FunctionSummary::VFuncId R) {
    return L.GUID == R.GUID && L.Offset == R.Offset;
  }

  static unsigned getHashValue(FunctionSummary::VFuncId I) { return I.GUID; }
};

template <> struct DenseMapInfo<FunctionSummary::ConstVCall> {
  static FunctionSummary::ConstVCall getEmptyKey() {
    return {{0, uint64_t(-1)}, {}};
  }

  static FunctionSummary::ConstVCall getTombstoneKey() {
    return {{0, uint64_t(-2)}, {}};
  }

  static bool isEqual(FunctionSummary::ConstVCall L,
                      FunctionSummary::ConstVCall R) {
    return DenseMapInfo<FunctionSummary::VFuncId>::isEqual(L.VFunc, R.VFunc) &&
           L.Args == R.Args;
  }

  static unsigned getHashValue(FunctionSummary::ConstVCall I) {
    return I.VFunc.GUID;
  }
};

/// Global variable summary information to aid decisions and
/// implementation of importing.
///
/// Global variable summary has extra flag, telling if it is
/// modified during the program run or not. This affects ThinLTO
/// internalization
class GlobalVarSummary : public GlobalValueSummary {
public:
  struct GVarFlags {
    GVarFlags(bool ReadOnly = false) : ReadOnly(ReadOnly) {}

    unsigned ReadOnly : 1;
  } VarFlags;

  GlobalVarSummary(GVFlags Flags, GVarFlags VarFlags,
                   std::vector<ValueInfo> Refs)
      : GlobalValueSummary(GlobalVarKind, Flags, std::move(Refs)),
        VarFlags(VarFlags) {}

  /// Check if this is a global variable summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == GlobalVarKind;
  }

  GVarFlags varflags() const { return VarFlags; }
  void setReadOnly(bool RO) { VarFlags.ReadOnly = RO; }
  bool isReadOnly() const { return VarFlags.ReadOnly; }
};

struct TypeTestResolution {
  /// Specifies which kind of type check we should emit for this byte array.
  /// See http://clang.llvm.org/docs/ControlFlowIntegrityDesign.html for full
  /// details on each kind of check; the enumerators are described with
  /// reference to that document.
  enum Kind {
    Unsat,     ///< Unsatisfiable type (i.e. no global has this type metadata)
    ByteArray, ///< Test a byte array (first example)
    Inline,    ///< Inlined bit vector ("Short Inline Bit Vectors")
    Single,    ///< Single element (last example in "Short Inline Bit Vectors")
    AllOnes,   ///< All-ones bit vector ("Eliminating Bit Vector Checks for
               ///  All-Ones Bit Vectors")
  } TheKind = Unsat;

  /// Range of size-1 expressed as a bit width. For example, if the size is in
  /// range [1,256], this number will be 8. This helps generate the most compact
  /// instruction sequences.
  unsigned SizeM1BitWidth = 0;

  // The following fields are only used if the target does not support the use
  // of absolute symbols to store constants. Their meanings are the same as the
  // corresponding fields in LowerTypeTestsModule::TypeIdLowering in
  // LowerTypeTests.cpp.

  uint64_t AlignLog2 = 0;
  uint64_t SizeM1 = 0;
  uint8_t BitMask = 0;
  uint64_t InlineBits = 0;
};

struct WholeProgramDevirtResolution {
  enum Kind {
    Indir,        ///< Just do a regular virtual call
    SingleImpl,   ///< Single implementation devirtualization
    BranchFunnel, ///< When retpoline mitigation is enabled, use a branch funnel
                  ///< that is defined in the merged module. Otherwise same as
                  ///< Indir.
  } TheKind = Indir;

  std::string SingleImplName;

  struct ByArg {
    enum Kind {
      Indir,            ///< Just do a regular virtual call
      UniformRetVal,    ///< Uniform return value optimization
      UniqueRetVal,     ///< Unique return value optimization
      VirtualConstProp, ///< Virtual constant propagation
    } TheKind = Indir;

    /// Additional information for the resolution:
    /// - UniformRetVal: the uniform return value.
    /// - UniqueRetVal: the return value associated with the unique vtable (0 or
    ///   1).
    uint64_t Info = 0;

    // The following fields are only used if the target does not support the use
    // of absolute symbols to store constants.

    uint32_t Byte = 0;
    uint32_t Bit = 0;
  };

  /// Resolutions for calls with all constant integer arguments (excluding the
  /// first argument, "this"), where the key is the argument vector.
  std::map<std::vector<uint64_t>, ByArg> ResByArg;
};

struct TypeIdSummary {
  TypeTestResolution TTRes;

  /// Mapping from byte offset to whole-program devirt resolution for that
  /// (typeid, byte offset) pair.
  std::map<uint64_t, WholeProgramDevirtResolution> WPDRes;
};

/// 160 bits SHA1
using ModuleHash = std::array<uint32_t, 5>;

/// Type used for iterating through the global value summary map.
using const_gvsummary_iterator = GlobalValueSummaryMapTy::const_iterator;
using gvsummary_iterator = GlobalValueSummaryMapTy::iterator;

/// String table to hold/own module path strings, which additionally holds the
/// module ID assigned to each module during the plugin step, as well as a hash
/// of the module. The StringMap makes a copy of and owns inserted strings.
using ModulePathStringTableTy = StringMap<std::pair<uint64_t, ModuleHash>>;

/// Map of global value GUID to its summary, used to identify values defined in
/// a particular module, and provide efficient access to their summary.
using GVSummaryMapTy = DenseMap<GlobalValue::GUID, GlobalValueSummary *>;

/// Map of a type GUID to type id string and summary (multimap used
/// in case of GUID conflicts).
using TypeIdSummaryMapTy =
    std::multimap<GlobalValue::GUID, std::pair<std::string, TypeIdSummary>>;

/// Class to hold module path string table and global value map,
/// and encapsulate methods for operating on them.
class ModuleSummaryIndex {
private:
  /// Map from value name to list of summary instances for values of that
  /// name (may be duplicates in the COMDAT case, e.g.).
  GlobalValueSummaryMapTy GlobalValueMap;

  /// Holds strings for combined index, mapping to the corresponding module ID.
  ModulePathStringTableTy ModulePathStringTable;

  /// Mapping from type identifier GUIDs to type identifier and its summary
  /// information.
  TypeIdSummaryMapTy TypeIdMap;

  /// Mapping from original ID to GUID. If original ID can map to multiple
  /// GUIDs, it will be mapped to 0.
  std::map<GlobalValue::GUID, GlobalValue::GUID> OidGuidMap;

  /// Indicates that summary-based GlobalValue GC has run, and values with
  /// GVFlags::Live==false are really dead. Otherwise, all values must be
  /// considered live.
  bool WithGlobalValueDeadStripping = false;

  /// Indicates that summary-based synthetic entry count propagation has run
  bool HasSyntheticEntryCounts = false;

  /// Indicates that distributed backend should skip compilation of the
  /// module. Flag is suppose to be set by distributed ThinLTO indexing
  /// when it detected that the module is not needed during the final
  /// linking. As result distributed backend should just output a minimal
  /// valid object file.
  bool SkipModuleByDistributedBackend = false;

  /// If true then we're performing analysis of IR module, or parsing along with
  /// the IR from assembly. The value of 'false' means we're reading summary
  /// from BC or YAML source. Affects the type of value stored in NameOrGV
  /// union.
  bool HaveGVs;

  // True if the index was created for a module compiled with -fsplit-lto-unit.
  bool EnableSplitLTOUnit;

  // True if some of the modules were compiled with -fsplit-lto-unit and
  // some were not. Set when the combined index is created during the thin link.
  bool PartiallySplitLTOUnits = false;

  std::set<std::string> CfiFunctionDefs;
  std::set<std::string> CfiFunctionDecls;

  // Used in cases where we want to record the name of a global, but
  // don't have the string owned elsewhere (e.g. the Strtab on a module).
  StringSaver Saver;
  BumpPtrAllocator Alloc;

  // YAML I/O support.
  friend yaml::MappingTraits<ModuleSummaryIndex>;

  GlobalValueSummaryMapTy::value_type *
  getOrInsertValuePtr(GlobalValue::GUID GUID) {
    return &*GlobalValueMap.emplace(GUID, GlobalValueSummaryInfo(HaveGVs))
                 .first;
  }

public:
  // See HaveGVs variable comment.
  ModuleSummaryIndex(bool HaveGVs, bool EnableSplitLTOUnit = false)
      : HaveGVs(HaveGVs), EnableSplitLTOUnit(EnableSplitLTOUnit), Saver(Alloc) {
  }

  bool haveGVs() const { return HaveGVs; }

  gvsummary_iterator begin() { return GlobalValueMap.begin(); }
  const_gvsummary_iterator begin() const { return GlobalValueMap.begin(); }
  gvsummary_iterator end() { return GlobalValueMap.end(); }
  const_gvsummary_iterator end() const { return GlobalValueMap.end(); }
  size_t size() const { return GlobalValueMap.size(); }

  /// Convenience function for doing a DFS on a ValueInfo. Marks the function in
  /// the FunctionHasParent map.
  static void discoverNodes(ValueInfo V,
                            std::map<ValueInfo, bool> &FunctionHasParent) {
    if (!V.getSummaryList().size())
      return; // skip external functions that don't have summaries

    // Mark discovered if we haven't yet
    auto S = FunctionHasParent.emplace(V, false);

    // Stop if we've already discovered this node
    if (!S.second)
      return;

    FunctionSummary *F =
        dyn_cast<FunctionSummary>(V.getSummaryList().front().get());
    assert(F != nullptr && "Expected FunctionSummary node");

    for (auto &C : F->calls()) {
      // Insert node if necessary
      auto S = FunctionHasParent.emplace(C.first, true);

      // Skip nodes that we're sure have parents
      if (!S.second && S.first->second)
        continue;

      if (S.second)
        discoverNodes(C.first, FunctionHasParent);
      else
        S.first->second = true;
    }
  }

  // Calculate the callgraph root
  FunctionSummary calculateCallGraphRoot() {
    // Functions that have a parent will be marked in FunctionHasParent pair.
    // Once we've marked all functions, the functions in the map that are false
    // have no parent (so they're the roots)
    std::map<ValueInfo, bool> FunctionHasParent;

    for (auto &S : *this) {
      // Skip external functions
      if (!S.second.SummaryList.size() ||
          !isa<FunctionSummary>(S.second.SummaryList.front().get()))
        continue;
      discoverNodes(ValueInfo(HaveGVs, &S), FunctionHasParent);
    }

    std::vector<FunctionSummary::EdgeTy> Edges;
    // create edges to all roots in the Index
    for (auto &P : FunctionHasParent) {
      if (P.second)
        continue; // skip over non-root nodes
      Edges.push_back(std::make_pair(P.first, CalleeInfo{}));
    }
    if (Edges.empty()) {
      // Failed to find root - return an empty node
      return FunctionSummary::makeDummyFunctionSummary({});
    }
    auto CallGraphRoot = FunctionSummary::makeDummyFunctionSummary(Edges);
    return CallGraphRoot;
  }

  bool withGlobalValueDeadStripping() const {
    return WithGlobalValueDeadStripping;
  }
  void setWithGlobalValueDeadStripping() {
    WithGlobalValueDeadStripping = true;
  }

  bool hasSyntheticEntryCounts() const { return HasSyntheticEntryCounts; }
  void setHasSyntheticEntryCounts() { HasSyntheticEntryCounts = true; }

  bool skipModuleByDistributedBackend() const {
    return SkipModuleByDistributedBackend;
  }
  void setSkipModuleByDistributedBackend() {
    SkipModuleByDistributedBackend = true;
  }

  bool enableSplitLTOUnit() const { return EnableSplitLTOUnit; }
  void setEnableSplitLTOUnit() { EnableSplitLTOUnit = true; }

  bool partiallySplitLTOUnits() const { return PartiallySplitLTOUnits; }
  void setPartiallySplitLTOUnits() { PartiallySplitLTOUnits = true; }

  bool isGlobalValueLive(const GlobalValueSummary *GVS) const {
    return !WithGlobalValueDeadStripping || GVS->isLive();
  }
  bool isGUIDLive(GlobalValue::GUID GUID) const;

  /// Return a ValueInfo for the index value_type (convenient when iterating
  /// index).
  ValueInfo getValueInfo(const GlobalValueSummaryMapTy::value_type &R) const {
    return ValueInfo(HaveGVs, &R);
  }

  /// Return a ValueInfo for GUID if it exists, otherwise return ValueInfo().
  ValueInfo getValueInfo(GlobalValue::GUID GUID) const {
    auto I = GlobalValueMap.find(GUID);
    return ValueInfo(HaveGVs, I == GlobalValueMap.end() ? nullptr : &*I);
  }

  /// Return a ValueInfo for \p GUID.
  ValueInfo getOrInsertValueInfo(GlobalValue::GUID GUID) {
    return ValueInfo(HaveGVs, getOrInsertValuePtr(GUID));
  }

  // Save a string in the Index. Use before passing Name to
  // getOrInsertValueInfo when the string isn't owned elsewhere (e.g. on the
  // module's Strtab).
  StringRef saveString(StringRef String) { return Saver.save(String); }

  /// Return a ValueInfo for \p GUID setting value \p Name.
  ValueInfo getOrInsertValueInfo(GlobalValue::GUID GUID, StringRef Name) {
    assert(!HaveGVs);
    auto VP = getOrInsertValuePtr(GUID);
    VP->second.U.Name = Name;
    return ValueInfo(HaveGVs, VP);
  }

  /// Return a ValueInfo for \p GV and mark it as belonging to GV.
  ValueInfo getOrInsertValueInfo(const GlobalValue *GV) {
    assert(HaveGVs);
    auto VP = getOrInsertValuePtr(GV->getGUID());
    VP->second.U.GV = GV;
    return ValueInfo(HaveGVs, VP);
  }

  /// Return the GUID for \p OriginalId in the OidGuidMap.
  GlobalValue::GUID getGUIDFromOriginalID(GlobalValue::GUID OriginalID) const {
    const auto I = OidGuidMap.find(OriginalID);
    return I == OidGuidMap.end() ? 0 : I->second;
  }

  std::set<std::string> &cfiFunctionDefs() { return CfiFunctionDefs; }
  const std::set<std::string> &cfiFunctionDefs() const { return CfiFunctionDefs; }

  std::set<std::string> &cfiFunctionDecls() { return CfiFunctionDecls; }
  const std::set<std::string> &cfiFunctionDecls() const { return CfiFunctionDecls; }

  /// Add a global value summary for a value.
  void addGlobalValueSummary(const GlobalValue &GV,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addGlobalValueSummary(getOrInsertValueInfo(&GV), std::move(Summary));
  }

  /// Add a global value summary for a value of the given name.
  void addGlobalValueSummary(StringRef ValueName,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addGlobalValueSummary(getOrInsertValueInfo(GlobalValue::getGUID(ValueName)),
                          std::move(Summary));
  }

  /// Add a global value summary for the given ValueInfo.
  void addGlobalValueSummary(ValueInfo VI,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addOriginalName(VI.getGUID(), Summary->getOriginalName());
    // Here we have a notionally const VI, but the value it points to is owned
    // by the non-const *this.
    const_cast<GlobalValueSummaryMapTy::value_type *>(VI.getRef())
        ->second.SummaryList.push_back(std::move(Summary));
  }

  /// Add an original name for the value of the given GUID.
  void addOriginalName(GlobalValue::GUID ValueGUID,
                       GlobalValue::GUID OrigGUID) {
    if (OrigGUID == 0 || ValueGUID == OrigGUID)
      return;
    if (OidGuidMap.count(OrigGUID) && OidGuidMap[OrigGUID] != ValueGUID)
      OidGuidMap[OrigGUID] = 0;
    else
      OidGuidMap[OrigGUID] = ValueGUID;
  }

  /// Find the summary for global \p GUID in module \p ModuleId, or nullptr if
  /// not found.
  GlobalValueSummary *findSummaryInModule(GlobalValue::GUID ValueGUID,
                                          StringRef ModuleId) const {
    auto CalleeInfo = getValueInfo(ValueGUID);
    if (!CalleeInfo) {
      return nullptr; // This function does not have a summary
    }
    auto Summary =
        llvm::find_if(CalleeInfo.getSummaryList(),
                      [&](const std::unique_ptr<GlobalValueSummary> &Summary) {
                        return Summary->modulePath() == ModuleId;
                      });
    if (Summary == CalleeInfo.getSummaryList().end())
      return nullptr;
    return Summary->get();
  }

  /// Returns the first GlobalValueSummary for \p GV, asserting that there
  /// is only one if \p PerModuleIndex.
  GlobalValueSummary *getGlobalValueSummary(const GlobalValue &GV,
                                            bool PerModuleIndex = true) const {
    assert(GV.hasName() && "Can't get GlobalValueSummary for GV with no name");
    return getGlobalValueSummary(GV.getGUID(), PerModuleIndex);
  }

  /// Returns the first GlobalValueSummary for \p ValueGUID, asserting that
  /// there
  /// is only one if \p PerModuleIndex.
  GlobalValueSummary *getGlobalValueSummary(GlobalValue::GUID ValueGUID,
                                            bool PerModuleIndex = true) const;

  /// Table of modules, containing module hash and id.
  const StringMap<std::pair<uint64_t, ModuleHash>> &modulePaths() const {
    return ModulePathStringTable;
  }

  /// Table of modules, containing hash and id.
  StringMap<std::pair<uint64_t, ModuleHash>> &modulePaths() {
    return ModulePathStringTable;
  }

  /// Get the module ID recorded for the given module path.
  uint64_t getModuleId(const StringRef ModPath) const {
    return ModulePathStringTable.lookup(ModPath).first;
  }

  /// Get the module SHA1 hash recorded for the given module path.
  const ModuleHash &getModuleHash(const StringRef ModPath) const {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return It->second.second;
  }

  /// Convenience method for creating a promoted global name
  /// for the given value name of a local, and its original module's ID.
  static std::string getGlobalNameForLocal(StringRef Name, ModuleHash ModHash) {
    SmallString<256> NewName(Name);
    NewName += ".llvm.";
    NewName += utostr((uint64_t(ModHash[0]) << 32) |
                      ModHash[1]); // Take the first 64 bits
    return NewName.str();
  }

  /// Helper to obtain the unpromoted name for a global value (or the original
  /// name if not promoted).
  static StringRef getOriginalNameBeforePromote(StringRef Name) {
    std::pair<StringRef, StringRef> Pair = Name.split(".llvm.");
    return Pair.first;
  }

  typedef ModulePathStringTableTy::value_type ModuleInfo;

  /// Add a new module with the given \p Hash, mapped to the given \p
  /// ModID, and return a reference to the module.
  ModuleInfo *addModule(StringRef ModPath, uint64_t ModId,
                        ModuleHash Hash = ModuleHash{{0}}) {
    return &*ModulePathStringTable.insert({ModPath, {ModId, Hash}}).first;
  }

  /// Return module entry for module with the given \p ModPath.
  ModuleInfo *getModule(StringRef ModPath) {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return &*It;
  }

  /// Check if the given Module has any functions available for exporting
  /// in the index. We consider any module present in the ModulePathStringTable
  /// to have exported functions.
  bool hasExportedFunctions(const Module &M) const {
    return ModulePathStringTable.count(M.getModuleIdentifier());
  }

  const TypeIdSummaryMapTy &typeIds() const { return TypeIdMap; }

  /// Return an existing or new TypeIdSummary entry for \p TypeId.
  /// This accessor can mutate the map and therefore should not be used in
  /// the ThinLTO backends.
  TypeIdSummary &getOrInsertTypeIdSummary(StringRef TypeId) {
    auto TidIter = TypeIdMap.equal_range(GlobalValue::getGUID(TypeId));
    for (auto It = TidIter.first; It != TidIter.second; ++It)
      if (It->second.first == TypeId)
        return It->second.second;
    auto It = TypeIdMap.insert(
        {GlobalValue::getGUID(TypeId), {TypeId, TypeIdSummary()}});
    return It->second.second;
  }

  /// This returns either a pointer to the type id summary (if present in the
  /// summary map) or null (if not present). This may be used when importing.
  const TypeIdSummary *getTypeIdSummary(StringRef TypeId) const {
    auto TidIter = TypeIdMap.equal_range(GlobalValue::getGUID(TypeId));
    for (auto It = TidIter.first; It != TidIter.second; ++It)
      if (It->second.first == TypeId)
        return &It->second.second;
    return nullptr;
  }

  /// Collect for the given module the list of functions it defines
  /// (GUID -> Summary).
  void collectDefinedFunctionsForModule(StringRef ModulePath,
                                        GVSummaryMapTy &GVSummaryMap) const;

  /// Collect for each module the list of Summaries it defines (GUID ->
  /// Summary).
  void collectDefinedGVSummariesPerModule(
      StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries) const;

  /// Print to an output stream.
  void print(raw_ostream &OS, bool IsForDebug = false) const;

  /// Dump to stderr (for debugging).
  void dump() const;

  /// Export summary to dot file for GraphViz.
  void exportToDot(raw_ostream& OS) const;

  /// Print out strongly connected components for debugging.
  void dumpSCCs(raw_ostream &OS);

  /// Analyze index and detect unmodified globals
  void propagateConstants(const DenseSet<GlobalValue::GUID> &PreservedSymbols);
};

/// GraphTraits definition to build SCC for the index
template <> struct GraphTraits<ValueInfo> {
  typedef ValueInfo NodeRef;
  using EdgeRef = FunctionSummary::EdgeTy &;

  static NodeRef valueInfoFromEdge(FunctionSummary::EdgeTy &P) {
    return P.first;
  }
  using ChildIteratorType =
      mapped_iterator<std::vector<FunctionSummary::EdgeTy>::iterator,
                      decltype(&valueInfoFromEdge)>;

  using ChildEdgeIteratorType = std::vector<FunctionSummary::EdgeTy>::iterator;

  static NodeRef getEntryNode(ValueInfo V) { return V; }

  static ChildIteratorType child_begin(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return ChildIteratorType(
          FunctionSummary::ExternalNode.CallGraphEdgeList.begin(),
          &valueInfoFromEdge);
    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return ChildIteratorType(F->CallGraphEdgeList.begin(), &valueInfoFromEdge);
  }

  static ChildIteratorType child_end(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return ChildIteratorType(
          FunctionSummary::ExternalNode.CallGraphEdgeList.end(),
          &valueInfoFromEdge);
    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return ChildIteratorType(F->CallGraphEdgeList.end(), &valueInfoFromEdge);
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return FunctionSummary::ExternalNode.CallGraphEdgeList.begin();

    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return F->CallGraphEdgeList.begin();
  }

  static ChildEdgeIteratorType child_edge_end(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return FunctionSummary::ExternalNode.CallGraphEdgeList.end();

    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return F->CallGraphEdgeList.end();
  }

  static NodeRef edge_dest(EdgeRef E) { return E.first; }
};

template <>
struct GraphTraits<ModuleSummaryIndex *> : public GraphTraits<ValueInfo> {
  static NodeRef getEntryNode(ModuleSummaryIndex *I) {
    std::unique_ptr<GlobalValueSummary> Root =
        make_unique<FunctionSummary>(I->calculateCallGraphRoot());
    GlobalValueSummaryInfo G(I->haveGVs());
    G.SummaryList.push_back(std::move(Root));
    static auto P =
        GlobalValueSummaryMapTy::value_type(GlobalValue::GUID(0), std::move(G));
    return ValueInfo(I->haveGVs(), &P);
  }
};

static inline bool canImportGlobalVar(GlobalValueSummary *S) {
  assert(isa<GlobalVarSummary>(S->getBaseObject()));

  // We don't import GV with references, because it can result
  // in promotion of local variables in the source module.
  return !GlobalValue::isInterposableLinkage(S->linkage()) &&
         !S->notEligibleToImport() && S->refs().empty();
}
} // end namespace llvm

#endif // LLVM_IR_MODULESUMMARYINDEX_H
