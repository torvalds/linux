//===-- ARMBaseInstrInfo.h - ARM Base Instruction Information ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Base ARM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMBASEINSTRINFO_H
#define LLVM_LIB_TARGET_ARM_ARMBASEINSTRINFO_H

#include "MCTargetDesc/ARMBaseInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <array>
#include <cstdint>

#define GET_INSTRINFO_HEADER
#include "ARMGenInstrInfo.inc"

namespace llvm {

class ARMBaseRegisterInfo;
class ARMSubtarget;

class ARMBaseInstrInfo : public ARMGenInstrInfo {
  const ARMSubtarget &Subtarget;

protected:
  // Can be only subclassed.
  explicit ARMBaseInstrInfo(const ARMSubtarget &STI);

  void expandLoadStackGuardBase(MachineBasicBlock::iterator MI,
                                unsigned LoadImmOpc, unsigned LoadOpc) const;

  /// Build the equivalent inputs of a REG_SEQUENCE for the given \p MI
  /// and \p DefIdx.
  /// \p [out] InputRegs of the equivalent REG_SEQUENCE. Each element of
  /// the list is modeled as <Reg:SubReg, SubIdx>.
  /// E.g., REG_SEQUENCE %1:sub1, sub0, %2, sub1 would produce
  /// two elements:
  /// - %1:sub1, sub0
  /// - %2<:0>, sub1
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isRegSequenceLike().
  bool getRegSequenceLikeInputs(
      const MachineInstr &MI, unsigned DefIdx,
      SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const override;

  /// Build the equivalent inputs of a EXTRACT_SUBREG for the given \p MI
  /// and \p DefIdx.
  /// \p [out] InputReg of the equivalent EXTRACT_SUBREG.
  /// E.g., EXTRACT_SUBREG %1:sub1, sub0, sub1 would produce:
  /// - %1:sub1, sub0
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isExtractSubregLike().
  bool getExtractSubregLikeInputs(const MachineInstr &MI, unsigned DefIdx,
                                  RegSubRegPairAndIdx &InputReg) const override;

  /// Build the equivalent inputs of a INSERT_SUBREG for the given \p MI
  /// and \p DefIdx.
  /// \p [out] BaseReg and \p [out] InsertedReg contain
  /// the equivalent inputs of INSERT_SUBREG.
  /// E.g., INSERT_SUBREG %0:sub0, %1:sub1, sub3 would produce:
  /// - BaseReg: %0:sub0
  /// - InsertedReg: %1:sub1, sub3
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isInsertSubregLike().
  bool
  getInsertSubregLikeInputs(const MachineInstr &MI, unsigned DefIdx,
                            RegSubRegPair &BaseReg,
                            RegSubRegPairAndIdx &InsertedReg) const override;

  /// Commutes the operands in the given instruction.
  /// The commutable operands are specified by their indices OpIdx1 and OpIdx2.
  ///
  /// Do not call this method for a non-commutable instruction or for
  /// non-commutable pair of operand indices OpIdx1 and OpIdx2.
  /// Even though the instruction is commutable, the method may still
  /// fail to commute the operands, null pointer is returned in such cases.
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;

  /// If the specific machine instruction is a instruction that moves/copies
  /// value from one register to another register return true along with
  /// @Source machine operand and @Destination machine operand.
  bool isCopyInstrImpl(const MachineInstr &MI, const MachineOperand *&Source,
                       const MachineOperand *&Destination) const override;

public:
  // Return whether the target has an explicit NOP encoding.
  bool hasNOP() const;

  // Return the non-pre/post incrementing version of 'Opc'. Return 0
  // if there is not such an opcode.
  virtual unsigned getUnindexedOpcode(unsigned Opc) const = 0;

  MachineInstr *convertToThreeAddress(MachineFunction::iterator &MFI,
                                      MachineInstr &MI,
                                      LiveVariables *LV) const override;

  virtual const ARMBaseRegisterInfo &getRegisterInfo() const = 0;
  const ARMSubtarget &getSubtarget() const { return Subtarget; }

  ScheduleHazardRecognizer *
  CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                               const ScheduleDAG *DAG) const override;

  ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                     const ScheduleDAG *DAG) const override;

  // Branch analysis.
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  // Predication support.
  bool isPredicated(const MachineInstr &MI) const override;

