//===- Local.h - Functions to perform local transformations -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LOCAL_H
#define LLVM_TRANSFORMS_UTILS_LOCAL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cstdint>
#include <limits>

namespace llvm {

class AllocaInst;
class AssumptionCache;
class BasicBlock;
class BranchInst;
class CallInst;
class DbgVariableIntrinsic;
class DbgValueInst;
class DIBuilder;
class Function;
class Instruction;
class LazyValueInfo;
class LoadInst;
class MDNode;
class MemorySSAUpdater;
class PHINode;
class StoreInst;
class TargetLibraryInfo;
class TargetTransformInfo;

/// A set of parameters used to control the transforms in the SimplifyCFG pass.
/// Options may change depending on the position in the optimization pipeline.
/// For example, canonical form that includes switches and branches may later be
/// replaced by lookup tables and selects.
struct SimplifyCFGOptions {
  int BonusInstThreshold;
  bool ForwardSwitchCondToPhi;
  bool ConvertSwitchToLookupTable;
  bool NeedCanonicalLoop;
  bool SinkCommonInsts;
  AssumptionCache *AC;

  SimplifyCFGOptions(unsigned BonusThreshold = 1,
                     bool ForwardSwitchCond = false,
                     bool SwitchToLookup = false, bool CanonicalLoops = true,
                     bool SinkCommon = false,
                     AssumptionCache *AssumpCache = nullptr)
      : BonusInstThreshold(BonusThreshold),
        ForwardSwitchCondToPhi(ForwardSwitchCond),
        ConvertSwitchToLookupTable(SwitchToLookup),
        NeedCanonicalLoop(CanonicalLoops),
        SinkCommonInsts(SinkCommon),
        AC(AssumpCache) {}

