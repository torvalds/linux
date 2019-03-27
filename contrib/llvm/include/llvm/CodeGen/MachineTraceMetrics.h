//===- lib/CodeGen/MachineTraceMetrics.h - Super-scalar metrics -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for the MachineTraceMetrics analysis pass
// that estimates CPU resource usage and critical data dependency paths through
// preferred traces. This is useful for super-scalar CPUs where execution speed
// can be limited both by data dependencies and by limited execution resources.
//
// Out-of-order CPUs will often be executing instructions from multiple basic
// blocks at the same time. This makes it difficult to estimate the resource
// usage accurately in a single basic block. Resources can be estimated better
// by looking at a trace through the current basic block.
//
// For every block, the MachineTraceMetrics pass will pick a preferred trace
// that passes through the block. The trace is chosen based on loop structure,
// branch probabilities, and resource usage. The intention is to pick likely
// traces that would be the most affected by code transformations.
//
// It is expensive to compute a full arbitrary trace for every block, so to
// save some computations, traces are chosen to be convergent. This means that
// if the traces through basic blocks A and B ever cross when moving away from
// A and B, they never diverge again. This applies in both directions - If the
// traces meet above A and B, they won't diverge when going further back.
//
// Traces tend to align with loops. The trace through a block in an inner loop
// will begin at the loop entry block and end at a back edge. If there are
// nested loops, the trace may begin and end at those instead.
//
// For each trace, we compute the critical path length, which is the number of
// cycles required to execute the trace when execution is limited by data
// dependencies only. We also compute the resource height, which is the number
// of cycles required to execute all instructions in the trace when ignoring
// data dependencies.
//
// Every instruction in the current block has a slack - the number of cycles
// execution of the instruction can be delayed without extending the critical
// path.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINETRACEMETRICS_H
#define LLVM_CODEGEN_MACHINETRACEMETRICS_H

#include "llvm/ADT/SparseSet.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetSchedule.h"

namespace llvm {

class AnalysisUsage;
class MachineFunction;
class MachineInstr;
class MachineLoop;
class MachineLoopInfo;
class MachineRegisterInfo;
struct MCSchedClassDesc;
class raw_ostream;
class TargetInstrInfo;
class TargetRegisterInfo;

// Keep track of physreg data dependencies by recording each live register unit.
// Associate each regunit with an instruction operand. Depending on the
// direction instructions are scanned, it could be the operand that defined the
// regunit, or the highest operand to read the regunit.
struct LiveRegUnit {
  unsigned RegUnit;
  unsigned Cycle = 0;
  const MachineInstr *MI = nullptr;
  unsigned Op = 0;

  unsigned getSparseSetIndex() const { return RegUnit; }

  LiveRegUnit(unsigned RU) : RegUnit(RU) {}
};


class MachineTraceMetrics : public MachineFunctionPass {
  const MachineFunction *MF = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const MachineRegisterInfo *MRI = nullptr;
  const MachineLoopInfo *Loops = nullptr;
  TargetSchedModel SchedModel;

public:
  friend class Ensemble;
  friend class Trace;

  class Ensemble;

  static char ID;

  MachineTraceMetrics();

  void getAnalysisUsage(AnalysisUsage&) const override;
  bool runOnMachineFunction(MachineFunction&) override;
  void releaseMemory() override;
  void verifyAnalysis() const override;

  /// Per-basic block information that doesn't depend on the trace through the
  /// block.
  struct FixedBlockInfo {
    /// The number of non-trivial instructions in the block.
    /// Doesn't count PHI and COPY instructions that are likely to be removed.
    unsigned InstrCount = ~0u;

    /// True when the block contains calls.
    bool HasCalls = false;

    FixedBlockInfo() = default;

    /// Returns true when resource information for this block has been computed.
    bool hasResources() const { return InstrCount != ~0u; }

    /// Invalidate resource information.
    void invalidate() { InstrCount = ~0u; }
  };

  /// Get the fixed resource information about MBB. Compute it on demand.
  const FixedBlockInfo *getResources(const MachineBasicBlock*);

