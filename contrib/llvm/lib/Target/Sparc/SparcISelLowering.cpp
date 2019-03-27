//===-- SparcISelLowering.cpp - Sparc DAG Lowering Implementation ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the interfaces that Sparc uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "SparcISelLowering.h"
#include "MCTargetDesc/SparcMCExpr.h"
#include "SparcMachineFunctionInfo.h"
#include "SparcRegisterInfo.h"
#include "SparcTargetMachine.h"
#include "SparcTargetObjectFile.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
using namespace llvm;


//===----------------------------------------------------------------------===//
// Calling Convention Implementation
//===----------------------------------------------------------------------===//

static bool CC_Sparc_Assign_SRet(unsigned &ValNo, MVT &ValVT,
                                 MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                                 ISD::ArgFlagsTy &ArgFlags, CCState &State)
{
  assert (ArgFlags.isSRet());

  // Assign SRet argument.
  State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                         0,
                                         LocVT, LocInfo));
  return true;
}

static bool CC_Sparc_Assign_Split_64(unsigned &ValNo, MVT &ValVT,
                                     MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                                     ISD::ArgFlagsTy &ArgFlags, CCState &State)
{
  static const MCPhysReg RegList[] = {
    SP::I0, SP::I1, SP::I2, SP::I3, SP::I4, SP::I5
  };
  // Try to get first reg.
  if (unsigned Reg = State.AllocateReg(RegList)) {
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  } else {
    // Assign whole thing in stack.
    State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                           State.AllocateStack(8,4),
                                           LocVT, LocInfo));
    return true;
  }

  // Try to get second reg.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else
    State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                           State.AllocateStack(4,4),
                                           LocVT, LocInfo));
  return true;
}

static bool CC_Sparc_Assign_Ret_Split_64(unsigned &ValNo, MVT &ValVT,
                                         MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                                         ISD::ArgFlagsTy &ArgFlags, CCState &State)
{
  static const MCPhysReg RegList[] = {
    SP::I0, SP::I1, SP::I2, SP::I3, SP::I4, SP::I5
  };

  // Try to get first reg.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else
    return false;

  // Try to get second reg.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else
    return false;

  return true;
}

// Allocate a full-sized argument for the 64-bit ABI.
static bool CC_Sparc64_Full(unsigned &ValNo, MVT &ValVT,
                            MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                            ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  assert((LocVT == MVT::f32 || LocVT == MVT::f128
          || LocVT.getSizeInBits() == 64) &&
         "Can't handle non-64 bits locations");

  // Stack space is allocated for all arguments starting from [%fp+BIAS+128].
  unsigned size      = (LocVT == MVT::f128) ? 16 : 8;
  unsigned alignment = (LocVT == MVT::f128) ? 16 : 8;
  unsigned Offset = State.AllocateStack(size, alignment);
  unsigned Reg = 0;

  if (LocVT == MVT::i64 && Offset < 6*8)
    // Promote integers to %i0-%i5.
    Reg = SP::I0 + Offset/8;
  else if (LocVT == MVT::f64 && Offset < 16*8)
    // Promote doubles to %d0-%d30. (Which LLVM calls D0-D15).
    Reg = SP::D0 + Offset/8;
  else if (LocVT == MVT::f32 && Offset < 16*8)
    // Promote floats to %f1, %f3, ...
    Reg = SP::F1 + Offset/4;
  else if (LocVT == MVT::f128 && Offset < 16*8)
    // Promote long doubles to %q0-%q28. (Which LLVM calls Q0-Q7).
    Reg = SP::Q0 + Offset/16;

  // Promote to register when possible, otherwise use the stack slot.
  if (Reg) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return true;
  }

  // This argument goes on the stack in an 8-byte slot.
  // When passing floats, LocVT is smaller than 8 bytes. Adjust the offset to
  // the right-aligned float. The first 4 bytes of the stack slot are undefined.
  if (LocVT == MVT::f32)
    Offset += 4;

  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  return true;
}

// Allocate a half-sized argument for the 64-bit ABI.
//
// This is used when passing { float, int } structs by value in registers.
static bool CC_Sparc64_Half(unsigned &ValNo, MVT &ValVT,
                            MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                            ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  assert(LocVT.getSizeInBits() == 32 && "Can't handle non-32 bits locations");
  unsigned Offset = State.AllocateStack(4, 4);

  if (LocVT == MVT::f32 && Offset < 16*8) {
    // Promote floats to %f0-%f31.
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, SP::F0 + Offset/4,
                                     LocVT, LocInfo));
    return true;
  }

  if (LocVT == MVT::i32 && Offset < 6*8) {
    // Promote integers to %i0-%i5, using half the register.
    unsigned Reg = SP::I0 + Offset/8;
    LocVT = MVT::i64;
    LocInfo = CCValAssign::AExt;

    // Set the Custom bit if this i32 goes in the high bits of a register.
    if (Offset % 8 == 0)
      State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg,
                                             LocVT, LocInfo));
    else
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return true;
  }

  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  return true;
}

#include "SparcGenCallingConv.inc"

// The calling conventions in SparcCallingConv.td are described in terms of the
// callee's register window. This function translates registers to the
// corresponding caller window %o register.
static unsigned toCallerWindow(unsigned Reg) {
  static_assert(SP::I0 + 7 == SP::I7 && SP::O0 + 7 == SP::O7,
                "Unexpected enum");
  if (Reg >= SP::I0 && Reg <= SP::I7)
    return Reg - SP::I0 + SP::O0;
  return Reg;
}

SDValue
SparcTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  if (Subtarget->is64Bit())
    return LowerReturn_64(Chain, CallConv, IsVarArg, Outs, OutVals, DL, DAG);
  return LowerReturn_32(Chain, CallConv, IsVarArg, Outs, OutVals, DL, DAG);
}

SDValue
SparcTargetLowering::LowerReturn_32(SDValue Chain, CallingConv::ID CallConv,
                                    bool IsVarArg,
                                    const SmallVectorImpl<ISD::OutputArg> &Outs,
                                    const SmallVectorImpl<SDValue> &OutVals,
                                    const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  // CCValAssign - represent the assignment of the return value to locations.
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze return values.
  CCInfo.AnalyzeReturn(Outs, RetCC_Sparc32);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);
  // Make room for the return address offset.
  RetOps.push_back(SDValue());

  // Copy the result values into the output registers.
  for (unsigned i = 0, realRVLocIdx = 0;
       i != RVLocs.size();
       ++i, ++realRVLocIdx) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Arg = OutVals[realRVLocIdx];

    if (VA.needsCustom()) {
      assert(VA.getLocVT() == MVT::v2i32);
      // Legalize ret v2i32 -> ret 2 x i32 (Basically: do what would
      // happen by default if this wasn't a legal type)

      SDValue Part0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32,
                                  Arg,
                                  DAG.getConstant(0, DL, getVectorIdxTy(DAG.getDataLayout())));
      SDValue Part1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32,
                                  Arg,
                                  DAG.getConstant(1, DL, getVectorIdxTy(DAG.getDataLayout())));

      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Part0, Flag);
      Flag = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
      VA = RVLocs[++i]; // skip ahead to next loc
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Part1,
                               Flag);
    } else
      Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Arg, Flag);

    // Guarantee that all emitted copies are stuck together with flags.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  unsigned RetAddrOffset = 8; // Call Inst + Delay Slot
  // If the function returns a struct, copy the SRetReturnReg to I0
  if (MF.getFunction().hasStructRetAttr()) {
    SparcMachineFunctionInfo *SFI = MF.getInfo<SparcMachineFunctionInfo>();
    unsigned Reg = SFI->getSRetReturnReg();
    if (!Reg)
      llvm_unreachable("sret virtual register not created in the entry block");
    auto PtrVT = getPointerTy(DAG.getDataLayout());
    SDValue Val = DAG.getCopyFromReg(Chain, DL, Reg, PtrVT);
    Chain = DAG.getCopyToReg(Chain, DL, SP::I0, Val, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(SP::I0, PtrVT));
    RetAddrOffset = 12; // CallInst + Delay Slot + Unimp
  }

  RetOps[0] = Chain;  // Update chain.
  RetOps[1] = DAG.getConstant(RetAddrOffset, DL, MVT::i32);

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(SPISD::RET_FLAG, DL, MVT::Other, RetOps);
}

// Lower return values for the 64-bit ABI.
// Return values are passed the exactly the same way as function arguments.
SDValue
SparcTargetLowering::LowerReturn_64(SDValue Chain, CallingConv::ID CallConv,
                                    bool IsVarArg,
                                    const SmallVectorImpl<ISD::OutputArg> &Outs,
                                    const SmallVectorImpl<SDValue> &OutVals,
                                    const SDLoc &DL, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of the return value to locations.
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze return values.
  CCInfo.AnalyzeReturn(Outs, RetCC_Sparc64);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // The second operand on the return instruction is the return address offset.
  // The return address is always %i7+8 with the 64-bit ABI.
  RetOps.push_back(DAG.getConstant(8, DL, MVT::i32));

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");
    SDValue OutVal = OutVals[i];

    // Integer return values must be sign or zero extended by the callee.
    switch (VA.getLocInfo()) {
    case CCValAssign::Full: break;
    case CCValAssign::SExt:
      OutVal = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), OutVal);
      break;
    case CCValAssign::ZExt:
      OutVal = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), OutVal);
      break;
    case CCValAssign::AExt:
      OutVal = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), OutVal);
      break;
    default:
      llvm_unreachable("Unknown loc info!");
    }

    // The custom bit on an i32 return value indicates that it should be passed
    // in the high bits of the register.
    if (VA.getValVT() == MVT::i32 && VA.needsCustom()) {
      OutVal = DAG.getNode(ISD::SHL, DL, MVT::i64, OutVal,
                           DAG.getConstant(32, DL, MVT::i32));

      // The next value may go in the low bits of the same register.
      // Handle both at once.
      if (i+1 < RVLocs.size() && RVLocs[i+1].getLocReg() == VA.getLocReg()) {
        SDValue NV = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, OutVals[i+1]);
        OutVal = DAG.getNode(ISD::OR, DL, MVT::i64, OutVal, NV);
        // Skip the next value, it's already done.
        ++i;
      }
    }

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVal, Flag);

    // Guarantee that all emitted copies are stuck together with flags.
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;  // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(SPISD::RET_FLAG, DL, MVT::Other, RetOps);
}

SDValue SparcTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (Subtarget->is64Bit())
    return LowerFormalArguments_64(Chain, CallConv, IsVarArg, Ins,
                                   DL, DAG, InVals);
  return LowerFormalArguments_32(Chain, CallConv, IsVarArg, Ins,
                                 DL, DAG, InVals);
}

/// LowerFormalArguments32 - V8 uses a very simple ABI, where all values are
/// passed in either one or two GPRs, including FP values.  TODO: we should
/// pass FP values in FP registers for fastcc functions.
SDValue SparcTargetLowering::LowerFormalArguments_32(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  SparcMachineFunctionInfo *FuncInfo = MF.getInfo<SparcMachineFunctionInfo>();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Sparc32);

  const unsigned StackOffset = 92;
  bool IsLittleEndian = DAG.getDataLayout().isLittleEndian();

  unsigned InIdx = 0;
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i, ++InIdx) {
    CCValAssign &VA = ArgLocs[i];

    if (Ins[InIdx].Flags.isSRet()) {
      if (InIdx != 0)
        report_fatal_error("sparc only supports sret on the first parameter");
      // Get SRet from [%fp+64].
      int FrameIdx = MF.getFrameInfo().CreateFixedObject(4, 64, true);
      SDValue FIPtr = DAG.getFrameIndex(FrameIdx, MVT::i32);
      SDValue Arg =
          DAG.getLoad(MVT::i32, dl, Chain, FIPtr, MachinePointerInfo());
      InVals.push_back(Arg);
      continue;
    }

    if (VA.isRegLoc()) {
      if (VA.needsCustom()) {
        assert(VA.getLocVT() == MVT::f64 || VA.getLocVT() == MVT::v2i32);

        unsigned VRegHi = RegInfo.createVirtualRegister(&SP::IntRegsRegClass);
        MF.getRegInfo().addLiveIn(VA.getLocReg(), VRegHi);
        SDValue HiVal = DAG.getCopyFromReg(Chain, dl, VRegHi, MVT::i32);

        assert(i+1 < e);
        CCValAssign &NextVA = ArgLocs[++i];

        SDValue LoVal;
        if (NextVA.isMemLoc()) {
          int FrameIdx = MF.getFrameInfo().
            CreateFixedObject(4, StackOffset+NextVA.getLocMemOffset(),true);
          SDValue FIPtr = DAG.getFrameIndex(FrameIdx, MVT::i32);
          LoVal = DAG.getLoad(MVT::i32, dl, Chain, FIPtr, MachinePointerInfo());
        } else {
          unsigned loReg = MF.addLiveIn(NextVA.getLocReg(),
                                        &SP::IntRegsRegClass);
          LoVal = DAG.getCopyFromReg(Chain, dl, loReg, MVT::i32);
        }

        if (IsLittleEndian)
          std::swap(LoVal, HiVal);

        SDValue WholeValue =
          DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, LoVal, HiVal);
        WholeValue = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), WholeValue);
        InVals.push_back(WholeValue);
        continue;
      }
      unsigned VReg = RegInfo.createVirtualRegister(&SP::IntRegsRegClass);
      MF.getRegInfo().addLiveIn(VA.getLocReg(), VReg);
      SDValue Arg = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i32);
      if (VA.getLocVT() == MVT::f32)
        Arg = DAG.getNode(ISD::BITCAST, dl, MVT::f32, Arg);
      else if (VA.getLocVT() != MVT::i32) {
        Arg = DAG.getNode(ISD::AssertSext, dl, MVT::i32, Arg,
                          DAG.getValueType(VA.getLocVT()));
        Arg = DAG.getNode(ISD::TRUNCATE, dl, VA.getLocVT(), Arg);
      }
      InVals.push_back(Arg);
      continue;
    }

    assert(VA.isMemLoc());

    unsigned Offset = VA.getLocMemOffset()+StackOffset;
    auto PtrVT = getPointerTy(DAG.getDataLayout());

    if (VA.needsCustom()) {
      assert(VA.getValVT() == MVT::f64 || VA.getValVT() == MVT::v2i32);
      // If it is double-word aligned, just load.
      if (Offset % 8 == 0) {
        int FI = MF.getFrameInfo().CreateFixedObject(8,
                                                     Offset,
                                                     true);
        SDValue FIPtr = DAG.getFrameIndex(FI, PtrVT);
        SDValue Load =
            DAG.getLoad(VA.getValVT(), dl, Chain, FIPtr, MachinePointerInfo());
        InVals.push_back(Load);
        continue;
      }

      int FI = MF.getFrameInfo().CreateFixedObject(4,
                                                   Offset,
                                                   true);
      SDValue FIPtr = DAG.getFrameIndex(FI, PtrVT);
      SDValue HiVal =
          DAG.getLoad(MVT::i32, dl, Chain, FIPtr, MachinePointerInfo());
      int FI2 = MF.getFrameInfo().CreateFixedObject(4,
                                                    Offset+4,
                                                    true);
      SDValue FIPtr2 = DAG.getFrameIndex(FI2, PtrVT);

      SDValue LoVal =
          DAG.getLoad(MVT::i32, dl, Chain, FIPtr2, MachinePointerInfo());

      if (IsLittleEndian)
        std::swap(LoVal, HiVal);

      SDValue WholeValue =
        DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, LoVal, HiVal);
      WholeValue = DAG.getNode(ISD::BITCAST, dl, VA.getValVT(), WholeValue);
      InVals.push_back(WholeValue);
      continue;
    }

    int FI = MF.getFrameInfo().CreateFixedObject(4,
                                                 Offset,
                                                 true);
    SDValue FIPtr = DAG.getFrameIndex(FI, PtrVT);
    SDValue Load ;
    if (VA.getValVT() == MVT::i32 || VA.getValVT() == MVT::f32) {
      Load = DAG.getLoad(VA.getValVT(), dl, Chain, FIPtr, MachinePointerInfo());
    } else if (VA.getValVT() == MVT::f128) {
      report_fatal_error("SPARCv8 does not handle f128 in calls; "
                         "pass indirectly");
    } else {
      // We shouldn't see any other value types here.
      llvm_unreachable("Unexpected ValVT encountered in frame lowering.");
    }
    InVals.push_back(Load);
  }

  if (MF.getFunction().hasStructRetAttr()) {
    // Copy the SRet Argument to SRetReturnReg.
    SparcMachineFunctionInfo *SFI = MF.getInfo<SparcMachineFunctionInfo>();
    unsigned Reg = SFI->getSRetReturnReg();
    if (!Reg) {
      Reg = MF.getRegInfo().createVirtualRegister(&SP::IntRegsRegClass);
      SFI->setSRetReturnReg(Reg);
    }
    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), dl, Reg, InVals[0]);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Copy, Chain);
  }

  // Store remaining ArgRegs to the stack if this is a varargs function.
  if (isVarArg) {
    static const MCPhysReg ArgRegs[] = {
      SP::I0, SP::I1, SP::I2, SP::I3, SP::I4, SP::I5
    };
    unsigned NumAllocated = CCInfo.getFirstUnallocated(ArgRegs);
    const MCPhysReg *CurArgReg = ArgRegs+NumAllocated, *ArgRegEnd = ArgRegs+6;
    unsigned ArgOffset = CCInfo.getNextStackOffset();
    if (NumAllocated == 6)
      ArgOffset += StackOffset;
    else {
      assert(!ArgOffset);
      ArgOffset = 68+4*NumAllocated;
    }

    // Remember the vararg offset for the va_start implementation.
    FuncInfo->setVarArgsFrameOffset(ArgOffset);

    std::vector<SDValue> OutChains;

    for (; CurArgReg != ArgRegEnd; ++CurArgReg) {
      unsigned VReg = RegInfo.createVirtualRegister(&SP::IntRegsRegClass);
      MF.getRegInfo().addLiveIn(*CurArgReg, VReg);
      SDValue Arg = DAG.getCopyFromReg(DAG.getRoot(), dl, VReg, MVT::i32);

      int FrameIdx = MF.getFrameInfo().CreateFixedObject(4, ArgOffset,
                                                         true);
      SDValue FIPtr = DAG.getFrameIndex(FrameIdx, MVT::i32);

      OutChains.push_back(
          DAG.getStore(DAG.getRoot(), dl, Arg, FIPtr, MachinePointerInfo()));
      ArgOffset += 4;
    }

    if (!OutChains.empty()) {
      OutChains.push_back(Chain);
      Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
    }
  }

  return Chain;
}