  // Support 'builder' pattern to set members by name at construction time.
  SimplifyCFGOptions &bonusInstThreshold(int I) {
    BonusInstThreshold = I;
    return *this;
  }
  SimplifyCFGOptions &forwardSwitchCondToPhi(bool B) {
    ForwardSwitchCondToPhi = B;
    return *this;
  }
  SimplifyCFGOptions &convertSwitchToLookupTable(bool B) {
    ConvertSwitchToLookupTable = B;
    return *this;
  }
  SimplifyCFGOptions &needCanonicalLoops(bool B) {
    NeedCanonicalLoop = B;
    return *this;
  }
  SimplifyCFGOptions &sinkCommonInsts(bool B) {
    SinkCommonInsts = B;
    return *this;
  }
  SimplifyCFGOptions &setAssumptionCache(AssumptionCache *Cache) {
    AC = Cache;
    return *this;
  }
};

//===----------------------------------------------------------------------===//
//  Local constant propagation.
//

/// If a terminator instruction is predicated on a constant value, convert it
/// into an unconditional branch to the constant destination.
/// This is a nontrivial operation because the successors of this basic block
/// must have their PHI nodes updated.
/// Also calls RecursivelyDeleteTriviallyDeadInstructions() on any branch/switch
/// conditions and indirectbr addresses this might make dead if
/// DeleteDeadConditions is true.
bool ConstantFoldTerminator(BasicBlock *BB, bool DeleteDeadConditions = false,
                            const TargetLibraryInfo *TLI = nullptr,
                            DomTreeUpdater *DTU = nullptr);

//===----------------------------------------------------------------------===//
//  Local dead code elimination.
//

/// Return true if the result produced by the instruction is not used, and the
/// instruction has no side effects.
bool isInstructionTriviallyDead(Instruction *I,
                                const TargetLibraryInfo *TLI = nullptr);

/// Return true if the result produced by the instruction would have no side
/// effects if it was not used. This is equivalent to checking whether
/// isInstructionTriviallyDead would be true if the use count was 0.
bool wouldInstructionBeTriviallyDead(Instruction *I,
                                     const TargetLibraryInfo *TLI = nullptr);

/// If the specified value is a trivially dead instruction, delete it.
/// If that makes any of its operands trivially dead, delete them too,
/// recursively. Return true if any instructions were deleted.
bool RecursivelyDeleteTriviallyDeadInstructions(
    Value *V, const TargetLibraryInfo *TLI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr);

/// Delete all of the instructions in `DeadInsts`, and all other instructions
/// that deleting these in turn causes to be trivially dead.
///
/// The initial instructions in the provided vector must all have empty use
/// lists and satisfy `isInstructionTriviallyDead`.
///
/// `DeadInsts` will be used as scratch storage for this routine and will be
/// empty afterward.
void RecursivelyDeleteTriviallyDeadInstructions(
    SmallVectorImpl<Instruction *> &DeadInsts,
    const TargetLibraryInfo *TLI = nullptr, MemorySSAUpdater *MSSAU = nullptr);

/// If the specified value is an effectively dead PHI node, due to being a
/// def-use chain of single-use nodes that either forms a cycle or is terminated
/// by a trivially dead instruction, delete it. If that makes any of its
/// operands trivially dead, delete them too, recursively. Return true if a
/// change was made.
bool RecursivelyDeleteDeadPHINode(PHINode *PN,
                                  const TargetLibraryInfo *TLI = nullptr);

/// Scan the specified basic block and try to simplify any instructions in it
/// and recursively delete dead instructions.
///
/// This returns true if it changed the code, note that it can delete
/// instructions in other blocks as well in this block.
bool SimplifyInstructionsInBlock(BasicBlock *BB,
                                 const TargetLibraryInfo *TLI = nullptr);

/// Replace all the uses of an SSA value in @llvm.dbg intrinsics with
/// undef. This is useful for signaling that a variable, e.g. has been
/// found dead and hence it's unavailable at a given program point.
/// Returns true if the dbg values have been changed.
bool replaceDbgUsesWithUndef(Instruction *I);

//===----------------------------------------------------------------------===//
//  Control Flow Graph Restructuring.
//

/// Like BasicBlock::removePredecessor, this method is called when we're about
/// to delete Pred as a predecessor of BB. If BB contains any PHI nodes, this
/// drops the entries in the PHI nodes for Pred.
///
/// Unlike the removePredecessor method, this attempts to simplify uses of PHI
/// nodes that collapse into identity values.  For example, if we have:
///   x = phi(1, 0, 0, 0)
///   y = and x, z
///
/// .. and delete the predecessor corresponding to the '1', this will attempt to
/// recursively fold the 'and' to 0.
void RemovePredecessorAndSimplify(BasicBlock *BB, BasicBlock *Pred,
                                  DomTreeUpdater *DTU = nullptr);

/// BB is a block with one predecessor and its predecessor is known to have one
/// successor (BB!). Eliminate the edge between them, moving the instructions in
/// the predecessor into BB. This deletes the predecessor block.
void MergeBasicBlockIntoOnlyPred(BasicBlock *BB, DomTreeUpdater *DTU = nullptr);

/// BB is known to contain an unconditional branch, and contains no instructions
/// other than PHI nodes, potential debug intrinsics and the branch. If
/// possible, eliminate BB by rewriting all the predecessors to branch to the
/// successor block and return true. If we can't transform, return false.
bool TryToSimplifyUncondBranchFromEmptyBlock(BasicBlock *BB,
                                             DomTreeUpdater *DTU = nullptr);

/// Check for and eliminate duplicate PHI nodes in this block. This doesn't try
/// to be clever about PHI nodes which differ only in the order of the incoming
/// values, but instcombine orders them so it usually won't matter.
bool EliminateDuplicatePHINodes(BasicBlock *BB);

/// This function is used to do simplification of a CFG.  For example, it
/// adjusts branches to branches to eliminate the extra hop, it eliminates
/// unreachable basic blocks, and does other peephole optimization of the CFG.
/// It returns true if a modification was made, possibly deleting the basic
/// block that was pointed to. LoopHeaders is an optional input parameter
/// providing the set of loop headers that SimplifyCFG should not eliminate.
bool simplifyCFG(BasicBlock *BB, const TargetTransformInfo &TTI,
                 const SimplifyCFGOptions &Options = {},
                 SmallPtrSetImpl<BasicBlock *> *LoopHeaders = nullptr);

/// This function is used to flatten a CFG. For example, it uses parallel-and
/// and parallel-or mode to collapse if-conditions and merge if-regions with
/// identical statements.
bool FlattenCFG(BasicBlock *BB, AliasAnalysis *AA = nullptr);

/// If this basic block is ONLY a setcc and a branch, and if a predecessor
/// branches to us and one of our successors, fold the setcc into the
/// predecessor and use logical operations to pick the right destination.
bool FoldBranchToCommonDest(BranchInst *BI, unsigned BonusInstThreshold = 1);

/// This function takes a virtual register computed by an Instruction and
/// replaces it with a slot in the stack frame, allocated via alloca.
/// This allows the CFG to be changed around without fear of invalidating the
/// SSA information for the value. It returns the pointer to the alloca inserted
/// to create a stack slot for X.
AllocaInst *DemoteRegToStack(Instruction &X,
                             bool VolatileLoads = false,
                             Instruction *AllocaPoint = nullptr);

/// This function takes a virtual register computed by a phi node and replaces
/// it with a slot in the stack frame, allocated via alloca. The phi node is
/// deleted and it returns the pointer to the alloca inserted.
AllocaInst *DemotePHIToStack(PHINode *P, Instruction *AllocaPoint = nullptr);

/// Try to ensure that the alignment of \p V is at least \p PrefAlign bytes. If
/// the owning object can be modified and has an alignment less than \p
/// PrefAlign, it will be increased and \p PrefAlign returned. If the alignment
/// cannot be increased, the known alignment of the value is returned.
///
/// It is not always possible to modify the alignment of the underlying object,
/// so if alignment is important, a more reliable approach is to simply align
/// all global variables and allocation instructions to their preferred
/// alignment from the beginning.
unsigned getOrEnforceKnownAlignment(Value *V, unsigned PrefAlign,
                                    const DataLayout &DL,
                                    const Instruction *CxtI = nullptr,
                                    AssumptionCache *AC = nullptr,
                                    const DominatorTree *DT = nullptr);

/// Try to infer an alignment for the specified pointer.
inline unsigned getKnownAlignment(Value *V, const DataLayout &DL,
                                  const Instruction *CxtI = nullptr,
                                  AssumptionCache *AC = nullptr,
                                  const DominatorTree *DT = nullptr) {
  return getOrEnforceKnownAlignment(V, 0, DL, CxtI, AC, DT);
}

///===---------------------------------------------------------------------===//
///  Dbg Intrinsic utilities
///

/// Inserts a llvm.dbg.value intrinsic before a store to an alloca'd value
/// that has an associated llvm.dbg.declare or llvm.dbg.addr intrinsic.
void ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                     StoreInst *SI, DIBuilder &Builder);

