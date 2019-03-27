//===-- X86InstrInfo.h - X86 Instruction Information ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86INSTRINFO_H
#define LLVM_LIB_TARGET_X86_X86INSTRINFO_H

#include "MCTargetDesc/X86BaseInfo.h"
#include "X86InstrFMA3Info.h"
#include "X86RegisterInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <vector>

#define GET_INSTRINFO_HEADER
#include "X86GenInstrInfo.inc"

namespace llvm {
class MachineInstrBuilder;
class X86RegisterInfo;
class X86Subtarget;

namespace X86 {

enum AsmComments {
  // For instr that was compressed from EVEX to VEX.
  AC_EVEX_2_VEX = MachineInstr::TAsmComments
};

// X86 specific condition code. These correspond to X86_*_COND in
// X86InstrInfo.td. They must be kept in synch.
enum CondCode {
  COND_A = 0,
  COND_AE = 1,
  COND_B = 2,
  COND_BE = 3,
  COND_E = 4,
  COND_G = 5,
  COND_GE = 6,
  COND_L = 7,
  COND_LE = 8,
  COND_NE = 9,
  COND_NO = 10,
  COND_NP = 11,
  COND_NS = 12,
  COND_O = 13,
  COND_P = 14,
  COND_S = 15,
  LAST_VALID_COND = COND_S,

  // Artificial condition codes. These are used by AnalyzeBranch
  // to indicate a block terminated with two conditional branches that together
  // form a compound condition. They occur in code using FCMP_OEQ or FCMP_UNE,
  // which can't be represented on x86 with a single condition. These
  // are never used in MachineInstrs and are inverses of one another.
  COND_NE_OR_P,
  COND_E_AND_NP,

  COND_INVALID
};

// Turn condition code into conditional branch opcode.
unsigned GetCondBranchFromCond(CondCode CC);

/// Return a pair of condition code for the given predicate and whether
/// the instruction operands should be swaped to match the condition code.
std::pair<CondCode, bool> getX86ConditionCode(CmpInst::Predicate Predicate);

/// Return a set opcode for the given condition and whether it has
/// a memory operand.
unsigned getSETFromCond(CondCode CC, bool HasMemoryOperand = false);

/// Return a cmov opcode for the given condition, register size in
/// bytes, and operand type.
unsigned getCMovFromCond(CondCode CC, unsigned RegBytes,
                         bool HasMemoryOperand = false);

// Turn jCC opcode into condition code.
CondCode getCondFromBranchOpc(unsigned Opc);

// Turn setCC opcode into condition code.
CondCode getCondFromSETOpc(unsigned Opc);

// Turn CMov opcode into condition code.
CondCode getCondFromCMovOpc(unsigned Opc);

/// GetOppositeBranchCondition - Return the inverse of the specified cond,
/// e.g. turning COND_E to COND_NE.
CondCode GetOppositeBranchCondition(CondCode CC);

/// Get the VPCMP immediate for the given condition.
unsigned getVPCMPImmForCond(ISD::CondCode CC);

/// Get the VPCMP immediate if the opcodes are swapped.
unsigned getSwappedVPCMPImm(unsigned Imm);

/// Get the VPCOM immediate if the opcodes are swapped.
unsigned getSwappedVPCOMImm(unsigned Imm);

} // namespace X86

/// isGlobalStubReference - Return true if the specified TargetFlag operand is
/// a reference to a stub for a global, not the global itself.
inline static bool isGlobalStubReference(unsigned char TargetFlag) {
  switch (TargetFlag) {
  case X86II::MO_DLLIMPORT:               // dllimport stub.
  case X86II::MO_GOTPCREL:                // rip-relative GOT reference.
  case X86II::MO_GOT:                     // normal GOT reference.
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE: // Normal $non_lazy_ptr ref.
  case X86II::MO_DARWIN_NONLAZY:          // Normal $non_lazy_ptr ref.
  case X86II::MO_COFFSTUB:                // COFF .refptr stub.
    return true;
  default:
    return false;
  }
}

/// isGlobalRelativeToPICBase - Return true if the specified global value
/// reference is relative to a 32-bit PIC base (X86ISD::GlobalBaseReg).  If this
/// is true, the addressing mode has the PIC base register added in (e.g. EBX).
inline static bool isGlobalRelativeToPICBase(unsigned char TargetFlag) {
  switch (TargetFlag) {
  case X86II::MO_GOTOFF:                  // isPICStyleGOT: local global.
  case X86II::MO_GOT:                     // isPICStyleGOT: other global.
  case X86II::MO_PIC_BASE_OFFSET:         // Darwin local global.
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE: // Darwin/32 external global.
  case X86II::MO_TLVP:                    // ??? Pretty sure..
    return true;
  default:
    return false;
  }
}

inline static bool isScale(const MachineOperand &MO) {
  return MO.isImm() && (MO.getImm() == 1 || MO.getImm() == 2 ||
                        MO.getImm() == 4 || MO.getImm() == 8);
}

inline static bool isLeaMem(const MachineInstr &MI, unsigned Op) {
  if (MI.getOperand(Op).isFI())
    return true;
  return Op + X86::AddrSegmentReg <= MI.getNumOperands() &&
         MI.getOperand(Op + X86::AddrBaseReg).isReg() &&
         isScale(MI.getOperand(Op + X86::AddrScaleAmt)) &&
         MI.getOperand(Op + X86::AddrIndexReg).isReg() &&
         (MI.getOperand(Op + X86::AddrDisp).isImm() ||
          MI.getOperand(Op + X86::AddrDisp).isGlobal() ||
          MI.getOperand(Op + X86::AddrDisp).isCPI() ||
          MI.getOperand(Op + X86::AddrDisp).isJTI());
}

inline static bool isMem(const MachineInstr &MI, unsigned Op) {
  if (MI.getOperand(Op).isFI())
    return true;
  return Op + X86::AddrNumOperands <= MI.getNumOperands() &&
         MI.getOperand(Op + X86::AddrSegmentReg).isReg() && isLeaMem(MI, Op);
}

class X86InstrInfo final : public X86GenInstrInfo {
  X86Subtarget &Subtarget;
  const X86RegisterInfo RI;

