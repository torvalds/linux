//=- X86ScheduleBdVer2.td - X86 BdVer2 (Piledriver) Scheduling * tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for AMD bdver2 (Piledriver) to support
// instruction scheduling and other instruction cost heuristics.
// Based on:
//  * AMD Software Optimization Guide for AMD Family 15h Processors.
//    https://support.amd.com/TechDocs/47414_15h_sw_opt_guide.pdf
//  * The microarchitecture of Intel, AMD and VIA CPUs, By Agner Fog
//    http://www.agner.org/optimize/microarchitecture.pdf
//  * https://www.realworldtech.com/bulldozer/
//    Yes, that is for Bulldozer aka bdver1, not Piledriver aka bdver2.
//
//===----------------------------------------------------------------------===//

def BdVer2Model : SchedMachineModel {
  let IssueWidth = 4; // Up to 4 IPC can be decoded, issued, retired.
  let MicroOpBufferSize = 128; // RCU reorder buffer size, which is unconfirmed.
  let LoopMicroOpBufferSize = -1; // There does not seem to be a loop buffer.
  let LoadLatency = 4; // L1 data cache has a 4-cycle load-to-use latency.
  let HighLatency = 25; // FIXME: any better choice?
  let MispredictPenalty = 20; // Minimum branch misdirection penalty.

  let PostRAScheduler = 1; // Enable Post RegAlloc Scheduler pass.

  // FIXME: Incomplete. This flag is set to allow the scheduler to assign
  //        a default model to unrecognized opcodes.
  let CompleteModel = 0;
} // SchedMachineModel

let SchedModel = BdVer2Model in {


//===----------------------------------------------------------------------===//
// Pipes
//===----------------------------------------------------------------------===//

// There are total of eight pipes.

//===----------------------------------------------------------------------===//
// Integer execution pipes
//

// Two EX (ALU) pipes.
def PdEX0  : ProcResource<1>; // ALU, Integer Pipe0
def PdEX1  : ProcResource<1>; // ALU, Integer Pipe1
def PdEX01 : ProcResGroup<[PdEX0, PdEX1]>;

// Two AGLU pipes, identical.
def PdAGLU01 : ProcResource<2>; // AGU, Integer Pipe[23]

//===----------------------------------------------------------------------===//
// Floating point execution pipes
//

// Four FPU pipes.

def PdFPU0 : ProcResource<1>; // Vector/FPU Pipe0
def PdFPU1 : ProcResource<1>; // Vector/FPU Pipe1
def PdFPU2 : ProcResource<1>; // Vector/FPU Pipe2
def PdFPU3 : ProcResource<1>; // Vector/FPU Pipe3

// FPU grouping
def PdFPU01 : ProcResGroup<[PdFPU0, PdFPU1]>;
def PdFPU23 : ProcResGroup<[PdFPU2, PdFPU3]>;


//===----------------------------------------------------------------------===//
// RCU
//===----------------------------------------------------------------------===//

// The Retire Control Unit on Piledriver can retire up to 4 macro-ops per cycle.
// On the other hand, the RCU reorder buffer size for Piledriver does not
// seem be specified in any trustworthy source.
// But as per https://www.realworldtech.com/bulldozer/6/ the Bulldozer had
// RCU reorder buffer size of 128. So that is a good guess for now.
def PdRCU : RetireControlUnit<128, 4>;


//===----------------------------------------------------------------------===//
// Pipelines
//===----------------------------------------------------------------------===//

// There are total of two pipelines, each one with it's own scheduler.

//===----------------------------------------------------------------------===//
// Integer Pipeline Scheduling
//

// There is one Integer Scheduler per core.

// Integer physical register file has 96 registers of 64-bit.
def PdIntegerPRF : RegisterFile<96, [GR64, CCR]>;

// Unified Integer, Memory Scheduler has 40 entries.
def PdEX : ProcResGroup<[PdEX0, PdEX1, PdAGLU01]> {
  // Up to 4 IPC can be decoded, issued, retired.
  let BufferSize = 40;
}


//===----------------------------------------------------------------------===//
// FPU Pipeline Scheduling
//

// The FPU unit is shared between the two cores.

// FP physical register file has 160 registers of 128-bit.
// Operations on 256-bit data types are cracked into two COPs.
def PdFpuPRF : RegisterFile<160, [VR64, VR128, VR256], [1, 1, 2]>;

// Unified FP Scheduler has 64 entries,
def PdFPU : ProcResGroup<[PdFPU0, PdFPU1, PdFPU2, PdFPU3]> {
  // Up to 4 IPC can be decoded, issued, retired.
  let BufferSize = 64;
}


//===----------------------------------------------------------------------===//
// Functional units
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Load-Store Units
//

let Super = PdAGLU01 in
def PdLoad  : ProcResource<2> {
  // For Piledriver, the load queue is 40 entries deep.
  let BufferSize = 40;
}

def PdLoadQueue : LoadQueue<PdLoad>;

let Super = PdAGLU01 in
def PdStore : ProcResource<1> {
  // For Piledriver, the store queue is 24 entries deep.
  let BufferSize = 24;
}

def PdStoreQueue : StoreQueue<PdStore>;

//===----------------------------------------------------------------------===//
// Integer Execution Units
//

def PdDiv    : ProcResource<1>; // PdEX0; unpipelined integer division
def PdCount  : ProcResource<1>; // PdEX0; POPCNT, LZCOUNT

def PdMul    : ProcResource<1>; // PdEX1; integer multiplication
def PdBranch : ProcResource<1>; // PdEX1; JMP, fused branches

//===----------------------------------------------------------------------===//
// Floating-Point Units
//

// Two FMAC/FPFMA units.
def PdFPFMA  : ProcResource<2>; // PdFPU0, PdFPU1

// One 128-bit integer multiply-accumulate unit.
def PdFPMMA  : ProcResource<1>; // PdFPU0

// One fp conversion unit.
def PdFPCVT  : ProcResource<1>; // PdFPU0

// One unit for shuffles, packs, permutes, shifts.
def PdFPXBR  : ProcResource<1>; // PdFPU1

// Two 128-bit packed integer units.
def PdFPMAL  : ProcResource<2>; // PdFPU2, PdFPU3

// One FP store unit.
def PdFPSTO  : ProcResource<1>; // PdFPU3


//===----------------------------------------------------------------------===//
// Basic helper classes.
//===----------------------------------------------------------------------===//

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when dispatched by the schedulers.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass PdWriteRes<SchedWrite SchedRW,
                      list<ProcResourceKind> ExePorts, int Lat = 1,
                      list<int> Res = [], int UOps = 1> {
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ResourceCycles = Res;
    let NumMicroOps = UOps;
  }
}

multiclass __pdWriteResPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts, int Lat,
                            list<int> Res, int UOps,
                            int LoadLat, int LoadRes, int LoadUOps> {
  defm : PdWriteRes<SchedRW, ExePorts, Lat, Res, UOps>;

  defm : PdWriteRes<SchedRW.Folded,
                    !listconcat([PdLoad], ExePorts),
                    !add(Lat, LoadLat),
                    !if(!and(!empty(Res), !eq(LoadRes, 1)),
                      [],
                      !listconcat([LoadRes], Res)),
                    !add(UOps, LoadUOps)>;
}

multiclass PdWriteResExPair<X86FoldableSchedWrite SchedRW,
                            list<ProcResourceKind> ExePorts, int Lat = 1,
                            list<int> Res = [], int UOps = 1,
                            int LoadUOps = 0> {
  defm : __pdWriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                          /*LoadLat*/4, /*LoadRes*/1, LoadUOps>;
}

