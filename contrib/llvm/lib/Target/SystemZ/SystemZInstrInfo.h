//===-- SystemZInstrInfo.h - SystemZ instruction information ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the SystemZ implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H

#include "SystemZ.h"
#include "SystemZRegisterInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <cstdint>

#define GET_INSTRINFO_HEADER
#include "SystemZGenInstrInfo.inc"

namespace llvm {

class SystemZSubtarget;

namespace SystemZII {

enum {
  // See comments in SystemZInstrFormats.td.
  SimpleBDXLoad          = (1 << 0),
  SimpleBDXStore         = (1 << 1),
  Has20BitOffset         = (1 << 2),
  HasIndex               = (1 << 3),
  Is128Bit               = (1 << 4),
  AccessSizeMask         = (31 << 5),
  AccessSizeShift        = 5,
  CCValuesMask           = (15 << 10),
  CCValuesShift          = 10,
  CompareZeroCCMaskMask  = (15 << 14),
  CompareZeroCCMaskShift = 14,
  CCMaskFirst            = (1 << 18),
  CCMaskLast             = (1 << 19),
  IsLogical              = (1 << 20)
};

static inline unsigned getAccessSize(unsigned int Flags) {
  return (Flags & AccessSizeMask) >> AccessSizeShift;
}

static inline unsigned getCCValues(unsigned int Flags) {
  return (Flags & CCValuesMask) >> CCValuesShift;
}

static inline unsigned getCompareZeroCCMask(unsigned int Flags) {
  return (Flags & CompareZeroCCMaskMask) >> CompareZeroCCMaskShift;
}

// SystemZ MachineOperand target flags.
enum {
  // Masks out the bits for the access model.
  MO_SYMBOL_MODIFIER = (3 << 0),

  // @GOT (aka @GOTENT)
  MO_GOT = (1 << 0),

  // @INDNTPOFF
  MO_INDNTPOFF = (2 << 0)
};

// Classifies a branch.
enum BranchType {
  // An instruction that branches on the current value of CC.
  BranchNormal,

  // An instruction that peforms a 32-bit signed comparison and branches
  // on the result.
  BranchC,

  // An instruction that peforms a 32-bit unsigned comparison and branches
  // on the result.
  BranchCL,

  // An instruction that peforms a 64-bit signed comparison and branches
  // on the result.
  BranchCG,

  // An instruction that peforms a 64-bit unsigned comparison and branches
  // on the result.
  BranchCLG,

  // An instruction that decrements a 32-bit register and branches if
  // the result is nonzero.
  BranchCT,

  // An instruction that decrements a 64-bit register and branches if
  // the result is nonzero.
  BranchCTG
};

// Information about a branch instruction.
struct Branch {
  // The type of the branch.
  BranchType Type;

  // CCMASK_<N> is set if CC might be equal to N.
  unsigned CCValid;

  // CCMASK_<N> is set if the branch should be taken when CC == N.
  unsigned CCMask;

  // The target of the branch.
  const MachineOperand *Target;

  Branch(BranchType type, unsigned ccValid, unsigned ccMask,
         const MachineOperand *target)
    : Type(type), CCValid(ccValid), CCMask(ccMask), Target(target) {}
};

// Kinds of fused compares in compare-and-* instructions.  Together with type
// of the converted compare, this identifies the compare-and-*
// instruction.
enum FusedCompareType {
  // Relative branch - CRJ etc.
  CompareAndBranch,

  // Indirect branch, used for return - CRBReturn etc.
  CompareAndReturn,

  // Indirect branch, used for sibcall - CRBCall etc.
  CompareAndSibcall,

  // Trap
  CompareAndTrap
};

} // end namespace SystemZII

class SystemZInstrInfo : public SystemZGenInstrInfo {
  const SystemZRegisterInfo RI;
  SystemZSubtarget &STI;

  void splitMove(MachineBasicBlock::iterator MI, unsigned NewOpcode) const;
  void splitAdjDynAlloc(MachineBasicBlock::iterator MI) const;
  void expandRIPseudo(MachineInstr &MI, unsigned LowOpcode, unsigned HighOpcode,
                      bool ConvertHigh) const;
  void expandRIEPseudo(MachineInstr &MI, unsigned LowOpcode,
                       unsigned LowOpcodeK, unsigned HighOpcode) const;
  void expandRXYPseudo(MachineInstr &MI, unsigned LowOpcode,
                       unsigned HighOpcode) const;
  void expandLOCPseudo(MachineInstr &MI, unsigned LowOpcode,
                       unsigned HighOpcode) const;
  void expandLOCRPseudo(MachineInstr &MI, unsigned LowOpcode,
                        unsigned HighOpcode) const;
  void expandZExtPseudo(MachineInstr &MI, unsigned LowOpcode,
                        unsigned Size) const;
  void expandLoadStackGuard(MachineInstr *MI) const;