  virtual void anchor();

  bool AnalyzeBranchImpl(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                         MachineBasicBlock *&FBB,
                         SmallVectorImpl<MachineOperand> &Cond,
                         SmallVectorImpl<MachineInstr *> &CondBranches,
                         bool AllowModify) const;

public:
  explicit X86InstrInfo(X86Subtarget &STI);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const X86RegisterInfo &getRegisterInfo() const { return RI; }

  /// Returns the stack pointer adjustment that happens inside the frame
  /// setup..destroy sequence (e.g. by pushes, or inside the callee).
  int64_t getFrameAdjustment(const MachineInstr &I) const {
    assert(isFrameInstr(I));
    if (isFrameSetup(I))
      return I.getOperand(2).getImm();
    return I.getOperand(1).getImm();
  }

  /// Sets the stack pointer adjustment made inside the frame made up by this
  /// instruction.
  void setFrameAdjustment(MachineInstr &I, int64_t V) const {
    assert(isFrameInstr(I));
    if (isFrameSetup(I))
      I.getOperand(2).setImm(V);
    else
      I.getOperand(1).setImm(V);
  }

  /// getSPAdjust - This returns the stack pointer adjustment made by
  /// this instruction. For x86, we need to handle more complex call
  /// sequences involving PUSHes.
  int getSPAdjust(const MachineInstr &MI) const override;

  /// isCoalescableExtInstr - Return true if the instruction is a "coalescable"
  /// extension instruction. That is, it's like a copy where it's legal for the
  /// source to overlap the destination. e.g. X86::MOVSX64rr32. If this returns
  /// true, then it's expected the pre-extension value is available as a subreg
  /// of the result register. This also returns the sub-register index in
  /// SubIdx.
  bool isCoalescableExtInstr(const MachineInstr &MI, unsigned &SrcReg,
                             unsigned &DstReg, unsigned &SubIdx) const override;

