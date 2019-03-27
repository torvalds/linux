//===-- PPCFrameLowering.h - Define frame lowering for PowerPC --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCFRAMELOWERING_H
#define LLVM_LIB_TARGET_POWERPC_PPCFRAMELOWERING_H

#include "PPC.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class PPCSubtarget;

class PPCFrameLowering: public TargetFrameLowering {
  const PPCSubtarget &Subtarget;
  const unsigned ReturnSaveOffset;
  const unsigned TOCSaveOffset;
  const unsigned FramePointerSaveOffset;
  const unsigned LinkageSize;
  const unsigned BasePointerSaveOffset;

  /**
   * Find register[s] that can be used in function prologue and epilogue
   *
   * Find register[s] that can be use as scratch register[s] in function
   * prologue and epilogue to save various registers (Link Register, Base
   * Pointer, etc.). Prefer R0/R12, if available. Otherwise choose whatever
   * register[s] are available.
   *
   * This method will return true if it is able to find enough unique scratch
   * registers (1 or 2 depending on the requirement). If it is unable to find
   * enough available registers in the block, it will return false and set
   * any passed output parameter that corresponds to a required unique register
   * to PPC::NoRegister.
   *
   * \param[in] MBB The machine basic block to find an available register for
   * \param[in] UseAtEnd Specify whether the scratch register will be used at
   *                     the end of the basic block (i.e., will the scratch
   *                     register kill a register defined in the basic block)
   * \param[in] TwoUniqueRegsRequired Specify whether this basic block will
   *                                  require two unique scratch registers.
   * \param[out] SR1 The scratch register to use
   * \param[out] SR2 The second scratch register. If this pointer is not null
   *                 the function will attempt to set it to an available
   *                 register regardless of whether there is a hard requirement
   *                 for two unique scratch registers.
   * \return true if the required number of registers was found.
   *         false if the required number of scratch register weren't available.
   *         If either output parameter refers to a required scratch register
   *         that isn't available, it will be set to an invalid value.
   */
  bool findScratchRegister(MachineBasicBlock *MBB,
                           bool UseAtEnd,
                           bool TwoUniqueRegsRequired = false,
                           unsigned *SR1 = nullptr,
                           unsigned *SR2 = nullptr) const;
  bool twoUniqueScratchRegsRequired(MachineBasicBlock *MBB) const;

  /**
   * Create branch instruction for PPC::TCRETURN* (tail call return)
   *
   * \param[in] MBB that is terminated by PPC::TCRETURN*
   */
  void createTailCallBranchInstr(MachineBasicBlock &MBB) const;

public:
  PPCFrameLowering(const PPCSubtarget &STI);

  unsigned determineFrameLayout(MachineFunction &MF,
                                bool UpdateMF = true,
                                bool UseEstimate = false) const;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool hasFP(const MachineFunction &MF) const override;
  bool needsFP(const MachineFunction &MF) const;
  void replaceFPWithRealFP(MachineFunction &MF) const;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;
  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                     RegScavenger *RS = nullptr) const override;
  void addScavengingSpillSlot(MachineFunction &MF, RegScavenger *RS) const;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;
  /// This function will assign callee saved gprs to volatile vector registers
  /// for prologue spills when applicable. It returns false if there are any
  /// registers which were not spilled to volatile vector registers.
  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  std::vector<CalleeSavedInfo> &CSI,
                                  const TargetRegisterInfo *TRI) const override;

  /// targetHandlesStackFrameRounding - Returns true if the target is
  /// responsible for rounding up the stack frame (probably at emitPrologue
  /// time).
  bool targetHandlesStackFrameRounding() const override { return true; }

  /// getReturnSaveOffset - Return the previous frame offset to save the
  /// return address.
  unsigned getReturnSaveOffset() const { return ReturnSaveOffset; }

  /// getTOCSaveOffset - Return the previous frame offset to save the
  /// TOC register -- 64-bit SVR4 ABI only.
  unsigned getTOCSaveOffset() const { return TOCSaveOffset; }

  /// getFramePointerSaveOffset - Return the previous frame offset to save the
  /// frame pointer.
  unsigned getFramePointerSaveOffset() const { return FramePointerSaveOffset; }

  /// getBasePointerSaveOffset - Return the previous frame offset to save the
  /// base pointer.
  unsigned getBasePointerSaveOffset() const { return BasePointerSaveOffset; }

  /// getLinkageSize - Return the size of the PowerPC ABI linkage area.
  ///
  unsigned getLinkageSize() const { return LinkageSize; }

  const SpillSlot *
  getCalleeSavedSpillSlots(unsigned &NumEntries) const override;

  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  /// Methods used by shrink wrapping to determine if MBB can be used for the
  /// function prologue/epilogue.
  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;
};
} // End llvm namespace

#endif
