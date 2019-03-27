//===- CoverageMapping.cpp - Code coverage mapping support ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for clang's and llvm's instrumentation based
// code coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/Coverage/CoverageMappingReader.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;
using namespace coverage;

#define DEBUG_TYPE "coverage-mapping"

Counter CounterExpressionBuilder::get(const CounterExpression &E) {
  auto It = ExpressionIndices.find(E);
  if (It != ExpressionIndices.end())
    return Counter::getExpression(It->second);
  unsigned I = Expressions.size();
  Expressions.push_back(E);
  ExpressionIndices[E] = I;
  return Counter::getExpression(I);
}

void CounterExpressionBuilder::extractTerms(Counter C, int Factor,
                                            SmallVectorImpl<Term> &Terms) {
  switch (C.getKind()) {
  case Counter::Zero:
    break;
  case Counter::CounterValueReference:
    Terms.emplace_back(C.getCounterID(), Factor);
    break;
  case Counter::Expression:
    const auto &E = Expressions[C.getExpressionID()];
    extractTerms(E.LHS, Factor, Terms);
    extractTerms(
        E.RHS, E.Kind == CounterExpression::Subtract ? -Factor : Factor, Terms);
    break;
  }
}

Counter CounterExpressionBuilder::simplify(Counter ExpressionTree) {
  // Gather constant terms.
  SmallVector<Term, 32> Terms;
  extractTerms(ExpressionTree, +1, Terms);

  // If there are no terms, this is just a zero. The algorithm below assumes at
  // least one term.
  if (Terms.size() == 0)
    return Counter::getZero();

  // Group the terms by counter ID.
  llvm::sort(Terms, [](const Term &LHS, const Term &RHS) {
    return LHS.CounterID < RHS.CounterID;
  });

  // Combine terms by counter ID to eliminate counters that sum to zero.
  auto Prev = Terms.begin();
  for (auto I = Prev + 1, E = Terms.end(); I != E; ++I) {
    if (I->CounterID == Prev->CounterID) {
      Prev->Factor += I->Factor;
      continue;
    }
    ++Prev;
    *Prev = *I;
  }
  Terms.erase(++Prev, Terms.end());

  Counter C;
  // Create additions. We do this before subtractions to avoid constructs like
  // ((0 - X) + Y), as opposed to (Y - X).
  for (auto T : Terms) {
    if (T.Factor <= 0)
      continue;
    for (int I = 0; I < T.Factor; ++I)
      if (C.isZero())
        C = Counter::getCounter(T.CounterID);
      else
        C = get(CounterExpression(CounterExpression::Add, C,
                                  Counter::getCounter(T.CounterID)));
  }

  // Create subtractions.
  for (auto T : Terms) {
    if (T.Factor >= 0)
      continue;
    for (int I = 0; I < -T.Factor; ++I)
      C = get(CounterExpression(CounterExpression::Subtract, C,
                                Counter::getCounter(T.CounterID)));
  }
  return C;
}

Counter CounterExpressionBuilder::add(Counter LHS, Counter RHS) {
  return simplify(get(CounterExpression(CounterExpression::Add, LHS, RHS)));
}

Counter CounterExpressionBuilder::subtract(Counter LHS, Counter RHS) {
  return simplify(
      get(CounterExpression(CounterExpression::Subtract, LHS, RHS)));
}

void CounterMappingContext::dump(const Counter &C, raw_ostream &OS) const {
  switch (C.getKind()) {
  case Counter::Zero:
    OS << '0';
    return;
  case Counter::CounterValueReference:
    OS << '#' << C.getCounterID();
    break;
  case Counter::Expression: {
    if (C.getExpressionID() >= Expressions.size())
      return;
    const auto &E = Expressions[C.getExpressionID()];
    OS << '(';
    dump(E.LHS, OS);
    OS << (E.Kind == CounterExpression::Subtract ? " - " : " + ");
    dump(E.RHS, OS);
    OS << ')';
    break;
  }
  }
  if (CounterValues.empty())
    return;
  Expected<int64_t> Value = evaluate(C);
  if (auto E = Value.takeError()) {
    consumeError(std::move(E));
    return;
  }
  OS << '[' << *Value << ']';
}

