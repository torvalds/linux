//=- X86ScheduleZnver1.td - X86 Znver1 Scheduling -------------*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Znver1 to support instruction
// scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def Znver1Model : SchedMachineModel {
  // Zen can decode 4 instructions per cycle.
  let IssueWidth = 4;
  // Based on the reorder buffer we define MicroOpBufferSize
  let MicroOpBufferSize = 192;
  let LoadLatency = 4;
  let MispredictPenalty = 17;
  let HighLatency = 25;
  let PostRAScheduler = 1;

  // FIXME: This variable is required for incomplete model.
  // We haven't catered all instructions.
  // So, we reset the value of this variable so as to
  // say that the model is incomplete.
  let CompleteModel = 0;
}

let SchedModel = Znver1Model in {

// Zen can issue micro-ops to 10 different units in one cycle.
// These are
//  * Four integer ALU units (ZALU0, ZALU1, ZALU2, ZALU3)
//  * Two AGU units (ZAGU0, ZAGU1)
//  * Four FPU units (ZFPU0, ZFPU1, ZFPU2, ZFPU3)
// AGUs feed load store queues @two loads and 1 store per cycle.

// Four ALU units are defined below
def ZnALU0 : ProcResource<1>;
def ZnALU1 : ProcResource<1>;
def ZnALU2 : ProcResource<1>;
def ZnALU3 : ProcResource<1>;

// Two AGU units are defined below
def ZnAGU0 : ProcResource<1>;
def ZnAGU1 : ProcResource<1>;

// Four FPU units are defined below
def ZnFPU0 : ProcResource<1>;
def ZnFPU1 : ProcResource<1>;
def ZnFPU2 : ProcResource<1>;
def ZnFPU3 : ProcResource<1>;

// FPU grouping
def ZnFPU013  : ProcResGroup<[ZnFPU0, ZnFPU1, ZnFPU3]>;
def ZnFPU01   : ProcResGroup<[ZnFPU0, ZnFPU1]>;
def ZnFPU12   : ProcResGroup<[ZnFPU1, ZnFPU2]>;
def ZnFPU13   : ProcResGroup<[ZnFPU1, ZnFPU3]>;
def ZnFPU23   : ProcResGroup<[ZnFPU2, ZnFPU3]>;
def ZnFPU02   : ProcResGroup<[ZnFPU0, ZnFPU2]>;
def ZnFPU03   : ProcResGroup<[ZnFPU0, ZnFPU3]>;

// Below are the grouping of the units.
// Micro-ops to be issued to multiple units are tackled this way.

// ALU grouping
// ZnALU03 - 0,3 grouping
def ZnALU03: ProcResGroup<[ZnALU0, ZnALU3]>;

// 56 Entry (14x4 entries) Int Scheduler
def ZnALU : ProcResGroup<[ZnALU0, ZnALU1, ZnALU2, ZnALU3]> {
  let BufferSize=56;
}

// 28 Entry (14x2) AGU group. AGUs can't be used for all ALU operations
// but are relevant for some instructions
def ZnAGU : ProcResGroup<[ZnAGU0, ZnAGU1]> {
  let BufferSize=28;
}

// Integer Multiplication issued on ALU1.
def ZnMultiplier : ProcResource<1>;

// Integer division issued on ALU2.
def ZnDivider : ProcResource<1>;

// 4 Cycles integer load-to use Latency is captured
def : ReadAdvance<ReadAfterLd, 4>;

// 8 Cycles vector load-to use Latency is captured
def : ReadAdvance<ReadAfterVecLd, 8>;
def : ReadAdvance<ReadAfterVecXLd, 8>;
def : ReadAdvance<ReadAfterVecYLd, 8>;

// The Integer PRF for Zen is 168 entries, and it holds the architectural and
// speculative version of the 64-bit integer registers.
// Reference: "Software Optimization Guide for AMD Family 17h Processors"
def ZnIntegerPRF : RegisterFile<168, [GR64, CCR]>;

// 36 Entry (9x4 entries) floating-point Scheduler
def ZnFPU     : ProcResGroup<[ZnFPU0, ZnFPU1, ZnFPU2, ZnFPU3]> {
let BufferSize=36;
}

// The Zen FP Retire Queue renames SIMD and FP uOps onto a pool of 160 128-bit
// registers. Operations on 256-bit data types are cracked into two COPs.
// Reference: "Software Optimization Guide for AMD Family 17h Processors"
def ZnFpuPRF: RegisterFile<160, [VR64, VR128, VR256], [1, 1, 2]>;

// The unit can track up to 192 macro ops in-flight.
// The retire unit handles in-order commit of up to 8 macro ops per cycle.
// Reference: "Software Optimization Guide for AMD Family 17h Processors"
// To be noted, the retire unit is shared between integer and FP ops.
// In SMT mode it is 96 entry per thread. But, we do not use the conservative
// value here because there is currently no way to fully mode the SMT mode,
// so there is no point in trying.
def ZnRCU : RetireControlUnit<192, 8>;

// FIXME: there are 72 read buffers and 44 write buffers.

// (a folded load is an instruction that loads and does some operation)
// Ex: ADDPD xmm,[mem]-> This instruction has two micro-ops
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops.
//      a. load and
//      b. addpd
// This multiclass is for folded loads for integer units.
multiclass ZnWriteResPair<X86FoldableSchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts,
                          int Lat, list<int> Res = [], int UOps = 1,
                          int LoadLat = 4, int LoadUOps = 1> {
  // Register variant takes 1-cycle on Execution Port.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ResourceCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on ZnAGU
  // adds LoadLat cycles to the latency (default = 4).
  def : WriteRes<SchedRW.Folded, !listconcat([ZnAGU], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ResourceCycles = !if(!empty(Res), [], !listconcat([1], Res));
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// This multiclass is for folded loads for floating point units.
multiclass ZnWriteResFpuPair<X86FoldableSchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts,
                          int Lat, list<int> Res = [], int UOps = 1,
                          int LoadLat = 7, int LoadUOps = 0> {
  // Register variant takes 1-cycle on Execution Port.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ResourceCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on ZnAGU
  // adds LoadLat cycles to the latency (default = 7).
  def : WriteRes<SchedRW.Folded, !listconcat([ZnAGU], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ResourceCycles = !if(!empty(Res), [], !listconcat([1], Res));
    let NumMicroOps = !add(UOps, LoadUOps);
  }
}

// WriteRMW is set for instructions with Memory write
// operation in codegen
def : WriteRes<WriteRMW, [ZnAGU]>;

def : WriteRes<WriteStore,   [ZnAGU]>;
def : WriteRes<WriteStoreNT, [ZnAGU]>;
def : WriteRes<WriteMove,    [ZnALU]>;
def : WriteRes<WriteLoad,    [ZnAGU]> { let Latency = 8; }

def : WriteRes<WriteZero,  []>;
def : WriteRes<WriteLEA, [ZnALU]>;
defm : ZnWriteResPair<WriteALU,   [ZnALU], 1>;
defm : ZnWriteResPair<WriteADC,   [ZnALU], 1>;

defm : ZnWriteResPair<WriteIMul8,     [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul16,    [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul16Imm, [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul16Reg, [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul32,    [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul32Imm, [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul32Reg, [ZnALU1, ZnMultiplier], 4>;
//defm : ZnWriteResPair<WriteIMul64,    [ZnALU1, ZnMultiplier], 4, [1,1], 2>;
//defm : ZnWriteResPair<WriteIMul64Imm, [ZnALU1, ZnMultiplier], 4, [1,1], 2>;
//defm : ZnWriteResPair<WriteIMul64Reg, [ZnALU1, ZnMultiplier], 4, [1,1], 2>;

defm : X86WriteRes<WriteBSWAP32, [ZnALU], 1, [4], 1>;
defm : X86WriteRes<WriteBSWAP64, [ZnALU], 1, [4], 1>;
defm : X86WriteRes<WriteCMPXCHG, [ZnALU], 1, [1], 1>;
defm : X86WriteRes<WriteCMPXCHGRMW,[ZnALU,ZnAGU], 8, [1,1], 5>;
defm : X86WriteRes<WriteXCHG, [ZnALU], 1, [2], 2>;

defm : ZnWriteResPair<WriteShift,    [ZnALU], 1>;
defm : ZnWriteResPair<WriteShiftCL,  [ZnALU], 1>;
defm : ZnWriteResPair<WriteRotate,   [ZnALU], 1>;
defm : ZnWriteResPair<WriteRotateCL, [ZnALU], 1>;

defm : X86WriteRes<WriteSHDrri, [ZnALU], 1, [1], 1>;
defm : X86WriteResUnsupported<WriteSHDrrcl>;
defm : X86WriteResUnsupported<WriteSHDmri>;
defm : X86WriteResUnsupported<WriteSHDmrcl>;

defm : ZnWriteResPair<WriteJump,  [ZnALU], 1>;
defm : ZnWriteResFpuPair<WriteCRC32, [ZnFPU0], 3>;

defm : ZnWriteResPair<WriteCMOV,   [ZnALU], 1>;
defm : ZnWriteResPair<WriteCMOV2,  [ZnALU], 1>;
def  : WriteRes<WriteSETCC,  [ZnALU]>;
def  : WriteRes<WriteSETCCStore,  [ZnALU, ZnAGU]>;
defm : X86WriteRes<WriteLAHFSAHF, [ZnALU], 2, [1], 2>;

defm : X86WriteRes<WriteBitTest,         [ZnALU], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [ZnALU,ZnAGU], 5, [1,1], 2>;
defm : X86WriteRes<WriteBitTestRegLd,    [ZnALU,ZnAGU], 5, [1,1], 2>;
defm : X86WriteRes<WriteBitTestSet,      [ZnALU], 2, [1], 2>;
//defm : X86WriteRes<WriteBitTestSetImmLd, [ZnALU,ZnAGU], 5, [1,1], 2>;
//defm : X86WriteRes<WriteBitTestSetRegLd, [ZnALU,ZnAGU], 5, [1,1], 2>;

// Bit counts.
defm : ZnWriteResPair<WriteBSF, [ZnALU], 3>;
defm : ZnWriteResPair<WriteBSR, [ZnALU], 3>;
defm : ZnWriteResPair<WriteLZCNT,          [ZnALU], 2>;
defm : ZnWriteResPair<WriteTZCNT,          [ZnALU], 2>;
defm : ZnWriteResPair<WritePOPCNT,         [ZnALU], 1>;

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : ZnWriteResPair<WriteBEXTR, [ZnALU], 1>;
//defm : ZnWriteResPair<WriteBLS,   [ZnALU], 2>;
defm : ZnWriteResPair<WriteBZHI,  [ZnALU], 1>;

// IDIV
defm : ZnWriteResPair<WriteDiv8,   [ZnALU2, ZnDivider], 15, [1,15], 1>;
defm : ZnWriteResPair<WriteDiv16,  [ZnALU2, ZnDivider], 17, [1,17], 2>;
defm : ZnWriteResPair<WriteDiv32,  [ZnALU2, ZnDivider], 25, [1,25], 2>;
defm : ZnWriteResPair<WriteDiv64,  [ZnALU2, ZnDivider], 41, [1,41], 2>;
defm : ZnWriteResPair<WriteIDiv8,  [ZnALU2, ZnDivider], 15, [1,15], 1>;
defm : ZnWriteResPair<WriteIDiv16, [ZnALU2, ZnDivider], 17, [1,17], 2>;
defm : ZnWriteResPair<WriteIDiv32, [ZnALU2, ZnDivider], 25, [1,25], 2>;
defm : ZnWriteResPair<WriteIDiv64, [ZnALU2, ZnDivider], 41, [1,41], 2>;

// IMULH
def  : WriteRes<WriteIMulH, [ZnALU1, ZnMultiplier]>{
  let Latency = 4;
}

// Floating point operations
defm : X86WriteRes<WriteFLoad,         [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [ZnAGU,ZnFPU01], 8, [1,1], 1>;
defm : X86WriteRes<WriteFMaskedLoadY,  [ZnAGU,ZnFPU01], 8, [1,2], 2>;
defm : X86WriteRes<WriteFStore,        [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreX,       [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreY,       [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreNT,      [ZnAGU,ZnFPU2], 8, [1,1], 1>;
defm : X86WriteRes<WriteFStoreNTX,     [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteFStoreNTY,     [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteFMaskedStore,  [ZnAGU,ZnFPU01], 4, [1,1], 1>;
defm : X86WriteRes<WriteFMaskedStoreY, [ZnAGU,ZnFPU01], 5, [1,2], 2>;
defm : X86WriteRes<WriteFMove,         [ZnFPU], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [ZnFPU], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [ZnFPU], 1, [1], 1>;

defm : ZnWriteResFpuPair<WriteFAdd,      [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFAddX,     [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFAddY,     [ZnFPU0],  3>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;
defm : ZnWriteResFpuPair<WriteFAdd64,    [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFAdd64X,   [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFAdd64Y,   [ZnFPU0],  3>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;
defm : ZnWriteResFpuPair<WriteFCmp,      [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFCmpX,     [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFCmpY,     [ZnFPU0],  3>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;
defm : ZnWriteResFpuPair<WriteFCmp64,    [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFCmp64X,   [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFCmp64Y,   [ZnFPU0],  3>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;
defm : ZnWriteResFpuPair<WriteFCom,      [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WriteFBlend,    [ZnFPU01], 1>;
defm : ZnWriteResFpuPair<WriteFBlendY,   [ZnFPU01], 1>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;
defm : ZnWriteResFpuPair<WriteFVarBlend, [ZnFPU01], 1>;
defm : ZnWriteResFpuPair<WriteFVarBlendY,[ZnFPU01], 1>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;
defm : ZnWriteResFpuPair<WriteVarBlend,  [ZnFPU0],  1>;
defm : ZnWriteResFpuPair<WriteVarBlendY, [ZnFPU0],  1>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;
defm : ZnWriteResFpuPair<WriteCvtSS2I,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtPS2I,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtPS2IY,  [ZnFPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;
defm : ZnWriteResFpuPair<WriteCvtSD2I,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtPD2I,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtPD2IY,  [ZnFPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;
defm : ZnWriteResFpuPair<WriteCvtI2SS,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtI2PS,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtI2PSY,  [ZnFPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;
defm : ZnWriteResFpuPair<WriteCvtI2SD,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtI2PD,   [ZnFPU3],  5>;
defm : ZnWriteResFpuPair<WriteCvtI2PDY,  [ZnFPU3],  5>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;
defm : ZnWriteResFpuPair<WriteFDiv,      [ZnFPU3], 15>;
defm : ZnWriteResFpuPair<WriteFDivX,     [ZnFPU3], 15>;
//defm : ZnWriteResFpuPair<WriteFDivY,     [ZnFPU3], 15>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;
defm : ZnWriteResFpuPair<WriteFDiv64,    [ZnFPU3], 15>;
defm : ZnWriteResFpuPair<WriteFDiv64X,   [ZnFPU3], 15>;
//defm : ZnWriteResFpuPair<WriteFDiv64Y,   [ZnFPU3], 15>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;
defm : ZnWriteResFpuPair<WriteFSign,     [ZnFPU3],  2>;
defm : ZnWriteResFpuPair<WriteFRnd,      [ZnFPU3],  4, [1], 1, 7, 1>; // FIXME: Should folds require 1 extra uops?
defm : ZnWriteResFpuPair<WriteFRndY,     [ZnFPU3],  4, [1], 1, 7, 1>; // FIXME: Should folds require 1 extra uops?
defm : X86WriteResPairUnsupported<WriteFRndZ>;
defm : ZnWriteResFpuPair<WriteFLogic,    [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteFLogicY,   [ZnFPU],   1>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;
defm : ZnWriteResFpuPair<WriteFTest,     [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteFTestY,    [ZnFPU],   1>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;
defm : ZnWriteResFpuPair<WriteFShuffle,  [ZnFPU12], 1>;
defm : ZnWriteResFpuPair<WriteFShuffleY, [ZnFPU12], 1>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;
defm : ZnWriteResFpuPair<WriteFVarShuffle, [ZnFPU12], 1>;
defm : ZnWriteResFpuPair<WriteFVarShuffleY,[ZnFPU12], 1>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;
defm : ZnWriteResFpuPair<WriteFMul,      [ZnFPU01], 3, [1], 1, 7, 1>;
defm : ZnWriteResFpuPair<WriteFMulX,     [ZnFPU01], 3, [1], 1, 7, 1>;
defm : ZnWriteResFpuPair<WriteFMulY,     [ZnFPU01], 4, [1], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;
defm : ZnWriteResFpuPair<WriteFMul64,    [ZnFPU01], 3, [1], 1, 7, 1>;
defm : ZnWriteResFpuPair<WriteFMul64X,   [ZnFPU01], 3, [1], 1, 7, 1>;
defm : ZnWriteResFpuPair<WriteFMul64Y,   [ZnFPU01], 4, [1], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;
defm : ZnWriteResFpuPair<WriteFMA,       [ZnFPU03], 5>;
defm : ZnWriteResFpuPair<WriteFMAX,      [ZnFPU03], 5>;
defm : ZnWriteResFpuPair<WriteFMAY,      [ZnFPU03], 5>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;
defm : ZnWriteResFpuPair<WriteFRcp,      [ZnFPU01], 5>;
defm : ZnWriteResFpuPair<WriteFRcpX,     [ZnFPU01], 5>;
defm : ZnWriteResFpuPair<WriteFRcpY,     [ZnFPU01], 5, [1], 1, 7, 2>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;
//defm : ZnWriteResFpuPair<WriteFRsqrt,    [ZnFPU02], 5>;
defm : ZnWriteResFpuPair<WriteFRsqrtX,   [ZnFPU01], 5, [1], 1, 7, 1>;
//defm : ZnWriteResFpuPair<WriteFRsqrtY,   [ZnFPU01], 5, [2], 2>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;
defm : ZnWriteResFpuPair<WriteFSqrt,     [ZnFPU3], 20, [20]>;
defm : ZnWriteResFpuPair<WriteFSqrtX,    [ZnFPU3], 20, [20]>;
defm : ZnWriteResFpuPair<WriteFSqrtY,    [ZnFPU3], 28, [28], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;
defm : ZnWriteResFpuPair<WriteFSqrt64,   [ZnFPU3], 20, [20]>;
defm : ZnWriteResFpuPair<WriteFSqrt64X,  [ZnFPU3], 20, [20]>;
defm : ZnWriteResFpuPair<WriteFSqrt64Y,  [ZnFPU3], 40, [40], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;
defm : ZnWriteResFpuPair<WriteFSqrt80,   [ZnFPU3], 20, [20]>;

// Vector integer operations which uses FPU units
defm : X86WriteRes<WriteVecLoad,         [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [ZnAGU], 8, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [ZnAGU,ZnFPU01], 8, [1,2], 2>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [ZnAGU,ZnFPU01], 9, [1,3], 2>;
defm : X86WriteRes<WriteVecStore,        [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreX,       [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreY,       [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreNT,      [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecStoreNTY,     [ZnAGU], 1, [1], 1>;
defm : X86WriteRes<WriteVecMaskedStore,  [ZnAGU,ZnFPU01], 4, [1,1], 1>;
defm : X86WriteRes<WriteVecMaskedStoreY, [ZnAGU,ZnFPU01], 5, [1,2], 2>;
defm : X86WriteRes<WriteVecMove,         [ZnFPU], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [ZnFPU], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [ZnFPU], 2, [1], 2>;
defm : X86WriteRes<WriteVecMoveToGpr,    [ZnFPU2], 2, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [ZnFPU2], 3, [1], 1>;
defm : X86WriteRes<WriteEMMS,            [ZnFPU], 2, [1], 1>;

defm : ZnWriteResFpuPair<WriteVecShift,   [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVecShiftX,  [ZnFPU2],  1>;
defm : ZnWriteResFpuPair<WriteVecShiftY,  [ZnFPU2],  2>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;
defm : ZnWriteResFpuPair<WriteVecShiftImm,  [ZnFPU], 1>;
defm : ZnWriteResFpuPair<WriteVecShiftImmX, [ZnFPU], 1>;
defm : ZnWriteResFpuPair<WriteVecShiftImmY, [ZnFPU], 1>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;
defm : ZnWriteResFpuPair<WriteVecLogic,   [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVecLogicX,  [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVecLogicY,  [ZnFPU],   1>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;
defm : ZnWriteResFpuPair<WriteVecTest,    [ZnFPU12], 1, [2], 1, 7, 1>;
defm : ZnWriteResFpuPair<WriteVecTestY,   [ZnFPU12], 1, [2], 1, 7, 1>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;
defm : ZnWriteResFpuPair<WriteVecALU,     [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVecALUX,    [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVecALUY,    [ZnFPU],   1>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;
defm : ZnWriteResFpuPair<WriteVecIMul,    [ZnFPU0],  4>;
defm : ZnWriteResFpuPair<WriteVecIMulX,   [ZnFPU0],  4>;
defm : ZnWriteResFpuPair<WriteVecIMulY,   [ZnFPU0],  4>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;
defm : ZnWriteResFpuPair<WritePMULLD,     [ZnFPU0],  4, [1], 1, 7, 1>; // FIXME
defm : ZnWriteResFpuPair<WritePMULLDY,    [ZnFPU0],  5, [2], 1, 7, 1>; // FIXME
defm : X86WriteResPairUnsupported<WritePMULLDZ>;
defm : ZnWriteResFpuPair<WriteShuffle,    [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteShuffleX,   [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteShuffleY,   [ZnFPU],   1>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;
defm : ZnWriteResFpuPair<WriteVarShuffle, [ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVarShuffleX,[ZnFPU],   1>;
defm : ZnWriteResFpuPair<WriteVarShuffleY,[ZnFPU],   1>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;
defm : ZnWriteResFpuPair<WriteBlend,      [ZnFPU01], 1>;
defm : ZnWriteResFpuPair<WriteBlendY,     [ZnFPU01], 1>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;
defm : ZnWriteResFpuPair<WriteShuffle256, [ZnFPU],   2>;
defm : ZnWriteResFpuPair<WriteVarShuffle256, [ZnFPU],   2>;
defm : ZnWriteResFpuPair<WritePSADBW,     [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WritePSADBWX,    [ZnFPU0],  3>;
defm : ZnWriteResFpuPair<WritePSADBWY,    [ZnFPU0],  3>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;
defm : ZnWriteResFpuPair<WritePHMINPOS,   [ZnFPU0],  4>;

// Vector Shift Operations
defm : ZnWriteResFpuPair<WriteVarVecShift,  [ZnFPU12], 1>;
defm : ZnWriteResFpuPair<WriteVarVecShiftY, [ZnFPU12], 1>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;

// Vector insert/extract operations.
defm : ZnWriteResFpuPair<WriteVecInsert,   [ZnFPU],   1>;

def : WriteRes<WriteVecExtract, [ZnFPU12, ZnFPU2]> {
  let Latency = 2;
  let ResourceCycles = [1, 2];
}
def : WriteRes<WriteVecExtractSt, [ZnAGU, ZnFPU12, ZnFPU2]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2, 3];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK, [ZnFPU2]>;
def : WriteRes<WriteMMXMOVMSK, [ZnFPU2]>;
def : WriteRes<WriteVecMOVMSK, [ZnFPU2]>;

def : WriteRes<WriteVecMOVMSKY, [ZnFPU2]> {
  let NumMicroOps = 2;
  let Latency = 2;
  let ResourceCycles = [2];
}

// AES Instructions.
defm : ZnWriteResFpuPair<WriteAESDecEnc, [ZnFPU01], 4>;
defm : ZnWriteResFpuPair<WriteAESIMC,    [ZnFPU01], 4>;
defm : ZnWriteResFpuPair<WriteAESKeyGen, [ZnFPU01], 4>;

def : WriteRes<WriteFence,  [ZnAGU]>;
def : WriteRes<WriteNop, []>;

// Following instructions with latency=100 are microcoded.
// We set long latency so as to block the entire pipeline.
defm : ZnWriteResFpuPair<WriteFShuffle256, [ZnFPU], 100>;
defm : ZnWriteResFpuPair<WriteFVarShuffle256, [ZnFPU], 100>;

// Microcoded Instructions
def ZnWriteMicrocoded : SchedWriteRes<[]> {
  let Latency = 100;
}

def : SchedAlias<WriteMicrocoded, ZnWriteMicrocoded>;
def : SchedAlias<WriteFCMOV, ZnWriteMicrocoded>;
def : SchedAlias<WriteSystem, ZnWriteMicrocoded>;
def : SchedAlias<WriteMPSAD, ZnWriteMicrocoded>;
def : SchedAlias<WriteMPSADY, ZnWriteMicrocoded>;
def : SchedAlias<WriteMPSADLd, ZnWriteMicrocoded>;
def : SchedAlias<WriteMPSADYLd, ZnWriteMicrocoded>;
def : SchedAlias<WriteCLMul, ZnWriteMicrocoded>;
def : SchedAlias<WriteCLMulLd, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpIStrM, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpIStrMLd, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpEStrI, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpEStrILd, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpEStrM, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpEStrMLd, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpIStrI, ZnWriteMicrocoded>;
def : SchedAlias<WritePCmpIStrILd, ZnWriteMicrocoded>;
def : SchedAlias<WriteLDMXCSR, ZnWriteMicrocoded>;
def : SchedAlias<WriteSTMXCSR, ZnWriteMicrocoded>;

//=== Regex based InstRW ===//
// Notation:
// - r: register.
// - m = memory.
// - i = immediate
// - mm: 64 bit mmx register.
// - x = 128 bit xmm register.
// - (x)mm = mmx or xmm register.
// - y = 256 bit ymm register.
// - v = any vector register.

//=== Integer Instructions ===//
//-- Move instructions --//
// MOV.
// r16,m.
def : InstRW<[WriteALULd, ReadAfterLd], (instrs MOV16rm)>;

// MOVSX, MOVZX.
// r,m.
def : InstRW<[WriteLoad], (instregex "MOV(S|Z)X32rm(8|16)")>;

// XCHG.
// r,m.
def ZnWriteXCHGrm : SchedWriteRes<[ZnAGU, ZnALU]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[ZnWriteXCHGrm, ReadAfterLd], (instregex "XCHG(8|16|32|64)rm")>;

def : InstRW<[WriteMicrocoded], (instrs XLAT)>;

// POP16.
// r.
def ZnWritePop16r : SchedWriteRes<[ZnAGU]>{
  let Latency = 5;
  let NumMicroOps = 2;
}
def : InstRW<[ZnWritePop16r], (instrs POP16rmm)>;
def : InstRW<[WriteMicrocoded], (instregex "POPF(16|32)")>;
def : InstRW<[WriteMicrocoded], (instregex "POPA(16|32)")>;


// PUSH.
// r. Has default values.
// m.
def ZnWritePUSH : SchedWriteRes<[ZnAGU]>{
  let Latency = 4;
}
def : InstRW<[ZnWritePUSH], (instregex "PUSH(16|32)rmm")>;

//PUSHF
def : InstRW<[WriteMicrocoded], (instregex "PUSHF(16|32)")>;

// PUSHA.
def ZnWritePushA : SchedWriteRes<[ZnAGU]> {
  let Latency = 8;
}
def : InstRW<[ZnWritePushA], (instregex "PUSHA(16|32)")>;

//LAHF
def : InstRW<[WriteMicrocoded], (instrs LAHF)>;

// MOVBE.
// r,m.
def ZnWriteMOVBE : SchedWriteRes<[ZnAGU, ZnALU]> {
  let Latency = 5;
}
def : InstRW<[ZnWriteMOVBE, ReadAfterLd], (instregex "MOVBE(16|32|64)rm")>;

// m16,r16.
def : InstRW<[ZnWriteMOVBE], (instregex "MOVBE(16|32|64)mr")>;

//-- Arithmetic instructions --//

// ADD SUB.
// m,r/i.
def : InstRW<[WriteALULd], (instregex "(ADD|SUB)(8|16|32|64)m(r|i)",
                          "(ADD|SUB)(8|16|32|64)mi8",
                          "(ADD|SUB)64mi32")>;

// ADC SBB.
// m,r/i.
def : InstRW<[WriteALULd],
             (instregex "(ADC|SBB)(8|16|32|64)m(r|i)",
              "(ADC|SBB)(16|32|64)mi8",
              "(ADC|SBB)64mi32")>;

// INC DEC NOT NEG.
// m.
def : InstRW<[WriteALULd],
             (instregex "(INC|DEC|NOT|NEG)(8|16|32|64)m")>;

// MUL IMUL.
// r16.
def ZnWriteMul16 : SchedWriteRes<[ZnALU1, ZnMultiplier]> {
  let Latency = 3;
}
def : SchedAlias<WriteIMul16, ZnWriteMul16>;
def : SchedAlias<WriteIMul16Imm, ZnWriteMul16>; // TODO: is this right?
def : SchedAlias<WriteIMul16Reg, ZnWriteMul16>; // TODO: is this right?
def : SchedAlias<WriteIMul16ImmLd, ZnWriteMul16>; // TODO: this is definitely wrong but matches what the instregex did.
def : SchedAlias<WriteIMul16RegLd, ZnWriteMul16>; // TODO: this is definitely wrong but matches what the instregex did.

// m16.
def ZnWriteMul16Ld : SchedWriteRes<[ZnAGU, ZnALU1, ZnMultiplier]> {
  let Latency = 8;
}
def : SchedAlias<WriteIMul16Ld, ZnWriteMul16Ld>;

// r32.
def ZnWriteMul32 : SchedWriteRes<[ZnALU1, ZnMultiplier]> {
  let Latency = 3;
}
def : SchedAlias<WriteIMul32, ZnWriteMul32>;
def : SchedAlias<WriteIMul32Imm, ZnWriteMul32>; // TODO: is this right?
def : SchedAlias<WriteIMul32Reg, ZnWriteMul32>; // TODO: is this right?
def : SchedAlias<WriteIMul32ImmLd, ZnWriteMul32>; // TODO: this is definitely wrong but matches what the instregex did.
def : SchedAlias<WriteIMul32RegLd, ZnWriteMul32>; // TODO: this is definitely wrong but matches what the instregex did.

// m32.
def ZnWriteMul32Ld : SchedWriteRes<[ZnAGU, ZnALU1, ZnMultiplier]> {
  let Latency = 8;
}
def : SchedAlias<WriteIMul32Ld, ZnWriteMul32Ld>;

// r64.
def ZnWriteMul64 : SchedWriteRes<[ZnALU1, ZnMultiplier]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteIMul64, ZnWriteMul64>;
def : SchedAlias<WriteIMul64Imm, ZnWriteMul64>; // TODO: is this right?
def : SchedAlias<WriteIMul64Reg, ZnWriteMul64>; // TODO: is this right?
def : SchedAlias<WriteIMul64ImmLd, ZnWriteMul64>; // TODO: this is definitely wrong but matches what the instregex did.
def : SchedAlias<WriteIMul64RegLd, ZnWriteMul64>; // TODO: this is definitely wrong but matches what the instregex did.

// m64.
def ZnWriteMul64Ld : SchedWriteRes<[ZnAGU, ZnALU1, ZnMultiplier]> {
  let Latency = 9;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteIMul64Ld, ZnWriteMul64Ld>;

// MULX.
// r32,r32,r32.
def ZnWriteMulX32 : SchedWriteRes<[ZnALU1, ZnMultiplier]> {
  let Latency = 3;
  let ResourceCycles = [1, 2];
}
def : InstRW<[ZnWriteMulX32], (instrs MULX32rr)>;

// r32,r32,m32.
def ZnWriteMulX32Ld : SchedWriteRes<[ZnAGU, ZnALU1, ZnMultiplier]> {
  let Latency = 8;
  let ResourceCycles = [1, 2, 2];
}
def : InstRW<[ZnWriteMulX32Ld, ReadAfterLd], (instrs MULX32rm)>;

// r64,r64,r64.
def ZnWriteMulX64 : SchedWriteRes<[ZnALU1]> {
  let Latency = 3;
}
def : InstRW<[ZnWriteMulX64], (instrs MULX64rr)>;

// r64,r64,m64.
def ZnWriteMulX64Ld : SchedWriteRes<[ZnAGU, ZnALU1, ZnMultiplier]> {
  let Latency = 8;
}
def : InstRW<[ZnWriteMulX64Ld, ReadAfterLd], (instrs MULX64rm)>;

//-- Control transfer instructions --//

// J(E|R)CXZ.
def ZnWriteJCXZ : SchedWriteRes<[ZnALU03]>;
def : InstRW<[ZnWriteJCXZ], (instrs JCXZ, JECXZ, JRCXZ)>;

// INTO
def : InstRW<[WriteMicrocoded], (instrs INTO)>;

// LOOP.
def ZnWriteLOOP : SchedWriteRes<[ZnALU03]>;
def : InstRW<[ZnWriteLOOP], (instrs LOOP)>;

// LOOP(N)E, LOOP(N)Z
def ZnWriteLOOPE : SchedWriteRes<[ZnALU03]>;
def : InstRW<[ZnWriteLOOPE], (instrs LOOPE, LOOPNE)>;

// CALL.
// r.
def ZnWriteCALLr : SchedWriteRes<[ZnAGU, ZnALU03]>;
def : InstRW<[ZnWriteCALLr], (instregex "CALL(16|32)r")>;

def : InstRW<[WriteMicrocoded], (instregex "CALL(16|32)m")>;

// RET.
def ZnWriteRET : SchedWriteRes<[ZnALU03]> {
  let NumMicroOps = 2;
}
def : InstRW<[ZnWriteRET], (instregex "RET(L|Q|W)", "LRET(L|Q|W)",
                            "IRET(16|32|64)")>;

//-- Logic instructions --//

// AND OR XOR.
// m,r/i.
def : InstRW<[WriteALULd],
             (instregex "(AND|OR|XOR)(8|16|32|64)m(r|i)",
              "(AND|OR|XOR)(8|16|32|64)mi8", "(AND|OR|XOR)64mi32")>;

// Define ALU latency variants
def ZnWriteALULat2 : SchedWriteRes<[ZnALU]> {
  let Latency = 2;
}
def ZnWriteALULat2Ld : SchedWriteRes<[ZnAGU, ZnALU]> {
  let Latency = 6;
}

// BTR BTS BTC.
// m,r,i.
def ZnWriteBTRSCm : SchedWriteRes<[ZnAGU, ZnALU]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
// m,r,i.
def : SchedAlias<WriteBitTestSetImmRMW, ZnWriteBTRSCm>;
def : SchedAlias<WriteBitTestSetRegRMW, ZnWriteBTRSCm>;

// BLSI BLSMSK BLSR.
// r,r.
def : SchedAlias<WriteBLS, ZnWriteALULat2>;
// r,m.
def : SchedAlias<WriteBLSLd, ZnWriteALULat2Ld>;

// CLD STD.
def : InstRW<[WriteALU], (instrs STD, CLD)>;

// PDEP PEXT.
// r,r,r.
def : InstRW<[WriteMicrocoded], (instregex "PDEP(32|64)rr", "PEXT(32|64)rr")>;
// r,r,m.
def : InstRW<[WriteMicrocoded], (instregex "PDEP(32|64)rm", "PEXT(32|64)rm")>;

// RCR RCL.
// m,i.
def : InstRW<[WriteMicrocoded], (instregex "RC(R|L)(8|16|32|64)m(1|i|CL)")>;

// SHR SHL SAR.
// m,i.
def : InstRW<[WriteShiftLd], (instregex "S(A|H)(R|L)(8|16|32|64)m(i|1)")>;

// SHRD SHLD.
// m,r
def : InstRW<[WriteShiftLd], (instregex "SH(R|L)D(16|32|64)mri8")>;

// r,r,cl.
def : InstRW<[WriteMicrocoded], (instregex "SH(R|L)D(16|32|64)rrCL")>;

// m,r,cl.
def : InstRW<[WriteMicrocoded], (instregex "SH(R|L)D(16|32|64)mrCL")>;

//-- Misc instructions --//
// CMPXCHG8B.
def ZnWriteCMPXCHG8B : SchedWriteRes<[ZnAGU, ZnALU]> {
  let NumMicroOps = 18;
}
def : InstRW<[ZnWriteCMPXCHG8B], (instrs CMPXCHG8B)>;

def : InstRW<[WriteMicrocoded], (instrs CMPXCHG16B)>;

// LEAVE
def ZnWriteLEAVE : SchedWriteRes<[ZnALU, ZnAGU]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def : InstRW<[ZnWriteLEAVE], (instregex "LEAVE")>;

// PAUSE.
def : InstRW<[WriteMicrocoded], (instrs PAUSE)>;

// RDTSC.
def : InstRW<[WriteMicrocoded], (instregex "RDTSC")>;

// RDPMC.
def : InstRW<[WriteMicrocoded], (instrs RDPMC)>;

// RDRAND.
def : InstRW<[WriteMicrocoded], (instrs RDRAND16r, RDRAND32r, RDRAND64r)>;

// XGETBV.
def : InstRW<[WriteMicrocoded], (instrs XGETBV)>;

//-- String instructions --//
// CMPS.
def : InstRW<[WriteMicrocoded], (instregex "CMPS(B|L|Q|W)")>;

// LODSB/W.
def : InstRW<[WriteMicrocoded], (instregex "LODS(B|W)")>;

// LODSD/Q.
def : InstRW<[WriteMicrocoded], (instregex "LODS(L|Q)")>;

// MOVS.
def : InstRW<[WriteMicrocoded], (instregex "MOVS(B|L|Q|W)")>;

// SCAS.
def : InstRW<[WriteMicrocoded], (instregex "SCAS(B|W|L|Q)")>;

// STOS
def : InstRW<[WriteMicrocoded], (instregex "STOS(B|L|Q|W)")>;

// XADD.
def ZnXADD : SchedWriteRes<[ZnALU]>;
def : InstRW<[ZnXADD], (instregex "XADD(8|16|32|64)rr")>;
def : InstRW<[WriteMicrocoded], (instregex "XADD(8|16|32|64)rm")>;

//=== Floating Point x87 Instructions ===//
//-- Move instructions --//

def ZnWriteFLDr : SchedWriteRes<[ZnFPU13]> ;

def ZnWriteSTr: SchedWriteRes<[ZnFPU23]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// LD_F.
// r.
def : InstRW<[ZnWriteFLDr], (instrs LD_Frr)>;

// m.
def ZnWriteLD_F80m : SchedWriteRes<[ZnAGU, ZnFPU13]> {
  let NumMicroOps = 2;
}
def : InstRW<[ZnWriteLD_F80m], (instrs LD_F80m)>;

// FBLD.
def : InstRW<[WriteMicrocoded], (instrs FBLDm)>;

// FST(P).
// r.
def : InstRW<[ZnWriteSTr], (instregex "ST_(F|FP)rr")>;

// m80.
def ZnWriteST_FP80m : SchedWriteRes<[ZnAGU, ZnFPU23]> {
  let Latency = 5;
}
def : InstRW<[ZnWriteST_FP80m], (instrs ST_FP80m)>;

// FBSTP.
// m80.
def : InstRW<[WriteMicrocoded], (instrs FBSTPm)>;

def ZnWriteFXCH : SchedWriteRes<[ZnFPU]>;

// FXCHG.
def : InstRW<[ZnWriteFXCH], (instrs XCH_F)>;

// FILD.
def ZnWriteFILD : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 11;
  let NumMicroOps = 2;
}
def : InstRW<[ZnWriteFILD], (instregex "ILD_F(16|32|64)m")>;

// FIST(P) FISTTP.
def ZnWriteFIST : SchedWriteRes<[ZnAGU, ZnFPU23]> {
  let Latency = 12;
}
def : InstRW<[ZnWriteFIST], (instregex "IS(T|TT)_(F|FP)(16|32|64)m")>;

def ZnWriteFPU13 : SchedWriteRes<[ZnAGU, ZnFPU13]> {
  let Latency = 8;
}

def ZnWriteFPU3 : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 11;
}

// FLDZ.
def : SchedAlias<WriteFLD0, ZnWriteFPU13>;

// FLD1.
def : SchedAlias<WriteFLD1, ZnWriteFPU3>;

// FLDPI FLDL2E etc.
def : SchedAlias<WriteFLDC, ZnWriteFPU3>;

// FNSTSW.
// AX.
def : InstRW<[WriteMicrocoded], (instrs FNSTSW16r)>;

// m16.
def : InstRW<[WriteMicrocoded], (instrs FNSTSWm)>;

// FLDCW.
def : InstRW<[WriteMicrocoded], (instrs FLDCW16m)>;

// FNSTCW.
def : InstRW<[WriteMicrocoded], (instrs FNSTCW16m)>;

// FINCSTP FDECSTP.
def : InstRW<[ZnWriteFPU3], (instrs FINCSTP, FDECSTP)>;

// FFREE.
def : InstRW<[ZnWriteFPU3], (instregex "FFREE")>;

// FNSAVE.
def : InstRW<[WriteMicrocoded], (instrs FSAVEm)>;

// FRSTOR.
def : InstRW<[WriteMicrocoded], (instrs FRSTORm)>;

//-- Arithmetic instructions --//

def ZnWriteFPU3Lat1 : SchedWriteRes<[ZnFPU3]> ;

def ZnWriteFPU0Lat1 : SchedWriteRes<[ZnFPU0]> ;

def ZnWriteFPU0Lat1Ld : SchedWriteRes<[ZnAGU, ZnFPU0]> {
  let Latency = 8;
}

// FCHS.
def : InstRW<[ZnWriteFPU3Lat1], (instregex "CHS_F")>;

// FCOM(P) FUCOM(P).
// r.
def : InstRW<[ZnWriteFPU0Lat1], (instregex "COM(P?)_FST0r", "UCOM_F(P?)r")>;
// m.
def : InstRW<[ZnWriteFPU0Lat1Ld], (instregex "FCOM(P?)(32|64)m")>;

// FCOMPP FUCOMPP.
// r.
def : InstRW<[ZnWriteFPU0Lat1], (instrs FCOMPP, UCOM_FPPr)>;

def ZnWriteFPU02 : SchedWriteRes<[ZnAGU, ZnFPU02]>
{
  let Latency = 9;
}

// FCOMI(P) FUCOMI(P).
// m.
def : InstRW<[ZnWriteFPU02], (instrs COM_FIPr, COM_FIr, UCOM_FIPr, UCOM_FIr)>;

def ZnWriteFPU03 : SchedWriteRes<[ZnAGU, ZnFPU03]>
{
  let Latency = 12;
  let NumMicroOps = 2;
  let ResourceCycles = [1,3];
}

// FICOM(P).
def : InstRW<[ZnWriteFPU03], (instregex "FICOM(P?)(16|32)m")>;

// FTST.
def : InstRW<[ZnWriteFPU0Lat1], (instregex "TST_F")>;

// FXAM.
def : InstRW<[ZnWriteFPU3Lat1], (instrs FXAM)>;

// FPREM.
def : InstRW<[WriteMicrocoded], (instrs FPREM)>;

// FPREM1.
def : InstRW<[WriteMicrocoded], (instrs FPREM1)>;

// FRNDINT.
def : InstRW<[WriteMicrocoded], (instrs FRNDINT)>;

// FSCALE.
def : InstRW<[WriteMicrocoded], (instrs FSCALE)>;

// FXTRACT.
def : InstRW<[WriteMicrocoded], (instrs FXTRACT)>;

// FNOP.
def : InstRW<[ZnWriteFPU0Lat1], (instrs FNOP)>;

// WAIT.
def : InstRW<[ZnWriteFPU0Lat1], (instrs WAIT)>;

// FNCLEX.
def : InstRW<[WriteMicrocoded], (instrs FNCLEX)>;

// FNINIT.
def : InstRW<[WriteMicrocoded], (instrs FNINIT)>;

//=== Integer MMX and XMM Instructions ===//

// PACKSSWB/DW.
// mm <- mm.
def ZnWriteFPU12 : SchedWriteRes<[ZnFPU12]> ;
def ZnWriteFPU12Y : SchedWriteRes<[ZnFPU12]> {
  let NumMicroOps = 2;
}
def ZnWriteFPU12m : SchedWriteRes<[ZnAGU, ZnFPU12]> ;
def ZnWriteFPU12Ym : SchedWriteRes<[ZnAGU, ZnFPU12]> {
  let Latency = 8;
  let NumMicroOps = 2;
}

def : InstRW<[ZnWriteFPU12], (instrs MMX_PACKSSDWirr,
                                     MMX_PACKSSWBirr,
                                     MMX_PACKUSWBirr)>;
def : InstRW<[ZnWriteFPU12m], (instrs MMX_PACKSSDWirm,
                                      MMX_PACKSSWBirm,
                                      MMX_PACKUSWBirm)>;

// VPMOVSX/ZX BW BD BQ WD WQ DQ.
// y <- x.
def : InstRW<[ZnWriteFPU12Y], (instregex "VPMOV(SX|ZX)(BW|BD|BQ|WD|WQ|DQ)Yrr")>;
def : InstRW<[ZnWriteFPU12Ym], (instregex "VPMOV(SX|ZX)(BW|BD|BQ|WD|WQ|DQ)Yrm")>;

def ZnWriteFPU013 : SchedWriteRes<[ZnFPU013]> ;
def ZnWriteFPU013Y : SchedWriteRes<[ZnFPU013]> {
  let Latency = 2;
}
def ZnWriteFPU013m : SchedWriteRes<[ZnAGU, ZnFPU013]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def ZnWriteFPU013Ld : SchedWriteRes<[ZnAGU, ZnFPU013]> {
  let Latency = 8;
  let NumMicroOps = 2;
}
def ZnWriteFPU013LdY : SchedWriteRes<[ZnAGU, ZnFPU013]> {
  let Latency = 9;
  let NumMicroOps = 2;
}

// PBLENDW.
// x,x,i / v,v,v,i
def : InstRW<[ZnWriteFPU013], (instregex "(V?)PBLENDWrri")>;
// ymm
def : InstRW<[ZnWriteFPU013Y], (instrs VPBLENDWYrri)>;

// x,m,i / v,v,m,i
def : InstRW<[ZnWriteFPU013Ld], (instregex "(V?)PBLENDWrmi")>;
// y,m,i
def : InstRW<[ZnWriteFPU013LdY], (instrs VPBLENDWYrmi)>;

def ZnWriteFPU01 : SchedWriteRes<[ZnFPU01]> ;
def ZnWriteFPU01Y : SchedWriteRes<[ZnFPU01]> {
  let NumMicroOps = 2;
}

// VPBLENDD.
// v,v,v,i.
def : InstRW<[ZnWriteFPU01], (instrs VPBLENDDrri)>;
// ymm
def : InstRW<[ZnWriteFPU01Y], (instrs VPBLENDDYrri)>;

// v,v,m,i
def ZnWriteFPU01Op2 : SchedWriteRes<[ZnAGU, ZnFPU01]> {
  let NumMicroOps = 2;
  let Latency = 8;
  let ResourceCycles = [1, 2];
}
def ZnWriteFPU01Op2Y : SchedWriteRes<[ZnAGU, ZnFPU01]> {
  let NumMicroOps = 2;
  let Latency = 9;
  let ResourceCycles = [1, 3];
}
def : InstRW<[ZnWriteFPU01Op2], (instrs VPBLENDDrmi)>;
def : InstRW<[ZnWriteFPU01Op2Y], (instrs VPBLENDDYrmi)>;

// MASKMOVQ.
def : InstRW<[WriteMicrocoded], (instregex "MMX_MASKMOVQ(64)?")>;

// MASKMOVDQU.
def : InstRW<[WriteMicrocoded], (instregex "(V?)MASKMOVDQU(64)?")>;

// VPMASKMOVD.
// ymm
def : InstRW<[WriteMicrocoded],
                               (instregex "VPMASKMOVD(Y?)rm")>;
// m, v,v.
def : InstRW<[WriteMicrocoded], (instregex "VPMASKMOV(D|Q)(Y?)mr")>;

// VPBROADCAST B/W.
// x, m8/16.
def ZnWriteVPBROADCAST128Ld : SchedWriteRes<[ZnAGU, ZnFPU12]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2];
}
def : InstRW<[ZnWriteVPBROADCAST128Ld],
                                     (instregex "VPBROADCAST(B|W)rm")>;

// y, m8/16
def ZnWriteVPBROADCAST256Ld : SchedWriteRes<[ZnAGU, ZnFPU1]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2];
}
def : InstRW<[ZnWriteVPBROADCAST256Ld],
                                     (instregex "VPBROADCAST(B|W)Yrm")>;

// VPGATHER.
def : InstRW<[WriteMicrocoded], (instregex "VPGATHER(Q|D)(Q|D)(Y?)rm")>;

//-- Arithmetic instructions --//

// HADD, HSUB PS/PD
// PHADD|PHSUB (S) W/D.
def : SchedAlias<WritePHAdd,    ZnWriteMicrocoded>;
def : SchedAlias<WritePHAddLd,  ZnWriteMicrocoded>;
def : SchedAlias<WritePHAddX,   ZnWriteMicrocoded>;
def : SchedAlias<WritePHAddXLd, ZnWriteMicrocoded>;
def : SchedAlias<WritePHAddY,   ZnWriteMicrocoded>;
def : SchedAlias<WritePHAddYLd, ZnWriteMicrocoded>;

// PCMPGTQ.
def ZnWritePCMPGTQr : SchedWriteRes<[ZnFPU03]>;
def : InstRW<[ZnWritePCMPGTQr], (instregex "(V?)PCMPGTQ(Y?)rr")>;

// x <- x,m.
def ZnWritePCMPGTQm : SchedWriteRes<[ZnAGU, ZnFPU03]> {
  let Latency = 8;
}
// ymm.
def ZnWritePCMPGTQYm : SchedWriteRes<[ZnAGU, ZnFPU03]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,2];
}
def : InstRW<[ZnWritePCMPGTQm], (instregex "(V?)PCMPGTQrm")>;
def : InstRW<[ZnWritePCMPGTQYm], (instrs VPCMPGTQYrm)>;

//-- Logic instructions --//

// PSLL,PSRL,PSRA W/D/Q.
// x,x / v,v,x.
def ZnWritePShift  : SchedWriteRes<[ZnFPU2]> ;
def ZnWritePShiftY : SchedWriteRes<[ZnFPU2]> {
  let Latency = 2;
}

// PSLL,PSRL DQ.
def : InstRW<[ZnWritePShift], (instregex "(V?)PS(R|L)LDQri")>;
def : InstRW<[ZnWritePShiftY], (instregex "(V?)PS(R|L)LDQYri")>;

//=== Floating Point XMM and YMM Instructions ===//
//-- Move instructions --//

// VPERM2F128.
def : InstRW<[WriteMicrocoded], (instrs VPERM2F128rr)>;
def : InstRW<[WriteMicrocoded], (instrs VPERM2F128rm)>;

def ZnWriteBROADCAST : SchedWriteRes<[ZnAGU, ZnFPU13]> {
  let NumMicroOps = 2;
  let Latency = 8;
}
// VBROADCASTF128.
def : InstRW<[ZnWriteBROADCAST], (instrs VBROADCASTF128)>;

// EXTRACTPS.
// r32,x,i.
def ZnWriteEXTRACTPSr : SchedWriteRes<[ZnFPU12, ZnFPU2]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2];
}
def : InstRW<[ZnWriteEXTRACTPSr], (instregex "(V?)EXTRACTPSrr")>;

def ZnWriteEXTRACTPSm : SchedWriteRes<[ZnAGU,ZnFPU12, ZnFPU2]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ResourceCycles = [5, 1, 2];
}
// m32,x,i.
def : InstRW<[ZnWriteEXTRACTPSm], (instregex "(V?)EXTRACTPSmr")>;

// VEXTRACTF128.
// x,y,i.
def : InstRW<[ZnWriteFPU013], (instrs VEXTRACTF128rr)>;

// m128,y,i.
def : InstRW<[ZnWriteFPU013m], (instrs VEXTRACTF128mr)>;

def ZnWriteVINSERT128r: SchedWriteRes<[ZnFPU013]> {
  let Latency = 2;
  let ResourceCycles = [2];
}
def ZnWriteVINSERT128Ld: SchedWriteRes<[ZnAGU,ZnFPU013]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2];
}
// VINSERTF128.
// y,y,x,i.
def : InstRW<[ZnWriteVINSERT128r], (instrs VINSERTF128rr)>;
def : InstRW<[ZnWriteVINSERT128Ld], (instrs VINSERTF128rm)>;

// VGATHER.
def : InstRW<[WriteMicrocoded], (instregex "VGATHER(Q|D)(PD|PS)(Y?)rm")>;

//-- Conversion instructions --//
def ZnWriteCVTPD2PSr: SchedWriteRes<[ZnFPU3]> {
  let Latency = 4;
}
def ZnWriteCVTPD2PSYr: SchedWriteRes<[ZnFPU3]> {
  let Latency = 5;
}

// CVTPD2PS.
// x,x.
def : SchedAlias<WriteCvtPD2PS,  ZnWriteCVTPD2PSr>;
// y,y.
def : SchedAlias<WriteCvtPD2PSY, ZnWriteCVTPD2PSYr>;
// z,z.
defm : X86WriteResUnsupported<WriteCvtPD2PSZ>;

def ZnWriteCVTPD2PSLd: SchedWriteRes<[ZnAGU,ZnFPU03]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [1,2];
}
// x,m128.
def : SchedAlias<WriteCvtPD2PSLd, ZnWriteCVTPD2PSLd>;

// x,m256.
def ZnWriteCVTPD2PSYLd : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 11;
}
def : SchedAlias<WriteCvtPD2PSYLd, ZnWriteCVTPD2PSYLd>;
// z,m512
defm : X86WriteResUnsupported<WriteCvtPD2PSZLd>;

// CVTSD2SS.
// x,x.
// Same as WriteCVTPD2PSr
def : SchedAlias<WriteCvtSD2SS, ZnWriteCVTPD2PSr>;

// x,m64.
def : SchedAlias<WriteCvtSD2SSLd, ZnWriteCVTPD2PSLd>;

// CVTPS2PD.
// x,x.
def ZnWriteCVTPS2PDr : SchedWriteRes<[ZnFPU3]> {
  let Latency = 3;
}
def : SchedAlias<WriteCvtPS2PD, ZnWriteCVTPS2PDr>;

// x,m64.
// y,m128.
def ZnWriteCVTPS2PDLd : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 10;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteCvtPS2PDLd, ZnWriteCVTPS2PDLd>;
def : SchedAlias<WriteCvtPS2PDYLd, ZnWriteCVTPS2PDLd>;
defm : X86WriteResUnsupported<WriteCvtPS2PDZLd>;

// y,x.
def ZnWriteVCVTPS2PDY : SchedWriteRes<[ZnFPU3]> {
  let Latency = 3;
}
def : SchedAlias<WriteCvtPS2PDY, ZnWriteVCVTPS2PDY>;
defm : X86WriteResUnsupported<WriteCvtPS2PDZ>;

// CVTSS2SD.
// x,x.
def ZnWriteCVTSS2SDr : SchedWriteRes<[ZnFPU3]> {
  let Latency = 4;
}
def : SchedAlias<WriteCvtSS2SD, ZnWriteCVTSS2SDr>;

// x,m32.
def ZnWriteCVTSS2SDLd : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2];
}
def : SchedAlias<WriteCvtSS2SDLd, ZnWriteCVTSS2SDLd>;

def ZnWriteCVTDQ2PDr: SchedWriteRes<[ZnFPU12,ZnFPU3]> {
  let Latency = 5;
}
// CVTDQ2PD.
// x,x.
def : InstRW<[ZnWriteCVTDQ2PDr], (instregex "(V)?CVTDQ2PDrr")>;

// Same as xmm
// y,x.
def : InstRW<[ZnWriteCVTDQ2PDr], (instrs VCVTDQ2PDYrr)>;

def ZnWriteCVTPD2DQr: SchedWriteRes<[ZnFPU12, ZnFPU3]> {
  let Latency = 5;
}
// CVT(T)PD2DQ.
// x,x.
def : InstRW<[ZnWriteCVTDQ2PDr], (instregex "(V?)CVT(T?)PD2DQrr")>;

def ZnWriteCVTPD2DQLd: SchedWriteRes<[ZnAGU,ZnFPU12,ZnFPU3]> {
  let Latency = 12;
  let NumMicroOps = 2;
}
// x,m128.
def : InstRW<[ZnWriteCVTPD2DQLd], (instregex "(V?)CVT(T?)PD2DQrm")>;
// same as xmm handling
// x,y.
def : InstRW<[ZnWriteCVTPD2DQr], (instregex "VCVT(T?)PD2DQYrr")>;
// x,m256.
def : InstRW<[ZnWriteCVTPD2DQLd], (instregex "VCVT(T?)PD2DQYrm")>;

def ZnWriteCVTPS2PIr: SchedWriteRes<[ZnFPU3]> {
  let Latency = 4;
}
// CVT(T)PS2PI.
// mm,x.
def : InstRW<[ZnWriteCVTPS2PIr], (instregex "MMX_CVT(T?)PS2PIirr")>;

// CVTPI2PD.
// x,mm.
def : InstRW<[ZnWriteCVTPS2PDr], (instrs MMX_CVTPI2PDirr)>;

// CVT(T)PD2PI.
// mm,x.
def : InstRW<[ZnWriteCVTPS2PIr], (instregex "MMX_CVT(T?)PD2PIirr")>;

def ZnWriteCVSTSI2SSr: SchedWriteRes<[ZnFPU3]> {
  let Latency = 5;
}

// same as CVTPD2DQr
// CVT(T)SS2SI.
// r32,x.
def : InstRW<[ZnWriteCVTPD2DQr], (instregex "(V?)CVT(T?)SS2SI(64)?rr")>;
// same as CVTPD2DQm
// r32,m32.
def : InstRW<[ZnWriteCVTPD2DQLd], (instregex "(V?)CVT(T?)SS2SI(64)?rm")>;

def ZnWriteCVSTSI2SDr: SchedWriteRes<[ZnFPU013, ZnFPU3]> {
  let Latency = 5;
}
// CVTSI2SD.
// x,r32/64.
def : InstRW<[ZnWriteCVSTSI2SDr], (instregex "(V?)CVTSI(64)?2SDrr")>;


def ZnWriteCVSTSI2SIr: SchedWriteRes<[ZnFPU3, ZnFPU2]> {
  let Latency = 5;
}
def ZnWriteCVSTSI2SILd: SchedWriteRes<[ZnAGU, ZnFPU3, ZnFPU2]> {
  let Latency = 12;
}
// CVTSD2SI.
// r32/64
def : InstRW<[ZnWriteCVSTSI2SIr], (instregex "(V?)CVT(T?)SD2SI(64)?rr")>;
// r32,m32.
def : InstRW<[ZnWriteCVSTSI2SILd], (instregex "(V?)CVT(T?)SD2SI(64)?rm")>;

// VCVTPS2PH.
// x,v,i.
def : SchedAlias<WriteCvtPS2PH,    ZnWriteMicrocoded>;
def : SchedAlias<WriteCvtPS2PHY,   ZnWriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;
// m,v,i.
def : SchedAlias<WriteCvtPS2PHSt,  ZnWriteMicrocoded>;
def : SchedAlias<WriteCvtPS2PHYSt, ZnWriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

// VCVTPH2PS.
// v,x.
def : SchedAlias<WriteCvtPH2PS,    ZnWriteMicrocoded>;
def : SchedAlias<WriteCvtPH2PSY,   ZnWriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPH2PSZ>;
// v,m.
def : SchedAlias<WriteCvtPH2PSLd,  ZnWriteMicrocoded>;
def : SchedAlias<WriteCvtPH2PSYLd, ZnWriteMicrocoded>;
defm : X86WriteResUnsupported<WriteCvtPH2PSZLd>;

//-- SSE4A instructions --//
// EXTRQ
def ZnWriteEXTRQ: SchedWriteRes<[ZnFPU12, ZnFPU2]> {
  let Latency = 2;
}
def : InstRW<[ZnWriteEXTRQ], (instregex "EXTRQ")>;

// INSERTQ
def ZnWriteINSERTQ: SchedWriteRes<[ZnFPU03,ZnFPU1]> {
  let Latency = 4;
}
def : InstRW<[ZnWriteINSERTQ], (instregex "INSERTQ")>;

//-- SHA instructions --//
// SHA256MSG2
def : InstRW<[WriteMicrocoded], (instregex "SHA256MSG2(Y?)r(r|m)")>;

// SHA1MSG1, SHA256MSG1
// x,x.
def ZnWriteSHA1MSG1r : SchedWriteRes<[ZnFPU12]> {
  let Latency = 2;
  let ResourceCycles = [2];
}
def : InstRW<[ZnWriteSHA1MSG1r], (instregex "SHA(1|256)MSG1rr")>;
// x,m.
def ZnWriteSHA1MSG1Ld : SchedWriteRes<[ZnAGU, ZnFPU12]> {
  let Latency = 9;
  let ResourceCycles = [1,2];
}
def : InstRW<[ZnWriteSHA1MSG1Ld], (instregex "SHA(1|256)MSG1rm")>;

// SHA1MSG2
// x,x.
def ZnWriteSHA1MSG2r : SchedWriteRes<[ZnFPU12]> ;
def : InstRW<[ZnWriteSHA1MSG2r], (instrs SHA1MSG2rr)>;
// x,m.
def ZnWriteSHA1MSG2Ld : SchedWriteRes<[ZnAGU, ZnFPU12]> {
  let Latency = 8;
}
def : InstRW<[ZnWriteSHA1MSG2Ld], (instrs SHA1MSG2rm)>;

// SHA1NEXTE
// x,x.
def ZnWriteSHA1NEXTEr : SchedWriteRes<[ZnFPU1]> ;
def : InstRW<[ZnWriteSHA1NEXTEr], (instrs SHA1NEXTErr)>;
// x,m.
def ZnWriteSHA1NEXTELd : SchedWriteRes<[ZnAGU, ZnFPU1]> {
  let Latency = 8;
}
def : InstRW<[ZnWriteSHA1NEXTELd], (instrs SHA1NEXTErm)>;

// SHA1RNDS4
// x,x.
def ZnWriteSHA1RNDS4r : SchedWriteRes<[ZnFPU1]> {
  let Latency = 6;
}
def : InstRW<[ZnWriteSHA1RNDS4r], (instrs SHA1RNDS4rri)>;
// x,m.
def ZnWriteSHA1RNDS4Ld : SchedWriteRes<[ZnAGU, ZnFPU1]> {
  let Latency = 13;
}
def : InstRW<[ZnWriteSHA1RNDS4Ld], (instrs SHA1RNDS4rmi)>;

// SHA256RNDS2
// x,x.
def ZnWriteSHA256RNDS2r : SchedWriteRes<[ZnFPU1]> {
  let Latency = 4;
}
def : InstRW<[ZnWriteSHA256RNDS2r], (instrs SHA256RNDS2rr)>;
// x,m.
def ZnWriteSHA256RNDS2Ld : SchedWriteRes<[ZnAGU, ZnFPU1]> {
  let Latency = 11;
}
def : InstRW<[ZnWriteSHA256RNDS2Ld], (instrs SHA256RNDS2rm)>;

//-- Arithmetic instructions --//

// HADD, HSUB PS/PD
def : SchedAlias<WriteFHAdd,    ZnWriteMicrocoded>;
def : SchedAlias<WriteFHAddLd,  ZnWriteMicrocoded>;
def : SchedAlias<WriteFHAddY,   ZnWriteMicrocoded>;
def : SchedAlias<WriteFHAddYLd, ZnWriteMicrocoded>;

// VDIVPS.
// TODO - convert to ZnWriteResFpuPair
// y,y,y.
def ZnWriteVDIVPSYr : SchedWriteRes<[ZnFPU3]> {
  let Latency = 12;
  let ResourceCycles = [12];
}
def : SchedAlias<WriteFDivY,   ZnWriteVDIVPSYr>;

// y,y,m256.
def ZnWriteVDIVPSYLd : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 19;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 19];
}
def : SchedAlias<WriteFDivYLd,  ZnWriteVDIVPSYLd>;

// VDIVPD.
// TODO - convert to ZnWriteResFpuPair
// y,y,y.
def ZnWriteVDIVPDY : SchedWriteRes<[ZnFPU3]> {
  let Latency = 15;
  let ResourceCycles = [15];
}
def : SchedAlias<WriteFDiv64Y, ZnWriteVDIVPDY>;

// y,y,m256.
def ZnWriteVDIVPDYLd : SchedWriteRes<[ZnAGU, ZnFPU3]> {
  let Latency = 22;
  let NumMicroOps = 2;
  let ResourceCycles = [1,22];
}
def : SchedAlias<WriteFDiv64YLd, ZnWriteVDIVPDYLd>;

// DPPS.
// x,x,i / v,v,v,i.
def : SchedAlias<WriteDPPS,   ZnWriteMicrocoded>;
def : SchedAlias<WriteDPPSY,  ZnWriteMicrocoded>;

// x,m,i / v,v,m,i.
def : SchedAlias<WriteDPPSLd, ZnWriteMicrocoded>;
def : SchedAlias<WriteDPPSYLd,ZnWriteMicrocoded>;

// DPPD.
// x,x,i.
def : SchedAlias<WriteDPPD,   ZnWriteMicrocoded>;

// x,m,i.
def : SchedAlias<WriteDPPDLd, ZnWriteMicrocoded>;

// RSQRTSS
// TODO - convert to ZnWriteResFpuPair
// x,x.
def ZnWriteRSQRTSSr : SchedWriteRes<[ZnFPU02]> {
  let Latency = 5;
}
def : SchedAlias<WriteFRsqrt, ZnWriteRSQRTSSr>;

// x,m128.
def ZnWriteRSQRTSSLd: SchedWriteRes<[ZnAGU, ZnFPU02]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ResourceCycles = [1,2]; // FIXME: Is this right?
}
def : SchedAlias<WriteFRsqrtLd, ZnWriteRSQRTSSLd>;

// RSQRTPS
// TODO - convert to ZnWriteResFpuPair
// y,y.
def ZnWriteRSQRTPSYr : SchedWriteRes<[ZnFPU01]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def : SchedAlias<WriteFRsqrtY, ZnWriteRSQRTPSYr>;

// y,m256.
def ZnWriteRSQRTPSYLd : SchedWriteRes<[ZnAGU, ZnFPU01]> {
  let Latency = 12;
  let NumMicroOps = 2;
}
def : SchedAlias<WriteFRsqrtYLd, ZnWriteRSQRTPSYLd>;

//-- Other instructions --//

// VZEROUPPER.
def : InstRW<[WriteMicrocoded], (instrs VZEROUPPER)>;

// VZEROALL.
def : InstRW<[WriteMicrocoded], (instrs VZEROALL)>;

} // SchedModel
