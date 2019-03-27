//===- SampleProf.h - Sampling profiling format support ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains common definitions used in the reading and writing of
// sample profile data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_SAMPLEPROF_H
#define LLVM_PROFILEDATA_SAMPLEPROF_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <system_error>
#include <utility>

namespace llvm {

class raw_ostream;

const std::error_category &sampleprof_category();

enum class sampleprof_error {
  success = 0,
  bad_magic,
  unsupported_version,
  too_large,
  truncated,
  malformed,
  unrecognized_format,
  unsupported_writing_format,
  truncated_name_table,
  not_implemented,
  counter_overflow,
  ostream_seek_unsupported
};

inline std::error_code make_error_code(sampleprof_error E) {
  return std::error_code(static_cast<int>(E), sampleprof_category());
}

inline sampleprof_error MergeResult(sampleprof_error &Accumulator,
                                    sampleprof_error Result) {
  // Prefer first error encountered as later errors may be secondary effects of
  // the initial problem.
  if (Accumulator == sampleprof_error::success &&
      Result != sampleprof_error::success)
    Accumulator = Result;
  return Accumulator;
}

} // end namespace llvm

namespace std {

template <>
struct is_error_code_enum<llvm::sampleprof_error> : std::true_type {};

} // end namespace std

namespace llvm {
namespace sampleprof {

enum SampleProfileFormat {
  SPF_None = 0,
  SPF_Text = 0x1,
  SPF_Compact_Binary = 0x2,
  SPF_GCC = 0x3,
  SPF_Binary = 0xff
};

static inline uint64_t SPMagic(SampleProfileFormat Format = SPF_Binary) {
  return uint64_t('S') << (64 - 8) | uint64_t('P') << (64 - 16) |
         uint64_t('R') << (64 - 24) | uint64_t('O') << (64 - 32) |
         uint64_t('F') << (64 - 40) | uint64_t('4') << (64 - 48) |
         uint64_t('2') << (64 - 56) | uint64_t(Format);
}

// Get the proper representation of a string in the input Format.
static inline StringRef getRepInFormat(StringRef Name,
                                       SampleProfileFormat Format,
                                       std::string &GUIDBuf) {
  if (Name.empty())
    return Name;
  GUIDBuf = std::to_string(Function::getGUID(Name));
  return (Format == SPF_Compact_Binary) ? StringRef(GUIDBuf) : Name;
}

static inline uint64_t SPVersion() { return 103; }

/// Represents the relative location of an instruction.
///
/// Instruction locations are specified by the line offset from the
/// beginning of the function (marked by the line where the function
/// header is) and the discriminator value within that line.
///
/// The discriminator value is useful to distinguish instructions
/// that are on the same line but belong to different basic blocks
/// (e.g., the two post-increment instructions in "if (p) x++; else y++;").
struct LineLocation {
  LineLocation(uint32_t L, uint32_t D) : LineOffset(L), Discriminator(D) {}

  void print(raw_ostream &OS) const;
  void dump() const;

  bool operator<(const LineLocation &O) const {
    return LineOffset < O.LineOffset ||
           (LineOffset == O.LineOffset && Discriminator < O.Discriminator);
  }

  uint32_t LineOffset;
  uint32_t Discriminator;
};

raw_ostream &operator<<(raw_ostream &OS, const LineLocation &Loc);

/// Representation of a single sample record.
///
/// A sample record is represented by a positive integer value, which
/// indicates how frequently was the associated line location executed.
///
/// Additionally, if the associated location contains a function call,
/// the record will hold a list of all the possible called targets. For
/// direct calls, this will be the exact function being invoked. For
/// indirect calls (function pointers, virtual table dispatch), this
/// will be a list of one or more functions.
class SampleRecord {
public:
  using CallTargetMap = StringMap<uint64_t>;

  SampleRecord() = default;