  ARMCC::CondCodes getPredicate(const MachineInstr &MI) const {
    int PIdx = MI.findFirstPredOperandIdx();
    return PIdx != -1 ? (ARMCC::CondCodes)MI.getOperand(PIdx).getImm()
                      : ARMCC::AL;
  }

  bool PredicateInstruction(MachineInstr &MI,
                            ArrayRef<MachineOperand> Pred) const override;

  bool SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                         ArrayRef<MachineOperand> Pred2) const override;

  bool DefinesPredicate(MachineInstr &MI,
                        std::vector<MachineOperand> &Pred) const override;

  bool isPredicable(const MachineInstr &MI) const override;

  // CPSR defined in instruction
  static bool isCPSRDefined(const MachineInstr &MI);
  bool isAddrMode3OpImm(const MachineInstr &MI, unsigned Op) const;
  bool isAddrMode3OpMinusReg(const MachineInstr &MI, unsigned Op) const;

  // Load, scaled register offset
  bool isLdstScaledReg(const MachineInstr &MI, unsigned Op) const;
  // Load, scaled register offset, not plus LSL2
  bool isLdstScaledRegNotPlusLsl2(const MachineInstr &MI, unsigned Op) const;
  // Minus reg for ldstso addr mode
  bool isLdstSoMinusReg(const MachineInstr &MI, unsigned Op) const;
  // Scaled register offset in address mode 2
  bool isAm2ScaledReg(const MachineInstr &MI, unsigned Op) const;
  // Load multiple, base reg in list
  bool isLDMBaseRegInList(const MachineInstr &MI) const;
  // get LDM variable defs size
  unsigned getLDMVariableDefsSize(const MachineInstr &MI) const;

  /// GetInstSize - Returns the size of the specified MachineInstr.
  ///
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  unsigned isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  unsigned isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  unsigned isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                     int &FrameIndex) const override;
  unsigned isStoreToStackSlotPostFE(const MachineInstr &MI,
                                    int &FrameIndex) const override;

  void copyToCPSR(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                  unsigned SrcReg, bool KillSrc,
                  const ARMSubtarget &Subtarget) const;
  void copyFromCPSR(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned DestReg, bool KillSrc,
                    const ARMSubtarget &Subtarget) const;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  bool shouldSink(const MachineInstr &MI) const override;

  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     unsigned DestReg, unsigned SubIdx,
                     const MachineInstr &Orig,
                     const TargetRegisterInfo &TRI) const override;

  MachineInstr &
  duplicate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
            const MachineInstr &Orig) const override;

  const MachineInstrBuilder &AddDReg(MachineInstrBuilder &MIB, unsigned Reg,
                                     unsigned SubIdx, unsigned State,
                                     const TargetRegisterInfo *TRI) const;

  bool produceSameValue(const MachineInstr &MI0, const MachineInstr &MI1,
                        const MachineRegisterInfo *MRI) const override;

  /// areLoadsFromSameBasePtr - This is used by the pre-regalloc scheduler to
  /// determine if two loads are loading from the same base address. It should
  /// only return true if the base pointers are the same and the only
  /// differences between the two addresses is the offset. It also returns the
  /// offsets by reference.
  bool areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2, int64_t &Offset1,
                               int64_t &Offset2) const override;

  /// shouldScheduleLoadsNear - This is a used by the pre-regalloc scheduler to
  /// determine (in conjunction with areLoadsFromSameBasePtr) if two loads
  /// should be scheduled togther. On some targets if two loads are loading from
  /// addresses in the same cache line, it's better if they are scheduled
  /// together. This function takes two integers that represent the load offsets
  /// from the common base address. It returns true if it decides it's desirable
  /// to schedule the two loads together. "NumLoads" is the number of loads that
  /// have already been scheduled after Load1.
  bool shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                               int64_t Offset1, int64_t Offset2,
                               unsigned NumLoads) const override;

  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  bool isProfitableToIfCvt(MachineBasicBlock &MBB,
                           unsigned NumCycles, unsigned ExtraPredCycles,
                           BranchProbability Probability) const override;

  bool isProfitableToIfCvt(MachineBasicBlock &TMBB, unsigned NumT,
                           unsigned ExtraT, MachineBasicBlock &FMBB,
                           unsigned NumF, unsigned ExtraF,
                           BranchProbability Probability) const override;

  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                                 BranchProbability Probability) const override {
    return NumCycles == 1;
  }

  bool isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                 MachineBasicBlock &FMBB) const override;

  /// analyzeCompare - For a comparison instruction, return the source registers
  /// in SrcReg and SrcReg2 if having two register operands, and the value it
  /// compares against in CmpValue. Return true if the comparison instruction
  /// can be analyzed.
  bool analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                      unsigned &SrcReg2, int &CmpMask,
                      int &CmpValue) const override;

  /// optimizeCompareInstr - Convert the instruction to set the zero flag so
  /// that we can remove a "comparison with zero"; Remove a redundant CMP
  /// instruction if the flags can be updated in the same way by an earlier
  /// instruction such as SUB.
  bool optimizeCompareInstr(MachineInstr &CmpInstr, unsigned SrcReg,
                            unsigned SrcReg2, int CmpMask, int CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  bool analyzeSelect(const MachineInstr &MI,
                     SmallVectorImpl<MachineOperand> &Cond, unsigned &TrueOp,
                     unsigned &FalseOp, bool &Optimizable) const override;

  MachineInstr *optimizeSelect(MachineInstr &MI,
                               SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                               bool) const override;

  /// FoldImmediate - 'Reg' is known to be defined by a move immediate
  /// instruction, try to fold the immediate into the use instruction.
  bool FoldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, unsigned Reg,
                     MachineRegisterInfo *MRI) const override;

  unsigned getNumMicroOps(const InstrItineraryData *ItinData,
                          const MachineInstr &MI) const override;

  int getOperandLatency(const InstrItineraryData *ItinData,
                        const MachineInstr &DefMI, unsigned DefIdx,
                        const MachineInstr &UseMI,
                        unsigned UseIdx) const override;
  int getOperandLatency(const InstrItineraryData *ItinData,
                        SDNode *DefNode, unsigned DefIdx,
                        SDNode *UseNode, unsigned UseIdx) const override;

  /// VFP/NEON execution domains.
  std::pair<uint16_t, uint16_t>
  getExecutionDomain(const MachineInstr &MI) const override;
  void setExecutionDomain(MachineInstr &MI, unsigned Domain) const override;

  unsigned
  getPartialRegUpdateClearance(const MachineInstr &, unsigned,
                               const TargetRegisterInfo *) const override;
  void breakPartialRegDependency(MachineInstr &, unsigned,
                                 const TargetRegisterInfo *TRI) const override;

  /// Get the number of addresses by LDM or VLDM or zero for unknown.
  unsigned getNumLDMAddresses(const MachineInstr &MI) const;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableBitmaskMachineOperandTargetFlags() const override;