Expected<int64_t> CounterMappingContext::evaluate(const Counter &C) const {
  switch (C.getKind()) {
  case Counter::Zero:
    return 0;
  case Counter::CounterValueReference:
    if (C.getCounterID() >= CounterValues.size())
      return errorCodeToError(errc::argument_out_of_domain);
    return CounterValues[C.getCounterID()];
  case Counter::Expression: {
    if (C.getExpressionID() >= Expressions.size())
      return errorCodeToError(errc::argument_out_of_domain);
    const auto &E = Expressions[C.getExpressionID()];
    Expected<int64_t> LHS = evaluate(E.LHS);
    if (!LHS)
      return LHS;
    Expected<int64_t> RHS = evaluate(E.RHS);
    if (!RHS)
      return RHS;
    return E.Kind == CounterExpression::Subtract ? *LHS - *RHS : *LHS + *RHS;
  }
  }
  llvm_unreachable("Unhandled CounterKind");
}

void FunctionRecordIterator::skipOtherFiles() {
  while (Current != Records.end() && !Filename.empty() &&
         Filename != Current->Filenames[0])
    ++Current;
  if (Current == Records.end())
    *this = FunctionRecordIterator();
}

Error CoverageMapping::loadFunctionRecord(
    const CoverageMappingRecord &Record,
    IndexedInstrProfReader &ProfileReader) {
  StringRef OrigFuncName = Record.FunctionName;
  if (OrigFuncName.empty())
    return make_error<CoverageMapError>(coveragemap_error::malformed);

  if (Record.Filenames.empty())
    OrigFuncName = getFuncNameWithoutPrefix(OrigFuncName);
  else
    OrigFuncName = getFuncNameWithoutPrefix(OrigFuncName, Record.Filenames[0]);

  CounterMappingContext Ctx(Record.Expressions);

  std::vector<uint64_t> Counts;
  if (Error E = ProfileReader.getFunctionCounts(Record.FunctionName,
                                                Record.FunctionHash, Counts)) {
    instrprof_error IPE = InstrProfError::take(std::move(E));
    if (IPE == instrprof_error::hash_mismatch) {
      FuncHashMismatches.emplace_back(Record.FunctionName, Record.FunctionHash);
      return Error::success();
    } else if (IPE != instrprof_error::unknown_function)
      return make_error<InstrProfError>(IPE);
    Counts.assign(Record.MappingRegions.size(), 0);
  }
  Ctx.setCounts(Counts);

  assert(!Record.MappingRegions.empty() && "Function has no regions");

  // This coverage record is a zero region for a function that's unused in
  // some TU, but used in a different TU. Ignore it. The coverage maps from the
  // the other TU will either be loaded (providing full region counts) or they
  // won't (in which case we don't unintuitively report functions as uncovered
  // when they have non-zero counts in the profile).
  if (Record.MappingRegions.size() == 1 &&
      Record.MappingRegions[0].Count.isZero() && Counts[0] > 0)
    return Error::success();

  FunctionRecord Function(OrigFuncName, Record.Filenames);
  for (const auto &Region : Record.MappingRegions) {
    Expected<int64_t> ExecutionCount = Ctx.evaluate(Region.Count);
    if (auto E = ExecutionCount.takeError()) {
      consumeError(std::move(E));
      return Error::success();
    }
    Function.pushRegion(Region, *ExecutionCount);
  }

  // Don't create records for (filenames, function) pairs we've already seen.
  auto FilenamesHash = hash_combine_range(Record.Filenames.begin(),
                                          Record.Filenames.end());
  if (!RecordProvenance[FilenamesHash].insert(hash_value(OrigFuncName)).second)
    return Error::success();

  Functions.push_back(std::move(Function));
  return Error::success();
}

Expected<std::unique_ptr<CoverageMapping>> CoverageMapping::load(
    ArrayRef<std::unique_ptr<CoverageMappingReader>> CoverageReaders,
    IndexedInstrProfReader &ProfileReader) {
  auto Coverage = std::unique_ptr<CoverageMapping>(new CoverageMapping());

  for (const auto &CoverageReader : CoverageReaders) {
    for (auto RecordOrErr : *CoverageReader) {
      if (Error E = RecordOrErr.takeError())
        return std::move(E);
      const auto &Record = *RecordOrErr;
      if (Error E = Coverage->loadFunctionRecord(Record, ProfileReader))
        return std::move(E);
    }
  }

  return std::move(Coverage);
}

