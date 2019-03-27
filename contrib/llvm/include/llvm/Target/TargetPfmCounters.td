//===- TargetPfmCounters.td - Target Pfm Counters -*- tablegen ----------*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces for performance counters.

// Definition of a hardware counters from libpfm identifiers.
class PfmCounter<string counter> {
  // The name of the counter that measures events.
  // The name can be "some_counter + some_other_counter", in which case the
  // measured value is the sum of events on these counters.
  string Counter = counter;
}

// Issue counters can be tied to a ProcResource
class PfmIssueCounter<string resource_name, string counter>
    : PfmCounter<counter> {
  // The name of the ProcResource on which uops are issued. This is used by
  // llvm-exegesis to compare measurements with values in the SchedModels.
  // If the CPU has a sched model, this should correspond to the name of a
  // ProcResource.
  string ResourceName = resource_name;
}

def NoPfmCounter : PfmCounter <""> {}

// Set of PfmCounters for measuring sched model characteristics.
class ProcPfmCounters {
  // Processors can define how to measure cycles by defining a CycleCounter.
  PfmCounter CycleCounter = NoPfmCounter;
  // Processors can define how to measure uops by defining a UopsCounter.
  PfmCounter UopsCounter = NoPfmCounter;
  // Processors can define how to measure issued uops by defining IssueCounters.
  list<PfmIssueCounter> IssueCounters = [];
}

// A binding of a set of counters to a CPU.
class PfmCountersBinding<string cpu_name, ProcPfmCounters counters> {
  string CpuName = cpu_name;
  ProcPfmCounters Counters = counters;
}

// Declares the default binding for unbound CPUs for the target.
class PfmCountersDefaultBinding<ProcPfmCounters counters>
    : PfmCountersBinding<"", counters> {}
