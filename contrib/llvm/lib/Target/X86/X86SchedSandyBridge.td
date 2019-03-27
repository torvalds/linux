//=- X86SchedSandyBridge.td - X86 Sandy Bridge Scheduling ----*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Sandy Bridge to support instruction
// scheduling and other instruction cost heuristics.
//
// Note that we define some instructions here that are not supported by SNB,
// but we still have to define them because SNB is the default subtarget for
// X86. These instructions are tagged with a comment `Unsupported = 1`.
//
//===----------------------------------------------------------------------===//

def SandyBridgeModel : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and SB can decode 4
  // instructions per cycle.
  // FIXME: Identify instructions that aren't a single fused micro-op.
  let IssueWidth = 4;
  let MicroOpBufferSize = 168; // Based on the reorder buffer.
  let LoadLatency = 5;
  let MispredictPenalty = 16;

  // Based on the LSD (loop-stream detector) queue size.
  let LoopMicroOpBufferSize = 28;

  // This flag is set to allow the scheduler to assign
  // a default model to unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = SandyBridgeModel in {

// Sandy Bridge can issue micro-ops to 6 different ports in one cycle.

// Ports 0, 1, and 5 handle all computation.
def SBPort0 : ProcResource<1>;
def SBPort1 : ProcResource<1>;
def SBPort5 : ProcResource<1>;

// Ports 2 and 3 are identical. They handle loads and the address half of
// stores.
def SBPort23 : ProcResource<2>;

// Port 4 gets the data half of stores. Store data can be available later than
// the store address, but since we don't model the latency of stores, we can
// ignore that.
def SBPort4 : ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def SBPort01  : ProcResGroup<[SBPort0, SBPort1]>;
def SBPort05  : ProcResGroup<[SBPort0, SBPort5]>;
def SBPort15  : ProcResGroup<[SBPort1, SBPort5]>;
def SBPort015 : ProcResGroup<[SBPort0, SBPort1, SBPort5]>;

// 54 Entry Unified Scheduler
def SBPortAny : ProcResGroup<[SBPort0, SBPort1, SBPort23, SBPort4, SBPort5]> {
  let BufferSize=54;
}

// Integer division issued on port 0.
def SBDivider : ProcResource<1>;
// FP division and sqrt on port 0.
def SBFPDivider : ProcResource<1>;

// Integer loads are 5 cycles, so ReadAfterLd registers needn't be available until 5
// cycles after the memory operand.
def : ReadAdvance<ReadAfterLd, 5>;

// Vector loads are 5/6/7 cycles, so ReadAfterVec*Ld registers needn't be available
// until 5/6/7 cycles after the memory operand.
def : ReadAdvance<ReadAfterVecLd, 5>;
def : ReadAdvance<ReadAfterVecXLd, 6>;
def : ReadAdvance<ReadAfterVecYLd, 7>;

// Many SchedWrites are defined in pairs with and without a folded load.
// Instructions with folded loads are usually micro-fused, so they only appear
// as two micro-ops when queued in the reservation station.
// This multiclass defines the resource usage for variants with and without
// folded loads.
multiclass SBWriteResPair<X86FoldableSchedWrite SchedRW,
                          list<ProcResourceKind> ExePorts,
                          int Lat, list<int> Res = [1], int UOps = 1,
                          int LoadLat = 5> {
  // Register variant is using a single cycle on ExePort.
  def : WriteRes<SchedRW, ExePorts> {
    let Latency = Lat;
    let ResourceCycles = Res;
    let NumMicroOps = UOps;
  }

  // Memory variant also uses a cycle on port 2/3 and adds LoadLat cycles to
  // the latency (default = 5).
  def : WriteRes<SchedRW.Folded, !listconcat([SBPort23], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ResourceCycles = !listconcat([1], Res);
    let NumMicroOps = !add(UOps, 1);
  }
}

// A folded store needs a cycle on port 4 for the store data, and an extra port
// 2/3 cycle to recompute the address.
def : WriteRes<WriteRMW, [SBPort23,SBPort4]>;

def : WriteRes<WriteStore,   [SBPort23, SBPort4]>;
def : WriteRes<WriteStoreNT, [SBPort23, SBPort4]>;
def : WriteRes<WriteLoad,    [SBPort23]> { let Latency = 5; }
def : WriteRes<WriteMove,    [SBPort015]>;
def : WriteRes<WriteZero,    []>;

// Arithmetic.
defm : SBWriteResPair<WriteALU,    [SBPort015], 1>;
defm : SBWriteResPair<WriteADC,    [SBPort05,SBPort015], 2, [1,1], 2>;

defm : SBWriteResPair<WriteIMul8,     [SBPort1],   3>;
defm : SBWriteResPair<WriteIMul16,    [SBPort1,SBPort05,SBPort015], 4, [1,1,2], 4>;
defm : X86WriteRes<WriteIMul16Imm,    [SBPort1,SBPort015], 4, [1,1], 2>;
defm : X86WriteRes<WriteIMul16ImmLd,  [SBPort1,SBPort015,SBPort23], 8, [1,1,1], 3>;
defm : SBWriteResPair<WriteIMul16Reg, [SBPort1],   3>;
defm : SBWriteResPair<WriteIMul32,    [SBPort1,SBPort05,SBPort015], 4, [1,1,1], 3>;
defm : SBWriteResPair<WriteIMul32Imm, [SBPort1],   3>;
defm : SBWriteResPair<WriteIMul32Reg, [SBPort1],   3>;
defm : SBWriteResPair<WriteIMul64,    [SBPort1,SBPort0], 4, [1,1], 2>;
defm : SBWriteResPair<WriteIMul64Imm, [SBPort1],   3>;
defm : SBWriteResPair<WriteIMul64Reg, [SBPort1],   3>;
def  : WriteRes<WriteIMulH, []> { let Latency = 3; }

defm : X86WriteRes<WriteXCHG,      [SBPort015], 2, [3], 3>;
defm : X86WriteRes<WriteBSWAP32,   [SBPort1], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64,   [SBPort1, SBPort05], 2, [1,1], 2>;
defm : X86WriteRes<WriteCMPXCHG,   [SBPort05, SBPort015], 5, [1,3], 4>;
defm : X86WriteRes<WriteCMPXCHGRMW,[SBPort015, SBPort5, SBPort23, SBPort4], 8, [1, 2, 2, 1], 6>;