Expected<std::unique_ptr<CoverageMapping>>
CoverageMapping::load(ArrayRef<StringRef> ObjectFilenames,
                      StringRef ProfileFilename, ArrayRef<StringRef> Arches) {
  auto ProfileReaderOrErr = IndexedInstrProfReader::create(ProfileFilename);
  if (Error E = ProfileReaderOrErr.takeError())
    return std::move(E);
  auto ProfileReader = std::move(ProfileReaderOrErr.get());

  SmallVector<std::unique_ptr<CoverageMappingReader>, 4> Readers;
  SmallVector<std::unique_ptr<MemoryBuffer>, 4> Buffers;
  for (const auto &File : llvm::enumerate(ObjectFilenames)) {
    auto CovMappingBufOrErr = MemoryBuffer::getFileOrSTDIN(File.value());
    if (std::error_code EC = CovMappingBufOrErr.getError())
      return errorCodeToError(EC);
    StringRef Arch = Arches.empty() ? StringRef() : Arches[File.index()];
    auto CoverageReaderOrErr =
        BinaryCoverageReader::create(CovMappingBufOrErr.get(), Arch);
    if (Error E = CoverageReaderOrErr.takeError())
      return std::move(E);
    Readers.push_back(std::move(CoverageReaderOrErr.get()));
    Buffers.push_back(std::move(CovMappingBufOrErr.get()));
  }
  return load(Readers, *ProfileReader);
}

namespace {

/// Distributes functions into instantiation sets.
///
/// An instantiation set is a collection of functions that have the same source
/// code, ie, template functions specializations.
class FunctionInstantiationSetCollector {
  using MapT = std::map<LineColPair, std::vector<const FunctionRecord *>>;
  MapT InstantiatedFunctions;

public:
  void insert(const FunctionRecord &Function, unsigned FileID) {
    auto I = Function.CountedRegions.begin(), E = Function.CountedRegions.end();
    while (I != E && I->FileID != FileID)
      ++I;
    assert(I != E && "function does not cover the given file");
    auto &Functions = InstantiatedFunctions[I->startLoc()];
    Functions.push_back(&Function);
  }

  MapT::iterator begin() { return InstantiatedFunctions.begin(); }
  MapT::iterator end() { return InstantiatedFunctions.end(); }
};

class SegmentBuilder {
  std::vector<CoverageSegment> &Segments;
  SmallVector<const CountedRegion *, 8> ActiveRegions;

  SegmentBuilder(std::vector<CoverageSegment> &Segments) : Segments(Segments) {}

  /// Emit a segment with the count from \p Region starting at \p StartLoc.
  //
  /// \p IsRegionEntry: The segment is at the start of a new non-gap region.
  /// \p EmitSkippedRegion: The segment must be emitted as a skipped region.
  void startSegment(const CountedRegion &Region, LineColPair StartLoc,
                    bool IsRegionEntry, bool EmitSkippedRegion = false) {
    bool HasCount = !EmitSkippedRegion &&
                    (Region.Kind != CounterMappingRegion::SkippedRegion);

    // If the new segment wouldn't affect coverage rendering, skip it.
    if (!Segments.empty() && !IsRegionEntry && !EmitSkippedRegion) {
      const auto &Last = Segments.back();
      if (Last.HasCount == HasCount && Last.Count == Region.ExecutionCount &&
          !Last.IsRegionEntry)
        return;
    }

    if (HasCount)
      Segments.emplace_back(StartLoc.first, StartLoc.second,
                            Region.ExecutionCount, IsRegionEntry,
                            Region.Kind == CounterMappingRegion::GapRegion);
    else
      Segments.emplace_back(StartLoc.first, StartLoc.second, IsRegionEntry);

    LLVM_DEBUG({
      const auto &Last = Segments.back();
      dbgs() << "Segment at " << Last.Line << ":" << Last.Col
             << " (count = " << Last.Count << ")"
             << (Last.IsRegionEntry ? ", RegionEntry" : "")
             << (!Last.HasCount ? ", Skipped" : "")
             << (Last.IsGapRegion ? ", Gap" : "") << "\n";
    });
  }

