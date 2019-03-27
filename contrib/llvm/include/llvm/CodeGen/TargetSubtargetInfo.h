//===- llvm/CodeGen/TargetSubtargetInfo.h - Target Information --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the subtarget options of a Target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETSUBTARGETINFO_H
#define LLVM_CODEGEN_TARGETSUBTARGETINFO_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/PBQPRAConstraint.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CodeGen.h"
#include <memory>
#include <vector>


namespace llvm {

class CallLowering;
class InstrItineraryData;
struct InstrStage;
class InstructionSelector;
class LegalizerInfo;
class MachineInstr;
struct MachineSchedPolicy;
struct MCReadAdvanceEntry;
struct MCWriteLatencyEntry;
struct MCWriteProcResEntry;
class RegisterBankInfo;
class SDep;
class SelectionDAGTargetInfo;
struct SubtargetFeatureKV;
struct SubtargetInfoKV;
class SUnit;
class TargetFrameLowering;
class TargetInstrInfo;
class TargetLowering;
class TargetRegisterClass;
class TargetRegisterInfo;
class TargetSchedModel;
class Triple;

//===----------------------------------------------------------------------===//
///
/// TargetSubtargetInfo - Generic base class for all target subtargets.  All
/// Target-specific options that control code generation and printing should
/// be exposed through a TargetSubtargetInfo-derived class.
///
class TargetSubtargetInfo : public MCSubtargetInfo {
protected: // Can only create subclasses...
  TargetSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS,
                      ArrayRef<SubtargetFeatureKV> PF,
                      ArrayRef<SubtargetFeatureKV> PD,
                      const SubtargetInfoKV *ProcSched,
                      const MCWriteProcResEntry *WPR,
                      const MCWriteLatencyEntry *WL,
                      const MCReadAdvanceEntry *RA, const InstrStage *IS,
                      const unsigned *OC, const unsigned *FP);

public:
  // AntiDepBreakMode - Type of anti-dependence breaking that should
  // be performed before post-RA scheduling.
  using AntiDepBreakMode = enum { ANTIDEP_NONE, ANTIDEP_CRITICAL, ANTIDEP_ALL };
  using RegClassVector = SmallVectorImpl<const TargetRegisterClass *>;

  TargetSubtargetInfo() = delete;
  TargetSubtargetInfo(const TargetSubtargetInfo &) = delete;
  TargetSubtargetInfo &operator=(const TargetSubtargetInfo &) = delete;
  ~TargetSubtargetInfo() override;

  virtual bool isXRaySupported() const { return false; }

  // Interfaces to the major aspects of target machine information:
  //
  // -- Instruction opcode and operand information
  // -- Pipelines and scheduling information
  // -- Stack frame information
  // -- Selection DAG lowering information
  // -- Call lowering information
  //
  // N.B. These objects may change during compilation. It's not safe to cache
  // them between functions.
  virtual const TargetInstrInfo *getInstrInfo() const { return nullptr; }
  virtual const TargetFrameLowering *getFrameLowering() const {
    return nullptr;
  }
  virtual const TargetLowering *getTargetLowering() const { return nullptr; }
  virtual const SelectionDAGTargetInfo *getSelectionDAGInfo() const {
    return nullptr;
  }
  virtual const CallLowering *getCallLowering() const { return nullptr; }

  // FIXME: This lets targets specialize the selector by subtarget (which lets
  // us do things like a dedicated avx512 selector).  However, we might want
  // to also specialize selectors by MachineFunction, which would let us be
  // aware of optsize/optnone and such.
  virtual const InstructionSelector *getInstructionSelector() const {
    return nullptr;
  }

  virtual unsigned getHwMode() const { return 0; }

  /// Target can subclass this hook to select a different DAG scheduler.
  virtual RegisterScheduler::FunctionPassCtor
      getDAGScheduler(CodeGenOpt::Level) const {
    return nullptr;
  }

  virtual const LegalizerInfo *getLegalizerInfo() const { return nullptr; }

  /// getRegisterInfo - If register information is available, return it.  If
  /// not, return null.
  virtual const TargetRegisterInfo *getRegisterInfo() const { return nullptr; }

  /// If the information for the register banks is available, return it.
  /// Otherwise return nullptr.
  virtual const RegisterBankInfo *getRegBankInfo() const { return nullptr; }

  /// getInstrItineraryData - Returns instruction itinerary data for the target
  /// or specific subtarget.
  virtual const InstrItineraryData *getInstrItineraryData() const {
    return nullptr;
  }

  /// Resolve a SchedClass at runtime, where SchedClass identifies an
  /// MCSchedClassDesc with the isVariant property. This may return the ID of
  /// another variant SchedClass, but repeated invocation must quickly terminate
  /// in a nonvariant SchedClass.
  virtual unsigned resolveSchedClass(unsigned SchedClass,
                                     const MachineInstr *MI,
                                     const TargetSchedModel *SchedModel) const {
    return 0;
  }

  /// Returns true if MI is a dependency breaking zero-idiom instruction for the
  /// subtarget.
  ///
  /// This function also sets bits in Mask related to input operands that
  /// are not in a data dependency relationship.  There is one bit for each
  /// machine operand; implicit operands follow explicit operands in the bit
  /// representation used for Mask.  An empty (i.e. a mask with all bits
  /// cleared) means: data dependencies are "broken" for all the explicit input
  /// machine operands of MI.
  virtual bool isZeroIdiom(const MachineInstr *MI, APInt &Mask) const {
    return false;
  }

