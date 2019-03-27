//===-- CallingConvLower.cpp - Calling Conventions ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the CCState class, used for lowering and implementing
// calling conventions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

CCState::CCState(CallingConv::ID CC, bool isVarArg, MachineFunction &mf,
                 SmallVectorImpl<CCValAssign> &locs, LLVMContext &C)
    : CallingConv(CC), IsVarArg(isVarArg), MF(mf),
      TRI(*MF.getSubtarget().getRegisterInfo()), Locs(locs), Context(C) {
  // No stack is used.
  StackOffset = 0;
  MaxStackArgAlign = 1;

  clearByValRegsInfo();
  UsedRegs.resize((TRI.getNumRegs()+31)/32);
}

/// Allocate space on the stack large enough to pass an argument by value.
/// The size and alignment information of the argument is encoded in
/// its parameter attribute.
void CCState::HandleByVal(unsigned ValNo, MVT ValVT,
                          MVT LocVT, CCValAssign::LocInfo LocInfo,
                          int MinSize, int MinAlign,
                          ISD::ArgFlagsTy ArgFlags) {
  unsigned Align = ArgFlags.getByValAlign();
  unsigned Size  = ArgFlags.getByValSize();
  if (MinSize > (int)Size)
    Size = MinSize;
  if (MinAlign > (int)Align)
    Align = MinAlign;
  ensureMaxAlignment(Align);
  MF.getSubtarget().getTargetLowering()->HandleByVal(this, Size, Align);
  Size = unsigned(alignTo(Size, MinAlign));
  unsigned Offset = AllocateStack(Size, Align);
  addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
}

/// Mark a register and all of its aliases as allocated.
void CCState::MarkAllocated(unsigned Reg) {
  for (MCRegAliasIterator AI(Reg, &TRI, true); AI.isValid(); ++AI)
    UsedRegs[*AI/32] |= 1 << (*AI&31);
}

bool CCState::IsShadowAllocatedReg(unsigned Reg) const {
  if (!isAllocated(Reg))
    return false;

  for (auto const &ValAssign : Locs) {
    if (ValAssign.isRegLoc()) {
      for (MCRegAliasIterator AI(ValAssign.getLocReg(), &TRI, true);
           AI.isValid(); ++AI) {
        if (*AI == Reg)
          return false;
      }
    }
  }
  return true;
}

/// Analyze an array of argument values,
/// incorporating info about the formals into this state.
void
CCState::AnalyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn Fn) {
  unsigned NumArgs = Ins.size();

  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT ArgVT = Ins[i].VT;
    ISD::ArgFlagsTy ArgFlags = Ins[i].Flags;
    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, *this)) {
#ifndef NDEBUG
      dbgs() << "Formal argument #" << i << " has unhandled type "
             << EVT(ArgVT).getEVTString() << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Analyze the return values of a function, returning true if the return can
/// be performed without sret-demotion and false otherwise.
bool CCState::CheckReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                          CCAssignFn Fn) {
  // Determine which register each value should be copied into.
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    MVT VT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (Fn(i, VT, VT, CCValAssign::Full, ArgFlags, *this))
      return false;
  }
  return true;
}

