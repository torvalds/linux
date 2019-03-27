//===- LoopAccessAnalysis.cpp - Loop Access Analysis Implementation --------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The implementation for the loop memory dependence that was originally
// developed for the loop vectorizer.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "loop-accesses"

static cl::opt<unsigned, true>
VectorizationFactor("force-vector-width", cl::Hidden,
                    cl::desc("Sets the SIMD width. Zero is autoselect."),
                    cl::location(VectorizerParams::VectorizationFactor));
unsigned VectorizerParams::VectorizationFactor;

static cl::opt<unsigned, true>
VectorizationInterleave("force-vector-interleave", cl::Hidden,
                        cl::desc("Sets the vectorization interleave count. "
                                 "Zero is autoselect."),
                        cl::location(
                            VectorizerParams::VectorizationInterleave));
unsigned VectorizerParams::VectorizationInterleave;

static cl::opt<unsigned, true> RuntimeMemoryCheckThreshold(
    "runtime-memory-check-threshold", cl::Hidden,
    cl::desc("When performing memory disambiguation checks at runtime do not "
             "generate more than this number of comparisons (default = 8)."),
    cl::location(VectorizerParams::RuntimeMemoryCheckThreshold), cl::init(8));
unsigned VectorizerParams::RuntimeMemoryCheckThreshold;

/// The maximum iterations used to merge memory checks
static cl::opt<unsigned> MemoryCheckMergeThreshold(
    "memory-check-merge-threshold", cl::Hidden,
    cl::desc("Maximum number of comparisons done when trying to merge "
             "runtime memory checks. (default = 100)"),
    cl::init(100));

/// Maximum SIMD width.
const unsigned VectorizerParams::MaxVectorWidth = 64;

/// We collect dependences up to this threshold.
static cl::opt<unsigned>
    MaxDependences("max-dependences", cl::Hidden,
                   cl::desc("Maximum number of dependences collected by "
                            "loop-access analysis (default = 100)"),
                   cl::init(100));

/// This enables versioning on the strides of symbolically striding memory
/// accesses in code like the following.
///   for (i = 0; i < N; ++i)
///     A[i * Stride1] += B[i * Stride2] ...
///
/// Will be roughly translated to
///    if (Stride1 == 1 && Stride2 == 1) {
///      for (i = 0; i < N; i+=4)
///       A[i:i+3] += ...
///    } else
///      ...
static cl::opt<bool> EnableMemAccessVersioning(
    "enable-mem-access-versioning", cl::init(true), cl::Hidden,
    cl::desc("Enable symbolic stride memory access versioning"));

/// Enable store-to-load forwarding conflict detection. This option can
/// be disabled for correctness testing.
static cl::opt<bool> EnableForwardingConflictDetection(
    "store-to-load-forwarding-conflict-detection", cl::Hidden,
    cl::desc("Enable conflict detection in loop-access analysis"),
    cl::init(true));

bool VectorizerParams::isInterleaveForced() {
  return ::VectorizationInterleave.getNumOccurrences() > 0;
}

Value *llvm::stripIntegerCast(Value *V) {
  if (auto *CI = dyn_cast<CastInst>(V))
    if (CI->getOperand(0)->getType()->isIntegerTy())
      return CI->getOperand(0);
  return V;
}

const SCEV *llvm::replaceSymbolicStrideSCEV(PredicatedScalarEvolution &PSE,
                                            const ValueToValueMap &PtrToStride,
                                            Value *Ptr, Value *OrigPtr) {
  const SCEV *OrigSCEV = PSE.getSCEV(Ptr);

  // If there is an entry in the map return the SCEV of the pointer with the
  // symbolic stride replaced by one.
  ValueToValueMap::const_iterator SI =
      PtrToStride.find(OrigPtr ? OrigPtr : Ptr);
  if (SI != PtrToStride.end()) {
    Value *StrideVal = SI->second;

    // Strip casts.
    StrideVal = stripIntegerCast(StrideVal);

    ScalarEvolution *SE = PSE.getSE();
    const auto *U = cast<SCEVUnknown>(SE->getSCEV(StrideVal));
    const auto *CT =
        static_cast<const SCEVConstant *>(SE->getOne(StrideVal->getType()));

    PSE.addPredicate(*SE->getEqualPredicate(U, CT));
    auto *Expr = PSE.getSCEV(Ptr);

    LLVM_DEBUG(dbgs() << "LAA: Replacing SCEV: " << *OrigSCEV
                      << " by: " << *Expr << "\n");
    return Expr;
  }

  // Otherwise, just return the SCEV of the original pointer.
  return OrigSCEV;
}

/// Calculate Start and End points of memory access.
/// Let's assume A is the first access and B is a memory access on N-th loop
/// iteration. Then B is calculated as:
///   B = A + Step*N .
/// Step value may be positive or negative.
/// N is a calculated back-edge taken count:
///     N = (TripCount > 0) ? RoundDown(TripCount -1 , VF) : 0
/// Start and End points are calculated in the following way:
/// Start = UMIN(A, B) ; End = UMAX(A, B) + SizeOfElt,
/// where SizeOfElt is the size of single memory access in bytes.
///
/// There is no conflict when the intervals are disjoint:
/// NoConflict = (P2.Start >= P1.End) || (P1.Start >= P2.End)
void RuntimePointerChecking::insert(Loop *Lp, Value *Ptr, bool WritePtr,
                                    unsigned DepSetId, unsigned ASId,
                                    const ValueToValueMap &Strides,
                                    PredicatedScalarEvolution &PSE) {
  // Get the stride replaced scev.
  const SCEV *Sc = replaceSymbolicStrideSCEV(PSE, Strides, Ptr);
  ScalarEvolution *SE = PSE.getSE();

  const SCEV *ScStart;
  const SCEV *ScEnd;

  if (SE->isLoopInvariant(Sc, Lp))
    ScStart = ScEnd = Sc;
  else {
    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Sc);
    assert(AR && "Invalid addrec expression");
    const SCEV *Ex = PSE.getBackedgeTakenCount();

    ScStart = AR->getStart();
    ScEnd = AR->evaluateAtIteration(Ex, *SE);
    const SCEV *Step = AR->getStepRecurrence(*SE);

    // For expressions with negative step, the upper bound is ScStart and the
    // lower bound is ScEnd.
    if (const auto *CStep = dyn_cast<SCEVConstant>(Step)) {
      if (CStep->getValue()->isNegative())
        std::swap(ScStart, ScEnd);
    } else {
      // Fallback case: the step is not constant, but we can still
      // get the upper and lower bounds of the interval by using min/max
      // expressions.
      ScStart = SE->getUMinExpr(ScStart, ScEnd);
      ScEnd = SE->getUMaxExpr(AR->getStart(), ScEnd);
    }
    // Add the size of the pointed element to ScEnd.
    unsigned EltSize =
      Ptr->getType()->getPointerElementType()->getScalarSizeInBits() / 8;
    const SCEV *EltSizeSCEV = SE->getConstant(ScEnd->getType(), EltSize);
    ScEnd = SE->getAddExpr(ScEnd, EltSizeSCEV);
  }

  Pointers.emplace_back(Ptr, ScStart, ScEnd, WritePtr, DepSetId, ASId, Sc);
}

SmallVector<RuntimePointerChecking::PointerCheck, 4>
RuntimePointerChecking::generateChecks() const {
  SmallVector<PointerCheck, 4> Checks;

  for (unsigned I = 0; I < CheckingGroups.size(); ++I) {
    for (unsigned J = I + 1; J < CheckingGroups.size(); ++J) {
      const RuntimePointerChecking::CheckingPtrGroup &CGI = CheckingGroups[I];
      const RuntimePointerChecking::CheckingPtrGroup &CGJ = CheckingGroups[J];

      if (needsChecking(CGI, CGJ))
        Checks.push_back(std::make_pair(&CGI, &CGJ));
    }
  }
  return Checks;
}

void RuntimePointerChecking::generateChecks(
    MemoryDepChecker::DepCandidates &DepCands, bool UseDependencies) {
  assert(Checks.empty() && "Checks is not empty");
  groupChecks(DepCands, UseDependencies);
  Checks = generateChecks();
}

bool RuntimePointerChecking::needsChecking(const CheckingPtrGroup &M,
                                           const CheckingPtrGroup &N) const {
  for (unsigned I = 0, EI = M.Members.size(); EI != I; ++I)
    for (unsigned J = 0, EJ = N.Members.size(); EJ != J; ++J)
      if (needsChecking(M.Members[I], N.Members[J]))
        return true;
  return false;
}

/// Compare \p I and \p J and return the minimum.
/// Return nullptr in case we couldn't find an answer.
static const SCEV *getMinFromExprs(const SCEV *I, const SCEV *J,
                                   ScalarEvolution *SE) {
  const SCEV *Diff = SE->getMinusSCEV(J, I);
  const SCEVConstant *C = dyn_cast<const SCEVConstant>(Diff);

  if (!C)
    return nullptr;
  if (C->getValue()->isNegative())
    return J;
  return I;
}

bool RuntimePointerChecking::CheckingPtrGroup::addPointer(unsigned Index) {
  const SCEV *Start = RtCheck.Pointers[Index].Start;
  const SCEV *End = RtCheck.Pointers[Index].End;

  // Compare the starts and ends with the known minimum and maximum
  // of this set. We need to know how we compare against the min/max
  // of the set in order to be able to emit memchecks.
  const SCEV *Min0 = getMinFromExprs(Start, Low, RtCheck.SE);
  if (!Min0)
    return false;

  const SCEV *Min1 = getMinFromExprs(End, High, RtCheck.SE);
  if (!Min1)
    return false;

  // Update the low bound  expression if we've found a new min value.
  if (Min0 == Start)
    Low = Start;

  // Update the high bound expression if we've found a new max value.
  if (Min1 != End)
    High = End;

  Members.push_back(Index);
  return true;
}

void RuntimePointerChecking::groupChecks(
    MemoryDepChecker::DepCandidates &DepCands, bool UseDependencies) {
  // We build the groups from dependency candidates equivalence classes
  // because:
  //    - We know that pointers in the same equivalence class share
  //      the same underlying object and therefore there is a chance
  //      that we can compare pointers
  //    - We wouldn't be able to merge two pointers for which we need
  //      to emit a memcheck. The classes in DepCands are already
  //      conveniently built such that no two pointers in the same
  //      class need checking against each other.

  // We use the following (greedy) algorithm to construct the groups
  // For every pointer in the equivalence class:
  //   For each existing group:
  //   - if the difference between this pointer and the min/max bounds
  //     of the group is a constant, then make the pointer part of the
  //     group and update the min/max bounds of that group as required.

  CheckingGroups.clear();

  // If we need to check two pointers to the same underlying object
  // with a non-constant difference, we shouldn't perform any pointer
  // grouping with those pointers. This is because we can easily get
  // into cases where the resulting check would return false, even when
  // the accesses are safe.
  //
  // The following example shows this:
  // for (i = 0; i < 1000; ++i)
  //   a[5000 + i * m] = a[i] + a[i + 9000]
  //
  // Here grouping gives a check of (5000, 5000 + 1000 * m) against
  // (0, 10000) which is always false. However, if m is 1, there is no
  // dependence. Not grouping the checks for a[i] and a[i + 9000] allows
  // us to perform an accurate check in this case.
  //
  // The above case requires that we have an UnknownDependence between
  // accesses to the same underlying object. This cannot happen unless
  // FoundNonConstantDistanceDependence is set, and therefore UseDependencies
  // is also false. In this case we will use the fallback path and create
  // separate checking groups for all pointers.

  // If we don't have the dependency partitions, construct a new
  // checking pointer group for each pointer. This is also required
  // for correctness, because in this case we can have checking between
  // pointers to the same underlying object.
  if (!UseDependencies) {
    for (unsigned I = 0; I < Pointers.size(); ++I)
      CheckingGroups.push_back(CheckingPtrGroup(I, *this));
    return;
  }

  unsigned TotalComparisons = 0;

  DenseMap<Value *, unsigned> PositionMap;
  for (unsigned Index = 0; Index < Pointers.size(); ++Index)
    PositionMap[Pointers[Index].PointerValue] = Index;

  // We need to keep track of what pointers we've already seen so we
  // don't process them twice.
  SmallSet<unsigned, 2> Seen;

  // Go through all equivalence classes, get the "pointer check groups"
  // and add them to the overall solution. We use the order in which accesses
  // appear in 'Pointers' to enforce determinism.
  for (unsigned I = 0; I < Pointers.size(); ++I) {
    // We've seen this pointer before, and therefore already processed
    // its equivalence class.
    if (Seen.count(I))
      continue;

    MemoryDepChecker::MemAccessInfo Access(Pointers[I].PointerValue,
                                           Pointers[I].IsWritePtr);

    SmallVector<CheckingPtrGroup, 2> Groups;
    auto LeaderI = DepCands.findValue(DepCands.getLeaderValue(Access));

    // Because DepCands is constructed by visiting accesses in the order in
    // which they appear in alias sets (which is deterministic) and the
    // iteration order within an equivalence class member is only dependent on
    // the order in which unions and insertions are performed on the
    // equivalence class, the iteration order is deterministic.
    for (auto MI = DepCands.member_begin(LeaderI), ME = DepCands.member_end();
         MI != ME; ++MI) {
      unsigned Pointer = PositionMap[MI->getPointer()];
      bool Merged = false;
      // Mark this pointer as seen.
      Seen.insert(Pointer);

      // Go through all the existing sets and see if we can find one
      // which can include this pointer.
      for (CheckingPtrGroup &Group : Groups) {
        // Don't perform more than a certain amount of comparisons.
        // This should limit the cost of grouping the pointers to something
        // reasonable.  If we do end up hitting this threshold, the algorithm
        // will create separate groups for all remaining pointers.
        if (TotalComparisons > MemoryCheckMergeThreshold)
          break;

        TotalComparisons++;

        if (Group.addPointer(Pointer)) {
          Merged = true;
          break;
        }
      }

      if (!Merged)
        // We couldn't add this pointer to any existing set or the threshold
        // for the number of comparisons has been reached. Create a new group
        // to hold the current pointer.
        Groups.push_back(CheckingPtrGroup(Pointer, *this));
    }

    // We've computed the grouped checks for this partition.
    // Save the results and continue with the next one.
    llvm::copy(Groups, std::back_inserter(CheckingGroups));
  }
}

