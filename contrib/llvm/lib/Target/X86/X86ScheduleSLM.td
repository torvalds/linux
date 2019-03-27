//=- X86ScheduleSLM.td - X86 Silvermont Scheduling -----------*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Intel Silvermont to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def SLMModel : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and SLM can decode 2
  // instructions per cycle.
  let IssueWidth = 2;
  let MicroOpBufferSize = 32; // Based on the reorder buffer.
  let LoadLatency = 3;
  let MispredictPenalty = 10;
  let PostRAScheduler = 1;

  // For small loops, expand by a small factor to hide the backedge cost.
  let LoopMicroOpBufferSize = 10;

  // FIXME: SSE4 is unimplemented. This flag is set to allow
  // the scheduler to assign a default model to unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = SLMModel in {

// Silvermont has 5 reservation stations for micro-ops
def SLM_IEC_RSV0 : ProcResource<1>;
def SLM_IEC_RSV1 : ProcResource<1>;
def SLM_FPC_RSV0 : ProcResource<1> { let BufferSize = 1; }
def SLM_FPC_RSV1 : ProcResource<1> { let BufferSize = 1; }
def SLM_MEC_RSV  : ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def SLM_IEC_RSV01  : ProcResGroup<[SLM_IEC_RSV0, SLM_IEC_RSV1]>;
def SLM_FPC_RSV01  : ProcResGroup<[SLM_FPC_RSV0, SLM_FPC_RSV1]>;

def SLMDivider      : ProcResource<1>;
def SLMFPMultiplier : ProcResource<1>;
def SLMFPDivider    : ProcResource<1>;

// Loads are 3 cycles, so ReadAfterLd registers needn't be available until 3
// cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 3>;
def : ReadAdvance<ReadAfterVecLd, 3>;
def : ReadAdvance<ReadAfterVecXLd, 3>;
def : ReadAdvance<ReadAfterVecYLd, 3>;

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when queued in the reservation station.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass SLMWriteResPair<X86FoldableSchedWrite SchedRW,
                           list<ProcResourceKind> ExePorts,
                           int Lat, list<int> Res = [1], int UOps = 1,
                           int LoadLat = 3> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ResourceCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on MEC_RSV and adds LoadLat cycles to
  // the latency (default = 3).
  def : WriteRes<SchedRW.Folded, !listconcat([SLM_MEC_RSV], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ResourceCycles = !listconcat([1], Res);
    let NumMicroOps = UOps;
  }
}

// A folded store needs a cycle on MEC_RSV for the store data, but it does not
// need an extra port cycle to recompute the address.
def : WriteRes<WriteRMW, [SLM_MEC_RSV]>;

def : WriteRes<WriteStore,   [SLM_IEC_RSV01, SLM_MEC_RSV]>;
def : WriteRes<WriteStoreNT, [SLM_IEC_RSV01, SLM_MEC_RSV]>;
def : WriteRes<WriteLoad,    [SLM_MEC_RSV]> { let Latency = 3; }
def : WriteRes<WriteMove,    [SLM_IEC_RSV01]>;
def : WriteRes<WriteZero,    []>;

// Load/store MXCSR.
// FIXME: These are probably wrong. They are copy pasted from WriteStore/Load.
def : WriteRes<WriteSTMXCSR, [SLM_IEC_RSV01, SLM_MEC_RSV]>;
def : WriteRes<WriteLDMXCSR,  [SLM_MEC_RSV]> { let Latency = 3; }

// Treat misc copies as a move.
def : InstRW<[WriteMove], (instrs COPY)>;

defm : SLMWriteResPair<WriteALU,    [SLM_IEC_RSV01], 1>;
defm : SLMWriteResPair<WriteADC,    [SLM_IEC_RSV01], 1>;

