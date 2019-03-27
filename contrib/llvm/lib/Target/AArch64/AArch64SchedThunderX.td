//==- AArch64SchedThunderX.td - Cavium ThunderX T8X Scheduling Definitions -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the ARM ThunderX T8X
// (T88, T81, T83) processors.
// Loosely based on Cortex-A53 which is somewhat similar.
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The following definitions describe the simpler per-operand machine model.
// This works with MachineScheduler. See llvm/MC/MCSchedule.h for details.

// Cavium ThunderX T8X scheduling machine model.
def ThunderXT8XModel : SchedMachineModel {
  let IssueWidth = 2;         // 2 micro-ops dispatched per cycle.
  let MicroOpBufferSize = 0;  // ThunderX T88/T81/T83 are in-order.
  let LoadLatency = 3;        // Optimistic load latency.
  let MispredictPenalty = 8;  // Branch mispredict penalty.
  let PostRAScheduler = 1;    // Use PostRA scheduler.
  let CompleteModel = 1;

  list<Predicate> UnsupportedFeatures = [HasSVE];

  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

// Modeling each pipeline with BufferSize == 0 since T8X is in-order.
def THXT8XUnitALU    : ProcResource<2> { let BufferSize = 0; } // Int ALU
def THXT8XUnitMAC    : ProcResource<1> { let BufferSize = 0; } // Int MAC
def THXT8XUnitDiv    : ProcResource<1> { let BufferSize = 0; } // Int Division
def THXT8XUnitLdSt   : ProcResource<1> { let BufferSize = 0; } // Load/Store
def THXT8XUnitBr     : ProcResource<1> { let BufferSize = 0; } // Branch
def THXT8XUnitFPALU  : ProcResource<1> { let BufferSize = 0; } // FP ALU
def THXT8XUnitFPMDS  : ProcResource<1> { let BufferSize = 0; } // FP Mul/Div/Sqrt

//===----------------------------------------------------------------------===//
// Subtarget-specific SchedWrite types mapping the ProcResources and
// latencies.

let SchedModel = ThunderXT8XModel in {

// ALU
def : WriteRes<WriteImm, [THXT8XUnitALU]> { let Latency = 1; }
def : WriteRes<WriteI, [THXT8XUnitALU]> { let Latency = 1; }
def : WriteRes<WriteISReg, [THXT8XUnitALU]> { let Latency = 2; }
def : WriteRes<WriteIEReg, [THXT8XUnitALU]> { let Latency = 2; }
def : WriteRes<WriteIS, [THXT8XUnitALU]> { let Latency = 2; }
def : WriteRes<WriteExtr, [THXT8XUnitALU]> { let Latency = 2; }

// MAC
def : WriteRes<WriteIM32, [THXT8XUnitMAC]> {
  let Latency = 4;
  let ResourceCycles = [1];
}

def : WriteRes<WriteIM64, [THXT8XUnitMAC]> {
  let Latency = 4;
  let ResourceCycles = [1];
}

// Div
def : WriteRes<WriteID32, [THXT8XUnitDiv]> {
  let Latency = 12;
  let ResourceCycles = [6];
}

def : WriteRes<WriteID64, [THXT8XUnitDiv]> {
  let Latency = 14;
  let ResourceCycles = [8];
}

// Load
def : WriteRes<WriteLD, [THXT8XUnitLdSt]> { let Latency = 3; }
def : WriteRes<WriteLDIdx, [THXT8XUnitLdSt]> { let Latency = 3; }
def : WriteRes<WriteLDHi, [THXT8XUnitLdSt]> { let Latency = 3; }

// Vector Load
def : WriteRes<WriteVLD, [THXT8XUnitLdSt]> {
  let Latency = 8;
  let ResourceCycles = [3];
}

def THXT8XWriteVLD1 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 6;
  let ResourceCycles = [1];
}

def THXT8XWriteVLD2 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 11;
  let ResourceCycles = [7];
}

def THXT8XWriteVLD3 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 12;
  let ResourceCycles = [8];
}