private:
  unsigned getInstBundleLength(const MachineInstr &MI) const;

  int getVLDMDefCycle(const InstrItineraryData *ItinData,
                      const MCInstrDesc &DefMCID,
                      unsigned DefClass,
                      unsigned DefIdx, unsigned DefAlign) const;
  int getLDMDefCycle(const InstrItineraryData *ItinData,
                     const MCInstrDesc &DefMCID,
                     unsigned DefClass,
                     unsigned DefIdx, unsigned DefAlign) const;
  int getVSTMUseCycle(const InstrItineraryData *ItinData,
                      const MCInstrDesc &UseMCID,
                      unsigned UseClass,
                      unsigned UseIdx, unsigned UseAlign) const;
  int getSTMUseCycle(const InstrItineraryData *ItinData,
                     const MCInstrDesc &UseMCID,
                     unsigned UseClass,
                     unsigned UseIdx, unsigned UseAlign) const;
  int getOperandLatency(const InstrItineraryData *ItinData,
                        const MCInstrDesc &DefMCID,
                        unsigned DefIdx, unsigned DefAlign,
                        const MCInstrDesc &UseMCID,
                        unsigned UseIdx, unsigned UseAlign) const;

  int getOperandLatencyImpl(const InstrItineraryData *ItinData,
                            const MachineInstr &DefMI, unsigned DefIdx,
                            const MCInstrDesc &DefMCID, unsigned DefAdj,
                            const MachineOperand &DefMO, unsigned Reg,
                            const MachineInstr &UseMI, unsigned UseIdx,
                            const MCInstrDesc &UseMCID, unsigned UseAdj) const;

  unsigned getPredicationCost(const MachineInstr &MI) const override;

  unsigned getInstrLatency(const InstrItineraryData *ItinData,
                           const MachineInstr &MI,
                           unsigned *PredCost = nullptr) const override;

  int getInstrLatency(const InstrItineraryData *ItinData,
                      SDNode *Node) const override;

  bool hasHighOperandLatency(const TargetSchedModel &SchedModel,
                             const MachineRegisterInfo *MRI,
                             const MachineInstr &DefMI, unsigned DefIdx,
                             const MachineInstr &UseMI,
                             unsigned UseIdx) const override;
  bool hasLowDefLatency(const TargetSchedModel &SchedModel,
                        const MachineInstr &DefMI,
                        unsigned DefIdx) const override;

  /// verifyInstruction - Perform target specific instruction verification.
  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;

  virtual void expandLoadStackGuard(MachineBasicBlock::iterator MI) const = 0;

  void expandMEMCPY(MachineBasicBlock::iterator) const;