// Lower formal arguments for the 64 bit ABI.
SDValue SparcTargetLowering::LowerFormalArguments_64(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();

  // Analyze arguments according to CC_Sparc64.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_Sparc64);

  // The argument array begins at %fp+BIAS+128, after the register save area.
  const unsigned ArgArea = 128;

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    if (VA.isRegLoc()) {
      // This argument is passed in a register.
      // All integer register arguments are promoted by the caller to i64.

      // Create a virtual register for the promoted live-in value.
      unsigned VReg = MF.addLiveIn(VA.getLocReg(),
                                   getRegClassFor(VA.getLocVT()));
      SDValue Arg = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());

      // Get the high bits for i32 struct elements.
      if (VA.getValVT() == MVT::i32 && VA.needsCustom())
        Arg = DAG.getNode(ISD::SRL, DL, VA.getLocVT(), Arg,
                          DAG.getConstant(32, DL, MVT::i32));

      // The caller promoted the argument, so insert an Assert?ext SDNode so we
      // won't promote the value again in this function.
      switch (VA.getLocInfo()) {
      case CCValAssign::SExt:
        Arg = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Arg,
                          DAG.getValueType(VA.getValVT()));
        break;
      case CCValAssign::ZExt:
        Arg = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Arg,
                          DAG.getValueType(VA.getValVT()));
        break;
      default:
        break;
      }

      // Truncate the register down to the argument type.
      if (VA.isExtInLoc())
        Arg = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Arg);

      InVals.push_back(Arg);
      continue;
    }

    // The registers are exhausted. This argument was passed on the stack.
    assert(VA.isMemLoc());
    // The CC_Sparc64_Full/Half functions compute stack offsets relative to the
    // beginning of the arguments area at %fp+BIAS+128.
    unsigned Offset = VA.getLocMemOffset() + ArgArea;
    unsigned ValSize = VA.getValVT().getSizeInBits() / 8;
    // Adjust offset for extended arguments, SPARC is big-endian.
    // The caller will have written the full slot with extended bytes, but we
    // prefer our own extending loads.
    if (VA.isExtInLoc())
      Offset += 8 - ValSize;
    int FI = MF.getFrameInfo().CreateFixedObject(ValSize, Offset, true);
    InVals.push_back(
        DAG.getLoad(VA.getValVT(), DL, Chain,
                    DAG.getFrameIndex(FI, getPointerTy(MF.getDataLayout())),
                    MachinePointerInfo::getFixedStack(MF, FI)));
  }

  if (!IsVarArg)
    return Chain;

  // This function takes variable arguments, some of which may have been passed
  // in registers %i0-%i5. Variable floating point arguments are never passed
  // in floating point registers. They go on %i0-%i5 or on the stack like
  // integer arguments.
  //
  // The va_start intrinsic needs to know the offset to the first variable
  // argument.
  unsigned ArgOffset = CCInfo.getNextStackOffset();
  SparcMachineFunctionInfo *FuncInfo = MF.getInfo<SparcMachineFunctionInfo>();
  // Skip the 128 bytes of register save area.
  FuncInfo->setVarArgsFrameOffset(ArgOffset + ArgArea +
                                  Subtarget->getStackPointerBias());

  // Save the variable arguments that were passed in registers.
  // The caller is required to reserve stack space for 6 arguments regardless
  // of how many arguments were actually passed.
  SmallVector<SDValue, 8> OutChains;
  for (; ArgOffset < 6*8; ArgOffset += 8) {
    unsigned VReg = MF.addLiveIn(SP::I0 + ArgOffset/8, &SP::I64RegsRegClass);
    SDValue VArg = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64);
    int FI = MF.getFrameInfo().CreateFixedObject(8, ArgOffset + ArgArea, true);
    auto PtrVT = getPointerTy(MF.getDataLayout());
    OutChains.push_back(
        DAG.getStore(Chain, DL, VArg, DAG.getFrameIndex(FI, PtrVT),
                     MachinePointerInfo::getFixedStack(MF, FI)));
  }

  if (!OutChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);

  return Chain;
}

SDValue
SparcTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                               SmallVectorImpl<SDValue> &InVals) const {
  if (Subtarget->is64Bit())
    return LowerCall_64(CLI, InVals);
  return LowerCall_32(CLI, InVals);
}

static bool hasReturnsTwiceAttr(SelectionDAG &DAG, SDValue Callee,
                                ImmutableCallSite CS) {
  if (CS)
    return CS.hasFnAttr(Attribute::ReturnsTwice);

  const Function *CalleeFn = nullptr;
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    CalleeFn = dyn_cast<Function>(G->getGlobal());
  } else if (ExternalSymbolSDNode *E =
             dyn_cast<ExternalSymbolSDNode>(Callee)) {
    const Function &Fn = DAG.getMachineFunction().getFunction();
    const Module *M = Fn.getParent();
    const char *CalleeName = E->getSymbol();
    CalleeFn = M->getFunction(CalleeName);
  }

  if (!CalleeFn)
    return false;
  return CalleeFn->hasFnAttribute(Attribute::ReturnsTwice);
}

// Lower a call for the 32-bit ABI.
SDValue
SparcTargetLowering::LowerCall_32(TargetLowering::CallLoweringInfo &CLI,
                                  SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool isVarArg                         = CLI.IsVarArg;

  // Sparc target does not yet support tail call optimization.
  isTailCall = false;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_Sparc32);

  // Get the size of the outgoing arguments stack space requirement.
  unsigned ArgsSize = CCInfo.getNextStackOffset();

  // Keep stack frames 8-byte aligned.
  ArgsSize = (ArgsSize+7) & ~7;

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();

  // Create local copies for byval args.
  SmallVector<SDValue, 8> ByValArgs;
  for (unsigned i = 0,  e = Outs.size(); i != e; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    if (!Flags.isByVal())
      continue;

    SDValue Arg = OutVals[i];
    unsigned Size = Flags.getByValSize();
    unsigned Align = Flags.getByValAlign();

    if (Size > 0U) {
      int FI = MFI.CreateStackObject(Size, Align, false);
      SDValue FIPtr = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue SizeNode = DAG.getConstant(Size, dl, MVT::i32);

      Chain = DAG.getMemcpy(Chain, dl, FIPtr, Arg, SizeNode, Align,
                            false,        // isVolatile,
                            (Size <= 32), // AlwaysInline if size <= 32,
                            false,        // isTailCall
                            MachinePointerInfo(), MachinePointerInfo());
      ByValArgs.push_back(FIPtr);
    }
    else {
      SDValue nullVal;
      ByValArgs.push_back(nullVal);
    }
  }

  Chain = DAG.getCALLSEQ_START(Chain, ArgsSize, 0, dl);

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  const unsigned StackOffset = 92;
  bool hasStructRetAttr = false;
  unsigned SRetArgSize = 0;
  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, realArgIdx = 0, byvalArgIdx = 0, e = ArgLocs.size();
       i != e;
       ++i, ++realArgIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[realArgIdx];

    ISD::ArgFlagsTy Flags = Outs[realArgIdx].Flags;

    // Use local copy if it is a byval arg.
    if (Flags.isByVal()) {
      Arg = ByValArgs[byvalArgIdx++];
      if (!Arg) {
        continue;
      }
    }

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      break;
    }

    if (Flags.isSRet()) {
      assert(VA.needsCustom());
      // store SRet argument in %sp+64
      SDValue StackPtr = DAG.getRegister(SP::O6, MVT::i32);
      SDValue PtrOff = DAG.getIntPtrConstant(64, dl);
      PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, PtrOff);
      MemOpChains.push_back(
          DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
      hasStructRetAttr = true;
      // sret only allowed on first argument
      assert(Outs[realArgIdx].OrigArgIndex == 0);
      PointerType *Ty = cast<PointerType>(CLI.getArgs()[0].Ty);
      Type *ElementTy = Ty->getElementType();
      SRetArgSize = DAG.getDataLayout().getTypeAllocSize(ElementTy);
      continue;
    }

    if (VA.needsCustom()) {
      assert(VA.getLocVT() == MVT::f64 || VA.getLocVT() == MVT::v2i32);

      if (VA.isMemLoc()) {
        unsigned Offset = VA.getLocMemOffset() + StackOffset;
        // if it is double-word aligned, just store.
        if (Offset % 8 == 0) {
          SDValue StackPtr = DAG.getRegister(SP::O6, MVT::i32);
          SDValue PtrOff = DAG.getIntPtrConstant(Offset, dl);
          PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, PtrOff);
          MemOpChains.push_back(
              DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
          continue;
        }
      }

      if (VA.getLocVT() == MVT::f64) {
        // Move from the float value from float registers into the
        // integer registers.
        if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Arg))
          Arg = bitcastConstantFPToInt(C, dl, DAG);
        else
          Arg = DAG.getNode(ISD::BITCAST, dl, MVT::v2i32, Arg);
      }

      SDValue Part0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i32,
                                  Arg,
                                  DAG.getConstant(0, dl, getVectorIdxTy(DAG.getDataLayout())));
      SDValue Part1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i32,
                                  Arg,
                                  DAG.getConstant(1, dl, getVectorIdxTy(DAG.getDataLayout())));

      if (VA.isRegLoc()) {
        RegsToPass.push_back(std::make_pair(VA.getLocReg(), Part0));
        assert(i+1 != e);
        CCValAssign &NextVA = ArgLocs[++i];
        if (NextVA.isRegLoc()) {
          RegsToPass.push_back(std::make_pair(NextVA.getLocReg(), Part1));
        } else {
          // Store the second part in stack.
          unsigned Offset = NextVA.getLocMemOffset() + StackOffset;
          SDValue StackPtr = DAG.getRegister(SP::O6, MVT::i32);
          SDValue PtrOff = DAG.getIntPtrConstant(Offset, dl);
          PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, PtrOff);
          MemOpChains.push_back(
              DAG.getStore(Chain, dl, Part1, PtrOff, MachinePointerInfo()));
        }
      } else {
        unsigned Offset = VA.getLocMemOffset() + StackOffset;
        // Store the first part.
        SDValue StackPtr = DAG.getRegister(SP::O6, MVT::i32);
        SDValue PtrOff = DAG.getIntPtrConstant(Offset, dl);
        PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, PtrOff);
        MemOpChains.push_back(
            DAG.getStore(Chain, dl, Part0, PtrOff, MachinePointerInfo()));
        // Store the second part.
        PtrOff = DAG.getIntPtrConstant(Offset + 4, dl);
        PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, PtrOff);
        MemOpChains.push_back(
            DAG.getStore(Chain, dl, Part1, PtrOff, MachinePointerInfo()));
      }
      continue;
    }

    // Arguments that can be passed on register must be kept at
    // RegsToPass vector
    if (VA.isRegLoc()) {
      if (VA.getLocVT() != MVT::f32) {
        RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
        continue;
      }
      Arg = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      continue;
    }

    assert(VA.isMemLoc());

    // Create a store off the stack pointer for this argument.
    SDValue StackPtr = DAG.getRegister(SP::O6, MVT::i32);
    SDValue PtrOff = DAG.getIntPtrConstant(VA.getLocMemOffset() + StackOffset,
                                           dl);
    PtrOff = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr, PtrOff);
    MemOpChains.push_back(
        DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
  }


  // Emit all stores, make sure the occur before any copies into physregs.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token
  // chain and flag operands which copy the outgoing args into registers.
  // The InFlag in necessary since all emitted instructions must be
  // stuck together.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    unsigned Reg = toCallerWindow(RegsToPass[i].first);
    Chain = DAG.getCopyToReg(Chain, dl, Reg, RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  bool hasReturnsTwice = hasReturnsTwiceAttr(DAG, Callee, CLI.CS);

  // If the callee is a GlobalAddress node (quite common, every direct call is)
  // turn it into a TargetGlobalAddress node so that legalize doesn't hack it.
  // Likewise ExternalSymbol -> TargetExternalSymbol.
  unsigned TF = isPositionIndependent() ? SparcMCExpr::VK_Sparc_WPLT30 : 0;
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i32, 0, TF);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32, TF);

  // Returns a chain & a flag for retval copy to use
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  if (hasStructRetAttr)
    Ops.push_back(DAG.getTargetConstant(SRetArgSize, dl, MVT::i32));
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(toCallerWindow(RegsToPass[i].first),
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const SparcRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *Mask =
      ((hasReturnsTwice)
           ? TRI->getRTCallPreservedMask(CallConv)
           : TRI->getCallPreservedMask(DAG.getMachineFunction(), CallConv));
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  Chain = DAG.getNode(SPISD::CALL, dl, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(ArgsSize, dl, true),
                             DAG.getIntPtrConstant(0, dl, true), InFlag, dl);
  InFlag = Chain.getValue(1);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RVInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  RVInfo.AnalyzeCallResult(Ins, RetCC_Sparc32);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    if (RVLocs[i].getLocVT() == MVT::v2i32) {
      SDValue Vec = DAG.getNode(ISD::UNDEF, dl, MVT::v2i32);
      SDValue Lo = DAG.getCopyFromReg(
          Chain, dl, toCallerWindow(RVLocs[i++].getLocReg()), MVT::i32, InFlag);
      Chain = Lo.getValue(1);
      InFlag = Lo.getValue(2);
      Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2i32, Vec, Lo,
                        DAG.getConstant(0, dl, MVT::i32));
      SDValue Hi = DAG.getCopyFromReg(
          Chain, dl, toCallerWindow(RVLocs[i].getLocReg()), MVT::i32, InFlag);
      Chain = Hi.getValue(1);
      InFlag = Hi.getValue(2);
      Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2i32, Vec, Hi,
                        DAG.getConstant(1, dl, MVT::i32));
      InVals.push_back(Vec);
    } else {
      Chain =
          DAG.getCopyFromReg(Chain, dl, toCallerWindow(RVLocs[i].getLocReg()),
                             RVLocs[i].getValVT(), InFlag)
              .getValue(1);
      InFlag = Chain.getValue(2);
      InVals.push_back(Chain.getValue(0));
    }
  }

  return Chain;
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
unsigned SparcTargetLowering::getRegisterByName(const char* RegName, EVT VT,
                                               SelectionDAG &DAG) const {
  unsigned Reg = StringSwitch<unsigned>(RegName)
    .Case("i0", SP::I0).Case("i1", SP::I1).Case("i2", SP::I2).Case("i3", SP::I3)
    .Case("i4", SP::I4).Case("i5", SP::I5).Case("i6", SP::I6).Case("i7", SP::I7)
    .Case("o0", SP::O0).Case("o1", SP::O1).Case("o2", SP::O2).Case("o3", SP::O3)
    .Case("o4", SP::O4).Case("o5", SP::O5).Case("o6", SP::O6).Case("o7", SP::O7)
    .Case("l0", SP::L0).Case("l1", SP::L1).Case("l2", SP::L2).Case("l3", SP::L3)
    .Case("l4", SP::L4).Case("l5", SP::L5).Case("l6", SP::L6).Case("l7", SP::L7)
    .Case("g0", SP::G0).Case("g1", SP::G1).Case("g2", SP::G2).Case("g3", SP::G3)
    .Case("g4", SP::G4).Case("g5", SP::G5).Case("g6", SP::G6).Case("g7", SP::G7)
    .Default(0);

  if (Reg)
    return Reg;

  report_fatal_error("Invalid register name global variable");
}