def THXT8XWriteVLD4 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 13;
  let ResourceCycles = [9];
}

def THXT8XWriteVLD5 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 13;
  let ResourceCycles = [9];
}

// Pre/Post Indexing
def : WriteRes<WriteAdr, []> { let Latency = 0; }

// Store
def : WriteRes<WriteST, [THXT8XUnitLdSt]> { let Latency = 1; }
def : WriteRes<WriteSTP, [THXT8XUnitLdSt]> { let Latency = 1; }
def : WriteRes<WriteSTIdx, [THXT8XUnitLdSt]> { let Latency = 1; }
def : WriteRes<WriteSTX, [THXT8XUnitLdSt]> { let Latency = 1; }

// Vector Store
def : WriteRes<WriteVST, [THXT8XUnitLdSt]>;
def THXT8XWriteVST1 : SchedWriteRes<[THXT8XUnitLdSt]>;

def THXT8XWriteVST2 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 10;
  let ResourceCycles = [9];
}

def THXT8XWriteVST3 : SchedWriteRes<[THXT8XUnitLdSt]> {
  let Latency = 11;
  let ResourceCycles = [10];
}

def : WriteRes<WriteAtomic, []> { let Unsupported = 1; }

// Branch
def : WriteRes<WriteBr, [THXT8XUnitBr]>;
def THXT8XWriteBR : SchedWriteRes<[THXT8XUnitBr]>;
def : WriteRes<WriteBrReg, [THXT8XUnitBr]>;
def THXT8XWriteBRR : SchedWriteRes<[THXT8XUnitBr]>;
def THXT8XWriteRET : SchedWriteRes<[THXT8XUnitALU]>;
def : WriteRes<WriteSys, [THXT8XUnitBr]>;
def : WriteRes<WriteBarrier, [THXT8XUnitBr]>;
def : WriteRes<WriteHint, [THXT8XUnitBr]>;