  /// Emit segments for active regions which end before \p Loc.
  ///
  /// \p Loc: The start location of the next region. If None, all active
  /// regions are completed.
  /// \p FirstCompletedRegion: Index of the first completed region.
  void completeRegionsUntil(Optional<LineColPair> Loc,
                            unsigned FirstCompletedRegion) {
    // Sort the completed regions by end location. This makes it simple to
    // emit closing segments in sorted order.
    auto CompletedRegionsIt = ActiveRegions.begin() + FirstCompletedRegion;
    std::stable_sort(CompletedRegionsIt, ActiveRegions.end(),
                      [](const CountedRegion *L, const CountedRegion *R) {
                        return L->endLoc() < R->endLoc();
                      });

    // Emit segments for all completed regions.
    for (unsigned I = FirstCompletedRegion + 1, E = ActiveRegions.size(); I < E;
         ++I) {
      const auto *CompletedRegion = ActiveRegions[I];
      assert((!Loc || CompletedRegion->endLoc() <= *Loc) &&
             "Completed region ends after start of new region");

      const auto *PrevCompletedRegion = ActiveRegions[I - 1];
      auto CompletedSegmentLoc = PrevCompletedRegion->endLoc();

      // Don't emit any more segments if they start where the new region begins.
      if (Loc && CompletedSegmentLoc == *Loc)
        break;

      // Don't emit a segment if the next completed region ends at the same
      // location as this one.
      if (CompletedSegmentLoc == CompletedRegion->endLoc())
        continue;

      // Use the count from the last completed region which ends at this loc.
      for (unsigned J = I + 1; J < E; ++J)
        if (CompletedRegion->endLoc() == ActiveRegions[J]->endLoc())
          CompletedRegion = ActiveRegions[J];

      startSegment(*CompletedRegion, CompletedSegmentLoc, false);
    }

    auto Last = ActiveRegions.back();
    if (FirstCompletedRegion && Last->endLoc() != *Loc) {
      // If there's a gap after the end of the last completed region and the
      // start of the new region, use the last active region to fill the gap.
      startSegment(*ActiveRegions[FirstCompletedRegion - 1], Last->endLoc(),
                   false);
    } else if (!FirstCompletedRegion && (!Loc || *Loc != Last->endLoc())) {
      // Emit a skipped segment if there are no more active regions. This
      // ensures that gaps between functions are marked correctly.
      startSegment(*Last, Last->endLoc(), false, true);
    }

    // Pop the completed regions.
    ActiveRegions.erase(CompletedRegionsIt, ActiveRegions.end());
  }

  void buildSegmentsImpl(ArrayRef<CountedRegion> Regions) {
    for (const auto &CR : enumerate(Regions)) {
      auto CurStartLoc = CR.value().startLoc();

      // Active regions which end before the current region need to be popped.
      auto CompletedRegions =
          std::stable_partition(ActiveRegions.begin(), ActiveRegions.end(),
                                [&](const CountedRegion *Region) {
                                  return !(Region->endLoc() <= CurStartLoc);
                                });
      if (CompletedRegions != ActiveRegions.end()) {
        unsigned FirstCompletedRegion =
            std::distance(ActiveRegions.begin(), CompletedRegions);
        completeRegionsUntil(CurStartLoc, FirstCompletedRegion);
      }

      bool GapRegion = CR.value().Kind == CounterMappingRegion::GapRegion;

      // Try to emit a segment for the current region.
      if (CurStartLoc == CR.value().endLoc()) {
        // Avoid making zero-length regions active. If it's the last region,
        // emit a skipped segment. Otherwise use its predecessor's count.
        const bool Skipped = (CR.index() + 1) == Regions.size();
        startSegment(ActiveRegions.empty() ? CR.value() : *ActiveRegions.back(),
                     CurStartLoc, !GapRegion, Skipped);
        continue;
      }
      if (CR.index() + 1 == Regions.size() ||
          CurStartLoc != Regions[CR.index() + 1].startLoc()) {
        // Emit a segment if the next region doesn't start at the same location
        // as this one.
        startSegment(CR.value(), CurStartLoc, !GapRegion);
      }

      // This region is active (i.e not completed).
      ActiveRegions.push_back(&CR.value());
    }

    // Complete any remaining active regions.
    if (!ActiveRegions.empty())
      completeRegionsUntil(None, 0);
  }