/// Inserts a llvm.dbg.value intrinsic before a load of an alloca'd value
/// that has an associated llvm.dbg.declare or llvm.dbg.addr intrinsic.
void ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                     LoadInst *LI, DIBuilder &Builder);

/// Inserts a llvm.dbg.value intrinsic after a phi that has an associated
/// llvm.dbg.declare or llvm.dbg.addr intrinsic.
void ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                     PHINode *LI, DIBuilder &Builder);

/// Lowers llvm.dbg.declare intrinsics into appropriate set of
/// llvm.dbg.value intrinsics.
bool LowerDbgDeclare(Function &F);

/// Propagate dbg.value intrinsics through the newly inserted PHIs.
void insertDebugValuesForPHIs(BasicBlock *BB,
                              SmallVectorImpl<PHINode *> &InsertedPHIs);

/// Finds all intrinsics declaring local variables as living in the memory that
/// 'V' points to. This may include a mix of dbg.declare and
/// dbg.addr intrinsics.
TinyPtrVector<DbgVariableIntrinsic *> FindDbgAddrUses(Value *V);

/// Finds the llvm.dbg.value intrinsics describing a value.
void findDbgValues(SmallVectorImpl<DbgValueInst *> &DbgValues, Value *V);

/// Finds the debug info intrinsics describing a value.
void findDbgUsers(SmallVectorImpl<DbgVariableIntrinsic *> &DbgInsts, Value *V);

/// Replaces llvm.dbg.declare instruction when the address it
/// describes is replaced with a new value. If Deref is true, an
/// additional DW_OP_deref is prepended to the expression. If Offset
/// is non-zero, a constant displacement is added to the expression
/// (between the optional Deref operations). Offset can be negative.
bool replaceDbgDeclare(Value *Address, Value *NewAddress,
                       Instruction *InsertBefore, DIBuilder &Builder,
                       bool DerefBefore, int Offset, bool DerefAfter);

