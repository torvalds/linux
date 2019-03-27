//===-- SystemZConstantPoolValue.cpp - SystemZ constant-pool value --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZConstantPoolValue.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

SystemZConstantPoolValue::
SystemZConstantPoolValue(const GlobalValue *gv,
                         SystemZCP::SystemZCPModifier modifier)
  : MachineConstantPoolValue(gv->getType()), GV(gv), Modifier(modifier) {}

SystemZConstantPoolValue *
SystemZConstantPoolValue::Create(const GlobalValue *GV,
                                 SystemZCP::SystemZCPModifier Modifier) {
  return new SystemZConstantPoolValue(GV, Modifier);
}

int SystemZConstantPoolValue::
getExistingMachineCPValue(MachineConstantPool *CP, unsigned Alignment) {
  unsigned AlignMask = Alignment - 1;
  const std::vector<MachineConstantPoolEntry> &Constants = CP->getConstants();
  for (unsigned I = 0, E = Constants.size(); I != E; ++I) {
    if (Constants[I].isMachineConstantPoolEntry() &&
        (Constants[I].getAlignment() & AlignMask) == 0) {
      auto *ZCPV =
        static_cast<SystemZConstantPoolValue *>(Constants[I].Val.MachineCPVal);
      if (ZCPV->GV == GV && ZCPV->Modifier == Modifier)
        return I;
    }
  }
  return -1;
}

void SystemZConstantPoolValue::addSelectionDAGCSEId(FoldingSetNodeID &ID) {
  ID.AddPointer(GV);
  ID.AddInteger(Modifier);
}

void SystemZConstantPoolValue::print(raw_ostream &O) const {
  O << GV << "@" << int(Modifier);
}