  /// Sort a nested sequence of regions from a single file.
  static void sortNestedRegions(MutableArrayRef<CountedRegion> Regions) {
    llvm::sort(Regions, [](const CountedRegion &LHS, const CountedRegion &RHS) {
      if (LHS.startLoc() != RHS.startLoc())
        return LHS.startLoc() < RHS.startLoc();
      if (LHS.endLoc() != RHS.endLoc())
        // When LHS completely contains RHS, we sort LHS first.
        return RHS.endLoc() < LHS.endLoc();
      // If LHS and RHS cover the same area, we need to sort them according
      // to their kinds so that the most suitable region will become "active"
      // in combineRegions(). Because we accumulate counter values only from
      // regions of the same kind as the first region of the area, prefer
      // CodeRegion to ExpansionRegion and ExpansionRegion to SkippedRegion.
      static_assert(CounterMappingRegion::CodeRegion <
                            CounterMappingRegion::ExpansionRegion &&
                        CounterMappingRegion::ExpansionRegion <
                            CounterMappingRegion::SkippedRegion,
                    "Unexpected order of region kind values");
      return LHS.Kind < RHS.Kind;
    });
  }

  /// Combine counts of regions which cover the same area.
  static ArrayRef<CountedRegion>
  combineRegions(MutableArrayRef<CountedRegion> Regions) {
    if (Regions.empty())
      return Regions;
    auto Active = Regions.begin();
    auto End = Regions.end();
    for (auto I = Regions.begin() + 1; I != End; ++I) {
      if (Active->startLoc() != I->startLoc() ||
          Active->endLoc() != I->endLoc()) {
        // Shift to the next region.
        ++Active;
        if (Active != I)
          *Active = *I;
        continue;
      }
      // Merge duplicate region.
      // If CodeRegions and ExpansionRegions cover the same area, it's probably
      // a macro which is fully expanded to another macro. In that case, we need
      // to accumulate counts only from CodeRegions, or else the area will be
      // counted twice.
      // On the other hand, a macro may have a nested macro in its body. If the
      // outer macro is used several times, the ExpansionRegion for the nested
      // macro will also be added several times. These ExpansionRegions cover
      // the same source locations and have to be combined to reach the correct
      // value for that area.
      // We add counts of the regions of the same kind as the active region
      // to handle the both situations.
      if (I->Kind == Active->Kind)
        Active->ExecutionCount += I->ExecutionCount;
    }
    return Regions.drop_back(std::distance(++Active, End));
  }

public:
  /// Build a sorted list of CoverageSegments from a list of Regions.
  static std::vector<CoverageSegment>
  buildSegments(MutableArrayRef<CountedRegion> Regions) {
    std::vector<CoverageSegment> Segments;
    SegmentBuilder Builder(Segments);

    sortNestedRegions(Regions);
    ArrayRef<CountedRegion> CombinedRegions = combineRegions(Regions);

    LLVM_DEBUG({
      dbgs() << "Combined regions:\n";
      for (const auto &CR : CombinedRegions)
        dbgs() << "  " << CR.LineStart << ":" << CR.ColumnStart << " -> "
               << CR.LineEnd << ":" << CR.ColumnEnd
               << " (count=" << CR.ExecutionCount << ")\n";
    });

    Builder.buildSegmentsImpl(CombinedRegions);

#ifndef NDEBUG
    for (unsigned I = 1, E = Segments.size(); I < E; ++I) {
      const auto &L = Segments[I - 1];
      const auto &R = Segments[I];
      if (!(L.Line < R.Line) && !(L.Line == R.Line && L.Col < R.Col)) {
        LLVM_DEBUG(dbgs() << " ! Segment " << L.Line << ":" << L.Col
                          << " followed by " << R.Line << ":" << R.Col << "\n");
        assert(false && "Coverage segments not unique or sorted");
      }
    }
#endif

    return Segments;
  }
};

} // end anonymous namespace