  /// Get the scaled number of cycles used per processor resource in MBB.
  /// This is an array with SchedModel.getNumProcResourceKinds() entries.
  /// The getResources() function above must have been called first.
  ///
  /// These numbers have already been scaled by SchedModel.getResourceFactor().
  ArrayRef<unsigned> getProcResourceCycles(unsigned MBBNum) const;

  /// A virtual register or regunit required by a basic block or its trace
  /// successors.
  struct LiveInReg {
    /// The virtual register required, or a register unit.
    unsigned Reg;

    /// For virtual registers: Minimum height of the defining instruction.
    /// For regunits: Height of the highest user in the trace.
    unsigned Height;

    LiveInReg(unsigned Reg, unsigned Height = 0) : Reg(Reg), Height(Height) {}
  };

  /// Per-basic block information that relates to a specific trace through the
  /// block. Convergent traces means that only one of these is required per
  /// block in a trace ensemble.
  struct TraceBlockInfo {
    /// Trace predecessor, or NULL for the first block in the trace.
    /// Valid when hasValidDepth().
    const MachineBasicBlock *Pred = nullptr;

    /// Trace successor, or NULL for the last block in the trace.
    /// Valid when hasValidHeight().
    const MachineBasicBlock *Succ = nullptr;

    /// The block number of the head of the trace. (When hasValidDepth()).
    unsigned Head;

    /// The block number of the tail of the trace. (When hasValidHeight()).
    unsigned Tail;

    /// Accumulated number of instructions in the trace above this block.
    /// Does not include instructions in this block.
    unsigned InstrDepth = ~0u;

    /// Accumulated number of instructions in the trace below this block.
    /// Includes instructions in this block.
    unsigned InstrHeight = ~0u;

    TraceBlockInfo() = default;

    /// Returns true if the depth resources have been computed from the trace
    /// above this block.
    bool hasValidDepth() const { return InstrDepth != ~0u; }

    /// Returns true if the height resources have been computed from the trace
    /// below this block.
    bool hasValidHeight() const { return InstrHeight != ~0u; }

    /// Invalidate depth resources when some block above this one has changed.
    void invalidateDepth() { InstrDepth = ~0u; HasValidInstrDepths = false; }

    /// Invalidate height resources when a block below this one has changed.
    void invalidateHeight() { InstrHeight = ~0u; HasValidInstrHeights = false; }

    /// Assuming that this is a dominator of TBI, determine if it contains
    /// useful instruction depths. A dominating block can be above the current
    /// trace head, and any dependencies from such a far away dominator are not
    /// expected to affect the critical path.
    ///
    /// Also returns true when TBI == this.
    bool isUsefulDominator(const TraceBlockInfo &TBI) const {
      // The trace for TBI may not even be calculated yet.
      if (!hasValidDepth() || !TBI.hasValidDepth())
        return false;
      // Instruction depths are only comparable if the traces share a head.
      if (Head != TBI.Head)
        return false;
      // It is almost always the case that TBI belongs to the same trace as
      // this block, but rare convoluted cases involving irreducible control
      // flow, a dominator may share a trace head without actually being on the
      // same trace as TBI. This is not a big problem as long as it doesn't
      // increase the instruction depth.
      return HasValidInstrDepths && InstrDepth <= TBI.InstrDepth;
    }

    // Data-dependency-related information. Per-instruction depth and height
    // are computed from data dependencies in the current trace, using
    // itinerary data.

    /// Instruction depths have been computed. This implies hasValidDepth().
    bool HasValidInstrDepths = false;

    /// Instruction heights have been computed. This implies hasValidHeight().
    bool HasValidInstrHeights = false;

    /// Critical path length. This is the number of cycles in the longest data
    /// dependency chain through the trace. This is only valid when both
    /// HasValidInstrDepths and HasValidInstrHeights are set.
    unsigned CriticalPath;

    /// Live-in registers. These registers are defined above the current block
    /// and used by this block or a block below it.
    /// This does not include PHI uses in the current block, but it does
    /// include PHI uses in deeper blocks.
    SmallVector<LiveInReg, 4> LiveIns;