private:
  /// Modeling special VFP / NEON fp MLA / MLS hazards.

  /// MLxEntryMap - Map fp MLA / MLS to the corresponding entry in the internal
  /// MLx table.
  DenseMap<unsigned, unsigned> MLxEntryMap;

  /// MLxHazardOpcodes - Set of add / sub and multiply opcodes that would cause
  /// stalls when scheduled together with fp MLA / MLS opcodes.
  SmallSet<unsigned, 16> MLxHazardOpcodes;

public:
  /// isFpMLxInstruction - Return true if the specified opcode is a fp MLA / MLS
  /// instruction.
  bool isFpMLxInstruction(unsigned Opcode) const {
    return MLxEntryMap.count(Opcode);
  }

  /// isFpMLxInstruction - This version also returns the multiply opcode and the
  /// addition / subtraction opcode to expand to. Return true for 'HasLane' for
  /// the MLX instructions with an extra lane operand.
  bool isFpMLxInstruction(unsigned Opcode, unsigned &MulOpc,
                          unsigned &AddSubOpc, bool &NegAcc,
                          bool &HasLane) const;

  /// canCauseFpMLxStall - Return true if an instruction of the specified opcode
  /// will cause stalls when scheduled after (within 4-cycle window) a fp
  /// MLA / MLS instruction.
  bool canCauseFpMLxStall(unsigned Opcode) const {
    return MLxHazardOpcodes.count(Opcode);
  }

  /// Returns true if the instruction has a shift by immediate that can be
  /// executed in one cycle less.
  bool isSwiftFastImmShift(const MachineInstr *MI) const;

  /// Returns predicate register associated with the given frame instruction.
  unsigned getFramePred(const MachineInstr &MI) const {
    assert(isFrameInstr(MI));
    // Operands of ADJCALLSTACKDOWN/ADJCALLSTACKUP:
    // - argument declared in the pattern:
    // 0 - frame size
    // 1 - arg of CALLSEQ_START/CALLSEQ_END
    // 2 - predicate code (like ARMCC::AL)
    // - added by predOps:
    // 3 - predicate reg
    return MI.getOperand(3).getReg();
  }
};

/// Get the operands corresponding to the given \p Pred value. By default, the
/// predicate register is assumed to be 0 (no register), but you can pass in a
/// \p PredReg if that is not the case.
static inline std::array<MachineOperand, 2> predOps(ARMCC::CondCodes Pred,
                                                    unsigned PredReg = 0) {
  return {{MachineOperand::CreateImm(static_cast<int64_t>(Pred)),
           MachineOperand::CreateReg(PredReg, false)}};
}

/// Get the operand corresponding to the conditional code result. By default,
/// this is 0 (no register).
static inline MachineOperand condCodeOp(unsigned CCReg = 0) {
  return MachineOperand::CreateReg(CCReg, false);
}

/// Get the operand corresponding to the conditional code result for Thumb1.
/// This operand will always refer to CPSR and it will have the Define flag set.
/// You can optionally set the Dead flag by means of \p isDead.
static inline MachineOperand t1CondCodeOp(bool isDead = false) {
  return MachineOperand::CreateReg(ARM::CPSR,
                                   /*Define*/ true, /*Implicit*/ false,
                                   /*Kill*/ false, isDead);
}

static inline
bool isUncondBranchOpcode(int Opc) {
  return Opc == ARM::B || Opc == ARM::tB || Opc == ARM::t2B;
}