// Fixup floating point arguments in the ... part of a varargs call.
//
// The SPARC v9 ABI requires that floating point arguments are treated the same
// as integers when calling a varargs function. This does not apply to the
// fixed arguments that are part of the function's prototype.
//
// This function post-processes a CCValAssign array created by
// AnalyzeCallOperands().
static void fixupVariableFloatArgs(SmallVectorImpl<CCValAssign> &ArgLocs,
                                   ArrayRef<ISD::OutputArg> Outs) {
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    const CCValAssign &VA = ArgLocs[i];
    MVT ValTy = VA.getLocVT();
    // FIXME: What about f32 arguments? C promotes them to f64 when calling
    // varargs functions.
    if (!VA.isRegLoc() || (ValTy != MVT::f64 && ValTy != MVT::f128))
      continue;
    // The fixed arguments to a varargs function still go in FP registers.
    if (Outs[VA.getValNo()].IsFixed)
      continue;

    // This floating point argument should be reassigned.
    CCValAssign NewVA;

    // Determine the offset into the argument array.
    unsigned firstReg = (ValTy == MVT::f64) ? SP::D0 : SP::Q0;
    unsigned argSize  = (ValTy == MVT::f64) ? 8 : 16;
    unsigned Offset = argSize * (VA.getLocReg() - firstReg);
    assert(Offset < 16*8 && "Offset out of range, bad register enum?");

    if (Offset < 6*8) {
      // This argument should go in %i0-%i5.
      unsigned IReg = SP::I0 + Offset/8;
      if (ValTy == MVT::f64)
        // Full register, just bitconvert into i64.
        NewVA = CCValAssign::getReg(VA.getValNo(), VA.getValVT(),
                                    IReg, MVT::i64, CCValAssign::BCvt);
      else {
        assert(ValTy == MVT::f128 && "Unexpected type!");
        // Full register, just bitconvert into i128 -- We will lower this into
        // two i64s in LowerCall_64.
        NewVA = CCValAssign::getCustomReg(VA.getValNo(), VA.getValVT(),
                                          IReg, MVT::i128, CCValAssign::BCvt);
      }
    } else {
      // This needs to go to memory, we're out of integer registers.
      NewVA = CCValAssign::getMem(VA.getValNo(), VA.getValVT(),
                                  Offset, VA.getLocVT(), VA.getLocInfo());
    }
    ArgLocs[i] = NewVA;
  }
}

// Lower a call for the 64-bit ABI.
SDValue
SparcTargetLowering::LowerCall_64(TargetLowering::CallLoweringInfo &CLI,
                                  SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  // Sparc target does not yet support tail call optimization.
  CLI.IsTailCall = false;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CLI.CallConv, CLI.IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(CLI.Outs, CC_Sparc64);

  // Get the size of the outgoing arguments stack space requirement.
  // The stack offset computed by CC_Sparc64 includes all arguments.
  // Called functions expect 6 argument words to exist in the stack frame, used
  // or not.
  unsigned ArgsSize = std::max(6*8u, CCInfo.getNextStackOffset());

  // Keep stack frames 16-byte aligned.
  ArgsSize = alignTo(ArgsSize, 16);

  // Varargs calls require special treatment.
  if (CLI.IsVarArg)
    fixupVariableFloatArgs(ArgLocs, CLI.Outs);

  // Adjust the stack pointer to make room for the arguments.
  // FIXME: Use hasReservedCallFrame to avoid %sp adjustments around all calls
  // with more than 6 arguments.
  Chain = DAG.getCALLSEQ_START(Chain, ArgsSize, 0, DL);

  // Collect the set of registers to pass to the function and their values.
  // This will be emitted as a sequence of CopyToReg nodes glued to the call
  // instruction.
  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;

  // Collect chains from all the memory opeations that copy arguments to the
  // stack. They must follow the stack pointer adjustment above and precede the
  // call instruction itself.
  SmallVector<SDValue, 8> MemOpChains;

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    const CCValAssign &VA = ArgLocs[i];
    SDValue Arg = CLI.OutVals[i];

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default:
      llvm_unreachable("Unknown location info!");
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      // fixupVariableFloatArgs() may create bitcasts from f128 to i128. But
      // SPARC does not support i128 natively. Lower it into two i64, see below.
      if (!VA.needsCustom() || VA.getValVT() != MVT::f128
          || VA.getLocVT() != MVT::i128)
        Arg = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), Arg);
      break;
    }

    if (VA.isRegLoc()) {
      if (VA.needsCustom() && VA.getValVT() == MVT::f128
          && VA.getLocVT() == MVT::i128) {
        // Store and reload into the integer register reg and reg+1.
        unsigned Offset = 8 * (VA.getLocReg() - SP::I0);
        unsigned StackOffset = Offset + Subtarget->getStackPointerBias() + 128;
        SDValue StackPtr = DAG.getRegister(SP::O6, PtrVT);
        SDValue HiPtrOff = DAG.getIntPtrConstant(StackOffset, DL);
        HiPtrOff = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, HiPtrOff);
        SDValue LoPtrOff = DAG.getIntPtrConstant(StackOffset + 8, DL);
        LoPtrOff = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, LoPtrOff);

        // Store to %sp+BIAS+128+Offset
        SDValue Store =
            DAG.getStore(Chain, DL, Arg, HiPtrOff, MachinePointerInfo());
        // Load into Reg and Reg+1
        SDValue Hi64 =
            DAG.getLoad(MVT::i64, DL, Store, HiPtrOff, MachinePointerInfo());
        SDValue Lo64 =
            DAG.getLoad(MVT::i64, DL, Store, LoPtrOff, MachinePointerInfo());
        RegsToPass.push_back(std::make_pair(toCallerWindow(VA.getLocReg()),
                                            Hi64));
        RegsToPass.push_back(std::make_pair(toCallerWindow(VA.getLocReg()+1),
                                            Lo64));
        continue;
      }

      // The custom bit on an i32 return value indicates that it should be
      // passed in the high bits of the register.
      if (VA.getValVT() == MVT::i32 && VA.needsCustom()) {
        Arg = DAG.getNode(ISD::SHL, DL, MVT::i64, Arg,
                          DAG.getConstant(32, DL, MVT::i32));

        // The next value may go in the low bits of the same register.
        // Handle both at once.
        if (i+1 < ArgLocs.size() && ArgLocs[i+1].isRegLoc() &&
            ArgLocs[i+1].getLocReg() == VA.getLocReg()) {
          SDValue NV = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64,
                                   CLI.OutVals[i+1]);
          Arg = DAG.getNode(ISD::OR, DL, MVT::i64, Arg, NV);
          // Skip the next value, it's already done.
          ++i;
        }
      }
      RegsToPass.push_back(std::make_pair(toCallerWindow(VA.getLocReg()), Arg));
      continue;
    }

    assert(VA.isMemLoc());

    // Create a store off the stack pointer for this argument.
    SDValue StackPtr = DAG.getRegister(SP::O6, PtrVT);
    // The argument area starts at %fp+BIAS+128 in the callee frame,
    // %sp+BIAS+128 in ours.
    SDValue PtrOff = DAG.getIntPtrConstant(VA.getLocMemOffset() +
                                           Subtarget->getStackPointerBias() +
                                           128, DL);
    PtrOff = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, PtrOff);
    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo()));
  }

  // Emit all stores, make sure they occur before the call.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Build a sequence of CopyToReg nodes glued together with token chain and
  // glue operands which copy the outgoing args into registers. The InGlue is
  // necessary since all emitted instructions must be stuck together in order
  // to pass the live physical registers.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, DL,
                             RegsToPass[i].first, RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress node (quite common, every direct call is)
  // turn it into a TargetGlobalAddress node so that legalize doesn't hack it.
  // Likewise ExternalSymbol -> TargetExternalSymbol.
  SDValue Callee = CLI.Callee;
  bool hasReturnsTwice = hasReturnsTwiceAttr(DAG, Callee, CLI.CS);
  unsigned TF = isPositionIndependent() ? SparcMCExpr::VK_Sparc_WPLT30 : 0;
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, PtrVT, 0, TF);
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT, TF);

  // Build the operands for the call instruction itself.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const SparcRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *Mask =
      ((hasReturnsTwice) ? TRI->getRTCallPreservedMask(CLI.CallConv)
                         : TRI->getCallPreservedMask(DAG.getMachineFunction(),
                                                     CLI.CallConv));
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  // Make sure the CopyToReg nodes are glued to the call instruction which
  // consumes the registers.
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  // Now the call itself.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(SPISD::CALL, DL, NodeTys, Ops);
  InGlue = Chain.getValue(1);

  // Revert the stack pointer immediately after the call.
  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(ArgsSize, DL, true),
                             DAG.getIntPtrConstant(0, DL, true), InGlue, DL);
  InGlue = Chain.getValue(1);

  // Now extract the return values. This is more or less the same as
  // LowerFormalArguments_64.

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RVInfo(CLI.CallConv, CLI.IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Set inreg flag manually for codegen generated library calls that
  // return float.
  if (CLI.Ins.size() == 1 && CLI.Ins[0].VT == MVT::f32 && !CLI.CS)
    CLI.Ins[0].Flags.setInReg();

  RVInfo.AnalyzeCallResult(CLI.Ins, RetCC_Sparc64);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    unsigned Reg = toCallerWindow(VA.getLocReg());

    // When returning 'inreg {i32, i32 }', two consecutive i32 arguments can
    // reside in the same register in the high and low bits. Reuse the
    // CopyFromReg previous node to avoid duplicate copies.
    SDValue RV;
    if (RegisterSDNode *SrcReg = dyn_cast<RegisterSDNode>(Chain.getOperand(1)))
      if (SrcReg->getReg() == Reg && Chain->getOpcode() == ISD::CopyFromReg)
        RV = Chain.getValue(0);

    // But usually we'll create a new CopyFromReg for a different register.
    if (!RV.getNode()) {
      RV = DAG.getCopyFromReg(Chain, DL, Reg, RVLocs[i].getLocVT(), InGlue);
      Chain = RV.getValue(1);
      InGlue = Chain.getValue(2);
    }

    // Get the high bits for i32 struct elements.
    if (VA.getValVT() == MVT::i32 && VA.needsCustom())
      RV = DAG.getNode(ISD::SRL, DL, VA.getLocVT(), RV,
                       DAG.getConstant(32, DL, MVT::i32));

    // The callee promoted the return value, so insert an Assert?ext SDNode so
    // we won't promote the value again in this function.
    switch (VA.getLocInfo()) {
    case CCValAssign::SExt:
      RV = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), RV,
                       DAG.getValueType(VA.getValVT()));
      break;
    case CCValAssign::ZExt:
      RV = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), RV,
                       DAG.getValueType(VA.getValVT()));
      break;
    default:
      break;
    }

    // Truncate the register down to the return value type.
    if (VA.isExtInLoc())
      RV = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), RV);

    InVals.push_back(RV);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
// TargetLowering Implementation
//===----------------------------------------------------------------------===//

TargetLowering::AtomicExpansionKind SparcTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  if (AI->getOperation() == AtomicRMWInst::Xchg &&
      AI->getType()->getPrimitiveSizeInBits() == 32)
    return AtomicExpansionKind::None; // Uses xchg instruction

  return AtomicExpansionKind::CmpXChg;
}

/// IntCondCCodeToICC - Convert a DAG integer condition code to a SPARC ICC
/// condition.
static SPCC::CondCodes IntCondCCodeToICC(ISD::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Unknown integer condition code!");
  case ISD::SETEQ:  return SPCC::ICC_E;
  case ISD::SETNE:  return SPCC::ICC_NE;
  case ISD::SETLT:  return SPCC::ICC_L;
  case ISD::SETGT:  return SPCC::ICC_G;
  case ISD::SETLE:  return SPCC::ICC_LE;
  case ISD::SETGE:  return SPCC::ICC_GE;
  case ISD::SETULT: return SPCC::ICC_CS;
  case ISD::SETULE: return SPCC::ICC_LEU;
  case ISD::SETUGT: return SPCC::ICC_GU;
  case ISD::SETUGE: return SPCC::ICC_CC;
  }
}

/// FPCondCCodeToFCC - Convert a DAG floatingp oint condition code to a SPARC
/// FCC condition.
static SPCC::CondCodes FPCondCCodeToFCC(ISD::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Unknown fp condition code!");
  case ISD::SETEQ:
  case ISD::SETOEQ: return SPCC::FCC_E;
  case ISD::SETNE:
  case ISD::SETUNE: return SPCC::FCC_NE;
  case ISD::SETLT:
  case ISD::SETOLT: return SPCC::FCC_L;
  case ISD::SETGT:
  case ISD::SETOGT: return SPCC::FCC_G;
  case ISD::SETLE:
  case ISD::SETOLE: return SPCC::FCC_LE;
  case ISD::SETGE:
  case ISD::SETOGE: return SPCC::FCC_GE;
  case ISD::SETULT: return SPCC::FCC_UL;
  case ISD::SETULE: return SPCC::FCC_ULE;
  case ISD::SETUGT: return SPCC::FCC_UG;
  case ISD::SETUGE: return SPCC::FCC_UGE;
  case ISD::SETUO:  return SPCC::FCC_U;
  case ISD::SETO:   return SPCC::FCC_O;
  case ISD::SETONE: return SPCC::FCC_LG;
  case ISD::SETUEQ: return SPCC::FCC_UE;
  }
}

