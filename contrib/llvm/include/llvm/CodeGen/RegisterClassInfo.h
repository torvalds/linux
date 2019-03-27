//===- RegisterClassInfo.h - Dynamic Register Class Info --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the RegisterClassInfo class which provides dynamic
// information about target register classes. Callee saved and reserved
// registers depends on calling conventions and other dynamic information, so
// some things cannot be determined statically.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERCLASSINFO_H
#define LLVM_CODEGEN_REGISTERCLASSINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace llvm {

class RegisterClassInfo {
  struct RCInfo {
    unsigned Tag = 0;
    unsigned NumRegs = 0;
    bool ProperSubClass = false;
    uint8_t MinCost = 0;
    uint16_t LastCostChange = 0;
    std::unique_ptr<MCPhysReg[]> Order;

    RCInfo() = default;

    operator ArrayRef<MCPhysReg>() const {
      return makeArrayRef(Order.get(), NumRegs);
    }
  };

  // Brief cached information for each register class.
  std::unique_ptr<RCInfo[]> RegClass;

  // Tag changes whenever cached information needs to be recomputed. An RCInfo
  // entry is valid when its tag matches.
  unsigned Tag = 0;

  const MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  // Callee saved registers of last MF. Assumed to be valid until the next
  // runOnFunction() call.
  // Used only to determine if an update was made to CalleeSavedAliases.
  const MCPhysReg *CalleeSavedRegs = nullptr;

  // Map register alias to the callee saved Register.
  SmallVector<MCPhysReg, 4> CalleeSavedAliases;

  // Reserved registers in the current MF.
  BitVector Reserved;

  std::unique_ptr<unsigned[]> PSetLimits;

  // Compute all information about RC.
  void compute(const TargetRegisterClass *RC) const;

  // Return an up-to-date RCInfo for RC.
  const RCInfo &get(const TargetRegisterClass *RC) const {
    const RCInfo &RCI = RegClass[RC->getID()];
    if (Tag != RCI.Tag)
      compute(RC);
    return RCI;
  }

public:
  RegisterClassInfo();

  /// runOnFunction - Prepare to answer questions about MF. This must be called
  /// before any other methods are used.
  void runOnMachineFunction(const MachineFunction &MF);

  /// getNumAllocatableRegs - Returns the number of actually allocatable
  /// registers in RC in the current function.
  unsigned getNumAllocatableRegs(const TargetRegisterClass *RC) const {
    return get(RC).NumRegs;
  }

  /// getOrder - Returns the preferred allocation order for RC. The order
  /// contains no reserved registers, and registers that alias callee saved
  /// registers come last.
  ArrayRef<MCPhysReg> getOrder(const TargetRegisterClass *RC) const {
    return get(RC);
  }

  /// isProperSubClass - Returns true if RC has a legal super-class with more
  /// allocatable registers.
  ///
  /// Register classes like GR32_NOSP are not proper sub-classes because %esp
  /// is not allocatable.  Similarly, tGPR is not a proper sub-class in Thumb
  /// mode because the GPR super-class is not legal.
  bool isProperSubClass(const TargetRegisterClass *RC) const {
    return get(RC).ProperSubClass;
  }

  /// getLastCalleeSavedAlias - Returns the last callee saved register that
  /// overlaps PhysReg, or 0 if Reg doesn't overlap a CalleeSavedAliases.
  unsigned getLastCalleeSavedAlias(unsigned PhysReg) const {
    assert(TargetRegisterInfo::isPhysicalRegister(PhysReg));
    if (PhysReg < CalleeSavedAliases.size())
      return CalleeSavedAliases[PhysReg];
    return 0;
  }

  /// Get the minimum register cost in RC's allocation order.
  /// This is the smallest value returned by TRI->getCostPerUse(Reg) for all
  /// the registers in getOrder(RC).
  unsigned getMinCost(const TargetRegisterClass *RC) {
    return get(RC).MinCost;
  }

  /// Get the position of the last cost change in getOrder(RC).
  ///
  /// All registers in getOrder(RC).slice(getLastCostChange(RC)) will have the
  /// same cost according to TRI->getCostPerUse().
  unsigned getLastCostChange(const TargetRegisterClass *RC) {
    return get(RC).LastCostChange;
  }

  /// Get the register unit limit for the given pressure set index.
  ///
  /// RegisterClassInfo adjusts this limit for reserved registers.
  unsigned getRegPressureSetLimit(unsigned Idx) const {
    if (!PSetLimits[Idx])
      PSetLimits[Idx] = computePSetLimit(Idx);
    return PSetLimits[Idx];
  }

protected:
  unsigned computePSetLimit(unsigned Idx) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERCLASSINFO_H
