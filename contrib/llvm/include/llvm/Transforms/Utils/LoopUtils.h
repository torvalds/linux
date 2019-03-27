//===- llvm/Transforms/Utils/LoopUtils.h - Loop utilities -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some loop transformation utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOOPUTILS_H
#define LLVM_TRANSFORMS_UTILS_LOOPUTILS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"

namespace llvm {

class AliasSet;
class AliasSetTracker;
class BasicBlock;
class DataLayout;
class Loop;
class LoopInfo;
class MemorySSAUpdater;
class OptimizationRemarkEmitter;
class PredicatedScalarEvolution;
class PredIteratorCache;
class ScalarEvolution;
class SCEV;
class TargetLibraryInfo;
class TargetTransformInfo;

BasicBlock *InsertPreheaderForLoop(Loop *L, DominatorTree *DT, LoopInfo *LI,
                                   bool PreserveLCSSA);

/// Ensure that all exit blocks of the loop are dedicated exits.
///
/// For any loop exit block with non-loop predecessors, we split the loop
/// predecessors to use a dedicated loop exit block. We update the dominator
/// tree and loop info if provided, and will preserve LCSSA if requested.
bool formDedicatedExitBlocks(Loop *L, DominatorTree *DT, LoopInfo *LI,
                             bool PreserveLCSSA);

/// Ensures LCSSA form for every instruction from the Worklist in the scope of
/// innermost containing loop.
///
/// For the given instruction which have uses outside of the loop, an LCSSA PHI
/// node is inserted and the uses outside the loop are rewritten to use this
/// node.
///
/// LoopInfo and DominatorTree are required and, since the routine makes no
/// changes to CFG, preserved.
///
/// Returns true if any modifications are made.
bool formLCSSAForInstructions(SmallVectorImpl<Instruction *> &Worklist,
                              DominatorTree &DT, LoopInfo &LI);

/// Put loop into LCSSA form.
///
/// Looks at all instructions in the loop which have uses outside of the
/// current loop. For each, an LCSSA PHI node is inserted and the uses outside
/// the loop are rewritten to use this node.
///
/// LoopInfo and DominatorTree are required and preserved.
///
/// If ScalarEvolution is passed in, it will be preserved.
///
/// Returns true if any modifications are made to the loop.
bool formLCSSA(Loop &L, DominatorTree &DT, LoopInfo *LI, ScalarEvolution *SE);

/// Put a loop nest into LCSSA form.
///
/// This recursively forms LCSSA for a loop nest.
///
/// LoopInfo and DominatorTree are required and preserved.
///
/// If ScalarEvolution is passed in, it will be preserved.
///
/// Returns true if any modifications are made to the loop.
bool formLCSSARecursively(Loop &L, DominatorTree &DT, LoopInfo *LI,
                          ScalarEvolution *SE);

/// Walk the specified region of the CFG (defined by all blocks
/// dominated by the specified block, and that are in the current loop) in
/// reverse depth first order w.r.t the DominatorTree. This allows us to visit
/// uses before definitions, allowing us to sink a loop body in one pass without
/// iteration. Takes DomTreeNode, AliasAnalysis, LoopInfo, DominatorTree,
/// DataLayout, TargetLibraryInfo, Loop, AliasSet information for all
/// instructions of the loop and loop safety information as
/// arguments. Diagnostics is emitted via \p ORE. It returns changed status.
bool sinkRegion(DomTreeNode *, AliasAnalysis *, LoopInfo *, DominatorTree *,
                TargetLibraryInfo *, TargetTransformInfo *, Loop *,
                AliasSetTracker *, MemorySSAUpdater *, ICFLoopSafetyInfo *,
                OptimizationRemarkEmitter *ORE);

/// Walk the specified region of the CFG (defined by all blocks
/// dominated by the specified block, and that are in the current loop) in depth
/// first order w.r.t the DominatorTree.  This allows us to visit definitions
/// before uses, allowing us to hoist a loop body in one pass without iteration.
/// Takes DomTreeNode, AliasAnalysis, LoopInfo, DominatorTree, DataLayout,
/// TargetLibraryInfo, Loop, AliasSet information for all instructions of the
/// loop and loop safety information as arguments. Diagnostics is emitted via \p
/// ORE. It returns changed status.
bool hoistRegion(DomTreeNode *, AliasAnalysis *, LoopInfo *, DominatorTree *,
                 TargetLibraryInfo *, Loop *, AliasSetTracker *,
                 MemorySSAUpdater *, ICFLoopSafetyInfo *,
                 OptimizationRemarkEmitter *ORE);

/// This function deletes dead loops. The caller of this function needs to
/// guarantee that the loop is infact dead.
/// The function requires a bunch or prerequisites to be present:
///   - The loop needs to be in LCSSA form
///   - The loop needs to have a Preheader
///   - A unique dedicated exit block must exist
///
/// This also updates the relevant analysis information in \p DT, \p SE, and \p
/// LI if pointers to those are provided.
/// It also updates the loop PM if an updater struct is provided.

void deleteDeadLoop(Loop *L, DominatorTree *DT, ScalarEvolution *SE,
                    LoopInfo *LI);

/// Try to promote memory values to scalars by sinking stores out of
/// the loop and moving loads to before the loop.  We do this by looping over
/// the stores in the loop, looking for stores to Must pointers which are
/// loop invariant. It takes a set of must-alias values, Loop exit blocks
/// vector, loop exit blocks insertion point vector, PredIteratorCache,
/// LoopInfo, DominatorTree, Loop, AliasSet information for all instructions
/// of the loop and loop safety information as arguments.
/// Diagnostics is emitted via \p ORE. It returns changed status.
bool promoteLoopAccessesToScalars(const SmallSetVector<Value *, 8> &,
                                  SmallVectorImpl<BasicBlock *> &,
                                  SmallVectorImpl<Instruction *> &,
                                  PredIteratorCache &, LoopInfo *,
                                  DominatorTree *, const TargetLibraryInfo *,
                                  Loop *, AliasSetTracker *,
                                  ICFLoopSafetyInfo *,
                                  OptimizationRemarkEmitter *);

/// Does a BFS from a given node to all of its children inside a given loop.
/// The returned vector of nodes includes the starting point.
SmallVector<DomTreeNode *, 16> collectChildrenInLoop(DomTreeNode *N,
                                                     const Loop *CurLoop);

/// Returns the instructions that use values defined in the loop.
SmallVector<Instruction *, 8> findDefsUsedOutsideOfLoop(Loop *L);

/// Find string metadata for loop
///
/// If it has a value (e.g. {"llvm.distribute", 1} return the value as an
/// operand or null otherwise.  If the string metadata is not found return
/// Optional's not-a-value.
Optional<const MDOperand *> findStringMetadataForLoop(const Loop *TheLoop,
                                                      StringRef Name);

/// Find named metadata for a loop with an integer value.
llvm::Optional<int> getOptionalIntLoopAttribute(Loop *TheLoop, StringRef Name);

/// Create a new loop identifier for a loop created from a loop transformation.
///
/// @param OrigLoopID The loop ID of the loop before the transformation.
/// @param FollowupAttrs List of attribute names that contain attributes to be
///                      added to the new loop ID.
/// @param InheritOptionsAttrsPrefix Selects which attributes should be inherited
///                                  from the original loop. The following values
///                                  are considered:
///        nullptr   : Inherit all attributes from @p OrigLoopID.
///        ""        : Do not inherit any attribute from @p OrigLoopID; only use
///                    those specified by a followup attribute.
///        "<prefix>": Inherit all attributes except those which start with
///                    <prefix>; commonly used to remove metadata for the
///                    applied transformation.
/// @param AlwaysNew If true, do not try to reuse OrigLoopID and never return
///                  None.
///
/// @return The loop ID for the after-transformation loop. The following values
///         can be returned:
///         None         : No followup attribute was found; it is up to the
///                        transformation to choose attributes that make sense.
///         @p OrigLoopID: The original identifier can be reused.
///         nullptr      : The new loop has no attributes.
///         MDNode*      : A new unique loop identifier.
Optional<MDNode *>
makeFollowupLoopID(MDNode *OrigLoopID, ArrayRef<StringRef> FollowupAttrs,
                   const char *InheritOptionsAttrsPrefix = "",
                   bool AlwaysNew = false);

/// Look for the loop attribute that disables all transformation heuristic.
bool hasDisableAllTransformsHint(const Loop *L);

/// The mode sets how eager a transformation should be applied.
enum TransformationMode {
  /// The pass can use heuristics to determine whether a transformation should
  /// be applied.
  TM_Unspecified,

