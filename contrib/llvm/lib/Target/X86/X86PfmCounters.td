//===-- X86PfmCounters.td - X86 Hardware Counters ----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the available hardware counters for various subtargets.
//
//===----------------------------------------------------------------------===//

def UnhaltedCoreCyclesPfmCounter : PfmCounter<"unhalted_core_cycles">;
def UopsIssuedPfmCounter : PfmCounter<"uops_issued:any">;

// No default counters on X86.
def DefaultPfmCounters : ProcPfmCounters {}
def : PfmCountersDefaultBinding<DefaultPfmCounters>;

// Intel X86 Counters.
def PentiumPfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"uops_retired">;
}
def : PfmCountersBinding<"pentiumpro", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium2", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium3", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium3m", PentiumPfmCounters>;
def : PfmCountersBinding<"pentium-m", PentiumPfmCounters>;

def CorePfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
}
def : PfmCountersBinding<"yonah", CorePfmCounters>;
def : PfmCountersBinding<"prescott", CorePfmCounters>;
def : PfmCountersBinding<"core2", CorePfmCounters>;
def : PfmCountersBinding<"penryn", CorePfmCounters>;
def : PfmCountersBinding<"nehalem", CorePfmCounters>;
def : PfmCountersBinding<"corei7", CorePfmCounters>;
def : PfmCountersBinding<"westmere", CorePfmCounters>;

def AtomPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
}
def : PfmCountersBinding<"bonnell", AtomPfmCounters>;
def : PfmCountersBinding<"atom", AtomPfmCounters>;

def SLMPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:any">;
}
def : PfmCountersBinding<"silvermont", SLMPfmCounters>;
def : PfmCountersBinding<"goldmont", SLMPfmCounters>;
def : PfmCountersBinding<"goldmont-plus", SLMPfmCounters>;
def : PfmCountersBinding<"tremont", SLMPfmCounters>;

def KnightPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = PfmCounter<"uops_retired:all">;
}
def : PfmCountersBinding<"knl", KnightPfmCounters>;
def : PfmCountersBinding<"knm", KnightPfmCounters>;

def SandyBridgePfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"SBPort0",  "uops_dispatched_port:port_0">,
    PfmIssueCounter<"SBPort1",  "uops_dispatched_port:port_1">,
    PfmIssueCounter<"SBPort23", "uops_dispatched_port:port_2 + uops_dispatched_port:port_3">,
    PfmIssueCounter<"SBPort4",  "uops_dispatched_port:port_4">,
    PfmIssueCounter<"SBPort5",  "uops_dispatched_port:port_5">
  ];
}
def : PfmCountersBinding<"sandybridge", SandyBridgePfmCounters>;
def : PfmCountersBinding<"ivybridge", SandyBridgePfmCounters>;

def HaswellPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"HWPort0", "uops_dispatched_port:port_0">,
    PfmIssueCounter<"HWPort1", "uops_dispatched_port:port_1">,
    PfmIssueCounter<"HWPort2", "uops_dispatched_port:port_2">,
    PfmIssueCounter<"HWPort3", "uops_dispatched_port:port_3">,
    PfmIssueCounter<"HWPort4", "uops_dispatched_port:port_4">,
    PfmIssueCounter<"HWPort5", "uops_dispatched_port:port_5">,
    PfmIssueCounter<"HWPort6", "uops_dispatched_port:port_6">,
    PfmIssueCounter<"HWPort7", "uops_dispatched_port:port_7">
  ];
}
def : PfmCountersBinding<"haswell", HaswellPfmCounters>;

def BroadwellPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"BWPort0", "uops_executed_port:port_0">,
    PfmIssueCounter<"BWPort1", "uops_executed_port:port_1">,
    PfmIssueCounter<"BWPort2", "uops_executed_port:port_2">,
    PfmIssueCounter<"BWPort3", "uops_executed_port:port_3">,
    PfmIssueCounter<"BWPort4", "uops_executed_port:port_4">,
    PfmIssueCounter<"BWPort5", "uops_executed_port:port_5">,
    PfmIssueCounter<"BWPort6", "uops_executed_port:port_6">,
    PfmIssueCounter<"BWPort7", "uops_executed_port:port_7">
  ];
}
def : PfmCountersBinding<"broadwell", BroadwellPfmCounters>;

def SkylakeClientPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"SKLPort0", "uops_dispatched_port:port_0">,
    PfmIssueCounter<"SKLPort1", "uops_dispatched_port:port_1">,
    PfmIssueCounter<"SKLPort2", "uops_dispatched_port:port_2">,
    PfmIssueCounter<"SKLPort3", "uops_dispatched_port:port_3">,
    PfmIssueCounter<"SKLPort4", "uops_dispatched_port:port_4">,
    PfmIssueCounter<"SKLPort5", "uops_dispatched_port:port_5">,
    PfmIssueCounter<"SKLPort6", "uops_dispatched_port:port_6">,
    PfmIssueCounter<"SKLPort7", "uops_dispatched_port:port_7">
  ];
}
def : PfmCountersBinding<"skylake", SkylakeClientPfmCounters>;

