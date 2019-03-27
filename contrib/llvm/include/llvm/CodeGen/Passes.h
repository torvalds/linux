//===-- Passes.h - Target independent code generation passes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces to access the target independent code generation
// passes provided by the LLVM backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PASSES_H
#define LLVM_CODEGEN_PASSES_H

#include <functional>
#include <string>

namespace llvm {

class FunctionPass;
class MachineFunction;
class MachineFunctionPass;
class ModulePass;
class Pass;
class TargetMachine;
class TargetRegisterClass;
class raw_ostream;

} // End llvm namespace

/// List of target independent CodeGen pass IDs.
namespace llvm {
  FunctionPass *createAtomicExpandPass();

  /// createUnreachableBlockEliminationPass - The LLVM code generator does not
  /// work well with unreachable basic blocks (what live ranges make sense for a
  /// block that cannot be reached?).  As such, a code generator should either
  /// not instruction select unreachable blocks, or run this pass as its
  /// last LLVM modifying pass to clean up blocks that are not reachable from
  /// the entry block.
  FunctionPass *createUnreachableBlockEliminationPass();

  /// MachineFunctionPrinter pass - This pass prints out the machine function to
  /// the given stream as a debugging tool.
  MachineFunctionPass *
  createMachineFunctionPrinterPass(raw_ostream &OS,
                                   const std::string &Banner ="");

  /// MIRPrinting pass - this pass prints out the LLVM IR into the given stream
  /// using the MIR serialization format.
  MachineFunctionPass *createPrintMIRPass(raw_ostream &OS);

  /// This pass resets a MachineFunction when it has the FailedISel property
  /// as if it was just created.
  /// If EmitFallbackDiag is true, the pass will emit a
  /// DiagnosticInfoISelFallback for every MachineFunction it resets.
  /// If AbortOnFailedISel is true, abort compilation instead of resetting.
  MachineFunctionPass *createResetMachineFunctionPass(bool EmitFallbackDiag,
                                                      bool AbortOnFailedISel);

  /// createCodeGenPreparePass - Transform the code to expose more pattern
  /// matching during instruction selection.
  FunctionPass *createCodeGenPreparePass();

  /// createScalarizeMaskedMemIntrinPass - Replace masked load, store, gather
  /// and scatter intrinsics with scalar code when target doesn't support them.
  FunctionPass *createScalarizeMaskedMemIntrinPass();

  /// AtomicExpandID -- Lowers atomic operations in terms of either cmpxchg
  /// load-linked/store-conditional loops.
  extern char &AtomicExpandID;

  /// MachineLoopInfo - This pass is a loop analysis pass.
  extern char &MachineLoopInfoID;

  /// MachineDominators - This pass is a machine dominators analysis pass.
  extern char &MachineDominatorsID;

/// MachineDominanaceFrontier - This pass is a machine dominators analysis pass.
  extern char &MachineDominanceFrontierID;

  /// MachineRegionInfo - This pass computes SESE regions for machine functions.
  extern char &MachineRegionInfoPassID;

  /// EdgeBundles analysis - Bundle machine CFG edges.
  extern char &EdgeBundlesID;

  /// LiveVariables pass - This pass computes the set of blocks in which each
  /// variable is life and sets machine operand kill flags.
  extern char &LiveVariablesID;

  /// PHIElimination - This pass eliminates machine instruction PHI nodes
  /// by inserting copy instructions.  This destroys SSA information, but is the
  /// desired input for some register allocators.  This pass is "required" by
  /// these register allocator like this: AU.addRequiredID(PHIEliminationID);
  extern char &PHIEliminationID;

  /// LiveIntervals - This analysis keeps track of the live ranges of virtual
  /// and physical registers.
  extern char &LiveIntervalsID;

  /// LiveStacks pass. An analysis keeping track of the liveness of stack slots.
  extern char &LiveStacksID;

  /// TwoAddressInstruction - This pass reduces two-address instructions to
  /// use two operands. This destroys SSA information but it is desired by
  /// register allocators.
  extern char &TwoAddressInstructionPassID;

  /// ProcessImpicitDefs pass - This pass removes IMPLICIT_DEFs.
  extern char &ProcessImplicitDefsID;

  /// RegisterCoalescer - This pass merges live ranges to eliminate copies.
  extern char &RegisterCoalescerID;