bool RuntimePointerChecking::arePointersInSamePartition(
    const SmallVectorImpl<int> &PtrToPartition, unsigned PtrIdx1,
    unsigned PtrIdx2) {
  return (PtrToPartition[PtrIdx1] != -1 &&
          PtrToPartition[PtrIdx1] == PtrToPartition[PtrIdx2]);
}

bool RuntimePointerChecking::needsChecking(unsigned I, unsigned J) const {
  const PointerInfo &PointerI = Pointers[I];
  const PointerInfo &PointerJ = Pointers[J];

  // No need to check if two readonly pointers intersect.
  if (!PointerI.IsWritePtr && !PointerJ.IsWritePtr)
    return false;

  // Only need to check pointers between two different dependency sets.
  if (PointerI.DependencySetId == PointerJ.DependencySetId)
    return false;

  // Only need to check pointers in the same alias set.
  if (PointerI.AliasSetId != PointerJ.AliasSetId)
    return false;

  return true;
}

void RuntimePointerChecking::printChecks(
    raw_ostream &OS, const SmallVectorImpl<PointerCheck> &Checks,
    unsigned Depth) const {
  unsigned N = 0;
  for (const auto &Check : Checks) {
    const auto &First = Check.first->Members, &Second = Check.second->Members;

    OS.indent(Depth) << "Check " << N++ << ":\n";

    OS.indent(Depth + 2) << "Comparing group (" << Check.first << "):\n";
    for (unsigned K = 0; K < First.size(); ++K)
      OS.indent(Depth + 2) << *Pointers[First[K]].PointerValue << "\n";

    OS.indent(Depth + 2) << "Against group (" << Check.second << "):\n";
    for (unsigned K = 0; K < Second.size(); ++K)
      OS.indent(Depth + 2) << *Pointers[Second[K]].PointerValue << "\n";
  }
}

void RuntimePointerChecking::print(raw_ostream &OS, unsigned Depth) const {

  OS.indent(Depth) << "Run-time memory checks:\n";
  printChecks(OS, Checks, Depth);

  OS.indent(Depth) << "Grouped accesses:\n";
  for (unsigned I = 0; I < CheckingGroups.size(); ++I) {
    const auto &CG = CheckingGroups[I];

    OS.indent(Depth + 2) << "Group " << &CG << ":\n";
    OS.indent(Depth + 4) << "(Low: " << *CG.Low << " High: " << *CG.High
                         << ")\n";
    for (unsigned J = 0; J < CG.Members.size(); ++J) {
      OS.indent(Depth + 6) << "Member: " << *Pointers[CG.Members[J]].Expr
                           << "\n";
    }
  }
}

namespace {

/// Analyses memory accesses in a loop.
///
/// Checks whether run time pointer checks are needed and builds sets for data
/// dependence checking.
class AccessAnalysis {
public:
  /// Read or write access location.
  typedef PointerIntPair<Value *, 1, bool> MemAccessInfo;
  typedef SmallVector<MemAccessInfo, 8> MemAccessInfoList;

  AccessAnalysis(const DataLayout &Dl, Loop *TheLoop, AliasAnalysis *AA,
                 LoopInfo *LI, MemoryDepChecker::DepCandidates &DA,
                 PredicatedScalarEvolution &PSE)
      : DL(Dl), TheLoop(TheLoop), AST(*AA), LI(LI), DepCands(DA),
        IsRTCheckAnalysisNeeded(false), PSE(PSE) {}

  /// Register a load  and whether it is only read from.
  void addLoad(MemoryLocation &Loc, bool IsReadOnly) {
    Value *Ptr = const_cast<Value*>(Loc.Ptr);
    AST.add(Ptr, LocationSize::unknown(), Loc.AATags);
    Accesses.insert(MemAccessInfo(Ptr, false));
    if (IsReadOnly)
      ReadOnlyPtr.insert(Ptr);
  }

  /// Register a store.
  void addStore(MemoryLocation &Loc) {
    Value *Ptr = const_cast<Value*>(Loc.Ptr);
    AST.add(Ptr, LocationSize::unknown(), Loc.AATags);
    Accesses.insert(MemAccessInfo(Ptr, true));
  }

  /// Check if we can emit a run-time no-alias check for \p Access.
  ///
  /// Returns true if we can emit a run-time no alias check for \p Access.
  /// If we can check this access, this also adds it to a dependence set and
  /// adds a run-time to check for it to \p RtCheck. If \p Assume is true,
  /// we will attempt to use additional run-time checks in order to get
  /// the bounds of the pointer.
  bool createCheckForAccess(RuntimePointerChecking &RtCheck,
                            MemAccessInfo Access,
                            const ValueToValueMap &Strides,
                            DenseMap<Value *, unsigned> &DepSetId,
                            Loop *TheLoop, unsigned &RunningDepId,
                            unsigned ASId, bool ShouldCheckStride,
                            bool Assume);

  /// Check whether we can check the pointers at runtime for
  /// non-intersection.
  ///
  /// Returns true if we need no check or if we do and we can generate them
  /// (i.e. the pointers have computable bounds).
  bool canCheckPtrAtRT(RuntimePointerChecking &RtCheck, ScalarEvolution *SE,
                       Loop *TheLoop, const ValueToValueMap &Strides,
                       bool ShouldCheckWrap = false);

  /// Goes over all memory accesses, checks whether a RT check is needed
  /// and builds sets of dependent accesses.
  void buildDependenceSets() {
    processMemAccesses();
  }

  /// Initial processing of memory accesses determined that we need to
  /// perform dependency checking.
  ///
  /// Note that this can later be cleared if we retry memcheck analysis without
  /// dependency checking (i.e. FoundNonConstantDistanceDependence).
  bool isDependencyCheckNeeded() { return !CheckDeps.empty(); }

  /// We decided that no dependence analysis would be used.  Reset the state.
  void resetDepChecks(MemoryDepChecker &DepChecker) {
    CheckDeps.clear();
    DepChecker.clearDependences();
  }

  MemAccessInfoList &getDependenciesToCheck() { return CheckDeps; }

private:
  typedef SetVector<MemAccessInfo> PtrAccessSet;

  /// Go over all memory access and check whether runtime pointer checks
  /// are needed and build sets of dependency check candidates.
  void processMemAccesses();

  /// Set of all accesses.
  PtrAccessSet Accesses;

  const DataLayout &DL;

  /// The loop being checked.
  const Loop *TheLoop;

  /// List of accesses that need a further dependence check.
  MemAccessInfoList CheckDeps;

  /// Set of pointers that are read only.
  SmallPtrSet<Value*, 16> ReadOnlyPtr;

  /// An alias set tracker to partition the access set by underlying object and
  //intrinsic property (such as TBAA metadata).
  AliasSetTracker AST;

  LoopInfo *LI;

  /// Sets of potentially dependent accesses - members of one set share an
  /// underlying pointer. The set "CheckDeps" identfies which sets really need a
  /// dependence check.
  MemoryDepChecker::DepCandidates &DepCands;

  /// Initial processing of memory accesses determined that we may need
  /// to add memchecks.  Perform the analysis to determine the necessary checks.
  ///
  /// Note that, this is different from isDependencyCheckNeeded.  When we retry
  /// memcheck analysis without dependency checking
  /// (i.e. FoundNonConstantDistanceDependence), isDependencyCheckNeeded is
  /// cleared while this remains set if we have potentially dependent accesses.
  bool IsRTCheckAnalysisNeeded;

  /// The SCEV predicate containing all the SCEV-related assumptions.
  PredicatedScalarEvolution &PSE;
};

} // end anonymous namespace

/// Check whether a pointer can participate in a runtime bounds check.
/// If \p Assume, try harder to prove that we can compute the bounds of \p Ptr
/// by adding run-time checks (overflow checks) if necessary.
static bool hasComputableBounds(PredicatedScalarEvolution &PSE,
                                const ValueToValueMap &Strides, Value *Ptr,
                                Loop *L, bool Assume) {
  const SCEV *PtrScev = replaceSymbolicStrideSCEV(PSE, Strides, Ptr);

  // The bounds for loop-invariant pointer is trivial.
  if (PSE.getSE()->isLoopInvariant(PtrScev, L))
    return true;

  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(PtrScev);

  if (!AR && Assume)
    AR = PSE.getAsAddRec(Ptr);

  if (!AR)
    return false;

  return AR->isAffine();
}

/// Check whether a pointer address cannot wrap.
static bool isNoWrap(PredicatedScalarEvolution &PSE,
                     const ValueToValueMap &Strides, Value *Ptr, Loop *L) {
  const SCEV *PtrScev = PSE.getSCEV(Ptr);
  if (PSE.getSE()->isLoopInvariant(PtrScev, L))
    return true;

  int64_t Stride = getPtrStride(PSE, Ptr, L, Strides);
  if (Stride == 1 || PSE.hasNoOverflow(Ptr, SCEVWrapPredicate::IncrementNUSW))
    return true;

  return false;
}

bool AccessAnalysis::createCheckForAccess(RuntimePointerChecking &RtCheck,
                                          MemAccessInfo Access,
                                          const ValueToValueMap &StridesMap,
                                          DenseMap<Value *, unsigned> &DepSetId,
                                          Loop *TheLoop, unsigned &RunningDepId,
                                          unsigned ASId, bool ShouldCheckWrap,
                                          bool Assume) {
  Value *Ptr = Access.getPointer();

  if (!hasComputableBounds(PSE, StridesMap, Ptr, TheLoop, Assume))
    return false;

  // When we run after a failing dependency check we have to make sure
  // we don't have wrapping pointers.
  if (ShouldCheckWrap && !isNoWrap(PSE, StridesMap, Ptr, TheLoop)) {
    auto *Expr = PSE.getSCEV(Ptr);
    if (!Assume || !isa<SCEVAddRecExpr>(Expr))
      return false;
    PSE.setNoOverflow(Ptr, SCEVWrapPredicate::IncrementNUSW);
  }

  // The id of the dependence set.
  unsigned DepId;

  if (isDependencyCheckNeeded()) {
    Value *Leader = DepCands.getLeaderValue(Access).getPointer();
    unsigned &LeaderId = DepSetId[Leader];
    if (!LeaderId)
      LeaderId = RunningDepId++;
    DepId = LeaderId;
  } else
    // Each access has its own dependence set.
    DepId = RunningDepId++;

  bool IsWrite = Access.getInt();
  RtCheck.insert(TheLoop, Ptr, IsWrite, DepId, ASId, StridesMap, PSE);
  LLVM_DEBUG(dbgs() << "LAA: Found a runtime check ptr:" << *Ptr << '\n');

  return true;
 }

