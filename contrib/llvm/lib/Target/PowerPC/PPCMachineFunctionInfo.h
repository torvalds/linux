//===-- PPCMachineFunctionInfo.h - Private data used for PowerPC --*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the PowerPC specific subclass of MachineFunctionInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_POWERPC_PPCMACHINEFUNCTIONINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetCallingConv.h"

namespace llvm {

/// PPCFunctionInfo - This class is derived from MachineFunction private
/// PowerPC target-specific information for each MachineFunction.
class PPCFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// FramePointerSaveIndex - Frame index of where the old frame pointer is
  /// stored.  Also used as an anchor for instructions that need to be altered
  /// when using frame pointers (dyna_add, dyna_sub.)
  int FramePointerSaveIndex = 0;

  /// ReturnAddrSaveIndex - Frame index of where the return address is stored.
  ///
  int ReturnAddrSaveIndex = 0;

  /// Frame index where the old base pointer is stored.
  int BasePointerSaveIndex = 0;

  /// Frame index where the old PIC base pointer is stored.
  int PICBasePointerSaveIndex = 0;

  /// MustSaveLR - Indicates whether LR is defined (or clobbered) in the current
  /// function.  This is only valid after the initial scan of the function by
  /// PEI.
  bool MustSaveLR;

  /// Do we have to disable shrink-wrapping? This has to be set if we emit any
  /// instructions that clobber LR in the entry block because discovering this
  /// in PEI is too late (happens after shrink-wrapping);
  bool ShrinkWrapDisabled = false;

  /// Does this function have any stack spills.
  bool HasSpills = false;

  /// Does this function spill using instructions with only r+r (not r+i)
  /// forms.
  bool HasNonRISpills = false;

  /// SpillsCR - Indicates whether CR is spilled in the current function.
  bool SpillsCR = false;

  /// Indicates whether VRSAVE is spilled in the current function.
  bool SpillsVRSAVE = false;

  /// LRStoreRequired - The bool indicates whether there is some explicit use of
  /// the LR/LR8 stack slot that is not obvious from scanning the code.  This
  /// requires that the code generator produce a store of LR to the stack on
  /// entry, even though LR may otherwise apparently not be used.
  bool LRStoreRequired = false;

  /// This function makes use of the PPC64 ELF TOC base pointer (register r2).
  bool UsesTOCBasePtr = false;

  /// MinReservedArea - This is the frame size that is at least reserved in a
  /// potential caller (parameter+linkage area).
  unsigned MinReservedArea = 0;

  /// TailCallSPDelta - Stack pointer delta used when tail calling. Maximum
  /// amount the stack pointer is adjusted to make the frame bigger for tail
  /// calls. Used for creating an area before the register spill area.
  int TailCallSPDelta = 0;

  /// HasFastCall - Does this function contain a fast call. Used to determine
  /// how the caller's stack pointer should be calculated (epilog/dynamicalloc).
  bool HasFastCall = false;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;

  /// VarArgsStackOffset - StackOffset for start of stack
  /// arguments.

  int VarArgsStackOffset = 0;

  /// VarArgsNumGPR - Index of the first unused integer
  /// register for parameter passing.
  unsigned VarArgsNumGPR = 0;

  /// VarArgsNumFPR - Index of the first unused double
  /// register for parameter passing.
  unsigned VarArgsNumFPR = 0;

  /// CRSpillFrameIndex - FrameIndex for CR spill slot for 32-bit SVR4.
  int CRSpillFrameIndex = 0;

  /// If any of CR[2-4] need to be saved in the prologue and restored in the
  /// epilogue then they are added to this array. This is used for the
  /// 64-bit SVR4 ABI.
  SmallVector<unsigned, 3> MustSaveCRs;

  /// Hold onto our MachineFunction context.
  MachineFunction &MF;

  /// Whether this uses the PIC Base register or not.
  bool UsesPICBase = false;

  /// True if this function has a subset of CSRs that is handled explicitly via
  /// copies
  bool IsSplitCSR = false;

  /// We keep track attributes for each live-in virtual registers
  /// to use SExt/ZExt flags in later optimization.
  std::vector<std::pair<unsigned, ISD::ArgFlagsTy>> LiveInAttrs;

public:
  explicit PPCFunctionInfo(MachineFunction &MF) : MF(MF) {}

  int getFramePointerSaveIndex() const { return FramePointerSaveIndex; }
  void setFramePointerSaveIndex(int Idx) { FramePointerSaveIndex = Idx; }