/// Replaces llvm.dbg.declare instruction when the alloca it describes
/// is replaced with a new value. If Deref is true, an additional
/// DW_OP_deref is prepended to the expression. If Offset is non-zero,
/// a constant displacement is added to the expression (between the
/// optional Deref operations). Offset can be negative. The new
/// llvm.dbg.declare is inserted immediately after AI.
bool replaceDbgDeclareForAlloca(AllocaInst *AI, Value *NewAllocaAddress,
                                DIBuilder &Builder, bool DerefBefore,
                                int Offset, bool DerefAfter);

/// Replaces multiple llvm.dbg.value instructions when the alloca it describes
/// is replaced with a new value. If Offset is non-zero, a constant displacement
/// is added to the expression (after the mandatory Deref). Offset can be
/// negative. New llvm.dbg.value instructions are inserted at the locations of
/// the instructions they replace.
void replaceDbgValueForAlloca(AllocaInst *AI, Value *NewAllocaAddress,
                              DIBuilder &Builder, int Offset = 0);

/// Assuming the instruction \p I is going to be deleted, attempt to salvage
/// debug users of \p I by writing the effect of \p I in a DIExpression.
/// Returns true if any debug users were updated.
bool salvageDebugInfo(Instruction &I);

/// Point debug users of \p From to \p To or salvage them. Use this function
/// only when replacing all uses of \p From with \p To, with a guarantee that
/// \p From is going to be deleted.
///
/// Follow these rules to prevent use-before-def of \p To:
///   . If \p To is a linked Instruction, set \p DomPoint to \p To.
///   . If \p To is an unlinked Instruction, set \p DomPoint to the Instruction
///     \p To will be inserted after.
///   . If \p To is not an Instruction (e.g a Constant), the choice of
///     \p DomPoint is arbitrary. Pick \p From for simplicity.
///
/// If a debug user cannot be preserved without reordering variable updates or
/// introducing a use-before-def, it is either salvaged (\ref salvageDebugInfo)
/// or deleted. Returns true if any debug users were updated.
bool replaceAllDbgUsesWith(Instruction &From, Value &To, Instruction &DomPoint,
                           DominatorTree &DT);

/// Remove all instructions from a basic block other than it's terminator
/// and any present EH pad instructions.
unsigned removeAllNonTerminatorAndEHPadInstructions(BasicBlock *BB);

/// Insert an unreachable instruction before the specified
/// instruction, making it and the rest of the code in the block dead.
unsigned changeToUnreachable(Instruction *I, bool UseLLVMTrap,
                             bool PreserveLCSSA = false,
                             DomTreeUpdater *DTU = nullptr);

/// Convert the CallInst to InvokeInst with the specified unwind edge basic
/// block.  This also splits the basic block where CI is located, because
/// InvokeInst is a terminator instruction.  Returns the newly split basic
/// block.
BasicBlock *changeToInvokeAndSplitBasicBlock(CallInst *CI,
                                             BasicBlock *UnwindEdge);

/// Replace 'BB's terminator with one that does not have an unwind successor
/// block. Rewrites `invoke` to `call`, etc. Updates any PHIs in unwind
/// successor.
///
/// \param BB  Block whose terminator will be replaced.  Its terminator must
///            have an unwind successor.
void removeUnwindEdge(BasicBlock *BB, DomTreeUpdater *DTU = nullptr);

/// Remove all blocks that can not be reached from the function's entry.
///
/// Returns true if any basic block was removed.
bool removeUnreachableBlocks(Function &F, LazyValueInfo *LVI = nullptr,
                             DomTreeUpdater *DTU = nullptr,
                             MemorySSAUpdater *MSSAU = nullptr);

/// Combine the metadata of two instructions so that K can replace J. Some
/// metadata kinds can only be kept if K does not move, meaning it dominated
/// J in the original IR.
///
/// Metadata not listed as known via KnownIDs is removed
void combineMetadata(Instruction *K, const Instruction *J,
                     ArrayRef<unsigned> KnownIDs, bool DoesKMove);