bool AccessAnalysis::canCheckPtrAtRT(RuntimePointerChecking &RtCheck,
                                     ScalarEvolution *SE, Loop *TheLoop,
                                     const ValueToValueMap &StridesMap,
                                     bool ShouldCheckWrap) {
  // Find pointers with computable bounds. We are going to use this information
  // to place a runtime bound check.
  bool CanDoRT = true;

  bool NeedRTCheck = false;
  if (!IsRTCheckAnalysisNeeded) return true;

  bool IsDepCheckNeeded = isDependencyCheckNeeded();

  // We assign a consecutive id to access from different alias sets.
  // Accesses between different groups doesn't need to be checked.
  unsigned ASId = 1;
  for (auto &AS : AST) {
    int NumReadPtrChecks = 0;
    int NumWritePtrChecks = 0;
    bool CanDoAliasSetRT = true;

    // We assign consecutive id to access from different dependence sets.
    // Accesses within the same set don't need a runtime check.
    unsigned RunningDepId = 1;
    DenseMap<Value *, unsigned> DepSetId;

    SmallVector<MemAccessInfo, 4> Retries;

    for (auto A : AS) {
      Value *Ptr = A.getValue();
      bool IsWrite = Accesses.count(MemAccessInfo(Ptr, true));
      MemAccessInfo Access(Ptr, IsWrite);

      if (IsWrite)
        ++NumWritePtrChecks;
      else
        ++NumReadPtrChecks;

      if (!createCheckForAccess(RtCheck, Access, StridesMap, DepSetId, TheLoop,
                                RunningDepId, ASId, ShouldCheckWrap, false)) {
        LLVM_DEBUG(dbgs() << "LAA: Can't find bounds for ptr:" << *Ptr << '\n');
        Retries.push_back(Access);
        CanDoAliasSetRT = false;
      }
    }

    // If we have at least two writes or one write and a read then we need to
    // check them.  But there is no need to checks if there is only one
    // dependence set for this alias set.
    //
    // Note that this function computes CanDoRT and NeedRTCheck independently.
    // For example CanDoRT=false, NeedRTCheck=false means that we have a pointer
    // for which we couldn't find the bounds but we don't actually need to emit
    // any checks so it does not matter.
    bool NeedsAliasSetRTCheck = false;
    if (!(IsDepCheckNeeded && CanDoAliasSetRT && RunningDepId == 2))
      NeedsAliasSetRTCheck = (NumWritePtrChecks >= 2 ||
                             (NumReadPtrChecks >= 1 && NumWritePtrChecks >= 1));

    // We need to perform run-time alias checks, but some pointers had bounds
    // that couldn't be checked.
    if (NeedsAliasSetRTCheck && !CanDoAliasSetRT) {
      // Reset the CanDoSetRt flag and retry all accesses that have failed.
      // We know that we need these checks, so we can now be more aggressive
      // and add further checks if required (overflow checks).
      CanDoAliasSetRT = true;
      for (auto Access : Retries)
        if (!createCheckForAccess(RtCheck, Access, StridesMap, DepSetId,
                                  TheLoop, RunningDepId, ASId,
                                  ShouldCheckWrap, /*Assume=*/true)) {
          CanDoAliasSetRT = false;
          break;
        }
    }

    CanDoRT &= CanDoAliasSetRT;
    NeedRTCheck |= NeedsAliasSetRTCheck;
    ++ASId;
  }

  // If the pointers that we would use for the bounds comparison have different
  // address spaces, assume the values aren't directly comparable, so we can't
  // use them for the runtime check. We also have to assume they could
  // overlap. In the future there should be metadata for whether address spaces
  // are disjoint.
  unsigned NumPointers = RtCheck.Pointers.size();
  for (unsigned i = 0; i < NumPointers; ++i) {
    for (unsigned j = i + 1; j < NumPointers; ++j) {
      // Only need to check pointers between two different dependency sets.
      if (RtCheck.Pointers[i].DependencySetId ==
          RtCheck.Pointers[j].DependencySetId)
       continue;
      // Only need to check pointers in the same alias set.
      if (RtCheck.Pointers[i].AliasSetId != RtCheck.Pointers[j].AliasSetId)
        continue;

      Value *PtrI = RtCheck.Pointers[i].PointerValue;
      Value *PtrJ = RtCheck.Pointers[j].PointerValue;

      unsigned ASi = PtrI->getType()->getPointerAddressSpace();
      unsigned ASj = PtrJ->getType()->getPointerAddressSpace();
      if (ASi != ASj) {
        LLVM_DEBUG(
            dbgs() << "LAA: Runtime check would require comparison between"
                      " different address spaces\n");
        return false;
      }
    }
  }

  if (NeedRTCheck && CanDoRT)
    RtCheck.generateChecks(DepCands, IsDepCheckNeeded);

  LLVM_DEBUG(dbgs() << "LAA: We need to do " << RtCheck.getNumberOfChecks()
                    << " pointer comparisons.\n");

  RtCheck.Need = NeedRTCheck;

  bool CanDoRTIfNeeded = !NeedRTCheck || CanDoRT;
  if (!CanDoRTIfNeeded)
    RtCheck.reset();
  return CanDoRTIfNeeded;
}

void AccessAnalysis::processMemAccesses() {
  // We process the set twice: first we process read-write pointers, last we
  // process read-only pointers. This allows us to skip dependence tests for
  // read-only pointers.

  LLVM_DEBUG(dbgs() << "LAA: Processing memory accesses...\n");
  LLVM_DEBUG(dbgs() << "  AST: "; AST.dump());
  LLVM_DEBUG(dbgs() << "LAA:   Accesses(" << Accesses.size() << "):\n");
  LLVM_DEBUG({
    for (auto A : Accesses)
      dbgs() << "\t" << *A.getPointer() << " (" <<
                (A.getInt() ? "write" : (ReadOnlyPtr.count(A.getPointer()) ?
                                         "read-only" : "read")) << ")\n";
  });

  // The AliasSetTracker has nicely partitioned our pointers by metadata
  // compatibility and potential for underlying-object overlap. As a result, we
  // only need to check for potential pointer dependencies within each alias
  // set.
  for (auto &AS : AST) {
    // Note that both the alias-set tracker and the alias sets themselves used
    // linked lists internally and so the iteration order here is deterministic
    // (matching the original instruction order within each set).

    bool SetHasWrite = false;

    // Map of pointers to last access encountered.
    typedef DenseMap<Value*, MemAccessInfo> UnderlyingObjToAccessMap;
    UnderlyingObjToAccessMap ObjToLastAccess;

    // Set of access to check after all writes have been processed.
    PtrAccessSet DeferredAccesses;

    // Iterate over each alias set twice, once to process read/write pointers,
    // and then to process read-only pointers.
    for (int SetIteration = 0; SetIteration < 2; ++SetIteration) {
      bool UseDeferred = SetIteration > 0;
      PtrAccessSet &S = UseDeferred ? DeferredAccesses : Accesses;

      for (auto AV : AS) {
        Value *Ptr = AV.getValue();

        // For a single memory access in AliasSetTracker, Accesses may contain
        // both read and write, and they both need to be handled for CheckDeps.
        for (auto AC : S) {
          if (AC.getPointer() != Ptr)
            continue;

          bool IsWrite = AC.getInt();

          // If we're using the deferred access set, then it contains only
          // reads.
          bool IsReadOnlyPtr = ReadOnlyPtr.count(Ptr) && !IsWrite;
          if (UseDeferred && !IsReadOnlyPtr)
            continue;
          // Otherwise, the pointer must be in the PtrAccessSet, either as a
          // read or a write.
          assert(((IsReadOnlyPtr && UseDeferred) || IsWrite ||
                  S.count(MemAccessInfo(Ptr, false))) &&
                 "Alias-set pointer not in the access set?");

          MemAccessInfo Access(Ptr, IsWrite);
          DepCands.insert(Access);

          // Memorize read-only pointers for later processing and skip them in
          // the first round (they need to be checked after we have seen all
          // write pointers). Note: we also mark pointer that are not
          // consecutive as "read-only" pointers (so that we check
          // "a[b[i]] +="). Hence, we need the second check for "!IsWrite".
          if (!UseDeferred && IsReadOnlyPtr) {
            DeferredAccesses.insert(Access);
            continue;
          }

          // If this is a write - check other reads and writes for conflicts. If
          // this is a read only check other writes for conflicts (but only if
          // there is no other write to the ptr - this is an optimization to
          // catch "a[i] = a[i] + " without having to do a dependence check).
          if ((IsWrite || IsReadOnlyPtr) && SetHasWrite) {
            CheckDeps.push_back(Access);
            IsRTCheckAnalysisNeeded = true;
          }

          if (IsWrite)
            SetHasWrite = true;

          // Create sets of pointers connected by a shared alias set and
          // underlying object.
          typedef SmallVector<Value *, 16> ValueVector;
          ValueVector TempObjects;

          GetUnderlyingObjects(Ptr, TempObjects, DL, LI);
          LLVM_DEBUG(dbgs()
                     << "Underlying objects for pointer " << *Ptr << "\n");
          for (Value *UnderlyingObj : TempObjects) {
            // nullptr never alias, don't join sets for pointer that have "null"
            // in their UnderlyingObjects list.
            if (isa<ConstantPointerNull>(UnderlyingObj) &&
                !NullPointerIsDefined(
                    TheLoop->getHeader()->getParent(),
                    UnderlyingObj->getType()->getPointerAddressSpace()))
              continue;

            UnderlyingObjToAccessMap::iterator Prev =
                ObjToLastAccess.find(UnderlyingObj);
            if (Prev != ObjToLastAccess.end())
              DepCands.unionSets(Access, Prev->second);

            ObjToLastAccess[UnderlyingObj] = Access;
            LLVM_DEBUG(dbgs() << "  " << *UnderlyingObj << "\n");
          }
        }
      }
    }
  }
}

static bool isInBoundsGep(Value *Ptr) {
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr))
    return GEP->isInBounds();
  return false;
}

/// Return true if an AddRec pointer \p Ptr is unsigned non-wrapping,
/// i.e. monotonically increasing/decreasing.
static bool isNoWrapAddRec(Value *Ptr, const SCEVAddRecExpr *AR,
                           PredicatedScalarEvolution &PSE, const Loop *L) {
  // FIXME: This should probably only return true for NUW.
  if (AR->getNoWrapFlags(SCEV::NoWrapMask))
    return true;

  // Scalar evolution does not propagate the non-wrapping flags to values that
  // are derived from a non-wrapping induction variable because non-wrapping
  // could be flow-sensitive.
  //
  // Look through the potentially overflowing instruction to try to prove
  // non-wrapping for the *specific* value of Ptr.

  // The arithmetic implied by an inbounds GEP can't overflow.
  auto *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP || !GEP->isInBounds())
    return false;

  // Make sure there is only one non-const index and analyze that.
  Value *NonConstIndex = nullptr;
  for (Value *Index : make_range(GEP->idx_begin(), GEP->idx_end()))
    if (!isa<ConstantInt>(Index)) {
      if (NonConstIndex)
        return false;
      NonConstIndex = Index;
    }
  if (!NonConstIndex)
    // The recurrence is on the pointer, ignore for now.
    return false;

  // The index in GEP is signed.  It is non-wrapping if it's derived from a NSW
  // AddRec using a NSW operation.
  if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(NonConstIndex))
    if (OBO->hasNoSignedWrap() &&
        // Assume constant for other the operand so that the AddRec can be
        // easily found.
        isa<ConstantInt>(OBO->getOperand(1))) {
      auto *OpScev = PSE.getSCEV(OBO->getOperand(0));

      if (auto *OpAR = dyn_cast<SCEVAddRecExpr>(OpScev))
        return OpAR->getLoop() == L && OpAR->getNoWrapFlags(SCEV::FlagNSW);
    }

  return false;
}