// FP ALU
def : WriteRes<WriteF, [THXT8XUnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFCmp, [THXT8XUnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFCvt, [THXT8XUnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFCopy, [THXT8XUnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteFImm, [THXT8XUnitFPALU]> { let Latency = 6; }
def : WriteRes<WriteV, [THXT8XUnitFPALU]> { let Latency = 6; }

// FP Mul, Div, Sqrt
def : WriteRes<WriteFMul, [THXT8XUnitFPMDS]> { let Latency = 6; }
def : WriteRes<WriteFDiv, [THXT8XUnitFPMDS]> {
  let Latency = 22;
  let ResourceCycles = [19];
}

def THXT8XWriteFMAC : SchedWriteRes<[THXT8XUnitFPMDS]> { let Latency = 10; }

def THXT8XWriteFDivSP : SchedWriteRes<[THXT8XUnitFPMDS]> {
  let Latency = 12;
  let ResourceCycles = [9];
}

def THXT8XWriteFDivDP : SchedWriteRes<[THXT8XUnitFPMDS]> {
  let Latency = 22;
  let ResourceCycles = [19];
}

def THXT8XWriteFSqrtSP : SchedWriteRes<[THXT8XUnitFPMDS]> {
  let Latency = 17;
  let ResourceCycles = [14];
}

def THXT8XWriteFSqrtDP : SchedWriteRes<[THXT8XUnitFPMDS]> {
  let Latency = 31;
  let ResourceCycles = [28];
}

//===----------------------------------------------------------------------===//
// Subtarget-specific SchedRead types.

// No forwarding for these reads.
def : ReadAdvance<ReadExtrHi, 1>;
def : ReadAdvance<ReadAdrBase, 2>;
def : ReadAdvance<ReadVLD, 2>;

// FIXME: This needs more targeted benchmarking.
// ALU - Most operands in the ALU pipes are not needed for two cycles. Shiftable
//       operands are needed one cycle later if and only if they are to be
//       shifted. Otherwise, they too are needed two cycles later. This same
//       ReadAdvance applies to Extended registers as well, even though there is
//       a separate SchedPredicate for them.
def : ReadAdvance<ReadI, 2, [WriteImm, WriteI,
                             WriteISReg, WriteIEReg, WriteIS,
                             WriteID32, WriteID64,
                             WriteIM32, WriteIM64]>;
def THXT8XReadShifted : SchedReadAdvance<1, [WriteImm, WriteI,
                                          WriteISReg, WriteIEReg, WriteIS,
                                          WriteID32, WriteID64,
                                          WriteIM32, WriteIM64]>;
def THXT8XReadNotShifted : SchedReadAdvance<2, [WriteImm, WriteI,
                                             WriteISReg, WriteIEReg, WriteIS,
                                             WriteID32, WriteID64,
                                             WriteIM32, WriteIM64]>;
def THXT8XReadISReg : SchedReadVariant<[
	SchedVar<RegShiftedPred, [THXT8XReadShifted]>,
	SchedVar<NoSchedPred, [THXT8XReadNotShifted]>]>;
def : SchedAlias<ReadISReg, THXT8XReadISReg>;

def THXT8XReadIEReg : SchedReadVariant<[
	SchedVar<RegExtendedPred, [THXT8XReadShifted]>,
	SchedVar<NoSchedPred, [THXT8XReadNotShifted]>]>;
def : SchedAlias<ReadIEReg, THXT8XReadIEReg>;

// MAC - Operands are generally needed one cycle later in the MAC pipe.
//       Accumulator operands are needed two cycles later.
def : ReadAdvance<ReadIM, 1, [WriteImm,WriteI,
                              WriteISReg, WriteIEReg, WriteIS,
                              WriteID32, WriteID64,
                              WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadIMA, 2, [WriteImm, WriteI,
                               WriteISReg, WriteIEReg, WriteIS,
                               WriteID32, WriteID64,
                               WriteIM32, WriteIM64]>;

// Div
def : ReadAdvance<ReadID, 1, [WriteImm, WriteI,
                              WriteISReg, WriteIEReg, WriteIS,
                              WriteID32, WriteID64,
                              WriteIM32, WriteIM64]>;

//===----------------------------------------------------------------------===//
// Subtarget-specific InstRW.

//---
// Branch
//---
def : InstRW<[THXT8XWriteBR], (instregex "^B$")>;
def : InstRW<[THXT8XWriteBR], (instregex "^BL$")>;
def : InstRW<[THXT8XWriteBR], (instregex "^B..$")>;
def : InstRW<[THXT8XWriteBR], (instregex "^CBNZ")>;
def : InstRW<[THXT8XWriteBR], (instregex "^CBZ")>;
def : InstRW<[THXT8XWriteBR], (instregex "^TBNZ")>;
def : InstRW<[THXT8XWriteBR], (instregex "^TBZ")>;
def : InstRW<[THXT8XWriteBRR], (instregex "^BR$")>;
def : InstRW<[THXT8XWriteBRR], (instregex "^BLR$")>;

//---
// Ret
//---
def : InstRW<[THXT8XWriteRET], (instregex "^RET$")>;

//---
// Miscellaneous
//---
def : InstRW<[WriteI], (instrs COPY)>;

//---
// Vector Loads
//---
def : InstRW<[THXT8XWriteVLD1], (instregex "LD1i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVLD1], (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD1], (instregex "LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD2], (instregex "LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD3], (instregex "LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD4], (instregex "LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD1, WriteAdr], (instregex "LD1i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVLD1, WriteAdr], (instregex "LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVLD1, WriteAdr], (instregex "LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVLD2, WriteAdr], (instregex "LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVLD3, WriteAdr], (instregex "LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVLD4, WriteAdr], (instregex "LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[THXT8XWriteVLD1], (instregex "LD2i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVLD1], (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD2], (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[THXT8XWriteVLD4], (instregex "LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD1, WriteAdr], (instregex "LD2i(8|16|32|64)(_POST)?$")>;
def : InstRW<[THXT8XWriteVLD1, WriteAdr], (instregex "LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)(_POST)?$")>;
def : InstRW<[THXT8XWriteVLD2, WriteAdr], (instregex "LD2Twov(8b|4h|2s)(_POST)?$")>;
def : InstRW<[THXT8XWriteVLD4, WriteAdr], (instregex "LD2Twov(16b|8h|4s|2d)(_POST)?$")>;

def : InstRW<[THXT8XWriteVLD2], (instregex "LD3i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVLD2], (instregex "LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD4], (instregex "LD3Threev(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[THXT8XWriteVLD3], (instregex "LD3Threev(2d)$")>;
def : InstRW<[THXT8XWriteVLD2, WriteAdr], (instregex "LD3i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVLD2, WriteAdr], (instregex "LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVLD4, WriteAdr], (instregex "LD3Threev(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[THXT8XWriteVLD3, WriteAdr], (instregex "LD3Threev(2d)_POST$")>;

def : InstRW<[THXT8XWriteVLD2], (instregex "LD4i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVLD2], (instregex "LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVLD5], (instregex "LD4Fourv(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[THXT8XWriteVLD4], (instregex "LD4Fourv(2d)$")>;
def : InstRW<[THXT8XWriteVLD2, WriteAdr], (instregex "LD4i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVLD2, WriteAdr], (instregex "LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVLD5, WriteAdr], (instregex "LD4Fourv(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[THXT8XWriteVLD4, WriteAdr], (instregex "LD4Fourv(2d)_POST$")>;

//---
// Vector Stores
//---
def : InstRW<[THXT8XWriteVST1], (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVST1], (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVST1], (instregex "ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVST2], (instregex "ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVST2], (instregex "ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVST1, WriteAdr], (instregex "ST1i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVST1, WriteAdr], (instregex "ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVST1, WriteAdr], (instregex "ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

def : InstRW<[THXT8XWriteVST1], (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVST1], (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[THXT8XWriteVST2], (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[THXT8XWriteVST1, WriteAdr], (instregex "ST2i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVST1, WriteAdr], (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[THXT8XWriteVST2], (instregex "ST3i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVST3], (instregex "ST3Threev(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[THXT8XWriteVST2], (instregex "ST3Threev(2d)$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST3i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVST3, WriteAdr], (instregex "ST3Threev(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST3Threev(2d)_POST$")>;

def : InstRW<[THXT8XWriteVST2], (instregex "ST4i(8|16|32|64)$")>;
def : InstRW<[THXT8XWriteVST3], (instregex "ST4Fourv(8b|4h|2s|1d|16b|8h|4s)$")>;
def : InstRW<[THXT8XWriteVST2], (instregex "ST4Fourv(2d)$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST4i(8|16|32|64)_POST$")>;
def : InstRW<[THXT8XWriteVST3, WriteAdr], (instregex "ST4Fourv(8b|4h|2s|1d|16b|8h|4s)_POST$")>;
def : InstRW<[THXT8XWriteVST2, WriteAdr], (instregex "ST4Fourv(2d)_POST$")>;

//---
// Floating Point MAC, DIV, SQRT
//---
def : InstRW<[THXT8XWriteFMAC], (instregex "^FN?M(ADD|SUB).*")>;
def : InstRW<[THXT8XWriteFMAC], (instregex "^FML(A|S).*")>;
def : InstRW<[THXT8XWriteFDivSP], (instrs FDIVSrr)>;
def : InstRW<[THXT8XWriteFDivDP], (instrs FDIVDrr)>;
def : InstRW<[THXT8XWriteFDivSP], (instregex "^FDIVv.*32$")>;
def : InstRW<[THXT8XWriteFDivDP], (instregex "^FDIVv.*64$")>;
def : InstRW<[THXT8XWriteFSqrtSP], (instregex "^.*SQRT.*32$")>;
def : InstRW<[THXT8XWriteFSqrtDP], (instregex "^.*SQRT.*64$")>;

}
