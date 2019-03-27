//===-- X86MachineFunctionInfo.h - X86 machine function info ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares X86-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_X86_X86MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/MachineValueType.h"

namespace llvm {

/// X86MachineFunctionInfo - This class is derived from MachineFunction and
/// contains private X86 target-specific information for each MachineFunction.
class X86MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// ForceFramePointer - True if the function is required to use of frame
  /// pointer for reasons other than it containing dynamic allocation or
  /// that FP eliminatation is turned off. For example, Cygwin main function
  /// contains stack pointer re-alignment code which requires FP.
  bool ForceFramePointer = false;

  /// RestoreBasePointerOffset - Non-zero if the function has base pointer
  /// and makes call to llvm.eh.sjlj.setjmp. When non-zero, the value is a
  /// displacement from the frame pointer to a slot where the base pointer
  /// is stashed.
  signed char RestoreBasePointerOffset = 0;

  /// CalleeSavedFrameSize - Size of the callee-saved register portion of the
  /// stack frame in bytes.
  unsigned CalleeSavedFrameSize = 0;

  /// BytesToPopOnReturn - Number of bytes function pops on return (in addition
  /// to the space used by the return address).
  /// Used on windows platform for stdcall & fastcall name decoration
  unsigned BytesToPopOnReturn = 0;

  /// ReturnAddrIndex - FrameIndex for return slot.
  int ReturnAddrIndex = 0;

  /// FrameIndex for return slot.
  int FrameAddrIndex = 0;

  /// TailCallReturnAddrDelta - The number of bytes by which return address
  /// stack slot is moved as the result of tail call optimization.
  int TailCallReturnAddrDelta = 0;

  /// SRetReturnReg - Some subtargets require that sret lowering includes
  /// returning the value of the returned struct in a register. This field
  /// holds the virtual register into which the sret argument is passed.
  unsigned SRetReturnReg = 0;

  /// GlobalBaseReg - keeps track of the virtual register initialized for
  /// use as the global base register. This is used for PIC in some PIC
  /// relocation models.
  unsigned GlobalBaseReg = 0;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;
  /// RegSaveFrameIndex - X86-64 vararg func register save area.
  int RegSaveFrameIndex = 0;
  /// VarArgsGPOffset - X86-64 vararg func int reg offset.
  unsigned VarArgsGPOffset = 0;
  /// VarArgsFPOffset - X86-64 vararg func fp reg offset.
  unsigned VarArgsFPOffset = 0;
  /// ArgumentStackSize - The number of bytes on stack consumed by the arguments
  /// being passed on the stack.
  unsigned ArgumentStackSize = 0;
  /// NumLocalDynamics - Number of local-dynamic TLS accesses.
  unsigned NumLocalDynamics = 0;
  /// HasPushSequences - Keeps track of whether this function uses sequences
  /// of pushes to pass function parameters.
  bool HasPushSequences = false;

  /// True if the function recovers from an SEH exception, and therefore needs
  /// to spill and restore the frame pointer.
  bool HasSEHFramePtrSave = false;

  /// The frame index of a stack object containing the original frame pointer
  /// used to address arguments in a function using a base pointer.
  int SEHFramePtrSaveIndex = 0;

  /// True if this function has a subset of CSRs that is handled explicitly via
  /// copies.
  bool IsSplitCSR = false;

  /// True if this function uses the red zone.
  bool UsesRedZone = false;

  /// True if this function has WIN_ALLOCA instructions.
  bool HasWinAlloca = false;

private:
  /// ForwardedMustTailRegParms - A list of virtual and physical registers
  /// that must be forwarded to every musttail call.
  SmallVector<ForwardedRegister, 1> ForwardedMustTailRegParms;

public:
  X86MachineFunctionInfo() = default;

  explicit X86MachineFunctionInfo(MachineFunction &MF) {}

  bool getForceFramePointer() const { return ForceFramePointer;}
  void setForceFramePointer(bool forceFP) { ForceFramePointer = forceFP; }

  bool getHasPushSequences() const { return HasPushSequences; }
  void setHasPushSequences(bool HasPush) { HasPushSequences = HasPush; }

  bool getRestoreBasePointer() const { return RestoreBasePointerOffset!=0; }
  void setRestoreBasePointer(const MachineFunction *MF);
  int getRestoreBasePointerOffset() const {return RestoreBasePointerOffset; }

  unsigned getCalleeSavedFrameSize() const { return CalleeSavedFrameSize; }
  void setCalleeSavedFrameSize(unsigned bytes) { CalleeSavedFrameSize = bytes; }

  unsigned getBytesToPopOnReturn() const { return BytesToPopOnReturn; }
  void setBytesToPopOnReturn (unsigned bytes) { BytesToPopOnReturn = bytes;}

  int getRAIndex() const { return ReturnAddrIndex; }
  void setRAIndex(int Index) { ReturnAddrIndex = Index; }

  int getFAIndex() const { return FrameAddrIndex; }
  void setFAIndex(int Index) { FrameAddrIndex = Index; }

  int getTCReturnAddrDelta() const { return TailCallReturnAddrDelta; }
  void setTCReturnAddrDelta(int delta) {TailCallReturnAddrDelta = delta;}

  unsigned getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(unsigned Reg) { SRetReturnReg = Reg; }

  unsigned getGlobalBaseReg() const { return GlobalBaseReg; }
  void setGlobalBaseReg(unsigned Reg) { GlobalBaseReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Idx) { VarArgsFrameIndex = Idx; }

  int getRegSaveFrameIndex() const { return RegSaveFrameIndex; }
  void setRegSaveFrameIndex(int Idx) { RegSaveFrameIndex = Idx; }

  unsigned getVarArgsGPOffset() const { return VarArgsGPOffset; }
  void setVarArgsGPOffset(unsigned Offset) { VarArgsGPOffset = Offset; }

  unsigned getVarArgsFPOffset() const { return VarArgsFPOffset; }
  void setVarArgsFPOffset(unsigned Offset) { VarArgsFPOffset = Offset; }

  unsigned getArgumentStackSize() const { return ArgumentStackSize; }
  void setArgumentStackSize(unsigned size) { ArgumentStackSize = size; }

  unsigned getNumLocalDynamicTLSAccesses() const { return NumLocalDynamics; }
  void incNumLocalDynamicTLSAccesses() { ++NumLocalDynamics; }

  bool getHasSEHFramePtrSave() const { return HasSEHFramePtrSave; }
  void setHasSEHFramePtrSave(bool V) { HasSEHFramePtrSave = V; }

  int getSEHFramePtrSaveIndex() const { return SEHFramePtrSaveIndex; }
  void setSEHFramePtrSaveIndex(int Index) { SEHFramePtrSaveIndex = Index; }

  SmallVectorImpl<ForwardedRegister> &getForwardedMustTailRegParms() {
    return ForwardedMustTailRegParms;
  }

  bool isSplitCSR() const { return IsSplitCSR; }
  void setIsSplitCSR(bool s) { IsSplitCSR = s; }

  bool getUsesRedZone() const { return UsesRedZone; }
  void setUsesRedZone(bool V) { UsesRedZone = V; }

  bool hasWinAlloca() const { return HasWinAlloca; }
  void setHasWinAlloca(bool v) { HasWinAlloca = v; }
};

} // End llvm namespace

#endif