  /// MachineScheduler - This pass schedules machine instructions.
  extern char &MachineSchedulerID;

  /// PostMachineScheduler - This pass schedules machine instructions postRA.
  extern char &PostMachineSchedulerID;

  /// SpillPlacement analysis. Suggest optimal placement of spill code between
  /// basic blocks.
  extern char &SpillPlacementID;

  /// ShrinkWrap pass. Look for the best place to insert save and restore
  // instruction and update the MachineFunctionInfo with that information.
  extern char &ShrinkWrapID;

  /// LiveRangeShrink pass. Move instruction close to its definition to shrink
  /// the definition's live range.
  extern char &LiveRangeShrinkID;

  /// Greedy register allocator.
  extern char &RAGreedyID;

  /// Basic register allocator.
  extern char &RABasicID;

  /// VirtRegRewriter pass. Rewrite virtual registers to physical registers as
  /// assigned in VirtRegMap.
  extern char &VirtRegRewriterID;

  /// UnreachableMachineBlockElimination - This pass removes unreachable
  /// machine basic blocks.
  extern char &UnreachableMachineBlockElimID;

  /// DeadMachineInstructionElim - This pass removes dead machine instructions.
  extern char &DeadMachineInstructionElimID;

  /// This pass adds dead/undef flags after analyzing subregister lanes.
  extern char &DetectDeadLanesID;

  /// This pass perform post-ra machine sink for COPY instructions.
  extern char &PostRAMachineSinkingID;

  /// FastRegisterAllocation Pass - This pass register allocates as fast as
  /// possible. It is best suited for debug code where live ranges are short.
  ///
  FunctionPass *createFastRegisterAllocator();

  /// BasicRegisterAllocation Pass - This pass implements a degenerate global
  /// register allocator using the basic regalloc framework.
  ///
  FunctionPass *createBasicRegisterAllocator();

  /// Greedy register allocation pass - This pass implements a global register
  /// allocator for optimized builds.
  ///
  FunctionPass *createGreedyRegisterAllocator();

  /// PBQPRegisterAllocation Pass - This pass implements the Partitioned Boolean
  /// Quadratic Prograaming (PBQP) based register allocator.
  ///
  FunctionPass *createDefaultPBQPRegisterAllocator();

  /// PrologEpilogCodeInserter - This pass inserts prolog and epilog code,
  /// and eliminates abstract frame references.
  extern char &PrologEpilogCodeInserterID;
  MachineFunctionPass *createPrologEpilogInserterPass();

  /// ExpandPostRAPseudos - This pass expands pseudo instructions after
  /// register allocation.
  extern char &ExpandPostRAPseudosID;

  /// createPostRAHazardRecognizer - This pass runs the post-ra hazard
  /// recognizer.
  extern char &PostRAHazardRecognizerID;

  /// createPostRAScheduler - This pass performs post register allocation
  /// scheduling.
  extern char &PostRASchedulerID;

  /// BranchFolding - This pass performs machine code CFG based
  /// optimizations to delete branches to branches, eliminate branches to
  /// successor blocks (creating fall throughs), and eliminating branches over
  /// branches.
  extern char &BranchFolderPassID;

  /// BranchRelaxation - This pass replaces branches that need to jump further
  /// than is supported by a branch instruction.
  extern char &BranchRelaxationPassID;

  /// MachineFunctionPrinterPass - This pass prints out MachineInstr's.
  extern char &MachineFunctionPrinterPassID;

  /// MIRPrintingPass - this pass prints out the LLVM IR using the MIR
  /// serialization format.
  extern char &MIRPrintingPassID;

  /// TailDuplicate - Duplicate blocks with unconditional branches
  /// into tails of their predecessors.
  extern char &TailDuplicateID;

  /// Duplicate blocks with unconditional branches into tails of their
  /// predecessors. Variant that works before register allocation.
  extern char &EarlyTailDuplicateID;

  /// MachineTraceMetrics - This pass computes critical path and CPU resource
  /// usage in an ensemble of traces.
  extern char &MachineTraceMetricsID;

  /// EarlyIfConverter - This pass performs if-conversion on SSA form by
  /// inserting cmov instructions.
  extern char &EarlyIfConverterID;

  /// This pass performs instruction combining using trace metrics to estimate
  /// critical-path and resource depth.
  extern char &MachineCombinerID;