  /// Returns true if MI is a dependency breaking instruction for the subtarget.
  ///
  /// Similar in behavior to `isZeroIdiom`. However, it knows how to identify
  /// all dependency breaking instructions (i.e. not just zero-idioms).
  /// 
  /// As for `isZeroIdiom`, this method returns a mask of "broken" dependencies.
  /// (See method `isZeroIdiom` for a detailed description of Mask).
  virtual bool isDependencyBreaking(const MachineInstr *MI, APInt &Mask) const {
    return isZeroIdiom(MI, Mask);
  }

  /// Returns true if MI is a candidate for move elimination.
  ///
  /// A candidate for move elimination may be optimized out at register renaming
  /// stage. Subtargets can specify the set of optimizable moves by
  /// instantiating tablegen class `IsOptimizableRegisterMove` (see
  /// llvm/Target/TargetInstrPredicate.td).
  ///
  /// SubtargetEmitter is responsible for processing all the definitions of class
  /// IsOptimizableRegisterMove, and auto-generate an override for this method.
  virtual bool isOptimizableRegisterMove(const MachineInstr *MI) const {
    return false;
  }

  /// True if the subtarget should run MachineScheduler after aggressive
  /// coalescing.
  ///
  /// This currently replaces the SelectionDAG scheduler with the "source" order
  /// scheduler (though see below for an option to turn this off and use the
  /// TargetLowering preference). It does not yet disable the postRA scheduler.
  virtual bool enableMachineScheduler() const;

  /// Support printing of [latency:throughput] comment in output .S file.
  virtual bool supportPrintSchedInfo() const { return false; }

  /// True if the machine scheduler should disable the TLI preference
  /// for preRA scheduling with the source level scheduler.
  virtual bool enableMachineSchedDefaultSched() const { return true; }

  /// True if the subtarget should enable joining global copies.
  ///
  /// By default this is enabled if the machine scheduler is enabled, but
  /// can be overridden.
  virtual bool enableJoinGlobalCopies() const;

  /// True if the subtarget should run a scheduler after register allocation.
  ///
  /// By default this queries the PostRAScheduling bit in the scheduling model
  /// which is the preferred way to influence this.
  virtual bool enablePostRAScheduler() const;

  /// True if the subtarget should run the atomic expansion pass.
  virtual bool enableAtomicExpand() const;

  /// True if the subtarget should run the indirectbr expansion pass.
  virtual bool enableIndirectBrExpand() const;

  /// Override generic scheduling policy within a region.
  ///
  /// This is a convenient way for targets that don't provide any custom
  /// scheduling heuristics (no custom MachineSchedStrategy) to make
  /// changes to the generic scheduling policy.
  virtual void overrideSchedPolicy(MachineSchedPolicy &Policy,
                                   unsigned NumRegionInstrs) const {}

  // Perform target specific adjustments to the latency of a schedule
  // dependency.
  virtual void adjustSchedDependency(SUnit *def, SUnit *use, SDep &dep) const {}

  // For use with PostRAScheduling: get the anti-dependence breaking that should
  // be performed before post-RA scheduling.
  virtual AntiDepBreakMode getAntiDepBreakMode() const { return ANTIDEP_NONE; }

  // For use with PostRAScheduling: in CriticalPathRCs, return any register
  // classes that should only be considered for anti-dependence breaking if they
  // are on the critical path.
  virtual void getCriticalPathRCs(RegClassVector &CriticalPathRCs) const {
    return CriticalPathRCs.clear();
  }

  // Provide an ordered list of schedule DAG mutations for the post-RA
  // scheduler.
  virtual void getPostRAMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  }

  // Provide an ordered list of schedule DAG mutations for the machine
  // pipeliner.
  virtual void getSMSMutations(
      std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  }

  // For use with PostRAScheduling: get the minimum optimization level needed
  // to enable post-RA scheduling.
  virtual CodeGenOpt::Level getOptLevelToEnablePostRAScheduler() const {
    return CodeGenOpt::Default;
  }

  /// True if the subtarget should run the local reassignment
  /// heuristic of the register allocator.
  /// This heuristic may be compile time intensive, \p OptLevel provides
  /// a finer grain to tune the register allocator.
  virtual bool enableRALocalReassignment(CodeGenOpt::Level OptLevel) const;

  /// True if the subtarget should consider the cost of local intervals
  /// created by a split candidate when choosing the best split candidate. This
  /// heuristic may be compile time intensive.
  virtual bool enableAdvancedRASplitCost() const;

  /// Enable use of alias analysis during code generation (during MI
  /// scheduling, DAGCombine, etc.).
  virtual bool useAA() const;

  /// Enable the use of the early if conversion pass.
  virtual bool enableEarlyIfConversion() const { return false; }

  /// Return PBQPConstraint(s) for the target.
  ///
  /// Override to provide custom PBQP constraints.
  virtual std::unique_ptr<PBQPRAConstraint> getCustomPBQPConstraints() const {
    return nullptr;
  }

  /// Enable tracking of subregister liveness in register allocator.
  /// Please use MachineRegisterInfo::subRegLivenessEnabled() instead where
  /// possible.
  virtual bool enableSubRegLiveness() const { return false; }

  /// Returns string representation of scheduler comment
  std::string getSchedInfoStr(const MachineInstr &MI) const;
  std::string getSchedInfoStr(MCInst const &MCI) const override;

  /// This is called after a .mir file was loaded.
  virtual void mirFileLoaded(MachineFunction &MF) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETSUBTARGETINFO_H
