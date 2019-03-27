//===-- R600InstrInfo.h - R600 Instruction Info Interface -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition for R600InstrInfo
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600INSTRINFO_H
#define LLVM_LIB_TARGET_AMDGPU_R600INSTRINFO_H

#include "R600RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "R600GenInstrInfo.inc"

namespace llvm {

namespace R600InstrFlags {
enum : uint64_t {
 REGISTER_STORE = UINT64_C(1) << 62,
 REGISTER_LOAD = UINT64_C(1) << 63
};
}

class AMDGPUTargetMachine;
class DFAPacketizer;
class MachineFunction;
class MachineInstr;
class MachineInstrBuilder;
class R600Subtarget;

class R600InstrInfo final : public R600GenInstrInfo {
private:
  const R600RegisterInfo RI;
  const R600Subtarget &ST;

  std::vector<std::pair<int, unsigned>>
  ExtractSrcs(MachineInstr &MI, const DenseMap<unsigned, unsigned> &PV,
              unsigned &ConstCount) const;

  MachineInstrBuilder buildIndirectRead(MachineBasicBlock *MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned ValueReg, unsigned Address,
                                        unsigned OffsetReg,
                                        unsigned AddrChan) const;

  MachineInstrBuilder buildIndirectWrite(MachineBasicBlock *MBB,
                                         MachineBasicBlock::iterator I,
                                         unsigned ValueReg, unsigned Address,
                                         unsigned OffsetReg,
                                         unsigned AddrChan) const;
public:
  enum BankSwizzle {
    ALU_VEC_012_SCL_210 = 0,
    ALU_VEC_021_SCL_122,
    ALU_VEC_120_SCL_212,
    ALU_VEC_102_SCL_221,
    ALU_VEC_201,
    ALU_VEC_210
  };

  explicit R600InstrInfo(const R600Subtarget &);

  const R600RegisterInfo &getRegisterInfo() const {
    return RI;
  }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;
  bool isLegalToSplitMBBAt(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI) const override;

  bool isReductionOp(unsigned opcode) const;
  bool isCubeOp(unsigned opcode) const;

  /// \returns true if this \p Opcode represents an ALU instruction.
  bool isALUInstr(unsigned Opcode) const;
  bool hasInstrModifiers(unsigned Opcode) const;
  bool isLDSInstr(unsigned Opcode) const;
  bool isLDSRetInstr(unsigned Opcode) const;

  /// \returns true if this \p Opcode represents an ALU instruction or an
  /// instruction that will be lowered in ExpandSpecialInstrs Pass.
  bool canBeConsideredALU(const MachineInstr &MI) const;

  bool isTransOnly(unsigned Opcode) const;
  bool isTransOnly(const MachineInstr &MI) const;
  bool isVectorOnly(unsigned Opcode) const;
  bool isVectorOnly(const MachineInstr &MI) const;
  bool isExport(unsigned Opcode) const;

  bool usesVertexCache(unsigned Opcode) const;
  bool usesVertexCache(const MachineInstr &MI) const;
  bool usesTextureCache(unsigned Opcode) const;
  bool usesTextureCache(const MachineInstr &MI) const;

  bool mustBeLastInClause(unsigned Opcode) const;
  bool usesAddressRegister(MachineInstr &MI) const;
  bool definesAddressRegister(MachineInstr &MI) const;
  bool readsLDSSrcReg(const MachineInstr &MI) const;

  /// \returns The operand Index for the Sel operand given an index to one
  /// of the instruction's src operands.
  int getSelIdx(unsigned Opcode, unsigned SrcIdx) const;

  /// \returns a pair for each src of an ALU instructions.
  /// The first member of a pair is the register id.
  /// If register is ALU_CONST, second member is SEL.
  /// If register is ALU_LITERAL, second member is IMM.
  /// Otherwise, second member value is undefined.
  SmallVector<std::pair<MachineOperand *, int64_t>, 3>
  getSrcs(MachineInstr &MI) const;