  /// StackSlotColoring - This pass performs stack coloring and merging.
  /// It merges disjoint allocas to reduce the stack size.
  extern char &StackColoringID;

  /// IfConverter - This pass performs machine code if conversion.
  extern char &IfConverterID;

  FunctionPass *createIfConverter(
      std::function<bool(const MachineFunction &)> Ftor);

  /// MachineBlockPlacement - This pass places basic blocks based on branch
  /// probabilities.
  extern char &MachineBlockPlacementID;

  /// MachineBlockPlacementStats - This pass collects statistics about the
  /// basic block placement using branch probabilities and block frequency
  /// information.
  extern char &MachineBlockPlacementStatsID;

  /// GCLowering Pass - Used by gc.root to perform its default lowering
  /// operations.
  FunctionPass *createGCLoweringPass();

  /// ShadowStackGCLowering - Implements the custom lowering mechanism
  /// used by the shadow stack GC.  Only runs on functions which opt in to
  /// the shadow stack collector.
  FunctionPass *createShadowStackGCLoweringPass();

  /// GCMachineCodeAnalysis - Target-independent pass to mark safe points
  /// in machine code. Must be added very late during code generation, just
  /// prior to output, and importantly after all CFG transformations (such as
  /// branch folding).
  extern char &GCMachineCodeAnalysisID;

  /// Creates a pass to print GC metadata.
  ///
  FunctionPass *createGCInfoPrinter(raw_ostream &OS);

  /// MachineCSE - This pass performs global CSE on machine instructions.
  extern char &MachineCSEID;

  /// ImplicitNullChecks - This pass folds null pointer checks into nearby
  /// memory operations.
  extern char &ImplicitNullChecksID;

  /// This pass performs loop invariant code motion on machine instructions.
  extern char &MachineLICMID;

  /// This pass performs loop invariant code motion on machine instructions.
  /// This variant works before register allocation. \see MachineLICMID.
  extern char &EarlyMachineLICMID;

  /// MachineSinking - This pass performs sinking on machine instructions.
  extern char &MachineSinkingID;

  /// MachineCopyPropagation - This pass performs copy propagation on
  /// machine instructions.
  extern char &MachineCopyPropagationID;

  /// PeepholeOptimizer - This pass performs peephole optimizations -
  /// like extension and comparison eliminations.
  extern char &PeepholeOptimizerID;

  /// OptimizePHIs - This pass optimizes machine instruction PHIs
  /// to take advantage of opportunities created during DAG legalization.
  extern char &OptimizePHIsID;

  /// StackSlotColoring - This pass performs stack slot coloring.
  extern char &StackSlotColoringID;

  /// This pass lays out funclets contiguously.
  extern char &FuncletLayoutID;

  /// This pass inserts the XRay instrumentation sleds if they are supported by
  /// the target platform.
  extern char &XRayInstrumentationID;

  /// This pass inserts FEntry calls
  extern char &FEntryInserterID;

  /// This pass implements the "patchable-function" attribute.
  extern char &PatchableFunctionID;

  /// createStackProtectorPass - This pass adds stack protectors to functions.
  ///
  FunctionPass *createStackProtectorPass();

  /// createMachineVerifierPass - This pass verifies cenerated machine code
  /// instructions for correctness.
  ///
  FunctionPass *createMachineVerifierPass(const std::string& Banner);

  /// createDwarfEHPass - This pass mulches exception handling code into a form
  /// adapted to code generation.  Required if using dwarf exception handling.
  FunctionPass *createDwarfEHPass();

  /// createWinEHPass - Prepares personality functions used by MSVC on Windows,
  /// in addition to the Itanium LSDA based personalities.
  FunctionPass *createWinEHPass(bool DemoteCatchSwitchPHIOnly = false);

  /// createSjLjEHPreparePass - This pass adapts exception handling code to use
  /// the GCC-style builtin setjmp/longjmp (sjlj) to handling EH control flow.
  ///
  FunctionPass *createSjLjEHPreparePass();

  /// createWasmEHPass - This pass adapts exception handling code to use
  /// WebAssembly's exception handling scheme.
  FunctionPass *createWasmEHPass();

  /// LocalStackSlotAllocation - This pass assigns local frame indices to stack
  /// slots relative to one another and allocates base registers to access them
  /// when it is estimated by the target to be out of range of normal frame
  /// pointer or stack pointer index addressing.
  extern char &LocalStackSlotAllocationID;