/// Combine the metadata of two instructions so that K can replace J. This
/// specifically handles the case of CSE-like transformations. Some
/// metadata can only be kept if K dominates J. For this to be correct,
/// K cannot be hoisted.
///
/// Unknown metadata is removed.
void combineMetadataForCSE(Instruction *K, const Instruction *J,
                           bool DoesKMove);

/// Patch the replacement so that it is not more restrictive than the value
/// being replaced. It assumes that the replacement does not get moved from
/// its original position.
void patchReplacementInstruction(Instruction *I, Value *Repl);

// Replace each use of 'From' with 'To', if that use does not belong to basic
// block where 'From' is defined. Returns the number of replacements made.
unsigned replaceNonLocalUsesWith(Instruction *From, Value *To);

/// Replace each use of 'From' with 'To' if that use is dominated by
/// the given edge.  Returns the number of replacements made.
unsigned replaceDominatedUsesWith(Value *From, Value *To, DominatorTree &DT,
                                  const BasicBlockEdge &Edge);
/// Replace each use of 'From' with 'To' if that use is dominated by
/// the end of the given BasicBlock. Returns the number of replacements made.
unsigned replaceDominatedUsesWith(Value *From, Value *To, DominatorTree &DT,
                                  const BasicBlock *BB);

/// Return true if the CallSite CS calls a gc leaf function.
///
/// A leaf function is a function that does not safepoint the thread during its
/// execution.  During a call or invoke to such a function, the callers stack
/// does not have to be made parseable.
///
/// Most passes can and should ignore this information, and it is only used
/// during lowering by the GC infrastructure.
bool callsGCLeafFunction(ImmutableCallSite CS, const TargetLibraryInfo &TLI);

/// Copy a nonnull metadata node to a new load instruction.
///
/// This handles mapping it to range metadata if the new load is an integer
/// load instead of a pointer load.
void copyNonnullMetadata(const LoadInst &OldLI, MDNode *N, LoadInst &NewLI);

/// Copy a range metadata node to a new load instruction.
///
/// This handles mapping it to nonnull metadata if the new load is a pointer
/// load instead of an integer load and the range doesn't cover null.
void copyRangeMetadata(const DataLayout &DL, const LoadInst &OldLI, MDNode *N,
                       LoadInst &NewLI);

/// Remove the debug intrinsic instructions for the given instruction.
void dropDebugUsers(Instruction &I);

/// Hoist all of the instructions in the \p IfBlock to the dominant block
/// \p DomBlock, by moving its instructions to the insertion point \p InsertPt.
///
/// The moved instructions receive the insertion point debug location values
/// (DILocations) and their debug intrinsic instructions (dbg.values) are
/// removed.
void hoistAllInstructionsInto(BasicBlock *DomBlock, Instruction *InsertPt,
                              BasicBlock *BB);

//===----------------------------------------------------------------------===//
//  Intrinsic pattern matching
//

/// Try to match a bswap or bitreverse idiom.
///
/// If an idiom is matched, an intrinsic call is inserted before \c I. Any added
/// instructions are returned in \c InsertedInsts. They will all have been added
/// to a basic block.
///
/// A bitreverse idiom normally requires around 2*BW nodes to be searched (where
/// BW is the bitwidth of the integer type). A bswap idiom requires anywhere up
/// to BW / 4 nodes to be searched, so is significantly faster.
///
/// This function returns true on a successful match or false otherwise.
bool recognizeBSwapOrBitReverseIdiom(
    Instruction *I, bool MatchBSwaps, bool MatchBitReversals,
    SmallVectorImpl<Instruction *> &InsertedInsts);

//===----------------------------------------------------------------------===//
//  Sanitizer utilities
//

/// Given a CallInst, check if it calls a string function known to CodeGen,
/// and mark it with NoBuiltin if so.  To be used by sanitizers that intend
/// to intercept string functions and want to avoid converting them to target
/// specific instructions.
void maybeMarkSanitizerLibraryCallNoBuiltin(CallInst *CI,
                                            const TargetLibraryInfo *TLI);

//===----------------------------------------------------------------------===//
//  Transform predicates
//

/// Given an instruction, is it legal to set operand OpIdx to a non-constant
/// value?
bool canReplaceOperandWithVariable(const Instruction *I, unsigned OpIdx);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOCAL_H