/// Check whether the access through \p Ptr has a constant stride.
int64_t llvm::getPtrStride(PredicatedScalarEvolution &PSE, Value *Ptr,
                           const Loop *Lp, const ValueToValueMap &StridesMap,
                           bool Assume, bool ShouldCheckWrap) {
  Type *Ty = Ptr->getType();
  assert(Ty->isPointerTy() && "Unexpected non-ptr");

  // Make sure that the pointer does not point to aggregate types.
  auto *PtrTy = cast<PointerType>(Ty);
  if (PtrTy->getElementType()->isAggregateType()) {
    LLVM_DEBUG(dbgs() << "LAA: Bad stride - Not a pointer to a scalar type"
                      << *Ptr << "\n");
    return 0;
  }

  const SCEV *PtrScev = replaceSymbolicStrideSCEV(PSE, StridesMap, Ptr);

  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(PtrScev);
  if (Assume && !AR)
    AR = PSE.getAsAddRec(Ptr);

  if (!AR) {
    LLVM_DEBUG(dbgs() << "LAA: Bad stride - Not an AddRecExpr pointer " << *Ptr
                      << " SCEV: " << *PtrScev << "\n");
    return 0;
  }

  // The accesss function must stride over the innermost loop.
  if (Lp != AR->getLoop()) {
    LLVM_DEBUG(dbgs() << "LAA: Bad stride - Not striding over innermost loop "
                      << *Ptr << " SCEV: " << *AR << "\n");
    return 0;
  }

  // The address calculation must not wrap. Otherwise, a dependence could be
  // inverted.
  // An inbounds getelementptr that is a AddRec with a unit stride
  // cannot wrap per definition. The unit stride requirement is checked later.
  // An getelementptr without an inbounds attribute and unit stride would have
  // to access the pointer value "0" which is undefined behavior in address
  // space 0, therefore we can also vectorize this case.
  bool IsInBoundsGEP = isInBoundsGep(Ptr);
  bool IsNoWrapAddRec = !ShouldCheckWrap ||
    PSE.hasNoOverflow(Ptr, SCEVWrapPredicate::IncrementNUSW) ||
    isNoWrapAddRec(Ptr, AR, PSE, Lp);
  if (!IsNoWrapAddRec && !IsInBoundsGEP &&
      NullPointerIsDefined(Lp->getHeader()->getParent(),
                           PtrTy->getAddressSpace())) {
    if (Assume) {
      PSE.setNoOverflow(Ptr, SCEVWrapPredicate::IncrementNUSW);
      IsNoWrapAddRec = true;
      LLVM_DEBUG(dbgs() << "LAA: Pointer may wrap in the address space:\n"
                        << "LAA:   Pointer: " << *Ptr << "\n"
                        << "LAA:   SCEV: " << *AR << "\n"
                        << "LAA:   Added an overflow assumption\n");
    } else {
      LLVM_DEBUG(
          dbgs() << "LAA: Bad stride - Pointer may wrap in the address space "
                 << *Ptr << " SCEV: " << *AR << "\n");
      return 0;
    }
  }

  // Check the step is constant.
  const SCEV *Step = AR->getStepRecurrence(*PSE.getSE());

  // Calculate the pointer stride and check if it is constant.
  const SCEVConstant *C = dyn_cast<SCEVConstant>(Step);
  if (!C) {
    LLVM_DEBUG(dbgs() << "LAA: Bad stride - Not a constant strided " << *Ptr
                      << " SCEV: " << *AR << "\n");
    return 0;
  }

  auto &DL = Lp->getHeader()->getModule()->getDataLayout();
  int64_t Size = DL.getTypeAllocSize(PtrTy->getElementType());
  const APInt &APStepVal = C->getAPInt();

  // Huge step value - give up.
  if (APStepVal.getBitWidth() > 64)
    return 0;

  int64_t StepVal = APStepVal.getSExtValue();

  // Strided access.
  int64_t Stride = StepVal / Size;
  int64_t Rem = StepVal % Size;
  if (Rem)
    return 0;

  // If the SCEV could wrap but we have an inbounds gep with a unit stride we
  // know we can't "wrap around the address space". In case of address space
  // zero we know that this won't happen without triggering undefined behavior.
  if (!IsNoWrapAddRec && Stride != 1 && Stride != -1 &&
      (IsInBoundsGEP || !NullPointerIsDefined(Lp->getHeader()->getParent(),
                                              PtrTy->getAddressSpace()))) {
    if (Assume) {
      // We can avoid this case by adding a run-time check.
      LLVM_DEBUG(dbgs() << "LAA: Non unit strided pointer which is not either "
                        << "inbouds or in address space 0 may wrap:\n"
                        << "LAA:   Pointer: " << *Ptr << "\n"
                        << "LAA:   SCEV: " << *AR << "\n"
                        << "LAA:   Added an overflow assumption\n");
      PSE.setNoOverflow(Ptr, SCEVWrapPredicate::IncrementNUSW);
    } else
      return 0;
  }

  return Stride;
}

bool llvm::sortPtrAccesses(ArrayRef<Value *> VL, const DataLayout &DL,
                           ScalarEvolution &SE,
                           SmallVectorImpl<unsigned> &SortedIndices) {
  assert(llvm::all_of(
             VL, [](const Value *V) { return V->getType()->isPointerTy(); }) &&
         "Expected list of pointer operands.");
  SmallVector<std::pair<int64_t, Value *>, 4> OffValPairs;
  OffValPairs.reserve(VL.size());

  // Walk over the pointers, and map each of them to an offset relative to
  // first pointer in the array.
  Value *Ptr0 = VL[0];
  const SCEV *Scev0 = SE.getSCEV(Ptr0);
  Value *Obj0 = GetUnderlyingObject(Ptr0, DL);

  llvm::SmallSet<int64_t, 4> Offsets;
  for (auto *Ptr : VL) {
    // TODO: Outline this code as a special, more time consuming, version of
    // computeConstantDifference() function.
    if (Ptr->getType()->getPointerAddressSpace() !=
        Ptr0->getType()->getPointerAddressSpace())
      return false;
    // If a pointer refers to a different underlying object, bail - the
    // pointers are by definition incomparable.
    Value *CurrObj = GetUnderlyingObject(Ptr, DL);
    if (CurrObj != Obj0)
      return false;

    const SCEV *Scev = SE.getSCEV(Ptr);
    const auto *Diff = dyn_cast<SCEVConstant>(SE.getMinusSCEV(Scev, Scev0));
    // The pointers may not have a constant offset from each other, or SCEV
    // may just not be smart enough to figure out they do. Regardless,
    // there's nothing we can do.
    if (!Diff)
      return false;

    // Check if the pointer with the same offset is found.
    int64_t Offset = Diff->getAPInt().getSExtValue();
    if (!Offsets.insert(Offset).second)
      return false;
    OffValPairs.emplace_back(Offset, Ptr);
  }
  SortedIndices.clear();
  SortedIndices.resize(VL.size());
  std::iota(SortedIndices.begin(), SortedIndices.end(), 0);

  // Sort the memory accesses and keep the order of their uses in UseOrder.
  std::stable_sort(SortedIndices.begin(), SortedIndices.end(),
                   [&OffValPairs](unsigned Left, unsigned Right) {
                     return OffValPairs[Left].first < OffValPairs[Right].first;
                   });

  // Check if the order is consecutive already.
  if (llvm::all_of(SortedIndices, [&SortedIndices](const unsigned I) {
        return I == SortedIndices[I];
      }))
    SortedIndices.clear();

  return true;
}

/// Take the address space operand from the Load/Store instruction.
/// Returns -1 if this is not a valid Load/Store instruction.
static unsigned getAddressSpaceOperand(Value *I) {
  if (LoadInst *L = dyn_cast<LoadInst>(I))
    return L->getPointerAddressSpace();
  if (StoreInst *S = dyn_cast<StoreInst>(I))
    return S->getPointerAddressSpace();
  return -1;
}

/// Returns true if the memory operations \p A and \p B are consecutive.
bool llvm::isConsecutiveAccess(Value *A, Value *B, const DataLayout &DL,
                               ScalarEvolution &SE, bool CheckType) {
  Value *PtrA = getLoadStorePointerOperand(A);
  Value *PtrB = getLoadStorePointerOperand(B);
  unsigned ASA = getAddressSpaceOperand(A);
  unsigned ASB = getAddressSpaceOperand(B);

  // Check that the address spaces match and that the pointers are valid.
  if (!PtrA || !PtrB || (ASA != ASB))
    return false;

  // Make sure that A and B are different pointers.
  if (PtrA == PtrB)
    return false;

  // Make sure that A and B have the same type if required.
  if (CheckType && PtrA->getType() != PtrB->getType())
    return false;

  unsigned IdxWidth = DL.getIndexSizeInBits(ASA);
  Type *Ty = cast<PointerType>(PtrA->getType())->getElementType();
  APInt Size(IdxWidth, DL.getTypeStoreSize(Ty));

  APInt OffsetA(IdxWidth, 0), OffsetB(IdxWidth, 0);
  PtrA = PtrA->stripAndAccumulateInBoundsConstantOffsets(DL, OffsetA);
  PtrB = PtrB->stripAndAccumulateInBoundsConstantOffsets(DL, OffsetB);

  //  OffsetDelta = OffsetB - OffsetA;
  const SCEV *OffsetSCEVA = SE.getConstant(OffsetA);
  const SCEV *OffsetSCEVB = SE.getConstant(OffsetB);
  const SCEV *OffsetDeltaSCEV = SE.getMinusSCEV(OffsetSCEVB, OffsetSCEVA);
  const SCEVConstant *OffsetDeltaC = dyn_cast<SCEVConstant>(OffsetDeltaSCEV);
  const APInt &OffsetDelta = OffsetDeltaC->getAPInt();
  // Check if they are based on the same pointer. That makes the offsets
  // sufficient.
  if (PtrA == PtrB)
    return OffsetDelta == Size;

  // Compute the necessary base pointer delta to have the necessary final delta
  // equal to the size.
  // BaseDelta = Size - OffsetDelta;
  const SCEV *SizeSCEV = SE.getConstant(Size);
  const SCEV *BaseDelta = SE.getMinusSCEV(SizeSCEV, OffsetDeltaSCEV);

  // Otherwise compute the distance with SCEV between the base pointers.
  const SCEV *PtrSCEVA = SE.getSCEV(PtrA);
  const SCEV *PtrSCEVB = SE.getSCEV(PtrB);
  const SCEV *X = SE.getAddExpr(PtrSCEVA, BaseDelta);
  return X == PtrSCEVB;
}

MemoryDepChecker::VectorizationSafetyStatus
MemoryDepChecker::Dependence::isSafeForVectorization(DepType Type) {
  switch (Type) {
  case NoDep:
  case Forward:
  case BackwardVectorizable:
    return VectorizationSafetyStatus::Safe;

  case Unknown:
    return VectorizationSafetyStatus::PossiblySafeWithRtChecks;
  case ForwardButPreventsForwarding:
  case Backward:
  case BackwardVectorizableButPreventsForwarding:
    return VectorizationSafetyStatus::Unsafe;
  }
  llvm_unreachable("unexpected DepType!");
}

bool MemoryDepChecker::Dependence::isBackward() const {
  switch (Type) {
  case NoDep:
  case Forward:
  case ForwardButPreventsForwarding:
  case Unknown:
    return false;

  case BackwardVectorizable:
  case Backward:
  case BackwardVectorizableButPreventsForwarding:
    return true;
  }
  llvm_unreachable("unexpected DepType!");
}

bool MemoryDepChecker::Dependence::isPossiblyBackward() const {
  return isBackward() || Type == Unknown;
}