defm : SBWriteResPair<WriteDiv8,   [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteDiv16,  [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteDiv32,  [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteDiv64,  [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteIDiv8,  [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteIDiv16, [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteIDiv32, [SBPort0, SBDivider], 25, [1, 10]>;
defm : SBWriteResPair<WriteIDiv64, [SBPort0, SBDivider], 25, [1, 10]>;

// SHLD/SHRD.
defm : X86WriteRes<WriteSHDrri, [SBPort05, SBPort015], 2, [1, 1], 2>;
defm : X86WriteRes<WriteSHDrrcl,[SBPort05, SBPort015], 4, [3, 1], 4>;
defm : X86WriteRes<WriteSHDmri, [SBPort4,SBPort23,SBPort05,SBPort015], 8, [1, 2, 1, 1], 5>;
defm : X86WriteRes<WriteSHDmrcl,[SBPort4,SBPort23,SBPort05,SBPort015], 10, [1, 2, 3, 1], 7>;

defm : SBWriteResPair<WriteShift,    [SBPort05],  1>;
defm : SBWriteResPair<WriteShiftCL,  [SBPort05],  3, [3], 3>;
defm : SBWriteResPair<WriteRotate,   [SBPort05],  2, [2], 2>;
defm : SBWriteResPair<WriteRotateCL, [SBPort05],  3, [3], 3>;

defm : SBWriteResPair<WriteJump,  [SBPort5],   1>;
defm : SBWriteResPair<WriteCRC32, [SBPort1],   3, [1], 1, 5>;

defm : SBWriteResPair<WriteCMOV,  [SBPort05,SBPort015], 2, [1,1], 2>; // Conditional move.
defm : SBWriteResPair<WriteCMOV2, [SBPort05,SBPort015], 3, [2,1], 3>; // Conditional (CF + ZF flag) move.
defm : X86WriteRes<WriteFCMOV, [SBPort5,SBPort05], 3, [2,1], 3>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [SBPort05]>; // Setcc.
def  : WriteRes<WriteSETCCStore, [SBPort05,SBPort4,SBPort23]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

defm : X86WriteRes<WriteLAHFSAHF,        [SBPort05], 1, [1], 1>;
defm : X86WriteRes<WriteBitTest,         [SBPort05], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [SBPort05,SBPort23], 6, [1,1], 2>;
//defm : X86WriteRes<WriteBitTestRegLd,    [SBPort05,SBPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestSet,      [SBPort05], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestSetImmLd, [SBPort05,SBPort23], 6, [1,1], 3>;
defm : X86WriteRes<WriteBitTestSetRegLd, [SBPort05,SBPort23,SBPort5,SBPort015], 8, [1,1,1,1], 5>;

// This is for simple LEAs with one or two input operands.
// The complex ones can only execute on port 1, and they require two cycles on
// the port to read all inputs. We don't model that.
def : WriteRes<WriteLEA, [SBPort01]>;

// Bit counts.
defm : SBWriteResPair<WriteBSF, [SBPort1], 3, [1], 1, 5>;
defm : SBWriteResPair<WriteBSR, [SBPort1], 3, [1], 1, 5>;
defm : SBWriteResPair<WriteLZCNT,          [SBPort1], 3, [1], 1, 5>;
defm : SBWriteResPair<WriteTZCNT,          [SBPort1], 3, [1], 1, 5>;
defm : SBWriteResPair<WritePOPCNT,         [SBPort1], 3, [1], 1, 6>;

// BMI1 BEXTR/BLS, BMI2 BZHI
// NOTE: These don't exist on Sandy Bridge. Ports are guesses.
defm : SBWriteResPair<WriteBEXTR, [SBPort05,SBPort1], 2, [1,1], 2>;
defm : SBWriteResPair<WriteBLS,   [SBPort015], 1>;
defm : SBWriteResPair<WriteBZHI,  [SBPort1], 1>;

// Scalar and vector floating point.
defm : X86WriteRes<WriteFLD0,          [SBPort5], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,          [SBPort0,SBPort5], 1, [1,1], 2>;
defm : X86WriteRes<WriteFLDC,          [SBPort0,SBPort1], 1, [1,1], 2>;
defm : X86WriteRes<WriteFLoad,         [SBPort23], 5, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [SBPort23], 6, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [SBPort23], 7, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [SBPort23,SBPort05], 8, [1,2], 3>;
defm : X86WriteRes<WriteFMaskedLoadY,  [SBPort23,SBPort05], 9, [1,2], 3>;
defm : X86WriteRes<WriteFStore,        [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteFStoreX,       [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteFStoreY,       [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteFStoreNT,      [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteFStoreNTX,     [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteFStoreNTY,     [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteFMaskedStore,  [SBPort4,SBPort01,SBPort23], 5, [1,1,1], 3>;
defm : X86WriteRes<WriteFMaskedStoreY, [SBPort4,SBPort01,SBPort23], 5, [1,1,1], 3>;
defm : X86WriteRes<WriteFMove,         [SBPort5], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [SBPort5], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [SBPort5], 1, [1], 1>;
defm : X86WriteRes<WriteEMMS,          [SBPort015], 31, [31], 31>;

defm : SBWriteResPair<WriteFAdd,    [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFAddX,   [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFAddY,   [SBPort1],  3, [1], 1, 7>;
defm : SBWriteResPair<WriteFAddZ,   [SBPort1],  3, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFAdd64,  [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFAdd64X, [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFAdd64Y, [SBPort1],  3, [1], 1, 7>;
defm : SBWriteResPair<WriteFAdd64Z, [SBPort1],  3, [1], 1, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteFCmp,    [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFCmpX,   [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFCmpY,   [SBPort1],  3, [1], 1, 7>;
defm : SBWriteResPair<WriteFCmpZ,   [SBPort1],  3, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFCmp64,  [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFCmp64X, [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteFCmp64Y, [SBPort1],  3, [1], 1, 7>;
defm : SBWriteResPair<WriteFCmp64Z, [SBPort1],  3, [1], 1, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteFCom,    [SBPort1],  3>;

defm : SBWriteResPair<WriteFMul,    [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFMulX,   [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFMulY,   [SBPort0],  5, [1], 1, 7>;
defm : SBWriteResPair<WriteFMulZ,   [SBPort0],  5, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFMul64,  [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFMul64X, [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFMul64Y, [SBPort0],  5, [1], 1, 7>;
defm : SBWriteResPair<WriteFMul64Z, [SBPort0],  5, [1], 1, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteFDiv,    [SBPort0,SBFPDivider], 14, [1,14], 1, 6>;
defm : SBWriteResPair<WriteFDivX,   [SBPort0,SBFPDivider], 14, [1,14], 1, 6>;
defm : SBWriteResPair<WriteFDivY,   [SBPort0,SBPort05,SBFPDivider], 29, [2,1,28], 3, 7>;
defm : SBWriteResPair<WriteFDivZ,   [SBPort0,SBPort05,SBFPDivider], 29, [2,1,28], 3, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFDiv64,  [SBPort0,SBFPDivider], 22, [1,22], 1, 6>;
defm : SBWriteResPair<WriteFDiv64X, [SBPort0,SBFPDivider], 22, [1,22], 1, 6>;
defm : SBWriteResPair<WriteFDiv64Y, [SBPort0,SBPort05,SBFPDivider], 45, [2,1,44], 3, 7>;
defm : SBWriteResPair<WriteFDiv64Z, [SBPort0,SBPort05,SBFPDivider], 45, [2,1,44], 3, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteFRcp,   [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFRcpX,  [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFRcpY,  [SBPort0,SBPort05],  7, [2,1], 3, 7>;
defm : SBWriteResPair<WriteFRcpZ,  [SBPort0,SBPort05],  7, [2,1], 3, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteFRsqrt, [SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFRsqrtX,[SBPort0],  5, [1], 1, 6>;
defm : SBWriteResPair<WriteFRsqrtY,[SBPort0,SBPort05],  7, [2,1], 3, 7>;
defm : SBWriteResPair<WriteFRsqrtZ,[SBPort0,SBPort05],  7, [2,1], 3, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteFSqrt,    [SBPort0,SBFPDivider], 14, [1,14], 1, 6>;
defm : SBWriteResPair<WriteFSqrtX,   [SBPort0,SBFPDivider], 14, [1,14], 1, 6>;
defm : SBWriteResPair<WriteFSqrtY,   [SBPort0,SBPort05,SBFPDivider], 29, [2,1,28], 3, 7>;
defm : SBWriteResPair<WriteFSqrtZ,   [SBPort0,SBPort05,SBFPDivider], 29, [2,1,28], 3, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFSqrt64,  [SBPort0,SBFPDivider], 21, [1,21], 1, 6>;
defm : SBWriteResPair<WriteFSqrt64X, [SBPort0,SBFPDivider], 21, [1,21], 1, 6>;
defm : SBWriteResPair<WriteFSqrt64Y, [SBPort0,SBPort05,SBFPDivider], 45, [2,1,44], 3, 7>;
defm : SBWriteResPair<WriteFSqrt64Z, [SBPort0,SBPort05,SBFPDivider], 45, [2,1,44], 3, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFSqrt80,  [SBPort0,SBFPDivider], 24, [1,24], 1, 6>;

defm : SBWriteResPair<WriteDPPD,   [SBPort0,SBPort1,SBPort5],  9, [1,1,1], 3, 6>;
defm : SBWriteResPair<WriteDPPS,   [SBPort0,SBPort1,SBPort5], 12, [1,2,1], 4, 6>;
defm : SBWriteResPair<WriteDPPSY,  [SBPort0,SBPort1,SBPort5], 12, [1,2,1], 4, 7>;
defm : SBWriteResPair<WriteDPPSZ,  [SBPort0,SBPort1,SBPort5], 12, [1,2,1], 4, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFSign,    [SBPort5], 1>;
defm : SBWriteResPair<WriteFRnd,     [SBPort1], 3, [1], 1, 6>;
defm : SBWriteResPair<WriteFRndY,    [SBPort1], 3, [1], 1, 7>;
defm : SBWriteResPair<WriteFRndZ,    [SBPort1], 3, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFLogic,   [SBPort5], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteFLogicY,  [SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFLogicZ,  [SBPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFTest,    [SBPort0], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteFTestY,   [SBPort0], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFTestZ,   [SBPort0], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFShuffle, [SBPort5], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteFShuffleY,[SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFShuffleZ,[SBPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFVarShuffle, [SBPort5], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteFVarShuffleY,[SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFVarShuffleZ,[SBPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFBlend,    [SBPort05], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteFBlendY,   [SBPort05], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFBlendZ,   [SBPort05], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteFVarBlend, [SBPort05], 2, [2], 2, 6>;
defm : SBWriteResPair<WriteFVarBlendY,[SBPort05], 2, [2], 2, 7>;
defm : SBWriteResPair<WriteFVarBlendZ,[SBPort05], 2, [2], 2, 7>; // Unsupported = 1

// Conversion between integer and float.
defm : SBWriteResPair<WriteCvtSS2I,   [SBPort0,SBPort1], 5, [1,1], 2>;
defm : SBWriteResPair<WriteCvtPS2I,           [SBPort1], 3, [1], 1, 6>;
defm : SBWriteResPair<WriteCvtPS2IY,          [SBPort1], 3, [1], 1, 7>;
defm : SBWriteResPair<WriteCvtPS2IZ,          [SBPort1], 3, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteCvtSD2I,   [SBPort0,SBPort1], 5, [1,1], 2>;
defm : SBWriteResPair<WriteCvtPD2I,   [SBPort1,SBPort5], 4, [1,1], 2, 6>;
defm : X86WriteRes<WriteCvtPD2IY,     [SBPort1,SBPort5], 4, [1,1], 2>;
defm : X86WriteRes<WriteCvtPD2IZ,     [SBPort1,SBPort5], 4, [1,1], 2>; // Unsupported = 1
defm : X86WriteRes<WriteCvtPD2IYLd,   [SBPort1,SBPort5,SBPort23], 11, [1,1,1], 3>;
defm : X86WriteRes<WriteCvtPD2IZLd,   [SBPort1,SBPort5,SBPort23], 11, [1,1,1], 3>; // Unsupported = 1

defm : X86WriteRes<WriteCvtI2SS,      [SBPort1,SBPort5],  5, [1,2], 3>;
defm : X86WriteRes<WriteCvtI2SSLd,    [SBPort1,SBPort5,SBPort23], 10, [1,1,1], 3>;
defm : SBWriteResPair<WriteCvtI2PS,           [SBPort1],  3, [1], 1, 6>;
defm : SBWriteResPair<WriteCvtI2PSY,          [SBPort1],  3, [1], 1, 7>;
defm : SBWriteResPair<WriteCvtI2PSZ,          [SBPort1],  3, [1], 1, 7>; // Unsupported = 1
defm : X86WriteRes<WriteCvtI2SD,      [SBPort1,SBPort5],  4, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2PD,      [SBPort1,SBPort5],  4, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2PDY,     [SBPort1,SBPort5],  4, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2PDZ,     [SBPort1,SBPort5],  4, [1,1], 2>; // Unsupported = 1
defm : X86WriteRes<WriteCvtI2SDLd,   [SBPort1,SBPort23],  9, [1,1], 2>;
defm : X86WriteRes<WriteCvtI2PDLd,   [SBPort1,SBPort5,SBPort23], 10, [1,1,1], 3>;
defm : X86WriteRes<WriteCvtI2PDYLd,  [SBPort1,SBPort5,SBPort23], 10, [1,1,1], 3>;
defm : X86WriteRes<WriteCvtI2PDZLd,  [SBPort1,SBPort5,SBPort23], 10, [1,1,1], 3>; // Unsupported = 1

defm : SBWriteResPair<WriteCvtSS2SD,  [SBPort0], 1, [1], 1, 6>;
defm : X86WriteRes<WriteCvtPS2PD,     [SBPort0,SBPort5], 2, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDY,    [SBPort0,SBPort5], 2, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDZ,    [SBPort0,SBPort5], 2, [1,1], 2>; // Unsupported = 1
defm : X86WriteRes<WriteCvtPS2PDLd,  [SBPort0,SBPort23], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDYLd, [SBPort0,SBPort23], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PDZLd, [SBPort0,SBPort23], 7, [1,1], 2>; // Unsupported = 1
defm : SBWriteResPair<WriteCvtSD2SS,  [SBPort1,SBPort5], 4, [1,1], 2, 6>;
defm : SBWriteResPair<WriteCvtPD2PS,  [SBPort1,SBPort5], 4, [1,1], 2, 6>;
defm : SBWriteResPair<WriteCvtPD2PSY, [SBPort1,SBPort5], 4, [1,1], 2, 7>;
defm : SBWriteResPair<WriteCvtPD2PSZ, [SBPort1,SBPort5], 4, [1,1], 2, 7>; // Unsupported = 1

defm : SBWriteResPair<WriteCvtPH2PS,  [SBPort1], 3>;
defm : SBWriteResPair<WriteCvtPH2PSY, [SBPort1], 3>;
defm : SBWriteResPair<WriteCvtPH2PSZ, [SBPort1], 3>; // Unsupported = 1

defm : X86WriteRes<WriteCvtPS2PH,    [SBPort1], 3, [1], 1>;
defm : X86WriteRes<WriteCvtPS2PHY,   [SBPort1], 3, [1], 1>;
defm : X86WriteRes<WriteCvtPS2PHZ,   [SBPort1], 3, [1], 1>; // Unsupported = 1
defm : X86WriteRes<WriteCvtPS2PHSt,  [SBPort1, SBPort23, SBPort4], 4, [1,1,1], 1>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [SBPort1, SBPort23, SBPort4], 4, [1,1,1], 1>;
defm : X86WriteRes<WriteCvtPS2PHZSt, [SBPort1, SBPort23, SBPort4], 4, [1,1,1], 1>; // Unsupported = 1

// Vector integer operations.
defm : X86WriteRes<WriteVecLoad,         [SBPort23], 5, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [SBPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [SBPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [SBPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [SBPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [SBPort23,SBPort05], 8, [1,2], 3>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [SBPort23,SBPort05], 9, [1,2], 3>;
defm : X86WriteRes<WriteVecStore,        [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteVecStoreX,       [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteVecStoreY,       [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteVecStoreNT,      [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteVecStoreNTY,     [SBPort23,SBPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteVecMaskedStore,  [SBPort4,SBPort01,SBPort23], 5, [1,1,1], 3>;
defm : X86WriteRes<WriteVecMaskedStoreY, [SBPort4,SBPort01,SBPort23], 5, [1,1,1], 3>;
defm : X86WriteRes<WriteVecMove,         [SBPort05], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [SBPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [SBPort05], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveToGpr,    [SBPort0], 2, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [SBPort5], 1, [1], 1>;

defm : SBWriteResPair<WriteVecLogic, [SBPort015], 1, [1], 1, 5>;
defm : SBWriteResPair<WriteVecLogicX,[SBPort015], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteVecLogicY,[SBPort015], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteVecLogicZ,[SBPort015], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVecTest,  [SBPort0,SBPort5], 2, [1,1], 2, 6>;
defm : SBWriteResPair<WriteVecTestY, [SBPort0,SBPort5], 2, [1,1], 2, 7>;
defm : SBWriteResPair<WriteVecTestZ, [SBPort0,SBPort5], 2, [1,1], 2, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVecALU,   [SBPort1],  3, [1], 1, 5>;
defm : SBWriteResPair<WriteVecALUX,  [SBPort15], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteVecALUY,  [SBPort15], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteVecALUZ,  [SBPort15], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVecIMul,  [SBPort0], 5, [1], 1, 5>;
defm : SBWriteResPair<WriteVecIMulX, [SBPort0], 5, [1], 1, 6>;
defm : SBWriteResPair<WriteVecIMulY, [SBPort0], 5, [1], 1, 7>;
defm : SBWriteResPair<WriteVecIMulZ, [SBPort0], 5, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WritePMULLD,   [SBPort0], 5, [1], 1, 6>;
defm : SBWriteResPair<WritePMULLDY,  [SBPort0], 5, [1], 1, 7>; // TODO this is probably wrong for 256/512-bit for the "generic" model
defm : SBWriteResPair<WritePMULLDZ,  [SBPort0], 5, [1], 1, 7>;  // Unsupported = 1
defm : SBWriteResPair<WriteShuffle,  [SBPort5], 1, [1], 1, 5>;
defm : SBWriteResPair<WriteShuffleX, [SBPort15], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteShuffleY, [SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteShuffleZ, [SBPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVarShuffle,  [SBPort15], 1, [1], 1, 5>;
defm : SBWriteResPair<WriteVarShuffleX, [SBPort15], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteVarShuffleY, [SBPort15], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteVarShuffleZ, [SBPort15], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteBlend,   [SBPort15], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteBlendY,  [SBPort15], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteBlendZ,  [SBPort15], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVarBlend, [SBPort15], 2, [2], 2, 6>;
defm : SBWriteResPair<WriteVarBlendY,[SBPort15], 2, [2], 2, 7>;
defm : SBWriteResPair<WriteVarBlendZ,[SBPort15], 2, [2], 2, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteMPSAD,  [SBPort0, SBPort15], 7, [1,2], 3, 6>;
defm : SBWriteResPair<WriteMPSADY, [SBPort0, SBPort15], 7, [1,2], 3, 7>;
defm : SBWriteResPair<WriteMPSADZ, [SBPort0, SBPort15], 7, [1,2], 3, 7>; // Unsupported = 1
defm : SBWriteResPair<WritePSADBW,  [SBPort0], 5, [1], 1, 5>;
defm : SBWriteResPair<WritePSADBWX, [SBPort0], 5, [1], 1, 6>;
defm : SBWriteResPair<WritePSADBWY, [SBPort0], 5, [1], 1, 7>;
defm : SBWriteResPair<WritePSADBWZ, [SBPort0], 5, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WritePHMINPOS,  [SBPort0], 5, [1], 1, 6>;

// Vector integer shifts.
defm : SBWriteResPair<WriteVecShift,     [SBPort5], 1, [1], 1, 5>;
defm : SBWriteResPair<WriteVecShiftX,    [SBPort0,SBPort15], 2, [1,1], 2, 6>;
defm : SBWriteResPair<WriteVecShiftY,    [SBPort0,SBPort15], 4, [1,1], 2, 7>;
defm : SBWriteResPair<WriteVecShiftZ,    [SBPort0,SBPort15], 4, [1,1], 2, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVecShiftImm,  [SBPort5], 1, [1], 1, 5>;
defm : SBWriteResPair<WriteVecShiftImmX, [SBPort0], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteVecShiftImmY, [SBPort0], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteVecShiftImmZ, [SBPort0], 1, [1], 1, 7>; // Unsupported = 1
defm : SBWriteResPair<WriteVarVecShift,  [SBPort0], 1, [1], 1, 6>;
defm : SBWriteResPair<WriteVarVecShiftY, [SBPort0], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteVarVecShiftZ, [SBPort0], 1, [1], 1, 7>; // Unsupported = 1

// Vector insert/extract operations.
def : WriteRes<WriteVecInsert, [SBPort5,SBPort15]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : WriteRes<WriteVecInsertLd, [SBPort23,SBPort15]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

def : WriteRes<WriteVecExtract, [SBPort0,SBPort15]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : WriteRes<WriteVecExtractSt, [SBPort4,SBPort23,SBPort15]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : SBWriteResPair<WriteFHAdd,  [SBPort1,SBPort5], 5, [1,2], 3, 6>;
defm : SBWriteResPair<WriteFHAddY, [SBPort1,SBPort5], 5, [1,2], 3, 7>;
defm : SBWriteResPair<WriteFHAddZ, [SBPort1,SBPort5], 5, [1,2], 3, 7>; // Unsupported = 1
defm : SBWriteResPair<WritePHAdd,  [SBPort15], 3, [3], 3, 5>;
defm : SBWriteResPair<WritePHAddX, [SBPort15], 3, [3], 3, 6>;
defm : SBWriteResPair<WritePHAddY, [SBPort15], 3, [3], 3, 7>;
defm : SBWriteResPair<WritePHAddZ, [SBPort15], 3, [3], 3, 7>; // Unsupported = 1

////////////////////////////////////////////////////////////////////////////////
// String instructions.
////////////////////////////////////////////////////////////////////////////////

// Packed Compare Implicit Length Strings, Return Mask
def : WriteRes<WritePCmpIStrM, [SBPort0]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def : WriteRes<WritePCmpIStrMLd, [SBPort0, SBPort23]> {
  let Latency = 17;
  let NumMicroOps = 4;
  let ResourceCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Mask
def : WriteRes<WritePCmpEStrM, [SBPort015]> {
  let Latency = 11;
  let ResourceCycles = [8];
}
def : WriteRes<WritePCmpEStrMLd, [SBPort015, SBPort23]> {
  let Latency = 11;
  let ResourceCycles = [7, 1];
}

// Packed Compare Implicit Length Strings, Return Index
def : WriteRes<WritePCmpIStrI, [SBPort0]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def : WriteRes<WritePCmpIStrILd, [SBPort0,SBPort23]> {
  let Latency = 17;
  let NumMicroOps = 4;
  let ResourceCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Index
def : WriteRes<WritePCmpEStrI, [SBPort015]> {
  let Latency = 4;
  let ResourceCycles = [8];
}
def : WriteRes<WritePCmpEStrILd, [SBPort015, SBPort23]> {
  let Latency = 4;
  let ResourceCycles = [7, 1];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK,    [SBPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSK,  [SBPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSKY, [SBPort0]> { let Latency = 2; }
def : WriteRes<WriteMMXMOVMSK,  [SBPort0]> { let Latency = 1; }

// AES Instructions.
def : WriteRes<WriteAESDecEnc, [SBPort5,SBPort015]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def : WriteRes<WriteAESDecEncLd, [SBPort5,SBPort23,SBPort015]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}

def : WriteRes<WriteAESIMC, [SBPort5]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def : WriteRes<WriteAESIMCLd, [SBPort5,SBPort23]> {
  let Latency = 18;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}

def : WriteRes<WriteAESKeyGen, [SBPort015]> {
  let Latency = 8;
  let ResourceCycles = [11];
}
def : WriteRes<WriteAESKeyGenLd, [SBPort015, SBPort23]> {
  let Latency = 8;
  let ResourceCycles = [10, 1];
}

// Carry-less multiplication instructions.
def : WriteRes<WriteCLMul, [SBPort015]> {
  let Latency = 14;
  let ResourceCycles = [18];
}
def : WriteRes<WriteCLMulLd, [SBPort015, SBPort23]> {
  let Latency = 14;
  let ResourceCycles = [17, 1];
}

// Load/store MXCSR.
// FIXME: This is probably wrong. Only STMXCSR should require Port4.
def : WriteRes<WriteLDMXCSR, [SBPort0,SBPort4,SBPort5,SBPort23]> { let Latency = 5; let NumMicroOps = 4; let ResourceCycles = [1,1,1,1]; }
def : WriteRes<WriteSTMXCSR, [SBPort0,SBPort4,SBPort5,SBPort23]> { let Latency = 5; let NumMicroOps = 4; let ResourceCycles = [1,1,1,1]; }

def : WriteRes<WriteSystem,     [SBPort015]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [SBPort015]> { let Latency = 100; }
def : WriteRes<WriteFence, [SBPort23, SBPort4]>;
def : WriteRes<WriteNop, []>;

// AVX2/FMA is not supported on that architecture, but we should define the basic
// scheduling resources anyway.
defm : SBWriteResPair<WriteFShuffle256, [SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFVarShuffle256, [SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteShuffle256, [SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteVarShuffle256, [SBPort5], 1, [1], 1, 7>;
defm : SBWriteResPair<WriteFMA,  [SBPort01],  5>;
defm : SBWriteResPair<WriteFMAX, [SBPort01],  5>;
defm : SBWriteResPair<WriteFMAY, [SBPort01],  5>;
defm : SBWriteResPair<WriteFMAZ, [SBPort01],  5>;  // Unsupported = 1

// Remaining SNB instrs.

def SBWriteResGroup1 : SchedWriteRes<[SBPort1]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup1], (instrs COMP_FST0r,
                                        COM_FST0r,
                                        UCOM_FPr,
                                        UCOM_Fr)>;

def SBWriteResGroup2 : SchedWriteRes<[SBPort5]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup2], (instrs FDECSTP, FINCSTP, FFREE, FFREEP, FNOP,
                                        LD_Frr, ST_Frr, ST_FPrr)>;
def: InstRW<[SBWriteResGroup2], (instrs LOOP, LOOPE, LOOPNE)>; // FIXME: This seems wrong compared to other Intel CPUs.
def: InstRW<[SBWriteResGroup2], (instrs RETQ)>;

def SBWriteResGroup4 : SchedWriteRes<[SBPort05]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup4], (instrs CDQ, CQO)>;

def SBWriteResGroup5 : SchedWriteRes<[SBPort15]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup5], (instrs MMX_PABSBrr,
                                        MMX_PABSDrr,
                                        MMX_PABSWrr,
                                        MMX_PADDQirr,
                                        MMX_PALIGNRrri,
                                        MMX_PSIGNBrr,
                                        MMX_PSIGNDrr,
                                        MMX_PSIGNWrr)>;

def SBWriteResGroup9 : SchedWriteRes<[SBPort05]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SBWriteResGroup9], (instregex "SET(A|BE)r")>;

def SBWriteResGroup11 : SchedWriteRes<[SBPort015]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SBWriteResGroup11], (instrs SCASB,
                                         SCASL,
                                         SCASQ,
                                         SCASW)>;

def SBWriteResGroup12 : SchedWriteRes<[SBPort0,SBPort1]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup12], (instregex "(V?)(U?)COMI(SD|SS)rr")>;

def SBWriteResGroup15 : SchedWriteRes<[SBPort0,SBPort015]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup15], (instrs CWD,
                                         FNSTSW16r)>;

def SBWriteResGroup18 : SchedWriteRes<[SBPort5,SBPort015]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup18], (instrs JCXZ, JECXZ, JRCXZ,
                                         MMX_MOVDQ2Qrr)>;

def SBWriteResGroup21 : SchedWriteRes<[SBPort1]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup21], (instrs PUSHFS64)>;

def SBWriteResGroup22 : SchedWriteRes<[SBPort0,SBPort5]> {
  let Latency = 3;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup22], (instregex "(V?)EXTRACTPSrr")>;

def SBWriteResGroup23 : SchedWriteRes<[SBPort05]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def: InstRW<[SBWriteResGroup23], (instregex "RCL(8|16|32|64)r1",
                                            "RCR(8|16|32|64)r1")>;

def SBWriteResGroup25_1 : SchedWriteRes<[SBPort23,SBPort015]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SBWriteResGroup25_1], (instrs LEAVE, LEAVE64)>;

def SBWriteResGroup26_2 : SchedWriteRes<[SBPort0,SBPort1,SBPort5]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup26_2], (instrs COM_FIPr, COM_FIr, UCOM_FIPr, UCOM_FIr)>;

def SBWriteResGroup29 : SchedWriteRes<[SBPort1,SBPort015]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup29], (instrs MOV64sr)>;

def SBWriteResGroup29_2 : SchedWriteRes<[SBPort5,SBPort015]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ResourceCycles = [1,3];
}
def: InstRW<[SBWriteResGroup29_2], (instrs PAUSE)>;

def SBWriteResGroup30 : SchedWriteRes<[SBPort0]> {
  let Latency = 5;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}

def SBWriteResGroup31 : SchedWriteRes<[SBPort23]> {
  let Latency = 5;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup31], (instregex "MOVSX(16|32|64)rm(8|16|32)",
                                            "MOVZX(16|32|64)rm(8|16)")>;

def SBWriteResGroup76 : SchedWriteRes<[SBPort05]> {
  let Latency = 5;
  let NumMicroOps = 8;
  let ResourceCycles = [8];
}
def: InstRW<[SBWriteResGroup76], (instregex "RCL(8|16|32|64)r(i|CL)",
                                            "RCR(8|16|32|64)r(i|CL)")>;

def SBWriteResGroup33 : SchedWriteRes<[SBPort4,SBPort23]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup33], (instregex "PUSH(16r|32r|64r|64i8)")>;

def SBWriteResGroup35 : SchedWriteRes<[SBPort1,SBPort5]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SBWriteResGroup35], (instrs CLI)>;

def SBWriteResGroup35_2 : SchedWriteRes<[SBPort1,SBPort4,SBPort23]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup35_2], (instrs PUSHGS64)>;
def: InstRW<[SBWriteResGroup35_2], (instregex "ISTT_FP(16|32|64)m")>;

def SBWriteResGroup36 : SchedWriteRes<[SBPort4,SBPort5,SBPort23]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup36], (instrs CALL64pcrel32)>;
def: InstRW<[SBWriteResGroup36], (instregex "CALL(16|32|64)r",
                                            "(V?)EXTRACTPSmr")>;

def SBWriteResGroup40 : SchedWriteRes<[SBPort4,SBPort23,SBPort015]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup40], (instrs STOSB, STOSL, STOSQ, STOSW)>;

def SBWriteResGroup41 : SchedWriteRes<[SBPort5,SBPort015]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ResourceCycles = [1,3];
}
def: InstRW<[SBWriteResGroup41], (instrs FNINIT)>;

def SBWriteResGroup43 : SchedWriteRes<[SBPort4,SBPort23,SBPort05]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[SBWriteResGroup43], (instregex "SET(A|BE)m")>;

def SBWriteResGroup45 : SchedWriteRes<[SBPort0,SBPort4,SBPort23,SBPort15]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SBWriteResGroup45], (instregex "(V?)PEXTR(D|Q)mr",
                                            "PUSHF(16|64)")>;

def SBWriteResGroup46 : SchedWriteRes<[SBPort4,SBPort5,SBPort01,SBPort23]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SBWriteResGroup46], (instregex "CLFLUSH")>;

def SBWriteResGroup47 : SchedWriteRes<[SBPort4,SBPort5,SBPort01,SBPort23]> {
  let Latency = 5;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SBWriteResGroup47], (instregex "FXRSTOR")>;

def SBWriteResGroup48 : SchedWriteRes<[SBPort23]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup48], (instrs MMX_MOVD64from64rm,
                                         VBROADCASTSSrm)>;
def: InstRW<[SBWriteResGroup48], (instregex "POP(16|32|64)r",
                                            "(V?)MOV64toPQIrm",
                                            "(V?)MOVDDUPrm",
                                            "(V?)MOVDI2PDIrm",
                                            "(V?)MOVQI2PQIrm",
                                            "(V?)MOVSDrm",
                                            "(V?)MOVSHDUPrm",
                                            "(V?)MOVSLDUPrm",
                                            "(V?)MOVSSrm")>;

def SBWriteResGroup49 : SchedWriteRes<[SBPort5,SBPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup49], (instrs MOV16sm)>;

def SBWriteResGroup51 : SchedWriteRes<[SBPort23,SBPort15]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup51], (instrs MMX_PABSBrm,
                                         MMX_PABSDrm,
                                         MMX_PABSWrm,
                                         MMX_PALIGNRrmi,
                                         MMX_PSIGNBrm,
                                         MMX_PSIGNDrm,
                                         MMX_PSIGNWrm)>;

def SBWriteResGroup52 : SchedWriteRes<[SBPort23,SBPort015]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup52], (instrs LODSL, LODSQ)>;

def SBWriteResGroup53 : SchedWriteRes<[SBPort4,SBPort23]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SBWriteResGroup53], (instregex "ST_F(32|64)m",
                                            "ST_FP(32|64|80)m")>;

def SBWriteResGroup54 : SchedWriteRes<[SBPort23]> {
  let Latency = 7;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SBWriteResGroup54], (instrs VBROADCASTSDYrm,
                                         VBROADCASTSSYrm,
                                         VMOVDDUPYrm,
                                         VMOVSHDUPYrm,
                                         VMOVSLDUPYrm)>;

def SBWriteResGroup58 : SchedWriteRes<[SBPort23,SBPort05]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup58], (instrs VINSERTF128rm)>;

def SBWriteResGroup59 : SchedWriteRes<[SBPort23,SBPort15]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup59], (instrs MMX_PADDQirm)>;

def SBWriteResGroup62 : SchedWriteRes<[SBPort5,SBPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SBWriteResGroup62], (instrs VERRm, VERWm)>;

def SBWriteResGroup63 : SchedWriteRes<[SBPort23,SBPort015]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SBWriteResGroup63], (instrs LODSB, LODSW)>;

def SBWriteResGroup64 : SchedWriteRes<[SBPort5,SBPort01,SBPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup64], (instrs FARJMP64)>;

def SBWriteResGroup66 : SchedWriteRes<[SBPort0,SBPort4,SBPort23]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[SBWriteResGroup66], (instrs FNSTSWm)>;

def SBWriteResGroup67 : SchedWriteRes<[SBPort1,SBPort5,SBPort015]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,2,1];
}
def: InstRW<[SBWriteResGroup67], (instregex "SLDT(16|32|64)r",
                                            "STR(16|32|64)r")>;

def SBWriteResGroup68 : SchedWriteRes<[SBPort4,SBPort5,SBPort23]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[SBWriteResGroup68], (instrs FNSTCW16m)>;
def: InstRW<[SBWriteResGroup68], (instregex "CALL(16|32|64)m")>;

def SBWriteResGroup69 : SchedWriteRes<[SBPort4,SBPort23,SBPort05]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,2,1];
}
def: InstRW<[SBWriteResGroup69], (instregex "SAR(8|16|32|64)m(1|i)",
                                            "SHL(8|16|32|64)m(1|i)",
                                            "SHR(8|16|32|64)m(1|i)")>;

def SBWriteResGroup77 : SchedWriteRes<[SBPort0,SBPort1,SBPort23]> {
  let Latency = 8;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup77], (instregex "(V?)(U?)COMI(SD|SS)rm")>;

def SBWriteResGroup81 : SchedWriteRes<[SBPort4, SBPort23, SBPort015]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ResourceCycles = [1, 2, 1];
}
def: InstRW<[SBWriteResGroup81], (instregex "CMPXCHG(8|16)B")>;

def SBWriteResGroup83 : SchedWriteRes<[SBPort23,SBPort015]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [2,3];
}
def: InstRW<[SBWriteResGroup83], (instrs CMPSB,
                                         CMPSL,
                                         CMPSQ,
                                         CMPSW)>;

def SBWriteResGroup84 : SchedWriteRes<[SBPort4,SBPort5,SBPort23]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,2];
}
def: InstRW<[SBWriteResGroup84], (instrs FLDCW16m)>;

def SBWriteResGroup85 : SchedWriteRes<[SBPort4,SBPort23,SBPort05]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,2];
}
def: InstRW<[SBWriteResGroup85], (instregex "ROL(8|16|32|64)m(1|i)",
                                            "ROR(8|16|32|64)m(1|i)")>;

def SBWriteResGroup86 : SchedWriteRes<[SBPort4,SBPort23,SBPort015]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,2];
}
def: InstRW<[SBWriteResGroup86], (instrs MOVSB, MOVSL, MOVSQ, MOVSW)>;
def: InstRW<[SBWriteResGroup86], (instregex "XADD(8|16|32|64)rm")>;

def SBWriteResGroup87 : SchedWriteRes<[SBPort4,SBPort5,SBPort01,SBPort23]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[SBWriteResGroup87], (instrs FARCALL64)>;

def SBWriteResGroup93 : SchedWriteRes<[SBPort0,SBPort1,SBPort23]> {
  let Latency = 9;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup93], (instregex "CVT(T?)(SD|SS)2SI(64)?rm")>;

def SBWriteResGroup95 : SchedWriteRes<[SBPort5,SBPort01,SBPort23]> {
  let Latency = 9;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup95], (instregex "LD_F(32|64|80)m")>;

def SBWriteResGroup97 : SchedWriteRes<[SBPort1,SBPort4,SBPort23]> {
  let Latency = 9;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[SBWriteResGroup97], (instregex "IST_F(16|32)m",
                                            "IST_FP(16|32|64)m")>;

def SBWriteResGroup97_2 : SchedWriteRes<[SBPort4,SBPort23,SBPort05]> {
  let Latency = 9;
  let NumMicroOps = 6;
  let ResourceCycles = [1,2,3];
}
def: InstRW<[SBWriteResGroup97_2], (instregex "ROL(8|16|32|64)mCL",
                                              "ROR(8|16|32|64)mCL",
                                              "SAR(8|16|32|64)mCL",
                                              "SHL(8|16|32|64)mCL",
                                              "SHR(8|16|32|64)mCL")>;

def SBWriteResGroup98 : SchedWriteRes<[SBPort4,SBPort23,SBPort015]> {
  let Latency = 9;
  let NumMicroOps = 6;
  let ResourceCycles = [1,2,3];
}
def: SchedAlias<WriteADCRMW, SBWriteResGroup98>;

def SBWriteResGroup99 : SchedWriteRes<[SBPort4,SBPort23,SBPort05,SBPort015]> {
  let Latency = 9;
  let NumMicroOps = 6;
  let ResourceCycles = [1,2,2,1];
}
def: InstRW<[SBWriteResGroup99, ReadAfterLd], (instrs ADC8mr, ADC16mr, ADC32mr, ADC64mr,
                                                      SBB8mr, SBB16mr, SBB32mr, SBB64mr)>;

def SBWriteResGroup100 : SchedWriteRes<[SBPort4,SBPort5,SBPort23,SBPort05,SBPort015]> {
  let Latency = 9;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,2,1,1];
}
def : SchedAlias<WriteBitTestRegLd, SBWriteResGroup100>; // TODO - this is incorrect - no RMW

def SBWriteResGroup101 : SchedWriteRes<[SBPort1,SBPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup101], (instregex "(ADD|SUB|SUBR)_F(32|64)m",
                                             "ILD_F(16|32|64)m")>;

def SBWriteResGroup104 : SchedWriteRes<[SBPort0,SBPort23]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup104], (instregex "(V?)PCMPGTQrm")>;

def SBWriteResGroup106 : SchedWriteRes<[SBPort1,SBPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SBWriteResGroup106], (instregex "FICOM(P?)(16|32)m")>;

def SBWriteResGroup108 : SchedWriteRes<[SBPort05,SBPort23]> {
  let Latency = 11;
  let NumMicroOps = 11;
  let ResourceCycles = [7,4];
}
def: InstRW<[SBWriteResGroup108], (instregex "RCL(8|16|32|64)m",
                                             "RCR(8|16|32|64)m")>;

def SBWriteResGroup111 : SchedWriteRes<[SBPort0,SBPort23]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup111], (instregex "MUL_F(32|64)m")>;

def SBWriteResGroup114 : SchedWriteRes<[SBPort1,SBPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SBWriteResGroup114], (instregex "(ADD|SUB|SUBR)_FI(16|32)m")>;

def SBWriteResGroup119 : SchedWriteRes<[SBPort0,SBPort1,SBPort23]> {
  let Latency = 15;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup119], (instregex "MUL_FI(16|32)m")>;

def SBWriteResGroup130 : SchedWriteRes<[SBPort0,SBPort23]> {
  let Latency = 31;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SBWriteResGroup130], (instregex "DIV(R?)_F(32|64)m")>;

def SBWriteResGroup131 : SchedWriteRes<[SBPort0,SBPort1,SBPort23]> {
  let Latency = 34;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SBWriteResGroup131], (instregex "DIV(R?)_FI(16|32)m")>;

def SBWriteResGroupVzeroall : SchedWriteRes<[SBPort5]> {
  let Latency = 9;
  let NumMicroOps = 20;
  let ResourceCycles = [2];
}
def: InstRW<[SBWriteResGroupVzeroall], (instrs VZEROALL)>;

def SBWriteResGroupVzeroupper : SchedWriteRes<[]> {
  let Latency = 1;
  let NumMicroOps = 4;
  let ResourceCycles = [];
}
def: InstRW<[SBWriteResGroupVzeroupper], (instrs VZEROUPPER)>;

def: InstRW<[WriteZero], (instrs CLC)>;

// Intruction variants handled by the renamer. These might not need execution
// ports in certain conditions.
// See Agner's Fog "The microarchitecture of Intel, AMD and VIA CPUs",
// section "Sandy Bridge and Ivy Bridge Pipeline" > "Register allocation and
// renaming".
// These can be investigated with llvm-exegesis, e.g.
// echo 'pxor %mm0, %mm0' | /tmp/llvm-exegesis -mode=uops -snippets-file=-
// echo 'vxorpd %xmm0, %xmm0, %xmm1' | /tmp/llvm-exegesis -mode=uops -snippets-file=-

def SBWriteZeroLatency : SchedWriteRes<[]> {
  let Latency = 0;
}

def SBWriteZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SBWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteALU]>
]>;
def : InstRW<[SBWriteZeroIdiom], (instrs SUB32rr, SUB64rr,
                                         XOR32rr, XOR64rr)>;

def SBWriteFZeroIdiom : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SBWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteFLogic]>
]>;
def : InstRW<[SBWriteFZeroIdiom], (instrs XORPSrr, VXORPSrr, XORPDrr,
                                          VXORPDrr)>;

def SBWriteVZeroIdiomLogicX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SBWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecLogicX]>
]>;
def : InstRW<[SBWriteVZeroIdiomLogicX], (instrs PXORrr, VPXORrr)>;

def SBWriteVZeroIdiomALUX : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SBWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [WriteVecALUX]>
]>;
def : InstRW<[SBWriteVZeroIdiomALUX], (instrs PSUBBrr, VPSUBBrr,
                                              PSUBDrr, VPSUBDrr,
                                              PSUBQrr, VPSUBQrr,
                                              PSUBWrr, VPSUBWrr,
                                              PCMPGTBrr, VPCMPGTBrr,
                                              PCMPGTDrr, VPCMPGTDrr,
                                              PCMPGTWrr, VPCMPGTWrr)>;

def SBWriteVZeroIdiomPCMPGTQ : SchedWriteVariant<[
    SchedVar<MCSchedPredicate<ZeroIdiomPredicate>, [SBWriteZeroLatency]>,
    SchedVar<NoSchedPred,                          [SBWriteResGroup30]>
]>;
def : InstRW<[SBWriteVZeroIdiomPCMPGTQ], (instrs PCMPGTQrr, VPCMPGTQrr)>;

} // SchedModel
