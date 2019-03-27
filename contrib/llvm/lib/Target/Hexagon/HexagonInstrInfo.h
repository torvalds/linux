//===- HexagonInstrInfo.h - Hexagon Instruction Information -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONINSTRINFO_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONINSTRINFO_H

#include "MCTargetDesc/HexagonBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/Support/MachineValueType.h"
#include <cstdint>
#include <vector>

#define GET_INSTRINFO_HEADER
#include "HexagonGenInstrInfo.inc"

namespace llvm {

class HexagonSubtarget;
class MachineBranchProbabilityInfo;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class TargetRegisterInfo;

class HexagonInstrInfo : public HexagonGenInstrInfo {
  const HexagonSubtarget &Subtarget;

  enum BundleAttribute {
    memShufDisabledMask = 0x4
  };

  virtual void anchor();

public:
  explicit HexagonInstrInfo(HexagonSubtarget &ST);

  /// TargetInstrInfo overrides.

  /// If the specified machine instruction is a direct
  /// load from a stack slot, return the virtual or physical register number of
  /// the destination along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than loading from the stack slot.
  unsigned isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;

  /// If the specified machine instruction is a direct
  /// store to a stack slot, return the virtual or physical register number of
  /// the source reg along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than storing to the stack slot.
  unsigned isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  /// Check if the instruction or the bundle of instructions has
  /// load from stack slots. Return the frameindex and machine memory operand
  /// if true.
  bool hasLoadFromStackSlot(
      const MachineInstr &MI,
      SmallVectorImpl<const MachineMemOperand *> &Accesses) const override;

  /// Check if the instruction or the bundle of instructions has
  /// store to stack slots. Return the frameindex and machine memory operand
  /// if true.
  bool hasStoreToStackSlot(
      const MachineInstr &MI,
      SmallVectorImpl<const MachineMemOperand *> &Accesses) const override;

  /// Analyze the branching code at the end of MBB, returning
  /// true if it cannot be understood (e.g. it's a switch dispatch or isn't
  /// implemented for a target).  Upon success, this returns false and returns
  /// with the following information in various cases:
  ///
  /// 1. If this block ends with no branches (it just falls through to its succ)
  ///    just return false, leaving TBB/FBB null.
  /// 2. If this block ends with only an unconditional branch, it sets TBB to be
  ///    the destination block.
  /// 3. If this block ends with a conditional branch and it falls through to a
  ///    successor block, it sets TBB to be the branch destination block and a
  ///    list of operands that evaluate the condition. These operands can be
  ///    passed to other TargetInstrInfo methods to create new branches.
  /// 4. If this block ends with a conditional branch followed by an
  ///    unconditional branch, it returns the 'true' destination in TBB, the
  ///    'false' destination in FBB, and a list of operands that evaluate the
  ///    condition.  These operands can be passed to other TargetInstrInfo
  ///    methods to create new branches.
  ///
  /// Note that removeBranch and insertBranch must be implemented to support
  /// cases where this method returns success.
  ///
  /// If AllowModify is true, then this routine is allowed to modify the basic
  /// block (e.g. delete instructions after the unconditional branch).
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  /// Remove the branching code at the end of the specific MBB.
  /// This is only invoked in cases where AnalyzeBranch returns success. It
  /// returns the number of instructions that were removed.
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  /// Insert branch code into the end of the specified MachineBasicBlock.
  /// The operands to this method are the same as those
  /// returned by AnalyzeBranch.  This is only invoked in cases where
  /// AnalyzeBranch returns success. It returns the number of instructions
  /// inserted.
  ///
  /// It is also invoked by tail merging to add unconditional branches in
  /// cases where AnalyzeBranch doesn't apply because there was no original
  /// branch to analyze.  At least this much must be implemented, else tail
  /// merging needs to be disabled.
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  /// Analyze the loop code, return true if it cannot be understood. Upon
  /// success, this function returns false and returns information about the
  /// induction variable and compare instruction used at the end.
  bool analyzeLoop(MachineLoop &L, MachineInstr *&IndVarInst,
                   MachineInstr *&CmpInst) const override;

  /// Generate code to reduce the loop iteration by one and check if the loop
  /// is finished.  Return the value/register of the new loop count.  We need
  /// this function when peeling off one or more iterations of a loop. This
  /// function assumes the nth iteration is peeled first.
  unsigned reduceLoopCount(MachineBasicBlock &MBB,
                           MachineInstr *IndVar, MachineInstr &Cmp,
                           SmallVectorImpl<MachineOperand> &Cond,
                           SmallVectorImpl<MachineInstr *> &PrevInsts,
                           unsigned Iter, unsigned MaxIter) const override;