SparcTargetLowering::SparcTargetLowering(const TargetMachine &TM,
                                         const SparcSubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  MVT PtrVT = MVT::getIntegerVT(8 * TM.getPointerSize(0));

  // Instructions which use registers as conditionals examine all the
  // bits (as does the pseudo SELECT_CC expansion). I don't think it
  // matters much whether it's ZeroOrOneBooleanContent, or
  // ZeroOrNegativeOneBooleanContent, so, arbitrarily choose the
  // former.
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  // Set up the register classes.
  addRegisterClass(MVT::i32, &SP::IntRegsRegClass);
  if (!Subtarget->useSoftFloat()) {
    addRegisterClass(MVT::f32, &SP::FPRegsRegClass);
    addRegisterClass(MVT::f64, &SP::DFPRegsRegClass);
    addRegisterClass(MVT::f128, &SP::QFPRegsRegClass);
  }
  if (Subtarget->is64Bit()) {
    addRegisterClass(MVT::i64, &SP::I64RegsRegClass);
  } else {
    // On 32bit sparc, we define a double-register 32bit register
    // class, as well. This is modeled in LLVM as a 2-vector of i32.
    addRegisterClass(MVT::v2i32, &SP::IntPairRegClass);

    // ...but almost all operations must be expanded, so set that as
    // the default.
    for (unsigned Op = 0; Op < ISD::BUILTIN_OP_END; ++Op) {
      setOperationAction(Op, MVT::v2i32, Expand);
    }
    // Truncating/extending stores/loads are also not supported.
    for (MVT VT : MVT::integer_vector_valuetypes()) {
      setLoadExtAction(ISD::SEXTLOAD, VT, MVT::v2i32, Expand);
      setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::v2i32, Expand);
      setLoadExtAction(ISD::EXTLOAD, VT, MVT::v2i32, Expand);

      setLoadExtAction(ISD::SEXTLOAD, MVT::v2i32, VT, Expand);
      setLoadExtAction(ISD::ZEXTLOAD, MVT::v2i32, VT, Expand);
      setLoadExtAction(ISD::EXTLOAD, MVT::v2i32, VT, Expand);

      setTruncStoreAction(VT, MVT::v2i32, Expand);
      setTruncStoreAction(MVT::v2i32, VT, Expand);
    }
    // However, load and store *are* legal.
    setOperationAction(ISD::LOAD, MVT::v2i32, Legal);
    setOperationAction(ISD::STORE, MVT::v2i32, Legal);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2i32, Legal);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v2i32, Legal);

    // And we need to promote i64 loads/stores into vector load/store
    setOperationAction(ISD::LOAD, MVT::i64, Custom);
    setOperationAction(ISD::STORE, MVT::i64, Custom);

    // Sadly, this doesn't work:
    //    AddPromotedToType(ISD::LOAD, MVT::i64, MVT::v2i32);
    //    AddPromotedToType(ISD::STORE, MVT::i64, MVT::v2i32);
  }

  // Turn FP extload into load/fpextend
  for (MVT VT : MVT::fp_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f32, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f64, Expand);
  }

  // Sparc doesn't have i1 sign extending load
  for (MVT VT : MVT::integer_valuetypes())
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);

  // Turn FP truncstore into trunc + store.
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f128, MVT::f32, Expand);
  setTruncStoreAction(MVT::f128, MVT::f64, Expand);

  // Custom legalize GlobalAddress nodes into LO/HI parts.
  setOperationAction(ISD::GlobalAddress, PtrVT, Custom);
  setOperationAction(ISD::GlobalTLSAddress, PtrVT, Custom);
  setOperationAction(ISD::ConstantPool, PtrVT, Custom);
  setOperationAction(ISD::BlockAddress, PtrVT, Custom);

  // Sparc doesn't have sext_inreg, replace them with shl/sra
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8 , Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1 , Expand);

  // Sparc has no REM or DIVREM operations.
  setOperationAction(ISD::UREM, MVT::i32, Expand);
  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);

  // ... nor does SparcV9.
  if (Subtarget->is64Bit()) {
    setOperationAction(ISD::UREM, MVT::i64, Expand);
    setOperationAction(ISD::SREM, MVT::i64, Expand);
    setOperationAction(ISD::SDIVREM, MVT::i64, Expand);
    setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  }

  // Custom expand fp<->sint
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);

  // Custom Expand fp<->uint
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);

  setOperationAction(ISD::BITCAST, MVT::f32, Expand);
  setOperationAction(ISD::BITCAST, MVT::i32, Expand);

  // Sparc has no select or setcc: expand to SELECT_CC.
  setOperationAction(ISD::SELECT, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::f32, Expand);
  setOperationAction(ISD::SELECT, MVT::f64, Expand);
  setOperationAction(ISD::SELECT, MVT::f128, Expand);

  setOperationAction(ISD::SETCC, MVT::i32, Expand);
  setOperationAction(ISD::SETCC, MVT::f32, Expand);
  setOperationAction(ISD::SETCC, MVT::f64, Expand);
  setOperationAction(ISD::SETCC, MVT::f128, Expand);

  // Sparc doesn't have BRCOND either, it has BR_CC.
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BR_CC, MVT::f32, Custom);
  setOperationAction(ISD::BR_CC, MVT::f64, Custom);
  setOperationAction(ISD::BR_CC, MVT::f128, Custom);

  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f128, Custom);

  setOperationAction(ISD::ADDC, MVT::i32, Custom);
  setOperationAction(ISD::ADDE, MVT::i32, Custom);
  setOperationAction(ISD::SUBC, MVT::i32, Custom);
  setOperationAction(ISD::SUBE, MVT::i32, Custom);

  if (Subtarget->is64Bit()) {
    setOperationAction(ISD::ADDC, MVT::i64, Custom);
    setOperationAction(ISD::ADDE, MVT::i64, Custom);
    setOperationAction(ISD::SUBC, MVT::i64, Custom);
    setOperationAction(ISD::SUBE, MVT::i64, Custom);
    setOperationAction(ISD::BITCAST, MVT::f64, Expand);
    setOperationAction(ISD::BITCAST, MVT::i64, Expand);
    setOperationAction(ISD::SELECT, MVT::i64, Expand);
    setOperationAction(ISD::SETCC, MVT::i64, Expand);
    setOperationAction(ISD::BR_CC, MVT::i64, Custom);
    setOperationAction(ISD::SELECT_CC, MVT::i64, Custom);

    setOperationAction(ISD::CTPOP, MVT::i64,
                       Subtarget->usePopc() ? Legal : Expand);
    setOperationAction(ISD::CTTZ , MVT::i64, Expand);
    setOperationAction(ISD::CTLZ , MVT::i64, Expand);
    setOperationAction(ISD::BSWAP, MVT::i64, Expand);
    setOperationAction(ISD::ROTL , MVT::i64, Expand);
    setOperationAction(ISD::ROTR , MVT::i64, Expand);
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);
  }

  // ATOMICs.
  // Atomics are supported on SparcV9. 32-bit atomics are also
  // supported by some Leon SparcV8 variants. Otherwise, atomics
  // are unsupported.
  if (Subtarget->isV9())
    setMaxAtomicSizeInBitsSupported(64);
  else if (Subtarget->hasLeonCasa())
    setMaxAtomicSizeInBitsSupported(32);
  else
    setMaxAtomicSizeInBitsSupported(0);

  setMinCmpXchgSizeInBits(32);

  setOperationAction(ISD::ATOMIC_SWAP, MVT::i32, Legal);

  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Legal);

  // Custom Lower Atomic LOAD/STORE
  setOperationAction(ISD::ATOMIC_LOAD, MVT::i32, Custom);
  setOperationAction(ISD::ATOMIC_STORE, MVT::i32, Custom);

  if (Subtarget->is64Bit()) {
    setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i64, Legal);
    setOperationAction(ISD::ATOMIC_SWAP, MVT::i64, Legal);
    setOperationAction(ISD::ATOMIC_LOAD, MVT::i64, Custom);
    setOperationAction(ISD::ATOMIC_STORE, MVT::i64, Custom);
  }

  if (!Subtarget->is64Bit()) {
    // These libcalls are not available in 32-bit.
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
  }

  if (!Subtarget->isV9()) {
    // SparcV8 does not have FNEGD and FABSD.
    setOperationAction(ISD::FNEG, MVT::f64, Custom);
    setOperationAction(ISD::FABS, MVT::f64, Custom);
  }

  setOperationAction(ISD::FSIN , MVT::f128, Expand);
  setOperationAction(ISD::FCOS , MVT::f128, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f128, Expand);
  setOperationAction(ISD::FREM , MVT::f128, Expand);
  setOperationAction(ISD::FMA  , MVT::f128, Expand);
  setOperationAction(ISD::FSIN , MVT::f64, Expand);
  setOperationAction(ISD::FCOS , MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f64, Expand);
  setOperationAction(ISD::FREM , MVT::f64, Expand);
  setOperationAction(ISD::FMA  , MVT::f64, Expand);
  setOperationAction(ISD::FSIN , MVT::f32, Expand);
  setOperationAction(ISD::FCOS , MVT::f32, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f32, Expand);
  setOperationAction(ISD::FREM , MVT::f32, Expand);
  setOperationAction(ISD::FMA  , MVT::f32, Expand);
  setOperationAction(ISD::CTTZ , MVT::i32, Expand);
  setOperationAction(ISD::CTLZ , MVT::i32, Expand);
  setOperationAction(ISD::ROTL , MVT::i32, Expand);
  setOperationAction(ISD::ROTR , MVT::i32, Expand);
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f128, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  setOperationAction(ISD::FPOW , MVT::f128, Expand);
  setOperationAction(ISD::FPOW , MVT::f64, Expand);
  setOperationAction(ISD::FPOW , MVT::f32, Expand);

  setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);

  // Expands to [SU]MUL_LOHI.
  setOperationAction(ISD::MULHU,     MVT::i32, Expand);
  setOperationAction(ISD::MULHS,     MVT::i32, Expand);
  setOperationAction(ISD::MUL,       MVT::i32, Expand);

  if (Subtarget->useSoftMulDiv()) {
    // .umul works for both signed and unsigned
    setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
    setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
    setLibcallName(RTLIB::MUL_I32, ".umul");

    setOperationAction(ISD::SDIV, MVT::i32, Expand);
    setLibcallName(RTLIB::SDIV_I32, ".div");

    setOperationAction(ISD::UDIV, MVT::i32, Expand);
    setLibcallName(RTLIB::UDIV_I32, ".udiv");

    setLibcallName(RTLIB::SREM_I32, ".rem");
    setLibcallName(RTLIB::UREM_I32, ".urem");
  }

  if (Subtarget->is64Bit()) {
    setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
    setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
    setOperationAction(ISD::MULHU,     MVT::i64, Expand);
    setOperationAction(ISD::MULHS,     MVT::i64, Expand);

    setOperationAction(ISD::UMULO,     MVT::i64, Custom);
    setOperationAction(ISD::SMULO,     MVT::i64, Custom);

    setOperationAction(ISD::SHL_PARTS, MVT::i64, Expand);
    setOperationAction(ISD::SRA_PARTS, MVT::i64, Expand);
    setOperationAction(ISD::SRL_PARTS, MVT::i64, Expand);
  }

  // VASTART needs to be custom lowered to use the VarArgsFrameIndex.
  setOperationAction(ISD::VASTART           , MVT::Other, Custom);
  // VAARG needs to be lowered to not do unaligned accesses for doubles.
  setOperationAction(ISD::VAARG             , MVT::Other, Custom);

  setOperationAction(ISD::TRAP              , MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP         , MVT::Other, Legal);

  // Use the default implementation.
  setOperationAction(ISD::VACOPY            , MVT::Other, Expand);
  setOperationAction(ISD::VAEND             , MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE         , MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE      , MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32  , Custom);

  setStackPointerRegisterToSaveRestore(SP::O6);

  setOperationAction(ISD::CTPOP, MVT::i32,
                     Subtarget->usePopc() ? Legal : Expand);

  if (Subtarget->isV9() && Subtarget->hasHardQuad()) {
    setOperationAction(ISD::LOAD, MVT::f128, Legal);
    setOperationAction(ISD::STORE, MVT::f128, Legal);
  } else {
    setOperationAction(ISD::LOAD, MVT::f128, Custom);
    setOperationAction(ISD::STORE, MVT::f128, Custom);
  }

  if (Subtarget->hasHardQuad()) {
    setOperationAction(ISD::FADD,  MVT::f128, Legal);
    setOperationAction(ISD::FSUB,  MVT::f128, Legal);
    setOperationAction(ISD::FMUL,  MVT::f128, Legal);
    setOperationAction(ISD::FDIV,  MVT::f128, Legal);
    setOperationAction(ISD::FSQRT, MVT::f128, Legal);
    setOperationAction(ISD::FP_EXTEND, MVT::f128, Legal);
    setOperationAction(ISD::FP_ROUND,  MVT::f64, Legal);
    if (Subtarget->isV9()) {
      setOperationAction(ISD::FNEG, MVT::f128, Legal);
      setOperationAction(ISD::FABS, MVT::f128, Legal);
    } else {
      setOperationAction(ISD::FNEG, MVT::f128, Custom);
      setOperationAction(ISD::FABS, MVT::f128, Custom);
    }

    if (!Subtarget->is64Bit()) {
      setLibcallName(RTLIB::FPTOSINT_F128_I64, "_Q_qtoll");
      setLibcallName(RTLIB::FPTOUINT_F128_I64, "_Q_qtoull");
      setLibcallName(RTLIB::SINTTOFP_I64_F128, "_Q_lltoq");
      setLibcallName(RTLIB::UINTTOFP_I64_F128, "_Q_ulltoq");
    }

  } else {
    // Custom legalize f128 operations.

    setOperationAction(ISD::FADD,  MVT::f128, Custom);
    setOperationAction(ISD::FSUB,  MVT::f128, Custom);
    setOperationAction(ISD::FMUL,  MVT::f128, Custom);
    setOperationAction(ISD::FDIV,  MVT::f128, Custom);
    setOperationAction(ISD::FSQRT, MVT::f128, Custom);
    setOperationAction(ISD::FNEG,  MVT::f128, Custom);
    setOperationAction(ISD::FABS,  MVT::f128, Custom);

    setOperationAction(ISD::FP_EXTEND, MVT::f128, Custom);
    setOperationAction(ISD::FP_ROUND,  MVT::f64, Custom);
    setOperationAction(ISD::FP_ROUND,  MVT::f32, Custom);

    // Setup Runtime library names.
    if (Subtarget->is64Bit() && !Subtarget->useSoftFloat()) {
      setLibcallName(RTLIB::ADD_F128,  "_Qp_add");
      setLibcallName(RTLIB::SUB_F128,  "_Qp_sub");
      setLibcallName(RTLIB::MUL_F128,  "_Qp_mul");
      setLibcallName(RTLIB::DIV_F128,  "_Qp_div");
      setLibcallName(RTLIB::SQRT_F128, "_Qp_sqrt");
      setLibcallName(RTLIB::FPTOSINT_F128_I32, "_Qp_qtoi");
      setLibcallName(RTLIB::FPTOUINT_F128_I32, "_Qp_qtoui");
      setLibcallName(RTLIB::SINTTOFP_I32_F128, "_Qp_itoq");
      setLibcallName(RTLIB::UINTTOFP_I32_F128, "_Qp_uitoq");
      setLibcallName(RTLIB::FPTOSINT_F128_I64, "_Qp_qtox");
      setLibcallName(RTLIB::FPTOUINT_F128_I64, "_Qp_qtoux");
      setLibcallName(RTLIB::SINTTOFP_I64_F128, "_Qp_xtoq");
      setLibcallName(RTLIB::UINTTOFP_I64_F128, "_Qp_uxtoq");
      setLibcallName(RTLIB::FPEXT_F32_F128, "_Qp_stoq");
      setLibcallName(RTLIB::FPEXT_F64_F128, "_Qp_dtoq");
      setLibcallName(RTLIB::FPROUND_F128_F32, "_Qp_qtos");
      setLibcallName(RTLIB::FPROUND_F128_F64, "_Qp_qtod");
    } else if (!Subtarget->useSoftFloat()) {
      setLibcallName(RTLIB::ADD_F128,  "_Q_add");
      setLibcallName(RTLIB::SUB_F128,  "_Q_sub");
      setLibcallName(RTLIB::MUL_F128,  "_Q_mul");
      setLibcallName(RTLIB::DIV_F128,  "_Q_div");
      setLibcallName(RTLIB::SQRT_F128, "_Q_sqrt");
      setLibcallName(RTLIB::FPTOSINT_F128_I32, "_Q_qtoi");
      setLibcallName(RTLIB::FPTOUINT_F128_I32, "_Q_qtou");
      setLibcallName(RTLIB::SINTTOFP_I32_F128, "_Q_itoq");
      setLibcallName(RTLIB::UINTTOFP_I32_F128, "_Q_utoq");
      setLibcallName(RTLIB::FPTOSINT_F128_I64, "_Q_qtoll");
      setLibcallName(RTLIB::FPTOUINT_F128_I64, "_Q_qtoull");
      setLibcallName(RTLIB::SINTTOFP_I64_F128, "_Q_lltoq");
      setLibcallName(RTLIB::UINTTOFP_I64_F128, "_Q_ulltoq");
      setLibcallName(RTLIB::FPEXT_F32_F128, "_Q_stoq");
      setLibcallName(RTLIB::FPEXT_F64_F128, "_Q_dtoq");
      setLibcallName(RTLIB::FPROUND_F128_F32, "_Q_qtos");
      setLibcallName(RTLIB::FPROUND_F128_F64, "_Q_qtod");
    }
  }

  if (Subtarget->fixAllFDIVSQRT()) {
    // Promote FDIVS and FSQRTS to FDIVD and FSQRTD instructions instead as
    // the former instructions generate errata on LEON processors.
    setOperationAction(ISD::FDIV, MVT::f32, Promote);
    setOperationAction(ISD::FSQRT, MVT::f32, Promote);
  }

  if (Subtarget->hasNoFMULS()) {
    setOperationAction(ISD::FMUL, MVT::f32, Promote);
  }

  // Custom combine bitcast between f64 and v2i32
  if (!Subtarget->is64Bit())
    setTargetDAGCombine(ISD::BITCAST);

  if (Subtarget->hasLeonCycleCounter())
    setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Custom);

  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  setMinFunctionAlignment(2);

  computeRegisterProperties(Subtarget->getRegisterInfo());
}

bool SparcTargetLowering::useSoftFloat() const {
  return Subtarget->useSoftFloat();
}