std::vector<StringRef> CoverageMapping::getUniqueSourceFiles() const {
  std::vector<StringRef> Filenames;
  for (const auto &Function : getCoveredFunctions())
    Filenames.insert(Filenames.end(), Function.Filenames.begin(),
                     Function.Filenames.end());
  llvm::sort(Filenames);
  auto Last = std::unique(Filenames.begin(), Filenames.end());
  Filenames.erase(Last, Filenames.end());
  return Filenames;
}

static SmallBitVector gatherFileIDs(StringRef SourceFile,
                                    const FunctionRecord &Function) {
  SmallBitVector FilenameEquivalence(Function.Filenames.size(), false);
  for (unsigned I = 0, E = Function.Filenames.size(); I < E; ++I)
    if (SourceFile == Function.Filenames[I])
      FilenameEquivalence[I] = true;
  return FilenameEquivalence;
}

/// Return the ID of the file where the definition of the function is located.
static Optional<unsigned> findMainViewFileID(const FunctionRecord &Function) {
  SmallBitVector IsNotExpandedFile(Function.Filenames.size(), true);
  for (const auto &CR : Function.CountedRegions)
    if (CR.Kind == CounterMappingRegion::ExpansionRegion)
      IsNotExpandedFile[CR.ExpandedFileID] = false;
  int I = IsNotExpandedFile.find_first();
  if (I == -1)
    return None;
  return I;
}

/// Check if SourceFile is the file that contains the definition of
/// the Function. Return the ID of the file in that case or None otherwise.
static Optional<unsigned> findMainViewFileID(StringRef SourceFile,
                                             const FunctionRecord &Function) {
  Optional<unsigned> I = findMainViewFileID(Function);
  if (I && SourceFile == Function.Filenames[*I])
    return I;
  return None;
}

static bool isExpansion(const CountedRegion &R, unsigned FileID) {
  return R.Kind == CounterMappingRegion::ExpansionRegion && R.FileID == FileID;
}

CoverageData CoverageMapping::getCoverageForFile(StringRef Filename) const {
  CoverageData FileCoverage(Filename);
  std::vector<CountedRegion> Regions;

  for (const auto &Function : Functions) {
    auto MainFileID = findMainViewFileID(Filename, Function);
    auto FileIDs = gatherFileIDs(Filename, Function);
    for (const auto &CR : Function.CountedRegions)
      if (FileIDs.test(CR.FileID)) {
        Regions.push_back(CR);
        if (MainFileID && isExpansion(CR, *MainFileID))
          FileCoverage.Expansions.emplace_back(CR, Function);
      }
  }

  LLVM_DEBUG(dbgs() << "Emitting segments for file: " << Filename << "\n");
  FileCoverage.Segments = SegmentBuilder::buildSegments(Regions);

  return FileCoverage;
}

std::vector<InstantiationGroup>
CoverageMapping::getInstantiationGroups(StringRef Filename) const {
  FunctionInstantiationSetCollector InstantiationSetCollector;
  for (const auto &Function : Functions) {
    auto MainFileID = findMainViewFileID(Filename, Function);
    if (!MainFileID)
      continue;
    InstantiationSetCollector.insert(Function, *MainFileID);
  }

  std::vector<InstantiationGroup> Result;
  for (auto &InstantiationSet : InstantiationSetCollector) {
    InstantiationGroup IG{InstantiationSet.first.first,
                          InstantiationSet.first.second,
                          std::move(InstantiationSet.second)};
    Result.emplace_back(std::move(IG));
  }
  return Result;
}

CoverageData
CoverageMapping::getCoverageForFunction(const FunctionRecord &Function) const {
  auto MainFileID = findMainViewFileID(Function);
  if (!MainFileID)
    return CoverageData();

  CoverageData FunctionCoverage(Function.Filenames[*MainFileID]);
  std::vector<CountedRegion> Regions;
  for (const auto &CR : Function.CountedRegions)
    if (CR.FileID == *MainFileID) {
      Regions.push_back(CR);
      if (isExpansion(CR, *MainFileID))
        FunctionCoverage.Expansions.emplace_back(CR, Function);
    }

  LLVM_DEBUG(dbgs() << "Emitting segments for function: " << Function.Name
                    << "\n");
  FunctionCoverage.Segments = SegmentBuilder::buildSegments(Regions);

  return FunctionCoverage;
}