  /// Return true if it's profitable to predicate
  /// instructions with accumulated instruction latency of "NumCycles"
  /// of the specified basic block, where the probability of the instructions
  /// being executed is given by Probability, and Confidence is a measure
  /// of our confidence that it will be properly predicted.
  bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                           unsigned ExtraPredCycles,
                           BranchProbability Probability) const override;

  /// Second variant of isProfitableToIfCvt. This one
  /// checks for the case where two basic blocks from true and false path
  /// of a if-then-else (diamond) are predicated on mutally exclusive
  /// predicates, where the probability of the true path being taken is given
  /// by Probability, and Confidence is a measure of our confidence that it
  /// will be properly predicted.
  bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                           unsigned NumTCycles, unsigned ExtraTCycles,
                           MachineBasicBlock &FMBB,
                           unsigned NumFCycles, unsigned ExtraFCycles,
                           BranchProbability Probability) const override;

  /// Return true if it's profitable for if-converter to duplicate instructions
  /// of specified accumulated instruction latencies in the specified MBB to
  /// enable if-conversion.
  /// The probability of the instructions being executed is given by
  /// Probability, and Confidence is a measure of our confidence that it
  /// will be properly predicted.
  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                                 BranchProbability Probability) const override;

  /// Emit instructions to copy a pair of physical registers.
  ///
  /// This function should support copies within any legal register class as
  /// well as any cross-class copies created during instruction selection.
  ///
  /// The source and destination registers may overlap, which may require a
  /// careful implementation when multiple copy instructions are required for
  /// large registers. See for example the ARM target.
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;

  /// Store the specified register of the given register class to the specified
  /// stack frame index. The store instruction is to be added to the given
  /// machine basic block before the specified machine instruction. If isKill
  /// is true, the register operand is the last use and must be marked kill.
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;

  /// Load the specified register of the given register class from the specified
  /// stack frame index. The load instruction is to be added to the given
  /// machine basic block before the specified machine instruction.
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  /// This function is called for all pseudo instructions
  /// that remain after register allocation. Many pseudo instructions are
  /// created to help register allocation. This is the place to convert them
  /// into real instructions. The target can edit MI in place, or it can insert
  /// new instructions and erase MI. The function should return true if
  /// anything was changed.
  bool expandPostRAPseudo(MachineInstr &MI) const override;

  /// Get the base register and byte offset of a load/store instr.
  bool getMemOperandWithOffset(MachineInstr &LdSt, MachineOperand *&BaseOp,
                               int64_t &Offset,
                               const TargetRegisterInfo *TRI) const override;

  /// Reverses the branch condition of the specified condition list,
  /// returning false on success and true if it cannot be reversed.
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond)
        const override;

  /// Insert a noop into the instruction stream at the specified point.
  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  /// Returns true if the instruction is already predicated.
  bool isPredicated(const MachineInstr &MI) const override;

  /// Return true for post-incremented instructions.
  bool isPostIncrement(const MachineInstr &MI) const override;

  /// Convert the instruction into a predicated instruction.
  /// It returns true if the operation was successful.
  bool PredicateInstruction(MachineInstr &MI,
                            ArrayRef<MachineOperand> Cond) const override;

  /// Returns true if the first specified predicate
  /// subsumes the second, e.g. GE subsumes GT.
  bool SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                         ArrayRef<MachineOperand> Pred2) const override;

  /// If the specified instruction defines any predicate
  /// or condition code register(s) used for predication, returns true as well
  /// as the definition predicate(s) by reference.
  bool DefinesPredicate(MachineInstr &MI,
                        std::vector<MachineOperand> &Pred) const override;

  /// Return true if the specified instruction can be predicated.
  /// By default, this returns true for every instruction with a
  /// PredicateOperand.
  bool isPredicable(const MachineInstr &MI) const override;

  /// Test if the given instruction should be considered a scheduling boundary.
  /// This primarily includes labels and terminators.
  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  /// Measure the specified inline asm to determine an approximation of its
  /// length.
  unsigned getInlineAsmLength(const char *Str,
                              const MCAsmInfo &MAI) const override;

  /// Allocate and return a hazard recognizer to use for this target when
  /// scheduling the machine instructions after register allocation.
  ScheduleHazardRecognizer*
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                     const ScheduleDAG *DAG) const override;

  /// For a comparison instruction, return the source registers
  /// in SrcReg and SrcReg2 if having two register operands, and the value it
  /// compares against in CmpValue. Return true if the comparison instruction
  /// can be analyzed.
  bool analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                      unsigned &SrcReg2, int &Mask, int &Value) const override;

  /// Compute the instruction latency of a given instruction.
  /// If the instruction has higher cost when predicated, it's returned via
  /// PredCost.
  unsigned getInstrLatency(const InstrItineraryData *ItinData,
                           const MachineInstr &MI,
                           unsigned *PredCost = nullptr) const override;

  /// Create machine specific model for scheduling.
  DFAPacketizer *
  CreateTargetScheduleState(const TargetSubtargetInfo &STI) const override;

  // Sometimes, it is possible for the target
  // to tell, even without aliasing information, that two MIs access different
  // memory addresses. This function returns true if two MIs access different
  // memory addresses and false otherwise.
  bool
  areMemAccessesTriviallyDisjoint(MachineInstr &MIa, MachineInstr &MIb,
                                  AliasAnalysis *AA = nullptr) const override;

  /// For instructions with a base and offset, return the position of the
  /// base register and offset operands.
  bool getBaseAndOffsetPosition(const MachineInstr &MI, unsigned &BasePos,
                                unsigned &OffsetPos) const override;

  /// If the instruction is an increment of a constant value, return the amount.
  bool getIncrementValue(const MachineInstr &MI, int &Value) const override;

  /// getOperandLatency - Compute and return the use operand latency of a given
  /// pair of def and use.
  /// In most cases, the static scheduling itinerary was enough to determine the
  /// operand latency. But it may not be possible for instructions with variable
  /// number of defs / uses.
  ///
  /// This is a raw interface to the itinerary that may be directly overriden by
  /// a target. Use computeOperandLatency to get the best estimate of latency.
  int getOperandLatency(const InstrItineraryData *ItinData,
                        const MachineInstr &DefMI, unsigned DefIdx,
                        const MachineInstr &UseMI,
                        unsigned UseIdx) const override;

  /// Decompose the machine operand's target flags into two values - the direct
  /// target flag value and any of bit flags that are applied.
  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  /// Return an array that contains the direct target flag values and their
  /// names.
  ///
  /// MIR Serialization is able to serialize only the target flags that are
  /// defined by this method.
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  /// Return an array that contains the bitmask target flag values and their
  /// names.
  ///
  /// MIR Serialization is able to serialize only the target flags that are
  /// defined by this method.
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableBitmaskMachineOperandTargetFlags() const override;

  bool isTailCall(const MachineInstr &MI) const override;

  /// HexagonInstrInfo specifics.

  unsigned createVR(MachineFunction *MF, MVT VT) const;
  MachineInstr *findLoopInstr(MachineBasicBlock *BB, unsigned EndLoopOp,
                              MachineBasicBlock *TargetBB,
                              SmallPtrSet<MachineBasicBlock *, 8> &Visited) const;

  bool isBaseImmOffset(const MachineInstr &MI) const;
  bool isAbsoluteSet(const MachineInstr &MI) const;
  bool isAccumulator(const MachineInstr &MI) const;
  bool isAddrModeWithOffset(const MachineInstr &MI) const;
  bool isComplex(const MachineInstr &MI) const;
  bool isCompoundBranchInstr(const MachineInstr &MI) const;
  bool isConstExtended(const MachineInstr &MI) const;
  bool isDeallocRet(const MachineInstr &MI) const;
  bool isDependent(const MachineInstr &ProdMI,
                   const MachineInstr &ConsMI) const;
  bool isDotCurInst(const MachineInstr &MI) const;
  bool isDotNewInst(const MachineInstr &MI) const;
  bool isDuplexPair(const MachineInstr &MIa, const MachineInstr &MIb) const;
  bool isEarlySourceInstr(const MachineInstr &MI) const;
  bool isEndLoopN(unsigned Opcode) const;
  bool isExpr(unsigned OpType) const;
  bool isExtendable(const MachineInstr &MI) const;
  bool isExtended(const MachineInstr &MI) const;
  bool isFloat(const MachineInstr &MI) const;
  bool isHVXMemWithAIndirect(const MachineInstr &I,
                             const MachineInstr &J) const;
  bool isIndirectCall(const MachineInstr &MI) const;
  bool isIndirectL4Return(const MachineInstr &MI) const;
  bool isJumpR(const MachineInstr &MI) const;
  bool isJumpWithinBranchRange(const MachineInstr &MI, unsigned offset) const;
  bool isLateInstrFeedsEarlyInstr(const MachineInstr &LRMI,
                                  const MachineInstr &ESMI) const;
  bool isLateResultInstr(const MachineInstr &MI) const;
  bool isLateSourceInstr(const MachineInstr &MI) const;
  bool isLoopN(const MachineInstr &MI) const;
  bool isMemOp(const MachineInstr &MI) const;
  bool isNewValue(const MachineInstr &MI) const;
  bool isNewValue(unsigned Opcode) const;
  bool isNewValueInst(const MachineInstr &MI) const;
  bool isNewValueJump(const MachineInstr &MI) const;
  bool isNewValueJump(unsigned Opcode) const;
  bool isNewValueStore(const MachineInstr &MI) const;
  bool isNewValueStore(unsigned Opcode) const;
  bool isOperandExtended(const MachineInstr &MI, unsigned OperandNum) const;
  bool isPredicatedNew(const MachineInstr &MI) const;
  bool isPredicatedNew(unsigned Opcode) const;
  bool isPredicatedTrue(const MachineInstr &MI) const;
  bool isPredicatedTrue(unsigned Opcode) const;
  bool isPredicated(unsigned Opcode) const;
  bool isPredicateLate(unsigned Opcode) const;
  bool isPredictedTaken(unsigned Opcode) const;
  bool isSaveCalleeSavedRegsCall(const MachineInstr &MI) const;
  bool isSignExtendingLoad(const MachineInstr &MI) const;
  bool isSolo(const MachineInstr &MI) const;
  bool isSpillPredRegOp(const MachineInstr &MI) const;
  bool isTC1(const MachineInstr &MI) const;
  bool isTC2(const MachineInstr &MI) const;
  bool isTC2Early(const MachineInstr &MI) const;
  bool isTC4x(const MachineInstr &MI) const;
  bool isToBeScheduledASAP(const MachineInstr &MI1,
                           const MachineInstr &MI2) const;
  bool isHVXVec(const MachineInstr &MI) const;
  bool isValidAutoIncImm(const EVT VT, const int Offset) const;
  bool isValidOffset(unsigned Opcode, int Offset,
                     const TargetRegisterInfo *TRI, bool Extend = true) const;
  bool isVecAcc(const MachineInstr &MI) const;
  bool isVecALU(const MachineInstr &MI) const;
  bool isVecUsableNextPacket(const MachineInstr &ProdMI,
                             const MachineInstr &ConsMI) const;
  bool isZeroExtendingLoad(const MachineInstr &MI) const;

  bool addLatencyToSchedule(const MachineInstr &MI1,
                            const MachineInstr &MI2) const;
  bool canExecuteInBundle(const MachineInstr &First,
                          const MachineInstr &Second) const;
  bool doesNotReturn(const MachineInstr &CallMI) const;
  bool hasEHLabel(const MachineBasicBlock *B) const;
  bool hasNonExtEquivalent(const MachineInstr &MI) const;
  bool hasPseudoInstrPair(const MachineInstr &MI) const;
  bool hasUncondBranch(const MachineBasicBlock *B) const;
  bool mayBeCurLoad(const MachineInstr &MI) const;
  bool mayBeNewStore(const MachineInstr &MI) const;
  bool producesStall(const MachineInstr &ProdMI,
                     const MachineInstr &ConsMI) const;
  bool producesStall(const MachineInstr &MI,
                     MachineBasicBlock::const_instr_iterator MII) const;
  bool predCanBeUsedAsDotNew(const MachineInstr &MI, unsigned PredReg) const;
  bool PredOpcodeHasJMP_c(unsigned Opcode) const;
  bool predOpcodeHasNot(ArrayRef<MachineOperand> Cond) const;

  unsigned getAddrMode(const MachineInstr &MI) const;
  MachineOperand *getBaseAndOffset(const MachineInstr &MI, int64_t &Offset,
                                   unsigned &AccessSize) const;
  SmallVector<MachineInstr*,2> getBranchingInstrs(MachineBasicBlock& MBB) const;
  unsigned getCExtOpNum(const MachineInstr &MI) const;
  HexagonII::CompoundGroup
  getCompoundCandidateGroup(const MachineInstr &MI) const;
  unsigned getCompoundOpcode(const MachineInstr &GA,
                             const MachineInstr &GB) const;
  int getCondOpcode(int Opc, bool sense) const;
  int getDotCurOp(const MachineInstr &MI) const;
  int getNonDotCurOp(const MachineInstr &MI) const;
  int getDotNewOp(const MachineInstr &MI) const;
  int getDotNewPredJumpOp(const MachineInstr &MI,
                          const MachineBranchProbabilityInfo *MBPI) const;
  int getDotNewPredOp(const MachineInstr &MI,
                      const MachineBranchProbabilityInfo *MBPI) const;
  int getDotOldOp(const MachineInstr &MI) const;
  HexagonII::SubInstructionGroup getDuplexCandidateGroup(const MachineInstr &MI)
                                                         const;
  short getEquivalentHWInstr(const MachineInstr &MI) const;
  unsigned getInstrTimingClassLatency(const InstrItineraryData *ItinData,
                                      const MachineInstr &MI) const;
  bool getInvertedPredSense(SmallVectorImpl<MachineOperand> &Cond) const;
  unsigned getInvertedPredicatedOpcode(const int Opc) const;
  int getMaxValue(const MachineInstr &MI) const;
  unsigned getMemAccessSize(const MachineInstr &MI) const;
  int getMinValue(const MachineInstr &MI) const;
  short getNonExtOpcode(const MachineInstr &MI) const;
  bool getPredReg(ArrayRef<MachineOperand> Cond, unsigned &PredReg,
                  unsigned &PredRegPos, unsigned &PredRegFlags) const;
  short getPseudoInstrPair(const MachineInstr &MI) const;
  short getRegForm(const MachineInstr &MI) const;
  unsigned getSize(const MachineInstr &MI) const;
  uint64_t getType(const MachineInstr &MI) const;
  unsigned getUnits(const MachineInstr &MI) const;

  MachineBasicBlock::instr_iterator expandVGatherPseudo(MachineInstr &MI) const;

  /// getInstrTimingClassLatency - Compute the instruction latency of a given
  /// instruction using Timing Class information, if available.
  unsigned nonDbgBBSize(const MachineBasicBlock *BB) const;
  unsigned nonDbgBundleSize(MachineBasicBlock::const_iterator BundleHead) const;

  void immediateExtend(MachineInstr &MI) const;
  bool invertAndChangeJumpTarget(MachineInstr &MI,
                                 MachineBasicBlock *NewTarget) const;
  void genAllInsnTimingClasses(MachineFunction &MF) const;
  bool reversePredSense(MachineInstr &MI) const;
  unsigned reversePrediction(unsigned Opcode) const;
  bool validateBranchCond(const ArrayRef<MachineOperand> &Cond) const;

  void setBundleNoShuf(MachineBasicBlock::instr_iterator MIB) const;
  bool getBundleNoShuf(const MachineInstr &MIB) const;
  // Addressing mode relations.
  short changeAddrMode_abs_io(short Opc) const;
  short changeAddrMode_io_abs(short Opc) const;
  short changeAddrMode_io_pi(short Opc) const;
  short changeAddrMode_io_rr(short Opc) const;
  short changeAddrMode_pi_io(short Opc) const;
  short changeAddrMode_rr_io(short Opc) const;
  short changeAddrMode_rr_ur(short Opc) const;
  short changeAddrMode_ur_rr(short Opc) const;

  short changeAddrMode_abs_io(const MachineInstr &MI) const {
    return changeAddrMode_abs_io(MI.getOpcode());
  }
  short changeAddrMode_io_abs(const MachineInstr &MI) const {
    return changeAddrMode_io_abs(MI.getOpcode());
  }
  short changeAddrMode_io_rr(const MachineInstr &MI) const {
    return changeAddrMode_io_rr(MI.getOpcode());
  }
  short changeAddrMode_rr_io(const MachineInstr &MI) const {
    return changeAddrMode_rr_io(MI.getOpcode());
  }
  short changeAddrMode_rr_ur(const MachineInstr &MI) const {
    return changeAddrMode_rr_ur(MI.getOpcode());
  }
  short changeAddrMode_ur_rr(const MachineInstr &MI) const {
    return changeAddrMode_ur_rr(MI.getOpcode());
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONINSTRINFO_H