multiclass PdWriteResXMMPair<X86FoldableSchedWrite SchedRW,
                             list<ProcResourceKind> ExePorts, int Lat = 1,
                             list<int> Res = [], int UOps = 1,
                             int LoadUOps = 0> {
  defm : __pdWriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           /*LoadLat*/5, /*LoadRes*/1, LoadUOps>;
}

multiclass PdWriteResYMMPair<X86FoldableSchedWrite SchedRW,
                             list<ProcResourceKind> ExePorts, int Lat,
                             list<int> Res, int UOps = 2,
                             int LoadUOps = 0> {
  defm : __pdWriteResPair<SchedRW, ExePorts, Lat, Res, UOps,
                           /*LoadLat*/5, /*LoadRes*/2, LoadUOps>;
}

//===----------------------------------------------------------------------===//
// Here be dragons.
//===----------------------------------------------------------------------===//

// L1 data cache has a 4-cycle load-to-use latency, so ReadAfterLd registers
// needn't be available until 4 cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 4>;

// Vector loads are 5 cycles, so ReadAfterVec*Ld registers needn't be available
// until 5 cycles after the memory operand.
def : ReadAdvance<ReadAfterVecLd, 5>;
def : ReadAdvance<ReadAfterVecXLd, 5>;
def : ReadAdvance<ReadAfterVecYLd, 5>;

// A folded store needs a cycle on the PdStore for the store data.
def : WriteRes<WriteRMW, [PdStore]>;

////////////////////////////////////////////////////////////////////////////////
// Loads, stores, and moves, not folded with other operations.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteLoad,    [PdLoad]> { let Latency = 5; }
def : WriteRes<WriteStore,   [PdStore]>;
def : WriteRes<WriteStoreNT, [PdStore]>;
def : WriteRes<WriteMove,    [PdEX01]>;

// Load/store MXCSR.
// FIXME: These are copy and pasted from WriteLoad/Store.
def : WriteRes<WriteLDMXCSR, [PdLoad]> { let Latency = 5; }
def : WriteRes<WriteSTMXCSR, [PdStore]> { let NumMicroOps = 2; }

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

////////////////////////////////////////////////////////////////////////////////
// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteZero, [/*No ExePorts*/]>;

////////////////////////////////////////////////////////////////////////////////
// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResExPair<WriteJump,  [PdEX1, PdBranch]>;

////////////////////////////////////////////////////////////////////////////////
// Special case scheduling classes.
////////////////////////////////////////////////////////////////////////////////

def : WriteRes<WriteSystem,     [PdEX01]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [PdEX01]> { let Latency = 100; }
def : WriteRes<WriteFence,      [PdStore]>;

def PdWriteXLAT : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
}
def : InstRW<[PdWriteXLAT], (instrs XLAT)>;

def PdWriteLARrr : SchedWriteRes<[PdEX01]> {
  let Latency = 184;
  let NumMicroOps = 45;
}
def : InstRW<[PdWriteLARrr], (instregex "LAR(16|32|64)rr",
                                        "LSL(16|32|64)rr")>;

// Nops don't have dependencies, so there's no actual latency, but we set this
// to '1' to tell the scheduler that the nop uses an ALU slot for a cycle.
def : WriteRes<WriteNop, [PdEX01]>;

////////////////////////////////////////////////////////////////////////////////
// Arithmetic.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResExPair<WriteALU,     [PdEX01]>;

def PdWriteLXADD : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteLXADD], (instrs LXADD8, LXADD16, LXADD32, LXADD64)>;

def PdWriteBMI1 : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteBMI1],
             (instrs BLCFILL32rr, BLCFILL64rr, BLCI32rr, BLCI64rr,
                     BLCIC32rr, BLCIC64rr, BLCMSK32rr, BLCMSK64rr,
                     BLCS32rr, BLCS64rr, BLSFILL32rr, BLSFILL64rr,
                     BLSIC32rr, BLSIC64rr, T1MSKC32rr, T1MSKC64rr,
                     TZMSK32rr, TZMSK64rr)>;

def PdWriteBMI1m : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteBMI1m],
             (instrs BLCFILL32rm, BLCFILL64rm, BLCI32rm, BLCI64rm,
                     BLCIC32rm, BLCIC64rm, BLCMSK32rm, BLCMSK64rm,
                     BLCS32rm, BLCS64rm, BLSFILL32rm, BLSFILL64rm,
                     BLSIC32rm, BLSIC64rm, T1MSKC32rm, T1MSKC64rm,
                     TZMSK32rm, TZMSK64rm)>;

defm : PdWriteResExPair<WriteADC,    [PdEX01],                  1,  [2]>;

defm : PdWriteRes<WriteBSWAP32,      [PdEX1]>;
defm : PdWriteRes<WriteBSWAP64,      [PdEX1]>;
defm : PdWriteRes<WriteCMPXCHG,      [PdEX1],                   3,  [],       5>;
defm : PdWriteRes<WriteCMPXCHGRMW,   [PdEX1, PdStore, PdLoad],  3,  [], 2>;
defm : PdWriteRes<WriteXCHG,         [PdEX1],                   1,  [],       2>;

def PdWriteCMPXCHG8rr : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let NumMicroOps = 3;
}
def : InstRW<[PdWriteCMPXCHG8rr], (instrs CMPXCHG8rr)>;

def PdWriteCMPXCHG8rm : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let NumMicroOps = 5;
}
def : InstRW<[PdWriteCMPXCHG8rm], (instrs CMPXCHG8rm)>;

def PdWriteCMPXCHG16rm_CMPXCHG32rm_CMPXCHG64rm : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let NumMicroOps = 6;
}
def : InstRW<[PdWriteCMPXCHG16rm_CMPXCHG32rm_CMPXCHG64rm],
             (instrs CMPXCHG16rm, CMPXCHG32rm, CMPXCHG64rm)>;

def PdWriteCMPXCHG8B : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let NumMicroOps = 18;
}
def : InstRW<[PdWriteCMPXCHG8B], (instrs CMPXCHG8B)>;

def PdWriteCMPXCHG16B : SchedWriteRes<[PdEX1]> {
  let Latency = 3;
  let NumMicroOps = 22;
}
def : InstRW<[PdWriteCMPXCHG16B], (instrs CMPXCHG16B)>;

def PdWriteXCHG16rr : SchedWriteRes<[PdEX1]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteXCHG16rr], (instrs XCHG16rr)>;

def PdWriteXADD : SchedWriteRes<[PdEX1]> {
  let Latency = 2;
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteXADD], (instrs XADD8rr, XADD16rr, XADD32rr, XADD64rr)>;

def PdWriteXADDm : SchedWriteRes<[PdEX1]> {
let Latency = 6;
let NumMicroOps = 4;
}
def : InstRW<[PdWriteXADDm], (instrs XADD8rm, XADD16rm, XADD32rm, XADD64rm)>;

defm : PdWriteResExPair<WriteIMul8,     [PdEX1, PdMul],          4>;
defm : PdWriteResExPair<WriteIMul16,    [PdEX1, PdMul],          4,  [],    2>;
defm : PdWriteResExPair<WriteIMul16Imm, [PdEX1, PdMul],          5,  [],    2>;
defm : PdWriteResExPair<WriteIMul16Reg, [PdEX1, PdMul],          4>;
defm : PdWriteResExPair<WriteIMul32,    [PdEX1, PdMul],          4>;
defm : PdWriteResExPair<WriteIMul32Imm, [PdEX1, PdMul],          4,  [],    1, 1>;
defm : PdWriteResExPair<WriteIMul32Reg, [PdEX1, PdMul],          4>;
defm : PdWriteResExPair<WriteIMul64,    [PdEX1, PdMul],          6,  [1, 4]>;
defm : PdWriteResExPair<WriteIMul64Imm, [PdEX1, PdMul],          6,  [1, 4],1, 1>;
defm : PdWriteResExPair<WriteIMul64Reg, [PdEX1, PdMul],          6,  [1, 4]>;
defm : X86WriteResUnsupported<WriteIMulH>; // BMI2 MULX