bool MemoryDepChecker::Dependence::isForward() const {
  switch (Type) {
  case Forward:
  case ForwardButPreventsForwarding:
    return true;

  case NoDep:
  case Unknown:
  case BackwardVectorizable:
  case Backward:
  case BackwardVectorizableButPreventsForwarding:
    return false;
  }
  llvm_unreachable("unexpected DepType!");
}

bool MemoryDepChecker::couldPreventStoreLoadForward(uint64_t Distance,
                                                    uint64_t TypeByteSize) {
  // If loads occur at a distance that is not a multiple of a feasible vector
  // factor store-load forwarding does not take place.
  // Positive dependences might cause troubles because vectorizing them might
  // prevent store-load forwarding making vectorized code run a lot slower.
  //   a[i] = a[i-3] ^ a[i-8];
  //   The stores to a[i:i+1] don't align with the stores to a[i-3:i-2] and
  //   hence on your typical architecture store-load forwarding does not take
  //   place. Vectorizing in such cases does not make sense.
  // Store-load forwarding distance.

  // After this many iterations store-to-load forwarding conflicts should not
  // cause any slowdowns.
  const uint64_t NumItersForStoreLoadThroughMemory = 8 * TypeByteSize;
  // Maximum vector factor.
  uint64_t MaxVFWithoutSLForwardIssues = std::min(
      VectorizerParams::MaxVectorWidth * TypeByteSize, MaxSafeDepDistBytes);

  // Compute the smallest VF at which the store and load would be misaligned.
  for (uint64_t VF = 2 * TypeByteSize; VF <= MaxVFWithoutSLForwardIssues;
       VF *= 2) {
    // If the number of vector iteration between the store and the load are
    // small we could incur conflicts.
    if (Distance % VF && Distance / VF < NumItersForStoreLoadThroughMemory) {
      MaxVFWithoutSLForwardIssues = (VF >>= 1);
      break;
    }
  }

  if (MaxVFWithoutSLForwardIssues < 2 * TypeByteSize) {
    LLVM_DEBUG(
        dbgs() << "LAA: Distance " << Distance
               << " that could cause a store-load forwarding conflict\n");
    return true;
  }

  if (MaxVFWithoutSLForwardIssues < MaxSafeDepDistBytes &&
      MaxVFWithoutSLForwardIssues !=
          VectorizerParams::MaxVectorWidth * TypeByteSize)
    MaxSafeDepDistBytes = MaxVFWithoutSLForwardIssues;
  return false;
}

void MemoryDepChecker::mergeInStatus(VectorizationSafetyStatus S) {
  if (Status < S)
    Status = S;
}

/// Given a non-constant (unknown) dependence-distance \p Dist between two
/// memory accesses, that have the same stride whose absolute value is given
/// in \p Stride, and that have the same type size \p TypeByteSize,
/// in a loop whose takenCount is \p BackedgeTakenCount, check if it is
/// possible to prove statically that the dependence distance is larger
/// than the range that the accesses will travel through the execution of
/// the loop. If so, return true; false otherwise. This is useful for
/// example in loops such as the following (PR31098):
///     for (i = 0; i < D; ++i) {
///                = out[i];
///       out[i+D] =
///     }
static bool isSafeDependenceDistance(const DataLayout &DL, ScalarEvolution &SE,
                                     const SCEV &BackedgeTakenCount,
                                     const SCEV &Dist, uint64_t Stride,
                                     uint64_t TypeByteSize) {

  // If we can prove that
  //      (**) |Dist| > BackedgeTakenCount * Step
  // where Step is the absolute stride of the memory accesses in bytes,
  // then there is no dependence.
  //
  // Ratioanle:
  // We basically want to check if the absolute distance (|Dist/Step|)
  // is >= the loop iteration count (or > BackedgeTakenCount).
  // This is equivalent to the Strong SIV Test (Practical Dependence Testing,
  // Section 4.2.1); Note, that for vectorization it is sufficient to prove
  // that the dependence distance is >= VF; This is checked elsewhere.
  // But in some cases we can prune unknown dependence distances early, and
  // even before selecting the VF, and without a runtime test, by comparing
  // the distance against the loop iteration count. Since the vectorized code
  // will be executed only if LoopCount >= VF, proving distance >= LoopCount
  // also guarantees that distance >= VF.
  //
  const uint64_t ByteStride = Stride * TypeByteSize;
  const SCEV *Step = SE.getConstant(BackedgeTakenCount.getType(), ByteStride);
  const SCEV *Product = SE.getMulExpr(&BackedgeTakenCount, Step);

  const SCEV *CastedDist = &Dist;
  const SCEV *CastedProduct = Product;
  uint64_t DistTypeSize = DL.getTypeAllocSize(Dist.getType());
  uint64_t ProductTypeSize = DL.getTypeAllocSize(Product->getType());

  // The dependence distance can be positive/negative, so we sign extend Dist;
  // The multiplication of the absolute stride in bytes and the
  // backdgeTakenCount is non-negative, so we zero extend Product.
  if (DistTypeSize > ProductTypeSize)
    CastedProduct = SE.getZeroExtendExpr(Product, Dist.getType());
  else
    CastedDist = SE.getNoopOrSignExtend(&Dist, Product->getType());

  // Is  Dist - (BackedgeTakenCount * Step) > 0 ?
  // (If so, then we have proven (**) because |Dist| >= Dist)
  const SCEV *Minus = SE.getMinusSCEV(CastedDist, CastedProduct);
  if (SE.isKnownPositive(Minus))
    return true;

  // Second try: Is  -Dist - (BackedgeTakenCount * Step) > 0 ?
  // (If so, then we have proven (**) because |Dist| >= -1*Dist)
  const SCEV *NegDist = SE.getNegativeSCEV(CastedDist);
  Minus = SE.getMinusSCEV(NegDist, CastedProduct);
  if (SE.isKnownPositive(Minus))
    return true;

  return false;
}

/// Check the dependence for two accesses with the same stride \p Stride.
/// \p Distance is the positive distance and \p TypeByteSize is type size in
/// bytes.
///
/// \returns true if they are independent.
static bool areStridedAccessesIndependent(uint64_t Distance, uint64_t Stride,
                                          uint64_t TypeByteSize) {
  assert(Stride > 1 && "The stride must be greater than 1");
  assert(TypeByteSize > 0 && "The type size in byte must be non-zero");
  assert(Distance > 0 && "The distance must be non-zero");

  // Skip if the distance is not multiple of type byte size.
  if (Distance % TypeByteSize)
    return false;

  uint64_t ScaledDist = Distance / TypeByteSize;

  // No dependence if the scaled distance is not multiple of the stride.
  // E.g.
  //      for (i = 0; i < 1024 ; i += 4)
  //        A[i+2] = A[i] + 1;
  //
  // Two accesses in memory (scaled distance is 2, stride is 4):
  //     | A[0] |      |      |      | A[4] |      |      |      |
  //     |      |      | A[2] |      |      |      | A[6] |      |
  //
  // E.g.
  //      for (i = 0; i < 1024 ; i += 3)
  //        A[i+4] = A[i] + 1;
  //
  // Two accesses in memory (scaled distance is 4, stride is 3):
  //     | A[0] |      |      | A[3] |      |      | A[6] |      |      |
  //     |      |      |      |      | A[4] |      |      | A[7] |      |
  return ScaledDist % Stride;
}

MemoryDepChecker::Dependence::DepType
MemoryDepChecker::isDependent(const MemAccessInfo &A, unsigned AIdx,
                              const MemAccessInfo &B, unsigned BIdx,
                              const ValueToValueMap &Strides) {
  assert (AIdx < BIdx && "Must pass arguments in program order");

  Value *APtr = A.getPointer();
  Value *BPtr = B.getPointer();
  bool AIsWrite = A.getInt();
  bool BIsWrite = B.getInt();

  // Two reads are independent.
  if (!AIsWrite && !BIsWrite)
    return Dependence::NoDep;

  // We cannot check pointers in different address spaces.
  if (APtr->getType()->getPointerAddressSpace() !=
      BPtr->getType()->getPointerAddressSpace())
    return Dependence::Unknown;

  int64_t StrideAPtr = getPtrStride(PSE, APtr, InnermostLoop, Strides, true);
  int64_t StrideBPtr = getPtrStride(PSE, BPtr, InnermostLoop, Strides, true);

  const SCEV *Src = PSE.getSCEV(APtr);
  const SCEV *Sink = PSE.getSCEV(BPtr);

  // If the induction step is negative we have to invert source and sink of the
  // dependence.
  if (StrideAPtr < 0) {
    std::swap(APtr, BPtr);
    std::swap(Src, Sink);
    std::swap(AIsWrite, BIsWrite);
    std::swap(AIdx, BIdx);
    std::swap(StrideAPtr, StrideBPtr);
  }

  const SCEV *Dist = PSE.getSE()->getMinusSCEV(Sink, Src);

  LLVM_DEBUG(dbgs() << "LAA: Src Scev: " << *Src << "Sink Scev: " << *Sink
                    << "(Induction step: " << StrideAPtr << ")\n");
  LLVM_DEBUG(dbgs() << "LAA: Distance for " << *InstMap[AIdx] << " to "
                    << *InstMap[BIdx] << ": " << *Dist << "\n");

  // Need accesses with constant stride. We don't want to vectorize
  // "A[B[i]] += ..." and similar code or pointer arithmetic that could wrap in
  // the address space.
  if (!StrideAPtr || !StrideBPtr || StrideAPtr != StrideBPtr){
    LLVM_DEBUG(dbgs() << "Pointer access with non-constant stride\n");
    return Dependence::Unknown;
  }

  Type *ATy = APtr->getType()->getPointerElementType();
  Type *BTy = BPtr->getType()->getPointerElementType();
  auto &DL = InnermostLoop->getHeader()->getModule()->getDataLayout();
  uint64_t TypeByteSize = DL.getTypeAllocSize(ATy);
  uint64_t Stride = std::abs(StrideAPtr);
  const SCEVConstant *C = dyn_cast<SCEVConstant>(Dist);
  if (!C) {
    if (TypeByteSize == DL.getTypeAllocSize(BTy) &&
        isSafeDependenceDistance(DL, *(PSE.getSE()),
                                 *(PSE.getBackedgeTakenCount()), *Dist, Stride,
                                 TypeByteSize))
      return Dependence::NoDep;

    LLVM_DEBUG(dbgs() << "LAA: Dependence because of non-constant distance\n");
    FoundNonConstantDistanceDependence = true;
    return Dependence::Unknown;
  }

  const APInt &Val = C->getAPInt();
  int64_t Distance = Val.getSExtValue();

  // Attempt to prove strided accesses independent.
  if (std::abs(Distance) > 0 && Stride > 1 && ATy == BTy &&
      areStridedAccessesIndependent(std::abs(Distance), Stride, TypeByteSize)) {
    LLVM_DEBUG(dbgs() << "LAA: Strided accesses are independent\n");
    return Dependence::NoDep;
  }

  // Negative distances are not plausible dependencies.
  if (Val.isNegative()) {
    bool IsTrueDataDependence = (AIsWrite && !BIsWrite);
    if (IsTrueDataDependence && EnableForwardingConflictDetection &&
        (couldPreventStoreLoadForward(Val.abs().getZExtValue(), TypeByteSize) ||
         ATy != BTy)) {
      LLVM_DEBUG(dbgs() << "LAA: Forward but may prevent st->ld forwarding\n");
      return Dependence::ForwardButPreventsForwarding;
    }

    LLVM_DEBUG(dbgs() << "LAA: Dependence is negative\n");
    return Dependence::Forward;
  }

  // Write to the same location with the same size.
  // Could be improved to assert type sizes are the same (i32 == float, etc).
  if (Val == 0) {
    if (ATy == BTy)
      return Dependence::Forward;
    LLVM_DEBUG(
        dbgs() << "LAA: Zero dependence difference but different types\n");
    return Dependence::Unknown;
  }

  assert(Val.isStrictlyPositive() && "Expect a positive value");

  if (ATy != BTy) {
    LLVM_DEBUG(
        dbgs()
        << "LAA: ReadWrite-Write positive dependency with different types\n");
    return Dependence::Unknown;
  }

  // Bail out early if passed-in parameters make vectorization not feasible.
  unsigned ForcedFactor = (VectorizerParams::VectorizationFactor ?
                           VectorizerParams::VectorizationFactor : 1);
  unsigned ForcedUnroll = (VectorizerParams::VectorizationInterleave ?
                           VectorizerParams::VectorizationInterleave : 1);
  // The minimum number of iterations for a vectorized/unrolled version.
  unsigned MinNumIter = std::max(ForcedFactor * ForcedUnroll, 2U);

  // It's not vectorizable if the distance is smaller than the minimum distance
  // needed for a vectroized/unrolled version. Vectorizing one iteration in
  // front needs TypeByteSize * Stride. Vectorizing the last iteration needs
  // TypeByteSize (No need to plus the last gap distance).
  //
  // E.g. Assume one char is 1 byte in memory and one int is 4 bytes.
  //      foo(int *A) {
  //        int *B = (int *)((char *)A + 14);
  //        for (i = 0 ; i < 1024 ; i += 2)
  //          B[i] = A[i] + 1;
  //      }
  //
  // Two accesses in memory (stride is 2):
  //     | A[0] |      | A[2] |      | A[4] |      | A[6] |      |
  //                              | B[0] |      | B[2] |      | B[4] |
  //
  // Distance needs for vectorizing iterations except the last iteration:
  // 4 * 2 * (MinNumIter - 1). Distance needs for the last iteration: 4.
  // So the minimum distance needed is: 4 * 2 * (MinNumIter - 1) + 4.
  //
  // If MinNumIter is 2, it is vectorizable as the minimum distance needed is
  // 12, which is less than distance.
  //
  // If MinNumIter is 4 (Say if a user forces the vectorization factor to be 4),
  // the minimum distance needed is 28, which is greater than distance. It is
  // not safe to do vectorization.
  uint64_t MinDistanceNeeded =
      TypeByteSize * Stride * (MinNumIter - 1) + TypeByteSize;
  if (MinDistanceNeeded > static_cast<uint64_t>(Distance)) {
    LLVM_DEBUG(dbgs() << "LAA: Failure because of positive distance "
                      << Distance << '\n');
    return Dependence::Backward;
  }

  // Unsafe if the minimum distance needed is greater than max safe distance.
  if (MinDistanceNeeded > MaxSafeDepDistBytes) {
    LLVM_DEBUG(dbgs() << "LAA: Failure because it needs at least "
                      << MinDistanceNeeded << " size in bytes");
    return Dependence::Backward;
  }

  // Positive distance bigger than max vectorization factor.
  // FIXME: Should use max factor instead of max distance in bytes, which could
  // not handle different types.
  // E.g. Assume one char is 1 byte in memory and one int is 4 bytes.
  //      void foo (int *A, char *B) {
  //        for (unsigned i = 0; i < 1024; i++) {
  //          A[i+2] = A[i] + 1;
  //          B[i+2] = B[i] + 1;
  //        }
  //      }
  //
  // This case is currently unsafe according to the max safe distance. If we
  // analyze the two accesses on array B, the max safe dependence distance
  // is 2. Then we analyze the accesses on array A, the minimum distance needed
  // is 8, which is less than 2 and forbidden vectorization, But actually
  // both A and B could be vectorized by 2 iterations.
  MaxSafeDepDistBytes =
      std::min(static_cast<uint64_t>(Distance), MaxSafeDepDistBytes);

  bool IsTrueDataDependence = (!AIsWrite && BIsWrite);
  if (IsTrueDataDependence && EnableForwardingConflictDetection &&
      couldPreventStoreLoadForward(Distance, TypeByteSize))
    return Dependence::BackwardVectorizableButPreventsForwarding;

  uint64_t MaxVF = MaxSafeDepDistBytes / (TypeByteSize * Stride);
  LLVM_DEBUG(dbgs() << "LAA: Positive distance " << Val.getSExtValue()
                    << " with max VF = " << MaxVF << '\n');
  uint64_t MaxVFInBits = MaxVF * TypeByteSize * 8;
  MaxSafeRegisterWidth = std::min(MaxSafeRegisterWidth, MaxVFInBits);
  return Dependence::BackwardVectorizable;
}

