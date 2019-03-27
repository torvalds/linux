//===-- SystemZCallingConv.h - Calling conventions for SystemZ --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZCALLINGCONV_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZCALLINGCONV_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/MC/MCRegisterInfo.h"

namespace llvm {
namespace SystemZ {
  const unsigned NumArgGPRs = 5;
  extern const MCPhysReg ArgGPRs[NumArgGPRs];

  const unsigned NumArgFPRs = 4;
  extern const MCPhysReg ArgFPRs[NumArgFPRs];
} // end namespace SystemZ

class SystemZCCState : public CCState {
private:
  /// Records whether the value was a fixed argument.
  /// See ISD::OutputArg::IsFixed.
  SmallVector<bool, 4> ArgIsFixed;

  /// Records whether the value was widened from a short vector type.
  SmallVector<bool, 4> ArgIsShortVector;

  // Check whether ArgVT is a short vector type.
  bool IsShortVectorType(EVT ArgVT) {
    return ArgVT.isVector() && ArgVT.getStoreSize() <= 8;
  }

public:
  SystemZCCState(CallingConv::ID CC, bool isVarArg, MachineFunction &MF,
                 SmallVectorImpl<CCValAssign> &locs, LLVMContext &C)
      : CCState(CC, isVarArg, MF, locs, C) {}

  void AnalyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                              CCAssignFn Fn) {
    // Formal arguments are always fixed.
    ArgIsFixed.clear();
    for (unsigned i = 0; i < Ins.size(); ++i)
      ArgIsFixed.push_back(true);
    // Record whether the call operand was a short vector.
    ArgIsShortVector.clear();
    for (unsigned i = 0; i < Ins.size(); ++i)
      ArgIsShortVector.push_back(IsShortVectorType(Ins[i].ArgVT));

    CCState::AnalyzeFormalArguments(Ins, Fn);
  }

  void AnalyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                           CCAssignFn Fn) {
    // Record whether the call operand was a fixed argument.
    ArgIsFixed.clear();
    for (unsigned i = 0; i < Outs.size(); ++i)
      ArgIsFixed.push_back(Outs[i].IsFixed);
    // Record whether the call operand was a short vector.
    ArgIsShortVector.clear();
    for (unsigned i = 0; i < Outs.size(); ++i)
      ArgIsShortVector.push_back(IsShortVectorType(Outs[i].ArgVT));

    CCState::AnalyzeCallOperands(Outs, Fn);
  }

  // This version of AnalyzeCallOperands in the base class is not usable
  // since we must provide a means of accessing ISD::OutputArg::IsFixed.
  void AnalyzeCallOperands(const SmallVectorImpl<MVT> &Outs,
                           SmallVectorImpl<ISD::ArgFlagsTy> &Flags,
                           CCAssignFn Fn) = delete;

  bool IsFixed(unsigned ValNo) { return ArgIsFixed[ValNo]; }
  bool IsShortVector(unsigned ValNo) { return ArgIsShortVector[ValNo]; }
};

// Handle i128 argument types.  These need to be passed by implicit
// reference.  This could be as simple as the following .td line:
//    CCIfType<[i128], CCPassIndirect<i64>>,
// except that i128 is not a legal type, and therefore gets split by
// common code into a pair of i64 arguments.
inline bool CC_SystemZ_I128Indirect(unsigned &ValNo, MVT &ValVT,
                                    MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags,
                                    CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // ArgFlags.isSplit() is true on the first part of a i128 argument;
  // PendingMembers.empty() is false on all subsequent parts.
  if (!ArgFlags.isSplit() && PendingMembers.empty())
    return false;

  // Push a pending Indirect value location for each part.
  LocVT = MVT::i64;
  LocInfo = CCValAssign::Indirect;
  PendingMembers.push_back(CCValAssign::getPending(ValNo, ValVT,
                                                   LocVT, LocInfo));
  if (!ArgFlags.isSplitEnd())
    return true;

  // OK, we've collected all parts in the pending list.  Allocate
  // the location (register or stack slot) for the indirect pointer.
  // (This duplicates the usual i64 calling convention rules.)
  unsigned Reg = State.AllocateReg(SystemZ::ArgGPRs);
  unsigned Offset = Reg ? 0 : State.AllocateStack(8, 8);

  // Use that same location for all the pending parts.
  for (auto &It : PendingMembers) {
    if (Reg)
      It.convertToReg(Reg);
    else
      It.convertToMem(Offset);
    State.addLoc(It);
  }

  PendingMembers.clear();

  return true;
}

} // end namespace llvm

#endif
