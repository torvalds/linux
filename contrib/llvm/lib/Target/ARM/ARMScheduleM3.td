//=- ARMScheduleM3.td - ARM Cortex-M3 Scheduling Definitions -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the ARM Cortex-M3 processor.
//
//===----------------------------------------------------------------------===//

def CortexM3Model : SchedMachineModel {
  let IssueWidth        = 1; // Only IT can be dual-issued, so assume single-issue
  let MicroOpBufferSize = 0; // In-order
  let LoadLatency       = 2; // Latency when not pipelined, not pc-relative
  let MispredictPenalty = 2; // Best case branch taken cost

  let CompleteModel = 0;
}