const char *SparcTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((SPISD::NodeType)Opcode) {
  case SPISD::FIRST_NUMBER:    break;
  case SPISD::CMPICC:          return "SPISD::CMPICC";
  case SPISD::CMPFCC:          return "SPISD::CMPFCC";
  case SPISD::BRICC:           return "SPISD::BRICC";
  case SPISD::BRXCC:           return "SPISD::BRXCC";
  case SPISD::BRFCC:           return "SPISD::BRFCC";
  case SPISD::SELECT_ICC:      return "SPISD::SELECT_ICC";
  case SPISD::SELECT_XCC:      return "SPISD::SELECT_XCC";
  case SPISD::SELECT_FCC:      return "SPISD::SELECT_FCC";
  case SPISD::Hi:              return "SPISD::Hi";
  case SPISD::Lo:              return "SPISD::Lo";
  case SPISD::FTOI:            return "SPISD::FTOI";
  case SPISD::ITOF:            return "SPISD::ITOF";
  case SPISD::FTOX:            return "SPISD::FTOX";
  case SPISD::XTOF:            return "SPISD::XTOF";
  case SPISD::CALL:            return "SPISD::CALL";
  case SPISD::RET_FLAG:        return "SPISD::RET_FLAG";
  case SPISD::GLOBAL_BASE_REG: return "SPISD::GLOBAL_BASE_REG";
  case SPISD::FLUSHW:          return "SPISD::FLUSHW";
  case SPISD::TLS_ADD:         return "SPISD::TLS_ADD";
  case SPISD::TLS_LD:          return "SPISD::TLS_LD";
  case SPISD::TLS_CALL:        return "SPISD::TLS_CALL";
  }
  return nullptr;
}

EVT SparcTargetLowering::getSetCCResultType(const DataLayout &, LLVMContext &,
                                            EVT VT) const {
  if (!VT.isVector())
    return MVT::i32;
  return VT.changeVectorElementTypeToInteger();
}

/// isMaskedValueZeroForTargetNode - Return true if 'Op & Mask' is known to
/// be zero. Op is expected to be a target specific node. Used by DAG
/// combiner.
void SparcTargetLowering::computeKnownBitsForTargetNode
                                (const SDValue Op,
                                 KnownBits &Known,
                                 const APInt &DemandedElts,
                                 const SelectionDAG &DAG,
                                 unsigned Depth) const {
  KnownBits Known2;
  Known.resetAll();

  switch (Op.getOpcode()) {
  default: break;
  case SPISD::SELECT_ICC:
  case SPISD::SELECT_XCC:
  case SPISD::SELECT_FCC:
    Known = DAG.computeKnownBits(Op.getOperand(1), Depth + 1);
    Known2 = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);

    // Only known if known in both the LHS and RHS.
    Known.One &= Known2.One;
    Known.Zero &= Known2.Zero;
    break;
  }
}

// Look at LHS/RHS/CC and see if they are a lowered setcc instruction.  If so
// set LHS/RHS and SPCC to the LHS/RHS of the setcc and SPCC to the condition.
static void LookThroughSetCC(SDValue &LHS, SDValue &RHS,
                             ISD::CondCode CC, unsigned &SPCC) {
  if (isNullConstant(RHS) &&
      CC == ISD::SETNE &&
      (((LHS.getOpcode() == SPISD::SELECT_ICC ||
         LHS.getOpcode() == SPISD::SELECT_XCC) &&
        LHS.getOperand(3).getOpcode() == SPISD::CMPICC) ||
       (LHS.getOpcode() == SPISD::SELECT_FCC &&
        LHS.getOperand(3).getOpcode() == SPISD::CMPFCC)) &&
      isOneConstant(LHS.getOperand(0)) &&
      isNullConstant(LHS.getOperand(1))) {
    SDValue CMPCC = LHS.getOperand(3);
    SPCC = cast<ConstantSDNode>(LHS.getOperand(2))->getZExtValue();
    LHS = CMPCC.getOperand(0);
    RHS = CMPCC.getOperand(1);
  }
}

// Convert to a target node and set target flags.
SDValue SparcTargetLowering::withTargetFlags(SDValue Op, unsigned TF,
                                             SelectionDAG &DAG) const {
  if (const GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(Op))
    return DAG.getTargetGlobalAddress(GA->getGlobal(),
                                      SDLoc(GA),
                                      GA->getValueType(0),
                                      GA->getOffset(), TF);

  if (const ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(Op))
    return DAG.getTargetConstantPool(CP->getConstVal(),
                                     CP->getValueType(0),
                                     CP->getAlignment(),
                                     CP->getOffset(), TF);

  if (const BlockAddressSDNode *BA = dyn_cast<BlockAddressSDNode>(Op))
    return DAG.getTargetBlockAddress(BA->getBlockAddress(),
                                     Op.getValueType(),
                                     0,
                                     TF);

  if (const ExternalSymbolSDNode *ES = dyn_cast<ExternalSymbolSDNode>(Op))
    return DAG.getTargetExternalSymbol(ES->getSymbol(),
                                       ES->getValueType(0), TF);

  llvm_unreachable("Unhandled address SDNode");
}

// Split Op into high and low parts according to HiTF and LoTF.
// Return an ADD node combining the parts.
SDValue SparcTargetLowering::makeHiLoPair(SDValue Op,
                                          unsigned HiTF, unsigned LoTF,
                                          SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Hi = DAG.getNode(SPISD::Hi, DL, VT, withTargetFlags(Op, HiTF, DAG));
  SDValue Lo = DAG.getNode(SPISD::Lo, DL, VT, withTargetFlags(Op, LoTF, DAG));
  return DAG.getNode(ISD::ADD, DL, VT, Hi, Lo);
}

// Build SDNodes for producing an address from a GlobalAddress, ConstantPool,
// or ExternalSymbol SDNode.
SDValue SparcTargetLowering::makeAddress(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = getPointerTy(DAG.getDataLayout());

  // Handle PIC mode first. SPARC needs a got load for every variable!
  if (isPositionIndependent()) {
    const Module *M = DAG.getMachineFunction().getFunction().getParent();
    PICLevel::Level picLevel = M->getPICLevel();
    SDValue Idx;

    if (picLevel == PICLevel::SmallPIC) {
      // This is the pic13 code model, the GOT is known to be smaller than 8KiB.
      Idx = DAG.getNode(SPISD::Lo, DL, Op.getValueType(),
                        withTargetFlags(Op, SparcMCExpr::VK_Sparc_GOT13, DAG));
    } else {
      // This is the pic32 code model, the GOT is known to be smaller than 4GB.
      Idx = makeHiLoPair(Op, SparcMCExpr::VK_Sparc_GOT22,
                         SparcMCExpr::VK_Sparc_GOT10, DAG);
    }

    SDValue GlobalBase = DAG.getNode(SPISD::GLOBAL_BASE_REG, DL, VT);
    SDValue AbsAddr = DAG.getNode(ISD::ADD, DL, VT, GlobalBase, Idx);
    // GLOBAL_BASE_REG codegen'ed with call. Inform MFI that this
    // function has calls.
    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    MFI.setHasCalls(true);
    return DAG.getLoad(VT, DL, DAG.getEntryNode(), AbsAddr,
                       MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  }

  // This is one of the absolute code models.
  switch(getTargetMachine().getCodeModel()) {
  default:
    llvm_unreachable("Unsupported absolute code model");
  case CodeModel::Small:
    // abs32.
    return makeHiLoPair(Op, SparcMCExpr::VK_Sparc_HI,
                        SparcMCExpr::VK_Sparc_LO, DAG);
  case CodeModel::Medium: {
    // abs44.
    SDValue H44 = makeHiLoPair(Op, SparcMCExpr::VK_Sparc_H44,
                               SparcMCExpr::VK_Sparc_M44, DAG);
    H44 = DAG.getNode(ISD::SHL, DL, VT, H44, DAG.getConstant(12, DL, MVT::i32));
    SDValue L44 = withTargetFlags(Op, SparcMCExpr::VK_Sparc_L44, DAG);
    L44 = DAG.getNode(SPISD::Lo, DL, VT, L44);
    return DAG.getNode(ISD::ADD, DL, VT, H44, L44);
  }
  case CodeModel::Large: {
    // abs64.
    SDValue Hi = makeHiLoPair(Op, SparcMCExpr::VK_Sparc_HH,
                              SparcMCExpr::VK_Sparc_HM, DAG);
    Hi = DAG.getNode(ISD::SHL, DL, VT, Hi, DAG.getConstant(32, DL, MVT::i32));
    SDValue Lo = makeHiLoPair(Op, SparcMCExpr::VK_Sparc_HI,
                              SparcMCExpr::VK_Sparc_LO, DAG);
    return DAG.getNode(ISD::ADD, DL, VT, Hi, Lo);
  }
  }
}

SDValue SparcTargetLowering::LowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

SDValue SparcTargetLowering::LowerConstantPool(SDValue Op,
                                               SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

SDValue SparcTargetLowering::LowerBlockAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  return makeAddress(Op, DAG);
}

SDValue SparcTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                                   SelectionDAG &DAG) const {

  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  SDLoc DL(GA);
  const GlobalValue *GV = GA->getGlobal();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  TLSModel::Model model = getTargetMachine().getTLSModel(GV);

  if (model == TLSModel::GeneralDynamic || model == TLSModel::LocalDynamic) {
    unsigned HiTF = ((model == TLSModel::GeneralDynamic)
                     ? SparcMCExpr::VK_Sparc_TLS_GD_HI22
                     : SparcMCExpr::VK_Sparc_TLS_LDM_HI22);
    unsigned LoTF = ((model == TLSModel::GeneralDynamic)
                     ? SparcMCExpr::VK_Sparc_TLS_GD_LO10
                     : SparcMCExpr::VK_Sparc_TLS_LDM_LO10);
    unsigned addTF = ((model == TLSModel::GeneralDynamic)
                      ? SparcMCExpr::VK_Sparc_TLS_GD_ADD
                      : SparcMCExpr::VK_Sparc_TLS_LDM_ADD);
    unsigned callTF = ((model == TLSModel::GeneralDynamic)
                       ? SparcMCExpr::VK_Sparc_TLS_GD_CALL
                       : SparcMCExpr::VK_Sparc_TLS_LDM_CALL);

    SDValue HiLo = makeHiLoPair(Op, HiTF, LoTF, DAG);
    SDValue Base = DAG.getNode(SPISD::GLOBAL_BASE_REG, DL, PtrVT);
    SDValue Argument = DAG.getNode(SPISD::TLS_ADD, DL, PtrVT, Base, HiLo,
                               withTargetFlags(Op, addTF, DAG));

    SDValue Chain = DAG.getEntryNode();
    SDValue InFlag;

    Chain = DAG.getCALLSEQ_START(Chain, 1, 0, DL);
    Chain = DAG.getCopyToReg(Chain, DL, SP::O0, Argument, InFlag);
    InFlag = Chain.getValue(1);
    SDValue Callee = DAG.getTargetExternalSymbol("__tls_get_addr", PtrVT);
    SDValue Symbol = withTargetFlags(Op, callTF, DAG);

    SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
    const uint32_t *Mask = Subtarget->getRegisterInfo()->getCallPreservedMask(
        DAG.getMachineFunction(), CallingConv::C);
    assert(Mask && "Missing call preserved mask for calling convention");
    SDValue Ops[] = {Chain,
                     Callee,
                     Symbol,
                     DAG.getRegister(SP::O0, PtrVT),
                     DAG.getRegisterMask(Mask),
                     InFlag};
    Chain = DAG.getNode(SPISD::TLS_CALL, DL, NodeTys, Ops);
    InFlag = Chain.getValue(1);
    Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(1, DL, true),
                               DAG.getIntPtrConstant(0, DL, true), InFlag, DL);
    InFlag = Chain.getValue(1);
    SDValue Ret = DAG.getCopyFromReg(Chain, DL, SP::O0, PtrVT, InFlag);

    if (model != TLSModel::LocalDynamic)
      return Ret;

    SDValue Hi = DAG.getNode(SPISD::Hi, DL, PtrVT,
                 withTargetFlags(Op, SparcMCExpr::VK_Sparc_TLS_LDO_HIX22, DAG));
    SDValue Lo = DAG.getNode(SPISD::Lo, DL, PtrVT,
                 withTargetFlags(Op, SparcMCExpr::VK_Sparc_TLS_LDO_LOX10, DAG));
    HiLo =  DAG.getNode(ISD::XOR, DL, PtrVT, Hi, Lo);
    return DAG.getNode(SPISD::TLS_ADD, DL, PtrVT, Ret, HiLo,
                   withTargetFlags(Op, SparcMCExpr::VK_Sparc_TLS_LDO_ADD, DAG));
  }

  if (model == TLSModel::InitialExec) {
    unsigned ldTF     = ((PtrVT == MVT::i64)? SparcMCExpr::VK_Sparc_TLS_IE_LDX
                         : SparcMCExpr::VK_Sparc_TLS_IE_LD);

    SDValue Base = DAG.getNode(SPISD::GLOBAL_BASE_REG, DL, PtrVT);

    // GLOBAL_BASE_REG codegen'ed with call. Inform MFI that this
    // function has calls.
    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    MFI.setHasCalls(true);

    SDValue TGA = makeHiLoPair(Op,
                               SparcMCExpr::VK_Sparc_TLS_IE_HI22,
                               SparcMCExpr::VK_Sparc_TLS_IE_LO10, DAG);
    SDValue Ptr = DAG.getNode(ISD::ADD, DL, PtrVT, Base, TGA);
    SDValue Offset = DAG.getNode(SPISD::TLS_LD,
                                 DL, PtrVT, Ptr,
                                 withTargetFlags(Op, ldTF, DAG));
    return DAG.getNode(SPISD::TLS_ADD, DL, PtrVT,
                       DAG.getRegister(SP::G7, PtrVT), Offset,
                       withTargetFlags(Op,
                                       SparcMCExpr::VK_Sparc_TLS_IE_ADD, DAG));
  }

  assert(model == TLSModel::LocalExec);
  SDValue Hi = DAG.getNode(SPISD::Hi, DL, PtrVT,
                  withTargetFlags(Op, SparcMCExpr::VK_Sparc_TLS_LE_HIX22, DAG));
  SDValue Lo = DAG.getNode(SPISD::Lo, DL, PtrVT,
                  withTargetFlags(Op, SparcMCExpr::VK_Sparc_TLS_LE_LOX10, DAG));
  SDValue Offset =  DAG.getNode(ISD::XOR, DL, PtrVT, Hi, Lo);

  return DAG.getNode(ISD::ADD, DL, PtrVT,
                     DAG.getRegister(SP::G7, PtrVT), Offset);
}

SDValue SparcTargetLowering::LowerF128_LibCallArg(SDValue Chain,
                                                  ArgListTy &Args, SDValue Arg,
                                                  const SDLoc &DL,
                                                  SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  EVT ArgVT = Arg.getValueType();
  Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());

  ArgListEntry Entry;
  Entry.Node = Arg;
  Entry.Ty   = ArgTy;

  if (ArgTy->isFP128Ty()) {
    // Create a stack object and pass the pointer to the library function.
    int FI = MFI.CreateStackObject(16, 8, false);
    SDValue FIPtr = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    Chain = DAG.getStore(Chain, DL, Entry.Node, FIPtr, MachinePointerInfo(),
                         /* Alignment = */ 8);

    Entry.Node = FIPtr;
    Entry.Ty   = PointerType::getUnqual(ArgTy);
  }
  Args.push_back(Entry);
  return Chain;
}