  int getReturnAddrSaveIndex() const { return ReturnAddrSaveIndex; }
  void setReturnAddrSaveIndex(int idx) { ReturnAddrSaveIndex = idx; }

  int getBasePointerSaveIndex() const { return BasePointerSaveIndex; }
  void setBasePointerSaveIndex(int Idx) { BasePointerSaveIndex = Idx; }

  int getPICBasePointerSaveIndex() const { return PICBasePointerSaveIndex; }
  void setPICBasePointerSaveIndex(int Idx) { PICBasePointerSaveIndex = Idx; }

  unsigned getMinReservedArea() const { return MinReservedArea; }
  void setMinReservedArea(unsigned size) { MinReservedArea = size; }

  int getTailCallSPDelta() const { return TailCallSPDelta; }
  void setTailCallSPDelta(int size) { TailCallSPDelta = size; }

  /// MustSaveLR - This is set when the prolog/epilog inserter does its initial
  /// scan of the function. It is true if the LR/LR8 register is ever explicitly
  /// defined/clobbered in the machine function (e.g. by calls and movpctolr,
  /// which is used in PIC generation), or if the LR stack slot is explicitly
  /// referenced by builtin_return_address.
  void setMustSaveLR(bool U) { MustSaveLR = U; }
  bool mustSaveLR() const    { return MustSaveLR; }

  /// We certainly don't want to shrink wrap functions if we've emitted a
  /// MovePCtoLR8 as that has to go into the entry, so the prologue definitely
  /// has to go into the entry block.
  void setShrinkWrapDisabled(bool U) { ShrinkWrapDisabled = U; }
  bool shrinkWrapDisabled() const { return ShrinkWrapDisabled; }

  void setHasSpills()      { HasSpills = true; }
  bool hasSpills() const   { return HasSpills; }

  void setHasNonRISpills()    { HasNonRISpills = true; }
  bool hasNonRISpills() const { return HasNonRISpills; }

  void setSpillsCR()       { SpillsCR = true; }
  bool isCRSpilled() const { return SpillsCR; }

  void setSpillsVRSAVE()       { SpillsVRSAVE = true; }
  bool isVRSAVESpilled() const { return SpillsVRSAVE; }

  void setLRStoreRequired() { LRStoreRequired = true; }
  bool isLRStoreRequired() const { return LRStoreRequired; }

  void setUsesTOCBasePtr()    { UsesTOCBasePtr = true; }
  bool usesTOCBasePtr() const { return UsesTOCBasePtr; }

  void setHasFastCall() { HasFastCall = true; }
  bool hasFastCall() const { return HasFastCall;}

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  int getVarArgsStackOffset() const { return VarArgsStackOffset; }
  void setVarArgsStackOffset(int Offset) { VarArgsStackOffset = Offset; }

  unsigned getVarArgsNumGPR() const { return VarArgsNumGPR; }
  void setVarArgsNumGPR(unsigned Num) { VarArgsNumGPR = Num; }

  unsigned getVarArgsNumFPR() const { return VarArgsNumFPR; }
  void setVarArgsNumFPR(unsigned Num) { VarArgsNumFPR = Num; }

  /// This function associates attributes for each live-in virtual register.
  void addLiveInAttr(unsigned VReg, ISD::ArgFlagsTy Flags) {
    LiveInAttrs.push_back(std::make_pair(VReg, Flags));
  }

  /// This function returns true if the specified vreg is
  /// a live-in register and sign-extended.
  bool isLiveInSExt(unsigned VReg) const;

  /// This function returns true if the specified vreg is
  /// a live-in register and zero-extended.
  bool isLiveInZExt(unsigned VReg) const;

  int getCRSpillFrameIndex() const { return CRSpillFrameIndex; }
  void setCRSpillFrameIndex(int idx) { CRSpillFrameIndex = idx; }

  const SmallVectorImpl<unsigned> &
    getMustSaveCRs() const { return MustSaveCRs; }
  void addMustSaveCR(unsigned Reg) { MustSaveCRs.push_back(Reg); }

  void setUsesPICBase(bool uses) { UsesPICBase = uses; }
  bool usesPICBase() const { return UsesPICBase; }

  bool isSplitCSR() const { return IsSplitCSR; }
  void setIsSplitCSR(bool s) { IsSplitCSR = s; }

  MCSymbol *getPICOffsetSymbol() const;

  MCSymbol *getGlobalEPSymbol() const;
  MCSymbol *getLocalEPSymbol() const;
  MCSymbol *getTOCOffsetSymbol() const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_POWERPC_PPCMACHINEFUNCTIONINFO_H
