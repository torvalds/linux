//===----------------------- DispatchStage.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file models the dispatch component of an instruction pipeline.
///
/// The DispatchStage is responsible for updating instruction dependencies
/// and communicating to the simulated instruction scheduler that an instruction
/// is ready to be scheduled for execution.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_DISPATCH_STAGE_H
#define LLVM_MCA_DISPATCH_STAGE_H

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/HWEventListener.h"
#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/HardwareUnits/RetireControlUnit.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

// Implements the hardware dispatch logic.
//
// This class is responsible for the dispatch stage, in which instructions are
// dispatched in groups to the Scheduler.  An instruction can be dispatched if
// the following conditions are met:
//  1) There are enough entries in the reorder buffer (see class
//     RetireControlUnit) to write the opcodes associated with the instruction.
//  2) There are enough physical registers to rename output register operands.
//  3) There are enough entries available in the used buffered resource(s).
//
// The number of micro opcodes that can be dispatched in one cycle is limited by
// the value of field 'DispatchWidth'. A "dynamic dispatch stall" occurs when
// processor resources are not available. Dispatch stall events are counted
// during the entire execution of the code, and displayed by the performance
// report when flag '-dispatch-stats' is specified.
//
// If the number of micro opcodes exceedes DispatchWidth, then the instruction
// is dispatched in multiple cycles.
class DispatchStage final : public Stage {
  unsigned DispatchWidth;
  unsigned AvailableEntries;
  unsigned CarryOver;
  InstRef CarriedOver;
  const MCSubtargetInfo &STI;
  RetireControlUnit &RCU;
  RegisterFile &PRF;

  bool checkRCU(const InstRef &IR) const;
  bool checkPRF(const InstRef &IR) const;
  bool canDispatch(const InstRef &IR) const;
  Error dispatch(InstRef IR);

  void updateRAWDependencies(ReadState &RS, const MCSubtargetInfo &STI);

  void notifyInstructionDispatched(const InstRef &IR,
                                   ArrayRef<unsigned> UsedPhysRegs,
                                   unsigned uOps) const;

public:
  DispatchStage(const MCSubtargetInfo &Subtarget, const MCRegisterInfo &MRI,
                unsigned MaxDispatchWidth, RetireControlUnit &R,
                RegisterFile &F)
      : DispatchWidth(MaxDispatchWidth), AvailableEntries(MaxDispatchWidth),
        CarryOver(0U), CarriedOver(), STI(Subtarget), RCU(R), PRF(F) {}

  bool isAvailable(const InstRef &IR) const override;

  // The dispatch logic internally doesn't buffer instructions. So there is
  // never work to do at the beginning of every cycle.
  bool hasWorkToComplete() const override { return false; }
  Error cycleStart() override;
  Error execute(InstRef &IR) override;

#ifndef NDEBUG
  void dump() const;
#endif
};
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_DISPATCH_STAGE_H