SDValue
SparcTargetLowering::LowerF128Op(SDValue Op, SelectionDAG &DAG,
                                 const char *LibFuncName,
                                 unsigned numArgs) const {

  ArgListTy Args;

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  SDValue Callee = DAG.getExternalSymbol(LibFuncName, PtrVT);
  Type *RetTy = Op.getValueType().getTypeForEVT(*DAG.getContext());
  Type *RetTyABI = RetTy;
  SDValue Chain = DAG.getEntryNode();
  SDValue RetPtr;

  if (RetTy->isFP128Ty()) {
    // Create a Stack Object to receive the return value of type f128.
    ArgListEntry Entry;
    int RetFI = MFI.CreateStackObject(16, 8, false);
    RetPtr = DAG.getFrameIndex(RetFI, PtrVT);
    Entry.Node = RetPtr;
    Entry.Ty   = PointerType::getUnqual(RetTy);
    if (!Subtarget->is64Bit())
      Entry.IsSRet = true;
    Entry.IsReturned = false;
    Args.push_back(Entry);
    RetTyABI = Type::getVoidTy(*DAG.getContext());
  }

  assert(Op->getNumOperands() >= numArgs && "Not enough operands!");
  for (unsigned i = 0, e = numArgs; i != e; ++i) {
    Chain = LowerF128_LibCallArg(Chain, Args, Op.getOperand(i), SDLoc(Op), DAG);
  }
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(SDLoc(Op)).setChain(Chain)
    .setCallee(CallingConv::C, RetTyABI, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);

  // chain is in second result.
  if (RetTyABI == RetTy)
    return CallInfo.first;

  assert (RetTy->isFP128Ty() && "Unexpected return type!");

  Chain = CallInfo.second;

  // Load RetPtr to get the return value.
  return DAG.getLoad(Op.getValueType(), SDLoc(Op), Chain, RetPtr,
                     MachinePointerInfo(), /* Alignment = */ 8);
}

SDValue SparcTargetLowering::LowerF128Compare(SDValue LHS, SDValue RHS,
                                              unsigned &SPCC, const SDLoc &DL,
                                              SelectionDAG &DAG) const {

  const char *LibCall = nullptr;
  bool is64Bit = Subtarget->is64Bit();
  switch(SPCC) {
  default: llvm_unreachable("Unhandled conditional code!");
  case SPCC::FCC_E  : LibCall = is64Bit? "_Qp_feq" : "_Q_feq"; break;
  case SPCC::FCC_NE : LibCall = is64Bit? "_Qp_fne" : "_Q_fne"; break;
  case SPCC::FCC_L  : LibCall = is64Bit? "_Qp_flt" : "_Q_flt"; break;
  case SPCC::FCC_G  : LibCall = is64Bit? "_Qp_fgt" : "_Q_fgt"; break;
  case SPCC::FCC_LE : LibCall = is64Bit? "_Qp_fle" : "_Q_fle"; break;
  case SPCC::FCC_GE : LibCall = is64Bit? "_Qp_fge" : "_Q_fge"; break;
  case SPCC::FCC_UL :
  case SPCC::FCC_ULE:
  case SPCC::FCC_UG :
  case SPCC::FCC_UGE:
  case SPCC::FCC_U  :
  case SPCC::FCC_O  :
  case SPCC::FCC_LG :
  case SPCC::FCC_UE : LibCall = is64Bit? "_Qp_cmp" : "_Q_cmp"; break;
  }

  auto PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Callee = DAG.getExternalSymbol(LibCall, PtrVT);
  Type *RetTy = Type::getInt32Ty(*DAG.getContext());
  ArgListTy Args;
  SDValue Chain = DAG.getEntryNode();
  Chain = LowerF128_LibCallArg(Chain, Args, LHS, DL, DAG);
  Chain = LowerF128_LibCallArg(Chain, Args, RHS, DL, DAG);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL).setChain(Chain)
    .setCallee(CallingConv::C, RetTy, Callee, std::move(Args));

  std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);

  // result is in first, and chain is in second result.
  SDValue Result =  CallInfo.first;

  switch(SPCC) {
  default: {
    SDValue RHS = DAG.getTargetConstant(0, DL, Result.getValueType());
    SPCC = SPCC::ICC_NE;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_UL : {
    SDValue Mask   = DAG.getTargetConstant(1, DL, Result.getValueType());
    Result = DAG.getNode(ISD::AND, DL, Result.getValueType(), Result, Mask);
    SDValue RHS    = DAG.getTargetConstant(0, DL, Result.getValueType());
    SPCC = SPCC::ICC_NE;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_ULE: {
    SDValue RHS = DAG.getTargetConstant(2, DL, Result.getValueType());
    SPCC = SPCC::ICC_NE;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_UG :  {
    SDValue RHS = DAG.getTargetConstant(1, DL, Result.getValueType());
    SPCC = SPCC::ICC_G;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_UGE: {
    SDValue RHS = DAG.getTargetConstant(1, DL, Result.getValueType());
    SPCC = SPCC::ICC_NE;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }

  case SPCC::FCC_U  :  {
    SDValue RHS = DAG.getTargetConstant(3, DL, Result.getValueType());
    SPCC = SPCC::ICC_E;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_O  :  {
    SDValue RHS = DAG.getTargetConstant(3, DL, Result.getValueType());
    SPCC = SPCC::ICC_NE;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_LG :  {
    SDValue Mask   = DAG.getTargetConstant(3, DL, Result.getValueType());
    Result = DAG.getNode(ISD::AND, DL, Result.getValueType(), Result, Mask);
    SDValue RHS    = DAG.getTargetConstant(0, DL, Result.getValueType());
    SPCC = SPCC::ICC_NE;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  case SPCC::FCC_UE : {
    SDValue Mask   = DAG.getTargetConstant(3, DL, Result.getValueType());
    Result = DAG.getNode(ISD::AND, DL, Result.getValueType(), Result, Mask);
    SDValue RHS    = DAG.getTargetConstant(0, DL, Result.getValueType());
    SPCC = SPCC::ICC_E;
    return DAG.getNode(SPISD::CMPICC, DL, MVT::Glue, Result, RHS);
  }
  }
}

static SDValue
LowerF128_FPEXTEND(SDValue Op, SelectionDAG &DAG,
                   const SparcTargetLowering &TLI) {

  if (Op.getOperand(0).getValueType() == MVT::f64)
    return TLI.LowerF128Op(Op, DAG,
                           TLI.getLibcallName(RTLIB::FPEXT_F64_F128), 1);

  if (Op.getOperand(0).getValueType() == MVT::f32)
    return TLI.LowerF128Op(Op, DAG,
                           TLI.getLibcallName(RTLIB::FPEXT_F32_F128), 1);

  llvm_unreachable("fpextend with non-float operand!");
  return SDValue();
}

static SDValue
LowerF128_FPROUND(SDValue Op, SelectionDAG &DAG,
                  const SparcTargetLowering &TLI) {
  // FP_ROUND on f64 and f32 are legal.
  if (Op.getOperand(0).getValueType() != MVT::f128)
    return Op;

  if (Op.getValueType() == MVT::f64)
    return TLI.LowerF128Op(Op, DAG,
                           TLI.getLibcallName(RTLIB::FPROUND_F128_F64), 1);
  if (Op.getValueType() == MVT::f32)
    return TLI.LowerF128Op(Op, DAG,
                           TLI.getLibcallName(RTLIB::FPROUND_F128_F32), 1);

  llvm_unreachable("fpround to non-float!");
  return SDValue();
}

static SDValue LowerFP_TO_SINT(SDValue Op, SelectionDAG &DAG,
                               const SparcTargetLowering &TLI,
                               bool hasHardQuad) {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  assert(VT == MVT::i32 || VT == MVT::i64);

  // Expand f128 operations to fp128 abi calls.
  if (Op.getOperand(0).getValueType() == MVT::f128
      && (!hasHardQuad || !TLI.isTypeLegal(VT))) {
    const char *libName = TLI.getLibcallName(VT == MVT::i32
                                             ? RTLIB::FPTOSINT_F128_I32
                                             : RTLIB::FPTOSINT_F128_I64);
    return TLI.LowerF128Op(Op, DAG, libName, 1);
  }

  // Expand if the resulting type is illegal.
  if (!TLI.isTypeLegal(VT))
    return SDValue();

  // Otherwise, Convert the fp value to integer in an FP register.
  if (VT == MVT::i32)
    Op = DAG.getNode(SPISD::FTOI, dl, MVT::f32, Op.getOperand(0));
  else
    Op = DAG.getNode(SPISD::FTOX, dl, MVT::f64, Op.getOperand(0));

  return DAG.getNode(ISD::BITCAST, dl, VT, Op);
}

static SDValue LowerSINT_TO_FP(SDValue Op, SelectionDAG &DAG,
                               const SparcTargetLowering &TLI,
                               bool hasHardQuad) {
  SDLoc dl(Op);
  EVT OpVT = Op.getOperand(0).getValueType();
  assert(OpVT == MVT::i32 || (OpVT == MVT::i64));

  EVT floatVT = (OpVT == MVT::i32) ? MVT::f32 : MVT::f64;

  // Expand f128 operations to fp128 ABI calls.
  if (Op.getValueType() == MVT::f128
      && (!hasHardQuad || !TLI.isTypeLegal(OpVT))) {
    const char *libName = TLI.getLibcallName(OpVT == MVT::i32
                                             ? RTLIB::SINTTOFP_I32_F128
                                             : RTLIB::SINTTOFP_I64_F128);
    return TLI.LowerF128Op(Op, DAG, libName, 1);
  }

  // Expand if the operand type is illegal.
  if (!TLI.isTypeLegal(OpVT))
    return SDValue();

  // Otherwise, Convert the int value to FP in an FP register.
  SDValue Tmp = DAG.getNode(ISD::BITCAST, dl, floatVT, Op.getOperand(0));
  unsigned opcode = (OpVT == MVT::i32)? SPISD::ITOF : SPISD::XTOF;
  return DAG.getNode(opcode, dl, Op.getValueType(), Tmp);
}

static SDValue LowerFP_TO_UINT(SDValue Op, SelectionDAG &DAG,
                               const SparcTargetLowering &TLI,
                               bool hasHardQuad) {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();

  // Expand if it does not involve f128 or the target has support for
  // quad floating point instructions and the resulting type is legal.
  if (Op.getOperand(0).getValueType() != MVT::f128 ||
      (hasHardQuad && TLI.isTypeLegal(VT)))
    return SDValue();

  assert(VT == MVT::i32 || VT == MVT::i64);

  return TLI.LowerF128Op(Op, DAG,
                         TLI.getLibcallName(VT == MVT::i32
                                            ? RTLIB::FPTOUINT_F128_I32
                                            : RTLIB::FPTOUINT_F128_I64),
                         1);
}

static SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG,
                               const SparcTargetLowering &TLI,
                               bool hasHardQuad) {
  SDLoc dl(Op);
  EVT OpVT = Op.getOperand(0).getValueType();
  assert(OpVT == MVT::i32 || OpVT == MVT::i64);

  // Expand if it does not involve f128 or the target has support for
  // quad floating point instructions and the operand type is legal.
  if (Op.getValueType() != MVT::f128 || (hasHardQuad && TLI.isTypeLegal(OpVT)))
    return SDValue();

  return TLI.LowerF128Op(Op, DAG,
                         TLI.getLibcallName(OpVT == MVT::i32
                                            ? RTLIB::UINTTOFP_I32_F128
                                            : RTLIB::UINTTOFP_I64_F128),
                         1);
}

static SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG,
                          const SparcTargetLowering &TLI,
                          bool hasHardQuad) {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);
  unsigned Opc, SPCC = ~0U;

  // If this is a br_cc of a "setcc", and if the setcc got lowered into
  // an CMP[IF]CC/SELECT_[IF]CC pair, find the original compared values.
  LookThroughSetCC(LHS, RHS, CC, SPCC);

  // Get the condition flag.
  SDValue CompareFlag;
  if (LHS.getValueType().isInteger()) {
    CompareFlag = DAG.getNode(SPISD::CMPICC, dl, MVT::Glue, LHS, RHS);
    if (SPCC == ~0U) SPCC = IntCondCCodeToICC(CC);
    // 32-bit compares use the icc flags, 64-bit uses the xcc flags.
    Opc = LHS.getValueType() == MVT::i32 ? SPISD::BRICC : SPISD::BRXCC;
  } else {
    if (!hasHardQuad && LHS.getValueType() == MVT::f128) {
      if (SPCC == ~0U) SPCC = FPCondCCodeToFCC(CC);
      CompareFlag = TLI.LowerF128Compare(LHS, RHS, SPCC, dl, DAG);
      Opc = SPISD::BRICC;
    } else {
      CompareFlag = DAG.getNode(SPISD::CMPFCC, dl, MVT::Glue, LHS, RHS);
      if (SPCC == ~0U) SPCC = FPCondCCodeToFCC(CC);
      Opc = SPISD::BRFCC;
    }
  }
  return DAG.getNode(Opc, dl, MVT::Other, Chain, Dest,
                     DAG.getConstant(SPCC, dl, MVT::i32), CompareFlag);
}

static SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG,
                              const SparcTargetLowering &TLI,
                              bool hasHardQuad) {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);
  SDLoc dl(Op);
  unsigned Opc, SPCC = ~0U;

  // If this is a select_cc of a "setcc", and if the setcc got lowered into
  // an CMP[IF]CC/SELECT_[IF]CC pair, find the original compared values.
  LookThroughSetCC(LHS, RHS, CC, SPCC);

  SDValue CompareFlag;
  if (LHS.getValueType().isInteger()) {
    CompareFlag = DAG.getNode(SPISD::CMPICC, dl, MVT::Glue, LHS, RHS);
    Opc = LHS.getValueType() == MVT::i32 ?
          SPISD::SELECT_ICC : SPISD::SELECT_XCC;
    if (SPCC == ~0U) SPCC = IntCondCCodeToICC(CC);
  } else {
    if (!hasHardQuad && LHS.getValueType() == MVT::f128) {
      if (SPCC == ~0U) SPCC = FPCondCCodeToFCC(CC);
      CompareFlag = TLI.LowerF128Compare(LHS, RHS, SPCC, dl, DAG);
      Opc = SPISD::SELECT_ICC;
    } else {
      CompareFlag = DAG.getNode(SPISD::CMPFCC, dl, MVT::Glue, LHS, RHS);
      Opc = SPISD::SELECT_FCC;
      if (SPCC == ~0U) SPCC = FPCondCCodeToFCC(CC);
    }
  }
  return DAG.getNode(Opc, dl, TrueVal.getValueType(), TrueVal, FalseVal,
                     DAG.getConstant(SPCC, dl, MVT::i32), CompareFlag);
}

static SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG,
                            const SparcTargetLowering &TLI) {
  MachineFunction &MF = DAG.getMachineFunction();
  SparcMachineFunctionInfo *FuncInfo = MF.getInfo<SparcMachineFunctionInfo>();
  auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());

  // Need frame address to find the address of VarArgsFrameIndex.
  MF.getFrameInfo().setFrameAddressIsTaken(true);

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  SDLoc DL(Op);
  SDValue Offset =
      DAG.getNode(ISD::ADD, DL, PtrVT, DAG.getRegister(SP::I6, PtrVT),
                  DAG.getIntPtrConstant(FuncInfo->getVarArgsFrameOffset(), DL));
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, Offset, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

static SDValue LowerVAARG(SDValue Op, SelectionDAG &DAG) {
  SDNode *Node = Op.getNode();
  EVT VT = Node->getValueType(0);
  SDValue InChain = Node->getOperand(0);
  SDValue VAListPtr = Node->getOperand(1);
  EVT PtrVT = VAListPtr.getValueType();
  const Value *SV = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  SDLoc DL(Node);
  SDValue VAList =
      DAG.getLoad(PtrVT, DL, InChain, VAListPtr, MachinePointerInfo(SV));
  // Increment the pointer, VAList, to the next vaarg.
  SDValue NextPtr = DAG.getNode(ISD::ADD, DL, PtrVT, VAList,
                                DAG.getIntPtrConstant(VT.getSizeInBits()/8,
                                                      DL));
  // Store the incremented VAList to the legalized pointer.
  InChain = DAG.getStore(VAList.getValue(1), DL, NextPtr, VAListPtr,
                         MachinePointerInfo(SV));
  // Load the actual argument out of the pointer VAList.
  // We can't count on greater alignment than the word size.
  return DAG.getLoad(VT, DL, InChain, VAList, MachinePointerInfo(),
                     std::min(PtrVT.getSizeInBits(), VT.getSizeInBits()) / 8);
}

static SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG,
                                       const SparcSubtarget *Subtarget) {
  SDValue Chain = Op.getOperand(0);  // Legalize the chain.
  SDValue Size  = Op.getOperand(1);  // Legalize the size.
  unsigned Align = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue();
  unsigned StackAlign = Subtarget->getFrameLowering()->getStackAlignment();
  EVT VT = Size->getValueType(0);
  SDLoc dl(Op);

  // TODO: implement over-aligned alloca. (Note: also implies
  // supporting support for overaligned function frames + dynamic
  // allocations, at all, which currently isn't supported)
  if (Align > StackAlign) {
    const MachineFunction &MF = DAG.getMachineFunction();
    report_fatal_error("Function \"" + Twine(MF.getName()) + "\": "
                       "over-aligned dynamic alloca not supported.");
  }

  // The resultant pointer needs to be above the register spill area
  // at the bottom of the stack.
  unsigned regSpillArea;
  if (Subtarget->is64Bit()) {
    regSpillArea = 128;
  } else {
    // On Sparc32, the size of the spill area is 92. Unfortunately,
    // that's only 4-byte aligned, not 8-byte aligned (the stack
    // pointer is 8-byte aligned). So, if the user asked for an 8-byte
    // aligned dynamic allocation, we actually need to add 96 to the
    // bottom of the stack, instead of 92, to ensure 8-byte alignment.

    // That also means adding 4 to the size of the allocation --
    // before applying the 8-byte rounding. Unfortunately, we the
    // value we get here has already had rounding applied. So, we need
    // to add 8, instead, wasting a bit more memory.

    // Further, this only actually needs to be done if the required
    // alignment is > 4, but, we've lost that info by this point, too,
    // so we always apply it.

    // (An alternative approach would be to always reserve 96 bytes
    // instead of the required 92, but then we'd waste 4 extra bytes
    // in every frame, not just those with dynamic stack allocations)

    // TODO: modify code in SelectionDAGBuilder to make this less sad.

    Size = DAG.getNode(ISD::ADD, dl, VT, Size,
                       DAG.getConstant(8, dl, VT));
    regSpillArea = 96;
  }

  unsigned SPReg = SP::O6;
  SDValue SP = DAG.getCopyFromReg(Chain, dl, SPReg, VT);
  SDValue NewSP = DAG.getNode(ISD::SUB, dl, VT, SP, Size); // Value
  Chain = DAG.getCopyToReg(SP.getValue(1), dl, SPReg, NewSP);    // Output chain

  regSpillArea += Subtarget->getStackPointerBias();

  SDValue NewVal = DAG.getNode(ISD::ADD, dl, VT, NewSP,
                               DAG.getConstant(regSpillArea, dl, VT));
  SDValue Ops[2] = { NewVal, Chain };
  return DAG.getMergeValues(Ops, dl);
}


static SDValue getFLUSHW(SDValue Op, SelectionDAG &DAG) {
  SDLoc dl(Op);
  SDValue Chain = DAG.getNode(SPISD::FLUSHW,
                              dl, MVT::Other, DAG.getEntryNode());
  return Chain;
}

static SDValue getFRAMEADDR(uint64_t depth, SDValue Op, SelectionDAG &DAG,
                            const SparcSubtarget *Subtarget,
                            bool AlwaysFlush = false) {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned FrameReg = SP::I6;
  unsigned stackBias = Subtarget->getStackPointerBias();

  SDValue FrameAddr;
  SDValue Chain;

  // flush first to make sure the windowed registers' values are in stack
  Chain = (depth || AlwaysFlush) ? getFLUSHW(Op, DAG) : DAG.getEntryNode();

  FrameAddr = DAG.getCopyFromReg(Chain, dl, FrameReg, VT);

  unsigned Offset = (Subtarget->is64Bit()) ? (stackBias + 112) : 56;

  while (depth--) {
    SDValue Ptr = DAG.getNode(ISD::ADD, dl, VT, FrameAddr,
                              DAG.getIntPtrConstant(Offset, dl));
    FrameAddr = DAG.getLoad(VT, dl, Chain, Ptr, MachinePointerInfo());
  }
  if (Subtarget->is64Bit())
    FrameAddr = DAG.getNode(ISD::ADD, dl, VT, FrameAddr,
                            DAG.getIntPtrConstant(stackBias, dl));
  return FrameAddr;
}


static SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG,
                              const SparcSubtarget *Subtarget) {

  uint64_t depth = Op.getConstantOperandVal(0);

  return getFRAMEADDR(depth, Op, DAG, Subtarget);

}

static SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG,
                               const SparcTargetLowering &TLI,
                               const SparcSubtarget *Subtarget) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (TLI.verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  uint64_t depth = Op.getConstantOperandVal(0);

  SDValue RetAddr;
  if (depth == 0) {
    auto PtrVT = TLI.getPointerTy(DAG.getDataLayout());
    unsigned RetReg = MF.addLiveIn(SP::I7, TLI.getRegClassFor(PtrVT));
    RetAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl, RetReg, VT);
    return RetAddr;
  }

  // Need frame address to find return address of the caller.
  SDValue FrameAddr = getFRAMEADDR(depth - 1, Op, DAG, Subtarget, true);

  unsigned Offset = (Subtarget->is64Bit()) ? 120 : 60;
  SDValue Ptr = DAG.getNode(ISD::ADD,
                            dl, VT,
                            FrameAddr,
                            DAG.getIntPtrConstant(Offset, dl));
  RetAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), Ptr, MachinePointerInfo());

  return RetAddr;
}

static SDValue LowerF64Op(SDValue SrcReg64, const SDLoc &dl, SelectionDAG &DAG,
                          unsigned opcode) {
  assert(SrcReg64.getValueType() == MVT::f64 && "LowerF64Op called on non-double!");
  assert(opcode == ISD::FNEG || opcode == ISD::FABS);

  // Lower fneg/fabs on f64 to fneg/fabs on f32.
  // fneg f64 => fneg f32:sub_even, fmov f32:sub_odd.
  // fabs f64 => fabs f32:sub_even, fmov f32:sub_odd.

  // Note: in little-endian, the floating-point value is stored in the
  // registers are in the opposite order, so the subreg with the sign
  // bit is the highest-numbered (odd), rather than the
  // lowest-numbered (even).

  SDValue Hi32 = DAG.getTargetExtractSubreg(SP::sub_even, dl, MVT::f32,
                                            SrcReg64);
  SDValue Lo32 = DAG.getTargetExtractSubreg(SP::sub_odd, dl, MVT::f32,
                                            SrcReg64);

  if (DAG.getDataLayout().isLittleEndian())
    Lo32 = DAG.getNode(opcode, dl, MVT::f32, Lo32);
  else
    Hi32 = DAG.getNode(opcode, dl, MVT::f32, Hi32);

  SDValue DstReg64 = SDValue(DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF,
                                                dl, MVT::f64), 0);
  DstReg64 = DAG.getTargetInsertSubreg(SP::sub_even, dl, MVT::f64,
                                       DstReg64, Hi32);
  DstReg64 = DAG.getTargetInsertSubreg(SP::sub_odd, dl, MVT::f64,
                                       DstReg64, Lo32);
  return DstReg64;
}

// Lower a f128 load into two f64 loads.
static SDValue LowerF128Load(SDValue Op, SelectionDAG &DAG)
{
  SDLoc dl(Op);
  LoadSDNode *LdNode = dyn_cast<LoadSDNode>(Op.getNode());
  assert(LdNode && LdNode->getOffset().isUndef()
         && "Unexpected node type");

  unsigned alignment = LdNode->getAlignment();
  if (alignment > 8)
    alignment = 8;

  SDValue Hi64 =
      DAG.getLoad(MVT::f64, dl, LdNode->getChain(), LdNode->getBasePtr(),
                  LdNode->getPointerInfo(), alignment);
  EVT addrVT = LdNode->getBasePtr().getValueType();
  SDValue LoPtr = DAG.getNode(ISD::ADD, dl, addrVT,
                              LdNode->getBasePtr(),
                              DAG.getConstant(8, dl, addrVT));
  SDValue Lo64 = DAG.getLoad(MVT::f64, dl, LdNode->getChain(), LoPtr,
                             LdNode->getPointerInfo(), alignment);

  SDValue SubRegEven = DAG.getTargetConstant(SP::sub_even64, dl, MVT::i32);
  SDValue SubRegOdd  = DAG.getTargetConstant(SP::sub_odd64, dl, MVT::i32);

  SDNode *InFP128 = DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF,
                                       dl, MVT::f128);
  InFP128 = DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, dl,
                               MVT::f128,
                               SDValue(InFP128, 0),
                               Hi64,
                               SubRegEven);
  InFP128 = DAG.getMachineNode(TargetOpcode::INSERT_SUBREG, dl,
                               MVT::f128,
                               SDValue(InFP128, 0),
                               Lo64,
                               SubRegOdd);
  SDValue OutChains[2] = { SDValue(Hi64.getNode(), 1),
                           SDValue(Lo64.getNode(), 1) };
  SDValue OutChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
  SDValue Ops[2] = {SDValue(InFP128,0), OutChain};
  return DAG.getMergeValues(Ops, dl);
}

static SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG)
{
  LoadSDNode *LdNode = cast<LoadSDNode>(Op.getNode());

  EVT MemVT = LdNode->getMemoryVT();
  if (MemVT == MVT::f128)
    return LowerF128Load(Op, DAG);

  return Op;
}

// Lower a f128 store into two f64 stores.
static SDValue LowerF128Store(SDValue Op, SelectionDAG &DAG) {
  SDLoc dl(Op);
  StoreSDNode *StNode = dyn_cast<StoreSDNode>(Op.getNode());
  assert(StNode && StNode->getOffset().isUndef()
         && "Unexpected node type");
  SDValue SubRegEven = DAG.getTargetConstant(SP::sub_even64, dl, MVT::i32);
  SDValue SubRegOdd  = DAG.getTargetConstant(SP::sub_odd64, dl, MVT::i32);

  SDNode *Hi64 = DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                    dl,
                                    MVT::f64,
                                    StNode->getValue(),
                                    SubRegEven);
  SDNode *Lo64 = DAG.getMachineNode(TargetOpcode::EXTRACT_SUBREG,
                                    dl,
                                    MVT::f64,
                                    StNode->getValue(),
                                    SubRegOdd);

  unsigned alignment = StNode->getAlignment();
  if (alignment > 8)
    alignment = 8;

  SDValue OutChains[2];
  OutChains[0] =
      DAG.getStore(StNode->getChain(), dl, SDValue(Hi64, 0),
                   StNode->getBasePtr(), MachinePointerInfo(), alignment);
  EVT addrVT = StNode->getBasePtr().getValueType();
  SDValue LoPtr = DAG.getNode(ISD::ADD, dl, addrVT,
                              StNode->getBasePtr(),
                              DAG.getConstant(8, dl, addrVT));
  OutChains[1] = DAG.getStore(StNode->getChain(), dl, SDValue(Lo64, 0), LoPtr,
                              MachinePointerInfo(), alignment);
  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
}

static SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG)
{
  SDLoc dl(Op);
  StoreSDNode *St = cast<StoreSDNode>(Op.getNode());

  EVT MemVT = St->getMemoryVT();
  if (MemVT == MVT::f128)
    return LowerF128Store(Op, DAG);

  if (MemVT == MVT::i64) {
    // Custom handling for i64 stores: turn it into a bitcast and a
    // v2i32 store.
    SDValue Val = DAG.getNode(ISD::BITCAST, dl, MVT::v2i32, St->getValue());
    SDValue Chain = DAG.getStore(
        St->getChain(), dl, Val, St->getBasePtr(), St->getPointerInfo(),
        St->getAlignment(), St->getMemOperand()->getFlags(), St->getAAInfo());
    return Chain;
  }

  return SDValue();
}

static SDValue LowerFNEGorFABS(SDValue Op, SelectionDAG &DAG, bool isV9) {
  assert((Op.getOpcode() == ISD::FNEG || Op.getOpcode() == ISD::FABS)
         && "invalid opcode");

  SDLoc dl(Op);

  if (Op.getValueType() == MVT::f64)
    return LowerF64Op(Op.getOperand(0), dl, DAG, Op.getOpcode());
  if (Op.getValueType() != MVT::f128)
    return Op;

  // Lower fabs/fneg on f128 to fabs/fneg on f64
  // fabs/fneg f128 => fabs/fneg f64:sub_even64, fmov f64:sub_odd64
  // (As with LowerF64Op, on little-endian, we need to negate the odd
  // subreg)

  SDValue SrcReg128 = Op.getOperand(0);
  SDValue Hi64 = DAG.getTargetExtractSubreg(SP::sub_even64, dl, MVT::f64,
                                            SrcReg128);
  SDValue Lo64 = DAG.getTargetExtractSubreg(SP::sub_odd64, dl, MVT::f64,
                                            SrcReg128);

  if (DAG.getDataLayout().isLittleEndian()) {
    if (isV9)
      Lo64 = DAG.getNode(Op.getOpcode(), dl, MVT::f64, Lo64);
    else
      Lo64 = LowerF64Op(Lo64, dl, DAG, Op.getOpcode());
  } else {
    if (isV9)
      Hi64 = DAG.getNode(Op.getOpcode(), dl, MVT::f64, Hi64);
    else
      Hi64 = LowerF64Op(Hi64, dl, DAG, Op.getOpcode());
  }

  SDValue DstReg128 = SDValue(DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF,
                                                 dl, MVT::f128), 0);
  DstReg128 = DAG.getTargetInsertSubreg(SP::sub_even64, dl, MVT::f128,
                                        DstReg128, Hi64);
  DstReg128 = DAG.getTargetInsertSubreg(SP::sub_odd64, dl, MVT::f128,
                                        DstReg128, Lo64);
  return DstReg128;
}

static SDValue LowerADDC_ADDE_SUBC_SUBE(SDValue Op, SelectionDAG &DAG) {

  if (Op.getValueType() != MVT::i64)
    return Op;

  SDLoc dl(Op);
  SDValue Src1 = Op.getOperand(0);
  SDValue Src1Lo = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Src1);
  SDValue Src1Hi = DAG.getNode(ISD::SRL, dl, MVT::i64, Src1,
                               DAG.getConstant(32, dl, MVT::i64));
  Src1Hi = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Src1Hi);

  SDValue Src2 = Op.getOperand(1);
  SDValue Src2Lo = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Src2);
  SDValue Src2Hi = DAG.getNode(ISD::SRL, dl, MVT::i64, Src2,
                               DAG.getConstant(32, dl, MVT::i64));
  Src2Hi = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Src2Hi);


  bool hasChain = false;
  unsigned hiOpc = Op.getOpcode();
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Invalid opcode");
  case ISD::ADDC: hiOpc = ISD::ADDE; break;
  case ISD::ADDE: hasChain = true; break;
  case ISD::SUBC: hiOpc = ISD::SUBE; break;
  case ISD::SUBE: hasChain = true; break;
  }
  SDValue Lo;
  SDVTList VTs = DAG.getVTList(MVT::i32, MVT::Glue);
  if (hasChain) {
    Lo = DAG.getNode(Op.getOpcode(), dl, VTs, Src1Lo, Src2Lo,
                     Op.getOperand(2));
  } else {
    Lo = DAG.getNode(Op.getOpcode(), dl, VTs, Src1Lo, Src2Lo);
  }
  SDValue Hi = DAG.getNode(hiOpc, dl, VTs, Src1Hi, Src2Hi, Lo.getValue(1));
  SDValue Carry = Hi.getValue(1);

  Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i64, Lo);
  Hi = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i64, Hi);
  Hi = DAG.getNode(ISD::SHL, dl, MVT::i64, Hi,
                   DAG.getConstant(32, dl, MVT::i64));

  SDValue Dst = DAG.getNode(ISD::OR, dl, MVT::i64, Hi, Lo);
  SDValue Ops[2] = { Dst, Carry };
  return DAG.getMergeValues(Ops, dl);
}

// Custom lower UMULO/SMULO for SPARC. This code is similar to ExpandNode()
// in LegalizeDAG.cpp except the order of arguments to the library function.
static SDValue LowerUMULO_SMULO(SDValue Op, SelectionDAG &DAG,
                                const SparcTargetLowering &TLI)
{
  unsigned opcode = Op.getOpcode();
  assert((opcode == ISD::UMULO || opcode == ISD::SMULO) && "Invalid Opcode.");

  bool isSigned = (opcode == ISD::SMULO);
  EVT VT = MVT::i64;
  EVT WideVT = MVT::i128;
  SDLoc dl(Op);
  SDValue LHS = Op.getOperand(0);

  if (LHS.getValueType() != VT)
    return Op;

  SDValue ShiftAmt = DAG.getConstant(63, dl, VT);

  SDValue RHS = Op.getOperand(1);
  SDValue HiLHS = DAG.getNode(ISD::SRA, dl, VT, LHS, ShiftAmt);
  SDValue HiRHS = DAG.getNode(ISD::SRA, dl, MVT::i64, RHS, ShiftAmt);
  SDValue Args[] = { HiLHS, LHS, HiRHS, RHS };

  SDValue MulResult = TLI.makeLibCall(DAG,
                                      RTLIB::MUL_I128, WideVT,
                                      Args, isSigned, dl).first;
  SDValue BottomHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT,
                                   MulResult, DAG.getIntPtrConstant(0, dl));
  SDValue TopHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT,
                                MulResult, DAG.getIntPtrConstant(1, dl));
  if (isSigned) {
    SDValue Tmp1 = DAG.getNode(ISD::SRA, dl, VT, BottomHalf, ShiftAmt);
    TopHalf = DAG.getSetCC(dl, MVT::i32, TopHalf, Tmp1, ISD::SETNE);
  } else {
    TopHalf = DAG.getSetCC(dl, MVT::i32, TopHalf, DAG.getConstant(0, dl, VT),
                           ISD::SETNE);
  }
  // MulResult is a node with an illegal type. Because such things are not
  // generally permitted during this phase of legalization, ensure that
  // nothing is left using the node. The above EXTRACT_ELEMENT nodes should have
  // been folded.
  assert(MulResult->use_empty() && "Illegally typed node still in use!");

  SDValue Ops[2] = { BottomHalf, TopHalf } ;
  return DAG.getMergeValues(Ops, dl);
}