    void print(raw_ostream&) const;
  };

  /// InstrCycles represents the cycle height and depth of an instruction in a
  /// trace.
  struct InstrCycles {
    /// Earliest issue cycle as determined by data dependencies and instruction
    /// latencies from the beginning of the trace. Data dependencies from
    /// before the trace are not included.
    unsigned Depth;

    /// Minimum number of cycles from this instruction is issued to the of the
    /// trace, as determined by data dependencies and instruction latencies.
    unsigned Height;
  };

  /// A trace represents a plausible sequence of executed basic blocks that
  /// passes through the current basic block one. The Trace class serves as a
  /// handle to internal cached data structures.
  class Trace {
    Ensemble &TE;
    TraceBlockInfo &TBI;

    unsigned getBlockNum() const { return &TBI - &TE.BlockInfo[0]; }

  public:
    explicit Trace(Ensemble &te, TraceBlockInfo &tbi) : TE(te), TBI(tbi) {}

    void print(raw_ostream&) const;

    /// Compute the total number of instructions in the trace.
    unsigned getInstrCount() const {
      return TBI.InstrDepth + TBI.InstrHeight;
    }

    /// Return the resource depth of the top/bottom of the trace center block.
    /// This is the number of cycles required to execute all instructions from
    /// the trace head to the trace center block. The resource depth only
    /// considers execution resources, it ignores data dependencies.
    /// When Bottom is set, instructions in the trace center block are included.
    unsigned getResourceDepth(bool Bottom) const;

    /// Return the resource length of the trace. This is the number of cycles
    /// required to execute the instructions in the trace if they were all
    /// independent, exposing the maximum instruction-level parallelism.
    ///
    /// Any blocks in Extrablocks are included as if they were part of the
    /// trace. Likewise, extra resources required by the specified scheduling
    /// classes are included. For the caller to account for extra machine
    /// instructions, it must first resolve each instruction's scheduling class.
    unsigned getResourceLength(
        ArrayRef<const MachineBasicBlock *> Extrablocks = None,
        ArrayRef<const MCSchedClassDesc *> ExtraInstrs = None,
        ArrayRef<const MCSchedClassDesc *> RemoveInstrs = None) const;

    /// Return the length of the (data dependency) critical path through the
    /// trace.
    unsigned getCriticalPath() const { return TBI.CriticalPath; }

    /// Return the depth and height of MI. The depth is only valid for
    /// instructions in or above the trace center block. The height is only
    /// valid for instructions in or below the trace center block.
    InstrCycles getInstrCycles(const MachineInstr &MI) const {
      return TE.Cycles.lookup(&MI);
    }

    /// Return the slack of MI. This is the number of cycles MI can be delayed
    /// before the critical path becomes longer.
    /// MI must be an instruction in the trace center block.
    unsigned getInstrSlack(const MachineInstr &MI) const;

    /// Return the Depth of a PHI instruction in a trace center block successor.
    /// The PHI does not have to be part of the trace.
    unsigned getPHIDepth(const MachineInstr &PHI) const;

    /// A dependence is useful if the basic block of the defining instruction
    /// is part of the trace of the user instruction. It is assumed that DefMI
    /// dominates UseMI (see also isUsefulDominator).
    bool isDepInTrace(const MachineInstr &DefMI,
                      const MachineInstr &UseMI) const;
  };

  /// A trace ensemble is a collection of traces selected using the same
  /// strategy, for example 'minimum resource height'. There is one trace for
  /// every block in the function.
  class Ensemble {
    friend class Trace;

    SmallVector<TraceBlockInfo, 4> BlockInfo;
    DenseMap<const MachineInstr*, InstrCycles> Cycles;
    SmallVector<unsigned, 0> ProcResourceDepths;
    SmallVector<unsigned, 0> ProcResourceHeights;

