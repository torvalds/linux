//===- ScheduleDAGMutation.h - MachineInstr Scheduling ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ScheduleDAGMutation class, which represents
// a target-specific mutation of the dependency graph for scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SCHEDULEDAGMUTATION_H
#define LLVM_CODEGEN_SCHEDULEDAGMUTATION_H

namespace llvm {

class ScheduleDAGInstrs;

/// Mutate the DAG as a postpass after normal DAG building.
class ScheduleDAGMutation {
  virtual void anchor();

public:
  virtual ~ScheduleDAGMutation() = default;

  virtual void apply(ScheduleDAGInstrs *DAG) = 0;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SCHEDULEDAGMUTATION_H