  unsigned isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  unsigned isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex,
                               unsigned &MemBytes) const override;
  /// isLoadFromStackSlotPostFE - Check for post-frame ptr elimination
  /// stack locations as well.  This uses a heuristic so it isn't
  /// reliable for correctness.
  unsigned isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                     int &FrameIndex) const override;

  unsigned isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  unsigned isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex,
                              unsigned &MemBytes) const override;
  /// isStoreToStackSlotPostFE - Check for post-frame ptr elimination
  /// stack locations as well.  This uses a heuristic so it isn't
  /// reliable for correctness.
  unsigned isStoreToStackSlotPostFE(const MachineInstr &MI,
                                    int &FrameIndex) const override;

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                         AliasAnalysis *AA) const override;
  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     unsigned DestReg, unsigned SubIdx,
                     const MachineInstr &Orig,
                     const TargetRegisterInfo &TRI) const override;

  /// Given an operand within a MachineInstr, insert preceding code to put it
  /// into the right format for a particular kind of LEA instruction. This may
  /// involve using an appropriate super-register instead (with an implicit use
  /// of the original) or creating a new virtual register and inserting COPY
  /// instructions to get the data into the right class.
  ///
  /// Reference parameters are set to indicate how caller should add this
  /// operand to the LEA instruction.
  bool classifyLEAReg(MachineInstr &MI, const MachineOperand &Src,
                      unsigned LEAOpcode, bool AllowSP, unsigned &NewSrc,
                      bool &isKill, MachineOperand &ImplicitOp,
                      LiveVariables *LV) const;

  /// convertToThreeAddress - This method must be implemented by targets that
  /// set the M_CONVERTIBLE_TO_3_ADDR flag.  When this flag is set, the target
  /// may be able to convert a two-address instruction into a true
  /// three-address instruction on demand.  This allows the X86 target (for
  /// example) to convert ADD and SHL instructions into LEA instructions if they
  /// would require register copies due to two-addressness.
  ///
  /// This method returns a null pointer if the transformation cannot be
  /// performed, otherwise it returns the new instruction.
  ///
  MachineInstr *convertToThreeAddress(MachineFunction::iterator &MFI,
                                      MachineInstr &MI,
                                      LiveVariables *LV) const override;

  /// Returns true iff the routine could find two commutable operands in the
  /// given machine instruction.
  /// The 'SrcOpIdx1' and 'SrcOpIdx2' are INPUT and OUTPUT arguments. Their
  /// input values can be re-defined in this method only if the input values
  /// are not pre-defined, which is designated by the special value
  /// 'CommuteAnyOperandIndex' assigned to it.
  /// If both of indices are pre-defined and refer to some operands, then the
  /// method simply returns true if the corresponding operands are commutable
  /// and returns false otherwise.
  ///
  /// For example, calling this method this way:
  ///     unsigned Op1 = 1, Op2 = CommuteAnyOperandIndex;
  ///     findCommutedOpIndices(MI, Op1, Op2);
  /// can be interpreted as a query asking to find an operand that would be
  /// commutable with the operand#1.
  bool findCommutedOpIndices(MachineInstr &MI, unsigned &SrcOpIdx1,
                             unsigned &SrcOpIdx2) const override;

  /// Returns an adjusted FMA opcode that must be used in FMA instruction that
  /// performs the same computations as the given \p MI but which has the
  /// operands \p SrcOpIdx1 and \p SrcOpIdx2 commuted.
  /// It may return 0 if it is unsafe to commute the operands.
  /// Note that a machine instruction (instead of its opcode) is passed as the
  /// first parameter to make it possible to analyze the instruction's uses and
  /// commute the first operand of FMA even when it seems unsafe when you look
  /// at the opcode. For example, it is Ok to commute the first operand of
  /// VFMADD*SD_Int, if ONLY the lowest 64-bit element of the result is used.
  ///
  /// The returned FMA opcode may differ from the opcode in the given \p MI.
  /// For example, commuting the operands #1 and #3 in the following FMA
  ///     FMA213 #1, #2, #3
  /// results into instruction with adjusted opcode:
  ///     FMA231 #3, #2, #1
  unsigned
  getFMA3OpcodeToCommuteOperands(const MachineInstr &MI, unsigned SrcOpIdx1,
                                 unsigned SrcOpIdx2,
                                 const X86InstrFMA3Group &FMA3Group) const;

  // Branch analysis.
  bool isUnpredicatedTerminator(const MachineInstr &MI) const override;
  bool isUnconditionalTailCall(const MachineInstr &MI) const override;
  bool canMakeTailCallConditional(SmallVectorImpl<MachineOperand> &Cond,
                                  const MachineInstr &TailCall) const override;
  void replaceBranchWithTailCall(MachineBasicBlock &MBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 const MachineInstr &TailCall) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  bool getMemOperandWithOffset(MachineInstr &LdSt, MachineOperand *&BaseOp,
                               int64_t &Offset,
                               const TargetRegisterInfo *TRI) const override;
  bool analyzeBranchPredicate(MachineBasicBlock &MBB,
                              TargetInstrInfo::MachineBranchPredicate &MBP,
                              bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool canInsertSelect(const MachineBasicBlock &, ArrayRef<MachineOperand> Cond,
                       unsigned, unsigned, int &, int &, int &) const override;
  void insertSelect(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                    const DebugLoc &DL, unsigned DstReg,
                    ArrayRef<MachineOperand> Cond, unsigned TrueReg,
                    unsigned FalseReg) const override;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, unsigned SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;

  void storeRegToAddr(MachineFunction &MF, unsigned SrcReg, bool isKill,
                      SmallVectorImpl<MachineOperand> &Addr,
                      const TargetRegisterClass *RC,
                      ArrayRef<MachineMemOperand *> MMOs,
                      SmallVectorImpl<MachineInstr *> &NewMIs) const;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, unsigned DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;

  void loadRegFromAddr(MachineFunction &MF, unsigned DestReg,
                       SmallVectorImpl<MachineOperand> &Addr,
                       const TargetRegisterClass *RC,
                       ArrayRef<MachineMemOperand *> MMOs,
                       SmallVectorImpl<MachineInstr *> &NewMIs) const;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  /// Check whether the target can fold a load that feeds a subreg operand
  /// (or a subreg operand that feeds a store).
  bool isSubregFoldable() const override { return true; }

  /// foldMemoryOperand - If this target supports it, fold a load or store of
  /// the specified stack slot into the specified machine instruction for the
  /// specified operand(s).  If this is possible, the target should perform the
  /// folding and return true, otherwise it should return false.  If it folds
  /// the instruction, it is likely that the MachineInstruction the iterator
  /// references has been changed.
  MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt, int FrameIndex,
                        LiveIntervals *LIS = nullptr) const override;

  /// foldMemoryOperand - Same as the previous version except it allows folding
  /// of any load and store from / to any address, not just from a specific
  /// stack slot.
  MachineInstr *foldMemoryOperandImpl(
      MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
      MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
      LiveIntervals *LIS = nullptr) const override;

  /// unfoldMemoryOperand - Separate a single instruction which folded a load or
  /// a store or a load and a store into two or more instruction. If this is
  /// possible, returns true as well as the new instructions by reference.
  bool
  unfoldMemoryOperand(MachineFunction &MF, MachineInstr &MI, unsigned Reg,
                      bool UnfoldLoad, bool UnfoldStore,
                      SmallVectorImpl<MachineInstr *> &NewMIs) const override;

  bool unfoldMemoryOperand(SelectionDAG &DAG, SDNode *N,
                           SmallVectorImpl<SDNode *> &NewNodes) const override;

  /// getOpcodeAfterMemoryUnfold - Returns the opcode of the would be new
  /// instruction after load / store are unfolded from an instruction of the
  /// specified opcode. It returns zero if the specified unfolding is not
  /// possible. If LoadRegIndex is non-null, it is filled in with the operand
  /// index of the operand which will hold the register holding the loaded
  /// value.
  unsigned
  getOpcodeAfterMemoryUnfold(unsigned Opc, bool UnfoldLoad, bool UnfoldStore,
                             unsigned *LoadRegIndex = nullptr) const override;

  /// areLoadsFromSameBasePtr - This is used by the pre-regalloc scheduler
  /// to determine if two loads are loading from the same base address. It
  /// should only return true if the base pointers are the same and the
  /// only differences between the two addresses are the offset. It also returns
  /// the offsets by reference.
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
  bool shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2, int64_t Offset1,
                               int64_t Offset2,
                               unsigned NumLoads) const override;

  void getNoop(MCInst &NopInst) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  /// isSafeToMoveRegClassDefs - Return true if it's safe to move a machine
  /// instruction that defines the specified register class.
  bool isSafeToMoveRegClassDefs(const TargetRegisterClass *RC) const override;

  /// isSafeToClobberEFLAGS - Return true if it's safe insert an instruction tha
  /// would clobber the EFLAGS condition register. Note the result may be
  /// conservative. If it cannot definitely determine the safety after visiting
  /// a few instructions in each direction it assumes it's not safe.
  bool isSafeToClobberEFLAGS(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I) const;

  /// True if MI has a condition code def, e.g. EFLAGS, that is
  /// not marked dead.
  bool hasLiveCondCodeDef(MachineInstr &MI) const;

  /// getGlobalBaseReg - Return a virtual register initialized with the
  /// the global base register value. Output instructions required to
  /// initialize the register in the function entry block, if necessary.
  ///
  unsigned getGlobalBaseReg(MachineFunction *MF) const;

  std::pair<uint16_t, uint16_t>
  getExecutionDomain(const MachineInstr &MI) const override;

  uint16_t getExecutionDomainCustom(const MachineInstr &MI) const;

  void setExecutionDomain(MachineInstr &MI, unsigned Domain) const override;

  bool setExecutionDomainCustom(MachineInstr &MI, unsigned Domain) const;

  unsigned
  getPartialRegUpdateClearance(const MachineInstr &MI, unsigned OpNum,
                               const TargetRegisterInfo *TRI) const override;
  unsigned getUndefRegClearance(const MachineInstr &MI, unsigned &OpNum,
                                const TargetRegisterInfo *TRI) const override;
  void breakPartialRegDependency(MachineInstr &MI, unsigned OpNum,
                                 const TargetRegisterInfo *TRI) const override;

  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                      unsigned OpNum,
                                      ArrayRef<MachineOperand> MOs,
                                      MachineBasicBlock::iterator InsertPt,
                                      unsigned Size, unsigned Alignment,
                                      bool AllowCommute) const;

  bool isHighLatencyDef(int opc) const override;

  bool hasHighOperandLatency(const TargetSchedModel &SchedModel,
                             const MachineRegisterInfo *MRI,
                             const MachineInstr &DefMI, unsigned DefIdx,
                             const MachineInstr &UseMI,
                             unsigned UseIdx) const override;

  bool useMachineCombiner() const override { return true; }

  bool isAssociativeAndCommutative(const MachineInstr &Inst) const override;

  bool hasReassociableOperands(const MachineInstr &Inst,
                               const MachineBasicBlock *MBB) const override;

  void setSpecialOperandAttr(MachineInstr &OldMI1, MachineInstr &OldMI2,
                             MachineInstr &NewMI1,
                             MachineInstr &NewMI2) const override;

  /// analyzeCompare - For a comparison instruction, return the source registers
  /// in SrcReg and SrcReg2 if having two register operands, and the value it
  /// compares against in CmpValue. Return true if the comparison instruction
  /// can be analyzed.
  bool analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                      unsigned &SrcReg2, int &CmpMask,
                      int &CmpValue) const override;

  /// optimizeCompareInstr - Check if there exists an earlier instruction that
  /// operates on the same source operands and sets flags in the same way as
  /// Compare; remove Compare if possible.
  bool optimizeCompareInstr(MachineInstr &CmpInstr, unsigned SrcReg,
                            unsigned SrcReg2, int CmpMask, int CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  /// optimizeLoadInstr - Try to remove the load by folding it to a register
  /// operand at the use. We fold the load instructions if and only if the
  /// def and use are in the same BB. We only look at one load and see
  /// whether it can be folded into MI. FoldAsLoadDefReg is the virtual register
  /// defined by the load we are trying to fold. DefMI returns the machine
  /// instruction that defines FoldAsLoadDefReg, and the function returns
  /// the machine instruction generated due to folding.
  MachineInstr *optimizeLoadInstr(MachineInstr &MI,
                                  const MachineRegisterInfo *MRI,
                                  unsigned &FoldAsLoadDefReg,
                                  MachineInstr *&DefMI) const override;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  virtual outliner::OutlinedFunction getOutliningCandidateInfo(
      std::vector<outliner::Candidate> &RepeatedSequenceLocs) const override;

  bool isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                   bool OutlineFromLinkOnceODRs) const override;

  outliner::InstrType
  getOutliningType(MachineBasicBlock::iterator &MIT, unsigned Flags) const override;

  void buildOutlinedFrame(MachineBasicBlock &MBB, MachineFunction &MF,
                          const outliner::OutlinedFunction &OF) const override;

  MachineBasicBlock::iterator
  insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &It, MachineFunction &MF,
                     const outliner::Candidate &C) const override;