static inline
bool isCondBranchOpcode(int Opc) {
  return Opc == ARM::Bcc || Opc == ARM::tBcc || Opc == ARM::t2Bcc;
}

static inline bool isJumpTableBranchOpcode(int Opc) {
  return Opc == ARM::BR_JTr || Opc == ARM::BR_JTm_i12 ||
         Opc == ARM::BR_JTm_rs || Opc == ARM::BR_JTadd || Opc == ARM::tBR_JTr ||
         Opc == ARM::t2BR_JT;
}

static inline
bool isIndirectBranchOpcode(int Opc) {
  return Opc == ARM::BX || Opc == ARM::MOVPCRX || Opc == ARM::tBRIND;
}

static inline bool isPopOpcode(int Opc) {
  return Opc == ARM::tPOP_RET || Opc == ARM::LDMIA_RET ||
         Opc == ARM::t2LDMIA_RET || Opc == ARM::tPOP || Opc == ARM::LDMIA_UPD ||
         Opc == ARM::t2LDMIA_UPD || Opc == ARM::VLDMDIA_UPD;
}

static inline bool isPushOpcode(int Opc) {
  return Opc == ARM::tPUSH || Opc == ARM::t2STMDB_UPD ||
         Opc == ARM::STMDB_UPD || Opc == ARM::VSTMDDB_UPD;
}

/// getInstrPredicate - If instruction is predicated, returns its predicate
/// condition, otherwise returns AL. It also returns the condition code
/// register by reference.
ARMCC::CondCodes getInstrPredicate(const MachineInstr &MI, unsigned &PredReg);

unsigned getMatchingCondBranchOpcode(unsigned Opc);

/// Determine if MI can be folded into an ARM MOVCC instruction, and return the
/// opcode of the SSA instruction representing the conditional MI.
unsigned canFoldARMInstrIntoMOVCC(unsigned Reg,
                                  MachineInstr *&MI,
                                  const MachineRegisterInfo &MRI);

/// Map pseudo instructions that imply an 'S' bit onto real opcodes. Whether
/// the instruction is encoded with an 'S' bit is determined by the optional
/// CPSR def operand.
unsigned convertAddSubFlagsOpcode(unsigned OldOpc);

/// emitARMRegPlusImmediate / emitT2RegPlusImmediate - Emits a series of
/// instructions to materializea destreg = basereg + immediate in ARM / Thumb2
/// code.
void emitARMRegPlusImmediate(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MBBI,
                             const DebugLoc &dl, unsigned DestReg,
                             unsigned BaseReg, int NumBytes,
                             ARMCC::CondCodes Pred, unsigned PredReg,
                             const ARMBaseInstrInfo &TII, unsigned MIFlags = 0);

void emitT2RegPlusImmediate(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator &MBBI,
                            const DebugLoc &dl, unsigned DestReg,
                            unsigned BaseReg, int NumBytes,
                            ARMCC::CondCodes Pred, unsigned PredReg,
                            const ARMBaseInstrInfo &TII, unsigned MIFlags = 0);
void emitThumbRegPlusImmediate(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator &MBBI,
                               const DebugLoc &dl, unsigned DestReg,
                               unsigned BaseReg, int NumBytes,
                               const TargetInstrInfo &TII,
                               const ARMBaseRegisterInfo &MRI,
                               unsigned MIFlags = 0);

/// Tries to add registers to the reglist of a given base-updating
/// push/pop instruction to adjust the stack by an additional
/// NumBytes. This can save a few bytes per function in code-size, but
/// obviously generates more memory traffic. As such, it only takes
/// effect in functions being optimised for size.
bool tryFoldSPUpdateIntoPushPop(const ARMSubtarget &Subtarget,
                                MachineFunction &MF, MachineInstr *MI,
                                unsigned NumBytes);

/// rewriteARMFrameIndex / rewriteT2FrameIndex -
/// Rewrite MI to access 'Offset' bytes from the FP. Return false if the
/// offset could not be handled directly in MI, and return the left-over
/// portion by reference.
bool rewriteARMFrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                          unsigned FrameReg, int &Offset,
                          const ARMBaseInstrInfo &TII);

bool rewriteT2FrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                         unsigned FrameReg, int &Offset,
                         const ARMBaseInstrInfo &TII);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMBASEINSTRINFO_H