bool MemoryDepChecker::areDepsSafe(DepCandidates &AccessSets,
                                   MemAccessInfoList &CheckDeps,
                                   const ValueToValueMap &Strides) {

  MaxSafeDepDistBytes = -1;
  SmallPtrSet<MemAccessInfo, 8> Visited;
  for (MemAccessInfo CurAccess : CheckDeps) {
    if (Visited.count(CurAccess))
      continue;

    // Get the relevant memory access set.
    EquivalenceClasses<MemAccessInfo>::iterator I =
      AccessSets.findValue(AccessSets.getLeaderValue(CurAccess));

    // Check accesses within this set.
    EquivalenceClasses<MemAccessInfo>::member_iterator AI =
        AccessSets.member_begin(I);
    EquivalenceClasses<MemAccessInfo>::member_iterator AE =
        AccessSets.member_end();

    // Check every access pair.
    while (AI != AE) {
      Visited.insert(*AI);
      EquivalenceClasses<MemAccessInfo>::member_iterator OI = std::next(AI);
      while (OI != AE) {
        // Check every accessing instruction pair in program order.
        for (std::vector<unsigned>::iterator I1 = Accesses[*AI].begin(),
             I1E = Accesses[*AI].end(); I1 != I1E; ++I1)
          for (std::vector<unsigned>::iterator I2 = Accesses[*OI].begin(),
               I2E = Accesses[*OI].end(); I2 != I2E; ++I2) {
            auto A = std::make_pair(&*AI, *I1);
            auto B = std::make_pair(&*OI, *I2);

            assert(*I1 != *I2);
            if (*I1 > *I2)
              std::swap(A, B);

            Dependence::DepType Type =
                isDependent(*A.first, A.second, *B.first, B.second, Strides);
            mergeInStatus(Dependence::isSafeForVectorization(Type));

            // Gather dependences unless we accumulated MaxDependences
            // dependences.  In that case return as soon as we find the first
            // unsafe dependence.  This puts a limit on this quadratic
            // algorithm.
            if (RecordDependences) {
              if (Type != Dependence::NoDep)
                Dependences.push_back(Dependence(A.second, B.second, Type));

              if (Dependences.size() >= MaxDependences) {
                RecordDependences = false;
                Dependences.clear();
                LLVM_DEBUG(dbgs()
                           << "Too many dependences, stopped recording\n");
              }
            }
            if (!RecordDependences && !isSafeForVectorization())
              return false;
          }
        ++OI;
      }
      AI++;
    }
  }

  LLVM_DEBUG(dbgs() << "Total Dependences: " << Dependences.size() << "\n");
  return isSafeForVectorization();
}

SmallVector<Instruction *, 4>
MemoryDepChecker::getInstructionsForAccess(Value *Ptr, bool isWrite) const {
  MemAccessInfo Access(Ptr, isWrite);
  auto &IndexVector = Accesses.find(Access)->second;

  SmallVector<Instruction *, 4> Insts;
  transform(IndexVector,
                 std::back_inserter(Insts),
                 [&](unsigned Idx) { return this->InstMap[Idx]; });
  return Insts;
}

const char *MemoryDepChecker::Dependence::DepName[] = {
    "NoDep", "Unknown", "Forward", "ForwardButPreventsForwarding", "Backward",
    "BackwardVectorizable", "BackwardVectorizableButPreventsForwarding"};

void MemoryDepChecker::Dependence::print(
    raw_ostream &OS, unsigned Depth,
    const SmallVectorImpl<Instruction *> &Instrs) const {
  OS.indent(Depth) << DepName[Type] << ":\n";
  OS.indent(Depth + 2) << *Instrs[Source] << " -> \n";
  OS.indent(Depth + 2) << *Instrs[Destination] << "\n";
}

bool LoopAccessInfo::canAnalyzeLoop() {
  // We need to have a loop header.
  LLVM_DEBUG(dbgs() << "LAA: Found a loop in "
                    << TheLoop->getHeader()->getParent()->getName() << ": "
                    << TheLoop->getHeader()->getName() << '\n');

  // We can only analyze innermost loops.
  if (!TheLoop->empty()) {
    LLVM_DEBUG(dbgs() << "LAA: loop is not the innermost loop\n");
    recordAnalysis("NotInnerMostLoop") << "loop is not the innermost loop";
    return false;
  }

  // We must have a single backedge.
  if (TheLoop->getNumBackEdges() != 1) {
    LLVM_DEBUG(
        dbgs() << "LAA: loop control flow is not understood by analyzer\n");
    recordAnalysis("CFGNotUnderstood")
        << "loop control flow is not understood by analyzer";
    return false;
  }

  // We must have a single exiting block.
  if (!TheLoop->getExitingBlock()) {
    LLVM_DEBUG(
        dbgs() << "LAA: loop control flow is not understood by analyzer\n");
    recordAnalysis("CFGNotUnderstood")
        << "loop control flow is not understood by analyzer";
    return false;
  }

  // We only handle bottom-tested loops, i.e. loop in which the condition is
  // checked at the end of each iteration. With that we can assume that all
  // instructions in the loop are executed the same number of times.
  if (TheLoop->getExitingBlock() != TheLoop->getLoopLatch()) {
    LLVM_DEBUG(
        dbgs() << "LAA: loop control flow is not understood by analyzer\n");
    recordAnalysis("CFGNotUnderstood")
        << "loop control flow is not understood by analyzer";
    return false;
  }

  // ScalarEvolution needs to be able to find the exit count.
  const SCEV *ExitCount = PSE->getBackedgeTakenCount();
  if (ExitCount == PSE->getSE()->getCouldNotCompute()) {
    recordAnalysis("CantComputeNumberOfIterations")
        << "could not determine number of loop iterations";
    LLVM_DEBUG(dbgs() << "LAA: SCEV could not compute the loop exit count.\n");
    return false;
  }

  return true;
}