#define GET_INSTRINFO_HELPER_DECLS
#include "X86GenInstrInfo.inc"

protected:
  /// Commutes the operands in the given instruction by changing the operands
  /// order and/or changing the instruction's opcode and/or the immediate value
  /// operand.
  ///
  /// The arguments 'CommuteOpIdx1' and 'CommuteOpIdx2' specify the operands
  /// to be commuted.
  ///
  /// Do not call this method for a non-commutable instruction or
  /// non-commutable operands.
  /// Even though the instruction is commutable, the method may still
  /// fail to commute the operands, null pointer is returned in such cases.
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned CommuteOpIdx1,
                                       unsigned CommuteOpIdx2) const override;

  /// If the specific machine instruction is a instruction that moves/copies
  /// value from one register to another register return true along with
  /// @Source machine operand and @Destination machine operand.
  bool isCopyInstrImpl(const MachineInstr &MI, const MachineOperand *&Source,
                       const MachineOperand *&Destination) const override;

private:
  /// This is a helper for convertToThreeAddress for 8 and 16-bit instructions.
  /// We use 32-bit LEA to form 3-address code by promoting to a 32-bit
  /// super-register and then truncating back down to a 8/16-bit sub-register.
  MachineInstr *convertToThreeAddressWithLEA(unsigned MIOpc,
                                             MachineFunction::iterator &MFI,
                                             MachineInstr &MI,
                                             LiveVariables *LV) const;

  /// Handles memory folding for special case instructions, for instance those
  /// requiring custom manipulation of the address.
  MachineInstr *foldMemoryOperandCustom(MachineFunction &MF, MachineInstr &MI,
                                        unsigned OpNum,
                                        ArrayRef<MachineOperand> MOs,
                                        MachineBasicBlock::iterator InsertPt,
                                        unsigned Size, unsigned Align) const;

  /// isFrameOperand - Return true and the FrameIndex if the specified
  /// operand and follow operands form a reference to the stack frame.
  bool isFrameOperand(const MachineInstr &MI, unsigned int Op,
                      int &FrameIndex) const;

  /// Returns true iff the routine could find two commutable operands in the
  /// given machine instruction with 3 vector inputs.
  /// The 'SrcOpIdx1' and 'SrcOpIdx2' are INPUT and OUTPUT arguments. Their
  /// input values can be re-defined in this method only if the input values
  /// are not pre-defined, which is designated by the special value
  /// 'CommuteAnyOperandIndex' assigned to it.
  /// If both of indices are pre-defined and refer to some operands, then the
  /// method simply returns true if the corresponding operands are commutable
  /// and returns false otherwise.
  ///
  /// For example, calling this method this way:
  ///     unsigned Op1 = 1, Op2 = CommuteAnyOperandIndex;
  ///     findThreeSrcCommutedOpIndices(MI, Op1, Op2);
  /// can be interpreted as a query asking to find an operand that would be
  /// commutable with the operand#1.
  ///
  /// If IsIntrinsic is set, operand 1 will be ignored for commuting.
  bool findThreeSrcCommutedOpIndices(const MachineInstr &MI,
                                     unsigned &SrcOpIdx1,
                                     unsigned &SrcOpIdx2,
                                     bool IsIntrinsic = false) const;
};

} // namespace llvm

#endif
