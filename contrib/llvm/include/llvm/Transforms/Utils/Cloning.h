//===- Cloning.h - Clone various parts of LLVM programs ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines various functions that are used to clone chunks of LLVM
// code for various purposes.  This varies from copying whole modules into new
// modules, to cloning functions with different arguments, to inlining
// functions, to copying basic blocks to support loop unrolling or superblock
// formation, etc.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CLONING_H
#define LLVM_TRANSFORMS_UTILS_CLONING_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <functional>
#include <memory>
#include <vector>

namespace llvm {

class AllocaInst;
class BasicBlock;
class BlockFrequencyInfo;
class CallInst;
class CallGraph;
class DebugInfoFinder;
class DominatorTree;
class Function;
class Instruction;
class InvokeInst;
class Loop;
class LoopInfo;
class Module;
class ProfileSummaryInfo;
class ReturnInst;
class DomTreeUpdater;

/// Return an exact copy of the specified module
std::unique_ptr<Module> CloneModule(const Module &M);
std::unique_ptr<Module> CloneModule(const Module &M, ValueToValueMapTy &VMap);

/// Return a copy of the specified module. The ShouldCloneDefinition function
/// controls whether a specific GlobalValue's definition is cloned. If the
/// function returns false, the module copy will contain an external reference
/// in place of the global definition.
std::unique_ptr<Module>
CloneModule(const Module &M, ValueToValueMapTy &VMap,
            function_ref<bool(const GlobalValue *)> ShouldCloneDefinition);

/// This struct can be used to capture information about code
/// being cloned, while it is being cloned.
struct ClonedCodeInfo {
  /// This is set to true if the cloned code contains a normal call instruction.
  bool ContainsCalls = false;

  /// This is set to true if the cloned code contains a 'dynamic' alloca.
  /// Dynamic allocas are allocas that are either not in the entry block or they
  /// are in the entry block but are not a constant size.
  bool ContainsDynamicAllocas = false;

  /// All cloned call sites that have operand bundles attached are appended to
  /// this vector.  This vector may contain nulls or undefs if some of the
  /// originally inserted callsites were DCE'ed after they were cloned.
  std::vector<WeakTrackingVH> OperandBundleCallSites;

  ClonedCodeInfo() = default;
};

/// Return a copy of the specified basic block, but without
/// embedding the block into a particular function.  The block returned is an
/// exact copy of the specified basic block, without any remapping having been
/// performed.  Because of this, this is only suitable for applications where
/// the basic block will be inserted into the same function that it was cloned
/// from (loop unrolling would use this, for example).
///
/// Also, note that this function makes a direct copy of the basic block, and
/// can thus produce illegal LLVM code.  In particular, it will copy any PHI
/// nodes from the original block, even though there are no predecessors for the
/// newly cloned block (thus, phi nodes will have to be updated).  Also, this
/// block will branch to the old successors of the original block: these
/// successors will have to have any PHI nodes updated to account for the new
/// incoming edges.
///
/// The correlation between instructions in the source and result basic blocks
/// is recorded in the VMap map.
///
/// If you have a particular suffix you'd like to use to add to any cloned
/// names, specify it as the optional third parameter.
///
/// If you would like the basic block to be auto-inserted into the end of a
/// function, you can specify it as the optional fourth parameter.
///
/// If you would like to collect additional information about the cloned
/// function, you can specify a ClonedCodeInfo object with the optional fifth
/// parameter.
BasicBlock *CloneBasicBlock(const BasicBlock *BB, ValueToValueMapTy &VMap,
                            const Twine &NameSuffix = "", Function *F = nullptr,
                            ClonedCodeInfo *CodeInfo = nullptr,
                            DebugInfoFinder *DIFinder = nullptr);

/// Return a copy of the specified function and add it to that
/// function's module.  Also, any references specified in the VMap are changed
/// to refer to their mapped value instead of the original one.  If any of the
/// arguments to the function are in the VMap, the arguments are deleted from
/// the resultant function.  The VMap is updated to include mappings from all of
/// the instructions and basicblocks in the function from their old to new
/// values.  The final argument captures information about the cloned code if
/// non-null.
///
/// VMap contains no non-identity GlobalValue mappings and debug info metadata
/// will not be cloned.
///
Function *CloneFunction(Function *F, ValueToValueMapTy &VMap,
                        ClonedCodeInfo *CodeInfo = nullptr);

/// Clone OldFunc into NewFunc, transforming the old arguments into references
/// to VMap values.  Note that if NewFunc already has basic blocks, the ones
/// cloned into it will be added to the end of the function.  This function
/// fills in a list of return instructions, and can optionally remap types
/// and/or append the specified suffix to all values cloned.
///
/// If ModuleLevelChanges is false, VMap contains no non-identity GlobalValue
/// mappings.
///
void CloneFunctionInto(Function *NewFunc, const Function *OldFunc,
                       ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                       SmallVectorImpl<ReturnInst*> &Returns,
                       const char *NameSuffix = "",
                       ClonedCodeInfo *CodeInfo = nullptr,
                       ValueMapTypeRemapper *TypeMapper = nullptr,
                       ValueMaterializer *Materializer = nullptr);

void CloneAndPruneIntoFromInst(Function *NewFunc, const Function *OldFunc,
                               const Instruction *StartingInst,
                               ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                               SmallVectorImpl<ReturnInst *> &Returns,
                               const char *NameSuffix = "",
                               ClonedCodeInfo *CodeInfo = nullptr);

/// This works exactly like CloneFunctionInto,
/// except that it does some simple constant prop and DCE on the fly.  The
/// effect of this is to copy significantly less code in cases where (for
/// example) a function call with constant arguments is inlined, and those
/// constant arguments cause a significant amount of code in the callee to be
/// dead.  Since this doesn't produce an exactly copy of the input, it can't be
/// used for things like CloneFunction or CloneModule.
///
/// If ModuleLevelChanges is false, VMap contains no non-identity GlobalValue
/// mappings.
///
void CloneAndPruneFunctionInto(Function *NewFunc, const Function *OldFunc,
                               ValueToValueMapTy &VMap, bool ModuleLevelChanges,
                               SmallVectorImpl<ReturnInst*> &Returns,
                               const char *NameSuffix = "",
                               ClonedCodeInfo *CodeInfo = nullptr,
                               Instruction *TheCall = nullptr);

/// This class captures the data input to the InlineFunction call, and records
/// the auxiliary results produced by it.
class InlineFunctionInfo {
public:
  explicit InlineFunctionInfo(CallGraph *cg = nullptr,
                              std::function<AssumptionCache &(Function &)>
                                  *GetAssumptionCache = nullptr,
                              ProfileSummaryInfo *PSI = nullptr,
                              BlockFrequencyInfo *CallerBFI = nullptr,
                              BlockFrequencyInfo *CalleeBFI = nullptr)
      : CG(cg), GetAssumptionCache(GetAssumptionCache), PSI(PSI),
        CallerBFI(CallerBFI), CalleeBFI(CalleeBFI) {}