static SDValue LowerATOMIC_LOAD_STORE(SDValue Op, SelectionDAG &DAG) {
  if (isStrongerThanMonotonic(cast<AtomicSDNode>(Op)->getOrdering()))
  // Expand with a fence.
  return SDValue();

  // Monotonic load/stores are legal.
  return Op;
}

SDValue SparcTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                     SelectionDAG &DAG) const {
  unsigned IntNo = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();
  SDLoc dl(Op);
  switch (IntNo) {
  default: return SDValue();    // Don't custom lower most intrinsics.
  case Intrinsic::thread_pointer: {
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    return DAG.getRegister(SP::G7, PtrVT);
  }
  }
}

SDValue SparcTargetLowering::
LowerOperation(SDValue Op, SelectionDAG &DAG) const {

  bool hasHardQuad = Subtarget->hasHardQuad();
  bool isV9        = Subtarget->isV9();

  switch (Op.getOpcode()) {
  default: llvm_unreachable("Should not custom lower this!");

  case ISD::RETURNADDR:         return LowerRETURNADDR(Op, DAG, *this,
                                                       Subtarget);
  case ISD::FRAMEADDR:          return LowerFRAMEADDR(Op, DAG,
                                                      Subtarget);
  case ISD::GlobalTLSAddress:   return LowerGlobalTLSAddress(Op, DAG);
  case ISD::GlobalAddress:      return LowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:       return LowerBlockAddress(Op, DAG);
  case ISD::ConstantPool:       return LowerConstantPool(Op, DAG);
  case ISD::FP_TO_SINT:         return LowerFP_TO_SINT(Op, DAG, *this,
                                                       hasHardQuad);
  case ISD::SINT_TO_FP:         return LowerSINT_TO_FP(Op, DAG, *this,
                                                       hasHardQuad);
  case ISD::FP_TO_UINT:         return LowerFP_TO_UINT(Op, DAG, *this,
                                                       hasHardQuad);
  case ISD::UINT_TO_FP:         return LowerUINT_TO_FP(Op, DAG, *this,
                                                       hasHardQuad);
  case ISD::BR_CC:              return LowerBR_CC(Op, DAG, *this,
                                                  hasHardQuad);
  case ISD::SELECT_CC:          return LowerSELECT_CC(Op, DAG, *this,
                                                      hasHardQuad);
  case ISD::VASTART:            return LowerVASTART(Op, DAG, *this);
  case ISD::VAARG:              return LowerVAARG(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: return LowerDYNAMIC_STACKALLOC(Op, DAG,
                                                               Subtarget);

  case ISD::LOAD:               return LowerLOAD(Op, DAG);
  case ISD::STORE:              return LowerSTORE(Op, DAG);
  case ISD::FADD:               return LowerF128Op(Op, DAG,
                                       getLibcallName(RTLIB::ADD_F128), 2);
  case ISD::FSUB:               return LowerF128Op(Op, DAG,
                                       getLibcallName(RTLIB::SUB_F128), 2);
  case ISD::FMUL:               return LowerF128Op(Op, DAG,
                                       getLibcallName(RTLIB::MUL_F128), 2);
  case ISD::FDIV:               return LowerF128Op(Op, DAG,
                                       getLibcallName(RTLIB::DIV_F128), 2);
  case ISD::FSQRT:              return LowerF128Op(Op, DAG,
                                       getLibcallName(RTLIB::SQRT_F128),1);
  case ISD::FABS:
  case ISD::FNEG:               return LowerFNEGorFABS(Op, DAG, isV9);
  case ISD::FP_EXTEND:          return LowerF128_FPEXTEND(Op, DAG, *this);
  case ISD::FP_ROUND:           return LowerF128_FPROUND(Op, DAG, *this);
  case ISD::ADDC:
  case ISD::ADDE:
  case ISD::SUBC:
  case ISD::SUBE:               return LowerADDC_ADDE_SUBC_SUBE(Op, DAG);
  case ISD::UMULO:
  case ISD::SMULO:              return LowerUMULO_SMULO(Op, DAG, *this);
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE:       return LowerATOMIC_LOAD_STORE(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  }
}

SDValue SparcTargetLowering::bitcastConstantFPToInt(ConstantFPSDNode *C,
                                                    const SDLoc &DL,
                                                    SelectionDAG &DAG) const {
  APInt V = C->getValueAPF().bitcastToAPInt();
  SDValue Lo = DAG.getConstant(V.zextOrTrunc(32), DL, MVT::i32);
  SDValue Hi = DAG.getConstant(V.lshr(32).zextOrTrunc(32), DL, MVT::i32);
  if (DAG.getDataLayout().isLittleEndian())
    std::swap(Lo, Hi);
  return DAG.getBuildVector(MVT::v2i32, DL, {Hi, Lo});
}

SDValue SparcTargetLowering::PerformBITCASTCombine(SDNode *N,
                                                   DAGCombinerInfo &DCI) const {
  SDLoc dl(N);
  SDValue Src = N->getOperand(0);

  if (isa<ConstantFPSDNode>(Src) && N->getSimpleValueType(0) == MVT::v2i32 &&
      Src.getSimpleValueType() == MVT::f64)
    return bitcastConstantFPToInt(cast<ConstantFPSDNode>(Src), dl, DCI.DAG);

  return SDValue();
}

SDValue SparcTargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  default:
    break;
  case ISD::BITCAST:
    return PerformBITCASTCombine(N, DCI);
  }
  return SDValue();
}

MachineBasicBlock *
SparcTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default: llvm_unreachable("Unknown SELECT_CC!");
  case SP::SELECT_CC_Int_ICC:
  case SP::SELECT_CC_FP_ICC:
  case SP::SELECT_CC_DFP_ICC:
  case SP::SELECT_CC_QFP_ICC:
    return expandSelectCC(MI, BB, SP::BCOND);
  case SP::SELECT_CC_Int_FCC:
  case SP::SELECT_CC_FP_FCC:
  case SP::SELECT_CC_DFP_FCC:
  case SP::SELECT_CC_QFP_FCC:
    return expandSelectCC(MI, BB, SP::FBCOND);
  }
}

MachineBasicBlock *
SparcTargetLowering::expandSelectCC(MachineInstr &MI, MachineBasicBlock *BB,
                                    unsigned BROpcode) const {
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  unsigned CC = (SPCC::CondCodes)MI.getOperand(3).getImm();

  // To "insert" a SELECT_CC instruction, we actually have to insert the
  // triangle control-flow pattern. The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and the condition code for the branch.
  //
  // We produce the following control flow:
  //     ThisMBB
  //     |  \
  //     |  IfFalseMBB
  //     | /
  //    SinkMBB
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  MachineBasicBlock *ThisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *IfFalseMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *SinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, IfFalseMBB);
  F->insert(It, SinkMBB);

  // Transfer the remainder of ThisMBB and its successor edges to SinkMBB.
  SinkMBB->splice(SinkMBB->begin(), ThisMBB,
                  std::next(MachineBasicBlock::iterator(MI)), ThisMBB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(ThisMBB);

  // Set the new successors for ThisMBB.
  ThisMBB->addSuccessor(IfFalseMBB);
  ThisMBB->addSuccessor(SinkMBB);

  BuildMI(ThisMBB, dl, TII.get(BROpcode))
    .addMBB(SinkMBB)
    .addImm(CC);

  // IfFalseMBB just falls through to SinkMBB.
  IfFalseMBB->addSuccessor(SinkMBB);

  // %Result = phi [ %TrueValue, ThisMBB ], [ %FalseValue, IfFalseMBB ]
  BuildMI(*SinkMBB, SinkMBB->begin(), dl, TII.get(SP::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(1).getReg())
      .addMBB(ThisMBB)
      .addReg(MI.getOperand(2).getReg())
      .addMBB(IfFalseMBB);

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return SinkMBB;
}

//===----------------------------------------------------------------------===//
//                         Sparc Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
SparcTargetLowering::ConstraintType
SparcTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:  break;
    case 'r':
    case 'f':
    case 'e':
      return C_RegisterClass;
    case 'I': // SIMM13
      return C_Other;
    }
  }

  return TargetLowering::getConstraintType(Constraint);
}

TargetLowering::ConstraintWeight SparcTargetLowering::
getSingleConstraintMatchWeight(AsmOperandInfo &info,
                               const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
  // If we don't have a value, we can't do a match,
  // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;

  // Look at the constraint type.
  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'I': // SIMM13
    if (ConstantInt *C = dyn_cast<ConstantInt>(info.CallOperandVal)) {
      if (isInt<13>(C->getSExtValue()))
        weight = CW_Constant;
    }
    break;
  }
  return weight;
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void SparcTargetLowering::
LowerAsmOperandForConstraint(SDValue Op,
                             std::string &Constraint,
                             std::vector<SDValue> &Ops,
                             SelectionDAG &DAG) const {
  SDValue Result(nullptr, 0);

  // Only support length 1 constraints for now.
  if (Constraint.length() > 1)
    return;

  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default: break;
  case 'I':
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      if (isInt<13>(C->getSExtValue())) {
        Result = DAG.getTargetConstant(C->getSExtValue(), SDLoc(Op),
                                       Op.getValueType());
        break;
      }
      return;
    }
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

std::pair<unsigned, const TargetRegisterClass *>
SparcTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      if (VT == MVT::v2i32)
        return std::make_pair(0U, &SP::IntPairRegClass);
      else
        return std::make_pair(0U, &SP::IntRegsRegClass);
    case 'f':
      if (VT == MVT::f32 || VT == MVT::i32)
        return std::make_pair(0U, &SP::FPRegsRegClass);
      else if (VT == MVT::f64 || VT == MVT::i64)
        return std::make_pair(0U, &SP::LowDFPRegsRegClass);
      else if (VT == MVT::f128)
        return std::make_pair(0U, &SP::LowQFPRegsRegClass);
      // This will generate an error message
      return std::make_pair(0U, nullptr);
    case 'e':
      if (VT == MVT::f32 || VT == MVT::i32)
        return std::make_pair(0U, &SP::FPRegsRegClass);
      else if (VT == MVT::f64 || VT == MVT::i64 )
        return std::make_pair(0U, &SP::DFPRegsRegClass);
      else if (VT == MVT::f128)
        return std::make_pair(0U, &SP::QFPRegsRegClass);
      // This will generate an error message
      return std::make_pair(0U, nullptr);
    }
  } else if (!Constraint.empty() && Constraint.size() <= 5
              && Constraint[0] == '{' && *(Constraint.end()-1) == '}') {
    // constraint = '{r<d>}'
    // Remove the braces from around the name.
    StringRef name(Constraint.data()+1, Constraint.size()-2);
    // Handle register aliases:
    //       r0-r7   -> g0-g7
    //       r8-r15  -> o0-o7
    //       r16-r23 -> l0-l7
    //       r24-r31 -> i0-i7
    uint64_t intVal = 0;
    if (name.substr(0, 1).equals("r")
        && !name.substr(1).getAsInteger(10, intVal) && intVal <= 31) {
      const char regTypes[] = { 'g', 'o', 'l', 'i' };
      char regType = regTypes[intVal/8];
      char regIdx = '0' + (intVal % 8);
      char tmp[] = { '{', regType, regIdx, '}', 0 };
      std::string newConstraint = std::string(tmp);
      return TargetLowering::getRegForInlineAsmConstraint(TRI, newConstraint,
                                                          VT);
    }
    if (name.substr(0, 1).equals("f") &&
        !name.substr(1).getAsInteger(10, intVal) && intVal <= 63) {
      std::string newConstraint;

      if (VT == MVT::f32 || VT == MVT::Other) {
        newConstraint = "{f" + utostr(intVal) + "}";
      } else if (VT == MVT::f64 && (intVal % 2 == 0)) {
        newConstraint = "{d" + utostr(intVal / 2) + "}";
      } else if (VT == MVT::f128 && (intVal % 4 == 0)) {
        newConstraint = "{q" + utostr(intVal / 4) + "}";
      } else {
        return std::make_pair(0U, nullptr);
      }
      return TargetLowering::getRegForInlineAsmConstraint(TRI, newConstraint,
                                                          VT);
    }
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

bool
SparcTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The Sparc target isn't yet aware of offsets.
  return false;
}

void SparcTargetLowering::ReplaceNodeResults(SDNode *N,
                                             SmallVectorImpl<SDValue>& Results,
                                             SelectionDAG &DAG) const {

  SDLoc dl(N);

  RTLIB::Libcall libCall = RTLIB::UNKNOWN_LIBCALL;

  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Do not know how to custom type legalize this operation!");

  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    // Custom lower only if it involves f128 or i64.
    if (N->getOperand(0).getValueType() != MVT::f128
        || N->getValueType(0) != MVT::i64)
      return;
    libCall = ((N->getOpcode() == ISD::FP_TO_SINT)
               ? RTLIB::FPTOSINT_F128_I64
               : RTLIB::FPTOUINT_F128_I64);

    Results.push_back(LowerF128Op(SDValue(N, 0),
                                  DAG,
                                  getLibcallName(libCall),
                                  1));
    return;
  case ISD::READCYCLECOUNTER: {
    assert(Subtarget->hasLeonCycleCounter());
    SDValue Lo = DAG.getCopyFromReg(N->getOperand(0), dl, SP::ASR23, MVT::i32);
    SDValue Hi = DAG.getCopyFromReg(Lo, dl, SP::G0, MVT::i32);
    SDValue Ops[] = { Lo, Hi };
    SDValue Pair = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Ops);
    Results.push_back(Pair);
    Results.push_back(N->getOperand(0));
    return;
  }
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    // Custom lower only if it involves f128 or i64.
    if (N->getValueType(0) != MVT::f128
        || N->getOperand(0).getValueType() != MVT::i64)
      return;

    libCall = ((N->getOpcode() == ISD::SINT_TO_FP)
               ? RTLIB::SINTTOFP_I64_F128
               : RTLIB::UINTTOFP_I64_F128);

    Results.push_back(LowerF128Op(SDValue(N, 0),
                                  DAG,
                                  getLibcallName(libCall),
                                  1));
    return;
  case ISD::LOAD: {
    LoadSDNode *Ld = cast<LoadSDNode>(N);
    // Custom handling only for i64: turn i64 load into a v2i32 load,
    // and a bitcast.
    if (Ld->getValueType(0) != MVT::i64 || Ld->getMemoryVT() != MVT::i64)
      return;

    SDLoc dl(N);
    SDValue LoadRes = DAG.getExtLoad(
        Ld->getExtensionType(), dl, MVT::v2i32, Ld->getChain(),
        Ld->getBasePtr(), Ld->getPointerInfo(), MVT::v2i32, Ld->getAlignment(),
        Ld->getMemOperand()->getFlags(), Ld->getAAInfo());

    SDValue Res = DAG.getNode(ISD::BITCAST, dl, MVT::i64, LoadRes);
    Results.push_back(Res);
    Results.push_back(LoadRes.getValue(1));
    return;
  }
  }
}

// Override to enable LOAD_STACK_GUARD lowering on Linux.
bool SparcTargetLowering::useLoadStackGuardNode() const {
  if (!Subtarget->isTargetLinux())
    return TargetLowering::useLoadStackGuardNode();
  return true;
}

// Override to disable global variable loading on Linux.
void SparcTargetLowering::insertSSPDeclarations(Module &M) const {
  if (!Subtarget->isTargetLinux())
    return TargetLowering::insertSSPDeclarations(M);
}
