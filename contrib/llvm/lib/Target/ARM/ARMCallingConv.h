//=== ARMCallingConv.h - ARM Custom Calling Convention Routines -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the ARM Calling Convention that
// aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMCALLINGCONV_H
#define LLVM_LIB_TARGET_ARM_ARMCALLINGCONV_H

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

// APCS f64 is in register pairs, possibly split to stack
static bool f64AssignAPCS(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                          CCValAssign::LocInfo &LocInfo,
                          CCState &State, bool CanFail) {
  static const MCPhysReg RegList[] = { ARM::R0, ARM::R1, ARM::R2, ARM::R3 };

  // Try to get the first register.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else {
    // For the 2nd half of a v2f64, do not fail.
    if (CanFail)
      return false;

    // Put the whole thing on the stack.
    State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                           State.AllocateStack(8, 4),
                                           LocVT, LocInfo));
    return true;
  }

  // Try to get the second register.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else
    State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                           State.AllocateStack(4, 4),
                                           LocVT, LocInfo));
  return true;
}

static bool CC_ARM_APCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                   CCValAssign::LocInfo &LocInfo,
                                   ISD::ArgFlagsTy &ArgFlags,
                                   CCState &State) {
  if (!f64AssignAPCS(ValNo, ValVT, LocVT, LocInfo, State, true))
    return false;
  if (LocVT == MVT::v2f64 &&
      !f64AssignAPCS(ValNo, ValVT, LocVT, LocInfo, State, false))
    return false;
  return true;  // we handled it
}

// AAPCS f64 is in aligned register pairs
static bool f64AssignAAPCS(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                           CCValAssign::LocInfo &LocInfo,
                           CCState &State, bool CanFail) {
  static const MCPhysReg HiRegList[] = { ARM::R0, ARM::R2 };
  static const MCPhysReg LoRegList[] = { ARM::R1, ARM::R3 };
  static const MCPhysReg ShadowRegList[] = { ARM::R0, ARM::R1 };
  static const MCPhysReg GPRArgRegs[] = { ARM::R0, ARM::R1, ARM::R2, ARM::R3 };

  unsigned Reg = State.AllocateReg(HiRegList, ShadowRegList);
  if (Reg == 0) {

    // If we had R3 unallocated only, now we still must to waste it.
    Reg = State.AllocateReg(GPRArgRegs);
    assert((!Reg || Reg == ARM::R3) && "Wrong GPRs usage for f64");

    // For the 2nd half of a v2f64, do not just fail.
    if (CanFail)
      return false;

    // Put the whole thing on the stack.
    State.addLoc(CCValAssign::getCustomMem(ValNo, ValVT,
                                           State.AllocateStack(8, 8),
                                           LocVT, LocInfo));
    return true;
  }

  unsigned i;
  for (i = 0; i < 2; ++i)
    if (HiRegList[i] == Reg)
      break;

  unsigned T = State.AllocateReg(LoRegList[i]);
  (void)T;
  assert(T == LoRegList[i] && "Could not allocate register");

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         LocVT, LocInfo));
  return true;
}

static bool CC_ARM_AAPCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags,
                                    CCState &State) {
  if (!f64AssignAAPCS(ValNo, ValVT, LocVT, LocInfo, State, true))
    return false;
  if (LocVT == MVT::v2f64 &&
      !f64AssignAAPCS(ValNo, ValVT, LocVT, LocInfo, State, false))
    return false;
  return true;  // we handled it
}

static bool f64RetAssign(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                         CCValAssign::LocInfo &LocInfo, CCState &State) {
  static const MCPhysReg HiRegList[] = { ARM::R0, ARM::R2 };
  static const MCPhysReg LoRegList[] = { ARM::R1, ARM::R3 };

  unsigned Reg = State.AllocateReg(HiRegList, LoRegList);
  if (Reg == 0)
    return false; // we didn't handle it

  unsigned i;
  for (i = 0; i < 2; ++i)
    if (HiRegList[i] == Reg)
      break;

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         LocVT, LocInfo));
  return true;
}

static bool RetCC_ARM_APCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                      CCValAssign::LocInfo &LocInfo,
                                      ISD::ArgFlagsTy &ArgFlags,
                                      CCState &State) {
  if (!f64RetAssign(ValNo, ValVT, LocVT, LocInfo, State))
    return false;
  if (LocVT == MVT::v2f64 && !f64RetAssign(ValNo, ValVT, LocVT, LocInfo, State))
    return false;
  return true;  // we handled it
}

static bool RetCC_ARM_AAPCS_Custom_f64(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                       CCValAssign::LocInfo &LocInfo,
                                       ISD::ArgFlagsTy &ArgFlags,
                                       CCState &State) {
  return RetCC_ARM_APCS_Custom_f64(ValNo, ValVT, LocVT, LocInfo, ArgFlags,
                                   State);
}

static const MCPhysReg RRegList[] = { ARM::R0,  ARM::R1,  ARM::R2,  ARM::R3 };