  /// If non-null, InlineFunction will update the callgraph to reflect the
  /// changes it makes.
  CallGraph *CG;
  std::function<AssumptionCache &(Function &)> *GetAssumptionCache;
  ProfileSummaryInfo *PSI;
  BlockFrequencyInfo *CallerBFI, *CalleeBFI;

  /// InlineFunction fills this in with all static allocas that get copied into
  /// the caller.
  SmallVector<AllocaInst *, 4> StaticAllocas;

  /// InlineFunction fills this in with callsites that were inlined from the
  /// callee. This is only filled in if CG is non-null.
  SmallVector<WeakTrackingVH, 8> InlinedCalls;

  /// All of the new call sites inlined into the caller.
  ///
  /// 'InlineFunction' fills this in by scanning the inlined instructions, and
  /// only if CG is null. If CG is non-null, instead the value handle
  /// `InlinedCalls` above is used.
  SmallVector<CallSite, 8> InlinedCallSites;

  void reset() {
    StaticAllocas.clear();
    InlinedCalls.clear();
    InlinedCallSites.clear();
  }
};

/// This function inlines the called function into the basic
/// block of the caller.  This returns false if it is not possible to inline
/// this call.  The program is still in a well defined state if this occurs
/// though.
///
/// Note that this only does one level of inlining.  For example, if the
/// instruction 'call B' is inlined, and 'B' calls 'C', then the call to 'C' now
/// exists in the instruction stream.  Similarly this will inline a recursive
/// function by one level.
///
/// Note that while this routine is allowed to cleanup and optimize the
/// *inlined* code to minimize the actual inserted code, it must not delete
/// code in the caller as users of this routine may have pointers to
/// instructions in the caller that need to remain stable.
///
/// If ForwardVarArgsTo is passed, inlining a function with varargs is allowed
/// and all varargs at the callsite will be passed to any calls to
/// ForwardVarArgsTo. The caller of InlineFunction has to make sure any varargs
/// are only used by ForwardVarArgsTo.
InlineResult InlineFunction(CallInst *C, InlineFunctionInfo &IFI,
                            AAResults *CalleeAAR = nullptr,
                            bool InsertLifetime = true);
InlineResult InlineFunction(InvokeInst *II, InlineFunctionInfo &IFI,
                            AAResults *CalleeAAR = nullptr,
                            bool InsertLifetime = true);
InlineResult InlineFunction(CallSite CS, InlineFunctionInfo &IFI,
                            AAResults *CalleeAAR = nullptr,
                            bool InsertLifetime = true,
                            Function *ForwardVarArgsTo = nullptr);

/// Clones a loop \p OrigLoop.  Returns the loop and the blocks in \p
/// Blocks.
///
/// Updates LoopInfo and DominatorTree assuming the loop is dominated by block
/// \p LoopDomBB.  Insert the new blocks before block specified in \p Before.
/// Note: Only innermost loops are supported.
Loop *cloneLoopWithPreheader(BasicBlock *Before, BasicBlock *LoopDomBB,
                             Loop *OrigLoop, ValueToValueMapTy &VMap,
                             const Twine &NameSuffix, LoopInfo *LI,
                             DominatorTree *DT,
                             SmallVectorImpl<BasicBlock *> &Blocks);

/// Remaps instructions in \p Blocks using the mapping in \p VMap.
void remapInstructionsInBlocks(const SmallVectorImpl<BasicBlock *> &Blocks,
                               ValueToValueMapTy &VMap);

/// Split edge between BB and PredBB and duplicate all non-Phi instructions
/// from BB between its beginning and the StopAt instruction into the split
/// block. Phi nodes are not duplicated, but their uses are handled correctly:
/// we replace them with the uses of corresponding Phi inputs. ValueMapping
/// is used to map the original instructions from BB to their newly-created
/// copies. Returns the split block.
BasicBlock *DuplicateInstructionsInSplitBetween(BasicBlock *BB,
                                                BasicBlock *PredBB,
                                                Instruction *StopAt,
                                                ValueToValueMapTy &ValueMapping,
                                                DomTreeUpdater &DTU);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CLONING_H