/// Analyze the returned values of a return,
/// incorporating info about the result values into this state.
void CCState::AnalyzeReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                            CCAssignFn Fn) {
  // Determine which register each value should be copied into.
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    MVT VT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (Fn(i, VT, VT, CCValAssign::Full, ArgFlags, *this)) {
#ifndef NDEBUG
      dbgs() << "Return operand #" << i << " has unhandled type "
             << EVT(VT).getEVTString() << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Analyze the outgoing arguments to a call,
/// incorporating info about the passed values into this state.
void CCState::AnalyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  CCAssignFn Fn) {
  unsigned NumOps = Outs.size();
  for (unsigned i = 0; i != NumOps; ++i) {
    MVT ArgVT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call operand #" << i << " has unhandled type "
             << EVT(ArgVT).getEVTString() << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Same as above except it takes vectors of types and argument flags.
void CCState::AnalyzeCallOperands(SmallVectorImpl<MVT> &ArgVTs,
                                  SmallVectorImpl<ISD::ArgFlagsTy> &Flags,
                                  CCAssignFn Fn) {
  unsigned NumOps = ArgVTs.size();
  for (unsigned i = 0; i != NumOps; ++i) {
    MVT ArgVT = ArgVTs[i];
    ISD::ArgFlagsTy ArgFlags = Flags[i];
    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call operand #" << i << " has unhandled type "
             << EVT(ArgVT).getEVTString() << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Analyze the return values of a call, incorporating info about the passed
/// values into this state.
void CCState::AnalyzeCallResult(const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn Fn) {
  for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
    MVT VT = Ins[i].VT;
    ISD::ArgFlagsTy Flags = Ins[i].Flags;
    if (Fn(i, VT, VT, CCValAssign::Full, Flags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call result #" << i << " has unhandled type "
             << EVT(VT).getEVTString() << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Same as above except it's specialized for calls that produce a single value.
void CCState::AnalyzeCallResult(MVT VT, CCAssignFn Fn) {
  if (Fn(0, VT, VT, CCValAssign::Full, ISD::ArgFlagsTy(), *this)) {
#ifndef NDEBUG
    dbgs() << "Call result has unhandled type "
           << EVT(VT).getEVTString() << '\n';
#endif
    llvm_unreachable(nullptr);
  }
}

static bool isValueTypeInRegForCC(CallingConv::ID CC, MVT VT) {
  if (VT.isVector())
    return true; // Assume -msse-regparm might be in effect.
  if (!VT.isInteger())
    return false;
  if (CC == CallingConv::X86_VectorCall || CC == CallingConv::X86_FastCall)
    return true;
  return false;
}

void CCState::getRemainingRegParmsForType(SmallVectorImpl<MCPhysReg> &Regs,
                                          MVT VT, CCAssignFn Fn) {
  unsigned SavedStackOffset = StackOffset;
  unsigned SavedMaxStackArgAlign = MaxStackArgAlign;
  unsigned NumLocs = Locs.size();

  // Set the 'inreg' flag if it is used for this calling convention.
  ISD::ArgFlagsTy Flags;
  if (isValueTypeInRegForCC(CallingConv, VT))
    Flags.setInReg();

  // Allocate something of this value type repeatedly until we get assigned a
  // location in memory.
  bool HaveRegParm = true;
  while (HaveRegParm) {
    if (Fn(0, VT, VT, CCValAssign::Full, Flags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call has unhandled type " << EVT(VT).getEVTString()
             << " while computing remaining regparms\n";
#endif
      llvm_unreachable(nullptr);
    }
    HaveRegParm = Locs.back().isRegLoc();
  }

  // Copy all the registers from the value locations we added.
  assert(NumLocs < Locs.size() && "CC assignment failed to add location");
  for (unsigned I = NumLocs, E = Locs.size(); I != E; ++I)
    if (Locs[I].isRegLoc())
      Regs.push_back(MCPhysReg(Locs[I].getLocReg()));

  // Clear the assigned values and stack memory. We leave the registers marked
  // as allocated so that future queries don't return the same registers, i.e.
  // when i64 and f64 are both passed in GPRs.
  StackOffset = SavedStackOffset;
  MaxStackArgAlign = SavedMaxStackArgAlign;
  Locs.resize(NumLocs);
}

void CCState::analyzeMustTailForwardedRegisters(
    SmallVectorImpl<ForwardedRegister> &Forwards, ArrayRef<MVT> RegParmTypes,
    CCAssignFn Fn) {
  // Oftentimes calling conventions will not user register parameters for
  // variadic functions, so we need to assume we're not variadic so that we get
  // all the registers that might be used in a non-variadic call.
  SaveAndRestore<bool> SavedVarArg(IsVarArg, false);
  SaveAndRestore<bool> SavedMustTail(AnalyzingMustTailForwardedRegs, true);

  for (MVT RegVT : RegParmTypes) {
    SmallVector<MCPhysReg, 8> RemainingRegs;
    getRemainingRegParmsForType(RemainingRegs, RegVT, Fn);
    const TargetLowering *TL = MF.getSubtarget().getTargetLowering();
    const TargetRegisterClass *RC = TL->getRegClassFor(RegVT);
    for (MCPhysReg PReg : RemainingRegs) {
      unsigned VReg = MF.addLiveIn(PReg, RC);
      Forwards.push_back(ForwardedRegister(VReg, PReg, RegVT));
    }
  }
}

bool CCState::resultsCompatible(CallingConv::ID CalleeCC,
                                CallingConv::ID CallerCC, MachineFunction &MF,
                                LLVMContext &C,
                                const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn CalleeFn, CCAssignFn CallerFn) {
  if (CalleeCC == CallerCC)
    return true;
  SmallVector<CCValAssign, 4> RVLocs1;
  CCState CCInfo1(CalleeCC, false, MF, RVLocs1, C);
  CCInfo1.AnalyzeCallResult(Ins, CalleeFn);

  SmallVector<CCValAssign, 4> RVLocs2;
  CCState CCInfo2(CallerCC, false, MF, RVLocs2, C);
  CCInfo2.AnalyzeCallResult(Ins, CallerFn);

  if (RVLocs1.size() != RVLocs2.size())
    return false;
  for (unsigned I = 0, E = RVLocs1.size(); I != E; ++I) {
    const CCValAssign &Loc1 = RVLocs1[I];
    const CCValAssign &Loc2 = RVLocs2[I];
    if (Loc1.getLocInfo() != Loc2.getLocInfo())
      return false;
    bool RegLoc1 = Loc1.isRegLoc();
    if (RegLoc1 != Loc2.isRegLoc())
      return false;
    if (RegLoc1) {
      if (Loc1.getLocReg() != Loc2.getLocReg())
        return false;
    } else {
      if (Loc1.getLocMemOffset() != Loc2.getLocMemOffset())
        return false;
    }
  }
  return true;
}