defm : PdWriteResExPair<WriteDiv8,    [PdEX1, PdDiv],           12,  [1, 12]>;
defm : PdWriteResExPair<WriteDiv16,   [PdEX1, PdDiv],           15,  [1, 15],   2>;
defm : PdWriteResExPair<WriteDiv32,   [PdEX1, PdDiv],           14,  [1, 14],   2>;
defm : PdWriteResExPair<WriteDiv64,   [PdEX1, PdDiv],           14,  [1, 14],   2>;

defm : PdWriteResExPair<WriteIDiv8,   [PdEX1, PdDiv],           12,  [1, 12]>;
defm : PdWriteResExPair<WriteIDiv16,  [PdEX1, PdDiv],           15,  [1, 17],   2>;
defm : PdWriteResExPair<WriteIDiv32,  [PdEX1, PdDiv],           14,  [1, 25],   2>;
defm : PdWriteResExPair<WriteIDiv64,  [PdEX1, PdDiv],           14,  [1, 14],   2>;

defm : PdWriteResExPair<WriteCRC32,   [PdEX01],                  3,  [4],       3>;

def PdWriteCRC32r32r16 : SchedWriteRes<[PdEX01]> {
  let Latency = 5;
  let ResourceCycles = [4];
  let NumMicroOps = 5;
}
def : InstRW<[PdWriteCRC32r32r16], (instrs CRC32r32r16)>;

def PdWriteCRC32r32r32 : SchedWriteRes<[PdEX01]> {
  let Latency = 6;
  let ResourceCycles = [4];
  let NumMicroOps = 7;
}
def : InstRW<[PdWriteCRC32r32r32], (instrs CRC32r32r32)>;

def PdWriteCRC32r64r64 : SchedWriteRes<[PdEX01]> {
  let Latency = 10;
  let ResourceCycles = [4];
  let NumMicroOps = 11;
}
def : InstRW<[PdWriteCRC32r64r64], (instrs CRC32r64r64)>;

defm : PdWriteResExPair<WriteCMOV,    [PdEX01]>; // Conditional move.
defm : PdWriteResExPair<WriteCMOV2,   [PdEX01], 1, [], 1, 1>; // Conditional (CF + ZF flag) move.

def : InstRW<[WriteCMOV2.Folded], (instrs CMOVG16rm, CMOVG32rm, CMOVG64rm,
                                          CMOVGE16rm, CMOVGE32rm, CMOVGE64rm,
                                          CMOVL16rm, CMOVL32rm, CMOVL64rm,
                                          CMOVLE16rm, CMOVLE32rm, CMOVLE64rm)>;

defm : PdWriteRes<WriteFCMOV,        [PdFPU0, PdFPFMA]>; // x87 conditional move.

def : WriteRes<WriteSETCC,           [PdEX01]>; // Setcc.
def : WriteRes<WriteSETCCStore,      [PdEX01, PdStore]>;

def PdWriteSETGEmSETGmSETLEmSETLm : SchedWriteRes<[PdEX01]> {
  let ResourceCycles = [2];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteSETGEmSETGmSETLEmSETLm], (instrs SETGEm, SETGm,
                                                      SETLEm, SETLm)>;

defm : PdWriteRes<WriteLAHFSAHF,      [PdEX01],          2,  [],     2>;

def WriteLAHF : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let NumMicroOps = 4;
}
def : InstRW<[WriteLAHF], (instrs LAHF)>;

def WriteSAHF : SchedWriteRes<[PdEX01]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[WriteSAHF], (instrs SAHF)>;

defm : PdWriteRes<WriteBitTest,          [PdEX01],         1, [1],     1>;
defm : PdWriteRes<WriteBitTestImmLd,     [PdEX01, PdLoad], 5, [1, 1],  1>;
defm : PdWriteRes<WriteBitTestRegLd,     [PdEX01, PdLoad], 5, [1, 1],  7>;
defm : PdWriteRes<WriteBitTestSet,       [PdEX01],         2, [1],     2>;
defm : PdWriteRes<WriteBitTestSetImmLd,  [PdEX01, PdLoad], 6, [1, 1],  4>;
defm : PdWriteRes<WriteBitTestSetImmRMW, [PdEX01, PdLoad], 6, [1, 1],  4>;
defm : PdWriteRes<WriteBitTestSetRegLd,  [PdEX01, PdLoad], 6, [1, 1], 10>;
defm : PdWriteRes<WriteBitTestSetRegRMW, [PdEX01, PdLoad], 6, [1, 1], 10>;

// This is for simple LEAs with one or two input operands.
// FIXME: SAGU 3-operand LEA
def : WriteRes<WriteLEA,              [PdEX01]> { let NumMicroOps = 2; }

// Bit counts.
defm : PdWriteResExPair<WriteBSF,     [PdEX01],          3,  [4],     6, 2>;
defm : PdWriteResExPair<WriteBSR,     [PdEX01],          4,  [4],     7, 2>;
defm : PdWriteResExPair<WritePOPCNT,  [PdEX01],          4>;
defm : PdWriteResExPair<WriteLZCNT,   [PdEX01],          2,  [],      2>;
defm : PdWriteResExPair<WriteTZCNT,   [PdEX01],          2,  [2],     2>;

// BMI1 BEXTR, BMI2 BZHI
defm : PdWriteResExPair<WriteBEXTR,   [PdEX01],          2,  [],     2>;
defm : PdWriteResExPair<WriteBLS,     [PdEX01],          2,  [],     2>;
defm : PdWriteResExPair<WriteBZHI,    [PdEX01]>;

////////////////////////////////////////////////////////////////////////////////
// Integer shifts and rotates.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResExPair<WriteShift,    [PdEX01]>;
defm : PdWriteResExPair<WriteShiftCL,  [PdEX01]>;
defm : PdWriteResExPair<WriteRotate,   [PdEX01]>;
defm : PdWriteResExPair<WriteRotateCL, [PdEX01]>;

def PdWriteRCL8rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 12;
  let NumMicroOps = 26;
}
def : InstRW<[PdWriteRCL8rCL], (instrs RCL8rCL)>;

def PdWriteRCR8ri : SchedWriteRes<[PdEX01]> {
  let Latency = 12;
  let NumMicroOps = 23;
}
def : InstRW<[PdWriteRCR8ri], (instrs RCR8ri)>;

def PdWriteRCR8rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 11;
  let NumMicroOps = 24;
}
def : InstRW<[PdWriteRCR8rCL], (instrs RCR8rCL)>;

def PdWriteRCL16rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 10;
  let NumMicroOps = 22;
}
def : InstRW<[PdWriteRCL16rCL], (instrs RCL16rCL)>;

def PdWriteRCR16ri : SchedWriteRes<[PdEX01]> {
  let Latency = 10;
  let NumMicroOps = 19;
}
def : InstRW<[PdWriteRCR16ri], (instrs RCR16ri)>;