  unsigned  isLegalUpTo(
    const std::vector<std::vector<std::pair<int, unsigned> > > &IGSrcs,
    const std::vector<R600InstrInfo::BankSwizzle> &Swz,
    const std::vector<std::pair<int, unsigned> > &TransSrcs,
    R600InstrInfo::BankSwizzle TransSwz) const;

  bool FindSwizzleForVectorSlot(
    const std::vector<std::vector<std::pair<int, unsigned> > > &IGSrcs,
    std::vector<R600InstrInfo::BankSwizzle> &SwzCandidate,
    const std::vector<std::pair<int, unsigned> > &TransSrcs,
    R600InstrInfo::BankSwizzle TransSwz) const;

  /// Given the order VEC_012 < VEC_021 < VEC_120 < VEC_102 < VEC_201 < VEC_210
  /// returns true and the first (in lexical order) BankSwizzle affectation
  /// starting from the one already provided in the Instruction Group MIs that
  /// fits Read Port limitations in BS if available. Otherwise returns false
  /// and undefined content in BS.
  /// isLastAluTrans should be set if the last Alu of MIs will be executed on
  /// Trans ALU. In this case, ValidTSwizzle returns the BankSwizzle value to
  /// apply to the last instruction.
  /// PV holds GPR to PV registers in the Instruction Group MIs.
  bool fitsReadPortLimitations(const std::vector<MachineInstr *> &MIs,
                               const DenseMap<unsigned, unsigned> &PV,
                               std::vector<BankSwizzle> &BS,
                               bool isLastAluTrans) const;

  /// An instruction group can only access 2 channel pair (either [XY] or [ZW])
  /// from KCache bank on R700+. This function check if MI set in input meet
  /// this limitations
  bool fitsConstReadLimitations(const std::vector<MachineInstr *> &) const;
  /// Same but using const index set instead of MI set.
  bool fitsConstReadLimitations(const std::vector<unsigned>&) const;

  /// Vector instructions are instructions that must fill all
  /// instruction slots within an instruction group.
  bool isVector(const MachineInstr &MI) const;

  bool isMov(unsigned Opcode) const;

  DFAPacketizer *
  CreateTargetScheduleState(const TargetSubtargetInfo &) const override;

  bool reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemvoed = nullptr) const override;

  bool isPredicated(const MachineInstr &MI) const override;