    void computeTrace(const MachineBasicBlock*);
    void computeDepthResources(const MachineBasicBlock*);
    void computeHeightResources(const MachineBasicBlock*);
    unsigned computeCrossBlockCriticalPath(const TraceBlockInfo&);
    void computeInstrDepths(const MachineBasicBlock*);
    void computeInstrHeights(const MachineBasicBlock*);
    void addLiveIns(const MachineInstr *DefMI, unsigned DefOp,
                    ArrayRef<const MachineBasicBlock*> Trace);

  protected:
    MachineTraceMetrics &MTM;

    explicit Ensemble(MachineTraceMetrics*);

    virtual const MachineBasicBlock *pickTracePred(const MachineBasicBlock*) =0;
    virtual const MachineBasicBlock *pickTraceSucc(const MachineBasicBlock*) =0;
    const MachineLoop *getLoopFor(const MachineBasicBlock*) const;
    const TraceBlockInfo *getDepthResources(const MachineBasicBlock*) const;
    const TraceBlockInfo *getHeightResources(const MachineBasicBlock*) const;
    ArrayRef<unsigned> getProcResourceDepths(unsigned MBBNum) const;
    ArrayRef<unsigned> getProcResourceHeights(unsigned MBBNum) const;

  public:
    virtual ~Ensemble();

    virtual const char *getName() const = 0;
    void print(raw_ostream&) const;
    void invalidate(const MachineBasicBlock *MBB);
    void verify() const;

    /// Get the trace that passes through MBB.
    /// The trace is computed on demand.
    Trace getTrace(const MachineBasicBlock *MBB);

    /// Updates the depth of an machine instruction, given RegUnits.
    void updateDepth(TraceBlockInfo &TBI, const MachineInstr&,
                     SparseSet<LiveRegUnit> &RegUnits);
    void updateDepth(const MachineBasicBlock *, const MachineInstr&,
                     SparseSet<LiveRegUnit> &RegUnits);

    /// Updates the depth of the instructions from Start to End.
    void updateDepths(MachineBasicBlock::iterator Start,
                      MachineBasicBlock::iterator End,
                      SparseSet<LiveRegUnit> &RegUnits);

  };

  /// Strategies for selecting traces.
  enum Strategy {
    /// Select the trace through a block that has the fewest instructions.
    TS_MinInstrCount,

    TS_NumStrategies
  };

  /// Get the trace ensemble representing the given trace selection strategy.
  /// The returned Ensemble object is owned by the MachineTraceMetrics analysis,
  /// and valid for the lifetime of the analysis pass.
  Ensemble *getEnsemble(Strategy);

  /// Invalidate cached information about MBB. This must be called *before* MBB
  /// is erased, or the CFG is otherwise changed.
  ///
  /// This invalidates per-block information about resource usage for MBB only,
  /// and it invalidates per-trace information for any trace that passes
  /// through MBB.
  ///
  /// Call Ensemble::getTrace() again to update any trace handles.
  void invalidate(const MachineBasicBlock *MBB);

private:
  // One entry per basic block, indexed by block number.
  SmallVector<FixedBlockInfo, 4> BlockInfo;

  // Cycles consumed on each processor resource per block.
  // The number of processor resource kinds is constant for a given subtarget,
  // but it is not known at compile time. The number of cycles consumed by
  // block B on processor resource R is at ProcResourceCycles[B*Kinds + R]
  // where Kinds = SchedModel.getNumProcResourceKinds().
  SmallVector<unsigned, 0> ProcResourceCycles;

  // One ensemble per strategy.
  Ensemble* Ensembles[TS_NumStrategies];

  // Convert scaled resource usage to a cycle count that can be compared with
  // latencies.
  unsigned getCycles(unsigned Scaled) {
    unsigned Factor = SchedModel.getLatencyFactor();
    return (Scaled + Factor - 1) / Factor;
  }
};

inline raw_ostream &operator<<(raw_ostream &OS,
                               const MachineTraceMetrics::Trace &Tr) {
  Tr.print(OS);
  return OS;
}

inline raw_ostream &operator<<(raw_ostream &OS,
                               const MachineTraceMetrics::Ensemble &En) {
  En.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINETRACEMETRICS_H