void LoopAccessInfo::analyzeLoop(AliasAnalysis *AA, LoopInfo *LI,
                                 const TargetLibraryInfo *TLI,
                                 DominatorTree *DT) {
  typedef SmallPtrSet<Value*, 16> ValueSet;

  // Holds the Load and Store instructions.
  SmallVector<LoadInst *, 16> Loads;
  SmallVector<StoreInst *, 16> Stores;

  // Holds all the different accesses in the loop.
  unsigned NumReads = 0;
  unsigned NumReadWrites = 0;

  PtrRtChecking->Pointers.clear();
  PtrRtChecking->Need = false;

  const bool IsAnnotatedParallel = TheLoop->isAnnotatedParallel();

  // For each block.
  for (BasicBlock *BB : TheLoop->blocks()) {
    // Scan the BB and collect legal loads and stores.
    for (Instruction &I : *BB) {
      // If this is a load, save it. If this instruction can read from memory
      // but is not a load, then we quit. Notice that we don't handle function
      // calls that read or write.
      if (I.mayReadFromMemory()) {
        // Many math library functions read the rounding mode. We will only
        // vectorize a loop if it contains known function calls that don't set
        // the flag. Therefore, it is safe to ignore this read from memory.
        auto *Call = dyn_cast<CallInst>(&I);
        if (Call && getVectorIntrinsicIDForCall(Call, TLI))
          continue;

        // If the function has an explicit vectorized counterpart, we can safely
        // assume that it can be vectorized.
        if (Call && !Call->isNoBuiltin() && Call->getCalledFunction() &&
            TLI->isFunctionVectorizable(Call->getCalledFunction()->getName()))
          continue;

        auto *Ld = dyn_cast<LoadInst>(&I);
        if (!Ld || (!Ld->isSimple() && !IsAnnotatedParallel)) {
          recordAnalysis("NonSimpleLoad", Ld)
              << "read with atomic ordering or volatile read";
          LLVM_DEBUG(dbgs() << "LAA: Found a non-simple load.\n");
          CanVecMem = false;
          return;
        }
        NumLoads++;
        Loads.push_back(Ld);
        DepChecker->addAccess(Ld);
        if (EnableMemAccessVersioning)
          collectStridedAccess(Ld);
        continue;
      }

      // Save 'store' instructions. Abort if other instructions write to memory.
      if (I.mayWriteToMemory()) {
        auto *St = dyn_cast<StoreInst>(&I);
        if (!St) {
          recordAnalysis("CantVectorizeInstruction", St)
              << "instruction cannot be vectorized";
          CanVecMem = false;
          return;
        }
        if (!St->isSimple() && !IsAnnotatedParallel) {
          recordAnalysis("NonSimpleStore", St)
              << "write with atomic ordering or volatile write";
          LLVM_DEBUG(dbgs() << "LAA: Found a non-simple store.\n");
          CanVecMem = false;
          return;
        }
        NumStores++;
        Stores.push_back(St);
        DepChecker->addAccess(St);
        if (EnableMemAccessVersioning)
          collectStridedAccess(St);
      }
    } // Next instr.
  } // Next block.

  // Now we have two lists that hold the loads and the stores.
  // Next, we find the pointers that they use.

  // Check if we see any stores. If there are no stores, then we don't
  // care if the pointers are *restrict*.
  if (!Stores.size()) {
    LLVM_DEBUG(dbgs() << "LAA: Found a read-only loop!\n");
    CanVecMem = true;
    return;
  }

  MemoryDepChecker::DepCandidates DependentAccesses;
  AccessAnalysis Accesses(TheLoop->getHeader()->getModule()->getDataLayout(),
                          TheLoop, AA, LI, DependentAccesses, *PSE);

  // Holds the analyzed pointers. We don't want to call GetUnderlyingObjects
  // multiple times on the same object. If the ptr is accessed twice, once
  // for read and once for write, it will only appear once (on the write
  // list). This is okay, since we are going to check for conflicts between
  // writes and between reads and writes, but not between reads and reads.
  ValueSet Seen;

  // Record uniform store addresses to identify if we have multiple stores
  // to the same address.
  ValueSet UniformStores;

  for (StoreInst *ST : Stores) {
    Value *Ptr = ST->getPointerOperand();

    if (isUniform(Ptr))
      HasDependenceInvolvingLoopInvariantAddress |=
          !UniformStores.insert(Ptr).second;

    // If we did *not* see this pointer before, insert it to  the read-write
    // list. At this phase it is only a 'write' list.
    if (Seen.insert(Ptr).second) {
      ++NumReadWrites;

      MemoryLocation Loc = MemoryLocation::get(ST);
      // The TBAA metadata could have a control dependency on the predication
      // condition, so we cannot rely on it when determining whether or not we
      // need runtime pointer checks.
      if (blockNeedsPredication(ST->getParent(), TheLoop, DT))
        Loc.AATags.TBAA = nullptr;

      Accesses.addStore(Loc);
    }
  }

  if (IsAnnotatedParallel) {
    LLVM_DEBUG(
        dbgs() << "LAA: A loop annotated parallel, ignore memory dependency "
               << "checks.\n");
    CanVecMem = true;
    return;
  }

  for (LoadInst *LD : Loads) {
    Value *Ptr = LD->getPointerOperand();
    // If we did *not* see this pointer before, insert it to the
    // read list. If we *did* see it before, then it is already in
    // the read-write list. This allows us to vectorize expressions
    // such as A[i] += x;  Because the address of A[i] is a read-write
    // pointer. This only works if the index of A[i] is consecutive.
    // If the address of i is unknown (for example A[B[i]]) then we may
    // read a few words, modify, and write a few words, and some of the
    // words may be written to the same address.
    bool IsReadOnlyPtr = false;
    if (Seen.insert(Ptr).second ||
        !getPtrStride(*PSE, Ptr, TheLoop, SymbolicStrides)) {
      ++NumReads;
      IsReadOnlyPtr = true;
    }

    // See if there is an unsafe dependency between a load to a uniform address and
    // store to the same uniform address.
    if (UniformStores.count(Ptr)) {
      LLVM_DEBUG(dbgs() << "LAA: Found an unsafe dependency between a uniform "
                           "load and uniform store to the same address!\n");
      HasDependenceInvolvingLoopInvariantAddress = true;
    }

    MemoryLocation Loc = MemoryLocation::get(LD);
    // The TBAA metadata could have a control dependency on the predication
    // condition, so we cannot rely on it when determining whether or not we
    // need runtime pointer checks.
    if (blockNeedsPredication(LD->getParent(), TheLoop, DT))
      Loc.AATags.TBAA = nullptr;

    Accesses.addLoad(Loc, IsReadOnlyPtr);
  }

  // If we write (or read-write) to a single destination and there are no
  // other reads in this loop then is it safe to vectorize.
  if (NumReadWrites == 1 && NumReads == 0) {
    LLVM_DEBUG(dbgs() << "LAA: Found a write-only loop!\n");
    CanVecMem = true;
    return;
  }

  // Build dependence sets and check whether we need a runtime pointer bounds
  // check.
  Accesses.buildDependenceSets();

  // Find pointers with computable bounds. We are going to use this information
  // to place a runtime bound check.
  bool CanDoRTIfNeeded = Accesses.canCheckPtrAtRT(*PtrRtChecking, PSE->getSE(),
                                                  TheLoop, SymbolicStrides);
  if (!CanDoRTIfNeeded) {
    recordAnalysis("CantIdentifyArrayBounds") << "cannot identify array bounds";
    LLVM_DEBUG(dbgs() << "LAA: We can't vectorize because we can't find "
                      << "the array bounds.\n");
    CanVecMem = false;
    return;
  }

  LLVM_DEBUG(
      dbgs() << "LAA: We can perform a memory runtime check if needed.\n");

  CanVecMem = true;
  if (Accesses.isDependencyCheckNeeded()) {
    LLVM_DEBUG(dbgs() << "LAA: Checking memory dependencies\n");
    CanVecMem = DepChecker->areDepsSafe(
        DependentAccesses, Accesses.getDependenciesToCheck(), SymbolicStrides);
    MaxSafeDepDistBytes = DepChecker->getMaxSafeDepDistBytes();

    if (!CanVecMem && DepChecker->shouldRetryWithRuntimeCheck()) {
      LLVM_DEBUG(dbgs() << "LAA: Retrying with memory checks\n");

      // Clear the dependency checks. We assume they are not needed.
      Accesses.resetDepChecks(*DepChecker);

      PtrRtChecking->reset();
      PtrRtChecking->Need = true;

      auto *SE = PSE->getSE();
      CanDoRTIfNeeded = Accesses.canCheckPtrAtRT(*PtrRtChecking, SE, TheLoop,
                                                 SymbolicStrides, true);

      // Check that we found the bounds for the pointer.
      if (!CanDoRTIfNeeded) {
        recordAnalysis("CantCheckMemDepsAtRunTime")
            << "cannot check memory dependencies at runtime";
        LLVM_DEBUG(dbgs() << "LAA: Can't vectorize with memory checks\n");
        CanVecMem = false;
        return;
      }

      CanVecMem = true;
    }
  }

  if (CanVecMem)
    LLVM_DEBUG(
        dbgs() << "LAA: No unsafe dependent memory operations in loop.  We"
               << (PtrRtChecking->Need ? "" : " don't")
               << " need runtime memory checks.\n");
  else {
    recordAnalysis("UnsafeMemDep")
        << "unsafe dependent memory operations in loop. Use "
           "#pragma loop distribute(enable) to allow loop distribution "
           "to attempt to isolate the offending operations into a separate "
           "loop";
    LLVM_DEBUG(dbgs() << "LAA: unsafe dependent memory operations in loop\n");
  }
}

bool LoopAccessInfo::blockNeedsPredication(BasicBlock *BB, Loop *TheLoop,
                                           DominatorTree *DT)  {
  assert(TheLoop->contains(BB) && "Unknown block used");

  // Blocks that do not dominate the latch need predication.
  BasicBlock* Latch = TheLoop->getLoopLatch();
  return !DT->dominates(BB, Latch);
}

OptimizationRemarkAnalysis &LoopAccessInfo::recordAnalysis(StringRef RemarkName,
                                                           Instruction *I) {
  assert(!Report && "Multiple reports generated");

  Value *CodeRegion = TheLoop->getHeader();
  DebugLoc DL = TheLoop->getStartLoc();

  if (I) {
    CodeRegion = I->getParent();
    // If there is no debug location attached to the instruction, revert back to
    // using the loop's.
    if (I->getDebugLoc())
      DL = I->getDebugLoc();
  }

  Report = make_unique<OptimizationRemarkAnalysis>(DEBUG_TYPE, RemarkName, DL,
                                                   CodeRegion);
  return *Report;
}

bool LoopAccessInfo::isUniform(Value *V) const {
  auto *SE = PSE->getSE();
  // Since we rely on SCEV for uniformity, if the type is not SCEVable, it is
  // never considered uniform.
  // TODO: Is this really what we want? Even without FP SCEV, we may want some
  // trivially loop-invariant FP values to be considered uniform.
  if (!SE->isSCEVable(V->getType()))
    return false;
  return (SE->isLoopInvariant(SE->getSCEV(V), TheLoop));
}

