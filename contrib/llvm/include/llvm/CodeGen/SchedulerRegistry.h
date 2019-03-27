//===- llvm/CodeGen/SchedulerRegistry.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for instruction scheduler function
// pass registry (RegisterScheduler).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULERREGISTRY_H
#define LLVM_CODEGEN_SCHEDULERREGISTRY_H

#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {

//===----------------------------------------------------------------------===//
///
/// RegisterScheduler class - Track the registration of instruction schedulers.
///
//===----------------------------------------------------------------------===//

class ScheduleDAGSDNodes;
class SelectionDAGISel;

class RegisterScheduler
    : public MachinePassRegistryNode<
          ScheduleDAGSDNodes *(*)(SelectionDAGISel *, CodeGenOpt::Level)> {
public:
  using FunctionPassCtor = ScheduleDAGSDNodes *(*)(SelectionDAGISel*,
                                                   CodeGenOpt::Level);

  static MachinePassRegistry<FunctionPassCtor> Registry;

  RegisterScheduler(const char *N, const char *D, FunctionPassCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }
  ~RegisterScheduler() { Registry.Remove(this); }


  // Accessors.
  RegisterScheduler *getNext() const {
    return (RegisterScheduler *)MachinePassRegistryNode::getNext();
  }

  static RegisterScheduler *getList() {
    return (RegisterScheduler *)Registry.getList();
  }

  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

/// createBURRListDAGScheduler - This creates a bottom up register usage
/// reduction list scheduler.
ScheduleDAGSDNodes *createBURRListDAGScheduler(SelectionDAGISel *IS,
                                               CodeGenOpt::Level OptLevel);

/// createBURRListDAGScheduler - This creates a bottom up list scheduler that
/// schedules nodes in source code order when possible.
ScheduleDAGSDNodes *createSourceListDAGScheduler(SelectionDAGISel *IS,
                                                 CodeGenOpt::Level OptLevel);

/// createHybridListDAGScheduler - This creates a bottom up register pressure
/// aware list scheduler that make use of latency information to avoid stalls
/// for long latency instructions in low register pressure mode. In high
/// register pressure mode it schedules to reduce register pressure.
ScheduleDAGSDNodes *createHybridListDAGScheduler(SelectionDAGISel *IS,
                                                 CodeGenOpt::Level);

/// createILPListDAGScheduler - This creates a bottom up register pressure
/// aware list scheduler that tries to increase instruction level parallelism
/// in low register pressure mode. In high register pressure mode it schedules
/// to reduce register pressure.
ScheduleDAGSDNodes *createILPListDAGScheduler(SelectionDAGISel *IS,
                                              CodeGenOpt::Level);

/// createFastDAGScheduler - This creates a "fast" scheduler.
///
ScheduleDAGSDNodes *createFastDAGScheduler(SelectionDAGISel *IS,
                                           CodeGenOpt::Level OptLevel);

/// createVLIWDAGScheduler - Scheduler for VLIW targets. This creates top down
/// DFA driven list scheduler with clustering heuristic to control
/// register pressure.
ScheduleDAGSDNodes *createVLIWDAGScheduler(SelectionDAGISel *IS,
                                           CodeGenOpt::Level OptLevel);
/// createDefaultScheduler - This creates an instruction scheduler appropriate
/// for the target.
ScheduleDAGSDNodes *createDefaultScheduler(SelectionDAGISel *IS,
                                           CodeGenOpt::Level OptLevel);

/// createDAGLinearizer - This creates a "no-scheduling" scheduler which
/// linearize the DAG using topological order.
ScheduleDAGSDNodes *createDAGLinearizer(SelectionDAGISel *IS,
                                        CodeGenOpt::Level OptLevel);

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULERREGISTRY_H