defm : SLMWriteResPair<WriteIMul8,     [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul16,    [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul16Imm, [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul16Reg, [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul32,    [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul32Imm, [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul32Reg, [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul64,    [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul64Imm, [SLM_IEC_RSV1],  3>;
defm : SLMWriteResPair<WriteIMul64Reg, [SLM_IEC_RSV1],  3>;

defm : X86WriteRes<WriteBSWAP32, [SLM_IEC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64, [SLM_IEC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteCMPXCHG, [SLM_IEC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteCMPXCHGRMW, [SLM_IEC_RSV01, SLM_MEC_RSV], 4, [1, 2], 2>;
defm : X86WriteRes<WriteXCHG,      [SLM_IEC_RSV01], 1, [1], 1>;

defm : SLMWriteResPair<WriteShift,    [SLM_IEC_RSV0],  1>;
defm : SLMWriteResPair<WriteShiftCL,  [SLM_IEC_RSV0],  1>;
defm : SLMWriteResPair<WriteRotate,   [SLM_IEC_RSV0],  1>;
defm : SLMWriteResPair<WriteRotateCL, [SLM_IEC_RSV0],  1>;

defm : X86WriteRes<WriteSHDrri, [SLM_IEC_RSV0],  1, [1], 1>;
defm : X86WriteRes<WriteSHDrrcl,[SLM_IEC_RSV0],  1, [1], 1>;
defm : X86WriteRes<WriteSHDmri, [SLM_MEC_RSV, SLM_IEC_RSV0], 4, [2, 1], 2>;
defm : X86WriteRes<WriteSHDmrcl,[SLM_MEC_RSV, SLM_IEC_RSV0], 4, [2, 1], 2>;

defm : SLMWriteResPair<WriteJump,   [SLM_IEC_RSV1],  1>;
defm : SLMWriteResPair<WriteCRC32,  [SLM_IEC_RSV1],  3>;

defm : SLMWriteResPair<WriteCMOV,  [SLM_IEC_RSV01], 2, [2]>;
defm : SLMWriteResPair<WriteCMOV2, [SLM_IEC_RSV01], 2, [2]>;
defm : X86WriteRes<WriteFCMOV, [SLM_FPC_RSV1], 3, [1], 1>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [SLM_IEC_RSV01]>;
def  : WriteRes<WriteSETCCStore, [SLM_IEC_RSV01, SLM_MEC_RSV]> {
  // FIXME Latency and NumMicrOps?
  let ResourceCycles = [2,1];
}
defm : X86WriteRes<WriteLAHFSAHF,        [SLM_IEC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteBitTest,         [SLM_IEC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [SLM_IEC_RSV01, SLM_MEC_RSV], 4, [1,1], 1>;
defm : X86WriteRes<WriteBitTestRegLd,    [SLM_IEC_RSV01, SLM_MEC_RSV], 4, [1,1], 1>;
defm : X86WriteRes<WriteBitTestSet,      [SLM_IEC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestSetImmLd, [SLM_IEC_RSV01, SLM_MEC_RSV], 3, [1,1], 1>;
defm : X86WriteRes<WriteBitTestSetRegLd, [SLM_IEC_RSV01, SLM_MEC_RSV], 3, [1,1], 1>;

// This is for simple LEAs with one or two input operands.
// The complex ones can only execute on port 1, and they require two cycles on
// the port to read all inputs. We don't model that.
def : WriteRes<WriteLEA, [SLM_IEC_RSV1]>;

// Bit counts.
defm : SLMWriteResPair<WriteBSF, [SLM_IEC_RSV01], 10, [20], 10>;
defm : SLMWriteResPair<WriteBSR, [SLM_IEC_RSV01], 10, [20], 10>;
defm : SLMWriteResPair<WriteLZCNT,          [SLM_IEC_RSV0], 3>;
defm : SLMWriteResPair<WriteTZCNT,          [SLM_IEC_RSV0], 3>;
defm : SLMWriteResPair<WritePOPCNT,         [SLM_IEC_RSV0], 3>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : X86WriteResPairUnsupported<WriteBEXTR>;
defm : X86WriteResPairUnsupported<WriteBLS>;
defm : X86WriteResPairUnsupported<WriteBZHI>;

defm : SLMWriteResPair<WriteDiv8,   [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteDiv16,  [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteDiv32,  [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteDiv64,  [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteIDiv8,  [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteIDiv16, [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteIDiv32, [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;
defm : SLMWriteResPair<WriteIDiv64, [SLM_IEC_RSV01, SLMDivider], 25, [1,25], 1, 4>;

// Scalar and vector floating point.
defm : X86WriteRes<WriteFLD0,       [SLM_FPC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,       [SLM_FPC_RSV01], 1, [1], 1>;
defm : X86WriteRes<WriteFLDC,       [SLM_FPC_RSV01], 1, [2], 2>;
def  : WriteRes<WriteFLoad,         [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteFLoadX,        [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteFLoadY,        [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteFMaskedLoad,   [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteFMaskedLoadY,  [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteFStore,        [SLM_MEC_RSV]>;
def  : WriteRes<WriteFStoreX,       [SLM_MEC_RSV]>;
def  : WriteRes<WriteFStoreY,       [SLM_MEC_RSV]>;
def  : WriteRes<WriteFStoreNT,      [SLM_MEC_RSV]>;
def  : WriteRes<WriteFStoreNTX,     [SLM_MEC_RSV]>;
def  : WriteRes<WriteFStoreNTY,     [SLM_MEC_RSV]>;
def  : WriteRes<WriteFMaskedStore,  [SLM_MEC_RSV]>;
def  : WriteRes<WriteFMaskedStoreY, [SLM_MEC_RSV]>;
def  : WriteRes<WriteFMove,         [SLM_FPC_RSV01]>;
def  : WriteRes<WriteFMoveX,        [SLM_FPC_RSV01]>;
def  : WriteRes<WriteFMoveY,        [SLM_FPC_RSV01]>;
defm : X86WriteRes<WriteEMMS,       [SLM_FPC_RSV01], 10, [10], 9>;

defm : SLMWriteResPair<WriteFAdd,     [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFAddX,    [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFAddY,    [SLM_FPC_RSV1], 3>;
defm : X86WriteResPairUnsupported<WriteFAddZ>;
defm : SLMWriteResPair<WriteFAdd64,   [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFAdd64X,  [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFAdd64Y,  [SLM_FPC_RSV1], 3>;
defm : X86WriteResPairUnsupported<WriteFAdd64Z>;
defm : SLMWriteResPair<WriteFCmp,     [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFCmpX,    [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFCmpY,    [SLM_FPC_RSV1], 3>;
defm : X86WriteResPairUnsupported<WriteFCmpZ>;
defm : SLMWriteResPair<WriteFCmp64,   [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFCmp64X,  [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFCmp64Y,  [SLM_FPC_RSV1], 3>;
defm : X86WriteResPairUnsupported<WriteFCmp64Z>;
defm : SLMWriteResPair<WriteFCom,     [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFMul,     [SLM_FPC_RSV0, SLMFPMultiplier], 5, [1,2]>;
defm : SLMWriteResPair<WriteFMulX,    [SLM_FPC_RSV0, SLMFPMultiplier], 5, [1,2]>;
defm : SLMWriteResPair<WriteFMulY,    [SLM_FPC_RSV0, SLMFPMultiplier], 5, [1,2]>;
defm : X86WriteResPairUnsupported<WriteFMulZ>;
defm : SLMWriteResPair<WriteFMul64,   [SLM_FPC_RSV0, SLMFPMultiplier], 5, [1,2]>;
defm : SLMWriteResPair<WriteFMul64X,  [SLM_FPC_RSV0, SLMFPMultiplier], 5, [1,2]>;
defm : SLMWriteResPair<WriteFMul64Y,  [SLM_FPC_RSV0, SLMFPMultiplier], 5, [1,2]>;
defm : X86WriteResPairUnsupported<WriteFMul64Z>;
defm : SLMWriteResPair<WriteFDiv,     [SLM_FPC_RSV0, SLMFPDivider], 19, [1,17]>;
defm : SLMWriteResPair<WriteFDivX,    [SLM_FPC_RSV0, SLMFPDivider], 39, [1,39]>;
defm : SLMWriteResPair<WriteFDivY,    [SLM_FPC_RSV0, SLMFPDivider], 39, [1,39]>;
defm : X86WriteResPairUnsupported<WriteFDivZ>;
defm : SLMWriteResPair<WriteFDiv64,   [SLM_FPC_RSV0, SLMFPDivider], 34, [1,32]>;
defm : SLMWriteResPair<WriteFDiv64X,  [SLM_FPC_RSV0, SLMFPDivider], 69, [1,69]>;
defm : SLMWriteResPair<WriteFDiv64Y,  [SLM_FPC_RSV0, SLMFPDivider], 69, [1,69]>;
defm : X86WriteResPairUnsupported<WriteFDiv64Z>;
defm : SLMWriteResPair<WriteFRcp,     [SLM_FPC_RSV0], 5>;
defm : SLMWriteResPair<WriteFRcpX,    [SLM_FPC_RSV0], 5>;
defm : SLMWriteResPair<WriteFRcpY,    [SLM_FPC_RSV0], 5>;
defm : X86WriteResPairUnsupported<WriteFRcpZ>;
defm : SLMWriteResPair<WriteFRsqrt,   [SLM_FPC_RSV0], 5>;
defm : SLMWriteResPair<WriteFRsqrtX,  [SLM_FPC_RSV0], 5>;
defm : SLMWriteResPair<WriteFRsqrtY,  [SLM_FPC_RSV0], 5>;
defm : X86WriteResPairUnsupported<WriteFRsqrtZ>;
defm : SLMWriteResPair<WriteFSqrt,    [SLM_FPC_RSV0,SLMFPDivider], 20, [1,20], 1, 3>;
defm : SLMWriteResPair<WriteFSqrtX,   [SLM_FPC_RSV0,SLMFPDivider], 41, [1,40], 1, 3>;
defm : SLMWriteResPair<WriteFSqrtY,   [SLM_FPC_RSV0,SLMFPDivider], 41, [1,40], 1, 3>;
defm : X86WriteResPairUnsupported<WriteFSqrtZ>;
defm : SLMWriteResPair<WriteFSqrt64,  [SLM_FPC_RSV0,SLMFPDivider], 35, [1,35], 1, 3>;
defm : SLMWriteResPair<WriteFSqrt64X, [SLM_FPC_RSV0,SLMFPDivider], 71, [1,70], 1, 3>;
defm : SLMWriteResPair<WriteFSqrt64Y, [SLM_FPC_RSV0,SLMFPDivider], 71, [1,70], 1, 3>;
defm : X86WriteResPairUnsupported<WriteFSqrt64Z>;
defm : SLMWriteResPair<WriteFSqrt80,  [SLM_FPC_RSV0,SLMFPDivider], 40, [1,40]>;
defm : SLMWriteResPair<WriteDPPD,   [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteDPPS,   [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteDPPSY,  [SLM_FPC_RSV1], 3>;
defm : X86WriteResPairUnsupported<WriteDPPSZ>;
defm : SLMWriteResPair<WriteFSign,  [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WriteFRnd,   [SLM_FPC_RSV1], 3>;
defm : SLMWriteResPair<WriteFRndY,  [SLM_FPC_RSV1], 3>;
defm : X86WriteResPairUnsupported<WriteFRndZ>;
defm : SLMWriteResPair<WriteFLogic, [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WriteFLogicY, [SLM_FPC_RSV01], 1>;
defm : X86WriteResPairUnsupported<WriteFLogicZ>;
defm : SLMWriteResPair<WriteFTest,  [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WriteFTestY, [SLM_FPC_RSV01], 1>;
defm : X86WriteResPairUnsupported<WriteFTestZ>;
defm : SLMWriteResPair<WriteFShuffle,  [SLM_FPC_RSV0], 1>;
defm : SLMWriteResPair<WriteFShuffleY, [SLM_FPC_RSV0], 1>;
defm : X86WriteResPairUnsupported<WriteFShuffleZ>;
defm : SLMWriteResPair<WriteFVarShuffle, [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteFVarShuffleY,[SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteFVarShuffleZ>;
defm : SLMWriteResPair<WriteFBlend,  [SLM_FPC_RSV0],  1>;

// Conversion between integer and float.
defm : SLMWriteResPair<WriteCvtSS2I,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPS2I,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPS2IY,  [SLM_FPC_RSV01], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPS2IZ>;
defm : SLMWriteResPair<WriteCvtSD2I,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPD2I,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPD2IY,  [SLM_FPC_RSV01], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPD2IZ>;

defm : SLMWriteResPair<WriteCvtI2SS,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtI2PS,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtI2PSY,  [SLM_FPC_RSV01], 4>;
defm : X86WriteResPairUnsupported<WriteCvtI2PSZ>;
defm : SLMWriteResPair<WriteCvtI2SD,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtI2PD,   [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtI2PDY,  [SLM_FPC_RSV01], 4>;
defm : X86WriteResPairUnsupported<WriteCvtI2PDZ>;

defm : SLMWriteResPair<WriteCvtSS2SD,  [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPS2PD,  [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPS2PDY, [SLM_FPC_RSV01], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPS2PDZ>;
defm : SLMWriteResPair<WriteCvtSD2SS,  [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPD2PS,  [SLM_FPC_RSV01], 4>;
defm : SLMWriteResPair<WriteCvtPD2PSY, [SLM_FPC_RSV01], 4>;
defm : X86WriteResPairUnsupported<WriteCvtPD2PSZ>;

// Vector integer operations.
def  : WriteRes<WriteVecLoad,         [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecLoadX,        [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecLoadY,        [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecLoadNT,       [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecLoadNTY,      [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecMaskedLoad,   [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecMaskedLoadY,  [SLM_MEC_RSV]> { let Latency = 3; }
def  : WriteRes<WriteVecStore,        [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecStoreX,       [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecStoreY,       [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecStoreNT,      [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecStoreNTY,     [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecMaskedStore,  [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecMaskedStoreY, [SLM_MEC_RSV]>;
def  : WriteRes<WriteVecMove,         [SLM_FPC_RSV01]>;
def  : WriteRes<WriteVecMoveX,        [SLM_FPC_RSV01]>;
def  : WriteRes<WriteVecMoveY,        [SLM_FPC_RSV01]>;
def  : WriteRes<WriteVecMoveToGpr,    [SLM_IEC_RSV01]>;
def  : WriteRes<WriteVecMoveFromGpr,  [SLM_IEC_RSV01]>;

defm : SLMWriteResPair<WriteVecShift,    [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVecShiftX,   [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVecShiftY,   [SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteVecShiftZ>;
defm : SLMWriteResPair<WriteVecShiftImm, [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVecShiftImmX,[SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVecShiftImmY,[SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteVecShiftImmZ>;
defm : SLMWriteResPair<WriteVecLogic, [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WriteVecLogicX,[SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WriteVecLogicY,[SLM_FPC_RSV01], 1>;
defm : X86WriteResPairUnsupported<WriteVecLogicZ>;
defm : SLMWriteResPair<WriteVecTest,  [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WriteVecTestY, [SLM_FPC_RSV01], 1>;
defm : X86WriteResPairUnsupported<WriteVecTestZ>;
defm : SLMWriteResPair<WriteVecALU,   [SLM_FPC_RSV01],  1>;
defm : SLMWriteResPair<WriteVecALUX,  [SLM_FPC_RSV01],  1>;
defm : SLMWriteResPair<WriteVecALUY,  [SLM_FPC_RSV01],  1>;
defm : X86WriteResPairUnsupported<WriteVecALUZ>;
defm : SLMWriteResPair<WriteVecIMul,  [SLM_FPC_RSV0],   4>;
defm : SLMWriteResPair<WriteVecIMulX, [SLM_FPC_RSV0],   4>;
defm : SLMWriteResPair<WriteVecIMulY, [SLM_FPC_RSV0],   4>;
defm : X86WriteResPairUnsupported<WriteVecIMulZ>;
// FIXME: The below is closer to correct, but caused some perf regressions.
//defm : SLMWriteResPair<WritePMULLD,  [SLM_FPC_RSV0],   11, [11], 7>;
defm : SLMWriteResPair<WritePMULLD,  [SLM_FPC_RSV0],   4>;
defm : SLMWriteResPair<WritePMULLDY, [SLM_FPC_RSV0],   4>;
defm : X86WriteResPairUnsupported<WritePMULLDZ>;
defm : SLMWriteResPair<WriteShuffle,  [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteShuffleY, [SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteShuffleZ>;
defm : SLMWriteResPair<WriteShuffleX, [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVarShuffle,  [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVarShuffleX, [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteVarShuffleY, [SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteVarShuffleZ>;
defm : SLMWriteResPair<WriteBlend,  [SLM_FPC_RSV0],  1>;
defm : SLMWriteResPair<WriteBlendY, [SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteBlendZ>;
defm : SLMWriteResPair<WriteMPSAD,  [SLM_FPC_RSV0],  7>;
defm : SLMWriteResPair<WriteMPSADY, [SLM_FPC_RSV0],  7>;
defm : X86WriteResPairUnsupported<WriteMPSADZ>;
defm : SLMWriteResPair<WritePSADBW,  [SLM_FPC_RSV0],  4>;
defm : SLMWriteResPair<WritePSADBWX, [SLM_FPC_RSV0],  4>;
defm : SLMWriteResPair<WritePSADBWY, [SLM_FPC_RSV0],  4>;
defm : X86WriteResPairUnsupported<WritePSADBWZ>;
defm : SLMWriteResPair<WritePHMINPOS,  [SLM_FPC_RSV0],   4>;

// Vector insert/extract operations.
defm : SLMWriteResPair<WriteVecInsert, [SLM_FPC_RSV0],  1>;

def  : WriteRes<WriteVecExtract, [SLM_FPC_RSV0]>;
def  : WriteRes<WriteVecExtractSt, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [1, 2];
}

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : SLMWriteResPair<WriteFHAdd,   [SLM_FPC_RSV01], 3, [2]>;
defm : SLMWriteResPair<WriteFHAddY,  [SLM_FPC_RSV01], 3, [2]>;
defm : X86WriteResPairUnsupported<WriteFHAddZ>;
defm : SLMWriteResPair<WritePHAdd,   [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WritePHAddX,  [SLM_FPC_RSV01], 1>;
defm : SLMWriteResPair<WritePHAddY,  [SLM_FPC_RSV01], 1>;
defm : X86WriteResPairUnsupported<WritePHAddZ>;

// String instructions.
// Packed Compare Implicit Length Strings, Return Mask
def : WriteRes<WritePCmpIStrM, [SLM_FPC_RSV0]> {
  let Latency = 13;
  let ResourceCycles = [13];
}
def : WriteRes<WritePCmpIStrMLd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 13;
  let ResourceCycles = [13, 1];
}

// Packed Compare Explicit Length Strings, Return Mask
def : WriteRes<WritePCmpEStrM, [SLM_FPC_RSV0]> {
  let Latency = 17;
  let ResourceCycles = [17];
}
def : WriteRes<WritePCmpEStrMLd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 17;
  let ResourceCycles = [17, 1];
}

// Packed Compare Implicit Length Strings, Return Index
def : WriteRes<WritePCmpIStrI, [SLM_FPC_RSV0]> {
  let Latency = 17;
  let ResourceCycles = [17];
}
def : WriteRes<WritePCmpIStrILd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 17;
  let ResourceCycles = [17, 1];
}

// Packed Compare Explicit Length Strings, Return Index
def : WriteRes<WritePCmpEStrI, [SLM_FPC_RSV0]> {
  let Latency = 21;
  let ResourceCycles = [21];
}
def : WriteRes<WritePCmpEStrILd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 21;
  let ResourceCycles = [21, 1];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK,    [SLM_FPC_RSV1]> { let Latency = 4; }
def : WriteRes<WriteVecMOVMSK,  [SLM_FPC_RSV1]> { let Latency = 4; }
def : WriteRes<WriteVecMOVMSKY, [SLM_FPC_RSV1]> { let Latency = 4; }
def : WriteRes<WriteMMXMOVMSK,  [SLM_FPC_RSV1]> { let Latency = 4; }

// AES Instructions.
def : WriteRes<WriteAESDecEnc, [SLM_FPC_RSV0]> {
  let Latency = 8;
  let ResourceCycles = [5];
}
def : WriteRes<WriteAESDecEncLd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 8;
  let ResourceCycles = [5, 1];
}

def : WriteRes<WriteAESIMC, [SLM_FPC_RSV0]> {
  let Latency = 8;
  let ResourceCycles = [5];
}
def : WriteRes<WriteAESIMCLd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 8;
  let ResourceCycles = [5, 1];
}

def : WriteRes<WriteAESKeyGen, [SLM_FPC_RSV0]> {
  let Latency = 8;
  let ResourceCycles = [5];
}
def : WriteRes<WriteAESKeyGenLd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 8;
  let ResourceCycles = [5, 1];
}

// Carry-less multiplication instructions.
def : WriteRes<WriteCLMul, [SLM_FPC_RSV0]> {
  let Latency = 10;
  let ResourceCycles = [10];
}
def : WriteRes<WriteCLMulLd, [SLM_FPC_RSV0, SLM_MEC_RSV]> {
  let Latency = 10;
  let ResourceCycles = [10, 1];
}

def : WriteRes<WriteSystem,     [SLM_FPC_RSV0]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [SLM_FPC_RSV0]> { let Latency = 100; }
def : WriteRes<WriteFence, [SLM_MEC_RSV]>;
def : WriteRes<WriteNop, []>;

// AVX/FMA is not supported on that architecture, but we should define the basic
// scheduling resources anyway.
def  : WriteRes<WriteIMulH, [SLM_FPC_RSV0]>;
defm : X86WriteResPairUnsupported<WriteFBlendY>;
defm : X86WriteResPairUnsupported<WriteFBlendZ>;
defm : SLMWriteResPair<WriteVarBlend, [SLM_FPC_RSV0], 1>;
defm : X86WriteResPairUnsupported<WriteVarBlendY>;
defm : X86WriteResPairUnsupported<WriteVarBlendZ>;
defm : SLMWriteResPair<WriteFVarBlend, [SLM_FPC_RSV0], 1>;
defm : X86WriteResPairUnsupported<WriteFVarBlendY>;
defm : X86WriteResPairUnsupported<WriteFVarBlendZ>;
defm : X86WriteResPairUnsupported<WriteFShuffle256>;
defm : X86WriteResPairUnsupported<WriteFVarShuffle256>;
defm : X86WriteResPairUnsupported<WriteShuffle256>;
defm : X86WriteResPairUnsupported<WriteVarShuffle256>;
defm : SLMWriteResPair<WriteVarVecShift,  [SLM_FPC_RSV0],  1>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftY>;
defm : X86WriteResPairUnsupported<WriteVarVecShiftZ>;
defm : X86WriteResPairUnsupported<WriteFMA>;
defm : X86WriteResPairUnsupported<WriteFMAX>;
defm : X86WriteResPairUnsupported<WriteFMAY>;
defm : X86WriteResPairUnsupported<WriteFMAZ>;

defm : X86WriteResPairUnsupported<WriteCvtPH2PS>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSY>;
defm : X86WriteResPairUnsupported<WriteCvtPH2PSZ>;
defm : X86WriteResUnsupported<WriteCvtPS2PH>;
defm : X86WriteResUnsupported<WriteCvtPS2PHY>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZ>;
defm : X86WriteResUnsupported<WriteCvtPS2PHSt>;
defm : X86WriteResUnsupported<WriteCvtPS2PHYSt>;
defm : X86WriteResUnsupported<WriteCvtPS2PHZSt>;

} // SchedModel