// FIXME: this function is currently a duplicate of the one in
// LoopVectorize.cpp.
static Instruction *getFirstInst(Instruction *FirstInst, Value *V,
                                 Instruction *Loc) {
  if (FirstInst)
    return FirstInst;
  if (Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent() == Loc->getParent() ? I : nullptr;
  return nullptr;
}

namespace {

/// IR Values for the lower and upper bounds of a pointer evolution.  We
/// need to use value-handles because SCEV expansion can invalidate previously
/// expanded values.  Thus expansion of a pointer can invalidate the bounds for
/// a previous one.
struct PointerBounds {
  TrackingVH<Value> Start;
  TrackingVH<Value> End;
};

} // end anonymous namespace

/// Expand code for the lower and upper bound of the pointer group \p CG
/// in \p TheLoop.  \return the values for the bounds.
static PointerBounds
expandBounds(const RuntimePointerChecking::CheckingPtrGroup *CG, Loop *TheLoop,
             Instruction *Loc, SCEVExpander &Exp, ScalarEvolution *SE,
             const RuntimePointerChecking &PtrRtChecking) {
  Value *Ptr = PtrRtChecking.Pointers[CG->Members[0]].PointerValue;
  const SCEV *Sc = SE->getSCEV(Ptr);

  unsigned AS = Ptr->getType()->getPointerAddressSpace();
  LLVMContext &Ctx = Loc->getContext();

  // Use this type for pointer arithmetic.
  Type *PtrArithTy = Type::getInt8PtrTy(Ctx, AS);

  if (SE->isLoopInvariant(Sc, TheLoop)) {
    LLVM_DEBUG(dbgs() << "LAA: Adding RT check for a loop invariant ptr:"
                      << *Ptr << "\n");
    // Ptr could be in the loop body. If so, expand a new one at the correct
    // location.
    Instruction *Inst = dyn_cast<Instruction>(Ptr);
    Value *NewPtr = (Inst && TheLoop->contains(Inst))
                        ? Exp.expandCodeFor(Sc, PtrArithTy, Loc)
                        : Ptr;
    // We must return a half-open range, which means incrementing Sc.
    const SCEV *ScPlusOne = SE->getAddExpr(Sc, SE->getOne(PtrArithTy));
    Value *NewPtrPlusOne = Exp.expandCodeFor(ScPlusOne, PtrArithTy, Loc);
    return {NewPtr, NewPtrPlusOne};
  } else {
    Value *Start = nullptr, *End = nullptr;
    LLVM_DEBUG(dbgs() << "LAA: Adding RT check for range:\n");
    Start = Exp.expandCodeFor(CG->Low, PtrArithTy, Loc);
    End = Exp.expandCodeFor(CG->High, PtrArithTy, Loc);
    LLVM_DEBUG(dbgs() << "Start: " << *CG->Low << " End: " << *CG->High
                      << "\n");
    return {Start, End};
  }
}

/// Turns a collection of checks into a collection of expanded upper and
/// lower bounds for both pointers in the check.
static SmallVector<std::pair<PointerBounds, PointerBounds>, 4> expandBounds(
    const SmallVectorImpl<RuntimePointerChecking::PointerCheck> &PointerChecks,
    Loop *L, Instruction *Loc, ScalarEvolution *SE, SCEVExpander &Exp,
    const RuntimePointerChecking &PtrRtChecking) {
  SmallVector<std::pair<PointerBounds, PointerBounds>, 4> ChecksWithBounds;

  // Here we're relying on the SCEV Expander's cache to only emit code for the
  // same bounds once.
  transform(
      PointerChecks, std::back_inserter(ChecksWithBounds),
      [&](const RuntimePointerChecking::PointerCheck &Check) {
        PointerBounds
          First = expandBounds(Check.first, L, Loc, Exp, SE, PtrRtChecking),
          Second = expandBounds(Check.second, L, Loc, Exp, SE, PtrRtChecking);
        return std::make_pair(First, Second);
      });

  return ChecksWithBounds;
}

std::pair<Instruction *, Instruction *> LoopAccessInfo::addRuntimeChecks(
    Instruction *Loc,
    const SmallVectorImpl<RuntimePointerChecking::PointerCheck> &PointerChecks)
    const {
  const DataLayout &DL = TheLoop->getHeader()->getModule()->getDataLayout();
  auto *SE = PSE->getSE();
  SCEVExpander Exp(*SE, DL, "induction");
  auto ExpandedChecks =
      expandBounds(PointerChecks, TheLoop, Loc, SE, Exp, *PtrRtChecking);

  LLVMContext &Ctx = Loc->getContext();
  Instruction *FirstInst = nullptr;
  IRBuilder<> ChkBuilder(Loc);
  // Our instructions might fold to a constant.
  Value *MemoryRuntimeCheck = nullptr;

  for (const auto &Check : ExpandedChecks) {
    const PointerBounds &A = Check.first, &B = Check.second;
    // Check if two pointers (A and B) conflict where conflict is computed as:
    // start(A) <= end(B) && start(B) <= end(A)
    unsigned AS0 = A.Start->getType()->getPointerAddressSpace();
    unsigned AS1 = B.Start->getType()->getPointerAddressSpace();

    assert((AS0 == B.End->getType()->getPointerAddressSpace()) &&
           (AS1 == A.End->getType()->getPointerAddressSpace()) &&
           "Trying to bounds check pointers with different address spaces");

    Type *PtrArithTy0 = Type::getInt8PtrTy(Ctx, AS0);
    Type *PtrArithTy1 = Type::getInt8PtrTy(Ctx, AS1);

    Value *Start0 = ChkBuilder.CreateBitCast(A.Start, PtrArithTy0, "bc");
    Value *Start1 = ChkBuilder.CreateBitCast(B.Start, PtrArithTy1, "bc");
    Value *End0 =   ChkBuilder.CreateBitCast(A.End,   PtrArithTy1, "bc");
    Value *End1 =   ChkBuilder.CreateBitCast(B.End,   PtrArithTy0, "bc");

    // [A|B].Start points to the first accessed byte under base [A|B].
    // [A|B].End points to the last accessed byte, plus one.
    // There is no conflict when the intervals are disjoint:
    // NoConflict = (B.Start >= A.End) || (A.Start >= B.End)
    //
    // bound0 = (B.Start < A.End)
    // bound1 = (A.Start < B.End)
    //  IsConflict = bound0 & bound1
    Value *Cmp0 = ChkBuilder.CreateICmpULT(Start0, End1, "bound0");
    FirstInst = getFirstInst(FirstInst, Cmp0, Loc);
    Value *Cmp1 = ChkBuilder.CreateICmpULT(Start1, End0, "bound1");
    FirstInst = getFirstInst(FirstInst, Cmp1, Loc);
    Value *IsConflict = ChkBuilder.CreateAnd(Cmp0, Cmp1, "found.conflict");
    FirstInst = getFirstInst(FirstInst, IsConflict, Loc);
    if (MemoryRuntimeCheck) {
      IsConflict =
          ChkBuilder.CreateOr(MemoryRuntimeCheck, IsConflict, "conflict.rdx");
      FirstInst = getFirstInst(FirstInst, IsConflict, Loc);
    }
    MemoryRuntimeCheck = IsConflict;
  }

  if (!MemoryRuntimeCheck)
    return std::make_pair(nullptr, nullptr);

  // We have to do this trickery because the IRBuilder might fold the check to a
  // constant expression in which case there is no Instruction anchored in a
  // the block.
  Instruction *Check = BinaryOperator::CreateAnd(MemoryRuntimeCheck,
                                                 ConstantInt::getTrue(Ctx));
  ChkBuilder.Insert(Check, "memcheck.conflict");
  FirstInst = getFirstInst(FirstInst, Check, Loc);
  return std::make_pair(FirstInst, Check);
}

std::pair<Instruction *, Instruction *>
LoopAccessInfo::addRuntimeChecks(Instruction *Loc) const {
  if (!PtrRtChecking->Need)
    return std::make_pair(nullptr, nullptr);

  return addRuntimeChecks(Loc, PtrRtChecking->getChecks());
}

void LoopAccessInfo::collectStridedAccess(Value *MemAccess) {
  Value *Ptr = nullptr;
  if (LoadInst *LI = dyn_cast<LoadInst>(MemAccess))
    Ptr = LI->getPointerOperand();
  else if (StoreInst *SI = dyn_cast<StoreInst>(MemAccess))
    Ptr = SI->getPointerOperand();
  else
    return;

  Value *Stride = getStrideFromPointer(Ptr, PSE->getSE(), TheLoop);
  if (!Stride)
    return;

  LLVM_DEBUG(dbgs() << "LAA: Found a strided access that is a candidate for "
                       "versioning:");
  LLVM_DEBUG(dbgs() << "  Ptr: " << *Ptr << " Stride: " << *Stride << "\n");

  // Avoid adding the "Stride == 1" predicate when we know that
  // Stride >= Trip-Count. Such a predicate will effectively optimize a single
  // or zero iteration loop, as Trip-Count <= Stride == 1.
  //
  // TODO: We are currently not making a very informed decision on when it is
  // beneficial to apply stride versioning. It might make more sense that the
  // users of this analysis (such as the vectorizer) will trigger it, based on
  // their specific cost considerations; For example, in cases where stride
  // versioning does  not help resolving memory accesses/dependences, the
  // vectorizer should evaluate the cost of the runtime test, and the benefit
  // of various possible stride specializations, considering the alternatives
  // of using gather/scatters (if available).

  const SCEV *StrideExpr = PSE->getSCEV(Stride);
  const SCEV *BETakenCount = PSE->getBackedgeTakenCount();

  // Match the types so we can compare the stride and the BETakenCount.
  // The Stride can be positive/negative, so we sign extend Stride;
  // The backdgeTakenCount is non-negative, so we zero extend BETakenCount.
  const DataLayout &DL = TheLoop->getHeader()->getModule()->getDataLayout();
  uint64_t StrideTypeSize = DL.getTypeAllocSize(StrideExpr->getType());
  uint64_t BETypeSize = DL.getTypeAllocSize(BETakenCount->getType());
  const SCEV *CastedStride = StrideExpr;
  const SCEV *CastedBECount = BETakenCount;
  ScalarEvolution *SE = PSE->getSE();
  if (BETypeSize >= StrideTypeSize)
    CastedStride = SE->getNoopOrSignExtend(StrideExpr, BETakenCount->getType());
  else
    CastedBECount = SE->getZeroExtendExpr(BETakenCount, StrideExpr->getType());
  const SCEV *StrideMinusBETaken = SE->getMinusSCEV(CastedStride, CastedBECount);
  // Since TripCount == BackEdgeTakenCount + 1, checking:
  // "Stride >= TripCount" is equivalent to checking:
  // Stride - BETakenCount > 0
  if (SE->isKnownPositive(StrideMinusBETaken)) {
    LLVM_DEBUG(
        dbgs() << "LAA: Stride>=TripCount; No point in versioning as the "
                  "Stride==1 predicate will imply that the loop executes "
                  "at most once.\n");
    return;
  }
  LLVM_DEBUG(dbgs() << "LAA: Found a strided access that we can version.");

  SymbolicStrides[Ptr] = Stride;
  StrideSet.insert(Stride);
}

LoopAccessInfo::LoopAccessInfo(Loop *L, ScalarEvolution *SE,
                               const TargetLibraryInfo *TLI, AliasAnalysis *AA,
                               DominatorTree *DT, LoopInfo *LI)
    : PSE(llvm::make_unique<PredicatedScalarEvolution>(*SE, *L)),
      PtrRtChecking(llvm::make_unique<RuntimePointerChecking>(SE)),
      DepChecker(llvm::make_unique<MemoryDepChecker>(*PSE, L)), TheLoop(L),
      NumLoads(0), NumStores(0), MaxSafeDepDistBytes(-1), CanVecMem(false),
      HasDependenceInvolvingLoopInvariantAddress(false) {
  if (canAnalyzeLoop())
    analyzeLoop(AA, LI, TLI, DT);
}

void LoopAccessInfo::print(raw_ostream &OS, unsigned Depth) const {
  if (CanVecMem) {
    OS.indent(Depth) << "Memory dependences are safe";
    if (MaxSafeDepDistBytes != -1ULL)
      OS << " with a maximum dependence distance of " << MaxSafeDepDistBytes
         << " bytes";
    if (PtrRtChecking->Need)
      OS << " with run-time checks";
    OS << "\n";
  }

  if (Report)
    OS.indent(Depth) << "Report: " << Report->getMsg() << "\n";

  if (auto *Dependences = DepChecker->getDependences()) {
    OS.indent(Depth) << "Dependences:\n";
    for (auto &Dep : *Dependences) {
      Dep.print(OS, Depth + 2, DepChecker->getMemoryInstructions());
      OS << "\n";
    }
  } else
    OS.indent(Depth) << "Too many dependences, not recorded\n";

  // List the pair of accesses need run-time checks to prove independence.
  PtrRtChecking->print(OS, Depth);
  OS << "\n";

  OS.indent(Depth) << "Non vectorizable stores to invariant address were "
                   << (HasDependenceInvolvingLoopInvariantAddress ? "" : "not ")
                   << "found in loop.\n";

  OS.indent(Depth) << "SCEV assumptions:\n";
  PSE->getUnionPredicate().print(OS, Depth);

  OS << "\n";

  OS.indent(Depth) << "Expressions re-written:\n";
  PSE->print(OS, Depth);
}

const LoopAccessInfo &LoopAccessLegacyAnalysis::getInfo(Loop *L) {
  auto &LAI = LoopAccessInfoMap[L];

  if (!LAI)
    LAI = llvm::make_unique<LoopAccessInfo>(L, SE, TLI, AA, DT, LI);

  return *LAI.get();
}

void LoopAccessLegacyAnalysis::print(raw_ostream &OS, const Module *M) const {
  LoopAccessLegacyAnalysis &LAA = *const_cast<LoopAccessLegacyAnalysis *>(this);

  for (Loop *TopLevelLoop : *LI)
    for (Loop *L : depth_first(TopLevelLoop)) {
      OS.indent(2) << L->getHeader()->getName() << ":\n";
      auto &LAI = LAA.getInfo(L);
      LAI.print(OS, 4);
    }
}

bool LoopAccessLegacyAnalysis::runOnFunction(Function &F) {
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto *TLIP = getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
  TLI = TLIP ? &TLIP->getTLI() : nullptr;
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

  return false;
}

void LoopAccessLegacyAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();

    AU.setPreservesAll();
}

char LoopAccessLegacyAnalysis::ID = 0;
static const char laa_name[] = "Loop Access Analysis";
#define LAA_NAME "loop-accesses"

INITIALIZE_PASS_BEGIN(LoopAccessLegacyAnalysis, LAA_NAME, laa_name, false, true)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(LoopAccessLegacyAnalysis, LAA_NAME, laa_name, false, true)

AnalysisKey LoopAccessAnalysis::Key;

LoopAccessInfo LoopAccessAnalysis::run(Loop &L, LoopAnalysisManager &AM,
                                       LoopStandardAnalysisResults &AR) {
  return LoopAccessInfo(&L, &AR.SE, &AR.TLI, &AR.AA, &AR.DT, &AR.LI);
}

namespace llvm {

  Pass *createLAAPass() {
    return new LoopAccessLegacyAnalysis();
  }

} // end namespace llvm
