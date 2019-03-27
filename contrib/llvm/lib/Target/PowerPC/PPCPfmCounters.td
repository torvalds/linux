//===-- PPCPfmCounters.td - PPC Hardware Counters ----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the available hardware counters for PPC.
//
//===----------------------------------------------------------------------===//

def CpuCyclesPfmCounter : PfmCounter<"CYCLES">;

def DefaultPfmCounters : ProcPfmCounters {
  let CycleCounter = CpuCyclesPfmCounter;
}
def : PfmCountersDefaultBinding<DefaultPfmCounters>;