CoverageData CoverageMapping::getCoverageForExpansion(
    const ExpansionRecord &Expansion) const {
  CoverageData ExpansionCoverage(
      Expansion.Function.Filenames[Expansion.FileID]);
  std::vector<CountedRegion> Regions;
  for (const auto &CR : Expansion.Function.CountedRegions)
    if (CR.FileID == Expansion.FileID) {
      Regions.push_back(CR);
      if (isExpansion(CR, Expansion.FileID))
        ExpansionCoverage.Expansions.emplace_back(CR, Expansion.Function);
    }

  LLVM_DEBUG(dbgs() << "Emitting segments for expansion of file "
                    << Expansion.FileID << "\n");
  ExpansionCoverage.Segments = SegmentBuilder::buildSegments(Regions);

  return ExpansionCoverage;
}

LineCoverageStats::LineCoverageStats(
    ArrayRef<const CoverageSegment *> LineSegments,
    const CoverageSegment *WrappedSegment, unsigned Line)
    : ExecutionCount(0), HasMultipleRegions(false), Mapped(false), Line(Line),
      LineSegments(LineSegments), WrappedSegment(WrappedSegment) {
  // Find the minimum number of regions which start in this line.
  unsigned MinRegionCount = 0;
  auto isStartOfRegion = [](const CoverageSegment *S) {
    return !S->IsGapRegion && S->HasCount && S->IsRegionEntry;
  };
  for (unsigned I = 0; I < LineSegments.size() && MinRegionCount < 2; ++I)
    if (isStartOfRegion(LineSegments[I]))
      ++MinRegionCount;

  bool StartOfSkippedRegion = !LineSegments.empty() &&
                              !LineSegments.front()->HasCount &&
                              LineSegments.front()->IsRegionEntry;

  HasMultipleRegions = MinRegionCount > 1;
  Mapped =
      !StartOfSkippedRegion &&
      ((WrappedSegment && WrappedSegment->HasCount) || (MinRegionCount > 0));

  if (!Mapped)
    return;

  // Pick the max count from the non-gap, region entry segments and the
  // wrapped count.
  if (WrappedSegment)
    ExecutionCount = WrappedSegment->Count;
  if (!MinRegionCount)
    return;
  for (const auto *LS : LineSegments)
    if (isStartOfRegion(LS))
      ExecutionCount = std::max(ExecutionCount, LS->Count);
}

LineCoverageIterator &LineCoverageIterator::operator++() {
  if (Next == CD.end()) {
    Stats = LineCoverageStats();
    Ended = true;
    return *this;
  }
  if (Segments.size())
    WrappedSegment = Segments.back();
  Segments.clear();
  while (Next != CD.end() && Next->Line == Line)
    Segments.push_back(&*Next++);
  Stats = LineCoverageStats(Segments, WrappedSegment, Line);
  ++Line;
  return *this;
}

static std::string getCoverageMapErrString(coveragemap_error Err) {
  switch (Err) {
  case coveragemap_error::success:
    return "Success";
  case coveragemap_error::eof:
    return "End of File";
  case coveragemap_error::no_data_found:
    return "No coverage data found";
  case coveragemap_error::unsupported_version:
    return "Unsupported coverage format version";
  case coveragemap_error::truncated:
    return "Truncated coverage data";
  case coveragemap_error::malformed:
    return "Malformed coverage data";
  }
  llvm_unreachable("A value of coveragemap_error has no message.");
}

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class CoverageMappingErrorCategoryType : public std::error_category {
  const char *name() const noexcept override { return "llvm.coveragemap"; }
  std::string message(int IE) const override {
    return getCoverageMapErrString(static_cast<coveragemap_error>(IE));
  }
};

} // end anonymous namespace

std::string CoverageMapError::message() const {
  return getCoverageMapErrString(Err);
}

static ManagedStatic<CoverageMappingErrorCategoryType> ErrorCategory;

const std::error_category &llvm::coverage::coveragemap_category() {
  return *ErrorCategory;
}

char CoverageMapError::ID = 0;