  bool isPredicable(const MachineInstr &MI) const override;

  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                                 BranchProbability Probability) const override;

  bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                           unsigned ExtraPredCycles,
                           BranchProbability Probability) const override ;

  bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                           unsigned NumTCycles, unsigned ExtraTCycles,
                           MachineBasicBlock &FMBB,
                           unsigned NumFCycles, unsigned ExtraFCycles,
                           BranchProbability Probability) const override;

  bool DefinesPredicate(MachineInstr &MI,
                        std::vector<MachineOperand> &Pred) const override;

  bool isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                 MachineBasicBlock &FMBB) const override;

  bool PredicateInstruction(MachineInstr &MI,
                            ArrayRef<MachineOperand> Pred) const override;

  unsigned int getPredicationCost(const MachineInstr &) const override;

  unsigned int getInstrLatency(const InstrItineraryData *ItinData,
                               const MachineInstr &MI,
                               unsigned *PredCost = nullptr) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  /// Reserve the registers that may be accesed using indirect addressing.
  void reserveIndirectRegisters(BitVector &Reserved,
                                const MachineFunction &MF,
                                const R600RegisterInfo &TRI) const;

  /// Calculate the "Indirect Address" for the given \p RegIndex and
  /// \p Channel
  ///
  /// We model indirect addressing using a virtual address space that can be
  /// accesed with loads and stores.  The "Indirect Address" is the memory
  /// address in this virtual address space that maps to the given \p RegIndex
  /// and \p Channel.
  unsigned calculateIndirectAddress(unsigned RegIndex, unsigned Channel) const;


  /// \returns The register class to be used for loading and storing values
  /// from an "Indirect Address" .
  const TargetRegisterClass *getIndirectAddrRegClass() const;

  /// \returns the smallest register index that will be accessed by an indirect
  /// read or write or -1 if indirect addressing is not used by this program.
  int getIndirectIndexBegin(const MachineFunction &MF) const;

  /// \returns the largest register index that will be accessed by an indirect
  /// read or write or -1 if indirect addressing is not used by this program.
  int getIndirectIndexEnd(const MachineFunction &MF) const;

  /// Build instruction(s) for an indirect register write.
  ///
  /// \returns The instruction that performs the indirect register write
  MachineInstrBuilder buildIndirectWrite(MachineBasicBlock *MBB,
                                         MachineBasicBlock::iterator I,
                                         unsigned ValueReg, unsigned Address,
                                         unsigned OffsetReg) const;

  /// Build instruction(s) for an indirect register read.
  ///
  /// \returns The instruction that performs the indirect register read
  MachineInstrBuilder buildIndirectRead(MachineBasicBlock *MBB,
                                        MachineBasicBlock::iterator I,
                                        unsigned ValueReg, unsigned Address,
                                        unsigned OffsetReg) const;

  unsigned getMaxAlusPerClause() const;

  /// buildDefaultInstruction - This function returns a MachineInstr with all
  /// the instruction modifiers initialized to their default values.  You can
  /// use this function to avoid manually specifying each instruction modifier
  /// operand when building a new instruction.
  ///
  /// \returns a MachineInstr with all the instruction modifiers initialized
  /// to their default values.
  MachineInstrBuilder buildDefaultInstruction(MachineBasicBlock &MBB,
                                              MachineBasicBlock::iterator I,
                                              unsigned Opcode,
                                              unsigned DstReg,
                                              unsigned Src0Reg,
                                              unsigned Src1Reg = 0) const;

  MachineInstr *buildSlotOfVectorInstruction(MachineBasicBlock &MBB,
                                             MachineInstr *MI,
                                             unsigned Slot,
                                             unsigned DstReg) const;

  MachineInstr *buildMovImm(MachineBasicBlock &BB,
                            MachineBasicBlock::iterator I,
                            unsigned DstReg,
                            uint64_t Imm) const;

  MachineInstr *buildMovInstr(MachineBasicBlock *MBB,
                              MachineBasicBlock::iterator I,
                              unsigned DstReg, unsigned SrcReg) const;

  /// Get the index of Op in the MachineInstr.
  ///
  /// \returns -1 if the Instruction does not contain the specified \p Op.
  int getOperandIdx(const MachineInstr &MI, unsigned Op) const;

  /// Get the index of \p Op for the given Opcode.
  ///
  /// \returns -1 if the Instruction does not contain the specified \p Op.
  int getOperandIdx(unsigned Opcode, unsigned Op) const;

  /// Helper function for setting instruction flag values.
  void setImmOperand(MachineInstr &MI, unsigned Op, int64_t Imm) const;

  ///Add one of the MO_FLAG* flags to the specified \p Operand.
  void addFlag(MachineInstr &MI, unsigned Operand, unsigned Flag) const;

  ///Determine if the specified \p Flag is set on this \p Operand.
  bool isFlagSet(const MachineInstr &MI, unsigned Operand, unsigned Flag) const;

  /// \param SrcIdx The register source to set the flag on (e.g src0, src1, src2)
  /// \param Flag The flag being set.
  ///
  /// \returns the operand containing the flags for this instruction.
  MachineOperand &getFlagOp(MachineInstr &MI, unsigned SrcIdx = 0,
                            unsigned Flag = 0) const;

  /// Clear the specified flag on the instruction.
  void clearFlag(MachineInstr &MI, unsigned Operand, unsigned Flag) const;

  // Helper functions that check the opcode for status information
  bool isRegisterStore(const MachineInstr &MI) const {
    return get(MI.getOpcode()).TSFlags & R600InstrFlags::REGISTER_STORE;
  }

  bool isRegisterLoad(const MachineInstr &MI) const {
    return get(MI.getOpcode()).TSFlags & R600InstrFlags::REGISTER_LOAD;
  }

  unsigned getAddressSpaceForPseudoSourceKind(
      unsigned Kind) const override;
};

namespace R600 {

int getLDSNoRetOp(uint16_t Opcode);

} //End namespace AMDGPU

} // End llvm namespace

#endif