  /// Increment the number of samples for this record by \p S.
  /// Optionally scale sample count \p S by \p Weight.
  ///
  /// Sample counts accumulate using saturating arithmetic, to avoid wrapping
  /// around unsigned integers.
  sampleprof_error addSamples(uint64_t S, uint64_t Weight = 1) {
    bool Overflowed;
    NumSamples = SaturatingMultiplyAdd(S, Weight, NumSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Add called function \p F with samples \p S.
  /// Optionally scale sample count \p S by \p Weight.
  ///
  /// Sample counts accumulate using saturating arithmetic, to avoid wrapping
  /// around unsigned integers.
  sampleprof_error addCalledTarget(StringRef F, uint64_t S,
                                   uint64_t Weight = 1) {
    uint64_t &TargetSamples = CallTargets[F];
    bool Overflowed;
    TargetSamples =
        SaturatingMultiplyAdd(S, Weight, TargetSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Return true if this sample record contains function calls.
  bool hasCalls() const { return !CallTargets.empty(); }

  uint64_t getSamples() const { return NumSamples; }
  const CallTargetMap &getCallTargets() const { return CallTargets; }

  /// Merge the samples in \p Other into this record.
  /// Optionally scale sample counts by \p Weight.
  sampleprof_error merge(const SampleRecord &Other, uint64_t Weight = 1) {
    sampleprof_error Result = addSamples(Other.getSamples(), Weight);
    for (const auto &I : Other.getCallTargets()) {
      MergeResult(Result, addCalledTarget(I.first(), I.second, Weight));
    }
    return Result;
  }

  void print(raw_ostream &OS, unsigned Indent) const;
  void dump() const;

private:
  uint64_t NumSamples = 0;
  CallTargetMap CallTargets;
};

raw_ostream &operator<<(raw_ostream &OS, const SampleRecord &Sample);

class FunctionSamples;

using BodySampleMap = std::map<LineLocation, SampleRecord>;
// NOTE: Using a StringMap here makes parsed profiles consume around 17% more
// memory, which is *very* significant for large profiles.
using FunctionSamplesMap = std::map<std::string, FunctionSamples>;
using CallsiteSampleMap = std::map<LineLocation, FunctionSamplesMap>;

/// Representation of the samples collected for a function.
///
/// This data structure contains all the collected samples for the body
/// of a function. Each sample corresponds to a LineLocation instance
/// within the body of the function.
class FunctionSamples {
public:
  FunctionSamples() = default;

  void print(raw_ostream &OS = dbgs(), unsigned Indent = 0) const;
  void dump() const;

  sampleprof_error addTotalSamples(uint64_t Num, uint64_t Weight = 1) {
    bool Overflowed;
    TotalSamples =
        SaturatingMultiplyAdd(Num, Weight, TotalSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  sampleprof_error addHeadSamples(uint64_t Num, uint64_t Weight = 1) {
    bool Overflowed;
    TotalHeadSamples =
        SaturatingMultiplyAdd(Num, Weight, TotalHeadSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  sampleprof_error addBodySamples(uint32_t LineOffset, uint32_t Discriminator,
                                  uint64_t Num, uint64_t Weight = 1) {
    return BodySamples[LineLocation(LineOffset, Discriminator)].addSamples(
        Num, Weight);
  }

  sampleprof_error addCalledTargetSamples(uint32_t LineOffset,
                                          uint32_t Discriminator,
                                          StringRef FName, uint64_t Num,
                                          uint64_t Weight = 1) {
    return BodySamples[LineLocation(LineOffset, Discriminator)].addCalledTarget(
        FName, Num, Weight);
  }

  /// Return the number of samples collected at the given location.
  /// Each location is specified by \p LineOffset and \p Discriminator.
  /// If the location is not found in profile, return error.
  ErrorOr<uint64_t> findSamplesAt(uint32_t LineOffset,
                                  uint32_t Discriminator) const {
    const auto &ret = BodySamples.find(LineLocation(LineOffset, Discriminator));
    if (ret == BodySamples.end())
      return std::error_code();
    else
      return ret->second.getSamples();
  }

  /// Returns the call target map collected at a given location.
  /// Each location is specified by \p LineOffset and \p Discriminator.
  /// If the location is not found in profile, return error.
  ErrorOr<SampleRecord::CallTargetMap>
  findCallTargetMapAt(uint32_t LineOffset, uint32_t Discriminator) const {
    const auto &ret = BodySamples.find(LineLocation(LineOffset, Discriminator));
    if (ret == BodySamples.end())
      return std::error_code();
    return ret->second.getCallTargets();
  }

  /// Return the function samples at the given callsite location.
  FunctionSamplesMap &functionSamplesAt(const LineLocation &Loc) {
    return CallsiteSamples[Loc];
  }

  /// Returns the FunctionSamplesMap at the given \p Loc.
  const FunctionSamplesMap *
  findFunctionSamplesMapAt(const LineLocation &Loc) const {
    auto iter = CallsiteSamples.find(Loc);
    if (iter == CallsiteSamples.end())
      return nullptr;
    return &iter->second;
  }

  /// Returns a pointer to FunctionSamples at the given callsite location \p Loc
  /// with callee \p CalleeName. If no callsite can be found, relax the
  /// restriction to return the FunctionSamples at callsite location \p Loc
  /// with the maximum total sample count.
  const FunctionSamples *findFunctionSamplesAt(const LineLocation &Loc,
                                               StringRef CalleeName) const {
    std::string CalleeGUID;
    CalleeName = getRepInFormat(CalleeName, Format, CalleeGUID);

    auto iter = CallsiteSamples.find(Loc);
    if (iter == CallsiteSamples.end())
      return nullptr;
    auto FS = iter->second.find(CalleeName);
    if (FS != iter->second.end())
      return &FS->second;
    // If we cannot find exact match of the callee name, return the FS with
    // the max total count.
    uint64_t MaxTotalSamples = 0;
    const FunctionSamples *R = nullptr;
    for (const auto &NameFS : iter->second)
      if (NameFS.second.getTotalSamples() >= MaxTotalSamples) {
        MaxTotalSamples = NameFS.second.getTotalSamples();
        R = &NameFS.second;
      }
    return R;
  }

  bool empty() const { return TotalSamples == 0; }

  /// Return the total number of samples collected inside the function.
  uint64_t getTotalSamples() const { return TotalSamples; }

  /// Return the total number of branch samples that have the function as the
  /// branch target. This should be equivalent to the sample of the first
  /// instruction of the symbol. But as we directly get this info for raw
  /// profile without referring to potentially inaccurate debug info, this
  /// gives more accurate profile data and is preferred for standalone symbols.
  uint64_t getHeadSamples() const { return TotalHeadSamples; }

  /// Return the sample count of the first instruction of the function.
  /// The function can be either a standalone symbol or an inlined function.
  uint64_t getEntrySamples() const {
    // Use either BodySamples or CallsiteSamples which ever has the smaller
    // lineno.
    if (!BodySamples.empty() &&
        (CallsiteSamples.empty() ||
         BodySamples.begin()->first < CallsiteSamples.begin()->first))
      return BodySamples.begin()->second.getSamples();
    if (!CallsiteSamples.empty()) {
      uint64_t T = 0;
      // An indirect callsite may be promoted to several inlined direct calls.
      // We need to get the sum of them.
      for (const auto &N_FS : CallsiteSamples.begin()->second)
        T += N_FS.second.getEntrySamples();
      return T;
    }
    return 0;
  }

  /// Return all the samples collected in the body of the function.
  const BodySampleMap &getBodySamples() const { return BodySamples; }

  /// Return all the callsite samples collected in the body of the function.
  const CallsiteSampleMap &getCallsiteSamples() const {
    return CallsiteSamples;
  }

  /// Merge the samples in \p Other into this one.
  /// Optionally scale samples by \p Weight.
  sampleprof_error merge(const FunctionSamples &Other, uint64_t Weight = 1) {
    sampleprof_error Result = sampleprof_error::success;
    Name = Other.getName();
    MergeResult(Result, addTotalSamples(Other.getTotalSamples(), Weight));
    MergeResult(Result, addHeadSamples(Other.getHeadSamples(), Weight));
    for (const auto &I : Other.getBodySamples()) {
      const LineLocation &Loc = I.first;
      const SampleRecord &Rec = I.second;
      MergeResult(Result, BodySamples[Loc].merge(Rec, Weight));
    }
    for (const auto &I : Other.getCallsiteSamples()) {
      const LineLocation &Loc = I.first;
      FunctionSamplesMap &FSMap = functionSamplesAt(Loc);
      for (const auto &Rec : I.second)
        MergeResult(Result, FSMap[Rec.first].merge(Rec.second, Weight));
    }
    return Result;
  }

  /// Recursively traverses all children, if the total sample count of the
  /// corresponding function is no less than \p Threshold, add its corresponding
  /// GUID to \p S. Also traverse the BodySamples to add hot CallTarget's GUID
  /// to \p S.
  void findInlinedFunctions(DenseSet<GlobalValue::GUID> &S, const Module *M,
                            uint64_t Threshold) const {
    if (TotalSamples <= Threshold)
      return;
    S.insert(getGUID(Name));
    // Import hot CallTargets, which may not be available in IR because full
    // profile annotation cannot be done until backend compilation in ThinLTO.
    for (const auto &BS : BodySamples)
      for (const auto &TS : BS.second.getCallTargets())
        if (TS.getValue() > Threshold) {
          const Function *Callee =
              M->getFunction(getNameInModule(TS.getKey(), M));
          if (!Callee || !Callee->getSubprogram())
            S.insert(getGUID(TS.getKey()));
        }
    for (const auto &CS : CallsiteSamples)
      for (const auto &NameFS : CS.second)
        NameFS.second.findInlinedFunctions(S, M, Threshold);
  }

  /// Set the name of the function.
  void setName(StringRef FunctionName) { Name = FunctionName; }

  /// Return the function name.
  StringRef getName() const { return Name; }

  /// Return the original function name if it exists in Module \p M.
  StringRef getFuncNameInModule(const Module *M) const {
    return getNameInModule(Name, M);
  }

  /// Translate \p Name into its original name in Module.
  /// When the Format is not SPF_Compact_Binary, \p Name needs no translation.
  /// When the Format is SPF_Compact_Binary, \p Name in current FunctionSamples
  /// is actually GUID of the original function name. getNameInModule will
  /// translate \p Name in current FunctionSamples into its original name.
  /// If the original name doesn't exist in \p M, return empty StringRef.
  StringRef getNameInModule(StringRef Name, const Module *M) const {
    if (Format != SPF_Compact_Binary)
      return Name;
    // Expect CurrentModule to be initialized by GUIDToFuncNameMapper.
    if (M != CurrentModule)
      llvm_unreachable("Input Module should be the same as CurrentModule");
    auto iter = GUIDToFuncNameMap.find(std::stoull(Name.data()));
    if (iter == GUIDToFuncNameMap.end())
      return StringRef();
    return iter->second;
  }

  /// Returns the line offset to the start line of the subprogram.
  /// We assume that a single function will not exceed 65535 LOC.
  static unsigned getOffset(const DILocation *DIL);

  /// Get the FunctionSamples of the inline instance where DIL originates
  /// from.
  ///
  /// The FunctionSamples of the instruction (Machine or IR) associated to
  /// \p DIL is the inlined instance in which that instruction is coming from.
  /// We traverse the inline stack of that instruction, and match it with the
  /// tree nodes in the profile.
  ///
  /// \returns the FunctionSamples pointer to the inlined instance.
  const FunctionSamples *findFunctionSamples(const DILocation *DIL) const;

  static SampleProfileFormat Format;
  /// GUIDToFuncNameMap saves the mapping from GUID to the symbol name, for
  /// all the function symbols defined or declared in CurrentModule.
  static DenseMap<uint64_t, StringRef> GUIDToFuncNameMap;
  static Module *CurrentModule;

  class GUIDToFuncNameMapper {
  public:
    GUIDToFuncNameMapper(Module &M) {
      if (Format != SPF_Compact_Binary)
        return;

      for (const auto &F : M) {
        StringRef OrigName = F.getName();
        GUIDToFuncNameMap.insert({Function::getGUID(OrigName), OrigName});
        /// Local to global var promotion used by optimization like thinlto
        /// will rename the var and add suffix like ".llvm.xxx" to the
        /// original local name. In sample profile, the suffixes of function
        /// names are all stripped. Since it is possible that the mapper is
        /// built in post-thin-link phase and var promotion has been done,
        /// we need to add the substring of function name without the suffix
        /// into the GUIDToFuncNameMap.
        auto pos = OrigName.find('.');
        if (pos != StringRef::npos) {
          StringRef NewName = OrigName.substr(0, pos);
          GUIDToFuncNameMap.insert({Function::getGUID(NewName), NewName});
        }
      }
      CurrentModule = &M;
    }

    ~GUIDToFuncNameMapper() {
      if (Format != SPF_Compact_Binary)
        return;

      GUIDToFuncNameMap.clear();
      CurrentModule = nullptr;
    }
  };

  // Assume the input \p Name is a name coming from FunctionSamples itself.
  // If the format is SPF_Compact_Binary, the name is already a GUID and we
  // don't want to return the GUID of GUID.
  static uint64_t getGUID(StringRef Name) {
    return (Format == SPF_Compact_Binary) ? std::stoull(Name.data())
                                          : Function::getGUID(Name);
  }

private:
  /// Mangled name of the function.
  StringRef Name;

  /// Total number of samples collected inside this function.
  ///
  /// Samples are cumulative, they include all the samples collected
  /// inside this function and all its inlined callees.
  uint64_t TotalSamples = 0;

  /// Total number of samples collected at the head of the function.
  /// This is an approximation of the number of calls made to this function
  /// at runtime.
  uint64_t TotalHeadSamples = 0;

  /// Map instruction locations to collected samples.
  ///
  /// Each entry in this map contains the number of samples
  /// collected at the corresponding line offset. All line locations
  /// are an offset from the start of the function.
  BodySampleMap BodySamples;

  /// Map call sites to collected samples for the called function.
  ///
  /// Each entry in this map corresponds to all the samples
  /// collected for the inlined function call at the given
  /// location. For example, given:
  ///
  ///     void foo() {
  ///  1    bar();
  ///  ...
  ///  8    baz();
  ///     }
  ///
  /// If the bar() and baz() calls were inlined inside foo(), this
  /// map will contain two entries.  One for all the samples collected
  /// in the call to bar() at line offset 1, the other for all the samples
  /// collected in the call to baz() at line offset 8.
  CallsiteSampleMap CallsiteSamples;
};

raw_ostream &operator<<(raw_ostream &OS, const FunctionSamples &FS);

/// Sort a LocationT->SampleT map by LocationT.
///
/// It produces a sorted list of <LocationT, SampleT> records by ascending
/// order of LocationT.
template <class LocationT, class SampleT> class SampleSorter {
public:
  using SamplesWithLoc = std::pair<const LocationT, SampleT>;
  using SamplesWithLocList = SmallVector<const SamplesWithLoc *, 20>;

  SampleSorter(const std::map<LocationT, SampleT> &Samples) {
    for (const auto &I : Samples)
      V.push_back(&I);
    std::stable_sort(V.begin(), V.end(),
                     [](const SamplesWithLoc *A, const SamplesWithLoc *B) {
                       return A->first < B->first;
                     });
  }

  const SamplesWithLocList &get() const { return V; }

private:
  SamplesWithLocList V;
};

} // end namespace sampleprof
} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROF_H