  /// ExpandISelPseudos - This pass expands pseudo-instructions.
  extern char &ExpandISelPseudosID;

  /// UnpackMachineBundles - This pass unpack machine instruction bundles.
  extern char &UnpackMachineBundlesID;

  FunctionPass *
  createUnpackMachineBundles(std::function<bool(const MachineFunction &)> Ftor);

  /// FinalizeMachineBundles - This pass finalize machine instruction
  /// bundles (created earlier, e.g. during pre-RA scheduling).
  extern char &FinalizeMachineBundlesID;

  /// StackMapLiveness - This pass analyses the register live-out set of
  /// stackmap/patchpoint intrinsics and attaches the calculated information to
  /// the intrinsic for later emission to the StackMap.
  extern char &StackMapLivenessID;

  /// LiveDebugValues pass
  extern char &LiveDebugValuesID;

  /// createJumpInstrTables - This pass creates jump-instruction tables.
  ModulePass *createJumpInstrTablesPass();

  /// createForwardControlFlowIntegrityPass - This pass adds control-flow
  /// integrity.
  ModulePass *createForwardControlFlowIntegrityPass();

  /// InterleavedAccess Pass - This pass identifies and matches interleaved
  /// memory accesses to target specific intrinsics.
  ///
  FunctionPass *createInterleavedAccessPass();

  /// InterleavedLoadCombines Pass - This pass identifies interleaved loads and
  /// combines them into wide loads detectable by InterleavedAccessPass
  ///
  FunctionPass *createInterleavedLoadCombinePass();

  /// LowerEmuTLS - This pass generates __emutls_[vt].xyz variables for all
  /// TLS variables for the emulated TLS model.
  ///
  ModulePass *createLowerEmuTLSPass();

  /// This pass lowers the \@llvm.load.relative and \@llvm.objc.* intrinsics to
  /// instructions.  This is unsafe to do earlier because a pass may combine the
  /// constant initializer into the load, which may result in an overflowing
  /// evaluation.
  ModulePass *createPreISelIntrinsicLoweringPass();

  /// GlobalMerge - This pass merges internal (by default) globals into structs
  /// to enable reuse of a base pointer by indexed addressing modes.
  /// It can also be configured to focus on size optimizations only.
  ///
  Pass *createGlobalMergePass(const TargetMachine *TM, unsigned MaximalOffset,
                              bool OnlyOptimizeForSize = false,
                              bool MergeExternalByDefault = false);

  /// This pass splits the stack into a safe stack and an unsafe stack to
  /// protect against stack-based overflow vulnerabilities.
  FunctionPass *createSafeStackPass();

  /// This pass detects subregister lanes in a virtual register that are used
  /// independently of other lanes and splits them into separate virtual
  /// registers.
  extern char &RenameIndependentSubregsID;

  /// This pass is executed POST-RA to collect which physical registers are
  /// preserved by given machine function.
  FunctionPass *createRegUsageInfoCollector();

  /// Return a MachineFunction pass that identifies call sites
  /// and propagates register usage information of callee to caller
  /// if available with PysicalRegisterUsageInfo pass.
  FunctionPass *createRegUsageInfoPropPass();

  /// This pass performs software pipelining on machine instructions.
  extern char &MachinePipelinerID;

  /// This pass frees the memory occupied by the MachineFunction.
  FunctionPass *createFreeMachineFunctionPass();

  /// This pass performs outlining on machine instructions directly before
  /// printing assembly.
  ModulePass *createMachineOutlinerPass(bool RunOnAllFunctions = true);

  /// This pass expands the experimental reduction intrinsics into sequences of
  /// shuffles.
  FunctionPass *createExpandReductionsPass();

  // This pass expands memcmp() to load/stores.
  FunctionPass *createExpandMemCmpPass();

  /// Creates Break False Dependencies pass. \see BreakFalseDeps.cpp
  FunctionPass *createBreakFalseDeps();

  // This pass expands indirectbr instructions.
  FunctionPass *createIndirectBrExpandPass();

  /// Creates CFI Instruction Inserter pass. \see CFIInstrInserter.cpp
  FunctionPass *createCFIInstrInserter();

} // End llvm namespace

#endif