def PdWriteRCL32rCLRCL64rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let NumMicroOps = 17;
}
def : InstRW<[PdWriteRCL32rCLRCL64rCL], (instrs RCL32rCL, RCL64rCL)>;

def PdWriteRCR64rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteRCR64rCL], (instrs RCR64rCL)>;

def PdWriteRCR32rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteRCR32rCL ], (instrs RCR32rCL)>;

def PdWriteRCR32riRCR64ri : SchedWriteRes<[PdEX01]> {
  let Latency = 7;
  let NumMicroOps = 15;
}
def : InstRW<[PdWriteRCR32riRCR64ri], (instrs RCR32ri, RCR64ri)>;


def PdWriteRCR16rCL : SchedWriteRes<[PdEX01]> {
  let Latency = 9;
  let NumMicroOps = 20;
}
def : InstRW<[PdWriteRCR16rCL], (instrs RCR16rCL)>;

def PdWriteRCL16ri : SchedWriteRes<[PdEX01]> {
  let Latency = 11;
  let NumMicroOps = 21;
}
def : InstRW<[PdWriteRCL16ri], (instrs RCL16ri)>;

def PdWriteRCL3264ri : SchedWriteRes<[PdEX01]> {
  let Latency = 8;
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteRCL3264ri], (instrs RCL32ri, RCL64ri)>;

def PdWriteRCL8ri : SchedWriteRes<[PdEX01]> {
  let Latency = 13;
  let NumMicroOps = 25;
}
def : InstRW<[PdWriteRCL8ri], (instrs RCL8ri)>;

// SHLD/SHRD.
defm : PdWriteRes<WriteSHDrri,       [PdEX01],         4, [6], 6>;
defm : PdWriteRes<WriteSHDrrcl,      [PdEX01],         4, [8], 7>;

def PdWriteSHLD32rri8SHRD16rri8 : SchedWriteRes<[PdEX01]> {
  let Latency = 3;
  let ResourceCycles = [6];
  let NumMicroOps = 6;
}
def : InstRW<[PdWriteSHLD32rri8SHRD16rri8 ], (instrs SHLD32rri8, SHRD16rri8)>;

def PdWriteSHLD16rrCLSHLD32rrCLSHRD32rrCL : SchedWriteRes<[PdEX01]> {
  let Latency = 4;
  let ResourceCycles = [8];
  let NumMicroOps = 7;
}
def : InstRW<[PdWriteSHLD16rrCLSHLD32rrCLSHRD32rrCL], (instrs SHLD16rrCL,
                                                              SHLD32rrCL,
                                                              SHRD32rrCL)>;

defm : PdWriteRes<WriteSHDmri,       [PdLoad, PdEX01], 4, [1, 22], 8>;
defm : PdWriteRes<WriteSHDmrcl,      [PdLoad, PdEX01], 4, [1, 22], 8>;

////////////////////////////////////////////////////////////////////////////////
// Floating point. This covers both scalar and vector operations.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteFLD0,               [PdFPU1, PdFPSTO], 3>;
defm : PdWriteRes<WriteFLD1,               [PdFPU1, PdFPSTO], 3>;
defm : PdWriteRes<WriteFLDC,               [PdFPU1, PdFPSTO], 3>;

defm : PdWriteRes<WriteFLoad,              [PdLoad, PdFPU01, PdFPFMA], 5>;
defm : PdWriteRes<WriteFLoadX,             [PdLoad, PdFPU01, PdFPFMA], 5>;
defm : PdWriteRes<WriteFLoadY,             [PdLoad, PdFPU01, PdFPFMA], 5, [], 2>;

defm : PdWriteRes<WriteFMaskedLoad,        [PdLoad, PdFPU01, PdFPFMA], 6, [1, 1, 2]>;
defm : PdWriteRes<WriteFMaskedLoadY,       [PdLoad, PdFPU01, PdFPFMA], 6, [2, 2, 4], 2>;

defm : PdWriteRes<WriteFStore,             [PdStore, PdFPU1,  PdFPSTO], 2>;
defm : PdWriteRes<WriteFStoreX,            [PdStore, PdFPU1,  PdFPSTO]>;
defm : PdWriteRes<WriteFStoreY,            [PdStore, PdFPU1,  PdFPSTO], 1, [], 4>;

def PdWriteMOVHPm : SchedWriteRes<[PdStore, PdFPU1,  PdFPSTO]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteMOVHPm], (instrs MOVHPDmr, MOVHPSmr, VMOVHPDmr, VMOVHPSmr)>;

def PdWriteVMOVUPDYmrVMOVUPSYmr : SchedWriteRes<[PdStore, PdFPU1,  PdFPSTO]> {
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVMOVUPDYmrVMOVUPSYmr], (instrs VMOVUPDYmr, VMOVUPSYmr)>;

defm : PdWriteRes<WriteFStoreNT,           [PdStore, PdFPU1,  PdFPSTO], 3>;
defm : PdWriteRes<WriteFStoreNTX,          [PdStore, PdFPU1,  PdFPSTO], 3>;
defm : PdWriteRes<WriteFStoreNTY,          [PdStore, PdFPU1,  PdFPSTO], 3, [2, 2, 2], 4>;

defm : PdWriteRes<WriteFMaskedStore,       [PdStore, PdFPU01, PdFPFMA], 6, [1, 1, 4], 18>;
defm : PdWriteRes<WriteFMaskedStoreY,      [PdStore, PdFPU01, PdFPFMA], 6, [2, 2, 4], 34>;

defm : PdWriteRes<WriteFMove,              [PdFPU01, PdFPFMA]>;
defm : PdWriteRes<WriteFMoveX,             [PdFPU01, PdFPFMA]>;
defm : PdWriteRes<WriteFMoveY,             [PdFPU01, PdFPFMA], 2, [2, 2], 2>;

defm : PdWriteRes<WriteEMMS,               [PdFPU01, PdFPFMA], 2>;

