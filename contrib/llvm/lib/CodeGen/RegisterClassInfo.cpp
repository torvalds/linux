//===- RegisterClassInfo.cpp - Dynamic Register Class Info ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the RegisterClassInfo class which provides dynamic
// information about target register classes. Callee-saved vs. caller-saved and
// reserved registers depend on calling conventions and other dynamic
// information, so some things cannot be determined statically.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

static cl::opt<unsigned>
StressRA("stress-regalloc", cl::Hidden, cl::init(0), cl::value_desc("N"),
         cl::desc("Limit all regclasses to N registers"));

RegisterClassInfo::RegisterClassInfo() = default;

void RegisterClassInfo::runOnMachineFunction(const MachineFunction &mf) {
  bool Update = false;
  MF = &mf;

  // Allocate new array the first time we see a new target.
  if (MF->getSubtarget().getRegisterInfo() != TRI) {
    TRI = MF->getSubtarget().getRegisterInfo();
    RegClass.reset(new RCInfo[TRI->getNumRegClasses()]);
    Update = true;
  }

  // Does this MF have different CSRs?
  assert(TRI && "no register info set");

  // Get the callee saved registers.
  const MCPhysReg *CSR = MF->getRegInfo().getCalleeSavedRegs();
  if (Update || CSR != CalleeSavedRegs) {
    // Build a CSRAlias map. Every CSR alias saves the last
    // overlapping CSR.
    CalleeSavedAliases.resize(TRI->getNumRegs(), 0);
    for (const MCPhysReg *I = CSR; *I; ++I)
      for (MCRegAliasIterator AI(*I, TRI, true); AI.isValid(); ++AI)
        CalleeSavedAliases[*AI] = *I;

    Update = true;
  }
  CalleeSavedRegs = CSR;

  // Different reserved registers?
  const BitVector &RR = MF->getRegInfo().getReservedRegs();
  if (Reserved.size() != RR.size() || RR != Reserved) {
    Update = true;
    Reserved = RR;
  }

  // Invalidate cached information from previous function.
  if (Update) {
    unsigned NumPSets = TRI->getNumRegPressureSets();
    PSetLimits.reset(new unsigned[NumPSets]);
    std::fill(&PSetLimits[0], &PSetLimits[NumPSets], 0);
    ++Tag;
  }
}

/// compute - Compute the preferred allocation order for RC with reserved
/// registers filtered out. Volatile registers come first followed by CSR
/// aliases ordered according to the CSR order specified by the target.
void RegisterClassInfo::compute(const TargetRegisterClass *RC) const {
  assert(RC && "no register class given");
  RCInfo &RCI = RegClass[RC->getID()];

  // Raw register count, including all reserved regs.
  unsigned NumRegs = RC->getNumRegs();

  if (!RCI.Order)
    RCI.Order.reset(new MCPhysReg[NumRegs]);

  unsigned N = 0;
  SmallVector<MCPhysReg, 16> CSRAlias;
  unsigned MinCost = 0xff;
  unsigned LastCost = ~0u;
  unsigned LastCostChange = 0;

  // FIXME: Once targets reserve registers instead of removing them from the
  // allocation order, we can simply use begin/end here.
  ArrayRef<MCPhysReg> RawOrder = RC->getRawAllocationOrder(*MF);
  for (unsigned i = 0; i != RawOrder.size(); ++i) {
    unsigned PhysReg = RawOrder[i];
    // Remove reserved registers from the allocation order.
    if (Reserved.test(PhysReg))
      continue;
    unsigned Cost = TRI->getCostPerUse(PhysReg);
    MinCost = std::min(MinCost, Cost);

    if (CalleeSavedAliases[PhysReg])
      // PhysReg aliases a CSR, save it for later.
      CSRAlias.push_back(PhysReg);
    else {
      if (Cost != LastCost)
        LastCostChange = N;
      RCI.Order[N++] = PhysReg;
      LastCost = Cost;
    }
  }
  RCI.NumRegs = N + CSRAlias.size();
  assert(RCI.NumRegs <= NumRegs && "Allocation order larger than regclass");

  // CSR aliases go after the volatile registers, preserve the target's order.
  for (unsigned i = 0, e = CSRAlias.size(); i != e; ++i) {
    unsigned PhysReg = CSRAlias[i];
    unsigned Cost = TRI->getCostPerUse(PhysReg);
    if (Cost != LastCost)
      LastCostChange = N;
    RCI.Order[N++] = PhysReg;
    LastCost = Cost;
  }

  // Register allocator stress test.  Clip register class to N registers.
  if (StressRA && RCI.NumRegs > StressRA)
    RCI.NumRegs = StressRA;

  // Check if RC is a proper sub-class.
  if (const TargetRegisterClass *Super =
          TRI->getLargestLegalSuperClass(RC, *MF))
    if (Super != RC && getNumAllocatableRegs(Super) > RCI.NumRegs)
      RCI.ProperSubClass = true;

  RCI.MinCost = uint8_t(MinCost);
  RCI.LastCostChange = LastCostChange;

  LLVM_DEBUG({
    dbgs() << "AllocationOrder(" << TRI->getRegClassName(RC) << ") = [";
    for (unsigned I = 0; I != RCI.NumRegs; ++I)
      dbgs() << ' ' << printReg(RCI.Order[I], TRI);
    dbgs() << (RCI.ProperSubClass ? " ] (sub-class)\n" : " ]\n");
  });

  // RCI is now up-to-date.
  RCI.Tag = Tag;
}

/// This is not accurate because two overlapping register sets may have some
/// nonoverlapping reserved registers. However, computing the allocation order
/// for all register classes would be too expensive.
unsigned RegisterClassInfo::computePSetLimit(unsigned Idx) const {
  const TargetRegisterClass *RC = nullptr;
  unsigned NumRCUnits = 0;
  for (const TargetRegisterClass *C : TRI->regclasses()) {
    const int *PSetID = TRI->getRegClassPressureSets(C);
    for (; *PSetID != -1; ++PSetID) {
      if ((unsigned)*PSetID == Idx)
        break;
    }
    if (*PSetID == -1)
      continue;

    // Found a register class that counts against this pressure set.
    // For efficiency, only compute the set order for the largest set.
    unsigned NUnits = TRI->getRegClassWeight(C).WeightLimit;
    if (!RC || NUnits > NumRCUnits) {
      RC = C;
      NumRCUnits = NUnits;
    }
  }
  compute(RC);
  unsigned NReserved = RC->getNumRegs() - getNumAllocatableRegs(RC);
  return TRI->getRegPressureSetLimit(*MF, Idx) -
         TRI->getRegClassWeight(RC).RegWeight * NReserved;
}