static const MCPhysReg SRegList[] = { ARM::S0,  ARM::S1,  ARM::S2,  ARM::S3,
                                      ARM::S4,  ARM::S5,  ARM::S6,  ARM::S7,
                                      ARM::S8,  ARM::S9,  ARM::S10, ARM::S11,
                                      ARM::S12, ARM::S13, ARM::S14,  ARM::S15 };
static const MCPhysReg DRegList[] = { ARM::D0, ARM::D1, ARM::D2, ARM::D3,
                                      ARM::D4, ARM::D5, ARM::D6, ARM::D7 };
static const MCPhysReg QRegList[] = { ARM::Q0, ARM::Q1, ARM::Q2, ARM::Q3 };


// Allocate part of an AAPCS HFA or HVA. We assume that each member of the HA
// has InConsecutiveRegs set, and that the last member also has
// InConsecutiveRegsLast set. We must process all members of the HA before
// we can allocate it, as we need to know the total number of registers that
// will be needed in order to (attempt to) allocate a contiguous block.
static bool CC_ARM_AAPCS_Custom_Aggregate(unsigned &ValNo, MVT &ValVT,
                                          MVT &LocVT,
                                          CCValAssign::LocInfo &LocInfo,
                                          ISD::ArgFlagsTy &ArgFlags,
                                          CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // AAPCS HFAs must have 1-4 elements, all of the same type
  if (PendingMembers.size() > 0)
    assert(PendingMembers[0].getLocVT() == LocVT);

  // Add the argument to the list to be allocated once we know the size of the
  // aggregate. Store the type's required alignmnent as extra info for later: in
  // the [N x i64] case all trace has been removed by the time we actually get
  // to do allocation.
  PendingMembers.push_back(CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo,
                                                   ArgFlags.getOrigAlign()));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  // Try to allocate a contiguous block of registers, each of the correct
  // size to hold one member.
  auto &DL = State.getMachineFunction().getDataLayout();
  unsigned StackAlign = DL.getStackAlignment();
  unsigned Align = std::min(PendingMembers[0].getExtraInfo(), StackAlign);

  ArrayRef<MCPhysReg> RegList;
  switch (LocVT.SimpleTy) {
  case MVT::i32: {
    RegList = RRegList;
    unsigned RegIdx = State.getFirstUnallocated(RegList);

    // First consume all registers that would give an unaligned object. Whether
    // we go on stack or in regs, no-one will be using them in future.
    unsigned RegAlign = alignTo(Align, 4) / 4;
    while (RegIdx % RegAlign != 0 && RegIdx < RegList.size())
      State.AllocateReg(RegList[RegIdx++]);

    break;
  }
  case MVT::f16:
  case MVT::f32:
    RegList = SRegList;
    break;
  case MVT::v4f16:
  case MVT::f64:
    RegList = DRegList;
    break;
  case MVT::v8f16:
  case MVT::v2f64:
    RegList = QRegList;
    break;
  default:
    llvm_unreachable("Unexpected member type for block aggregate");
    break;
  }

  unsigned RegResult = State.AllocateRegBlock(RegList, PendingMembers.size());
  if (RegResult) {
    for (SmallVectorImpl<CCValAssign>::iterator It = PendingMembers.begin();
         It != PendingMembers.end(); ++It) {
      It->convertToReg(RegResult);
      State.addLoc(*It);
      ++RegResult;
    }
    PendingMembers.clear();
    return true;
  }

  // Register allocation failed, we'll be needing the stack
  unsigned Size = LocVT.getSizeInBits() / 8;
  if (LocVT == MVT::i32 && State.getNextStackOffset() == 0) {
    // If nothing else has used the stack until this point, a non-HFA aggregate
    // can be split between regs and stack.
    unsigned RegIdx = State.getFirstUnallocated(RegList);
    for (auto &It : PendingMembers) {
      if (RegIdx >= RegList.size())
        It.convertToMem(State.AllocateStack(Size, Size));
      else
        It.convertToReg(State.AllocateReg(RegList[RegIdx++]));

      State.addLoc(It);
    }
    PendingMembers.clear();
    return true;
  } else if (LocVT != MVT::i32)
    RegList = SRegList;

  // Mark all regs as unavailable (AAPCS rule C.2.vfp for VFP, C.6 for core)
  for (auto Reg : RegList)
    State.AllocateReg(Reg);

  // After the first item has been allocated, the rest are packed as tightly as
  // possible. (E.g. an incoming i64 would have starting Align of 8, but we'll
  // be allocating a bunch of i32 slots).
  unsigned RestAlign = std::min(Align, Size);

  for (auto &It : PendingMembers) {
    It.convertToMem(State.AllocateStack(Size, Align));
    State.addLoc(It);
    Align = RestAlign;
  }

  // All pending members have now been allocated
  PendingMembers.clear();

  // This will be allocated by the last member of the aggregate
  return true;
}

} // End llvm namespace

#endif