defm : PdWriteResXMMPair<WriteFAdd,         [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFAddX,        [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFAddY,        [PdFPU0, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;

defm : PdWriteResXMMPair<WriteFAdd64,       [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFAdd64X,      [PdFPU0, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFAdd64Y,      [PdFPU0, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;

defm : PdWriteResXMMPair<WriteFCmp,         [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResXMMPair<WriteFCmpX,        [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFCmpY,        [PdFPU0, PdFPFMA],  2, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;

defm : PdWriteResXMMPair<WriteFCmp64,       [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResXMMPair<WriteFCmp64X,      [PdFPU0, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFCmp64Y,      [PdFPU0, PdFPFMA],  2, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;

defm : PdWriteResXMMPair<WriteFCom,         [PdFPU0, PdFPFMA, PdEX0], 1, [], 2>;

def PdWriteFCOMPm : SchedWriteRes<[PdFPU1, PdFPFMA]> {
  let Latency = 6;
}
def : InstRW<[PdWriteFCOMPm], (instrs FCOM32m, FCOM64m, FCOMP32m, FCOMP64m)>;

def PdWriteTST_F_UCOM_FPPr : SchedWriteRes<[PdFPU1, PdFPFMA]>;
def : InstRW<[PdWriteTST_F_UCOM_FPPr], (instrs TST_F, UCOM_FPPr)>;

defm : PdWriteResXMMPair<WriteFMul,         [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFMulX,        [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFMulY,        [PdFPU1, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;

defm : PdWriteResXMMPair<WriteFMul64,       [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFMul64X,      [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFMul64Y,      [PdFPU1, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;

defm : PdWriteResXMMPair<WriteFMA,          [PdFPU, PdFPFMA], 5>;
defm : PdWriteResXMMPair<WriteFMAX,         [PdFPU, PdFPFMA], 5>;
defm : PdWriteResYMMPair<WriteFMAY,         [PdFPU, PdFPFMA], 5,   [1, 1]>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;


defm : PdWriteResXMMPair<WriteDPPD,         [PdFPU1, PdFPFMA], 15, [1, 3],  15, 2>;

defm : PdWriteResXMMPair<WriteDPPS,         [PdFPU1, PdFPFMA], 25, [1, 3],  16, 2>;
defm : PdWriteResYMMPair<WriteDPPSY,        [PdFPU1, PdFPFMA], 27, [2, 6], /*or 29*/ 25, 4>;
defm : X86WriteResPairUnsupported<WriteDPPSZ>;

def PdWriteVDPPSrri : SchedWriteRes<[PdFPU1, PdFPFMA]> {
  let Latency = 25;
  let ResourceCycles = [1, 3];
  let NumMicroOps = 17;
}
def : InstRW<[PdWriteVDPPSrri], (instrs VDPPSrri)>;

defm : PdWriteResXMMPair<WriteFRcp,         [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFRcpX,        [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFRcpY,        [PdFPU1, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;

defm : PdWriteResXMMPair<WriteFRsqrt,       [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResXMMPair<WriteFRsqrtX,      [PdFPU1, PdFPFMA],  5>;
defm : PdWriteResYMMPair<WriteFRsqrtY,      [PdFPU1, PdFPFMA],  5, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;

defm : PdWriteResXMMPair<WriteFDiv,         [PdFPU1, PdFPFMA], 9, [1, 19]>;
defm : PdWriteResXMMPair<WriteFDivX,        [PdFPU1, PdFPFMA], 9, [1, 19]>;
defm : PdWriteResYMMPair<WriteFDivY,        [PdFPU1, PdFPFMA], 9, [2, 38]>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;

defm : PdWriteResXMMPair<WriteFDiv64,       [PdFPU1, PdFPFMA], 9, [1, 19]>;
defm : PdWriteResXMMPair<WriteFDiv64X,      [PdFPU1, PdFPFMA], 9, [1, 19]>;
defm : PdWriteResYMMPair<WriteFDiv64Y,      [PdFPU1, PdFPFMA], 9, [2, 38]>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;

defm : PdWriteResXMMPair<WriteFSqrt,        [PdFPU1, PdFPFMA], 9, [1, 21]>;
defm : PdWriteResXMMPair<WriteFSqrtX,       [PdFPU1, PdFPFMA], 9, [1, 21]>;
defm : PdWriteResYMMPair<WriteFSqrtY,       [PdFPU1, PdFPFMA], 9, [2, 42]>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;

defm : PdWriteResXMMPair<WriteFSqrt64,      [PdFPU1, PdFPFMA], 9, [1, 27]>;
defm : PdWriteResXMMPair<WriteFSqrt64X,     [PdFPU1, PdFPFMA], 9, [1, 27]>;
defm : PdWriteResYMMPair<WriteFSqrt64Y,     [PdFPU1, PdFPFMA], 9, [2, 54]>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;

defm : PdWriteResXMMPair<WriteFSqrt80,      [PdFPU1, PdFPFMA],  1, [1, 35]>;
defm : PdWriteResXMMPair<WriteFSign,        [PdFPU1, PdFPFMA]>;

defm : PdWriteResXMMPair<WriteFRnd,         [PdFPU1, PdFPSTO],  4>;
defm : PdWriteResYMMPair<WriteFRndY,        [PdFPU1, PdFPSTO],  4, [2, 1], 2>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;

def PdWriteVFRCZ : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVFRCZ], (instrs VFRCZPDrr, VFRCZPSrr,
                                     VFRCZSDrr, VFRCZSSrr)>;

def PdWriteVFRCZm : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 15;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVFRCZm], (instrs VFRCZPDrm, VFRCZPSrm,
                                      VFRCZSDrm, VFRCZSSrm)>;

def PdWriteVFRCZY : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 10;
  let ResourceCycles = [2, 1];
  let NumMicroOps = 4;
}
def : InstRW<[PdWriteVFRCZY], (instrs VFRCZPSYrr, VFRCZPDYrr)>;

def PdWriteVFRCZYm : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 15;
  let ResourceCycles = [2, 1];
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVFRCZYm], (instrs VFRCZPSYrm, VFRCZPDYrm)>;

defm : PdWriteResXMMPair<WriteFLogic,       [PdFPU01, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFLogicY,      [PdFPU01, PdFPFMA],  2, [2, 2]>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;

defm : PdWriteResXMMPair<WriteFTest,        [PdFPU0, PdFPFMA, PdEX0],  1, [], 2>;
defm : PdWriteResYMMPair<WriteFTestY,       [PdFPU01, PdFPFMA, PdEX0], 1, [2, 2, 1], 4, 2>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;

defm : PdWriteResXMMPair<WriteFShuffle,     [PdFPU01, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFShuffleY,    [PdFPU01, PdFPFMA],  2, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;

def PdWriteVBROADCASTF128 : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVBROADCASTF128], (instrs VBROADCASTF128)>;

defm : PdWriteResXMMPair<WriteFVarShuffle,  [PdFPU01, PdFPFMA],  3, [1, 4]>;
defm : PdWriteResYMMPair<WriteFVarShuffleY, [PdFPU01, PdFPFMA],  3, [2, 6], 2>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;

defm : PdWriteResXMMPair<WriteFBlend,       [PdFPU01, PdFPFMA],  2>;
defm : PdWriteResYMMPair<WriteFBlendY,      [PdFPU01, PdFPFMA],  2, [2, 2], 2>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;

defm : PdWriteResXMMPair<WriteFVarBlend,    [PdFPU01, PdFPFMA],  2, [1, 4]>;
defm : PdWriteResYMMPair<WriteFVarBlendY,   [PdFPU01, PdFPFMA],  2, [2, 6], 2>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;

defm : PdWriteResXMMPair<WriteFShuffle256,  [PdFPU01, PdFPFMA],  2, [], 2>;
defm : X86WriteResPairUnsupported<WriteFVarShuffle256>;

def PdWriteVEXTRACTF128rr : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 2;
}
def : InstRW<[PdWriteVEXTRACTF128rr], (instrs VEXTRACTF128rr)>;

def PdWriteVEXTRACTF128mr : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 7;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVEXTRACTF128mr], (instrs VEXTRACTF128mr)>;

def PdWriteVPERM2F128rr : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 4;
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVPERM2F128rr], (instrs VPERM2F128rr)>;

def PdWriteVPERM2F128rm : SchedWriteRes<[PdFPU01, PdFPFMA]> {
  let Latency = 8; // 4 + 4
  let NumMicroOps = 10;
}
def : InstRW<[PdWriteVPERM2F128rm], (instrs VPERM2F128rm)>;

////////////////////////////////////////////////////////////////////////////////
// Conversions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteCvtSS2I,   [PdFPU1, PdFPSTO, PdFPFMA, PdEX0], 13, [], 2>;

defm : PdWriteResXMMPair<WriteCvtPS2I,   [PdFPU1, PdFPSTO], 4>;
defm : PdWriteResYMMPair<WriteCvtPS2IY,  [PdFPU1, PdFPSTO], 4, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;

defm : PdWriteResXMMPair<WriteCvtSD2I,   [PdFPU1, PdFPSTO, PdFPFMA, PdEX0], 13, [], 2>;

defm : PdWriteResXMMPair<WriteCvtPD2I,   [PdFPU1, PdFPSTO],          8, [],        2>;
defm : PdWriteResYMMPair<WriteCvtPD2IY,  [PdFPU1, PdFPSTO, PdFPFMA], 8, [2, 1, 1], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;

def PdWriteMMX_CVTTPD2PIirr : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteMMX_CVTTPD2PIirr], (instrs MMX_CVTTPD2PIirr)>;

// FIXME: f+3 ST, LD+STC latency
defm : PdWriteResXMMPair<WriteCvtI2SS,   [PdFPU1, PdFPSTO], 4, [], 2>;
// FIXME: .Folded version is one NumMicroOp *less*..

defm : PdWriteResXMMPair<WriteCvtI2PS,   [PdFPU1, PdFPSTO], 4>;
defm : PdWriteResYMMPair<WriteCvtI2PSY,  [PdFPU1, PdFPSTO], 4, [2, 1]>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;

defm : PdWriteResXMMPair<WriteCvtI2SD,   [PdFPU1, PdFPSTO], 4, [], 2>;
// FIXME: .Folded version is one NumMicroOp *less*..

def WriteCVTSI642SDrr : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 13;
  let NumMicroOps = 2;
}
def : InstRW<[WriteCVTSI642SDrr], (instrs CVTSI642SDrr, CVTSI642SSrr)>;

defm : PdWriteResXMMPair<WriteCvtI2PD,   [PdFPU1, PdFPSTO], 8, [],     2>;
defm : PdWriteResYMMPair<WriteCvtI2PDY,  [PdFPU1, PdFPSTO], 8, [2, 1], 4, 1>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;

defm : PdWriteResXMMPair<WriteCvtSS2SD,  [PdFPU1, PdFPSTO], 4>;

defm : PdWriteResXMMPair<WriteCvtPS2PD,  [PdFPU1, PdFPSTO], 8, [],     2>;
defm : PdWriteResYMMPair<WriteCvtPS2PDY, [PdFPU1, PdFPSTO], 8, [2, 1], 4, 1>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>;

defm : PdWriteResXMMPair<WriteCvtSD2SS,  [PdFPU1, PdFPSTO], 4>;

defm : PdWriteResXMMPair<WriteCvtPD2PS,  [PdFPU1, PdFPSTO],          8, [],        2>;
defm : PdWriteResYMMPair<WriteCvtPD2PSY, [PdFPU1, PdFPSTO, PdFPFMA], 8, [2, 1, 1], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>;

def WriteMMX_CVTPD2PIirrMMX_CVTPI2PDirr : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def : InstRW<[WriteMMX_CVTPD2PIirrMMX_CVTPI2PDirr], (instrs MMX_CVTPD2PIirr,
                                                            MMX_CVTPI2PDirr)>;

def WriteMMX_CVTPI2PSirr : SchedWriteRes<[PdFPU1, PdFPSTO]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : InstRW<[WriteMMX_CVTPI2PSirr], (instrs MMX_CVTPI2PSirr)>;

defm : PdWriteResXMMPair<WriteCvtPH2PS,  [PdFPU1, PdFPSTO], 8, [],     2, 1>;
defm : PdWriteResYMMPair<WriteCvtPH2PSY, [PdFPU1, PdFPSTO], 8, [2, 1], 4, 3>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSZ>;

defm : PdWriteRes<WriteCvtPS2PH,        [PdFPU1, PdFPSTO],          8, [],        2>;
defm : PdWriteRes<WriteCvtPS2PHY,       [PdFPU1, PdFPSTO, PdFPFMA], 8, [2, 1, 1], 4>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;

defm : PdWriteRes<WriteCvtPS2PHSt,      [PdFPU1, PdFPSTO, PdStore],          4, [],           3>;
defm : PdWriteRes<WriteCvtPS2PHYSt,     [PdFPU1, PdFPSTO, PdFPFMA, PdStore], 4, [2, 1, 1, 1], 4>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

////////////////////////////////////////////////////////////////////////////////
// Vector integer operations.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteVecLoad,             [PdLoad, PdFPU01, PdFPMAL], 5>;
defm : PdWriteRes<WriteVecLoadX,            [PdLoad, PdFPU01, PdFPMAL], 5>;
defm : PdWriteRes<WriteVecLoadY,            [PdLoad, PdFPU01, PdFPMAL], 5, [], 2>;

defm : PdWriteRes<WriteVecLoadNT,           [PdLoad, PdFPU01, PdFPMAL], 5>;
defm : PdWriteRes<WriteVecLoadNTY,          [PdLoad, PdFPU01, PdFPMAL], 5>;

defm : PdWriteRes<WriteVecMaskedLoad,       [PdLoad, PdFPU01, PdFPMAL], 6, [1, 1, 2]>;
defm : PdWriteRes<WriteVecMaskedLoadY,      [PdLoad, PdFPU01, PdFPMAL], 6, [2, 2, 4], 2>;

defm : PdWriteRes<WriteVecStore,            [PdStore, PdFPU1,   PdFPSTO], 2>;
defm : PdWriteRes<WriteVecStoreX,           [PdStore, PdFPU1,   PdFPSTO]>;
defm : PdWriteRes<WriteVecStoreY,           [PdStore, PdFPU1,   PdFPSTO], 1, [], 4>;

def PdWriteVMOVDQUYmr : SchedWriteRes<[PdStore, PdFPU1,   PdFPSTO]> {
  let NumMicroOps = 8;
}
def : InstRW<[PdWriteVMOVDQUYmr], (instrs VMOVDQUYmr)>;

defm : PdWriteRes<WriteVecStoreNT,          [PdStore, PdFPU1,   PdFPSTO], 2>;
defm : PdWriteRes<WriteVecStoreNTY,         [PdStore, PdFPU1,   PdFPSTO], 2, [2, 2, 2], 4>;

defm : PdWriteRes<WriteVecMaskedStore,      [PdStore, PdFPU01, PdFPMAL], 6, [1, 1, 4]>;
defm : PdWriteRes<WriteVecMaskedStoreY,     [PdStore, PdFPU01, PdFPMAL], 6, [2, 2, 4], 2>;

defm : PdWriteRes<WriteVecMove,             [PdFPU01, PdFPMAL], 2>;
defm : PdWriteRes<WriteVecMoveX,            [PdFPU01, PdFPMAL], 2>;
defm : PdWriteRes<WriteVecMoveY,            [PdFPU01, PdFPMAL], 2, [2, 2], 2>;

defm : PdWriteRes<WriteVecMoveToGpr,        [PdFPU0, PdFPFMA, PdEX0], 10>;
defm : PdWriteRes<WriteVecMoveFromGpr,      [PdFPU01, PdFPFMA], 10, [], 2>;

defm : PdWriteResXMMPair<WriteVecALU,        [PdFPU01, PdFPMAL], 2>;
defm : PdWriteResXMMPair<WriteVecALUX,       [PdFPU01, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteVecALUY>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;

defm : PdWriteResXMMPair<WriteVecShift,      [PdFPU01, PdFPMAL], 3>;
defm : PdWriteResXMMPair<WriteVecShiftX,     [PdFPU01, PdFPMAL], 3>;
defm : X86WriteResPairUnsupported<WriteVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;

defm : PdWriteResXMMPair<WriteVecShiftImm,   [PdFPU01, PdFPMAL], 2>;
defm : PdWriteResXMMPair<WriteVecShiftImmX,  [PdFPU01, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmY>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;

defm : PdWriteResXMMPair<WriteVecIMul,       [PdFPU0, PdFPMMA], 4>;
defm : PdWriteResXMMPair<WriteVecIMulX,      [PdFPU0, PdFPMMA], 4>;
defm : X86WriteResPairUnsupported<WriteVecIMulY>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;

defm : PdWriteResXMMPair<WritePMULLD,        [PdFPU0, PdFPU01, PdFPMMA, PdFPMAL], 5, [2, 1, 2, 1]>;
defm : X86WriteResPairUnsupported<WritePMULLDY>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;

def JWriteVPMACS : SchedWriteRes<[PdFPU0, PdFPU01, PdFPMMA, PdFPMAL]> {
  let Latency = 4;
  let ResourceCycles = [2, 1, 2, 1];
}
def : InstRW<[JWriteVPMACS], (instrs VPMACSDQHrr, VPMACSDQLrr, VPMACSSDQHrr,
                                     VPMACSSDQLrr)>;

defm : PdWriteResXMMPair<WriteMPSAD,         [PdFPU0, PdFPMMA], 9, [1, 2], 9>;
defm : X86WriteResPairUnsupported<WriteMPSADY>;
defm : X86WriteResPairUnsupported<WriteMPSADZ>;

defm : PdWriteResXMMPair<WritePSADBW,        [PdFPU01, PdFPMAL], 4, [], 2>;
defm : PdWriteResXMMPair<WritePSADBWX,       [PdFPU01, PdFPMAL], 4, [], 2>;
defm : X86WriteResPairUnsupported<WritePSADBWY>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;

defm : PdWriteResXMMPair<WritePHMINPOS,      [PdFPU0,  PdFPMAL], 4, [], 2>;

defm : PdWriteResXMMPair<WriteShuffle,       [PdFPU01, PdFPMAL], 2>;
defm : PdWriteResXMMPair<WriteShuffleX,      [PdFPU01, PdFPMAL], 2>;
defm : PdWriteResYMMPair<WriteShuffleY,      [PdFPU01, PdFPMAL], 2,   [1, 1]>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;

defm : PdWriteResXMMPair<WriteVarShuffle,    [PdFPU01, PdFPMAL], 3, [1, 4]>;
defm : PdWriteResXMMPair<WriteVarShuffleX,   [PdFPU01, PdFPMAL], 3, [1, 4]>;
defm : X86WriteResPairUnsupported<WriteVarShuffleY>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;

defm : PdWriteResXMMPair<WriteBlend,         [PdFPU01, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteBlendY>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;

defm : PdWriteResXMMPair<WriteVarBlend,      [PdFPU01, PdFPMAL], 2, [1, 4]>;
defm : X86WriteResPairUnsupported<WriteVarBlendY>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;

defm : PdWriteResXMMPair<WriteVecLogic,      [PdFPU01, PdFPMAL], 2>;
defm : PdWriteResXMMPair<WriteVecLogicX,     [PdFPU01, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WriteVecLogicY>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;

defm : PdWriteResXMMPair<WriteVecTest,       [PdFPU0, PdFPFMA, PdEX0],  1, [], 2>;
defm : PdWriteResYMMPair<WriteVecTestY,      [PdFPU01, PdFPFMA, PdEX0], 1, [2, 2, 1], 4, 2>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;

defm : PdWriteResXMMPair<WriteShuffle256,    [PdFPU01, PdFPMAL]>;
defm : PdWriteResXMMPair<WriteVarShuffle256, [PdFPU01, PdFPMAL]>;

defm : PdWriteResXMMPair<WriteVarVecShift,   [PdFPU01, PdFPMAL], 3>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;

////////////////////////////////////////////////////////////////////////////////
// Vector insert/extract operations.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteVecInsert,    [PdFPU01, PdFPMAL], 2, [], 2>;
defm : PdWriteRes<WriteVecInsertLd,  [PdFPU01, PdFPMAL, PdLoad], 6, [], 2>;

defm : PdWriteRes<WriteVecExtract,   [PdFPU0, PdFPFMA, PdEX0], 13, [], 2>;
defm : PdWriteRes<WriteVecExtractSt, [PdFPU1, PdFPSTO, PdStore], 13, [], 2>;

def PdWriteEXTRQ : SchedWriteRes<[PdFPU01, PdFPMAL]> {
  let Latency = 3;
}
def : InstRW<[PdWriteEXTRQ], (instrs EXTRQ, EXTRQI)>;

////////////////////////////////////////////////////////////////////////////////
// SSE42 String instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WritePCmpIStrI, [PdFPU1, PdFPFMA, PdEX0], 14, [1, 2, 1], 7, 1>;
defm : PdWriteResXMMPair<WritePCmpIStrM, [PdFPU1, PdFPFMA, PdEX0],  6, [1, 2, 1], 7, 2>;

defm : PdWriteResXMMPair<WritePCmpEStrI, [PdFPU1, PdStore, PdLoad, PdFPMAL, PdFPFMA, PdEX0], 15, [1, 2, 6, 4, 1, 1], 27, 1>;
defm : PdWriteResXMMPair<WritePCmpEStrM, [PdFPU1, PdStore, PdLoad, PdFPMAL, PdFPFMA, PdEX0], 10, [1, 2, 6, 4, 1, 1], 27, 1>;

////////////////////////////////////////////////////////////////////////////////
// MOVMSK Instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteRes<WriteFMOVMSK, [PdFPU0, PdFPFMA, PdEX0],   10, [], 2>;

defm : PdWriteRes<WriteVecMOVMSK, [PdFPU0, PdFPFMA, PdEX0], 13, [], 2>;
defm : X86WriteResUnsupported<WriteVecMOVMSKY>;
// defm : X86WriteResUnsupported<WriteVecMOVMSKZ>;

defm : PdWriteRes<WriteMMXMOVMSK, [PdFPU0, PdFPFMA, PdEX0], 10, [], 2>;

////////////////////////////////////////////////////////////////////////////////
// AES Instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteAESIMC,    [PdFPU0, PdFPMMA], 5>;
defm : PdWriteResXMMPair<WriteAESKeyGen, [PdFPU0, PdFPMMA], 5>;
defm : PdWriteResXMMPair<WriteAESDecEnc, [PdFPU0, PdFPMMA], 9, [], 2>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteFHAdd,  [PdFPU0, PdFPFMA], 11, [],     3, 1>;
defm : PdWriteResYMMPair<WriteFHAddY, [PdFPU0, PdFPFMA], 11, [2, 1], 8, 2>;
defm : X86WriteResPairUnsupported<WriteFHAddZ>;

defm : PdWriteResXMMPair<WritePHAdd,  [PdFPU01, PdFPMAL], 5, [], 3, 1>;
defm : PdWriteResXMMPair<WritePHAddX, [PdFPU01, PdFPMAL], 2>;
defm : X86WriteResPairUnsupported<WritePHAddY>;
defm : X86WriteResPairUnsupported<WritePHAddZ>;

def : InstRW<[WritePHAdd], (instrs PHADDDrr, PHSUBDrr,
                                   PHADDWrr, PHSUBWrr,
                                   PHADDSWrr, PHSUBSWrr,
                                   VPHADDDrr, VPHSUBDrr,
                                   VPHADDWrr, VPHSUBWrr,
                                   VPHADDSWrr, VPHSUBSWrr)>;

def : InstRW<[WritePHAdd.Folded], (instrs PHADDDrm, PHSUBDrm,
                                          PHADDWrm, PHSUBWrm,
                                          PHADDSWrm, PHSUBSWrm,
                                          VPHADDDrm, VPHSUBDrm,
                                          VPHADDWrm, VPHSUBWrm,
                                          VPHADDSWrm, VPHSUBSWrm)>;

////////////////////////////////////////////////////////////////////////////////
// Carry-less multiplication instructions.
////////////////////////////////////////////////////////////////////////////////

defm : PdWriteResXMMPair<WriteCLMul, [PdFPU0, PdFPMMA], 12, [], 5, 1>;

def PdWriteVPCLMULQDQrr : SchedWriteRes<[PdFPU0, PdFPMMA]> {
  let Latency = 13;
  let NumMicroOps = 6;
}
def : InstRW<[PdWriteVPCLMULQDQrr], (instrs VPCLMULQDQrr)>;

////////////////////////////////////////////////////////////////////////////////
// SSE4A instructions.
////////////////////////////////////////////////////////////////////////////////

def PdWriteINSERTQ : SchedWriteRes<[PdFPU01, PdFPMAL]> {
  let Latency = 3;
  let ResourceCycles = [1, 4];
}
def : InstRW<[PdWriteINSERTQ], (instrs INSERTQ, INSERTQI)>;

////////////////////////////////////////////////////////////////////////////////
// AVX instructions.
////////////////////////////////////////////////////////////////////////////////

def PdWriteVBROADCASTYLd : SchedWriteRes<[PdLoad, PdFPU01, PdFPFMA]> {
  let Latency = 6;
  let ResourceCycles = [1, 2, 4];
  let NumMicroOps = 2;
}
def : InstRW<[PdWriteVBROADCASTYLd, ReadAfterLd], (instrs VBROADCASTSDYrm,
                                                          VBROADCASTSSYrm)>;

def PdWriteVZEROALL : SchedWriteRes<[]> {
  let Latency = 90;
  let NumMicroOps = 32;
}
def : InstRW<[PdWriteVZEROALL], (instrs VZEROALL)>;

def PdWriteVZEROUPPER : SchedWriteRes<[]> {
  let Latency = 46;
  let NumMicroOps = 16;
}
def : InstRW<[PdWriteVZEROUPPER], (instrs VZEROUPPER)>;

///////////////////////////////////////////////////////////////////////////////
//  SchedWriteVariant definitions.
///////////////////////////////////////////////////////////////////////////////

def PdWriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
}

def PdWriteZeroIdiom : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteALU]>
]>;
def : InstRW<[PdWriteZeroIdiom], (instrs SUB32rr, SUB64rr,
                                         XOR32rr, XOR64rr)>;

def PdWriteFZeroIdiom : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteFLogic]>
]>;
def : InstRW<[PdWriteFZeroIdiom], (instrs XORPSrr,  VXORPSrr,
                                          XORPDrr,  VXORPDrr,
                                          ANDNPSrr, VANDNPSrr,
                                          ANDNPDrr, VANDNPDrr)>;

// VXORPSYrr, VXORPDYrr, VANDNPSYrr, VANDNPDYrr "zero-idioms" have latency of 1.

def PdWriteVZeroIdiomLogic : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteVecLogic]>
]>;
def : InstRW<[PdWriteVZeroIdiomLogic], (instrs MMX_PXORirr, MMX_PANDNirr)>;

def PdWriteVZeroIdiomLogicX : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteVecLogicX]>
]>;
def : InstRW<[PdWriteVZeroIdiomLogicX], (instrs PXORrr,  VPXORrr,
                                                PANDNrr, VPANDNrr)>;

def PdWriteVZeroIdiomALU : SchedWriteVariant<[
  SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
  SchedVar<MCSchedPredicate<TruePred>,           [WriteVecALU]>
]>;
def : InstRW<[PdWriteVZeroIdiomALU], (instrs MMX_PSUBBirr,   MMX_PSUBDirr,
                                             MMX_PSUBQirr,   MMX_PSUBWirr,
                                             MMX_PCMPGTBirr,
                                             MMX_PCMPGTDirr,
                                             MMX_PCMPGTWirr)>;

def PdWriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [PdWriteZeroLatency]>,
    SchedVar<MCSchedPredicate<TruePred>,           [WriteVecALUX]>
]>;
def : InstRW<[PdWriteVZeroIdiomALUX], (instrs PSUBBrr, VPSUBBrr,
                                              PSUBDrr, VPSUBDrr,
                                              PSUBQrr, VPSUBQrr,
                                              PSUBWrr, VPSUBWrr,
                                              PCMPGTBrr, VPCMPGTBrr,
                                              PCMPGTDrr, VPCMPGTDrr,
                                              PCMPGTWrr, VPCMPGTWrr)>;

///////////////////////////////////////////////////////////////////////////////
// Dependency breaking instructions.
///////////////////////////////////////////////////////////////////////////////

// VPCMPGTQ, but not PCMPGTQ!

def : IsZeroIdiomFunction<[
  // GPR Zero-idioms.
  DepBreakingClass<[ SUB32rr, SUB64rr, XOR32rr, XOR64rr ], ZeroIdiomPredicate>,

  // MMX Zero-idioms.
  DepBreakingClass<[
    MMX_PXORirr, MMX_PANDNirr, MMX_PSUBBirr,
    MMX_PSUBDirr, MMX_PSUBQirr, MMX_PSUBWirr,
    MMX_PSUBSBirr, MMX_PSUBSWirr, MMX_PSUBUSBirr, MMX_PSUBUSWirr,
    MMX_PCMPGTBirr, MMX_PCMPGTDirr, MMX_PCMPGTWirr
  ], ZeroIdiomPredicate>,

  // SSE Zero-idioms.
  DepBreakingClass<[
    // fp variants.
    XORPSrr, XORPDrr, ANDNPSrr, ANDNPDrr,

    // int variants.
    PXORrr, PANDNrr,
    PSUBBrr, PSUBWrr, PSUBDrr, PSUBQrr,
    PSUBSBrr, PSUBSWrr, PSUBUSBrr, PSUBUSWrr,
    PCMPGTBrr, PCMPGTDrr, PCMPGTWrr
  ], ZeroIdiomPredicate>,

  // AVX Zero-idioms.
  DepBreakingClass<[
    // xmm fp variants.
    VXORPSrr, VXORPDrr, VANDNPSrr, VANDNPDrr,

    // xmm int variants.
    VPXORrr, VPANDNrr,
    VPSUBBrr, VPSUBWrr, VPSUBDrr, VPSUBQrr,
    VPSUBSBrr, VPSUBSWrr, VPSUBUSBrr, VPSUBUSWrr,
    VPCMPGTBrr, VPCMPGTWrr, VPCMPGTDrr, VPCMPGTQrr,

    // ymm variants.
    VXORPSYrr, VXORPDYrr, VANDNPSYrr, VANDNPDYrr
  ], ZeroIdiomPredicate>
]>;

def : IsDepBreakingFunction<[
  // GPR
  DepBreakingClass<[ SBB32rr, SBB64rr ], ZeroIdiomPredicate>,
  DepBreakingClass<[ CMP32rr, CMP64rr ], CheckSameRegOperand<0, 1> >,

  // MMX
  DepBreakingClass<[
    MMX_PCMPEQBirr, MMX_PCMPEQDirr, MMX_PCMPEQWirr
  ], ZeroIdiomPredicate>,

  // SSE
  DepBreakingClass<[
    PCMPEQBrr, PCMPEQWrr, PCMPEQDrr
    // But not PCMPEQQrr.
  ], ZeroIdiomPredicate>,

  // AVX
  DepBreakingClass<[
    VPCMPEQBrr, VPCMPEQWrr, VPCMPEQDrr
    // But not VPCMPEQQrr.
  ], ZeroIdiomPredicate>
]>;


} // SchedModel