  /// The transformation should be applied without considering a cost model.
  TM_Enable,

  /// The transformation should not be applied.
  TM_Disable,

  /// Force is a flag and should not be used alone.
  TM_Force = 0x04,

  /// The transformation was directed by the user, e.g. by a #pragma in
  /// the source code. If the transformation could not be applied, a
  /// warning should be emitted.
  TM_ForcedByUser = TM_Enable | TM_Force,

  /// The transformation must not be applied. For instance, `#pragma clang loop
  /// unroll(disable)` explicitly forbids any unrolling to take place. Unlike
  /// general loop metadata, it must not be dropped. Most passes should not
  /// behave differently under TM_Disable and TM_SuppressedByUser.
  TM_SuppressedByUser = TM_Disable | TM_Force
};

/// @{
/// Get the mode for LLVM's supported loop transformations.
TransformationMode hasUnrollTransformation(Loop *L);
TransformationMode hasUnrollAndJamTransformation(Loop *L);
TransformationMode hasVectorizeTransformation(Loop *L);
TransformationMode hasDistributeTransformation(Loop *L);
TransformationMode hasLICMVersioningTransformation(Loop *L);
/// @}

/// Set input string into loop metadata by keeping other values intact.
void addStringMetadataToLoop(Loop *TheLoop, const char *MDString,
                             unsigned V = 0);

/// Get a loop's estimated trip count based on branch weight metadata.
/// Returns 0 when the count is estimated to be 0, or None when a meaningful
/// estimate can not be made.
Optional<unsigned> getLoopEstimatedTripCount(Loop *L);

/// Check inner loop (L) backedge count is known to be invariant on all
/// iterations of its outer loop. If the loop has no parent, this is trivially
/// true.
bool hasIterationCountInvariantInParent(Loop *L, ScalarEvolution &SE);

/// Helper to consistently add the set of standard passes to a loop pass's \c
/// AnalysisUsage.
///
/// All loop passes should call this as part of implementing their \c
/// getAnalysisUsage.
void getLoopAnalysisUsage(AnalysisUsage &AU);

/// Returns true if is legal to hoist or sink this instruction disregarding the
/// possible introduction of faults.  Reasoning about potential faulting
/// instructions is the responsibility of the caller since it is challenging to
/// do efficiently from within this routine.
/// \p TargetExecutesOncePerLoop is true only when it is guaranteed that the
/// target executes at most once per execution of the loop body.  This is used
/// to assess the legality of duplicating atomic loads.  Generally, this is
/// true when moving out of loop and not true when moving into loops.
/// If \p ORE is set use it to emit optimization remarks.
bool canSinkOrHoistInst(Instruction &I, AAResults *AA, DominatorTree *DT,
                        Loop *CurLoop, AliasSetTracker *CurAST,
                        MemorySSAUpdater *MSSAU, bool TargetExecutesOncePerLoop,
                        OptimizationRemarkEmitter *ORE = nullptr);

/// Returns a Min/Max operation corresponding to MinMaxRecurrenceKind.
Value *createMinMaxOp(IRBuilder<> &Builder,
                      RecurrenceDescriptor::MinMaxRecurrenceKind RK,
                      Value *Left, Value *Right);

/// Generates an ordered vector reduction using extracts to reduce the value.
Value *
getOrderedReduction(IRBuilder<> &Builder, Value *Acc, Value *Src, unsigned Op,
                    RecurrenceDescriptor::MinMaxRecurrenceKind MinMaxKind =
                        RecurrenceDescriptor::MRK_Invalid,
                    ArrayRef<Value *> RedOps = None);

/// Generates a vector reduction using shufflevectors to reduce the value.
Value *getShuffleReduction(IRBuilder<> &Builder, Value *Src, unsigned Op,
                           RecurrenceDescriptor::MinMaxRecurrenceKind
                               MinMaxKind = RecurrenceDescriptor::MRK_Invalid,
                           ArrayRef<Value *> RedOps = None);

/// Create a target reduction of the given vector. The reduction operation
/// is described by the \p Opcode parameter. min/max reductions require
/// additional information supplied in \p Flags.
/// The target is queried to determine if intrinsics or shuffle sequences are
/// required to implement the reduction.
Value *createSimpleTargetReduction(IRBuilder<> &B,
                                   const TargetTransformInfo *TTI,
                                   unsigned Opcode, Value *Src,
                                   TargetTransformInfo::ReductionFlags Flags =
                                       TargetTransformInfo::ReductionFlags(),
                                   ArrayRef<Value *> RedOps = None);

/// Create a generic target reduction using a recurrence descriptor \p Desc
/// The target is queried to determine if intrinsics or shuffle sequences are
/// required to implement the reduction.
Value *createTargetReduction(IRBuilder<> &B, const TargetTransformInfo *TTI,
                             RecurrenceDescriptor &Desc, Value *Src,
                             bool NoNaN = false);

/// Get the intersection (logical and) of all of the potential IR flags
/// of each scalar operation (VL) that will be converted into a vector (I).
/// If OpValue is non-null, we only consider operations similar to OpValue
/// when intersecting.
/// Flag set: NSW, NUW, exact, and all of fast-math.
void propagateIRFlags(Value *I, ArrayRef<Value *> VL, Value *OpValue = nullptr);

/// Returns true if we can prove that \p S is defined and always negative in
/// loop \p L.
bool isKnownNegativeInLoop(const SCEV *S, const Loop *L, ScalarEvolution &SE);

/// Returns true if we can prove that \p S is defined and always non-negative in
/// loop \p L.
bool isKnownNonNegativeInLoop(const SCEV *S, const Loop *L,
                              ScalarEvolution &SE);

/// Returns true if \p S is defined and never is equal to signed/unsigned max.
bool cannotBeMaxInLoop(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                       bool Signed);

/// Returns true if \p S is defined and never is equal to signed/unsigned min.
bool cannotBeMinInLoop(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                       bool Signed);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOOPUTILS_H