def SkylakeServerPfmCounters : ProcPfmCounters {
  let CycleCounter = UnhaltedCoreCyclesPfmCounter;
  let UopsCounter = UopsIssuedPfmCounter;
  let IssueCounters = [
    PfmIssueCounter<"SKXPort0", "uops_dispatched_port:port_0">,
    PfmIssueCounter<"SKXPort1", "uops_dispatched_port:port_1">,
    PfmIssueCounter<"SKXPort2", "uops_dispatched_port:port_2">,
    PfmIssueCounter<"SKXPort3", "uops_dispatched_port:port_3">,
    PfmIssueCounter<"SKXPort4", "uops_dispatched_port:port_4">,
    PfmIssueCounter<"SKXPort5", "uops_dispatched_port:port_5">,
    PfmIssueCounter<"SKXPort6", "uops_dispatched_port:port_6">,
    PfmIssueCounter<"SKXPort7", "uops_dispatched_port:port_7">
  ];
}
def : PfmCountersBinding<"skylake-avx512", SkylakeServerPfmCounters>;
def : PfmCountersBinding<"cascadelake", SkylakeServerPfmCounters>;
def : PfmCountersBinding<"cannonlake", SkylakeServerPfmCounters>;
def : PfmCountersBinding<"icelake-client", SkylakeServerPfmCounters>;
def : PfmCountersBinding<"icelake-server", SkylakeServerPfmCounters>;

// AMD X86 Counters.
// Set basic counters for AMD cpus that we know libpfm4 supports.
def DefaultAMDPfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
}
def : PfmCountersBinding<"athlon", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-tbird", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-4", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-xp", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-mp", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"k8", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"opteron", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon64", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon-fx", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"k8-sse3", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"opteron-sse3", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"athlon64-sse3", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"amdfam10", DefaultAMDPfmCounters>;
def : PfmCountersBinding<"barcelona", DefaultAMDPfmCounters>;

def BdVer2PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"PdFPU0", "dispatched_fpu_ops:ops_pipe0 + dispatched_fpu_ops:ops_dual_pipe0">,
    PfmIssueCounter<"PdFPU1", "dispatched_fpu_ops:ops_pipe1 + dispatched_fpu_ops:ops_dual_pipe1">,
    PfmIssueCounter<"PdFPU2", "dispatched_fpu_ops:ops_pipe2 + dispatched_fpu_ops:ops_dual_pipe2">,
    PfmIssueCounter<"PdFPU3", "dispatched_fpu_ops:ops_pipe3 + dispatched_fpu_ops:ops_dual_pipe3">
  ];
}
def : PfmCountersBinding<"bdver1", BdVer2PfmCounters>;
def : PfmCountersBinding<"bdver2", BdVer2PfmCounters>;

def BdVer3PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"SrFPU0", "dispatched_fpu_ops:ops_pipe0 + dispatched_fpu_ops:ops_dual_pipe0">,
    PfmIssueCounter<"SrFPU1", "dispatched_fpu_ops:ops_pipe1 + dispatched_fpu_ops:ops_dual_pipe1">,
    PfmIssueCounter<"SrFPU2", "dispatched_fpu_ops:ops_pipe2 + dispatched_fpu_ops:ops_dual_pipe2">
  ];
}
def : PfmCountersBinding<"bdver3", BdVer3PfmCounters>;
def : PfmCountersBinding<"bdver4", BdVer3PfmCounters>;

def BtVer1PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"BtFPU0", "dispatched_fpu:pipe0">,
    PfmIssueCounter<"BtFPU1", "dispatched_fpu:pipe1">
  ];
}
def : PfmCountersBinding<"btver1", BtVer1PfmCounters>;

def BtVer2PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cpu_clk_unhalted">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"JFPU0", "dispatched_fpu:pipe0">,
    PfmIssueCounter<"JFPU1", "dispatched_fpu:pipe1">
  ];
}
def : PfmCountersBinding<"btver2", BtVer2PfmCounters>;

def ZnVer1PfmCounters : ProcPfmCounters {
  let CycleCounter = PfmCounter<"cycles_not_in_halt">;
  let UopsCounter = PfmCounter<"retired_uops">;
  let IssueCounters = [
    PfmIssueCounter<"ZnFPU0", "fpu_pipe_assignment:total0">,
    PfmIssueCounter<"ZnFPU1", "fpu_pipe_assignment:total1">,
    PfmIssueCounter<"ZnFPU2", "fpu_pipe_assignment:total2">,
    PfmIssueCounter<"ZnFPU3", "fpu_pipe_assignment:total3">,
    PfmIssueCounter<"ZnDivider", "div_op_count">
  ];
}
def : PfmCountersBinding<"znver1", ZnVer1PfmCounters>;