  MachineInstrBuilder
  emitGRX32Move(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                unsigned LowLowOpcode, unsigned Size, bool KillSrc,
                bool UndefSrc) const;

  virtual void anchor();

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

public:
  explicit SystemZInstrInfo(SystemZSubtarget &STI);

  // Override TargetInstrInfo.
  unsigned isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  unsigned isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  bool isStackSlotCopy(const MachineInstr &MI, int &DestFrameIndex,
                       int &SrcFrameIndex) const override;
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                      unsigned &SrcReg2, int &Mask, int &Value) const override;
  bool canInsertSelect(const MachineBasicBlock&, ArrayRef<MachineOperand> Cond,
                       unsigned, unsigned, int&, int&, int&) const override;
  void insertSelect(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                    const DebugLoc &DL, unsigned DstReg,
                    ArrayRef<MachineOperand> Cond, unsigned TrueReg,
                    unsigned FalseReg) const override;
  bool FoldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, unsigned Reg,
                     MachineRegisterInfo *MRI) const override;
  bool isPredicable(const MachineInstr &MI) const override;
  bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                           unsigned ExtraPredCycles,
                           BranchProbability Probability) const override;
  bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                           unsigned NumCyclesT, unsigned ExtraPredCyclesT,
                           MachineBasicBlock &FMBB,
                           unsigned NumCyclesF, unsigned ExtraPredCyclesF,
                           BranchProbability Probability) const override;
  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                            BranchProbability Probability) const override;
  bool PredicateInstruction(MachineInstr &MI,
                            ArrayRef<MachineOperand> Pred) const override;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            unsigned DestReg, int FrameIdx,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;
  MachineInstr *convertToThreeAddress(MachineFunction::iterator &MFI,
                                      MachineInstr &MI,
                                      LiveVariables *LV) const override;
  MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt, int FrameIndex,
                        LiveIntervals *LIS = nullptr) const override;
  MachineInstr *foldMemoryOperandImpl(
      MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
      MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
      LiveIntervals *LIS = nullptr) const override;
  bool expandPostRAPseudo(MachineInstr &MBBI) const override;
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const
    override;

  // Return the SystemZRegisterInfo, which this class owns.
  const SystemZRegisterInfo &getRegisterInfo() const { return RI; }

  // Return the size in bytes of MI.
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  // Return true if MI is a conditional or unconditional branch.
  // When returning true, set Cond to the mask of condition-code
  // values on which the instruction will branch, and set Target
  // to the operand that contains the branch target.  This target
  // can be a register or a basic block.
  SystemZII::Branch getBranchInfo(const MachineInstr &MI) const;

  // Get the load and store opcodes for a given register class.
  void getLoadStoreOpcodes(const TargetRegisterClass *RC,
                           unsigned &LoadOpcode, unsigned &StoreOpcode) const;

  // Opcode is the opcode of an instruction that has an address operand,
  // and the caller wants to perform that instruction's operation on an
  // address that has displacement Offset.  Return the opcode of a suitable
  // instruction (which might be Opcode itself) or 0 if no such instruction
  // exists.
  unsigned getOpcodeForOffset(unsigned Opcode, int64_t Offset) const;

  // If Opcode is a load instruction that has a LOAD AND TEST form,
  // return the opcode for the testing form, otherwise return 0.
  unsigned getLoadAndTest(unsigned Opcode) const;

  // Return true if ROTATE AND ... SELECTED BITS can be used to select bits
  // Mask of the R2 operand, given that only the low BitSize bits of Mask are
  // significant.  Set Start and End to the I3 and I4 operands if so.
  bool isRxSBGMask(uint64_t Mask, unsigned BitSize,
                   unsigned &Start, unsigned &End) const;

  // If Opcode is a COMPARE opcode for which an associated fused COMPARE AND *
  // operation exists, return the opcode for the latter, otherwise return 0.
  // MI, if nonnull, is the compare instruction.
  unsigned getFusedCompare(unsigned Opcode,
                           SystemZII::FusedCompareType Type,
                           const MachineInstr *MI = nullptr) const;

  // If Opcode is a LOAD opcode for with an associated LOAD AND TRAP
  // operation exists, returh the opcode for the latter, otherwise return 0.
  unsigned getLoadAndTrap(unsigned Opcode) const;

  // Emit code before MBBI in MI to move immediate value Value into
  // physical register Reg.
  void loadImmediate(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator MBBI,
                     unsigned Reg, uint64_t Value) const;

  // Sometimes, it is possible for the target to tell, even without
  // aliasing information, that two MIs access different memory
  // addresses. This function returns true if two MIs access different
  // memory addresses and false otherwise.
  bool
  areMemAccessesTriviallyDisjoint(MachineInstr &MIa, MachineInstr &MIb,
                                  AliasAnalysis *AA = nullptr) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H
